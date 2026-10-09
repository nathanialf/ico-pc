// yuv.hlsl: the FMV picture, 4:2:0 planes to RGB with the PS2 IPU's colour
// space conversion, drawn into the viewport (the picture's rectangle in the
// presenter's output box). port/render/rd_video.c.
//
// t1 (space2) is one R8 texture holding the three planes: Y in rows
// [0, h), Cb in rows [h, h + ch) columns [0, cw), Cr in the same rows from
// column cw. DrawCB:
//   g_tex.xy   picture size w, h (luma samples)
//   g_mode.x   cw (chroma columns), g_mode.y h (first chroma row)
//   g_param.x  1 = mirror (flip x)
//
// The CSC is the IPU's (CSC command, RGB32, no dither: libmpeg csc.c sends
// IPU_CMD 0x7 with DTE 0, OFM 0): ITU-R BT.601 limited range, coefficients
// in 1/64 units, Y below 16 clamped, chroma taken per 2 x 2 luma block
// without interpolation (the macroblock's own Cb/Cr sample), each channel
// rounded and clamped. The constants are PCSX2's IPU model
// (pcsx2/IPU/yuv2rgb.cpp, yuv2rgb_reference, commit 144a19ba): behaviour,
// not code, is taken from it.
//
// Scaling to the output is bilinear between converted samples: the four
// neighbours are converted exactly and blended, so at 1:1 the output is the
// IPU's value.
#include "common.hlsli"

struct YuvVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float2 t : TEXCOORD0;
};

YuvVSOut yuv_vs(uint id : SV_VertexID)
{
    YuvVSOut o;
    fullscreen_triangle(id, o.pos, o.t);
    return o;
}

Texture2D<float4> g_texture : register(t1, space2);

int plane_texel(int x, int y)
{
    return int(floor(g_texture.Load(int3(x, y, 0)).r * 255.0 + 0.5));
}

// The IPU conversion of the sample at luma (x, y), as 0..255 integers.
int3 ipu_csc(int x, int y)
{
    int w = int(g_tex.x);
    int h = int(g_tex.y);
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    int cw = int(g_mode.x);
    int ch0 = int(g_mode.y);
    int Y = plane_texel(x, y);
    int Cb = plane_texel(x >> 1, ch0 + (y >> 1)) - 128;
    int Cr = plane_texel(cw + (x >> 1), ch0 + (y >> 1)) - 128;
    int lum = (0x95 * max(0, Y - 16)) >> 6;
    int rcr = (0xCC * Cr) >> 6;
    int gcr = (-0x68 * Cr) >> 6;
    int gcb = (-0x32 * Cb) >> 6;
    int bcb = (0x102 * Cb) >> 6;
    return int3(clamp((lum + rcr + 1) >> 1, 0, 255), clamp((lum + gcr + gcb + 1) >> 1, 0, 255),
                clamp((lum + bcb + 1) >> 1, 0, 255));
}

float4 yuv_ps(YuvVSOut i) : SV_Target0
{
    float2 src = i.t * g_tex.xy;
    if (g_param.x != 0.0) {
        src.x = g_tex.x - src.x;
    }
    float2 p = src - 0.5;
    float2 f0 = floor(p);
    float2 f = p - f0;
    int x0 = int(f0.x);
    int y0 = int(f0.y);
    float3 c00 = float3(ipu_csc(x0, y0));
    float3 c10 = float3(ipu_csc(x0 + 1, y0));
    float3 c01 = float3(ipu_csc(x0, y0 + 1));
    float3 c11 = float3(ipu_csc(x0 + 1, y0 + 1));
    float3 c = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
    return float4(c * (1.0 / 255.0), 1.0);
}
