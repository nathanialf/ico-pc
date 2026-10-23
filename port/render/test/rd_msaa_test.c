/* rd_msaa_test.c: the anti-aliasing's MSAA (rd_replay.c, the MSAA epoch).
 *
 * Without a device: rd_aa_samples and rd_aa_supported_mask (the
 * supersampling values only).  With a device, the mask without the
 * epoch's depth resolve (depth_resolve_ps cleared for one call, as an
 * OpenGL driver below 4.5 leaves it): the supersampling values only.
 * On a Vulkan device (77 without one, or when it lacks 4 samples, a depth it
 * can sample or a fog that reads it: rd_aa_supported_mask without MSAA4),
 * at 4:3 Enhanced 1x, each frame replayed without anti-aliasing and with
 * MSAA4, SCENE read back:
 *   scene    triangles with slanted edges at several depths (GEQUAL) over a
 *            cleared scene, flat and textured integer-aligned sprites:
 *            every pixel whose 3x3 neighbourhood in the frame without
 *            anti-aliasing is one colour is the same byte for byte with
 *            MSAA4, some edge pixels differ, and the sprites' rectangles
 *            (two pixels round them) are byte-identical; the record says 4
 *            samples and one resolve, and no bind group slot of the replays
 *            without a binding (RdPerfRecord.unboundSlots);
 *   date     the same, then a DATE-tested sprite over it (the snapshot
 *            closes the epoch): again identical away from edges;
 *   fog      the fog self-test's grid fogged (cells at GS Z (n << 16) |
 *            0x8000, rd_fog_selftest_test's frame): per sample (with
 *            sample-rate shading) and through the resolved depth
 *            (rd__msaa_test_fog_resolved) both equal the frame without
 *            anti-aliasing (drawn with the fixed-function blend, which the
 *            fog of an MSAA frame keeps) on every pixel inside a cell (its
 *            3x3 one colour);
 *   redraw   a triangle slanted in depth (and a nearer one over
 *            part of it) drawn in the epoch, then the epoch closed by a
 *            pass on SCENE's depth with WORK2 as its colour (the flare
 *            mask's pass) where the slanted triangle is drawn again with
 *            GEQUAL and no Z write: WORK2 is the same as without
 *            anti-aliasing on every pixel away from an edge, and the
 *            triangle covers its interior (the depth resolve takes the
 *            smallest sample; the largest failed the re-draw on a slope);
 *   original the Original preset with MSAA4 asked for: the frame without
 *            anti-aliasing, byte for byte. */
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

#define GW 512
#define GH 448
#define CELLS 16

static RdTex s_tex;
static uint8_t s_texel[16 * 16 * 4];
static uint8_t s_off[GW * GH * 4], s_on[GW * GH * 4];

static const uint8_t kRed[4] = {220, 30, 10, 0x80}, kBlue[4] = {10, 40, 230, 0x80},
                     kGreen[4] = {20, 200, 60, 0x80}, kGrey[4] = {0x80, 0x80, 0x80, 0x80},
                     kYellow[4] = {240, 220, 20, 0x80}, kClear[4] = {30, 60, 90, 0x00};

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* a vertex at (x, y) in 1/16 GS pixels from the target's top-left */
static RdScreenVtx vtx(int32_t x, int32_t y, uint32_t z, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - GW / 2) * 16 + x;
    v.y = (2048 - GH / 2) * 16 + y;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

static void tri(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t z,
                const uint8_t c[4])
{
    RdScreenVtx v[3] = {vtx(x0, y0, z, c, 0, 0), vtx(x1, y1, z, c, 0, 0), vtx(x2, y2, z, c, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, v, 3, RD_SPACE_WORLD, 1, 0);
}

/* a sprite over whole GS pixels [x0, x1) x [y0, y1) */
static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t z, const uint8_t c[4],
                   int textured)
{
    RdScreenVtx v[2] = {vtx(x0 * 16, y0 * 16, z, c, 8.0f, 8.0f),
                        vtx(x1 * 16, y1 * 16, z, c, (float)(16 * 16 + 8), (float)(16 * 16 + 8))};
    if (textured) {
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    } else {
        rd_texture_off();
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    rd_texture_off();
}

/* the integer sprites' rectangles, two pixels round them */
static const int32_t kSprites[2][4] = {{40, 300, 140, 400}, {300, 320, 316, 336}};

static void sceneFrame(int date)
{
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, kClear, 1, 0);
    rd_abe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_texture_off();
    rd_z_write(1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    /* slanted edges, two depths crossing */
    tri(20 * 16 + 3, 20 * 16 + 7, 260 * 16 + 9, 60 * 16 + 1, 90 * 16 + 5, 240 * 16 + 11,
        0x40000000u, kRed);
    tri(150 * 16 + 1, 40 * 16 + 5, 420 * 16 + 13, 120 * 16 + 2, 200 * 16 + 7, 260 * 16 + 9,
        0x60000000u, kBlue);
    tri(60 * 16 + 9, 150 * 16 + 3, 480 * 16 + 4, 200 * 16 + 14, 330 * 16 + 2, 280 * 16 + 6,
        0x50000000u, kGreen);
    /* integer-aligned sprites: a flat backdrop and a textured one */
    sprite(kSprites[0][0], kSprites[0][1], kSprites[0][2], kSprites[0][3], 0x70000000u, kYellow, 0);
    sprite(kSprites[1][0], kSprites[1][1], kSprites[1][2], kSprites[1][3], 0x70000000u, kGrey, 1);
    if (date) {
        /* over the triangles, where the destination alpha has its MSB */
        rd_test_gs(0x3C000); /* DATE=1 DATM=1, Z ALWAYS */
        rd_z_write(0);
        sprite(10, 10, 500, 290, 0, kGrey, 1);
    }
    rd_end_frame(0); /* replayed here: interpolation is off */
}

static int readScene(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    rhi_wait_idle();
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), dst, (size_t)GW * GH * 4, &w, &h) || w != GW ||
        h != GH) {
        CHECK(0, "SCENE readback (%u x %u)", w, h);
        return 0;
    }
    return 1;
}

static int uniform3x3(const uint8_t *a, int x, int y)
{
    const uint8_t *c = &a[((size_t)y * GW + (size_t)x) * 4];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= GW || yy >= GH) {
                continue;
            }
            if (memcmp(&a[((size_t)yy * GW + (size_t)xx) * 4], c, 4) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* the frame without (s_off) and with (s_on) MSAA4: away from edges the same;
 * returns the pixels that differ */
static int compareEdges(const char *name, int wantSome)
{
    int interiorBad = 0, differ = 0, fx = -1, fy = -1;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            const size_t i = ((size_t)y * GW + (size_t)x) * 4;
            const int d = memcmp(&s_off[i], &s_on[i], 4) != 0;
            differ += d;
            if (d && uniform3x3(s_off, x, y)) {
                if (interiorBad++ == 0) {
                    fx = x;
                    fy = y;
                }
            }
        }
    }
    printf("  %s: %d pixels differ, %d of them away from an edge (first %d,%d)\n", name, differ,
           interiorBad, fx, fy);
    CHECK(interiorBad == 0, "%s: a pixel away from every edge differs (%d, %d)", name, fx, fy);
    if (wantSome) {
        CHECK(differ > 0, "%s: no edge pixel changed: the frame was not multisampled", name);
    }
    return differ;
}

static void settings(RdPreset preset, int aa)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = preset;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.sceneScale = 1.0f;
    s.antiAlias = (uint8_t)aa;
    rd_set_settings(&s);
}

static void drainPerf(uint32_t *samples, uint32_t *resolves, uint32_t *unbound)
{
    RdPerfRecord rec;
    *samples = *resolves = *unbound = 0;
    while (rd_perf_pop(&rec)) {
        *samples = rec.msaaSamples;
        *resolves = rec.msaaResolves;
        *unbound += rec.unboundSlots;
    }
}

static void testScene(void)
{
    uint32_t samples = 0, resolves = 0, unbound = 0;
    settings(RD_PRESET_ENHANCED, RD_AA_OFF);
    sceneFrame(0);
    if (!readScene(s_off)) {
        return;
    }
    settings(RD_PRESET_ENHANCED, RD_AA_MSAA4);
    sceneFrame(0);
    sceneFrame(0); /* the first replay's GPU record needs frames in flight to arrive */
    if (!readScene(s_on)) {
        return;
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        sceneFrame(0);
    }
    drainPerf(&samples, &resolves, &unbound);
    printf("  scene: the last record: %u samples, %u resolves; %u unbound slots\n", samples,
           resolves, unbound);
    CHECK(samples == 4 && resolves == 1, "scene: 4 samples, 1 resolve (%u, %u)", samples, resolves);
    /* the depth resolve's group (msaaFullscreen) binds every slot of the
     * texture layout: Direct3D 12 logs a slot left out */
    CHECK(unbound == 0, "scene: %u bind group slots without a binding", unbound);
    compareEdges("scene", 1);
    int spriteBad = 0;
    for (int k = 0; k < 2; k++) {
        for (int y = kSprites[k][1] - 2; y < kSprites[k][3] + 2; y++) {
            for (int x = kSprites[k][0] - 2; x < kSprites[k][2] + 2; x++) {
                const size_t i = ((size_t)y * GW + (size_t)x) * 4;
                spriteBad += memcmp(&s_off[i], &s_on[i], 4) != 0;
            }
        }
    }
    CHECK(spriteBad == 0, "scene: %d pixels of the integer sprites differ", spriteBad);
}

static void testDate(void)
{
    settings(RD_PRESET_ENHANCED, RD_AA_OFF);
    sceneFrame(1);
    if (!readScene(s_off)) {
        return;
    }
    settings(RD_PRESET_ENHANCED, RD_AA_MSAA4);
    sceneFrame(1);
    if (!readScene(s_on)) {
        return;
    }
    compareEdges("date", 1);
}

/* rd_fog_selftest_test's frame: the grid, then the fog */
static void lutOf(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255);
}

static void fogFrame(void)
{
    static const uint8_t clear[4] = {30, 60, 90, 0x80};
    static RdScreenVtx v[CELLS * CELLS * 2];
    static uint8_t lut[256 * 4];
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, clear, 1, 0);
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(1);
    rd_fba(0);
    rd_gouraud(0);
    memset(v, 0, sizeof(v));
    for (int c = 0; c < CELLS * CELLS; c++) {
        const int32_t cx = c % CELLS, cy = c / CELLS;
        const uint8_t rgba[4] = {(uint8_t)(c * 53 + 17), (uint8_t)(c * 101 + 71),
                                 (uint8_t)(c * 29 + 200), 0x40};
        for (int k = 0; k < 2; k++) {
            v[c * 2 + k] = vtx((cx + k) * GW / CELLS * 16, (cy + k) * GH / CELLS * 16,
                               ((uint32_t)c << 16) | 0x8000u, rgba, 0, 0);
        }
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, CELLS * CELLS * 2, RD_SPACE_FULLFRAME, 0, 0);
    /* a slanted triangle across cells: depth edges inside the grid */
    rd_test_gs(RD_TEST_Z_GEQUAL);
    tri(30 * 16 + 5, 50 * 16 + 3, 470 * 16 + 9, 90 * 16 + 11, 250 * 16 + 1, 400 * 16 + 7,
        0x00A08000u, kRed);
    rd_select_list(4);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_pabe(0);
    rd_blend_func(RD_BLEND_LERP_AS, 128);
    rd_texture(rd_target_texture(scene, RD_VIEW_DEPTH), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_z_write(0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_abe(1);
    for (int i = 0; i < 256; i++) {
        lutOf(i, &lut[i * 4]);
    }
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.lut = lut;
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
    pp.z = 0xFFFFFF;
    pp.rect[0] = (float)(0x8000 - GW * 8);
    pp.rect[1] = (float)(0x8000 - GH * 8);
    pp.rect[2] = (float)(0x8000 + GW * 8);
    pp.rect[3] = (float)(0x8000 + GH * 8);
    pp.uv[0] = pp.uv[1] = 8.0f;
    pp.uv[2] = (float)(8 + GW * 16);
    pp.uv[3] = (float)(8 + GH * 16);
    rd_post(RD_POST_FOG, &pp);
    rd_end_frame(0);
}

static uint8_t s_fogMs[GW * GH * 4];

static void testFog(void)
{
    /* the fog of an MSAA frame blends fixed-function, per sample and
     * resolved alike (rd_dst_read.c): the frame without anti-aliasing it
     * is compared with is drawn so too */
    const RdBlendExact exact = rd_blend_exact();
    rd_set_blend_exact(RD_BLEND_EXACT_OFF);
    settings(RD_PRESET_ENHANCED, RD_AA_OFF);
    fogFrame();
    fogFrame(); /* the first fogged replay of a scene size records the probe */
    rd_set_blend_exact(exact);
    if (!readScene(s_off)) {
        return;
    }
    settings(RD_PRESET_ENHANCED, RD_AA_MSAA4);
    rd__msaa_test_fog_resolved(false);
    fogFrame();
    if (!readScene(s_on)) {
        return;
    }
    memcpy(s_fogMs, s_on, sizeof(s_fogMs));
    printf("  fog: %s\n", rhi_limits()->sampleRateShading ? "per sample, then resolved"
                                                          : "resolved (no sample-rate shading)");
    compareEdges("fog per sample", 1);
    rd__msaa_test_fog_resolved(true);
    fogFrame();
    rd__msaa_test_fog_resolved(false);
    if (!readScene(s_on)) {
        return;
    }
    compareEdges("fog resolved", 1);
    /* per sample against resolved: inside the cells */
    int bad = 0;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            const size_t i = ((size_t)y * GW + (size_t)x) * 4;
            bad += uniform3x3(s_off, x, y) && memcmp(&s_fogMs[i], &s_on[i], 4) != 0;
        }
    }
    CHECK(bad == 0, "fog: per sample and resolved differ at %d interior pixels", bad);
}

/* testRedraw's frame: the epoch's triangles, then the re-draw into WORK2 */
static void redrawTri(const uint8_t c[4])
{
    RdScreenVtx v[3] = {vtx(40 * 16 + 5, 30 * 16 + 9, 0x20000000u, c, 0, 0),
                        vtx(470 * 16 + 11, 80 * 16 + 3, 0x68000000u, c, 0, 0),
                        vtx(120 * 16 + 7, 410 * 16 + 13, 0x44000000u, c, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, v, 3, RD_SPACE_WORLD, 1, 0);
}

static void redrawFrame(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0};
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    const RdTarget work2 = rd_target(RD_TARGET_WORK2);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, kClear, 1, 0);
    rd_abe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_texture_off();
    rd_z_write(1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    redrawTri(kRed);
    /* nearer than the slanted one wherever they overlap */
    tri(200 * 16 + 3, 150 * 16 + 9, 360 * 16 + 1, 170 * 16 + 5, 260 * 16 + 13, 330 * 16 + 7,
        0x78000000u, kBlue);
    /* the pass that closes the epoch: SCENE's depth, another colour */
    rd_select_list(7);
    rd_set_target(work2, scene, GW, GH, 0);
    rd_clear_target(work2, black, 0, 0);
    rd_z_write(0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    redrawTri(kGreen);
    rd_end_frame(0);
}

static int readWork2(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    rhi_wait_idle();
    if (!rd__read_target(rd_target(RD_TARGET_WORK2), dst, (size_t)GW * GH * 4, &w, &h) || w != GW ||
        h != GH) {
        CHECK(0, "WORK2 readback (%u x %u)", w, h);
        return 0;
    }
    return 1;
}

/* WORK2's pixels the re-draw covers in s_off (not the clear's black) whose
 * 3x3 is one colour there, and that hold s_off's colour in a */
static int redrawInterior(const uint8_t *a)
{
    static const uint8_t black[3] = {0, 0, 0};
    int n = 0;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            const size_t i = ((size_t)y * GW + (size_t)x) * 4;
            n += uniform3x3(s_off, x, y) && memcmp(&s_off[i], black, 3) != 0 &&
                 memcmp(&a[i], &s_off[i], 4) == 0;
        }
    }
    return n;
}

static void testRedraw(void)
{
    settings(RD_PRESET_ENHANCED, RD_AA_OFF);
    redrawFrame();
    if (!readWork2(s_off)) {
        return;
    }
    settings(RD_PRESET_ENHANCED, RD_AA_MSAA4);
    redrawFrame();
    if (!readWork2(s_on)) {
        return;
    }
    const int off = redrawInterior(s_off), on = redrawInterior(s_on);
    printf("  redraw: %d interior pixels re-drawn without anti-aliasing, %d with MSAA4\n", off, on);
    CHECK(off > 20000, "redraw: the frame without anti-aliasing re-draws %d interior pixels", off);
    CHECK(on == off, "redraw: MSAA4 re-draws %d of the %d interior pixels", on, off);
    compareEdges("redraw", 0);
}

static void testOriginal(void)
{
    settings(RD_PRESET_ORIGINAL, RD_AA_OFF);
    sceneFrame(1);
    if (!readScene(s_off)) {
        return;
    }
    settings(RD_PRESET_ORIGINAL, RD_AA_MSAA4);
    sceneFrame(1);
    if (!readScene(s_on)) {
        return;
    }
    CHECK(memcmp(s_off, s_on, sizeof(s_off)) == 0,
          "original: MSAA4 asked for under the Original preset changed SCENE");
}

int main(void)
{
    printf("rd_msaa_test\n");
    CHECK(rd_aa_samples(RD_AA_OFF) == 1 && rd_aa_samples(RD_AA_SSAA4) == 1 &&
              rd_aa_samples(RD_AA_MSAA2) == 2 && rd_aa_samples(RD_AA_MSAA4) == 4 &&
              rd_aa_samples(RD_AA_MSAA8) == 8,
          "rd_aa_samples");
    CHECK(rd_aa_supported_mask() == ((1u << RD_AA_OFF) | (1u << RD_AA_SSAA2) | (1u << RD_AA_SSAA4)),
          "without a device: off and the supersampling values (0x%x)", rd_aa_supported_mask());
    for (int i = 0; i < 16 * 16; i++) {
        const uint32_t h = hash((uint32_t)i * 2654435761u + 7u);
        s_texel[i * 4 + 0] = (uint8_t)h;
        s_texel[i * 4 + 1] = (uint8_t)(h >> 8);
        s_texel[i * 4 + 2] = (uint8_t)(h >> 16);
        s_texel[i * 4 + 3] = 0x80;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.sceneScale = 1.0f;
    if (!rd_init(GW, GH, &s, NULL)) {
        printf("rd_msaa_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    const RhiLimits *lim = rhi_limits();
    const uint32_t mask = rd_aa_supported_mask();
    printf("  device %s: sample counts 0x%x, sample-rate shading %d, depth sampled %d, fog path "
           "%s: supported 0x%x\n",
           rhi_adapter_name(), lim->msaaCounts, lim->sampleRateShading, lim->depthSampled,
           rd__fog_path_name(g_rd.fogPath), mask);
    CHECK((mask & 7u) == 7u, "off and supersampling always supported");
    const bool want4 = (lim->msaaCounts & 4u) && lim->depthSampled && g_rd.fogPath != RD_FOG_BUFFER;
    CHECK(((mask >> RD_AA_MSAA4) & 1u) == (want4 ? 1u : 0u), "MSAA4 listed as the device has it");
    {
        /* without the epoch's depth resolve (an OpenGL driver below 4.5
           refuses depth_resolve_ps, rd__gpu_init): no MSAA value is
           offered, the supersampling ones are */
        const RhiShader keep = g_rd.fs[RD_FS_DEPTH_RESOLVE];
        g_rd.fs[RD_FS_DEPTH_RESOLVE] = (RhiShader){0};
        const uint32_t without = rd_aa_supported_mask();
        g_rd.fs[RD_FS_DEPTH_RESOLVE] = keep;
        CHECK(without == ((1u << RD_AA_OFF) | (1u << RD_AA_SSAA2) | (1u << RD_AA_SSAA4)),
              "without depth_resolve_ps: off and the supersampling values (0x%x)", without);
        CHECK(rd_aa_supported_mask() == mask, "the module back: the mask as before");
    }
    if (!(mask & (1u << RD_AA_MSAA4))) {
        rd_shutdown();
        printf("rd_msaa_test: SKIP: no 4-sample MSAA on this device\n");
        return failures ? 1 : 77;
    }
    s_tex = rd_create_texture(16, 16, s_texel, RD_TEXA_80_80, "msaa test");
    testScene();
    testDate();
    testFog();
    testRedraw();
    testOriginal();
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_destroy_texture(s_tex);
    rd_shutdown();
    if (failures) {
        printf("rd_msaa_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_msaa_test: ok\n");
    return 0;
}
