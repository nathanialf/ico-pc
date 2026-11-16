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
#include "rd_shadow_count.h"
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
    /* the release's cap (RD_SR_LEVEL_CAP 3): the devices' and tests' scales
     * within it, the phone's 4.10 x 2.41 and anything a step above 3 not */
    CHECK(rd_sr_within_cap(3.0f, 3.0f) && rd_sr_within_cap(3.0000002f, 1.0f) &&
              rd_sr_within_cap(2.8125f, 2.41071f) && rd_sr_within_cap(1.0f, 2.0f) &&
              rd_sr_within_cap(1.0f, 1.0f),
          "within the cap: 3 x 3, 3.0000002, 2.8125 x 2.41, 1 x 2, 1x");
    CHECK(!rd_sr_within_cap(3.01f, 1.0f) && !rd_sr_within_cap(4.10417f, 2.41071f) &&
              !rd_sr_within_cap(1.0f, 4.82f) && !rd_sr_within_cap(32.0f, 1.0f),
          "above the cap: 3.01, 4.10 x 2.41, 4.82 down, 32");
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

/* ---------------------------------------------------------- the cuts */

static uint32_t s_rng = 12345u;

static uint32_t rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return s_rng >> 8;
}

/* the fused tent against the two passes (rd_sr_tent_texel across into a
 * temporary, then down), every texel, odd scales 1, 3, 5 on each axis,
 * random bytes and sparse levels */
static void testTent2(void)
{
    enum { TW = 37, TH = 23 };

    static uint8_t lv[TW * TH * 4], tmp[TW * TH * 4], two[TW * TH * 4];
    static const int kL[] = {1, 3, 5};
    int bad = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < TW * TH * 4; i++) {
            lv[i] =
                pass ? (uint8_t)((rnd() % 7u) == 0u ? rnd() & 255u : 0u) : (uint8_t)(rnd() & 255u);
        }
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                const int lx = kL[a], ly = kL[b];
                for (uint32_t y = 0; y < TH; y++) {
                    for (uint32_t x = 0; x < TW; x++) {
                        rd_sr_tent_texel(lv, TW, TH, 0, (float)lx, x, y, tmp + (y * TW + x) * 4u);
                    }
                }
                for (uint32_t y = 0; y < TH; y++) {
                    for (uint32_t x = 0; x < TW; x++) {
                        rd_sr_tent_texel(tmp, TW, TH, 1, (float)ly, x, y, two + (y * TW + x) * 4u);
                        uint8_t one[4];
                        rd_sr_tent2_texel(lv, TW, TH, lx, ly, x, y, one);
                        bad += memcmp(one, two + (y * TW + x) * 4u, 4) != 0;
                    }
                }
            }
        }
    }
    CHECK(bad == 0, "the fused tent differs from the two passes at %d texels", bad);
}

/* the empty-area mask: a sparse count at 2.8125 x 2.41071 with a Full pixel
 * crop, box-reduced as box_reduce_ps (the mean over each GS pixel's texels,
 * rounded), and every first-level fine texel: where the mask says empty the
 * step gives 0 (so the early return is the step's own bytes), and the mask
 * says empty somewhere */
static void testMask(void)
{
    enum { GW = 64, GH = 48 };

    const float sx = 2.8125f, sy = 2.41071f, ox = 2.5f;
    const uint32_t cw = (uint32_t)(GW * sx - 2.0f * ox), ch = (uint32_t)floorf(GH * sy);
    uint8_t *cnt = calloc((size_t)cw * ch * 4u, 1), *red = calloc((size_t)GW * GH * 4u, 1);
    if (!cnt || !red) {
        free(cnt);
        free(red);
        CHECK(0, "memory");
        return;
    }
    /* two blobs and single texels */
    for (uint32_t y = 0; y < ch; y++) {
        for (uint32_t x = 0; x < cw; x++) {
            const int in = (x > 30 && x < 52 && y > 20 && y < 33) || (x == 101 && y == 77) ||
                           (x == 140 && y == 5) || ((rnd() % 997u) == 0u);
            uint8_t *q = cnt + ((size_t)y * cw + x) * 4u;
            q[0] = q[1] = q[2] = in ? 4u : 0u;
            q[3] = in ? 0x80u : 0u;
        }
    }
    for (uint32_t gy = 0; gy < GH; gy++) {
        for (uint32_t gx = 0; gx < GW; gx++) {
            const float lx = gx * sx - ox, hx = lx + sx, ly = gy * sy, hy = ly + sy;
            float a = 0.0f;
            for (int y = (int)floorf(ly); y < (int)ceilf(hy) && y < (int)ch; y++) {
                for (int x = (int)floorf(lx) < 0 ? 0 : (int)floorf(lx);
                     x < (int)ceilf(hx) && x < (int)cw; x++) {
                    if (y < 0) {
                        continue;
                    }
                    const float w = (fminf(hx, x + 1.0f) - fmaxf(lx, (float)x)) *
                                    (fminf(hy, y + 1.0f) - fmaxf(ly, (float)y));
                    a += cnt[((size_t)y * cw + (size_t)x) * 4u + 3u] / 255.0f * w;
                }
            }
            red[((size_t)gy * GW + gx) * 4u + 3u] = (uint8_t)floorf(a / (sx * sy) * 255.0f + 0.5f);
        }
    }
    RdSrStep p;
    memset(&p, 0, sizeof(p));
    p.first = 1;
    p.kx = 2.0f * sx / 3.0f;
    p.ky = 2.0f * sy / 3.0f;
    p.ax = -ox;
    p.sx = sx;
    p.sy = sy;
    p.fx = p.fy = 0.25f;
    p.repS = p.repT = 1;
    memcpy(p.col, kCol, 4);
    int wrong = 0, empty = 0;
    for (uint32_t my = 0; my < GH / 2u * 3u; my++) {
        for (uint32_t mx = 0; mx < GW / 2u * 3u; mx++) {
            uint8_t o[4];
            rd_sr_step_texel(cnt, cw, ch, &p, mx, my, o);
            const float ax = (float)mx * p.kx + p.ax, ay = (float)my * p.ky + p.ay;
            if (rd_sr_mask_empty(red, GW, GH, cw, ch, ax, ay, sx, sy, ox)) {
                empty++;
                wrong += o[0] || o[1] || o[2] || o[3];
            }
        }
    }
    CHECK(wrong == 0 && empty > 0, "the mask: %d empty texels, %d of them not 0", empty, wrong);
    free(cnt);
    free(red);
}

/* the boxes: a count nonzero only inside a GS box, the chain run in full
 * (the reference), every nonzero texel of each level inside the level's box
 * (before the tent) and of its tent inside the final box; at a REPEAT axis
 * whose box comes near an edge the box is the whole axis */
static void testBoxes(void)
{
    enum { GW = 80, GH = 64, L = 3 };

    static const int kBox[][4] = {
        {30, 20, 41, 33}, {0, 0, 9, 7}, {70, 50, 79, 63}, {40, 30, 40, 30}};
    const uint32_t cw = GW * L, ch = GH * L;
    uint8_t *cnt = calloc((size_t)cw * ch * 4u, 1);
    uint8_t *lv[3], *fin[3];
    uint32_t lw[3], lh[3];
    for (int k = 0; k < 3; k++) {
        lw[k] = (GW >> (k + 1)) * L;
        lh[k] = (GH >> (k + 1)) * L;
        lv[k] = calloc((size_t)lw[k] * lh[k] * 4u, 1);
        fin[k] = calloc((size_t)lw[k] * lh[k] * 4u, 1);
    }
    for (size_t b = 0; cnt && b < sizeof kBox / sizeof kBox[0]; b++) {
        for (int rep = 0; rep < 2; rep++) {
            memset(cnt, 0, (size_t)cw * ch * 4u);
            for (int gy = kBox[b][1]; gy <= kBox[b][3]; gy++) {
                for (int gx = kBox[b][0]; gx <= kBox[b][2]; gx++) {
                    for (int j = 0; j < L * L; j++) {
                        if (rnd() % 3u == 0u || (gx == kBox[b][0] || gy == kBox[b][3])) {
                            uint8_t *q =
                                cnt +
                                ((size_t)(gy * L + j / L) * cw + (size_t)(gx * L + j % L)) * 4u;
                            q[0] = q[1] = q[2] = 4u;
                            q[3] = 0x80u;
                        }
                    }
                }
            }
            int lo[2] = {kBox[b][0], kBox[b][1]}, hi[2] = {kBox[b][2], kBox[b][3]};
            int ssz[2] = {GW, GH};
            const uint8_t *src = cnt;
            uint32_t sw = cw, sh = ch;
            for (int k = 0; k < 3; k++) {
                RdSrStep p;
                memset(&p, 0, sizeof(p));
                p.first = k == 0;
                p.kx = p.ky = 2.0f;
                p.sx = p.sy = (float)L;
                p.fx = p.fy = 0.25f;
                p.repS = p.repT = rep;
                memcpy(p.col, kCol, 4);
                const int dw = (int)lw[k] / L, dh = (int)lh[k] / L;
                int nlo[2], nhi[2];
                const bool any =
                    rd_sr_step_box(lo[0], hi[0], ssz[0], 0, dw, rep, &nlo[0], &nhi[0]) &&
                    rd_sr_step_box(lo[1], hi[1], ssz[1], 0, dh, rep, &nlo[1], &nhi[1]);
                int fx0 = 0, fx1 = -1, fy0 = 0, fy1 = -1;
                if (any) {
                    rd_sr_tent_box(nlo[0] * L, (nhi[0] + 1) * L - 1, L, (int)lw[k], &fx0, &fx1);
                    rd_sr_tent_box(nlo[1] * L, (nhi[1] + 1) * L - 1, L, (int)lh[k], &fy0, &fy1);
                }
                int out = 0, outFin = 0;
                for (uint32_t y = 0; y < lh[k]; y++) {
                    for (uint32_t x = 0; x < lw[k]; x++) {
                        uint8_t *q = lv[k] + ((size_t)y * lw[k] + x) * 4u;
                        rd_sr_step_texel(src, sw, sh, &p, x, y, q);
                        const int inside = any && (int)x >= nlo[0] * L &&
                                           (int)x < (nhi[0] + 1) * L && (int)y >= nlo[1] * L &&
                                           (int)y < (nhi[1] + 1) * L;
                        out += (q[0] || q[1] || q[2] || q[3]) && !inside;
                    }
                }
                for (uint32_t y = 0; y < lh[k]; y++) {
                    for (uint32_t x = 0; x < lw[k]; x++) {
                        uint8_t *q = fin[k] + ((size_t)y * lw[k] + x) * 4u;
                        rd_sr_tent2_texel(lv[k], lw[k], lh[k], L, L, x, y, q);
                        const int inside =
                            (int)x >= fx0 && (int)x <= fx1 && (int)y >= fy0 && (int)y <= fy1;
                        outFin += (q[0] || q[1] || q[2] || q[3]) && !inside;
                    }
                }
                CHECK(
                    out == 0 && outFin == 0,
                    "box %zu %s level %d: %d texels outside the step's box, %d outside the tent's",
                    b, rep ? "repeat" : "clamp", k, out, outFin);
                if (!any) {
                    break;
                }
                lo[0] = nlo[0];
                lo[1] = nlo[1];
                hi[0] = nhi[0];
                hi[1] = nhi[1];
                ssz[0] = dw;
                ssz[1] = dh;
                src = lv[k];
                sw = lw[k];
                sh = lh[k];
            }
        }
    }
    free(cnt);
    for (int k = 0; k < 3; k++) {
        free(lv[k]);
        free(fin[k]);
    }
}

/* the render area: a level texture over 200 pictures whose boxes move,
 * shrink, empty and grow, its passes clearing the area rd_sr_area gives (as
 * Vulkan clears the render area alone; and, the second time, as a backend
 * without tiles clears the whole) and drawing inside the box: after every
 * pass the texture is 0 outside the box and the draw's bytes inside it (the
 * texture as a pass over the whole would leave it), and an empty box over a
 * texture already 0 takes no pass */
static void testArea(void)
{
    enum { AW = 40, AH = 30 };

    static uint8_t tex[AW * AH], want[AW * AH];
    for (int whole = 0; whole < 2; whole++) {
        RdSrTrack tr;
        memset(&tr, 0, sizeof(tr));
        memset(tex, 0xAB, sizeof tex); /* a new texture's contents: anything */
        int bad = 0, skipped = 0;
        for (int f = 0; f < 200; f++) {
            RhiRect box = {0, 0, 0, 0};
            if (rnd() % 5u != 0u) {
                box.x = (int32_t)(rnd() % AW);
                box.y = (int32_t)(rnd() % AH);
                box.w = 1u + rnd() % (uint32_t)(AW - box.x);
                box.h = 1u + rnd() % (uint32_t)(AH - box.y);
            }
            memset(want, 0, sizeof want);
            for (uint32_t y = 0; y < box.h; y++) {
                for (uint32_t x = 0; x < box.w; x++) {
                    want[(box.y + (int32_t)y) * AW + box.x + (int32_t)x] =
                        (uint8_t)(rnd() % 3u ? rnd() & 255u : 0u);
                }
            }
            RhiRect area;
            if (!rd_sr_area(&tr.last, &tr.known, &box, AW, AH, &area)) {
                skipped++;
            } else {
                for (int y = 0; y < AH; y++) {
                    for (int x = 0; x < AW; x++) {
                        const int in = x >= area.x && x < area.x + (int)area.w && y >= area.y &&
                                       y < area.y + (int)area.h;
                        if (in || whole) {
                            tex[y * AW + x] = 0; /* the CLEAR */
                        }
                    }
                }
                for (uint32_t y = 0; y < box.h; y++) {
                    for (uint32_t x = 0; x < box.w; x++) {
                        const int i = (box.y + (int32_t)y) * AW + box.x + (int32_t)x;
                        tex[i] = want[i]; /* the draw */
                    }
                }
            }
            bad += memcmp(tex, want, sizeof tex) != 0;
        }
        CHECK(bad == 0 && skipped > 0, "the render area (%s): %d pictures wrong, %d passes skipped",
              whole ? "the whole cleared" : "the area cleared", bad, skipped);
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
    rec.shadowFineTexels = 250000;
    rec.shadowFineArea = 1000000;
    rec.shadowFineRendered = 400000;
    rec.shadowCountStencil = 1;
    rec.shadowCountGuard = 0;
    rec.shadowCountForm = RD_SC_STENCIL + 1;
    rd__shadow_fine_pop(&rec);
    char text[512];
    size_t at = 0;
    text[0] = '\0';
    rd__shadow_fine_text(text, sizeof text, &at);
    printf("  10 s clause: %s\n", text);
    CHECK(strcmp(text, "; shadow levels: 1 shadows at the scene's scale, 2 at the PS2's size "
                       "(filter 2) in 1 replays (10.0 passes a shadow, drawing 25.0 % of their "
                       "targets' texels in render areas of 40.0 %, 0.25 M texels a shadow; the "
                       "count 1 from the stencil, 0 full-size passes, 0 seven draws, 0 guard "
                       "fallbacks, form stencil)") == 0,
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
    rd__shadow_fine_drawn(&rec, st, sizeof st);
    CHECK(strcmp(st, "shadow levels drawn: 250000 texels of the 1000000 their passes' targets "
                     "hold (25.0 %), render areas 400000 texels") == 0,
          "the drawn words: %s", st);
}

/* the full-size count's form (rd_shadow_count.c): the trial's order, the
 * median, the choice and its ties, list 3's time from a timed replay's
 * pieces (an unstamped piece leaves the sample out), the --stats words */
static void testCountForms(void)
{
    uint8_t got[RD_SC_FORMS] = {0, 0, 0};
    /* the turn: the stencil step, the seven draws, the full-size pass */
    CHECK(rd_sc_trial_next(0, got) == RD_SC_STENCIL && rd_sc_trial_next(1, got) == RD_SC_SEVEN &&
              rd_sc_trial_next(2, got) == RD_SC_FULL && rd_sc_trial_next(3, got) == RD_SC_STENCIL,
          "the trial's turn");
    got[RD_SC_SEVEN] = RD_SC_TRIAL_N; /* the seven draws' samples in: left out */
    CHECK(rd_sc_trial_next(1, got) == RD_SC_FULL, "a form whose samples are in is left out");
    got[RD_SC_STENCIL] = got[RD_SC_FULL] = RD_SC_TRIAL_N;
    CHECK(rd_sc_trial_next(0, got) == -1, "every sample in: no form");
    const float v3[3] = {9.0f, 4.0f, 30.0f};
    const float v2[2] = {5.0f, 3.0f};
    CHECK(rd_sc_median(v3, 3) == 9.0f && rd_sc_median(v2, 2) == 3.0f && rd_sc_median(v3, 1) == 9.0f,
          "the median");

    /* the choice by table (rd_sc_choose): samples [stencil, full, seven],
     * counts, the form and margin wanted (margin: the larger of 10 % of the
     * stencil step's median and the widest spread) */
    static const struct {
        float ms[RD_SC_FORMS][RD_SC_TRIAL_N]; /* indexed by RD_SC_FULL, _STENCIL, _SEVEN */
        uint8_t n[RD_SC_FORMS];
        int want;
        float margin;
        const char *what;
    } k_choose[] = {
        /* full 9.5-10, stencil 4.0-4.2, seven 7.0-7.5: spread 0.5 > 0.41 */
        {{{9.8f, 9.5f, 10.0f}, {4.0f, 4.2f, 4.1f}, {7.3f, 7.0f, 7.5f}},
         {3, 3, 3},
         RD_SC_STENCIL,
         0.5f,
         "the stencil step the cheapest"},
        /* stencil 12 (margin 1.2 > spread 0.7): full 9.2 < 10.8 */
        {{{9.0f, 9.4f, 9.2f}, {12.0f, 12.5f, 11.75f}, {10.0f, 10.25f, 10.125f}},
         {3, 3, 3},
         RD_SC_FULL,
         1.2f,
         "the full-size pass clearly cheaper"},
        /* a marginal win: full 4.75 against 5.0 - 0.5 */
        {{{4.75f, 4.875f, 4.625f}, {5.0f, 5.125f, 4.875f}, {6.0f, 6.0f, 6.0f}},
         {3, 3, 3},
         RD_SC_STENCIL,
         0.5f,
         "a marginal win keeps the stencil step"},
        /* noisy stencil samples (spread 4): full 4.125 not under 6 - 4 */
        {{{4.0f, 4.125f, 4.25f}, {5.0f, 9.0f, 6.0f}, {8.0f, 8.0f, 8.0f}},
         {3, 3, 3},
         RD_SC_STENCIL,
         4.0f,
         "a noisy stencil step keeps it"},
        /* the seven draws left out (n 0): full 7 < 10 - 1 */
        {{{7.0f, 7.0f, 7.0f}, {10.0f, 10.0f, 10.0f}, {0.0f, 0.0f, 0.0f}},
         {3, 3, 0},
         RD_SC_FULL,
         1.0f,
         "the seven draws left out"},
        /* both under by the margin, equal: the full-size pass */
        {{{7.0f, 7.0f, 7.0f}, {10.0f, 10.0f, 10.0f}, {7.0f, 7.0f, 7.0f}},
         {3, 3, 3},
         RD_SC_FULL,
         1.0f,
         "equal alternatives: the full-size pass"},
        /* the seven draws the lower */
        {{{7.0f, 7.0f, 7.0f}, {10.0f, 10.0f, 10.0f}, {6.0f, 6.0f, 6.0f}},
         {3, 3, 3},
         RD_SC_SEVEN,
         1.0f,
         "the seven draws the lowest"},
        /* the stencil step not measured */
        {{{7.0f, 7.0f, 7.0f}, {10.0f, 10.0f, 0.0f}, {6.0f, 6.0f, 6.0f}},
         {3, 2, 3},
         -1,
         0.0f,
         "the stencil step not measured"},
    };

    for (size_t i = 0; i < sizeof k_choose / sizeof k_choose[0]; i++) {
        float med[RD_SC_FORMS], margin = -1.0f;
        const int got = rd_sc_choose(k_choose[i].ms, k_choose[i].n, med, &margin);
        CHECK(got == k_choose[i].want && fabsf(margin - k_choose[i].margin) < 1.0e-4f,
              "the choice (%s): %d (%d), margin %.4f (%.4f)", k_choose[i].what, got,
              k_choose[i].want, (double)margin, (double)k_choose[i].margin);
    }
    /* the check's comparison (rd_sc_check_differ): rows pitch 12 bytes,
     * 2 x 2 texels, one texel off */
    {
        uint8_t a[24], b[24];
        memset(a, 0, sizeof a);
        memset(a + 4, 0x80, 4);
        memcpy(b, a, sizeof b);
        uint32_t fx = 0, fy = 0;
        CHECK(rd_sc_check_differ(a, b, 2, 2, 12, &fx, &fy) == 0, "the same texels");
        b[12 + 4 + 3] = 0x7F; /* texel (1, 1) */
        b[8] = 0xFF;          /* past the row's texels: not compared */
        CHECK(rd_sc_check_differ(a, b, 2, 2, 12, &fx, &fy) == 1 && fx == 1 && fy == 1,
              "one texel apart at (1,1): (%u,%u)", (unsigned)fx, (unsigned)fy);
    }
    RdPerfPass p[4];
    memset(p, 0, sizeof(p));
    p[0].list = 2;
    p[0].gpuUs = 1000.0f;
    p[1].list = 3;
    p[1].gpuUs = 1500.0f;
    p[2].list = 3;
    p[2].gpuUs = 2500.0f;
    p[3].list = 4;
    p[3].gpuUs = 700.0f;
    bool written = false;
    const double ms = rd_sc_list_ms(p, 4, 3, &written);
    CHECK(written && ms == 4.0, "list 3's time: %.3f ms (4.000), written %d", ms, (int)written);
    p[2].gpuUs = -1.0f; /* its stamps unwritten */
    (void)rd_sc_list_ms(p, 4, 3, &written);
    CHECK(!written, "an unstamped piece: no sample");
    (void)rd_sc_list_ms(p, 4, 5, &written);
    CHECK(!written, "no piece of the list: no sample");
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.shadowCountStencil = 1;
    rec.shadowCountFull = 2;
    rec.shadowCountSeven = 3;
    rec.shadowCountGuard = 4;
    rec.shadowCountForm = RD_SC_SEVEN + 1;
    char st[256];
    rd__shadow_count_stats(&rec, st, sizeof st);
    printf("  --stats: %s\n", st);
    CHECK(strcmp(st, "shadow count above 1x: 1 from the stencil, 2 full-size passes, 3 seven "
                     "draws, 4 guard fallbacks (form seven)") == 0,
          "the count's stats words: %s", st);
    rec.shadowCountForm = 0;
    rd__shadow_count_stats(&rec, st, sizeof st);
    CHECK(strstr(st, "(form none)") != NULL, "no form: %s", st);
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
    testTent2();
    testMask();
    testBoxes();
    testArea();
    testLines();
    testCountForms();
    if (failures) {
        printf("rd_shadow_fine_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_fine_test: ok\n");
    return 0;
}
