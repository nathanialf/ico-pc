// font.hlsl: font_ps, screen prims that sample an R8 coverage texture
// (rd_CreateTextureR8: the port's font atlas pages, port/ui/font.c).
// rd__PlanScreenDraw selects it (RD_FS_FONT) for the bound texture's format
// behind sprite_ui_vs or sprite_world_vs, with sprite_ps's bind groups: the
// atlas at t1 with its sampler, the DATE snapshot at t2.
//
// The texel is the alpha the RGBA8 atlas held before package R8 (coverage
// in GS units, 0x80 = full) and stands for a white texel with that alpha:
// read back as sprite_ps reads a texel, then sprite_ps's texture function,
// TCC, alpha test, DATE and dual-source output, so the bytes are those of
// the RGBA8 path, filtered or not (the sampler filters the same UNORM8
// values).
//
// font_sheet_ps (v0.4.2, package F-A): screen and overlay prims that sample
// a sheet texture (rd_CreateTextureSheet, RD_FS_FONT_SHEET): R8 coverage
// 0..255 at the same t1, the style in g_param (rimOn, rimLevel, fillLevel,
// dither, as rd_replay.c writes them).  rd.h says what it draws;
// sheet_text.hlsli holds the texel arithmetic.
#include "common.hlsli"
#include "sheet_text.hlsli"

// The vertex shader's output: must match sprite.hlsl's SpriteVSOut, which
// sprite_ui_vs and sprite_world_vs write.
struct SpriteVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0; // 0..255, interpolated
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised
};

Texture2D<float4> g_atlas : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<float> g_dateSnap : register(t2, space2);

DualOut font_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint a = (uint)floor(g_atlas.Sample(g_sampler, i.uv).r * 255.0 + 0.5);
        col = gs_texture_function(uint4(255u, 255u, 255u, a), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// The coverage byte at texel p of t1; 0 outside the texture.
uint sheet_cov(int2 p, int2 size)
{
    if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y) {
        return 0u;
    }
    return (uint)floor(g_atlas.Load(int3(p, 0)).r * 255.0 + 0.5);
}

DualOut font_sheet_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint tw, th;
        g_atlas.GetDimensions(tw, th);
        const int2 size = int2((int)tw, (int)th);
        const uint4 style = uint4(g_param + 0.5);
        // the four sheet texels around the sample (a bilinear read with the
        // texel centres at +0.5), each rebuilt from the coverage grid
        const float2 pos = i.uv * float2(size) - 0.5;
        const float2 base = floor(pos);
        const float2 f = pos - base;
        const int2 p0 = int2(base);
        uint g[SHEET_GH][SHEET_GW];
        [unroll] for (int gy = 0; gy < SHEET_GH; gy++) {
            [unroll] for (int gx = 0; gx < SHEET_GW; gx++) {
                g[gy][gx] = sheet_cov(p0 + int2(gx - SHEET_RX, gy - SHEET_RY), size);
            }
        }
        float2 v[2][2];
        [unroll] for (int ty = 0; ty < 2; ty++) {
            [unroll] for (int tx = 0; tx < 2; tx++) {
                uint r = 0u;
                [unroll] for (int dy = 0; dy <= 2 * SHEET_RY; dy++) {
                    [unroll] for (int dx = 0; dx <= 2 * SHEET_RX; dx++) {
                        r = max(r, g[ty + dy][tx + dx]);
                    }
                }
                const int2 p = p0 + int2(tx, ty);
                const uint2 st = sheet_texel(g[ty + SHEET_RY][tx + SHEET_RX], r, style,
                                             sheet_threshold(p, style.w));
                v[ty][tx] = float2(st);
            }
        }
        const float2 m = lerp(lerp(v[0][0], v[0][1], f.x), lerp(v[1][0], v[1][1], f.x), f.y);
        const uint2 gm = uint2(floor(m + 0.5));
        col = gs_texture_function(uint4(gm.x, gm.x, gm.x, gm.y), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}
