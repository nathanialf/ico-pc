/* rd_glow_fold.c: the glow's mask folded into the reduction that reads it
 * (docs/port/RENDER_POST.md, "The glow's mask in its reduction"; on every
 * device and backend that presents, [dev] glow_fold = true|false and
 * ICO_RD_GLOW_FOLD=1|0 the A/B).
 *
 * List 7's glow (staticBlur.c makeFullScreenFlareBefore/After) fills WORK2
 * (fillWork2: one untextured sprite over the frame), draws the mask into it
 * (makeMaskPatternToWork2: SCENE 1:1, MODULATE by RGBAQ (0, 0, 0, 128),
 * the GS bilinear of SCENE's alpha, ATST LESS AREF 129 AFAIL KEEP, no
 * blend), then reduces WORK2 into WORK0 (reduceWork2ToWork0: the box above
 * 1x, the GS bilinear at 1x).  WORK2 is the frame's size: on the PowerVR
 * phone at 2364 x 1080 the mask's pass stores 10.2 MB and the reduction
 * loads them again, nothing else reading them (the title's "glow WORK2 1
 * draw 1.23 ms", glow rooms up to 4.7 ms).  The fold draws neither the fill
 * nor the mask: the reduction is drawn with SCENE as its texture through
 * fx_frame_ps with FXF_GLOW_MASK (port/shaders/fx_sprite.hlsl
 * fx_glow_texel), which computes every WORK2 texel its kernel loads as the
 * mask's sprite stored it over the fill: the mask's fragment at that texel
 * through fx_sprite_ps's own lines (coverage, U and V, the bilinear of
 * SCENE's alpha, the alpha test, FBA), the fill where the sprite left the
 * texel, then the reduction's own kernel, TFX and tail.  The same
 * operations on the same operands, so the same integers: what is left out
 * is the RGBA8 store and load between the two draws (k / 255 written,
 * floor(x * 255 + 0.5) read: k for every k).  One full-frame pass fewer a
 * glow picture (two on a desktop, where the fill's clear is a pass of its
 * own), and WORK2 is neither stored nor read.
 *
 * Where it holds (the scan, rd__glow_fold_scan_cmd, in rd_store_plan.c
 * rd__store_plan_scan's one walk of the lists; rd__glow_fold_plan the same
 * over a frame of its own, the tests):
 *   - the frame touches WORK2 (a draw into it or on its depth, its texture,
 *     a clear, a copy or exact blend naming it, a command the scan does not
 *     know) with three commands and no other: the fill, the mask and the
 *     reduction, in that order (a clear or a draw before the fill, which
 *     the fill covers, aside);
 *   - the fill: a post sprite into WORK2, untextured, no blend, no alpha
 *     test that can fail, no DATE, the colour mask whole, no Z test or Z
 *     write, the scissor the whole target, covering every texel (its GS
 *     pixels, as rd_replay.c blurSpriteClears reads them): WORK2 holds its
 *     colour (with FBA's MSB) everywhere the mask leaves;
 *   - the mask: a post sprite into WORK2 textured from SCENE's colour
 *     (RGBA32), MODULATE with RGBAQ's RGB 0 (fx_frame_ps's alpha alone, the
 *     form rd_fx_plan.c draws it in), no blend, no DATE, AFAIL not
 *     RGB_ONLY, the colour mask whole, no Z test or Z write, the scissor
 *     whole, its UV span its rect's on both axes (U(X) = u0 + (X - x0)
 *     exactly), WORK2 and SCENE of one size, scale and Full pixel crop (so
 *     the mask is at the GS's own filter, never the above-1x kernels), and
 *     no glow edge strip (rd_blur_border.c glowEdge);
 *   - nothing between the mask and the reduction writes SCENE's colour (a
 *     draw into it, a clear, a copy or exact blend naming it, a command
 *     the scan does not know; draws into other targets may come between):
 *     SCENE is read by the reduction as the mask read it;
 *   - the reduction: a post sprite (not the display's reduction) from
 *     WORK2's colour (RGBA32) into another target, no destination read (no
 *     blend, no DATE, AFAIL not RGB_ONLY), the mask's TEX0 size, no glow
 *     edge strip, not the display reduction's hardware sprite;
 *   - the switch on (rd__glow_fold_on), the fx plan on with fx_frame_ps
 *     (RdProfile.fxPlan, every class), a device, no MSAA, no pass window.
 * In 9 of the 14 corpus glow frames the sun's fans (dispSun, before the
 * mask) or the lamps' meshes (after it) draw into WORK2: the chain is
 * drawn as before there, and the frame counted kept with its reason.
 *
 * The replay: blurPostSprite (rd_blur_border.c) asks rd__glow_fold_skip
 * for every post sprite, after SCENE's border repeat (done at the mask as
 * before): the fill and the mask are not drawn, their state kept.  At the
 * reduction rd_fx_plan.c rd__fx_plan_draw asks rd__glow_fold_due, which
 * gives the mask's words for fx_glow_mask (the fill, the rect, WORK2's
 * XYOFFSET as the mask's pass has it, rd_replay.c frameRefresh, U - X and
 * V - Y, the alpha test, the flags, the crop); a reduction that cannot take
 * them (no pipeline, a target made again) draws the fill and the mask
 * first after all, then reads WORK2 as before (rd__glow_fold_undo): the
 * same bytes, and the fold off from then on.
 *
 * WORK2's own bytes are then not the mask: nothing in the frame reads them
 * (above), and the next frame's fill covers them.  A frame that reads
 * WORK2 before its fill would read the last folded frame's: such a frame
 * turns the fold off from then on (logged).  A reader outside the replay
 * (rd__read_target: the tests, the replay tool's --target and
 * --pass-dump) would see them too: with no output (a renderer whose
 * targets its caller reads) the fold is off unless ICO_RD_GLOW_FOLD=1 asks
 * for it (the A/B), and a read of WORK2 after a folded replay is logged.
 *
 * Counted (RdPerfRecord.glowFolded, glowKept, glowWhy): the 10 s line
 * "window: glow: WORK2 fold: N folded, M kept (why)" and rd_replay_tool
 * --stats; the log says the first fold and each reason once. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

enum { PHASE_IDLE, PHASE_FILL, PHASE_MASK, PHASE_DONE };

/* the scan of this replay, and the commands the walk leaves out */
static struct {
    bool scanning;
    uint8_t phase, why;
    const char *detail; /* the refusal's words (the log) */
    bool glow;          /* a draw into WORK2: a glow frame */
    bool readFirst;     /* WORK2 read before its fill */
    RdGlowFoldPlan plan;
    /* the replay */
    bool on;
    uint32_t replay;
    bool fillSkipped, maskSkipped, drawn, undoing;
    RdStateBlock fillSt, maskSt;
    int fillHalf, maskHalf;
    RdCmd fillCmd, maskCmd;
    const RdFrame *f;
    uint32_t words[RD_GLOW_FOLD_WORDS];
} s_gf;

/* off from here on (a frame that read WORK2 before its fill, a reduction
 * that did not take a planned fold) */
static bool s_off;
static int s_force = -1; /* rd__glow_fold_force: -1 the switch's */

/* a replay folded since WORK2 was last drawn (the readback's line) */
static bool s_work2Stale;

static const char *const kWhy[RD_GLOW_FOLD_WHY_COUNT] = {
    [RD_GLOW_FOLD_WHY_NONE] = "folded",
    [RD_GLOW_FOLD_WHY_FANS] = "drawn into WORK2 between its fill and the mask (the sun's fans)",
    [RD_GLOW_FOLD_WHY_LAMPS] = "drawn into WORK2 between the mask and its reduction (the lamps)",
    [RD_GLOW_FOLD_WHY_SCENE] = "SCENE drawn between the mask and its reduction",
    [RD_GLOW_FOLD_WHY_OTHER] = "WORK2 drawn or read elsewhere in the frame",
    [RD_GLOW_FOLD_WHY_FORM] = "the fill, the mask or the reduction not in the fold's form",
    [RD_GLOW_FOLD_WHY_MSAA] = "anti-aliasing (MSAA)",
    [RD_GLOW_FOLD_WHY_PLAN] = "the fx plan or fx_frame_ps missing",
    [RD_GLOW_FOLD_WHY_UNDONE] = "the reduction could not take it",
};

const char *rd_glow_fold_why_text(uint32_t why)
{
    return why < RD_GLOW_FOLD_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

void rd__glow_fold_force(int on)
{
    s_force = on;
    s_off = false;
}

bool rd__glow_fold_on(void)
{
    if (s_off) {
        return false;
    }
    if (s_force >= 0) {
        return s_force != 0;
    }
    static int env = -1;
    if (env < 0) {
        /* [dev] glow_fold (port/platform/host_config.c): 1 on, 0 off; not
           set: on where the renderer presents.  Without an output (the
           tests, rd_replay_tool's --target and --pass-dump) its caller
           reads the targets, WORK2 among them, which the fold leaves
           undrawn: off there unless asked for (the A/B) */
        const char *e = getenv("ICO_RD_GLOW_FOLD");
        env = e && e[0] == '1' ? 1 : e && e[0] == '0' ? 0 : 2;
    }
    if (env != 2) {
        return env == 1;
    }
    return g_rd.settings.outputWidth > 0 && g_rd.settings.outputHeight > 0;
}

/* ------------------------------------------------------------- the scan */

static uint32_t idOf(int named)
{
    return rd_target((RdTargetId)named).id;
}

/* the post record of command c, false when the payload does not hold it */
static bool postRec(const RdFrame *f, const RdCmd *c, RdPostRec *p)
{
    if (!f || !f->payload || (uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize) {
        return false;
    }
    memcpy(p, f->payload + c->u[1], sizeof(*p));
    return true;
}

/* the state's texture as a target's colour (RGBA view): its target, else 0 */
static uint32_t texTarget(const RdStateBlock *st)
{
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET ? t->target : 0u;
}

/* whether command c under st touches target id: *reads when it reads it
 * (its texture, a copy's source, an exact blend, the fog's texture, a
 * command the scan does not know), *writes when it may write it */
static void touch(const RdCmd *c, const RdStateBlock *st, uint32_t id, bool *reads, bool *writes)
{
    *reads = *writes = false;
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return;
    case RDC_CLEAR:
        *writes = c->u[0] == id;
        return;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        *reads = c->u[0] == id || c->u[1] == id;
        *writes = *reads;
        return;
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
        const RdTexRec *t = rd__tex_rec(st->tex);
        const bool fog = c->type == RDC_POST_STUB && c->b[0] == RD_POST_FOG;
        *reads = (st->ds.texEnabled || fog) && t && t->kind == RD_TEXKIND_TARGET && t->target == id;
        *writes = st->color == id || st->depth == id;
        *reads = *reads || st->depth == id;
        return;
    }
    default:
        *reads = *writes = true;
        return;
    }
}

/* whether command c under st may write target id's colour (a draw into
 * it, a clear, a copy or exact blend naming it, a command the scan does not
 * know); the overlay's text and the deferred text's quads (the present
 * draws them) write nothing */
static bool writesColor(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return false;
    case RDC_CLEAR:
        return c->u[0] == id;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        return c->u[0] == id || c->u[1] == id;
    case RDC_SCREEN:
        return st->color == id && !(g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS);
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_POST_STUB:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
    case RDC_WORLD_PRIMS:
        return st->color == id;
    default:
        return true;
    }
}

static bool noDepth(const RdDrawState *d)
{
    return d->zwrite == RD_ZWRITE_OFF && (!d->test.zte || d->test.ztst == RD_ZTST_ALWAYS);
}

/* a post sprite of the glow's kinds (not the display's reduction) */
static bool glowSprite(const RdCmd *c)
{
    return c->type == RDC_POST_STUB && rd__is_blur_kind(c->b[0]) && c->b[0] != RD_POST_REDUCTION;
}

/* the scissor of st covers target t whole */
static bool scissorWhole(const RdTargetRec *t, const RdStateBlock *st)
{
    RhiRect sc;
    return rd__scissor_texels(t, st, 0, &sc) && sc.x == 0 && sc.y == 0 && sc.w == t->tw &&
           sc.h == t->th;
}

/* a 12.4 coordinate held in a float that is a whole number in 0..65535 */
static bool word16(float v)
{
    return v >= 0.0f && v <= 65535.0f && v == floorf(v);
}

/* every texel of t (GS size w x h, its scale and crop) has its GS pixel
 * inside the GS frame and inside rect (12.4, x0 y0 x1 y1) under XYOFFSET
 * (ox16, oy16) in sixteenths: fx_sprite_ps's coverage (the texel's pixel
 * gb in [x0, x1)), as rd_replay.c blurSpriteClears reads it */
static bool coversTarget(const RdTargetRec *t, const float rect[4], int32_t ox16, int32_t oy16)
{
    if (t->w == 0 || t->h == 0 || t->tw == 0 || t->th == 0 || !(t->sx > 0.0f) || !(t->sy > 0.0f)) {
        return false;
    }
    /* the last texel's pixel (its left or top edge, plus the crop) inside
     * the frame, with room for the divide's rounding */
    if (((float)(t->tw - 1) + t->ox) / t->sx + 0.01f >= (float)t->w ||
        (float)(t->th - 1) / t->sy + 0.01f >= (float)t->h) {
        return false;
    }
    return (int32_t)rect[0] <= ox16 && (int32_t)rect[1] <= oy16 &&
           (int32_t)rect[2] > ox16 + 16 * ((int32_t)t->w - 1) &&
           (int32_t)rect[3] > oy16 + 16 * ((int32_t)t->h - 1);
}

/* the XYOFFSET a post sprite under st draws its target with, in
 * sixteenths (rd_replay.c frameRefresh, drawOffset: a screen copy drops
 * the half pixel sceneKeepsHalf gave the target, half) */
static void origin16(const RdStateBlock *st, int half, int32_t *ox16, int32_t *oy16)
{
    const uint32_t use = half ? st->useOffset & ~(uint32_t)RD_TARGET_HALF_Y : st->useOffset;
    *ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    *oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1)) + ((use & RD_TARGET_HALF_Y) ? 8 : 0);
}

/* the glow's edge strip (rd_blur_border.c glowEdge) the replay would draw
 * after c under st: a read of the sprite's source again */
static bool edgeStrip(const RdFrame *f, const RdCmd *c, const RdStateBlock *st)
{
    static Replay q; /* rd__replay_glow_edge_columns reads its state alone */
    q.st = *st;
    return rd__replay_glow_edge_columns(&q, f, c) > 0;
}

/* the fill: NULL (plan->fill set) or why not */
static const char *fillForm(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                            RdGlowFoldPlan *plan)
{
    const RdDrawState *d = &st->ds;
    const RdTargetRec *t = rd__target_rec(st->color);
    RdPostRec p;
    if (!t || !postRec(f, c, &p)) {
        return "the fill has no record";
    }
    if (d->texEnabled || d->abe || d->test.date != RD_DATE_OFF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->colorMask != 0xF || d->fbmsk != 0 ||
        !noDepth(d)) {
        return "the fill is textured, blends, tests or writes Z";
    }
    int32_t ox16, oy16;
    origin16(st, 0, &ox16, &oy16);
    if (!scissorWhole(t, st) || !coversTarget(t, p.rect, ox16, oy16)) {
        return "the fill leaves texels of WORK2";
    }
    plan->fill = (uint32_t)p.rgba[0] | ((uint32_t)p.rgba[1] << 8) | ((uint32_t)p.rgba[2] << 16) |
                 ((uint32_t)(p.rgba[3] | (d->fba ? 0x80u : 0u)) << 24);
    return NULL;
}

/* the mask: NULL (plan's mask fields set) or why not */
static const char *maskForm(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                            RdGlowFoldPlan *plan)
{
    const RdDrawState *d = &st->ds;
    const uint32_t sceneId = idOf(RD_TARGET_SCENE);
    const RdTargetRec *w2 = rd__target_rec(st->color), *sc = rd__target_rec(sceneId);
    const RdTexRec *t = rd__tex_rec(st->tex);
    RdPostRec p;
    if (!w2 || !sc || !postRec(f, c, &p)) {
        return "the mask has no record";
    }
    if (!t || t->kind != RD_TEXKIND_TARGET || t->target != sceneId || t->view != RD_VIEW_RGBA ||
        t->src != RD_TEXSRC_RGBA32) {
        return "the mask does not read SCENE's RGBA";
    }
    if (w2->tw != sc->tw || w2->th != sc->th || w2->w != sc->w || w2->h != sc->h ||
        w2->sx != sc->sx || w2->sy != sc->sy || w2->ox != sc->ox) {
        return "WORK2 and SCENE of another size, scale or crop";
    }
    if (d->texFn != RD_TEXFN_MODULATE || p.rgba[0] || p.rgba[1] || p.rgba[2] ||
        (p.lines & 3u) != 0u || d->abe || d->test.date != RD_DATE_OFF ||
        (d->test.ate && (d->test.afail == RD_AFAIL_RGB_ONLY || d->test.atst > 7u)) ||
        d->colorMask != 0xF || d->fbmsk != 0 || !noDepth(d)) {
        return "the mask is not SCENE's alpha alone, or blends, or writes Z";
    }
    float uv[4];
    rd__blur_uv_rect(c->b[0], &p, uv);
    for (int k = 0; k < 4; k++) {
        if (!word16(p.rect[k]) || uv[k] != floorf(uv[k]) || !(fabsf(uv[k]) < 65536.0f)) {
            return "the mask's rect or UVs off the 12.4 grid";
        }
    }
    const int32_t x0 = (int32_t)p.rect[0], y0 = (int32_t)p.rect[1];
    const int32_t x1 = (int32_t)p.rect[2], y1 = (int32_t)p.rect[3];
    const int32_t du = (int32_t)uv[0] - x0, dv = (int32_t)uv[1] - y0;
    /* U(X) = u0 + (X - x0) (u1 - u0) / (x1 - x0) is u0 + X - x0 when the
     * spans are one: the product (X - x0) d stays within 31 bits (d at
     * most 46340), and gs_div_trunc_rcp of n d by d is n */
    if (x1 <= x0 || y1 <= y0 || (int32_t)uv[2] - (int32_t)uv[0] != x1 - x0 ||
        (int32_t)uv[3] - (int32_t)uv[1] != y1 - y0 || x1 - x0 > 46340 || y1 - y0 > 46340 ||
        du < -32768 || du > 32767 || dv < -32768 || dv > 32767) {
        return "the mask's UVs not its rect's span";
    }
    if (!scissorWhole(w2, st) || edgeStrip(f, c, st)) {
        return "the mask's scissor or edge strip";
    }
    plan->rectX = (uint32_t)x0 | ((uint32_t)x1 << 16);
    plan->rectY = (uint32_t)y0 | ((uint32_t)y1 << 16);
    plan->duv = ((uint32_t)du & 0xFFFFu) | ((uint32_t)dv << 16);
    plan->test = (uint32_t)p.rgba[3] | ((uint32_t)d->test.aref << 8) |
                 ((uint32_t)d->test.atst << 16) | ((uint32_t)(d->test.ate != 0) << 20) |
                 ((uint32_t)d->test.afail << 24);
    plan->flags = rd__replay_blur_flags(d, p.lines, 1, 0) &
                  (RD_FXF_TCC | RD_FXF_LINEAR | RD_FXF_CLAMP_S | RD_FXF_CLAMP_T | RD_FXF_FBA);
    plan->texW = p.scalar[0];
    plan->texH = p.scalar[1];
    return NULL;
}

/* the reduction: NULL or why not */
static const char *reductionForm(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                                 const RdGlowFoldPlan *plan)
{
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = rd__tex_rec(st->tex);
    RdPostRec p;
    if (!postRec(f, c, &p)) {
        return "the reduction has no record";
    }
    if (st->color == idOf(RD_TARGET_WORK2) || st->color == idOf(RD_TARGET_SCENE) ||
        !rd__target_rec(st->color)) {
        return "the reduction draws into WORK2 or SCENE";
    }
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view != RD_VIEW_RGBA ||
        t->src != RD_TEXSRC_RGBA32) {
        return "the reduction does not read WORK2's RGBA";
    }
    if (d->abe || d->test.date != RD_DATE_OFF ||
        (d->test.ate && d->test.afail == RD_AFAIL_RGB_ONLY)) {
        return "the reduction reads its destination";
    }
    if (p.scalar[0] != plan->texW || p.scalar[1] != plan->texH) {
        return "the reduction's TEX0 size is not the mask's";
    }
    if (rd__blur_screen_fallback(c->b[0], &p, st) != ~0u || edgeStrip(f, c, st)) {
        return "the reduction's hardware sprite or edge strip";
    }
    return NULL;
}

static void refuse(uint8_t why, const char *detail)
{
    if (!s_gf.why) {
        s_gf.why = why;
        s_gf.detail = detail;
    }
}

void rd__glow_fold_scan_reset(void)
{
    memset(&s_gf.plan, 0, sizeof(s_gf.plan));
    s_gf.phase = PHASE_IDLE;
    s_gf.why = RD_GLOW_FOLD_WHY_NONE;
    s_gf.detail = NULL;
    s_gf.glow = s_gf.readFirst = false;
}

void rd__glow_fold_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                            uint32_t ord)
{
    if (state) {
        return;
    }
    const uint32_t w2 = idOf(RD_TARGET_WORK2);
    bool reads, writes;
    touch(c, st, w2, &reads, &writes);
    s_gf.glow = s_gf.glow || (writes && c->type == RDC_POST_STUB); /* staticBlur's sprites */
    if (!reads && !writes) {
        /* SCENE as the mask read it, for the reduction to read: draws
         * into other targets may come between */
        if (s_gf.phase == PHASE_MASK && writesColor(c, st, idOf(RD_TARGET_SCENE))) {
            refuse(RD_GLOW_FOLD_WHY_SCENE, "a draw into SCENE between the mask and its reduction");
        }
        return;
    }
    if (s_gf.why) {
        return; /* decided */
    }
    RdGlowFoldPlan *pl = &s_gf.plan;
    switch (s_gf.phase) {
    case PHASE_IDLE:
        if (glowSprite(c) && st->color == w2 && !st->ds.texEnabled) {
            const char *why = fillForm(f, c, st, pl);
            if (why) {
                refuse(RD_GLOW_FOLD_WHY_FORM, why);
                return;
            }
            pl->fillOrd = ord;
            pl->fillCmd = c;
            s_gf.phase = PHASE_FILL;
            return;
        }
        if (!reads) {
            return; /* a clear or a draw the fill covers */
        }
        s_gf.readFirst = true; /* the WORK2 an earlier replay left */
        refuse(RD_GLOW_FOLD_WHY_OTHER, "WORK2 read before the glow's fill");
        return;
    case PHASE_FILL:
        if (glowSprite(c) && st->color == w2 && texTarget(st) == idOf(RD_TARGET_SCENE)) {
            const char *why = maskForm(f, c, st, pl);
            if (why) {
                refuse(RD_GLOW_FOLD_WHY_FORM, why);
                return;
            }
            pl->maskOrd = ord;
            pl->maskCmd = c;
            s_gf.phase = PHASE_MASK;
            return;
        }
        refuse(writes && c->type != RDC_POST_STUB ? RD_GLOW_FOLD_WHY_FANS : RD_GLOW_FOLD_WHY_OTHER,
               "WORK2 touched between its fill and the mask");
        return;
    case PHASE_MASK:
        if (glowSprite(c) && !writes && texTarget(st) == w2) {
            const char *why = reductionForm(f, c, st, pl);
            if (why) {
                refuse(RD_GLOW_FOLD_WHY_FORM, why);
                return;
            }
            pl->reductionOrd = ord;
            pl->reductionCmd = c;
            s_gf.phase = PHASE_DONE;
            return;
        }
        refuse(writes ? RD_GLOW_FOLD_WHY_LAMPS : RD_GLOW_FOLD_WHY_OTHER,
               "WORK2 touched between the mask and its reduction");
        return;
    default:
        refuse(RD_GLOW_FOLD_WHY_OTHER, "WORK2 touched after its reduction");
        return;
    }
}

/* the scan's verdict: NULL (plan on) or why not */
static const char *scanVerdict(void)
{
    if (s_gf.why) {
        return s_gf.detail ? s_gf.detail : kWhy[s_gf.why];
    }
    if (s_gf.phase != PHASE_DONE) {
        if (s_gf.phase != PHASE_IDLE) {
            s_gf.why = RD_GLOW_FOLD_WHY_FORM;
            return "the glow's chain not finished in the frame";
        }
        return ""; /* no glow */
    }
    s_gf.plan.on = true;
    return NULL;
}

const char *rd__glow_fold_plan(const RdFrame *f, int keep, RdGlowFoldPlan *out, uint32_t *why)
{
    rd__glow_fold_scan_reset();
    const char *verdict = "no frame";
    if (f) {
        RdStateBlock st = f->startState;
        uint32_t ord = 0;
        for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
            const RdCmdList *list = &f->lists[l];
            for (uint32_t i = 0; i < list->count; i++, ord++) {
                const RdCmd *c = &list->cmds[i];
                const bool state = rd__apply_state(&st, c);
                rd__glow_fold_scan_cmd(f, c, &st, state, ord);
            }
        }
        verdict = scanVerdict();
    }
    *out = s_gf.plan;
    if (why) {
        *why = s_gf.why;
    }
    return verdict;
}

/* --------------------------------------------------------- the replay's */

static uint32_t s_frameNo;

/* once per reason a run (the 10 s line counts them all) */
static void logKept(uint8_t why, const char *detail)
{
    static bool said[RD_GLOW_FOLD_WHY_COUNT];
    if (why < RD_GLOW_FOLD_WHY_COUNT && !said[why]) {
        said[why] = true;
        rd__log("glow: WORK2 fold: the mask drawn into WORK2 and read by its reduction: %s (%s; "
                "frame %u; reported once for this reason)",
                kWhy[why], detail && detail[0] ? detail : "-", s_frameNo);
    }
}

bool rd__glow_fold_scan_begin(const RdFrame *f)
{
    s_gf.scanning = false;
    s_gf.on = false;
    s_gf.fillSkipped = s_gf.maskSkipped = s_gf.drawn = false;
    g_rdPerf.glowFolded = g_rdPerf.glowKept = g_rdPerf.glowWhy = 0;
    rd__glow_fold_scan_reset();
    if (!f || g_rdWin.first || !rd__glow_fold_on()) {
        return false; /* a pass window reads targets mid-frame */
    }
    s_frameNo = f->number;
    s_gf.scanning = true;
    return true;
}

void rd__glow_fold_scan_end(void)
{
    if (!s_gf.scanning) {
        return;
    }
    s_gf.scanning = false;
    if (s_gf.readFirst && s_work2Stale) {
        s_off = true;
        rd__log("glow: WORK2 fold: frame %u reads WORK2 before the glow's fill, which a folded "
                "frame before it left undrawn: the fold is off from now on",
                s_frameNo);
    }
    const char *verdict = scanVerdict();
    if (verdict && !s_gf.glow) {
        return; /* no glow in the frame: nothing to count */
    }
    if (!verdict) {
        if (rd_aa_samples(rd__aa_mode()) > 1u) {
            s_gf.why = RD_GLOW_FOLD_WHY_MSAA;
            verdict = "the MSAA epoch resolves SCENE";
        } else if (!g_rd.hasDevice || !g_rd.profile.fxPlan || !g_rd.fs[RD_FS_FX_FRAME].id) {
            s_gf.why = RD_GLOW_FOLD_WHY_PLAN;
            verdict = "[dev] fx_plan off or no fx_frame_ps";
        }
    }
    if (verdict) {
        s_work2Stale = false; /* drawn as before */
        g_rdPerf.glowKept = 1;
        g_rdPerf.glowWhy = s_gf.why ? s_gf.why : RD_GLOW_FOLD_WHY_OTHER;
        logKept((uint8_t)g_rdPerf.glowWhy, verdict);
        return;
    }
    s_gf.on = true;
    s_gf.replay = g_rd.replayCounter;
    s_gf.f = NULL;
}

static bool active(void)
{
    return s_gf.on && s_gf.replay == g_rd.replayCounter && !s_gf.undoing;
}

bool rd__glow_fold_skip(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (!active() || (c != s_gf.plan.fillCmd && c != s_gf.plan.maskCmd) ||
        (g_rdStoreOrd != s_gf.plan.fillOrd && g_rdStoreOrd != s_gf.plan.maskOrd)) {
        return false;
    }
    s_gf.f = f;
    if (c == s_gf.plan.fillCmd) {
        s_gf.fillSt = r->st;
        s_gf.fillHalf = r->halfAdded;
        s_gf.fillCmd = *c;
        s_gf.fillSkipped = true;
    } else {
        if (!s_gf.fillSkipped) {
            return false;
        }
        s_gf.maskSt = r->st;
        s_gf.maskHalf = r->halfAdded;
        s_gf.maskCmd = *c;
        s_gf.maskSkipped = true;
        /* fx_glow_mask's words (fx_sprite.hlsl): the mask pass's FrameCB as
         * rd_replay.c frameRefresh makes it (XYOFFSET, drawOffset's half,
         * vpo.z the crop's left-out texels) */
        const RdTargetRec *w2 = rd__target_rec(r->st.color);
        int32_t ox16, oy16;
        origin16(&r->st, r->halfAdded, &ox16, &oy16);
        const RdGlowFoldPlan *pl = &s_gf.plan;
        const float vpoz = w2 && w2->ox > 0.0f ? w2->ox : 0.0f, half = 0.5f;
        s_gf.words[0] = pl->fill;
        s_gf.words[1] = pl->rectX;
        s_gf.words[2] = pl->rectY;
        s_gf.words[3] = ((uint32_t)ox16 & 0xFFFFu) | ((uint32_t)oy16 << 16);
        s_gf.words[4] = pl->duv;
        s_gf.words[5] = pl->test;
        s_gf.words[6] = pl->flags;
        memcpy(&s_gf.words[7], &vpoz, sizeof(vpoz));
        memcpy(&s_gf.words[8], &half, sizeof(half));
    }
    r->writeSerial++; /* as the sprite's draw (DATE's snapshot reuse) */
    return true;
}

bool rd__glow_fold_due(Replay *r, const RdFxDraw *x, uint32_t words[RD_GLOW_FOLD_WORDS],
                       RdTargetRec **scene)
{
    if (!active() || x->c != s_gf.plan.reductionCmd || g_rdStoreOrd != s_gf.plan.reductionOrd) {
        return false;
    }
    RdTargetRec *sc = rd__target_rec(idOf(RD_TARGET_SCENE));
    const RdTargetRec *w2 = rd__target_rec(idOf(RD_TARGET_WORK2));
    const char *why = NULL;
    if (!s_gf.fillSkipped || !s_gf.maskSkipped) {
        why = "the fill or the mask was drawn";
    } else if (!sc || !w2 || x->src != w2 || !x->textured || x->dstRead || x->selfSample ||
               x->tfmt != RD_TEXSRC_RGBA32 || !sc->color.id || sc->format != RHI_FMT_RGBA8_UNORM ||
               w2->format != RHI_FMT_RGBA8_UNORM || sc->tw != w2->tw || sc->th != w2->th ||
               sc->sx != w2->sx || sc->sy != w2->sy || sc->ox != w2->ox) {
        why = "the reduction's source is not as planned";
    }
    if (why) {
        rd__glow_fold_undo(r, why);
        return false;
    }
    memcpy(words, s_gf.words, sizeof(s_gf.words));
    *scene = sc;
    return true;
}

void rd__glow_fold_drawn(void)
{
    s_gf.drawn = true;
    s_work2Stale = true;
    g_rdPerf.glowFolded = 1;
    static bool said;
    if (!said) {
        said = true;
        rd__log("glow: WORK2 fold: the glow's mask computed inside its reduction from SCENE's "
                "alpha (fx_frame_ps), WORK2 neither drawn nor read: one full-frame pass fewer "
                "(frame %u; reported once)",
                s_frameNo);
    }
}

void rd__glow_fold_undo(Replay *r, const char *why)
{
    s_off = true;
    s_gf.undoing = true;
    g_rdPerf.glowKept = 1;
    g_rdPerf.glowWhy = RD_GLOW_FOLD_WHY_UNDONE;
    rd__log("glow: WORK2 fold: %s: the fill and the mask drawn before the reduction after all "
            "(frame %u); the fold is off from now on",
            why, s_frameNo);
    if (s_gf.f && (s_gf.fillSkipped || s_gf.maskSkipped)) {
        const RdStateBlock st = r->st;
        const int half = r->halfAdded;
        if (s_gf.fillSkipped) {
            r->st = s_gf.fillSt;
            r->halfAdded = s_gf.fillHalf;
            rd__replay_blur_sprite_draw(r, s_gf.f, &s_gf.fillCmd);
        }
        if (s_gf.maskSkipped) {
            r->st = s_gf.maskSt;
            r->halfAdded = s_gf.maskHalf;
            rd__replay_blur_sprite_draw(r, s_gf.f, &s_gf.maskCmd);
        }
        r->st = st;
        r->halfAdded = half;
    }
    s_gf.fillSkipped = s_gf.maskSkipped = false;
    s_gf.undoing = false;
    s_gf.on = false;
}

void rd__glow_fold_end(void)
{
    if (active() && (s_gf.fillSkipped || s_gf.maskSkipped) && !s_gf.drawn) {
        s_off = true;
        g_rdPerf.glowKept = 1;
        g_rdPerf.glowWhy = RD_GLOW_FOLD_WHY_UNDONE;
        rd__log("glow: WORK2 fold: frame %u's reduction was not reached: WORK2 holds neither "
                "the fill nor the mask (nothing in the frame read it); the fold is off from now "
                "on",
                s_frameNo);
    }
    s_gf.on = false;
    s_gf.f = NULL;
}

void rd__glow_fold_readback(uint32_t id)
{
    if (!s_work2Stale || id != idOf(RD_TARGET_WORK2)) {
        return;
    }
    static bool said;
    if (!said) {
        said = true;
        rd__log("glow: WORK2 fold: WORK2 read back after a replay that folded the glow's mask "
                "into its reduction: it holds what was there before, not the mask "
                "(ICO_RD_GLOW_FOLD=0 draws it; reported once)");
    }
}
