/* rd_fades_fold.c: the fades fold (docs/port/RENDER_POST.md, "The fades fold";
 * on every device, no setting: docs/port/DIVERGENCES.md VD-27 holds its
 * measured bound; ICO_RD_FADES_FOLD=0 is the tests' way to draw the
 * sprites as before, never a player's or a config key).
 *
 * List 11 ends with the game's full-frame post sprites (rd_post.c): the
 * brightness (a white sprite, ALPHA 0x44 at alpha = the step), the fade
 * (the fade colour, ALPHA 0x44 at its level) and the letterbox's two bars
 * (black, ALPHA 0x64 at FIX = the bars' level, Z write on), each a GS lerp
 * ((Cs - Cd) C >> 7) + Cd of one constant colour over SCENE.  The
 * reduction at the end of list 12 then reads all of SCENE into DISPLAY.
 * On a tile-based GPU those sprites cost a whole pass on SCENE (its colour
 * and depth loaded and stored, about 2.65 ms at Auto on the PowerVR phone)
 * when nothing else draws in list 11.  The fold draws them in the
 * reduction instead: up to four steps (the lerp's colour, its factor k,
 * the alpha it writes, the rows it covers) applied to every SCENE texel
 * the reduction reads, in the sprites' order, and the sprites themselves
 * are not drawn (rd_replay.c doScreen asks rd__fades_fold_skip).
 *
 * Where it holds (rd__fades_fold_plan, the whole frame read before the
 * walk, from rd_store_plan.c rd__store_plan_scan):
 *   - the sprites are the run at the end of list 11: after the last action
 *     that is not one of them (the deferred text's quads the present draws,
 *     the overlay text and the NOPs draw nothing into SCENE and are passed
 *     over), each one sprite into SCENE (RGBA8), untextured, one colour,
 *     ABE with a lerp (ALPHA 0x44 or 0x64; PABE with As's MSB clear draws
 *     Cs: k 0x80), k at most 0x80 (the result stays in 0..255, so COLCLAMP
 *     does not matter), the colour mask whole, no alpha test that can fail,
 *     no DATE, no AA1, no Z test, the scissor the whole target, covering
 *     every column and either every row (a full step) or the rows from the
 *     top or to the bottom (a bar); two bars of one colour, factor and
 *     alpha, one from the top and one to the bottom, are one step;
 *   - a sprite that writes Z (the bars) only while the present does not
 *     read SCENE's depth (the effects depth) and no replay found SCENE's
 *     depth read before its clear (a keep frame drawing on the depth the
 *     frame before left, rd_store_plan.c's rule): otherwise it is drawn and
 *     ends the run (the sprites after it still fold).  Nothing after the
 *     run tests SCENE's depth in this replay, by the run's construction;
 *   - list 12 holds nothing but state, NOPs, the overlay and deferred text,
 *     the reduction's black sprite (into DISPLAY) and the reduction's
 *     textured sprite reading SCENE's colour (no blend, no DATE, its tint
 *     not black: fx_frame_ps's alpha-only form never takes it), and nothing
 *     after that sprite: so SCENE's colour is read by the reduction alone
 *     after the sprites (the border's column repeat, rd_blur_border.c,
 *     copies whole columns of SCENE before the reduction, which the steps,
 *     the same on every column, commute with);
 *   - no pass window (rd_replay_tool's bisect reads SCENE mid-frame), no
 *     MSAA (the epoch resolves SCENE, a per-sample lerp is not the lerp of
 *     the resolve), at most four steps, and above 1x no bar while the
 *     reduction resamples SCENE through the above-1x kernels (their taps
 *     span several rows; the bilinear's two rows are handled, below; the
 *     kernels are foreseen as rd_replay.c doBlurSpriteDraw picks them,
 *     rd__a1x_gs_keep included);
 *   - the rounding (roundingRefusal): a step whose k is 0 or 0x80 stores
 *     the same level under any blend and filter; of the steps between, on
 *     each kind of row (between the bars, in a bar) after the last k 0x80
 *     one, any number at scale 1 under an exact blend, one at scale 1 under
 *     the fixed-function blend, none above 1x.
 *
 * The reduction (rd_replay.c doBlurSpriteDraw, rd__fades_fold_fx; or its
 * hardware sprite above 1x, rd_pixel_plan.c rd__fill_draw_cb_extra,
 * rd__fades_fold_screen):
 *   - at scale 1 (fx_sprite_ps, FXF_FOLD) the steps are applied to each of
 *     the bilinear's four taps before the GS filter (fx_sprite.hlsl
 *     fx_fold4, gs_math.hlsli gs_fold_ch): every tap is the texel SCENE
 *     would have held, so DISPLAY is the two passes' bytes when the steps
 *     compute what the sprites' blend stored.  Under an exact blend
 *     ([dev] blend = attachment or storage) the sprites store the GS's
 *     floor, and so does the step: byte for byte.  Under the fixed-function
 *     blend (the default) the sprites store the nearest level (VD-7), and
 *     the step adds 64 before its shift: the nearest level too, but for the
 *     blender's own ties and precision (a level, counted by ctest
 *     rd_fades_fold_ab; docs/port/DIVERGENCES.md VD-27), so one such step a
 *     row (two would add their levels);
 *   - above 1x (sprite_ps, ICO_DF_FOLD) the hardware filters SCENE first
 *     and the steps follow on the sampled texel (sprite.hlsl sprite_fold).
 *     Only steps of k 0 or 0x80 are taken there: the rows such a step
 *     covers hold one value, which any filter keeps, so the filter of the
 *     stepped texels is the stepped filter byte for byte.  A step between
 *     levels is not: the filter rounds its sum and the step its own
 *     product, and the two orders end two levels apart on some channels
 *     (a CPU model of a rounding and of a truncating bilinear, even for
 *     one step of a small k).  Where the filter's two rows straddle a
 *     bar's edge, each row is sampled alone, stepped by its own steps and
 *     weighed as the filter weighs them (the shader's weighing against the
 *     hardware's: a level at most, on those rows alone);
 *   - DISPLAY's alpha is the last step's (what SCENE's alpha would have
 *     been), the bars' rows SCENE's texel rows the bars covered (their
 *     corners snapped to whole GS pixels on a scaled target, as rd_replay.c
 *     snapAxis draws them: rd_fx_plan.c fxAxisCovers's mapping).
 * SCENE itself then holds the picture without the sprites (its pass dump
 * differs by design); DISPLAY, the present and every later frame read
 * DISPLAY.  A reduction that does not take the fold (a way this file does
 * not foresee) leaves the frame without the sprites: logged, and the fold
 * is off from then on (rd__fades_fold_end).
 *
 * Counters: RdPerfRecord.fadesFolded, the sprites not drawn; the stats
 * line and the 10 s pixel diet line. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* off from here on: a reduction that did not take a planned fold */
static bool s_off;

/* a replay found SCENE's depth read before its clear: the bars are drawn */
static bool s_depthCarried;

bool rd__fades_fold_on(void)
{
    static int on = -1;
    if (on < 0) {
        /* how the game draws list 11's last sprites everywhere: measured
           within two levels on DISPLAY against the sprites drawn
           (rd_fades_fold_ab, VD-27); ICO_RD_FADES_FOLD=0 draws them into
           SCENE for the tests that read SCENE back (rd_gsbase) or hold
           golden bytes of a frame with the sprites, and for the A/B */
        const char *e = getenv("ICO_RD_FADES_FOLD");
        on = !(e && e[0] == '0');
    }
    return on != 0 && !s_off;
}

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static int32_t clamp16(int32_t v)
{
    return v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v);
}

/* the texels [*first, *end) a sprite's edge pair a, b (12.4, absolute)
 * covers on one axis of a target whose GS pixel 0 is at origin16, k texels
 * a GS pixel: the corners snapped up to whole GS pixels on a scaled axis
 * (rd_replay.c snapAxis), a texel covered when its centre t + 0.5 lies in
 * [a k + 0.5, b k + 0.5) (FrameCB's half texel; the top-left rule), so
 * t in [ceil(a k), ceil(b k)): at scale 1 the GS's pixels */
static void axisTexels(int32_t a, int32_t b, int32_t origin16, int snap, double k, int32_t *first,
                       int32_t *end)
{
    if (snap) {
        a = (a + 15) & ~15;
        b = (b + 15) & ~15;
    }
    *first = (int32_t)ceil((double)(a - origin16) / 16.0 * k);
    *end = (int32_t)ceil((double)(b - origin16) / 16.0 * k);
}

/* one sprite of the run: its step and the rows it covers */
typedef struct FoldSprite {
    uint32_t word, alpha; /* R | G << 8 | B << 16 | k << 24, As with FBA's MSB */
    int side;             /* 0 every row, 1 a bar from the top, 2 a bar to the bottom */
    int32_t rows;         /* a top bar's end, a bottom bar's first row (texels) */
    uint32_t ord;
} FoldSprite;

/* not a sprite of the fold's kind at all (the run ends there, nothing to say) */
static const char *const kNotKind = "";

/* command c under state st (half: the draw offset loses its half pixel, a
 * screen copy after sceneKeepsHalf) as a sprite of the run: NULL and *out
 * filled, kNotKind, or why such a sprite cannot fold */
static const char *foldSprite(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, int half,
                              const RdFadesFoldEnv *env, FoldSprite *out)
{
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    const RdDrawState *d = &st->ds;
    if (c->type != RDC_SCREEN || c->b[0] != RD_PRIM_SPRITES || st->color != sceneId ||
        d->texEnabled || !d->abe ||
        (d->blend != RD_BLEND_LERP_AS && d->blend != RD_BLEND_LERP_AS_ALT &&
         d->blend != RD_BLEND_LERP_FIX)) {
        return kNotKind;
    }
    const RdTargetRec *sc = rd__target_rec(sceneId);
    if (!sc || sc->format != RHI_FMT_RGBA8_UNORM || sc->w == 0 || sc->h == 0) {
        return "SCENE is not an RGBA8 target";
    }
    if (c->u[1] != 2 || (uint64_t)c->u[0] + 2u * sizeof(RdScreenVtx) > f->payloadSize ||
        c->b[4] != 0) {
        return "a sprite command of more than one sprite";
    }
    RdScreenVtx v[2]; /* the colour and Z the second vertex's (flat, rd_replay.c) */
    memcpy(v, f->payload + c->u[0], sizeof(v));
    if (d->colorMask != 0xF || d->fbmsk != 0 || st->aa1 || d->test.date != RD_DATE_OFF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS)) {
        return "a sprite under a mask or a test";
    }
    if (d->zwrite == RD_ZWRITE_ON && (env->effectsDepth || env->depthCarried)) {
        return env->effectsDepth ? "a sprite writes SCENE's depth, which the effects depth reads"
                                 : "a sprite writes SCENE's depth, which a later frame reads";
    }
    const uint8_t as = v[1].rgba[3];
    uint32_t k = d->blend == RD_BLEND_LERP_FIX ? d->blendFix : as;
    if (d->pabe && !(as & 0x80u)) {
        k = 0x80; /* PABE: Cs unblended */
    }
    if (k > 0x80) {
        return "a lerp factor above 0x80";
    }
    if (st->scissor[0] > 0 || st->scissor[1] > 0 || st->scissor[2] < (int32_t)sc->w - 1 ||
        st->scissor[3] < (int32_t)sc->h - 1) {
        return "a scissor inside the frame";
    }
    /* the coverage, as rd_replay.c doScreen draws it (convVtx's clamp,
     * frameRefresh's origin with drawOffset's half pixel) */
    const int32_t x0 = clamp16(v[0].x < v[1].x ? v[0].x : v[1].x);
    const int32_t x1 = clamp16(v[0].x > v[1].x ? v[0].x : v[1].x);
    const int32_t y0 = clamp16(v[0].y < v[1].y ? v[0].y : v[1].y);
    const int32_t y1 = clamp16(v[0].y > v[1].y ? v[0].y : v[1].y);
    const uint32_t use = half && c->b[1] == RD_SPACE_FULLFRAME
                             ? st->useOffset & ~(uint32_t)RD_TARGET_HALF_Y
                             : st->useOffset;
    const int32_t ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    const int32_t oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1)) + ((use & RD_TARGET_HALF_Y) ? 8 : 0);
    if (floorDiv16(-(x0 - ox16)) < 0 || -floorDiv16(-(x1 - ox16)) < (int32_t)sc->w) {
        return "a sprite narrower than the frame";
    }
    const uint32_t th = sc->th ? sc->th : sc->h;
    int32_t first, end;
    /* snapped on a scaled axis but for the port's own text (spriteUvShift) */
    const int snap = sc->sy > 1.0f && c->b[2] != RD_UV_FIXED_CONTINUOUS;
    axisTexels(y0, y1, oy16, snap, (double)th / (double)sc->h, &first, &end);
    out->word = (uint32_t)v[1].rgba[0] | ((uint32_t)v[1].rgba[1] << 8) |
                ((uint32_t)v[1].rgba[2] << 16) | (k << 24);
    out->alpha = (uint32_t)as | (d->fba ? 0x80u : 0u);
    out->ord = 0;
    if (first <= 0 && end >= (int32_t)th) {
        out->side = 0;
        out->rows = 0;
    } else if (first <= 0 && end > 0) {
        out->side = 1;
        out->rows = end;
    } else if (end >= (int32_t)th && first < (int32_t)th) {
        out->side = 2;
        out->rows = first;
    } else {
        return "a sprite over rows in the middle of the frame";
    }
    return NULL;
}

/* rd_replay.c blurAbove1xSteps for the reduction's hardware sprite: true
 * when it leaves the GS's filter (its above-1x kernels span several rows) */
static bool reductionAbove1x(const RdTargetRec *dst, const RdTargetRec *src, const RdPostRec *p)
{
    if (!dst || !src ||
        (dst->sx == 1.0f && dst->sy == 1.0f && src->sx == 1.0f && src->sy == 1.0f)) {
        return false;
    }
    const double dx = fabs((double)p->rect[2] - (double)p->rect[0]);
    const double dy = fabs((double)p->rect[3] - (double)p->rect[1]);
    if (!(dx > 0.0) || !(dy > 0.0) || !(dst->sx > 0.0f) || !(dst->sy > 0.0f)) {
        return false;
    }
    const double kx = fabs((double)p->uv[2] - (double)p->uv[0]) / dx * src->sx / dst->sx * 32.0;
    const double ky = fabs((double)p->uv[3] - (double)p->uv[1]) / dy * src->sy / dst->sy * 32.0;
    const uint32_t sx = kx >= RD_FX_STEP_MAX ? RD_FX_STEP_MAX : (uint32_t)floor(kx + 0.5);
    const uint32_t sy = ky >= RD_FX_STEP_MAX ? RD_FX_STEP_MAX : (uint32_t)floor(ky + 0.5);
    return sx > RD_FX_BOX_MIN32 || sy > RD_FX_BOX_MIN32 || (sx > 0 && sx < RD_FX_BSPLINE_MAX32) ||
           (sy > 0 && sy < RD_FX_BSPLINE_MAX32);
}

/* the reduction's textured sprite c under st reading SCENE as the fold
 * needs: NULL, *scaled its way (the hardware sprite), or why not */
static const char *foldReduction(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                                 bool *scaled, bool *above1x)
{
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = rd__tex_rec(st->tex);
    if (!t || t->kind != RD_TEXKIND_TARGET || t->target != sceneId || t->view != RD_VIEW_RGBA ||
        st->color == sceneId) {
        return "the reduction reads another texture";
    }
    if ((uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize) {
        return "the reduction has no record";
    }
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if ((d->abe && !(d->blend == RD_BLEND_LERP_FIX && d->blendFix == 128)) ||
        d->test.date != RD_DATE_OFF || d->magFilter != RD_FILTER_LINEAR) {
        return "the reduction blends, tests DATE or is not bilinear";
    }
    if (d->texFn != RD_TEXFN_DECAL && p.rgba[0] == 0 && p.rgba[1] == 0 && p.rgba[2] == 0) {
        return "the reduction's tint is black";
    }
    *scaled = rd__blur_screen_fallback(RD_POST_REDUCTION, &p, st) != ~0u;
    /* rd_replay.c doBlurSpriteDraw: the GS's filter kept (no DF_ABOVE1X)
     * only while rd__set_a1x_gs_keep is on and the steps stay inside it */
    *above1x = *scaled && (!rd__a1x_gs_keep() || reductionAbove1x(rd__target_rec(st->color),
                                                                  rd__target_rec(sceneId), &p));
    return NULL;
}

/* a command the replay draws nothing into SCENE for */
static bool passOver(const RdCmd *c, const RdFadesFoldEnv *env)
{
    return c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT ||
           (c->type == RDC_SCREEN && env->deferText && c->b[3] == RD_SCREEN_TEXT_QUADS);
}

/* does c read SCENE's depth (a test, a DATE, the fog, a depth view)? */
static bool readsSceneDepth(const RdCmd *c, const RdStateBlock *st, uint32_t sceneId)
{
    const RdDrawState *d = &st->ds;
    if (c->type == RDC_CLEAR || c->type == RDC_COPY || c->type == RDC_EXACT_BLEND ||
        c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
        return false;
    }
    const bool test = d->test.zte && d->test.ztst != RD_ZTST_ALWAYS;
    if (st->depth == sceneId && (test || c->type == RDC_SHADOW_STRIP ||
                                 (c->type == RDC_POST_STUB && c->b[0] == RD_POST_FOG))) {
        return true;
    }
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET && t->view == RD_VIEW_DEPTH && t->target == sceneId;
}

/* The rounding (the file comment's "the rounding"): why the steps p (n,
 * steps, sides filled) cannot give the bytes the sprites would, or NULL.
 * A step whose k is 0 (Cd kept) or 0x80 (Cs) stores the same level under
 * every blend and filter, a constant over its rows; one between them
 * rounds.  Per kind of row (between the bars, in a bar) the steps between
 * levels since the last k 0x80 one are counted: on the GS sprite model
 * under an exact blend any number (each tap is the texel SCENE would have
 * held, byte for byte); under the fixed-function blend one (each rounds to
 * the nearest level where the blender rounds by its own precision: a level
 * apart at most for one step, two after two); after the hardware filter
 * (scaled) none (the filter's rounding and the step's do not commute: two
 * levels apart on some channels for a single step whatever the filter's
 * own rounding). */
static const char *roundingRefusal(const RdFadesFold *p, bool exact, int scaled)
{
    if (exact && !scaled) {
        return NULL;
    }
    for (int bar = 0; bar < 2; bar++) {
        uint32_t between = 0;
        for (uint32_t m = 0; m < p->n; m++) {
            if (p->sides[m] != 0 && !bar) {
                continue; /* a bar step: not on the rows between the bars */
            }
            const uint32_t k = p->steps[m] >> 24;
            if (k == 0x80u) {
                between = 0;
            } else if (k != 0u) {
                between++;
            }
        }
        if (scaled && between > 0) {
            return "a step between two levels after the hardware filter (above 1x), whose "
                   "rounding is not the sprite's";
        }
        if (between > 1) {
            return "two steps between levels on one row under the fixed-function blend";
        }
    }
    return NULL;
}

const char *rd__fades_fold_plan(const RdFrame *f, int keep, const RdFadesFoldEnv *env,
                                RdFadesFold *out)
{
    memset(out, 0, sizeof(*out));
    out->bars = 0xFFFF0000u; /* no bar */
    if (!f) {
        return "no frame";
    }
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *sc = rd__target_rec(sceneId);
    const uint32_t headHalf = rd__replay_scene_head_half(f);
    RdStateBlock st = f->startState;
    int half = 0;
    bool depthCleared = sc && sc->clearPending;
    FoldSprite run[RD_FADES_FOLD_SPRITES];
    uint32_t n = 0;
    const char *why = NULL; /* the run's last refusal (a sprite of the kind drawn) */
    const char *reduction = "no reduction reads SCENE after them";
    bool reduced = false, scaled = false, above1x = false;
    const char *verdict = NULL; /* no fold, decided: the walk goes on for the depth's reads */
    uint32_t ord = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++, ord++) {
            const RdCmd *c = &list->cmds[i];
            if (rd__apply_state(&st, c)) {
                if (c->type == RDC_TARGET) {
                    half = rd__replay_scene_keeps_half(&st, l, headHalf);
                }
                continue;
            }
            if (passOver(c, env)) {
                continue;
            }
            if (c->type == RDC_CLEAR && c->b[4] && c->u[0] == sceneId) {
                depthCleared = true;
            } else if (!depthCleared && !out->depthRead && readsSceneDepth(c, &st, sceneId)) {
                out->depthRead = 1; /* the depth the replay before left */
                out->depthReadList = l;
            }
            if (l < 11 || verdict) {
                continue; /* the depth's reads alone from here */
            }
            if (l == 11) {
                FoldSprite s;
                const char *r = foldSprite(f, c, &st, half, env, &s);
                if (r == NULL && n < RD_FADES_FOLD_SPRITES) {
                    s.ord = ord;
                    run[n++] = s;
                    why = NULL;
                    continue;
                }
                why = r == NULL ? "more than eight sprites" : (r[0] ? r : NULL);
                n = 0;
                continue;
            }
            /* list 12: the reduction, and nothing else that draws */
            if (n == 0) {
                verdict = why ? why : kNotKind; /* list 11 ends with no such sprite */
                continue;
            }
            if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_REDUCTION && !reduced) {
                if (!st.ds.texEnabled) {
                    if (st.color == sceneId) {
                        verdict = "the reduction clears SCENE";
                    }
                    continue; /* its black sprite into DISPLAY */
                }
                reduction = foldReduction(f, c, &st, &scaled, &above1x);
                if (reduction) {
                    verdict = reduction;
                    continue;
                }
                reduced = true;
                out->reductionOrd = ord;
                continue;
            }
            verdict = reduced ? "list 12 draws after the reduction"
                              : "list 12 draws before the reduction";
        }
    }
    if (verdict) {
        return verdict;
    }
    if (n == 0) {
        return why ? why : kNotKind;
    }
    if (!reduced) {
        return reduction;
    }
    if (env->msaa) {
        return "anti-aliasing (MSAA) resolves SCENE";
    }
    /* the steps: two bars of one step, one from the top and one to the
     * bottom, are one step; every bar step covers the same rows */
    int sides = 0;
    int32_t top = 0, bottom = 0xFFFF;
    for (uint32_t i = 0; i < n; i++) {
        const FoldSprite *s = &run[i];
        if (s->side == 1) {
            if (top && top != s->rows) {
                return "bars of two heights";
            }
            top = s->rows;
        } else if (s->side == 2) {
            if (bottom != 0xFFFF && bottom != s->rows) {
                return "bars of two heights";
            }
            bottom = s->rows;
        }
        const uint32_t m = out->n;
        const bool joins = m > 0 && s->side != 0 && (out->sides[m - 1] | s->side) == 3 &&
                           out->sides[m - 1] != 3 && out->sides[m - 1] != 0 &&
                           out->steps[m - 1] == s->word &&
                           ((out->alphas >> (8u * (m - 1))) & 255u) == s->alpha;
        if (joins) {
            out->sides[m - 1] = 3;
            continue;
        }
        if (m == RD_FADES_FOLD_STEPS) {
            return "more than four steps";
        }
        out->steps[m] = s->word;
        out->alphas |= s->alpha << (8u * m);
        out->sides[m] = (uint8_t)s->side;
        out->n = m + 1;
    }
    for (uint32_t m = 0; m < out->n; m++) {
        if (out->sides[m] == 0) {
            continue;
        }
        if (sides && sides != out->sides[m]) {
            return "bar steps of different bars";
        }
        sides = out->sides[m];
        out->meta |= 1u << (4u + m);
    }
    if (sides && top > bottom) {
        return "the bars overlap";
    }
    if (sides && scaled && above1x) {
        return "the reduction resamples SCENE over several rows (above 1x) under the bars";
    }
    const char *rounding = roundingRefusal(out, env->exact, scaled);
    if (rounding) {
        return rounding;
    }
    out->meta |= out->n | (env->exact ? 0u : 8u);
    out->bars = (uint32_t)(sides & 1 ? top : 0) | ((uint32_t)(sides & 2 ? bottom : 0xFFFF) << 16);
    out->scaled = scaled;
    out->sprites = n;
    for (uint32_t i = 0; i < n; i++) {
        out->ords[i] = run[i].ord;
    }
    out->on = true;
    return NULL;
}

/* this replay's fold (rd__fades_fold_scan) */
static RdFadesFold s_fold;
static uint32_t s_applied; /* the reduction sprites that took it in this replay */

/* the frame number of the replay planned (the log lines) */
static uint32_t s_frame;

static void logRefusal(const char *why)
{
    static uint32_t s_count;
    s_count++;
    if (s_count <= 8 || s_count % 100 == 0) {
        rd__log("fades fold: list 11's last sprites are drawn into SCENE: %s (frame %u; the first "
                "8, then every 100th: %u so far)",
                why, s_frame, s_count);
    }
}

/* whether the replay's sprites blend exactly (rd_dst_read.c: the GS's
 * floor) or through the fixed-function blender (the nearest level): the
 * asked exact mode's own usage bit on an RGBA8 SCENE, RD_DST_USAGE_STORAGE
 * under storage, RD_DST_USAGE_ATTACHMENT under attachment (rd_core.c gives
 * a target the asked mode's bit alone) */
bool rd__fades_fold_exact(int mode, uint32_t usage, bool rgba8)
{
    if (!rgba8) {
        return false;
    }
    if (mode == RD_BLEND_EXACT_STORAGE) {
        return (usage & RD_DST_USAGE_STORAGE) != 0u;
    }
    /* rd_replay.c exactReadable: every pass on such a target begun readable */
    return mode == RD_BLEND_EXACT_ATTACHMENT && (usage & RD_DST_USAGE_ATTACHMENT) != 0u;
}

static bool sceneBlendExact(const RdTargetRec *sc)
{
    return sc &&
           rd__fades_fold_exact(g_rd.dstMode, sc->dstUsage, sc->format == RHI_FMT_RGBA8_UNORM);
}

void rd__fades_fold_scan(const RdFrame *f, int keep)
{
    s_fold.on = false;
    s_applied = 0;
    g_rdPerf.fadesFolded = 0;
    if (!f || !rd__fades_fold_on() || g_rdWin.first) {
        return;
    }
    if (rd__uber_on()) {
        /* GS state as shader data (rd_uber.c) draws the reduction's
           hardware sprite through the attachment read's twin, which has no
           fold (sprite.hlsl sprite_fold is the plain build's) */
        static bool said; /* once a run */
        if (!said) {
            said = true;
            rd__log("fades fold: off under GS state as shader data (the reduction's sprite "
                    "draws through the colour read, which has no fold); list 11's sprites are "
                    "drawn into SCENE");
        }
        return;
    }
    s_frame = f->number;
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    RdFadesFoldEnv env;
    memset(&env, 0, sizeof(env));
    env.effectsDepth = rd__effects_depth_wanted(); /* the present reads it */
    env.depthCarried = s_depthCarried;
    env.deferText = g_rd.deferText;
    env.exact = sceneBlendExact(sc);
    env.msaa = rd_aa_samples(rd__aa_mode()) > 1u;
    RdFadesFold plan;
    const char *why = rd__fades_fold_plan(f, keep, &env, &plan);
    if (plan.depthRead && !s_depthCarried) {
        s_depthCarried = true;
        rd__log("fades fold: SCENE's depth is read in list %d before this replay clears it "
                "(frame %u): the letterbox bars, which write it, are drawn into SCENE from now on "
                "(reported once)",
                plan.depthReadList, f->number);
        if (why == NULL) {
            /* this frame's bars were planned with the depth thought fresh */
            env.depthCarried = true;
            why = rd__fades_fold_plan(f, keep, &env, &plan);
        }
    }
    if (why) {
        if (why[0]) {
            logRefusal(why);
        }
        return;
    }
    s_fold = plan;
    s_fold.replay = g_rd.replayCounter;
    g_rdPerf.fadesFolded = plan.sprites;
    static bool s_logged;
    if (!s_logged) {
        s_logged = true;
        char steps[4 * 64] = "";
        size_t at = 0;
        for (uint32_t i = 0; i < plan.n && i < RD_FADES_FOLD_STEPS; i++) {
            const uint32_t w = plan.steps[i];
            char one[64]; /* at most 45 bytes */
            const int k = snprintf(one, sizeof(one), "%s%s (%u, %u, %u) at %u/128", i ? ", " : "",
                                   plan.sides[i] ? "bars" : "the whole frame", w & 255u,
                                   (w >> 8) & 255u, (w >> 16) & 255u, w >> 24);
            if (k > 0 && at + (size_t)k < sizeof(steps)) {
                memcpy(steps + at, one, (size_t)k + 1u);
                at += (size_t)k;
            }
        }
        rd__log("fades fold: %u of list 11's last sprites applied by the reduction instead of "
                "drawn into SCENE (frame %u; %s; %s; SCENE %ux%u texels, rounding %s; within two "
                "levels of the sprites drawn: DIVERGENCES.md VD-27) (reported once)",
                plan.sprites, f->number, steps,
                plan.scaled ? "the steps after the hardware filter (above 1x)"
                            : "the steps on SCENE's texels before the GS filter",
                sc ? sc->tw : 0, sc ? sc->th : 0,
                env.exact ? "the GS's floor (the exact blend)"
                          : "the nearest level (the fixed-function blend)");
    }
}

static bool active(void)
{
    return s_fold.on && s_fold.replay == g_rd.replayCounter;
}

bool rd__fades_fold_skip(uint32_t ord)
{
    if (!s_fold.on || ord < s_fold.ords[0] || s_fold.replay != g_rd.replayCounter) {
        return false;
    }
    for (uint32_t i = 0; i < s_fold.sprites; i++) {
        if (s_fold.ords[i] == ord) {
            return true;
        }
    }
    return false;
}

/* the reduction's sprite at the planned command did not take the fold */
static void notTaken(const char *why)
{
    s_off = true;
    rd__log("fades fold: the reduction took another way (%s): frame %u's picture lacks list "
            "11's last %u sprites; the fold is off from now on",
            why, s_frame, s_fold.sprites);
    s_fold.on = false;
}

void rd__fades_fold_fx(IcoDrawCB *cb, uint8_t kind, const RdTargetRec *src)
{
    if (!active() || kind != RD_POST_REDUCTION || g_rdStoreOrd != s_fold.reductionOrd) {
        return;
    }
    const uint32_t want = RD_FXF_TEXTURED | RD_FXF_LINEAR;
    const uint32_t mask = want | RD_FXF_ABE | RD_FXF_DST | RD_FXF_ABOVE1X;
    if (s_fold.scaled || src != rd__target_rec(rd_target(RD_TARGET_SCENE).id) ||
        (cb->mode[0] & mask) != want) {
        notTaken("the GS sprite model, not as planned");
        return;
    }
    cb->mode[0] |= RD_FXF_FOLD;
    memcpy(cb->aux0, s_fold.steps, sizeof(cb->aux0));
    cb->blend[0] = s_fold.meta;
    cb->blend[1] = s_fold.alphas;
    cb->blend[2] = s_fold.bars;
    s_applied++;
}

void rd__fades_fold_screen(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                           IcoDrawCB *cb)
{
    if (!active() || g_rdStoreOrd != s_fold.reductionOrd || !ds || !ds->textured) {
        return;
    }
    const RdTexRec *t = r->st.ds.texEnabled ? rd__tex_rec(r->st.tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_TARGET || t->target != rd_target(RD_TARGET_SCENE).id) {
        return;
    }
    const bool bars = (s_fold.meta & 0xF0u) != 0u;
    if (!s_fold.scaled || !dp ||
        (dp->key.fs != RD_FS_SPRITE && dp->key.fs != RD_FS_SPRITE_PLANES) ||
        (bars && (cb->mode[0] & ICO_DF_ABOVE1X))) {
        notTaken("the hardware sprite, not as planned");
        return;
    }
    cb->mode[0] |= ICO_DF_FOLD;
    memcpy(cb->aux0, s_fold.steps, sizeof(cb->aux0));
    cb->aux1[0] = s_fold.meta;
    cb->aux1[1] = s_fold.alphas;
    cb->aux1[2] = s_fold.bars;
    s_applied++;
}

void rd__fades_fold_end(void)
{
    if (active() && s_applied == 0) {
        notTaken("no reduction sprite reached");
        g_rdPerf.fadesFolded = 0;
    }
    s_fold.on = false;
}
