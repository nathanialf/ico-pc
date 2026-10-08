// sheet_text.hlsli: the sheet texel of font_sheet_ps (font.hlsl; v0.4.2,
// package F-A).  rd.h rd_CreateTextureSheet says what a sheet texture is
// and what the shader makes of it; port/render/test/sheet_ref.c is the
// same arithmetic on the CPU (the tests' oracle).
//
// The constants are mirrored as ICO_SHEET_* in shader_consts.h, which C
// reads; the two must hold the same values (sheet_ref.c includes this file
// with ICO_SHEET_TEXT_C defined, which leaves the defines alone, and
// static-asserts each pair).  Every step is integer arithmetic, so the GPU
// and the CPU agree exactly on the texels; only the bilinear weights are
// float.
#ifndef ICO_SHEET_TEXT_HLSLI
#define ICO_SHEET_TEXT_HLSLI

// The rim: a weighted dilation of the coverage, the largest of
// c(p + d) * WX[|dx|] * WY[|dy|] (per mille each) within SHEET_RX texels
// across and SHEET_RY texels down (a sheet texel is one x unit wide and one
// field line tall), so the dark halo fades out with the distance from the
// letters.  The reach and the falloff are the sheets' survey
// (port/ui/menu_font.c, above kSheetInk): the English sheets' rim alpha.
#define SHEET_RX 6
#define SHEET_RY 4
#define SHEET_WX_0 1000
#define SHEET_WX_1 760
#define SHEET_WX_2 610
#define SHEET_WX_3 540
#define SHEET_WX_4 500
#define SHEET_WX_5 390
#define SHEET_WX_6 330
#define SHEET_WY_0 1000
#define SHEET_WY_1 770
#define SHEET_WY_2 440
#define SHEET_WY_3 360
#define SHEET_WY_4 270
// The levels the opacity and the rim-to-fill mix are quantised to (the
// sheets' 16 colours hold three antialiasing steps between none and full).
#define SHEET_LEVELS 8
// The 4x4 Bayer matrix, one row a constant, the texel at x & 3 in nibble
// x (row 0 is 0 8 2 10, row 1 12 4 14 6, row 2 3 11 1 9, row 3 15 7 13 5).
#define SHEET_BAYER_ROW0 0xA280
#define SHEET_BAYER_ROW1 0x6E4C
#define SHEET_BAYER_ROW2 0x91B3
#define SHEET_BAYER_ROW3 0x5D7F
// The quantiser's threshold, in 32nds: 2 * bayer + 1 dithered (1..31),
// SHEET_T_OFF (16, a half: rounding) with dither off.
#define SHEET_T_OFF 16
// v0.4.2 (package F-G): the largest scale of a strip (texels a sheet
// texel; shader_consts.h ICO_SHEET_SCALE_MAX).  Above 1 the rim comes
// precomputed in the texture's bottom half (rd.h rd_SheetRim).
#define SHEET_SCALE_MAX 4
// The grid of coverage texels the four sheet texels of a bilinear sample
// are rebuilt from.
#define SHEET_GW (2 + 2 * SHEET_RX)
#define SHEET_GH (2 + 2 * SHEET_RY)

#ifndef ICO_SHEET_TEXT_C

// The falloff across at |dx| = i (0..SHEET_RX) and down at |dy| = i
// (0..SHEET_RY), per mille; constant under [unroll].
uint sheet_wx(int i)
{
    return i == 0   ? (uint)SHEET_WX_0
           : i == 1 ? (uint)SHEET_WX_1
           : i == 2 ? (uint)SHEET_WX_2
           : i == 3 ? (uint)SHEET_WX_3
           : i == 4 ? (uint)SHEET_WX_4
           : i == 5 ? (uint)SHEET_WX_5
                    : (uint)SHEET_WX_6;
}
uint sheet_wy(int i)
{
    return i == 0   ? (uint)SHEET_WY_0
           : i == 1 ? (uint)SHEET_WY_1
           : i == 2 ? (uint)SHEET_WY_2
           : i == 3 ? (uint)SHEET_WY_3
                    : (uint)SHEET_WY_4;
}

// The rim from the largest weighted coverage m (c * wx * wy, 0..255e6):
// 0..255, rounded.
uint sheet_rim(uint m)
{
    return (m + 500000u) / 1000000u;
}

// The threshold (in 32nds) at sheet texel p; dither 0: SHEET_T_OFF.
uint sheet_threshold(int2 p, uint dither)
{
    if (dither == 0u) {
        return SHEET_T_OFF;
    }
    const uint x = ((uint)p.x) & 3u, y = ((uint)p.y) & 3u;
    const uint row = y == 0u   ? (uint)SHEET_BAYER_ROW0
                     : y == 1u ? (uint)SHEET_BAYER_ROW1
                     : y == 2u ? (uint)SHEET_BAYER_ROW2
                               : (uint)SHEET_BAYER_ROW3;
    return 2u * ((row >> (4u * x)) & 15u) + 1u;
}

// v 0..255 quantised to SHEET_LEVELS levels against threshold th (32nds),
// returned on the 0..255 scale (0, 64, 128, 191, 255 for 5 levels; 8 since v0.4.2's geometry fit).
uint sheet_quantise(uint v, uint th)
{
    const uint n = (uint)SHEET_LEVELS - 1u;
    const uint q = (v * n * 32u + th * 255u) / (255u * 32u);
    return (min(q, n) * 255u + n / 2u) / n;
}

// The sheet texel from its coverage c and rim r (both 0..255) under the
// style (the rim's weight 0..64, rimLevel, fillLevel, dither) at threshold
// th: x grey 0..255, y alpha in GS units (0x80 full).
uint2 sheet_texel(uint c, uint r, uint4 style, uint th)
{
    const uint a = style.x != 0u ? max(c, (r * style.x + 32u) / 64u) : c;
    const uint t = a != 0u ? (c * 255u + a / 2u) / a : 0u;
    const uint aq = sheet_quantise(a, th), tq = sheet_quantise(t, th);
    const uint grey = (style.y * (255u - tq) + style.z * tq + 127u) / 255u;
    return uint2(grey, (aq * 128u + 127u) / 255u);
}

#endif // ICO_SHEET_TEXT_C
#endif // ICO_SHEET_TEXT_HLSLI
