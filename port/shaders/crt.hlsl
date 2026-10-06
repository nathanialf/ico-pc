// crt.hlsl: the CRT filter (package CRT; docs/port/DISPLAY.md "CRT filter",
// docs/port/RENDER_API.md "The CRT pass"; port/render/rd_crt.c drives it).
//
// Written for ico-pc. The maths reference for the Gaussian beam and the
// phosphor mask patterns is Timothy Lottes' crt-lottes shader, which he
// placed in the public domain; no code was taken from it, and no GPL CRT
// shader (CRT-Royale, crt-guest and the like) was used as a source.
//
//   crt_vs        the fullscreen triangle over the viewport; t 0..1 across it
//   crt_bloom_ps  the virtual source (the PS2 picture's pixel grid) into a
//                 target of half its size: linear light, a 9-tap horizontal
//                 Gaussian (sigma 2 taps of 2 source pixels)
//   crt_blur_ps   the same target size, the 9-tap Gaussian vertically
//   crt_ps        the box of the output: curvature, the beam of the two
//                 nearest source lines, the phosphor mask, halation and
//                 bloom from the blurred target, vignette, rounded corners
//
// Bindings: CrtCB (b1, space1: the DrawCB slot, and the same 112 bytes, so
// the passes run under the post pipelines' layouts; shader_consts.h
// IcoCrtCB), t1 the source (the virtual source, or for crt_blur_ps the
// horizontal pass's target), t2 the blurred target (crt_ps), s1 bilinear and
// clamped. No FrameCB: the passes compute everything from the viewport.
// This file does not include common.hlsli, whose DrawCB sits in the same
// register.

#ifdef __spirv__
#define VK_LOC(n) [[vk::location(n)]]
#else
#define VK_LOC(n)
#endif

cbuffer CrtCB : register(b1, space1)
{
    float4 c_src;  // the virtual source: w, h in pixels, 1 / w, 1 / h
    float4 c_box;  // the box: w, h, x, y in output pixels
    float4 c_beam; // scanline strength, beam width min, max (lines), horizontal blur (pixels)
    float4 c_mask; // type (0 none, 1 grille, 2 slot, 3 dots), strength, pitch (px), halation
    float4 c_glow; // bloom, curvature x, curvature y, corner radius (of the box height)
    float4 c_tone; // vignette, gamma in, gamma out, strength
    float4 c_pass; // x mirror, y 0, zw 1 / the blurred target's size (crt_ps) or the pass's step
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

// One source line in linear light at x (source pixels, centres at k + 0.5):
// a Gaussian of c_beam.w / 2 pixels over the four nearest pixels, normalised
float3 lineColour(float ln, float x)
{
    float sx = x - 0.5;
    float x0 = floor(sx);
    float f = sx - x0;
    float sig = max(0.5 * c_beam.w, 0.05);
    float k = -0.5 / (sig * sig);
    float v = (ln + 0.5) * c_src.w;
    float3 c = float3(0.0, 0.0, 0.0);
    float wsum = 0.0;
    [unroll] for (int j = -1; j <= 2; j++) {
        float d = f - float(j);
        float w = exp(k * d * d);
        c += w * linearAt(float2((x0 + float(j) + 0.5) * c_src.z, v));
        wsum += w;
    }
    return c / wsum;
}

// The beam of a line of colour c at distance d (lines): a Gaussian whose
// full width at half maximum grows from c_beam.y (dark) to c_beam.z (bright),
// per channel, normalised to unit area so a line's light is its colour
// whatever the width (the brighter, the wider and the flatter)
float3 beamOf(float3 c, float d)
{
    float3 width = lerp(c_beam.yyy, c_beam.zzz, saturate(c));
    float3 sig = max(width * (1.0 / 2.3548), float3(0.05, 0.05, 0.05));
    return c * exp(-0.5 * (d * d) / (sig * sig)) / (sig * 2.5066283);
}

// The phosphor mask at output pixel p (box-relative, integer): the lit
// channels 1.5, the dark ones 0.5 (crt-lottes' maskLight and maskDark)
float3 maskOf(float2 p)
{
    const float hi = 1.5, lo = 0.5;
    float type = c_mask.x, pitch = c_mask.z;
    float3 m = float3(lo, lo, lo);
    float x = p.x;
    if (type > 2.5) {
        // dot triads: rows of 2/3 pitch, every other row shifted half a triad
        float rowH = max(1.0, floor(pitch * 2.0 / 3.0 + 0.5));
        if (fmod(floor(p.y / rowH), 2.0) > 0.5) {
            x += pitch * 0.5;
        }
    } else if (type > 1.5) {
        // slot mask: a dark line every 2 pitch lines, offset half that in
        // every other triad column
        float slotH = 2.0 * pitch;
        float yy = p.y + (fmod(floor(p.x / pitch), 2.0) > 0.5 ? floor(slotH * 0.5) : 0.0);
        if (fmod(yy, slotH) < 1.0) {
            return m;
        }
    }
    if (pitch < 2.5) {
        // two pixels a triad: magenta and green
        if (fmod(floor(x), 2.0) < 0.5) {
            m.rb = float2(hi, hi);
        } else {
            m.g = hi;
        }
        return m;
    }
    float ph = floor(frac(floor(x) / pitch + 1e-4) * 3.0);
    if (ph < 0.5) {
        m.r = hi;
    } else if (ph < 1.5) {
        m.g = hi;
    } else {
        m.b = hi;
    }
    return m;
}

float4 crt_ps(CrtVSOut i) : SV_Target0
{
    float2 px = i.pos.xy - c_box.zw; // box pixels, centres at k + 0.5
    float2 q = px / c_box.xy;
    float mirror = c_pass.x;

    // the plain picture (what the box blit shows: each line doubled, the
    // pixels bilinear across), for the strength
    float2 qm = float2(mirror > 0.5 ? 1.0 - q.x : q.x, q.y);
    float4 plain = g_texture.SampleLevel(
        g_sampler, float2(qm.x, (floor(q.y * c_src.y) + 0.5) * c_src.w), 0.0);

    // curvature: x scaled by 1 + y^2 cx, y by 1 + x^2 cy (crt-lottes' warp)
    float2 c = q * 2.0 - 1.0;
    c *= float2(1.0 + c.y * c.y * c_glow.y, 1.0 + c.x * c.x * c_glow.z);
    float2 w = c * 0.5 + 0.5;

    // rounded corners and the warped edge, antialiased over a pixel
    float2 hb = c_box.xy * 0.5;
    float r = c_glow.w * c_box.y;
    float2 e = abs((w - 0.5) * c_box.xy) - (hb - r);
    float sdf = length(max(e, float2(0.0, 0.0))) + min(max(e.x, e.y), 0.0) - r;
    float edge = saturate(0.5 - sdf);

    float2 wm = float2(mirror > 0.5 ? 1.0 - w.x : w.x, w.y);
    float2 s = wm * c_src.xy; // source pixels and lines

    // the beam: the two nearest source lines
    float sy = s.y - 0.5;
    float l0 = floor(sy);
    float f = sy - l0;
    float3 c0 = lineColour(l0, s.x);
    float3 c1 = lineColour(l0 + 1.0, s.x);
    float3 even = lerp(c0, c1, f);
    float3 beam = beamOf(c0, f) + beamOf(c1, 1.0 - f);
    float3 col = lerp(even, beam, c_beam.x);

    // the phosphor mask, in output pixels
    if (c_mask.x > 0.5 && c_mask.y > 0.0) {
        col *= lerp(float3(1.0, 1.0, 1.0), maskOf(floor(px)), c_mask.y);
    }

    // halation (wide, all light) and bloom (narrower, the bright parts)
    float3 b = g_bloom.SampleLevel(g_sampler, wm, 0.0).rgb;
    if (c_mask.w > 0.0) {
        float2 dx = float2(4.0 * c_pass.z, 0.0), dy = float2(0.0, 4.0 * c_pass.w);
        float3 halo = b + g_bloom.SampleLevel(g_sampler, wm + dx, 0.0).rgb +
                      g_bloom.SampleLevel(g_sampler, wm - dx, 0.0).rgb +
                      g_bloom.SampleLevel(g_sampler, wm + dy, 0.0).rgb +
                      g_bloom.SampleLevel(g_sampler, wm - dy, 0.0).rgb;
        col += c_mask.w * halo * 0.2;
    }
    col += c_glow.x * b * smoothstep(0.2, 1.0, luma(b));

    // vignette
    if (c_tone.x > 0.0) {
        float v = 16.0 * w.x * w.y * (1.0 - w.x) * (1.0 - w.y);
        col *= exp2(log2(max(v, 1e-6)) * c_tone.x);
    }
    col *= edge;

    float3 crt = powp(saturate(col), 1.0 / c_tone.z);
    return float4(lerp(plain.rgb, crt, c_tone.w), plain.a);
}
