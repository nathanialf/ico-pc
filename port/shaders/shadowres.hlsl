// shadowres.hlsl: the shadow's levels at the scene's scale above 1x
// (port/render/rd_shadowres.c, rd_shadowres.h; docs/port/RENDER_POST.md,
// "The shadow's levels above 1x";
// DIVERGENCES.md VD-34). Both behind blit_vs, one triangle over the target,
// no blend, RGBA8 in and out, read with Load.
//
//   shadowres_step_ps  one of the game's three level sprites (Shadow.c
//                      shadowHostChain) evaluated at every texel of a level
//                      at the scene's scale (the smallest odd whole one at or
//                      above it, rd_sr_level_scale): the GS's two taps 0.75 / 0.25
//                      one source texel of the PS2 apart on each axis, the
//                      first level's taps each the box of count texels its
//                      GS pixel covers (the box rd_replay.c's reduction
//                      averages), the later levels' taps the level before
//                      at that position (linear between its texels); the
//                      texel's bytes rounded, then MODULATE by the sprite's
//                      colour as sprite_ps does ((T C) >> 7, TCC RGBA).
//   shadowres_tent_ps  one axis of the tent a level's old bilinear
//                      magnification supplied: weights S - |t - c| over the
//                      texels t = n - m, c = S / 2 - 1/2 (the composite reads
//                      the level at the scene's scale where the PS2 read the
//                      GS texel's centre), normalised, the indices held to
//                      the level's edge; in bytes, rounded.
//
// Bindings: t1 the source (Load only; s1 bound, unused).
// DrawCB (rd_shadowres.c rd__shadow_fine_screen, tent):
//   step  g_mode.x 0 the first level (boxes of the count), 1 a later level
//         (points of the level before); g_mode.y bit 0 the sprite's S axis
//         REPEATs, bit 1 its T axis (else CLAMP: a tap past the source's edge
//         reads its edge, as the GS's sampler does); g_param.xy the source
//         texels a destination texel moves its taps by (2 S_src / S_dst), zw
//         the source texels of one PS2 texel (S_src: the second tap's offset
//         and the box's width); g_scale.xy the first tap's origin in source
//         texels (the sprite's whole-texel offset times S_src, less a Full
//         pixel count's left-out texels); g_uvRect.xy the second tap's weight
//         on each axis (the sprite's sub-texel offset: 0.25 for the game's
//         u = 2 x + 0.75; the first tap takes 1 - it); g_tex.xy the source's
//         size; g_col the sprite's colour (bytes)
//   tent  g_mode.x 0 across, 1 down; g_param.x S of that axis; g_tex.xy
//         the level's size (the indices are held inside it)
#include "common.hlsli"

Texture2D<float4> g_texture : ICO_REG(t1, 2);
SamplerState g_sampler : ICO_REG(s1, 2);

struct SrIn {
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

// the most source texels a step's box spans on an axis (two PS2 texels at
// a scale of 31) and the most taps of a tent (2 S + 2 at a scale of 31): a
// loop's end the GPU computed wrong cannot lengthen it
#define SR_SPAN_MAX 64
#define SR_TENT_MAX 64

// the box weight of source texel i: [a, a + s) at (1 - f) / s, [a + s,
// a + 2 s) at f / s
float sr_box_w(float i, float a, float s, float f)
{
    const float b = a + s;
    const float c = b + s;
    const float w0 = max(min(b, i + 1.0) - max(a, i), 0.0);
    const float w1 = max(min(c, i + 1.0) - max(b, i), 0.0);
    return ((1.0 - f) * w0 + f * w1) / s;
}

// an index of an axis of n texels: wrapped (repeat) or held to the edge
int sr_index(int i, int n, bool repeat)
{
    if (repeat) {
        const int r = i % n;
        return r < 0 ? r + n : r;
    }
    return clamp(i, 0, n - 1);
}

float4 sr_load(int x, int y)
{
    return g_texture.Load(int3(x, y, 0));
}

// the level before at source coordinate j (texel k at j = k), linear
// between its texels, the indices wrapped or held as the sprite's sampler
float4 sr_point(float2 j, bool rs, bool rt)
{
    const int2 n = int2(g_tex.xy);
    const float2 f0 = floor(j);
    const float2 f = j - f0;
    const int ax = sr_index(int(f0.x), n.x, rs), bx = sr_index(int(f0.x) + 1, n.x, rs);
    const int ay = sr_index(int(f0.y), n.y, rt), by = sr_index(int(f0.y) + 1, n.y, rt);
    const float4 top = sr_load(ax, ay) * (1.0 - f.x) + sr_load(bx, ay) * f.x;
    const float4 bot = sr_load(ax, by) * (1.0 - f.x) + sr_load(bx, by) * f.x;
    return top * (1.0 - f.y) + bot * f.y;
}

float4 shadowres_step_ps(SrIn i) : SV_Target0
{
    const float2 m = floor(i.pos.xy);
    const float2 a = m * g_param.xy + g_scale.xy;
    const float2 s = g_param.zw;
    const float2 f = g_uvRect.xy;
    const bool rs = (g_mode.y & 1u) != 0u;
    const bool rt = (g_mode.y & 2u) != 0u;
    const int2 n = int2(g_tex.xy);
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    if (g_mode.x == 0u) {
        const int2 first = int2(floor(a));
        const int2 last = min(int2(ceil(a + 2.0 * s)), first + int2(SR_SPAN_MAX, SR_SPAN_MAX));
        for (int y = first.y; y < last.y; y++) {
            const float wy = sr_box_w(float(y), a.y, s.y, f.y);
            const int yy = sr_index(y, n.y, rt);
            for (int x = first.x; x < last.x; x++) {
                sum += sr_load(sr_index(x, n.x, rs), yy) * (sr_box_w(float(x), a.x, s.x, f.x) * wy);
            }
        }
    } else {
        sum = sr_point(a, rs, rt) * ((1.0 - f.x) * (1.0 - f.y)) +
              sr_point(a + float2(s.x, 0.0), rs, rt) * (f.x * (1.0 - f.y)) +
              sr_point(a + float2(0.0, s.y), rs, rt) * ((1.0 - f.x) * f.y) +
              sr_point(a + s, rs, rt) * (f.x * f.y);
    }
    const uint4 t = uint4(floor(sum * 255.0 + 0.5));
    const uint4 o = min((t * g_col) >> 7, uint4(255u, 255u, 255u, 255u));
    return float4(o) * (1.0 / 255.0);
}

float4 shadowres_tent_ps(SrIn i) : SV_Target0
{
    const int2 p = int2(i.pos.xy);
    const bool down = g_mode.x != 0u;
    const float s = g_param.x;
    const float c = 0.5 * s - 0.5;
    const int n = down ? p.y : p.x;
    const int lim = (down ? int(g_tex.y) : int(g_tex.x)) - 1;
    const int t0 = int(floor(c - s));
    const int t1 = min(int(ceil(c + s)), t0 + SR_TENT_MAX);
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float wsum = 0.0;
    for (int t = t0; t <= t1; t++) {
        const float w = max(s - abs(float(t) - c), 0.0);
        const int k = clamp(n - t, 0, lim);
        const float4 e = floor(sr_load(down ? p.x : k, down ? k : p.y) * 255.0 + 0.5);
        sum += e * w;
        wsum += w;
    }
    return floor(sum / max(wsum, 1.0e-6) + 0.5) * (1.0 / 255.0);
}
