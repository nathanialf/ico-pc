// sprite.hlsl: screen-space textured or untextured primitives in GS window
// coordinates (RD_PROG_SCREEN and RD_PROG_WORLD_PRIM): quads, strips,
// triangles, lines and points, whatever topology the pipeline sets.
//
// Vertex layout (IcoSpriteVertex in shader_consts.h, 20 bytes):
//   loc 0  RHI_VTX_U16x2_UINT   XY, GS 12.4 fixed point
//   loc 1  RHI_VTX_U32x1        Z, GS value (PSMZ24 scale unless FrameCB.g_z says otherwise)
//   loc 2  RHI_VTX_U8x4_UINT    RGBA 0..255, alpha 0x80 = 1.0
//   loc 3  RHI_VTX_F32x2        UV in texels of t1
// Entries: sprite_ui_vs and sprite_world_vs are the same mapping (no space
// transforms a position: a GS X is drawn where the game put it at every
// aspect); the two entries keep the draw's RD_SPACE_UI / WORLD tag in the
// pipeline key. sprite_ps serves both.
//
// sprite_aa1_ui_vs, sprite_aa1_world_vs and sprite_aa1_ps draw the lines
// and triangles PRIM.AA1 antialiases. Their vertex (IcoSpriteAa1Vertex,
// 24 bytes) adds
//   loc 4  RHI_VTX_F32x1        the coverage: 0..1 on the geometry
//                               rd_replay.c adds along an edge, interpolated
//                               without perspective; ICO_AA1_INTERIOR (2.0)
//                               on a triangle's own vertices
// and sprite_aa1_ps takes the coverage as the fragment's alpha.
//
// The planes build (ICO_SPRITE_PLANES=1, port/shaders/CMakeLists.txt):
// SpriteVSOut and SpriteStqVSOut carry the triangle's planes
// (SPRITE_PLANES) only there. sprite_plane_*_vs and sprite_stq_plane_*_vs
// exist only in it, and sprite_ps, sprite_texa_ps and sprite_stq_ps are
// built again with it as sprite_ps_planes, sprite_texa_ps_planes and
// sprite_stq_ps_planes (rd_screen_planes.c rd__plane_pass pairs them).
// Every other entry is the plain build: pos, colour and uv or stq, the
// layout font.hlsl, fog_lut.hlsl and raw_wrap.hlsl read too.
#include "common.hlsli"

struct SpriteVSIn {
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
};

// The planes of a screen-prim triangle (docs/port/RENDER.md, "Screen
// planes"; sprite_plane_*_vs, rd_screen_planes.c), flat: p0 the colour at the
// triangle's first corner, p1 and p2 its change per texel across and down;
// p3.xyz S, T, Q there (S and T normalised), p4.xyz and p5.xyz their
// changes; p3.w and p4.w the corner in texels from the target's centre,
// p5.w 1 when the planes hold (else the GPU's interpolation).  The GPU
// interpolates from the pixel's place in the target, which moves the
// Gouraud colours of the 4:3 columns by a step with the frame width; the
// planes are evaluated at the pixel's offset from the corner, the same
// numbers at every width.  The planes build alone (ICO_SPRITE_PLANES).
#ifdef ICO_SPRITE_PLANES
#define SPRITE_PLANES                                                                              \
    VK_LOC(2) nointerpolation float4 p0 : TEXCOORD3;                                               \
    VK_LOC(3) nointerpolation float4 p1 : TEXCOORD4;                                               \
    VK_LOC(4) nointerpolation float4 p2 : TEXCOORD5;                                               \
    VK_LOC(5) nointerpolation float4 p3 : TEXCOORD6;                                               \
    VK_LOC(6) nointerpolation float4 p4 : TEXCOORD7;                                               \
    VK_LOC(7) nointerpolation float4 p5 : TEXCOORD8;
#else
#define SPRITE_PLANES
#endif

struct SpriteVSOut {
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;   // 0..255, interpolated
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised
    SPRITE_PLANES
};

SpriteVSOut sprite_vertex(SpriteVSIn i)
{
    SpriteVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.uv = gs_uv_norm(i.uv);
#ifdef ICO_SPRITE_PLANES
    o.p0 = o.p1 = o.p2 = o.p3 = o.p4 = o.p5 = float4(0.0, 0.0, 0.0, 0.0);
#endif
    return o;
}

#ifdef ICO_SPRITE_PLANES
// The vertex of sprite_plane_*_vs and sprite_stq_plane_*_vs
// (IcoSpritePlaneVertex): SpriteVSIn, Q, and the triangle's planes per GS
// pixel (shader_consts.h)
struct SpritePlaneVSIn {
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
    VK_LOC(4) float q : TEXCOORD2;
    VK_LOC(5) float4 c0 : TEXCOORD3;
    VK_LOC(6) float4 cx : TEXCOORD4;
    VK_LOC(7) float4 cy : TEXCOORD5;
    VK_LOC(8) float4 s0 : TEXCOORD6;
    VK_LOC(9) float4 sx : TEXCOORD7;
    VK_LOC(10) float4 sy : TEXCOORD8;
};

// the planes per texel of the bound target (the GS pixel's changes over
// its texels per GS pixel), S and T normalised; none without the exact
// viewport
void sprite_planes(SpritePlaneVSIn i, out float4 p0, out float4 p1, out float4 p2, out float4 p3,
                   out float4 p4, out float4 p5)
{
    p0 = p1 = p2 = p3 = p4 = p5 = float4(0.0, 0.0, 0.0, 0.0);
    if (g_vp.x == 0.0) {
        return;
    }
    precise float2 k = 1.0 / g_vp.zw;
    precise float2 corner = gs_texel_offset(float2(i.s0.w, i.sx.w));
    precise float3 n = float3(g_tex.zw, 1.0);
    p0 = i.c0;
    precise float4 cx = i.cx * k.x;
    precise float4 cy = i.cy * k.y;
    // S less the Full pixel crop's share times Q (gs_stq_norm), per plane
    precise float3 c = float3(g_scale.z, 0.0, 0.0);
    precise float3 s0 = i.s0.xyz * n - c * i.s0.z;
    precise float3 sx = (i.sx.xyz * n - c * i.sx.z) * k.x;
    precise float3 sy = (i.sy.xyz * n - c * i.sy.z) * k.y;
    p1 = cx;
    p2 = cy;
    p3 = float4(s0, corner.x);
    p4 = float4(sx, corner.y);
    p5 = float4(sy, 1.0);
}

SpriteVSOut sprite_plane_vertex(SpritePlaneVSIn i)
{
    SpriteVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.uv = gs_stq_norm(i.uv, i.q).xy /
           i.q; // Q is 1 here (rd_screen_planes.c), read so the input is used
    sprite_planes(i, o.p0, o.p1, o.p2, o.p3, o.p4, o.p5);
    return o;
}

SpriteVSOut sprite_plane_ui_vs(SpritePlaneVSIn i)
{
    return sprite_plane_vertex(i);
}

SpriteVSOut sprite_plane_world_vs(SpritePlaneVSIn i)
{
    return sprite_plane_vertex(i);
}
#endif

// The pixel's colour and S, T, Q from the planes (p5.w 1), else the GPU's;
// the plain build has no planes: the GPU's
#ifdef ICO_SPRITE_PLANES
#define SPRITE_PLANE_ATTRS(i, col, stq)                                                            \
    sprite_plane_attrs(i.pos, i.p0, i.p1, i.p2, i.p3, i.p4, i.p5, col, stq)
void sprite_plane_attrs(float4 pos, float4 p0, float4 p1, float4 p2, float4 p3, float4 p4,
                        float4 p5, inout float4 col, inout float3 stq)
{
    if (p5.w == 0.0) {
        return;
    }
    float2 d = gs_plane_offset(pos, float2(p3.w, p4.w));
    precise float4 c = p0 + p1 * d.x + p2 * d.y;
    precise float3 s = p3.xyz + p4.xyz * d.x + p5.xyz * d.y;
    col = c;
    stq = s;
}
#else
#define SPRITE_PLANE_ATTRS(i, col, stq)
#endif

SpriteVSOut sprite_ui_vs(SpriteVSIn i)
{
    return sprite_vertex(i);
}

SpriteVSOut sprite_world_vs(SpriteVSIn i)
{
    return sprite_vertex(i);
}

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
// The DATE snapshot: R8, 1.0 where the bound target's alpha MSB
// was set, addressed in target pixels. Read only under DF_DATE; otherwise a
// 1x1 dummy is bound and never fetched.
Texture2D<float> g_dateSnap : register(t2, space2);

// gs_bspline_bilinear4 with its weights and places from kSpriteBspline
float4 sprite_bspline4(float2 tc, float2 size)
{
    // gs_bspline_bilinear4's per-fraction terms, one row per 4-bit fraction f:
    // x, y the two taps' weights (w0 + w1) / 24576 and (w2 + w3) / 24576, z, w
    // their places inside their texel pairs w1 / (w0 + w1) and
    // w3 / max(w2 + w3, 1) (gs_bspline_w's weights), each the float the
    // division rounds to
    static const float4 kSpriteBspline[16] = {
        float4(0.833333313, 0.166666672, 0.800000012, 0.0),
        float4(0.800211608, 0.199788406, 0.828384042, 0.000203665986),
        float4(0.763671875, 0.236328125, 0.853793681, 0.00137741049),
        float4(0.724202454, 0.275797516, 0.876559138, 0.00398347573),
        float4(0.682291687, 0.317708343, 0.896946549, 0.00819672085),
        float4(0.638427734, 0.361572266, 0.915168881, 0.0140670715),
        float4(0.593098938, 0.406901032, 0.9313941, 0.0216000006),
        float4(0.54679364, 0.45320639, 0.945750833, 0.0307954755),
        float4(0.5, 0.5, 0.958333313, 0.0416666679),
        float4(0.45320639, 0.54679364, 0.969204545, 0.0542491451),
        float4(0.406901032, 0.593098938, 0.978399992, 0.0686059296),
        float4(0.361572266, 0.638427734, 0.985932946, 0.0848311037),
        float4(0.317708343, 0.682291687, 0.991803288, 0.103053436),
        float4(0.275797516, 0.724202454, 0.996016502, 0.123440839),
        float4(0.236328125, 0.763671875, 0.998622596, 0.146206304),
        float4(0.199788406, 0.800211608, 0.999796331, 0.171615988)
    };
    const int2 c16 = int2(floor(tc * 16.0 + 0.5));
    const int2 i0 = (c16 - 8) >> 4;
    const uint2 f = uint2((c16 - 8) & 15);
    const float4 wx = kSpriteBspline[f.x], wy = kSpriteBspline[f.y];
    const float2 g0 = float2(wx.x, wy.x), g1 = float2(wx.y, wy.y);
    const float2 h0 = float2(i0) - 1.0 + float2(wx.z, wy.z) + 0.5;
    const float2 h1 = float2(i0) + 1.0 + float2(wx.w, wy.w) + 0.5;
    const float2 inv = 1.0 / size;
    return g0.y * (g0.x * g_texture.SampleLevel(g_sampler, float2(h0.x, h0.y) * inv, 0.0) +
                   g1.x * g_texture.SampleLevel(g_sampler, float2(h1.x, h0.y) * inv, 0.0)) +
           g1.y * (g0.x * g_texture.SampleLevel(g_sampler, float2(h0.x, h1.y) * inv, 0.0) +
                   g1.x * g_texture.SampleLevel(g_sampler, float2(h1.x, h1.y) * inv, 0.0));
}

// Above 1x (DF_ABOVE1X: a bilinear draw sampling a target at another scale
// than the bound target's, rd_replay.c fillDrawCB): the texel at the
// normalised uv, after TEXA, resampled by the step, in t1's texels per
// bound texel per axis (g_scale.w, step32x * 4096 + step32y, the sprite
// command's from the CPU: rd_fx_plan.c rd__a1x_sprite_steps; 0: the
// footprint of the pixel, ddx/ddy of the texel coordinate) through
// common.hlsli's kernels about the sample point (the coordinate in t1's
// 12.4 texels, as gs_texa_texture rounds it):
//   both axes under half a texel  the cubic B-spline as four bilinear taps
//                                 of the draw's own sampler
//                                 (sprite_bspline4: gs_bspline_bilinear4
//                                 with its terms from a table), TEXA after
//   an axis over two texels       the area box on that axis (the other
//                                 axis its own kernel), every tap loaded
//                                 with the draw's wrap reproduced on t1's
//                                 size and TEXA per texel (gs_texa_load),
//                                 combined separably in integers as
//                                 fx_sprite_ps combines them
// Returns false (t untouched) without the flag or when neither axis leaves
// the GS's filter: the caller samples as before.
bool sprite_above1x(float2 uv, out uint4 t)
{
    t = uint4(0u, 0u, 0u, 0u);
    if ((g_mode.x & DF_ABOVE1X) == 0u) {
        return false;
    }
    uint w, h;
    g_texture.GetDimensions(w, h);
    const int2 size = max(int2(int(w), int(h)), int2(1, 1));
    const float2 tc = uv * float2(size);
    const uint steps = uint(g_scale.w);
    int sx = int(steps >> 12), sy = int(steps & 4095u);
    [branch] if (steps == 0u) {
        const float2 dx = ddx(tc), dy = ddy(tc);
        const float2 k = max(abs(dx), abs(dy)) * 32.0 + 0.5;
        sx = int(min(floor(k.x), 4095.0));
        sy = int(min(floor(k.y), 4095.0));
    }
    const int kx = gs_rs_kind(sx, 1u), ky = gs_rs_kind(sy, 1u);
    if (kx == GS_RS_GS && ky == GS_RS_GS) {
        return false;
    }
    if (kx == GS_RS_BSPLINE && ky == GS_RS_BSPLINE) {
        const float4 f = sprite_bspline4(tc, float2(size));
        t = gs_texa_expand(uint4(floor(saturate(f) * 255.0 + 0.5)), g_mode.y & 0xFFu,
                           g_mode.y >> 8);
        return true;
    }
    const int2 c16 = int2(floor(tc * 16.0 + 0.5));
    const GsAxis ax = gs_resample_axis(c16.x, sx, true);
    const GsAxis ay = gs_resample_axis(c16.y, sy, true);
    const uint divx = gs_resample_div(ax), divy = gs_resample_div(ay);
    uint4 sum = uint4(0u, 0u, 0u, 0u);
    const int nx = min(ax.count, GS_RS_TAPS_MAX), ny = min(ay.count, GS_RS_TAPS_MAX);
    [loop] for (int j = 0; j < ny; j++) {
        uint4 row = uint4(0u, 0u, 0u, 0u);
        [loop] for (int i = 0; i < nx; i++) {
            row += gs_texa_load(g_texture, int2(ax.first + i, ay.first + j), size, g_mode.x) *
                   gs_resample_weight(ax, i);
        }
        sum += ((row * 16u + divx / 2u) / divx) * gs_resample_weight(ay, j);
    }
    t = (sum + 8u * divy) / (16u * divy);
    return true;
}

GS_PS_ATTR GS_PS_OUT sprite_ps(SpriteVSOut i)
{
    float3 st = float3(i.uv, 1.0);
    SPRITE_PLANE_ATTRS(i, i.col, st);
    i.uv = st.xy;
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        // UV primitives: the GS's level of detail with Q = 1 (gs_sample);
        // above 1x the step's kernel (sprite_above1x)
        uint4 t;
        if (!sprite_above1x(i.uv, t)) {
            const float4 tf = gs_sample(g_texture, g_sampler, i.uv, 1.0);
            t = uint4(floor(tf * 255.0 + 0.5));
            t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        }
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}

// sprite_texa_ps: sprite_ps for a PSMCT24 or PSMCT16 texture
// (or a 24- or 16-bit CLUT) under a TEXA with AEM and a linear filter, which
// the planner (rd_pipeline.c rd__texa_per_texel) gives this entry: TEXA per
// texel before the bilinear weights, as the GS (gs_texa_texture). sprite_ps
// itself is unchanged.
GS_PS_ATTR GS_PS_OUT sprite_texa_ps(SpriteVSOut i)
{
    float3 st = float3(i.uv, 1.0);
    SPRITE_PLANE_ATTRS(i, i.col, st);
    i.uv = st.xy;
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = (g_mode.x & DF_GS_LOD) != 0u
                      ? gs_texa_texture_lod(g_texture, i.uv, 1.0, g_mode.x)
                      : gs_texa_texture(g_texture, g_sampler, i.uv, g_mode.x);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}

// ------------------------------------------------------------------- PRIM.AA1

// sprite_ps's DATE, texture function and TEXA: the fragment's colour before
// the alpha test, or a discard (sprite_ps keeps its own copy).
uint4 sprite_colour(float4 pos, float4 vcol, float2 uv, float q)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(pos.xy), 0)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(vcol + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t;
        if ((g_mode.x & DF_GS_LOD) != 0u &&
            (g_mode.x & (DF_TEXA_MAG_LINEAR | DF_TEXA_MIN_LINEAR)) != 0u) {
            // GS levels in the Original preset (rd_pipeline.c
            // rd__gs_filter_int): the GS's integer filter at its LOD
            t = gs_texa_texture_lod(g_texture, uv, q, g_mode.x);
        } else if (!sprite_above1x(uv, t)) {
            t = uint4(floor(gs_sample(g_texture, g_sampler, uv, q) * 255.0 + 0.5));
            t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        }
        col = gs_texture_function(t, col, g_mode.x);
    }
    return col;
}

struct SpriteAa1VSIn {
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
    VK_LOC(4) float cov : TEXCOORD2;
};

struct SpriteAa1VSOut {
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;
    VK_LOC(1) float2 uv : TEXCOORD0;
    VK_LOC(2) noperspective float cov : TEXCOORD1;
};

SpriteAa1VSOut sprite_aa1_vertex(SpriteAa1VSIn i)
{
    SpriteAa1VSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.uv = gs_uv_norm(i.uv);
    o.cov = i.cov;
    return o;
}

SpriteAa1VSOut sprite_aa1_ui_vs(SpriteAa1VSIn i)
{
    return sprite_aa1_vertex(i);
}

SpriteAa1VSOut sprite_aa1_world_vs(SpriteAa1VSIn i)
{
    return sprite_aa1_vertex(i);
}

// The coverage alpha: a = the coverage on the
// 0x80 scale for an edge pixel (16-bit coverage >> 9, so 0..0x7F), 0x80 for
// an interior one. With PRIM.ABE 0 (DF_AA1_FULL) a replaces the fragment's
// alpha; with ABE 1 it replaces it only where that alpha is exactly 0x80.
// This happens before the alpha test, and the alpha that results is the
// blend's As and the alpha written.
GS_PS_ATTR GS_PS_OUT sprite_aa1_ps(SpriteAa1VSOut i)
{
    uint4 col = sprite_colour(i.pos, i.col, i.uv, 1.0);
    uint a = 0x80u;
    if (i.cov < 1.5) {
        a = uint(floor(saturate(i.cov) * 65535.0)) >> 9;
    }
    if ((g_mode.x & DF_AA1_FULL) != 0u || col.a == 0x80u) {
        col.a = a;
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}

// ------------------------------------------------------------ perspective STQ

// The vertex of sprite_stq_*_vs (IcoSpriteStqVertex, 24 bytes) adds
//   loc 4  RHI_VTX_F32x1   Q
// to the four of SpriteVSIn; loc 3 holds S and T in texels of t1 (times the
// texture size, not divided by Q). The GS interpolates S, T and Q in screen
// space and divides per pixel: the vertex shader passes (s, t, q) without
// perspective, sprite_stq_ps divides. Colour stays screen-space linear as
// everywhere. Drawn only for the screen prims whose Q is not 1; every other prim keeps sprite_ps.
struct SpriteStqVSIn {
    VK_LOC(0) uint2 xy : POSITION;
    VK_LOC(1) uint z : TEXCOORD1;
    VK_LOC(2) uint4 col : COLOR0;
    VK_LOC(3) float2 uv : TEXCOORD0;
    VK_LOC(4) float q : TEXCOORD2;
};

struct SpriteStqVSOut {
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;
    VK_LOC(1) noperspective float3 stq : TEXCOORD0; // s, t normalised, q
    SPRITE_PLANES
};

SpriteStqVSOut sprite_stq_vertex(SpriteStqVSIn i)
{
    SpriteStqVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.stq = gs_stq_norm(i.uv, i.q);
#ifdef ICO_SPRITE_PLANES
    o.p0 = o.p1 = o.p2 = o.p3 = o.p4 = o.p5 = float4(0.0, 0.0, 0.0, 0.0);
#endif
    return o;
}

#ifdef ICO_SPRITE_PLANES
// sprite_stq_vertex with the triangle's planes (IcoSpritePlaneVertex)
SpriteStqVSOut sprite_stq_plane_vertex(SpritePlaneVSIn i)
{
    SpriteStqVSOut o;
    o.pos = float4(gs_xy_to_ndc(i.xy), gs_depth(i.z), 1.0);
    o.col = float4(i.col);
    o.stq = gs_stq_norm(i.uv, i.q);
    sprite_planes(i, o.p0, o.p1, o.p2, o.p3, o.p4, o.p5);
    return o;
}

SpriteStqVSOut sprite_stq_plane_ui_vs(SpritePlaneVSIn i)
{
    return sprite_stq_plane_vertex(i);
}

SpriteStqVSOut sprite_stq_plane_world_vs(SpritePlaneVSIn i)
{
    return sprite_stq_plane_vertex(i);
}
#endif

SpriteStqVSOut sprite_stq_ui_vs(SpriteStqVSIn i)
{
    return sprite_stq_vertex(i);
}

SpriteStqVSOut sprite_stq_world_vs(SpriteStqVSIn i)
{
    return sprite_stq_vertex(i);
}

GS_PS_ATTR GS_PS_OUT sprite_stq_ps(SpriteStqVSOut i)
{
    SPRITE_PLANE_ATTRS(i, i.col, i.stq);
    uint4 col = sprite_colour(i.pos, i.col, i.stq.xy / i.stq.z, i.stq.z);
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}

// date_snap_ps: the bound target's alpha MSB into the R8 DATE
// snapshot, pixel for pixel (t1 is the target, read with Load; viewport =
// the target's size). Drawn with blit_vs.
struct DateSnapIn {
    float4 pos : SV_Position;
};

float date_snap_ps(DateSnapIn i) : SV_Target0
{
    float a = g_texture.Load(int3(int2(i.pos.xy), 0)).a;
    return a * 255.0 >= 127.5 ? 1.0 : 0.0;
}

// DATE through the stencil (rd_replay_tiler.c rd__replay_date_take: a DATE draw's
// destination alpha test without the R8 snapshot, inside the draw's own
// pass; RD_DATE_STENCIL_BIT). Both drawn with blit_vs over the area the
// draw can touch (the scissor), colour mask 0, the scene depth bound with
// its test ALWAYS and no write:
//   date_clear_ps    no output and no read: the pipeline's stencil
//                    (RD_STENCIL_DATE_CLEAR) writes 0 into bit 6 over the
//                    area
//   date_stencil_ps  the colour attachment read as an input attachment
//                    (Vulkan local read, after the colour barrier): the
//                    fragment is kept where the alpha's MSB is set, as
//                    date_snap_ps writes 1 into the snapshot, and the
//                    pipeline's stencil (RD_STENCIL_DATE_SET) writes 1 into
//                    bit 6 there. Group 2 rd's layoutLocal (t1 the input
//                    attachment). Its DXIL reads t1 as a texture (never
//                    used: D3D12 has no local read). Built with
//                    ICO_DATE_STENCIL=1 (the other entries keep no input
//                    attachment).
// The DATE draw then tests bit 6 (RD_STENCIL_DATE_TEST0 / TEST1 for DATM 0
// / 1) with DF_DATE off, so it sees the alpha as it was before its first
// primitive, as the snapshot did.
void date_clear_ps(DateSnapIn i)
{
}

// built with ICO_DATE_STENCIL=1 alone: the input attachment's declaration
// adds the InputAttachment capability to every entry of the file
#if defined(ICO_DATE_STENCIL) && defined(__spirv__)
[[vk::input_attachment_index(0)]] SubpassInput<float4> g_dateIn : register(t1, space2);
#define DATE_LOCAL_READ 1
#endif

void date_stencil_ps(DateSnapIn i)
{
#ifdef DATE_LOCAL_READ
    float a = g_dateIn.SubpassLoad().a;
#else
    float a = g_texture.Load(int3(int2(i.pos.xy), 0)).a;
#endif
    if (!(a * 255.0 >= 127.5)) {
        discard;
    }
}
