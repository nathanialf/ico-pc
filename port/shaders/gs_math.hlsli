// gs_math.hlsli: the GS integer arithmetic, scalar only.
//
// This file is valid HLSL and valid C. port/shaders/test/gs_math_test.c
// includes it through hlsl_shim.h and checks every function against the
// formulas of port/test/gs_blend_test.c; common.hlsli wraps the functions in
// vector forms. Keep it to scalar int/uint/float, min/max/clamp, ?: and
// constants so both compilers accept it.
//
// Conventions (docs/port/RENDER_API.md section 3):
//   colour and alpha are 0..255 integers, alpha 0x80 = 1.0;
//   texture function  min((tex * col) >> 7, 255);
//   blend             ((A - B) * C >> 7) + D, arithmetic shift (toward -inf).
#ifndef ICO_GS_MATH_HLSLI
#define ICO_GS_MATH_HLSLI

#ifdef ICO_GS_MATH_C
#define GS_FN static inline
#else
#define GS_FN
#endif

// RdAlphaTest (port/render/rd_state.h)
#define ATST_NEVER 0
#define ATST_ALWAYS 1
#define ATST_LESS 2
#define ATST_LEQUAL 3
#define ATST_EQUAL 4
#define ATST_GEQUAL 5
#define ATST_GREATER 6
#define ATST_NOTEQUAL 7

// RdTexA
#define TEXA_80_80 0
#define TEXA_7F_81_AEM 1
#define TEXA_80_80_AEM 2

// Source texel format for TEXA expansion: how the baked RGBA8 texel's alpha
// byte is to be read.
#define TEXFMT_RGBA32 0 // alpha is the texel's own byte; TEXA ignored
#define TEXFMT_RGB24 1  // no alpha in the source: alpha = TA0 (AEM: 0 when RGB is 0)
#define TEXFMT_RGBA16 2 // alpha byte is the 1-bit A (0 or 1): TA1 when set, else TA0

// Texture function MODULATE, one channel: min((tex * col) >> 7, 255).
GS_FN uint gs_tfx_mod(uint tex, uint col)
{
    return min((tex * col) >> 7, 255u);
}

// Alpha of a PSMCT24 or PSMCT16 texel under TEXA. mode is an RdTexA value;
// fmt TEXFMT_RGB24 or TEXFMT_RGBA16; abit is the 16-bit format's A bit.
GS_FN uint gs_texa_alpha(uint r, uint g, uint b, uint abit, uint mode, uint fmt)
{
    uint ta0 = 0x80u;
    uint ta1 = 0x80u;
    uint aem = 0u;
    if (mode == TEXA_7F_81_AEM) {
        ta0 = 0x7Fu;
        ta1 = 0x81u;
        aem = 1u;
    } else if (mode == TEXA_80_80_AEM) {
        aem = 1u;
    }
    uint a = ta0;
    if (fmt == TEXFMT_RGBA16 && abit != 0u) {
        a = ta1;
    }
    if (aem != 0u && (r | g | b) == 0u) {
        a = 0u;
    }
    return a;
}

// Alpha test compare for an RdAlphaTest value. True when the fragment passes.
GS_FN bool gs_alpha_pass(uint atst, uint aref, uint a)
{
    bool r = true;
    if (atst == ATST_NEVER) {
        r = false;
    } else if (atst == ATST_LESS) {
        r = a < aref;
    } else if (atst == ATST_LEQUAL) {
        r = a <= aref;
    } else if (atst == ATST_EQUAL) {
        r = a == aref;
    } else if (atst == ATST_GEQUAL) {
        r = a >= aref;
    } else if (atst == ATST_GREATER) {
        r = a > aref;
    } else if (atst == ATST_NOTEQUAL) {
        r = a != aref;
    }
    return r;
}

// One term of the ALPHA register: 0 = Cs, 1 = Cd, 2 = zero.
GS_FN int gs_blend_term(uint sel, int cs, int cd)
{
    return sel == 0u ? cs : (sel == 1u ? cd : 0);
}

// The factor C: 0 = As, 1 = Ad, 2 = FIX.
GS_FN int gs_blend_factor(uint sel, int as, int ad, int fix)
{
    return sel == 0u ? as : (sel == 1u ? ad : fix);
}

// ((A - B) * C >> 7) + D for one channel, clamped to 0..255 when clampMode
// is non-zero (COLCLAMP 1), wrapped to 8 bits otherwise.
GS_FN int gs_blend_ch(int a, int b, int c, int d, uint clampMode)
{
    int v = (((a - b) * c) >> 7) + d;
    return clampMode != 0u ? clamp(v, 0, 255) : (v & 255);
}

// The full ALPHA register form for one colour channel. reg is
// A | B << 2 | C << 4 | D << 6 as the GS holds it.
GS_FN int gs_blend_reg_ch(uint reg, int cs, int cd, int as, int ad, int fix, uint clampMode)
{
    int a = gs_blend_term(reg & 3u, cs, cd);
    int b = gs_blend_term((reg >> 2) & 3u, cs, cd);
    int c = gs_blend_factor((reg >> 4) & 3u, as, ad, fix);
    int d = gs_blend_term((reg >> 6) & 3u, cs, cd);
    return gs_blend_ch(a, b, c, d, clampMode);
}

// GS Z to reversed-Z depth: 1 - z * scale. scale is 1 / 2^24 for PSMZ24
// values (0xFFFFFF, the game's far, becomes about 0), 1 / 2^32 for PSMZ32.
// Exact for integers below 2^24 at scale 2^-24.
GS_FN float gs_z_to_depth(uint z, float scale)
{
    return clamp(1.0f - (float)z * scale, 0.0f, 1.0f);
}

#endif
