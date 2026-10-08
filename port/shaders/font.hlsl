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

// The coverage bytes of the 2x2 texels at q (x), q + (1, 0) (y),
// q + (0, 1) (z) and q + (1, 1) (w) of t1, in one gather (the sample point
// is the corner the four share, half a texel from any other footprint); 0
// outside the texture, whatever the sampler's address mode.
uint4 sheet_cov4(int2 q, int2 size)
{
    const float4 v = g_atlas.GatherRed(g_sampler, (float2(q) + 1.0) / float2(size));
    // Gather's order: x (0, 1), y (1, 1), z (1, 0), w (0, 0)
    uint4 c = uint4(floor(float4(v.w, v.z, v.x, v.y) * 255.0 + 0.5));
    const bool in0x = q.x >= 0 && q.x < size.x, in1x = q.x + 1 >= 0 && q.x + 1 < size.x;
    const bool in0y = q.y >= 0 && q.y < size.y, in1y = q.y + 1 >= 0 && q.y + 1 < size.y;
    c.x = in0x && in0y ? c.x : 0u;
    c.y = in1x && in0y ? c.y : 0u;
    c.z = in0x && in1y ? c.z : 0u;
    c.w = in1x && in1y ? c.w : 0u;
    return c;
}

// v0.4.2 (package F-G): the four texels at p0 .. p0 + (1, 1) of a strip
// rasterised at s texels a sheet texel (2..SHEET_SCALE_MAX).  The texture's
// top half is the coverage, its bottom half the rim rd_SheetRim made from
// it on the CPU (rd.h: the dilation with the falloff at distance / s, RX * s
// and RY * s texels across and down, which a pixel cannot afford to redo:
// 50 x 34 texels at 4x), each 0 outside its half.  The threshold is the
// Bayer entry of the sheet texel the texel lies in (an s x s cell: the
// grain is the sheets', not finer); the levels are the same.
void sheet_scaled(int2 p0, int2 size, uint4 style, uint dither, uint s, out float2 v[2][2])
{
    const int hh = size.y / 2;
    // the coverage and the rim of the same texels, each 0 outside its half
    // (the gather addresses the whole texture)
    uint4 c = sheet_cov4(p0, size), r = sheet_cov4(p0 + int2(0, hh), size);
    const bool in0 = p0.y >= 0 && p0.y < hh, in1 = p0.y + 1 >= 0 && p0.y + 1 < hh;
    c = uint4(in0 ? c.xy : uint2(0u, 0u), in1 ? c.zw : uint2(0u, 0u));
    r = uint4(in0 ? r.xy : uint2(0u, 0u), in1 ? r.zw : uint2(0u, 0u));
    const uint cc[2][2] = {{c.x, c.y}, {c.z, c.w}};
    const uint rr[2][2] = {{r.x, r.y}, {r.z, r.w}};
    [unroll] for (int ty = 0; ty < 2; ty++) {
        [unroll] for (int tx = 0; tx < 2; tx++) {
            // the sheet texel of texel p: floor(p / s), kept positive for
            // the division (the offset is a multiple of the Bayer cell)
            const int2 p = p0 + int2(tx, ty);
            const int2 q = (p + 4096 * (int)s) / (int)s - 4096;
            const uint2 st = sheet_texel(cc[ty][tx], rr[ty][tx], style, sheet_threshold(q, dither));
            v[ty][tx] = float2(st);
        }
    }
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
        // param.w: the dither bit, the strip's scale above it (F-G)
        const uint dither = style.w & 1u, scale = (style.w >> 1) + 1u;
        // the four sheet texels around the sample (a bilinear read with the
        // texel centres at +0.5), each rebuilt from the coverage grid
        const float2 pos = i.uv * float2(size) - 0.5;
        const float2 base = floor(pos);
        const float2 f = pos - base;
        const int2 p0 = int2(base);
        float2 v[2][2];
        if (scale > 1u) {
            sheet_scaled(p0, size, style, dither, min(scale, (uint)SHEET_SCALE_MAX), v);
        } else {
            // the grid is even on both sides (2 + 2 * reach): one gather a 2x2
            // block, a quarter of the reads of one Load a texel
            uint g[SHEET_GH][SHEET_GW];
            [unroll] for (int by = 0; by < SHEET_GH; by += 2) {
                [unroll] for (int bx = 0; bx < SHEET_GW; bx += 2) {
                    const uint4 c = sheet_cov4(p0 + int2(bx - SHEET_RX, by - SHEET_RY), size);
                    g[by][bx] = c.x;
                    g[by][bx + 1] = c.y;
                    g[by + 1][bx] = c.z;
                    g[by + 1][bx + 1] = c.w;
                }
            }
            // the rim's weighted dilation, separable (the weight is a product
            // and a max commutes with a non-negative factor, so this is
            // exactly the max over the box of c * wx * wy): each grid row's
            // largest c * wx for the two texel columns, then down with wy
            uint h[SHEET_GH][2];
            [unroll] for (int ry = 0; ry < SHEET_GH; ry++) {
                [unroll] for (int tx = 0; tx < 2; tx++) {
                    uint mx = 0u;
                    [unroll] for (int dx = 0; dx <= 2 * SHEET_RX; dx++) {
                        mx = max(mx, g[ry][tx + dx] * sheet_wx(abs(dx - SHEET_RX)));
                    }
                    h[ry][tx] = mx;
                }
            }
            [unroll] for (int ty = 0; ty < 2; ty++) {
                [unroll] for (int tx = 0; tx < 2; tx++) {
                    uint m = 0u;
                    [unroll] for (int dy = 0; dy <= 2 * SHEET_RY; dy++) {
                        m = max(m, h[ty + dy][tx] * sheet_wy(abs(dy - SHEET_RY)));
                    }
                    const uint r = sheet_rim(m);
                    const int2 p = p0 + int2(tx, ty);
                    const uint2 st = sheet_texel(g[ty + SHEET_RY][tx + SHEET_RX], r, style,
                                                 sheet_threshold(p, dither));
                    v[ty][tx] = float2(st);
                }
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
