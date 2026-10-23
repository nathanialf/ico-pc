// common.hlsli: what every ICO shader shares. The byte layouts below are
// mirrored by port/shaders/shader_consts.h (C); change both together.
//
// Binding scheme: the register space is the RHI bind group, the register
// number is the RHI slot. DXC is run with -fvk-b-shift 0, -fvk-t-shift 16,
// -fvk-s-shift 32 so Vulkan bindings come out as slot + 0 / 16 / 32.
//   group 0  b0            FrameCB, once per frame
//   group 1  b1            DrawCB, per draw (or per post pass)
//            b2, b3        the VU programs' VuCB and VuBoneCB (vu_common.hlsli)
//            t0            the VU vertex stream (vu_common.hlsli)
//   group 2  t1..t4, s1..s4 textures and samplers
// Clip space is D3D: x, y in -1..1 with +y up, depth 0..1, reversed-Z
// (near = 1, far = 0, depth test GEQUAL).
#ifndef ICO_COMMON_HLSLI
#define ICO_COMMON_HLSLI

#include "gs_math.hlsli"

// ------------------------------------------------------------- Above 1x
// The resampling kernels of a scaled target (docs/port/RENDER.md "Above
// 1x"). This block is valid HLSL and valid C: shaders_pixel_test and the
// CPU reference (port/render/test/above1x_ref.c) include it through
// hlsl_shim.h (ICO_GS_MATH_C) and check it; everything after it is HLSL
// only. Integers throughout, so a CPU model gives the GPU's bytes.
//
// One axis at a time, by the step: source texels per destination texel,
// in 1/32 (step32). The kernel is centred on the GS's own sample point,
// c16: the GS's U (or V) in the source's 12.4 texels, texel i spanning
// [16 i, 16 i + 16) (the GS's bilinear blends the texels around c16 - 8,
// its nearest takes the one c16 falls in):
//   GS_RS_BOX      step > 2 texels (step32 > GS_RS_BOX_MIN32): an area box
//                  of width step32 / 32 texels on [lo32, lo32 + step32),
//                  lo32 = 2 c16 - step32 / 2, each texel weighted by its
//                  overlap in 1/32 texel (gs_box_w); the weights sum to
//                  step32. Nearest and bilinear alike.
//   GS_RS_BSPLINE  step < 0.5 texel (step32 < GS_RS_BSPLINE_MAX32) with the
//                  bilinear filter: the cubic B-spline (B = 1, C = 0: no
//                  overshoot, so an alpha mask stays within its values),
//                  four taps from texel ((c16 - 8) >> 4) - 1, weights from
//                  the GS's 4-bit fraction (c16 - 8) & 15 (gs_bspline_w),
//                  summing to GS_RS_BSPLINE_DIV.
//   GS_RS_GS       otherwise: the GS's own taps on this axis (bilinear: two
//                  taps weighted 16 - f and f; nearest: one tap).
// A draw takes this path only under a flag its C side sets when a scaled
// target is involved and an axis is BOX or BSPLINE; at scale 1 nothing
// here runs, and the PS2's own decimations (the 4:1 and 3.5:1 chain steps)
// stay the GS's there.
//
// Two entry points:
//   Load-based (fx_sprite_ps): gs_resample_axis gives an axis's taps
//     (GsAxis: first texel, count, and gs_resample_weight /
//     gs_resample_div); the caller fetches each tap with its own
//     addressing (fx_texel: CLAMP or REPEAT on the TEX0 size, the backing
//     texture's clamp, TEXA before filtering) and combines them
//     separably: per row R = sum(wx t), R' = (16 R + Dx / 2) / Dx, then
//     t = (sum(wy R') + 8 Dy) / (16 Dy) (the row's four extra bits keep
//     the sums within 32 bits: R' <= 4080, sum(wy R') <= 24576 * 4080).
//   Sampler-based (gs_bspline_bilinear4): the B-spline in both axes as four
//     hardware bilinear taps (each axis's four weights folded into two
//     taps placed between texels: g0 = w0 + w1 at texel i0 - 1 + w1 / g0,
//     g1 = w2 + w3 at texel i0 + 1 + w3 / g1), for a sampler that
//     reproduces the addressing; the filter's own 8-bit weights make it
//     inexact (within 2 LSB of the Load form).
// gs_bayer8 is the 8x8 ordered-dither threshold 0..63 of a texel.
#define GS_RS_GS 0
#define GS_RS_BOX 1
#define GS_RS_BSPLINE 2
#define GS_RS_BOX_MIN32 64      // above 2 texels a destination texel: the box
#define GS_RS_BSPLINE_MAX32 16  // below half a texel: the B-spline
#define GS_RS_BOX_MAX32 1024    // the box's widest: 32 texels
// The most taps an axis has (gs_rs_count): 33, a box of 32 texels starting
// part-way into a texel; the Load-based loops clamp the count to it, so a
// count the GPU computed wrong cannot run a loop past it
#define GS_RS_TAPS_MAX (GS_RS_BOX_MAX32 / 32 + 1)
#define GS_RS_BSPLINE_DIV 24576 // 6 * 16^3: the B-spline's weights' sum

// The 8x8 Bayer matrix at (x, y) (only the low three bits of each count):
// the bits of x ^ y and y interleaved and reversed, 0..63, each value once
// in every 8x8 block; row 0 is 0 32 8 40 2 34 10 42.
GS_FN uint gs_bayer8(uint x, uint y)
{
    const uint a = (x ^ y) & 7u;
    const uint b = y & 7u;
    return ((a & 1u) << 5) | ((b & 1u) << 4) | ((a & 2u) << 2) | ((b & 2u) << 1) |
           ((a & 4u) >> 1) | ((b & 4u) >> 2);
}

// The cubic B-spline's weight of tap k (0..3: texels i0 - 1 .. i0 + 2) at
// the GS's 4-bit fraction f, in 1/24576 (6 * 16^3; t = f / 16):
//   (1 - t)^3 / 6, (3 t^3 - 6 t^2 + 4) / 6, (-3 t^3 + 3 t^2 + 3 t + 1) / 6,
//   t^3 / 6, exact in these units.
GS_FN int gs_bspline_w(uint f, uint k)
{
    const int t = (int)(f & 15u);
    const int t2 = t * t;
    const int t3 = t2 * t;
    const int u = 16 - t;
    return k == 0u ? u * u * u
           : k == 1u ? 3 * t3 - 96 * t2 + 16384
           : k == 2u ? -3 * t3 + 48 * t2 + 768 * t + 4096
           : t3;
}

// The overlap of texel i ([32 i, 32 i + 32) in 1/32 texel) with the box
// [lo32, hi32), in 1/32 texel (0 outside it).
GS_FN int gs_box_w(int lo32, int hi32, int i)
{
    return max(0, min(hi32, 32 * i + 32) - max(lo32, 32 * i));
}

// The kernel of an axis (GS_RS_*) for step32 and the filter.
GS_FN int gs_rs_kind(int step32, uint bilinear)
{
    return step32 > GS_RS_BOX_MIN32 ? GS_RS_BOX
           : (step32 > 0 && step32 < GS_RS_BSPLINE_MAX32 && bilinear != 0u) ? GS_RS_BSPLINE
           : GS_RS_GS;
}

// The box's left edge in 1/32 texel: centred on c16, step32 at most
// GS_RS_BOX_MAX32.
GS_FN int gs_rs_box_lo(int c16, int step32)
{
    return 2 * c16 - (min(step32, GS_RS_BOX_MAX32) >> 1);
}

// The first tap of an axis (a texel index, before addressing).
GS_FN int gs_rs_first(int kind, int c16, int step32, uint bilinear)
{
    return kind == GS_RS_BOX ? gs_rs_box_lo(c16, step32) >> 5
           : kind == GS_RS_BSPLINE ? ((c16 - 8) >> 4) - 1
           : bilinear != 0u ? (c16 - 8) >> 4
           : c16 >> 4;
}

// The number of taps of an axis.
GS_FN int gs_rs_count(int kind, int c16, int step32, uint bilinear)
{
    const int lo = gs_rs_box_lo(c16, step32);
    return kind == GS_RS_BOX ? ((lo + min(step32, GS_RS_BOX_MAX32) - 1) >> 5) - (lo >> 5) + 1
           : kind == GS_RS_BSPLINE ? 4
           : bilinear != 0u ? 2
           : 1;
}

// The weight of tap k (texel gs_rs_first + k) of an axis.
GS_FN int gs_rs_weight(int kind, int c16, int step32, uint bilinear, int k)
{
    const int lo = gs_rs_box_lo(c16, step32);
    const uint f = (uint)(c16 - 8) & 15u;
    return kind == GS_RS_BOX ? gs_box_w(lo, lo + min(step32, GS_RS_BOX_MAX32), (lo >> 5) + k)
           : kind == GS_RS_BSPLINE ? gs_bspline_w(f, (uint)k)
           : bilinear != 0u ? (k == 0 ? 16 - (int)f : (int)f)
           : 1;
}

// The sum of an axis's weights.
GS_FN int gs_rs_div(int kind, int step32, uint bilinear)
{
    return kind == GS_RS_BOX ? min(step32, GS_RS_BOX_MAX32)
           : kind == GS_RS_BSPLINE ? GS_RS_BSPLINE_DIV
           : bilinear != 0u ? 16
           : 1;
}

// The fog above 1x (fog_lut.hlsl, g_param.w): one channel of the fog's
// texel between the table's neighbours lo = LUT[n] and hi = LUT[n + 1] at
// the depth's next eight bits f (Z bits 8..15), times the texture
// function's factor mul in 1/128 (MODULATE: the vertex colour; DECAL:
// 128), with the ordered dither's threshold b (gs_bayer8, 0..63) before
// the rounding down:
//   floor((lo (256 - f) + hi f) / 256 * mul / 128 + (2 b + 1) / 128),
// at most 255. In 1/32768 throughout (the sum below 2^24): at f = 0 and
// mul = 128 it is lo exactly, whatever b.
GS_FN uint gs_fog_channel(uint lo, uint hi, uint f, uint mul, uint b)
{
    const uint x = (lo * (256u - f) + hi * f) * mul;
    return min((x + 256u * (2u * (b & 63u) + 1u)) >> 15, 255u);
}

#ifndef ICO_GS_MATH_C // the rest is HLSL only

// Vulkan-only attributes: DXC rejects them (as warnings, which are errors
// here) when compiling DXIL.
#ifdef __spirv__
#define VK_LOC(n) [[vk::location(n)]]
#define VK_DUAL(n, i) [[vk::location(n), vk::index(i)]]
#else
#define VK_LOC(n)
#define VK_DUAL(n, i)
#endif

// ---------------------------------------------------------------- FrameCB
// Group 0, slot 0. 336 bytes (shader_consts.h IcoFrameCB, asserted there).
// Matrices are column-major float[16] (element
// [column * 4 + row]) used as mul(M, v) with column vectors; rd's RdCamera
// carries them in that order.
cbuffer FrameCB : register(b0, space0)
{
    column_major float4x4 g_view;
    column_major float4x4 g_proj;
    column_major float4x4 g_viewProj;
    float4 g_cameraPos; // xyz world-space eye, w = 1 on a camera cut tick
    float4 g_clip;      // x near, y far, z zoom, w aspect (width / height)
    float4 g_target;    // xy size of the bound target in GS pixels, zw 1 / size
    float4 g_origin;    // xy GS window coordinate of the target's top-left pixel
                        // (XYOFFSET / 16), zw added to the result (GS pixel-centre
                        // convention; 0 puts GS integer coordinates on pixel edges)
    // The native frame (port/include/ico_frame.h). g_frameRegs[0] (g_frame):
    // x the region scale, 512 / W on a target as wide as the native frame W,
    // exactly 1 on every other target and at 4:3 (no shader reads it);
    // y, z the drawing camera's picture, its half-width and half-height in
    // GS pixels about (2048, 2048) (vu_common.hlsli vu_picture_half: 256 by
    // half the native height for the frame's camera, a reflection camera's
    // own); w the exact viewport's half texel (below). Nothing transforms a
    // position: a GS X is drawn where the game put it, at every aspect, and
    // the VU programs' region test reads the position as computed
    // (vu_common.hlsli vu_triangle_out).
    // g_frameRegs[1] (g_vp): the exact viewport (rd_replay.c
    // rd__frame_group_ex, setViewport): x, y 2 / its width and height in
    // texels (a power of two, centred on the target: positions on the GS's
    // 1/16 grid map to the same texel at every frame width, exactly),
    // z, w the target's texels per GS pixel; 0 when the viewport is the
    // target (the present, the films, the shader tests: the plain mapping).
    float4 g_frameRegs[2];
    float4 g_z;    // x scale (1 / 2^24), yz the bound target's texels per GS
                   // pixel (1 in Original), w 1: the VU programs output
                   // unquantised X, Y (only the developer switch
                   // ICO_RD_VU_OFFGRID, Enhanced on a scaled target)
    float4 g_misc; // x frame counter, y preset (0 Original, 1 Enhanced), zw the
                   // target's share of the exact viewport (fullscreen_triangle)
    float4 g_vpo;  // xy the exact viewport's centre this far (0 or 0.5 texel)
                   // before the target's (its origin on a whole texel for an
                   // odd-sized target: no device's viewport sub-pixel
                   // precision is relied on), z the texels of the GS frame
                   // a Full pixel target leaves out at its left (its texture
                   // the frame's exact width alone, centred: rd_replay.c
                   // rd__frame_group_ex), else 0; w reserved
};

// ---------------------------------------------------------------- DrawCB
// Group 1, slot 1. 112 bytes.
//   g_col    constant colour RGBA 0..255 (blit tint, fade colour)
//   g_mode   x = DF_* flags
//            y = TEXA mode | TEXFMT << 8
//            z = ATST | ate << 8 | split << 16 (0 none, 1 keep passing
//                fragments, 2 keep failing fragments: AFAIL FB_ONLY passes)
//            w = AREF
//   g_blend  x = ALPHA register (A | B << 2 | C << 4 | D << 6)
//            y = FIX 0..255, z = COLCLAMP
//            w = with DF_GS_LOD, TEX1's mipmap fields (gs_lod), else 0
//   g_uvRect source rectangle in texels (u0, v0, u1, v1), blit passes
//   g_tex    xy = size of the t1 texture in texels, zw = 1 / size
//   g_param  kind-specific: each entry's header says what it reads (e.g.
//            blend_int's source offset in pixels, xy)
//   g_scale  xy t1 texels per GS texel: 1 for images and unscaled targets,
//            the target's scale for a scaled one; z, with t1 a Full pixel
//            crop of the frame (rd_replay.c, RdTargetRec.ox), its left-out
//            texels over its width (ox / tw: the normalised U subtracted,
//            gs_uv_norm), else 0; w fx_sprite_ps's resampling steps above
//            1x (fx_sprite.hlsl), else 0
cbuffer DrawCB : register(b1, space1)
{
    uint4 g_col;
    uint4 g_mode;
    uint4 g_blend;
    float4 g_uvRect;
    float4 g_tex;
    float4 g_param;
    float4 g_scale;
    // (shader_consts.h IcoDrawCB.aux0, aux1): spare blocks of the
    // folded blur pair and of the pixel cuts' per-draw constants
    uint4 g_aux0;
    uint4 g_aux1;
};

#define DF_TEXTURED 1u    // PRIM.TME
#define DF_DECAL 2u       // TFX DECAL (else MODULATE)
#define DF_TCC_RGBA 4u    // TCC: texture alpha used (else vertex alpha)
#define DF_FBA 8u         // force the alpha MSB on write
#define DF_PABE 16u       // blend only where As has its MSB set (else Cs unblended)
#define DF_FIX_FACTOR 32u // dual-source factor is FIX / 128 instead of As / 128
#define DF_PREMUL 64u     // colour output is (Cs * factor) >> 7, pipeline src factor ONE
#define DF_DATE 128u      // TEST.DATE: destination alpha test against the snapshot in t2
#define DF_DATM 256u      // TEST.DATM: with DF_DATE, pass where the MSB is 1 (else 0)
#define DF_AA1_FULL 512u  // sprite_aa1_ps: PRIM.ABE 0, the coverage alpha replaces every alpha
#define DF_C1_DST 32768u  // the pipeline is Cs + Cd * c1 (dst factor SRC1): see gs_dual_out
// The two-pass blend without dual-source blending (the
// *_nodual entries, built with ICO_NO_DUAL=1; rd_pipeline.c rd__expand_no_dual):
#define DF_NODUAL_FACTOR 65536u      // colour pass: c0.a is the blend factor c1 would carry
#define DF_NODUAL_ALPHA_PASS 131072u // alpha pass: c0.a is the stored alpha (RGB unused)
// sprite_texa_ps and vu_texa_ps (gs_texa_texture): the sampler state the
// four-tap filter reproduces; the CLAMP bits also for gs_sample_lod_nearest
// (DF_GS_LOD with DF_NEAREST). Other entries never read these bits.
#define DF_TEXA_MAG_LINEAR 1024u // TEX1.MMAG linear
#define DF_TEXA_MIN_LINEAR 2048u // TEX1.MMIN linear
#define DF_TEXA_CLAMP_S 4096u    // CLAMP on s (else REPEAT)
#define DF_TEXA_CLAMP_T 8192u    // CLAMP on t (else REPEAT)
#define DF_TEXA_MIN_SAMPLED                                                                        \
    16384u // a minified pixel takes the bound sampler (the Enhanced
           // trilinear or anisotropic filter over the mips), TEXA after
// The texture has its GS mipmap levels (the TIM2's own, not box filtered):
// sample at the GS's level of detail (gs_lod_level) with TEX1's fields in
// g_blend.w. Every entry that samples a game texture honours it.
#define DF_GS_LOD 262144u
// A destination-read pass (the *_dstrov and *_dstatt entries): the channels
// it writes, R G B A = bits 19..22 (shader_consts.h ICO_DF_WMASK_SHIFT)
#define DF_WMASK_SHIFT 19u
// A VU draw with a clip plane (vu_common.hlsli vu_plane_distance,
// port/render/rd_chain.h): the pixels past it are discarded
#define DF_PLANE 8388608u
// Both of the draw's filters nearest (rd_replay.c fillDrawCB): gs_sample
// takes the texel the GS takes (gs_nearest_uv; with DF_GS_LOD
// gs_sample_lod_nearest, which reads DF_TEXA_CLAMP_S/T for the wrap)
#define DF_NEAREST 16777216u
// Above 1x (rd_replay.c fillDrawCB): a bilinear draw sampling a target
// whose scale is not the bound target's, without GS levels; sprite_ps and
// sprite_colour resample it by the step (sprite.hlsl sprite_above1x), the
// wrap in DF_TEXA_CLAMP_S/T. Never set when the two scales are equal, so
// never when every target is at scale 1.
#define DF_ABOVE1X 33554432u
// PRIM.AA1 under DATE (rd_replay_aa1.inc aa1DateDraw): each primitive is two
// draws, one keeping the fragments whose written alpha (As, FBA's MSB
// added) has its MSB set, the other those whose MSB is clear, so the
// stencil can carry the destination alpha's MSB from one primitive to the
// next (sprite.hlsl sprite_aa1_ps). shader_consts.h ICO_DF_AA1_MSB_SET,
// ICO_DF_AA1_MSB_CLEAR.
#define DF_AA1_MSB_SET 134217728u
#define DF_AA1_MSB_CLEAR 268435456u

// TEST.DATE. snap is the R8 DATE snapshot (1.0 where the destination alpha
// had its MSB set when the snapshot was taken). Returns true when the
// fragment must be discarded.
bool gs_date_discard(uint flags, float snap)
{
    if ((flags & DF_DATE) == 0u) {
        return false;
    }
    bool msb = snap >= 0.5;
    bool want = (flags & DF_DATM) != 0u;
    return msb != want;
}

#define g_frame g_frameRegs[0]
#define g_vp g_frameRegs[1]

// ------------------------------------------------------------ vector forms

uint4 gs_tfx_modulate4(uint4 tex, uint4 col)
{
    return min((tex * col) >> 7, uint4(255, 255, 255, 255));
}

// Texture function with TCC. tex and col are 0..255 per channel.
uint4 gs_texture_function(uint4 tex, uint4 col, uint flags)
{
    uint4 o;
    if ((flags & DF_DECAL) != 0u) {
        o.rgb = tex.rgb;
        o.a = (flags & DF_TCC_RGBA) != 0u ? tex.a : col.a;
    } else {
        o.rgb = gs_tfx_modulate4(tex, col).rgb;
        o.a = (flags & DF_TCC_RGBA) != 0u ? gs_tfx_mod(tex.a, col.a) : col.a;
    }
    return o;
}

// A texel's alpha under TEXA for PSMCT24 / PSMCT16 sources; RGBA32 texels
// pass through.
uint4 gs_texa_expand(uint4 t, uint mode, uint fmt)
{
    if (fmt == TEXFMT_RGBA32) {
        return t;
    }
    return uint4(t.rgb, gs_texa_alpha(t.r, t.g, t.b, t.a, mode, fmt));
}

// The GS's bilinear blend of the four texels around a sample point with
// its 4-bit fractions fu, fv: across first, each step truncated,
//   top = a + (((b - a) fu) >> 4), bottom = c + (((d - c) fu) >> 4),
//   t = top + (((bottom - top) fv) >> 4)
// (arithmetic shifts: a negative step floors).  PCSX2's software renderer
// filters so; its per-draw dumps of the opening's blur chain (staticBlur,
// 20 sprite steps and the eye blur) match this to the bit, where one
// weighted sum >> 8 is a level above on ~5 % of the pixels of each step
// and the 20 steps add up to ~15 % more glow (rd_blur_test
// testBilerpPcsx2).
uint4 gs_bilerp(uint4 a, uint4 b, uint4 c, uint4 d, uint fu, uint fv)
{
    const int4 ia = int4(a), ib = int4(b), ic = int4(c), id = int4(d);
    const int4 top = ia + (((ib - ia) * int(fu)) >> 4);
    const int4 bot = ic + (((id - ic) * int(fu)) >> 4);
    return uint4(top + (((bot - top) * int(fv)) >> 4));
}

// Above 1x (the kernels at the top of this file): one axis's taps for the
// Load-based form. The caller fetches texels first .. first + count - 1
// with its own addressing and combines them as the header says.
struct GsAxis {
    int first;
    int count;
    int kind; // GS_RS_*
    int c16;
    int step32;
    uint bilinear;
};

GsAxis gs_resample_axis(int c16, int step32, bool bilinear)
{
    GsAxis a;
    a.bilinear = bilinear ? 1u : 0u;
    a.kind = gs_rs_kind(step32, a.bilinear);
    a.c16 = c16;
    a.step32 = step32;
    a.first = gs_rs_first(a.kind, c16, step32, a.bilinear);
    a.count = gs_rs_count(a.kind, c16, step32, a.bilinear);
    return a;
}

uint gs_resample_weight(GsAxis a, int k)
{
    return uint(gs_rs_weight(a.kind, a.c16, a.step32, a.bilinear, k));
}

uint gs_resample_div(GsAxis a)
{
    return uint(gs_rs_div(a.kind, a.step32, a.bilinear));
}

// The sampler-based form: the B-spline in both axes from four bilinear
// taps of smp (which must address as the draw does). tc is the GS's sample
// point in texels of tex (the 12.4 coordinate / 16, the texel centre at
// i + 0.5); the fraction is the GS's 4-bit one, as gs_bspline_w takes it.
float4 gs_bspline_bilinear4(Texture2D<float4> tex, SamplerState smp, float2 tc, float2 size)
{
    const int2 c16 = int2(floor(tc * 16.0 + 0.5));
    const int2 i0 = (c16 - 8) >> 4;
    const uint2 f = uint2((c16 - 8) & 15);
    float2 g0, g1, h0, h1;
    [unroll] for (uint k = 0u; k < 2u; k++) {
        const float w0 = float(gs_bspline_w(f[k], 0u)), w1 = float(gs_bspline_w(f[k], 1u));
        const float w2 = float(gs_bspline_w(f[k], 2u)), w3 = float(gs_bspline_w(f[k], 3u));
        g0[k] = (w0 + w1) / float(GS_RS_BSPLINE_DIV);
        g1[k] = (w2 + w3) / float(GS_RS_BSPLINE_DIV);
        // the taps' centres in texels (texel j's centre at j + 0.5)
        h0[k] = float(i0[k]) - 1.0 + w1 / (w0 + w1) + 0.5;
        h1[k] = float(i0[k]) + 1.0 + w3 / max(w2 + w3, 1.0) + 0.5;
    }
    const float2 inv = 1.0 / size;
    return g0.y * (g0.x * tex.SampleLevel(smp, float2(h0.x, h0.y) * inv, 0.0) +
                   g1.x * tex.SampleLevel(smp, float2(h1.x, h0.y) * inv, 0.0)) +
           g1.y * (g0.x * tex.SampleLevel(smp, float2(h0.x, h1.y) * inv, 0.0) +
                   g1.x * tex.SampleLevel(smp, float2(h1.x, h1.y) * inv, 0.0));
}

// TEXA before filtering (sprite_texa_ps, vu_texa_ps): the texel of a
// PSMCT24 or PSMCT16 texture (or one with a 24- or 16-bit CLUT) under TEXA
// with AEM, filtered as the GS filters it.
// The GS expands TEXA per texel and then filters; the sampler would filter
// RGB and the A bit first, so where texels of different alpha meet (AEM's
// black, 7F/81's two A values) a bilinear edge differs. Here each of the
// four texels around the sample point is loaded, expanded, and blended
// with the GS's 4-bit fractions (the UV rounded to 12.4 texels, u - 0.5,
// gs_bilerp), as fx_sprite_ps does. Addressing is the
// sampler's: REPEAT or CLAMP at t1's own size. MAG or MIN by the
// footprint (rho > 1 texel: minified), as the sampler picks it. uvn is the
// normalised UV; flags g_mode.x.
// The texel the GS's nearest filter takes at tc (in texels of the level
// sampled): the coordinate at the GS's 4-bit texel fraction (rounded, as
// the bilinear taps below take it), its integer part. The GS samples a
// pixel at its integer coordinate, so a UV step of a whole or half texel
// per pixel puts every few pixels exactly on a texel boundary, where the
// GS takes the texel the boundary starts; the interpolated UV is that
// value only to within float rounding, which depends on the pixel's place
// in the target, and floor(tc) took the texel before on some of them.
int2 gs_nearest_texel(float2 tc)
{
    return int2(floor(tc * 16.0 + 0.5)) >> 4;
}

uint4 gs_texa_load(Texture2D<float4> tex, int2 c, int2 size, uint flags)
{
    c.x = (flags & DF_TEXA_CLAMP_S) != 0u ? clamp(c.x, 0, size.x - 1) : gs_wrap_repeat(c.x, size.x);
    c.y = (flags & DF_TEXA_CLAMP_T) != 0u ? clamp(c.y, 0, size.y - 1) : gs_wrap_repeat(c.y, size.y);
    uint4 t = uint4(floor(tex.Load(int3(c, 0)) * 255.0 + 0.5));
    return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
}

// The texel c of t1's mip level lv, 0..255, wrapped by DF_TEXA_CLAMP_S/T
// (REPEAT or CLAMP at the level's size), before TEXA.
uint4 gs_load_lv(Texture2D<float4> tex, int2 c, int2 size, uint lv, uint flags)
{
    c.x = (flags & DF_TEXA_CLAMP_S) != 0u ? clamp(c.x, 0, size.x - 1) : gs_wrap_repeat(c.x, size.x);
    c.y = (flags & DF_TEXA_CLAMP_T) != 0u ? clamp(c.y, 0, size.y - 1) : gs_wrap_repeat(c.y, size.y);
    return uint4(floor(tex.Load(int3(c, int(lv))) * 255.0 + 0.5));
}

uint4 gs_texa_texture(Texture2D<float4> tex, SamplerState smp, float2 uvn, uint flags)
{
    uint w, h;
    tex.GetDimensions(w, h);
    const int2 size = max(int2(int(w), int(h)), int2(1, 1));
    const float2 tc = uvn * float2(size);
    const float2 dx = ddx(uvn), dy = ddy(uvn);
    const float2 tdx = dx * float2(size), tdy = dy * float2(size);
    const bool minified = max(dot(tdx, tdx), dot(tdy, tdy)) > 1.0;
    if (minified && (flags & DF_TEXA_MIN_SAMPLED) != 0u) {
        uint4 t = uint4(floor(tex.SampleGrad(smp, uvn, dx, dy) * 255.0 + 0.5));
        return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
    }
    const bool filtered = (flags & (minified ? DF_TEXA_MIN_LINEAR : DF_TEXA_MAG_LINEAR)) != 0u;
    if (!filtered) {
        return gs_texa_load(tex, gs_nearest_texel(tc), size, flags);
    }
    const int2 q = int2(floor(tc * 16.0 + 0.5)) - int2(8, 8);
    const int2 i0 = q >> 4;
    const uint fu = uint(q.x & 15), fv = uint(q.y & 15);
    const uint4 a = gs_texa_load(tex, i0, size, flags);
    const uint4 b = gs_texa_load(tex, i0 + int2(1, 0), size, flags);
    const uint4 c = gs_texa_load(tex, i0 + int2(0, 1), size, flags);
    const uint4 d = gs_texa_load(tex, i0 + int2(1, 1), size, flags);
    return gs_bilerp(a, b, c, d, fu, fv);
}

// ------------------------------------------------------- GS mipmapping
// TEX1's mipmap fields in g_blend.w (rd_state.h rd_tex1_lod): bits 0..2
// MMIN, 3..5 MXL (already no further than the texture's levels), 6 LCM,
// 7..8 L, 16..27 K (12-bit signed, sixteenths of a level).

// The GS's level of detail at a pixel whose interpolated Q is q:
// LOD = (log2(1 / |Q|) << L) + K, or K alone with LCM.
float gs_lod(float q)
{
    const uint p = g_blend.w;
    const float k = float(int(p << 4u) >> 20) * (1.0 / 16.0);
    if (((p >> 6u) & 1u) != 0u) {
        return k;
    }
    const float l = float(1u << ((p >> 7u) & 3u));
    return -log2(max(abs(q), 1.0e-30)) * l + k;
}

// The level SampleLevel takes for the pixel, with the sampler the replay
// binds for these draws (rd_replay.c gsLodSampler: linear between levels
// for MMIN 3 and 5, nearest otherwise). A LOD of 0 or below magnifies:
// level 0 with MMAG. Above 0 MMIN picks it, never past MXL: the nearest
// level for 2 and 4 (rounded half up), the LOD itself for 3 and 5 (the
// sampler blends the two levels around it), level 0 for 0 and 1. A
// minified pixel at level 0 gets a level just above 0, so the sampler takes
// the MMIN filter there as the GS does.
float gs_lod_level(float q)
{
    const float lod = gs_lod(q);
    if (!(lod > 0.0)) {
        return 0.0;
    }
    const uint p = g_blend.w;
    const uint mmin = p & 7u;
    const float mxl = float((p >> 3u) & 7u);
    float lv = 0.0;
    if (mmin == 3u || mmin == 5u) {
        lv = min(lod, mxl);
    } else if (mmin == 2u || mmin == 4u) {
        lv = min(floor(lod + 0.5), mxl);
    }
    return max(lv, 1.0 / 256.0);
}

// The centre of the texel the GS's nearest filter takes at uvn (normalised
// on tex's level 0, gs_nearest_texel), for the sampler to read: the
// interpolated UV's own rounding took the texel on either side of a
// boundary depending on the frame's width and the row.
float2 gs_nearest_uv(Texture2D<float4> tex, float2 uvn)
{
    uint w, h;
    tex.GetDimensions(w, h);
    const float2 size = float2(max(w, 1u), max(h, 1u));
    return (float2(gs_nearest_texel(uvn * size)) + 0.5) / size;
}

// gs_sample with DF_GS_LOD and DF_NEAREST (both filters nearest): the
// GS's nearest texel (gs_nearest_texel) on the level gs_lod_level picks,
// wrapped as the sampler wraps (DF_TEXA_CLAMP_S/T), and for MMIN 3 on the
// level after it too, the two blended by the LOD's fraction in 256ths,
// truncated (as gs_texa_texture_lod blends them).
float4 gs_sample_lod_nearest(Texture2D<float4> tex, float2 uvn, float q)
{
    const float lv = gs_lod_level(q);
    const uint l0 = uint(floor(lv));
    uint w, h, n;
    tex.GetDimensions(l0, w, h, n);
    int2 size = max(int2(int(w), int(h)), int2(1, 1));
    uint4 t = gs_load_lv(tex, gs_nearest_texel(uvn * float2(size)), size, l0, g_mode.x);
    if (gs_lod(q) > 0.0 && (g_blend.w & 7u) == 3u) {
        const uint f = uint(floor((lv - float(l0)) * 256.0));
        if (f > 0u) {
            tex.GetDimensions(l0 + 1u, w, h, n);
            size = max(int2(int(w), int(h)), int2(1, 1));
            const uint4 u =
                gs_load_lv(tex, gs_nearest_texel(uvn * float2(size)), size, l0 + 1u, g_mode.x);
            t = (t * (256u - f) + u * f) >> 8;
        }
    }
    return float4(t) * (1.0 / 255.0);
}

// With DF_GS_LOD in the Original preset, a filter linear (rd_replay.c
// fillDrawCB, rd_pipeline.c rd__gs_filter_int) and no TEXA to apply per
// texel (rd__texa_per_texel): gs_sample filters on the GS's grid with the
// sampler (gs_sample_gs_bilinear), which rd_replay.c gsLodSampler binds
// nearest between levels. shader_consts.h ICO_DF_GS_BILINEAR_GRID.
#define DF_GS_BILINEAR_GRID 67108864u

// Level lv of tex at uvn, 0..255, the sampler's filter at the GS's sample
// point: the coordinate in the level's texels rounded to the GS's 12.4
// grid as gs_texa_level rounds it (q = floor(uvn size 16 + 0.5)), so the
// sampler's texel pair, (q - 8) >> 4 and the next, and its weight,
// ((q - 8) & 15) / 16, are the GS's. q / 16 / size is exact in float for
// the GS's power-of-two sizes (a texture is padded to 2^TW x 2^TH), so the
// sampler's coordinate, q / 16 - 0.5 texels, has four fraction bits and
// every device's sub-texel precision holds it (Vulkan's minimum is 4
// bits), rounded or truncated alike. The sampler
// wraps (REPEAT or CLAMP at the level's size) as gs_load_lv does. lod the
// level for SampleLevel: lv, or for level 0 at most 1/256 above it (above
// 0 the sampler takes the MMIN filter, as gs_lod_level has it).
uint4 gs_grid_level(Texture2D<float4> tex, SamplerState smp, float2 uvn, uint lv, float lod)
{
    uint w, h, n;
    tex.GetDimensions(lv, w, h, n);
    const float2 size = float2(max(w, 1u), max(h, 1u));
    const float2 tc = uvn * size;
    const float2 q = floor(tc * 16.0 + 0.5);
    const float2 uvg = q * (1.0 / 16.0) / size;
    return uint4(floor(tex.SampleLevel(smp, uvg, lod) * 255.0 + 0.5));
}

// gs_sample with DF_GS_BILINEAR_GRID: gs_texa_texture_lod's levels and
// blend with the sampler's bilinear inside a level (gs_grid_level): the
// level gs_lod_level picks, and for MMIN 3 and 5 (minified) the level
// after it, blended by the LOD's fraction in 256ths, truncated. Within a
// level the sampler weighs the four texels in one sum, rounded, where the
// GS truncates after each of its three steps (gs_bilerp): on a device
// whose filter is exact to within a level the sampler's value is 0 to 2
// levels above the GS's, never below (docs/port/RENDER.md, "Textures");
// the blend of two levels keeps that bound.
float4 gs_sample_gs_bilinear(Texture2D<float4> tex, SamplerState smp, float2 uvn, float q)
{
    const float lv = gs_lod_level(q);
    const uint l0 = uint(floor(lv));
    uint4 t = gs_grid_level(tex, smp, uvn, l0, max(float(l0), min(lv, 1.0 / 256.0)));
    const uint mmin = g_blend.w & 7u;
    if (gs_lod(q) > 0.0 && (mmin == 3u || mmin == 5u)) {
        const uint f = uint(floor((lv - float(l0)) * 256.0));
        if (f > 0u) {
            const uint4 u = gs_grid_level(tex, smp, uvn, l0 + 1u, float(l0 + 1u));
            t = (t * (256u - f) + u * f) >> 8;
        }
    }
    return float4(t) * (1.0 / 255.0);
}

// The texel at uv: at the GS's level with DF_GS_LOD (the GS's nearest
// texel there with DF_NEAREST, the GS's grid with DF_GS_BILINEAR_GRID),
// the GS's nearest texel with DF_NEAREST alone (the sampler has the one
// level), else as the sampler takes it (q unused).
float4 gs_sample(Texture2D<float4> tex, SamplerState smp, float2 uv, float q)
{
    if ((g_mode.x & DF_GS_LOD) != 0u) {
        if ((g_mode.x & DF_NEAREST) != 0u) {
            return gs_sample_lod_nearest(tex, uv, q);
        }
        if ((g_mode.x & DF_GS_BILINEAR_GRID) != 0u) {
            return gs_sample_gs_bilinear(tex, smp, uv, q);
        }
        return tex.SampleLevel(smp, uv, gs_lod_level(q));
    }
    if ((g_mode.x & DF_NEAREST) != 0u) {
        return tex.SampleLevel(smp, gs_nearest_uv(tex, uv), 0.0);
    }
    return tex.Sample(smp, uv);
}

// gs_texa_load at mip level lv of t1.
uint4 gs_texa_load_lv(Texture2D<float4> tex, int2 c, int2 size, uint lv, uint flags)
{
    return gs_texa_expand(gs_load_lv(tex, c, size, lv, flags), g_mode.y & 0xFFu, g_mode.y >> 8);
}

// gs_texa_texture's four taps at mip level lv (filtered: bilinear with the
// GS's 4-bit fractions, else the nearest texel).
uint4 gs_texa_level(Texture2D<float4> tex, float2 uvn, uint lv, bool filtered, uint flags)
{
    uint w, h, n;
    tex.GetDimensions(lv, w, h, n);
    const int2 size = max(int2(int(w), int(h)), int2(1, 1));
    const float2 tc = uvn * float2(size);
    if (!filtered) {
        return gs_texa_load_lv(tex, gs_nearest_texel(tc), size, lv, flags);
    }
    const int2 q = int2(floor(tc * 16.0 + 0.5)) - int2(8, 8);
    const int2 i0 = q >> 4;
    const uint fu = uint(q.x & 15), fv = uint(q.y & 15);
    const uint4 a = gs_texa_load_lv(tex, i0, size, lv, flags);
    const uint4 b = gs_texa_load_lv(tex, i0 + int2(1, 0), size, lv, flags);
    const uint4 c = gs_texa_load_lv(tex, i0 + int2(0, 1), size, lv, flags);
    const uint4 d = gs_texa_load_lv(tex, i0 + int2(1, 1), size, lv, flags);
    return gs_bilerp(a, b, c, d, fu, fv);
}

// gs_texa_texture with DF_GS_LOD: the level (or the two levels, blended by
// the LOD's fraction in 256ths, for MMIN 3 and 5) by gs_lod_level, the
// filter within a level MMAG or MMIN by the LOD's sign.
uint4 gs_texa_texture_lod(Texture2D<float4> tex, float2 uvn, float q, uint flags)
{
    const bool minified = gs_lod(q) > 0.0;
    const bool filtered = (flags & (minified ? DF_TEXA_MIN_LINEAR : DF_TEXA_MAG_LINEAR)) != 0u;
    const float lv = gs_lod_level(q);
    const uint l0 = uint(floor(lv));
    uint4 t = gs_texa_level(tex, uvn, l0, filtered, flags);
    const uint mmin = g_blend.w & 7u;
    if (minified && (mmin == 3u || mmin == 5u)) {
        const uint f = uint(floor((lv - float(l0)) * 256.0));
        if (f > 0u) {
            const uint4 u = gs_texa_level(tex, uvn, l0 + 1u, filtered, flags);
            t = (t * (256u - f) + u * f) >> 8;
        }
    }
    return t;
}

// The integer blend per channel with the ALPHA register, for feedback
// passes on RGBA8_UINT targets. Alpha is not blended: the GS writes the
// source alpha.
uint4 gs_blend_int(uint4 cs, uint4 cd, uint reg, uint fix, uint clampMode)
{
    int4 s = int4(cs);
    int4 d = int4(cd);
    uint4 o;
    o.r = (uint)gs_blend_reg_ch(reg, s.r, d.r, s.a, d.a, (int)fix, clampMode);
    o.g = (uint)gs_blend_reg_ch(reg, s.g, d.g, s.a, d.a, (int)fix, clampMode);
    o.b = (uint)gs_blend_reg_ch(reg, s.b, d.b, s.a, d.a, (int)fix, clampMode);
    o.a = cs.a;
    return o;
}

// Alpha test with the split passes. Returns true when the fragment must be
// discarded. flagsZ is g_mode.z, aref is g_mode.w.
bool gs_alpha_discard(uint flagsZ, uint aref, uint a)
{
    uint atst = (flagsZ & 0xFFu);
    bool ate = ((flagsZ >> 8) & 1u) != 0u;
    uint split = (flagsZ >> 16) & 3u;
    if (!ate) {
        return false;
    }
    bool pass = gs_alpha_pass(atst, aref, a);
    if (split == 1u) {
        return !pass;
    }
    if (split == 2u) {
        return pass;
    }
    return !pass; // AFAIL KEEP
}

// -------------------------------------------------------------- outputs

// Dual-source pixel output. SV_Target0: colour/255 with the stored GS alpha
// (a/255, raw: 0x80 stays 0x80). SV_Target1: the blend factor As/128 (or
// FIX/128), replicated to all four lanes so SRC1_COLOR and SRC1_ALPHA agree.
//
// A factor above 1.0 (As or FIX above 0x80) does not survive the hardware
// blender: for UNORM targets the factor is clamped to 0..1 (measured on
// llvmpipe; the Vulkan spec clamps fixed-point blend
// inputs). DF_PREMUL moves the multiply into the shader for the additive and
// subtractive forms (Cs*F + Cd, Cd - Cs*F): c0.rgb = min((Cs * f) >> 7, 255),
// exactly the GS term, and the pipeline blends with src factor ONE.
//
// The factor is the fragment's own As: FBA only forces the MSB of the alpha
// the GS stores, after the blend. PABE with the MSB of As clear writes Cs
// unblended: c0 = Cs and the c1 that makes the pipeline's equation give Cs,
// 1.0 for the lerp forms ((Cs - Cd) * 1 + Cd) and 0 where c1 scales Cd alone
// (DF_C1_DST: Cd*FIX + Cs). The premultiplied forms (ONE, ONE) cannot drop
// Cd: Cs + Cd or Cd - Cs there (rd_pipeline.c reports it; the game's PABE
// draws are all lerps).
//
// Without dual-source blending (ICO_NO_DUAL, the *_nodual
// entries) DualOut has c0 alone. A LERP or Cd*FIX + Cs draw becomes two
// passes (rd_pipeline.c rd__expand_no_dual): the colour pass (DF_NODUAL_FACTOR)
// writes the factor c1 would carry into c0.a, blended with SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA (or ONE / SRC_ALPHA) under an RGB-only mask; the alpha
// pass (DF_NODUAL_ALPHA_PASS) writes the stored alpha with blending off. The
// factor's value is the same float as c1's, so the blender sees the same
// numbers. Every other path reads c0 alone already.
#ifdef ICO_NO_DUAL
struct DualOut {
    VK_LOC(0) float4 c0 : SV_Target0;
};
#else
struct DualOut {
    VK_DUAL(0, 0) float4 c0 : SV_Target0;
    VK_DUAL(0, 1) float4 c1 : SV_Target1;
};
#endif

#ifdef ICO_NO_DUAL
DualOut gs_dual_out(uint4 col, uint flags, uint fix)
{
    uint f = (flags & DF_FIX_FACTOR) != 0u ? fix : col.a;
    const bool unblended = (flags & DF_PABE) != 0u && (col.a & 0x80u) == 0u;
    uint a = col.a;
    if ((flags & DF_FBA) != 0u) {
        a |= 0x80u;
    }
    DualOut o;
    if ((flags & DF_NODUAL_ALPHA_PASS) != 0u) {
        o.c0 = float4(float3(col.rgb), float(a)) * (1.0 / 255.0);
        return o;
    }
    const bool factor = (flags & DF_NODUAL_FACTOR) != 0u;
    if (unblended) {
        const float c1 = (flags & DF_C1_DST) != 0u ? 0.0 : 1.0;
        o.c0 = float4(float3(col.rgb), float(a)) * (1.0 / 255.0);
        if (factor) {
            o.c0.a = c1;
        }
        return o;
    }
    if ((flags & DF_PREMUL) != 0u) {
        uint3 p = min((col.rgb * f) >> 7, uint3(255, 255, 255));
        o.c0 = float4(float3(p), float(a)) * (1.0 / 255.0);
        return o;
    }
    o.c0 = float4(float3(col.rgb), float(a)) * (1.0 / 255.0);
    if (factor) {
        o.c0.a = float(f) * (1.0 / 128.0);
    }
    return o;
}
#else

DualOut gs_dual_out(uint4 col, uint flags, uint fix)
{
    uint f = (flags & DF_FIX_FACTOR) != 0u ? fix : col.a;
    const bool unblended = (flags & DF_PABE) != 0u && (col.a & 0x80u) == 0u;
    uint a = col.a;
    if ((flags & DF_FBA) != 0u) {
        a |= 0x80u;
    }
    DualOut o;
    if (unblended) {
        const float c1 = (flags & DF_C1_DST) != 0u ? 0.0 : 1.0;
        o.c0 = float4(float3(col.rgb), float(a)) * (1.0 / 255.0);
        o.c1 = float4(c1, c1, c1, c1);
        return o;
    }
    if ((flags & DF_PREMUL) != 0u) {
        uint3 p = min((col.rgb * f) >> 7, uint3(255, 255, 255));
        o.c0 = float4(float3(p), float(a)) * (1.0 / 255.0);
        o.c1 = float4(1.0, 1.0, 1.0, 1.0);
        return o;
    }
    o.c0 = float4(float3(col.rgb), float(a)) * (1.0 / 255.0);
    o.c1 = float4(f, f, f, f) * (1.0 / 128.0);
    return o;
}
#endif

// ------------------------------------------------- destination read
// The GS blend computed here, exactly, instead of in the GPU's blender
// (port/render/rd_dst_read.c says why and when). The fragment reads its own
// pixel of the bound target, works out what the GS stores there
// (gs_dst_channel: ((A - B) * C >> 7) + D with C = As, Ad or FIX on the
// 0x80 = 1.0 scale, COLCLAMP, PABE, FBA, the write mask) and writes it with
// the pipeline's blending off. The fixed-function blender instead computes
// Cs * F + Cd * (1 - F) in floats and rounds to nearest, where the GS
// truncates: one level up on about half the pixels of every LERP.
//
//   ICO_DST_READ=1 (*_dstrov): the target is a rasterizer-ordered view,
//     u0 in space 3 (D3D12 ROV; Vulkan storage image under
//     VK_EXT_fragment_shader_interlock, which DXC emits for it:
//     PixelInterlockOrderedEXT around the access). The pass has no colour
//     attachment, so the shader writes the target and applies the write
//     mask itself. globallycoherent (SPIR-V Coherent): the critical
//     section's load sees the previous fragment's store, not a stale cache
//     line. [earlydepthstencil]: the Z and stencil tests run before
//     the shader, as the GS tests before it writes (a fragment that fails
//     never reaches the store); the planner keeps a discard and a Z write
//     from meeting in one such pass (rd_dst_read.c).
//   ICO_DST_READ=2 (*_dstatt, Vulkan only): the colour attachment read as
//     an input attachment (VK_KHR_dynamic_rendering_local_read) in
//     rasterization order (VK_EXT_rasterization_order_attachment_access),
//     the result written to SV_Target0 under the pipeline's write mask;
//     depth, stencil and discard as on any pass. Compiled for DXIL the
//     entries are the ICO_DST_READ=1 ones (never used there).
#ifndef ICO_DST_READ
#define ICO_DST_MODE 0
#elif ICO_DST_READ == 2 && defined(__spirv__)
#define ICO_DST_MODE 2
#elif ICO_DST_READ != 0
#define ICO_DST_MODE 1
#else
#define ICO_DST_MODE 0
#endif

// The pixel the GS stores for colour col (As in col.a) over dst.
uint4 gs_dst_pixel(uint4 col, uint4 dst, uint flags)
{
    const bool fba = (flags & DF_FBA) != 0u;
    const bool pabe = (flags & DF_PABE) != 0u;
    const uint wmask = (flags >> DF_WMASK_SHIFT) & 15u;
    const uint reg = g_blend.x, fix = g_blend.y, cl = g_blend.z;
    uint4 o;
    o.r = gs_dst_channel(0u, col.r, dst.r, col.a, dst.a, reg, fix, cl, fba, pabe, wmask);
    o.g = gs_dst_channel(1u, col.g, dst.g, col.a, dst.a, reg, fix, cl, fba, pabe, wmask);
    o.b = gs_dst_channel(2u, col.b, dst.b, col.a, dst.a, reg, fix, cl, fba, pabe, wmask);
    o.a = gs_dst_channel(3u, col.a, dst.a, col.a, dst.a, reg, fix, cl, fba, pabe, wmask);
    return o;
}

#if ICO_DST_MODE == 1
#ifdef __spirv__
[[vk::image_format("rgba8")]]
#endif
globallycoherent RasterizerOrderedTexture2D<unorm float4> g_dstRov : register(u0, space3);

// A discard here (GS_DISCARD) marks the fragment instead of ending it:
// the entry runs on to GS_RETURN, whose critical section stores the pixel
// it read back unchanged. Vulkan's interlock wants every invocation to run
// OpBeginInvocationInterlockEXT and OpEndInvocationInterlockEXT exactly
// once, in that order (SPV_EXT_fragment_shader_interlock, validation
// rules); DXC ends the section before each OpKill, on paths that never
// began it. The depth and stencil tests and writes ran before the shader
// ([earlydepthstencil]) and there is no colour attachment, so the store
// left unchanged is all a discard did here.
static bool g_dstKill = false;
#define GS_DISCARD g_dstKill = true

// UNORM8 holds k / 255 exactly: a load gives k back, a store of k / 255
// stores k (as blend_int's and fx_sprite_ps's targets)
void gs_dst_out(uint4 col, float4 pos)
{
    const uint2 p = uint2(pos.xy);
    const uint4 dst = uint4(floor(g_dstRov[p] * 255.0 + 0.5));
    const uint4 o = g_dstKill ? dst : gs_dst_pixel(col, dst, g_mode.x);
    g_dstRov[p] = float4(o) * (1.0 / 255.0);
}

#define GS_PS_OUT void
#define GS_PS_ATTR [earlydepthstencil]
#define GS_RETURN(col, pos)                                                                        \
    {                                                                                              \
        gs_dst_out(col, pos);                                                                      \
        return;                                                                                    \
    }
#elif ICO_DST_MODE == 2
[[vk::input_attachment_index(0)]] [[vk::binding(16, 3)]] SubpassInput<float4> g_dstIn;

struct DstOut {
    VK_LOC(0) float4 c0 : SV_Target0;
};

DstOut gs_dst_out(uint4 col)
{
    const uint4 dst = uint4(floor(g_dstIn.SubpassLoad() * 255.0 + 0.5));
    DstOut o;
    o.c0 = float4(gs_dst_pixel(col, dst, g_mode.x)) * (1.0 / 255.0);
    return o;
}

#define GS_PS_OUT DstOut
#define GS_PS_ATTR
#define GS_RETURN(col, pos) return gs_dst_out(col)
#define GS_DISCARD discard
#else
// every other build of an entry: the fixed-function blend's outputs
#define GS_PS_OUT DualOut
#define GS_PS_ATTR
#define GS_RETURN(col, pos) return gs_dual_out(col, g_mode.x, g_blend.y)
#define GS_DISCARD discard
#endif

// ------------------------------------------------------- vertex helpers

// A GS window coordinate in GS pixels (XYOFFSET-relative / 16) to clip
// space, the same for every space tag.  precise (here and in gs_depth,
// vu_common.hlsli vu_homogeneous_position): no fused multiply-add, so every
// pipeline that draws a primitive gets the same position and depth bits
// (the two-pass blend of a device without dual-source blending draws it
// twice, the second pass GEQUAL against the depth the first wrote:
// rd_pipeline.c).
//
// With the exact viewport (g_vp.x set, rd_replay.c s_vpExact) every step
// is exact for a position on the GS's 1/16 grid at an integer scale: the
// offset from the target's centre (GS pixels, 4 fraction bits), times the
// scale, plus the half texel, is the texel offset from the centre; times
// 2 / the viewport's power-of-two size is clip space; the GPU's viewport
// transform multiplies it back by half that size and adds the centre.  The
// clip-space numbers are the same at every frame width (a frame-wide
// target's centre is GS 2048 and the viewport's size is one for every
// target), so is what the GPU clips of a triangle.  The plain mapping
// (1 / the target's size, then the GPU's multiply by it) rounds at any
// size that is not a power of two.
float2 gs_px_to_ndc(float2 px)
{
    if (g_vp.x != 0.0) {
        precise float2 c = g_origin.xy + g_target.xy * 0.5;
        precise float2 q = (px - c) * g_vp.zw + g_frame.w + g_vpo.xy;
        precise float2 ndc = float2(q.x * g_vp.x, -(q.y * g_vp.y));
        return ndc;
    }
    precise float2 p = px - g_origin.xy + g_origin.zw;
    precise float2 ndc = float2(p.x * g_target.z * 2.0 - 1.0, 1.0 - p.y * g_target.w * 2.0);
    return ndc;
}

// A 12.4 GS window coordinate (as two u16 in a uint2) to clip space.
float2 gs_xy_to_ndc(uint2 xy)
{
    return gs_px_to_ndc(float2(xy) * (1.0 / 16.0));
}

// A UV in t1's GS texels normalised (g_tex.zw), less a Full pixel crop's
// left-out share (g_scale.z: 0 but for a cropped frame-wide target, whose
// g_tex.z is then its scale over its width, rd_replay.c fillDrawCB).  The
// STQ form subtracts it times Q, so S / Q is the normalised UV.
float2 gs_uv_norm(float2 uv)
{
    return uv * g_tex.zw - float2(g_scale.z, 0.0);
}

float3 gs_stq_norm(float2 st, float q)
{
    return float3(st * g_tex.zw - float2(g_scale.z, 0.0) * q, q);
}

// The exact viewport's attribute planes (vu_common.hlsli vu_attr_planes,
// sprite.hlsl sprite_plane_attrs).  gs_texel_offset: GS pixels to texels
// from the target's centre (gs_px_to_ndc's q: exact on the 1/16 grid at an
// integer scale, the same numbers at every frame width).  gs_plane_offset:
// a pixel's offset in texels from a corner given that way (pixel centres
// are k + 0.5, the centre a whole or half texel: exact).
float2 gs_texel_offset(float2 px)
{
    precise float2 c = g_origin.xy + g_target.xy * 0.5;
    precise float2 q = (px - c) * g_vp.zw + g_frame.w;
    return q;
}

float2 gs_plane_offset(float4 pos, float2 corner)
{
    // the texture's centre: the frame's, less the Full pixel crop's
    // left-out texels (g_vpo.z, 0 on every other target)
    precise float2 c = g_target.xy * g_vp.zw * 0.5 - float2(g_vpo.z, 0.0);
    precise float2 d = (pos.xy - c) - corner;
    return d;
}

float gs_depth(uint z)
{
    precise float d = gs_z_to_depth(z, g_z.x);
    return d;
}

// Fullscreen triangle from SV_VertexID, no vertex buffer: ndc covers the
// target, t is 0..1 across it (y down).  Under the exact viewport the
// target is the share g_misc.zw (a power of two's fraction: exact) of it
// about its centre.
void fullscreen_triangle(uint id, out float4 pos, out float2 t)
{
    float2 c = float2((id << 1) & 2u, id & 2u);
    float2 k = g_misc.z != 0.0 ? g_misc.zw : float2(1.0, 1.0);
    // the viewport's centre g_vpo before the target's (0 in the plain mapping)
    pos = float4((c.x * 2.0 - 1.0) * k.x + g_vpo.x * g_vp.x,
                 (1.0 - c.y * 2.0) * k.y - g_vpo.y * g_vp.y, 0.0, 1.0);
    t = c;
}

#endif // ICO_GS_MATH_C
#endif
