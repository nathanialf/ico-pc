// fog_lut.hlsl: the depth fog of fog_DrawFog (ico2/seki/src/ZFog.c),
// RD_POST_FOG.
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
//   t1  the Z source's depth target itself (D32F, sampled as depth;
//       rd_replay.c moves it to the depth-read state for the fog pass, which
//       binds no depth attachment), where the GS reads a copy of the Z buffer
//   t2  the 256x1 RGBA8 LUT in index order (ZFog.c's CLUT unswizzled from
//       its CSM1 storage order)
//   g_tex.xy   size of the depth source in GS pixels (the UV's texel units)
//   g_col.x    the sprite's GS Z
//   g_col.y    the Z test (RdZTest: ALWAYS 1, GEQUAL 2, GREATER 3, NEVER 0)
//   g_param.x  the depth source's GS Z scale (PSMZ32: 2^-33 on a float
//              depth buffer, 2^-32 on D24S8; rd__target_z_scale)
//   g_param.y  2^24 - 1 when the depth source is 24-bit UNORM (D24S8),
//              0 on a float one
//   g_param.z  1: the depth read is replaced by 0 (a test switch,
//              ICO_RD_FOG_SABOTAGE: the reads of a device that returns
//              nothing), else 0
// fog_lut_buffer_ps (FOG_BUFFER) reads t1 as an R32_UINT colour texture
// instead: the depth aspect copied out to a buffer and back into a colour
// texture (rd_fog_path.c), for a device whose depth reads fail.  The words
// are the depth's bits: a float on D32S8, and on D24S8 the 24-bit step in
// the low bits with the top byte undefined, so it is masked off.
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

#ifdef FOG_BUFFER
Texture2D<uint> g_fogDepth : register(t1, space2);
#else
Texture2D<float> g_fogDepth : register(t1, space2);
#endif
Texture2D<float4> g_fogLut : register(t2, space2);

#define FOG_ZTST_NEVER 0u
#define FOG_ZTST_ALWAYS 1u
#define FOG_ZTST_GEQUAL 2u
#define FOG_ZTST_GREATER 3u

// The GS Z a stored depth d stands for: the inverse of gs_z_to_depth, z =
// d / scale (a power of two: exact), and for PSMZ32 on a float depth buffer
// (GS_ZSCALE_32F) the top band [1 - 2^-8, 1) back to 0xFFFF0000 + its
// steps.  Z clamps to 2^32 - 256.
uint fog_gs_z(float d, float scale)
{
    if (scale < 1.5e-10 && d >= 1.0 - 1.0 / 256.0) {
        return 0xFFFF0000u + (uint)((d - (1.0 - 1.0 / 256.0)) * 16777216.0);
    }
    float zf = clamp(d / scale, 0.0, 4294967040.0);
    return (uint)zf;
}

DualOut fog_lut_ps(FogPSIn i)
{
    // the texel the sprite's UV addresses, nearest (ZFog.c writes TEX1 0);
    // in the depth target's own texels (g_scale, 1 in Original)
    float2 sc = float2(g_scale.x > 0.0 ? g_scale.x : 1.0, g_scale.y > 0.0 ? g_scale.y : 1.0);
    float2 fsize = g_tex.xy * sc;
    int2 size = int2(round(fsize));
    int2 tc = clamp(int2(floor(i.uv * fsize)), int2(0, 0), size - 1);
#ifdef FOG_BUFFER
    uint raw = g_fogDepth.Load(int3(tc, 0));
    // the step as the sampled UNORM depth gives it (raw / (2^24 - 1)), or
    // the float's own bits
    float d = g_param.y > 0.0 ? float(raw & 0xFFFFFFu) / 16777215.0 : asfloat(raw);
#else
    float d = g_fogDepth.Load(int3(tc, 0));
#endif
    if (g_param.z > 0.5) {
        d = 0.0;
    }
    uint ztst = g_col.y;
    uint z = fog_gs_z(d, g_param.x);
    if (ztst != FOG_ZTST_ALWAYS) {
        // GS GEQUAL (sprite Z >= buffer Z) is depth(sprite) >= depth(buffer)
        float ds = gs_z_to_depth(g_col.x, g_param.x);
        if (g_param.y > 0.0) {
            // a 24-bit depth buffer holds the step nearest each depth, so
            // the sprite's depth is compared as the step it would store, as
            // the GS compares integers: 0xFFFFFF at 2^-32 is step 65535.996,
            // and a pixel drawn at Z 0xFFFF80 .. 0xFFFFFF stores step 65536,
            // above the sprite's float depth, which failed GEQUAL
            ds = round(ds * g_param.y);
            d = round(d * g_param.y);
        }
        bool pass = ztst == FOG_ZTST_GEQUAL ? ds >= d : (ztst == FOG_ZTST_GREATER ? ds > d : false);
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
