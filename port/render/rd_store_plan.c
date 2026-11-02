/* rd_store_plan.c: the store plan (RdProfile.storePlan; docs/port/WORK_BUFFERS.md
 * "The store plan").
 *
 * A tile-based GPU reads every loaded attachment into its tiles when a
 * pass begins and writes every stored one back when it ends: SCENE's depth
 * is 10 MB at 2364x1080, about 1 ms each way on the PowerVR phone, and
 * every pass on SCENE has its depth bound, read or not.  Before the walk
 * (rd__store_plan_scan) the replayed lists are read once with their state,
 * and for every depth texture the last command that may touch its depth or
 * stencil is found, by ordinal (the commands of the replayed lists counted
 * in order, state commands included).  A command touches a depth when it
 * is
 *   - a draw (screen prims, the VU draws, a post sprite) on the state's
 *     depth with the Z test on (ZTE and not ALWAYS), Z write on, DATE on
 *     (the stencil bit) or PRIM.AA1 (its stencil pairs);
 *   - a draw whose texture is a target's depth view (RD_VIEW_DEPTH);
 *   - a shadow command (the stencil count, the volumes' Z test);
 *   - the fog (its depth read; the crack filler's copy and fill of the
 *     colour target's depth): the state's depth, the colour target's and
 *     a depth view the texture names;
 *   - a command the plan does not know: every depth.
 * A copy, an exact blend and a clear touch none (a clear only writes).
 * The walk counts the same ordinals (g_rdStoreOrd), and a pass begun while
 * command k is replayed holds the work of command k and later ones only (a
 * screen run or a VU merge is flushed into the pass it was recorded in; a
 * fold reads ahead, never back).  So when a depth's last touch lies before
 * k, nothing in the pass or after it reads that depth, and nothing in it
 * writes it: beginPass neither loads nor stores it (DONT_CARE; a pending
 * clear stays a CLEAR load), nor its stencil, unless a shadow window is
 * open on it.  Every other pass keeps its loads and stores as before, so
 * a pass that touches a depth stores it even when no later pass reads it
 * (beginPass cannot know where the pass will end).  The passes into
 * SCENE_MS (the MSAA epoch) keep theirs; the passes begun outside
 * beginPass (the new targets' clears, the shadow reset's own pass, the
 * crack filler's own pass, the MSAA resolves) keep theirs.
 *
 * Past the replay a depth counts as touched (no pass drops it) when the
 * present reads SCENE's (rd__effects_depth_wanted: [video] effects_depth
 * with an effects program loaded; the setting alone, on by default, reads
 * nothing: keeping on it kept SCENE's stores on every phone), when a
 * replay found it
 * read before its clear (a frame that draws on the depth the replay before
 * left: said once in the log, kept from then on; a target new this replay,
 * cleared by rd__clear_new_targets before the walk, holds no earlier
 * replay's depth: its read before the frame's own clear is not carried and
 * keeps nothing, said in the log too), after a caller read it
 * back (rd__read_target_depth, the tests), and in every replay with a pass
 * window (the tool's bisect reads targets mid-frame: no plan).  The passes
 * dropped lie after the depth's last touch and draw nothing into it, so a
 * device that skips the write-back leaves the memory as the last stored
 * pass wrote it.
 *
 * A planned replay that drops no depth store while a pass had a depth says
 * why at its end (rd__store_plan_end, the first 8 then every 100th; and
 * RdPerfRecord.storeWhy for the 10 s line): the depth's last touch (its
 * command's type, list and index), the passes begun after it with the
 * depth bound, and what kept the first of them (the effects depth, the
 * keep, the shadow window, a command the plan does not know) or that none
 * began after it.
 *
 * ICO_RD_STORE_POISON=1 (tests): lavapipe keeps an attachment's contents
 * whatever its load and store ops say (Mesa 25.0.7, measured), so a wrong
 * plan would not show there.  With the poison every load the plan leaves
 * undefined (a load it made DONT_CARE, or a load after a store it dropped)
 * clears the depth to 0.5 and the stencil to 0x5A instead, the garbage a
 * tile-based GPU may leave: a plan that drops a store some later pass
 * reads changes the picture (rd_store_plan_ab; ICO_RD_STORE_PLAN=all, which
 * drops every depth store and keeps the loads, must change it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* the command the walk replays (rd_replay.c replayFrame), counted as the
 * scan counts */
uint32_t g_rdStoreOrd;

#define PLAN_DEPTHS 64
#define PLAN_NONE UINT32_MAX
#define PLAN_AFTER UINT32_MAX /* PlanDepth.last: touched after the replay */

typedef struct PlanDepth {
    uint32_t tex;    /* the depth texture's id (RhiTexture.id) */
    uint32_t target; /* the target whose depth it is (the log's name) */
    /* the last command touching it plus one (0: none; PLAN_AFTER: touched
     * after the replay), and its first read (not a write alone), first
     * clear (0 when it was cleared before the walk) and first read's list
     * (PLAN_NONE: none) */
    uint32_t last, firstRead, firstClear;
    int firstList;
    bool dropped; /* its last pass begun so far dropped the store */
    /* fresh: its target is new this replay (clearPending: no replay before
     * wrote it); frameClear: the frame's own first clear of it (PLAN_NONE:
     * none), for the log's "not a carried read" */
    bool fresh;
    uint32_t frameClear;
    /* the diagnosis of a replay that drops nothing: the last touch in the
     * frame plus one (0: none; last is PLAN_AFTER past the replay), its
     * command's type, post kind (RDC_POST_STUB), list and index in it; why
     * last is PLAN_AFTER (RD_STORE_WHY_EFFECTS or _KEEP); the passes begun
     * with it bound, those begun after lastIn, and what kept the first of
     * those (RD_STORE_WHY_*) */
    uint32_t lastIn, lastCmd;
    uint8_t lastType, lastPost;
    int8_t lastList;
    uint8_t why, afterWhy;
    uint32_t passes, afterPasses;
} PlanDepth;

static struct {
    uint8_t mode; /* this replay: 0 every store kept, 1 the plan, 2 every depth store dropped */
    bool poison;  /* ICO_RD_STORE_POISON=1 */
    bool full;    /* more depths than PLAN_DEPTHS: those not listed keep their stores */
    uint32_t anyTouch; /* a command the plan does not know, plus one (0: none) */
    /* the last such command's type, list and index in it (the line) */
    uint8_t anyType;
    int8_t anyList;
    uint32_t anyCmd;
    /* the command the scan reads (planTouch notes it) */
    uint8_t curType, curPost;
    int8_t curList;
    uint32_t curCmd;
    bool effects;        /* this replay's present reads SCENE's depth (rd__effects_depth_wanted) */
    uint32_t fullPasses; /* passes with a depth the full plan does not hold */
    uint32_t n;
    PlanDepth d[PLAN_DEPTHS];
    /* across replays, by texture id: the depths touched after their replay
     * (keep: from a read before the clear or a readback), those whose last
     * pass in the replay before dropped the store (the log's word), and
     * those the poison holds undefined (in this replay) */
    uint32_t keep[PLAN_DEPTHS], nKeep;
    /* by the keep's entries: the target and the frame that kept each, for
     * the 10 s line (RdPerfRecord.storeKept*) */
    uint32_t keepTarget[PLAN_DEPTHS], keepFrame[PLAN_DEPTHS];
    uint32_t scanFrame; /* the last scanned frame's number (a readback's keep) */
    uint32_t prevDropped[PLAN_DEPTHS], nPrevDropped;
    uint32_t undef[PLAN_DEPTHS], nUndef;
} s_plan;

static bool planHas(const uint32_t *set, uint32_t n, uint32_t tex)
{
    for (uint32_t i = 0; i < n; i++) {
        if (set[i] == tex) {
            return true;
        }
    }
    return false;
}

static void planAdd(uint32_t *set, uint32_t *n, uint32_t tex)
{
    if (tex != 0 && *n < PLAN_DEPTHS && !planHas(set, *n, tex)) {
        set[(*n)++] = tex;
    }
}

static void planDel(uint32_t *set, uint32_t *n, uint32_t tex)
{
    for (uint32_t i = 0; i < *n; i++) {
        if (set[i] == tex) {
            set[i] = set[--*n];
            return;
        }
    }
}

/* target id's depth in the plan, made when add (NULL: no depth, or the
 * plan is full); a new one past the replay when the present reads it or a
 * replay found it read before its clear */
static PlanDepth *planDepth(uint32_t id, bool add)
{
    const RdTargetRec *t = rd__target_rec(id);
    if (!t || !t->withDepth || !t->depth.id) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_plan.n; i++) {
        if (s_plan.d[i].tex == t->depth.id) {
            return &s_plan.d[i];
        }
    }
    if (!add || s_plan.n == PLAN_DEPTHS) {
        s_plan.full = s_plan.full || add;
        return NULL;
    }
    PlanDepth *p = &s_plan.d[s_plan.n++];
    memset(p, 0, sizeof(*p));
    p->tex = t->depth.id;
    p->target = id;
    p->firstRead = PLAN_NONE;
    p->fresh = t->clearPending != 0;
    p->firstClear = p->fresh ? 0u : PLAN_NONE; /* rd__clear_new_targets clears it */
    p->frameClear = PLAN_NONE;
    p->firstList = -1;
    const bool effects = s_plan.effects && id == RD_TARGET_SCENE + 1u;
    if (effects || planHas(s_plan.keep, s_plan.nKeep, p->tex)) {
        p->last = PLAN_AFTER;
        p->why = effects ? RD_STORE_WHY_EFFECTS : RD_STORE_WHY_KEEP;
    }
    p->lastList = -1;
    return p;
}

/* command ord (of list l) touches target id's depth; read: not a write alone */
static void planTouch(uint32_t id, uint32_t ord, int l, bool read)
{
    PlanDepth *p = planDepth(id, true);
    if (!p) {
        return;
    }
    if (p->last != PLAN_AFTER) {
        p->last = ord + 1u;
    }
    p->lastIn = ord + 1u;
    p->lastType = s_plan.curType;
    p->lastPost = s_plan.curPost;
    p->lastList = s_plan.curList;
    p->lastCmd = s_plan.curCmd;
    if (read && p->firstRead == PLAN_NONE) {
        p->firstRead = ord;
        p->firstList = l;
    }
}

/* a draw's touches under the state st: its depth by the Z test, Z write,
 * DATE or PRIM.AA1, and a depth view it samples */
static void planDraw(const RdStateBlock *st, uint32_t ord, int l)
{
    const RdDrawState *ds = &st->ds;
    const bool test = ds->test.zte && ds->test.ztst != RD_ZTST_ALWAYS;
    const bool stencil = ds->test.date != RD_DATE_OFF || st->aa1;
    if (test || stencil || ds->zwrite == RD_ZWRITE_ON) {
        planTouch(st->depth, ord, l, test || stencil);
    }
    const RdTexRec *tv = ds->texEnabled ? rd__tex_rec(st->tex) : NULL;
    if (tv && tv->kind == RD_TEXKIND_TARGET && tv->view == RD_VIEW_DEPTH) {
        planTouch(tv->target, ord, l, true);
    }
}

/* the keep's state in this replay's record: the depths of the replay the
 * keep holds, and its first depth's target and frame where this replay has
 * it (the window's 10 s line) */
static void planKeepNote(void)
{
    g_rdPerf.storeKept = 0;
    g_rdPerf.storeKeptTarget = RD_PERF_TARGET_NONE;
    g_rdPerf.storeKeptFrame = 0;
    uint32_t first = PLAN_DEPTHS;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        for (uint32_t j = 0; j < s_plan.nKeep; j++) {
            if (s_plan.keep[j] == s_plan.d[i].tex) {
                g_rdPerf.storeKept++;
                first = j < first ? j : first;
                break;
            }
        }
    }
    if (first < PLAN_DEPTHS) {
        g_rdPerf.storeKeptTarget = rd__target_code(s_plan.keepTarget[first]);
        g_rdPerf.storeKeptFrame = s_plan.keepFrame[first];
    }
}

/* tex kept from now on: the target and frame that kept it */
static void planKeep(uint32_t tex, uint32_t target, uint32_t frame)
{
    const uint32_t at = s_plan.nKeep;
    planAdd(s_plan.keep, &s_plan.nKeep, tex);
    if (s_plan.nKeep > at) {
        s_plan.keepTarget[at] = target;
        s_plan.keepFrame[at] = frame;
    }
}

void rd__store_plan_scan(const RdFrame *f, int keep)
{
    rd__fades_fold_scan(f, keep); /* the sprites the reduction draws (rd_fades_fold.c) */
    s_plan.n = s_plan.nUndef = 0;
    s_plan.full = false;
    s_plan.anyTouch = 0;
    s_plan.anyList = -1;
    s_plan.fullPasses = 0;
    g_rdStoreOrd = 0;
    s_plan.mode = !g_rd.profile.storePlan ? 0 : g_rd.profile.storeAll ? 2 : 1;
    if (g_rdWin.first) {
        s_plan.mode = 0; /* a pass window reads targets mid-frame */
    }
    const char *poison = getenv("ICO_RD_STORE_POISON");
    s_plan.poison = poison && strcmp(poison, "1") == 0;
    g_rdPerf.storePlanMode = s_plan.mode;
    s_plan.scanFrame = f->number;
    if (s_plan.mode == 0) {
        return;
    }
    s_plan.effects = rd__effects_depth_wanted();
    if (g_rd.settings.effectsDepth) {
        /* once a run each way: the setting is on by default, and reads
         * nothing without an effects program */
        static bool s_saidOn, s_saidOff;
        bool *said = s_plan.effects ? &s_saidOn : &s_saidOff;
        if (!*said) {
            *said = true;
            rd__log(s_plan.effects
                        ? "store plan: the present reads SCENE's depth for an effects program "
                          "([video] effects_depth): its stores are kept"
                        : "store plan: [video] effects_depth is on but no effects program reads "
                          "it: SCENE's depth stores are planned");
        }
    }
    RdStateBlock st = f->startState;
    uint32_t ord = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++, ord++) {
            const RdCmd *c = &list->cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            s_plan.curType = c->type;
            s_plan.curPost = c->type == RDC_POST_STUB ? c->b[0] : 0;
            s_plan.curList = (int8_t)l;
            s_plan.curCmd = i;
            switch (c->type) {
            case RDC_NOP:
            case RDC_OVERLAY_TEXT:
            case RDC_COPY:
            case RDC_EXACT_BLEND:
            case RDC_WORLD_PRIMS: /* not replayed */
                break;
            case RDC_CLEAR: {
                PlanDepth *p = c->b[4] ? planDepth(c->u[0], true) : NULL;
                if (p && p->firstClear == PLAN_NONE) {
                    p->firstClear = ord;
                }
                if (p && p->frameClear == PLAN_NONE) {
                    p->frameClear = ord;
                }
                break;
            }
            case RDC_SHADOW_RESET:
            case RDC_SHADOW_STRIP:
            case RDC_SHADOW_RESOLVE:
                planTouch(st.depth, ord, l, true);
                break;
            case RDC_POST_STUB:
                if (c->b[0] == RD_POST_FOG) {
                    planTouch(st.depth, ord, l, true);
                    planTouch(st.color, ord, l, true);
                    const RdTexRec *tv = rd__tex_rec(st.tex);
                    if (tv && tv->kind == RD_TEXKIND_TARGET && tv->view == RD_VIEW_DEPTH) {
                        planTouch(tv->target, ord, l, true);
                    }
                    break;
                }
                planDraw(&st, ord, l); /* a post sprite: a draw */
                break;
            case RDC_SCREEN:
                if (!rd__fades_fold_skip(ord)) {
                    planDraw(&st, ord, l); /* a folded sprite is not drawn */
                }
                break;
            case RDC_MESH:
            case RDC_SKINNED:
            case RDC_GRID:
            case RDC_PARTICLES:
                planDraw(&st, ord, l);
                break;
            default:
                s_plan.anyTouch = ord + 1u; /* unknown: every depth */
                s_plan.anyType = c->type;
                s_plan.anyList = (int8_t)l;
                s_plan.anyCmd = i;
                break;
            }
        }
    }
    /* a depth read before its clear reads what the replay before left: kept
     * from now on (said for the first 4 a run).  A target new this replay
     * (rd__clear_new_targets clears it before the walk) holds no replay's
     * work: a read before the frame's own clear reads that clear, not a
     * carried depth (said for the first 8 a run, then every 100th) */
    static int s_carriedLogged;
    static uint32_t s_freshSeen;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        PlanDepth *p = &s_plan.d[i];
        if (p->fresh && p->firstRead != PLAN_NONE &&
            (p->frameClear == PLAN_NONE || p->firstRead < p->frameClear)) {
            s_freshSeen++;
            if (s_freshSeen <= 8u || s_freshSeen % 100u == 0u) {
                rd__log("store plan: %s's depth is read in list %d before a clear in frame %u, "
                        "the target new this replay: not a carried read (%u so far)",
                        rd__target_name(p->target), p->firstList, f->number, s_freshSeen);
            }
        }
        const bool carried = p->firstRead != PLAN_NONE &&
                             (p->firstClear == PLAN_NONE || p->firstRead < p->firstClear);
        if (!carried || planHas(s_plan.keep, s_plan.nKeep, p->tex)) {
            continue;
        }
        planKeep(p->tex, p->target, f->number);
        p->last = PLAN_AFTER;
        p->why = RD_STORE_WHY_KEEP;
        if (s_carriedLogged < 4) {
            s_carriedLogged++;
            const bool was = planHas(s_plan.prevDropped, s_plan.nPrevDropped, p->tex);
            rd__log("store plan: %s's depth is read in list %d before this replay clears it "
                    "(frame %u%s): its stores are kept from now on%s",
                    rd__target_name(p->target), p->firstList, f->number,
                    keep ? ", a keep frame" : "",
                    was ? " (the replay before had dropped its last store: this picture may "
                          "show a depth left undefined)"
                        : "");
        }
    }
    planKeepNote();
}

void rd__store_plan_pass(const RdTargetRec *d, uint32_t did, RhiLoadOp *depthLoad,
                         RhiLoadOp *stencilLoad, RhiStoreOp *depthStore, RhiStoreOp *stencilStore,
                         float *clearDepth, uint8_t *clearStencil)
{
    g_rdPerf.storePasses++;
    if (!d) {
        return;
    }
    g_rdPerf.storeDepthPasses++;
    if (s_plan.mode == 0) {
        return;
    }
    PlanDepth *p = planDepth(did, true);
    if (!p) {
        s_plan.fullPasses++;
        return; /* the plan is full: kept */
    }
    const uint32_t tex = d->depth.id;
    const bool live = rd__stencil_live(rd__replay_stencil(), tex);
    const bool drop =
        s_plan.mode == 2 || (!live && p->last <= g_rdStoreOrd && s_plan.anyTouch <= g_rdStoreOrd);
    const RhiLoadOp dl = *depthLoad, sl = *stencilLoad;
    p->dropped = drop;
    p->passes++;
    if (p->lastIn <= g_rdStoreOrd) {
        /* begun after the depth's last touch in the frame: dropped, or kept
         * by the first of these that holds (the drop's own order) */
        p->afterPasses++;
        if (!drop && p->afterWhy == RD_STORE_WHY_NONE) {
            p->afterWhy = live                     ? RD_STORE_WHY_STENCIL
                          : p->last > g_rdStoreOrd ? p->why
                                                   : RD_STORE_WHY_ANY;
        }
    }
    if (drop) {
        *depthStore = RHI_STORE_DONT_CARE;
        *stencilStore = RHI_STORE_DONT_CARE;
        g_rdPerf.storeDepthDropped++;
        if (s_plan.mode == 1) {
            /* nothing in the pass reads it either */
            *depthLoad = dl == RHI_LOAD_LOAD ? RHI_LOAD_DONT_CARE : dl;
            *stencilLoad = sl == RHI_LOAD_LOAD ? RHI_LOAD_DONT_CARE : sl;
        }
    }
    if (!s_plan.poison) {
        return;
    }
    /* the tests' garbage where the plan left the depth undefined */
    const bool undef = planHas(s_plan.undef, s_plan.nUndef, tex);
    if (*depthLoad == RHI_LOAD_DONT_CARE && dl == RHI_LOAD_LOAD) {
        *depthLoad = RHI_LOAD_CLEAR;
        *clearDepth = 0.5f;
    } else if (*depthLoad == RHI_LOAD_LOAD && undef) {
        *depthLoad = RHI_LOAD_CLEAR;
        *clearDepth = 0.5f;
    }
    if ((*stencilLoad == RHI_LOAD_DONT_CARE && sl == RHI_LOAD_LOAD) ||
        (*stencilLoad == RHI_LOAD_LOAD && undef)) {
        *stencilLoad = RHI_LOAD_CLEAR;
        *clearStencil = 0x5A;
    }
    if (*depthStore == RHI_STORE_DONT_CARE) {
        planAdd(s_plan.undef, &s_plan.nUndef, tex);
    } else {
        planDel(s_plan.undef, &s_plan.nUndef, tex);
    }
}

const char *rd__store_why_text(int why)
{
    switch (why) {
    case RD_STORE_WHY_EFFECTS:
        return "kept for an effects program ([video] effects_depth)";
    case RD_STORE_WHY_KEEP:
        return "keep (a read before the clear or a readback)";
    case RD_STORE_WHY_STENCIL:
        return "stencil live (a shadow window open on it)";
    case RD_STORE_WHY_ANY:
        return "anyTouch (a command the plan does not know)";
    case RD_STORE_WHY_LAST:
        return "last touch in the last pass";
    case RD_STORE_WHY_FULL:
        return "more depths than the plan holds";
    default:
        return "none";
    }
}

/* a command type's words for the line */
static const char *cmdWords(uint8_t type, uint8_t post)
{
    switch (type) {
    case RDC_SCREEN:
        return "a screen draw";
    case RDC_MESH:
        return "a mesh draw";
    case RDC_SKINNED:
        return "a skinned mesh draw";
    case RDC_GRID:
        return "a grid draw";
    case RDC_PARTICLES:
        return "a particle draw";
    case RDC_SHADOW_RESET:
        return "the shadow reset";
    case RDC_SHADOW_STRIP:
        return "a shadow volume";
    case RDC_SHADOW_RESOLVE:
        return "the shadow resolve";
    case RDC_POST_STUB:
        return post == RD_POST_FOG ? "the fog" : "a post sprite";
    default:
        return "a command";
    }
}

/* a planned replay with a depth bound that dropped no store: why, in the
 * record (the 10 s line) and in the log (the first 8, then every 100th),
 * for SCENE's depth where the replay has it, else the first with a pass */
static void planNothingDropped(void)
{
    g_rdPerf.storeWhy = RD_STORE_WHY_NONE;
    if (s_plan.mode != 1 || g_rdPerf.storeDepthDropped != 0 || g_rdPerf.storeDepthPasses == 0) {
        return;
    }
    const PlanDepth *p = NULL;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        const PlanDepth *q = &s_plan.d[i];
        if (q->passes && (!p || q->target == RD_TARGET_SCENE + 1u)) {
            p = q;
            if (q->target == RD_TARGET_SCENE + 1u) {
                break;
            }
        }
    }
    const uint8_t why = !p               ? RD_STORE_WHY_FULL
                        : p->afterPasses ? (p->afterWhy ? p->afterWhy : RD_STORE_WHY_ANY)
                                         : RD_STORE_WHY_LAST;
    g_rdPerf.storeWhy = why;
    static uint32_t s_seen;
    s_seen++;
    if (s_seen > 8u && s_seen % 100u != 0u) {
        return;
    }
    if (!p) {
        rd__log("store plan: nothing dropped (frame %u): %u passes with a depth the plan does not "
                "hold: %s (%u so far)",
                s_plan.scanFrame, s_plan.fullPasses, rd__store_why_text(why), s_seen);
        return;
    }
    char touch[96];
    if (p->lastIn) {
        snprintf(touch, sizeof(touch), "last touched by %s in list %d at command %u",
                 cmdWords(p->lastType, p->lastPost), p->lastList, p->lastCmd);
    } else {
        snprintf(touch, sizeof(touch), "not touched in the frame");
    }
    char reason[160];
    if (why == RD_STORE_WHY_ANY && s_plan.anyTouch) {
        snprintf(reason, sizeof(reason), "anyTouch from command type %u in list %d at command %u",
                 (unsigned)s_plan.anyType, s_plan.anyList, s_plan.anyCmd);
    } else if (why == RD_STORE_WHY_KEEP && rd__store_plan_kept(p->target)) {
        uint32_t frame = 0;
        for (uint32_t j = 0; j < s_plan.nKeep; j++) {
            if (s_plan.keep[j] == p->tex) {
                frame = s_plan.keepFrame[j];
            }
        }
        snprintf(reason, sizeof(reason), "%s since frame %u", rd__store_why_text(why), frame);
    } else {
        snprintf(reason, sizeof(reason), "%s", rd__store_why_text(why));
    }
    rd__log("store plan: nothing dropped (frame %u): %s's depth %s; %u passes began after it with "
            "the depth bound (%u in all): %s (%u so far)",
            s_plan.scanFrame, rd__target_name(p->target), touch, p->afterPasses, p->passes, reason,
            s_seen);
}

/* The shadow count's colour between its reset and its resolve
 * (WORK_BUFFERS.md "The shadow count's colour").  The volumes write only the
 * stencil (rd__shadow_volume_key: colour mask 0), and the count target's
 * colour from its reset on is the resolve's: the seven draws' pass clears
 * it at its load (doShadowResolve), the one draw at scale 1 writes every
 * texel (rd_shadow_one.c, its load DONT_CARE), and above 1x the one draw
 * leaves it undrawn and every read of it as a texture until the next reset
 * takes the reduced count instead (resolveTexture, s_shadowRedFor; Shadow.c
 * is its one user: the reset, the volumes, the resolve and the chain's
 * level 0).  So nothing reads what a pass in the window loads or stores of
 * it: on a tile-based GPU its 2364x1080 colour (10 MB) is read into the
 * tiles and written back for nothing at each such pass. */
void rd__store_plan_count_color(const RdTargetRec *c, RhiColorAttachment *a)
{
    if (s_plan.mode == 0 || !c || c->named || a->load != RHI_LOAD_LOAD) {
        return;
    }
    a->load = RHI_LOAD_DONT_CARE;
    a->store = RHI_STORE_DONT_CARE;
    g_rdPerf.storeColorDropped++;
    g_rdPerf.shadowColorDropped++;
    g_rdPerf.shadowColorW = (uint16_t)c->tw;
    g_rdPerf.shadowColorH = (uint16_t)c->th;
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("store plan: the shadow count's colour (%ux%u, %.1f MB) is neither loaded nor "
                "stored while the count is made (the volumes write only the stencil; the "
                "resolve writes the colour, or the reduced count stands for it above 1x); "
                "reported once",
                c->tw, c->th, (double)c->tw * c->th * 4.0 / 1048576.0);
    }
    if (s_plan.poison) {
        /* the tests' garbage where the colour is left undefined */
        a->load = RHI_LOAD_CLEAR;
        a->clear[0] = a->clear[1] = a->clear[2] = a->clear[3] = 90.0f / 255.0f; /* 0x5A */
    }
}

void rd__store_plan_end(void)
{
    rd__fades_fold_end(); /* a fold no reduction took (rd_fades_fold.c) */
    planNothingDropped();
    s_plan.nPrevDropped = 0;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        if (s_plan.d[i].dropped) {
            planAdd(s_plan.prevDropped, &s_plan.nPrevDropped, s_plan.d[i].tex);
        }
    }
    s_plan.mode = 0;
    s_plan.n = s_plan.nUndef = 0;
}

bool rd__store_plan_kept(uint32_t target)
{
    const RdTargetRec *t = rd__target_rec(target);
    return t && t->depth.id && planHas(s_plan.keep, s_plan.nKeep, t->depth.id);
}

void rd__store_plan_readback(uint32_t target)
{
    const RdTargetRec *t = rd__target_rec(target);
    if (!g_rd.profile.storePlan || !t || !t->depth.id ||
        planHas(s_plan.keep, s_plan.nKeep, t->depth.id)) {
        return;
    }
    /* this read sees what the last replay left: where the device keeps the
     * contents (lavapipe does) whatever the plan dropped */
    planKeep(t->depth.id, target, s_plan.scanFrame);
    rd__log("store plan: %s's depth is read back: its stores are kept from now on",
            rd__target_name(target));
}
