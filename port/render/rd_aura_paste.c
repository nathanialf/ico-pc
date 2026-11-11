/* rd_aura_paste.c: the aura's paste as the alpha alone where it reads only
 * rows of FEED128 proven alpha 0, the rest as before (docs/port/
 * RENDER_POST.md, "The aura's paste as its constant part"; rd_aura_paste.h).
 *
 * staticBlur.c's aura in a room with no aura object on screen: AURA_WORK
 * holds only its clear (0, 0, 0, 0), and auraInspireAfter's sprites
 *   8:39  AURA_WORK into WORK0 (rows 0-111), Cs * As + Cd, MODULATE by RGBAQ
 *         (0, 0, 0, 128): rd_const_read.c draws it untextured in the clear's
 *         colour, whose alpha is (0 * 128) >> 7 = 0, so every texel it
 *         covers is left alpha 0 (the GS writes As, FBA 0);
 *   8:47  WORK0 into FEED128 by the GS's bilinear at scale 1, the same blend
 *         and RGBAQ: a FEED128 texel whose taps are all alpha 0 gets
 *         (0 * 128) >> 7 = 0;
 *   8:55  FEED128 over SCENE (pasteFeedBackAreaToFB), (Cs - Cd) * As + Cd,
 *         MODULATE by RGBAQ (64, 64, 64, 128): a SCENE texel whose kernel's
 *         taps are all alpha 0 has As 0, so its RGB is ((Cs - Cd) 0 >> 7) +
 *         Cd = Cd and its alpha 0.
 * The constant fold (rd_fx_plan.c fxConstantDraw: the sprite untextured
 * with RGBAQ's alpha 0, the blend proved the identity on RGB for every Cd
 * and Ad, the alpha alone written) writes those bytes there without the
 * texture's taps and the destination's read.  The rows of SCENE whose taps
 * reach a row not proven (the bottom band: FEED128's rows 112-127 hold
 * WORK0's alpha the aura's sprite did not cover, measured at 128, and the
 * paste's B-spline reaches row 112 and past) are drawn as the paste,
 * scissored to them.  Both in the paste's own pass (the local read's,
 * after a held crack fill and a deferred glow paste, rd_late_pass.c).
 *
 * The proof is a chain of records, each one target's rows (across every
 * column) proven alpha 0: started by a sprite drawn in a cleared source's
 * colour (rd__const_took) whose alpha is 0, carried by the next action's
 * sprite reading the recorded target at scale 1 (its taps by the GS's
 * nearest or bilinear, fx_sprite_ps's V to the bit), taken by the next
 * action's sprite into SCENE reading it; any action between (counted by
 * rd_const_read.c, every command but the state deltas and the deferred
 * text) ends the chain.  Each link needs: the sprite's alpha written whole
 * (no alpha test, no DATE, FBA 0, the colour mask's A, no FBMSK), its
 * alpha 0 at an alpha 0 texel (rd__const_tfx), RGBA32 (no TEXA), the
 * scissor across every column, the rect over every column; the paste
 * besides the RGB identity at As 0 (rd__fx_fold: PABE refuses) and the
 * scissor whole.  A SCENE texel row's window Y is taken a step either way
 * (the float's rounding of (t + 0.5) / scale), its V and its taps from
 * there (rd__ap_taps: the box, the B-spline or the GS's taps of
 * FXF_ABOVE1X's steps), addressed as fx_axis addresses them; a row that
 * REPEAT moves counts as not proven unless every row is.  Anything not
 * proven: the paste whole, counted by reason.
 *
 * Counted: RdPerfRecord.auraPasteSplit, auraPasteRows (SCENE rows drawn as
 * the alpha alone), auraPasteBand, auraPasteKept, auraPasteWhy; the 10 s
 * line "window: aura paste: P drawn as the alpha alone (B band rows each), K
 * read whole (why)" (rd_aura_paste_line); --stats. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_aura_paste.h"
#include "rd_replay.h"

static int s_force = -1;

/* the last record and the planned split (this replay's) */
static struct {
    bool valid;
    uint32_t replay, actions;
    const RdTargetRec *target;
    uint32_t tex;
    RdApRows rows;
} s_rec;

static struct {
    bool due;
    uint32_t replay, ord;
    const RdCmd *c;
    uint32_t c0, c1;
} s_plan;

/* the 10 s line's sums (the window's thread clears them) */
static struct {
    uint64_t split, band, kept, why[RD_AP_WHY_COUNT];
} s_sum;

static const char *const kWhy[RD_AP_WHY_COUNT] = {
    [RD_AP_WHY_NONE] = "-",
    [RD_AP_WHY_OFF] = "switched off",
    [RD_AP_WHY_CHAIN] = "its source not proven alpha 0",
    [RD_AP_WHY_FORM] = "the paste's form",
    [RD_AP_WHY_SCISSOR] = "a scissor",
    [RD_AP_WHY_LOCAL] = "no local read",
    [RD_AP_WHY_ROWS] = "no row reads proven rows only",
};

const char *rd__aura_paste_why_text(int why)
{
    return why >= 0 && why < RD_AP_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

void rd__aura_paste_force(int on)
{
    s_force = on < 0 ? -1 : on != 0;
}

bool rd__aura_paste_on(void)
{
    if (s_force >= 0) {
        return s_force != 0;
    }
    static int env = -1;
    if (env < 0) {
        /* the tests' A/B only (TESTING.md): no config key */
        const char *e = getenv("ICO_RD_AURA_PASTE");
        env = !(e && e[0] == '0');
    }
    return env != 0;
}

/* ------------------------------------------------------------ the rows */

void rd__ap_rows_set(RdApRows *r, int32_t row)
{
    if (row >= 0 && (uint32_t)row < r->n && row < RD_AP_ROWS) {
        r->w[row >> 6] |= 1ull << (row & 63);
    }
}

bool rd__ap_rows_has(const RdApRows *r, int32_t row)
{
    return row >= 0 && (uint32_t)row < r->n && row < RD_AP_ROWS &&
           (r->w[row >> 6] & (1ull << (row & 63))) != 0;
}

int32_t rd__ap_v(int32_t y, const float rect[4], const float uv[4])
{
    const int64_t y0 = (int64_t)rect[1], y1 = (int64_t)rect[3];
    const int64_t v0 = (int64_t)uv[1], v1 = (int64_t)uv[3];
    if (y1 == y0) {
        return (int32_t)v0;
    }
    return (int32_t)(v0 + ((int64_t)y - y0) * (v1 - v0) / (y1 - y0)); /* C truncates as the GS */
}

void rd__ap_taps(int32_t c16, int32_t step32, bool bilinear, int32_t *lo, int32_t *hi)
{
    /* common.hlsli gs_rs_kind, gs_rs_first, gs_rs_count */
    const int32_t s = step32 < 1024 ? step32 : 1024;
    if (step32 > 64) {
        const int32_t l = 2 * c16 - (s >> 1);
        *lo = l >> 5;
        *hi = (l + s - 1) >> 5;
    } else if (step32 > 0 && step32 < 16 && bilinear) {
        *lo = ((c16 - 8) >> 4) - 1;
        *hi = *lo + 3;
    } else if (bilinear) {
        *lo = (c16 - 8) >> 4;
        *hi = *lo + (((c16 - 8) & 15) != 0 ? 1 : 0);
    } else {
        *lo = *hi = c16 >> 4;
    }
}

bool rd__ap_proven(const RdApRows *p, int32_t lo, int32_t hi, bool clampT, int32_t lsz, int32_t asz)
{
    if (lsz < 1 || asz < 1 || hi < lo || hi - lo > 64) {
        return false;
    }
    int all = -1; /* every row of the texture proven (any REPEAT lands there): -1 not asked */
    for (int32_t y = lo; y <= hi; y++) {
        int32_t a = y;
        if (a < 0 || a >= lsz) {
            if (!clampT) {
                if (all < 0) {
                    all = 1;
                    for (int32_t k = 0; k < asz && all; k++) {
                        all = rd__ap_rows_has(p, k);
                    }
                }
                if (!all) {
                    return false;
                }
                continue;
            }
            a = a < 0 ? 0 : lsz - 1;
        }
        a = a < 0 ? 0 : (a >= asz ? asz - 1 : a);
        if (!rd__ap_rows_has(p, a)) {
            return false;
        }
    }
    return true;
}

void rd__ap_texel_y(uint32_t t, float sy, int32_t oy16, int32_t *ylo, int32_t *yhi)
{
    const float ts = sy > 0.0f ? sy : 1.0f;
    const float fc = ts != 1.0f ? ((float)t + 0.5f) / ts : (float)t + 0.5f;
    const double dc = ((double)t + 0.5) / (double)ts;
    const int32_t gf = (int32_t)floorf((fc - 0.5f) * 16.0f + 0.5f);
    const int32_t gd = (int32_t)floor((dc - 0.5) * 16.0 + 0.5);
    *ylo = oy16 + (gf < gd ? gf : gd) - 1;
    *yhi = oy16 + (gf > gd ? gf : gd) + 1;
}

bool rd__ap_paste_rows(const RdApPaste *in, uint32_t *c0, uint32_t *c1)
{
    uint32_t best0 = 0, best1 = 0, run0 = 0;
    bool inRun = false;
    for (uint32_t t = 0; t <= in->th; t++) {
        bool ok = false;
        if (t < in->th) {
            int32_t ylo, yhi;
            rd__ap_texel_y(t, in->sy, in->oy16, &ylo, &yhi);
            int32_t va = rd__ap_v(ylo, in->rect, in->uv), vb = rd__ap_v(yhi, in->rect, in->uv);
            if (va > vb) {
                const int32_t s = va;
                va = vb;
                vb = s;
            }
            /* a window Y above the rect's top: the shader's divide of a
             * negative product taken a step lower too */
            if (ylo < (int32_t)in->rect[1] || yhi < (int32_t)in->rect[1]) {
                va -= 1;
            }
            int32_t ca = va, cb = vb;
            if (in->ssy != 1.0f) {
                ca = (int32_t)floor((double)va * in->ssy) - 1;
                cb = (int32_t)ceil((double)vb * in->ssy) + 1;
            }
            int32_t lo, hi, l2, h2;
            rd__ap_taps(ca, in->step32, in->bilinear, &lo, &h2);
            rd__ap_taps(cb, in->step32, in->bilinear, &l2, &hi);
            ok = rd__ap_proven(in->proven, lo < l2 ? lo : l2, hi > h2 ? hi : h2, in->clampT,
                               in->lsz, in->asz);
        }
        if (ok && !inRun) {
            inRun = true;
            run0 = t;
        } else if (!ok && inRun) {
            inRun = false;
            if (t - run0 > best1 - best0) {
                best0 = run0;
                best1 = t;
            }
        }
    }
    *c0 = best0;
    *c1 = best1;
    return best1 > best0;
}

/* ------------------------------------------------------------ the chain */

/* the origin (12.4) fx_sprite_ps draws a post sprite with under r's state
 * (rd_replay.c drawOffset: a screen copy drops the frame head's half when
 * the target took it) */
static void origin16(const Replay *r, int32_t *ox16, int32_t *oy16)
{
    const RdStateBlock *st = &r->st;
    *ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    *oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1)) +
            (((st->useOffset & RD_TARGET_HALF_Y) && !r->halfAdded) ? 8 : 0);
}

/* the sprite writes its alpha, As, at every fragment it covers: no alpha
 * test, no DATE, FBA 0, the alpha in the colour mask, no FBMSK; As 0 at a
 * texel of alpha 0 (untextured: RGBAQ's) */
static bool writesAlphaZero(const RdFxDraw *x)
{
    const RdDrawState *d = x->d;
    if ((d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->test.date != RD_DATE_OFF || d->fba ||
        !(d->colorMask & 8u) || d->fbmsk != 0) {
        return false;
    }
    if (!x->textured) {
        return x->p->rgba[3] == 0;
    }
    if (x->tfmt != RD_TEXSRC_RGBA32) {
        return false;
    }
    static const uint8_t zero[4] = {0, 0, 0, 0};
    uint8_t out[4];
    rd__const_tfx(zero, x->p->rgba, rd__replay_blur_flags(d, x->p->lines, 1, 0), out);
    return out[3] == 0;
}

/* the rows of x's target (scale 1) its sprite covers across every column,
 * in the scissor; false when a column is left */
static bool coveredRows(const Replay *r, const RdFxDraw *x, uint32_t *r0, uint32_t *r1)
{
    const RdTargetRec *t = x->tc;
    RhiRect sc;
    if (!t || t->sx != 1.0f || t->sy != 1.0f || t->ox != 0.0f || t->tw == 0 || t->th > RD_AP_ROWS ||
        !rd__scissor_texels(t, &r->st, 0, &sc) || sc.x != 0 || sc.w != t->tw) {
        return false;
    }
    int32_t ox16, oy16;
    origin16(r, &ox16, &oy16);
    const float *q = x->p->rect;
    if (!((int32_t)q[0] <= ox16 && (int32_t)q[2] > ox16 + 16 * ((int32_t)t->tw - 1))) {
        return false;
    }
    uint32_t a = t->th, b = 0;
    for (uint32_t y = 0; y < t->th; y++) {
        const int32_t gy = oy16 + 16 * (int32_t)y;
        if (gy >= (int32_t)q[1] && gy < (int32_t)q[3] && (int32_t)y >= sc.y &&
            (int32_t)y < sc.y + (int32_t)sc.h) {
            a = y < a ? y : a;
            b = y + 1;
        }
    }
    *r0 = a;
    *r1 = b;
    return b > a;
}

static void kept(int why)
{
    g_rdPerf.auraPasteKept++;
    g_rdPerf.auraPasteWhy = (uint32_t)why;
    __atomic_add_fetch(&s_sum.kept, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_sum.why[why], 1, __ATOMIC_RELAXED);
}

void rd__aura_paste_kept(int why)
{
    s_plan.due = false;
    kept(why > RD_AP_WHY_NONE && why < RD_AP_WHY_COUNT ? why : RD_AP_WHY_FORM);
}

/* the aura's paste's form, for the reasons a split is not drawn: a sprite
 * of the aura into SCENE from a target that reads its destination */
static bool pasteLike(const Replay *r, const RdFxDraw *x)
{
    return x->c && x->c->b[0] == RD_POST_AURA && x->textured && x->src && x->dstRead &&
           x->tc == rd__target_rec(RD_TARGET_SCENE + 1u) && r->st.color != 0;
}

/* plan the paste's split over the record (false: kept, why said) */
static bool planPaste(Replay *r, const RdFxDraw *x, int *why)
{
    const RdDrawState *d = x->d;
    const RdTargetRec *tc = x->tc, *src = x->src;
    RhiRect sc;
    *why = RD_AP_WHY_FORM;
    if (!writesAlphaZero(x) || x->selfSample) {
        return false;
    }
    uint8_t rgba[4], ident = 0, rgb[3];
    memcpy(rgba, x->p->rgba, sizeof(rgba));
    rgba[3] = 0; /* As at a texel of alpha 0 (writesAlphaZero) */
    if (!d->abe || !rd__fx_fold(d, rgba, &ident, rgb) || (ident & 7u) != 7u) {
        return false; /* the paste does not keep RGB at As 0 (PABE, another blend) */
    }
    if (!rd__scissor_texels(tc, &r->st, 0, &sc) || sc.x != 0 || sc.y != 0 || sc.w != tc->tw ||
        sc.h != tc->th) {
        *why = RD_AP_WHY_SCISSOR;
        return false;
    }
    if (!g_rd.profile.fxPlan || !g_rd.profile.fxDstLocal || !g_rd.fs[RD_FS_FX_LOCAL].id ||
        !g_rd.layoutLocal.id || g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__replay_msaa_want() > 1u ||
        rd__uber_on()) {
        *why = RD_AP_WHY_LOCAL;
        return false;
    }
    int32_t ox16, oy16;
    origin16(r, &ox16, &oy16);
    RdApPaste in;
    memset(&in, 0, sizeof(in));
    in.th = tc->th;
    in.sy = tc->sy;
    in.ssy = src->sy;
    in.oy16 = oy16;
    memcpy(in.rect, x->p->rect, sizeof(in.rect));
    memcpy(in.uv, x->uv, sizeof(in.uv));
    in.step32 = (int32_t)(x->steps & 4095u);
    in.bilinear = d->magFilter == RD_FILTER_LINEAR;
    in.clampT = d->wrap.t == RD_WRAP_CLAMP;
    const float l = rintf(x->p->scalar[1] * src->sy);
    in.lsz = l >= 1.0f ? (int32_t)l : 1;
    in.asz = (int32_t)src->th;
    in.proven = &s_rec.rows;
    uint32_t c0, c1;
    if (!rd__ap_paste_rows(&in, &c0, &c1)) {
        *why = RD_AP_WHY_ROWS;
        return false;
    }
    s_plan.due = true;
    s_plan.replay = g_rd.replayCounter;
    s_plan.ord = g_rdStoreOrd;
    s_plan.c = x->c;
    s_plan.c0 = c0;
    s_plan.c1 = c1;
    return true;
}

/* the record carried through x (scale 1 both, the GS's taps): x's target's
 * rows whose every tap lies in recorded rows */
static bool carry(Replay *r, const RdFxDraw *x, RdApRows *out)
{
    const RdTargetRec *src = x->src;
    uint32_t r0, r1;
    if (!x->textured || !src || x->steps != 0 || src->sx != 1.0f || src->sy != 1.0f ||
        !writesAlphaZero(x) || !coveredRows(r, x, &r0, &r1)) {
        return false;
    }
    int32_t ox16, oy16;
    origin16(r, &ox16, &oy16);
    const bool lin = x->d->magFilter == RD_FILTER_LINEAR;
    const bool clampT = x->d->wrap.t == RD_WRAP_CLAMP;
    const float l = rintf(x->p->scalar[1]);
    const int32_t lsz = l >= 1.0f ? (int32_t)l : 1;
    memset(out, 0, sizeof(*out));
    out->n = x->tc->th;
    bool any = false;
    for (uint32_t y = r0; y < r1; y++) {
        /* fx_sprite_ps at scale 1: Y = oy + 16 y exactly, V to the bit */
        const int32_t v = rd__ap_v(oy16 + 16 * (int32_t)y, x->p->rect, x->uv);
        int32_t lo, hi;
        rd__ap_taps(v, 0, lin, &lo, &hi);
        if (rd__ap_proven(&s_rec.rows, lo, hi, clampT, lsz, (int32_t)src->th)) {
            rd__ap_rows_set(out, (int32_t)y);
            any = true;
        }
    }
    return any;
}

void rd__aura_paste_observe(Replay *r, const RdFxDraw *x)
{
    const uint32_t act = rd__const_actions();
    const bool on = rd__aura_paste_on() && g_rd.hasDevice;
    const bool next = s_rec.valid && s_rec.replay == g_rd.replayCounter &&
                      act == s_rec.actions + 1u && x->src && x->src == s_rec.target &&
                      x->src->color.id == s_rec.tex;
    s_plan.due = false;
    if (pasteLike(r, x)) {
        int why = !on ? RD_AP_WHY_OFF : !next ? RD_AP_WHY_CHAIN : RD_AP_WHY_NONE;
        if (why == RD_AP_WHY_NONE && !planPaste(r, x, &why)) {
            /* why set by planPaste */
        }
        if (!s_plan.due) {
            kept(why == RD_AP_WHY_NONE ? RD_AP_WHY_FORM : why);
        }
        s_rec.valid = false;
        return;
    }
    if (!on) {
        s_rec.valid = false;
        return;
    }
    RdApRows rows;
    if (next && carry(r, x, &rows)) {
        s_rec.rows = rows; /* carried into x's target */
        s_rec.actions = act;
        s_rec.target = x->tc;
        s_rec.tex = x->tc->color.id;
        return;
    }
    uint32_t r0, r1;
    if (!x->textured && rd__const_took(r->st.color) && x->tc && writesAlphaZero(x) &&
        coveredRows(r, x, &r0, &r1)) {
        /* drawn in a cleared source's colour, its alpha 0 (rd_const_read.c) */
        memset(&s_rec.rows, 0, sizeof(s_rec.rows));
        s_rec.rows.n = x->tc->th;
        for (uint32_t y = r0; y < r1; y++) {
            rd__ap_rows_set(&s_rec.rows, (int32_t)y);
        }
        s_rec.valid = true;
        s_rec.replay = g_rd.replayCounter;
        s_rec.actions = act;
        s_rec.target = x->tc;
        s_rec.tex = x->tc->color.id;
        return;
    }
    if (x->tc == s_rec.target || act != s_rec.actions) {
        s_rec.valid = false; /* drawn into the recorded target, or another action */
    }
}

bool rd__aura_paste_due(const RdFxDraw *x, uint32_t *c0, uint32_t *c1)
{
    if (!s_plan.due || s_plan.replay != g_rd.replayCounter || s_plan.ord != g_rdStoreOrd ||
        s_plan.c != x->c) {
        return false;
    }
    *c0 = s_plan.c0;
    *c1 = s_plan.c1;
    return true;
}

void rd__aura_paste_split(uint32_t c0, uint32_t c1, uint32_t th)
{
    s_plan.due = false;
    const uint32_t band = th - (c1 - c0);
    g_rdPerf.auraPasteSplit++;
    g_rdPerf.auraPasteRows += c1 - c0;
    g_rdPerf.auraPasteBand += band;
    __atomic_add_fetch(&s_sum.split, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_sum.band, band, __ATOMIC_RELAXED);
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("aura paste: the aura's paste drawn as the alpha alone over SCENE's rows %u-%u "
                "(they read only rows of its source proven alpha 0), as the paste over the "
                "other %u rows (reported once)",
                c0, c1 ? c1 - 1u : 0u, band);
    }
}

/* ------------------------------------------------------------ the line */

void rd_aura_paste_line(void)
{
    const uint64_t split = __atomic_exchange_n(&s_sum.split, 0, __ATOMIC_RELAXED);
    const uint64_t band = __atomic_exchange_n(&s_sum.band, 0, __ATOMIC_RELAXED);
    const uint64_t k = __atomic_exchange_n(&s_sum.kept, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_AP_WHY_COUNT];
    for (int i = 0; i < RD_AP_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_sum.why[i], 0, __ATOMIC_RELAXED);
    }
    if (split + k == 0) {
        return;
    }
    char why[384] = "";
    size_t at = 0;
    for (int i = 1; i < RD_AP_WHY_COUNT; i++) {
        if (!w[i]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[i], kWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: aura paste: %llu drawn as the alpha alone (%.1f band rows each), %llu read "
            "whole%s%s\n",
            (unsigned long long)split, split ? (double)band / (double)split : 0.0,
            (unsigned long long)k, why, at ? ")" : "");
}
