// font.hlsl: R8 atlas text for the debug font and later the port's own font.
// Same vertex layout as sprite.hlsl (SpriteVertex): UV in atlas texels, RGBA
// the text colour with alpha 0x80 = 1.0. The atlas (t1, R8_UNORM) is
// coverage; the output alpha is vertex alpha * coverage / 255 and the
// pipeline blends with LERP_AS or the equivalent dual-source factor.
// Glyph quads are UI space.
#include "common.hlsli"

struct FontVSIn
{
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
};

struct FontVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) nointerpolation uint4 col : COLOR0;
    VK_LOC(1) float2 uv : TEXCOORD0;
};

FontVSOut font_vs(FontVSIn i)
{
    FontVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy, SPACE_UI), gs_depth(i.z), 1.0);
    o.col = i.col;
    o.uv = i.uv * g_tex.zw;
    return o;
}

Texture2D<float4> g_atlas : register(t1, space2);
SamplerState g_sampler : register(s1, space2);

DualOut font_ps(FontVSOut i)
{
    uint cov = (uint)floor(g_atlas.Sample(g_sampler, i.uv).r * 255.0 + 0.5);
    uint4 col = i.col;
    col.a = (col.a * cov) / 255u;
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}
