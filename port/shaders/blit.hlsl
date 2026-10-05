// blit.hlsl: fullscreen copy with an optional tint, for the reduction, keep
// and fade passes. The destination rectangle is the viewport and scissor;
// the source rectangle is DrawCB.g_uvRect in texels of t1. No vertex buffer:
// draw 3 vertices.
//   blit_ps      single colour output; colour = texture function(texel, tint)
//   blit_fix_ps  dual-source output (factor from FIX or the texel alpha), for
//                passes that blend with LERP_FIX or LERP_AS in hardware
// Texture function flags are DrawCB.g_mode.x: with DF_TEXTURED clear the
// pass writes the tint colour alone (fade, letterbox).
#include "common.hlsli"

struct BlitVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

BlitVSOut blit_vs(uint id : SV_VertexID)
{
    BlitVSOut o;
    float2 t;
    fullscreen_triangle(id, o.pos, t);
    o.uv = (g_uvRect.xy + t * (g_uvRect.zw - g_uvRect.xy)) * g_tex.zw;
    return o;
}

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);

uint4 blit_color(float2 uv)
{
    uint4 col = g_col;
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, uv) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, g_col, g_mode.x);
    }
    return col;
}

float4 blit_ps(BlitVSOut i) : SV_Target0
{
    return float4(blit_color(i.uv)) * (1.0 / 255.0);
}

DualOut blit_fix_ps(BlitVSOut i)
{
    return gs_dual_out(blit_color(i.uv), g_mode.x, g_blend.y);
}
