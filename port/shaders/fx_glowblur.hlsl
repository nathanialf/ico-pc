// fx_glowblur.hlsl: the glow's blur chain in two passes (port/render/rd_glow_blur.c,
// rd_glow_blur.h; docs/port/RENDER_POST.md, "The glow's blur in two passes";
// DIVERGENCES.md VD-33).
//
//   fx_glowblur_ps  one axis of the chain's net operator: at each texel, the
//                   GB_TAPS hardware-bilinear taps of its column (across:
//                   WORK0 -> WORK1) or its row (down: WORK1 -> WORK0) that
//                   the CPU composed from the chain's twenty passes, summed
//                   in floats; down, times the chain's colour gains, less
//                   the loss model of its roundings (the column's and the
//                   row's, and the common part), at least 0.  Behind
//                   fx_rect_vs.  Plain filtering: a few taps and a table
//                   read a texel, no integer arithmetic of the GS.
//
// Bindings: t1 the source (WORK0 across, WORK1 down) with s1 the chain's
// sampler (LINEAR, CLAMP), t2 the table (RGBA8, read with Load): per axis
// GB_TAPS rows of taps at the texel's position along that axis (weight as
// 16 bits in R, G; offset + 64 in 1/256 texel as 16 bits in B, A), then
// the loss's whole levels (RGBA) and its 1/256ths; the across axis's rows
// first (rd_glow_blur.c rd__glow_blur_encode).
// DrawCB:
//   g_mode   x 0 across, 1 down; y the axis's first table row; z 1: the
//            loss subtracted (down); w the across axis's first row
//   g_tex    xy t1's texels, zw their reciprocals
//   g_param  the colour gains (RGBA; 1 across)
//   g_scale  the common loss (RGBA, levels / 255; 0 across)
#include "common.hlsli"

#define GB_TAPS 7 // rd_glow_blur.h RD_GLOW_BLUR_TAPS

Texture2D<float4> g_texture : ICO_REG(t1, 2);
SamplerState g_sampler : ICO_REG(s1, 2);
Texture2D<float4> g_table : ICO_REG(t2, 2);

// the table's texel as its bytes
uint4 gb_bytes(int x, int y)
{
    return uint4(floor(g_table.Load(int3(x, y, 0)) * 255.0 + 0.5));
}

// the loss (levels / 255) at position i of the axis whose loss rows start at row
float4 gb_loss(int i, int row)
{
    const float4 hi = float4(gb_bytes(i, row));
    const float4 lo = float4(gb_bytes(i, row + 1));
    return (hi + lo * (1.0 / 256.0)) * (1.0 / 255.0);
}

float4 fx_glowblur_ps(float4 pos : SV_Position) : SV_Target0
{
    const int2 px = int2(pos.xy);
    const bool down = g_mode.x != 0u;
    const int i = down ? px.y : px.x;
    const int rb = int(g_mode.y);
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < GB_TAPS; k++) {
        const uint4 t = gb_bytes(i, rb + k);
        const float w = float(t.x * 256u + t.y) * (1.0 / 65535.0);
        const float off = float(t.z * 256u + t.w) * (1.0 / 256.0) - 64.0;
        const float2 c = float2(px) + 0.5 + (down ? float2(0.0, off) : float2(off, 0.0));
        sum += w * g_texture.SampleLevel(g_sampler, c * g_tex.zw, 0.0);
    }
    float4 o = sum * g_param;
    if (g_mode.z != 0u) {
        o -= gb_loss(px.x, int(g_mode.w) + GB_TAPS) + gb_loss(px.y, rb + GB_TAPS) + g_scale;
    }
    return max(o, float4(0.0, 0.0, 0.0, 0.0));
}
