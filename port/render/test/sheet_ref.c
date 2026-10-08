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
_Static_assert(ICO_SHEET_LEVELS >= 2 && ICO_SHEET_LEVELS <= 256, "sheet levels");

/* the rim's falloff across (|dx|) and down (|dy|), per mille */
static const uint32_t kWx[ICO_SHEET_RX + 1] = {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2,
                                               ICO_SHEET_WX_3, ICO_SHEET_WX_4, ICO_SHEET_WX_5,
                                               ICO_SHEET_WX_6};
static const uint32_t kWy[ICO_SHEET_RY + 1] = {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2,
                                               ICO_SHEET_WY_3, ICO_SHEET_WY_4};

/* the standard 4x4 Bayer matrix, [y][x] */
static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static uint32_t covAt(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) {
        return 0;
    }
    return cov[(size_t)y * w + (uint32_t)x];
}

/* v 0..255 to the nearest of ICO_SHEET_LEVELS levels below v + th/32 of a
   step, on the 0..255 scale */
static uint32_t quantise(uint32_t v, uint32_t th32)
{
    const uint32_t n = ICO_SHEET_LEVELS - 1;
    uint32_t q = (v * n * 32u + th32 * 255u) / (255u * 32u);
    q = q > n ? n : q;
    return (q * 255u + n / 2u) / n;
}

void sheetref_Texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha)
{
    static const RdSheetStyle kDefault = {1, 0, 0xFF, 1};
    const RdSheetStyle *s = style ? style : &kDefault;
    const uint32_t c = covAt(cov, w, h, x, y);
    /* the rim: the largest coverage weighted by the falloff, rounded to 0..255 */
    uint32_t m = 0;
    for (int32_t dy = -ICO_SHEET_RY; dy <= ICO_SHEET_RY; dy++) {
        for (int32_t dx = -ICO_SHEET_RX; dx <= ICO_SHEET_RX; dx++) {
            const uint32_t k =
                covAt(cov, w, h, x + dx, y + dy) * kWx[dx < 0 ? -dx : dx] * kWy[dy < 0 ? -dy : dy];
            m = k > m ? k : m;
        }
    }
    const uint32_t r = (m + 500000u) / 1000000u;
    const uint32_t a = s->rimOn ? (c > r ? c : r) : c;
    const uint32_t t = a ? (c * 255u + a / 2u) / a : 0u;
    /* the threshold in 32nds: the Bayer entry's centre, or a half */
    const uint32_t th = s->dither ? 2u * kBayer[(uint32_t)y & 3u][(uint32_t)x & 3u] + 1u : 16u;
    const uint32_t aq = quantise(a, th), tq = quantise(t, th);
    const uint32_t grey =
        ((uint32_t)s->rimLevel * (255u - tq) + (uint32_t)s->fillLevel * tq + 127u) / 255u;
    *outGrey = (uint8_t)grey;
    *outAlpha = (uint8_t)((aq * 128u + 127u) / 255u);
}

void sheetref_Sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4])
{
    const float px = u * (float)w - 0.5f, py = v * (float)h - 0.5f;
    const float bx = floorf(px), by = floorf(py);
    const float fx = px - bx, fy = py - by;
    const int32_t x0 = (int32_t)bx, y0 = (int32_t)by;
    float g[2][2], al[2][2];
    for (int ty = 0; ty < 2; ty++) {
        for (int tx = 0; tx < 2; tx++) {
            uint8_t gr, a;
            sheetref_Texel(cov, w, h, x0 + tx, y0 + ty, style, &gr, &a);
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
