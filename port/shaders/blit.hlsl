// blit.hlsl: fullscreen copy with an optional tint, for the reduction, keep
// and fade passes. The destination rectangle is the viewport and scissor;
// the source rectangle is DrawCB.g_uvRect in texels of t1. No vertex buffer:
// draw 3 vertices.
//   blit_ps      single colour output; colour = texture function(texel, tint)
//   blit_depth_ps blit_ps plus SV_Depth from the scene's depth (the present's
//                effects depth)
//   aa_reduce_ps the supersampled DISPLAY box-averaged to the present box
//   crack_fill_ps, crack_count_ps the crack filler above 1x (rd_crack_fill.c)
// Texture function flags are DrawCB.g_mode.x: with DF_TEXTURED clear the
// pass writes the tint colour alone (fade, letterbox).
#include "common.hlsli"

struct BlitVSOut {
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

BlitVSOut blit_vs(uint id : SV_VertexID)
{
    BlitVSOut o;
    float2 t;
    fullscreen_triangle(id, o.pos, t);
    o.uv = (g_uvRect.xy + t * (g_uvRect.zw - g_uvRect.xy)) * g_tex.zw;
    return o;
}

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);

uint4 blit_color(float2 uv)
{
    uint4 col = g_col;
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, uv) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, g_col, g_mode.x);
    }
    return col;
}

float4 blit_ps(BlitVSOut i) : SV_Target0
{
    return float4(blit_color(i.uv)) * (1.0 / 255.0);
}

// blit_depth_ps (rd_present.c): blit_ps's colour, and
// SV_Depth from t2, the scene's depth (sampled in place), at the same normalised uv
// (the source rectangle covers the whole scene; a mirrored blit flips both)
// read nearest: the texel under uv. Drawn into the output's box with an
// output-size depth target cleared to 0.0, so an effects program hooked into
// the API (ReShade) finds the scene's depth at the picture's pixels and far in
// the bars. Depth grows with GS Z, near 1 and far 0 (gs_z_to_depth):
// RESHADE_DEPTH_INPUT_IS_REVERSED=1.
Texture2D<float> g_sceneDepth : register(t2, space2);

struct BlitDepthOut {
    VK_LOC(0) float4 c : SV_Target0;
    float d : SV_Depth;
};

BlitDepthOut blit_depth_ps(BlitVSOut i)
{
    BlitDepthOut o;
    o.c = float4(blit_color(i.uv)) * (1.0 / 255.0);
    uint w, h;
    g_sceneDepth.GetDimensions(w, h);
    int2 p = int2(floor(saturate(i.uv) * float2(w, h)));
    p = min(p, int2(int(w) - 1, int(h) - 1));
    o.d = g_sceneDepth.Load(int3(p, 0));
    return o;
}

// box_reduce_ps (rd_replay.c doShadowResolve): the exact box
// average of a scaled target down to its GS size, t1 read with Load. The
// output pixel p averages the source texels under [p * f, (p + 1) * f), f =
// DrawCB.g_param.xy the source texels per output pixel (not necessarily an
// integer: a texel counts by its overlap), g_param.zw the source size in
// texels. Drawn with blit_vs into an RGBA8 target of the GS size, the
// viewport the target. g_scale.z: a Full pixel source's left-out texels
// (rd_replay.c shadowReduce; the GS pixel p is then [p f - z, (p + 1) f -
// z), the texels outside the source not counted), else 0.
// BLIT_SPAN_MAX: the most texels an output pixel's span covers on an
// axis in box_reduce_ps and aa_reduce_ps, which clamp their loops' ends
// to first + it so that an end the GPU computed wrong cannot lengthen
// them. box_reduce_ps's f is the scene scale, at most 32768 texels over the
// GS frame's 224 rows (146.3: 148 texels with the partial ones);
// aa_reduce_ps's sums hold 32 bits up to 256 source texels a pixel on each
// axis (255 * (16 * 256)^2 < 2^32: 258 texels with the partial ones).
#define BLIT_SPAN_MAX 258

float4 box_reduce_ps(BlitVSOut i) : SV_Target0
{
    float2 lo = floor(i.pos.xy) * g_param.xy - float2(g_scale.z, 0.0);
    float2 hi = lo + g_param.xy;
    int2 first = max(int2(floor(lo)), int2(0, 0));
    int2 last = min(min(int2(ceil(hi)), int2(g_param.zw)), first + BLIT_SPAN_MAX);
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = first.y; y < last.y; y++) {
        float wy = min(hi.y, float(y + 1)) - max(lo.y, float(y));
        for (int x = first.x; x < last.x; x++) {
            float wx = min(hi.x, float(x + 1)) - max(lo.x, float(x));
            sum += g_texture.Load(int3(x, y, 0)) * (wx * wy);
        }
    }
    return sum / (g_param.x * g_param.y);
}

// aa_reduce_ps (rd_present.c, supersampling): DISPLAY box-averaged into
// the present box's own size, exact in integers. Each output pixel p covers
// the source span [lo, hi) per axis in 1/16 texels:
//   lo = c + p * a + (p * b) / B,  hi the same at p + 1
// (a = S / B and b = S % B for the source extent S in 1/16 texels and the
// box's size B in pixels, so lo = c + floor(p * S / B) without forming p * S;
// c the crop at the left in 1/16 texels). A texel's weight is its overlap
// with the span in 1/16 texels, its value the stored byte (t1 read with
// Load), and the pixel is the weighted sum over the span's area, rounded
// half up: (sum + area / 2) / area, written as k / 255 (stored exactly).
//   g_col   x a, y b, z B, w c   (columns)
//   g_blend x a, y b, z B, w c   (rows)
//   g_mode.xy  the source's size in texels (reads stop at it)
// Integer ratios (2x, 4x) are the exact box mean; others the mean over the
// 1/16-texel grid. The sums fit 32 bits up to 256 source texels a pixel
// on each axis (BLIT_SPAN_MAX).
float4 aa_reduce_ps(BlitVSOut i) : SV_Target0
{
    uint2 p = uint2(i.pos.xy);
    uint2 a = uint2(g_col.x, g_blend.x);
    uint2 b = uint2(g_col.y, g_blend.y);
    uint2 bb = max(uint2(g_col.z, g_blend.z), uint2(1u, 1u));
    uint2 c = uint2(g_col.w, g_blend.w);
    uint2 lo = c + p * a + (p * b) / bb;
    uint2 hi = c + (p + 1u) * a + ((p + 1u) * b) / bb;
    uint2 first = lo >> 4;
    uint2 last = min(min((hi + 15u) >> 4, g_mode.xy), first + uint(BLIT_SPAN_MAX));
    uint4 sum = uint4(0u, 0u, 0u, 0u);
    for (uint y = first.y; y < last.y; y++) {
        uint wy = min(hi.y, (y + 1u) * 16u) - max(lo.y, y * 16u);
        uint4 row = uint4(0u, 0u, 0u, 0u);
        for (uint x = first.x; x < last.x; x++) {
            uint wx = min(hi.x, (x + 1u) * 16u) - max(lo.x, x * 16u);
            row += uint4(floor(g_texture.Load(int3(int(x), int(y), 0)) * 255.0 + 0.5)) * wx;
        }
        sum += row * wy;
    }
    uint area = max((hi.x - lo.x) * (hi.y - lo.y), 1u);
    return float4((sum + area / 2u) / area) * (1.0 / 255.0);
}

// MSAA (rd_replay.c, the MSAA epoch). depth_resolve_ps: the multisampled
// scene depth (t1, Texture2DMS read with Load at each sample) into the
// single-sampled one as SV_Depth, the smallest sample (the depth grows with
// GS Z and the game tests GEQUAL: the farthest depth any sample saw), in a
// depth-only pass over the whole target, test ALWAYS with writes.  The
// sample patterns are centred on the pixel centre, so on a plane the
// smallest sample is at or below the centre's depth: a surface drawn again
// after the epoch single-sampled (the glow's GEQUAL re-draws) passes where
// it passed at 1x; the largest sample lies above the centre on any slope
// and would fail them.
// depth_upsample_ps: the other way, the single-sampled depth (t1) into
// every sample of the multisampled one, when an epoch starts on a scene
// that already holds a picture.
Texture2DMS<float> g_msDepth : register(t1, space2);

float depth_resolve_ps(BlitVSOut i) : SV_Depth
{
    uint w, h, n;
    g_msDepth.GetDimensions(w, h, n);
    int2 p = int2(i.pos.xy);
    float d = 1.0;
    // the port's MSAA has 2, 4 or 8 samples (rd_core.c rd_aa_samples)
    for (uint s = 0u; s < min(n, 8u); s++) {
        d = min(d, g_msDepth.Load(p, int(s)));
    }
    return d;
}

Texture2D<float> g_ssDepth : register(t1, space2);

float depth_upsample_ps(BlitVSOut i) : SV_Depth
{
    return g_ssDepth.Load(int3(int2(i.pos.xy), 0));
}

// The crack filler above 1x (port/render/rd_crack_fill.c; docs/port/RENDER.md,
// "Crack filling above 1x"), behind blit_vs over SCENE's texels, before the
// fog. A texel no draw covered keeps the clear's depth 0, which the fog's
// table takes as its entry 0 (fog_lut.hlsl): above 1x the 1/16 GS pixel
// cracks between triangles truncated apart (DIVERGENCES.md VD-19, VD-20)
// catch texel centres and show as dots of that colour. A texel of depth 0
// is a crack when along its row or its column a run of at most the reach's
// texels of depth 0 (it among them) has a written texel at each end; it
// then takes the nearer end's colour and depth, exact copies (the shorter
// run of the two axes, the row's on a tie; the end before it on a tie
// within the run). Every other texel is discarded.
//   t1  a copy of SCENE's colour (crack_fill_ps), read with Load
//   t2  a copy of SCENE's depth, read with Load (crack_fill_ps)
//   g_param.xy  the reach across columns and across rows, 0..3 texels
//               (ceil(scale / 2) on an axis above 1x, else 0)
// crack_count_ps: the same test over a 16 x 16 block of the depth copy (t1
// there) per output texel, the count n of cracks written as (n & 255,
// n >> 8) / 255 in R and G of an RGBA8 target (the filled pixels, read back
// by rd_crack_fill.c for its counters).
#define CRACK_REACH_MAX 3
#define CRACK_BLOCK 16

Texture2D<float> g_crackDepth : register(t1, space2);

// texel q of the depth copy z, 0 (unwritten) outside it
float crack_depth(Texture2D<float> z, int2 q, int2 size)
{
    int2 c = clamp(q, int2(0, 0), size - int2(1, 1));
    float v = z.Load(int3(c, 0));
    bool inside = q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
    return inside ? v : 0.0;
}

// the distance (1..reach) to the first written texel from p by step, 0 none
int crack_end(Texture2D<float> z, int2 p, int2 step, int reach, int2 size)
{
    int found = 0;
    [unroll] for (int k = CRACK_REACH_MAX; k >= 1; k--) {
        if (k <= reach && crack_depth(z, p + step * k, size) != 0.0) {
            found = k;
        }
    }
    return found;
}

// p a crack of the depth copy z: from the texel it takes
bool crack_at(Texture2D<float> z, int2 p, int2 size, int2 reach, out int2 from)
{
    from = p;
    if (crack_depth(z, p, size) != 0.0) {
        return false;
    }
    int run = CRACK_REACH_MAX + 1;
    int l = crack_end(z, p, int2(-1, 0), reach.x, size);
    int r = crack_end(z, p, int2(1, 0), reach.x, size);
    if (l > 0 && r > 0 && l + r - 1 <= reach.x) {
        run = l + r - 1;
        from = p + int2(l <= r ? -l : r, 0);
    }
    int u = crack_end(z, p, int2(0, -1), reach.y, size);
    int d = crack_end(z, p, int2(0, 1), reach.y, size);
    if (u > 0 && d > 0 && u + d - 1 <= reach.y && u + d - 1 < run) {
        run = u + d - 1;
        from = p + int2(0, u <= d ? -u : d);
    }
    return run <= CRACK_REACH_MAX;
}

struct CrackFillOut {
    VK_LOC(0) float4 c : SV_Target0;
    float d : SV_Depth;
};

CrackFillOut crack_fill_ps(BlitVSOut i)
{
    uint w, h;
    g_sceneDepth.GetDimensions(w, h);
    int2 p = int2(i.pos.xy);
    int2 from;
    if (!crack_at(g_sceneDepth, p, int2(int(w), int(h)), int2(g_param.xy), from)) {
        discard;
    }
    CrackFillOut o;
    o.c = g_texture.Load(int3(from, 0));
    o.d = g_sceneDepth.Load(int3(from, 0));
    return o;
}

float4 crack_count_ps(BlitVSOut i) : SV_Target0
{
    uint w, h;
    g_crackDepth.GetDimensions(w, h);
    int2 size = int2(int(w), int(h));
    int2 base = int2(i.pos.xy) * CRACK_BLOCK;
    uint n = 0u;
    for (int y = 0; y < CRACK_BLOCK; y++) {
        for (int x = 0; x < CRACK_BLOCK; x++) {
            int2 p = base + int2(x, y);
            int2 from;
            if (p.x < size.x && p.y < size.y && crack_at(g_crackDepth, p, size, int2(g_param.xy), from)) {
                n++;
            }
        }
    }
    return float4(float(n & 255u), float(n >> 8), 0.0, 255.0) * (1.0 / 255.0);
}

// camera_probe_ps (tests only, rd__camera_probe): FrameCB's
// matrices applied to the point DrawCB.g_param, written as raw float bits so
// the HLSL column_major packing can be compared with the C side. Target 4 x 3
// RGBA8_UNORM, drawn with blit_vs: column = component, row 0 mul(g_view, p),
// row 1 mul(g_proj, mul(g_view, p)), row 2 mul(g_viewProj, p); each pixel's
// RGBA holds the float's bytes, least significant first (k / 255 stores k
// exactly in UNORM8).
float4 camera_probe_ps(BlitVSOut i) : SV_Target0
{
    uint2 px = uint2(i.pos.xy);
    float4 v = mul(g_view, g_param);
    float4 r = v;
    if (px.y == 1u) {
        r = mul(g_proj, v);
    } else if (px.y == 2u) {
        r = mul(g_viewProj, g_param);
    }
    float f = r.x;
    if (px.x == 1u) {
        f = r.y;
    } else if (px.x == 2u) {
        f = r.z;
    } else if (px.x == 3u) {
        f = r.w;
    }
    uint bits = asuint(f);
    return float4(float(bits & 0xFFu), float((bits >> 8) & 0xFFu), float((bits >> 16) & 0xFFu),
                  float(bits >> 24)) *
           (1.0 / 255.0);
}
