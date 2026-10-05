// fog_lut.hlsl: the depth fog of fog_DrawFog (ico2/seki/src/ZFog.c),
// RD_POST_FOG (renderer wave 4, R4c; docs/port/RENDER_API.md "Depth fog").
//
// What the GS does: the Z buffer (PSMZ32) is copied pixel for pixel into a
// PSMCT32 buffer, a PSMT4 view of that buffer copies the third byte of every
// 32-bit word into the fourth, and the fog sprite reads the buffer as PSMT8H
// (the top byte of each PSMCT32 word) through the 256-entry fog CLUT, with
// MODULATE by (0x80, 0x80, 0x80, fogStrength), ALPHA 0x44 and a Z test
// GEQUAL at sprite Z 0xFFFFFF under ZMSK. So the CLUT index is bits 16..23
// of the 32-bit Z, and only pixels with Z <= 0xFFFFFF are fogged.
//
// fog_lut_ps runs behind sprite_ui_vs / sprite_world_vs (the fog sprite's
// two corners, expanded by rd_replay.c) and replaces the texture fetch:
//   t1  a copy of the Z source's depth (D32F, sampled as depth; rd_replay.c
//       copies it, as the GS copies the Z buffer before reading it)
//   t2  the 256x1 RGBA8 LUT in index order (ZFog.c's CLUT unswizzled from
//       its CSM1 storage order)
//   g_tex.xy   size of the depth source in GS pixels (the UV's texel units)
//   g_col.x    the sprite's GS Z
//   g_col.y    the Z test (RdZTest: ALWAYS 1, GEQUAL 2, GREATER 3, NEVER 0)
//   g_param.x  the depth source's GS Z scale (2^-32 for PSMZ32)
// The Z test is done here because the depth buffer being read cannot also be
// the bound attachment; it compares the same depth values the pipeline
// would (gs_z_to_depth of the sprite Z against the stored depth).
#include "common.hlsli"

// The sprite vertex shaders' output (sprite.hlsl SpriteVSOut).
struct FogPSIn
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;   // 0..255
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised by g_tex.zw
};

// A fullscreen triangle with the same outputs (vertex colour 0x80 grey);
// rd draws the fog with the sprite vertex shaders, this entry stays for
// the shader table.
FogPSIn fog_lut_vs(uint id : SV_VertexID)
{
    FogPSIn o;
    float2 t;
    fullscreen_triangle(id, o.pos, t);
    o.col = float4(128.0, 128.0, 128.0, 128.0);
    o.uv = t;
    return o;
}

Texture2D<float> g_fogDepth : register(t1, space2);
Texture2D<float4> g_fogLut : register(t2, space2);

#define FOG_ZTST_NEVER 0u
#define FOG_ZTST_ALWAYS 1u
#define FOG_ZTST_GEQUAL 2u
#define FOG_ZTST_GREATER 3u

// The GS Z a stored depth d stands for: the inverse of gs_z_to_depth,
// z = (zmax + 1) - d / scale. With scale a power of two the division is
// exact; at 2^-32 a depth near 1 carries Z to a multiple of 256 (the float's
// step there), and the subtraction from 2^32 is exact for z <= 2^31. Z
// above 2^32 - 256 clamps.
uint fog_gs_z(float d, float scale)
{
    float v = d / scale;
    float zf = 1.0 / scale - v;
    zf = clamp(zf, 0.0, 4294967040.0);
    return (uint)zf;
}

DualOut fog_lut_ps(FogPSIn i)
{
    // the texel the sprite's UV addresses, nearest (ZFog.c writes TEX1 0);
    // R7a: in the depth copy's own texels (g_scale, 1 in Original)
    float2 sc = float2(g_scale.x > 0.0 ? g_scale.x : 1.0, g_scale.y > 0.0 ? g_scale.y : 1.0);
    float2 fsize = g_tex.xy * sc;
    int2 size = int2(round(fsize));
    int2 tc = clamp(int2(floor(i.uv * fsize)), int2(0, 0), size - 1);
    float d = g_fogDepth.Load(int3(tc, 0));
    uint ztst = g_col.y;
    uint z = fog_gs_z(d, g_param.x);
    if (ztst != FOG_ZTST_ALWAYS) {
        // GS GEQUAL (sprite Z >= buffer Z) is depth(sprite) <= depth(buffer)
        float ds = gs_z_to_depth(g_col.x, g_param.x);
        bool pass = ztst == FOG_ZTST_GEQUAL ? d >= ds : (ztst == FOG_ZTST_GREATER ? d > ds : false);
        if (!pass) {
            discard;
        }
        // a pixel that passed has a Z at most the sprite's (GEQUAL) or below
        // it (GREATER): where the depth step rounds Z up past it (0xFFFFFF
        // stores as 2^24), take the bound
        uint zs = ztst == FOG_ZTST_GEQUAL ? g_col.x : g_col.x - 1u;
        z = min(z, zs);
    }
    // PSMT8H after the PSMT4 byte copy: bits 16..23 of the GS Z
    uint idx = (z >> 16) & 0xFFu;
    uint4 t = uint4(floor(g_fogLut.Load(int3(int(idx), 0, 0)) * 255.0 + 0.5));
    uint4 col = uint4(floor(i.col + 0.5));
    col = gs_texture_function(t, col, g_mode.x);
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}
