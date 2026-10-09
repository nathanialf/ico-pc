/* rd_mirror_test.c: the mirror mode.
 *
 * Without a device:
 *   flag      rd_set_mirror and RdSettings.mirror both turn rd_mirror_active on
 * On a Vulkan device (exit 77 without one), the presenter's output
 * (1024 x 768: the 4:3 box fills it, so the horizontal scale is exactly 2
 * and every sample position is a dyadic fraction):
 *   present   a WORLD-only frame (textured, scaled and untextured sprites,
 *             a triangle) drawn into DISPLAY: with the mirror on the output
 *             is the exact horizontal flip of the output with it off, in the
 *             Original preset and in Enhanced at 1x
 *   ui        UI sprites (1:1 nearest textured, 2x scaled, untextured,
 *             under a scissor narrower than the target), an axis-aligned
 *             UI quad of two textured triangles and UI points drawn into
 *             DISPLAY land on the same output pixels with the mirror on and
 *             off (the replay's flip and the present's cancel), while a
 *             WORLD sprite in the same frame is flipped
 *   scene     the same UI drawn into SCENE: SCENE with the mirror on is the
 *             exact flip of SCENE with it off
 *   reduce    that UI plus glyph-like stems (one to three pixels
 *             wide, quarter-pixel edges, a 1:1 textured run) in SCENE,
 *             reduced to DISPLAY by rd_post(RD_POST_REDUCTION) with a tint:
 *             DISPLAY with the mirror on is the exact flip of DISPLAY with
 *             it off (the mirrored reduction samples at u = x + 0.25, the
 *             mirror image of the GS's x + 0.75), so the presented UI is the
 *             same pixels with the mirror on and off; Original, and Enhanced
 *             at 1x and 2x
 *   interp    Enhanced with interpolation: rd_present(0.5) of a keyed WORLD
 *             sprite moving between two frames is flipped exactly too
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define OUT_W 1024
#define OUT_H 768
/* DISPLAY for a 512 x 512 scene: 512 x 256, XYOFFSET (2048 - 256, 2048 - 128) */
#define DW 512
#define DH 256

static const char kObj;
static RdTex s_tex;
static uint8_t s_texel[16 * 16 * 4];
static uint8_t s_off[OUT_W * OUT_H * 4], s_on[OUT_W * OUT_H * 4];

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static RdScreenVtx vtx(int32_t x, int32_t y, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = x;
    v.y = y;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

static void opaque2D(void)
{
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
}

/* a sprite (x0, y0)..(x1, y1) in 12.4 relative to a w x h target's top-left,
 * UVs in 12.4 texels (uvFixed) */
static void sprite(int w, int h, RdSpace sp, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   const uint8_t c[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1, RdKey key)
{
    const int32_t ox = (2048 - w / 2) * 16, oy = (2048 - h / 2) * 16;
    RdScreenVtx v[2] = {vtx(ox + x0, oy + y0, c, (float)u0, (float)v0),
                        vtx(ox + x1, oy + y1, c, (float)u1, (float)v1)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, sp, 1, key);
}

static void textured(void)
{
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
}

static const uint8_t kGrey[4] = {0x80, 0x80, 0x80, 0x80}, kRed[4] = {220, 30, 10, 0x80},
                     kBlue[4] = {10, 40, 230, 0x80}, kGreen[4] = {20, 200, 60, 0x80},
                     kBlack[4] = {0, 0, 0, 0x80};

/* WORLD content: asymmetric on purpose */
static void worldContent(int w, int h, int dx)
{
    rd_texture_off();
    sprite(w, h, RD_SPACE_WORLD, (20 + dx) * 16, 30 * 16, (90 + dx) * 16, 60 * 16, kRed, 0, 0, 0, 0,
           RD_KEY(&kObj, 1, 0));
    sprite(w, h, RD_SPACE_WORLD, 300 * 16 + 4, 100 * 16, 307 * 16 + 12, 180 * 16, kBlue, 0, 0, 0, 0,
           0);
    const int32_t ox = (2048 - w / 2) * 16, oy = (2048 - h / 2) * 16;
    RdScreenVtx t[3] = {vtx(ox + 400 * 16, oy + 20 * 16, kGreen, 0, 0),
                        vtx(ox + 500 * 16, oy + 40 * 16, kGreen, 0, 0),
                        vtx(ox + 420 * 16, oy + 120 * 16, kGreen, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_WORLD, 1, 0);
    textured();
    sprite(w, h, RD_SPACE_WORLD, 120 * 16, 150 * 16, 136 * 16, 166 * 16, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8, 0);
    sprite(w, h, RD_SPACE_WORLD, 150 * 16, 150 * 16, 182 * 16, 182 * 16, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8, 0);
    rd_texture_off();
}

/* UI content: the kinds the layout, the subtitles and the fonts draw */
static void uiContent(int w, int h)
{
    const int32_t ox = (2048 - w / 2) * 16, oy = (2048 - h / 2) * 16;
    textured();
    /* 1:1 with the +8 nudge, and 2x */
    sprite(w, h, RD_SPACE_UI, 40 * 16, 200 * 16, 56 * 16, 216 * 16, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8, 0);
    sprite(w, h, RD_SPACE_UI, 70 * 16, 190 * 16, 102 * 16, 222 * 16, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8, 0);
    /* a quad of two textured triangles, 1:1 */
    RdScreenVtx q[4] = {vtx(ox + 200 * 16, oy + 200 * 16, kGrey, 8, 8),
                        vtx(ox + 216 * 16, oy + 200 * 16, kGrey, 16 * 16 + 8, 8),
                        vtx(ox + 200 * 16, oy + 216 * 16, kGrey, 8, 16 * 16 + 8),
                        vtx(ox + 216 * 16, oy + 216 * 16, kGrey, 16 * 16 + 8, 16 * 16 + 8)};
    rd_screen_prims(RD_PRIM_TRIANGLE_STRIP, q, 4, RD_SPACE_UI, 1, 0);
    rd_texture_off();
    /* untextured, odd edges */
    sprite(w, h, RD_SPACE_UI, 250 * 16 + 4, 205 * 16, 263 * 16 + 9, 230 * 16, kGreen, 0, 0, 0, 0,
           0);
    /* points */
    RdScreenVtx p[3] = {vtx(ox + 330 * 16, oy + 210 * 16, kRed, 0, 0),
                        vtx(ox + 333 * 16 + 8, oy + 212 * 16, kBlue, 0, 0),
                        vtx(ox + 340 * 16 + 3, oy + 214 * 16, kGreen, 0, 0)};
    rd_screen_prims(RD_PRIM_POINTS, p, 3, RD_SPACE_UI, 1, 0);
    /* under a scissor that ends at x 389: half of the sprite is clipped */
    rd_scissor(0, 0, 389, h - 1);
    sprite(w, h, RD_SPACE_UI, 370 * 16, 200 * 16, 410 * 16, 220 * 16, kBlue, 0, 0, 0, 0, 0);
    rd_scissor(0, 0, w - 1, h - 1);
}

/* one frame into DISPLAY (or SCENE): world and/or UI */
static void frameInto(RdTargetId target, int world, int ui, int dx)
{
    const int w = target == RD_TARGET_DISPLAY ? DW : 512,
              h = target == RD_TARGET_DISPLAY ? DH : 512;
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(target), (RdTarget){0}, (uint32_t)w, (uint32_t)h, 0);
    rd_clear_target(rd_target(target), kBlack, 0, 0);
    opaque2D();
    if (world) {
        worldContent(w, h, dx);
    }
    rd_select_list(11);
    rd_set_target(rd_target(target), (RdTarget){0}, (uint32_t)w, (uint32_t)h, 0);
    opaque2D();
    if (ui) {
        uiContent(w, h);
    }
    rd_end_frame(0);
}

static int readOut(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    if (!rd__read_present(dst, OUT_W * OUT_H * 4, &w, &h) || w != OUT_W || h != OUT_H) {
        CHECK(0, "present readback (%u x %u)", w, h);
        return 0;
    }
    return 1;
}

static int readTarget(RdTargetId id, uint8_t *dst, size_t size, uint32_t *w, uint32_t *h)
{
    if (!rd__read_target(rd_target(id), dst, size, w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return 0;
    }
    return 1;
}

/* pixels of a where a(x) != b(W - 1 - x) (flip) or a(x) != b(x) */
static int compare(const uint8_t *a, const uint8_t *b, int w, int h, int flip, int *firstX,
                   int *firstY)
{
    int bad = 0;
    *firstX = *firstY = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int bx = flip ? w - 1 - x : x;
            if (memcmp(a + ((size_t)y * w + x) * 4, b + ((size_t)y * w + bx) * 4, 4) != 0) {
                if (bad++ == 0) {
                    *firstX = x;
                    *firstY = y;
                }
            }
        }
    }
    return bad;
}

static void settings(RdPreset preset, int interpolate)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = preset;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.sceneScale = preset == RD_PRESET_ENHANCED ? 1.0f : 0.0f;
    s.interpolate = (uint8_t)interpolate;
    rd_set_settings(&s);
}

static void testFlag(void)
{
    rd_set_mirror(0);
    CHECK(!rd_mirror_active(), "off by default");
    rd_set_mirror(1);
    CHECK(rd_mirror_active(), "rd_set_mirror");
    rd_set_mirror(0);
    RdSettings s = *rd_get_settings();
    s.mirror = 1;
    rd_set_settings(&s);
    rd_begin_frame();
    rd_end_frame(0);
    CHECK(rd_mirror_active(), "RdSettings.mirror");
    s.mirror = 0;
    rd_set_settings(&s);
    rd_begin_frame();
    rd_end_frame(0);
    CHECK(!rd_mirror_active(), "both off");
}

static void testPresent(RdPreset preset, const char *name)
{
    int fx, fy;
    settings(preset, 0);
    rd_set_mirror(0);
    frameInto(RD_TARGET_DISPLAY, 1, 0, 0);
    if (!readOut(s_off)) {
        return;
    }
    const int asym = compare(s_off, s_off, OUT_W, OUT_H, 1, &fx, &fy);
    CHECK(asym > 1000, "%s: the frame is not symmetric (%d)", name, asym);
    rd_set_mirror(1);
    frameInto(RD_TARGET_DISPLAY, 1, 0, 0);
    if (!readOut(s_on)) {
        return;
    }
    const int bad = compare(s_on, s_off, OUT_W, OUT_H, 1, &fx, &fy);
    CHECK(bad == 0, "%s: mirror on is the flip of off: %d pixels differ (first %d, %d)", name, bad,
          fx, fy);
    rd_set_mirror(0);
    printf("  present %s: flipped exactly (%d asymmetric pixels)\n", name, asym);
}

static void testUi(void)
{
    int fx, fy;
    settings(RD_PRESET_ORIGINAL, 0);
    rd_set_mirror(0);
    frameInto(RD_TARGET_DISPLAY, 0, 1, 0);
    if (!readOut(s_off)) {
        return;
    }
    rd_set_mirror(1);
    frameInto(RD_TARGET_DISPLAY, 0, 1, 0);
    if (!readOut(s_on)) {
        return;
    }
    int bad = compare(s_on, s_off, OUT_W, OUT_H, 0, &fx, &fy);
    CHECK(bad == 0, "UI: the same output pixels with the mirror on: %d differ (first %d, %d)", bad,
          fx, fy);
    /* the UI is there at all */
    int lit = 0;
    for (size_t i = 0; i < (size_t)OUT_W * OUT_H; i++) {
        lit += s_off[i * 4] | s_off[i * 4 + 1] | s_off[i * 4 + 2] ? 1 : 0;
    }
    CHECK(lit > 4000, "UI drawn (%d lit pixels)", lit);
    /* world and UI together: the UI stays, the world flips */
    rd_set_mirror(0);
    frameInto(RD_TARGET_DISPLAY, 1, 1, 0);
    readOut(s_off);
    rd_set_mirror(1);
    frameInto(RD_TARGET_DISPLAY, 1, 1, 0);
    readOut(s_on);
    int uiSame = 0, worldFlip = 0;
    for (int y = 0; y < OUT_H; y++) {
        for (int x = 0; x < OUT_W; x++) {
            const uint8_t *a = s_on + ((size_t)y * OUT_W + x) * 4;
            /* an output row is a third of a DISPLAY line: the UI band is
               lines 190..230, the world's 20..182 */
            if (y >= 3 * 192 && y < 3 * 228) {
                uiSame += memcmp(a, s_off + ((size_t)y * OUT_W + x) * 4, 4) != 0;
            } else if (y < 3 * 186) {
                worldFlip += memcmp(a, s_off + ((size_t)y * OUT_W + (OUT_W - 1 - x)) * 4, 4) != 0;
            }
        }
    }
    CHECK(uiSame == 0, "UI band unchanged with world drawn too: %d", uiSame);
    CHECK(worldFlip == 0, "world band flipped: %d", worldFlip);
    rd_set_mirror(0);
    printf("  ui: %d lit pixels on the same output pixels\n", lit);
}

static void testScene(void)
{
    static uint8_t a[512 * 512 * 4], b[512 * 512 * 4];
    uint32_t w, h;
    int fx, fy;
    settings(RD_PRESET_ORIGINAL, 0);
    rd_set_mirror(0);
    frameInto(RD_TARGET_SCENE, 0, 1, 0);
    if (!readTarget(RD_TARGET_SCENE, a, sizeof(a), &w, &h)) {
        return;
    }
    rd_set_mirror(1);
    frameInto(RD_TARGET_SCENE, 0, 1, 0);
    if (!readTarget(RD_TARGET_SCENE, b, sizeof(b), &w, &h)) {
        return;
    }
    const int bad = compare(b, a, (int)w, (int)h, 1, &fx, &fy);
    CHECK(bad == 0, "SCENE: UI flipped exactly: %d pixels differ (first %d, %d)", bad, fx, fy);
    /* a WORK target is never flipped */
    rd_begin_frame();
    rd_select_list(11);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    rd_clear_target(rd_target(RD_TARGET_WORK1), kBlack, 0, 0);
    opaque2D();
    rd_texture_off();
    sprite(256, 256, RD_SPACE_UI, 10 * 16, 10 * 16, 20 * 16, 20 * 16, kRed, 0, 0, 0, 0, 0);
    rd_end_frame(0);
    if (readTarget(RD_TARGET_WORK1, a, sizeof(a), &w, &h)) {
        CHECK(a[((size_t)15 * w + 15) * 4] == kRed[0], "WORK1: the UI sprite where it was drawn");
    }
    rd_set_mirror(0);
    printf("  scene: UI flipped about the target's centre\n");
}

/* glyph-like UI: stems one to three pixels wide with whole and quarter
 * pixel edges, and a 1:1 textured run (a font's texels) */
static void glyphContent(int w, int h)
{
    rd_texture_off();
    for (int i = 0; i < 12; i++) {
        const int32_t x0 = (60 + 23 * i) * 16 + (i & 3) * 4;
        const int32_t x1 = x0 + 16 * (1 + i % 3) + ((i >> 2) & 1) * 8;
        sprite(w, h, RD_SPACE_UI, x0, 300 * 16, x1, 340 * 16, i & 1 ? kRed : kGreen, 0, 0, 0, 0, 0);
    }
    textured();
    for (int i = 0; i < 6; i++) {
        sprite(w, h, RD_SPACE_UI, (70 + 37 * i) * 16 + 4 * i, 360 * 16, (86 + 37 * i) * 16 + 4 * i,
               376 * 16, kGrey, 8, 8, 16 * 16 + 8, 16 * 16 + 8, 0);
    }
    rd_texture_off();
}

/* UI into SCENE, then the reduction into DISPLAY */
static void reducedFrame(void)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, 512, 512, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBlack, 0, 0);
    rd_select_list(11);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, 512, 512, 0);
    opaque2D();
    uiContent(512, 512);
    glyphContent(512, 512);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = 120;
    pp.rgba[2] = 100;
    pp.rgba[3] = 0x80;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

static void testReduce(RdPreset preset, float scale, const char *name)
{
    static uint8_t a[1024 * 512 * 4], b[1024 * 512 * 4];
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    int fx, fy;
    settings(preset, 0);
    if (scale > 1.0f) {
        RdSettings s = *rd_get_settings();
        s.sceneScale = scale;
        rd_set_settings(&s);
    }
    rd_set_mirror(0);
    reducedFrame();
    if (!readTarget(RD_TARGET_DISPLAY, a, sizeof(a), &w, &h) || !readOut(s_off)) {
        return;
    }
    rd_set_mirror(1);
    reducedFrame();
    if (!readTarget(RD_TARGET_DISPLAY, b, sizeof(b), &w2, &h2) || !readOut(s_on)) {
        rd_set_mirror(0);
        return;
    }
    rd_set_mirror(0);
    CHECK(w == w2 && h == h2 && w == (uint32_t)(512 * scale), "%s: DISPLAY %ux%u", name, w, h);
    const int asym = compare(a, a, (int)w, (int)h, 1, &fx, &fy);
    CHECK(asym > 1000, "%s: the reduced UI is not symmetric (%d)", name, asym);
    int lit = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        lit += a[i * 4] | a[i * 4 + 1] | a[i * 4 + 2] ? 1 : 0;
    }
    CHECK(lit > 2000, "%s: UI reduced into DISPLAY (%d lit pixels)", name, lit);
    const int bad = compare(b, a, (int)w, (int)h, 1, &fx, &fy);
    CHECK(bad == 0, "%s: mirrored DISPLAY is the flip of DISPLAY: %d pixels differ (first %d, %d)",
          name, bad, fx, fy);
    const int same = compare(s_on, s_off, OUT_W, OUT_H, 0, &fx, &fy);
    CHECK(same == 0,
          "%s: the presented UI is the same with the mirror on: %d differ (first %d, %d)", name,
          same, fx, fy);
    printf("  reduce %s: DISPLAY %ux%u, %d lit, %d asymmetric pixels; %d pixels differ from the "
           "flip, %d presented pixels from the mirror off\n",
           name, w, h, lit, asym, bad, same);
}

static void testInterp(void)
{
    int fx, fy;
    settings(RD_PRESET_ENHANCED, 1);
    rd_set_mirror(0);
    frameInto(RD_TARGET_DISPLAY, 1, 0, 0);
    frameInto(RD_TARGET_DISPLAY, 1, 0, 64);
    CHECK(rd_interpolation_active(), "interpolation on");
    CHECK(rd_present(0.5f), "rd_present");
    if (!readOut(s_off)) {
        return;
    }
    rd_set_mirror(1);
    CHECK(rd_present(0.5f), "rd_present, mirror on");
    if (!readOut(s_on)) {
        return;
    }
    const int bad = compare(s_on, s_off, OUT_W, OUT_H, 1, &fx, &fy);
    CHECK(bad == 0, "interpolated present flipped: %d pixels differ (first %d, %d)", bad, fx, fy);
    /* the half-way sprite: red at DISPLAY x 52..121, output 104..243, flipped */
    const uint8_t *p = s_on + ((size_t)(45 * 3) * OUT_W + (OUT_W - 1 - 150)) * 4;
    CHECK(p[0] == kRed[0] && p[2] == kRed[2], "the blended sprite, mirrored (%u %u %u)", p[0], p[1],
          p[2]);
    rd_set_mirror(0);
    settings(RD_PRESET_ORIGINAL, 0);
    frameInto(RD_TARGET_DISPLAY, 0, 0, 0);
    printf("  interp: rd_present(0.5) flipped exactly\n");
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(512, 512)) {
        printf("rd_mirror_test: no context\n");
        return 1;
    }
    rd__set_not_implemented_fatal(false);
    rd_set_mirror(1);
    CHECK(rd_mirror_active(), "record-only: rd_set_mirror");
    rd_set_mirror(0);
    rd_shutdown();

    for (int i = 0; i < 16 * 16; i++) {
        uint32_t v = hash((uint32_t)i * 2654435761u + 7u);
        s_texel[i * 4 + 0] = (uint8_t)v;
        s_texel[i * 4 + 1] = (uint8_t)(v >> 8);
        s_texel[i * 4 + 2] = (uint8_t)(v >> 16);
        s_texel[i * 4 + 3] = 0x80;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    if (!rd_init(512, 512, &s, NULL)) {
        printf("rd_mirror_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    rd__set_not_implemented_fatal(false);
    s_tex = rd_create_texture(16, 16, s_texel, RD_TEXA_80_80, "mirror test");
    testFlag();
    testPresent(RD_PRESET_ORIGINAL, "Original");
    testPresent(RD_PRESET_ENHANCED, "Enhanced 1x");
    testUi();
    testScene();
    testReduce(RD_PRESET_ORIGINAL, 1.0f, "Original");
    testReduce(RD_PRESET_ENHANCED, 1.0f, "Enhanced 1x");
    testReduce(RD_PRESET_ENHANCED, 2.0f, "Enhanced 2x");
    testInterp();
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%u validation errors", verr);
    rd_shutdown();
    if (failures) {
        printf("rd_mirror_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mirror_test: ok\n");
    return 0;
}
