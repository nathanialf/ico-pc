// fx_sprite.hlsl: one GS sprite in the GS integer arithmetic: staticBlur.c's
// passes, RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR
// (port/render/rd_blur.c says what is modelled).
//
//   fx_rect_vs   fullscreen triangle at the sprite's depth (no vertex input;
//                the scissor and fx_sprite_ps's coverage test bound it)
//   fx_sprite_ps the sprite: coverage, UV, texel (nearest or the GS bilinear
//                with TEXA before filtering), TFX, alpha test, DATE, blend;
//                no hardware blending, the result written as k / 255
//   fx_sprite_u_ps (ICO_UINT_SRC=1) the same reading t1 and t2 through
//                their RGBA8_UINT views (RHI_ASPECT_UINT): the texel k
//                itself, where fx_sprite_ps converts k / 255 back with
//                floor(x * 255 + 0.5), which gives k for every k; so the
//                two write the same bytes (rd_replay.c doBlurSpriteDraw
//                picks it when both textures have the view)
//
// Bindings: t1 the texture (UNORM8, read with Load; its UINT view in
// fx_sprite_u_ps), t2 the destination as it was before the sprite (a copy
// of the bound target; with FXF_DST only; in fx_sprite_u_ps t1 again when
// unused), s1 unused.
// DrawCB:
//   g_col    RGBAQ 0..255
//   g_mode   x FXF_* flags, y TEXA mode | TEXFMT << 8,
//            z ATST | ATE << 8 | AFAIL << 16, w AREF
//   g_blend  x ALPHA register, y FIX, z COLCLAMP, w the sprite's GS Z
//   g_uvRect u0 v0 u1 v1, 12.4 texels (integers held as floats)
//   g_tex    xy the TEX0 size (2^TW, 2^TH), zw the size of t1
//   g_param  x0 y0 x1 y1, 12.4 window coordinates (integers held as floats)
//   g_scale  xy t1 texels per GS texel (a scaled target's scale; 1
//            for images and in Original), z t1's Full pixel crop: its
//            left-out texels over its width (common.hlsli), else 0,
//            w with FXF_ABOVE1X the steps (t1 texels per bound texel, in
//            1/32) step32x * 4096 + step32y (rd_replay.c doBlurSpriteDraw),
//            else 0
//   g_aux1   x, y the UV divides' 1 / (x1 - x0), 1 / (y1 - y0), floats
//            correctly rounded on the CPU (asfloat; gs_div_trunc_rcp), z, w
//            fx_texel's REPEAT and CLAMP size: max(1, round(g_tex.xy *
//            g_scale.xy)) as integers (rd_replay.c blurAux1)
// FrameCB: g_origin.xy the XYOFFSET of the bound target (GS pixels, + 0.5 y
// with the half-line offset), g_z.x the GS Z scale of its depth, g_z.yz the
// bound target's texels per GS pixel (1 in Original). The target is as wide
// as the native frame in GS pixels, so the scale is the resolution's alone,
// the same both ways (y == z) but under the Window resolution, whose box
// keeps the PS2's 7:6 pixel shape (rd_present.c rd__apply_display); the
// two are read apart for that.
//
// Scaled targets: a texel stands for the GS pixel coordinate its centre falls on, (pos / s - 0.5)
// in 12.4, and the texture coordinates address t1 at its own scale (UV
// times g_scale), so every sum below is the GS's at scale 1 (bit-exact) and
// the same picture at finer sampling elsewhere.
//
// Above 1x (FXF_ABOVE1X: a target source with a scaled target involved, an
// axis stepping over more than 2 t1 texels a texel or, bilinear, under
// half of one): the texel is resampled about the same GS sample point
// (U, V in t1's 12.4) by common.hlsli's kernels, an area box for a
// decimation, the cubic B-spline for a magnification, each tap fetched as
// fx_texel fetches (wrap, clamp, TEXA), then TFX, the tests and the blend
// as below, all in integers.  The flag is never set at scale 1.
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
#define FXF_ABOVE1X 8192u

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

#ifdef ICO_UINT_SRC
// fx_sprite_u_ps: the integer texel through the RGBA8_UINT view
Texture2D<uint4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<uint4> g_dest : register(t2, space2);

uint4 fx_load(Texture2D<uint4> t, int2 c)
{
    return t.Load(int3(c, 0));
}
#else
Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<float4> g_dest : register(t2, space2);

uint4 fx_load(Texture2D<float4> t, int2 c)
{
    return uint4(floor(t.Load(int3(c, 0)) * 255.0 + 0.5));
}
#endif

// t1 texels per GS texel, 1 when unset.
float2 fx_src_scale()
{
    return float2(g_scale.x > 0.0 ? g_scale.x : 1.0, g_scale.y > 0.0 ? g_scale.y : 1.0);
}

// t1's Full pixel crop in 1/16 texels: the texels of the GS frame left out
// at its left (g_scale.z times its width), 0 on every other texture
int fx_src_crop16()
{
    return g_scale.z != 0.0 ? int(round(g_scale.z * g_tex.z * 16.0)) : 0;
}

// One texel at integer texel coordinates: CLAMP or REPEAT on the TEX0 size,
// then clamped to the texture that backs it, then TEXA.  On a Full pixel
// crop c is in the crop's texels, the wrap and the clamp in the frame's
// (the crop's whole texels added back).
uint4 fx_texel(int2 c, uint flags)
{
    // max(1, round(g_tex.xy * fx_src_scale())), per draw (g_aux1.zw)
    const int2 lsz = int2(g_aux1.zw);
    const int2 asz = int2(g_tex.zw);
    const int k = fx_src_crop16() >> 4;
    c.x += k;
    // REPEAT: lsz is 2^TW x 2^TH at scale 1 and at an integer scale that
    // is a power of two (gs_wrap_repeat's mask), any size otherwise
    c.x = (flags & FXF_CLAMP_S) != 0u ? clamp(c.x, 0, lsz.x - 1) : gs_wrap_repeat(c.x, lsz.x);
    c.y = (flags & FXF_CLAMP_T) != 0u ? clamp(c.y, 0, lsz.y - 1) : gs_wrap_repeat(c.y, lsz.y);
    c.x -= k;
    c = clamp(c, int2(0, 0), asz - 1);
    uint4 t = fx_load(g_texture, c);
    return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
}

float4 fx_sprite_ps(float4 pos : SV_Position) : SV_Target0
{
    const uint flags = g_mode.x;
    const int2 px = int2(pos.xy);
    // the pixel's GS window coordinate, 12.4: px * 16 at scale 1.  On a
    // scaled target the texel's GS pixel (its s x s block) for the coverage,
    // and the GS position of its own centre for the UV
    const float2 ts = float2(g_z.y > 0.0 ? g_z.y : 1.0, g_z.z > 0.0 ? g_z.z : 1.0);
    // the texel in the frame's texels: a Full pixel target's left-out
    // texels added (g_vpo.z, 0 on every other target)
    const float2 fp = pos.xy + float2(g_vpo.z, 0.0);
    // fp / ts: four divides by a per-draw value, skipped at scale 1, where
    // a divide by exactly 1.0 returns its dividend (the reciprocal of 1.0
    // is 1.0 and x * 1.0 is x)
    float2 fb = fp - 0.5, fc = fp;
    [branch] if (ts.x != 1.0 || ts.y != 1.0) {
        fb = fb / ts;
        fc = fc / ts;
    }
    const int2 gb = int2(floor(fb)) * 16;
    const int2 gp = int2(floor((fc - 0.5) * 16.0 + 0.5));
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
        // the GS's U(X) = u0 + (X - x0) (u1 - u0) / (x1 - x0), truncated,
        // without the integer divide or a float one (gs_div_trunc_rcp: the
        // operands' bounds, the reciprocals the CPU rounded)
        const int u = u0 + gs_div_trunc_rcp((X - x0) * (u1 - u0), x1 - x0, asfloat(g_aux1.x));
        const int v = v0 + gs_div_trunc_rcp((Y - y0) * (v1 - v0), y1 - y0, asfloat(g_aux1.y));
        uint4 t;
        // the UV in t1's own texels, 12.4 (u, v at scale 1)
        const float2 ss = fx_src_scale();
        const int su = int(round(float(u) * ss.x)) - fx_src_crop16();
        const int sv = int(round(float(v) * ss.y));
        if ((flags & FXF_ABOVE1X) != 0u) {
            const uint steps = uint(g_scale.w);
            const bool lin = (flags & FXF_LINEAR) != 0u;
            const GsAxis ax = gs_resample_axis(su, int(steps >> 12), lin);
            const GsAxis ay = gs_resample_axis(sv, int(steps & 4095u), lin);
            const uint dx = gs_resample_div(ax), dy = gs_resample_div(ay);
            uint4 sum = uint4(0u, 0u, 0u, 0u);
            const int nx = min(ax.count, GS_RS_TAPS_MAX), ny = min(ay.count, GS_RS_TAPS_MAX);
            [loop] for (int j = 0; j < ny; j++) {
                uint4 row = uint4(0u, 0u, 0u, 0u);
                [loop] for (int i = 0; i < nx; i++) {
                    row += fx_texel(int2(ax.first + i, ay.first + j), flags) *
                           gs_resample_weight(ax, i);
                }
                sum += ((row * 16u + dx / 2u) / dx) * gs_resample_weight(ay, j);
            }
            t = (sum + 8u * dy) / (16u * dy);
        } else if ((flags & FXF_LINEAR) != 0u) {
            const int uu = su - 8, vv = sv - 8;
            const int2 i0 = int2(uu >> 4, vv >> 4);
            const uint fu = uint(uu & 15), fv = uint(vv & 15);
            const uint4 a = fx_texel(i0, flags);
            const uint4 b = fx_texel(i0 + int2(1, 0), flags);
            const uint4 c = fx_texel(i0 + int2(0, 1), flags);
            const uint4 d = fx_texel(i0 + int2(1, 1), flags);
            t = gs_bilerp(a, b, c, d, fu, fv);
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
