/* rd_fog_selftest_test.c: the fog's self-test and probe (rd_fog_path.c).
 *
 *   rd_fog_selftest test/rd_fog_selftest_test <path> [<w> <h> [<scale> [<aa> [<texels w> <texels h>
 *                                               [<out w> <out h> [<aspect>]]]]]]
 *
 * <texels w> <texels h> a scene of exactly that many texels (scale 0), as
 * the window's size gives; <aa> an RdAntiAlias value (default 0, off); with an MSAA count the device
 * lacks the test is skipped (77).  A scene of an odd texel count under MSAA
 * (1935x1080 for the PS2's 688x448) puts the probe's centre on a cell edge:
 * in place must still pass, and MSAA stays in force; with the
 * in-place and copy reads sabotaged (buffer taken) MSAA is off, and
 * rd_aa_supported_mask still lists it.  <out w> <out h> (texels 0 0): the
 * Enhanced window resolution in that output at 16:9, Full pixel on: SCENE
 * the window's exact pixels, the frame's exact width and a guard texel a
 * side (RdTargetRec.ox, RD_FULL_PIXEL_GUARD; rd_fog_selftest_fullpixel*:
 * 688 x 448 in 1920 x 1080).  <aspect> (default 16:9): the display aspect, so
 * a frame is cropped (624 wide at 1.6: exact 614.40) or exact (2048 at 48:9).
 *
 * On a Vulkan device (77 without one), at a wide scene (16:9, scale 2,
 * Full pixel) so the self-test runs on a SCENE whose texels are not its GS
 * pixels:
 *   1. rd_fog_selftest picks <path> (inplace on lavapipe; buffer when
 *      ICO_RD_FOG_SABOTAGE=inplace,copy makes the other two read 0) and
 *      says it passed;
 *   2. a frame recorded through the renderer's own calls (not the game's
 *      fog code) draws the self-test's grid, cell n at GS Z (n << 16) |
 *      0x8000, and fogs it: the first fogged replay records the probe, the
 *      next rd_begin_frame logs it ("fog: probe <path> ... idx=135,0,15,
 *      240,255": the centre cell, 119, 120, 135 or 136 as the centre texel
 *      falls by the cells' edges, and the four corner cells), and the fogged
 *      pixels at every cell centre equal the GS arithmetic (the probe's
 *      fog reads the exact entry, rd__fog_exact); the next fogged frame,
 *      the same draws, reads between entries above 1x (above1xDraws grows
 *      by its fog);
 *   3. the scene made again at another GS size runs the self-test again
 *      and arms the probe again (a second pair of lines).
 * The log lines themselves are matched by test/rd_fog_selftest.cmake. */
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

#define CELLS 16

/* the self-test's tables (rd_fog_path.c stLut, stCellColor) */
static void lutOf(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255);
}

static void cellColor(int c, uint8_t out[4])
{
    out[0] = (uint8_t)(c * 53 + 17);
    out[1] = (uint8_t)(c * 101 + 71);
    out[2] = (uint8_t)(c * 29 + 200);
    out[3] = 0x40;
}

static int lerp(int cs, int cd, int a)
{
    const int v = (((cs - cd) * a) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void recordFogFrame(uint32_t w, uint32_t h)
{
    static const uint8_t clear[4] = {30, 60, 90, 0x80};
    static RdScreenVtx v[CELLS * CELLS * 2];
    static uint8_t lut[256 * 4];
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, w, h, RD_TARGET_OFFSET);
    rd_clear_target(scene, clear, 1, 0);
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(1);
    rd_fba(0);
    rd_gouraud(0);
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, sizeof(v));
    for (int c = 0; c < CELLS * CELLS; c++) {
        const uint32_t cx = (uint32_t)(c % CELLS), cy = (uint32_t)(c / CELLS);
        uint8_t rgba[4];
        cellColor(c, rgba);
        for (int k = 0; k < 2; k++) {
            RdScreenVtx *p = &v[c * 2 + k];
            p->x = (ox + (int32_t)((cx + (uint32_t)k) * w / CELLS)) * 16;
            p->y = (oy + (int32_t)((cy + (uint32_t)k) * h / CELLS)) * 16;
            p->z = ((uint32_t)c << 16) | 0x8000u;
            p->q = 1.0f;
            memcpy(p->rgba, rgba, 4);
        }
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, CELLS * CELLS * 2, RD_SPACE_FULLFRAME, 0, 0);
    /* ZFog.c fogHostDraw's state and record */
    rd_select_list(4);
    rd_set_target(scene, scene, w, h, RD_TARGET_OFFSET);
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
    pp.rect[0] = (float)(0x8000 - (int32_t)w * 8);
    pp.rect[1] = (float)(0x8000 - (int32_t)h * 8);
    pp.rect[2] = (float)(0x8000 + (int32_t)w * 8);
    pp.rect[3] = (float)(0x8000 + (int32_t)h * 8);
    pp.uv[0] = pp.uv[1] = 8.0f;
    pp.uv[2] = (float)(8 + w * 16);
    pp.uv[3] = (float)(8 + h * 16);
    rd_post(RD_POST_FOG, &pp);
    rd_end_frame(0); /* replayed here: interpolation is off */
}

/* SCENE's pixel at every cell centre against the GS arithmetic */
static void checkCells(void)
{
    uint32_t tw = 0, th = 0;
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t) {
        CHECK(0, "no SCENE");
        return;
    }
    const size_t size = (size_t)t->tw * t->th * 4;
    uint8_t *px = malloc(size);
    if (!px) {
        CHECK(0, "out of memory");
        return;
    }
    rhi_wait_idle();
    const bool ok = rd__read_target(rd_target(RD_TARGET_SCENE), px, size, &tw, &th);
    CHECK(ok && tw == t->tw && th == t->th, "SCENE readback %ux%u", tw, th);
    int maxd = 0;
    for (int c = 0; ok && c < CELLS * CELLS; c++) {
        const uint32_t cx = (uint32_t)(c % CELLS), cy = (uint32_t)(c / CELLS);
        const double gx = ((double)(cx * t->w / CELLS) + (double)((cx + 1) * t->w / CELLS)) / 2;
        const double gy = ((double)(cy * t->h / CELLS) + (double)((cy + 1) * t->h / CELLS)) / 2;
        /* a Full pixel SCENE holds the frame's exact width alone: a cell
           outside it is not in the texture */
        const double tx = t->ox > 0.0f ? gx * t->sx - t->ox : gx * t->tw / t->w;
        if (tx < 0.0 || tx >= (double)t->tw) {
            continue;
        }
        const uint32_t x = (uint32_t)tx, y = (uint32_t)(gy * t->th / t->h);
        uint8_t l[4], cd[4];
        lutOf(c, l);
        cellColor(c, cd);
        const int want[4] = {lerp(l[0], cd[0], l[3]), lerp(l[1], cd[1], l[3]),
                             lerp(l[2], cd[2], l[3]), l[3]};
        const uint8_t *g = &px[((size_t)y * t->tw + x) * 4];
        for (int ch = 0; ch < 4; ch++) {
            const int d = abs((int)g[ch] - want[ch]);
            maxd = d > maxd ? d : maxd;
        }
    }
    printf("  the fogged grid on SCENE %ux%u (%ux%u texels): max difference %d LSB\n", t->w, t->h,
           t->tw, t->th, maxd);
    CHECK(maxd <= 2, "the fogged grid differs by %d LSB", maxd);
    free(px);
}

int main(int argc, char **argv)
{
    printf("rd_fog_selftest_test\n");
    if (argc < 2) {
        printf("usage: rd_fog_selftest_test inplace|copy|buffer [w h]\n");
        return 2;
    }
    int want = -1;
    for (int p = 0; p < RD_FOG_PATH_COUNT; p++) {
        if (strcmp(argv[1], rd__fog_path_name(p)) == 0) {
            want = p;
        }
    }
    CHECK(want >= 0, "path %s", argv[1]);
    const uint32_t w = argc > 3 ? (uint32_t)atoi(argv[2]) : 640;
    const uint32_t h = argc > 3 ? (uint32_t)atoi(argv[3]) : 448;
    const float scale = argc > 4 ? (float)atof(argv[4]) : 2.0f;
    const int aa = argc > 5 ? atoi(argv[5]) : 0;
    const uint32_t sceneW = argc > 7 ? (uint32_t)atoi(argv[6]) : 0;
    const uint32_t sceneH = argc > 7 ? (uint32_t)atoi(argv[7]) : 0;
    const uint32_t outW = argc > 9 ? (uint32_t)atoi(argv[8]) : 0;
    const uint32_t outH = argc > 9 ? (uint32_t)atoi(argv[9]) : 0;
    const float aspect = argc > 10 ? (float)atof(argv[10]) : 16.0f / 9.0f;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = aspect;
    st.sceneScale = sceneW || outW ? 0.0f : scale;
    st.sceneWidth = sceneW;
    st.sceneHeight = sceneH;
    st.antiAlias = (uint8_t)aa;
    st.fullPixel = 1;
    st.outputWidth = outW ? outW : 1280;
    st.outputHeight = outH ? outH : 720;
    if (!rd_init(w, h, &st, NULL)) {
        printf("rd_fog_selftest_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    CHECK(t && (scale <= 1.0f || t->tw != t->w), "a scaled, wide SCENE (%ux%u texels for %ux%u)",
          t ? t->tw : 0, t ? t->th : 0, w, h);
    if (outW) {
        CHECK(t && t->tw == outW + 2u * RD_FULL_PIXEL_GUARD && t->ox > 0.0f,
              "Full pixel: SCENE is the window's %u columns and the guard (%u, %g left out)", outW,
              t ? t->tw : 0, t ? (double)t->ox : 0.0);
    }
    if (aa > RD_AA_OFF && (rd_aa_supported_mask() & (1u << aa)) == 0u) {
        printf("rd_fog_selftest_test: SKIP: anti-aliasing %d is not offered here\n", aa);
        rd_shutdown();
        return 77;
    }

    /* 1. the self-test */
    const bool passed = rd_fog_selftest();
    printf("  self-test %s, path %s\n", passed ? "passed" : "failed",
           rd__fog_path_name(g_rd.fogPath));
    CHECK(passed, "the self-test passed");
    CHECK(g_rd.fogPath == want, "path %s, %s expected", rd__fog_path_name(g_rd.fogPath),
          rd__fog_path_name(want));
    CHECK(g_rd.depthCopy == (g_rd.fogPath != RD_FOG_INPLACE), "the depth copy with the path");
    /* the anti-aliasing and the path agree: MSAA drawn with a path that
       reads the multisampled depth, else off with the mask unchanged */
    CHECK((rd_aa_supported_mask() & (1u << aa)) != 0u, "the mask lists %d whatever the path", aa);
    if (rd_aa_samples(aa) > 1u) {
        const bool msaa = want != RD_FOG_BUFFER;
        CHECK(rd__aa_mode() == (msaa ? aa : RD_AA_OFF), "anti-aliasing in force %d, %d expected",
              rd__aa_mode(), msaa ? aa : RD_AA_OFF);
        CHECK(rd_aa_msaa_blocked() == !msaa, "blocked %d, %d expected", (int)rd_aa_msaa_blocked(),
              (int)!msaa);
    }

    /* 2. a recorded fog frame: the probe and the pixels.  The probe's fog
       reads the exact entry (rd__fog_exact), so the cells equal the GS
       arithmetic at any scale; the next fogged frame, the same draws, reads
       between entries above 1x (its fog counted in above1xDraws) */
    const bool scaled = t && (t->sx != 1.0f || t->sy != 1.0f);
    const uint32_t a0 = g_rd.above1xDraws;
    recordFogFrame(w, h);
    const uint32_t probeA1x = g_rd.above1xDraws - a0;
    CHECK(rd__fog_probe_pending(), "the fogged replay recorded the probe");
    checkCells();
    rd_begin_frame(); /* logs the probe */
    CHECK(!rd__fog_probe_pending(), "the probe read back");
    rd_end_frame(0);
    const uint32_t a1 = g_rd.above1xDraws;
    recordFogFrame(w, h);
    const uint32_t nextA1x = g_rd.above1xDraws - a1;
    CHECK(!rd__fog_probe_pending(), "one probe for each scene size");
    CHECK(scaled ? nextA1x > probeA1x : nextA1x == probeA1x,
          "above 1x draws: %u on the probe's frame, %u on the next (scaled %d)", probeA1x, nextA1x,
          (int)scaled);

    /* 3. another scene size: the self-test and the probe again */
    rd_reset_scene(w + 64, h);
    CHECK(g_rd.fogPath == want, "path %s after the new size, %s expected",
          rd__fog_path_name(g_rd.fogPath), rd__fog_path_name(want));
    recordFogFrame(w + 64, h);
    CHECK(rd__fog_probe_pending(), "the new size armed the probe");
    checkCells();
    rd_begin_frame();
    rd_end_frame(0);

    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_fog_selftest_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_fog_selftest_test: ok\n");
    return 0;
}
