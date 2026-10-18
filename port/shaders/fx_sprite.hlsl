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
//   fx_frame_ps  (ICO_FX_FRAME=1) fx_sprite_ps with the frame-size forms
//                port/render/rd_fx_plan.c draws the open pass's sprites
//                with, each an identity of fx_sprite_ps's bytes:
//                FXF_ALPHA_ONLY  the texel's alpha alone, RGB 0: set only
//                                for MODULATE with RGBAQ's RGB 0 and no
//                                blend, where (t * 0) >> 7 is 0 and the
//                                blend leaves Cs (the glow's mask)
//                FXF_OUT_PREMUL  the ALPHA register's (A - B) C >> 7 with
//                                Cd's term 0, its magnitude at most 255,
//                                for a pipeline that adds Cd (ONE, ONE) or
//                                takes it from Cd (reverse subtract): set
//                                only for the four forms A, B in {Cs, 0},
//                                C in {As, FIX}, D Cd, COLCLAMP 1, no PABE
//                                (the paste's Cs + Cd)
//   fx_local_ps  (ICO_FX_DST_LOCAL=1) fx_sprite_ps reading the destination
//                inside the open pass by local read: t1 the bound target
//                as an input attachment (SubpassLoad: the fragment's own
//                texel as the pass holds it after the by-region barrier
//                rd_fx_plan.c records before the draw, the value the
//                snapshot's Load returns: fx_rect_vs draws one triangle,
//                so no other fragment of the sprite wrote it), t2 the
//                texture (rd's layoutLocal); never a self-sampling sprite.
//                Its DXIL reads t1 as a texture (never used: D3D12 has no
//                local read)
//
// Bindings: t1 the texture (UNORM8, read with Load; its UINT view in
// fx_sprite_u_ps), t2 the destination as it was before the sprite (a copy
// of the bound target; with FXF_DST only; in fx_sprite_u_ps t1 again when
// unused), s1 unused; fx_local_ps as above.
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
#define FXF_ALPHA_ONLY 16384u
#define FXF_OUT_PREMUL 32768u

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
#elif defined(ICO_FX_DST_LOCAL)
// fx_local_ps: rd's layoutLocal (t1 the input attachment, t2 the texture)
Texture2D<float4> g_texture : register(t2, space2);
#ifdef __spirv__
[[vk::input_attachment_index(0)]] SubpassInput<float4> g_destIn : register(t1, space2);
#define FX_DST_LOCAL_READ 1
#else
Texture2D<float4> g_dest : register(t1, space2);
#endif

uint4 fx_load(Texture2D<float4> t, int2 c)
{
    return uint4(floor(t.Load(int3(c, 0)) * 255.0 + 0.5));
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

#if defined(ICO_FX_FRAME) && (defined(ICO_UINT_SRC) || defined(ICO_FX_DST_LOCAL))
#error "fx_frame_ps is the UNORM build with its textures in t1, t2"
#endif

// The destination at the fragment's texel px, 0..255: the snapshot's, or
// the open pass's own by local read (fx_local_ps)
uint4 fx_dest(int2 px)
{
#ifdef FX_DST_LOCAL_READ
    return uint4(floor(g_destIn.SubpassLoad() * 255.0 + 0.5));
#else
    return fx_load(g_dest, px);
#endif
}

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

// fx_texel's addressing on one axis, which the other axis does not change:
// CLAMP (clampAxis) or REPEAT on the TEX0 size lsz (max(1, round(g_tex.xy
// * fx_src_scale())), per draw: g_aux1.zw), then clamped to the asz texels
// of the texture that backs it.  k: a Full pixel crop's whole texels (the
// x axis; 0 on y): c is in the crop's texels, the wrap and the clamp in the
// frame's (the crop's texels added back).  REPEAT: lsz is 2^TW x 2^TH at
// scale 1 and at an integer scale that is a power of two (gs_wrap_repeat's
// mask), any size otherwise.
int fx_axis(int c, bool clampAxis, int lsz, int asz, int k)
{
    c += k;
    c = clampAxis ? clamp(c, 0, lsz - 1) : gs_wrap_repeat(c, lsz);
    return clamp(c - k, 0, asz - 1);
}

int fx_axis_x(int c, uint flags)
{
    return fx_axis(c, (flags & FXF_CLAMP_S) != 0u, int(g_aux1.z), int(g_tex.z),
                   fx_src_crop16() >> 4);
}

int fx_axis_y(int c, uint flags)
{
    return fx_axis(c, (flags & FXF_CLAMP_T) != 0u, int(g_aux1.w), int(g_tex.w), 0);
}

// The texel at addressed coordinates (fx_axis_x, fx_axis_y), after TEXA
uint4 fx_texel_at(int x, int y)
{
    return gs_texa_expand(fx_load(g_texture, int2(x, y)), g_mode.y & 0xFFu, g_mode.y >> 8);
}

// One texel at integer texel coordinates: addressed (fx_axis_x,
// fx_axis_y), then TEXA.
uint4 fx_texel(int2 c, uint flags)
{
    return fx_texel_at(fx_axis_x(c.x, flags), fx_axis_y(c.y, flags));
}

#ifdef ICO_FX_FRAME
// fx_texel_at's alpha alone: TEXA reads RGB only for a 24- or 16-bit source
// (gs_texa_expand returns an RGBA32 texel as it is)
uint fx_texel_alpha(int x, int y)
{
    const float4 t = g_texture.Load(int3(x, y, 0));
    const uint fmt = g_mode.y >> 8;
    [branch] if (fmt == TEXFMT_RGBA32) {
        return uint(floor(t.a * 255.0 + 0.5));
    }
    const uint4 u = uint4(floor(t * 255.0 + 0.5));
    return gs_texa_alpha(u.r, u.g, u.b, u.a, g_mode.y & 0xFFu, fmt);
}

// gs_bilerp's step on one channel: a + (((b - a) f) >> 4)
int fx_lerp16(int a, int b, uint f)
{
    return a + (((b - a) * int(f)) >> 4);
}

// FXF_OUT_PREMUL: per colour channel |((A - B) C) >> 7| of the ALPHA register
// reg with Cd's term 0 (A, B in {Cs, 0}: the CPU's condition), at most 255:
// the add's (A Cs, B 0) is >= 0, the subtract's (A 0, B Cs) <= 0, an
// arithmetic shift as the GS's, so the pipeline's Cd + it (ONE, ONE) or
// Cd - it (reverse subtract), clamped to 0..255 by the UNORM target, is
// the GS's clamped ((A - B) C >> 7) + Cd
uint3 fx_premul(uint4 cs, uint reg, uint fix)
{
    const int c = gs_blend_factor((reg >> 4) & 3u, int(cs.a), 0, int(fix));
    const int3 s = int3(cs.rgb);
    int3 v;
    v.r = ((gs_blend_term(reg & 3u, s.r, 0) - gs_blend_term((reg >> 2) & 3u, s.r, 0)) * c) >> 7;
    v.g = ((gs_blend_term(reg & 3u, s.g, 0) - gs_blend_term((reg >> 2) & 3u, s.g, 0)) * c) >> 7;
    v.b = ((gs_blend_term(reg & 3u, s.b, 0) - gs_blend_term((reg >> 2) & 3u, s.b, 0)) * c) >> 7;
    return uint3(min(abs(v), int3(255, 255, 255)));
}
#endif

// The sprite's texel at (su, sv), t1's 12.4 texels: above 1x the step's
// kernel (FXF_ABOVE1X, g_scale.w), else the GS's bilinear (FXF_LINEAR) or
// nearest texel, each tap addressed by fx_texel's rules and TEXA'd before
// filtering
uint4 fx_sprite_texel(int su, int sv, uint flags)
{
    uint4 t;
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
        // the four taps' addressing per axis: each column and row once
        const int uu = su - 8, vv = sv - 8;
        const int x0 = fx_axis_x(uu >> 4, flags), x1 = fx_axis_x((uu >> 4) + 1, flags);
        const int y0 = fx_axis_y(vv >> 4, flags), y1 = fx_axis_y((vv >> 4) + 1, flags);
        const uint fu = uint(uu & 15), fv = uint(vv & 15);
        const uint4 a = fx_texel_at(x0, y0);
        const uint4 b = fx_texel_at(x1, y0);
        const uint4 c = fx_texel_at(x0, y1);
        const uint4 d = fx_texel_at(x1, y1);
        t = gs_bilerp(a, b, c, d, fu, fv);
    } else {
        t = fx_texel(int2(su >> 4, sv >> 4), flags);
    }
    return t;
}

// The texture function (TFX, TCC) of texel t with RGBAQ col
uint4 fx_tfx(uint4 t, uint4 col, uint flags)
{
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
    return o;
}

#ifdef ICO_FX_FRAME
// FXF_ALPHA_ONLY below 1x (the kernels' path takes the whole texel): the
// sprite's alpha where MODULATE's RGB is (t * 0) >> 7 = 0: (t.a a) >> 7
// with TCC, the texel's alpha by the GS's bilinear (FXF_LINEAR, gs_bilerp
// on that channel) or nearest; RGBAQ's a without
uint fx_alpha_only(int su, int sv, uint a, uint flags)
{
    if ((flags & FXF_TCC) == 0u) {
        return a;
    }
    uint ta;
    if ((flags & FXF_LINEAR) != 0u) {
        const int uu = su - 8, vv = sv - 8;
        const int x0 = fx_axis_x(uu >> 4, flags), x1 = fx_axis_x((uu >> 4) + 1, flags);
        const int y0 = fx_axis_y(vv >> 4, flags), y1 = fx_axis_y((vv >> 4) + 1, flags);
        const uint fu = uint(uu & 15), fv = uint(vv & 15);
        const int top = fx_lerp16(int(fx_texel_alpha(x0, y0)), int(fx_texel_alpha(x1, y0)), fu);
        const int bot = fx_lerp16(int(fx_texel_alpha(x0, y1)), int(fx_texel_alpha(x1, y1)), fu);
        ta = uint(fx_lerp16(top, bot, fv));
    } else {
        ta = fx_texel_alpha(fx_axis_x(su >> 4, flags), fx_axis_y(sv >> 4, flags));
    }
    return min((ta * a) >> 7, 255u);
}
#endif

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
        // the UV in t1's own texels, 12.4 (u, v at scale 1)
        const float2 ss = fx_src_scale();
        const int su = int(round(float(u) * ss.x)) - fx_src_crop16();
        const int sv = int(round(float(v) * ss.y));
#ifdef ICO_FX_FRAME
        if ((flags & (FXF_ALPHA_ONLY | FXF_ABOVE1X)) == FXF_ALPHA_ONLY) {
            col = uint4(0u, 0u, 0u, fx_alpha_only(su, sv, col.a, flags));
        } else
#endif
        {
            col = fx_tfx(fx_sprite_texel(su, sv, flags), col, flags);
        }
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
        dst = fx_dest(px);
    }
    if ((flags & FXF_DATE) != 0u) {
        const bool msb = (dst.a & 0x80u) != 0u;
        if (msb != ((flags & FXF_DATM) != 0u)) {
            discard;
        }
    }

    uint4 res = col;
    const bool pabeOff = (flags & FXF_PABE) != 0u && (col.a & 0x80u) == 0u;
#ifdef ICO_FX_FRAME
    if ((flags & FXF_OUT_PREMUL) != 0u) {
        res.rgb = fx_premul(col, g_blend.x, g_blend.y); // the pipeline adds or takes Cd
    } else
#endif
    if ((flags & FXF_ABE) != 0u && !pabeOff) {
        res.rgb = gs_blend_int(col, dst, g_blend.x, g_blend.y, g_blend.z).rgb;
    }
    res.a = col.a | ((flags & FXF_FBA) != 0u ? 0x80u : 0u);
    if (keepDestAlpha) {
        res.a = dst.a;
    }
    return float4(res) * (1.0 / 255.0);
}

// fx_blur2_ps: a pair of the blur's passes (staticBlur.c blur(n): pass 1
// WORK0 -> WORK1 with the rect shrunk, pass 2 WORK1 -> WORK0 with the UVs
// shrunk) drawn as one pass into WORK0 (rd_replay_tiler.c rd__replay_blur_fold, the
// profile's blurFold). For a pixel of pass 2 it computes what fx_sprite_ps
// computes, where each of its four WORK1 taps is not loaded but computed:
// pass 1's sprite at that WORK1 texel (its UV from its rect, the four WORK0
// taps, gs_bilerp, the texture function with pass 1's colour, FBA), then
// TEXA as pass 2's fetch applies it. The integers are the two passes', in
// their order: the same bytes. The replay folds a pair only where that
// holds by construction: scale 1 and no crop on both targets (the same
// size, the same origin), both passes textured from the other target,
// LINEAR, CLAMP, no blend, no alpha test, no DATE, no destination read, the
// colour mask whole, no depth, the same flags, TEXA, TEX0 size and origin;
// every WORK1 tap pass 2 weighs (a weight above 0) inside the texels pass 1
// covers; and the next pair's pass 1 covering the same texels, so the pair
// drawn last as two passes leaves WORK1 as the chain did. The two textures
// may differ in size (WORK1 is 256 x 256, WORK0 256 x 128): each fetch
// clamps on its own.
// Bindings: t1 WORK0 as it was before the pair (its snapshot), t2 unused.
// DrawCB: pass 2's fields as fx_sprite_ps reads them (g_param its rect,
// g_uvRect its UVs, g_col its colour, g_aux1 its reciprocals and fx_texel's
// TEX0 size, the same for both, g_tex.zw WORK1's size), and pass 1's:
//   g_aux0   x x0 | y0 << 16, y x1 | y1 << 16 (its rect, 12.4),
//            z u0 | v0 << 16, w u1 | v1 << 16 (its UVs, 12.4)
//   g_scale  xy WORK0's size (its texture, t1: the clamp of its fetch),
//            z R + 256 G, w B + 256 A of its colour (integers held as
//            floats; at scale 1 without a crop fx_sprite_ps reads neither)
#ifndef ICO_UINT_SRC
// fx_texel's addressing at scale 1 without a crop on a texture of asz
// texels (g_scale holds pass 1's here)
int2 fx2_addr(int2 c, uint flags, int2 asz)
{
    const int2 lsz = int2(g_aux1.zw);
    c.x = (flags & FXF_CLAMP_S) != 0u ? clamp(c.x, 0, lsz.x - 1) : gs_wrap_repeat(c.x, lsz.x);
    c.y = (flags & FXF_CLAMP_T) != 0u ? clamp(c.y, 0, lsz.y - 1) : gs_wrap_repeat(c.y, lsz.y);
    return clamp(c, int2(0, 0), asz - 1);
}

// pass 1's fetch of WORK0 (t1, g_scale.xy texels)
uint4 fx2_texel(int2 c, uint flags)
{
    const uint4 t = fx_load(g_texture, fx2_addr(c, flags, int2(g_scale.xy)));
    return gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
}

// fx_sprite_ps's texture function
uint4 fx2_tfx(uint4 t, uint4 col, uint flags)
{
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
    return o;
}

// pass 1's pixel at WORK1 texel c (its GS window coordinate o + 16 c at
// scale 1), as WORK1 would hold it: fx_sprite_ps's arithmetic with pass
// 1's rect, UVs and colour (the UV divides exact, gs_div_trunc: the same
// truncated quotients as the reciprocal form), TEXA as pass 2's fetch
// applies it to the stored texel
uint4 fx2_pass1(int2 c, int2 o, uint flags)
{
    const int x0 = int(g_aux0.x & 0xFFFFu), y0 = int(g_aux0.x >> 16);
    const int x1 = int(g_aux0.y & 0xFFFFu), y1 = int(g_aux0.y >> 16);
    const int u0 = int(g_aux0.z & 0xFFFFu), v0 = int(g_aux0.z >> 16);
    const int u1 = int(g_aux0.w & 0xFFFFu), v1 = int(g_aux0.w >> 16);
    const int X = o.x + c.x * 16;
    const int Y = o.y + c.y * 16;
    const int u = u0 + gs_div_trunc((X - x0) * (u1 - u0), x1 - x0);
    const int v = v0 + gs_div_trunc((Y - y0) * (v1 - v0), y1 - y0);
    const int uu = u - 8, vv = v - 8;
    const int2 i0 = int2(uu >> 4, vv >> 4);
    const uint fu = uint(uu & 15), fv = uint(vv & 15);
    const uint4 a = fx2_texel(i0, flags);
    const uint4 b = fx2_texel(i0 + int2(1, 0), flags);
    const uint4 cc = fx2_texel(i0 + int2(0, 1), flags);
    const uint4 d = fx2_texel(i0 + int2(1, 1), flags);
    const uint rg = uint(g_scale.z), ba = uint(g_scale.w);
    const uint4 col = uint4(rg & 255u, rg >> 8, ba & 255u, ba >> 8);
    uint4 res = fx2_tfx(gs_bilerp(a, b, cc, d, fu, fv), col, flags);
    res.a = res.a | ((flags & FXF_FBA) != 0u ? 0x80u : 0u);
    return gs_texa_expand(res, g_mode.y & 0xFFu, g_mode.y >> 8);
}

float4 fx_blur2_ps(float4 pos : SV_Position) : SV_Target0
{
    const uint flags = g_mode.x;
    // pass 2's pixel as fx_sprite_ps has it (scale 1: ts is 1, g_vpo.z 0)
    const float2 ts = float2(g_z.y > 0.0 ? g_z.y : 1.0, g_z.z > 0.0 ? g_z.z : 1.0);
    const float2 fp = pos.xy + float2(g_vpo.z, 0.0);
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
    const int u0 = int(g_uvRect.x), v0 = int(g_uvRect.y);
    const int u1 = int(g_uvRect.z), v1 = int(g_uvRect.w);
    const int u = u0 + gs_div_trunc_rcp((X - x0) * (u1 - u0), x1 - x0, asfloat(g_aux1.x));
    const int v = v0 + gs_div_trunc_rcp((Y - y0) * (v1 - v0), y1 - y0, asfloat(g_aux1.y));
    // LINEAR (the fold's condition), at scale 1: su = u, sv = v
    const int uu = u - 8, vv = v - 8;
    const int2 i0 = int2(uu >> 4, vv >> 4);
    const uint fu = uint(uu & 15), fv = uint(vv & 15);
    // pass 2's fetch of WORK1 (g_tex.zw texels), each texel pass 1's pixel
    const int2 o = int2(ox, oy), w1 = int2(g_tex.zw);
    const uint4 a = fx2_pass1(fx2_addr(i0, flags, w1), o, flags);
    const uint4 b = fx2_pass1(fx2_addr(i0 + int2(1, 0), flags, w1), o, flags);
    const uint4 c = fx2_pass1(fx2_addr(i0 + int2(0, 1), flags, w1), o, flags);
    const uint4 d = fx2_pass1(fx2_addr(i0 + int2(1, 1), flags, w1), o, flags);
    uint4 res = fx2_tfx(gs_bilerp(a, b, c, d, fu, fv), g_col, flags);
    res.a = res.a | ((flags & FXF_FBA) != 0u ? 0x80u : 0u);
    return float4(res) * (1.0 / 255.0);
}
#endif
