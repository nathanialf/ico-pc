// sprite.hlsl: screen-space textured or untextured primitives in GS window
// coordinates (RD_PROG_SCREEN and RD_PROG_WORLD_PRIM): quads, strips,
// triangles, lines and points, whatever topology the pipeline sets.
//
// Vertex layout (SpriteVertex in shader_consts.h, 20 bytes):
//   loc 0  RHI_VTX_U16x2_UINT   XY, GS 12.4 fixed point
//   loc 1  RHI_VTX_U32x1        Z, GS value (PSMZ24 scale unless FrameCB.g_z says otherwise)
//   loc 2  RHI_VTX_U8x4_UINT    RGBA 0..255, alpha 0x80 = 1.0
//   loc 3  RHI_VTX_F32x2        UV in texels of t1
// Entries: sprite_ui_vs and sprite_world_vs differ only in the FrameCB.g_space
// slot they apply; sprite_ps serves both.
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
// The DATE snapshot (wave 2): R8, 1.0 where the bound target's alpha MSB
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
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, i.uv) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// date_snap_ps (wave 2): the bound target's alpha MSB into the R8 DATE
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
