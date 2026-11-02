/* rd_pass_cut.c: passes a picture does not need (docs/port/WORK_BUFFERS.md,
 * "Passes cut"; [dev] clear_carry, clear_skip and last_store = auto|on|off,
 * ICO_RD_CLEAR_CARRY, ICO_RD_CLEAR_SKIP, ICO_RD_LAST_STORE; auto: on on the
 * tile-based classes, RdProfile.passCut).
 *
 * A tile-based GPU pays for every render pass it begins: what the pass
 * loads is read into its tiles, what it stores is written back, about 1 ms
 * per 10 MB on the PowerVR phone at 2366 x 1080.  Three of the picture's
 * passes, or parts of them, move bytes nothing looks at.  Each cut leaves
 * every target's bytes as they were.
 *
 * 1. The clear carry (rd_replay.c endPassFor).  A clear is pending until
 *    the next pass on its target takes it as its load op (beginPass,
 *    rd__take_pending_clear) or it is recorded as a pass of its own
 *    (flushClear), which endPass does for whoever ends a pass.  A draw
 *    that samples a target not in SHADER_READ ends the open pass to put
 *    the texture there (resolveTexture, doBlurSpriteDraw): that flushed a
 *    pending clear of the draw's own target into a pass of its own just
 *    before the draw's pass on it began.  The reduction does it in every
 *    play replay: DISPLAY's black clear (the reduction's untextured
 *    sprite, rd__fx_reduction_clears), then the textured sprite's read of
 *    SCENE: "reduction DISPLAY 0 draws" and "reduction DISPLAY 1 draws",
 *    DISPLAY written, read and written again.  With the carry the clear
 *    stays pending across that end when the texture made readable is not
 *    the clear's target (a transition of the target's own textures records
 *    the clear first, rd__transition), no shadow reset is pending (its pass
 *    and the clear's would change order) and no MSAA epoch is wanted (its
 *    clears of SCENE go to SCENE_MS).  Exact: a pending clear is recorded,
 *    as a load op or as its own pass, before anything else touches its
 *    target (every touch is a pass begun on it, which takes or flushes it,
 *    a transition of its textures, which flushes it, or a later endPass,
 *    which flushes it), and only the source's barrier lies between where it
 *    was flushed and where it now is; a CLEAR load of the same values over
 *    the whole target (a pass taking a clear has no area, rd_pass_area.c)
 *    writes what the clear's own pass wrote.
 *
 * 2. The clear skip (rd_replay.c flushClear).  The aura clears AURA_WORK
 *    (staticBlur.c auraInspireBefore) and with no aura object on screen
 *    nothing draws into it; WORK0's sprite then reads it as its clear's
 *    colour without sampling it (rd_const_read.c), and the clear's own
 *    pass ("aura AURA_WORK 2364x1080 0 draws") wrote 10 MB nothing reads.
 *    A pending clear of a target with no reader outside the command lists
 *    (AURA_WORK and AURA_TAP: no code but the lists' commands draws into
 *    them or reads them; every other named target is read by the present,
 *    the glow hold, the water, the dumps or the interpolation, and SCENE
 *    and DISPLAY by the present) is not recorded when it is flushed while
 *    the action walked is the sprite the constant read drew in its colour
 *    from that target (rd__clear_skip_served), the clear holds no depth,
 *    and no later command of the replay touches the target (the scan: a
 *    draw into it or on its depth, a texture of it, a copy or exact blend
 *    naming it, a command the scan does not know; another clear of it is
 *    not a touch, it overwrites).  The target's memory then holds what was
 *    there before the clear, while the frame's picture of it is the clear:
 *    the clear is kept as a lazy clear (target, colour texture, the clear's
 *    colour) and recorded where anything could see the difference: at the
 *    start of a later replay that touches the target before clearing it
 *    (rd__clear_skip_begin, from rd__clear_new_targets after the scan), and
 *    before a read of the target outside a replay (rd__read_target: the
 *    replay tool's PNGs, the tests).  A clear of the target recorded or
 *    taken later, a new texture (a cleared target: clearPending) or a new
 *    context forgets it.  So every read of the target sees the clear's
 *    bytes, as without the skip.
 *
 * 3. The last store (rd_store_plan.c rd__store_plan_pass).  The store plan
 *    drops a depth's loads and stores in every pass begun after its last
 *    touch; the pass that holds the touch still stored it, since beginPass
 *    cannot know where the walk will end the pass.  On the phone that is
 *    the fog's pass (SCENE's depth, 13-20 MB stored, then never read).  The
 *    pass begun at command k on colour cid and depth did, whose depth's
 *    last touch is command t >= k, has its depth and stencil stored
 *    DONT_CARE when the walk is known to keep that pass open from its begin
 *    until command t is done: then every later pass on the depth begins at
 *    a command after t, where the plan neither loads nor stores it, and
 *    nothing reads it between.  Known, by the walk's code:
 *      - the pass is a draw's own (rd__last_store_arm: bindDrawEx, the
 *        crack filler's pass the local fog keeps), its depth loaded (not
 *        a clear's pass, which the flush of a pending clear records inside
 *        another pass's begin), the plan in force (mode 1), no shadow
 *        window open on the depth, no command the plan does not know
 *        after k, the depth not kept past the replay;
 *      - no MSAA epoch wanted, the fixed-function blend (no storage pass
 *        switch, dstPassMode), GS state as shader data off, no pass split
 *        ([dev] pass_split), no pass window (the tool's bisect);
 *      - command k is a screen-prim or VU draw on cid with its depth did
 *        bound (prepareDraw binds the state's depth on a target with
 *        one), DATE off (its snapshot or stencil take ends passes),
 *        COLCLAMP 0's wrap off (its own passes), not textured from a target
 *        (resolveTexture snapshots it or ends the pass to read it): after
 *        bindDrawEx begins the pass, such a draw records only into it; or
 *        command k is the fog drawing on the local read (its pass the
 *        scene pass, kept open by doFog and the crack filler's fold) with
 *        no probe wanted and no edge read due (both end the pass after the
 *        draw to copy the depth), said by doFog before it begins anything
 *        (rd__last_store_fog);
 *      - every command after k up to t is a state change, a NOP, overlay
 *        text, or such a draw on cid with did bound (no other kind of
 *        command is taken: a clear, a copy, a post sprite, the fog, a
 *        shadow command and anything else may end the pass).
 *    Every other place the walk ends or begins a pass (endPass, beginPass,
 *    takeSnap, dateSnapshot, doScreenWrap, doExact, doCopy, the shadow
 *    commands, the fog's paths, blurSwapTarget, doBlurSpriteDraw, the
 *    pass area's end, the fx plan, the blur border, the crack filler, the
 *    aura box, Motion blur: Smooth, the shadow's own pass, the pass split,
 *    dstPassMode, the MSAA epoch) lies on a path those commands do not
 *    take.  The guard (rd__last_store_end): a pass with its store dropped
 *    that the walk ends before command t is done turns the cut off for the
 *    run and says so (it would mean a path missed here; the poison of
 *    rd_store_plan.c, ICO_RD_STORE_POISON=1, shows such a store's loss in
 *    rd_pass_cut_ab).  The pass area (rd_pass_area.c) ends a pass with an
 *    area only at a command no area run holds, which none of those is.
 *
 * The lines:
 *   rd: pass cut: <the cut>: on|off (<why>)        (start, each)
 *   rd: pass cut: a pass whose depth store was dropped at the last touch
 *       ended before it ... (the first 8, then every 100th)
 *   window: per replay ... (rd_perf_backend_text) "; passes cut: C clears
 *       taken by the next pass, S clears skipped, D depth stores dropped
 *       at the last touch a replay"
 *   the replay tool: --stats "pass cut: clear carry on|off: C ..." */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

static const char *const kEnv[RD_CUT_COUNT] = {"ICO_RD_CLEAR_CARRY", "ICO_RD_CLEAR_SKIP",
                                               "ICO_RD_LAST_STORE"};
static const char *const kKey[RD_CUT_COUNT] = {"clear_carry", "clear_skip", "last_store"};
static const char *const kWhat[RD_CUT_COUNT] = {
    "a clear carried past a source's barrier into the next pass on its target",
    "a clear nothing reads before it is cleared again not drawn",
    "the depth not stored by the pass holding its last touch"};

/* a command's kind for the last store (the scan) */
enum { CUT_NONE = 0, CUT_DRAW, CUT_FOG, CUT_OTHER };

typedef struct CutCmd {
    uint8_t kind;
    uint32_t color, depth; /* st.color; st.depth when its target has a depth, else 0 */
} CutCmd;

/* the targets the clear skip takes (no reader outside the lists) */
#define CUT_SKIP_TARGETS 2
#define CUT_LAZY 4

static struct {
    int forced[RD_CUT_COUNT];
    bool on[RD_CUT_COUNT];
    /* the scan of replay `replay` */
    CutCmd *cmd;
    uint32_t n, cap, replay;
    bool valid;
    /* the clear skip's targets: their first command that touches or clears
     * them plus one (0 none) and whether it clears, their last touch plus
     * one (0 none) */
    uint32_t skipId[CUT_SKIP_TARGETS];
    uint32_t first[CUT_SKIP_TARGETS], last[CUT_SKIP_TARGETS];
    bool firstClears[CUT_SKIP_TARGETS];
    /* the sprite the constant read drew from a cleared source: the replay,
     * the command and the source */
    uint32_t servedReplay, servedOrd, servedId;
    bool served;

    /* the clears not recorded: target id, colour texture id, colour */
    struct {
        uint32_t id, tex;
        float col[4];
    } lazy[CUT_LAZY];

    uint32_t nLazy;
    /* the clear carried (rd__clear_carry_note), until taken or flushed */
    uint32_t carried;
    /* the last store: the pass about to begin (rd__last_store_arm), the
     * fog's promise (replay, command plus one), the open pass whose store
     * was dropped and the command it must last through */
    uint32_t armColor, armDepth;
    uint32_t fogReplay, fogOrd;
    bool dropOpen;
    uint32_t dropReplay, dropUntil;
    bool broken;
    uint32_t brokenSeen;
} s_cut = {.forced = {-1, -1, -1}};

static bool decide(int w, char *why, size_t size)
{
    const char *env = getenv(kEnv[w]);
    const bool def = g_rd.profile.passCut;
    if (s_cut.forced[w] >= 0) {
        snprintf(why, size, "the tests' switch");
        return s_cut.forced[w] != 0;
    }
    const int want = rd__pass_area_pick(env, def); /* the same words as pass_area's */
    const bool auto_ = !env || !env[0] || strcmp(env, "auto") == 0;
    const bool known = auto_ || rd__pass_area_pick(env, 0) == rd__pass_area_pick(env, 1);
    if (!known) {
        snprintf(why, size, "%s=%.32s is not auto, on or off: the device profile's, %s", kEnv[w],
                 env, def ? "a tile-based GPU" : "an immediate-mode GPU");
    } else if (!auto_) {
        snprintf(why, size, "[dev] %s = %s, %s=%.32s", kKey[w], want ? "on" : "off", kEnv[w], env);
    } else {
        snprintf(why, size, "%s's default; [dev] %s = %s",
                 def ? "a tile-based GPU" : "an immediate-mode GPU", kKey[w],
                 def ? "off draws it as before" : "on cuts it");
    }
    return want != 0;
}

static void note(int w, const char *why)
{
    rd__log("pass cut: %s: %s (%s)", kWhat[w], s_cut.on[w] ? "on" : "off", why);
}

void rd__pass_cut_init(void)
{
    for (int w = 0; w < RD_CUT_COUNT; w++) {
        char why[160];
        s_cut.on[w] = decide(w, why, sizeof why);
        note(w, why);
    }
    s_cut.valid = s_cut.served = s_cut.dropOpen = s_cut.broken = false;
    s_cut.nLazy = 0; /* a new context: the textures are new */
    s_cut.carried = s_cut.armColor = s_cut.armDepth = 0;
    s_cut.fogReplay = s_cut.fogOrd = 0;
    s_cut.brokenSeen = 0;
}

bool rd__pass_cut_on(int which)
{
    return which >= 0 && which < RD_CUT_COUNT && s_cut.on[which];
}

void rd__pass_cut_force(int which, int mode)
{
    if (which < 0 || which >= RD_CUT_COUNT) {
        return;
    }
    s_cut.forced[which] = mode < 0 ? -1 : (mode ? 1 : 0);
    char why[160];
    s_cut.on[which] = decide(which, why, sizeof why);
    note(which, why);
    if (which == RD_CUT_LAST) {
        s_cut.broken = false;
    }
}

/* ------------------------------------------------------------- the scan */

static bool texIsTarget(const RdStateBlock *st, uint32_t *target)
{
    const RdTexRec *t = rd__tex_rec(st->tex);
    if (!t || t->kind != RD_TEXKIND_TARGET) {
        return false;
    }
    if (target) {
        *target = t->target;
    }
    return true;
}

static uint32_t depthOf(const RdStateBlock *st)
{
    const RdTargetRec *td = rd__target_rec(st->depth);
    return td && td->withDepth && td->depth.id ? st->depth : 0u;
}

/* command c under st (not a state command): its kind for the last store */
static uint8_t cutKind(const RdCmd *c, const RdStateBlock *st)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return CUT_NONE;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
        if (c->type == RDC_SCREEN && rd__wrap_applies(st)) {
            return CUT_OTHER; /* doScreenWrap: passes of its own */
        }
        if (st->ds.test.date != RD_DATE_OFF || (st->ds.texEnabled && texIsTarget(st, NULL))) {
            return CUT_OTHER; /* DATE's snapshot or stencil; a target's read */
        }
        return CUT_DRAW;
    case RDC_POST_STUB:
        return c->b[0] == RD_POST_FOG ? CUT_FOG : CUT_OTHER;
    default:
        return CUT_OTHER;
    }
}

/* whether command c under st touches target id (the clear skip): a draw
 * into it or on its depth, its texture (enabled or not), a copy or exact
 * blend naming it, a command the scan does not know; *clears: c is a
 * clear of it */
static bool touches(const RdCmd *c, const RdStateBlock *st, uint32_t id, bool *clears)
{
    *clears = false;
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return false;
    case RDC_CLEAR:
        *clears = c->u[0] == id;
        return false;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        return c->u[0] == id || c->u[1] == id;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_POST_STUB:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
    case RDC_WORLD_PRIMS: {
        uint32_t tex = 0;
        return st->color == id || st->depth == id || (texIsTarget(st, &tex) && tex == id);
    }
    default:
        return true;
    }
}

void rd__pass_cut_scan(const RdFrame *f, int keep)
{
    s_cut.valid = false;
    s_cut.served = false;
    s_cut.dropOpen = false;
    s_cut.n = 0;
    s_cut.skipId[0] = rd_target(RD_TARGET_AURA_WORK).id;
    s_cut.skipId[1] = rd_target(RD_TARGET_AURA_TAP).id;
    for (int k = 0; k < CUT_SKIP_TARGETS; k++) {
        s_cut.first[k] = s_cut.last[k] = 0;
        s_cut.firstClears[k] = false;
    }
    if (!f || g_rdWin.first || (!s_cut.on[RD_CUT_SKIP] && !s_cut.on[RD_CUT_LAST])) {
        return; /* a pass window reads targets mid-frame: nothing cut */
    }
    uint32_t total = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        total += f->lists[l].count;
    }
    if (total > s_cut.cap) {
        const uint32_t cap = total + total / 4u + 64u;
        CutCmd *p = realloc(s_cut.cmd, (size_t)cap * sizeof(*p));
        if (!p) {
            return; /* no memory: nothing cut this replay */
        }
        s_cut.cmd = p;
        s_cut.cap = cap;
    }
    RdStateBlock st = f->startState;
    uint32_t ord = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++, ord++) {
            const RdCmd *c = &list->cmds[i];
            CutCmd *e = &s_cut.cmd[ord];
            memset(e, 0, sizeof(*e));
            if (rd__apply_state(&st, c)) {
                continue; /* CUT_NONE */
            }
            e->kind = cutKind(c, &st);
            e->color = st.color;
            e->depth = depthOf(&st);
            for (int k = 0; k < CUT_SKIP_TARGETS; k++) {
                bool clears = false;
                const bool touch = touches(c, &st, s_cut.skipId[k], &clears);
                if ((touch || clears) && !s_cut.first[k]) {
                    s_cut.first[k] = ord + 1u;
                    s_cut.firstClears[k] = clears;
                }
                if (touch) {
                    s_cut.last[k] = ord + 1u;
                }
            }
        }
    }
    s_cut.n = ord;
    s_cut.replay = g_rd.replayCounter;
    s_cut.valid = true;
}

static bool scanHere(void)
{
    return s_cut.valid && s_cut.replay == g_rd.replayCounter;
}

/* ---------------------------------------------------------- 1. the carry */

void rd__clear_carry_note(uint32_t id)
{
    s_cut.carried = id;
}

void rd__clear_taken(uint32_t id)
{
    if (s_cut.carried && s_cut.carried == id) {
        g_rdPerf.clearsCarried++; /* the pass the carry spared */
        static uint32_t s_said;
        if (s_said < 1u) {
            s_said++;
            rd__log("pass cut: %s's clear drawn as the load of its next pass, not a pass of its "
                    "own (reported once)",
                    rd__target_name(id));
        }
    }
    s_cut.carried = 0;
    /* the clear is in the target's memory now: no lazy clear of it */
    for (uint32_t i = 0; i < s_cut.nLazy; i++) {
        if (s_cut.lazy[i].id == id) {
            s_cut.lazy[i] = s_cut.lazy[--s_cut.nLazy];
            break;
        }
    }
}

/* ------------------------------------------------------------ 2. the skip */

static int skipSlot(uint32_t id)
{
    for (int k = 0; k < CUT_SKIP_TARGETS; k++) {
        if (id && s_cut.skipId[k] == id) {
            return k;
        }
    }
    return -1;
}

void rd__clear_skip_served(uint32_t id)
{
    s_cut.served = true;
    s_cut.servedReplay = g_rd.replayCounter;
    s_cut.servedOrd = g_rdStoreOrd;
    s_cut.servedId = id;
}

static int lazyFind(uint32_t id)
{
    for (uint32_t i = 0; i < s_cut.nLazy; i++) {
        if (s_cut.lazy[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static void lazyDrop(int i)
{
    s_cut.lazy[i] = s_cut.lazy[--s_cut.nLazy];
}

bool rd__clear_skip(uint32_t id, const RdTargetRec *t, const float col[4], bool depth)
{
    const int k = skipSlot(id);
    const int had = lazyFind(id);
    const bool skip = s_cut.on[RD_CUT_SKIP] && scanHere() && !g_rdWin.first && k >= 0 && !depth &&
                      t && t->color.id && s_cut.served &&
                      s_cut.servedReplay == g_rd.replayCounter && s_cut.servedOrd == g_rdStoreOrd &&
                      s_cut.servedId == id && s_cut.last[k] <= g_rdStoreOrd + 1u;
    s_cut.carried = 0;
    if (!skip) {
        if (had >= 0) {
            lazyDrop(had); /* recorded now: the memory holds this clear */
        }
        return false;
    }
    int i = had;
    if (i < 0) {
        if (s_cut.nLazy == CUT_LAZY) {
            return false; /* no room: recorded */
        }
        i = (int)s_cut.nLazy++;
    }
    s_cut.lazy[i].id = id;
    s_cut.lazy[i].tex = t->color.id;
    memcpy(s_cut.lazy[i].col, col, sizeof(s_cut.lazy[i].col));
    g_rdPerf.clearsSkipped++;
    static uint32_t s_seen;
    s_seen++;
    if (s_seen <= 8u || s_seen % 100u == 0u) {
        rd__log("pass cut: %s's clear not drawn (frame's command %u: nothing reads it before it "
                "is cleared again; the sprite after it took its colour) (%u so far)",
                rd__target_name(id), g_rdStoreOrd, s_seen);
    }
    return true;
}

/* the lazy clear i recorded on cl as the pass flushClear would have begun */
static void lazyRecord(RhiCommandList cl, int i)
{
    RdTargetRec *t = rd__target_rec(s_cut.lazy[i].id);
    if (t && t->color.id == s_cut.lazy[i].tex && !t->clearPending) {
        const bool readable = rd__pass_wants_readable(t);
        rd__transition(cl, t->color, &t->colorState,
                       readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = t->color;
        p.color[0].load = RHI_LOAD_CLEAR;
        p.color[0].readable = readable;
        memcpy(p.color[0].clear, s_cut.lazy[i].col, sizeof(p.color[0].clear));
        p.colorCount = 1;
        p.width = t->tw;
        p.height = t->th;
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
    }
    lazyDrop(i); /* a new texture holds nothing of it either (cleared, clearPending) */
}

void rd__clear_skip_begin(void)
{
    for (int i = (int)s_cut.nLazy - 1; i >= 0; i--) {
        const RdTargetRec *t = rd__target_rec(s_cut.lazy[i].id);
        const int k = skipSlot(s_cut.lazy[i].id);
        if (!t || t->color.id != s_cut.lazy[i].tex || t->clearPending) {
            lazyDrop(i); /* a new texture: cleared before the walk, the clear gone with it */
            continue;
        }
        /* kept while this replay's first touch of the target is a clear
         * of it (that clear is then recorded, taken or skipped again) */
        if (s_cut.on[RD_CUT_SKIP] && scanHere() && k >= 0 &&
            (!s_cut.first[k] || s_cut.firstClears[k])) {
            continue;
        }
        lazyRecord(rd__replay_cl(), i);
    }
}

void rd__clear_skip_readback(uint32_t id)
{
    const int i = lazyFind(id);
    if (i < 0) {
        return;
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return;
    }
    lazyRecord(cl, i);
    rhi_end_commands(cl);
    rhi_submit(cl);
}

/* ------------------------------------------------------- 3. the last store */

void rd__last_store_arm(uint32_t cid, uint32_t did)
{
    s_cut.armColor = cid;
    s_cut.armDepth = did;
}

void rd__last_store_fog(bool holds)
{
    s_cut.fogReplay = g_rd.replayCounter;
    s_cut.fogOrd = holds ? g_rdStoreOrd + 1u : 0u;
}

static bool drawOn(const CutCmd *e, uint32_t cid, uint32_t did)
{
    return e->kind == CUT_DRAW && e->color == cid && e->depth == did;
}

bool rd__last_store_drop(uint32_t did, uint32_t last, RhiLoadOp depthLoad, bool live,
                         uint32_t anyTouch)
{
    const uint32_t k = g_rdStoreOrd, cid = s_cut.armColor;
    if (!s_cut.on[RD_CUT_LAST] || s_cut.broken || !scanHere() || !cid || s_cut.armDepth != did ||
        !did || depthLoad != RHI_LOAD_LOAD || live || last == UINT32_MAX || last <= k ||
        anyTouch > k || last > s_cut.n || k >= s_cut.n || g_rdWin.first ||
        rd__replay_msaa_want() > 1u || g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__uber_on() ||
        g_rd.passSplit) {
        return false;
    }
    const CutCmd *e = &s_cut.cmd[k];
    const bool fog = e->kind == CUT_FOG && s_cut.fogReplay == g_rd.replayCounter &&
                     s_cut.fogOrd == k + 1u && e->color == cid && did == cid;
    if (!drawOn(e, cid, did) && !fog) {
        return false; /* begun for work the walk may end before the last touch */
    }
    for (uint32_t i = k + 1u; i < last; i++) {
        const CutCmd *q = &s_cut.cmd[i];
        if (q->kind != CUT_NONE && !drawOn(q, cid, did)) {
            return false;
        }
    }
    s_cut.dropOpen = true;
    s_cut.dropReplay = g_rd.replayCounter;
    s_cut.dropUntil = last - 1u;
    g_rdPerf.storeLastDropped++;
    return true;
}

void rd__last_store_end(void)
{
    if (!s_cut.dropOpen) {
        return;
    }
    s_cut.dropOpen = false;
    if (s_cut.dropReplay != g_rd.replayCounter || g_rdStoreOrd > s_cut.dropUntil) {
        return; /* ended after the last touch was done: as promised */
    }
    s_cut.broken = true;
    s_cut.brokenSeen++;
    if (s_cut.brokenSeen <= 8u || s_cut.brokenSeen % 100u == 0u) {
        rd__log("pass cut: a pass whose depth store was dropped at the last touch ended at "
                "command %u, before the touch at command %u was done: the depth may be wrong "
                "in this picture; the cut is off for the rest of the run ([dev] last_store) "
                "(%u so far)",
                g_rdStoreOrd, s_cut.dropUntil, s_cut.brokenSeen);
    }
}
