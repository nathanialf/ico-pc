// yuv.hlsl: the FMV picture, 4:2:0 planes to RGB with the PS2 IPU's colour
// space conversion, drawn into the viewport (the picture's rectangle in the
// presenter's output box). port/render/rd_video.c.
//
// t1 (space2) is one R8 texture holding the three planes: Y in rows
// [0, h), Cb in rows [h, h + ch) columns [0, cw), Cr in the same rows from
// column cw. DrawCB:
//   g_tex.xy   picture size w, h (luma samples)
//   g_mode.x   cw (chroma columns), g_mode.y h (first chroma row)
//   g_mode.z   0, or with a field to show (g_mode.w bit 0) the rows one
//              picture takes (h + ch): the texture then holds three
//              pictures, the previous, the current and the next, each laid
//              out as above, at rows 0, g_mode.z and 2 g_mode.z
//   g_mode.w   bit 0 a field of the current picture is shown (the three
//              pictures are there); bit 1 its rows are the odd ones (the
//              bottom field); bit 2 it is the picture's second field in
//              time; bits 4-5 how the other rows are filled: 0 by the
//              deinterlacer (deint), 1 2 3 woven from the previous, the
//              next or the current picture (field matching)
//              bit 3 (yuv_ps only): t1 is not the planes but the field
//              already converted, w x h RGBA8 (yuv_field_ps's output)
//   g_param.x  1 = mirror (flip x)
//
// A field is two passes: yuv_field_ps converts the field at the picture's
// own size into an RGBA8 target (each output pixel one sample: the
// deinterlacer's reads are paid once per picture sample, not four times
// per output pixel), then yuv_ps scales that target as it scales the
// planes, the same bilinear blend between the same integer samples.
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

// --- a field of an interlaced picture --------------------------------------
//
// One plane of the three pictures: k 0 previous, 1 current, 2 next; the plane
// starts at column x0 and row r0 of its picture and is pw x n samples.  Rows
// past the plane's edge step back by two, staying in their field.
int field_texel(int k, int x, int y, int x0, int r0, int pw, int n)
{
    if (y < 0) {
        y += 2 * ((1 - y) / 2);
    } else if (y >= n) {
        y -= 2 * ((y - n) / 2 + 1);
    }
    x = clamp(x, 0, pw - 1);
    y = clamp(y, 0, n - 1);
    return plane_texel(x0 + x, k * int(g_mode.z) + r0 + y);
}

// The sample at (x, y) of one plane for the field shown: the field's own
// rows as decoded; the other rows woven from a matched picture, or
// deinterlaced the way yadif does it (FFmpeg vf_yadif, mode 0: an
// edge-directed interpolation within the field, limited by how much the
// missing row changes between the pictures around this field's instant),
// except that where nothing changes around the pixel in the three pictures
// (every temporal difference at most 2, codec noise) the missing row is
// woven: a still picture keeps its full detail and is exact.
int field_sample(int x, int y, int x0, int r0, int pw, int n)
{
    uint flags = g_mode.w;
    int keep = int((flags >> 1) & 1u);
    if ((y & 1) == keep) {
        return field_texel(1, x, y, x0, r0, pw, n);
    }
    uint match = (flags >> 4) & 3u;
    if (match != 0u) {
        int k = match == 1u ? 0 : match == 2u ? 2 : 1;
        return field_texel(k, x, y, x0, r0, pw, n);
    }
    // the pictures around this field's instant: the missing rows of the
    // first field fall between the previous picture and this one, those of
    // the second between this one and the next
    int kp = (flags & 4u) != 0u ? 1 : 0;
    int kn = kp + 1;
    int c = field_texel(1, x, y - 1, x0, r0, pw, n);
    int e = field_texel(1, x, y + 1, x0, r0, pw, n);
    int a2 = field_texel(kp, x, y, x0, r0, pw, n);
    int b2 = field_texel(kn, x, y, x0, r0, pw, n);
    int d = (a2 + b2) >> 1;
    int td0 = abs(a2 - b2);
    int td1 = (abs(field_texel(0, x, y - 1, x0, r0, pw, n) - c) +
               abs(field_texel(0, x, y + 1, x0, r0, pw, n) - e)) >> 1;
    int td2 = (abs(field_texel(2, x, y - 1, x0, r0, pw, n) - c) +
               abs(field_texel(2, x, y + 1, x0, r0, pw, n) - e)) >> 1;
    if (max(td0, max(td1, td2)) <= 2) {
        return d;
    }
    int diff = max(td0 >> 1, max(td1, td2));

    // the spatial prediction along the best of five directions
    int u[7];
    int v[7];
    for (int j = 0; j < 7; j++) {
        u[j] = field_texel(1, x - 3 + j, y - 1, x0, r0, pw, n);
        v[j] = field_texel(1, x - 3 + j, y + 1, x0, r0, pw, n);
    }
    // index 3 is x; score(j) compares u[x + j + i] with v[x - j + i], i -1..1
    int best = abs(u[2] - v[2]) + abs(c - e) + abs(u[4] - v[4]) - 1;
    int spred = (c + e) >> 1;
    int s = abs(u[1] - v[3]) + abs(u[2] - v[4]) + abs(u[3] - v[5]);
    if (s < best) {
        best = s;
        spred = (u[2] + v[4]) >> 1;
        s = abs(u[0] - v[4]) + abs(u[1] - v[5]) + abs(u[2] - v[6]);
        if (s < best) {
            best = s;
            spred = (u[1] + v[5]) >> 1;
        }
    }
    s = abs(u[3] - v[1]) + abs(u[4] - v[2]) + abs(u[5] - v[3]);
    if (s < best) {
        best = s;
        spred = (u[4] + v[2]) >> 1;
        s = abs(u[4] - v[0]) + abs(u[5] - v[1]) + abs(u[6] - v[2]);
        if (s < best) {
            best = s;
            spred = (u[5] + v[1]) >> 1;
        }
    }

    // yadif's spatial check: a missing row beyond both its neighbours, as
    // the rows two away are too, is detail, not motion
    int b = (field_texel(kp, x, y - 2, x0, r0, pw, n) + field_texel(kn, x, y - 2, x0, r0, pw, n)) >> 1;
    int f = (field_texel(kp, x, y + 2, x0, r0, pw, n) + field_texel(kn, x, y + 2, x0, r0, pw, n)) >> 1;
    int mx = max(max(d - e, d - c), min(b - c, f - e));
    int mn = min(min(d - e, d - c), max(b - c, f - e));
    diff = max(max(diff, mn), -mx);
    return clamp(spred, d - diff, d + diff);
}

// The IPU's CSC of one Y, Cb, Cr sample.
int3 ipu_csc_sample(int Y, int Cb, int Cr)
{
    Cb -= 128;
    Cr -= 128;
    int lum = (0x95 * max(0, Y - 16)) >> 6;
    int rcr = (0xCC * Cr) >> 6;
    int gcr = (-0x68 * Cr) >> 6;
    int gcb = (-0x32 * Cb) >> 6;
    int bcb = (0x102 * Cb) >> 6;
    return int3(clamp((lum + rcr + 1) >> 1, 0, 255), clamp((lum + gcr + gcb + 1) >> 1, 0, 255),
                clamp((lum + bcb + 1) >> 1, 0, 255));
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
    if ((g_mode.w & 1u) != 0u) {
        // a field: each plane on its own, chroma rows alternating between
        // the fields as luma rows do (4:2:0 in an interlaced frame)
        int ch = int(g_mode.z) - ch0;
        return ipu_csc_sample(field_sample(x, y, 0, 0, w, h),
                              field_sample(x >> 1, y >> 1, 0, ch0, cw, ch),
                              field_sample(x >> 1, y >> 1, cw, ch0, cw, ch));
    }
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

// A converted sample of yuv_field_ps's target, as 0..255 integers.
int3 field_rgb(int x, int y)
{
    x = clamp(x, 0, int(g_tex.x) - 1);
    y = clamp(y, 0, int(g_tex.y) - 1);
    return int3(floor(g_texture.Load(int3(x, y, 0)).rgb * 255.0 + 0.5));
}

// The sample yuv_ps blends: converted here from the planes, or by
// yuv_field_ps before.
int3 picture_rgb(int x, int y)
{
    if ((g_mode.w & 8u) != 0u) {
        return field_rgb(x, y);
    }
    return ipu_csc(x, y);
}

// A field converted 1:1 into a w x h target: pixel (x, y) is the IPU's
// conversion of the deinterlaced sample (x, y).
float4 yuv_field_ps(YuvVSOut i) : SV_Target0
{
    int2 p = int2(floor(i.pos.xy));
    return float4(float3(ipu_csc(p.x, p.y)) * (1.0 / 255.0), 1.0);
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
    float3 c00 = float3(picture_rgb(x0, y0));
    float3 c10 = float3(picture_rgb(x0 + 1, y0));
    float3 c01 = float3(picture_rgb(x0, y0 + 1));
    float3 c11 = float3(picture_rgb(x0 + 1, y0 + 1));
    float3 c = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
    return float4(c * (1.0 / 255.0), 1.0);
}
