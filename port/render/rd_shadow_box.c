/* rd_shadow_box.c: the shadow count and composites drawn only over the
 * shadow volumes' box (rd_shadow_box.h; docs/port/WORK_BUFFERS.md "The
 * shadows over the volumes' box").
 *
 * Shadow.c counts the volumes into the stencil of SCENE's depth, resolves
 * the count into a count target at the GS size, blurs it through SHADOW0, 1
 * and 2 (2:1 sprites, each over its whole level) and composites the three
 * levels into SCENE with three full-frame sprites under ATST GREATER 0,
 * AFAIL KEEP, Z write off.  Outside the volumes' box every one of those
 * reads zero:
 *   - the stencil's bits 0-5 are cleared at the reset (its own pass's
 *     clear, a pass's clear load over the whole depth, or the merged pass's
 *     clear: rd_pass_area.c never gives an area to a pass that clears the
 *     stencil) and written only by the volumes' INCR/DECR pipelines, inside
 *     their triangles; DATE's bit 6 is masked off by the count
 *     (shadow_count_ps's & 0x3F), so outside the triangles m is 0 and the
 *     one draw's average is +0.0: the count's texel (0, 0, 0, 0);
 *   - a level texel whose bilinear taps are all (0, 0, 0, 0) is (0, 0, 0, 0)
 *     under MODULATE or DECAL with TCC 1, no blend, FBA 0, every channel
 *     written (the sprite covers its whole level, so nothing older stays);
 *   - a composite fragment whose taps all hold alpha 0 has As 0 and fails
 *     its alpha test, which keeps the destination (AFAIL KEEP).
 * So the one draw is drawn over the count's box with its colour cleared to
 * 0 at the pass's load (the same bytes outside), the levels are drawn whole
 * (their texels outside their box are 0 either way), and each composite is
 * scissored to the box where its taps may reach a level texel that is not
 * 0, and its DATE setup (the stencil bit or the R8 snapshot) to that area.
 * No new kind of GPU work: a clear load op and narrower scissors.
 *
 * The boxes.  The count's: the strips' own 12.4 vertices (the payload the
 * replay converts; RD_SHADOW_TRIS or the float strip), clamped as convVtx
 * clamps them, relative to XYOFFSET, RD_SB_COUNT_MARGIN GS pixels wider each
 * side (the half-line offset, the texel centres' half texel, the temporal
 * jitter and the reduction's straddling texels add up to under 3.5 GS
 * pixels at a scale of 1 or more; a scale under 1 is refused).  Then through
 * each sprite (rd_sb_map_sprite): the destination pixels whose UV comes
 * within RD_SB_TEX_MARGIN texels of the source's box (bilinear taps reach
 * one texel, the B-spline two, the above-1x snap a GS pixel), with the
 * sprite's actual vertices at replay (after rd_layer and the Full pixel
 * extension).  A clamped axis whose box reaches an edge takes every UV past
 * that edge; a repeated one that reaches an edge or whose UVs leave the
 * texture is taken whole.
 *
 * Which commands are what (the scan, rd__shadow_box_scan_cmd, from the store
 * plan's walk, by the walk's ordinals): a resolve makes its colour target a
 * source; a screen sprite textured from a source (its RGBA view, an RGBA32
 * texel source) that covers its whole level at scale 1 in the level's form
 * makes that level a source; one in the composite's form is a composite;
 * any other draw on a source's target, a clear of it, a reset or strip on
 * it, and any command the scan does not know (a copy, a post sprite, an
 * exact blend) end it.  At replay a source holds its box only where its
 * producer drew this replay at the ordinal the scan named; otherwise the
 * consumer is drawn whole and counted with why: the seven draws (the box is
 * the one draw's), the MSAA epoch, no reset this replay (a keep frame's
 * later lists), the chain broken, the draw's form.  ICO_RD_SHADOW_BOX=0
 * (tests): everything whole.
 *
 * The DATE cut (rd_date_cut.c) predicts its runs from the composites in the
 * scan, not from their areas; a composite with nothing left to draw still
 * takes its DATE snapshot (bindDrawEx) and moves the write serial, as the
 * whole draw did, so its pass ends where the cut predicted. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_shadow_box.h"

static int32_t floorDiv16(int32_t a)
{
    return a >= 0 ? a / 16 : -((-a + 15) / 16);
}

static int32_t ceilDiv16(int32_t a)
{
    return -floorDiv16(-a);
}

static int32_t clamp16(int32_t v)
{
    return v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v);
}

/* ------------------------------------------------------------ geometry */

void rd_sb_extent_add(RdSbExtent *e, int32_t x16, int32_t y16, int32_t ox16, int32_t oy16)
{
    const int32_t x = clamp16(x16) - ox16, y = clamp16(y16) - oy16;
    if (!e->any) {
        e->minX = e->maxX = x;
        e->minY = e->maxY = y;
        e->any = 1;
        return;
    }
    e->minX = x < e->minX ? x : e->minX;
    e->maxX = x > e->maxX ? x : e->maxX;
    e->minY = y < e->minY ? y : e->minY;
    e->maxY = y > e->maxY ? y : e->maxY;
}

RdSbBox rd_sb_clip(RdSbBox a, uint32_t w, uint32_t h)
{
    if (a.empty || w == 0 || h == 0) {
        return (RdSbBox){0, 0, 0, 0, 1};
    }
    a.x0 = a.x0 < 0 ? 0 : a.x0;
    a.y0 = a.y0 < 0 ? 0 : a.y0;
    a.x1 = a.x1 > (int32_t)w - 1 ? (int32_t)w - 1 : a.x1;
    a.y1 = a.y1 > (int32_t)h - 1 ? (int32_t)h - 1 : a.y1;
    if (a.x1 < a.x0 || a.y1 < a.y0) {
        return (RdSbBox){0, 0, 0, 0, 1};
    }
    return a;
}

RdSbBox rd_sb_count_box(const RdSbExtent *e, uint32_t w, uint32_t h)
{
    if (!e->any) {
        return (RdSbBox){0, 0, 0, 0, 1};
    }
    const RdSbBox b = {
        floorDiv16(e->minX) - RD_SB_COUNT_MARGIN, floorDiv16(e->minY) - RD_SB_COUNT_MARGIN,
        ceilDiv16(e->maxX) + RD_SB_COUNT_MARGIN, ceilDiv16(e->maxY) + RD_SB_COUNT_MARGIN, 0};
    return rd_sb_clip(b, w, h);
}

RdSbBox rd_sb_union(RdSbBox a, RdSbBox b)
{
    if (a.empty) {
        return b;
    }
    if (b.empty) {
        return a;
    }
    a.x0 = b.x0 < a.x0 ? b.x0 : a.x0;
    a.y0 = b.y0 < a.y0 ? b.y0 : a.y0;
    a.x1 = b.x1 > a.x1 ? b.x1 : a.x1;
    a.y1 = b.y1 > a.y1 ? b.y1 : a.y1;
    return a;
}

bool rd_sb_map_axis(float xa, float xb, float ua, float ub, int32_t b0, int32_t b1, uint32_t size,
                    bool repeat, int32_t *d0, int32_t *d1)
{
    const double lo0 = (double)b0 - RD_SB_TEX_MARGIN, hi0 = (double)b1 + 1.0 + RD_SB_TEX_MARGIN;
    const double umin = ua < ub ? ua : ub, umax = ua < ub ? ub : ua;
    const double xmin = xa < xb ? xa : xb, xmax = xa < xb ? xb : xa;
    /* every pixel the sprite can cover */
    const int32_t s0 = (int32_t)floor(xmin) - 1, s1 = (int32_t)ceil(xmax) + 1;
    double lo = lo0, hi = hi0;
    if (repeat) {
        if (lo0 <= 0.0 || hi0 >= (double)size || umin < 0.0 || umax > (double)size) {
            *d0 = s0;
            *d1 = s1;
            return true; /* a tap may wrap onto the box: the whole axis */
        }
    } else {
        lo = lo0 <= 0.0 ? umin : lo; /* clamped: every UV before the edge reads it */
        hi = hi0 >= (double)size ? umax : hi;
    }
    const double a = lo > umin ? lo : umin, b = hi < umax ? hi : umax;
    if (a > b) {
        return false;
    }
    if (ua == ub) {
        *d0 = s0;
        *d1 = s1;
        return true;
    }
    const double k = ((double)xb - (double)xa) / ((double)ub - (double)ua);
    double p = (double)xa + (a - (double)ua) * k, q = (double)xa + (b - (double)ua) * k;
    if (p > q) {
        const double t = p;
        p = q;
        q = t;
    }
    int32_t e0 = (int32_t)floor(p) - RD_SB_PIX_MARGIN, e1 = (int32_t)ceil(q) + RD_SB_PIX_MARGIN;
    e0 = e0 < s0 ? s0 : e0;
    e1 = e1 > s1 ? s1 : e1;
    if (e1 < e0) {
        return false;
    }
    *d0 = e0;
    *d1 = e1;
    return true;
}

RdSbBox rd_sb_map_sprite(const RdScreenVtx *a, const RdScreenVtx *b, float uOff, float vOff,
                         int32_t ox16, int32_t oy16, const RdSbBox *src, uint32_t sw, uint32_t sh,
                         bool repS, bool repT)
{
    RdSbBox out = {0, 0, 0, 0, 1};
    if (src->empty) {
        return out;
    }
    const float xa = (float)(clamp16(a->x) - ox16) / 16.0f,
                xb = (float)(clamp16(b->x) - ox16) / 16.0f;
    const float ya = (float)(clamp16(a->y) - oy16) / 16.0f,
                yb = (float)(clamp16(b->y) - oy16) / 16.0f;
    const float ua = a->s / 16.0f + uOff, ub = b->s / 16.0f + uOff;
    const float va = a->t / 16.0f + vOff, vb = b->t / 16.0f + vOff;
    if (!rd_sb_map_axis(xa, xb, ua, ub, src->x0, src->x1, sw, repS, &out.x0, &out.x1) ||
        !rd_sb_map_axis(ya, yb, va, vb, src->y0, src->y1, sh, repT, &out.y0, &out.y1)) {
        return out;
    }
    out.empty = 0;
    return out;
}

bool rd_sb_covers(const RdScreenVtx *a, const RdScreenVtx *b, int32_t ox16, int32_t oy16,
                  uint32_t w, uint32_t h)
{
    const int32_t x0 = (a->x < b->x ? a->x : b->x) - ox16, x1 = (a->x < b->x ? b->x : a->x) - ox16;
    const int32_t y0 = (a->y < b->y ? a->y : b->y) - oy16, y1 = (a->y < b->y ? b->y : a->y) - oy16;
    return ceilDiv16(x0) <= 0 && ceilDiv16(x1) - 1 >= (int32_t)w - 1 && ceilDiv16(y0) <= 0 &&
           ceilDiv16(y1) - 1 >= (int32_t)h - 1;
}

/* ------------------------------------------------------------ the scan */

enum { ROLE_NONE, ROLE_RESOLVE, ROLE_LEVEL, ROLE_COMPOSITE, ROLE_WHOLE };

#define SB_ROLES 48
#define SB_CANDS 8
#define SB_CHAIN 16
#define SB_MADE 16

static struct {
    int env; /* ICO_RD_SHADOW_BOX: 0 off, else on */

    struct {
        uint32_t ord, src, prod;
        uint8_t role, why;
    } role[SB_ROLES];

    unsigned nRole;
    bool roleFull;

    struct {
        uint32_t target, ord;
    } cand[SB_CANDS];

    unsigned nCand;
    uint32_t chain[SB_CHAIN];
    unsigned nChain;
} s_scan = {.env = 1};

/* the replay's state: the count's window, the sources' boxes, the screen
 * draw between its two hooks */
static struct {
    uint32_t winReplay, winColor, gsW, gsH;
    bool winBad;
    RdSbExtent ext;
    bool stashBoxed;
    RdSbBox stashBox;
    uint8_t stashWhy;
    uint32_t madeReplay;

    struct {
        uint32_t target, ord;
        RdSbBox box;
        uint8_t why; /* RD_SB_WHY_COUNT: the box holds */
    } made[SB_MADE];

    unsigned nMade;
    /* doScreen between rd__shadow_box_screen and rd__shadow_box_drawn */
    uint32_t qOrd;
    uint8_t qRole, qWhy;
    uint32_t qDst;
    RdSbBox qBox;
    bool said;
    uint32_t refusals;
} s_rep;

static void candDrop(uint32_t target)
{
    for (unsigned i = 0; i < s_scan.nCand; i++) {
        if (s_scan.cand[i].target == target) {
            s_scan.cand[i] = s_scan.cand[--s_scan.nCand];
            return;
        }
    }
}

static bool candFind(uint32_t target, uint32_t *ord)
{
    for (unsigned i = 0; i < s_scan.nCand; i++) {
        if (s_scan.cand[i].target == target) {
            *ord = s_scan.cand[i].ord;
            return true;
        }
    }
    return false;
}

static void candAdd(uint32_t target, uint32_t ord)
{
    candDrop(target);
    if (s_scan.nCand < SB_CANDS) {
        s_scan.cand[s_scan.nCand].target = target;
        s_scan.cand[s_scan.nCand].ord = ord;
        s_scan.nCand++;
    }
    for (unsigned i = 0; i < s_scan.nChain; i++) {
        if (s_scan.chain[i] == target) {
            return;
        }
    }
    if (s_scan.nChain < SB_CHAIN) {
        s_scan.chain[s_scan.nChain++] = target;
    }
}

static bool inChain(uint32_t target)
{
    for (unsigned i = 0; i < s_scan.nChain; i++) {
        if (s_scan.chain[i] == target) {
            return true;
        }
    }
    return false;
}

static void roleAdd(uint32_t ord, uint8_t role, uint32_t src, uint32_t prod, uint8_t why)
{
    if (s_scan.nRole >= SB_ROLES) {
        s_scan.roleFull = true; /* the rest: no role, drawn as they are */
        return;
    }
    s_scan.role[s_scan.nRole].ord = ord;
    s_scan.role[s_scan.nRole].role = role;
    s_scan.role[s_scan.nRole].src = src;
    s_scan.role[s_scan.nRole].prod = prod;
    s_scan.role[s_scan.nRole].why = why;
    s_scan.nRole++;
}

int rd__shadow_box_role(uint32_t ord, uint32_t *src)
{
    for (unsigned i = 0; i < s_scan.nRole; i++) {
        if (s_scan.role[i].ord == ord) {
            if (src) {
                *src = s_scan.role[i].src;
            }
            return s_scan.role[i].role;
        }
    }
    return ROLE_NONE;
}

bool rd__shadow_box_scan_begin(const RdFrame *f)
{
    (void)f;
    const char *e = getenv("ICO_RD_SHADOW_BOX");
    s_scan.env = e && strcmp(e, "0") == 0 ? 0 : 1;
    s_scan.nRole = s_scan.nCand = s_scan.nChain = 0;
    s_scan.roleFull = false;
    return true;
}

void rd__shadow_box_scan_end(void)
{
    s_scan.nCand = 0;
}

/* the target a draw samples through its RGBA view, RGBA32 texels; else 0 */
static uint32_t rgbaSource(const RdStateBlock *st)
{
    if (!st->ds.texEnabled) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(st->tex);
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view != RD_VIEW_RGBA ||
        t->src != RD_TEXSRC_RGBA32) {
        return 0;
    }
    return t->target;
}

/* the sampled texel's alpha 0 is the sampled colour's alpha 0: TCC 1,
 * MODULATE ((At Av) >> 7) or DECAL (At); a sprite of uvFixed UVs, not the
 * mirrored UI, not AA1, not drawn through the frame camera or its ST shift
 * (rd_interp.c moves those on the CPU too, but the rule stays narrow) */
static bool spriteFormOk(const RdStateBlock *st, const RdCmd *c)
{
    return c->b[0] == RD_PRIM_SPRITES && c->b[2] != 0 && c->b[1] != RD_SPACE_UI && c->u[1] >= 2 &&
           c->b[4] == 0 && c->b[5] == 0 && (c->u[1] & 1u) == 0 && !st->aa1 &&
           st->ds.tcc == RD_TCC_RGBA && st->ds.texFn <= RD_TEXFN_DECAL;
}

/* a level: every channel of every pixel of its target written from the
 * source alone (no blend, FBA 0, all four channels, no alpha test, no DATE,
 * no Z test), one sprite over the whole of a target at scale 1 */
static bool levelForm(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, uint32_t src)
{
    const RdDrawState *d = &st->ds;
    const RdTargetRec *t = rd__target_rec(st->color);
    if (!spriteFormOk(st, c) || c->u[1] != 2 || d->abe || d->fba || d->colorMask != 0xF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->test.date != RD_DATE_OFF ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || !t || st->color == src ||
        t->tw != t->w || t->th != t->h) {
        return false;
    }
    if ((uint64_t)c->u[0] + 2u * sizeof(RdScreenVtx) > f->payloadSize) {
        return false;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    const int32_t ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    const int32_t oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1));
    return rd_sb_covers(&v[0], &v[1], ox16, oy16, t->w, t->h);
}

/* a composite: a fragment whose As is 0 fails the alpha test and keeps
 * the destination (AFAIL KEEP), with no Z write */
static bool compositeForm(const RdCmd *c, const RdStateBlock *st)
{
    const RdTestState *t = &st->ds.test;
    const bool killsZero =
        t->atst == RD_ATST_GREATER ||
        ((t->atst == RD_ATST_GEQUAL || t->atst == RD_ATST_EQUAL) && t->aref > 0) ||
        (t->atst == RD_ATST_NOTEQUAL && t->aref == 0);
    return spriteFormOk(st, c) && t->ate && killsZero && t->afail == RD_AFAIL_KEEP &&
           st->ds.zwrite == RD_ZWRITE_OFF;
}

void rd__shadow_box_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                             uint32_t ord)
{
    if (state) {
        return;
    }
    switch (c->type) {
    case RDC_NOP:
        return;
    case RDC_SHADOW_RESOLVE:
        candAdd(st->color, ord);
        roleAdd(ord, ROLE_RESOLVE, st->color, ord, RD_SB_WHY_COUNT);
        return;
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
        candDrop(st->color);
        return;
    case RDC_CLEAR:
        candDrop(c->u[0]);
        return;
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
        candDrop(st->color);
        return;
    case RDC_SCREEN:
        break;
    default:
        s_scan.nCand = 0; /* a copy, a post sprite, an exact blend, the unknown */
        return;
    }
    const uint32_t src = rgbaSource(st);
    uint32_t prod = 0;
    if (src && candFind(src, &prod)) {
        if (levelForm(f, c, st, src)) {
            roleAdd(ord, ROLE_LEVEL, src, prod, RD_SB_WHY_COUNT);
            candAdd(st->color, ord); /* the level: a source from here */
            return;
        }
        roleAdd(ord, compositeForm(c, st) ? ROLE_COMPOSITE : ROLE_WHOLE, src, prod, RD_SB_WHY_FORM);
    } else if (src && inChain(src)) {
        roleAdd(ord, ROLE_WHOLE, src, 0, RD_SB_WHY_CHAIN);
    }
    candDrop(st->color); /* another write to a source's target */
}

/* ----------------------------------------------------------- the replay */

static void madeReset(void)
{
    if (s_rep.madeReplay != g_rd.replayCounter) {
        s_rep.madeReplay = g_rd.replayCounter;
        s_rep.nMade = 0;
    }
}

static void madeAdd(uint32_t target, RdSbBox box, uint8_t why)
{
    madeReset();
    for (unsigned i = 0; i < s_rep.nMade; i++) {
        if (s_rep.made[i].target == target) {
            s_rep.made[i] = s_rep.made[--s_rep.nMade];
            break;
        }
    }
    if (s_rep.nMade < SB_MADE) {
        s_rep.made[s_rep.nMade].target = target;
        s_rep.made[s_rep.nMade].ord = g_rdStoreOrd;
        s_rep.made[s_rep.nMade].box = box;
        s_rep.made[s_rep.nMade].why = why;
        s_rep.nMade++;
    }
}

/* the source's box as its producer at ordinal prod drew it this replay:
 * RD_SB_WHY_COUNT with *box, else why it holds none */
static uint8_t madeFind(uint32_t target, uint32_t prod, RdSbBox *box)
{
    madeReset();
    for (unsigned i = 0; i < s_rep.nMade; i++) {
        if (s_rep.made[i].target == target && s_rep.made[i].ord == prod) {
            *box = s_rep.made[i].box;
            return s_rep.made[i].why;
        }
    }
    return RD_SB_WHY_CHAIN;
}

static const char *whyWords(int why)
{
    switch (why) {
    case RD_SB_WHY_OFF:
        return "ICO_RD_SHADOW_BOX=0";
    case RD_SB_WHY_SEVEN:
        return "the count by the seven draws";
    case RD_SB_WHY_MSAA:
        return "the count in the MSAA epoch";
    case RD_SB_WHY_WINDOW:
        return "no reset this replay, strips of another size or a scale under 1";
    case RD_SB_WHY_CHAIN:
        return "the source holds no box (written since, or not drawn)";
    case RD_SB_WHY_FORM:
        return "the draw's form (alpha test, TCC, TFX, Z write, STQ, prim)";
    default:
        return "?";
    }
}

static void whole(uint8_t why)
{
    if (why >= RD_SB_WHY_COUNT) {
        return;
    }
    g_rdPerf.shadowBoxWhy[why]++;
    if (why == RD_SB_WHY_OFF) {
        return;
    }
    s_rep.refusals++;
    if (s_rep.refusals <= 8u || s_rep.refusals % 100u == 0u) {
        rd__log("shadow box: drawn whole: %s (command %u; %u so far, the first 8 then every 100th)",
                whyWords(why), g_rdStoreOrd, s_rep.refusals);
    }
}

void rd__shadow_box_reset(uint32_t color)
{
    s_rep.winReplay = g_rd.replayCounter;
    s_rep.winColor = color;
    s_rep.winBad = false;
    s_rep.gsW = s_rep.gsH = 0;
    memset(&s_rep.ext, 0, sizeof(s_rep.ext));
}

static bool stripWindow(const RdStateBlock *st, int32_t *ox16, int32_t *oy16)
{
    if (s_rep.winReplay != g_rd.replayCounter) {
        return false;
    }
    if (s_rep.gsW == 0) {
        s_rep.gsW = st->gsW;
        s_rep.gsH = st->gsH;
    } else if (s_rep.gsW != st->gsW || s_rep.gsH != st->gsH) {
        s_rep.winBad = true;
    }
    *ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    *oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1));
    return true;
}

void rd__shadow_box_strip_v(const RdScreenVtx *v, uint32_t n, const RdStateBlock *st)
{
    int32_t ox16, oy16;
    if (!stripWindow(st, &ox16, &oy16)) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        rd_sb_extent_add(&s_rep.ext, v[i].x, v[i].y, ox16, oy16);
    }
}

void rd__shadow_box_strip_f(const float (*fv)[4], uint32_t n, const RdStateBlock *st)
{
    int32_t ox16, oy16;
    if (n < 3 || !stripWindow(st, &ox16, &oy16)) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        /* (int32_t) as doShadowStripIn converts them */
        const float x = fv[i][0], y = fv[i][1];
        if (!(x > -2.0e9f && x < 2.0e9f && y > -2.0e9f && y < 2.0e9f)) {
            s_rep.winBad = true; /* the conversion's int is not known: the count whole */
            continue;
        }
        rd_sb_extent_add(&s_rep.ext, (int32_t)x, (int32_t)y, ox16, oy16);
    }
}

bool rd__shadow_box_count_area(const RdTargetRec *tc, RhiRect *sc)
{
    s_rep.stashBoxed = false;
    s_rep.stashWhy = RD_SB_WHY_WINDOW;
    if (!s_scan.env) {
        s_rep.stashWhy = RD_SB_WHY_OFF;
        return false;
    }
    const RdTargetRec *w = rd__target_rec(s_rep.winColor);
    if (s_rep.winReplay != g_rd.replayCounter || w != tc || s_rep.winBad ||
        (s_rep.ext.any && (s_rep.gsW != tc->w || s_rep.gsH != tc->h)) || tc->sx < 1.0f ||
        tc->sy < 1.0f) {
        return false;
    }
    s_rep.stashBox = rd_sb_count_box(&s_rep.ext, tc->w, tc->h);
    s_rep.stashBoxed = true;
    *sc = s_rep.stashBox.empty ? (RhiRect){0, 0, 0, 0}
                               : (RhiRect){s_rep.stashBox.x0, s_rep.stashBox.y0,
                                           (uint32_t)(s_rep.stashBox.x1 - s_rep.stashBox.x0 + 1),
                                           (uint32_t)(s_rep.stashBox.y1 - s_rep.stashBox.y0 + 1)};
    return true;
}

void rd__shadow_box_count_done(uint32_t color)
{
    if (s_rep.stashBoxed) {
        madeAdd(color, s_rep.stashBox, RD_SB_WHY_COUNT);
        g_rdPerf.shadowBoxCounts++;
    } else {
        madeAdd(color, (RdSbBox){0, 0, 0, 0, 1}, s_rep.stashWhy);
        whole(s_rep.stashWhy);
    }
    s_rep.stashBoxed = false;
}

void rd__shadow_box_count_whole(uint32_t color, bool msaa)
{
    const uint8_t why = !s_scan.env ? RD_SB_WHY_OFF : msaa ? RD_SB_WHY_MSAA : RD_SB_WHY_SEVEN;
    madeAdd(color, (RdSbBox){0, 0, 0, 0, 1}, why);
    whole(why);
}

/* the box a sprite command's actual vertices take from the source */
static RdSbBox sprites(const RdStateBlock *st, const RdScreenVtx *v, uint32_t n, uint32_t src,
                       const RdSbBox *sb)
{
    const RdTargetRec *ts = rd__target_rec(src);
    RdSbBox out = {0, 0, 0, 0, 1};
    if (!ts || ts->w == 0 || ts->h == 0) {
        return (RdSbBox){-100000, -100000, 100000, 100000, 0}; /* unknown: everything */
    }
    const int32_t ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    const int32_t oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1));
    const float uOff = st->uvOffset[0] * (float)ts->w, vOff = st->uvOffset[1] * (float)ts->h;
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        out = rd_sb_union(out, rd_sb_map_sprite(&v[i], &v[i + 1], uOff, vOff, ox16, oy16, sb, ts->w,
                                                ts->h, st->ds.wrap.s == RD_WRAP_REPEAT,
                                                st->ds.wrap.t == RD_WRAP_REPEAT));
    }
    return out;
}

bool rd__shadow_box_screen(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v, uint32_t n,
                           RhiRect *rect)
{
    s_rep.qRole = ROLE_NONE;
    s_rep.qOrd = g_rdStoreOrd;
    uint32_t src = 0;
    const int role = rd__shadow_box_role(g_rdStoreOrd, &src);
    if (role != ROLE_LEVEL && role != ROLE_COMPOSITE) {
        if (role == ROLE_WHOLE) {
            for (unsigned i = 0; i < s_scan.nRole; i++) {
                if (s_scan.role[i].ord == g_rdStoreOrd) {
                    whole(s_scan.env ? s_scan.role[i].why : RD_SB_WHY_OFF);
                }
            }
        }
        return false;
    }
    uint32_t prod = 0;
    for (unsigned i = 0; i < s_scan.nRole; i++) {
        if (s_scan.role[i].ord == g_rdStoreOrd) {
            prod = s_scan.role[i].prod;
        }
    }
    RdSbBox sb = {0, 0, 0, 0, 1};
    uint8_t why = !s_scan.env ? RD_SB_WHY_OFF : madeFind(src, prod, &sb);
    const RdTargetRec *tc = rd__target_rec(st->color);
    if (why == RD_SB_WHY_COUNT && (!tc || tc->sx < 1.0f || tc->sy < 1.0f)) {
        why = RD_SB_WHY_FORM;
    }
    s_rep.qRole = (uint8_t)role;
    s_rep.qDst = st->color;
    s_rep.qWhy = why;
    if (role == ROLE_LEVEL) {
        /* drawn whole; its box is kept once it is drawn (rd__shadow_box_drawn) */
        if (why == RD_SB_WHY_COUNT) {
            const int32_t ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
            const int32_t oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1));
            if (n != 2 || !rd_sb_covers(&v[0], &v[1], ox16, oy16, tc->w, tc->h)) {
                s_rep.qWhy = RD_SB_WHY_FORM; /* the replayed vertices no longer cover it */
            } else {
                s_rep.qBox = rd_sb_clip(sprites(st, v, n, src, &sb), tc->w, tc->h);
            }
        }
        return false;
    }
    if (why != RD_SB_WHY_COUNT) {
        whole(why);
        s_rep.qRole = ROLE_NONE;
        return false;
    }
    (void)c;
    s_rep.qBox = rd_sb_clip(sprites(st, v, n, src, &sb), tc->w, tc->h);
    if (s_rep.qBox.empty || !rd__gs_rect_texels(tc, s_rep.qBox.x0, s_rep.qBox.y0, s_rep.qBox.x1,
                                                s_rep.qBox.y1, 0, rect)) {
        *rect = (RhiRect){0, 0, 0, 0};
    }
    if (!s_rep.said) {
        s_rep.said = true;
        rd__log("shadow box: on: the shadow count and its composites drawn over the volumes' box "
                "(the first: command %u, GS pixels %d..%d x %d..%d of %ux%u); the same bytes "
                "(reported once)",
                g_rdStoreOrd, s_rep.qBox.x0, s_rep.qBox.x1, s_rep.qBox.y0, s_rep.qBox.y1, tc->w,
                tc->h);
    }
    return true;
}

void rd__shadow_box_drawn(const RhiRect *all, const RhiRect *boxed)
{
    if (s_rep.qOrd != g_rdStoreOrd || s_rep.qRole == ROLE_NONE) {
        return;
    }
    if (s_rep.qRole == ROLE_LEVEL) {
        madeAdd(s_rep.qDst, s_rep.qWhy == RD_SB_WHY_COUNT ? s_rep.qBox : (RdSbBox){0, 0, 0, 0, 1},
                s_rep.qWhy);
    } else if (boxed) {
        g_rdPerf.shadowBoxComposites++;
        g_rdPerf.shadowBoxEmpty += boxed->w == 0 || boxed->h == 0;
        g_rdPerf.shadowBoxTexels += (uint64_t)boxed->w * boxed->h;
        g_rdPerf.shadowBoxAllTexels += (uint64_t)all->w * all->h;
    }
    s_rep.qRole = ROLE_NONE;
}

/* ------------------------------------------------------------ the lines */

static struct {
    uint64_t counts, composites, empty, texels, all, why[RD_SHADOW_BOX_WHYS];
    uint32_t replays;
} s_sum;

void rd__shadow_box_pop(const RdPerfRecord *rec)
{
    s_sum.counts += rec->shadowBoxCounts;
    s_sum.composites += rec->shadowBoxComposites;
    s_sum.empty += rec->shadowBoxEmpty;
    s_sum.texels += rec->shadowBoxTexels;
    s_sum.all += rec->shadowBoxAllTexels;
    for (int k = 0; k < RD_SHADOW_BOX_WHYS; k++) {
        s_sum.why[k] += rec->shadowBoxWhy[k];
    }
    s_sum.replays++;
}

/* "off 1.0, seven draws 0.5" (the reasons above 0), n the replays */
static void whyText(char *out, size_t size, const uint64_t *why, double n, bool perReplay)
{
    static const char *const kShort[RD_SB_WHY_COUNT] = {"off",       "seven draws", "MSAA",
                                                        "no window", "chain",       "form"};
    size_t at = 0;
    out[0] = '\0';
    for (int k = 0; k < RD_SB_WHY_COUNT && at < size; k++) {
        if (!why[k]) {
            continue;
        }
        const int w = perReplay ? snprintf(out + at, size - at, "%s%s %.1f", at ? ", " : "",
                                           kShort[k], (double)why[k] / n)
                                : snprintf(out + at, size - at, "%s%s %llu", at ? ", " : "",
                                           kShort[k], (unsigned long long)why[k]);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
}

void rd__shadow_box_text(char *out, size_t size, size_t *at)
{
    uint64_t k = 0;
    for (int i = 0; i < RD_SHADOW_BOX_WHYS; i++) {
        k += s_sum.why[i];
    }
    if (s_sum.replays == 0 || (s_sum.counts == 0 && s_sum.composites == 0 && k == 0) ||
        *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    const double n = (double)s_sum.replays;
    char why[160];
    whyText(why, sizeof why, s_sum.why, n, true);
    const int w = snprintf(out + *at, size - *at,
                           "; shadow box: %.1f counts and %.1f composites a replay over the "
                           "volumes' box (%.0f %% of their picture), %.1f whole%s%s%s",
                           (double)s_sum.counts / n, (double)s_sum.composites / n,
                           s_sum.all ? 100.0 * (double)s_sum.texels / (double)s_sum.all : 0.0,
                           (double)k / n, why[0] ? " (" : "", why, why[0] ? ")" : "");
    *at += w > 0 && (size_t)w < size - *at ? (size_t)w : 0;
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__shadow_box_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    uint64_t why[RD_SHADOW_BOX_WHYS], k = 0;
    for (int i = 0; i < RD_SHADOW_BOX_WHYS; i++) {
        why[i] = rec->shadowBoxWhy[i];
        k += why[i];
    }
    char words[160];
    whyText(words, sizeof words, why, 1.0, false);
    snprintf(out, size,
             "shadow box %s: %u counts and %u composites over the volumes' box (%u with nothing "
             "to draw; %llu of %llu texels), %llu whole%s%s%s",
             s_scan.env ? "on" : "off", rec->shadowBoxCounts, rec->shadowBoxComposites,
             rec->shadowBoxEmpty, (unsigned long long)rec->shadowBoxTexels,
             (unsigned long long)rec->shadowBoxAllTexels, (unsigned long long)k,
             words[0] ? " (" : "", words, words[0] ? ")" : "");
}
