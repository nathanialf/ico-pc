// blend_int.hlsl: the GS integer blend between two RGBA8_UINT textures, for
// feedback passes (motion blur, aura, dissolve, flare accumulation).
// Fullscreen, 1:1 texels, no sampler.
//   t1  source Cs (RGBA8_UINT), alpha is As
//   t2  destination Cd (RGBA8_UINT)
//   target RGBA8_UINT, no hardware blending
// out = ((A - B) * C >> 7) + D with A, B, C, D from DrawCB.g_blend.x (the
// ALPHA register), FIX from g_blend.y, COLCLAMP from g_blend.z. g_param.xy
// is an integer pixel offset added to the target pixel for both reads.
#include "common.hlsli"

float4 blend_int_vs(uint id : SV_VertexID) : SV_Position
{
    float4 pos;
    float2 t;
    fullscreen_triangle(id, pos, t);
    return pos;
}

Texture2D<uint4> g_src : register(t1, space2);
Texture2D<uint4> g_dst : register(t2, space2);

uint4 blend_int_ps(float4 frag : SV_Position) : SV_Target0
{
    int2 p = int2(frag.xy) + int2(g_param.xy);
    uint4 cs = g_src.Load(int3(p, 0));
    uint4 cd = g_dst.Load(int3(p, 0));
    return gs_blend_int(cs, cd, g_blend.x, g_blend.y, g_blend.z);
}
