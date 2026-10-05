// common.hlsli: what every ICO shader shares. The byte layouts below are
// mirrored by port/shaders/shader_consts.h (C); change both together.
//
// Binding scheme (port/rhi/vk/README.md "Descriptor scheme"): the register
// space is the RHI bind group, the register number is the RHI slot. DXC is
// run with -fvk-b-shift 0, -fvk-t-shift 16, -fvk-s-shift 32 so Vulkan
// bindings come out as slot + 0 / 16 / 32.
//   group 0  b0            FrameCB, once per frame
//   group 1  b1            DrawCB, per draw (or per post pass)
//            t0            storage buffer (bones, particles; later waves)
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
    float4 g_z;         // x scale (1 / 2^24), yzw reserved
    float4 g_misc;      // x frame counter, y preset (0 Original, 1 Enhanced), zw reserved
};

// ---------------------------------------------------------------- DrawCB
// Group 1, slot 1. 96 bytes.
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
//   g_param  kind-specific: blend_int source offset in pixels (xy);
//            fog_lut strength (x)
cbuffer DrawCB : register(b1, space1)
{
    uint4 g_col;
    uint4 g_mode;
    uint4 g_blend;
    float4 g_uvRect;
    float4 g_tex;
    float4 g_param;
};

#define DF_TEXTURED 1u   // PRIM.TME
#define DF_DECAL 2u      // TFX DECAL (else MODULATE)
#define DF_TCC_RGBA 4u   // TCC: texture alpha used (else vertex alpha)
#define DF_FBA 8u        // force the alpha MSB on write
#define DF_PABE 16u      // blend only where As has its MSB set
#define DF_FIX_FACTOR 32u // dual-source factor is FIX / 128 instead of As / 128
#define DF_PREMUL 64u    // colour output is (Cs * factor) >> 7, pipeline src factor ONE

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
// llvmpipe, docs/port/SHADERS.md; the Vulkan spec clamps fixed-point blend
// inputs). DF_PREMUL moves the multiply into the shader for the additive and
// subtractive forms (Cs*F + Cd, Cd - Cs*F): c0.rgb = min((Cs * f) >> 7, 255),
// exactly the GS term, and the pipeline blends with src factor ONE.
struct DualOut
{
    VK_DUAL(0, 0) float4 c0 : SV_Target0;
    VK_DUAL(0, 1) float4 c1 : SV_Target1;
};

DualOut gs_dual_out(uint4 col, uint flags, uint fix)
{
    uint a = col.a;
    if ((flags & DF_FBA) != 0u) {
        a |= 0x80u;
    }
    uint f = (flags & DF_FIX_FACTOR) != 0u ? fix : a;
    if ((flags & DF_PABE) != 0u && (a & 0x80u) == 0u) {
        f = 0u;
    }
    DualOut o;
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

// ------------------------------------------------------- vertex helpers

// A 12.4 GS window coordinate (as two u16 in a uint2) to clip space in the
// given space (SPACE_WORLD or SPACE_UI).
float2 gs_xy_to_ndc(uint2 xy, int space)
{
    float2 px = float2(xy) * (1.0 / 16.0) - g_origin.xy + g_origin.zw;
    float2 ndc = float2(px.x * g_target.z * 2.0 - 1.0, 1.0 - px.y * g_target.w * 2.0);
    return ndc * g_space[space].xy + g_space[space].zw;
}

float gs_depth(uint z)
{
    return gs_z_to_depth(z, g_z.x);
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
