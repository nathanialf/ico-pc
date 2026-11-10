/* rd_layer.c: the display layer (rd_layer.h; docs/port/DISPLAY_LAYER.md):
 * under a temporal upscaler the game's 2D layer (lists 11
 * and 12 after their last scene-class command) is drawn into COMPOSITE,
 * the upscaler's output at the display scale, and a keep frame holds the
 * composite instead of sending the picture through the upscaler again.
 * The plan per replay (rd__layer_scan), the boundary (rd__layer_due,
 * rd__layer_begin), the records lent (SCENE's to the composite, DISPLAY's
 * to PRE_DISPLAY before the boundary and to COMPOSITE_HELD for the keep
 * sprite), the keep sprite's 1:1 copy, the counters and the lines. */
#include "rd_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"
#include "rd_upscale.h"

static const char *const kSlotNames[RD_LAYER_SLOTS] = {"COMPOSITE", "COMPOSITE_HELD", "PRE_DISPLAY",
                                                       "PRE_HELD"};

static struct {
    /* this replay's plan (rd__layer_scan) */
    bool planned; /* a layer this replay, not begun yet */
    bool active;  /* begun: SCENE's record holds the composite */
    bool keep;    /* the replay is a keep frame's (lists 11-12) */
    RdLayerPlan p;
    Replay *r; /* the replay walking (from the boundary) */
    /* SCENE's record while it lends the composite's */
    RdTargetRec sceneSaved;
    uint32_t compColor; /* the composite's colour texture when lent */
    /* DISPLAY's record while it lends a slot's (RD_LAYER_SLOT_*), -1 none */
    int dispLent;
    RdTargetRec dispSaved;
    /* PRE_DISPLAY lent to DISPLAY from this ordinal (after the held copy
     * at the lists' head, RdLayerPlan.preCopyOrd), ~0u none */
    uint32_t lendAt;
    /* the keep sprite's filters, put back after it */
    bool filterSaved;
    uint8_t mag, min;
    /* across replays */
    bool held;         /* the composite holds a picture */
    bool preValid;     /* PRE_DISPLAY holds the last picture's reduction */
    uint32_t keepRun;  /* keep frames held since the last dispatch */
    bool depthCarried; /* a frame read SCENE's depth before its clear */
    uint32_t saidW, saidH;
    /* the 10 s line */
    double blockAt;
    RdUpscaleStats blockFrom;
} s_ly = {.dispLent = -1, .lendAt = ~0u};

/* ---------------------------------------------------------------- slots */

const char *rd__layer_slot_name(uint32_t slot)
{
    return slot >= (uint32_t)RD_LAYER_SLOT_FIRST && slot < (uint32_t)RD_MAX_TARGETS
               ? kSlotNames[slot - (uint32_t)RD_LAYER_SLOT_FIRST]
               : NULL;
}

RdTargetRec *rd__layer_rec(int slot)
{
    return &g_rd.targets[slot];
}

uint32_t rd__layer_id(int slot)
{
    const RdTargetRec *t = rd__layer_rec(slot);
    return t->live ? (t->gen << 16) | (uint32_t)(slot + 1) : 0u;
}

void rd__layer_slot_free(int slot)
{
    RdTargetRec *t = rd__layer_rec(slot);
    if (t->color.id || t->snap.id || t->depth.id) {
        rd__target_destroy_gpu(t); /* retired once the GPU is done with them */
    }
    const uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
}

bool rd__layer_slot_make(int slot, const RdTargetRec *g, uint32_t usage, const char *name)
{
    RdTargetRec *t = rd__layer_rec(slot);
    if (t->live && t->color.id && t->tw == g->tw && t->th == g->th) {
        t->w = g->w;
        t->h = g->h;
        t->sx = g->sx;
        t->sy = g->sy;
        t->ox = g->ox;
        return true;
    }
    rd__layer_slot_free(slot);
    uint32_t gen = (t->gen + 1u) & 0xFFFFu;
    t->gen = gen ? gen : 1u;
    t->w = g->w;
    t->h = g->h;
    t->tw = g->tw;
    t->th = g->th;
    t->sx = g->sx;
    t->sy = g->sy;
    t->ox = g->ox;
    t->format = RHI_FMT_RGBA8_UNORM;
    t->colorState = t->depthState = t->snapState = RHI_STATE_UNDEFINED;
    if (!g_rd.hasDevice) {
        t->live = 1;
        return true;
    }
    t->color = rhi_create_texture(&(RhiTextureDesc){t->tw, t->th, 1, RHI_FMT_RGBA8_UNORM,
                                                    RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED |
                                                        RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST | usage,
                                                    name, 0});
    t->live = t->color.id != 0;
    return t->live != 0;
}

void rd__layer_free(void)
{
    for (int s = RD_LAYER_SLOT_FIRST; s < RD_MAX_TARGETS; s++) {
        rd__layer_slot_free(s);
    }
    s_ly.held = s_ly.preValid = false;
    s_ly.saidW = s_ly.saidH = 0;
}

bool rd__layer_held(void)
{
    return s_ly.held;
}

void rd__layer_composite_new(void)
{
    s_ly.held = false;
}

/* ------------------------------------------------------------- the lends */

/* dst's record holds src's texture and texel geometry (no depth), its own
 * identity kept (gen, live, named, the view textures); *saved its own */
static void lend(RdTargetRec *dst, const RdTargetRec *src, RdTargetRec *saved)
{
    *saved = *dst;
    dst->w = src->w;
    dst->h = src->h;
    dst->tw = src->tw;
    dst->th = src->th;
    dst->sx = src->sx;
    dst->sy = src->sy;
    dst->ox = src->ox;
    dst->format = src->format;
    dst->color = src->color;
    dst->colorState = src->colorState;
    dst->snap = src->snap;
    dst->snapState = src->snapState;
    dst->depth = (RhiTexture){0};
    dst->depthState = RHI_STATE_UNDEFINED;
    dst->depthSampled = 0;
    dst->withDepth = 0;
    dst->dstUsage = src->dstUsage;
    dst->uintView = src->uintView;
    dst->zFormat = 0;
    dst->parked = dst->clearPending = 0;
}

/* src takes back its textures and their states from dst (a snapshot made
 * while lent, or a colour and snapshot that changed places, included);
 * dst is its own again */
static void giveBack(RdTargetRec *dst, RdTargetRec *src, const RdTargetRec *saved)
{
    src->color = dst->color;
    src->colorState = dst->colorState;
    src->snap = dst->snap;
    src->snapState = dst->snapState;
    *dst = *saved;
}

static RdTargetRec *displayRec(void)
{
    return rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
}

static RdTargetRec *sceneRec(void)
{
    return rd__target_rec(rd_target(RD_TARGET_SCENE).id);
}

static void lendDisplay(int slot)
{
    RdTargetRec *d = displayRec();
    if (!d || s_ly.dispLent >= 0) {
        return;
    }
    lend(d, rd__layer_rec(slot), &s_ly.dispSaved);
    s_ly.dispLent = slot;
}

static void giveBackDisplay(void)
{
    RdTargetRec *d = displayRec();
    if (s_ly.dispLent < 0) {
        return;
    }
    if (d) {
        giveBack(d, rd__layer_rec(s_ly.dispLent), &s_ly.dispSaved);
    }
    s_ly.dispLent = -1;
}

/* ----------------------------------------------------------- the switch */

bool rd__layer_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("ICO_RD_DISPLAY_LAYER");
        on = !(e && e[0] == '0');
    }
    return on != 0;
}

/* --------------------------------------------------------------- the plan */

const char *rd__layer_sc_name(int kind)
{
    static const char *const kNames[RD_LAYER_SC_COUNT] = {
        "none",
        "a VU draw",
        "a shadow command",
        "the fog",
        "a copy or exact blend of SCENE",
        "the Z test",
        "Z write",
        "DATE",
        "a blend reading the destination alpha",
        "PRIM.AA1",
        "SCENE's depth sampled",
        "a draw into another target sampling SCENE",
        "a command the layer does not know"};
    return kind >= 0 && kind < RD_LAYER_SC_COUNT ? kNames[kind] : "?";
}

typedef struct PlanWalk {
    RdLayerPlan *p;
    const RdFrame *f;
    uint32_t ord, scene, display, pre, preHeld;
    bool barsFree, sceneDepthCleared, keep;
    bool *carried;
    uint32_t lastScene; /* the ordinal after the last scene-class command, 0 none */
    uint32_t acts, tex, actsAt, texAt;
    /* the draws sampling SCENE in lists 11-12 (the keep copy needs none
     * between the boundary and it) */
    uint32_t reads[16], nReads;
    bool readsOver;
    int nRed;
} PlanWalk;

static bool isAction(uint8_t type)
{
    return !rd__cmd_is_state(type) && type != RDC_NOP && type != RDC_OVERLAY_TEXT;
}

/* a screen sprite covering the frame (GS 2048 -+ W / 2, H / 2) */
static bool coversFrame(const PlanWalk *w, const RdCmd *c, const RdStateBlock *st)
{
    if (c->type != RDC_SCREEN || c->b[0] != RD_PRIM_SPRITES || c->u[1] < 2u ||
        (uint64_t)c->u[0] + 2u * sizeof(RdScreenVtx) > (uint64_t)w->f->payloadSize) {
        return false;
    }
    RdScreenVtx v[2];
    memcpy(v, w->f->payload + c->u[0], sizeof(v));
    const int32_t x0 = v[0].x < v[1].x ? v[0].x : v[1].x, x1 = v[0].x < v[1].x ? v[1].x : v[0].x;
    const int32_t y0 = v[0].y < v[1].y ? v[0].y : v[1].y, y1 = v[0].y < v[1].y ? v[1].y : v[0].y;
    const int32_t fx0 = (2048 - (int32_t)(st->gsW >> 1)) * 16,
                  fy0 = (2048 - (int32_t)(st->gsH >> 1)) * 16;
    return x0 <= fx0 && y0 <= fy0 && x1 >= fx0 + (int32_t)st->gsW * 16 &&
           y1 >= fy0 + (int32_t)st->gsH * 16;
}

/* the kind of scene-class command c is under st (RD_LAYER_SC_NONE: not) */
static int sceneClassOf(PlanWalk *w, const RdCmd *c, const RdStateBlock *st)
{
    const uint32_t scene = w->scene;
    switch (c->type) {
    case RDC_CLEAR:
        return RD_LAYER_SC_NONE;
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
        return RD_LAYER_SC_VU;
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
        return RD_LAYER_SC_SHADOW;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        return c->u[0] == scene || c->u[1] == scene ? RD_LAYER_SC_COPY : RD_LAYER_SC_NONE;
    case RDC_POST_STUB:
        if (c->b[0] == RD_POST_FOG) {
            return RD_LAYER_SC_FOG;
        }
        break;
    case RDC_SCREEN:
    case RDC_WORLD_PRIMS:
        break;
    default:
        return RD_LAYER_SC_OTHER;
    }
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    const bool target = t && t->kind == RD_TEXKIND_TARGET;
    if (target && t->view == RD_VIEW_DEPTH && t->target == scene) {
        return RD_LAYER_SC_DEPTH_VIEW;
    }
    if (st->color != scene && st->depth != scene) {
        /* drawn elsewhere: scene-class when it samples SCENE (a render-scale
         * copy of the picture) */
        return target && t->target == scene ? RD_LAYER_SC_READ : RD_LAYER_SC_NONE;
    }
    const bool depth = st->depth == scene;
    const bool ztest = depth && d->test.zte && d->test.ztst != RD_ZTST_ALWAYS;
    if (ztest) {
        return RD_LAYER_SC_ZTEST;
    }
    if (depth && d->zwrite == RD_ZWRITE_ON && !w->barsFree) {
        /* a keep frame's full-frame sprites (the brightness, the keep
         * sprite, a fade: each writes Z untested) stay in the layer: the
         * next frame's head clear writes SCENE's depth over theirs, and
         * as scene-class they would refuse the keep frame (the upscaler's
         * smear of issue 203 again) */
        if (w->keep && coversFrame(w, c, st)) {
            w->p->keepZFree++;
            return RD_LAYER_SC_NONE;
        }
        return RD_LAYER_SC_ZWRITE;
    }
    if (d->test.date != RD_DATE_OFF) {
        return RD_LAYER_SC_DATE;
    }
    if (d->abe && (d->blend == RD_BLEND_CS_AD_ADD_CD || d->blend == RD_BLEND_CD_SUB_CS_AD ||
                   d->blend == RD_BLEND_LERP_AD)) {
        return RD_LAYER_SC_AD;
    }
    if (st->aa1 && depth) {
        return RD_LAYER_SC_AA1;
    }
    return RD_LAYER_SC_NONE;
}

/* the keep sprite (rd_post.c postKeep): one textured sprite into SCENE of
 * DISPLAY's RGB24 view, MODULATE, no blend, no Z test, GS 12.4 UVs, no UV
 * offset, covering the frame */
static bool isKeepSprite(const PlanWalk *w, const RdCmd *c, const RdStateBlock *st)
{
    const RdDrawState *d = &st->ds;
    if (c->type != RDC_SCREEN || c->b[0] != RD_PRIM_SPRITES || c->u[1] != 2u || c->b[2] != 1u ||
        st->color != w->scene || !d->texEnabled || d->texFn != RD_TEXFN_MODULATE || d->abe ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || d->test.date != RD_DATE_OFF ||
        st->uvOffset[0] != 0.0f || st->uvOffset[1] != 0.0f || st->aa1) {
        return false;
    }
    const RdTexRec *t = rd__tex_rec(st->tex);
    if (!t || t->kind != RD_TEXKIND_TARGET || t->target != w->display ||
        t->view != RD_VIEW_RGB24_TA0) {
        return false;
    }
    return coversFrame(w, c, st);
}

static void planCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    (void)index;
    PlanWalk *w = user;
    RdLayerPlan *p = w->p;
    const uint32_t ord = w->ord++;
    if (list >= 11 && p->list11Ord == ~0u) {
        p->list11Ord = ord;
    }
    /* SCENE's depth read before this frame clears it (the fades fold's rule,
     * rd_fades_fold.c): the letterbox bars' Z must reach it from then on */
    if (c->type == RDC_CLEAR && c->u[0] == w->scene && c->b[4]) {
        w->sceneDepthCleared = true;
    } else if (!w->sceneDepthCleared && isAction(c->type) && st->depth == w->scene &&
               ((st->ds.test.zte && st->ds.test.ztst != RD_ZTST_ALWAYS) ||
                (c->type == RDC_POST_STUB && c->b[0] == RD_POST_FOG))) {
        p->depthRead = true;
        if (!*w->carried) {
            *w->carried = true;
            w->barsFree = false;
        }
    }
    if (list < 11) {
        /* lists 0-10: what reads DISPLAY there */
        if (c->type == RDC_COPY && w->pre && w->preHeld &&
            (c->u[0] == w->pre || c->u[0] == w->preHeld) &&
            (c->u[1] == w->pre || c->u[1] == w->preHeld)) {
            p->preCopyOrd = ord;
        } else if ((c->type == RDC_COPY || c->type == RDC_EXACT_BLEND) &&
                   (c->u[0] == w->display || c->u[1] == w->display)) {
            p->displayCopied = true;
            p->feedsDisplay |= c->u[0] == w->display;
        } else if (isAction(c->type) && c->type != RDC_CLEAR) {
            const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
            p->feedsDisplay |= t && t->kind == RD_TEXKIND_TARGET && t->target == w->display;
        }
        return;
    }
    if (p->markerOrd != ~0u) {
        /* after the marker: the reduction's sprites */
        if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_REDUCTION && w->nRed < 2) {
            p->redOrd[w->nRed] = ord;
            p->red[w->nRed] = c;
            p->redSt[w->nRed] = *st;
            w->nRed++;
        }
        return;
    }
    if (c->type == RDC_UPSCALE_POINT) {
        if (list == 12 && c->u[0] == w->scene && c->u[1] == w->display) {
            p->markerOrd = ord;
            p->marker = c;
        }
        return;
    }
    if (!isAction(c->type)) {
        return;
    }
    w->acts++;
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    const bool textured =
        t != NULL && c->type != RDC_CLEAR && c->type != RDC_COPY && c->type != RDC_EXACT_BLEND;
    w->tex += textured;
    if (textured && t->kind == RD_TEXKIND_TARGET && t->target == w->scene &&
        t->view != RD_VIEW_DEPTH) {
        if (w->nReads < 16u) {
            w->reads[w->nReads++] = ord;
        } else {
            w->readsOver = true;
        }
    }
    if (p->keepAtOrd == ~0u && isKeepSprite(w, c, st)) {
        p->keepAtOrd = ord;
    }
    const int sc = sceneClassOf(w, c, st);
    if (sc != RD_LAYER_SC_NONE) {
        w->lastScene = ord + 1u;
        w->actsAt = w->acts;
        w->texAt = w->tex;
        p->late = sc;
        p->lateList = list;
        p->lateType = c->type;
    }
}

void rd__layer_plan(const RdFrame *f, int keep, bool effectsDepth, bool *depthCarried,
                    RdLayerPlan *p)
{
    memset(p, 0, sizeof(*p));
    p->list11Ord = p->markerOrd = p->boundaryOrd = p->keepOrd = p->keepAtOrd = ~0u;
    p->redOrd[0] = p->redOrd[1] = p->preCopyOrd = ~0u;
    bool carried = depthCarried ? *depthCarried : false;
    PlanWalk w;
    memset(&w, 0, sizeof(w));
    w.p = p;
    w.f = f;
    w.scene = rd_target(RD_TARGET_SCENE).id;
    w.display = rd_target(RD_TARGET_DISPLAY).id;
    w.pre = rd__layer_id(RD_LAYER_SLOT_PRE);
    w.preHeld = rd__layer_id(RD_LAYER_SLOT_PRE_HELD);
    w.carried = &carried;
    w.barsFree = !effectsDepth && !carried;
    w.keep = keep != 0;
    /* SCENE new this replay (rd__clear_new_targets clears it after this
     * scan, before the walk): a depth read before the frame's own clear
     * reads that clear, not a carried depth (the store plan's rule) */
    {
        const RdTargetRec *sr = rd__target_rec(w.scene);
        w.sceneDepthCleared = sr != NULL && sr->clearPending != 0;
    }
    RdStateBlock st = f->startState;
    rd__walk(f, keep, &st, planCmd, &w);
    p->count = w.ord;
    if (depthCarried) {
        *depthCarried = carried;
    }
    if (p->markerOrd == ~0u || p->list11Ord == ~0u) {
        p->markerOrd = ~0u;
        return;
    }
    p->boundaryOrd = w.lastScene > p->list11Ord ? w.lastScene : p->list11Ord;
    if (!w.lastScene) {
        p->late = RD_LAYER_SC_NONE;
    }
    p->draws = w.acts - w.actsAt;
    p->textured = w.tex - w.texAt;
    /* the keep copy: the keep sprite in the layer with no draw sampling
     * SCENE between the boundary and it (the held copy is the composite as
     * the boundary left it) */
    if (keep && p->keepAtOrd != ~0u && p->keepAtOrd >= p->boundaryOrd) {
        bool read = w.readsOver;
        for (uint32_t i = 0; i < w.nReads && !read; i++) {
            read = w.reads[i] >= p->boundaryOrd && w.reads[i] < p->keepAtOrd;
        }
        if (!read) {
            p->keepOrd = p->keepAtOrd;
        }
    }
}

/* ------------------------------------------------------------- the lines */

static void note(uint32_t *count, const char *fmt, const char *what, uint32_t frame)
{
    const uint32_t n = ++*count;
    if (n <= 8u || n % 100u == 0u) {
        char line[512];
        snprintf(line, sizeof(line), fmt, what);
        rd__log("display layer: frame %u: %s (%u times so far)", frame, line, n);
    }
}

static uint32_t s_nRefused, s_nLate, s_nNoKeep, s_nCopied, s_nKeepZ, s_nKeepZRefused;

static void refuse(const char *why, uint32_t frame)
{
    rd__upscale_stats_layer()->layerRefused++;
    note(&s_nRefused, "not begun (%s): lists 11-12 drawn into SCENE and upscaled with it", why,
         frame);
}

/* ------------------------------------------------------------- the replay */

void rd__layer_scan(const RdFrame *f, int keep)
{
    s_ly.planned = false;
    s_ly.keep = keep != 0;
    s_ly.r = NULL;
    s_ly.lendAt = ~0u;
    g_rdPerf.layerDraws = 0;
    if (s_ly.active || s_ly.dispLent >= 0) {
        rd__layer_end(NULL); /* a replay that ended early (never with a device lost) */
    }
    if (!f || !rd__upscale_point_on()) {
        return;
    }
    if (!rd__layer_on()) {
        static bool said;
        if (!said) {
            said = true;
            rd__log("display layer: off ([dev] display_layer = false): lists 11-12 are drawn "
                    "into SCENE and upscaled with it (the path before the layer, for an A/B)");
        }
        return;
    }
    const bool carried = s_ly.depthCarried;
    rd__layer_plan(f, keep, rd__effects_depth_wanted(), &s_ly.depthCarried, &s_ly.p);
    if (s_ly.depthCarried && !carried) {
        rd__log("display layer: SCENE's depth is read before frame %u clears it: draws that write "
                "Z without testing it (the letterbox bars) stay in SCENE from now on, but a keep "
                "frame's full-frame sprites (the brightness, the keep sprite, a fade) stay in the "
                "layer (reported once; each such keep frame is counted on its own line)",
                f->number);
    }
    if (s_ly.p.markerOrd == ~0u) {
        return; /* no reduction (the still re-present's empty keep frame, a dump before 17) */
    }
    if (g_rdWin.first) {
        refuse("the replay tool's pass window reads targets mid-frame", f->number);
        return;
    }
    if (keep && s_ly.p.keepZFree) {
        note(&s_nKeepZ,
             "%s: a keep frame's full-frame sprites that write Z kept in the layer, not in SCENE",
             s_ly.depthCarried ? "SCENE's depth carried (read before a clear)"
                               : "an effects program reads SCENE's depth",
             f->number);
    }
    if (keep && s_ly.p.keepAtOrd != ~0u && s_ly.p.keepOrd == ~0u &&
        s_ly.p.late == RD_LAYER_SC_ZWRITE && s_ly.p.keepAtOrd < s_ly.p.boundaryOrd) {
        /* a Z writer past the keep sprite that is not a full-frame sprite */
        note(&s_nKeepZRefused,
             "a keep frame refused for %s past its keep sprite while SCENE's depth is carried or "
             "read by an effects program: the keep frame is upscaled again",
             "a Z write", f->number);
    }
    if (keep && s_ly.p.keepAtOrd != ~0u && s_ly.p.keepOrd == ~0u) {
        refuse(s_ly.p.keepAtOrd < s_ly.p.boundaryOrd
                   ? "a keep frame whose keep sprite lies before a scene-class command"
                   : "a keep frame sampling SCENE before its keep sprite",
               f->number);
        return;
    }
    s_ly.planned = true;
    /* the motion blur reads PRE_DISPLAY where it reads DISPLAY: DISPLAY's
     * record lends PRE_DISPLAY's until the boundary (from after the
     * interpolator's held copy of PRE_DISPLAY at the lists' head when the
     * frame has one: rd__layer_due).  Not while the lists copy DISPLAY
     * (a held copy made before PRE_DISPLAY existed, at the display scale),
     * nor before PRE_DISPLAY holds a picture, nor while DISPLAY waits for
     * its first clear */
    const RdTargetRec *d = displayRec();
    if (!keep && s_ly.p.feedsDisplay && !s_ly.p.displayCopied && s_ly.preValid &&
        rd__layer_rec(RD_LAYER_SLOT_PRE)->live && d && !d->clearPending) {
        rd__upscale_stats_layer()->preLent++;
        if (s_ly.p.preCopyOrd == ~0u) {
            lendDisplay(RD_LAYER_SLOT_PRE);
        } else {
            s_ly.lendAt = s_ly.p.preCopyOrd + 1u;
        }
    } else if (!keep && s_ly.p.feedsDisplay && s_ly.p.displayCopied) {
        /* the interpolator's held copy was made of DISPLAY: PRE_DISPLAY and
         * PRE_HELD did not hold a picture at the tick's head (the first
         * tick under the layer, the first after the targets are made
         * again), or the frame copies DISPLAY itself.  Expected once each
         * time; a count that keeps growing is the motion blur reading the
         * upscaled picture */
        note(&s_nCopied, "%s",
             "lists 0-10 copy DISPLAY (the interpolation's held copy made before PRE_DISPLAY "
             "and PRE_HELD held a picture): the motion blur reads the upscaled DISPLAY this "
             "tick, not PRE_DISPLAY",
             f->number);
    }
}

bool rd__layer_due(uint32_t ord)
{
    if (ord >= s_ly.lendAt) {
        /* past the held copy of PRE_DISPLAY: the reads take it as DISPLAY */
        s_ly.lendAt = ~0u;
        if (s_ly.planned) {
            lendDisplay(RD_LAYER_SLOT_PRE);
        }
    }
    if (s_ly.filterSaved && ord > s_ly.p.keepOrd) {
        /* the walk is past the keep sprite: its run drawn, DISPLAY its own */
        if (s_ly.r) {
            rd__replay_flush_run(s_ly.r);
            s_ly.r->st.ds.magFilter = s_ly.mag;
            s_ly.r->st.ds.minFilter = s_ly.min;
        }
        s_ly.filterSaved = false;
        giveBackDisplay();
    }
    return s_ly.planned && ord >= s_ly.p.boundaryOrd;
}

bool rd__layer_active(void)
{
    return s_ly.active;
}

/* the reduction's sprites replayed from SCENE into PRE_DISPLAY (DISPLAY's
 * GS size at the render scale, SCENE's ox): DISPLAY's record lends
 * PRE_DISPLAY's while they draw, under each one's recorded state */
static void preDisplay(Replay *r, const RdFrame *f)
{
    const RdTargetRec *d = displayRec();
    if (!d || !s_ly.p.red[0]) {
        return;
    }
    RdTargetRec g;
    memset(&g, 0, sizeof(g));
    g.w = g_rd.gsW;
    g.h = g_rd.gsH;
    rd__target_scale_of(&g, -1); /* SCENE's: the render scale, the crop and its ox */
    const float sy = g.sy * (g_rd.fullHeight ? 2.0f : 1.0f);
    g.w = d->w;
    g.h = d->h;
    g.sy = sy;
    g.th = (uint32_t)((float)d->h * sy + 0.5f);
    g.th = g.th ? g.th : 1u;
    if (!rd__layer_slot_make(RD_LAYER_SLOT_PRE, &g, 0u, "rd pre-upscale display")) {
        return;
    }
    const RdStateBlock keep = r->st;
    rd__replay_vp_exact(true); /* the reduction's own viewport (the hook's is the plain one) */
    lendDisplay(RD_LAYER_SLOT_PRE);
    for (int i = 0; i < 2; i++) {
        if (s_ly.p.red[i]) {
            r->st = s_ly.p.redSt[i];
            rd__blur_post_sprite(r, f, s_ly.p.red[i]);
        }
    }
    rd__replay_flush_run(r);
    rd__replay_end_pass(r);
    rd__replay_vp_exact(false);
    r->st = keep;
    giveBackDisplay();
    if (rd_interpolation_active()) {
        /* the held copy of a tick's first present (rd_interp.c feedback):
         * PRE_DISPLAY's geometry (DISPLAY's GS size, the render scale,
         * SCENE's ox, sy doubled under the full height), made here because
         * the interpolation makes no GPU call */
        (void)rd__layer_feedback_ids(NULL, NULL, true);
    }
    s_ly.preValid = true;
    rd__upscale_stats_layer()->preReductions++;
}

/* the composite whole into COMPOSITE_HELD (outside any pass) */
static bool holdComposite(RdTargetRec *c)
{
    if (!rd__layer_slot_make(RD_LAYER_SLOT_HELD, c, 0u, "rd composite held")) {
        return false;
    }
    RdTargetRec *h = rd__layer_rec(RD_LAYER_SLOT_HELD);
    if (!g_rd.hasDevice) {
        return true;
    }
    const RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, c->color, &c->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, h->color, &h->colorState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, c->color, (RhiRect){0, 0, c->tw, c->th}, h->color, 0, 0);
    rd__transition(cl, h->color, &h->colorState, RHI_STATE_SHADER_READ);
    return true;
}

void rd__layer_begin(Replay *r, const RdFrame *f, bool msaa)
{
    if (!s_ly.planned) {
        return;
    }
    s_ly.planned = false; /* once a replay, whatever follows */
    const uint32_t frame = f ? f->number : 0u;
    RdUpscaleStats *st = rd__upscale_stats_layer();
    giveBackDisplay(); /* the lists before the boundary read PRE_DISPLAY */
    if (msaa) {
        refuse("SCENE is multisampled", frame);
        return;
    }
    if (!sceneRec() || !rd__upscale_composite_ensure()) {
        refuse("no COMPOSITE", frame);
        return;
    }
    if (s_ly.keep && !s_ly.held) {
        refuse("a keep frame before the composite holds a picture", frame);
        return;
    }
    RdTargetRec *c = rd__layer_rec(RD_LAYER_SLOT_COMPOSITE);
    /* a crack fill the fog armed and the walk still holds is SCENE's, before
     * the upscale (its own walk-end place reads SCENE's depth, which the
     * composite has none of) */
    rd__crack_fill_late(r, msaa);
    if (!s_ly.keep) {
        if (s_ly.p.feedsDisplay) {
            preDisplay(r, f); /* before the edge guard touches SCENE's first column */
        }
        if (!rd__upscale_dispatch_at(r, f, s_ly.p.marker, msaa, s_ly.keepRun)) {
            return; /* refused (logged there): the reduction reads SCENE */
        }
        s_ly.keepRun = 0;
    } else {
        st->keepHeld++;
        s_ly.keepRun++;
        if (s_ly.p.keepOrd == ~0u) {
            note(&s_nNoKeep, "%s",
                 "a keep frame with no keep sprite: the composite held, its layer drawn over it",
                 frame);
            st->keepNoSprite++;
        } else if (!holdComposite(c)) {
            refuse("no COMPOSITE_HELD", frame);
            return;
        }
    }
    /* SCENE's record lends the composite's from here to the replay's end */
    RdTargetRec *sc = sceneRec();
    s_ly.compColor = c->color.id;
    lend(sc, c, &s_ly.sceneSaved);
    s_ly.active = true;
    s_ly.r = r;
    r->writeSerial++; /* SCENE's colour is another texture (DATE's snapshot) */
    r->dateFor = 0;
    memset(r->frameKey, 0, sizeof(r->frameKey));
    st->layerFrames++;
    st->layerDraws += s_ly.p.draws;
    st->layerTextured += s_ly.p.textured;
    g_rdPerf.layerDraws = s_ly.p.draws;
    if (s_ly.p.late != RD_LAYER_SC_NONE) {
        st->layerLate++;
        char what[160];
        snprintf(what, sizeof(what), "%s in list %d (command type %u)",
                 rd__layer_sc_name(s_ly.p.late), s_ly.p.lateList, (unsigned)s_ly.p.lateType);
        note(&s_nLate,
             "the layer begun after a scene-class command, %s: what lies before it is upscaled "
             "with the scene",
             what, frame);
    }
    if (c->tw != s_ly.saidW || c->th != s_ly.saidH) {
        s_ly.saidW = c->tw;
        s_ly.saidH = c->th;
        rd__log("display layer: on under %s: lists 11-12 after the last scene-class command are "
                "drawn into COMPOSITE %ux%u (display scale %gx%g, Full pixel ox %g) after the "
                "upscale; keep frames hold the composite 1:1 darkened, no dispatch",
                rd__upscale_hook_name(), c->tw, c->th, (double)c->sx, (double)c->sy, (double)c->ox);
    }
}

const RdScreenVtx *rd__layer_keep_verts(Replay *r, const RdCmd *c, const RdScreenVtx *v)
{
    if (!s_ly.active || g_rdStoreOrd != s_ly.p.keepOrd || c->u[1] != 2u || s_ly.filterSaved) {
        return v;
    }
    const RdTargetRec *tc = rd__target_rec(r->st.color); /* SCENE's record: the composite */
    const RdTargetRec *h = rd__layer_rec(RD_LAYER_SLOT_HELD);
    if (!tc || !h->live) {
        return v;
    }
    /* the texture: DISPLAY's record lends COMPOSITE_HELD's (its geometry is
     * the composite's: the UVs below address its texels one for one) */
    lendDisplay(RD_LAYER_SLOT_HELD);
    s_ly.filterSaved = true;
    s_ly.mag = r->st.ds.magFilter;
    s_ly.min = r->st.ds.minFilter;
    r->st.ds.magFilter = r->st.ds.minFilter = RD_FILTER_NEAREST;
    /* the frame (GS 2048 -+ W / 2, H / 2) and a GS pixel more at each side
     * (the recorded sprite's margin: the frame covered whatever the half
     * line), less the half line the target's offset adds (RD_TARGET_HALF_Y
     * moves list 11's draws half a GS pixel down); the UVs the GS position
     * less the frame's corner, over the held composite's GS size in 12.4,
     * plus half a GS pixel: rd_replay.c snapAxis takes (s - 1) / 2s GS
     * pixels off a scaled sprite's UVs, leaving u = x + 1 / 2s, which a
     * target sampled at u * s puts on texel i's centre (i + 0.5) at any
     * scale and any Full pixel ox (at s <= 1, no snap: x + 0.5, the
     * centre too). The game's own 1:1 sprites carry the same + 0.5; a UV
     * on a texel's edge would leave NEAREST to the sampler's rounding,
     * repeated over a fade's ~256 ticks. The half line: FrameCB's
     * oy += 0.5 moves the draw up half a GS pixel, so the frame is moved
     * down by as much (the margin lies off the target) */
    static RdScreenVtx q[2];
    q[0] = v[0];
    q[1] = v[1];
    const int32_t W = (int32_t)r->st.gsW, H = (int32_t)r->st.gsH;
    const int32_t half = (r->st.useOffset & RD_TARGET_HALF_Y) ? 8 : 0;
    q[0].x = (2048 - W / 2 - 1) * 16;
    q[1].x = (2048 + W / 2 + 1) * 16;
    q[0].y = (2048 - H / 2 - 1) * 16 + half;
    q[1].y = (2048 + H / 2 + 1) * 16 + half;
    q[0].s = -0.5f * 16.0f;
    q[1].s = ((float)W + 1.5f) * 16.0f;
    q[0].t = -0.5f * 16.0f;
    q[1].t = ((float)H + 1.5f) * 16.0f;
    q[0].q = q[1].q = 1.0f;
    rd__upscale_stats_layer()->keepCopies++;
    return q;
}

/* a colour and snapshot that changed places while lent (rd_replay.c
 * blurSwapTarget): the picture copied back into the composite's own
 * texture (made with the storage flag the hook may need) */
static void keepOwnTexture(RdTargetRec *t)
{
    if (t->color.id == s_ly.compColor || t->snap.id != s_ly.compColor || !g_rd.hasDevice) {
        return;
    }
    const RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, t->snap, &t->snapState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, t->color, (RhiRect){0, 0, t->tw, t->th}, t->snap, 0, 0);
    const RhiTexture tx = t->color;
    const RhiState ts = t->colorState;
    t->color = t->snap;
    t->colorState = t->snapState;
    t->snap = tx;
    t->snapState = ts;
}

static void blockLog(void)
{
    const double now = rd__now_ms();
    if (s_ly.blockAt == 0.0) {
        s_ly.blockAt = now;
        s_ly.blockFrom = *rd__upscale_stats();
        return;
    }
    if (now - s_ly.blockAt < 10000.0) {
        return;
    }
    const RdUpscaleStats *n = rd__upscale_stats(), *o = &s_ly.blockFrom;
    const uint32_t disp = n->dispatched - o->dispatched, kept = n->keepHeld - o->keepHeld;
    if (disp || kept || n->refused != o->refused || n->layerRefused != o->layerRefused) {
        rd__log("upscale point: 10 s: %u pictures dispatched, %u keep frames held (no dispatch), "
                "resets %u (first %u, size %u, cut %u, keep run %u, camera %u, forgotten %u), "
                "display layer draws %u (textured %u, keep copies %u), %u frames with the layer "
                "begun after a scene-class command, %u pre-upscale reductions for the motion "
                "blur (read as DISPLAY by %u frames), refused %u",
                disp, kept, n->resets - o->resets,
                n->resetsBy[RD_UPSCALE_RESET_FIRST] - o->resetsBy[RD_UPSCALE_RESET_FIRST],
                n->resetsBy[RD_UPSCALE_RESET_SIZE] - o->resetsBy[RD_UPSCALE_RESET_SIZE],
                n->resetsBy[RD_UPSCALE_RESET_CUT] - o->resetsBy[RD_UPSCALE_RESET_CUT],
                n->resetsBy[RD_UPSCALE_RESET_KEEP] - o->resetsBy[RD_UPSCALE_RESET_KEEP],
                n->resetsBy[RD_UPSCALE_RESET_CAMERA] - o->resetsBy[RD_UPSCALE_RESET_CAMERA],
                n->resetsBy[RD_UPSCALE_RESET_FORGOTTEN] - o->resetsBy[RD_UPSCALE_RESET_FORGOTTEN],
                n->layerDraws - o->layerDraws, n->layerTextured - o->layerTextured,
                n->keepCopies - o->keepCopies, n->layerLate - o->layerLate,
                n->preReductions - o->preReductions, n->preLent - o->preLent,
                (n->refused - o->refused) + (n->layerRefused - o->layerRefused));
    }
    s_ly.blockAt = now;
    s_ly.blockFrom = *n;
}

void rd__layer_end(Replay *r)
{
    if (s_ly.filterSaved) {
        if (s_ly.r) {
            rd__replay_flush_run(s_ly.r);
            s_ly.r->st.ds.magFilter = s_ly.mag;
            s_ly.r->st.ds.minFilter = s_ly.min;
        }
        s_ly.filterSaved = false;
    }
    if (r) {
        rd__replay_flush_run(r);
        rd__replay_end_pass(r);
    }
    giveBackDisplay();
    if (s_ly.active) {
        RdTargetRec *sc = sceneRec();
        RdTargetRec *c = rd__layer_rec(RD_LAYER_SLOT_COMPOSITE);
        if (sc) {
            keepOwnTexture(sc);
            giveBack(sc, c, &s_ly.sceneSaved);
        }
        s_ly.active = false;
        s_ly.held = true;
        if (r) {
            r->writeSerial++;
            r->dateFor = 0;
            memset(r->frameKey, 0, sizeof(r->frameKey));
        }
    }
    s_ly.planned = false;
    s_ly.r = NULL;
    s_ly.lendAt = ~0u;
    if (r && rd__upscale_point_on()) {
        blockLog();
    }
}

bool rd__layer_feedback_ids(uint32_t *pre, uint32_t *preHeld, bool makeHeld)
{
    const RdTargetRec *p = rd__layer_rec(RD_LAYER_SLOT_PRE);
    if (makeHeld && p->live) {
        (void)rd__layer_slot_make(RD_LAYER_SLOT_PRE_HELD, p, 0u, "rd pre-upscale display held");
    }
    const uint32_t a = rd__layer_id(RD_LAYER_SLOT_PRE), b = rd__layer_id(RD_LAYER_SLOT_PRE_HELD);
    if (pre) {
        *pre = a;
    }
    if (preHeld) {
        *preHeld = b;
    }
    const RdTargetRec *h = rd__layer_rec(RD_LAYER_SLOT_PRE_HELD);
    return a && b && h->tw == p->tw && h->th == p->th && rd__upscale_point_on() && rd__layer_on();
}
