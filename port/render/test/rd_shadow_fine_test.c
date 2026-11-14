/* rd_shadow_fine_test.c: the arithmetic of the shadow's levels at the
 * scene's scale above 1x (rd_shadowres.c, rd_shadowres.h), CPU only.  Every expectation follows from the code's
 * rules (the numbers in the comments from the same arithmetic in floats).
 *
 *   1. the scales: the count's (tw / w, or sx under Full pixel's crop), the
 *      levels' (the smallest odd whole number at or above it, 1 at 1x);
 *   2. the box weights of a first-level tap sum to 1 inside the count, at
 *      whole and fractional scales;
 *   3. the tent: 1 2 3 2 1 at S 3 (t -1..3), the identity at S 1, the sum
 *      S^2 at whole S;
 *   4. at scale 1 the step is the GS's sprite: texel x = ((0.75 C[2x] +
 *      0.25 C[2x + 1]) rounded x col) >> 7 on both axes (the box of one
 *      texel; the point mode at whole taps), at any whole and sub-texel
 *      offset of the sprite's map and under CLAMP and REPEAT (the GS's taps
 *      at u = 2 x + u0, indices wrapped or held), and the tent leaves a
 *      level as it is;
 *   5. the straight edge moved across one SHADOW2 texel (8 GS pixels) in 24
 *      steps at scene scales 2, 2.41, 2.8125 (Resolution window's across),
 *      3 and 4 across, the y axis at scale 1 (the non-uniform case: the
 *      levels' rows at the PS2's count, the step and tent there the
 *      identity): the
 *      three composites' darkening (SHADOW0..2 at 40, 80, 120, shadowDepth
 *      80) of the PS2-size chain (the count box-reduced to its GS size, the
 *      GS levels, one bilinear tap of each at the SCENE texel's GS
 *      position) against the fine chain's (the full-size count, the step and the
 *      tent of rd_sr_step_texel and rd_sr_tent_texel at the levels' odd
 *      scale, one bilinear tap at S u - 1/2): the fine chain's 10-90 % width within
 *      RD_SRT_WIDTH_TOL of the PS2 chain's mean over the phases (the float model
 *      of this arithmetic: 3 to 6 % under, the levels' byte roundings), its
 *      spread over the phases under RD_SRT_SD_MAX SCENE texels (the model:
 *      0.36 to 0.68; the PS2 chain 2.3 to 5.3), and its 50 % point's wander over
 *      the phases under RD_SRT_WANDER_MAX texels and under half of
 *      the PS2 chain's (the model: 0.43 to 0.98 against 2.6 to 5.5), and its
 *      darkness (the darkening summed over the edge, the phases' mean)
 *      within RD_SRT_DARK_TOL of the PS2 chain's (the model: 0.01 to 0.15 %:
 *      the mean over the grid's placements keeps the integral);
 *   6. the 10 s clause and the --stats words from a made-up record. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_shadowres.h"
#include "rd_shadow_fine_vectors.h"
#include "rd_shadow_fine_sprite.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the bounds of item 5 (see the header) */
#define RD_SRT_WIDTH_TOL 0.08
#define RD_SRT_SD_MAX 1.0
#define RD_SRT_WANDER_MAX 1.5
#define RD_SRT_DARK_TOL 0.01

enum { GSW = 576, ROWS = 2, PHASES = 24 };

static const uint32_t kW[RD_SR_LEVELS] = {288, 144, 72};
static const uint32_t kBlend[RD_SR_LEVELS] = {40, 80, 120};
static const uint8_t kCol[4] = {128, 128, 128, 80};

/* the game's step (taps 0.75 / 0.25 from the first texel, CLAMP) */
static RdSrStep stepOf(int first, float kx, float sx)
{
    RdSrStep p;
    memset(&p, 0, sizeof(p));
    p.first = first;
    p.kx = kx;
    p.ky = 2.0f;
    p.sx = sx;
    p.sy = 1.0f;
    p.fx = p.fy = 0.25f;
    memcpy(p.col, kCol, 4);
    return p;
}

static void testScales(void)
{
    CHECK(rd_sr_count_scale(576, 1728, 3.0f, 0.0f) == 3.0f, "3x");
    CHECK(fabsf(rd_sr_count_scale(448, 1080, 2.4107f, 0.0f) - 1080.0f / 448.0f) < 1e-6f,
          "full height: th / h");
    CHECK(rd_sr_count_scale(576, 1388, 2.41f, 3.5f) == 2.41f, "Full pixel: sx");
    CHECK(rd_sr_count_scale(576, 576, 1.0f, 0.0f) == 1.0f, "1x");
    CHECK(rd_sr_level_scale(1.0f) == 1u && rd_sr_level_scale(0.8f) == 1u, "1 at or under 1x");
    CHECK(rd_sr_level_scale(2.0f) == 3u && rd_sr_level_scale(2.41f) == 3u &&
              rd_sr_level_scale(3.0f) == 3u && rd_sr_level_scale(3.0000002f) == 3u,
          "3 at 2x, 2.41x, 3x");
    CHECK(rd_sr_level_scale(1.2f) == 3u && rd_sr_level_scale(4.0f) == 5u &&
              rd_sr_level_scale(4.82f) == 5u && rd_sr_level_scale(5.0f) == 5u &&
              rd_sr_level_scale(5.5f) == 7u,
          "odd at or above");
}

static void testBox(void)
{
    static const float kS[] = {1.0f, 2.0f, 2.41f, 3.0f, 4.82f};
    for (size_t k = 0; k < sizeof kS / sizeof kS[0]; k++) {
        const float s = kS[k];
        for (int m = 0; m < 5; m++) {
            const float a = 0.37f * (float)m + 10.0f;
            double sum = 0.0;
            for (int i = (int)floorf(a); i < (int)ceilf(a + 2.0f * s); i++) {
                sum += rd_sr_box_w((float)i, a, s, 0.25f);
            }
            CHECK(fabs(sum - 1.0) < 1e-5, "box weights at s %g a %g sum %g", (double)s, (double)a,
                  sum);
        }
    }
}

static void testTent(void)
{
    int t0, t1;
    rd_sr_tent_range(3.0f, &t0, &t1);
    CHECK(t0 == -2 && t1 == 4, "S 3: t -2..4, %d..%d", t0, t1);
    static const float kWant[] = {0, 1, 2, 3, 2, 1, 0};
    for (int t = -2; t <= 4; t++) {
        CHECK(rd_sr_tent_w(t, 3.0f) == kWant[t + 2], "S 3 t %d: %g", t,
              (double)rd_sr_tent_w(t, 3.0f));
    }
    rd_sr_tent_range(1.0f, &t0, &t1);
    float sum = 0.0f;
    for (int t = t0; t <= t1; t++) {
        sum += rd_sr_tent_w(t, 1.0f);
        CHECK(rd_sr_tent_w(t, 1.0f) == (t == 0 ? 1.0f : 0.0f), "S 1 t %d", t);
    }
    CHECK(sum == 1.0f, "S 1 sums to 1");
    for (int s = 1; s <= 7; s += 2) {
        rd_sr_tent_range((float)s, &t0, &t1);
        sum = 0.0f;
        for (int t = t0; t <= t1; t++) {
            sum += rd_sr_tent_w(t, (float)s);
        }
        CHECK(sum == (float)(s * s), "S %d sums to %d: %g", s, s * s, (double)sum);
    }
}

/* an RGBA8 image of w x ROWS whose every row is a (alpha, RGB 4 where alpha
 * is not 0, as the count's table makes them) */
static uint8_t *rowsOf(const uint8_t *a, uint32_t w)
{
    uint8_t *p = calloc((size_t)w * ROWS * 4u, 1);
    for (uint32_t y = 0; p && y < ROWS; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *q = p + ((size_t)y * w + x) * 4u;
            q[0] = q[1] = q[2] = a[x] ? 4u : 0u;
            q[3] = a[x];
        }
    }
    return p;
}

static void testScaleOne(void)
{
    uint8_t a[64];
    for (int i = 0; i < 64; i++) {
        a[i] = (uint8_t)((i * 37 + 11) % 129);
    }
    uint8_t *src = rowsOf(a, 64);
    uint8_t *lvl = calloc(32u * ROWS * 4u, 1);
    for (uint32_t x = 0; src && lvl && x < 32; x++) {
        uint8_t box[4], pt[4];
        const RdSrStep pb = stepOf(1, 2.0f, 1.0f), pp = stepOf(0, 2.0f, 1.0f);
        rd_sr_step_texel(src, 64, ROWS, &pb, x, 0, box);
        rd_sr_step_texel(src, 64, ROWS, &pp, x, 0, pt);
        /* the GS: 0.75 / 0.25 across, the rows the same (0.75 + 0.25 of one) */
        const float v = 0.75f * (float)a[2 * x] + 0.25f * (float)a[2 * x + 1];
        const uint32_t t = (uint32_t)floorf(v + 0.5f), want = (t * 80u) >> 7;
        CHECK(box[3] == want && pt[3] == want, "1x texel %u: box %u point %u, the GS %u", x, box[3],
              pt[3], want);
        memcpy(lvl + (size_t)x * 4u, box, 4);
        memcpy(lvl + (32u + x) * 4u, box, 4);
    }
    /* any whole and sub-texel offset of the sprite's map, CLAMP or REPEAT
     * (the GS's state: the game leaves the wrap as the draw before set it) */
    static const float kU0[] = {0.75f, 0.5f, 1.25f, 0.3125f, 0.0625f};
    static const int kWhole[] = {0, 0, 0, -1, -1};
    static const float kFrac[] = {0.25f, 0.0f, 0.75f, 0.8125f, 0.5625f};
    for (size_t u = 0; u < sizeof kU0 / sizeof kU0[0]; u++) {
        int whole = 99;
        float frac = -1.0f;
        CHECK(rd_sr_offset(kU0[u], &whole, &frac) && whole == kWhole[u] && frac == kFrac[u],
              "u0 %g: whole %d frac %g", (double)kU0[u], whole, (double)frac);
        for (int rep = 0; src && rep < 2; rep++) {
            RdSrStep p = stepOf(1, 2.0f, 1.0f);
            p.ax = (float)whole;
            p.fx = frac;
            p.repS = rep;
            for (uint32_t x = 0; x < 32; x++) {
                uint8_t o[4];
                rd_sr_step_texel(src, 64, ROWS, &p, x, 0, o);
                const int i0 = rd_sr_index(2 * (int)x + whole, 64, rep);
                const int i1 = rd_sr_index(2 * (int)x + whole + 1, 64, rep);
                /* the rows: 0.75 + 0.25 of one row (both alike) */
                const float v = (1.0f - frac) * (float)a[i0] + frac * (float)a[i1];
                const uint32_t want = ((uint32_t)floorf(v + 0.5f) * 80u) >> 7;
                CHECK(o[3] == want, "u0 %g %s texel %u: %u, the GS %u", (double)kU0[u],
                      rep ? "repeat" : "clamp", x, o[3], want);
            }
        }
    }
    CHECK(rd_sr_index(-1, 64, 1) == 63 && rd_sr_index(64, 64, 1) == 0 &&
              rd_sr_index(-1, 64, 0) == 0 && rd_sr_index(64, 64, 0) == 63,
          "the wrap and the clamp");
    for (uint32_t x = 0; src && lvl && x < 32; x++) {
        uint8_t o[4];
        rd_sr_tent_texel(lvl, 32, ROWS, 0, 1.0f, x, 1, o);
        CHECK(memcmp(o, lvl + (32u + x) * 4u, 4) == 0, "the tent at S 1 keeps texel %u", x);
    }
    free(src);
    free(lvl);
}

/* ---------------------------------------------------- the straight edge */

/* row 0 copied to the other rows (the edge's rows are all alike: a texel's
 * rows of the box past the image's last row would read nothing) */
static void dupRow(uint8_t *img, uint32_t w)
{
    for (uint32_t y = 1; y < ROWS; y++) {
        memcpy(img + (size_t)y * w * 4u, img, (size_t)w * 4u);
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* one bilinear tap of row 0's alpha at c (texel k's centre at k + 0.5),
 * rounded to the byte as sprite_comp_ps rounds the sample */
static uint32_t tap(const uint8_t *img, uint32_t w, double c)
{
    const double x = c - 0.5, f0 = floor(x), f = x - f0;
    const int a = clampi((int)f0, 0, (int)w - 1), b = clampi((int)f0 + 1, 0, (int)w - 1);
    const double v = img[(size_t)a * 4u + 3u] * (1.0 - f) + img[(size_t)b * 4u + 3u] * f;
    return (uint32_t)floor(v + 0.5);
}

/* the three composites' darkening at SCENE texel j (the share left) */
static double shade(uint8_t *const lv[RD_SR_LEVELS], const uint32_t lw[RD_SR_LEVELS],
                    const double scale[RD_SR_LEVELS], double S, int j)
{
    const double X = ((double)j + 0.5) / S;
    double d = 1.0;
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        const double u = 0.25 + X / (double)(2u << k); /* uv 0.25 .. W + 0.25 over 576 */
        const uint32_t as = (tap(lv[k], lw[k], u * scale[k]) * kBlend[k]) >> 7;
        d *= 1.0 - (double)as / 128.0;
    }
    return d;
}

/* the 10-90 width and the 50 % point of a profile rising or falling once */
static bool profileOf(const double *p, int n, double *width, double *mid)
{
    double lo = p[0], hi = p[0];
    for (int i = 1; i < n; i++) {
        lo = p[i] < lo ? p[i] : lo;
        hi = p[i] > hi ? p[i] : hi;
    }
    if (!(hi > lo)) {
        return false;
    }
    double at[3] = {-1.0, -1.0, -1.0};
    static const double kLevel[3] = {0.1, 0.9, 0.5};
    for (int l = 0; l < 3; l++) {
        for (int i = 1; i < n; i++) {
            const double a = (p[i - 1] - lo) / (hi - lo), b = (p[i] - lo) / (hi - lo);
            if (a < kLevel[l] && kLevel[l] <= b) {
                at[l] = (double)(i - 1) + (kLevel[l] - a) / (b - a);
                break;
            }
        }
    }
    if (at[0] < 0.0 || at[1] < 0.0 || at[2] < 0.0) {
        return false;
    }
    *width = at[1] - at[0];
    *mid = at[2];
    return true;
}

typedef struct {
    double meanO, sdO, wanderO, meanH, sdH, wanderH, darkO, darkH;
} Sweep;

static bool sweep(double S, Sweep *out)
{
    const uint32_t cw = (uint32_t)floor((double)GSW * S + 0.5);
    const float sc = (float)cw / (float)GSW; /* tw / w */
    const uint32_t L = rd_sr_level_scale(sc);
    double wO[PHASES], mO[PHASES], wH[PHASES], mH[PHASES];
    bool ok = true;
    uint8_t *cntA = calloc(cw, 1), *redA = calloc(GSW, 1);
    uint8_t *gs[RD_SR_LEVELS] = {0}, *fine[RD_SR_LEVELS] = {0}, *tmp = NULL;
    uint32_t lw[RD_SR_LEVELS];
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        gs[k] = calloc((size_t)kW[k] * ROWS * 4u, 1);
        lw[k] = kW[k] * L;
        fine[k] = calloc((size_t)lw[k] * ROWS * 4u, 1);
        ok = ok && gs[k] && fine[k];
    }
    tmp = calloc((size_t)lw[0] * ROWS * 4u, 1);
    const int j0 = (int)(250.0 * S), j1 = (int)(350.0 * S), nj = j1 - j0;
    double *po = calloc((size_t)nj, sizeof(double)), *ph = calloc((size_t)nj, sizeof(double));
    ok = ok && cntA && redA && tmp && po && ph;
    for (int phase = 0; ok && phase < PHASES; phase++) {
        const double e = 300.0 + 8.0 * phase / PHASES;
        for (uint32_t i = 0; i < cw; i++) {
            cntA[i] = ((double)i + 0.5) / (double)sc < e ? 128u : 0u;
        }
        /* the PS2 chain: the box reduction (box_reduce_ps), then the GS levels */
        for (uint32_t p = 0; p < GSW; p++) {
            const double lo = p * (double)sc, hi = (p + 1) * (double)sc;
            double acc = 0.0;
            for (int i = (int)floor(lo); i < (int)ceil(hi) && i < (int)cw; i++) {
                acc += cntA[i] / 255.0 * (fmin(hi, i + 1.0) - fmax(lo, (double)i));
            }
            redA[p] = (uint8_t)floor(acc / (double)sc * 255.0 + 0.5);
        }
        uint8_t *red = rowsOf(redA, GSW), *cnt = rowsOf(cntA, cw);
        if (!red || !cnt) {
            free(red);
            free(cnt);
            ok = false;
            break;
        }
        const uint8_t *src = red;
        uint32_t sw = GSW;
        for (int k = 0; k < RD_SR_LEVELS; k++) {
            for (uint32_t x = 0; x < kW[k]; x++) {
                const RdSrStep p = stepOf(k == 0, 2.0f, 1.0f);
                rd_sr_step_texel(src, sw, ROWS, &p, x, 0, gs[k] + (size_t)x * 4u);
            }
            dupRow(gs[k], kW[k]);
            src = gs[k];
            sw = kW[k];
        }
        /* the fine chain: the full-size count, the steps at the levels' scale L, the
         * tent across (the rows at scale 1: no tent down) */
        src = cnt;
        sw = cw;
        float ss = sc;
        for (int k = 0; k < RD_SR_LEVELS; k++) {
            for (uint32_t x = 0; x < lw[k]; x++) {
                const RdSrStep p = stepOf(k == 0, 2.0f * ss / (float)L, ss);
                rd_sr_step_texel(src, sw, ROWS, &p, x, 0, fine[k] + (size_t)x * 4u);
            }
            dupRow(fine[k], lw[k]);
            src = fine[k];
            sw = lw[k];
            ss = (float)L;
        }
        for (int k = 0; k < RD_SR_LEVELS; k++) {
            for (uint32_t x = 0; x < lw[k]; x++) {
                rd_sr_tent_texel(fine[k], lw[k], ROWS, 0, (float)L, x, 0, tmp + (size_t)x * 4u);
            }
            memcpy(fine[k], tmp, (size_t)lw[k] * 4u);
            dupRow(fine[k], lw[k]);
        }
        free(red);
        free(cnt);
        const uint32_t gw[RD_SR_LEVELS] = {kW[0], kW[1], kW[2]};
        const double one[RD_SR_LEVELS] = {1.0, 1.0, 1.0};
        const double fineScale[RD_SR_LEVELS] = {(double)L, (double)L, (double)L};
        for (int j = j0; j < j1; j++) {
            po[j - j0] = shade(gs, gw, one, S, j);
            ph[j - j0] = shade(fine, lw, fineScale, S, j);
            out->darkO += (1.0 - po[j - j0]) / PHASES;
            out->darkH += (1.0 - ph[j - j0]) / PHASES;
        }
        ok = profileOf(po, nj, &wO[phase], &mO[phase]) && profileOf(ph, nj, &wH[phase], &mH[phase]);
        mO[phase] += (double)j0 - e * S;
        mH[phase] += (double)j0 - e * S;
    }
    if (ok) {
        double sO = 0, sH = 0, qO = 0, qH = 0, aO = 1e9, bO = -1e9, aH = 1e9, bH = -1e9;
        for (int p = 0; p < PHASES; p++) {
            sO += wO[p];
            sH += wH[p];
            aO = fmin(aO, mO[p]);
            bO = fmax(bO, mO[p]);
            aH = fmin(aH, mH[p]);
            bH = fmax(bH, mH[p]);
        }
        out->meanO = sO / PHASES;
        out->meanH = sH / PHASES;
        for (int p = 0; p < PHASES; p++) {
            qO += (wO[p] - out->meanO) * (wO[p] - out->meanO);
            qH += (wH[p] - out->meanH) * (wH[p] - out->meanH);
        }
        out->sdO = sqrt(qO / PHASES);
        out->sdH = sqrt(qH / PHASES);
        out->wanderO = bO - aO;
        out->wanderH = bH - aH;
    }
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        free(gs[k]);
        free(fine[k]);
    }
    free(tmp);
    free(cntA);
    free(redA);
    free(po);
    free(ph);
    return ok;
}

static void testEdge(void)
{
    static const double kS[] = {2.0, 2.41, 2.8125, 3.0, 4.0};
    for (size_t i = 0; i < sizeof kS / sizeof kS[0]; i++) {
        Sweep s;
        memset(&s, 0, sizeof(s));
        const bool ok = sweep(kS[i], &s);
        CHECK(ok, "S %g: the sweep ran", kS[i]);
        if (!ok) {
            continue;
        }
        printf("  S %g: the PS2's sizes: width %.3f (sd %.3f), middle wander %.3f, darkness "
               "%.3f; the scene's scale: width %.3f (sd %.3f), middle wander %.3f, darkness %.3f "
               "(SCENE texels)\n",
               kS[i], s.meanO, s.sdO, s.wanderO, s.darkO, s.meanH, s.sdH, s.wanderH, s.darkH);
        CHECK(fabs(s.darkH - s.darkO) <= RD_SRT_DARK_TOL * s.darkO,
              "S %g: the darkness %.3f against the PS2's sizes' %.3f (within %.0f %%)", kS[i],
              s.darkH, s.darkO, RD_SRT_DARK_TOL * 100.0);
        CHECK(fabs(s.meanH - s.meanO) <= RD_SRT_WIDTH_TOL * s.meanO,
              "S %g: the fine width %.3f against the PS2 chain's %.3f (within %.0f %%)", kS[i],
              s.meanH, s.meanO, RD_SRT_WIDTH_TOL * 100.0);
        CHECK(s.sdH <= RD_SRT_SD_MAX, "S %g: the fine width spread %.3f", kS[i], s.sdH);
        CHECK(s.wanderH <= RD_SRT_WANDER_MAX && s.wanderH <= 0.5 * s.wanderO,
              "S %g: the fine middle wanders %.3f (the PS2 chain %.3f)", kS[i], s.wanderH,
              s.wanderO);
    }
}

/* the recogniser over every real level sprite of the dumps (the vectors:
 * eight frame sizes 512x448 to 2048x448 and 576x512, 912x512, the wrap
 * REPEAT the game leaves): each accepted with the game's taps (offset 0,
 * weight 0.25), and the fields it judges, each broken alone, refused for
 * their own reason (rd_shadowres_math.c rd_sr_recognise) */
static void testRecognise(void)
{
    const int nv = (int)(sizeof kSftVecs / sizeof kSftVecs[0]);
    CHECK(nv >= 22, "%d vectors", nv);
    for (int i = 0; i < nv; i++) {
        const RdSrSprite sp = rd_sft_sprite(&kSftVecs[i]);
        int ox = 9, oy = 9;
        float fx = -1.0f, fy = -1.0f;
        char d[256];
        const int why = rd_sr_recognise(&sp, &ox, &oy, &fx, &fy, d, sizeof d);
        CHECK(why < 0 && ox == 0 && oy == 0 && fx == 0.25f && fy == 0.25f,
              "%s: %s (%s), offset %d,%d weight %g,%g", kSftVecs[i].where,
              why < 0 ? "accepted" : rd__shadow_fine_why_word(why), d, ox, oy, (double)fx,
              (double)fy);
        /* either corner order */
        RdSrSprite sw = sp;
        sw.x16[0] = sp.x16[1];
        sw.x16[1] = sp.x16[0];
        sw.s16[0] = sp.s16[1];
        sw.s16[1] = sp.s16[0];
        CHECK(rd_sr_recognise(&sw, &ox, &oy, &fx, &fy, d, sizeof d) < 0, "%s swapped: %s",
              kSftVecs[i].where, d);
    }
    const RdSrSprite base = rd_sft_sprite(&kSftVecs[0]);

    struct {
        int why;
        RdSrSprite sp;
    } bad[8];

    for (int k = 0; k < 8; k++) {
        bad[k].sp = base;
    }
    bad[0].why = RD_SR_WHY_MAP;
    bad[0].sp.x16[1] -= 16; /* one pixel short of the level */
    bad[1].why = RD_SR_WHY_MAP;
    bad[1].sp.s16[1] += 16.0f * (float)base.dstW; /* 3 texels a pixel */
    bad[2].why = RD_SR_WHY_FILTER;
    bad[2].sp.magF = 0;
    bad[3].why = RD_SR_WHY_BLEND;
    bad[3].sp.abe = 1;
    bad[4].why = RD_SR_WHY_TEST;
    bad[4].sp.date = 1;
    bad[5].why = RD_SR_WHY_COLOUR;
    bad[5].sp.rgba[1][3] = 81;
    bad[6].why = RD_SR_WHY_TEXFN;
    bad[6].sp.tcc = 0;
    bad[7].why = RD_SR_WHY_PRIM;
    bad[7].sp.n = 4;
    for (int k = 0; k < 8; k++) {
        int ox, oy;
        float fx, fy;
        char d[256];
        const int why = rd_sr_recognise(&bad[k].sp, &ox, &oy, &fx, &fy, d, sizeof d);
        CHECK(why == bad[k].why, "broken field %d: %s, %s expected (%s)", k,
              why < 0 ? "accepted" : rd__shadow_fine_why_word(why),
              rd__shadow_fine_why_word(bad[k].why), d);
    }
    /* the start 1/16 further: offset 0, weight 0.3125 (any sub-texel offset) */
    RdSrSprite shift = base;
    shift.s16[0] += 1.0f;
    shift.s16[1] += 1.0f;
    {
        int ox, oy;
        float fx, fy;
        char d[256];
        CHECK(rd_sr_recognise(&shift, &ox, &oy, &fx, &fy, d, sizeof d) < 0 && ox == 0 &&
                  fx == 0.3125f,
              "a 1/16 texel offset: %s, offset %d weight %g", d, ox, (double)fx);
    }
}

static void testLines(void)
{
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.shadowFineChains = 1;
    rec.shadowFinePasses = 10;
    rec.shadowFineRefused = 2;
    rec.shadowFineWhy[RD_SR_WHY_FILTER] = 2;
    rd__shadow_fine_pop(&rec);
    char text[512];
    size_t at = 0;
    text[0] = '\0';
    rd__shadow_fine_text(text, sizeof text, &at);
    printf("  10 s clause: %s\n", text);
    CHECK(strcmp(text, "; shadow levels: 1 shadows at the scene's scale, 2 at the PS2's size "
                       "(filter 2) in 1 replays (10.0 passes a replay)") == 0,
          "the 10 s clause: %s", text);
    const size_t was = at;
    rd__shadow_fine_text(text, sizeof text, &at);
    CHECK(at == was, "a second clause without a record");
    char st[256];
    rd__shadow_fine_stats(&rec, st, sizeof st);
    printf("  --stats: %s\n", st);
    CHECK(strcmp(st, "shadow levels: 1 shadows at the scene's scale, 10 passes, 2 at the PS2's "
                     "size (filter 2)") == 0,
          "the stats words: %s", st);
}

int main(void)
{
    printf("rd_shadow_fine_test\n");
    testScales();
    testBox();
    testTent();
    testScaleOne();
    testEdge();
    testRecognise();
    testLines();
    if (failures) {
        printf("rd_shadow_fine_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_fine_test: ok\n");
    return 0;
}
