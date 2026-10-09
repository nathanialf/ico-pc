// crt.hlsl: the CRT filter (port/render/rd_crt.c drives it).
//
// Written for ico-pc. The maths reference for the Gaussian beam is Timothy
// Lottes' crt-lottes shader, which he placed in the public domain; no code
// was taken from it, and no GPL CRT shader (CRT-Royale, crt-guest and the
// like) was used as a source.
//
//   crt_vs           the fullscreen triangle over the viewport; t 0..1 across it
//   crt_bloom_ps     the source grid (the PS2 picture's pixels) into a target
//                    of half its size: linear light, a 9-tap horizontal
//                    Gaussian (sigma 2 taps of 2 source pixels)
//   crt_blur_ps      the same target size, the 9-tap Gaussian vertically
//   crt_ps           the box of the output, per output pixel: curvature,
//                    the source pixel and line the warped position falls
//                    in, the phosphor its column lands on passing its
//                    channel and leaking the other two (maskOf) under the
//                    beam of the line at its
//                    height, halation and bloom from the blurred target,
//                    vignette, rounded corners, a soft shoulder, gamma
//
// Bindings: CrtCB (b1, space1: the DrawCB slot, and the same 112 bytes, so
// the passes run under the post pipelines' layouts; shader_consts.h
// IcoCrtCB), t1 the source (the grid; for crt_blur_ps the horizontal pass's
// target), t2 the blurred target (crt_ps),
// s1 bilinear and clamped. No FrameCB: the passes compute everything from
// the viewport. This file does not include common.hlsli, whose DrawCB sits
// in the same register.

#ifdef __spirv__
#define VK_LOC(n) [[vk::location(n)]]
#else
#define VK_LOC(n)
#endif

cbuffer CrtCB : register(b1, space1)
{
    float4 c_src;  // the source grid: w, h in pixels, 1 / w, 1 / h
    float4 c_box;  // the box: w, h, x, y in output pixels
    float4 c_beam; // scanline strength, beam width min, max (lines), gap columns a source pixel
    float4 c_mask; // type (0 none, 1 grille, 2 slot, 3 dots), strength (1 - the leak), fade, halation
    float4 c_glow; // bloom, curvature x, curvature y, corner radius (of the box height)
    float4 c_tone; // vignette, gamma in, gamma out, strength
    float4 c_pass; // x mirror, y unused (the slot's row gain is per pixel, rowGainOf), zw 1 /
                   // the blurred target's size
};

Texture2D<float4> g_texture : register(t1, space2);
Texture2D<float4> g_bloom : register(t2, space2);
SamplerState g_sampler : register(s1, space2);

struct CrtVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

CrtVSOut crt_vs(uint id : SV_VertexID)
{
    CrtVSOut o;
    float2 c = float2((id << 1) & 2u, id & 2u);
    o.pos = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    o.uv = c;
    return o;
}

// x^e for x >= 0 (exp2 / log2: no pow, whose negative-base warning -WX fails)
float3 powp(float3 x, float e)
{
    return exp2(log2(max(x, float3(1e-6, 1e-6, 1e-6))) * e);
}

float luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

float3 linearAt(float2 uv)
{
    return powp(g_texture.SampleLevel(g_sampler, uv, 0.0).rgb, c_tone.y);
}

// The 9-tap Gaussian, sigma 2 taps: exp(-i^2 / 8), i = 0..4, normalised
static const float kGauss[5] = {0.20416, 0.18017, 0.12383, 0.06628, 0.02763};

// Half-size, linear light, horizontal: each tap a bilinear sample between
// two source pixels and two source lines, the taps two source pixels apart.
float4 crt_bloom_ps(CrtVSOut i) : SV_Target0
{
    float3 c = linearAt(i.uv) * kGauss[0];
    [unroll] for (int k = 1; k <= 4; k++) {
        float2 d = float2(2.0 * float(k) * c_src.z, 0.0);
        c += (linearAt(i.uv + d) + linearAt(i.uv - d)) * kGauss[k];
    }
    return float4(c, 1.0);
}

// Vertical, on the horizontal pass's target (c_pass.w its texel height)
float4 crt_blur_ps(CrtVSOut i) : SV_Target0
{
    float3 c = g_texture.SampleLevel(g_sampler, i.uv, 0.0).rgb * kGauss[0];
    [unroll] for (int k = 1; k <= 4; k++) {
        float2 d = float2(0.0, float(k) * c_pass.w);
        c += (g_texture.SampleLevel(g_sampler, i.uv + d, 0.0).rgb +
              g_texture.SampleLevel(g_sampler, i.uv - d, 0.0).rgb) * kGauss[k];
    }
    return float4(c, 1.0);
}

// The source pixel p in linear light (clamped to the grid)
float3 linearLoad(int2 p)
{
    int2 hi = int2(c_src.xy) - int2(1, 1);
    return powp(g_texture.Load(int3(clamp(p, int2(0, 0), hi), 0)).rgb, c_tone.y);
}

// The beam of a line of colour c at distance d (lines): a Gaussian whose
// full width at half maximum grows from c_beam.y (dark) to c_beam.z (bright),
// per channel, normalised to unit area so a line's light is its colour
// whatever the width (the brighter, the wider and the flatter). Its peak
// passes the colour where the beam is narrower than about a line, which
// only a dark line's is: a bright line's beam is near its widest, a few
// per cent over at the most, and gainFadeOf and shoulderOf take that
// (rd_crt.c rd__crt_beam is the same function)
float3 beamOf(float3 c, float d)
{
    float3 width = lerp(c_beam.yyy, c_beam.zzz, saturate(c));
    float3 sig = max(width * (1.0 / 2.3548), float3(0.05, 0.05, 0.05));
    return c * exp(-0.5 * (d * d) / (sig * sig)) / (sig * 2.5066283);
}

// The phosphor mask of an output pixel at f across its source pixel and v
// down its line (0..1), odd its source column's parity (rd_crt.c
// rd__crt_mask_weight is the same function): per channel 1 its own stripe,
// 1 - gap (the mask strength's leak) the other two and a gap. A source
// pixel is r output pixels wide; its last g pixels are a gap, the rest
// three stripes, R, G, B from the left. Slot: a bridge over the last third
// of the line, half a line later in odd columns. Dots: the line's second
// half a second row of dots one stripe over (half a triad rounded down to
// whole stripes).
float3 stripeOf(float f, float v, float strength)
{
    float r = c_box.x * c_src.z, g = c_beam.w, leak = 1.0 - strength;
    if (c_mask.x > 2.5 && v >= 0.5) {
        f = frac(f + 1.0 / 3.0);
    }
    float u = f * r;
    if (u >= r - g) {
        return float3(leak, leak, leak);
    }
    int ch = min(int(u * 3.0 / (r - g)), 2);
    return float3(ch == 0 ? 1.0 : leak, ch == 1 ? 1.0 : leak, ch == 2 ? 1.0 : leak);
}

// the slot's bridge: 1 - the leak over the last third of the line, half a
// line later in odd columns; 1 elsewhere and in the other masks
float slotOf(float v, int odd, float strength)
{
    if (c_mask.x > 1.5 && c_mask.x < 2.5 && frac(v + (odd != 0 ? 0.5 : 0.0)) >= 2.0 / 3.0) {
        return 1.0 - strength;
    }
    return 1.0;
}

float3 maskOf(float f, float v, int odd, float strength)
{
    return stripeOf(f, v, strength) * slotOf(v, odd, strength);
}

// 1 over the slot bridges' mean over a line (rd_crt.c rd__crt_row_gain)
float rowGainOf(float strength)
{
    return c_mask.x > 1.5 && c_mask.x < 2.5 && strength < 1.0 ? 1.0 / (1.0 - strength / 3.0)
                                                               : 1.0;
}

// The mask's strength under a source pixel p (linear; rd_crt.c
// rd__crt_strength_at): eased to half from its brightest channel 0.5 to 1,
// so a white still shows its stripes, more softly, as a lit tube does
// where its glass glows
float strengthAt(float3 p)
{
    return c_mask.y * (1.0 - 0.5 * smoothstep(0.5, 1.0, max(p.r, max(p.g, p.b))));
}

// How much of the gains a pixel of colour col takes (rd_crt.c
// rd__crt_gain_fade): the gains (gain) bring each triad's light up to its
// pixel's, but where a lit stripe would pass 1 the light would clip and
// the stripes flatten into white. One fraction t for the three channels
// (so a colour keeps its hue): the largest that keeps each channel's
// brightest weight in the triad (top: 1 where it has a stripe, the leak in
// a triad short of one), col (1 + fade (top (1 + t (gain - 1)) - 1)), at
// 1. A highlight's triad is then darker than its pixel, as on a tube.
float gainFadeOf(float3 col, float3 gain, float3 top, float fade)
{
    float t = 1.0;
    [unroll] for (int k = 0; k < 3; k++) {
        float over = col[k] * fade * top[k] * (gain[k] - 1.0);
        if (over > 1e-6) {
            t = min(t, (1.0 - col[k] * (1.0 + fade * (top[k] - 1.0))) / over);
        }
    }
    return saturate(t);
}

// The soft shoulder (rd_crt.c rd__crt_shoulder): a colour whose brightest
// channel passes the knee (1 - 0.1 strength) is scaled down as a whole,
// that channel rolled off toward 1, so a light colour keeps its hue rather
// than clipping channel by channel toward white
float3 shoulderOf(float3 c, float st)
{
    float m = max(c.r, max(c.g, c.b));
    float k = 1.0 - 0.1 * st;
    if (m <= k) {
        return c;
    }
    if (k >= 0.9999) {
        return c / m;
    }
    float y = k + (1.0 - k) * (1.0 - exp(-(m - k) / (1.0 - k)));
    return c * (y / m);
}

// The box pixel px warped by the curvature (crt-lottes' warp: x scaled by
// 1 + y^2 cx, y by 1 + x^2 cy), 0..1 across the box
float2 warpOf(float2 px)
{
    float st = c_tone.w;
    float2 c = (px / c_box.xy) * 2.0 - 1.0;
    c *= float2(1.0 + c.y * c.y * c_glow.y * st, 1.0 + c.x * c.x * c_glow.z * st);
    return c * 0.5 + 0.5;
}

// The flat face (no x curvature: c_glow.y 0), without the loop. Output
// column j (centre j + 0.5) lies at u = j + 0.5 - sx r across source pixel
// sx, r = c_box.x / c_src.x output pixels wide. Every stripe, the gap and
// the source pixel itself are intervals of u, so each one's columns are
// those from one edge, ceil(u + sx r - 0.5), to the next: the pixel's own
// stripe and the count of each stripe's columns (the mean weight a
// channel has over the source pixel, (own + leak (n - own)) / n) come
// from the same edges, so a column on a boundary is counted where it is
// lit. The source pixel's edges are those of sx and sx + 1, so the pixels
// partition the columns exactly.
struct FlatCols
{
    float e0, e1; // the source pixel's first column and the next one's
    float o, r;
};

FlatCols flatCols(int sx)
{
    FlatCols c;
    c.r = c_box.x * c_src.z;
    c.o = float(sx) * c.r;
    c.e0 = ceil(c.o - 0.5);
    c.e1 = ceil(float(sx + 1) * c.r - 0.5);
    return c;
}

float edgeAt(FlatCols c, float u)
{
    if (u <= 0.0) {
        return c.e0;
    }
    if (u >= c.r) {
        return c.e1;
    }
    return clamp(ceil(u + c.o - 0.5), c.e0, c.e1);
}

// the columns of [lo, hi) of u' = u + d (wrapped at r) and whether j is one
float2 colsOf(FlatCols c, float lo, float hi, float d, float j)
{
    float a = lo - d, b = hi - d;
    float2 e = float2(0.0, 0.0), e2 = float2(0.0, 0.0);
    if (a >= 0.0) {
        e = float2(edgeAt(c, a), edgeAt(c, b));
    } else if (b <= 0.0) {
        e = float2(edgeAt(c, a + c.r), edgeAt(c, b + c.r));
    } else {
        e = float2(edgeAt(c, 0.0), edgeAt(c, b));
        e2 = float2(edgeAt(c, a + c.r), edgeAt(c, c.r));
    }
    float n = (e.y - e.x) + (e2.y - e2.x);
    float hit = ((j >= e.x && j < e.y) || (j >= e2.x && j < e2.y)) ? 1.0 : 0.0;
    return float2(n, hit);
}

// the stripe weights of column j in source pixel sx, in gain the gain that
// keeps the pixel's light and in top each channel's largest weight over
// the source pixel (stripeOf and triadGain on the flat face)
float3 stripeGainFlat(float j, int sx, float v, float strength, out float3 gain, out float3 top)
{
    FlatCols c = flatCols(sx);
    float g = c_beam.w, leak = 1.0 - strength;
    float d = (c_mask.x > 2.5 && v >= 0.5) ? c.r / 3.0 : 0.0;
    float t = (c.r - g) / 3.0;
    float2 k0 = colsOf(c, 0.0, t, d, j), k1 = colsOf(c, t, 2.0 * t, d, j),
           k2 = colsOf(c, 2.0 * t, c.r - g, d, j);
    float n = c.e1 - c.e0;
    float3 own = float3(k0.x, k1.x, k2.x);
    float3 mean = n > 0.0 ? (own + leak * (n - own)) / n : float3(1.0, 1.0, 1.0);
    gain = 1.0 / max(mean, float3(0.1, 0.1, 0.1));
    top = lerp(float3(leak, leak, leak), float3(1.0, 1.0, 1.0), saturate(own));
    return lerp(float3(leak, leak, leak), float3(1.0, 1.0, 1.0), float3(k0.y, k1.y, k2.y));
}

// 1 over the mean stripe weight, per channel, over the box pixels of px's
// row whose warped positions fall in source pixel sx (rd_crt.c
// rd__crt_triad_gain; the mean floored at 0.1): each triad keeps its own
// pixel's light whether its stripes are 1 or 2 output pixels wide. Only
// under x curvature (the flat face counts, stripeGainFlat).
float3 triadGain(float2 px, int sx, float v, float strength, out float3 top)
{
    int k0 = int(ceil(c_box.x * c_src.z)) + 1;
    float3 sum = float3(0.0, 0.0, 0.0);
    float n = 0.0;
    top = float3(0.0, 0.0, 0.0);
    [loop] for (int k = -k0; k <= k0; k++) {
        float x = px.x + float(k);
        if (x >= 0.0 && x < c_box.x) {
            float s = clamp(warpOf(float2(x, px.y)).x, 0.0, 0.99999) * c_src.x;
            if (int(floor(s)) == sx) {
                float3 sw = stripeOf(s - floor(s), v, strength);
                sum += sw;
                top = max(top, sw);
                n += 1.0;
            }
        }
    }
    float3 mean = n > 0.0 ? sum / n : float3(1.0, 1.0, 1.0);
    top = n > 0.0 ? top : float3(1.0, 1.0, 1.0);
    return 1.0 / max(mean, float3(0.1, 0.1, 0.1));
}

float4 crt_ps(CrtVSOut i) : SV_Target0
{
    float2 px = i.pos.xy - c_box.zw; // box pixels, centres at k + 0.5
    float mirror = c_pass.x;
    float st = c_tone.w; // the strength scales every effect

    // curvature
    float2 w = warpOf(px);

    // rounded corners and the warped edge, antialiased over a pixel
    float2 hb = c_box.xy * 0.5;
    float r = c_glow.w * st * c_box.y;
    float2 e = abs((w - 0.5) * c_box.xy) - (hb - r);
    float sdf = length(max(e, float2(0.0, 0.0))) + min(max(e.x, e.y), 0.0) - r;
    float edge = saturate(0.5 - sdf);

    // the source pixel and line the warped point falls in (on the screen:
    // the mirror takes the pixel from the other side, the stripes keep
    // their order), f and v the point's position across them
    float2 s = clamp(w, 0.0, 0.99999) * c_src.xy;
    int2 sp = int2(floor(s));
    float f = s.x - float(sp.x), v = s.y - float(sp.y);
    // the flat face: the source pixel whose column edges hold this column
    // (stripeGainFlat), which a float floor can miss by one on an edge
    bool flat = c_glow.y == 0.0;
    float j = floor(px.x);
    if (flat) {
        FlatCols fc = flatCols(sp.x);
        if (j < fc.e0 && sp.x > 0) {
            sp.x -= 1;
        } else if (j >= fc.e1 && sp.x < int(c_src.x) - 1) {
            sp.x += 1;
        }
    }
    int2 src = int2(mirror > 0.5 ? int(c_src.x) - 1 - sp.x : sp.x, sp.y);

    // the beam of the line at v, and the spill of the lines above and below
    float3 p = linearLoad(src);
    float d = v - 0.5;
    float3 beam = beamOf(p, d) + beamOf(linearLoad(src - int2(0, 1)), d + 1.0) +
                  beamOf(linearLoad(src + int2(0, 1)), 1.0 - d);
    float3 col = lerp(p, beam, c_beam.x);

    // the phosphor of this output pixel: its channel of the source pixel,
    // the other two leaking through (the strength eased in the highlights,
    // strengthAt), times the gains that keep each triad's light (triadGain
    // across, rowGainOf the slot's rows) as far as no lit stripe clips
    // (gainFadeOf), faded in by the box
    if (c_mask.x > 0.5 && c_mask.z > 0.0) {
        float ms = strengthAt(p);
        float3 gain, top;
        float3 m;
        if (flat) {
            m = stripeGainFlat(j, sp.x, v, ms, gain, top) * slotOf(v, sp.x & 1, ms);
        } else {
            m = maskOf(f, v, sp.x & 1, ms);
            gain = triadGain(px, sp.x, v, ms, top);
        }
        gain *= rowGainOf(ms);
        float t = gainFadeOf(col, gain, top, c_mask.z);
        m *= lerp(float3(1.0, 1.0, 1.0), gain, t);
        col *= lerp(float3(1.0, 1.0, 1.0), m, c_mask.z);
    }
    col = lerp(p, col, st);

    // halation (wide, all light) and bloom (narrower, the bright parts),
    // from the glow of the grid: each takes its share of the light from the
    // picture (rd_crt.c rd__crt_glow_mix), so a flat field keeps its level and
    // a highlight spreads rather than brightens
    float2 wm = float2(mirror > 0.5 ? 1.0 - w.x : w.x, w.y);
    float3 b = g_bloom.SampleLevel(g_sampler, wm, 0.0).rgb;
    float3 halo = b;
    float kh = 0.0;
    if (c_mask.w > 0.0) {
        float2 dx = float2(4.0 * c_pass.z, 0.0), dy = float2(0.0, 4.0 * c_pass.w);
        halo = (b + g_bloom.SampleLevel(g_sampler, wm + dx, 0.0).rgb +
                g_bloom.SampleLevel(g_sampler, wm - dx, 0.0).rgb +
                g_bloom.SampleLevel(g_sampler, wm + dy, 0.0).rgb +
                g_bloom.SampleLevel(g_sampler, wm - dy, 0.0).rgb) * 0.2;
        kh = st * c_mask.w;
    }
    float kb = st * c_glow.x * smoothstep(0.2, 1.0, luma(b));
    float kg = kh + kb;
    if (kg > 1.0) {
        kh /= kg;
        kb /= kg;
        kg = 1.0;
    }
    col = col * (1.0 - kg) + halo * kh + b * kb;

    // vignette
    if (c_tone.x > 0.0) {
        float vg = 16.0 * w.x * w.y * (1.0 - w.x) * (1.0 - w.y);
        col *= exp2(log2(max(vg, 1e-6)) * c_tone.x * st);
    }
    col *= edge;

    float gout = lerp(c_tone.y, c_tone.z, st);
    return float4(powp(saturate(shoulderOf(col, st)), 1.0 / gout), 1.0);
}
