// font.hlsl: font_ps, screen prims that sample an R8 coverage texture
// (rd_create_texture_r8: the port's font atlas pages, port/ui/font.c).
// rd__plan_screen_draw selects it (RD_FS_FONT) for the bound texture's format
// behind sprite_ui_vs or sprite_world_vs, with sprite_ps's bind groups: the
// atlas at t1 with its sampler, the DATE snapshot at t2.
//
// The texel is the alpha an RGBA8 atlas would hold (coverage in GS units,
// 0x80 = full) and stands for a white texel with that alpha:
// read back as sprite_ps reads a texel, then sprite_ps's texture function,
// TCC, alpha test, DATE and dual-source output, so the bytes are those of
// the RGBA8 path, filtered or not (the sampler filters the same UNORM8
// values).
//
// font_sheet_ps: screen and overlay prims that sample
// a sheet texture (rd_create_texture_sheet, RD_FS_FONT_SHEET): R8 coverage
// 0..255 at the same t1, the style in g_param (the rim's weight 0..64,
// rimLevel, fillLevel, dither | (scale - 1) << 1, as rd_replay.c writes
// them).  rd.h says what it draws;
// sheet_text.hlsli holds the texel arithmetic.
//
// font_sheet_baked_ps: the same draws from a 1x sheet
// (RD_FS_FONT_SHEET_BAKED): the renderer computed every sheet texel on the
// CPU with font_sheet_ps's arithmetic (rd_tex.c rd__sheet_bake_rect) into an
// RGBA8 texture, (grey, grey, grey, alpha) a texel, the page's texels inside
// an apron of SHEET_BAKE_AX x SHEET_BAKE_AY texels (sheet_text.hlsli).  The
// shader reads the four texels font_sheet_ps rebuilds and blends them the same
// way, so its output is font_sheet_ps's at scale 1, byte for byte.
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

Texture2D<float4> g_atlas : ICO_REG(t1, 2);
SamplerState g_sampler : ICO_REG(s1, 2);
Texture2D<float> g_dateSnap : ICO_REG(t2, 2);

GS_PS_ATTR GS_PS_OUT font_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, GS_DATE_SNAP(g_dateSnap, int2(i.pos.xy)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint a = (uint)floor(g_atlas.Sample(g_sampler, i.uv).r * 255.0 + 0.5);
        col = gs_texture_function(uint4(255u, 255u, 255u, a), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
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
    // 0 <= v < n as one unsigned comparison (n >= 0: a negative v wraps
    // above it), the choices without a branch (common.hlsli ICO_SELECT: 35
    // calls of this in font_sheet_ps)
    const bool in0x = (uint)q.x < (uint)size.x, in1x = (uint)(q.x + 1) < (uint)size.x;
    const bool in0y = (uint)q.y < (uint)size.y, in1y = (uint)(q.y + 1) < (uint)size.y;
    c.x = ICO_SELECT(ICO_AND(in0x, in0y), c.x, 0u);
    c.y = ICO_SELECT(ICO_AND(in1x, in0y), c.y, 0u);
    c.z = ICO_SELECT(ICO_AND(in0x, in1y), c.z, 0u);
    c.w = ICO_SELECT(ICO_AND(in1x, in1y), c.w, 0u);
    return c;
}

// The four texels at p0 ..
// p0 + (1, 1) of a strip rasterised at s texels a sheet texel
// (2..SHEET_SCALE_MAX).  The texture's top half is the coverage, its
// bottom half the rim rd_sheet_rim made on the CPU (rd.h: the 1x dilation of
// each sheet texel's mean coverage, its value over the sheet texel's s x s
// texels), each 0 outside its half.  The rim is drawn as a sheet drew it:
// each sheet texel quantised against its own Bayer entry, then magnified
// as blocks (each texel takes the level of the sheet texel it lies in), so
// the outline's solid band and its dither's grain are the sheet texel's at
// any scale; the letters are the fine coverage on top, quantised with the
// threshold of the same sheet texel.
void sheet_scaled(int2 p0, int2 size, uint4 style, uint dither, uint s, out float2 v[2][2])
{
    const int hh = size.y / 2;
    const int si = (int)s;
    // the coverage of the four texels, 0 outside the top half (the gather
    // addresses the whole texture)
    uint4 c = sheet_cov4(p0, size);
    const bool in0 = (uint)p0.y < (uint)hh, in1 = (uint)(p0.y + 1) < (uint)hh;
    c = uint4(ICO_SELECT(in0, c.x, 0u), ICO_SELECT(in0, c.y, 0u), ICO_SELECT(in1, c.z, 0u),
              ICO_SELECT(in1, c.w, 0u));
    const uint cc[2][2] = {{c.x, c.y}, {c.z, c.w}};
    // the rim of texel p is the level of the sheet texel it lies in,
    // q = floor(p / s) (kept positive for the division), magnified as
    // blocks: the band stays solid and the dither's grain stays the sheet
    // texel's at any scale (the user's pick, v0.11.1)
    [unroll] for (int ty = 0; ty < 2; ty++) {
        [unroll] for (int tx = 0; tx < 2; tx++) {
            const int2 p = p0 + int2(tx, ty);
            const int2 q = (p + 4096 * si) / si - 4096;
            const int2 t = q * si;
            const bool inside = ICO_AND((uint)t.x < (uint)size.x, (uint)t.y < (uint)hh);
            // the load's address clamped (it is made whether or not used)
            const int2 tc = clamp(t, int2(0, 0), int2(size.x - 1, hh - 1));
            const float rv = g_atlas.Load(int3(tc.x, hh + tc.y, 0)).r;
            const uint r = ICO_SELECT(inside, (uint)floor(rv * 255.0 + 0.5), 0u);
            const uint th = sheet_threshold(q, dither);
            const uint l = sheet_rim_level(r, style, th);
            const uint2 st = sheet_texel_scaled(cc[ty][tx], (float)l,
                                                (float)((l * 128u + 127u) / 255u), style, th);
            v[ty][tx] = float2(st);
        }
    }
}

GS_PS_ATTR GS_PS_OUT font_sheet_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, GS_DATE_SNAP(g_dateSnap, int2(i.pos.xy)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint tw, th;
        g_atlas.GetDimensions(tw, th);
        const int2 size = int2((int)tw, (int)th);
        const uint4 style = uint4(g_param + 0.5);
        // param.w: the dither bit, the strip's scale above it
        const uint dither = style.w & 1u, scale = (style.w >> 1) + 1u;
        // the four sheet texels around the sample (a bilinear read with the
        // texel centres at +0.5), each rebuilt from the coverage grid
        precise float2 pos = i.uv * float2(size) - 0.5;
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
        precise float2 m = lerp(lerp(v[0][0], v[0][1], f.x), lerp(v[1][0], v[1][1], f.x), f.y);
        const uint2 gm = uint2(floor(m + 0.5));
        col = gs_texture_function(uint4(gm.x, gm.x, gm.x, gm.y), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}

// The baked texel (grey, alpha) at page texel p of a texture dim texels in
// all (the page and its apron), the read clamped to the texture: outside the
// apron every texel is the outermost one's (no coverage, no rim).
float2 sheet_baked_texel(int2 p, int2 dim)
{
    const int2 q = clamp(p + int2(SHEET_BAKE_AX, SHEET_BAKE_AY), int2(0, 0), dim - 1);
    const float4 t = g_atlas.Load(int3(q, 0));
    return float2(floor(t.x * 255.0 + 0.5), floor(t.w * 255.0 + 0.5));
}

GS_PS_ATTR GS_PS_OUT font_sheet_baked_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, GS_DATE_SNAP(g_dateSnap, int2(i.pos.xy)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint tw, th;
        g_atlas.GetDimensions(tw, th);
        const int2 dim = int2((int)tw, (int)th);
        // the page's size, which the UVs are normalised by (font_sheet_ps's
        // size)
        const int2 size = dim - 2 * int2(SHEET_BAKE_AX, SHEET_BAKE_AY);
        // font_sheet_ps's bilinear read: the same position, weights and blend
        // (pos and m precise in both, so no driver contracts one shader's
        // mul/sub or lerp into an FMA and not the other's)
        precise float2 pos = i.uv * float2(size) - 0.5;
        const float2 base = floor(pos);
        const float2 f = pos - base;
        const int2 p0 = int2(base);
        const float2 v00 = sheet_baked_texel(p0, dim);
        const float2 v01 = sheet_baked_texel(p0 + int2(1, 0), dim);
        const float2 v10 = sheet_baked_texel(p0 + int2(0, 1), dim);
        const float2 v11 = sheet_baked_texel(p0 + int2(1, 1), dim);
        precise float2 m = lerp(lerp(v00, v01, f.x), lerp(v10, v11, f.x), f.y);
        const uint2 gm = uint2(floor(m + 0.5));
        col = gs_texture_function(uint4(gm.x, gm.x, gm.x, gm.y), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}
