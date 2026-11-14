/* rd_subpass_probe.c: the subpass read's first-start probe (rd_subpass_probe.h;
 * port/rhi/rhi_subpass_verdict.h; docs/port/RENDER.md, "The subpass read's
 * probe").
 *
 * The device decided the read when it was made (port/rhi/vk/vk_device.c
 * vkr_subpass_read_decide): kept or refused by an earlier start's verdict,
 * or on for this start's probe (RhiLimits.subpassProbe) with the mark
 * "probing" already on the disc.  DATE's self-test (rd_date_selftest.c)
 * checks DATE through the read first; then this file times it and writes
 * the verdict over the mark.  Its replays run before the game draws its
 * first picture (rd_fog_selftest's first run, after the start-up set), with
 * MSAA held off, on SCENE at its real size and on two temporary targets:
 *
 *   the read   list 0: SCENE cleared, SP_LAYERS full-frame layers of
 *              SP_CELLS x SP_CELLS sprites, each nearer than the last (Z
 *              GEQUAL, Z written), half the cells alpha 0x80 (as
 *              rd_local_probe.c's); list 3: SP_DATES full-frame DATE sprites
 *              (DATM 0, blending off, the alpha masked: no draw writes the
 *              alpha the next tests, so one stencil set-up or one R8
 *              snapshot serves the three, the count a game picture with
 *              shadow volumes takes under rd_date_one.c's sharing).  Way 0
 *              with
 *              RdProfile.dateStencil on (the set-up inside the pass), way 1
 *              with it off (the R8 snapshot, the pass ended and begun
 *              again): SCENE is the same texture both ways (the shape is
 *              the device's).
 *   the plain  four passes on X (a temporary target of SCENE's GS size, no
 *   pass       depth), each of SP_PLAIN_LAYERS full-frame layers without
 *              blending, each followed by a sprite on Y (SP_SIDE x SP_SIDE),
 *              so X is loaded and stored four times a replay.  Way 0 with
 *              X's own colour texture (made with the read's shape: the
 *              input-attachment usage, GENERAL), way 1 with a twin of its
 *              size made with RHI_TEX_NO_SUBPASS_READ (the usage and layout
 *              of a device without the read), lent to X's record for the
 *              block (its colour and state swapped in and back).
 *   the time   blocks of SP_PER_BLOCK replays a way, alternated, after one
 *              untimed replay of each frame (the pipelines made, the GPU
 *              awake); a second round only while the first took under
 *              SP_BUDGET_MS of the CPU's clock; each replay's GPU time from
 *              its timestamps (rd__perf_gpu_ms_of), read once
 *              RHI_FRAMES_IN_FLIGHT more replays brought them in; the least
 *              of each way, and the plain twin's spread (its largest over
 *              its least, minus one).
 *   the rule   rd__subpass_probe_keep (rd_subpass_probe.h).
 *
 * Lines (each once a start):
 *   rd: subpass read: kept (probe: the read 0.412 vs the snapshot 0.530 ms,
 *       a plain pass 0.210 vs 0.205 ms without the shape): the least of 4
 *       and 4, 4 and 4 replays on SCENE 912x512 at 912x512 texels; kept
 *       unless the read is more than 5 % slower or a plain pass more than 5
 *       % (its spread 1.0 %); 312.4 ms; the verdict kept for this device
 *       and driver (13b5:72120000:80200000)
 *   rd: subpass read: refused (probe: the read slower, ...): ...
 *   rd: subpass read: refused (DATE's self-test on the read: stencil fail 2
 *       at sample 1: 28C850FF for 28C85000, snapshot ok 5): ...
 *   rd: subpass read: not probed at this start (DATE draws by its R8
 *       snapshot: the profile or [dev] date_path); its mark taken away, the
 *       next start probes
 * A refusal turns DATE's stencil path off for the rest of the start
 * ("profile: DATE stencil off (probe)").  ICO_RD_SUBPASS_PROBE_SABOTAGE
 * (the tests alone): slow counts the read's time 4 times, fast a quarter,
 * plain the shaped plain pass's 4 times, end ends the process (exit 86)
 * right after DATE's self-test, as a GPU hang inside the probe would. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "attempt_ledger.h"
#include "rd_internal.h"
#include "rd_subpass_probe.h"
#include "rhi_subpass_verdict.h"

#define SP_CELLS 16
#define SP_LAYERS 8
#define SP_DATES 3
#define SP_PLAIN_PASSES 4
#define SP_PLAIN_LAYERS 2
#define SP_SIDE 64
#define SP_ROUNDS 2
#define SP_PER_BLOCK 2
#define SP_BUDGET_MS 400.0
#define SP_MAX (SP_ROUNDS * SP_PER_BLOCK)
#define SP_EXIT_END 86

static const uint8_t kClear[4] = {10, 20, 30, 0x00};
/* the DATE sprites' colours (DATM 0; their alpha masked) */
static const uint8_t kDate[SP_DATES][4] = {
    {250, 250, 10, 0x40}, {10, 250, 250, 0x60}, {120, 60, 200, 0x20}};
static const uint8_t kSmall[4] = {200, 100, 50, 0x80};

/* the ledger's words and the verdict's (the row keeps the pointer) */
static char s_why[256];

/* the window's phase hook (rd.h rd_set_probe_fn: diag_host.h
   ico_diag_set_probe): the probe's name while the device runs with its
   shape, NULL once the verdict is written or the probe is given up */
static void (*s_phase)(const char *name);

void rd_set_probe_fn(void (*fn)(const char *name))
{
    s_phase = fn;
}

static void phaseEnd(void)
{
    if (s_phase != NULL) {
        s_phase(NULL);
    }
}

RdSubpassProbeRule rd__subpass_probe_keep(double readMs, double snapMs, double shapedMs,
                                          double plainMs, double plainSpread)
{
    if (!(readMs > 0.0) || !(snapMs > 0.0) || !(shapedMs > 0.0) || !(plainMs > 0.0) ||
        !isfinite(readMs) || !isfinite(snapMs) || !isfinite(shapedMs) || !isfinite(plainMs)) {
        return RD_SUBPASS_PROBE_UNTIMED;
    }
    if (readMs > snapMs * (1.0 + RD_SUBPASS_PROBE_TOLERANCE)) {
        return RD_SUBPASS_PROBE_READ_SLOWER;
    }
    const double noise = isfinite(plainSpread) && plainSpread > RD_SUBPASS_PROBE_TOLERANCE
                             ? plainSpread
                             : RD_SUBPASS_PROBE_TOLERANCE;
    if (shapedMs > plainMs * (1.0 + noise)) {
        return RD_SUBPASS_PROBE_PLAIN_SLOWER;
    }
    return RD_SUBPASS_PROBE_KEEP;
}

/* a sprite from GS pixel (x0, y0) to (x1, y1) of a w x h target at GS Z z */
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

static bool clearOf(RdFrame *f, int list, uint32_t target, uint8_t depth)
{
    RdCmd *c = rd__st_push(f, list, RDC_CLEAR);
    if (c) {
        c->u[0] = target;
        memcpy(c->b, kClear, 4);
        c->b[4] = depth;
        c->u[1] = 0;
    }
    return c != NULL;
}

/* the list's state: no texture, no blending, no Z write, Z test word test */
static bool plainState(RdFrame *f, int list, uint64_t test, uint8_t zwrite)
{
    return rd__st_push(f, list, RDC_TEXTURE_OFF) && rd__st_push1(f, list, RDC_ABE, 0) &&
           rd__st_test(f, list, test) && rd__st_push1(f, list, RDC_ZWRITE, zwrite) &&
           rd__st_push1(f, list, RDC_FBA, 0) && rd__st_push1(f, list, RDC_SHADE, 0);
}

/* one layer of SP_CELLS x SP_CELLS sprites over a w x h target */
static bool pushLayer(RdFrame *f, int list, uint32_t w, uint32_t h, uint32_t layer, bool depthBands)
{
    static RdScreenVtx v[SP_CELLS * SP_CELLS * 2];
    for (uint32_t cell = 0; cell < SP_CELLS * SP_CELLS; cell++) {
        const uint32_t cx = cell % SP_CELLS, cy = cell / SP_CELLS;
        const uint8_t rgba[4] = {(uint8_t)(cell * 53u + layer * 17u), (uint8_t)(cell * 101u + 71u),
                                 (uint8_t)(cell * 29u + 200u + layer),
                                 ((cx ^ cy) & 1u) ? 0x80 : 0x00};
        /* each layer nearer than the last (GS Z grows towards the eye) */
        const uint32_t z = depthBands ? ((((layer + 1u) * 28u + cx) << 16) | 0x8000u) : 0u;
        sprite(&v[cell * 2], w, h, cx * w / SP_CELLS, cy * h / SP_CELLS, (cx + 1) * w / SP_CELLS,
               (cy + 1) * h / SP_CELLS, z, rgba);
    }
    return pushSprites(f, list, v, SP_CELLS * SP_CELLS * 2);
}

static void frameStart(RdFrame *f, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
}

/* the read's frame on SCENE: the layers, then the composites */
static bool buildRead(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    frameStart(f, w, h);
    bool ok = rd__st_target(f, 0, scene, w, h) && clearOf(f, 0, scene, 1) &&
              plainState(f, 0, RD_TEST_Z_ALWAYS, RD_ZWRITE_ON);
    for (uint32_t layer = 0; ok && layer < SP_LAYERS; layer++) {
        ok = rd__st_test(f, 0, layer ? RD_TEST_Z_GEQUAL : RD_TEST_Z_ALWAYS) &&
             pushLayer(f, 0, w, h, layer, true);
    }
    ok =
        ok && rd__st_target(f, 3, scene, w, h) && plainState(f, 3, RD_TEST_Z_ALWAYS, RD_ZWRITE_OFF);
    /* the alpha masked (FBMSK 0xFF000000): no DATE draw writes the alpha
       the next one tests, so each way takes one set-up or one snapshot for
       the three, as a game picture does under the one DATE set-up's
       sharing (rd_date_one.c) */
    RdCmd *m = ok ? rd__st_push(f, 3, RDC_COLORMASK) : NULL;
    if (m) {
        m->u[0] = 0xFF000000u;
    }
    ok = m != NULL;
    for (int k = 0; ok && k < SP_DATES; k++) {
        RdScreenVtx v[2];
        sprite(v, w, h, 0, 0, w, h, 0, kDate[k]);
        ok = rd__st_test(f, 3, (uint64_t)RD_TEST_DATE0) && pushSprites(f, 3, v, 2);
    }
    return ok;
}

/* the plain frame: X's passes, each ended by a sprite on Y */
static bool buildPlain(RdFrame *f, uint32_t x, uint32_t w, uint32_t h, uint32_t y)
{
    frameStart(f, w, h);
    bool ok = true;
    for (int pass = 0; ok && pass < SP_PLAIN_PASSES; pass++) {
        const int lx = pass * 2, ly = pass * 2 + 1;
        ok = rd__st_target(f, lx, x, w, h) && (pass > 0 || clearOf(f, lx, x, 0)) &&
             plainState(f, lx, RD_TEST_Z_ALWAYS, RD_ZWRITE_OFF);
        for (uint32_t layer = 0; ok && layer < SP_PLAIN_LAYERS; layer++) {
            ok = pushLayer(f, lx, w, h, (uint32_t)pass * SP_PLAIN_LAYERS + layer, false);
        }
        RdScreenVtx v[2];
        sprite(v, SP_SIDE, SP_SIDE, 0, 0, SP_SIDE, SP_SIDE, 0, kSmall);
        ok = ok && rd__st_target(f, ly, y, SP_SIDE, SP_SIDE) &&
             plainState(f, ly, RD_TEST_Z_ALWAYS, RD_ZWRITE_OFF) && pushSprites(f, ly, v, 2);
    }
    return ok;
}

/* the replays that bring the timed ones' stamps in: SCENE cleared */
static bool buildFlush(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    frameStart(f, w, h);
    return rd__st_target(f, 0, scene, w, h) && clearOf(f, 0, scene, 1);
}

static const char *sabotage(void)
{
    const char *e = getenv("ICO_RD_SUBPASS_PROBE_SABOTAGE");
    return e && e[0] ? e : NULL;
}

/* the verdict over the mark; false when it could not be written */
static bool writeVerdict(const RhiLimits *lim, RhiSubpassVerdict v, const char *why)
{
    phaseEnd(); /* the verdict follows: an end from here is not the probe's */
    char line[RHI_SUBPASS_VERDICT_MAX];
    (void)rhi_subpass_verdict_line(line, sizeof line, lim->subpassVerdictKey, v, why);
    if (!rhi_subpass_verdict_write(lim->subpassVerdictFile, line)) {
        rd__log("subpass read: its verdict could not be written to %s (the next start probes "
                "again)",
                lim->subpassVerdictFile);
        return false;
    }
    return true;
}

/* the file's verdict for this device and driver now */
static RhiSubpassVerdict fileVerdict(const RhiLimits *lim)
{
    char text[RHI_SUBPASS_VERDICT_MAX], why[RHI_SUBPASS_VERDICT_MAX];
    if (!rhi_subpass_verdict_read(lim->subpassVerdictFile, text, sizeof text)) {
        return RHI_SUBPASS_NONE;
    }
    return rhi_subpass_verdict_parse(text, lim->subpassVerdictKey, why, sizeof why);
}

void rd__subpass_probe_begin(void)
{
    if (!g_rd.hasDevice || s_phase == NULL) {
        return;
    }
    const RhiLimits *lim = rhi_limits();
    if (lim != NULL && lim->subpassProbe && lim->subpassVerdictFile && lim->subpassVerdictKey &&
        fileVerdict(lim) == RHI_SUBPASS_PROBING) {
        s_phase(RD_SUBPASS_PROBE_NAME);
    }
}

void rd__subpass_probe_report(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return;
    }
    const RhiLimits *lim = rhi_limits();
    if (lim == NULL) {
        return;
    }
    const bool on = lim->localReadSubpass;
    const char *why = lim->subpassWhy ? lim->subpassWhy : "";
    const bool refused = !on && strncmp(why, "refused", 7) == 0;
    ico_attempt_decide_subpass(lim->subpassRoute || on, on, refused, lim->subpassOffDefault, why);
}

/* not probed at this start: the mark away, so the next start probes */
static void notProbed(const RhiLimits *lim, const char *why)
{
    (void)rhi_subpass_verdict_write(lim->subpassVerdictFile, NULL);
    phaseEnd();
    snprintf(s_why, sizeof s_why, "not probed at this start (%s)", why);
    ico_attempt_decide_subpass(1, 0, 0, 0, s_why);
    rd__log("subpass read: not probed at this start (%s); its mark taken away, the next start "
            "probes",
            why);
}

/* one replay of f, its id appended to ids (n of them) */
static bool timed(const RdFrame *f, uint32_t *ids, uint32_t *n)
{
    const bool ok = rd__replay_frame(f, 0, false);
    if (ok) {
        ids[(*n)++] = g_rdPerf.replay;
    }
    return ok;
}

/* the least and the largest GPU time of n replays; the count with a time */
static uint32_t leastOf(const uint32_t *ids, uint32_t n, double *least, double *most)
{
    uint32_t got = 0;
    *least = INFINITY;
    *most = 0.0;
    for (uint32_t k = 0; k < n; k++) {
        double ms;
        if (rd__perf_gpu_ms_of(ids[k], &ms) && ms > 0.0) {
            *least = ms < *least ? ms : *least;
            *most = ms > *most ? ms : *most;
            got++;
        }
    }
    return got;
}

/* X's colour and its state swapped with the twin's (lent and given back) */
static void swapColour(RdTargetRec *x, RhiTexture *tex, RhiState *state)
{
    const RhiTexture t = x->color;
    const RhiState s = x->colorState;
    x->color = *tex;
    x->colorState = *state;
    *tex = t;
    *state = s;
}

void rd__subpass_probe_after_selftest(int ran, bool passed, const char *words)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return;
    }
    const RhiLimits *lim = rhi_limits();
    if (lim == NULL) {
        return;
    }
    if (!lim->subpassProbe || !lim->subpassVerdictFile || !lim->subpassVerdictKey) {
        if (lim->localReadSubpass && ran && !passed) {
            snprintf(s_why, sizeof s_why, "DATE's self-test on the read: stencil %s",
                     words ? words : "failed");
            ico_attempt_decide_subpass(1, 0, 1, 0, s_why);
        }
        return;
    }
    /* once a device: a renderer made again on it after the verdict finds
       it in the file (a refusal keeps DATE on its snapshot) */
    const RhiSubpassVerdict now = fileVerdict(lim);
    if (now == RHI_SUBPASS_REFUSED) {
        rd__profile_probe_off();
        ico_attempt_decide_subpass(1, 0, 1, 0, "refused by this start's probe");
        return;
    }
    if (now != RHI_SUBPASS_PROBING) {
        return;
    }
    if (!ran) {
        notProbed(lim, "DATE draws by its R8 snapshot: the profile or [dev] date_path");
        return;
    }
    if (!passed) {
        /* words: the stencil replay's result as rd_date_selftest.c's line
           has it after "stencil " ("fail 3 at sample 0: ...") */
        snprintf(s_why, sizeof s_why, "DATE's self-test on the read: stencil %s",
                 words ? words : "failed");
        (void)writeVerdict(lim, RHI_SUBPASS_REFUSED, s_why);
        ico_attempt_decide_subpass(1, 0, 1, 0, s_why);
        rd__log("subpass read: refused (%s): DATE takes its R8 snapshot; the verdict kept for "
                "this device and driver (%s)",
                s_why, lim->subpassVerdictKey);
        return;
    }
    const char *sab = sabotage();
    if (sab && strcmp(sab, "end") == 0) {
        rd__log("subpass read: ICO_RD_SUBPASS_PROBE_SABOTAGE=end: the start ends inside the "
                "probe (exit %d), as a GPU hang there would",
                SP_EXIT_END);
        fflush(stdout);
        fflush(stderr);
        _exit(SP_EXIT_END);
    }
    if (!rd__perf_list_stamps_on()) {
        rd__profile_probe_off();
        notProbed(lim, "[dev] perf_list_stamps = false: no time a replay");
        return;
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM ||
        !g_rd.profile.dateStencil) {
        rd__profile_probe_off();
        notProbed(lim, "no SCENE target to run it on");
        return;
    }
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t w = t->w, h = t->h;
    const uint32_t xId = rd__temp_target_alloc(w, h, 0, 1);
    const uint32_t yId = rd__temp_target_alloc(SP_SIDE, SP_SIDE, 0, 1);
    RdTargetRec *x = xId ? rd__target_rec(xId) : NULL;
    RhiTexture twin = {0};
    RhiState twinState = RHI_STATE_UNDEFINED;
    if (x && x->color.id) {
        twin = rhi_create_texture(&(RhiTextureDesc){x->tw, x->th, 1, RHI_FMT_RGBA8_UNORM,
                                                    RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED |
                                                        RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST |
                                                        RHI_TEX_NO_SUBPASS_READ,
                                                    "rd subpass probe plain twin", 0});
    }
    static RdFrame fr, fp, fl;
    memset(&fr, 0, sizeof fr);
    memset(&fp, 0, sizeof fp);
    memset(&fl, 0, sizeof fl);
    if (x == NULL || yId == 0 || twin.id == 0 || !buildRead(&fr, scene, w, h) ||
        !buildPlain(&fp, xId, w, h, yId) || !buildFlush(&fl, scene, w, h)) {
        rd__frame_free(&fr);
        rd__frame_free(&fp);
        rd__frame_free(&fl);
        if (twin.id) {
            rhi_destroy_texture(twin);
        }
        if (xId) {
            rd__temp_target_free(xId);
        }
        if (yId) {
            rd__temp_target_free(yId);
        }
        rd__profile_probe_off();
        notProbed(lim, "could not make its targets or build its frames");
        return;
    }
    bool ok = true;
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* no MSAA epoch: the read's own draws */
    const double t0 = rd__now_ms();
    uint32_t ids[4][SP_MAX];
    uint32_t n[4] = {0, 0, 0, 0};
    /* untimed: the pipelines made, the GPU awake (the read's way, X's own) */
    ok = ok && rd__replay_frame(&fr, 0, false) && rd__replay_frame(&fp, 0, false);
    for (int round = 0; ok && round < SP_ROUNDS; round++) {
        if (round > 0 && rd__now_ms() - t0 > SP_BUDGET_MS) {
            break;
        }
        /* 0 the read, 1 the snapshot, 2 the shaped plain pass, 3 its twin */
        for (int way = 0; ok && way < 4; way++) {
            g_rd.profile.dateStencil = way != 1;
            if (way == 3) {
                swapColour(x, &twin, &twinState);
            }
            for (int k = 0; ok && k < SP_PER_BLOCK; k++) {
                ok = timed(way < 2 ? &fr : &fp, ids[way], &n[way]);
            }
            if (way == 3) {
                swapColour(x, &twin, &twinState); /* X's own colour back */
            }
        }
    }
    g_rd.profile.dateStencil = true;
    /* the last blocks' stamps come in RHI_FRAMES_IN_FLIGHT replays later */
    for (int k = 0; ok && k < RHI_FRAMES_IN_FLIGHT; k++) {
        ok = rd__replay_frame(&fl, 0, false);
    }
    const double wallMs = rd__now_ms() - t0;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&fr);
    rd__frame_free(&fp);
    rd__frame_free(&fl);
    double least[4], most[4];
    uint32_t got[4];
    for (int way = 0; way < 4; way++) {
        got[way] = leastOf(ids[way], n[way], &least[way], &most[way]);
    }
    if (twin.id) {
        rhi_destroy_texture(twin);
    }
    if (xId) {
        rd__temp_target_free(xId);
    }
    if (yId) {
        rd__temp_target_free(yId);
    }
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    double readF = 1.0, plainF = 1.0;
    if (sab && strcmp(sab, "slow") == 0) {
        readF = 4.0;
    } else if (sab && strcmp(sab, "fast") == 0) {
        /* both of the rule's comparisons decided by the switch: the plain
           pass's own timing is noise on a loaded machine (CI's asan runs) */
        readF = 0.25;
        plainF = 0.25;
    } else if (sab && strcmp(sab, "plain") == 0) {
        plainF = 4.0;
    }
    if (sab) {
        rd__log("subpass read: ICO_RD_SUBPASS_PROBE_SABOTAGE=%s: the read's time counted %g "
                "times, the shaped plain pass's %g times",
                sab, readF, plainF);
    }
    const double readMs = got[0] ? least[0] * readF : 0.0, snapMs = got[1] ? least[1] : 0.0;
    const double shapedMs = got[2] ? least[2] * plainF : 0.0, plainMs = got[3] ? least[3] : 0.0;
    const double spread = got[3] > 1u && least[3] > 0.0 ? most[3] / least[3] - 1.0 : 0.0;
    const RdSubpassProbeRule rule =
        ok ? rd__subpass_probe_keep(readMs, snapMs, shapedMs, plainMs, spread)
           : RD_SUBPASS_PROBE_UNTIMED;
    const bool keep = rule == RD_SUBPASS_PROBE_KEEP;
    /* the file's words (the next start's device line quotes them) and the
       ledger's and the line's ("kept (...)") */
    char verdictWhy[200];
    snprintf(verdictWhy, sizeof verdictWhy,
             "probe: %sthe read %.3f vs the snapshot %.3f ms, a plain pass %.3f vs %.3f ms "
             "without the shape",
             rule == RD_SUBPASS_PROBE_READ_SLOWER    ? "the read slower, "
             : rule == RD_SUBPASS_PROBE_PLAIN_SLOWER ? "a plain pass slower with the shape, "
             : rule == RD_SUBPASS_PROBE_UNTIMED ? (ok ? "untimed: the replays brought no GPU time, "
                                                      : "untimed: a replay failed, ")
                                                : "",
             readMs, snapMs, shapedMs, plainMs);
    snprintf(s_why, sizeof s_why, "%s (%s)", keep ? "kept" : "refused", verdictWhy);
    (void)writeVerdict(lim, keep ? RHI_SUBPASS_KEPT : RHI_SUBPASS_REFUSED, verdictWhy);
    if (!keep) {
        rd__profile_probe_off(); /* DATE stencil off "(probe)": the snapshot draws DATE */
    }
    ico_attempt_decide_subpass(1, keep, !keep, 0, s_why);
    rd__log("subpass read: %s: the least of %u and %u, %u and %u replays on SCENE %ux%u at %ux%u "
            "texels; kept unless the read is more than %d %% slower or a plain pass more than %d "
            "%% (its spread %.1f %%); %.1f ms; the verdict kept for this device and driver (%s)",
            s_why, got[0], got[1], got[2], got[3], t->w, t->h, t->tw, t->th,
            (int)(RD_SUBPASS_PROBE_TOLERANCE * 100.0 + 0.5),
            (int)(RD_SUBPASS_PROBE_TOLERANCE * 100.0 + 0.5), spread * 100.0, wallMs,
            lim->subpassVerdictKey);
}
