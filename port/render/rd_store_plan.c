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
 * present reads SCENE's (RdSettings.effectsDepth), when a replay found it
 * read before its clear (a frame that draws on the depth the replay before
 * left: said once in the log, kept from then on), after a caller read it
 * back (rd__read_target_depth, the tests), and in every replay with a pass
 * window (the tool's bisect reads targets mid-frame: no plan).  The passes
 * dropped lie after the depth's last touch and draw nothing into it, so a
 * device that skips the write-back leaves the memory as the last stored
 * pass wrote it.
 *
 * ICO_RD_STORE_POISON=1 (tests): lavapipe keeps an attachment's contents
 * whatever its load and store ops say (Mesa 25.0.7, measured), so a wrong
 * plan would not show there.  With the poison every load the plan leaves
 * undefined (a load it made DONT_CARE, or a load after a store it dropped)
 * clears the depth to 0.5 and the stencil to 0x5A instead, the garbage a
 * tile-based GPU may leave: a plan that drops a store some later pass
 * reads changes the picture (rd_store_plan_ab; ICO_RD_STORE_PLAN=all, which
 * drops every depth store and keeps the loads, must change it). */
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

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
} PlanDepth;

static struct {
    uint8_t mode; /* this replay: 0 every store kept, 1 the plan, 2 every depth store dropped */
    bool poison;  /* ICO_RD_STORE_POISON=1 */
    bool full;    /* more depths than PLAN_DEPTHS: those not listed keep their stores */
    uint32_t anyTouch; /* a command the plan does not know, plus one (0: none) */
    uint32_t n;
    PlanDepth d[PLAN_DEPTHS];
    /* across replays, by texture id: the depths touched after their replay
     * (keep: from a read before the clear or a readback), those whose last
     * pass in the replay before dropped the store (the log's word), and
     * those the poison holds undefined (in this replay) */
    uint32_t keep[PLAN_DEPTHS], nKeep;
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
    p->firstClear = t->clearPending ? 0u : PLAN_NONE; /* rd__clear_new_targets clears it */
    p->firstList = -1;
    const bool effects = g_rd.settings.effectsDepth && id == RD_TARGET_SCENE + 1u;
    if (effects || planHas(s_plan.keep, s_plan.nKeep, p->tex)) {
        p->last = PLAN_AFTER;
    }
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

void rd__store_plan_scan(const RdFrame *f, int keep)
{
    s_plan.n = s_plan.nUndef = 0;
    s_plan.full = false;
    s_plan.anyTouch = 0;
    g_rdStoreOrd = 0;
    s_plan.mode = !g_rd.profile.storePlan ? 0 : g_rd.profile.storeAll ? 2 : 1;
    if (g_rdWin.first) {
        s_plan.mode = 0; /* a pass window reads targets mid-frame */
    }
    const char *poison = getenv("ICO_RD_STORE_POISON");
    s_plan.poison = poison && strcmp(poison, "1") == 0;
    g_rdPerf.storePlanMode = s_plan.mode;
    if (s_plan.mode == 0) {
        return;
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
            case RDC_MESH:
            case RDC_SKINNED:
            case RDC_GRID:
            case RDC_PARTICLES:
                planDraw(&st, ord, l);
                break;
            default:
                s_plan.anyTouch = ord + 1u; /* unknown: every depth */
                break;
            }
        }
    }
    /* a depth read before its clear reads what the replay before left: kept
     * from now on (said for the first 4 a run) */
    static int s_carriedLogged;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        PlanDepth *p = &s_plan.d[i];
        const bool carried = p->firstRead != PLAN_NONE &&
                             (p->firstClear == PLAN_NONE || p->firstRead < p->firstClear);
        if (!carried || planHas(s_plan.keep, s_plan.nKeep, p->tex)) {
            continue;
        }
        planAdd(s_plan.keep, &s_plan.nKeep, p->tex);
        p->last = PLAN_AFTER;
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
        return; /* the plan is full: kept */
    }
    const uint32_t tex = d->depth.id;
    const bool live = rd__stencil_live(rd__replay_stencil(), tex);
    const bool drop =
        s_plan.mode == 2 || (!live && p->last <= g_rdStoreOrd && s_plan.anyTouch <= g_rdStoreOrd);
    const RhiLoadOp dl = *depthLoad, sl = *stencilLoad;
    p->dropped = drop;
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

void rd__store_plan_end(void)
{
    s_plan.nPrevDropped = 0;
    for (uint32_t i = 0; i < s_plan.n; i++) {
        if (s_plan.d[i].dropped) {
            planAdd(s_plan.prevDropped, &s_plan.nPrevDropped, s_plan.d[i].tex);
        }
    }
    s_plan.mode = 0;
    s_plan.n = s_plan.nUndef = 0;
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
    planAdd(s_plan.keep, &s_plan.nKeep, t->depth.id);
    rd__log("store plan: %s's depth is read back: its stores are kept from now on",
            rd__target_name(target));
}
