// sprite.hlsl: screen-space textured or untextured primitives in GS window
// coordinates (RD_PROG_SCREEN and RD_PROG_WORLD_PRIM): quads, strips,
// triangles, lines and points, whatever topology the pipeline sets.
//
// Vertex layout (IcoSpriteVertex in shader_consts.h, 20 bytes):
//   loc 0  RHI_VTX_U16x2_UINT   XY, GS 12.4 fixed point
//   loc 1  RHI_VTX_U32x1        Z, GS value (PSMZ24 scale unless FrameCB.g_z says otherwise)
//   loc 2  RHI_VTX_U8x4_UINT    RGBA 0..255, alpha 0x80 = 1.0
//   loc 3  RHI_VTX_F32x2        UV in texels of t1
// Entries: sprite_ui_vs and sprite_world_vs differ only in the FrameCB.g_space
// slot they apply; sprite_ps serves both.
//
// sprite_aa1_ui_vs, sprite_aa1_world_vs and sprite_aa1_ps draw the lines
// and triangles PRIM.AA1 antialiases. Their vertex (IcoSpriteAa1Vertex,
// 24 bytes) adds
//   loc 4  RHI_VTX_F32x1        the coverage: 0..1 on the geometry
//                               rd_replay.c adds along an edge, interpolated
//                               without perspective; ICO_AA1_INTERIOR (2.0)
//                               on a triangle's own vertices
// and sprite_aa1_ps takes the coverage as the fragment's alpha.
#include "common.hlsli"

struct SpriteVSIn
{
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
};

struct SpriteVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0; // 0..255, interpolated
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised
};

SpriteVSOut sprite_vertex(SpriteVSIn i, int space)
{
    SpriteVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy, space), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.uv = i.uv * g_tex.zw;
    return o;
}

SpriteVSOut sprite_ui_vs(SpriteVSIn i)
{
    return sprite_vertex(i, SPACE_UI);
}

SpriteVSOut sprite_world_vs(SpriteVSIn i)
{
    return sprite_vertex(i, SPACE_WORLD);
}

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
// The DATE snapshot: R8, 1.0 where the bound target's alpha MSB
// was set, addressed in target pixels. Read only under DF_DATE; otherwise a
// 1x1 dummy is bound and never fetched.
Texture2D<float> g_dateSnap : register(t2, space2);

DualOut sprite_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, gs_block_uv(i.uv)) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// sprite_texa_ps: sprite_ps for a PSMCT24 or PSMCT16 texture
// (or a 24- or 16-bit CLUT) under a TEXA with AEM and a linear filter, which
// the planner (rd_pipeline.c rd__TexaPerTexel) gives this entry: TEXA per
// texel before the bilinear weights, as the GS (gs_texa_texture). sprite_ps
// itself is unchanged.
DualOut sprite_texa_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = gs_texa_texture(g_texture, g_sampler, gs_block_uv(i.uv), g_mode.x);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// ------------------------------------------------------------------- PRIM.AA1

// sprite_ps's DATE, texture function and TEXA: the fragment's colour before
// the alpha test, or a discard. (sprite_ps keeps its own copy, so that its
// SPIR-V and DXIL stay what they were.)
uint4 sprite_colour(float4 pos, float4 vcol, float2 uv)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(vcol + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, gs_block_uv(uv)) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    return col;
}

struct SpriteAa1VSIn
{
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
    VK_LOC(4) float cov : TEXCOORD2;
};

struct SpriteAa1VSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;
    VK_LOC(1) float2 uv : TEXCOORD0;
    VK_LOC(2) noperspective float cov : TEXCOORD1;
};

SpriteAa1VSOut sprite_aa1_vertex(SpriteAa1VSIn i, int space)
{
    SpriteAa1VSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy, space), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.uv = i.uv * g_tex.zw;
    o.cov = i.cov;
    return o;
}

SpriteAa1VSOut sprite_aa1_ui_vs(SpriteAa1VSIn i)
{
    return sprite_aa1_vertex(i, SPACE_UI);
}

SpriteAa1VSOut sprite_aa1_world_vs(SpriteAa1VSIn i)
{
    return sprite_aa1_vertex(i, SPACE_WORLD);
}

// The coverage alpha: a = the coverage on the
// 0x80 scale for an edge pixel (16-bit coverage >> 9, so 0..0x7F), 0x80 for
// an interior one. With PRIM.ABE 0 (DF_AA1_FULL) a replaces the fragment's
// alpha; with ABE 1 it replaces it only where that alpha is exactly 0x80.
// This happens before the alpha test, and the alpha that results is the
// blend's As and the alpha written.
DualOut sprite_aa1_ps(SpriteAa1VSOut i)
{
    uint4 col = sprite_colour(i.pos, i.col, i.uv);
    uint a = 0x80u;
    if (i.cov < 1.5) {
        a = uint(floor(saturate(i.cov) * 65535.0)) >> 9;
    }
    if ((g_mode.x & DF_AA1_FULL) != 0u || col.a == 0x80u) {
        col.a = a;
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// ------------------------------------------------------------ perspective STQ

// The vertex of sprite_stq_*_vs (IcoSpriteStqVertex, 24 bytes) adds
//   loc 4  RHI_VTX_F32x1   Q
// to the four of SpriteVSIn; loc 3 holds S and T in texels of t1 (times the
// texture size, not divided by Q). The GS interpolates S, T and Q in screen
// space and divides per pixel: the vertex shader passes (s, t, q) without
// perspective, sprite_stq_ps divides. Colour stays screen-space linear as
// everywhere. Drawn only for the screen prims whose Q is not 1; every other prim keeps sprite_ps.
struct SpriteStqVSIn
{
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
    VK_LOC(4) float q : TEXCOORD2;
};

struct SpriteStqVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;
    VK_LOC(1) noperspective float3 stq : TEXCOORD0; // s, t normalised, q
};

SpriteStqVSOut sprite_stq_vertex(SpriteStqVSIn i, int space)
{
    SpriteStqVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy, space), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.stq = float3(i.uv * g_tex.zw, i.q);
    return o;
}

SpriteStqVSOut sprite_stq_ui_vs(SpriteStqVSIn i)
{
    return sprite_stq_vertex(i, SPACE_UI);
}

SpriteStqVSOut sprite_stq_world_vs(SpriteStqVSIn i)
{
    return sprite_stq_vertex(i, SPACE_WORLD);
}

DualOut sprite_stq_ps(SpriteStqVSOut i)
{
    uint4 col = sprite_colour(i.pos, i.col, i.stq.xy / i.stq.z);
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// date_snap_ps: the bound target's alpha MSB into the R8 DATE
// snapshot, pixel for pixel (t1 is the target, read with Load; viewport =
// the target's size). Drawn with blit_vs.
struct DateSnapIn
{
    float4 pos : SV_Position;
};

float date_snap_ps(DateSnapIn i) : SV_Target0
{
    float a = g_texture.Load(int3(int2(i.pos.xy), 0)).a;
    return a * 255.0 >= 127.5 ? 1.0 : 0.0;
}
