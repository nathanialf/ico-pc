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
    "the scene's scale is above 31 on an axis"};

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

void rd_sr_step_texel(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, uint32_t mx,
                      uint32_t my, uint8_t out[4])
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
        const uint32_t t = (uint32_t)floorf(sum[ch] * 255.0f + 0.5f);
        const uint32_t o = (t * p->col[ch]) >> 7;
        out[ch] = (uint8_t)(o > 255u ? 255u : o);
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
    uint64_t chains, passes, original, why[RD_SR_WHY_N];
    uint32_t replays;
} s_sum;

void rd__shadow_fine_pop(const RdPerfRecord *rec)
{
    s_sum.chains += rec->shadowFineChains;
    s_sum.passes += rec->shadowFinePasses;
    s_sum.original += rec->shadowFineRefused;
    for (int k = 0; k < RD_SR_WHY_N; k++) {
        s_sum.why[k] += rec->shadowFineWhy[k];
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
    const int w =
        snprintf(out + *at, size - *at,
                 "; shadow levels: %llu shadows at the scene's scale, %llu at the PS2's "
                 "size%s in %u replays (%.1f passes a replay)",
                 (unsigned long long)s_sum.chains, (unsigned long long)s_sum.original, why,
                 (unsigned)s_sum.replays, (double)s_sum.passes / (double)s_sum.replays);
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
