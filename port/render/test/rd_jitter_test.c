/* rd_jitter_test.c: the scene's sub-pixel jitter (port/render/rd_jitter.h,
 * docs/port/RENDER.md "Jitter").
 *
 * Without a device:
 *   halton    rd__jitter_halton's first 8 terms of each base, exact: base 2
 *             1/2 1/4 3/4 1/8 5/8 3/8 7/8 1/16, base 3 1/3 2/3 1/9 4/9 7/9
 *             2/9 5/9 8/9, each less 0.5 (the double rounded to float once)
 *   parse     rd__frame_jitter_parse: "x,y" with each -0.5..0.5 taken,
 *             anything else refused; set, get and clear
 * On a Vulkan device (exit 77 without one, after the CPU cases):
 *   shift     SCENE at the Original preset (1x) and at Enhanced 2x: 24 rows
 *             of untextured quads whose left edges step 1/16 GS pixel a row
 *             (GS 100 + f / 16, f = 0..23) and 24 columns whose top edges do
 *             the same (GS 300 + f / 16).  C(f), the first lit texel of row
 *             f (the column's first lit row), without the jitter; with a
 *             jitter (jx, jy) in texels at scale s every row's C is C(f + k)
 *             of the run without, k = 16 jx / s (the columns' with
 *             16 jy / s): the picture moved by the jitter exactly, in
 *             texels, after the GS's 1/16 snap.  Jitters (0.25, -0.25) and
 *             (-0.25, 0.25); C(f + 16) = C(f) + s (the rows resolve the
 *             sixteenths).  A jitter of 0, 0: SCENE byte for byte as without
 *   whole     the Original preset: a Gouraud triangle (the screen planes:
 *             colour evaluated at the pixel's offset from its first corner,
 *             common.hlsli gs_plane_offset), a nearest and a bilinear
 *             textured sprite into SCENE, a sprite into WORK1, the
 *             reduction into DISPLAY, the present at 1024 x 768 (the box's
 *             horizontal scale exactly 2).  A whole-texel jitter (1, 1):
 *             SCENE is the picture without it moved one texel right and
 *             down, every texel (the planes take the jitter out: a pixel is
 *             evaluated at its jittered GS position); WORK1 (a work buffer,
 *             not jittered) byte for byte the same.  (1, 0): DISPLAY moved
 *             one texel right (the reduction itself not jittered: it would
 *             be two), the present two pixels right (the present not
 *             jittered); the jitter's frame-group count > 0
 * Any validation error fails the test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_jitter.h"
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

#define GS 512 /* the GS frame: 512 x 512 */
#define OUT_W 1024
#define OUT_H 768
#define ROWS 24

static const uint8_t kBlack[4] = {0, 0, 0, 0x80}, kWhite[4] = {0x80, 0x80, 0x80, 0x80},
                     kRed[4] = {0xFF, 0x10, 0x00, 0x80}, kGreen[4] = {0x00, 0xF0, 0x20, 0x80},
                     kBlue[4] = {0x08, 0x30, 0xFF, 0x80}, kGrey[4] = {0x80, 0x80, 0x80, 0x80};

static RdTex s_tex;
static uint8_t s_texel[16 * 16 * 4];
static uint8_t s_a[1024 * 1024 * 4], s_b[1024 * 1024 * 4];
static uint8_t s_outA[OUT_W * OUT_H * 4], s_outB[OUT_W * OUT_H * 4];

/* ------------------------------------------------------------- CPU cases */

static void testHalton(void)
{
    static const uint32_t den2[8] = {2, 4, 4, 8, 8, 8, 8, 16};
    static const uint32_t num2[8] = {1, 1, 3, 1, 5, 3, 7, 1};
    static const uint32_t den3[8] = {3, 3, 9, 9, 9, 9, 9, 9};
    static const uint32_t num3[8] = {1, 2, 1, 4, 7, 2, 5, 8};
    for (uint32_t i = 0; i < 8; i++) {
        float j[2];
        rd__jitter_halton(i, j);
        const float x = (float)((double)num2[i] / (double)den2[i] - 0.5);
        const float y = (float)((double)num3[i] / (double)den3[i] - 0.5);
        CHECK(j[0] == x && j[1] == y, "halton %u: (%.9g, %.9g), want (%.9g, %.9g)", i, (double)j[0],
              (double)j[1], (double)x, (double)y);
    }
    /* every term inside the half-texel square */
    for (uint32_t i = 0; i < 1000; i++) {
        float j[2];
        rd__jitter_halton(i * 7919u, j);
        CHECK(j[0] >= -0.5f && j[0] < 0.5f && j[1] >= -0.5f && j[1] < 0.5f,
              "halton %u: (%g, %g) outside -0.5..0.5", i * 7919u, (double)j[0], (double)j[1]);
    }
    printf("  halton: the first 8 terms of base 2 and 3 exact\n");
}

static void testParse(void)
{
    static const struct {
        const char *s;
        int ok;
        float x, y;
    } kCases[] = {
        {"0.25,0.25", 1, 0.25f, 0.25f},
        {"0,0", 1, 0.0f, 0.0f},
        {"-0.5, 0.5", 1, -0.5f, 0.5f},
        {" 0.125 , -0.375 ", 1, 0.125f, -0.375f},
        {"0.75,0", 0, 0, 0},
        {"0.25", 0, 0, 0},
        {"a,b", 0, 0, 0},
        {"0.1,0.2,0.3", 0, 0, 0},
        {"", 0, 0, 0},
        {"nan,0", 0, 0, 0},
        {"0,-0.51", 0, 0, 0},
    };

    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        float j[2] = {9.0f, 9.0f};
        const bool ok = rd__frame_jitter_parse(kCases[i].s, j);
        CHECK(ok == (kCases[i].ok != 0), "parse \"%s\": %d", kCases[i].s, ok);
        if (ok && kCases[i].ok) {
            CHECK(j[0] == kCases[i].x && j[1] == kCases[i].y, "parse \"%s\": (%g, %g)", kCases[i].s,
                  (double)j[0], (double)j[1]);
        }
        if (!ok) {
            CHECK(j[0] == 9.0f && j[1] == 9.0f, "parse \"%s\" refused but wrote out", kCases[i].s);
        }
    }
    CHECK(!rd__frame_jitter_parse(NULL, (float[2]){0}), "parse NULL");
    float g[2];
    rd__frame_jitter_set(0.25f, -0.125f);
    CHECK(rd__frame_jitter_get(g) && g[0] == 0.25f && g[1] == -0.125f, "set, get");
    CHECK(rd__frame_jitter_of(NULL) == NULL, "no target, no jitter");
    rd__frame_jitter_clear();
    CHECK(!rd__frame_jitter_get(g) && g[0] == 0.0f && g[1] == 0.0f, "clear, get");
    printf("  parse: x,y within -0.5..0.5 only; set, get, clear\n");
}

/* ------------------------------------------------------------ the frames */

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

/* 12.4 GS window coordinates of a w x h target's (x, y), x and y in 1/16 */
static int32_t gx(int w, int32_t x16)
{
    return (2048 - w / 2) * 16 + x16;
}

static int32_t gy(int h, int32_t y16)
{
    return (2048 - h / 2) * 16 + y16;
}

/* a quad of two triangles over (x0, y0)..(x1, y1), in 1/16 GS pixels */
static void quad(int w, int h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4])
{
    const RdScreenVtx v[6] = {
        vtx(gx(w, x0), gy(h, y0), c, 0, 0), vtx(gx(w, x1), gy(h, y0), c, 0, 0),
        vtx(gx(w, x0), gy(h, y1), c, 0, 0), vtx(gx(w, x1), gy(h, y0), c, 0, 0),
        vtx(gx(w, x1), gy(h, y1), c, 0, 0), vtx(gx(w, x0), gy(h, y1), c, 0, 0),
    };
    rd_screen_prims(RD_PRIM_TRIANGLES, v, 6, RD_SPACE_WORLD, 1, 0);
}

static void sprite(int w, int h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4],
                   int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    const RdScreenVtx v[2] = {vtx(gx(w, x0), gy(h, y0), c, (float)u0, (float)v0),
                              vtx(gx(w, x1), gy(h, y1), c, (float)u1, (float)v1)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

/* the shift case's frame: row f's quad from GS x 100 + f / 16 to 160 + f / 16
 * at y 20 + 8 f .. 24 + 8 f; column f's from y 300 + f / 16 to 380 + f / 16
 * at x 300 + 8 f .. 304 + 8 f */
static void shiftFrame(void)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, GS, GS, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBlack, 0, 0);
    opaque2D();
    rd_texture_off();
    for (int f = 0; f < ROWS; f++) {
        quad(GS, GS, 100 * 16 + f, (20 + 8 * f) * 16, 160 * 16 + f, (24 + 8 * f) * 16, kWhite);
        quad(GS, GS, (300 + 8 * f) * 16, 300 * 16 + f, (304 + 8 * f) * 16, 380 * 16 + f, kWhite);
    }
    rd_end_frame(0);
}

static int readTarget(RdTargetId id, uint8_t *dst, size_t size, uint32_t *w, uint32_t *h)
{
    if (!rd__read_target(rd_target(id), dst, size, w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return 0;
    }
    return 1;
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

static int lit(const uint8_t *img, uint32_t w, int x, int y)
{
    const uint8_t *p = img + ((size_t)y * w + (size_t)x) * 4;
    return p[0] | p[1] | p[2];
}

/* C(f) of every row and column of shiftFrame's SCENE (w x h texels, scale s):
 * the first lit texel; -1 none */
static void edges(const uint8_t *img, uint32_t w, uint32_t h, int s, int row[ROWS], int col[ROWS])
{
    for (int f = 0; f < ROWS; f++) {
        row[f] = col[f] = -1;
        const int y = (22 + 8 * f) * s, x = (302 + 8 * f) * s;
        for (int t = 90 * s; t < 130 * s && (uint32_t)t < w && (uint32_t)y < h; t++) {
            if (lit(img, w, t, y)) {
                row[f] = t;
                break;
            }
        }
        for (int t = 290 * s; t < 330 * s && (uint32_t)t < h && (uint32_t)x < w; t++) {
            if (lit(img, w, x, t)) {
                col[f] = t;
                break;
            }
        }
    }
}

static void settings(RdPreset preset, float scale)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = preset;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.sceneScale = preset == RD_PRESET_ENHANCED ? scale : 0.0f;
    rd_set_settings(&s);
}

static void testShift(RdPreset preset, int s, const char *name)
{
    settings(preset, (float)s);
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    rd__frame_jitter_clear();
    shiftFrame();
    if (!readTarget(RD_TARGET_SCENE, s_a, sizeof(s_a), &w, &h)) {
        return;
    }
    CHECK(w == (uint32_t)(GS * s) && h == (uint32_t)(GS * s), "%s: SCENE %ux%u", name, w, h);
    int row0[ROWS], col0[ROWS];
    edges(s_a, w, h, s, row0, col0);
    for (int f = 0; f < ROWS; f++) {
        CHECK(row0[f] >= 0 && col0[f] >= 0, "%s: row/column %d not drawn (%d, %d)", name, f,
              row0[f], col0[f]);
    }
    for (int f = 0; f + 16 < ROWS; f++) {
        CHECK(row0[f + 16] == row0[f] + s && col0[f + 16] == col0[f] + s,
              "%s: a GS pixel later is %d texels on (row %d: %d -> %d, column %d -> %d)", name, s,
              f, row0[f], row0[f + 16], col0[f], col0[f + 16]);
    }
    int steps = 0;
    for (int f = 0; f + 1 < ROWS; f++) {
        steps += row0[f + 1] != row0[f];
    }
    CHECK(steps >= s, "%s: the rows' edges step %d times (the sixteenths not resolved)", name,
          steps);

    /* 0, 0: the same bytes */
    rd__frame_jitter_set(0.0f, 0.0f);
    shiftFrame();
    if (!readTarget(RD_TARGET_SCENE, s_b, sizeof(s_b), &w2, &h2)) {
        rd__frame_jitter_clear();
        return;
    }
    CHECK(rd__frame_jitter_groups() > 0, "%s: 0, 0 moved no frame group", name);
    CHECK(w2 == w && h2 == h && memcmp(s_a, s_b, (size_t)w * h * 4) == 0,
          "%s: SCENE with the jitter 0, 0 differs from without", name);

    static const float kJ[2][2] = {{0.25f, -0.25f}, {-0.25f, 0.25f}};
    for (int c = 0; c < 2; c++) {
        rd__frame_jitter_set(kJ[c][0], kJ[c][1]);
        shiftFrame();
        if (!readTarget(RD_TARGET_SCENE, s_b, sizeof(s_b), &w2, &h2)) {
            break;
        }
        int row[ROWS], col[ROWS];
        edges(s_b, w2, h2, s, row, col);
        const int kx = (int)(16.0f * kJ[c][0] / (float)s), ky = (int)(16.0f * kJ[c][1] / (float)s);
        int checked = 0, moved = 0;
        for (int f = 0; f < ROWS; f++) {
            if (f + kx >= 0 && f + kx < ROWS) {
                CHECK(row[f] == row0[f + kx],
                      "%s: jitter (%g, %g): row %d's edge at %d, want row %d's %d", name,
                      (double)kJ[c][0], (double)kJ[c][1], f, row[f], f + kx, row0[f + kx]);
                checked++;
                moved += row[f] != row0[f];
            }
            if (f + ky >= 0 && f + ky < ROWS) {
                CHECK(col[f] == col0[f + ky],
                      "%s: jitter (%g, %g): column %d's edge at %d, want column %d's %d", name,
                      (double)kJ[c][0], (double)kJ[c][1], f, col[f], f + ky, col0[f + ky]);
                checked++;
                moved += col[f] != col0[f];
            }
        }
        CHECK(moved > 0, "%s: jitter (%g, %g) moved no edge", name, (double)kJ[c][0],
              (double)kJ[c][1]);
        printf("  shift %s: jitter (%g, %g) = %d/16 and %d/16 GS pixel: %d edges as predicted, "
               "%d moved\n",
               name, (double)kJ[c][0], (double)kJ[c][1], kx, ky, checked, moved);
    }
    rd__frame_jitter_clear();
}

/* the whole case's frame */
static void wholeFrame(void)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, GS, GS, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBlack, 0, 0);
    opaque2D();
    rd_texture_off();
    const RdScreenVtx t[3] = {vtx(gx(GS, 60 * 16 + 3), gy(GS, 40 * 16 + 5), kRed, 0, 0),
                              vtx(gx(GS, 250 * 16 + 9), gy(GS, 60 * 16 + 1), kGreen, 0, 0),
                              vtx(gx(GS, 90 * 16 + 14), gy(GS, 220 * 16 + 7), kBlue, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_WORLD, 1, 0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(GS, GS, 280 * 16, 40 * 16, 344 * 16, 104 * 16, kGrey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(GS, GS, 300 * 16 + 4, 150 * 16 + 4, 364 * 16 + 4, 214 * 16 + 4, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8);
    rd_texture_off();
    rd_select_list(11);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    rd_clear_target(rd_target(RD_TARGET_WORK1), kBlack, 0, 0);
    opaque2D();
    const RdScreenVtx w[3] = {vtx(gx(256, 10 * 16 + 3), gy(256, 10 * 16 + 5), kRed, 0, 0),
                              vtx(gx(256, 200 * 16 + 9), gy(256, 30 * 16 + 1), kGreen, 0, 0),
                              vtx(gx(256, 40 * 16 + 14), gy(256, 200 * 16 + 7), kBlue, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, w, 3, RD_SPACE_WORLD, 1, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = 128;
    pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

/* pixels of b (w x h) where b(x, y) != a(x - dx, y - dy), over x0 <= x < x1,
 * y0 <= y < y1 */
static int shifted(const uint8_t *a, const uint8_t *b, uint32_t w, int dx, int dy, int x0, int x1,
                   int y0, int y1, int *fx, int *fy)
{
    int bad = 0;
    *fx = *fy = -1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const uint8_t *p = b + ((size_t)y * w + (size_t)x) * 4;
            const uint8_t *q = a + ((size_t)(y - dy) * w + (size_t)(x - dx)) * 4;
            if (memcmp(p, q, 4) != 0) {
                if (bad++ == 0) {
                    *fx = x;
                    *fy = y;
                }
            }
        }
    }
    return bad;
}

static void testWhole(void)
{
    static uint8_t work0[256 * 256 * 4], work1[256 * 256 * 4];
    static uint8_t disp0[512 * 256 * 4], disp1[512 * 256 * 4];
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0, ww = 0, wh = 0, dw = 0, dh = 0;
    int fx, fy;
    settings(RD_PRESET_ORIGINAL, 1.0f);
    rd__frame_jitter_clear();
    wholeFrame();
    if (!readTarget(RD_TARGET_SCENE, s_a, sizeof(s_a), &w, &h) ||
        !readTarget(RD_TARGET_WORK1, work0, sizeof(work0), &ww, &wh) ||
        !readTarget(RD_TARGET_DISPLAY, disp0, sizeof(disp0), &dw, &dh) || !readOut(s_outA)) {
        return;
    }
    CHECK(w == GS && h == GS && ww == 256 && wh == 256 && dw == 512 && dh == 256,
          "whole: SCENE %ux%u, WORK1 %ux%u, DISPLAY %ux%u", w, h, ww, wh, dw, dh);
    int litN = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        litN += s_a[i * 4] | s_a[i * 4 + 1] | s_a[i * 4 + 2] ? 1 : 0;
    }
    CHECK(litN > 20000, "whole: the scene drawn (%d lit texels)", litN);

    /* (1, 1): SCENE one texel right and down, every texel; WORK1 the same */
    rd__frame_jitter_set(1.0f, 1.0f);
    wholeFrame();
    if (readTarget(RD_TARGET_SCENE, s_b, sizeof(s_b), &w2, &h2) &&
        readTarget(RD_TARGET_WORK1, work1, sizeof(work1), &ww, &wh)) {
        const int bad = shifted(s_a, s_b, w, 1, 1, 1, (int)w, 1, (int)h, &fx, &fy);
        CHECK(bad == 0,
              "whole: SCENE with the jitter (1, 1) is not the picture moved one texel: %d texels "
              "differ (first %d, %d)",
              bad, fx, fy);
        CHECK(memcmp(work0, work1, sizeof(work0)) == 0, "whole: WORK1 changed with the jitter");
        printf("  whole: SCENE moved (1, 1) exactly (%d lit texels), WORK1 unchanged\n", litN);
    }
    CHECK(rd__frame_jitter_groups() > 0, "whole: (1, 1) moved no frame group");

    /* (1, 0): DISPLAY one texel right, the present two pixels right */
    rd__frame_jitter_set(1.0f, 0.0f);
    wholeFrame();
    if (readTarget(RD_TARGET_DISPLAY, disp1, sizeof(disp1), &dw, &dh) && readOut(s_outB)) {
        const int badD = shifted(disp0, disp1, dw, 1, 0, 8, (int)dw - 8, 0, (int)dh, &fx, &fy);
        CHECK(badD == 0,
              "whole: DISPLAY with the jitter (1, 0) is not the reduction moved one texel: %d "
              "differ (first %d, %d)",
              badD, fx, fy);
        const int badP = shifted(s_outA, s_outB, OUT_W, 2, 0, 16, OUT_W - 16, 0, OUT_H, &fx, &fy);
        CHECK(badP == 0,
              "whole: the present with the jitter (1, 0) is not the picture moved two pixels: %d "
              "differ (first %d, %d)",
              badP, fx, fy);
        printf("  whole: DISPLAY moved one texel, the present two pixels (neither jittered)\n");
    }
    rd__frame_jitter_clear();
}

int main(void)
{
    testHalton();
    testParse();
    for (int i = 0; i < 16 * 16; i++) {
        uint32_t v = (uint32_t)i * 2654435761u + 7u;
        v ^= v >> 15;
        v *= 0x846CA68Bu;
        v ^= v >> 16;
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
    if (!rd_init(GS, GS, &s, NULL)) {
        printf("rd_jitter_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    rd__set_not_implemented_fatal(false); /* counted: none may be replayed (below) */
    s_tex = rd_create_texture(16, 16, s_texel, RD_TEXA_80_80, "jitter test");
    testShift(RD_PRESET_ORIGINAL, 1, "Original 1x");
    testShift(RD_PRESET_ENHANCED, 2, "Enhanced 2x");
    testWhole();
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%u validation errors", verr);
    rd_shutdown();
    if (failures) {
        printf("rd_jitter_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_jitter_test: ok\n");
    return 0;
}
