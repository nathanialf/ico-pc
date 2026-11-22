/* rd_vu_zero.c: the zero-area cull's pure part (rd_vu_zero.h): the lattice
 * word of a target, the CPU's copy of the rule stage's test
 * (port/shaders/vu_tri.hlsl vu_tri_zero), the check of a record read back
 * and the reasons' words.  No device: port/render/test/rd_vu_zero_test.c
 * runs it on every host. */
#include "rd_vu_zero.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "shader_consts.h"

/* vu_common.hlsli's flags of a triangle's slot A word (VU_TF_*) */
#define TF_RULE_MASK 3u
#define TF_AT_SHIFT 2u
#define TF_WRAP 8192u
#define TF_ZERO 131072u
/* vu_common.hlsli VU_RULE_EXACT */
#define RULE_EXACT 0u

const char *rd_vu_zero_reason_name(int why)
{
    static const char *const kName[RD_VU_ZERO_REASONS] = {"off",      "mapping",   "scale",
                                                          "jitter",   "off grid",  "multisampled",
                                                          "velocity", "own entry", "trial"};
    return why >= 0 && why < RD_VU_ZERO_REASONS ? kName[why] : "?";
}

/* rd_replay.c vpExtent: the largest power of two up to the limit, the
   size itself where that is smaller */
static double extentOf(uint32_t n, uint32_t lim)
{
    uint32_t p = 16384u;
    while (p > lim && p > 1u) {
        p >>= 1;
    }
    return (double)(p >= n ? p : n);
}

/* a whole number of texels per GS pixel, 1..15: the number, else 0 */
static int wholeScale(double k)
{
    const double r = floor(k + 0.5);
    return r >= 1.0 && r <= 15.0 && r == k ? (int)r : 0;
}

/* One axis: the texel place fb of a GS position g (1/16 GS pixels) is, as
 * rd__frame_group_ex and setViewport make it (common.hlsli gs_px_to_ndc,
 * the GPU's viewport transform),
 *   fb = vpX + P / 2 + (g / 16 - c) k + 0.5 + vpo
 * with c the target's centre (origin + w / 2), k its texels per GS pixel,
 * P the viewport's size, vpX its origin floor((t - P) / 2), vpo the
 * target's centre less the viewport's (0 under the Full pixel crop, whose
 * frame centre is the viewport's).  A texel centre is fb = i + 0.5, so in
 * sixteenths U = 16 fb - 8 = k g + K is a multiple of 16 there, with
 * K = 16 (vpX + P / 2 + 0.5 + vpo) - 16 k c - 8.  The word holds K mod 16;
 * false when K is not a whole number */
static bool axisOffset(uint32_t t, uint32_t w, double origin, int k, bool crop, uint32_t lim,
                       uint32_t *off)
{
    const double P = extentOf(t ? t : 1u, lim);
    const double vpX = floor(((double)t - P) * 0.5);
    const double vpo = crop ? 0.0 : (double)t * 0.5 - (vpX + P * 0.5);
    const double c = origin + (double)w * 0.5;
    const double K = 16.0 * (vpX + P * 0.5 + 0.5 + vpo) - 16.0 * (double)k * c - 8.0;
    if (K != floor(K) || fabs(K) > 1e9) {
        return false;
    }
    const long long m = ((long long)K % 16 + 16) % 16;
    *off = (uint32_t)m;
    return true;
}

uint32_t rd_vu_zero_word(const RdVuZeroMap *m, int *why)
{
    int w = RD_VU_ZERO_MAPPING;
    uint32_t word = 0;
    if (m == NULL || !m->exact || m->noHalf || m->w == 0u || m->h == 0u || m->tw == 0u ||
        m->th == 0u) {
        w = RD_VU_ZERO_MAPPING;
    } else if (m->jitter) {
        w = RD_VU_ZERO_JITTER;
    } else if (m->offGrid) {
        w = RD_VU_ZERO_GRID;
    } else {
        const bool crop = m->cropOx > 0.0f;
        /* rd__frame_group_ex's vp.z (crop: the target's own scale) and vp.w */
        const int kx = wholeScale(crop ? (double)m->sx : (double)m->tw / (double)m->w);
        const int ky = wholeScale((double)m->th / (double)m->h);
        uint32_t ox = 0, oy = 0;
        if (kx == 0 || ky == 0) {
            w = RD_VU_ZERO_SCALE;
        } else if (!axisOffset(m->tw, m->w, (double)m->originX, kx, crop, m->extentMax, &ox) ||
                   !axisOffset(m->th, m->h, (double)m->originY, ky, false, m->extentMax, &oy)) {
            w = RD_VU_ZERO_MAPPING;
        } else {
            word = ox | (oy << 4) | ((uint32_t)kx << 8) | ((uint32_t)ky << 12) | ICO_VU_ZERO_ON;
        }
    }
    if (why != NULL) {
        *why = word ? -1 : w;
    }
    return word;
}

int32_t rd_vu_zero_ftoi4(float x)
{
    const float v = x * 16.0f;
    if (isnan(v)) {
        return INT32_MIN; /* vu_ftoi: both comparisons fail */
    }
    if (v >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (v < -2147483648.0f) {
        return INT32_MIN;
    }
    return (int32_t)v;
}

/* vu_tri.hlsl vu_zero_edge */
static unsigned edgeIn(const int32_t a[2], const int32_t b[2], const int32_t p[2], int32_t s)
{
    const int32_t ex = b[0] - a[0], ey = b[1] - a[1];
    const int32_t v = s * (ex * (p[1] - a[1]) - ey * (p[0] - a[0]));
    const int32_t ax = ex < 0 ? -ex : ex, ay = ey < 0 ? -ey : ey;
    return v >= -ICO_VU_ZERO_GROW * (ax + ay) ? 1u : 0u;
}

int rd_vu_zero_test(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy,
                    uint32_t word)
{
    const int32_t kx = (int32_t)(word & 15u), ky = (int32_t)((word >> 4) & 15u);
    const int32_t sx = (int32_t)((word >> 8) & 15u), sy = (int32_t)((word >> 12) & 15u);
    int32_t a[2] = {ax * sx + kx, ay * sy + ky};
    int32_t b[2] = {bx * sx + kx, by * sy + ky};
    int32_t c[2] = {cx * sx + kx, cy * sy + ky};
    int32_t m0[2], m1[2];
    uint32_t n[2];
    for (int i = 0; i < 2; i++) {
        int32_t lo = a[i] < b[i] ? a[i] : b[i];
        lo = c[i] < lo ? c[i] : lo;
        int32_t hi = a[i] > b[i] ? a[i] : b[i];
        hi = c[i] > hi ? c[i] : hi;
        lo -= ICO_VU_ZERO_GROW;
        hi += ICO_VU_ZERO_GROW;
        /* the shader's logical shifts of the non-negative values */
        m0[i] = (int32_t)((uint32_t)(lo + 15) >> 4);
        m1[i] = (int32_t)((uint32_t)hi >> 4);
    }
    if (m1[0] - m0[0] == -1 || m1[1] - m0[1] == -1) {
        return 1;
    }
    n[0] = (uint32_t)(m1[0] - m0[0]);
    n[1] = (uint32_t)(m1[1] - m0[1]);
    if (n[0] >= (uint32_t)ICO_VU_ZERO_SPAN || n[1] >= (uint32_t)ICO_VU_ZERO_SPAN) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        a[i] -= m0[i] * 16;
        b[i] -= m0[i] * 16;
        c[i] -= m0[i] * 16;
    }
    const int32_t area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (area == 0) {
        return 0;
    }
    const int32_t s = ((uint32_t)area >> 31) != 0u ? -1 : 1;
    unsigned hit = 0;
    for (uint32_t j = 0; j < (uint32_t)ICO_VU_ZERO_SPAN; j++) {
        for (uint32_t i = 0; i < (uint32_t)ICO_VU_ZERO_SPAN; i++) {
            const int32_t p[2] = {(int32_t)(i * 16u), (int32_t)(j * 16u)};
            const unsigned in3 = edgeIn(a, b, p, s) & edgeIn(b, c, p, s) & edgeIn(c, a, p, s);
            hit |= in3 & (n[0] >= i ? 1u : 0u) & (n[1] >= j ? 1u : 0u);
        }
    }
    return hit != 0u ? 0 : 1;
}

static float fbitsOf(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

/* a recorded vertex's GS X and Y (vu_rec_vertex: ftoi4 of plane 1's xy)
   and its GS position flag (plane 2's w, bit 1) */
static void vertexOf(const uint32_t *r1, const uint32_t *r2, uint32_t v, int32_t g[2],
                     unsigned *gsOk)
{
    g[0] = rd_vu_zero_ftoi4(fbitsOf(r1[v * 4u + 0u]));
    g[1] = rd_vu_zero_ftoi4(fbitsOf(r1[v * 4u + 1u]));
    *gsOk = (r2[v * 4u + 3u] >> 1) & 1u;
}

static unsigned inRange(const int32_t g[2])
{
    return g[0] >= 0 && g[0] <= 65535 && g[1] >= 0 && g[1] <= 65535 ? 1u : 0u;
}

void rd_vu_zero_check(const uint32_t *a0, const uint32_t *r1, const uint32_t *r2, uint32_t kicks,
                      uint32_t mode, uint32_t word, RdVuZeroCheck *out)
{
    for (uint32_t j = 0; j < kicks; j++) {
        const uint32_t f = a0[j * 4u];
        int32_t g[3][2];
        unsigned ok[3];
        for (uint32_t k = 0; k < 3u; k++) {
            vertexOf(r1, r2, j + k, g[k], &ok[k]);
        }
        /* vu_tri_rule_pre_vs: every corner drawn at its GS position */
        const unsigned at1 = mode == ICO_VU_CLIP_NONE     ? ((f & TF_WRAP) != 0u)
                             : mode == ICO_VU_CLIP_REGION ? ((f & TF_RULE_MASK) == RULE_EXACT)
                                                          : (((f >> TF_AT_SHIFT) & 3u) == 1u);
        const unsigned elig = (word != 0u) & ok[0] & ok[1] & ok[2] & at1 & inRange(g[0]) &
                              inRange(g[1]) & inRange(g[2]);
        const unsigned want =
            elig && rd_vu_zero_test(g[0][0], g[0][1], g[1][0], g[1][1], g[2][0], g[2][1], word);
        const unsigned got = (f & TF_ZERO) != 0u;
        out->slots++;
        out->eligible += elig;
        out->culled += got;
        if (got != want) {
            if (out->mismatches++ == 0u) {
                snprintf(out->first, sizeof(out->first),
                         "triangle slot %u: the GPU %s it, the CPU %s it (corners %d,%d %d,%d "
                         "%d,%d in 1/16 GS pixels, word 0x%05X, flags 0x%05X)",
                         (unsigned)j, got ? "culled" : "kept", want ? "culls" : "keeps",
                         (int)g[0][0], (int)g[0][1], (int)g[1][0], (int)g[1][1], (int)g[2][0],
                         (int)g[2][1], (unsigned)word, (unsigned)f);
            }
        }
    }
}
