// fsr1.hlsl: AMD FidelityFX Super Resolution 1.0 at the present step
// (port/render/rd_fsr1.c drives it; Options > Picture > Upscaling).
//
//   fsr1_easu_ps  EASU, the edge-adaptive upscale: the present's source (the
//                 DISPLAY picture, line-doubled or full height, the columns
//                 past the exact width left out through the offset in the
//                 constants) into a texture of the presentation box's size
//   fsr1_rcas_ps  RCAS, the contrast-adaptive sharpening: that texture into a
//                 second of the same size, one pixel each
//
// Both behind blit_vs (the fullscreen triangle over the viewport, which is
// the whole target here), SV_Position as the integer output pixel. The
// algorithm is AMD's own, unmodified, from port/third_party/fsr1 (ffx_a.h,
// ffx_fsr1.h, MIT, Copyright (c) 2021 Advanced Micro Devices, Inc.; the
// licence is port/third_party/fsr1/LICENSE): this file only supplies the
// callbacks the header asks for. The 32-bit path (FSR_EASU_F, FSR_RCAS_F):
// no 16-bit types, so the SPIR-V stays Vulkan 1.0 core and the GLSL 430.
//
// Bindings: FsrCB (b1, space1: the DrawCB slot and its 144 bytes, so the
// passes run under the post pipelines' layouts and blit_vs reads DrawCB's
// aux1.w, the quarter turns, as 0; shader_consts.h IcoFsrCB), t1 the input,
// s1 bilinear and clamped (the gathers take the clamp; EASU's twelve taps
// past the input's edge read its edge). The constants are the CPU's
// (rd_fsr1.c: FsrEasuConOffset, FsrRcasCon). This file does not include
// common.hlsli, whose DrawCB sits in the same register.

#ifdef __spirv__
#define VK_LOC(n) [[vk::location(n)]]
#else
#define VK_LOC(n)
#endif

#include "reg.hlsli"

cbuffer FsrCB : ICO_REG(b1, 1)
{
    uint4 c_con0; // FsrEasuConOffset's four (IcoFsrCB.con0 .. con3)
    uint4 c_con1;
    uint4 c_con2;
    uint4 c_con3;
    uint4 c_rcas; // FsrRcasCon's (x: the sharpness as a float's bits)
    uint4 c_last; // x, y: the input's last column and row (RCAS's loads stay inside it)
};

Texture2D<float4> g_texture : ICO_REG(t1, 2);
SamplerState g_sampler : ICO_REG(s1, 2);

// blit_vs's output (blit.hlsl BlitVSOut), uv unused
struct FsrIn {
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

#define FSR_EASU_F 1
#define FSR_RCAS_F 1
#define FSR_RCAS_PASSTHROUGH_ALPHA 1

AF4 FsrEasuRF(AF2 p)
{
    return g_texture.GatherRed(g_sampler, p);
}

AF4 FsrEasuGF(AF2 p)
{
    return g_texture.GatherGreen(g_sampler, p);
}

AF4 FsrEasuBF(AF2 p)
{
    return g_texture.GatherBlue(g_sampler, p);
}

AF4 FsrRcasLoadF(ASU2 p)
{
    const int2 q = clamp(int2(p), int2(0, 0), int2(c_last.xy));
    return g_texture.Load(int3(q, 0));
}

void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b)
{
}

#include "ffx_fsr1.h"

float4 fsr1_easu_ps(FsrIn i) : SV_Target0
{
    const uint2 ip = uint2(i.pos.xy);
    float3 c;
    FsrEasuF(c, ip, c_con0, c_con1, c_con2, c_con3);
    // the alpha the plain box blit would have had: the input's, bilinear, at
    // the pixel's position (EASU filters colour alone)
    const float2 pp = float2(ip) * asfloat(c_con0.xy) + asfloat(c_con0.zw) + 0.5;
    const float a = g_texture.SampleLevel(g_sampler, pp * asfloat(c_con1.xy), 0.0).a;
    return float4(c, a);
}

float4 fsr1_rcas_ps(FsrIn i) : SV_Target0
{
    float4 c;
    FsrRcasF(c.r, c.g, c.b, c.a, uint2(i.pos.xy), c_rcas);
    return c;
}
