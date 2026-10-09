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
// Group 0, slot 0. 320 bytes. Matrices are column-major float[16] (element
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
    float4 g_space[2];  // [0] WORLD, [1] UI: ndc = ndc * xy + zw (scale xy, offset zw)
    float4 g_z;         // x scale (1 / 2^24), yz the bound target's texels per GS
                        // pixel (1 in Original), w 1: the VU programs output
                        // unquantised X, Y (only the developer switch
                        // ICO_RD_VU_OFFGRID, Enhanced on a scaled target)
    float4 g_misc;      // x frame counter, y preset (0 Original, 1 Enhanced), zw reserved
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
//   g_uvRect source rectangle in texels (u0, v0, u1, v1), blit passes
//   g_tex    xy = size of the t1 texture in texels, zw = 1 / size
//   g_param  kind-specific: each entry's header says what it reads (e.g.
//            blend_int's source offset in pixels, xy)
//   g_scale  xy t1 texels per GS texel: 1 for images and unscaled targets,
//            the target's scale for a scaled one; zw the x addressing
//            of a widened render-to-texture block, u' = u * z + w (0, 0:
//            none; gs_block_uv)
cbuffer DrawCB : register(b1, space1)
{
    uint4 g_col;
    uint4 g_mode;
    uint4 g_blend;
    float4 g_uvRect;
    float4 g_tex;
    float4 g_param;
    float4 g_scale;
};

// Widescreen reflections: a draw that samples a render-to-texture block
// widened by the display aspect maps its 4:3 u into the block (rd_replay.c
// fillDrawCB). z = 0 everywhere else, which returns uv as it is.
float2 gs_block_uv(float2 uv)
{
    return g_scale.z != 0.0 ? float2(uv.x * g_scale.z + g_scale.w, uv.y) : uv;
}

#define DF_TEXTURED 1u   // PRIM.TME
#define DF_DECAL 2u      // TFX DECAL (else MODULATE)
#define DF_TCC_RGBA 4u   // TCC: texture alpha used (else vertex alpha)
#define DF_FBA 8u        // force the alpha MSB on write
#define DF_PABE 16u      // blend only where As has its MSB set (else Cs unblended)
#define DF_FIX_FACTOR 32u // dual-source factor is FIX / 128 instead of As / 128
#define DF_PREMUL 64u    // colour output is (Cs * factor) >> 7, pipeline src factor ONE
#define DF_DATE 128u     // TEST.DATE: destination alpha test against the snapshot in t2
#define DF_DATM 256u     // TEST.DATM: with DF_DATE, pass where the MSB is 1 (else 0)
#define DF_AA1_FULL 512u // sprite_aa1_ps: PRIM.ABE 0, the coverage alpha replaces every alpha
#define DF_C1_DST 32768u // the pipeline is Cs + Cd * c1 (dst factor SRC1): see gs_dual_out
// The two-pass blend without dual-source blending (the
// *_nodual entries, built with ICO_NO_DUAL=1; rd_pipeline.c rd__ExpandNoDual):
#define DF_NODUAL_FACTOR 65536u      // colour pass: c0.a is the blend factor c1 would carry
#define DF_NODUAL_ALPHA_PASS 131072u // alpha pass: c0.a is the stored alpha (RGB unused)
// sprite_texa_ps and vu_texa_ps only (gs_texa_texture): the sampler state
// the four-tap filter reproduces. Other entries never read these bits.
#define DF_TEXA_MAG_LINEAR 1024u  // TEX1.MMAG linear
#define DF_TEXA_MIN_LINEAR 2048u  // TEX1.MMIN linear
#define DF_TEXA_CLAMP_S 4096u     // CLAMP on s (else REPEAT)
#define DF_TEXA_CLAMP_T 8192u     // CLAMP on t (else REPEAT)
#define DF_TEXA_MIN_SAMPLED 16384u // a minified pixel takes the bound sampler (the Enhanced
                                   // trilinear or anisotropic filter over the mips), TEXA after

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

// Space index into g_space.
#define SPACE_WORLD 0
#define SPACE_UI 1

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

// TEXA before filtering (sprite_texa_ps, vu_texa_ps): the texel of a
// PSMCT24 or PSMCT16 texture (or one with a 24- or 16-bit CLUT) under TEXA
// with AEM, filtered as the GS filters it.
// The GS expands TEXA per texel and then filters; the sampler would filter
// RGB and the A bit first, so where texels of different alpha meet (AEM's
// black, 7F/81's two A values) a bilinear edge differs. Here each of the
// four texels around the sample point is loaded, expanded, and weighted
// with the GS's 4-bit fractions (the UV rounded to 12.4 texels, u - 0.5,
// floor of the weighted sum >> 8), as fx_sprite_ps does. Addressing is the
// sampler's: REPEAT or CLAMP at t1's own size. MAG or MIN by the
// footprint (rho > 1 texel: minified), as the sampler picks it. uvn is the
// normalised UV (after gs_block_uv); flags g_mode.x.
uint4 gs_texa_load(Texture2D<float4> tex, int2 c, int2 size, uint flags)
{
    c.x = (flags & DF_TEXA_CLAMP_S) != 0u ? clamp(c.x, 0, size.x - 1)
                                          : ((c.x % size.x) + size.x) % size.x;
    c.y = (flags & DF_TEXA_CLAMP_T) != 0u ? clamp(c.y, 0, size.y - 1)
                                          : ((c.y % size.y) + size.y) % size.y;
    uint4 t = uint4(floor(tex.Load(int3(c, 0)) * 255.0 + 0.5));
    return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
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
        return gs_texa_load(tex, int2(floor(tc)), size, flags);
    }
    const int2 q = int2(floor(tc * 16.0 + 0.5)) - int2(8, 8);
    const int2 i0 = q >> 4;
    const uint fu = uint(q.x & 15), fv = uint(q.y & 15);
    const uint4 a = gs_texa_load(tex, i0, size, flags);
    const uint4 b = gs_texa_load(tex, i0 + int2(1, 0), size, flags);
    const uint4 c = gs_texa_load(tex, i0 + int2(0, 1), size, flags);
    const uint4 d = gs_texa_load(tex, i0 + int2(1, 1), size, flags);
    return (a * ((16u - fu) * (16u - fv)) + b * (fu * (16u - fv)) + c * ((16u - fu) * fv) +
            d * (fu * fv)) >> 8;
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
// passes (rd_pipeline.c rd__ExpandNoDual): the colour pass (DF_NODUAL_FACTOR)
// writes the factor c1 would carry into c0.a, blended with SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA (or ONE / SRC_ALPHA) under an RGB-only mask; the alpha
// pass (DF_NODUAL_ALPHA_PASS) writes the stored alpha with blending off. The
// factor's value is the same float as c1's, so the blender sees the same
// numbers. Every other path reads c0 alone already.
#ifdef ICO_NO_DUAL
struct DualOut
{
    VK_LOC(0) float4 c0 : SV_Target0;
};
#else
struct DualOut
{
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

// ------------------------------------------------------- vertex helpers

// A 12.4 GS window coordinate (as two u16 in a uint2) to clip space in the
// given space (SPACE_WORLD or SPACE_UI).  precise (here and in gs_depth,
// vu_common.hlsli vu_ndc and vu_homogeneous_position): no fused
// multiply-add, so every pipeline that draws a primitive gets the same
// position and depth bits (the two-pass blend of a device without
// dual-source blending draws it twice, the second pass GEQUAL against the
// depth the first wrote: rd_pipeline.c).
float2 gs_xy_to_ndc(uint2 xy, int space)
{
    precise float2 px = float2(xy) * (1.0 / 16.0) - g_origin.xy + g_origin.zw;
    precise float2 ndc = float2(px.x * g_target.z * 2.0 - 1.0, 1.0 - px.y * g_target.w * 2.0);
    precise float2 r = ndc * g_space[space].xy + g_space[space].zw;
    return r;
}

float gs_depth(uint z)
{
    precise float d = gs_z_to_depth(z, g_z.x);
    return d;
}

// Fullscreen triangle from SV_VertexID, no vertex buffer: ndc covers the
// target, t is 0..1 across it (y down).
void fullscreen_triangle(uint id, out float4 pos, out float2 t)
{
    float2 c = float2((id << 1) & 2u, id & 2u);
    pos = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    t = c;
}

#endif
