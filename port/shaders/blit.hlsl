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

// box_reduce_ps (package RSMALL; rd_replay.c doShadowResolve): the exact box
// average of a scaled target down to its GS size, t1 read with Load. The
// output pixel p averages the source texels under [p * f, (p + 1) * f), f =
// DrawCB.g_param.xy the source texels per output pixel (not necessarily an
// integer: a texel counts by its overlap), g_param.zw the source size in
// texels. Drawn with blit_vs into an RGBA8 target of the GS size, the
// viewport the target.
float4 box_reduce_ps(BlitVSOut i) : SV_Target0
{
    float2 lo = floor(i.pos.xy) * g_param.xy;
    float2 hi = lo + g_param.xy;
    int2 first = int2(floor(lo));
    int2 last = min(int2(ceil(hi)), int2(g_param.zw));
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = first.y; y < last.y; y++) {
        float wy = min(hi.y, float(y + 1)) - max(lo.y, float(y));
        for (int x = first.x; x < last.x; x++) {
            float wx = min(hi.x, float(x + 1)) - max(lo.x, float(x));
            sum += g_texture.Load(int3(x, y, 0)) * (wx * wy);
        }
    }
    return sum / (g_param.x * g_param.y);
}

// camera_probe_ps (wave 2, R2c; tests only, rd__CameraProbe): FrameCB's
// matrices applied to the point DrawCB.g_param, written as raw float bits so
// the HLSL column_major packing can be compared with the C side. Target 4 x 3
// RGBA8_UNORM, drawn with blit_vs: column = component, row 0 mul(g_view, p),
// row 1 mul(g_proj, mul(g_view, p)), row 2 mul(g_viewProj, p); each pixel's
// RGBA holds the float's bytes, least significant first (k / 255 stores k
// exactly in UNORM8).
float4 camera_probe_ps(BlitVSOut i) : SV_Target0
{
    uint2 px = uint2(i.pos.xy);
    float4 v = mul(g_view, g_param);
    float4 r = v;
    if (px.y == 1u) {
        r = mul(g_proj, v);
    } else if (px.y == 2u) {
        r = mul(g_viewProj, g_param);
    }
    float f = r.x;
    if (px.x == 1u) {
        f = r.y;
    } else if (px.x == 2u) {
        f = r.z;
    } else if (px.x == 3u) {
        f = r.w;
    }
    uint bits = asuint(f);
    return float4(float(bits & 0xFFu), float((bits >> 8) & 0xFFu), float((bits >> 16) & 0xFFu),
                  float(bits >> 24)) * (1.0 / 255.0);
}
