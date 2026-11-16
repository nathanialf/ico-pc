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
//
// shadowres_step_stencil_ps is shadowres_step_ps built with
// SHADOWRES_FROM_STENCIL (rd_shadowres.c, "The count from the stencil"):
// the first level's step alone, reading the count's texels where the
// full-size count pass (rd_shadow_one.c fullPass) would have written them:
// texel (x, y) is 0 outside the texel box g_aux0 (x0, y0, x1, y1, ends
// excluded: the pass's scissor, its clear outside it) and inside it the
// count's table texel of the stencil's bits 0-5 there (shadow_count_ps at a
// box of one texel: table[m] * (1 * 1) / (1 * 1) stored to RGBA8 is
// table[m]'s bytes, so the count texel's float is the table texel's float,
// both an RGBA8_UNORM texel of those bytes read with Load).  So sr_load
// returns the float the count texel holds and every other operation of the
// step is the same text: the same bytes.  Bindings: t1 the depth-stencil's
// stencil view (RHI_ASPECT_STENCIL, its channel g_mode.w: 0 red, 1 green),
// t2 the count's table (64 x 1), t3 the step's empty-area mask (g_mode.z).
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

SamplerState g_sampler : ICO_REG(s1, 2);
#ifdef SHADOWRES_FROM_STENCIL
Texture2D<uint4> g_srStencil : ICO_REG(t1, 2);
Texture2D<float4> g_srTable : ICO_REG(t2, 2);
// the step's empty-area mask (g_mode.z 1), as g_mask below
Texture2D<float4> g_mask : ICO_REG(t3, 2);
#else
Texture2D<float4> g_texture : ICO_REG(t1, 2);
// the step's empty-area mask (g_mode.z 1): the count box-reduced to its GS
// size (rd_replay.c's reduced count), read with Load
Texture2D<float4> g_mask : ICO_REG(t2, 2);
#endif

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
#ifdef SHADOWRES_FROM_STENCIL
    // the full-size count's texel (x, y) (the head comment): 0 outside the
    // box its pass drew, else the table texel of the stencil's bits 0-5
    if (x < int(g_aux0.x) || y < int(g_aux0.y) || x >= int(g_aux0.z) || y >= int(g_aux0.w)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint4 s = g_srStencil.Load(int3(x, y, 0));
    const uint m = (g_mode.w != 0u ? s.y : s.x) & 0x3Fu;
    return g_srTable.Load(int3(int(m), 0, 0));
#else
    return g_texture.Load(int3(x, y, 0));
#endif
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
    if (g_mode.x == 0u && g_mode.z != 0u) {
        // The empty area (rd_shadowres.c "The cuts"): the count texels the taps
        // touch, floor(a) .. ceil(a + 2 s) - 1, overlap the GS pixels g0..g1
        // (texel i covers [i, i + 1), GS pixel g [g S - ox, (g + 1) S - ox),
        // ox g_scale.z, S = s; none past the count's texels, where a tap wraps
        // or is held); a GS pixel's reduced
        // alpha is 0 only when every count texel overlapping it is 0 (a
        // nonzero texel's alpha is 0x80 and overlaps one of its GS pixels by
        // at least a quarter of a texel: 0x20 / (S.x S.y) > 0.5 below a scale
        // product of 64, which the CPU checks).  All zero: every tap is 0, the
        // sum 0, the texel 0, as the loop below would give.  Taken only where
        // g0..g1 lies inside the mask (no wrap or hold at its edge)
        const float2 o = float2(g_scale.z, 0.0);
        const int2 g0 = int2(floor((floor(a) + o) / s));
        const int2 g1 = int2(floor((ceil(a + 2.0 * s) - 1.0e-3 + o) / s));
        const int2 gm = int2(g_uvRect.zw);
        const float2 t0 = floor(a), t1 = ceil(a + 2.0 * s);
        if (t0.x >= 0.0 && t0.y >= 0.0 && t1.x <= g_tex.x && t1.y <= g_tex.y && g0.x >= 0 &&
            g0.y >= 0 && g1.x < gm.x && g1.y < gm.y && g1.x - g0.x < 4 && g1.y - g0.y < 4) {
            float occ = 0.0;
            for (int gy = g0.y; gy <= g1.y; gy++) {
                for (int gx = g0.x; gx <= g1.x; gx++) {
                    occ += g_mask.Load(int3(gx, gy, 0)).a;
                }
            }
            if (occ == 0.0) {
                return float4(0.0, 0.0, 0.0, 0.0);
            }
        }
    }
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

// shadowres_tent2_ps: the tent across and down in one pass, exactly the two
// passes of shadowres_tent_ps (rd_shadowres.c "The cuts"): at an odd whole
// scale L the weights are the integers L - |t - c| summing to L^2, the
// across pass's float sum of bytes times weights is that integer sum S
// exactly, and floor(S / W + 0.5) is S / W rounded half up in integers
// because an odd W never puts S / W on a half (it is at least 1 / (2 W) from
// one, far above the division's error); the across value the down pass
// reads back from RGBA8 is that byte.  So each of the down pass's rows is
// computed here as the across pass stored it, in integers, then combined
// the same way.  g_param.xy the scale of each axis, g_tex.xy the level's
// size; the indices held to its edge as both passes hold them.
uint4 sr_tent_row(int x, int k, int lx, int limx)
{
    const int cx = (lx - 1) / 2;
    uint4 sum = uint4(0u, 0u, 0u, 0u);
    for (int t = cx - lx + 1; t <= cx + lx - 1; t++) {
        const uint w = uint(lx - abs(t - cx));
        const int kx = clamp(x - t, 0, limx);
        sum += uint4(floor(sr_load(kx, k) * 255.0 + 0.5)) * w;
    }
    const uint wsum = uint(lx * lx);
    return (2u * sum + wsum) / (2u * wsum);
}

float4 shadowres_tent2_ps(SrIn i) : SV_Target0
{
    const int2 p = int2(i.pos.xy);
    const int lx = int(g_param.x + 0.5);
    const int ly = int(g_param.y + 0.5);
    const int limx = int(g_tex.x) - 1;
    const int limy = int(g_tex.y) - 1;
    const int cy = (ly - 1) / 2;
    uint4 sum = uint4(0u, 0u, 0u, 0u);
    for (int t = cy - ly + 1; t <= cy + ly - 1; t++) {
        const uint w = uint(ly - abs(t - cy));
        sum += sr_tent_row(p.x, clamp(p.y - t, 0, limy), lx, limx) * w;
    }
    const uint wsum = uint(ly * ly);
    return float4((2u * sum + wsum) / (2u * wsum)) * (1.0 / 255.0);
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
