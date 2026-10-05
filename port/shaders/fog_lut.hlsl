// fog_lut.hlsl: WAVE 4 PLACEHOLDER. Depth-indexed fog (fog_DrawFog, ZFog.c):
// sample the scene depth, turn it into a 0..255 index, look the colour up in
// a 256x1 RGBA LUT (RD_POST_FOG, RdPostParams.lut) and output it dual-source
// with FIX = strength so a hardware blend applies it.
//   t1  scene depth, sampled as depth (RHI_ASPECT_DEPTH), R32F
//   t2  256x1 RGBA8 LUT
//   g_param.x  strength 0..255 (the blend FIX); g_param.y, z unused
// The index is the top byte of the 24-bit GS Z. Which byte the original
// reads is an open item (RENDER_API.md section 8 item 3), so this entry is
// not to be relied on before wave 4 resolves it.
#include "common.hlsli"

struct FogVSOut
{
    float4 pos : SV_Position;
};

FogVSOut fog_lut_vs(uint id : SV_VertexID)
{
    FogVSOut o;
    float2 t;
    fullscreen_triangle(id, o.pos, t);
    return o;
}

Texture2D<float4> g_depth : register(t1, space2);
Texture2D<float4> g_lut : register(t2, space2);

DualOut fog_lut_ps(FogVSOut i)
{
    float d = g_depth.Load(int3(int2(i.pos.xy), 0)).r;
    uint z = (uint)((1.0 - d) * 16777216.0 + 0.5);
    uint idx = min(z >> 16, 255u);
    uint4 col = uint4(floor(g_lut.Load(int3(idx, 0, 0)) * 255.0 + 0.5));
    col.a = g_blend.y;
    return gs_dual_out(col, DF_FIX_FACTOR, g_blend.y);
}
