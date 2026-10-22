/* rd_crack_test.c: the crack filler above 1x (rd_crack_fill.c, issue 26).
 *
 *   rd_crack_test 2|4|1|frac|original [msaa]
 *
 * On a Vulkan device (77 without one, or without MSAA4 when msaa is
 * given), a 640 x 448 frame under Enhanced at scale 2, 4 or 1, at the
 * window's own 1800 x 1080 texels (frac: 2.8125 x 2.41) or under the
 * Original preset, recorded through the renderer's calls and replayed
 * twice (after a first replay that the fog's probe takes), the filler off
 * and then as the environment has it
 * (ICO_RD_CRACK_FILL), SCENE read back after the fog:
 *   cracks  two flat triangles that share a long diagonal edge, the second
 *           one's edge 1/16 GS pixel to the right of the first's (the
 *           form of DIVERGENCES.md VD-20: one world edge truncated through
 *           two node matrices): without the filler texels along the edge
 *           show the fog's colour of the clear (GS Z 0, the table's entry
 *           0) at 2x and above; with it none, each filled texel the colour
 *           (and, where the depth reads back, the depth) of a written
 *           texel at most the reach away on its row or its column;
 *   slit    two sprites 1.5 GS pixels apart (3 texels at 2x, 6 at 4x, 4 at
 *           2.8125: wider than the reach): the slit (its texels half a
 *           texel in from its sides) keeps the clear's fog, filler or not;
 *   rest    every texel the first replay drew is the same in both, colour
 *           and depth: the pictures differ only at the cracks;
 *   counts  the replay with the filler drew one fill pass and its count
 *           (rd__crack_fill_counts, every replay counted) equals the
 *           texels it changed;
 * at 1x, under Original, under MSAA or with ICO_RD_CRACK_FILL=0 no fill
 * pass is drawn and the two replays are byte-identical.  The log lines are
 * matched by test/rd_crack.cmake. */
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

#define GW 640
#define GH 448

/* the shapes in GS pixels from the target's top-left */
#define EDGE_X0 300.0 /* the shared edge from (EDGE_X0, TOP) to (LEFT, BOTTOM) */
#define LEFT 40.0
#define TOP 40.0
#define BOTTOM 400.0
#define BAND0 80.0 /* the rows measured along the edge, away from its ends */
#define BAND1 360.0
#define SLIT0 500.0 /* the slit between the sprites, [SLIT0, SLIT1) */
#define SLIT1 501.5

static const uint8_t kClear[4] = {30, 60, 90, 0x80};
static const uint8_t kTri1[4] = {200, 40, 40, 0x80};
static const uint8_t kTri2[4] = {40, 200, 40, 0x80};
static const uint8_t kRect[4] = {40, 40, 200, 0x80};

static RdScreenVtx vtx(double x, double y, uint32_t z, const uint8_t c[4])
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - GW / 2) * 16 + (int32_t)(x * 16.0 + 0.5);
    v.y = (2048 - GH / 2) * 16 + (int32_t)(y * 16.0 + 0.5);
    v.z = z;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* rd_fog_selftest_test's table */
static void lutOf(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255);
}

static void recordFrame(void)
{
    static uint8_t lut[256 * 4];
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, kClear, 1, 0);
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(1);
    rd_fba(0);
    rd_gouraud(0);
    /* the crack: the second triangle's shared edge one 12.4 step right */
    const double d = 1.0 / 16.0;
    RdScreenVtx t[6] = {
        vtx(LEFT, TOP, 0x00600000u, kTri1),       vtx(EDGE_X0, TOP, 0x00600000u, kTri1),
        vtx(LEFT, BOTTOM, 0x00600000u, kTri1),    vtx(EDGE_X0 + d, TOP, 0x00A00000u, kTri2),
        vtx(EDGE_X0, BOTTOM, 0x00A00000u, kTri2), vtx(LEFT + d, BOTTOM, 0x00A00000u, kTri2)};
    rd_screen_prims(RD_PRIM_TRIANGLES, t, 6, RD_SPACE_WORLD, 1, 0);
    /* the slit */
    RdScreenVtx s[4] = {vtx(400.0, TOP, 0x00800000u, kRect), vtx(SLIT0, BOTTOM, 0x00800000u, kRect),
                        vtx(SLIT1, TOP, 0x00800000u, kRect),
                        vtx(600.0, BOTTOM, 0x00800000u, kRect)};
    rd_screen_prims(RD_PRIM_SPRITES, s, 4, RD_SPACE_WORLD, 1, 0);
    /* ZFog.c fogHostDraw's state and record */
    rd_select_list(4);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_pabe(0);
    rd_blend_func(RD_BLEND_LERP_AS, 128);
    rd_texture(rd_target_texture(scene, RD_VIEW_DEPTH), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_z_write(0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_abe(1);
    rd_gouraud(0);
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
    rd_end_frame(0); /* replayed here: interpolation is off */
}

typedef struct Picture {
    uint8_t *px;
    float *z; /* NULL where the depth does not read back (D24S8) */
    uint32_t w, h;
    uint32_t fills; /* RdPerfRecord.crackFillPasses of its replay */
} Picture;

static bool replay(bool fill, Picture *p)
{
    rd__crack_fill_set_enabled(fill);
    recordFrame();
    p->fills = g_rdPerf.crackFillPasses;
    rhi_wait_idle();
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t) {
        return false;
    }
    const size_t n = (size_t)t->tw * t->th;
    p->px = malloc(n * 4);
    p->z = malloc(n * sizeof(float));
    if (!p->px || !p->z) {
        return false;
    }
    uint32_t zw = 0, zh = 0;
    const bool ok = rd__read_target(rd_target(RD_TARGET_SCENE), p->px, n * 4, &p->w, &p->h);
    if (!rhi_limits()->depthReadback ||
        !rd__read_target_depth(rd_target(RD_TARGET_SCENE), p->z, n * sizeof(float), &zw, &zh) ||
        zw != p->w || zh != p->h) {
        free(p->z);
        p->z = NULL;
    }
    return ok && p->w == t->tw && p->h == t->th;
}

/* the texel's GS position (FrameCB g_origin.zw: GS integers on texel
 * centres, rd_pixel_test's seamCracks) */
static double gsX(uint32_t x, float sx)
{
    return (double)x / sx;
}

int main(int argc, char **argv)
{
    printf("rd_crack_test\n");
    if (argc < 2) {
        printf("usage: rd_crack_test 2|4|1|frac|original [msaa]\n");
        return 2;
    }
    const bool original = strcmp(argv[1], "original") == 0;
    const bool frac = strcmp(argv[1], "frac") == 0;
    const bool msaa = argc > 2 && strcmp(argv[2], "msaa") == 0;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = original ? RD_PRESET_ORIGINAL : RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = original || frac ? (original ? 1.0f : 0.0f) : (float)atof(argv[1]);
    st.sceneWidth = frac ? 1800u : 0u;
    st.sceneHeight = frac ? 1080u : 0u;
    st.antiAlias = msaa ? (uint8_t)RD_AA_MSAA4 : (uint8_t)RD_AA_OFF;
    if (!rd_init(GW, GH, &st, NULL)) {
        printf("rd_crack_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    if (msaa && (rd_aa_supported_mask() & (1u << RD_AA_MSAA4)) == 0u) {
        printf("rd_crack_test: SKIP: MSAA4 is not offered here\n");
        rd_shutdown();
        return 77;
    }
    rd__crack_fill_count_all(true);
    RdCrackCounts c0;
    rd__crack_fill_counts(&c0);
    const bool envOn = c0.enabled; /* ICO_RD_CRACK_FILL */
    /* the first fogged replay is the fog probe's (its own read, the exact
     * table entry; under MSAA the resolved depth): one before the two
     * compared, which then draw the fog alike */
    rd__crack_fill_set_enabled(false);
    recordFrame();
    rd_begin_frame(); /* logs the probe */
    rd_end_frame(0);
    rd__crack_fill_counts(&c0);
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    CHECK(t != NULL, "no SCENE");
    if (!t) {
        rd_shutdown();
        return 1;
    }
    const float sx = t->sx, sy = t->sy;
    const uint32_t rx = rd__crack_fill_reach(sx), ry = rd__crack_fill_reach(sy);
    const bool active = envOn && !original && !msaa && (sx > 1.0f || sy > 1.0f);
    printf("  SCENE %ux%u texels, scale %gx%g, reach %ux%u; the filler %s\n", t->tw, t->th,
           (double)sx, (double)sy, rx, ry,
           active ? "draws" : (envOn ? "on, not drawn here" : "off (ICO_RD_CRACK_FILL=0)"));

    Picture off, on;
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    const bool okOff = replay(false, &off);
    const bool okOn = replay(envOn, &on);
    CHECK(okOff && okOn, "SCENE readbacks");
    rd__crack_fill_wait();
    RdCrackCounts c1;
    rd__crack_fill_counts(&c1);
    if (!(okOff && okOn)) {
        rd_shutdown();
        return 1;
    }
    const uint32_t w = off.w, h = off.h;
    /* the clear's fog: every texel nothing drew (corner texel 1, 1) */
    const uint8_t *ref = &off.px[((size_t)1 * w + 1) * 4];
    int cracksOff = 0, cracksOn = 0, near = 0, slitOff = 0, slitOn = 0, slitAll = 0;
    int changed = 0, changedDrawn = 0, badFrom = 0, badDepth = 0;
    for (uint32_t y = 0; y < h; y++) {
        const double gy = gsX(y, sy);
        const double ex = EDGE_X0 - (EDGE_X0 - LEFT) * (gy - TOP) / (BOTTOM - TOP);
        for (uint32_t x = 0; x < w; x++) {
            const size_t i = (size_t)y * w + x;
            const uint8_t *a = &off.px[i * 4], *b = &on.px[i * 4];
            const bool clearA = memcmp(a, ref, 4) == 0, clearB = memcmp(b, ref, 4) == 0;
            const double gx = gsX(x, sx);
            const bool band = gy >= BAND0 && gy <= BAND1;
            if (band && gx > ex - 2.0 / sx && gx < ex + 2.0 / sx) {
                near++;
                cracksOff += clearA;
                cracksOn += clearB;
            }
            /* half a texel in from the slit's sides: unwritten whichever
               side of a texel its centre is taken at */
            if (band && gx > SLIT0 + 0.5 / sx && gx < SLIT1 - 0.5 / sx) {
                slitAll++;
                slitOff += clearA;
                slitOn += clearB;
            }
            const bool zDiff = off.z && on.z && off.z[i] != on.z[i];
            if (memcmp(a, b, 4) == 0 && !zDiff) {
                continue;
            }
            changed++;
            changedDrawn += !clearA || (off.z && off.z[i] != 0.0f);
            /* a filled texel: a written texel within the reach on its row
               or its column has its colour (and its depth) */
            bool from = false, fromZ = false;
            for (int k = 1; k <= (int)RD_CRACK_REACH_MAX && !from; k++) {
                const int64_t nb[4][2] = {{(int64_t)x - k, y},
                                          {(int64_t)x + k, y},
                                          {x, (int64_t)y - k},
                                          {x, (int64_t)y + k}};
                for (int j = 0; j < 4 && !from; j++) {
                    const uint32_t r = j < 2 ? rx : ry;
                    if ((uint32_t)k > r || nb[j][0] < 0 || nb[j][1] < 0 || nb[j][0] >= (int64_t)w ||
                        nb[j][1] >= (int64_t)h) {
                        continue;
                    }
                    const size_t q = (size_t)nb[j][1] * w + (size_t)nb[j][0];
                    if (memcmp(&off.px[q * 4], ref, 4) != 0 && memcmp(&on.px[q * 4], b, 4) == 0) {
                        from = true;
                        fromZ = !on.z || on.z[i] == on.z[q];
                    }
                }
            }
            badFrom += !from;
            badDepth += from && !fromZ;
        }
    }
    printf("  cracks along the edge: %d texels of the clear's fog without the filler, %d with it, "
           "of %d near the edge\n",
           cracksOff, cracksOn, near);
    printf("  the slit: %d of %d texels the clear's fog without, %d with\n", slitOff, slitAll,
           slitOn);
    printf("  %d texels changed (%d the first replay drew; %d not a neighbour's colour, %d not its "
           "depth%s); fill passes %u and %u; counted %llu pixels over %llu replays (filled %llu)\n",
           changed, changedDrawn, badFrom, badDepth, off.z ? "" : ", no depth readback", off.fills,
           on.fills, (unsigned long long)(c1.pixels - c0.pixels),
           (unsigned long long)(c1.counted - c0.counted),
           (unsigned long long)(c1.filled - c0.filled));
    CHECK(near > 100, "the edge's band holds %d texels", near);
    CHECK(slitAll > 0 && slitOff == slitAll && slitOn == slitAll,
          "the slit: %d and %d of %d texels the clear's fog", slitOff, slitOn, slitAll);
    CHECK(changedDrawn == 0, "%d texels the first replay drew changed", changedDrawn);
    CHECK(off.fills == 0, "the replay with the filler off drew %u fill passes", off.fills);
    if (active) {
        CHECK(cracksOff > 0, "no crack without the filler at %gx%g", (double)sx, (double)sy);
        CHECK(cracksOn == 0, "%d cracks with the filler", cracksOn);
        CHECK(badFrom == 0 && badDepth == 0,
              "%d filled texels not a neighbour's colour, %d not its depth", badFrom, badDepth);
        CHECK(on.fills == 1, "%u fill passes", on.fills);
        CHECK(c1.counted - c0.counted == 1 && c1.filled - c0.filled == 1 &&
                  c1.pixels - c0.pixels == (uint64_t)changed,
              "counted %llu pixels over %llu replays (%llu filled), %d texels changed",
              (unsigned long long)(c1.pixels - c0.pixels),
              (unsigned long long)(c1.counted - c0.counted),
              (unsigned long long)(c1.filled - c0.filled), changed);
        CHECK(c1.rx == rx && c1.ry == ry, "the reach %ux%u, %ux%u expected", c1.rx, c1.ry, rx, ry);
    } else {
        CHECK(on.fills == 0 && changed == 0 && c1.filled == c0.filled,
              "not drawn here: %u fill passes, %d texels changed, %llu replays filled", on.fills,
              changed, (unsigned long long)(c1.filled - c0.filled));
        if (msaa) {
            CHECK(c1.skipped > c0.skipped, "the MSAA replay not counted as left unfilled");
        }
    }
    /* the reach: ceil(scale / 2) above 1x, at most 3 */
    CHECK(rd__crack_fill_reach(1.0f) == 0u && rd__crack_fill_reach(0.5f) == 0u &&
              rd__crack_fill_reach(1.154f) == 1u && rd__crack_fill_reach(2.0f) == 1u &&
              rd__crack_fill_reach(2.8125f) == 2u && rd__crack_fill_reach(4.0f) == 2u &&
              rd__crack_fill_reach(6.0f) == 3u && rd__crack_fill_reach(16.0f) == 3u,
          "rd__crack_fill_reach");
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    free(off.px);
    free(off.z);
    free(on.px);
    free(on.z);
    rd_shutdown();
    if (failures) {
        printf("rd_crack_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_crack_test: ok\n");
    return 0;
}
