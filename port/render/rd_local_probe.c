/* rd_local_probe.c: the desktop's local-read cost probe (rd_internal.h
 * rd__local_probe; docs/port/WORK_BUFFERS.md "The desktop's local read and
 * its probe").
 *
 * The local reads (the fog's depth read inside the scene pass, DATE
 * through the stencil bit, the motion blur's destination read; RdProfile
 * fogLocal, dateStencil, fxDstLocal) draw the same bytes as the snapshot
 * paths (rd_date_selftest.c, rd_fog_path.c and rd_fx_plan.c check that on
 * the device; build-host/tmp/v0710/x2-ab.sh on the corpus) and save a
 * picture's DATE snapshot passes, the fog's depth transitions and pass
 * breaks and the motion blur's copy of SCENE.  What they may cost an
 * immediate-mode GPU: SCENE's colour and depth sit in the local-read
 * layout (VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, vk_cmd.c) for the whole
 * scene pass and are made with the input-attachment usage, which a driver
 * may answer by giving up their compression for lists 0-2.  Nothing says
 * which is the larger on a given desktop GPU, so the desktop class (the
 * profile's desktopLocal) measures it once a renderer, after the
 * self-tests passed, on SCENE at its real size:
 *
 *   the frame  list 0: SCENE cleared, then LP_LAYERS full-frame layers of
 *              16 x 16 sprites, each nearer than the last (Z test GEQUAL,
 *              Z written), half the cells with alpha 0x80; list 3:
 *              LP_DATES full-frame DATE sprites (the shadow composites'
 *              form, DATM 0 and 1 alternating); list 4: the fog
 *              (RD_POST_FOG with a table, as ZFog.c records it); list 7:
 *              the motion blur's sprite (ALPHA LERP FIX 0x20 over the
 *              whole frame, as rd_fx_plan.c's self-test draws it).
 *   the runs   blocks of LP_PER_BLOCK replays with the local reads (the
 *              flags as the self-tests left them, the fog's path as the
 *              fog's self-test chose it) and with the snapshots (the three
 *              flags off, the fog through offPath), SCENE's textures made
 *              again for each block (rd__scene_textures_remake: the usages
 *              follow the flags), after one untimed replay; a second round
 *              only while the first took under LP_BUDGET_MS of the CPU's
 *              clock.  MSAA is held off (g_rd.msaaBlocked: the local reads
 *              never draw inside the MSAA epoch), the fog's own probes do
 *              not take the replays (rd__fog_timing), the fog draws as in a
 *              game frame (the crack fill before it above 1x under
 *              Enhanced).
 *   the time   each replay's GPU time from its timestamps (RdPerfRecord.
 *              gpuMs, first stamp to the last list's), read once
 *              RHI_FRAMES_IN_FLIGHT more replays (a clear) brought them in
 *              (rd__perf_gpu_ms_of); the least of each way.
 *   the rule   the local reads stay unless their least time is more than
 *              RD_LOCAL_PROBE_TOLERANCE (5 %) above the snapshots' least
 *              (rd__local_probe_keep).  Off: the three flags off
 *              ("(probe)", rd__profile_probe_off), every named target made
 *              again without the usages (rd__targets_remake), and the
 *              caller chooses the fog's depth path again.
 *
 * One line, after which the summary's words carry the verdict (the words
 * up to the colon):
 *   rd: profile: local read on the desktop: kept (probe 0.412 vs 0.530 ms):
 *       the local reads vs the snapshots, the least of 4 and 4 replays of
 *       its frame on SCENE 688x448 at 1376x896 texels; kept unless more
 *       than 5 % slower; 9.8 ms
 *   rd: profile: local read on the desktop: off (probe: slower, 0.611 vs
 *       0.530 ms): ...
 *   rd: profile: local read on the desktop: kept (not probed: no GPU
 *       timestamps)
 * and before it, where the probe turns them off, "rd: profile: fog local
 * off (probe)" for each flag that was on.
 * Not run: on any other class (a tile-based GPU keeps its default; D3D12,
 * D3D11 and OpenGL have no local read), without a window unless
 * ICO_RD_LOCAL_PROBE=1 (rd_profile.c: the replay tool and the tests keep
 * the snapshots), with ICO_RD_LOCAL_PROBE=0 (the local reads unprobed),
 * when a [dev] key set any of the three flags, or when the self-tests
 * turned all three off.  ICO_RD_LOCAL_PROBE_SABOTAGE=slow counts the local
 * reads' time 4 times, fast a quarter (ctests rd_local_probe_off and
 * rd_local_probe_kept: both verdicts' paths on lavapipe). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

#define LP_CELLS 16
#define LP_LAYERS 8
#define LP_DATES 3
#define LP_ROUNDS 2
#define LP_PER_BLOCK 2
#define LP_BUDGET_MS 20.0
#define LP_MAX (LP_ROUNDS * LP_PER_BLOCK)
#define LP_FIX 0x20u

/* GS TEST: ZTE, ZTST ALWAYS, DATE, DATM 1 (rd_date_selftest.c) */
#define LP_TEST_DATM1 0x3C000u

static const uint8_t kClear[4] = {10, 20, 30, 0x00};
static const uint8_t kDate[LP_DATES][4] = {
    {250, 250, 10, 0x40}, {10, 250, 250, 0x60}, {120, 60, 200, 0x20}};
static const uint8_t kBlur[4] = {250, 10, 128, 0x40};

bool rd__local_probe_keep(double localMs, double snapMs)
{
    if (!(localMs > 0.0) || !(snapMs > 0.0)) {
        return true; /* nothing measured (or NaN): the default stays */
    }
    return localMs <= snapMs * (1.0 + RD_LOCAL_PROBE_TOLERANCE);
}

/* a sprite from GS pixel (x0, y0) to (x1, y1) of a w x h frame at GS Z z */
static void sprite(RdScreenVtx v[2], uint32_t w, uint32_t h, uint32_t x0, uint32_t y0, uint32_t x1,
                   uint32_t y1, uint32_t z, const uint8_t rgba[4])
{
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, 2 * sizeof(*v));
    v[0].x = (ox + (int32_t)x0) * 16;
    v[0].y = (oy + (int32_t)y0) * 16;
    v[1].x = (ox + (int32_t)x1) * 16;
    v[1].y = (oy + (int32_t)y1) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].z = z;
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
}

static bool pushSprites(RdFrame *f, int list, const RdScreenVtx *v, uint32_t n)
{
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * sizeof(*v)));
    c->u[1] = n;
    return true;
}

static bool clearOf(RdFrame *f, uint32_t scene)
{
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    return c != NULL;
}

/* list 0: the layers */
static bool buildLayers(RdFrame *f, uint32_t w, uint32_t h)
{
    bool ok = rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_push1(f, 0, RDC_ABE, 0) &&
              rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
              rd__st_push1(f, 0, RDC_SHADE, 0);
    static RdScreenVtx v[LP_CELLS * LP_CELLS * 2];
    for (uint32_t layer = 0; ok && layer < LP_LAYERS; layer++) {
        ok = rd__st_test(f, 0, layer ? RD_TEST_Z_GEQUAL : RD_TEST_Z_ALWAYS);
        for (uint32_t cell = 0; cell < LP_CELLS * LP_CELLS; cell++) {
            const uint32_t cx = cell % LP_CELLS, cy = cell / LP_CELLS;
            const uint8_t rgba[4] = {
                (uint8_t)(cell * 53u + layer * 17u), (uint8_t)(cell * 101u + 71u),
                (uint8_t)(cell * 29u + 200u + layer), ((cx ^ cy) & 1u) ? 0x80 : 0x00};
            /* each layer's fog index band above the last's: (layer + 1) *
               28 plus the column, 0x8000 into it */
            const uint32_t z = (((layer + 1u) * 28u + cx) << 16) | 0x8000u;
            sprite(&v[cell * 2], w, h, cx * w / LP_CELLS, cy * h / LP_CELLS,
                   (cx + 1) * w / LP_CELLS, (cy + 1) * h / LP_CELLS, z, rgba);
        }
        ok = ok && pushSprites(f, 0, v, LP_CELLS * LP_CELLS * 2);
    }
    return ok;
}

/* list 3: the DATE composites */
static bool buildDate(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    bool ok = rd__st_target(f, 3, scene, w, h) && rd__st_push(f, 3, RDC_TEXTURE_OFF) &&
              rd__st_push1(f, 3, RDC_ABE, 0) && rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF) &&
              rd__st_push1(f, 3, RDC_FBA, 0) && rd__st_push1(f, 3, RDC_SHADE, 0);
    for (int k = 0; ok && k < LP_DATES; k++) {
        RdScreenVtx v[2];
        sprite(v, w, h, 0, 0, w, h, 0, kDate[k]);
        ok = rd__st_test(f, 3, (k & 1) ? (uint64_t)RD_TEST_DATE0 : (uint64_t)LP_TEST_DATM1) &&
             pushSprites(f, 3, v, 2);
    }
    return ok;
}

/* list 4: the fog (rd_fog_path.c stBuildFrame's, ZFog.c fogHostDraw) */
static bool buildFog(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    const RdTex depthView = rd_target_texture((RdTarget){scene}, RD_VIEW_DEPTH);
    bool ok = depthView.id && rd__st_target(f, 4, scene, w, h) && rd__st_push1(f, 4, RDC_PABE, 0);
    RdCmd *c = ok ? rd__st_push(f, 4, RDC_ALPHA) : NULL;
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 128;
    }
    c = c ? rd__st_push(f, 4, RDC_TEXTURE) : NULL;
    if (c) {
        c->u[0] = depthView.id;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    ok = c && rd__st_push1(f, 4, RDC_ZWRITE, RD_ZWRITE_OFF) && rd__st_test(f, 4, RD_TEST_Z_GEQUAL);
    c = ok ? rd__st_push(f, 4, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = RD_FILTER_NEAREST;
        c->b[1] = RD_FILTER_NEAREST;
    }
    ok = c && rd__st_push1(f, 4, RDC_ABE, 1) && rd__st_push1(f, 4, RDC_SHADE, 0);
    uint8_t lut[256 * 4];
    for (int i = 0; i < 256; i++) {
        lut[i * 4 + 0] = (uint8_t)i;
        lut[i * 4 + 1] = (uint8_t)(255 - i);
        lut[i * 4 + 2] = (uint8_t)(i * 7);
        lut[i * 4 + 3] = (uint8_t)(0x10 + i * 0x70 / 255);
    }
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    p.rgba[0] = p.rgba[1] = p.rgba[2] = 0x80;
    p.rgba[3] = 0x80;
    p.z = 0xFFFFFF;
    p.rect[0] = (float)(0x8000 - (int32_t)w * 8);
    p.rect[1] = (float)(0x8000 - (int32_t)h * 8);
    p.rect[2] = (float)(0x8000 + (int32_t)w * 8);
    p.rect[3] = (float)(0x8000 + (int32_t)h * 8);
    p.uv[0] = 8.0f;
    p.uv[1] = 8.0f;
    p.uv[2] = (float)(8 + w * 16);
    p.uv[3] = (float)(8 + h * 16);
    p.lutOffset = rd__frame_payload(f, lut, sizeof(lut));
    c = ok ? rd__st_push(f, 4, RDC_POST_STUB) : NULL;
    if (c) {
        c->b[0] = RD_POST_FOG;
        c->u[1] = rd__frame_payload(f, &p, (uint32_t)sizeof(p));
        c->u[2] = (uint32_t)sizeof(p);
    }
    return c != NULL;
}

/* list 7: the motion blur's sprite (rd_fx_plan.c stBuild's) */
static bool buildBlur(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    bool ok = rd__st_target(f, 7, scene, w, h) && rd__st_push(f, 7, RDC_TEXTURE_OFF) &&
              rd__st_push1(f, 7, RDC_ABE, 0) && rd__st_test(f, 7, RD_TEST_Z_ALWAYS) &&
              rd__st_push1(f, 7, RDC_ZWRITE, RD_ZWRITE_OFF) && rd__st_push1(f, 7, RDC_FBA, 0) &&
              rd__st_push1(f, 7, RDC_PABE, 0) && rd__st_push1(f, 7, RDC_COLCLAMP, 1) &&
              rd__st_push1(f, 7, RDC_SHADE, 0);
    RdCmd *b = ok ? rd__st_push(f, 7, RDC_BLEND) : NULL;
    if (b) {
        b->b[0] = RD_BLEND_LERP_FIX;
        b->b[1] = (uint8_t)LP_FIX;
        b->b[2] = 1;
    }
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    memcpy(p.rgba, kBlur, 4);
    p.exactInt = 1;
    p.rect[0] = (float)(ox * 16);
    p.rect[1] = (float)(oy * 16);
    p.rect[2] = (float)((ox + (int32_t)w) * 16);
    p.rect[3] = (float)((oy + (int32_t)h) * 16);
    p.scalar[0] = p.scalar[1] = 1024.0f;
    p.scalar[2] = 1.0f;
    p.lutOffset = ~0u;
    RdCmd *s = b ? rd__st_push(f, 7, RDC_POST_STUB) : NULL;
    if (s) {
        s->b[0] = (uint8_t)RD_POST_MOTION_BLUR;
        s->u[1] = rd__frame_payload(f, &p, (uint32_t)sizeof(p));
        s->u[2] = (uint32_t)sizeof(p);
    }
    return s != NULL;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    return rd__st_target(f, 0, scene, w, h) && clearOf(f, scene) && buildLayers(f, w, h) &&
           buildDate(f, scene, w, h) && buildFog(f, scene, w, h) && buildBlur(f, scene, w, h);
}

/* the replays that bring the timed ones' stamps in: SCENE cleared */
static bool buildFlush(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    return rd__st_target(f, 0, scene, w, h) && clearOf(f, scene);
}

/* the way a block replays: the three flags and the fog's depth path */
typedef struct LpWay {
    bool fog, date, fx;
    int path;
} LpWay;

static void setWay(const LpWay *w)
{
    g_rd.profile.fogLocal = w->fog;
    g_rd.profile.dateStencil = w->date;
    g_rd.profile.fxDstLocal = w->fx;
    rd__set_fog_path(w->path);
}

/* the sabotage factor on the local reads' time (1: none) */
static double sabotage(void)
{
    const char *e = getenv("ICO_RD_LOCAL_PROBE_SABOTAGE");
    if (e && strcmp(e, "slow") == 0) {
        return 4.0;
    }
    if (e && strcmp(e, "fast") == 0) {
        return 0.25;
    }
    return 1.0;
}

/* "kept (not probed: ...)" and its line */
static int notProbed(const char *why)
{
    char words[96];
    snprintf(words, sizeof words, "kept (not probed: %s)", why);
    rd__profile_local_words(words);
    rd__log("profile: local read on the desktop: %s", words);
    return 1;
}

int rd__local_probe(int offPath)
{
    RdProfile *p = &g_rd.profile;
    if (!g_rd.inited || !g_rd.hasDevice || !p->localProbe) {
        return -1;
    }
    if (!(p->fogLocal || p->dateStencil || p->fxDstLocal)) {
        rd__profile_local_words("off (self-test)");
        return -1; /* the self-tests turned them off: nothing to time */
    }
    if (!rhi_timestamps_supported() || !rd__perf_list_stamps_on()) {
        return notProbed(rhi_timestamps_supported() ? "[dev] perf_list_stamps = false"
                                                    : "no GPU timestamps");
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM) {
        return notProbed("no SCENE target to run it on");
    }
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    static RdFrame f, flush;
    if (!buildFrame(&f, scene, t->w, t->h) || !buildFlush(&flush, scene, t->w, t->h)) {
        rd__frame_free(&f);
        rd__frame_free(&flush);
        return notProbed("could not build its frame");
    }
    const double factor = sabotage();
    if (factor != 1.0) {
        rd__log("profile: ICO_RD_LOCAL_PROBE_SABOTAGE: the local reads' time counted %g times",
                factor);
    }
    const LpWay ways[2] = {{p->fogLocal, p->dateStencil, p->fxDstLocal, g_rd.fogPath},
                           {false, false, false, offPath}};
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* no MSAA epoch: the local reads' own draws */
    rd__fog_timing(true);
    const double t0 = rd__now_ms();
    uint32_t ids[2][LP_MAX];
    uint32_t n[2] = {0, 0};
    bool ok = rd__replay_frame(&f, 0, false); /* untimed: the pipelines made, the GPU awake */
    for (int round = 0; ok && round < LP_ROUNDS; round++) {
        if (round > 0 && rd__now_ms() - t0 > LP_BUDGET_MS) {
            break;
        }
        for (int way = 0; ok && way < 2; way++) {
            setWay(&ways[way]);
            /* SCENE made for this way (the usages follow the flags); the
               first block draws on SCENE as the renderer made it */
            ok = (round == 0 && way == 0) || rd__scene_textures_remake();
            for (int k = 0; ok && k < LP_PER_BLOCK; k++) {
                ok = rd__replay_frame(&f, 0, false);
                ids[way][n[way]++] = g_rdPerf.replay;
            }
        }
    }
    /* the last blocks' stamps come in RHI_FRAMES_IN_FLIGHT replays later */
    for (int k = 0; ok && k < RHI_FRAMES_IN_FLIGHT; k++) {
        ok = rd__replay_frame(&flush, 0, false);
    }
    const double wallMs = rd__now_ms() - t0;
    rd__fog_timing(false);
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    rd__frame_free(&flush);
    double best[2] = {INFINITY, INFINITY};
    uint32_t got[2] = {0, 0};
    for (int way = 0; way < 2; way++) {
        for (uint32_t k = 0; k < n[way]; k++) {
            double ms;
            if (rd__perf_gpu_ms_of(ids[way][k], &ms) && ms > 0.0) {
                best[way] = ms < best[way] ? ms : best[way];
                got[way]++;
            }
        }
    }
    const double localMs = got[0] ? best[0] * factor : 0.0, snapMs = got[1] ? best[1] : 0.0;
    const bool measured = ok && got[0] > 0 && got[1] > 0;
    const bool keep = !measured || rd__local_probe_keep(localMs, snapMs);
    if (keep) {
        /* the local reads back, SCENE made for them again (the last block
           was the snapshots') */
        setWay(&ways[0]);
        (void)rd__scene_textures_remake();
        if (!measured) {
            return notProbed(ok ? "the timed replays brought no GPU times"
                                : "a timed replay failed");
        }
    } else {
        setWay(&ways[0]);
        rd__profile_probe_off(); /* the flags off, "(probe)" */
        rd__set_fog_path(offPath);
        rd__targets_remake(); /* every named target without the usages */
    }
    char words[96];
    if (keep) {
        snprintf(words, sizeof words, "kept (probe %.3f vs %.3f ms)", localMs, snapMs);
    } else {
        snprintf(words, sizeof words, "off (probe: slower, %.3f vs %.3f ms)", localMs, snapMs);
    }
    rd__profile_local_words(words);
    t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    rd__log("profile: local read on the desktop: %s: the local reads vs the snapshots, the least "
            "of %u and %u replays of its frame on SCENE %ux%u at %ux%u texels; kept unless more "
            "than %d %% slower; %.1f ms",
            words, got[0], got[1], t ? t->w : 0u, t ? t->h : 0u, t ? t->tw : 0u, t ? t->th : 0u,
            (int)(RD_LOCAL_PROBE_TOLERANCE * 100.0 + 0.5), wallMs);
    return keep ? 1 : 0;
}
