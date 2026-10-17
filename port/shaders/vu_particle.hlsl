// vu_particle.hlsl: particle (RD_PROG_PARTICLE), START_PARTICLE
// (particle.vsm:79-149) for prim_DispParticle. The stream holds the batch as
// the VU gets it (PrimParticleObj from num on): qw 0 = count, 1..2 GIF tags,
// 3 clip minimum, 4 clip maximum, 5 = (size scale, du, dv, -), then two
// qwords per particle: (x, y, z, size), (u, v, grey, alpha). vu_draw.x =
// the batch's qw 0. Matrices (SET_PARTICLE_MATRIX): vu_mem[16..19] = model
// to GS screen (vf01..vf04), vu_mem[20..23] = the screen matrix
// (vf05..vf08). Drawn non-indexed, six vertices per particle: a GS sprite
// (PRIM 0xD6: TME, ABE, AA1) between the two corners, ST from (u, v) to
// (u + du, v + dv) with Q = 1, flat colour.  vu_particle_vs draws a sprite
// whole in each of its six invocations; with the record (vu_common.hlsli,
// the record stages) vu_particle_pre_vs makes its verdict and planes once
// and vu_particle_main_vs draws it from them (with vu_ps).
#include "vu_common.hlsli"

struct VuSprite {
    // ftoi4 of corner 0 (x - e, y - e) and Z, of corner 1 (x + e, y + e)
    // (scalars: no vector built whole whose lane nothing reads, which the
    // optimiser leaves undefined: vu_particle_pre_vs reads no Z)
    int c0x, c0y, c0z;
    int c1x, c1y, c1z;
    uint4 rgba;
    float4 st;  // u0, v0, u1, v1
    bool drawn; // the VU's test: alpha non-zero and both corners inside the clip window
    bool shown; // what the port can draw: alpha non-zero and w inside the window
};

VuSprite vu_particle(uint i)
{
    VuSprite o = (VuSprite)0;
    uint base = vu_draw.x;
    float4 clipMin = vu_stream[base + 3u];
    float4 clipMax = vu_stream[base + 4u];
    float4 k = vu_stream[base + 5u];
    float4 a = vu_stream[base + 6u + 2u * i];
    float4 b = vu_stream[base + 7u + 2u * i];
    // :97-104 h = M * (x, y, z, 1)
    float4 h = vu_matm(16u, float4(a.xyz, 1.0), 1.0);
    // :99-112 e = S * (size * k.x, size * k.x, 0, 1): the half extent
    precise float sz = (0.0 + a.w) * k.x;
    float4 e = vu_matm(20u, float4(sz, sz, 0.0, 1.0), 1.0);
    // :108 div; :116-121 corners h -/+ e on xy, xyz divided, w stays h.w
    float q = vu_rcp(h.w);
    // (Z apart: a vector lane nothing reads is left undefined by the
    // optimiser, and vu_particle_pre_vs reads no Z; the same products)
    precise float2 p0 = (h.xy - e.xy) * q;
    precise float2 p1 = (h.xy + e.xy) * q;
    precise float pz = h.z * q;
    // :125-133 both corners strictly inside the window on x, y and w;
    // :100-105 a zero alpha skips the particle
    bool alphaZero = (asuint(b.w) & 0x7F800000u) == 0u;
    o.drawn = !alphaZero && vu_inside(p0, h.w, clipMin, clipMax) != 0u &&
              vu_inside(p1, h.w, clipMin, clipMax) != 0u;
    // Out of the box the PS2's test; beside the picture and outside the
    // out-of-the-box state drawn (vu_particle_vs; docs/port/VU1_PROGRAMS.md,
    // "The region rule").
    // The port keeps the alpha test and the w window (the particle in
    // front of the eye and before the far limit); a sprite outside the x/y
    // window (1024..3071, prim_InitParticleByPartition) is dropped when its
    // quad reaches the camera's picture, as the triangles' region test is,
    // and drawn whole beside it (vu_particle_vs). That window is fixed in
    // GS pixels, so on a wide frame it ends at the frame's edge at 48:9 and
    // inside it at 60:9, where a sprite with a corner past it would vanish
    // whole.
    o.shown = !alphaZero && clipMin.w < h.w && h.w < clipMax.w;
    o.c0x = vu_ftoi4(p0.x);
    o.c0y = vu_ftoi4(p0.y);
    o.c0z = vu_ftoi4(pz);
    o.c1x = vu_ftoi4(p1.x);
    o.c1y = vu_ftoi4(p1.y);
    o.c1z = vu_ftoi4(pz);
    // :118-124 RGBA = ftoi0(grey, grey, grey, alpha)
    o.rgba = vu_rgba(float4(b.z, b.z, b.z, b.w));
    // :106-123 ST0 = (u, v, 1), ST1 = (u + du, v + dv, 1)
    precise float2 st1 = b.xy + k.yz;
    o.st = float4(b.xy, st1);
    return o;
}

uint vu_particle_probe(VuSprite s, uint field)
{
    uint r = 0u;
    if (field == 0u) {
        r = asuint(s.c0x);
    } else if (field == 1u) {
        r = asuint(s.c0y);
    } else if (field == 2u) {
        r = asuint(s.c1x);
    } else if (field == 3u) {
        r = asuint(s.c1y);
    } else if (field == 4u) {
        r = asuint(s.c0z);
    } else if (field == 5u) {
        r = s.rgba.x | (s.rgba.y << 8) | (s.rgba.z << 16) | (s.rgba.w << 24);
    } else if (field == 6u) {
        r = asuint(s.st.x);
    } else if (field == 7u) {
        r = asuint(s.st.y);
    } else if (field == 8u) {
        r = asuint(s.st.z);
    } else if (field == 9u) {
        r = asuint(s.st.w);
    } else if (field == 10u) {
        r = s.drawn ? 1u : 0u;
    } else if (field == 11u) {
        r = asuint(s.c1z);
    } else if (field == 12u) {
        r = s.shown ? 1u : 0u;
    }
    return r;
}

// Whether tick t (0 the tick before, 1 this tick) showed the sprite of
// position h (the blended camera's) and half extent e (the screen
// matrix's, the same at every camera): at the tick's camera (h_t = R_t h,
// vu_ticks) the VU draws it (both corners inside the x/y window, w inside
// its window) and its quad reaches the frame (vu_frame_ndc)
bool vu_particle_tick(float4 h, float4 e, uint t, float4 clipMin, float4 clipMax, float4 frame)
{
    uint b = 4u * t;
    precise float4 hk = vu_ticks[b] * h.x + vu_ticks[b + 1u] * h.y + vu_ticks[b + 2u] * h.z +
                        vu_ticks[b + 3u] * h.w;
    float q = vu_rcp(hk.w);
    precise float2 p0 = (hk.xy - e.xy) * q;
    precise float2 p1 = (hk.xy + e.xy) * q;
    if (vu_inside(p0, hk.w, clipMin, clipMax) == 0u || vu_inside(p1, hk.w, clipMin, clipMax) == 0u) {
        return false;
    }
    float2 n0 = vu_ndc(float2(vu_ftoi4(p0.x), vu_ftoi4(p0.y)) * (1.0 / 16.0));
    float2 n1 = vu_ndc(float2(vu_ftoi4(p1.x), vu_ftoi4(p1.y)) * (1.0 / 16.0));
    float2 lo = min(n0, n1);
    float2 hi = max(n0, n1);
    return lo.x < frame.y && hi.x > frame.x && lo.y < frame.z && hi.y > frame.w;
}

// The between-ticks rule for the x/y window (vu_common.hlsli vu_region_rule):
// whether a tick showed sprite i, seen through a camera blended between
// two ticks' (VU_F_TICKS): at tick k's camera (h_k = R_k h, vu_ticks; the
// half extent e is the screen matrix's, the same at every camera) the VU
// draws it (alpha, both corners inside the x/y window, w inside its
// window) and its quad reaches the frame (vu_frame_ndc).  The two ticks are
// written out (k = 0, then 1).
bool vu_particle_tick_shown(uint i)
{
    uint base = vu_draw.x;
    float4 clipMin = vu_stream[base + 3u];
    float4 clipMax = vu_stream[base + 4u];
    float4 k = vu_stream[base + 5u];
    float4 a = vu_stream[base + 6u + 2u * i];
    float4 h = vu_matm(16u, float4(a.xyz, 1.0), 1.0);
    precise float sz = (0.0 + a.w) * k.x;
    float4 e = vu_matm(20u, float4(sz, sz, 0.0, 1.0), 1.0);
    float4 frame = vu_frame_ndc();
    return vu_particle_tick(h, e, 0u, clipMin, clipMax, frame) ||
           vu_particle_tick(h, e, 1u, clipMin, clipMax, frame);
}

VuVSOut vu_particle_vs(uint vid : SV_VertexID)
{
    // the particle, or the tests' probe particle
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    VuSprite s = vu_particle(probe ? vu_probe_vertex(vid) : vid / 6u);
    if (probe) {
        return vu_probe_out(vid, vu_particle_probe(s, (vid / 3u) % VU_PROBE_FIELDS));
    }
    VuVSOut o = vu_out_init();
    if (!s.shown) {
        return o;
    }
    // A sprite the PS2 skips for its x/y window, as the region test's
    // triangles (vu_region_rule): out of the box not drawn when its quad
    // reaches into the camera's picture (its columns and its rows:
    // vu_rect_reaches_picture), drawn whole (the GPU clips the quad) when
    // wholly beside it, under photo mode's free camera, and in any other
    // state (VU_F_EXTENDED: a sprite is an effect, never a card
    // parked at the picture's edge; the shown test keeps it in front of the
    // eye). Out of the box between two ticks (VU_F_TICKS): drawn whole
    // when a tick showed it (vu_particle_tick_shown), as a triangle the
    // region rule drops at the blended camera is (vu_region_rule).
    if (!s.drawn && (vu_draw.z & (VU_F_BEHIND_EYE | VU_F_EXTENDED)) == 0u) {
        float2 p0 = vu_ndc(float2(int2(s.c0x, s.c0y)) * (1.0 / 16.0));
        float2 p1 = vu_ndc(float2(int2(s.c1x, s.c1y)) * (1.0 / 16.0));
#ifdef ICO_VU_PLAIN
        // the plain variant: no tick cameras (vu_common.hlsli)
        if (vu_rect_reaches_picture(min(p0, p1), max(p0, p1))) {
            return o;
        }
#else
        if (vu_rect_reaches_picture(min(p0, p1), max(p0, p1)) &&
            ((vu_draw.z & VU_F_TICKS) == 0u || !vu_particle_tick_shown(vid / 6u))) {
            return o;
        }
#endif
    }
    // the sprite's planes (vu_common.hlsli vu_attr_planes), under the
    // exact viewport: S runs across, T down, the same on both triangles
    // (made before the sprite's own values, so none of them is a value
    // chosen after it is computed)
    if (g_vp.x != 0.0) {
        float2 p0 = vu_texel_offset_px(float2(int2(s.c0x, s.c0y)) * (1.0 / 16.0));
        float2 p1 = vu_texel_offset_px(float2(int2(s.c1x, s.c1y)) * (1.0 / 16.0));
        float4 col = float4(s.rgba);
        o = vu_attr_planes_at(o, p0, float2(p1.x, p0.y), float2(p0.x, p1.y), col, col, col,
                              float3(s.st.xy, 1.0), float3(s.st.z, s.st.y, 1.0),
                              float3(s.st.x, s.st.w, 1.0));
    }
    // corners of the two triangles: (0,0) (1,0) (0,1), (0,1) (1,0) (1,1)
    uint c = vid % 6u;
    bool right = c == 1u || c == 4u || c == 5u;
    bool down = c == 2u || c == 3u || c == 5u;
    o.pos = vu_gs_position(right ? s.c1x : s.c0x, down ? s.c1y : s.c0y, s.c1z, false);
    o.col = float4(s.rgba);
    o.stq = float3(right ? s.st.z : s.st.x, down ? s.st.w : s.st.y, 1.0);
    return o;
}

// ------------------------------------------- the record (vu_common.hlsli)

// The vertex pre-pass's sprite stage: sprite id's drop verdict and planes
// as vu_particle_vs makes them, once a sprite; drawn twice, vu_tri.w 0
// writing slot A (vu_tri.x + id: the flags, the colour, r.xy; x1; y1) and 1
// slot B (vu_tri.y + id: a1; x0; y0).  The flags: VU_TF_KEPT the sprite is
// drawn, VU_TF_EXACT its planes hold (the exact viewport)
VuPreOut vu_particle_pre_vs(uint id : SV_VertexID)
{
    VuSprite s = vu_particle(id);
    uint kept = s.shown ? 1u : 0u;
    // vu_particle_vs's drop
    if (kept != 0u && !s.drawn && (vu_draw.z & (VU_F_BEHIND_EYE | VU_F_EXTENDED)) == 0u) {
        float2 p0 = vu_ndc(float2(int2(s.c0x, s.c0y)) * (1.0 / 16.0));
        float2 p1 = vu_ndc(float2(int2(s.c1x, s.c1y)) * (1.0 / 16.0));
        if (vu_rect_reaches_picture(min(p0, p1), max(p0, p1)) &&
            ((vu_draw.z & VU_F_TICKS) == 0u || !vu_particle_tick_shown(id))) {
            kept = 0u;
        }
    }
    // vu_particle_vs's planes
    VuVSOut o = vu_out_init();
    if (g_vp.x != 0.0) {
        float2 p0 = vu_texel_offset_px(float2(int2(s.c0x, s.c0y)) * (1.0 / 16.0));
        float2 p1 = vu_texel_offset_px(float2(int2(s.c1x, s.c1y)) * (1.0 / 16.0));
        float4 col = float4(s.rgba);
        o = vu_attr_planes_at(o, p0, float2(p1.x, p0.y), float2(p0.x, p1.y), col, col, col,
                              float3(s.st.xy, 1.0), float3(s.st.z, s.st.y, 1.0),
                              float3(s.st.x, s.st.w, 1.0));
    }
    uint f = (kept != 0u ? VU_TF_KEPT : 0u) | (g_vp.x != 0.0 ? VU_TF_EXACT : 0u);
    uint4 a0 = uint4(f, vu_rec_pack_rgba(o.a0), asuint(o.r.x), asuint(o.r.y));
    uint4 a1 = asuint(o.x1), a2 = asuint(o.y1);
    uint4 b0 = asuint(o.a1), b1 = asuint(o.x0), b2 = asuint(o.y0);
    // slot A or B, chosen value by value
    bool sb = vu_tri.w != 0u;
    uint4 r0, r1, r2;
    r0.x = sb ? b0.x : a0.x;
    r0.y = sb ? b0.y : a0.y;
    r0.z = sb ? b0.z : a0.z;
    r0.w = sb ? b0.w : a0.w;
    r1.x = sb ? b1.x : a1.x;
    r1.y = sb ? b1.y : a1.y;
    r1.z = sb ? b1.z : a1.z;
    r1.w = sb ? b1.w : a1.w;
    r2.x = sb ? b2.x : a2.x;
    r2.y = sb ? b2.y : a2.y;
    r2.z = sb ? b2.z : a2.z;
    r2.w = sb ? b2.w : a2.w;
    return vu_rec_point_out((sb ? vu_tri.y : vu_tri.x) + id, r0, r1, r2);
}

// Sprite vid / 6 as vu_particle_vs draws it, its drop verdict and planes
// from its record (slot A's flags; the planes from slots A and B,
// vu_rec_planes)
VuVSOut vu_particle_main_vs(uint vid : SV_VertexID)
{
    // the particle, or the tests' probe particle
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    VuSprite s = vu_particle(probe ? vu_probe_vertex(vid) : vid / 6u);
    if (probe) {
        return vu_probe_out(vid, vu_particle_probe(s, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint i = vid / 6u;
    VuVSOut o = vu_out_init();
    uint4 a0 = vu_rec_buf[vu_tri.x + i];
    uint f = a0.x;
    if ((f & VU_TF_KEPT) == 0u) {
        return o;
    }
    // corners of the two triangles: (0,0) (1,0) (0,1), (0,1) (1,0) (1,1)
    uint c = vid % 6u;
    bool right = c == 1u || c == 4u || c == 5u;
    bool down = c == 2u || c == 3u || c == 5u;
    o.pos = vu_gs_position(right ? s.c1x : s.c0x, down ? s.c1y : s.c0y, s.c1z, false);
    o.col = float4(s.rgba);
    o.stq = float3(right ? s.st.z : s.st.x, down ? s.st.w : s.st.y, 1.0);
    if ((f & VU_TF_EXACT) != 0u) {
        o = vu_rec_planes(o, a0, vu_tri.x + i, vu_tri.y + i);
    }
    return o;
}
