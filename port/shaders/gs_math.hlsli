// gs_math.hlsli: the GS integer arithmetic, scalar only.
//
// This file is valid HLSL and valid C. port/shaders/test/gs_math_test.c
// includes it through hlsl_shim.h and checks every function against the
// formulas of port/test/gs_blend_test.c; common.hlsli wraps the functions in
// vector forms. Keep it to scalar int/uint/float, min/max/clamp, ?: and
// constants so both compilers accept it.
//
// Conventions:
//   colour and alpha are 0..255 integers, alpha 0x80 = 1.0;
//   texture function  min((tex * col) >> 7, 255);
//   blend             ((A - B) * C >> 7) + D, arithmetic shift (toward -inf).
#ifndef ICO_GS_MATH_HLSLI
#define ICO_GS_MATH_HLSLI

#ifdef ICO_GS_MATH_C
#define GS_FN static inline
#define GS_BRANCH
#else
#define GS_FN
// an if kept a branch (SPIR-V DontFlatten): a select would run both sides.
// gs_wrap_repeat's condition is on a texture's size: the same for every
// pixel of a draw, except on the mip paths, which take the size at the
// pixel's level (common.hlsli gs_sample_lod_nearest, gs_texa_level). Every
// level of a 2^TW x 2^TH texture is a power of two; a replacement chain
// whose size is not can take both sides within a wave, the value right
// either way.
#define GS_BRANCH [branch]
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

// REPEAT addressing: the residue of c modulo n in 0..n-1 (n >= 1), what
// ((c % n) + n) % n gives (% keeps the dividend's sign, OpSRem and C99
// alike; |c % n| < n, so the sum is in 1..2n-1 and the second % is exact).
// For n a power of two 2^k, c & (n - 1) is the same value for every int c:
// in two's complement the low k bits of c are c - 2^k * floor(c / 2^k),
// the residue in 0..n-1, negative c included (n == 1 gives 0 both ways).
// The test (n & (n - 1)) == 0 holds exactly for the powers of two among
// n >= 1. A GS texture is 2^TW x 2^TH, so the double modulo, an emulated
// integer division twice on most GPUs, runs only for a size that is not
// a power of two (a scaled target at a fractional scale, a replacement
// texture). gs_math_test checks the two forms equal over every n up to
// 4096 and the powers of two to 2^30, c across several periods each way
// and at the int range's ends.
GS_FN int gs_wrap_repeat(int c, int n)
{
    const int m = n - 1;
    int r;
    GS_BRANCH if ((n & m) == 0) {
        r = c & m;
    } else {
        r = ((c % n) + n) % n;
    }
    return r;
}

// The float quotient gs_div_trunc starts from: a / d correctly rounded to
// float, then the divide. gs_math_test defines it first to move both by
// more than any API allows.
#ifndef GS_DIV_QUOT
#define GS_DIV_QUOT(a, d) ((float)(a) / (float)(d))
#endif

// n / d truncated toward zero, what OpSDiv (and C99's /) gives, without
// the integer division the GPU emulates, for d in 1..2^24 and
// |n| < d * 2^21 (the operands fx_sprite_ps gives it, there).
//
// a = |n| (two's complement, in uint: 2^31 for INT_MIN), Q = a / d, f the
// float quotient; the sign is applied the same way, without a branch.
// float(d) is exact (d <= 2^24). float(a) is within one ulp of a (Vulkan,
// D3D and Metal convert correctly rounded, half an ulp), so within
// 2^-23 * a, which divided by d is 2^-23 * Q < 0.25 for Q < 2^21. The
// divide is taken to be within 2.5 ulp of float(a) / float(d) (Vulkan's
// OpFDiv precision; the proof assumes no worse of the other APIs' divides),
// and below 2^21 an ulp is at most 2^-3, so 0.3125 at most.
// |f - Q| < 0.5625 < 1, so q0 = trunc(f) (f >= 0, so the floor) is
// floor(Q) - 1, floor(Q) or floor(Q) + 1, and r = a - q0 * d lies in
// [-d, 2d): one step back when r < 0, one on when r >= d, gives floor(Q)
// exactly. The product and the difference are taken modulo 2^32 (uint);
// the true r is below 2^25 in magnitude, so int(r) is it. The sign
// last: OpSDiv truncates toward zero, sign(n) * floor(|n| / d).
//
// fx_sprite_ps's UV (fx_sprite.hlsl): n = (X - x0) * (u1 - u0), d =
// x1 - x0. d >= 1 wherever the division runs (a pixel past the coverage
// test has x0 <= ox + gb.x < x1); d <= 65535 + 8 and |u1 - u0| <= 65543
// (staticBlur.c's sprites take 16-bit corners and UVs, rd_post.c's
// reduction at most 2560 * 16 + 8, the mirror moves U by -8). X - x0 is
// the coverage's ox + gb.x - x0, in 0..d-1, plus gp.x - gb.x, the pixel's
// centre against its block's start, in -8..16 at every scale >= 1 (the
// target's scale is never below 1, rd_present.c rd__apply_display): X - x0
// lies in about -8..d + 15, float rounding moving it by less than a pixel.
// So |n| <= (d + 30) * 65543 with room to spare, which is below d * 2^21
// for every d >= 1 (at d = 1, 2,031,833 < 2,097,152);
// above d = 1024 every int n is (2^31 / 1025 < 2^21), the product's
// int32 wrap included, where OpSDiv divided the wrapped value too. V the
// same with y. gs_math_test sweeps d over 1..2^17 and the quotient over
// its whole range near every multiple, with the quotient and float(a)
// moved by three ulps and one either way.
GS_FN int gs_div_trunc(int n, int d)
{
    const int sg = n >> 31; // arithmetic: -1 for n < 0, else 0
    const uint a = ((uint)n ^ (uint)sg) - (uint)sg;
    const int q0 = (int)(GS_DIV_QUOT(a, d));
    const int r = (int)(a - (uint)q0 * (uint)d);
    const int q = q0 - (r < 0 ? 1 : 0) + (r >= d ? 1 : 0);
    return (q ^ sg) - sg;
}

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

// What the GS stores in channel ch (0 R, 1 G, 2 B, 3 A) of a pixel for a
// fragment with colour channel cs and alpha as (after the texture function
// and the alpha test), over a destination whose channel is cd and alpha ad
// (port/render/rd_dst_read.c: the destination-read blend):
//   wmask   the channels written, bit ch (FRAME.FBMSK by whole channels):
//           a masked channel keeps cd;
//   alpha   never blended: As, with its MSB set under FBA;
//   PABE    an As whose MSB is clear writes Cs unblended;
//   colour  the ALPHA register's ((A - B) * C >> 7) + D, C = As, Ad or FIX
//           (0x80 = 1.0, above it the GS overshoots), clamped to 0..255 by
//           COLCLAMP 1 or wrapped to 8 bits by COLCLAMP 0 (gs_blend_reg_ch).
GS_FN uint gs_dst_channel(uint ch, uint cs, uint cd, uint as, uint ad, uint reg, uint fix,
                          uint clampMode, bool fba, bool pabe, uint wmask)
{
    uint o = cd;
    if (((wmask >> ch) & 1u) == 0u) {
        o = cd;
    } else if (ch == 3u) {
        o = (fba ? (as | 0x80u) : as) & 255u;
    } else if (pabe && (as & 0x80u) == 0u) {
        o = cs;
    } else {
        o = (uint)gs_blend_reg_ch(reg, (int)cs, (int)cd, (int)as, (int)ad, (int)fix, clampMode);
    }
    return o;
}

// GS Z to depth, increasing with Z (GS GEQUAL is the depth test GEQUAL,
// GREATER is GREATER): depth = z * scale. scale is 1 / 2^24 for PSMZ24, 1 /
// 2^16 for PSMZ16 (exact for every z: below 2^24, a float holds it) and, for
// PSMZ32 (every ZBUF the game writes; FrameCB g_z.x per target), 1 /
// 2^32 on a 24-bit depth buffer (D24S8: 2^8 GS units a step, the most it
// holds over the range) or GS_ZSCALE_32F = 1 / 2^33 on a float one: there
// z * 2^-33 in [0, 0.5) is z as a float scaled, exact below 2^24 and to 1
// part in 2^24 above (the game's 3D Z is about 2^24: 1 or 2 GS units a
// step), and the top 2^16 values (the UI's 0xFFFFFF9B, 0xFFFFFFFF) are
// exact in [1 - 2^-8, 1), one float step each.  (Increasing with Z, not
// 1 - z * scale: at z near 2^24 that put every depth near 1, where a float
// steps 2^-24: 256 GS units, so layers within that of each other tied, and
// the Queen's mist and hair behind her face passed the mirage's GEQUAL.)
// Z above the format's largest clamps to it.
#define GS_ZSCALE_32F (1.0 / 8589934592.0)
GS_FN float gs_z_to_depth(uint z, float scale)
{
    if (scale < 1.5e-10) {
        // PSMZ32 on a float depth buffer (GS_ZSCALE_32F)
        if (z >= 0xFFFF0000u) {
            return (1.0 - 1.0 / 256.0) + (float)(z - 0xFFFF0000u) * (1.0 / 16777216.0);
        }
        return (float)z * scale;
    }
    uint zmax = scale < 1.0e-9 ? 0xFFFFFFFFu : (uint)(1.0 / scale) - 1u;
    return (float)min(z, zmax) * scale;
}

#endif
