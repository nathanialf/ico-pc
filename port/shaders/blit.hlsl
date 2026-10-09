// blit.hlsl: fullscreen copy with an optional tint, for the reduction, keep
// and fade passes. The destination rectangle is the viewport and scissor;
// the source rectangle is DrawCB.g_uvRect in texels of t1. No vertex buffer:
// draw 3 vertices.
//   blit_ps      single colour output; colour = texture function(texel, tint)
//   blit_depth_ps blit_ps plus SV_Depth from the scene's depth (the present's
//                effects depth)
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

// blit_depth_ps (rd_present.c): blit_ps's colour, and
// SV_Depth from t2, the scene's depth (sampled in place), at the same normalised uv
// (the source rectangle covers the whole scene; a mirrored blit flips both)
// read nearest: the texel under uv. Drawn into the output's box with an
// output-size depth target cleared to 0.0, so an effects program hooked into
// the API (ReShade) finds the scene's depth at the picture's pixels and far in
// the bars. Depth grows with GS Z, near 1 and far 0 (gs_z_to_depth):
// RESHADE_DEPTH_INPUT_IS_REVERSED=1.
Texture2D<float> g_sceneDepth : register(t2, space2);

struct BlitDepthOut
{
    VK_LOC(0) float4 c : SV_Target0;
    float d : SV_Depth;
};

BlitDepthOut blit_depth_ps(BlitVSOut i)
{
    BlitDepthOut o;
    o.c = float4(blit_color(i.uv)) * (1.0 / 255.0);
    uint w, h;
    g_sceneDepth.GetDimensions(w, h);
    int2 p = int2(floor(saturate(i.uv) * float2(w, h)));
    p = min(p, int2(int(w) - 1, int(h) - 1));
    o.d = g_sceneDepth.Load(int3(p, 0));
    return o;
}

// box_reduce_ps (rd_replay.c doShadowResolve): the exact box
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

// camera_probe_ps (tests only, rd__camera_probe): FrameCB's
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
