/* rd_shadowres_math.c: the pure half of the shadow's levels at the scene's
 * scale (rd_shadowres.h): the scales, the step's and the tent's weights,
 * the reference of the two shaders, and the recogniser of the game's level
 * sprites.  No renderer state: the CPU test (rd_shadow_fine_test.c, with the
 * chains of the dumps as vectors) and the dump harness compile it on its
 * own. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_clause.h"
#include "rd_shadow_count.h" /* the count's form words */
#include "rd_shadowres.h"
#include "rd_state.h"

static const char *const kWhy[RD_SR_WHY_N] = {
    "not one two-vertex sprite with fixed UVs in the world's space",
    "drawn through the frame camera, the ST shift or PRIM.AA1",
    "its texture function is not MODULATE with TCC RGBA",
    "it blends, sets FBA or masks a colour channel",
    "it tests alpha, DATE or Z",
    "its filters are not both bilinear",
    "its two vertex colours differ",
    "its UVs do not step 2 texels a pixel over the whole level",
    "its source is not the count kept at its full size",
    "the shaders or pipelines could not be made",
    "the textures could not be made",
    "the scene's scale is above 3 on an axis (the largest the levels were measured at)"};

static const char *const kWhyWord[RD_SR_WHY_N] = {"prim",  "flags",  "texfn",    "blend",
                                                  "test",  "filter", "colour",   "map",
                                                  "count", "pipes",  "textures", "scale"};

const char *rd__shadow_fine_why_word(int why)
{
    return why >= 0 && why < RD_SR_WHY_N ? kWhyWord[why] : "?";
}

const char *rd_sr_why_text(int why)
{
    return why >= 0 && why < RD_SR_WHY_N ? kWhy[why] : "?";
}

/* ------------------------------------------------------------ arithmetic */

float rd_sr_count_scale(uint32_t w, uint32_t tw, float sx, float ox)
{
    if (w == 0u) {
        return 1.0f;
    }
    return ox > 0.0f ? sx : (float)tw / (float)w;
}

uint32_t rd_sr_level_scale(float s)
{
    if (!(s > 1.0f)) {
        return 1u;
    }
    /* the float's noise above a whole scale (3.0000002) is not a step up */
    const uint32_t n = (uint32_t)ceilf(s - 1.0e-3f);
    return (n & 1u) ? n : n + 1u;
}

bool rd_sr_within_cap(float cx, float cy)
{
    return cx <= RD_SR_SCALE_MAX && cy <= RD_SR_SCALE_MAX &&
           rd_sr_level_scale(cx) <= RD_SR_LEVEL_CAP && rd_sr_level_scale(cy) <= RD_SR_LEVEL_CAP;
}

float rd_sr_box_w(float i, float a, float s, float f)
{
    const float b = a + s, c = b + s;
    const float w0 = fmaxf(fminf(b, i + 1.0f) - fmaxf(a, i), 0.0f);
    const float w1 = fmaxf(fminf(c, i + 1.0f) - fmaxf(b, i), 0.0f);
    return ((1.0f - f) * w0 + f * w1) / s;
}

int rd_sr_index(int i, int n, int repeat)
{
    if (repeat) {
        const int r = i % n;
        return r < 0 ? r + n : r;
    }
    return i < 0 ? 0 : i > n - 1 ? n - 1 : i;
}

bool rd_sr_offset(float u0, int *whole, float *frac)
{
    /* the GS's bilinear tap at u: texels floor(u - 1/2) and the next,
     * weighted by the fraction; u0 is u at the level's pixel 0 */
    const float t = u0 - 0.5f;
    const float w = floorf(t);
    if (!(fabsf(t) < 1.0e6f)) {
        return false;
    }
    *whole = (int)w;
    *frac = t - w;
    return true;
}

float rd_sr_tent_w(int t, float s)
{
    const float c = 0.5f * s - 0.5f;
    return fmaxf(s - fabsf((float)t - c), 0.0f);
}

void rd_sr_tent_range(float s, int *t0, int *t1)
{
    const float c = 0.5f * s - 0.5f;
    *t0 = (int)floorf(c - s);
    const int e = (int)ceilf(c + s);
    *t1 = e < *t0 + RD_SR_SPAN_MAX ? e : *t0 + RD_SR_SPAN_MAX;
}

static float texelOf(const uint8_t *src, uint32_t sw, int x, int y, int ch)
{
    return (float)src[((size_t)y * sw + (size_t)x) * 4u + (size_t)ch] / 255.0f;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* the level before at (jx, jy), linear, the indices wrapped or held
 * (sr_point) */
static void pointAt(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, float jx,
                    float jy, float w, float acc[4])
{
    const float fx0 = floorf(jx), fy0 = floorf(jy);
    const float fx = jx - fx0, fy = jy - fy0;
    const int ax = rd_sr_index((int)fx0, (int)sw, p->repS);
    const int bx = rd_sr_index((int)fx0 + 1, (int)sw, p->repS);
    const int ay = rd_sr_index((int)fy0, (int)sh, p->repT);
    const int by = rd_sr_index((int)fy0 + 1, (int)sh, p->repT);
    for (int ch = 0; ch < 4; ch++) {
        const float top =
            texelOf(src, sw, ax, ay, ch) * (1.0f - fx) + texelOf(src, sw, bx, ay, ch) * fx;
        const float bot =
            texelOf(src, sw, ax, by, ch) * (1.0f - fx) + texelOf(src, sw, bx, by, ch) * fx;
        acc[ch] += (top * (1.0f - fy) + bot * fy) * w;
    }
}

void rd_sr_step_value(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, uint32_t mx,
                      uint32_t my, float val[4])
{
    const float a[2] = {(float)mx * p->kx + p->ax, (float)my * p->ky + p->ay};
    const float fx = p->fx, fy = p->fy;
    float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (p->first) {
        const int x0 = (int)floorf(a[0]), y0 = (int)floorf(a[1]);
        int lx = (int)ceilf(a[0] + 2.0f * p->sx), ly = (int)ceilf(a[1] + 2.0f * p->sy);
        lx = lx < x0 + RD_SR_SPAN_MAX ? lx : x0 + RD_SR_SPAN_MAX;
        ly = ly < y0 + RD_SR_SPAN_MAX ? ly : y0 + RD_SR_SPAN_MAX;
        for (int y = y0; y < ly; y++) {
            const float wy = rd_sr_box_w((float)y, a[1], p->sy, fy);
            const int yy = rd_sr_index(y, (int)sh, p->repT);
            for (int x = x0; x < lx; x++) {
                const float w = rd_sr_box_w((float)x, a[0], p->sx, fx) * wy;
                const int xx = rd_sr_index(x, (int)sw, p->repS);
                for (int ch = 0; ch < 4; ch++) {
                    sum[ch] += texelOf(src, sw, xx, yy, ch) * w;
                }
            }
        }
    } else {
        pointAt(src, sw, sh, p, a[0], a[1], (1.0f - fx) * (1.0f - fy), sum);
        pointAt(src, sw, sh, p, a[0] + p->sx, a[1], fx * (1.0f - fy), sum);
        pointAt(src, sw, sh, p, a[0], a[1] + p->sy, (1.0f - fx) * fy, sum);
        pointAt(src, sw, sh, p, a[0] + p->sx, a[1] + p->sy, fx * fy, sum);
    }
    for (int ch = 0; ch < 4; ch++) {
        val[ch] = sum[ch] * 255.0f;
    }
}

uint8_t rd_sr_step_byte(uint32_t t, uint8_t col)
{
    const uint32_t o = (t * col) >> 7;
    return (uint8_t)(o > 255u ? 255u : o);
}

void rd_sr_step_texel(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, uint32_t mx,
                      uint32_t my, uint8_t out[4])
{
    float val[4];
    rd_sr_step_value(src, sw, sh, p, mx, my, val);
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = rd_sr_step_byte((uint32_t)floorf(val[ch] + 0.5f), p->col[ch]);
    }
}

void rd_sr_tent_texel(const uint8_t *src, uint32_t w, uint32_t h, int down, float s, uint32_t x,
                      uint32_t y, uint8_t out[4])
{
    int t0, t1;
    rd_sr_tent_range(s, &t0, &t1);
    const int n = down ? (int)y : (int)x;
    const int lim = (down ? (int)h : (int)w) - 1;
    float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f}, wsum = 0.0f;
    for (int t = t0; t <= t1; t++) {
        const float wt = rd_sr_tent_w(t, s);
        const int k = clampi(n - t, 0, lim);
        const uint8_t *p =
            src + ((size_t)(down ? k : (int)y) * w + (size_t)(down ? (int)x : k)) * 4u;
        for (int ch = 0; ch < 4; ch++) {
            sum[ch] += (float)p[ch] * wt;
        }
        wsum += wt;
    }
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = (uint8_t)floorf(sum[ch] / (wsum > 1.0e-6f ? wsum : 1.0e-6f) + 0.5f);
    }
}

/* --------------------------------------------------------- the recogniser */

int rd_sr_recognise(const RdSrSprite *sp, int *ox, int *oy, float *fx, float *fy, char *detail,
                    size_t dsize)
{
    detail[0] = '\0';
    if (sp->prim != RD_PRIM_SPRITES || sp->n != 2 || sp->uvFixed == 0 || sp->space == RD_SPACE_UI ||
        sp->srcW == 0 || sp->srcH == 0 || sp->dstW == 0 || sp->dstH == 0) {
        snprintf(detail, dsize, "prim %u, %u vertices, fixed UVs %u, space %u", (unsigned)sp->prim,
                 (unsigned)sp->n, (unsigned)sp->uvFixed, (unsigned)sp->space);
        return RD_SR_WHY_PRIM;
    }
    if (sp->frameCam || sp->stShift || sp->aa1) {
        snprintf(detail, dsize, "frame camera %u, ST shift %u, AA1 %u", (unsigned)sp->frameCam,
                 (unsigned)sp->stShift, (unsigned)sp->aa1);
        return RD_SR_WHY_FLAGS;
    }
    if (sp->tcc != RD_TCC_RGBA || sp->texFn != RD_TEXFN_MODULATE) {
        snprintf(detail, dsize, "TCC %u, TFX %u", (unsigned)sp->tcc, (unsigned)sp->texFn);
        return RD_SR_WHY_TEXFN;
    }
    if (sp->abe || sp->fba || sp->colorMask != 0xF) {
        snprintf(detail, dsize, "ABE %u, FBA %u, mask %x", (unsigned)sp->abe, (unsigned)sp->fba,
                 (unsigned)sp->colorMask);
        return RD_SR_WHY_BLEND;
    }
    if ((sp->ate && sp->atst != RD_ATST_ALWAYS) || sp->date != RD_DATE_OFF ||
        (sp->zte && sp->ztst != RD_ZTST_ALWAYS)) {
        snprintf(detail, dsize, "ATE %u ATST %u, DATE %u, ZTE %u ZTST %u", (unsigned)sp->ate,
                 (unsigned)sp->atst, (unsigned)sp->date, (unsigned)sp->zte, (unsigned)sp->ztst);
        return RD_SR_WHY_TEST;
    }
    if (sp->magF != RD_FILTER_LINEAR || sp->minF != RD_FILTER_LINEAR) {
        snprintf(detail, dsize, "mag %u, min %u", (unsigned)sp->magF, (unsigned)sp->minF);
        return RD_SR_WHY_FILTER;
    }
    if (memcmp(sp->rgba[0], sp->rgba[1], 4) != 0) {
        snprintf(detail, dsize, "(%u,%u,%u,%u) and (%u,%u,%u,%u)", sp->rgba[0][0], sp->rgba[0][1],
                 sp->rgba[0][2], sp->rgba[0][3], sp->rgba[1][0], sp->rgba[1][1], sp->rgba[1][2],
                 sp->rgba[1][3]);
        return RD_SR_WHY_COLOUR;
    }
    /* the corners in GS pixels of the level (the origin 2048 - size / 2 of
     * the frame the sprite is drawn in), the UVs in texels; either corner
     * order */
    const float gx = (float)(2048 - (int32_t)(sp->gsW >> 1)),
                gy = (float)(2048 - (int32_t)(sp->gsH >> 1));
    const int i0 = sp->x16[0] <= sp->x16[1] ? 0 : 1, j0 = sp->y16[0] <= sp->y16[1] ? 0 : 1;
    const float xa = (float)sp->x16[i0] / 16.0f - gx, xb = (float)sp->x16[1 - i0] / 16.0f - gx;
    const float ya = (float)sp->y16[j0] / 16.0f - gy, yb = (float)sp->y16[1 - j0] / 16.0f - gy;
    const float ua = sp->s16[i0] / 16.0f + sp->uOff, ub = sp->s16[1 - i0] / 16.0f + sp->uOff;
    const float va = sp->t16[j0] / 16.0f + sp->vOff, vb = sp->t16[1 - j0] / 16.0f + sp->vOff;
    const float du = xb - xa > 0.0f ? (ub - ua) / (xb - xa) : 0.0f;
    const float dv = yb - ya > 0.0f ? (vb - va) / (yb - ya) : 0.0f;
    /* u at the level's pixel 0 (the GS samples pixel x at x) */
    const float u0 = ua - du * xa, v0 = va - dv * ya;
    /* the GS covers pixel p when ceil(x0) <= p < ceil(x1): the whole level
     * when ceil(xa) <= 0 and ceil(xb) >= its width (the game's sprite runs
     * from -0.25 to size - 0.25) */
    const bool covers = ceilf(xa) <= 0.0f && ceilf(ya) <= 0.0f && ceilf(xb) >= (float)sp->dstW &&
                        ceilf(yb) >= (float)sp->dstH;
    if (fabsf(du - 2.0f) > 1.0e-3f || fabsf(dv - 2.0f) > 1.0e-3f || !covers ||
        !rd_sr_offset(u0, ox, fx) || !rd_sr_offset(v0, oy, fy)) {
        snprintf(detail, dsize,
                 "x %.4g..%.4g y %.4g..%.4g of %ux%u (%s), u %.4g..%.4g v %.4g..%.4g (%.4g and "
                 "%.4g a pixel, from %.4g, %.4g)",
                 (double)xa, (double)xb, (double)ya, (double)yb, sp->dstW, sp->dstH,
                 covers ? "covered" : "not covered", (double)ua, (double)ub, (double)va, (double)vb,
                 (double)du, (double)dv, (double)u0, (double)v0);
        return RD_SR_WHY_MAP;
    }
    return -1;
}

/* ------------------------------------------------------------ the lines */

static struct {
    uint64_t chains, passes, original, why[RD_SR_WHY_N], texels, area, rendered;
    /* the full-size count's forms (rd_shadow_count.h) and the last form
     * asked + 1 */
    uint64_t stencil, full, seven, guard;
    uint32_t replays;
    uint8_t form;
} s_sum;

void rd__shadow_fine_pop(const RdPerfRecord *rec)
{
    s_sum.chains += rec->shadowFineChains;
    s_sum.passes += rec->shadowFinePasses;
    s_sum.original += rec->shadowFineRefused;
    s_sum.texels += rec->shadowFineTexels;
    s_sum.area += rec->shadowFineArea;
    s_sum.rendered += rec->shadowFineRendered;
    for (int k = 0; k < RD_SR_WHY_N; k++) {
        s_sum.why[k] += rec->shadowFineWhy[k];
    }
    s_sum.stencil += rec->shadowCountStencil;
    s_sum.full += rec->shadowCountFull;
    s_sum.seven += rec->shadowCountSeven;
    s_sum.guard += rec->shadowCountGuard;
    if (rec->shadowCountForm) {
        s_sum.form = rec->shadowCountForm;
    }
    s_sum.replays++;
}

/* " (filter 3, map 1)": the reasons with counts, empty for none */
static void whyList(char *out, size_t size, const uint64_t *why)
{
    size_t at = 0;
    out[0] = '\0';
    for (int k = 0; k < RD_SR_WHY_N && at + 1 < size; k++) {
        if (!why[k]) {
            continue;
        }
        const int w = snprintf(out + at, size - at, "%s%s %llu", at ? ", " : " (",
                               rd__shadow_fine_why_word(k), (unsigned long long)why[k]);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
    if (at && at + 2 < size) {
        out[at++] = ')';
        out[at] = '\0';
    }
}

void rd__shadow_fine_text(char *out, size_t size, size_t *at)
{
    if (s_sum.replays == 0 || (s_sum.chains == 0 && s_sum.original == 0) || *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    char why[160];
    whyList(why, sizeof why, s_sum.why);
    const int w = snprintf(
        out + *at, size - *at,
        "; shadow levels: %llu shadows at the scene's scale, %llu at the PS2's "
        "size%s in %u replays (%.1f passes a shadow, drawing %.1f %% of their targets' "
        "texels in render areas of %.1f %%, %.2f M texels a shadow; the count %llu from the "
        "stencil, %llu full-size passes, %llu seven draws, %llu guard fallbacks, form %s)",
        (unsigned long long)s_sum.chains, (unsigned long long)s_sum.original, why,
        (unsigned)s_sum.replays, s_sum.chains ? (double)s_sum.passes / (double)s_sum.chains : 0.0,
        s_sum.area ? 100.0 * (double)s_sum.texels / (double)s_sum.area : 0.0,
        s_sum.area ? 100.0 * (double)s_sum.rendered / (double)s_sum.area : 0.0,
        s_sum.chains ? (double)s_sum.texels / (double)s_sum.chains / 1.0e6 : 0.0,
        (unsigned long long)s_sum.stencil, (unsigned long long)s_sum.full,
        (unsigned long long)s_sum.seven, (unsigned long long)s_sum.guard,
        rd_sc_form_name((int)s_sum.form - 1));
    rd_clause_done(out, size, at, w);
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__shadow_fine_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    uint64_t why[RD_SR_WHY_N];
    for (int k = 0; k < RD_SR_WHY_N; k++) {
        why[k] = rec->shadowFineWhy[k];
    }
    char words[160];
    whyList(words, sizeof words, why);
    snprintf(out, size,
             "shadow levels: %u shadows at the scene's scale, %u passes, %u at the PS2's size%s",
             rec->shadowFineChains, rec->shadowFinePasses, rec->shadowFineRefused, words);
}

/* --------------------------------------------------------------- the cuts */

static int floorDiv2(int a)
{
    return a >= 0 ? a / 2 : -((-a + 1) / 2);
}

bool rd_sr_step_box(int srcLo, int srcHi, int srcSize, int whole, int dstSize, int repeat, int *lo,
                    int *hi)
{
    if (srcHi < srcLo) {
        return false; /* nothing in the source: the level is 0 there */
    }
    /* GS pixel x's fine texels read the source's GS pixels 2x + whole .. 2x +
     * whole + 3 at most (two taps one PS2 texel apart, each a box of one GS
     * pixel or a point with its linear neighbour, over the GS pixel's L
     * fine texels): x meets [srcLo, srcHi] only between these */
    int a = -floorDiv2(-(srcLo - whole - 3)); /* ceil((srcLo - whole - 3) / 2) */
    int b = floorDiv2(srcHi - whole);
    const int reach = 4 + (whole < 0 ? -whole : whole);
    if (repeat && (srcLo <= reach || srcHi >= srcSize - 1 - reach)) {
        a = 0; /* a wrapped tap can bring the source's far side in */
        b = dstSize - 1;
    }
    if (!repeat && srcLo <= 0) {
        a = 0; /* a tap held to the edge reads the edge's texel */
    }
    if (!repeat && srcHi >= srcSize - 1) {
        b = dstSize - 1;
    }
    *lo = a < 0 ? 0 : a;
    *hi = b > dstSize - 1 ? dstSize - 1 : b;
    return *lo <= *hi;
}

void rd_sr_tent_box(int lo, int hi, int L, int size, int *flo, int *fhi)
{
    /* B[n] sums E[n - t] over t in c - L + 1 .. c + L - 1, c = (L - 1) / 2 */
    const int c = (L - 1) / 2;
    const int a = lo + c - L + 1, b = hi + c + L - 1;
    *flo = a < 0 ? 0 : a;
    *fhi = b > size - 1 ? size - 1 : b;
}

void rd_sr_tent2_texel(const uint8_t *src, uint32_t w, uint32_t h, int lx, int ly, uint32_t x,
                       uint32_t y, uint8_t out[4])
{
    const int cx = (lx - 1) / 2, cy = (ly - 1) / 2;
    uint32_t sum[4] = {0, 0, 0, 0};
    for (int t = cy - ly + 1; t <= cy + ly - 1; t++) {
        const uint32_t wy = (uint32_t)(ly - (t > cy ? t - cy : cy - t));
        const int k = rd_sr_index((int)y - t, (int)h, 0);
        uint32_t row[4] = {0, 0, 0, 0};
        for (int s = cx - lx + 1; s <= cx + lx - 1; s++) {
            const uint32_t wx = (uint32_t)(lx - (s > cx ? s - cx : cx - s));
            const int kx = rd_sr_index((int)x - s, (int)w, 0);
            const uint8_t *p = src + ((size_t)k * w + (size_t)kx) * 4u;
            for (int ch = 0; ch < 4; ch++) {
                row[ch] += (uint32_t)p[ch] * wx;
            }
        }
        const uint32_t wsx = (uint32_t)(lx * lx);
        for (int ch = 0; ch < 4; ch++) {
            sum[ch] += (2u * row[ch] + wsx) / (2u * wsx) * wy;
        }
    }
    const uint32_t wsy = (uint32_t)(ly * ly);
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = (uint8_t)((2u * sum[ch] + wsy) / (2u * wsy));
    }
}

bool rd_sr_mask_empty(const uint8_t *mask, uint32_t mw, uint32_t mh, uint32_t sw, uint32_t sh,
                      float ax, float ay, float sx, float sy, float ox)
{
    /* the whole texels the taps touch, floor(a) .. ceil(a + 2 s) - 1, and the
     * GS pixels those texels overlap (a texel's other GS pixel may hold the
     * larger part of it) */
    const float t0x = floorf(ax), t0y = floorf(ay);
    const float t1x = ceilf(ax + 2.0f * sx), t1y = ceilf(ay + 2.0f * sy);
    if (t0x < 0.0f || t0y < 0.0f || t1x > (float)sw || t1y > (float)sh) {
        return false; /* a tap wraps or is held at the source's edge */
    }
    const int g0x = (int)floorf((t0x + ox) / sx), g0y = (int)floorf(t0y / sy);
    const int g1x = (int)floorf((t1x - 1.0e-3f + ox) / sx);
    const int g1y = (int)floorf((t1y - 1.0e-3f) / sy);
    if (g0x < 0 || g0y < 0 || g1x >= (int)mw || g1y >= (int)mh || g1x - g0x >= 4 ||
        g1y - g0y >= 4) {
        return false;
    }
    for (int y = g0y; y <= g1y; y++) {
        for (int x = g0x; x <= g1x; x++) {
            if (mask[((size_t)y * mw + (size_t)x) * 4u + 3u]) {
                return false;
            }
        }
    }
    return true;
}

void rd__shadow_fine_drawn(const RdPerfRecord *rec, char *out, size_t size)
{
    snprintf(out, size,
             "shadow levels drawn: %u texels of the %u their passes' targets hold (%.1f %%), "
             "render areas %u texels",
             rec->shadowFineTexels, rec->shadowFineArea,
             rec->shadowFineArea ? 100.0 * (double)rec->shadowFineTexels / rec->shadowFineArea
                                 : 0.0,
             rec->shadowFineRendered);
}

/* the smallest rectangle holding a and b (an empty one adds nothing) */
RhiRect rd_sr_rect_union(RhiRect a, RhiRect b)
{
    if (!a.w || !a.h) {
        return b;
    }
    if (!b.w || !b.h) {
        return a;
    }
    const int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    const int32_t ax1 = a.x + (int32_t)a.w, bx1 = b.x + (int32_t)b.w;
    const int32_t ay1 = a.y + (int32_t)a.h, by1 = b.y + (int32_t)b.h;
    const int32_t x1 = ax1 > bx1 ? ax1 : bx1, y1 = ay1 > by1 ? ay1 : by1;
    return (RhiRect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

bool rd_sr_area(RhiRect *last, bool *known, const RhiRect *box, uint32_t w, uint32_t h,
                RhiRect *area)
{
    const RhiRect all = {0, 0, w, h};
    *area = all;
    if (box && *known) {
        *area = rd_sr_rect_union(*last, *box);
        if (!area->w || !area->h) {
            return false;
        }
    }
    *known = true;
    *last = box ? *box : all;
    return true;
}
