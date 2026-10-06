// fx_sprite.hlsl: one GS sprite in the GS integer arithmetic (renderer wave
// 5, R5a): staticBlur.c's passes, RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR
// (port/render/rd_blur.c says what is modelled).
//
//   fx_rect_vs   fullscreen triangle at the sprite's depth (no vertex input;
//                the scissor and fx_sprite_ps's coverage test bound it)
//   fx_sprite_ps the sprite: coverage, UV, texel (nearest or the GS bilinear
//                with TEXA before filtering), TFX, alpha test, DATE, blend;
//                no hardware blending, the result written as k / 255
//
// Bindings: t1 the texture (UNORM8, read with Load), t2 the destination as
// it was before the sprite (a copy of the bound target; with FXF_DST only),
// s1 unused.
// DrawCB:
//   g_col    RGBAQ 0..255
//   g_mode   x FXF_* flags, y TEXA mode | TEXFMT << 8,
//            z ATST | ATE << 8 | AFAIL << 16, w AREF
//   g_blend  x ALPHA register, y FIX, z COLCLAMP, w the sprite's GS Z
//   g_uvRect u0 v0 u1 v1, 12.4 texels (integers held as floats)
//   g_tex    xy the TEX0 size (2^TW, 2^TH), zw the size of t1
//   g_param  x0 y0 x1 y1, 12.4 window coordinates (integers held as floats)
//   g_scale  xy t1 texels per GS texel (R7a: a scaled target's scale; 1
//            for images and in Original)
// FrameCB: g_origin.xy the XYOFFSET of the bound target (GS pixels, + 0.5 y
// with the half-line offset), g_z.x the GS Z scale of its depth, g_z.yz the
// bound target's texels per GS pixel (R7a; 1 in Original).
//
// Scaled targets (renderer wave 7, R7a): a texel
// stands for the GS pixel coordinate its centre falls on, (pos / s - 0.5)
// in 12.4, and the texture coordinates address t1 at its own scale (UV
// times g_scale), so every sum below is the GS's at scale 1 (bit-exact) and
// the same picture at finer sampling elsewhere.
#include "common.hlsli"

#define FXF_TEXTURED 1u
#define FXF_TFX_SHIFT 1u
#define FXF_TCC 8u
#define FXF_LINEAR 16u
#define FXF_CLAMP_S 32u
#define FXF_CLAMP_T 64u
#define FXF_ABE 128u
#define FXF_PABE 256u
#define FXF_FBA 512u
#define FXF_DATE 1024u
#define FXF_DATM 2048u
#define FXF_DST 4096u

#define AFAIL_KEEP 0u
#define AFAIL_FB_ONLY 1u
#define AFAIL_ZB_ONLY 2u
#define AFAIL_RGB_ONLY 3u

float4 fx_rect_vs(uint id : SV_VertexID) : SV_Position
{
    float4 pos;
    float2 t;
    fullscreen_triangle(id, pos, t);
    pos.z = gs_z_to_depth(g_blend.w, g_z.x);
    return pos;
}

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<float4> g_dest : register(t2, space2);

uint4 fx_load(Texture2D<float4> t, int2 c)
{
    return uint4(floor(t.Load(int3(c, 0)) * 255.0 + 0.5));
}

// One texel at integer texel coordinates: CLAMP or REPEAT on the TEX0 size,
// then clamped to the texture that backs it, then TEXA.
float2 fx_src_scale()
{
    return float2(g_scale.x > 0.0 ? g_scale.x : 1.0, g_scale.y > 0.0 ? g_scale.y : 1.0);
}

// REPEAT on a size that is a power of two at scale 1 (c & (n - 1) there);
// the modulo keeps it for a scaled size that is not
int fx_wrap(int c, int n)
{
    return ((c % n) + n) % n;
}

uint4 fx_texel(int2 c, uint flags)
{
    int2 lsz = max(int2(round(g_tex.xy * fx_src_scale())), int2(1, 1));
    int2 asz = int2(g_tex.zw);
    c.x = (flags & FXF_CLAMP_S) != 0u ? clamp(c.x, 0, lsz.x - 1) : fx_wrap(c.x, lsz.x);
    c.y = (flags & FXF_CLAMP_T) != 0u ? clamp(c.y, 0, lsz.y - 1) : fx_wrap(c.y, lsz.y);
    c = min(c, asz - 1);
    uint4 t = fx_load(g_texture, c);
    return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
}

float4 fx_sprite_ps(float4 pos : SV_Position) : SV_Target0
{
    const uint flags = g_mode.x;
    const int2 px = int2(pos.xy);
    // the pixel's GS window coordinate, 12.4: px * 16 at scale 1.  R7a: on a
    // scaled target the texel's GS pixel (its s x s block) for the coverage,
    // and the GS position of its own centre for the UV
    const float2 ts = float2(g_z.y > 0.0 ? g_z.y : 1.0, g_z.z > 0.0 ? g_z.z : 1.0);
    const int2 gb = int2(floor((pos.xy - 0.5) / ts)) * 16;
    const int2 gp = int2(floor((pos.xy / ts - 0.5) * 16.0 + 0.5));
    const int ox = int(round(g_origin.x * 16.0)), oy = int(round(g_origin.y * 16.0));
    const int X = ox + gp.x;
    const int Y = oy + gp.y;
    const int x0 = int(g_param.x), y0 = int(g_param.y), x1 = int(g_param.z), y1 = int(g_param.w);
    if (ox + gb.x < x0 || ox + gb.x >= x1 || oy + gb.y < y0 || oy + gb.y >= y1) {
        discard;
    }

    uint4 col = g_col;
    if ((flags & FXF_TEXTURED) != 0u) {
        const int u0 = int(g_uvRect.x), v0 = int(g_uvRect.y);
        const int u1 = int(g_uvRect.z), v1 = int(g_uvRect.w);
        const int u = u0 + ((X - x0) * (u1 - u0)) / (x1 - x0);
        const int v = v0 + ((Y - y0) * (v1 - v0)) / (y1 - y0);
        uint4 t;
        // the UV in t1's own texels, 12.4 (u, v at scale 1)
        const float2 ss = fx_src_scale();
        const int su = int(round(float(u) * ss.x)), sv = int(round(float(v) * ss.y));
        if ((flags & FXF_LINEAR) != 0u) {
            const int uu = su - 8, vv = sv - 8;
            const int2 i0 = int2(uu >> 4, vv >> 4);
            const uint fu = uint(uu & 15), fv = uint(vv & 15);
            const uint4 a = fx_texel(i0, flags);
            const uint4 b = fx_texel(i0 + int2(1, 0), flags);
            const uint4 c = fx_texel(i0 + int2(0, 1), flags);
            const uint4 d = fx_texel(i0 + int2(1, 1), flags);
            t = (a * ((16u - fu) * (16u - fv)) + b * (fu * (16u - fv)) + c * ((16u - fu) * fv) +
                 d * (fu * fv)) >> 8;
        } else {
            t = fx_texel(int2(su >> 4, sv >> 4), flags);
        }
        const uint tfx = (flags >> FXF_TFX_SHIFT) & 3u;
        const bool tcc = (flags & FXF_TCC) != 0u;
        uint4 o;
        if (tfx == 1u) {
            o.rgb = t.rgb;
            o.a = tcc ? t.a : col.a;
        } else {
            o.rgb = min((t.rgb * col.rgb) >> 7, uint3(255, 255, 255));
            if (tfx >= 2u) {
                o.rgb = min(o.rgb + col.aaa, uint3(255, 255, 255));
            }
            if (!tcc) {
                o.a = col.a;
            } else if (tfx == 0u) {
                o.a = min((t.a * col.a) >> 7, 255u);
            } else if (tfx == 2u) {
                o.a = min(t.a + col.a, 255u);
            } else {
                o.a = t.a;
            }
        }
        col = o;
    }

    // alpha test
    bool keepDestAlpha = false;
    const bool ate = ((g_mode.z >> 8) & 1u) != 0u;
    if (ate && !gs_alpha_pass(g_mode.z & 0xFFu, g_mode.w, col.a)) {
        const uint afail = (g_mode.z >> 16) & 3u;
        if (afail == AFAIL_KEEP || afail == AFAIL_ZB_ONLY) {
            discard;
        }
        keepDestAlpha = afail == AFAIL_RGB_ONLY;
    }

    uint4 dst = uint4(0, 0, 0, 0);
    if ((flags & FXF_DST) != 0u) {
        dst = fx_load(g_dest, px);
    }
    if ((flags & FXF_DATE) != 0u) {
        const bool msb = (dst.a & 0x80u) != 0u;
        if (msb != ((flags & FXF_DATM) != 0u)) {
            discard;
        }
    }

    uint4 res = col;
    const bool pabeOff = (flags & FXF_PABE) != 0u && (col.a & 0x80u) == 0u;
    if ((flags & FXF_ABE) != 0u && !pabeOff) {
        res.rgb = gs_blend_int(col, dst, g_blend.x, g_blend.y, g_blend.z).rgb;
    }
    res.a = col.a | ((flags & FXF_FBA) != 0u ? 0x80u : 0u);
    if (keepDestAlpha) {
        res.a = dst.a;
    }
    return float4(res) * (1.0 / 255.0);
}
