/* rd_glow_blur.c: the glow's blur chain in two passes (rd_glow_blur.h says
 * what and why; docs/port/RENDER_POST.md, "The glow's blur in two passes";
 * DIVERGENCES.md VD-33).
 *
 * The CPU side: the chain's net operator per axis from its recorded passes
 * (rd__glow_blur_axis: each WORK0 position's kernel row composed pass by
 * pass as fx_sprite_ps samples, in floats), its taps and the loss model put
 * in a small RGBA8 table (RD_GLOW_BLUR_TABLE_ROWS rows of WORK0's width),
 * uploaded through the ring when the chain's form changes (its passes'
 * rects, UVs, colours and sizes: the same every frame of a resolution), and
 * the two passes drawn through the replay's own pass set-up (bindDraw: the
 * target's pass begun with LOAD, its pending clear as the load op, the
 * glow's timing class), the table bound as t2 beside the source at t1 with
 * the chain's own sampler (LINEAR, CLAMP).
 *
 * Neither pass takes a DONT_CARE load: the across pass writes WORK0's
 * height of WORK1 (256 rows at 4:3, 128 of them drawn), so the rows below
 * keep their bytes; the down pass writes every texel of WORK0 (the chain's
 * pass 2 covers it, a condition here) and would be allowed one, but takes
 * the LOAD the replay begins it with (0.2 MB on the phone).  Under
 * ICO_RD_LAMPS_POISON=1 (the tests) WORK0 is cleared to magenta between
 * the two passes, so a texel the down pass did not write would show.
 *
 * Counted: the run's and the 10 s window's chains drawn in two passes, their
 * pairs, and the chains drawn as recorded by reason (rd__glow_blur_line,
 * "window: glow blur: ..."; rd_replay_tool --stats "stats: glow blur ...");
 * the first chain drawn and each reason once in the log. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_glow_blur.h"
#include "rd_lamps.h"
#include "rd_replay.h"

static const char *const kWhy[RD_GLOW_BLUR_WHY_COUNT] = {
    "drawn in two passes",
    "the glow's light plan is not lamps or whole",
    "a later command reads WORK1, WORK2, WORK3 or SCENE's alpha, or the chain's form is unknown",
    "Original 1x: the game's chain",
    "a pass not of the blur pair's form, or no eye blur after the pairs",
    "a pass adds a constant, a pass 1's colour is not 128, or its alpha is not the texture's",
    "a pass 2 leaves texels of WORK0, or WORK1 is of another width",
    "a kernel row wider than the taps hold",
    "a row weighs WORK1's bytes from before the chain",
    "no pipeline",
    "no table texture or ring space",
    "a pass window (the passes are counted as recorded)",
};

const char *rd__glow_blur_why_text(int why)
{
    return why >= 0 && why < RD_GLOW_BLUR_WHY_COUNT ? kWhy[why] : "unknown";
}

/* ------------------------------------------------------- the operator */

typedef struct GbRow {
    int32_t lo, len; /* the row's first position and its count */
    float w[RD_GLOW_BLUR_SUPPORT];
    float old; /* the weight on WORK1's bytes from before the chain */
} GbRow;

static int32_t gbFloor16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static int32_t gbClamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* o = (1 - t) A + t B; false when it would be wider than the support */
static bool gbMix(const GbRow *a, const GbRow *b, float t, GbRow *o)
{
    if (t <= 0.0f) {
        *o = *a;
        return true;
    }
    int32_t lo = 0, hi = 0;
    bool any = false;
    const GbRow *rows[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        if (rows[k]->len > 0) {
            const int32_t l = rows[k]->lo, h = rows[k]->lo + rows[k]->len;
            lo = any && lo < l ? lo : l;
            hi = any && hi > h ? hi : h;
            any = true;
        }
    }
    memset(o, 0, sizeof(*o));
    if (any && hi - lo > RD_GLOW_BLUR_SUPPORT) {
        return false;
    }
    o->lo = lo;
    o->len = any ? hi - lo : 0;
    const float f[2] = {1.0f - t, t};
    for (int k = 0; k < 2; k++) {
        for (int32_t j = 0; j < rows[k]->len; j++) {
            o->w[rows[k]->lo - lo + j] += f[k] * rows[k]->w[j];
        }
        o->old += f[k] * rows[k]->old;
    }
    return true;
}

/* the pass's colour gain of channel c (RGBA) and whether it multiplies
 * (fx_tfx: MODULATE or HIGHLIGHT, (t * col) >> 7 on RGB; the alpha t.a + 0
 * under HIGHLIGHT, (t.a * col.a) >> 7 under MODULATE, with TCC) */
static float gbGain(const RdBlurChainPass *p, int c, bool *mul)
{
    const uint32_t tfx = (p->flags >> RD_FXF_TFX_SHIFT) & 3u;
    uint32_t k = p->rgba[c];
    if (c == 3 && tfx == 2u) {
        k = 128u;
    }
    *mul = k != 128u;
    return (float)k / 128.0f;
}

/* the chain's form as the operator models it */
static int gbForm(const RdBlurChain *ch)
{
    if (ch->n < 4 || ch->n > RD_GLOW_BLUR_PASSES_MAX || (ch->n & 1u)) {
        return RD_GLOW_BLUR_WHY_FORM;
    }
    if (ch->w0 == 0 || ch->h0 == 0 || ch->w0 > RD_GLOW_BLUR_MAXN || ch->h0 > RD_GLOW_BLUR_MAXN ||
        ch->w1 != ch->w0 || ch->h1 < ch->h0 || ch->h1 > RD_GLOW_BLUR_MAXN) {
        return RD_GLOW_BLUR_WHY_COVER;
    }
    for (uint32_t i = 0; i < ch->n; i++) {
        const RdBlurChainPass *p = &ch->p[i];
        const uint32_t tfx = (p->flags >> RD_FXF_TFX_SHIFT) & 3u;
        if ((tfx != 0u && tfx != 2u) || !(p->flags & RD_FXF_TCC) || (p->flags & RD_FXF_FBA) ||
            (tfx == 2u && p->rgba[3] != 0u) || p->texfmt != RD_TEXSRC_RGBA32 ||
            p->toWork1 != (uint8_t)((i & 1u) == 0u)) {
            return RD_GLOW_BLUR_WHY_COLOUR;
        }
        if (p->rect[2] <= p->rect[0] || p->rect[3] <= p->rect[1] || p->lsz[0] < 1 ||
            p->lsz[1] < 1 || p->asz[0] < 1 || p->asz[1] < 1) {
            return RD_GLOW_BLUR_WHY_FORM;
        }
        bool mul = false;
        for (int c = 0; c < 4; c++) {
            (void)gbGain(p, c, &mul);
            if (p->toWork1 && mul) {
                return RD_GLOW_BLUR_WHY_COLOUR; /* the gain is the pass 2s' alone */
            }
        }
        if (!p->toWork1 && (p->c0[0] != 0 || p->c0[1] != 0 || p->c1[0] != (int32_t)ch->w0 - 1 ||
                            p->c1[1] != (int32_t)ch->h0 - 1)) {
            return RD_GLOW_BLUR_WHY_COVER;
        }
    }
    return RD_GLOW_BLUR_WHY_NONE;
}

int rd__glow_blur_axis(const RdBlurChain *ch, int axis, float *off, float *w, float *loss,
                       float gain[4], float lc[4], float *lost)
{
    const int form = gbForm(ch);
    if (form != RD_GLOW_BLUR_WHY_NONE) {
        return form;
    }
    const int32_t n0 = (int32_t)(axis ? ch->h0 : ch->w0), n1 = (int32_t)(axis ? ch->h1 : ch->w1);
    GbRow *r0 = calloc((size_t)n0, sizeof(GbRow)), *r1 = calloc((size_t)n1, sizeof(GbRow));
    float (*l0)[4] = calloc((size_t)n0, sizeof(*l0)), (*l1)[4] = calloc((size_t)n1, sizeof(*l1));
    if (!r0 || !r1 || !l0 || !l1) {
        free(r0);
        free(r1);
        free(l0);
        free(l1);
        return RD_GLOW_BLUR_WHY_MEMORY;
    }
    int why = RD_GLOW_BLUR_WHY_NONE;
    for (int32_t i = 0; i < n0; i++) {
        r0[i].lo = i;
        r0[i].len = 1;
        r0[i].w[0] = 1.0f;
    }
    for (int32_t i = 0; i < n1; i++) {
        r1[i].old = 1.0f;
    }
    float c0[4] = {0, 0, 0, 0}, c1[4] = {0, 0, 0, 0};
    for (int c = 0; c < 4; c++) {
        gain[c] = 1.0f;
        lc[c] = 0.0f;
    }
    GbRow tmp;
    for (uint32_t pi = 0; pi < ch->n && why == RD_GLOW_BLUR_WHY_NONE; pi++) {
        const RdBlurChainPass *p = &ch->p[pi];
        const bool to1 = p->toWork1 != 0;
        GbRow *S = to1 ? r0 : r1, *D = to1 ? r1 : r0;
        float (*sl)[4] = to1 ? l0 : l1, (*dl)[4] = to1 ? l1 : l0;
        float *sc = to1 ? c0 : c1, *dc = to1 ? c1 : c0;
        const int32_t nS = to1 ? n0 : n1, nD = to1 ? n1 : n0;
        float g[4];
        bool mul[4];
        for (int c = 0; c < 4; c++) {
            g[c] = gbGain(p, c, &mul[c]);
        }
        const int32_t x0 = p->rect[axis], x1 = p->rect[axis + 2];
        const int32_t u0 = p->uv[axis], u1 = p->uv[axis + 2];
        const int32_t top = gbClamp(p->lsz[axis], 1, 1 << 20) - 1;
        const int32_t back = gbClamp(p->asz[axis] < nS ? p->asz[axis] : nS, 1, nS) - 1;
        const int32_t ca = p->c0[axis] > 0 ? p->c0[axis] : 0;
        const int32_t cb = p->c1[axis] < nD - 1 ? p->c1[axis] : nD - 1;
        for (int32_t i = ca; i <= cb; i++) {
            /* fx_sprite_ps's U at the texel (blurFoldU), its taps by
             * fx_axis's clamps (the TEX0 size, then the texture) */
            const int32_t X = p->o[axis] + 16 * i;
            const int32_t U =
                u0 + (int32_t)(((int64_t)(X - x0) * (int64_t)(u1 - u0)) / (int64_t)(x1 - x0));
            const int32_t uu = U - 8, ia = gbFloor16(uu), fr = uu - 16 * ia;
            const int32_t i0 = gbClamp(gbClamp(ia, 0, top), 0, back);
            const int32_t i1 = gbClamp(gbClamp(ia + 1, 0, top), 0, back);
            const float t = (float)fr / 16.0f;
            if (!gbMix(&S[i0], &S[i1], t, &tmp)) {
                why = RD_GLOW_BLUR_WHY_WIDE;
                break;
            }
            D[i] = tmp;
            for (int c = 0; c < 4; c++) {
                dl[i][c] = ((1.0f - t) * sl[i0][c] + t * sl[i1][c] +
                            (fr != 0 ? RD_GLOW_BLUR_LOSS_LERP : 0.0f)) *
                           g[c];
            }
        }
        if (why != RD_GLOW_BLUR_WHY_NONE) {
            break;
        }
        for (int c = 0; c < 4; c++) {
            dc[c] = sc[c] * g[c] + (mul[c] ? RD_GLOW_BLUR_LOSS_MUL : 0.0f);
        }
        /* the glow's edge strip on a wide frame: WORK1's columns left of a
         * pass 1's first drawn one hold that column (rd_blur_border.c glowEdge) */
        if (axis == 0 && to1 && p->first == 0 && ca > 0 && ca <= cb) {
            for (int32_t i = 0; i < ca; i++) {
                D[i] = D[ca];
                memcpy(dl[i], dl[ca], sizeof(dl[i]));
            }
        }
        if (!to1) {
            for (int c = 0; c < 4; c++) {
                gain[c] *= g[c];
            }
        }
    }
    float worst = 0.0f;
    for (int32_t i = 0; i < n0 && why == RD_GLOW_BLUR_WHY_NONE; i++) {
        const GbRow *row = &r0[i];
        if (row->old > RD_GLOW_BLUR_OLD_MAX || row->len < 1) {
            why = RD_GLOW_BLUR_WHY_OLD;
            break;
        }
        float tot = 0.0f;
        for (int32_t j = 0; j < row->len; j++) {
            tot += row->w[j];
        }
        int32_t j0 = 0; /* the window's first index into the row */
        float mass = tot;
        if (row->len > 2 * RD_GLOW_BLUR_TAPS) {
            mass = -1.0f;
            for (int32_t s = 0; s + 2 * RD_GLOW_BLUR_TAPS <= row->len; s++) {
                float m = 0.0f;
                for (int32_t j = 0; j < 2 * RD_GLOW_BLUR_TAPS; j++) {
                    m += row->w[s + j];
                }
                if (m > mass) {
                    mass = m;
                    j0 = s;
                }
            }
        }
        const float rowLost = tot > 0.0f ? 1.0f - mass / tot : 1.0f;
        if (rowLost > RD_GLOW_BLUR_LOST_MAX || mass <= 0.0f) {
            why = RD_GLOW_BLUR_WHY_WIDE;
            break;
        }
        worst = rowLost > worst ? rowLost : worst;
        for (int k = 0; k < RD_GLOW_BLUR_TAPS; k++) {
            const int32_t a = j0 + 2 * k;
            const float wa = a < row->len ? row->w[a] : 0.0f;
            const float wb = a + 1 < row->len ? row->w[a + 1] : 0.0f;
            const float s = wa + wb;
            const float pos = (float)(row->lo + a) + (s > 0.0f ? wb / s : 0.0f);
            off[i * RD_GLOW_BLUR_TAPS + k] = s > 0.0f ? pos - (float)i : 0.0f;
            w[i * RD_GLOW_BLUR_TAPS + k] = s / mass;
        }
        for (int c = 0; c < 4; c++) {
            loss[4 * i + c] = l0[i][c];
        }
    }
    if (axis == 0) {
        memcpy(lc, c0, sizeof(c0));
    }
    *lost = worst;
    free(r0);
    free(r1);
    free(l0);
    free(l1);
    return why;
}

static void gbU16(uint8_t *p, float v)
{
    const long k = lrintf(v < 0.0f ? 0.0f : (v > 65535.0f ? 65535.0f : v));
    p[0] = (uint8_t)(k >> 8);
    p[1] = (uint8_t)(k & 255);
}

bool rd__glow_blur_encode(uint8_t *texels, uint32_t tw, const uint32_t n[2], float *const off[2],
                          float *const w[2], float *const loss[2])
{
    memset(texels, 0, (size_t)tw * RD_GLOW_BLUR_TABLE_ROWS * 4u);
    for (int a = 0; a < 2; a++) {
        if (n[a] > tw) {
            return false;
        }
        for (uint32_t i = 0; i < n[a]; i++) {
            for (int k = 0; k < RD_GLOW_BLUR_TAPS; k++) {
                const float o = off[a][i * RD_GLOW_BLUR_TAPS + k];
                if (!(o >= -64.0f && o <= 63.99f)) {
                    return false;
                }
                uint8_t *t = texels + ((size_t)(a * RD_GLOW_BLUR_AXIS_ROWS + k) * tw + i) * 4u;
                gbU16(t, w[a][i * RD_GLOW_BLUR_TAPS + k] * 65535.0f);
                gbU16(t + 2, (o + 64.0f) * 256.0f);
            }
            uint8_t *hi =
                texels + ((size_t)(a * RD_GLOW_BLUR_AXIS_ROWS + RD_GLOW_BLUR_TAPS) * tw + i) * 4u;
            uint8_t *lo = hi + (size_t)tw * 4u;
            for (int c = 0; c < 4; c++) {
                float v = loss[a][4 * i + c];
                v = v < 0.0f ? 0.0f : (v > 255.99f ? 255.99f : v);
                const long q = lrintf(v * 256.0f);
                hi[c] = (uint8_t)(q >> 8);
                lo[c] = (uint8_t)(q & 255);
            }
        }
    }
    return true;
}

/* ------------------------------------------------------- the replay */

static struct {
    uint32_t tried; /* g_rd.replayCounter of the replay that looked at its chain */
    bool triedValid;
    /* the table: its chain (the cache's key), texture, state, contents */
    RdBlurChain key;
    bool keyValid, uploaded;
    RhiTexture tex;
    RhiState texState;
    uint32_t tw;
    uint8_t *texels;
    float gain[4], lc[4], lost;
    int32_t support; /* the widest window used (texels), for the log */
    /* counts: since rd_init, and the 10 s window's */
    uint64_t chains, pairs, refused;
    int lastWhy;
    /* the replay s_gb.tried names: its chain drawn in two passes (its
     * pairs), or why not */
    uint32_t repPairs;
    bool repDrawn;
    int repWhy;
    uint64_t sChains, sPairs, sSaved, sWhy[RD_GLOW_BLUR_WHY_COUNT];
    bool said[RD_GLOW_BLUR_WHY_COUNT];
    bool saidOn;
} s_gb;

static void sumAdd(uint64_t *v, uint64_t n)
{
    if (n) {
        __atomic_add_fetch(v, n, __ATOMIC_RELAXED);
    }
}

bool rd__glow_blur_on(void)
{
    return g_rd.hasDevice && g_rd.fs[RD_FS_FX_GLOWBLUR].id &&
           (rd__lamps_bits() & RD_LAMPS_GLOWBLUR) != 0u;
}

RdPipeKeyInt rd__glow_blur_key(const RdStateBlock *s)
{
    RdPipeKeyInt k = rd__blur_fold_key(s, RHI_FMT_RGBA8_UNORM);
    k.fs = RD_FS_FX_GLOWBLUR;
    return k;
}

void rd__glow_blur_init(void)
{
    const bool keepSaid = s_gb.saidOn;
    free(s_gb.texels);
    memset(&s_gb, 0, sizeof(s_gb));
    s_gb.saidOn = keepSaid;
    const bool on = rd__glow_blur_on();
    if (g_rd.hasDevice && (on || (rd__lamps_wanted() & RD_LAMPS_GLOWBLUR))) {
        rd__log("glow blur: %s: the glow's blur pairs drawn as two passes (fx_glowblur_ps: %d "
                "bilinear taps a texel across, then down, the chain's kernel per column and "
                "row; identical to the eye, DIVERGENCES.md VD-33) in lamps and whole pictures "
                "above 1x%s",
                on ? "on" : "off", RD_GLOW_BLUR_TAPS,
                on ? "" : " (asked for, but the module is not made on this device)");
    }
}

void rd__glow_blur_shutdown(void)
{
    if (s_gb.tex.id && g_rd.hasDevice) {
        rhi_destroy_texture(s_gb.tex);
    }
    s_gb.tex = (RhiTexture){0};
    s_gb.tw = 0;
    s_gb.keyValid = false;
    s_gb.uploaded = false;
    free(s_gb.texels);
    s_gb.texels = NULL;
}

void rd__glow_blur_counts(uint64_t *chains, uint64_t *pairs, uint64_t *refused, int *lastWhy)
{
    if (chains) {
        *chains = s_gb.chains;
    }
    if (pairs) {
        *pairs = s_gb.pairs;
    }
    if (refused) {
        *refused = s_gb.refused;
    }
    if (lastWhy) {
        *lastWhy = s_gb.lastWhy;
    }
}

void rd__glow_blur_replay(uint32_t *chains, uint32_t *pairs, int *why)
{
    const bool here = s_gb.triedValid && s_gb.tried == g_rd.replayCounter;
    *chains = here && s_gb.repDrawn ? 1u : 0u;
    *pairs = here && s_gb.repDrawn ? s_gb.repPairs : 0u;
    *why = here ? s_gb.repWhy : RD_GLOW_BLUR_WHY_NONE;
}

static bool refuse(int why)
{
    s_gb.refused++;
    s_gb.lastWhy = why;
    s_gb.repDrawn = false;
    s_gb.repPairs = 0;
    s_gb.repWhy = why;
    if (why > 0 && why < RD_GLOW_BLUR_WHY_COUNT) {
        sumAdd(&s_gb.sWhy[why], 1);
        if (!s_gb.said[why]) {
            s_gb.said[why] = true;
            rd__log("glow blur: a chain drawn as recorded: %s (reported once a reason)", kWhy[why]);
        }
    }
    return false;
}

/* the plan and the scale: why not (NONE: go on) */
static int planWhy(void)
{
    if (g_rdWin.first) {
        return RD_GLOW_BLUR_WHY_WINDOW;
    }
    const int plan = rd__glow_light_plan();
    if (plan == RD_GLOW_LIGHT_WHOLE) {
        const uint32_t w = g_rdPerf.glowLightWhy;
        if (w == RD_GLOW_LIGHT_WHY_WORK || w == RD_GLOW_LIGHT_WHY_SCENE ||
            w == RD_GLOW_LIGHT_WHY_FORM || w == RD_GLOW_LIGHT_WHY_OFF ||
            w == RD_GLOW_LIGHT_WHY_NONE) {
            return RD_GLOW_BLUR_WHY_READER;
        }
    } else if (plan != RD_GLOW_LIGHT_LAMPS) {
        return RD_GLOW_BLUR_WHY_PLAN;
    }
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!sc || (sc->sx == 1.0f && sc->sy == 1.0f)) {
        return RD_GLOW_BLUR_WHY_ONE;
    }
    return RD_GLOW_BLUR_WHY_NONE;
}

/* the table for ch (computed and encoded when its form is new) */
static int tableFor(const RdBlurChain *ch)
{
    RdBlurChain k = *ch;
    k.last = 0; /* the loop's index is not part of the form */
    if (s_gb.keyValid && memcmp(&k, &s_gb.key, sizeof(k)) == 0) {
        return RD_GLOW_BLUR_WHY_NONE;
    }
    s_gb.keyValid = false;
    const uint32_t n[2] = {ch->w0, ch->h0};
    const uint32_t tw = n[0] > n[1] ? n[0] : n[1];
    float *off[2] = {NULL, NULL}, *w[2] = {NULL, NULL}, *loss[2] = {NULL, NULL};
    int why = RD_GLOW_BLUR_WHY_NONE;
    float gain[4], lc[4], lcy[4], gy[4], lost[2] = {0, 0};
    for (int a = 0; a < 2 && why == RD_GLOW_BLUR_WHY_NONE; a++) {
        off[a] = malloc((size_t)n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        w[a] = malloc((size_t)n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        loss[a] = malloc((size_t)n[a] * 4u * sizeof(float));
        if (!off[a] || !w[a] || !loss[a]) {
            why = RD_GLOW_BLUR_WHY_MEMORY;
            break;
        }
        why =
            rd__glow_blur_axis(ch, a, off[a], w[a], loss[a], a ? gy : gain, a ? lcy : lc, &lost[a]);
    }
    uint8_t *texels = NULL;
    if (why == RD_GLOW_BLUR_WHY_NONE) {
        texels = malloc((size_t)tw * RD_GLOW_BLUR_TABLE_ROWS * 4u);
        if (!texels) {
            why = RD_GLOW_BLUR_WHY_MEMORY;
        } else if (!rd__glow_blur_encode(texels, tw, n, off, w, loss)) {
            why = RD_GLOW_BLUR_WHY_WIDE;
        }
    }
    int32_t support = 0;
    if (why == RD_GLOW_BLUR_WHY_NONE) {
        for (int a = 0; a < 2; a++) {
            for (uint32_t i = 0; i < n[a]; i++) {
                float lo = 0.0f, hi = 0.0f;
                for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
                    if (w[a][i * RD_GLOW_BLUR_TAPS + t] > 0.0f) {
                        const float o = off[a][i * RD_GLOW_BLUR_TAPS + t];
                        lo = o < lo ? o : lo;
                        hi = o > hi ? o : hi;
                    }
                }
                const int32_t s = (int32_t)ceilf(hi - lo) + 1;
                support = s > support ? s : support;
            }
        }
    }
    for (int a = 0; a < 2; a++) {
        free(off[a]);
        free(w[a]);
        free(loss[a]);
    }
    if (why != RD_GLOW_BLUR_WHY_NONE) {
        free(texels);
        return why;
    }
    if (s_gb.tex.id && s_gb.tw != tw) {
        rhi_destroy_texture(s_gb.tex); /* the GPU's reads of it are ordered before (the RHI) */
        s_gb.tex = (RhiTexture){0};
    }
    if (!s_gb.tex.id) {
        s_gb.tex = rhi_create_texture(
            &(RhiTextureDesc){tw, RD_GLOW_BLUR_TABLE_ROWS, 1, RHI_FMT_RGBA8_UNORM,
                              RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd glow blur table", 0});
        s_gb.texState = RHI_STATE_UNDEFINED;
        s_gb.tw = tw;
        if (!s_gb.tex.id) {
            free(texels);
            return RD_GLOW_BLUR_WHY_MEMORY;
        }
    }
    free(s_gb.texels);
    s_gb.texels = texels;
    s_gb.uploaded = false;
    memcpy(s_gb.gain, gain, sizeof(gain));
    memcpy(s_gb.lc, lc, sizeof(lc));
    s_gb.lost = lost[0] > lost[1] ? lost[0] : lost[1];
    s_gb.support = support;
    s_gb.key = k;
    s_gb.keyValid = true;
    return RD_GLOW_BLUR_WHY_NONE;
}

/* the table's texels into its texture through the ring (outside a pass) */
static bool upload(RhiCommandList cl)
{
    if (s_gb.uploaded) {
        return true;
    }
    const RhiLimits *lim = rhi_limits();
    const uint32_t pitchA = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    const uint32_t pitch = (s_gb.tw * 4u + pitchA - 1u) / pitchA * pitchA;
    const uint64_t at = rd__ring_alloc((uint64_t)pitch * RD_GLOW_BLUR_TABLE_ROWS,
                                       lim->copyOffsetAlign ? lim->copyOffsetAlign : 4u);
    if (at == ~0ull) {
        return false;
    }
    const uint32_t slot = rd__ring_slot();
    uint8_t *p = g_rd.ringMap[slot] + at;
    for (uint32_t y = 0; y < RD_GLOW_BLUR_TABLE_ROWS; y++) {
        memcpy(p + (size_t)y * pitch, s_gb.texels + (size_t)y * s_gb.tw * 4u, (size_t)s_gb.tw * 4u);
    }
    rd__transition(cl, s_gb.tex, &s_gb.texState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(cl, g_rd.ring[slot], at, pitch, s_gb.tex, 0,
                                   (RhiRect){0, 0, s_gb.tw, RD_GLOW_BLUR_TABLE_ROWS});
    rd__transition(cl, s_gb.tex, &s_gb.texState, RHI_STATE_SHADER_READ);
    s_gb.uploaded = true;
    return true;
}

/* the poison's magenta over WORK0 before the down pass (the tests) */
static void poison(RhiCommandList cl, RdTargetRec *tc)
{
    const bool readable = rd__pass_wants_readable(tc);
    rd__transition(cl, tc->color, &tc->colorState,
                   readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc q;
    memset(&q, 0, sizeof(q));
    q.color[0].texture = tc->color;
    q.color[0].load = RHI_LOAD_CLEAR;
    q.color[0].readable = readable;
    q.color[0].clear[0] = 1.0f;
    q.color[0].clear[2] = 1.0f;
    q.color[0].clear[3] = 1.0f;
    q.colorCount = 1;
    q.width = tc->tw;
    q.height = tc->th;
    rhi_cmd_begin_render_pass(cl, &q);
    rhi_cmd_end_render_pass(cl);
}

/* fx_glowblur_ps's DrawCB (fx_glowblur.hlsl): the axis, its table rows,
 * the source's texels, the gains and the common loss (the down pass) */
static void drawCb(IcoDrawCB *cb, int down, const RdTargetRec *src)
{
    memset(cb, 0, sizeof(*cb));
    cb->mode[0] = down ? 1u : 0u;
    cb->mode[1] = down ? (uint32_t)RD_GLOW_BLUR_AXIS_ROWS : 0u;
    cb->mode[2] = down ? 1u : 0u; /* the loss subtracted */
    cb->mode[3] = 0u;             /* the across axis's rows (its loss) */
    cb->tex[0] = (float)src->tw;
    cb->tex[1] = (float)src->th;
    cb->tex[2] = 1.0f / (float)src->tw;
    cb->tex[3] = 1.0f / (float)src->th;
    for (int c = 0; c < 4; c++) {
        cb->param[c] = down ? s_gb.gain[c] : 1.0f;
        cb->scale[c] = down ? s_gb.lc[c] / 255.0f : 0.0f;
    }
}

/* one of the two passes: src at t1, the table at t2, into r's target */
static bool pass(Replay *r, RdTargetRec *dst, RdTargetRec *src, int down, const RhiPipeline pipe,
                 const RdPipeKeyInt *key, uint32_t h)
{
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, src->color, &src->colorState, RHI_STATE_SHADER_READ);
    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = dst;
    ds.tex = src->color;
    ds.dateTex = s_gb.tex;
    ds.tw = src->tw;
    ds.th = src->th;
    ds.textured = 1;
    ds.screenCopy = 1;
    r->mirror = 0;
    const RhiBindGroup g = rd__replay_bind_draw(r, &ds);
    if (!g.id) {
        return false;
    }
    /* the rows the chain's passes draw: WORK0's height (WORK1 is taller) */
    RhiRect sc = r->sc;
    const int32_t hh = (int32_t)h;
    if (sc.y + (int32_t)sc.h > hh) {
        sc.h = sc.y < hh ? (uint32_t)(hh - sc.y) : 0u;
    }
    if (sc.h == 0u || sc.w == 0u) {
        return false;
    }
    if (sc.h != r->sc.h) {
        r->sc = sc;
        rhi_cmd_set_scissor(cl, &sc);
    }
    IcoDrawCB cb;
    drawCb(&cb, down, src);
    rd__set_pipeline(cl, pipe, key);
    rd__bind_uniform(cl, 0, r->frameBG);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g);
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    rd__replay_end_pass(r);
    return true;
}

bool rd__glow_blur_chain(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (c->type != RDC_POST_STUB || (c->b[0] != RD_POST_FLARE && c->b[0] != RD_POST_BLOOM) ||
        r->st.color != rd_target(RD_TARGET_WORK1).id || !rd__glow_blur_on()) {
        return false;
    }
    if (s_gb.triedValid && s_gb.tried == g_rd.replayCounter) {
        return false; /* this replay's chain was looked at (a later pass 1 is inside it) */
    }
    s_gb.tried = g_rd.replayCounter;
    s_gb.triedValid = true;
    int why = planWhy();
    if (why != RD_GLOW_BLUR_WHY_NONE) {
        return refuse(why);
    }
    static RdBlurChain ch; /* large */
    why = rd__replay_blur_chain(r, f, c, &ch);
    if (why == RD_GLOW_BLUR_WHY_NONE && ch.work0 != rd_target(RD_TARGET_WORK0).id) {
        why = RD_GLOW_BLUR_WHY_FORM;
    }
    if (why == RD_GLOW_BLUR_WHY_NONE) {
        why = gbForm(&ch);
    }
    if (why != RD_GLOW_BLUR_WHY_NONE) {
        return refuse(why);
    }
    RdTargetRec *t0 = rd__target_rec(ch.work0), *t1 = rd__target_rec(ch.work1);
    if (!t0 || !t1 || !t0->color.id || !t1->color.id) {
        return refuse(RD_GLOW_BLUR_WHY_FORM);
    }
    const RdPipeKeyInt key = rd__glow_blur_key(&r->st);
    const RhiPipeline pipe = rd__get_pipeline(&key);
    if (!pipe.id) {
        if (rd__pipeline_gathering()) {
            s_gb.triedValid = false; /* a gathering replay names the key only */
            return false;
        }
        return refuse(RD_GLOW_BLUR_WHY_PIPE);
    }
    why = tableFor(&ch);
    if (why != RD_GLOW_BLUR_WHY_NONE) {
        return refuse(why);
    }
    rd__replay_end_pass(r); /* R's pass on WORK0 (the across pass samples it) */
    RhiCommandList cl = rd__replay_cl();
    if (!upload(cl)) {
        return refuse(RD_GLOW_BLUR_WHY_MEMORY);
    }
    /* across: pass 1's state (WORK1, its scissor, the chain's sampler) */
    if (!pass(r, t1, t0, 0, pipe, &key, ch.h0)) {
        return refuse(RD_GLOW_BLUR_WHY_COVER); /* nothing drawn: the chain as recorded */
    }
    /* down: the last pass 2's state; the loop skips to it */
    rd__replay_blur_chain_take(r, &ch);
    if (rd__lamps_poison()) {
        rd__transition(cl, t1->color, &t1->colorState, RHI_STATE_SHADER_READ);
        poison(cl, t0);
    }
    const RdPipeKeyInt key2 = rd__glow_blur_key(&r->st);
    const RhiPipeline pipe2 =
        memcmp(&key2, &key, sizeof(key)) == 0 ? pipe : rd__get_pipeline(&key2);
    if (!pipe2.id || !pass(r, t0, t1, 1, pipe2, &key2, ch.h0)) {
        /* the across pass is drawn and the loop skips the chain: say so */
        rd__log("glow blur: the down pass could not be drawn (%s): WORK0 keeps the reduction",
                pipe2.id ? "its scissor is empty" : "no pipeline");
        return true;
    }
    r->writeSerial += ch.n; /* the chain's alpha writes (DATE's snapshot reuse) */
    const uint64_t pairs = ch.n / 2u;
    /* the passes the replay would have drawn: each pass as one, or the blur
     * fold's pairs but the last as one pass each (every pair folds at 4:3
     * and 16:9; at 48:9 one more is drawn as two) */
    const uint64_t today =
        g_rd.profile.blurFold && g_rd.fs[RD_FS_FX_BLUR2].id ? pairs + 1u : 2u * pairs;
    s_gb.chains++;
    s_gb.pairs += pairs;
    s_gb.lastWhy = RD_GLOW_BLUR_WHY_NONE;
    s_gb.repDrawn = true;
    s_gb.repPairs = (uint32_t)pairs;
    s_gb.repWhy = RD_GLOW_BLUR_WHY_NONE;
    sumAdd(&s_gb.sChains, 1);
    sumAdd(&s_gb.sPairs, pairs);
    sumAdd(&s_gb.sSaved, today > 2u ? today - 2u : 0u);
    if (!s_gb.said[RD_GLOW_BLUR_WHY_NONE]) {
        s_gb.said[RD_GLOW_BLUR_WHY_NONE] = true;
        rd__log("glow blur: the glow's %llu blur pairs drawn as two passes on WORK0 %ux%u "
                "(fx_glowblur_ps, %d taps a texel; kernels up to %d texels wide, at most %.4f of "
                "a row's weight left out; colour gains %.3f %.3f %.3f; reported once)",
                (unsigned long long)pairs, (unsigned)ch.w0, (unsigned)ch.h0, RD_GLOW_BLUR_TAPS,
                (int)s_gb.support, (double)s_gb.lost, (double)s_gb.gain[0], (double)s_gb.gain[1],
                (double)s_gb.gain[2]);
    }
    return true;
}

void rd__glow_blur_line(void)
{
    const uint64_t n = __atomic_exchange_n(&s_gb.sChains, 0, __ATOMIC_RELAXED);
    const uint64_t pairs = __atomic_exchange_n(&s_gb.sPairs, 0, __ATOMIC_RELAXED);
    const uint64_t saved = __atomic_exchange_n(&s_gb.sSaved, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_GLOW_BLUR_WHY_COUNT], refused = 0;
    for (int i = 1; i < RD_GLOW_BLUR_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_gb.sWhy[i], 0, __ATOMIC_RELAXED);
        refused += w[i];
    }
    if (n + refused == 0) {
        return;
    }
    char why[1024] = "";
    size_t at = 0;
    for (int i = 1; i < RD_GLOW_BLUR_WHY_COUNT; i++) {
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
            "window: glow blur: %llu pictures with the glow's blur in two passes (%llu pairs, "
            "about %llu passes fewer), %llu chains drawn as recorded%s%s\n",
            (unsigned long long)n, (unsigned long long)pairs, (unsigned long long)saved,
            (unsigned long long)refused, why, at ? ")" : "");
}
