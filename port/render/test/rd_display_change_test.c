/* rd_display_change_test: a display change while the game runs (rd_core.c
 * rd_begin_frame's settings, rd_pipeline.c rd__display_pipelines).  On a
 * PowerVR DXT-48 phone: Original -> Enhanced at 2364x1080 (scale 2.8125 x
 * 2.41) made the no-planes twins (vu_np_ps, rd_pixel_plan.c) on first use,
 * 640-700 ms each on the main thread, and the game stopped.
 *
 * The frame: VU meshes under linear filters (the draws whose planes drop at
 * a fractional scale), world strips, UI sprites and the reduction into
 * DISPLAY, recorded and closed twice after each state (rd_end_frame replays
 * each: interpolation off), the second replay's record and DISPLAY kept.
 *
 *   fresh     rd_init at the Original preset (1x): the record and DISPLAY
 *   switch    rd_init again at Original with the start-up set
 *             (rd_precreate_pipelines, as the window does), then the
 *             Enhanced preset at an output whose box gives a fractional
 *             scale on both axes: the frame's rd_begin_frame makes the new
 *             state's missing pipelines (where vu_np_ps is loaded: some),
 *             and neither replay after it makes one (RdPerfRecord.
 *             pipelineCreates 0, rd__pipeline_count unchanged); the second
 *             replay draws VU passes through vu_np_ps where it is loaded
 *   back      the Original preset at 1x again: no replay makes a pipeline;
 *             the second replay's draws, render passes, pipeline binds,
 *             blur folds, glow passes, no-planes passes, above-1x draws and
 *             crack fill passes equal the fresh start's, and DISPLAY is the
 *             fresh start's byte for byte
 *
 * Exit 77 without a device. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"

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
#define MESHES 6
#define MESH_VERTS 300
#define QPV 3 /* normal_c: XYZ, ST (w = strip flag 1), RGBA */
#define DRAWS 24
/* the Enhanced output: its box's scale is not a whole number (checked) */
#define OUT_W 900
#define OUT_H 700

static RdMesh s_mesh[MESHES];
static RdTex s_tex[4];

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

/* the meshes and textures (made again after each rd_init) */
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
        d.debugName = "display change mesh";
        s_mesh[i] = rd_create_vu_mesh(&d);
    }
    static uint8_t px[64 * 64 * 4];
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 64 * 64; i++) {
            px[i * 4 + 0] = (uint8_t)(i * (t + 1));
            px[i * 4 + 1] = (uint8_t)(i >> 4);
            px[i * 4 + 2] = (uint8_t)(t * 60);
            px[i * 4 + 3] = (uint8_t)((i & 1) ? 0x80 : 0x40);
        }
        s_tex[t] = rd_create_texture(64, 64, px, RD_TEXA_80_80, "display change texture");
    }
}

static RdScreenVtx vtx(int x, int y, uint32_t z, float s, float t, uint8_t c)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - W / 2 + x) * 16;
    v.y = (2048 - H / 2 + y) * 16;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    v.rgba[0] = v.rgba[1] = v.rgba[2] = c;
    v.rgba[3] = 0x80;
    return v;
}

/* one tick's frame after its rd_begin_frame, recorded and closed
   (rd_end_frame replays it) */
static void frameBody(void)
{
    static const uint8_t bg[4] = {16, 24, 32, 0x80};
    RdTarget scene = rd_target(RD_TARGET_SCENE);

    rd_select_list(0);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_clear_target(scene, bg, 1, 0);
    rd_test_gs(0x50000);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    /* linear filters: the draws whose planes drop at a fractional scale
       (rd_pixel_plan.c rd__planes_dropped) */
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
        rd_texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_draw_vu_mesh(s_mesh[i % MESHES], &d, RD_KEY(1, (uint32_t)i, 0));
    }
    rd_texture(s_tex[1], RD_TEXFN_MODULATE, RD_TCC_RGBA);
    for (int i = 0; i < 12; i++) {
        RdScreenVtx s[8];
        for (int k = 0; k < 8; k++) {
            s[k] = vtx(20 + i * 30 + (k / 2) * 6, 40 + (k & 1) * 30, 0x1000000u + (uint32_t)i,
                       (float)k / 8.0f, (float)(k & 1), 100);
        }
        rd_screen_prims(RD_PRIM_TRIANGLE_STRIP, s, 8, RD_SPACE_WORLD, 0, RD_KEY(2, (uint32_t)i, 0));
    }
    rd_select_list(11);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_test_gs(0x30000);
    for (int i = 0; i < 24; i++) {
        RdScreenVtx s[2] = {
            vtx(10 + (i % 12) * 40, 300 + (i / 12) * 20, 0xFFFFFFFFu, 0.0f, 0.0f, 128),
            vtx(40 + (i % 12) * 40, 316 + (i / 12) * 20, 0xFFFFFFFFu, 64.0f * 16.0f, 64.0f * 16.0f,
                128)};
        rd_texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_UI, 1, RD_KEY(3, (uint32_t)i, 0));
    }
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = scene;
    pp.dst = rd_target(RD_TARGET_DISPLAY);
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

/* ------------------------------------------------------------ the states */

typedef struct Shot {
    uint32_t beginMade;  /* pipelines the first rd_begin_frame made (a display change's) */
    RdPerfRecord rec[2]; /* the two replays' records */
    uint32_t made[2];    /* pipelines made during each frame after its rd_begin_frame */
    uint8_t *px;         /* DISPLAY after the second */
    uint32_t w, h;
} Shot;

/* two frames, each replayed by rd_end_frame: the pipelines the first
   rd_begin_frame made (where a display change applies), the records, the
   pipelines made after each rd_begin_frame (the recording and the replay),
   DISPLAY after the second */
static void twoFrames(Shot *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 2; i++) {
        const uint32_t before = rd__pipeline_count();
        rd_begin_frame();
        const uint32_t begun = rd__pipeline_count();
        if (i == 0) {
            s->beginMade = begun - before;
        }
        frameBody();
        s->rec[i] = g_rdPerf; /* the record rd_end_frame's replay closed */
        while (rd_perf_pop(NULL)) {}
        s->made[i] = rd__pipeline_count() - begun;
    }
    const size_t cap = (size_t)W * H * 4u * 4u;
    s->px = malloc(cap);
    if (s->px && !rd__read_target(rd_target(RD_TARGET_DISPLAY), s->px, cap, &s->w, &s->h)) {
        free(s->px);
        s->px = NULL;
    }
}

static void settings(RdSettings *st, bool enhanced)
{
    memset(st, 0, sizeof(*st));
    st->outputWidth = enhanced ? OUT_W : 640;
    st->outputHeight = enhanced ? OUT_H : 480;
    st->aspect = 4.0f / 3.0f;
    if (enhanced) {
        /* the "window" resolution: scale 0, no size, the output's box */
        st->preset = RD_PRESET_ENHANCED;
        st->fullHeightScene = 1;
    } else {
        st->preset = RD_PRESET_ORIGINAL;
        st->sceneScale = 1.0f;
    }
}

/* the settings changed, then two frames: the first's rd_begin_frame applies
   them (Shot.beginMade: the pipelines the change made) */
static void changeTo(bool enhanced, Shot *s)
{
    RdSettings st;
    settings(&st, enhanced);
    rd_set_settings(&st);
    twoFrames(s);
}

static void checkNoneMade(const char *what, const Shot *s)
{
    for (int i = 0; i < 2; i++) {
        CHECK(s->rec[i].pipelineCreates == 0 && s->made[i] == 0,
              "%s: frame %d made %u pipelines in its replay (%u after its rd_begin_frame) on first "
              "use",
              what, i + 1, s->rec[i].pipelineCreates, s->made[i]);
    }
}

static void checkSame(const char *what, const RdPerfRecord *a, const RdPerfRecord *b)
{
#define SAME(f) CHECK(a->f == b->f, "%s: " #f " %u, the fresh start's %u", what, b->f, a->f)
    SAME(draws);
    SAME(renderPasses);
    SAME(pipelineBinds);
    SAME(blurFolds);
    SAME(glowPasses);
    SAME(vuNoPlanes);
    SAME(a1xDraws);
    SAME(crackFillPasses);
#undef SAME
}

int main(void)
{
    rd__set_not_implemented_fatal(true);
    RdSettings st;
    settings(&st, false);

    /* fresh: the Original preset from the start */
    if (!rd_init(W, H, &st, NULL)) {
        printf("no device: skipped\n");
        return 77;
    }
    makeAssets();
    Shot fresh;
    twoFrames(&fresh);
    CHECK(fresh.px != NULL, "fresh: DISPLAY read back");
    rd_shutdown();

    /* switch: the start-up set at Original, then Enhanced at a fractional
       scale */
    if (!rd_init(W, H, &st, NULL)) {
        printf("FAIL: the second rd_init\n");
        return 1;
    }
    makeAssets();
    rd_precreate_pipelines(); /* as the window does after rd_init */
    Shot start;
    twoFrames(&start);
    checkNoneMade("the start at Original", &start);

    Shot enh;
    changeTo(true, &enh);
    const uint32_t madeEnh = enh.beginMade;
    const float sx = g_rd.sceneSx, sy = g_rd.sceneSy;
    CHECK(sx != floorf(sx) || sy != floorf(sy), "Enhanced at %ux%u: scale %gx%g, not fractional",
          OUT_W, OUT_H, (double)sx, (double)sy);
    const bool twins = g_rd.fs[RD_FS_VU_NP].id != 0 && g_rd.profile.vuPlanesFrac &&
                       g_rd.dstMode == RD_BLEND_EXACT_OFF;
    printf("Enhanced: scale %gx%g, %u pipelines made at the change; no-planes passes %u\n",
           (double)sx, (double)sy, madeEnh, enh.rec[1].vuNoPlanes);
    if (twins) {
        CHECK(madeEnh > 0, "Enhanced: the change made no pipeline (the twins were not made)");
        CHECK(enh.rec[1].vuNoPlanes > 0, "Enhanced: no VU pass drew through vu_np_ps");
    } else {
        printf("Enhanced: vu_np_ps not drawn on this device or blend: the twins' checks skipped\n");
    }
    checkNoneMade("Enhanced", &enh);

    /* back: Original at 1x */
    Shot back;
    changeTo(false, &back);
    const uint32_t madeBack = back.beginMade;
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f, "Original: scale %gx%g",
          (double)g_rd.sceneSx, (double)g_rd.sceneSy);
    printf("Original again: %u pipelines made at the change\n", madeBack);
    checkNoneMade("Original again", &back);
    checkSame("Original again", &fresh.rec[1], &back.rec[1]);
    checkSame("the start at Original", &fresh.rec[1], &start.rec[1]);
    CHECK(back.px && fresh.px && back.w == fresh.w && back.h == fresh.h,
          "Original again: DISPLAY %ux%u, the fresh start's %ux%u", back.w, back.h, fresh.w,
          fresh.h);
    if (back.px && fresh.px && back.w == fresh.w && back.h == fresh.h) {
        size_t diff = 0;
        for (size_t i = 0; i < (size_t)back.w * back.h * 4u; i++) {
            diff += back.px[i] != fresh.px[i];
        }
        CHECK(diff == 0, "Original again: %zu bytes of DISPLAY differ from the fresh start's",
              diff);
    }
    free(fresh.px);
    free(start.px);
    free(enh.px);
    free(back.px);
    rd_shutdown();
    if (s_fail) {
        printf("rd_display_change: %d failure(s)\n", s_fail);
        return 1;
    }
    printf("rd_display_change: ok\n");
    return 0;
}
