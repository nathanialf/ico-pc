/*
 * port/render/rd_hold.c
 *
 * The paused scene's hold, its device half (rd_hold.h; docs/port/RENDER.md
 * "The paused scene's hold").
 *
 * A replay of a paused frame whose rules hold (rd__hold_scan) is one of:
 *
 *   a take      a full replay.  At the start of list 11 (rd__hold_list) the
 *               open pass ends and SCENE's colour is copied into the held
 *               copy, and every target lists 0-10 carry across replays
 *               (FEED128, FEED_HELD, WORK3_HELD) into this slot's readback,
 *               unless lists 0-10 left work owed there (a clear or a shadow
 *               reset not recorded yet, a crack fill held to a paste or to
 *               list 11's pass, the glow's paste deferred, a pass begun
 *               readable or as storage): then nothing is taken and the
 *               chain starts again (RD_HOLD_OWED).  SCENE's border repeat
 *               still due there (the fog's, no post sprite read SCENE in
 *               lists 0-10) stays where it falls, after list 11's draws:
 *               the held replays arm it so it falls at the same command.
 *   held        the frame with every action of lists 0-10 an RDC_NOP (their
 *               state commands, ordinals and overlay text kept, as
 *               rd_vis.c's skipped draws: every scan sees the same lists
 *               11-12 at the same ordinals), and at the start of list 11
 *               the held copy copied back into SCENE's colour.
 *
 * The chain: the full replays since the last change of the key (the lists,
 * the settings, the meshes' and textures' counters), each a take.  The held
 * copy stands for every later replay of the chain once it is settled: no
 * carried target, or the carried targets byte for byte the same at the copy
 * of two consecutive takes (their readbacks, compared at the slot's
 * recycle).  Why that is exact: a replay is a function of its frame, the
 * settings, the meshes and textures and the carried targets at its start.
 * Takes k - 1 and k had the same frame, settings, meshes and textures; if
 * the carried targets after k equal those after k - 1, then k began from
 * the targets it ended with, so every later replay of the same key begins
 * and ends there too and draws k's SCENE at list 11, which is the held copy
 * (the latest take's, equal to k's).  A held replay writes no carried
 * target, SCENE's depth is read by nothing in lists 11-12 (the rules: no Z
 * test, DATE, PRIM.AA1, depth view, fog or shadow there), and every other
 * target lists 11-12 read is SCENE's colour, so lists 11-12 and the present
 * draw from the same inputs as in a full replay.  The take itself changes
 * nothing of the full replay but where list 11's first pass begins: a pass
 * on SCENE ended at list 11 and begun again with SCENE loaded, the same
 * attachments, pipelines and planned stores (rd_store_plan.c decides each
 * pass's stores at its begin), except where list 11's draws join the open
 * pass without SCENE's depth (rd_late_pass.c, DIVERGENCES.md VD-31: other
 * pipelines), which is why the hold is off on a device that joins them.
 *
 * The first held picture of a run is made twice (rd__replay_frame_here):
 * held without a present, then in full, and SCENE and DISPLAY compared byte
 * for byte at their real size; a difference turns the hold off for the run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_hold.h"
#include "attempt_ledger.h"
#include "rd_late_pass.h"
#include "rd_layer.h"
#include "rd_present_scene.h"
#include "rd_replay.h"
#include "rd_submit.h"
#include "rd_thread.h"
#include "rd_upscale.h"
#include "rd_upscale_capture.h"

enum { MODE_NONE = 0, MODE_TAKE, MODE_HELD };

enum { SELF_NONE = 0, SELF_HELD, SELF_FULL, SELF_PASSED, SELF_FAILED };

/* a slot's readback of the carried targets */
typedef struct Readback {
    RhiBuffer buf;
    uint64_t size;
    bool pending;
    uint32_t chain, take; /* the chain and its take that copied it */
    uint64_t bytes;       /* the rows' bytes (pitch padding left out) */
    uint32_t n;

    struct {
        uint64_t off;
        uint32_t pitch, row, rows;
    } part[RD_HOLD_CROSS_MAX];
} Readback;

static struct {
    int env; /* -1 unread, 0 off (ICO_RD_SCENE_HOLD=0), 1 on, 2 every frame paused */
    /* this replay: its kind, and a take's reason not yet counted (-1 none):
       counted at list 11 under the take's outcome, on the replaying thread
       alone, so the 10 s line's reset never meets a take-back */
    int mode, pending;
    RdHoldScan scan;
    /* the chain */
    uint32_t chain, takes;
    bool take, settled;
    uint64_t lists, state, meshGen, texGen;
    /* the held copy */
    RhiTexture held;
    RhiState heldState;
    uint32_t w, h;
    RhiFormat format;
    uint64_t heldBytes;
    uint64_t heldPictures; /* since the copy was made (the free's line) */
    /* the readbacks, and the last one read (its chain and take) */
    Readback rb[RHI_FRAMES_IN_FLIGHT];
    uint8_t *last;
    uint64_t lastBytes, lastCap;
    uint32_t lastChain, lastTake;
    /* the self-test */
    int self;
    double selfT0;
    uint8_t *selfPx[2];
    size_t selfCap[2];
    uint32_t selfW[2], selfH[2];
    /* the device's verdict, last said */
    const char *deviceWhy;
    bool deviceSaid;
    /* the block's counts, read by the window thread's line (__atomic) */
    uint64_t why[RD_HOLD_COUNT];
    uint64_t heldMenu;
    uint64_t owedWhy[5];
    /* the takes found SCENE's border repeat still due at list 11 */
    bool repeat;
} s_h = {.env = -1, .pending = -1};

/* the stall lines' ring: replay number and its kind (__atomic) */
#define KIND_RING 256u
static uint32_t s_kindReplay[KIND_RING];
static uint8_t s_kindOf[KIND_RING];

static int envMode(void)
{
    if (s_h.env < 0) {
        const char *e = getenv("ICO_RD_SCENE_HOLD");
        s_h.env = !e || !e[0] ? 1 : e[0] == '0' ? 0 : strcmp(e, "paused") == 0 ? 2 : 1;
        if (s_h.env == 2) {
            rd__log("still scene: ICO_RD_SCENE_HOLD=paused: the replay tool's frame taken as "
                    "paused, its replays that present nothing too (the tests' A/B)");
        }
    }
    return s_h.env;
}

/* why this device or these settings cannot hold, NULL when they can */
static const char *deviceWhy(void)
{
    if (!g_rd.hasDevice || rhi_device_lost()) {
        return "no device";
    }
    if (rhi_backend() != RHI_BACKEND_VULKAN) {
        return "not Vulkan";
    }
    if ((rd__late_pass_bits() & RD_LATE_JOIN) != 0u && g_rd.profile.fxDstLocal &&
        g_rd.profile.fxPlan) {
        return "list 11's draws join the open pass here (VD-31)";
    }
    if (rd__replay_msaa_want() > 1u || rd_aa_samples(rd__aa_mode()) > 1u) {
        return "MSAA";
    }
    if (g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__uber_on()) {
        return "the exact blend";
    }
    /* the display layer is in force only under the upscaler's split
       (rd__layer_on is its [dev] switch, on by default: not the layer) */
    if (rd__upscale_point_on()) {
        return "a temporal upscaler";
    }
    if (rd__present_scene_on()) {
        return "the present from SCENE";
    }
    if (g_rd.settings.motionBlurSmooth) {
        return "Motion blur: Smooth";
    }
    if (g_rdWin.first) {
        return "a pass window (the replay tool)";
    }
    if (rd__upscale_capture_armed()) {
        return "an upscaler capture waiting";
    }
    if (g_rd.videoShown) {
        return "a video";
    }
    return NULL;
}

static void noteDevice(const char *why)
{
    if (s_h.deviceSaid && why == s_h.deviceWhy) {
        return;
    }
    s_h.deviceSaid = true;
    s_h.deviceWhy = why;
    if (s_h.self == SELF_FAILED) {
        return; /* refused for the run: the row says so */
    }
    if (why) {
        rd__log("still scene: off on this device or with these settings: %s%s", why,
                strcmp(why, "not Vulkan") == 0
                    ? " (OpenGL and Direct3D 11 draw every paused picture in full)"
                : strstr(why, "VD-31") ? " (a held picture would draw them in a pass of their own, "
                                         "with other pipelines)"
                                       : "");
    } else {
        rd__log("still scene: on (Vulkan): while the game is paused and its scene is the one "
                "the last full picture drew, lists 11-12 are drawn over a copy of SCENE taken "
                "at list 11; the first held picture is checked against the same picture in "
                "full");
    }
    ico_attempt_decide_scene_hold(why);
}

static void kindNote(int kind)
{
    const uint32_t replay = g_rdPerf.replay;
    const uint32_t i = replay % KIND_RING;
    __atomic_store_n(&s_kindOf[i], (uint8_t)kind, __ATOMIC_RELAXED);
    __atomic_store_n(&s_kindReplay[i], replay, __ATOMIC_RELEASE);
}

int rd_hold_replay_kind(uint32_t replay)
{
    const uint32_t i = replay % KIND_RING;
    if (__atomic_load_n(&s_kindReplay[i], __ATOMIC_ACQUIRE) != replay) {
        return RD_HOLD_KIND_NONE;
    }
    return __atomic_load_n(&s_kindOf[i], __ATOMIC_RELAXED);
}

static void count(int why)
{
    if (why >= 0 && why < RD_HOLD_COUNT) {
        __atomic_add_fetch(&s_h.why[why], 1u, __ATOMIC_RELAXED);
    }
}

/* the take's reason, counted once its outcome is known (or when the replay
   ended before list 11: at the next replay or the shutdown) */
static void countPending(void)
{
    if (s_h.pending >= 0) {
        count(s_h.pending);
        s_h.pending = -1;
    }
}

uint64_t rd__hold_count(int why)
{
    return why >= 0 && why < RD_HOLD_COUNT ? __atomic_load_n(&s_h.why[why], __ATOMIC_RELAXED) : 0u;
}

static void chainReset(void)
{
    s_h.chain++;
    s_h.takes = 0;
    s_h.take = false;
    s_h.settled = false;
}

/* --------------------------------------------------------------- memory */

static void freeAll(const char *why)
{
    if (s_h.held.id) {
        rd__log("still scene: the held copy freed (%s; %llu pictures held over it): SCENE "
                "%ux%u, %.1f MB",
                why, (unsigned long long)s_h.heldPictures, s_h.w, s_h.h,
                (double)s_h.heldBytes / (1024.0 * 1024.0));
        rd__retire_texture(s_h.held);
    }
    s_h.held = (RhiTexture){0};
    s_h.heldState = RHI_STATE_UNDEFINED;
    s_h.heldBytes = s_h.heldPictures = 0;
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_h.rb[i].buf.id) {
            rhi_destroy_buffer(s_h.rb[i].buf); /* deferred past the frames in flight */
        }
        memset(&s_h.rb[i], 0, sizeof(s_h.rb[i]));
    }
    free(s_h.last);
    s_h.last = NULL;
    s_h.lastBytes = s_h.lastCap = 0;
    chainReset();
}

static uint32_t bytesPer(RhiFormat f)
{
    switch (f) {
    case RHI_FMT_RGBA8_UNORM:
    case RHI_FMT_RGBA8_UINT:
    case RHI_FMT_BGRA8_UNORM:
        return 4;
    case RHI_FMT_R8_UNORM:
    case RHI_FMT_R8_UINT:
        return 1;
    default:
        return 0;
    }
}

static bool heldEnsure(const RdTargetRec *tc)
{
    if (s_h.held.id && s_h.w == tc->tw && s_h.h == tc->th && s_h.format == tc->format) {
        return true;
    }
    if (s_h.held.id) {
        rd__log("still scene: the held copy made again (SCENE %ux%u -> %ux%u)", s_h.w, s_h.h,
                tc->tw, tc->th);
        rd__retire_texture(s_h.held);
        s_h.held = (RhiTexture){0};
    }
    s_h.held = rhi_create_texture(&(RhiTextureDesc){
        tc->tw, tc->th, 1, tc->format, RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST, "rd held scene", 0});
    if (!s_h.held.id) {
        rd__log("still scene: no held copy (SCENE %ux%u: the texture was refused)", tc->tw, tc->th);
        return false;
    }
    s_h.heldState = RHI_STATE_UNDEFINED;
    s_h.w = tc->tw;
    s_h.h = tc->th;
    s_h.format = tc->format;
    s_h.heldBytes = (uint64_t)tc->tw * tc->th * bytesPer(tc->format);
    s_h.heldPictures = 0;
    rd__log("still scene: the held copy made: SCENE %ux%u, %.1f MB (freed when the pause ends)",
            tc->tw, tc->th, (double)s_h.heldBytes / (1024.0 * 1024.0));
    return true;
}

/* ------------------------------------------------------------- the take */

static uint64_t alignUp(uint64_t v, uint64_t a)
{
    return a > 1u ? (v + a - 1u) / a * a : v;
}

/* the carried targets into this slot's readback; false: none could be */
static bool readbackRecord(RhiCommandList cl)
{
    const uint32_t slot = rhi_frame_slot() % RHI_FRAMES_IN_FLIGHT;
    Readback *rb = &s_h.rb[slot];
    const uint64_t pa = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint64_t oa = rhi_limits()->copyOffsetAlign ? rhi_limits()->copyOffsetAlign : 1u;
    uint64_t need = 0, rows = 0;
    for (uint32_t k = 0; k < s_h.scan.crossN; k++) {
        const RdTargetRec *t = rd__target_rec(s_h.scan.cross[k]);
        const uint32_t bpp = t ? bytesPer(t->format) : 0u;
        if (!t || !t->color.id || bpp == 0) {
            return false; /* another format: never settled */
        }
        need = alignUp(need, oa) + alignUp((uint64_t)t->tw * bpp, pa) * t->th;
        rows += (uint64_t)t->tw * bpp * t->th;
    }
    if (rb->buf.id && rb->size < need) {
        rhi_destroy_buffer(rb->buf);
        rb->buf = (RhiBuffer){0};
    }
    if (!rb->buf.id) {
        rb->buf = rhi_create_buffer(&(RhiBufferDesc){need, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                     RHI_MEM_READBACK, "rd held feedback"});
        rb->size = rb->buf.id ? need : 0u;
        if (!rb->buf.id) {
            return false;
        }
    }
    uint64_t off = 0;
    rb->n = 0;
    for (uint32_t k = 0; k < s_h.scan.crossN; k++) {
        RdTargetRec *t = rd__target_rec(s_h.scan.cross[k]);
        const uint32_t row = t->tw * bytesPer(t->format);
        const uint32_t pitch = (uint32_t)alignUp(row, pa);
        off = alignUp(off, oa);
        rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
        rhi_cmd_copy_texture_to_buffer(cl, t->color, RHI_ASPECT_COLOR,
                                       (RhiRect){0, 0, t->tw, t->th}, rb->buf, off, pitch);
        rb->part[rb->n].off = off;
        rb->part[rb->n].pitch = pitch;
        rb->part[rb->n].row = row;
        rb->part[rb->n].rows = t->th;
        rb->n++;
        off += (uint64_t)pitch * t->th;
    }
    rb->bytes = rows;
    rb->pending = true;
    rb->chain = s_h.chain;
    rb->take = s_h.takes;
    return true;
}

/* why the copy cannot be taken at list 11, NULL when it can */
static const char *owedWhy(const Replay *r, int *k)
{
    const uint32_t owed = rd__replay_owed();
    const RdTargetRec *tc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (owed & 1u) {
        *k = 0;
        return "a clear not recorded yet";
    }
    if ((owed & 2u) || (tc && rd__stencil_live(rd__replay_stencil(), tc->depth.id))) {
        *k = 1;
        return "a shadow reset or window open";
    }
    if (rd__crack_fill_owed()) {
        *k = 2;
        return "a crack fill held to a paste or to list 11's pass";
    }
    if (rd__fx_defer_on()) {
        *k = 3;
        return "the glow's paste deferred";
    }
    if (r->passOpen && (r->passReadable || r->passStorage || r->passDepthReadable)) {
        *k = 4;
        return "a pass begun readable or as storage";
    }
    return NULL;
}

static void take(Replay *r)
{
    int k = 0;
    const char *why = owedWhy(r, &k);
    RdTargetRec *tc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!why && (!tc || !tc->color.id)) {
        why = "no SCENE";
        k = 0;
    }
    if (why) {
        static uint32_t said;
        if (said < 8u || said % 100u == 0u) {
            rd__log("still scene: no copy at list 11 of frame %u: %s (the scene is drawn in "
                    "full until a frame leaves nothing owed there)",
                    g_rdPerf.frame, why);
        }
        said++;
        __atomic_add_fetch(&s_h.owedWhy[k], 1u, __ATOMIC_RELAXED);
        /* counted under this reason instead of the decision's */
        s_h.pending = -1;
        count(RD_HOLD_OWED);
        chainReset();
        return;
    }
    countPending(); /* the decision's reason */
    if (!heldEnsure(tc)) {
        chainReset();
        return;
    }
    rd__replay_end_pass(r); /* list 11 begins its own pass on SCENE, loaded */
    /* the chain's state as this replay drew with it: its uploads came
       before list 11 (the texture levels' filter is set by the first) */
    s_h.state = rd__hold_state_key();
    /* SCENE's border repeat still due (the fog's columns, no post sprite
       read SCENE in lists 0-10): it stays where it falls, after list 11's
       draws, and the held replays arm it to fall there too */
    s_h.repeat = rd__scene_border_repeat_pending();
    const RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_h.held, &s_h.heldState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, tc->color, (RhiRect){0, 0, tc->tw, tc->th}, s_h.held, 0, 0);
    s_h.takes++;
    s_h.take = true;
    if (s_h.scan.crossN == 0) {
        s_h.settled = true; /* nothing carried: the copy is the frame's alone */
    } else if (!readbackRecord(cl)) {
        s_h.settled = false;
    }
}

static void restore(Replay *r)
{
    RdTargetRec *tc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    rd__replay_end_pass(r);
    const RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, s_h.held, &s_h.heldState, RHI_STATE_COPY_SRC);
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, s_h.held, (RhiRect){0, 0, s_h.w, s_h.h}, tc->color, 0, 0);
    r->writeSerial++; /* SCENE's colour written (DATE's snapshot taken again) */
    if (s_h.repeat) {
        rd__scene_border_rearm(); /* repeated where the full replay repeats it */
    }
    s_h.heldPictures++;
}

void rd__hold_list(Replay *r, int l)
{
    if (l != RD_HOLD_LIST) {
        return;
    }
    if (s_h.mode == MODE_TAKE) {
        take(r);
    } else if (s_h.mode == MODE_HELD) {
        restore(r);
    }
}

void rd__hold_collect(uint32_t slot)
{
    Readback *rb = &s_h.rb[slot % RHI_FRAMES_IN_FLIGHT];
    if (!rb->pending || !rb->buf.id) {
        return;
    }
    rb->pending = false;
    const uint8_t *src = rhi_map_buffer(rb->buf);
    if (!src) {
        return;
    }
    uint8_t *now = malloc(rb->bytes ? rb->bytes : 1u);
    if (!now) {
        return;
    }
    uint64_t at = 0;
    for (uint32_t k = 0; k < rb->n; k++) {
        for (uint32_t y = 0; y < rb->part[k].rows; y++) {
            memcpy(now + at, src + rb->part[k].off + (uint64_t)y * rb->part[k].pitch,
                   rb->part[k].row);
            at += rb->part[k].row;
        }
    }
    /* the take after the last one read, in the same chain, with the same
       bytes: settled (the file's header) */
    const bool next = s_h.last && rb->chain == s_h.lastChain && rb->take == s_h.lastTake + 1u;
    const bool same = next && rb->bytes == s_h.lastBytes && memcmp(now, s_h.last, at) == 0;
    if (same && rb->chain == s_h.chain && s_h.take) {
        if (!s_h.settled) {
            static uint32_t said;
            if (said++ < 8u) {
                rd__log("still scene: the feedback settled (%u targets, %llu bytes the same at "
                        "takes %u and %u of the chain)",
                        rb->n, (unsigned long long)at, s_h.lastTake, rb->take);
            }
        }
        s_h.settled = true;
    }
    free(s_h.last);
    s_h.last = now;
    s_h.lastBytes = at;
    s_h.lastChain = rb->chain;
    s_h.lastTake = rb->take;
}

/* --------------------------------------------------------- the held frame */

static RdFrame s_heldFrame;
static RdCmd *s_cmds[RD_HOLD_LIST];
static uint32_t s_caps[RD_HOLD_LIST];

static const RdFrame *heldFrame(const RdFrame *f)
{
    for (int l = 0; l < RD_HOLD_LIST; l++) {
        const uint32_t n = f->lists[l].count;
        if (n > s_caps[l]) {
            RdCmd *g = realloc(s_cmds[l], (size_t)n * sizeof(RdCmd));
            if (!g) {
                return NULL;
            }
            s_cmds[l] = g;
            s_caps[l] = n;
        }
    }
    s_heldFrame = *f;
    for (int l = 0; l < RD_HOLD_LIST; l++) {
        const uint32_t n = f->lists[l].count;
        if (n) {
            memcpy(s_cmds[l], f->lists[l].cmds, (size_t)n * sizeof(RdCmd));
        }
        for (uint32_t i = 0; i < n; i++) {
            RdCmd *c = &s_cmds[l][i];
            /* the actions not drawn, every other field kept (rd_vis.c's
               rule); the overlay text stays for the present */
            if (!rd__cmd_is_state(c->type) && c->type != RDC_OVERLAY_TEXT) {
                c->type = RDC_NOP;
            }
        }
        s_heldFrame.lists[l].cmds = s_cmds[l];
        s_heldFrame.lists[l].cap = n;
    }
    return &s_heldFrame;
}

/* ---------------------------------------------------------- the decision */

static int decide(const RdFrame *f, int keep, RdHoldIn *in)
{
    memset(in, 0, sizeof(*in));
    const int env = envMode();
    in->paused = f && f->paused && !keep;
    if (!in->paused) {
        return rd__hold_why(in);
    }
    in->on = env != 0 && s_h.self != SELF_FAILED;
    const char *dw = deviceWhy();
    noteDevice(dw);
    in->device = dw == NULL;
    if (!in->on || !in->device) {
        return rd__hold_why(in);
    }
    in->rule = rd__hold_scan(f, &s_h.scan);
    if (in->rule != RD_HOLD_RULE_OK) {
        return rd__hold_why(in);
    }
    in->take = s_h.take && s_h.held.id != 0;
    in->lists = rd__hold_lists_key(f);
    in->state = rd__hold_state_key();
    in->meshGen = rd__hold_mesh_gen();
    in->texGen = rd__hold_tex_gen();
    in->texWaiting = g_rd.texDirtyCount != 0 || g_rd.texLevelsFilter != (int)g_rd.filterUpgrade;
    in->refLists = s_h.lists;
    in->refState = s_h.state;
    in->refMeshGen = s_h.meshGen;
    in->refTexGen = s_h.texGen;
    in->settled = s_h.settled;
    return rd__hold_why(in);
}

/* a replay that presents nothing, outside the self-test: a dump's, a check's
 * or a probe's own replay (the VU skin check, F12's dump, the start-up
 * self-tests); the replay tool's frame under ICO_RD_SCENE_HOLD=paused is the
 * tests' picture (its --pass-dump replays present nothing) */
static bool sideReplay(bool present)
{
    return !present && envMode() != 2 && s_h.self != SELF_HELD;
}

const RdFrame *rd__hold_frame(const RdFrame *f, int keep, bool present)
{
    countPending(); /* a take that never reached list 11 */
    s_h.mode = MODE_NONE;
    if (sideReplay(present)) {
        /* not the picture: drawn in full and not counted, nothing freed (a
           probe's frame is not the game's, paused or not); its lists 0-10
           may write the carried targets, so the chain starts again */
        chainReset();
        kindNote(RD_HOLD_KIND_NONE);
        return f;
    }
    RdHoldIn in;
    int why = decide(f, keep, &in);
    if (s_h.self == SELF_HELD) {
        why = RD_HOLD_HELD; /* the self-test's held picture (rd__hold_selftest_due said so) */
    } else if ((s_h.self == SELF_FULL || s_h.self == SELF_NONE) && why == RD_HOLD_HELD) {
        /* the self-test's full picture, or a first held picture found only
           after this replay's wait (the settling's readback): a take of the
           same chain, the next replay the self-test's */
        why = RD_HOLD_FEEDBACK;
    }
    if (why == RD_HOLD_NOT_PAUSED) {
        if (s_h.held.id || s_h.take) {
            freeAll("the pause ended");
        }
        kindNote(RD_HOLD_KIND_NONE);
        return f;
    }
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (why == RD_HOLD_HELD && (!sc || sc->tw != s_h.w || sc->th != s_h.h)) {
        why = RD_HOLD_SETTINGS; /* SCENE made again since the copy */
    }
    if (why == RD_HOLD_HELD) {
        const RdFrame *h = heldFrame(f);
        if (h) {
            s_h.mode = MODE_HELD;
            if (s_h.self != SELF_HELD) {
                count(RD_HOLD_HELD);
                if (s_h.scan.menuDraws) {
                    __atomic_add_fetch(&s_h.heldMenu, 1u, __ATOMIC_RELAXED);
                }
                ico_attempt_fire(ICO_ATT_SCENE_HOLD, 1);
            }
            kindNote(RD_HOLD_KIND_HELD);
            return h;
        }
        why = RD_HOLD_FEEDBACK; /* no memory for the held frame: drawn in full */
    }
    ico_attempt_fire2(ICO_ATT_SCENE_HOLD, 1);
    kindNote(RD_HOLD_KIND_REPLAYED);
    switch (why) {
    case RD_HOLD_FIRST:
    case RD_HOLD_LISTS:
    case RD_HOLD_SETTINGS:
    case RD_HOLD_UPLOADS:
        chainReset(); /* a new chain with this key: this replay its first take */
        s_h.lists = in.lists;
        s_h.state = in.state;
        s_h.meshGen = in.meshGen;
        s_h.texGen = in.texGen;
        s_h.mode = MODE_TAKE;
        break;
    case RD_HOLD_FEEDBACK:
        s_h.mode = MODE_TAKE; /* the same key: the chain's next take */
        break;
    default:
        chainReset(); /* off, the device, the rules: no chain */
        break;
    }
    if (s_h.mode == MODE_TAKE) {
        s_h.pending = why; /* counted at list 11 (take) */
    } else {
        count(why);
    }
    return f;
}

/* ---------------------------------------------------------- the self-test */

bool rd__hold_selftest_due(const RdFrame *f, int keep, bool present)
{
    if (s_h.self != SELF_NONE || !f || sideReplay(present)) {
        return false;
    }
    RdHoldIn in;
    if (decide(f, keep, &in) != RD_HOLD_HELD) {
        return false;
    }
    s_h.self = SELF_HELD;
    s_h.selfT0 = rd__now_ms();
    return true;
}

/* SCENE's and DISPLAY's bytes after the replay just made, into slot k */
static bool readBoth(int k)
{
    (void)rd_thread_drain(); /* the submit thread's list before */
    rhi_wait_idle();
    const RdTargetId ids[2] = {RD_TARGET_SCENE, RD_TARGET_DISPLAY};
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        const RdTargetRec *t = rd__target_rec(rd_target(ids[i]).id);
        /* the held picture's in the first half of each buffer, the full
           one's in the second */
        const size_t need = t ? (size_t)t->tw * t->th * 4u : 0u;
        if (!t || need == 0) {
            ok = false;
            continue;
        }
        const size_t cap = 2u * need;
        if (s_h.selfCap[i] < cap) {
            free(s_h.selfPx[i]);
            s_h.selfPx[i] = malloc(cap);
            s_h.selfCap[i] = s_h.selfPx[i] ? cap : 0u;
        }
        uint32_t w = 0, h = 0;
        ok = ok && s_h.selfPx[i] &&
             rd__read_target(rd_target(ids[i]), s_h.selfPx[i] + (size_t)k * need, need, &w, &h);
        s_h.selfW[i] = w;
        s_h.selfH[i] = h;
    }
    return ok;
}

static bool s_selfReadOk;

void rd__hold_selftest_mid(void)
{
    s_selfReadOk = readBoth(0);
    s_h.self = SELF_FULL;
}

void rd__hold_selftest_end(bool ok)
{
    ok = ok && s_selfReadOk && readBoth(1);
    uint64_t diff[2] = {0, 0};
    for (int i = 0; i < 2 && ok; i++) {
        const size_t need = (size_t)s_h.selfW[i] * s_h.selfH[i] * 4u;
        const uint8_t *a = s_h.selfPx[i], *b = s_h.selfPx[i] + need;
        for (size_t p = 0; p < need; p++) {
            diff[i] += a[p] != b[p];
        }
    }
    const double ms = rd__now_ms() - s_h.selfT0;
    const bool same = ok && diff[0] == 0 && diff[1] == 0;
    s_h.self = same ? SELF_PASSED : SELF_FAILED;
    rd__log("still scene: self-test: the first held picture against the same picture in full: "
            "%s (SCENE %ux%u, %llu bytes apart; DISPLAY %ux%u, %llu bytes apart); the hold %s "
            "for the run; it cost one extra replay and two read-backs, %.1f ms",
            !ok    ? "not read back"
            : same ? "equal"
                   : "different",
            s_h.selfW[0], s_h.selfH[0], (unsigned long long)diff[0], s_h.selfW[1], s_h.selfH[1],
            (unsigned long long)diff[1], same ? "on" : "off", ms);
    if (!same) {
        ico_attempt_state(ICO_ATT_SCENE_HOLD, ICO_ATT_REFUSED,
                          ok ? "the first held picture differed from the same picture in full"
                             : "the self-test's pictures could not be read back");
        freeAll("its self-test refused it");
    }
    for (int i = 0; i < 2; i++) {
        free(s_h.selfPx[i]);
        s_h.selfPx[i] = NULL;
        s_h.selfCap[i] = 0;
    }
}

void rd__hold_shutdown(void)
{
    countPending();
    if (s_h.held.id) {
        rhi_destroy_texture(s_h.held);
    }
    s_h.held = (RhiTexture){0};
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_h.rb[i].buf.id) {
            rhi_destroy_buffer(s_h.rb[i].buf);
        }
        memset(&s_h.rb[i], 0, sizeof(s_h.rb[i]));
    }
    free(s_h.last);
    s_h.last = NULL;
    s_h.lastBytes = s_h.lastCap = 0;
    s_h.lastChain = s_h.lastTake = 0;
    for (int i = 0; i < 2; i++) {
        free(s_h.selfPx[i]);
        s_h.selfPx[i] = NULL;
        s_h.selfCap[i] = 0;
    }
    s_h.heldBytes = s_h.heldPictures = 0;
    s_h.w = s_h.h = 0;
    s_h.heldState = RHI_STATE_UNDEFINED;
    s_h.mode = MODE_NONE;
    s_h.self = SELF_NONE;
    s_h.deviceSaid = false;
    s_h.deviceWhy = NULL;
    s_h.repeat = false;
    chainReset();
}

/* ------------------------------------------------------------- the line */

void rd_hold_line(void)
{
    uint64_t w[RD_HOLD_COUNT];
    uint64_t any = 0, replayed = 0;
    for (int k = 0; k < RD_HOLD_COUNT; k++) {
        w[k] = __atomic_exchange_n(&s_h.why[k], 0, __ATOMIC_RELAXED);
        any += k == RD_HOLD_NOT_PAUSED ? 0u : w[k];
        replayed += k == RD_HOLD_HELD || k == RD_HOLD_NOT_PAUSED ? 0u : w[k];
    }
    const uint64_t menuN = __atomic_exchange_n(&s_h.heldMenu, 0, __ATOMIC_RELAXED);
    uint64_t owed[5];
    for (int k = 0; k < 5; k++) {
        owed[k] = __atomic_exchange_n(&s_h.owedWhy[k], 0, __ATOMIC_RELAXED);
    }
    if (any == 0) {
        return;
    }
    fprintf(stderr,
            "window: still scene: %llu pictures from the held scene (%llu under a menu), %llu "
            "replayed (%llu lists differ, %llu feedback not settled, %llu first, %llu settings, "
            "%llu uploads, %llu lists 11-12 the hold cannot keep, %llu the scene reads the "
            "picture before, %llu scene work owed at list 11 [%llu clears, %llu shadow, %llu "
            "crack fills, %llu glow pastes, %llu readable passes], %llu "
            "the device or settings, %llu off)\n",
            (unsigned long long)w[RD_HOLD_HELD], (unsigned long long)menuN,
            (unsigned long long)replayed, (unsigned long long)w[RD_HOLD_LISTS],
            (unsigned long long)w[RD_HOLD_FEEDBACK], (unsigned long long)w[RD_HOLD_FIRST],
            (unsigned long long)w[RD_HOLD_SETTINGS], (unsigned long long)w[RD_HOLD_UPLOADS],
            (unsigned long long)w[RD_HOLD_MENU], (unsigned long long)w[RD_HOLD_PICTURE],
            (unsigned long long)w[RD_HOLD_OWED], (unsigned long long)owed[0],
            (unsigned long long)owed[1], (unsigned long long)owed[2], (unsigned long long)owed[3],
            (unsigned long long)owed[4], (unsigned long long)w[RD_HOLD_DEVICE],
            (unsigned long long)w[RD_HOLD_OFF]);
}
