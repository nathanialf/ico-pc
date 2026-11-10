/* rd_display_change_2x_test: a live switch from the Original preset at 1x
 * to Enhanced at a whole 2x while the game runs, the case of the Intel HD
 * 530 crash (issue 123's report, v0.11.1: igvk64.dll inside
 * vkCmdBeginRendering in the first picture after Settings changed 1x to 2x;
 * port/rhi/rhi_pass_row.h).  rd_display_change switches at a fractional
 * scale with none of the passes that are new at 2x; this frame holds them:
 *
 *   list 0   SCENE cleared with its depth, VU meshes (normal_c, code 32)
 *            drawn through the vertex pre-pass with its record stored by
 *            the vertex shaders (ICO_RD_VU_PREPASS=1,
 *            ICO_RD_VU_RECORD=stores: the store passes, without attachments
 *            before v0.11.3, now on their throwaway target,
 *            rd_vu_rec_sink.h), world triangles writing the alpha, then a
 *            DATE-tested sprite over them (the DATE snapshot, or the
 *            stencil bit on a tile-based profile)
 *   list 3   the shadow count: its scene-sized target on SCENE's depth, a
 *            reset, a volume, the resolve
 *   list 4   the fog over SCENE's depth (above 1x under Enhanced the crack
 *            fill is armed by it and drawn after lists 4-6)
 *   list 11  UI sprites; list 12 the reduction into DISPLAY
 *
 * With the renderer's own thread (rd_thread.h, as the window runs it): three
 * frames at Original 1x (the start-up set made first, as the window does),
 * the thread stopped and its records drained, then the thread again,
 * Enhanced at scene scale 2 (output 960 x 720, the reporter's window) and
 * eight frames, the thread stopped.  Of the replays after the switch:
 * at least two recorded; every part of the frame drawn (the pre-pass's
 * passes, a DATE snapshot or stencil draw, the shadow count's resolve, the
 * crack fill in at least one); the scene at 2 x 2.  The store passes all
 * began on the throwaway target (none without it) where the record is
 * stored by the vertex shaders; the render pass breadcrumb names the last
 * pass begun (ended, a pass number, a pass ring marker) on the path asked
 * (--legacy: VkRenderPass objects, [dev] legacy_passes or the 1.1 profile).
 * Under the validation layer (the render tests' environment when the
 * toolchain has it) no validation error from the first frame to the end.
 *
 * Exit 77 without a Vulkan device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_thread.h"
#include "rd_vu_rec_sink.h"
#include "rd_vu_record.h"
#include "rhi_pass_crumb.h"
#include "vk/rhi_vk.h"

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define W 512
#define H 448
#define MESHES 4
#define MESH_VERTS 300
#define QPV 3 /* normal_c: XYZ, ST (w = strip flag 1), RGBA */
#define DRAWS 16
#define FRAMES_1X 3
#define FRAMES_2X 8

static RdMesh s_mesh[MESHES];
static RdTex s_tex[2];

static const uint8_t kRed[4] = {220, 30, 10, 0x80}, kGreen[4] = {20, 200, 40, 0x10},
                     kGrey[4] = {128, 128, 128, 0x80};

/* ------------------------------------------------------------ the frame */

static void meshQw(float (*qw)[4], uint32_t seed)
{
    memset(qw, 0, sizeof(float) * 4 * (1 + MESH_VERTS * QPV));
    uint32_t tag = MESH_VERTS | 0x8000u; /* NLOOP, EOP */
    memcpy(qw[0], &tag, 4);
    for (uint32_t v = 0; v < MESH_VERTS; v++) {
        float *p = qw[1 + v * QPV];
        p[0] = (float)((v * 37u + seed * 11u) % 400u) - 200.0f;
        p[1] = (float)((v * 53u + seed * 7u) % 300u) - 150.0f;
        p[2] = 1000.0f + (float)(v % 17u);
        p[3] = 1.0f;
        float *st = qw[1 + v * QPV + 1];
        st[0] = (float)(v % 8u) / 8.0f;
        st[1] = (float)(v % 5u) / 5.0f;
        st[2] = 1.0f;
        st[3] = 1.0f;
        float *c = qw[1 + v * QPV + 2];
        c[0] = c[1] = c[2] = 128.0f;
        c[3] = 128.0f;
    }
}

static void makeAssets(void)
{
    static float qw[1 + MESH_VERTS * QPV][4];
    for (int i = 0; i < MESHES; i++) {
        meshQw(qw, (uint32_t)i);
        RdVuBatchDesc b = {0};
        RdVuMeshDesc d;
        memset(&d, 0, sizeof(d));
        d.qw = (const float (*)[4])qw;
        d.qwCount = 1 + MESH_VERTS * QPV;
        d.qwPerVertex = QPV;
        d.batches = &b;
        d.batchCount = 1;
        d.debugName = "display change 2x mesh";
        s_mesh[i] = rd_create_vu_mesh(&d);
    }
    static uint8_t px[64 * 64 * 4];
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 64 * 64; i++) {
            px[i * 4 + 0] = (uint8_t)(i * (t + 1));
            px[i * 4 + 1] = (uint8_t)(i >> 4);
            px[i * 4 + 2] = (uint8_t)(t * 60);
            px[i * 4 + 3] = (uint8_t)((i & 1) ? 0x80 : 0x40);
        }
        s_tex[t] = rd_create_texture(64, 64, px, RD_TEXA_80_80, "display change 2x texture");
    }
}

/* a vertex at (x, y) in 1/16 GS pixels from the frame's top-left */
static RdScreenVtx vtx(int32_t x, int32_t y, uint32_t z, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - W / 2) * 16 + x;
    v.y = (2048 - H / 2) * 16 + y;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

static void lutOf(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255);
}

static void frame(void)
{
    static const uint8_t bg[4] = {16, 24, 32, 0x80};
    static uint8_t lut[256 * 4];
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();

    /* list 0: the VU meshes (through the pre-pass), triangles writing the
       alpha, a DATE-tested sprite */
    rd_select_list(0);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_clear_target(scene, bg, 1, 0);
    rd_z_write(1);
    rd_test_gs(0x50000);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    for (int i = 0; i < DRAWS; i++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[4][0] = 1.0f;
        d.vu.mem[5][1] = 1.0f;
        d.vu.mem[6][2] = 1.0f;
        d.vu.mem[7][0] = 2048.0f;
        d.vu.mem[7][1] = 2048.0f;
        d.vu.mem[7][3] = 1.0f;
        d.batchCount = 1;
        rd_texture(s_tex[i & 1], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_draw_vu_mesh(s_mesh[i % MESHES], &d, RD_KEY(1, (uint32_t)i, 0));
    }
    rd_texture_off();
    rd_abe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    {
        /* two depths, one alpha with its MSB and one without (the DATE
           test below passes on one) */
        RdScreenVtx t[6] = {vtx(20 * 16 + 3, 20 * 16 + 7, 0x40000000u, kRed, 0, 0),
                            vtx(260 * 16 + 9, 60 * 16 + 1, 0x40000000u, kRed, 0, 0),
                            vtx(90 * 16 + 5, 240 * 16 + 11, 0x40000000u, kRed, 0, 0),
                            vtx(150 * 16 + 1, 40 * 16 + 5, 0x60000000u, kGreen, 0, 0),
                            vtx(420 * 16 + 13, 120 * 16 + 2, 0x60000000u, kGreen, 0, 0),
                            vtx(200 * 16 + 7, 260 * 16 + 9, 0x60000000u, kGreen, 0, 0)};
        rd_screen_prims(RD_PRIM_TRIANGLES, t, 6, RD_SPACE_WORLD, 1, RD_KEY(2, 0, 0));
    }
    rd_test_gs(0x3C000); /* DATE=1 DATM=1, Z ALWAYS */
    rd_z_write(0);
    {
        RdScreenVtx s[2] = {vtx(10 * 16, 10 * 16, 0, kGrey, 8.0f, 8.0f),
                            vtx(500 * 16, 290 * 16, 0, kGrey, 264.0f, 264.0f)};
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture(s_tex[0], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_WORLD, 1, RD_KEY(3, 0, 0));
        rd_texture_off();
    }

    /* list 3: the shadow count (Shadow.c's: the count's target on SCENE's
       depth, a reset, a volume, the resolve; rd_msaa_test's) */
    rd_select_list(3);
    {
        const RdTarget cnt = rd_shadow_count_target(W, H);
        rd_set_target(cnt, scene, W, H, 0);
        rd_z_write(0);
        rd_test_gs(0x30000);
        rd_abe(0);
        rd_texture_off();
        rd_shadow_reset();
        rd_test_gs(RD_TEST_Z_GEQUAL);
        rd_blend_func(RD_BLEND_CS_FIX_ADD_CD, 0x80);
        rd_col_clamp(0);
        RdScreenVtx t[3] = {vtx(100 * 16, 100 * 16, 0x30000000u, kGrey, 0, 0),
                            vtx(220 * 16, 110 * 16, 0x30000000u, kGrey, 0, 0),
                            vtx(150 * 16, 230 * 16, 0x30000000u, kGrey, 0, 0)};
        const int8_t sign[1] = {1};
        rd_shadow_tris(t, sign, 1, RD_KEY(4, 0, 0));
        rd_shadow_resolve();
        rd_col_clamp(1);
    }

    /* list 4: the fog over SCENE's depth (ZFog.c fogHostDraw's state) */
    rd_select_list(4);
    rd_set_target(scene, scene, W, H, 0);
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
    {
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.lut = lut;
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
        pp.z = 0xFFFFFF;
        pp.rect[0] = (float)(0x8000 - W * 8);
        pp.rect[1] = (float)(0x8000 - H * 8);
        pp.rect[2] = (float)(0x8000 + W * 8);
        pp.rect[3] = (float)(0x8000 + H * 8);
        pp.uv[0] = pp.uv[1] = 8.0f;
        pp.uv[2] = (float)(8 + W * 16);
        pp.uv[3] = (float)(8 + H * 16);
        rd_post(RD_POST_FOG, &pp);
    }
    rd_abe(0);

    /* list 11: UI sprites; list 12: the reduction */
    rd_select_list(11);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_test_gs(0x30000);
    for (int i = 0; i < 12; i++) {
        RdScreenVtx s[2] = {
            vtx((10 + i * 40) * 16, 300 * 16, 0xFFFFFFFFu, kGrey, 0.0f, 0.0f),
            vtx((40 + i * 40) * 16, 316 * 16, 0xFFFFFFFFu, kGrey, 1024.0f, 1024.0f)};
        rd_texture(s_tex[i & 1], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_UI, 1, RD_KEY(5, (uint32_t)i, 0));
    }
    rd_texture_off();
    rd_select_list(12);
    {
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.src = scene;
        pp.dst = rd_target(RD_TARGET_DISPLAY);
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
        rd_post(RD_POST_REDUCTION, &pp);
    }
    rd_end_frame(0); /* replayed (interpolation is off) */
}

/* ------------------------------------------------------------ the records */

typedef struct Sum {
    uint32_t n;
    uint32_t prePasses, preFallbacks, dates, shadowResolves, crackFills, passes;
} Sum;

/* the records rd_perf_pop hands out of replays after `after` */
static void drain(Sum *s, uint32_t after)
{
    RdPerfRecord r;
    while (rd_perf_pop(&r)) {
        if (r.replay <= after) {
            continue;
        }
        s->n++;
        s->prePasses += r.vuPrePasses;
        s->preFallbacks += r.vuPreFallbacks;
        s->dates += r.dateSnapshots + r.dateStencilDraws;
        s->shadowResolves += r.shadowOne + r.shadowSeven;
        s->crackFills += r.crackFillPasses;
        s->passes += r.renderPasses;
    }
}

static void settings(RdSettings *st, bool twoX)
{
    memset(st, 0, sizeof(*st));
    st->outputWidth = twoX ? 960 : 640;
    st->outputHeight = twoX ? 720 : 480;
    st->aspect = 4.0f / 3.0f;
    st->preset = twoX ? RD_PRESET_ENHANCED : RD_PRESET_ORIGINAL;
    st->sceneScale = twoX ? 2.0f : 1.0f;
}

int main(int argc, char **argv)
{
    const bool legacy = argc > 1 && strcmp(argv[1], "--legacy") == 0;
    printf("rd_display_change_2x_test%s\n", legacy ? " --legacy" : "");
    rd__set_not_implemented_fatal(true);
    RdSettings st;
    settings(&st, false);
    if (!rd_init(W, H, &st, NULL)) {
        printf("rd_display_change_2x: SKIP: no device\n");
        return 77;
    }
    if (rhi_backend() != RHI_BACKEND_VULKAN) {
        printf("rd_display_change_2x: SKIP: not Vulkan (the breadcrumb and the legacy path are "
               "Vulkan's)\n");
        rd_shutdown();
        return 77;
    }
    const bool vl = rhi_limits()->validationLayer;
    const uint32_t errors0 = rhi_vk_validation_error_count();
    CHECK(vkr_test_legacy_passes() == legacy, "render passes %s, %s asked",
          vkr_test_legacy_passes() ? "legacy" : "dynamic", legacy ? "legacy" : "dynamic");
    const bool stores = g_rdRec.mode == RD_VU_RECORD_STORES;
    printf("device: render passes %s, VU record %s, validation layer %s\n",
           vkr_test_legacy_passes() ? "legacy (VkRenderPass)" : "dynamic", g_rdRec.why,
           vl ? "loaded" : "not loaded");
    makeAssets();
    rd_precreate_pipelines(); /* as the window does after rd_init */

    /* Original 1x with the render thread */
    bool thread = rd_thread_local_start(RD_THREAD_ON);
    CHECK(thread, "the renderer's own thread did not start (1x)");
    for (int i = 0; i < FRAMES_1X; i++) {
        frame();
    }
    if (thread) {
        rd_thread_local_stop();
    }
    Sum one;
    memset(&one, 0, sizeof(one));
    drain(&one, 0);
    const uint32_t sinkBefore = rd__vu_rec_sink_passes(NULL);
    const uint32_t lastAt1x = g_rdPerf.replay;
    printf("1x: %u records, %u pre-pass passes (%u VU draws the old way), %u DATE, %u shadow "
           "resolves, %u crack fills, store passes on the throwaway target %u\n",
           one.n, one.prePasses, one.preFallbacks, one.dates, one.shadowResolves, one.crackFills,
           sinkBefore);
    CHECK(one.crackFills == 0, "1x: %u crack fills (the fill is above 1x only)", one.crackFills);

    /* the live switch to Enhanced 2x, the thread running */
    thread = rd_thread_local_start(RD_THREAD_ON);
    CHECK(thread, "the renderer's own thread did not start (2x)");
    settings(&st, true);
    rd_set_settings(&st);
    for (int i = 0; i < FRAMES_2X; i++) {
        frame();
    }
    if (thread) {
        rd_thread_local_stop();
    }
    rhi_wait_idle();
    Sum two;
    memset(&two, 0, sizeof(two));
    drain(&two, lastAt1x);
    uint32_t sinkWithout = 0;
    const uint32_t sinkAfter = rd__vu_rec_sink_passes(&sinkWithout);
    printf("2x: scale %gx%g, %u records, %u passes, %u pre-pass passes (%u VU draws the old "
           "way), %u DATE, %u shadow resolves, %u crack fills, store passes on the throwaway "
           "target %u (%u without it)\n",
           (double)g_rd.sceneSx, (double)g_rd.sceneSy, two.n, two.passes, two.prePasses,
           two.preFallbacks, two.dates, two.shadowResolves, two.crackFills, sinkAfter - sinkBefore,
           sinkWithout);
    CHECK(g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f, "2x: scale %gx%g", (double)g_rd.sceneSx,
          (double)g_rd.sceneSy);
    CHECK(two.n >= 2, "2x: %u replays recorded after the switch, at least 2 expected", two.n);
    CHECK(two.prePasses > 0, "2x: the VU pre-pass drew no pass after the switch");
    CHECK(two.dates > 0, "2x: no DATE snapshot or stencil draw after the switch");
    CHECK(two.shadowResolves > 0, "2x: no shadow count resolve after the switch");
    CHECK(two.crackFills > 0, "2x: no crack fill pass after the switch");
    if (stores) {
        CHECK(sinkAfter > sinkBefore && sinkWithout == 0,
              "2x: %u store passes on the throwaway target after the switch, %u without it",
              sinkAfter - sinkBefore, sinkWithout);
    } else {
        printf("2x: the record is not stored by the vertex shaders here (%s): the throwaway "
               "target's check skipped\n",
               g_rdRec.why);
    }

    /* the breadcrumb of the last pass begun */
    {
        const RhiPassCrumb *pc = rhi_pass_crumb_last();
        char line[512];
        rhi_pass_crumb_format(pc, line, sizeof line);
        printf("breadcrumb: %s\n", line);
        CHECK(pc->seq > 0 && pc->phase == RHI_PASS_CRUMB_ENDED,
              "the breadcrumb: pass %llu, phase %u (ended expected)", (unsigned long long)pc->seq,
              (unsigned)pc->phase);
        CHECK(pc->marker > 0, "the breadcrumb names no pass ring marker");
        CHECK(pc->legacy == (legacy ? 1u : 0u), "the breadcrumb's path %s, %s asked",
              pc->legacy ? "legacy" : "dynamic", legacy ? "legacy" : "dynamic");
        CHECK(pc->viewMissing == 0, "the breadcrumb's last pass has a target without a view");
    }

    const uint32_t errors = rhi_vk_validation_error_count() - errors0;
    if (vl) {
        CHECK(errors == 0, "%u validation errors from the first frame to the end", errors);
    } else {
        printf("the validation layer is not loaded: the error count is not checked\n");
    }
    rd_shutdown();
    if (s_fail) {
        printf("rd_display_change_2x: %d failure(s)\n", s_fail);
        return 1;
    }
    printf("rd_display_change_2x: ok\n");
    return 0;
}
