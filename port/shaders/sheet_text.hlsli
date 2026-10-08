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

// The rim: the largest coverage within SHEET_RX texels across and SHEET_RY
// texels down (a sheet texel is one x unit wide and one field line tall).
// The values are the sheets' survey (port/ui/menu_font.c, above kSheetInk).
#define SHEET_RX 4
#define SHEET_RY 3
// The levels the opacity and the rim-to-fill mix are quantised to (the
// sheets' 16 colours hold three antialiasing steps between none and full).
#define SHEET_LEVELS 5
// The 4x4 Bayer matrix, one row a constant, the texel at x & 3 in nibble
// x (row 0 is 0 8 2 10, row 1 12 4 14 6, row 2 3 11 1 9, row 3 15 7 13 5).
#define SHEET_BAYER_ROW0 0xA280
#define SHEET_BAYER_ROW1 0x6E4C
#define SHEET_BAYER_ROW2 0x91B3
#define SHEET_BAYER_ROW3 0x5D7F
// The quantiser's threshold, in 32nds: 2 * bayer + 1 dithered (1..31),
// SHEET_T_OFF (16, a half: rounding) with dither off.
#define SHEET_T_OFF 16
// The grid of coverage texels the four sheet texels of a bilinear sample
// are rebuilt from.
#define SHEET_GW (2 + 2 * SHEET_RX)
#define SHEET_GH (2 + 2 * SHEET_RY)

#ifndef ICO_SHEET_TEXT_C

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
// returned on the 0..255 scale (0, 64, 128, 191, 255 for 5 levels).
uint sheet_quantise(uint v, uint th)
{
    const uint n = (uint)SHEET_LEVELS - 1u;
    const uint q = (v * n * 32u + th * 255u) / (255u * 32u);
    return (min(q, n) * 255u + n / 2u) / n;
}

// The sheet texel from its coverage c and rim r (both 0..255) under the
// style (rimOn, rimLevel, fillLevel, dither) at threshold th: x grey 0..255,
// y alpha in GS units (0x80 full).
uint2 sheet_texel(uint c, uint r, uint4 style, uint th)
{
    const uint a = style.x != 0u ? max(c, r) : c;
    const uint t = a != 0u ? (c * 255u + a / 2u) / a : 0u;
    const uint aq = sheet_quantise(a, th), tq = sheet_quantise(t, th);
    const uint grey = (style.y * (255u - tq) + style.z * tq + 127u) / 255u;
    return uint2(grey, (aq * 128u + 127u) / 255u);
}

#endif // ICO_SHEET_TEXT_C
#endif // ICO_SHEET_TEXT_HLSLI
