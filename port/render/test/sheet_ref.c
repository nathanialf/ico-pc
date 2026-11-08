/* sheet_ref.c: font_sheet_ps (port/shaders/font.hlsl) on the CPU; see
 * sheet_ref.h.  Written apart from the shader from rd.h's description, with
 * the constants of shader_consts.h, so a test that compares the two
 * compares two implementations. */
#include "sheet_ref.h"
#include <math.h>
#include "shader_consts.h"

/* the HLSL twins (sheet_text.hlsli's defines only): the same-value rule of
   shader_consts.h, checked here */
#define ICO_SHEET_TEXT_C 1
#include "sheet_text.hlsli"
_Static_assert(SHEET_RX == ICO_SHEET_RX, "sheet_text.hlsli SHEET_RX");
_Static_assert(SHEET_RY == ICO_SHEET_RY, "sheet_text.hlsli SHEET_RY");
_Static_assert(SHEET_WX_0 == ICO_SHEET_WX_0, "sheet_text.hlsli SHEET_WX_0");
_Static_assert(SHEET_WX_1 == ICO_SHEET_WX_1, "sheet_text.hlsli SHEET_WX_1");
_Static_assert(SHEET_WX_2 == ICO_SHEET_WX_2, "sheet_text.hlsli SHEET_WX_2");
_Static_assert(SHEET_WX_3 == ICO_SHEET_WX_3, "sheet_text.hlsli SHEET_WX_3");
_Static_assert(SHEET_WX_4 == ICO_SHEET_WX_4, "sheet_text.hlsli SHEET_WX_4");
_Static_assert(SHEET_WX_5 == ICO_SHEET_WX_5, "sheet_text.hlsli SHEET_WX_5");
_Static_assert(SHEET_WX_6 == ICO_SHEET_WX_6, "sheet_text.hlsli SHEET_WX_6");
_Static_assert(SHEET_WY_0 == ICO_SHEET_WY_0, "sheet_text.hlsli SHEET_WY_0");
_Static_assert(SHEET_WY_1 == ICO_SHEET_WY_1, "sheet_text.hlsli SHEET_WY_1");
_Static_assert(SHEET_WY_2 == ICO_SHEET_WY_2, "sheet_text.hlsli SHEET_WY_2");
_Static_assert(SHEET_WY_3 == ICO_SHEET_WY_3, "sheet_text.hlsli SHEET_WY_3");
_Static_assert(SHEET_WY_4 == ICO_SHEET_WY_4, "sheet_text.hlsli SHEET_WY_4");
_Static_assert(ICO_SHEET_RX == 6 && ICO_SHEET_RY == 4,
               "the falloff tables have RX + 1 and RY + 1 entries");
_Static_assert(ICO_SHEET_WX_0 == 1000 && ICO_SHEET_WY_0 == 1000,
               "the falloff is 1 at the texel itself");
_Static_assert(SHEET_LEVELS == ICO_SHEET_LEVELS, "sheet_text.hlsli SHEET_LEVELS");
_Static_assert(SHEET_BAYER_ROW0 == ICO_SHEET_BAYER_ROW0, "sheet_text.hlsli SHEET_BAYER_ROW0");
_Static_assert(SHEET_BAYER_ROW1 == ICO_SHEET_BAYER_ROW1, "sheet_text.hlsli SHEET_BAYER_ROW1");
_Static_assert(SHEET_BAYER_ROW2 == ICO_SHEET_BAYER_ROW2, "sheet_text.hlsli SHEET_BAYER_ROW2");
_Static_assert(SHEET_BAYER_ROW3 == ICO_SHEET_BAYER_ROW3, "sheet_text.hlsli SHEET_BAYER_ROW3");
_Static_assert(SHEET_T_OFF == ICO_SHEET_T_OFF, "sheet_text.hlsli SHEET_T_OFF");
_Static_assert(ICO_SHEET_T_OFF == 16, "dither off is a half of thDen's 32nds");
_Static_assert(SHEET_SOLID_ABOVE == ICO_SHEET_SOLID_ABOVE, "sheet_text.hlsli SHEET_SOLID_ABOVE");
_Static_assert(SHEET_SCALE_MAX == ICO_SHEET_SCALE_MAX, "sheet_text.hlsli SHEET_SCALE_MAX");
_Static_assert(SHEET_BAKE_AX == ICO_SHEET_BAKE_AX, "sheet_text.hlsli SHEET_BAKE_AX");
_Static_assert(SHEET_BAKE_AY == ICO_SHEET_BAKE_AY, "sheet_text.hlsli SHEET_BAKE_AY");
_Static_assert(ICO_SHEET_LEVELS >= 2 && ICO_SHEET_LEVELS <= 256, "sheet levels");

/* the shader's constants as a look: the rim's falloff across (|dx|) and
   down (|dy|), per mille, the levels and the 4x4 matrix */
static const SheetLook kDefaultLook = {
    ICO_SHEET_RX,
    ICO_SHEET_RY,
    {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2, ICO_SHEET_WX_3, ICO_SHEET_WX_4, ICO_SHEET_WX_5,
     ICO_SHEET_WX_6},
    {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2, ICO_SHEET_WY_3, ICO_SHEET_WY_4},
    ICO_SHEET_LEVELS,
    4,
    ICO_SHEET_SOLID_ABOVE,
};

const SheetLook *sheetref_default_look(void)
{
    return &kDefaultLook;
}

/* the standard 4x4 Bayer matrix, [y][x] */
static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* common.hlsli gs_bayer8: the bits of x ^ y and y interleaved and reversed */
uint32_t sheetref_bayer8(uint32_t x, uint32_t y)
{
    const uint32_t a = (x ^ y) & 7u, b = y & 7u;
    return ((a & 1u) << 5) | ((b & 1u) << 4) | ((a & 2u) << 2) | ((b & 2u) << 1) | ((a & 4u) >> 1) |
           ((b & 4u) >> 2);
}

static uint32_t covAt(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) {
        return 0;
    }
    return cov[(size_t)y * w + (uint32_t)x];
}

/* the threshold's denominator: 128ths of a step under the 8x8 matrix,
   32nds otherwise (font_sheet_ps's) */
static uint32_t thDen(const SheetLook *l)
{
    return l->bayer == 8 ? 128u : 32u;
}

/* the look's level count, 2..256 */
static uint32_t levelsOf(const SheetLook *l)
{
    return l->levels < 2 ? 2u : l->levels > 256 ? 256u : (uint32_t)l->levels;
}

/* v 0..255 to the nearest of the look's levels below v + th / den of a
   step, on the 0..255 scale (0 stays 0 at every threshold: th < den) */
static uint32_t quantise(const SheetLook *l, uint32_t v, uint32_t th)
{
    const uint32_t n = levelsOf(l) - 1u, den = thDen(l);
    uint32_t q = (v * n * den + th * 255u) / (255u * den);
    q = q > n ? n : q;
    return (q * 255u + n / 2u) / n;
}

/* floor(v / s) */
static int32_t floorDiv(int32_t v, int32_t s)
{
    return v >= 0 ? v / s : -((-v + s - 1) / s);
}

/* the threshold (thDen's units) of sheet texel (x, y), or a half */
static uint32_t thresholdAt(const RdSheetStyle *s, const SheetLook *l, int32_t x, int32_t y)
{
    if (!s->dither || (l->bayer != 4 && l->bayer != 8)) {
        return thDen(l) / 2u;
    }
    if (l->bayer == 8) {
        return 2u * sheetref_bayer8((uint32_t)x, (uint32_t)y) + 1u;
    }
    return 2u * kBayer[(uint32_t)y & 3u][(uint32_t)x & 3u] + 1u;
}

/* sheet_solid_threshold: the dither's th under the look's solidAbove, a
   half (plain rounding) from it up; solidAbove 0 never */
static uint32_t solidThreshold(const SheetLook *l, uint32_t a, uint32_t th)
{
    return l->solidAbove > 0 && a >= (uint32_t)l->solidAbove ? thDen(l) / 2u : th;
}

/* the rim's weight in 64ths */
static uint32_t rimWeight(const RdSheetStyle *s)
{
    return s->rimWeight && s->rimWeight < 64 ? s->rimWeight : 64u;
}

/* the coverage of sheet texel (x, y) of a strip at
   sc texels a sheet texel: the mean of its sc x sc texels, rounded */
static uint32_t sheetCovAt(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           uint32_t sc)
{
    uint32_t sum = 0;
    for (int32_t j = 0; j < (int32_t)sc; j++) {
        for (int32_t i = 0; i < (int32_t)sc; i++) {
            sum += covAt(cov, w, h, x * (int32_t)sc + i, y * (int32_t)sc + j);
        }
    }
    return (sum + sc * sc / 2u) / (sc * sc);
}

/* the rim of sheet texel (x, y): the largest sheet coverage weighted by
   the look's falloff, rounded to 0..255 */
static uint32_t sheetRimAt(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           uint32_t sc, const SheetLook *l)
{
    const int32_t rx = l->rx < 0 ? 0 : l->rx > ICO_SHEET_RX ? ICO_SHEET_RX : l->rx;
    const int32_t ry = l->ry < 0 ? 0 : l->ry > ICO_SHEET_RY ? ICO_SHEET_RY : l->ry;
    uint32_t m = 0;
    for (int32_t dy = -ry; dy <= ry; dy++) {
        for (int32_t dx = -rx; dx <= rx; dx++) {
            const uint32_t c = sc > 1 ? sheetCovAt(cov, w, h, x + dx, y + dy, sc)
                                      : covAt(cov, w, h, x + dx, y + dy);
            const uint32_t k =
                c * (uint32_t)l->wx[dx < 0 ? -dx : dx] * (uint32_t)l->wy[dy < 0 ? -dy : dy];
            m = k > m ? k : m;
        }
    }
    return (m + 500000u) / 1000000u;
}

uint32_t sheetref_rim_look(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           const SheetLook *look)
{
    return sheetRimAt(cov, w, h, x, y, 1, look ? look : &kDefaultLook);
}

void sheetref_texel_cr(uint32_t c, uint32_t r, int32_t x, int32_t y, const RdSheetStyle *s,
                       const SheetLook *l, uint8_t *outGrey, uint8_t *outAlpha)
{
    static const RdSheetStyle kDefault = {1, 0, 0xFF, 1, 0, 1};
    s = s ? s : &kDefault;
    l = l ? l : &kDefaultLook;
    const uint32_t rw = (r * rimWeight(s) + 32u) / 64u;
    const uint32_t a = s->rimOn ? (c > rw ? c : rw) : c;
    const uint32_t th = solidThreshold(l, a, thresholdAt(s, l, x, y));
    const uint32_t t = a ? (c * 255u + a / 2u) / a : 0u;
    const uint32_t aq = quantise(l, a, th), tq = quantise(l, t, th);
    const uint32_t grey =
        ((uint32_t)s->rimLevel * (255u - tq) + (uint32_t)s->fillLevel * tq + 127u) / 255u;
    *outGrey = (uint8_t)grey;
    *outAlpha = (uint8_t)((aq * 128u + 127u) / 255u);
}

void sheetref_texel_look(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                         const RdSheetStyle *style, const SheetLook *look, uint8_t *outGrey,
                         uint8_t *outAlpha)
{
    static const RdSheetStyle kDefault = {1, 0, 0xFF, 1, 0, 1};
    const RdSheetStyle *s = style ? style : &kDefault;
    const SheetLook *l = look ? look : &kDefaultLook;
    const uint32_t c = covAt(cov, w, h, x, y);
    /* the strip texels a sheet texel: 0 is 1 */
    const uint32_t sc = s->scale > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX
                        : s->scale                     ? s->scale
                                                       : 1u;
    if (sc == 1) {
        /* the rim: the largest coverage weighted by the falloff */
        sheetref_texel_cr(c, sheetRimAt(cov, w, h, x, y, 1, l), x, y, s, l, outGrey, outAlpha);
        return;
    }
    /* the threshold: the Bayer entry's centre, or a half; above 1x the
       sheet texel's, one entry for the sc x sc strip texels in it */
    const uint32_t th = thresholdAt(s, l, floorDiv(x, (int32_t)sc), floorDiv(y, (int32_t)sc));
    /* the rim levels of the four sheet texels around the texel's centre
       ((x + 0.5) / sc - 0.5, in 2sc-ths 2x + 1 - sc), each the rim alone
       quantised at its own threshold, blended bilinearly (font.hlsl
       sheet_scaled): on the 0..255 scale and in GS units */
    const int32_t nx = 2 * x + 1 - (int32_t)sc, ny = 2 * y + 1 - (int32_t)sc;
    const int32_t qx = floorDiv(nx, 2 * (int32_t)sc), qy = floorDiv(ny, 2 * (int32_t)sc);
    const float gx = (float)(nx - 2 * (int32_t)sc * qx) / (float)(2 * sc);
    const float gy = (float)(ny - 2 * (int32_t)sc * qy) / (float)(2 * sc);
    float lq[2][2], la[2][2];
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            const uint32_t r =
                s->rimOn ? sheetRimAt(cov, w, h, qx + i, qy + j, sc, l) * rimWeight(s) : 0u;
            const uint32_t ra = s->rimOn ? (r + 32u) / 64u : 0u;
            const uint32_t lv =
                quantise(l, ra, solidThreshold(l, ra, thresholdAt(s, l, qx + i, qy + j)));
            lq[j][i] = (float)lv;
            la[j][i] = (float)((lv * 128u + 127u) / 255u);
        }
    }
    const float q0 = lq[0][0] + (lq[0][1] - lq[0][0]) * gx,
                q1 = lq[1][0] + (lq[1][1] - lq[1][0]) * gx;
    const float a0 = la[0][0] + (la[0][1] - la[0][0]) * gx,
                a1 = la[1][0] + (la[1][1] - la[1][0]) * gx;
    const float lm = q0 + (q1 - q0) * gy, am = a0 + (a1 - a0) * gy;
    /* the fill on top: the texel's own coverage */
    const uint32_t rm = (uint32_t)floorf(lm + 0.5f);
    const uint32_t a = c > rm ? c : rm;
    const uint32_t t = a ? (c * 255u + a / 2u) / a : 0u;
    const uint32_t ths = solidThreshold(l, a, th);
    const uint32_t cq = quantise(l, c, ths), tq = quantise(l, t, ths);
    const float af = (float)((cq * 128u + 127u) / 255u);
    *outGrey =
        (uint8_t)(((uint32_t)s->rimLevel * (255u - tq) + (uint32_t)s->fillLevel * tq + 127u) /
                  255u);
    *outAlpha = (uint8_t)floorf((am > af ? am : af) + 0.5f);
}

void sheetref_texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha)
{
    sheetref_texel_look(cov, w, h, x, y, style, &kDefaultLook, outGrey, outAlpha);
}

void sheetref_sample_look(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                          const RdSheetStyle *style, const SheetLook *look, uint8_t rgba[4])
{
    const float px = u * (float)w - 0.5f, py = v * (float)h - 0.5f;
    const float bx = floorf(px), by = floorf(py);
    const float fx = px - bx, fy = py - by;
    const int32_t x0 = (int32_t)bx, y0 = (int32_t)by;
    float g[2][2], al[2][2];
    for (int ty = 0; ty < 2; ty++) {
        for (int tx = 0; tx < 2; tx++) {
            uint8_t gr, a;
            sheetref_texel_look(cov, w, h, x0 + tx, y0 + ty, style, look, &gr, &a);
            g[ty][tx] = (float)gr;
            al[ty][tx] = (float)a;
        }
    }
    const float g0 = g[0][0] + (g[0][1] - g[0][0]) * fx, g1 = g[1][0] + (g[1][1] - g[1][0]) * fx;
    const float a0 = al[0][0] + (al[0][1] - al[0][0]) * fx,
                a1 = al[1][0] + (al[1][1] - al[1][0]) * fx;
    const uint8_t grey = (uint8_t)floorf(g0 + (g1 - g0) * fy + 0.5f);
    rgba[0] = rgba[1] = rgba[2] = grey;
    rgba[3] = (uint8_t)floorf(a0 + (a1 - a0) * fy + 0.5f);
}

void sheetref_sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4])
{
    sheetref_sample_look(cov, w, h, u, v, style, &kDefaultLook, rgba);
}
