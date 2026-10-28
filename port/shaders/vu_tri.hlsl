// vu_tri.hlsl: the vertex and triangle pre-passes' stages and the main
// vertex shader of every mesh program (docs/port/RENDER.md, "The vertex and
// triangle pre-passes"; rd_replay_vu.c rd__vu_prepass, doVuDraw).
//   vu_pre_ps            a pre-pass point's record (vu_common.hlsli
//                        vu_pre_out, vu_rec_point_out) into the three
//                        RGBA32_UINT targets
//   vu_tri_rule_pre_vs   one point a triangle (kick vu_rec.z + 2 + id): its
//                        verdict up to the tick cameras, its STQ planes
//                        (record slot A)
//   vu_tri_tick_pre_vs   one tick camera's test (vu_tri.w) a triangle the
//                        blended camera drops (slot C, x or y by the
//                        colour mask)
//   vu_tri_planes_pre_vs its colour planes and the STQ planes' change down
//                        (slot B)
//   vu_main_vs           the triangle kick-2, kick-1, kick of every mesh
//                        program (normal_c, normal_l, cluster, mesh) at index
//                        value kick * 4 + corner, as the program's own entry
//                        draws it: the corner from the vertex record, the
//                        verdict and the planes from the triangle's
// The program's choices come from VuCB (vu_rec.w: the clip mode,
// VU_TRI_CHAIN, VU_TRI_HIDE; vu_lo, vu_hi: the region test's bounds) instead
// of being written in an entry.
#include "vu_common.hlsli"

// --------------------------------------------------- the record stages

// slot A: the flags (vu_common.hlsli VU_TF_*), the anchor's colour and
// place, the STQ planes' change across and down with the plane distance's
VuPreOut vu_tri_rule_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    uint kick = vu_rec.z + 2u + id;
    uint rule = vu_rec.w;
    uint mode = rule & VU_CLIP_MASK;
    VuVtx a = vu_rec_vertex(kick - 2u);
    VuVtx b = vu_rec_vertex(kick - 1u);
    VuVtx c = vu_rec_vertex(kick);
    uint marks = vu_kick_marks(kick);
    uint gsPos = vu_has_gs_position(a) & vu_has_gs_position(b) & vu_has_gs_position(c);
    uint f = gsPos != 0u ? VU_TF_GSPOS : 0u;
    if ((rule & VU_TRI_HIDE) != 0u && vu_content_hidden(marks)) {
        f |= VU_TF_HIDDEN;
    }
    if (mode == VU_CLIP_REGION) {
        // vu_region_rule up to its tick tail (vu_tri_tick_pre_vs; vu_main_vs
        // resolves it), and vu_rule_card's condition
        uint r = VU_RULE_EXACT;
        if ((a.inside & b.inside & c.inside) == 0u) {
            if ((vu_draw.z & VU_F_BEHIND_EYE) != 0u) {
                r = VU_RULE_CLIP;
            } else {
                float4 pic = vu_picture_ndc();
                VuCutZ z = vu_cut_z(vu_cut_position(a), vu_cut_position(b), vu_cut_position(c));
                r = vu_region_rule_cut(a, b, c, z, pic);
            }
        }
        f |= r;
        if ((marks & VU_MARK_JOINED) == 0u && vu_strip_card(kick)) {
            f |= VU_TF_CARD;
        }
    } else if (mode == VU_CLIP_NONE) {
        // vu_triangle_out_pos's exact for clip mode none
        uint wrap = (a.gx & 0xFFFF) == a.gx && (a.gy & 0xFFFF) == a.gy && (b.gx & 0xFFFF) == b.gx &&
                            (b.gy & 0xFFFF) == b.gy && (c.gx & 0xFFFF) == c.gx &&
                            (c.gy & 0xFFFF) == c.gy
                        ? 1u
                        : 0u;
        f |= wrap != 0u ? VU_TF_WRAP | VU_TF_EXACT : 0u;
    } else {
        // vu_triangle_out_pos's VU_CLIP_SCISSOR without the draw's
        // VU_F_CUT_ONLY / VU_F_KICK_ONLY (vu_main_vs applies them)
        uint fa = a.clipf, fb = b.clipf, fc = c.clipf;
        uint at = 0u, exact = 0u;
        if ((fa | fb | fc) == 0u) {
            at = 1u; // kicked as it is
            exact = 1u;
        } else if ((fa & fb & fc) != 0u) {
            // trivially rejected
        } else if (((fa | fb | fc) & 0x30u) != 0u || min(a.h.w, min(b.h.w, c.h.w)) <= 0.0) {
            at = 2u; // SCISSOR_COMMON; the GPU clips
            exact = gsPos;
        } else {
            at = 1u; // only x/y flags: the GS scissor does the rest
            exact = 1u;
        }
        f |= (at << VU_TF_AT_SHIFT) | (exact != 0u ? VU_TF_EXACT : 0u);
        f |= (fa | fb | fc) != 0u ? VU_TF_CUT : 0u;
    }
    // the planes as vu_mesh_out makes them (vu_attr_planes, vu_plane_out),
    // whether they hold or not (vu_main_vs says which)
    VuPlaneTri t = vu_plane_tri(a, b, c);
    f |= t.k << VU_TF_ANCHOR_SHIFT;
    VuVSOut p = vu_attr_planes(vu_out_init(), t, a, b, c, 1u);
    if ((rule & VU_TRI_CHAIN) != 0u) {
        if (vu_batch.z != 0u) {
            p = vu_plane_out(p, t, kick, 0u);
            float da = vu_plane_distance(kick - 2u), db = vu_plane_distance(kick - 1u);
            float dc = vu_plane_distance(kick);
            if (da < 0.0 && db < 0.0 && dc < 0.0) {
                f |= VU_TF_PAST;
            }
        }
        // vu_later_out's condition, under the mark (1 or 0 each: flags
        // combined after they are computed are uints)
        uint zmax = max(asuint(a.gz), max(asuint(b.gz), asuint(c.gz)));
        uint later = ((marks & VU_MARK_LATER) != 0u ? 1u : 0u) & gsPos &
                     (zmax < 0xF0000000u ? 1u : 0u);
        f |= later != 0u ? VU_TF_LATER : 0u;
    }
    uint4 r0 = uint4(f, vu_rec_pack_rgba(p.a0), asuint(p.r.x), asuint(p.r.y));
    return vu_rec_point_out(vu_tri.x + kick, r0, asuint(p.x1), asuint(p.y1));
}

// slot C, the component vu_tri.w (the draw's colour mask writes it alone):
// whether tick vu_tri.w showed the triangle (vu_tick_shown), for a
// triangle of a VU_F_TICKS draw in the region mode the blended camera's
// verdict can drop, else 0; vu_main_vs reads it only where that verdict
// (slot A) is VU_RULE_DROP.  Can drop: not wholly inside, not
// VU_F_BEHIND_EYE, and not under the card test with every corner in front
// of the eye (VU_F_EXTENDED, vu_strip.y, every h.w > 0: vu_region_rule_cut
// then gives VU_RULE_CLIP or VU_RULE_STRIP whatever the cut says; NaN
// fails a comparison, so such a corner keeps the test), the conditions
// alone, without the cut (in a wide frame most of the triangles beside the
// picture are in front of the eye: the tick tests are left out for them)
VuPreOut vu_tri_tick_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    uint kick = vu_rec.z + 2u + id;
    VuVtx a = vu_rec_vertex(kick - 2u);
    VuVtx b = vu_rec_vertex(kick - 1u);
    VuVtx c = vu_rec_vertex(kick);
    uint s = 0u;
    bool strip = (vu_draw.z & VU_F_EXTENDED) != 0u && vu_strip.y != 0u && a.h.w > 0.0 &&
                 b.h.w > 0.0 && c.h.w > 0.0;
    if ((vu_rec.w & VU_CLIP_MASK) == VU_CLIP_REGION && (a.inside & b.inside & c.inside) == 0u &&
        (vu_draw.z & VU_F_BEHIND_EYE) == 0u && (vu_draw.z & VU_F_TICKS) != 0u && !strip) {
        float4 pic = vu_picture_ndc();
        float4 frame = vu_frame_ndc();
        s = vu_tick_shown(a, b, c, vu_tri.w, vu_lo, vu_hi, pic, frame);
    }
    uint4 z4 = uint4(0u, 0u, 0u, 0u);
    return vu_rec_point_out(vu_tri.z + kick, uint4(s, s, 0u, 0u), z4, z4);
}

// slot B: the STQ planes' values at the anchor with the plane distance's,
// the colour planes (vu_attr_planes, vu_plane_out)
VuPreOut vu_tri_planes_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    uint kick = vu_rec.z + 2u + id;
    VuVtx a = vu_rec_vertex(kick - 2u);
    VuVtx b = vu_rec_vertex(kick - 1u);
    VuVtx c = vu_rec_vertex(kick);
    VuPlaneTri t = vu_plane_tri(a, b, c);
    VuVSOut p = vu_attr_planes(vu_out_init(), t, a, b, c, 1u);
    if ((vu_rec.w & VU_TRI_CHAIN) != 0u && vu_batch.z != 0u) {
        p = vu_plane_out(p, t, kick, 0u);
    }
    return vu_rec_point_out(vu_tri.y + kick, asuint(p.a1), asuint(p.x0), asuint(p.y0));
}

// ------------------------------------------------------- the main shader

// The triangle kick-2, kick-1, kick at its corner as vu_mesh_out draws it:
// the corner's record (vu_rec_vertex), the triangle's (slot A; slot C for
// the tick cameras' verdicts), the verdict resolved as vu_region_rule's
// tail and vu_rule_card do, the position chosen as vu_triangle_out_pos
// does, the chain plane's cull and distance and the later bias as
// vu_plane_out and vu_later_out apply them; the planes where they hold
// from slots A and B (vu_rec_planes: VuVSOut's flat varyings, as the
// program's entry hands them over)
VuVSOut vu_main_vs(uint vid : SV_VertexID)
{
    vid = VU_MERGE_ENTRY(vid); // the merge build: the draw's own value
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    uint kick = vid >> 2;
    uint corner = vid & 3u;
    // the corner, or the tests' probe vertex
    VuVtx me = vu_rec_vertex(probe ? vu_probe_vertex(vid) : kick - 2u + corner);
    if (probe) {
        return vu_probe_out(vid, vu_probe_field(me, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint rule = vu_rec.w;
    uint mode = rule & VU_CLIP_MASK;
    uint4 a0 = vu_rec_buf[vu_tri.x + kick];
    uint f = a0.x;
    VuVSOut o = vu_out_init();
    if ((f & VU_TF_HIDDEN) != 0u) {
        return o;
    }
    o.col = float4(me.rgba);
    o.stq = me.stq;
    uint at = 0u;
    uint exact = 0u;
    if (mode == VU_CLIP_NONE) {
        at = (me.gx & 0xFFFF) != me.gx || (me.gy & 0xFFFF) != me.gy ? 3u : 1u;
        exact = (f & VU_TF_EXACT) != 0u ? 1u : 0u;
    } else if (mode == VU_CLIP_REGION) {
        uint r = f & VU_TF_RULE_MASK;
        if (r == VU_RULE_DROP && (vu_draw.z & VU_F_TICKS) != 0u) {
            // vu_region_rule's tail: a tick that drew it clips it, one
            // that sent it to the card test sends it there
            uint4 s = vu_rec_buf[vu_tri.z + kick];
            r = s.x == 2u || s.y == 2u ? VU_RULE_CLIP
                                       : ((s.x | s.y) != 0u ? VU_RULE_STRIP : VU_RULE_DROP);
        }
        if (r == VU_RULE_STRIP) {
            // vu_rule_card
            r = (f & VU_TF_CARD) != 0u ? VU_RULE_DROP : VU_RULE_CLIP;
        }
        if (r == VU_RULE_EXACT) {
            at = 1u;
            exact = 1u;
        } else if (r == VU_RULE_CLIP) {
            at = 2u;
            exact = (f & VU_TF_GSPOS) != 0u ? 1u : 0u;
        }
    } else {
        uint cut = (f & VU_TF_CUT) != 0u ? 1u : 0u;
        if ((cut != 0u && (vu_draw.z & VU_F_KICK_ONLY) != 0u) ||
            (cut == 0u && (vu_draw.z & VU_F_CUT_ONLY) != 0u)) {
            // drawn by the other pass
        } else {
            at = (f >> VU_TF_AT_SHIFT) & 3u;
            exact = (f & VU_TF_EXACT) != 0u ? 1u : 0u;
        }
    }
    o.pos = vu_corner_position(me, mode, at);
    if ((rule & VU_TRI_CHAIN) != 0u) {
        if (vu_batch.z != 0u) {
            // vu_plane_out: culled value by value, the corner's distance
            bool past = (f & VU_TF_PAST) != 0u;
            o.pos.x = past ? VU_CULLED.x : o.pos.x;
            o.pos.y = past ? VU_CULLED.y : o.pos.y;
            o.pos.z = past ? VU_CULLED.z : o.pos.z;
            o.pos.w = past ? VU_CULLED.w : o.pos.w;
            o.plane = vu_plane_distance(kick - 2u + corner);
        }
        if ((f & VU_TF_LATER) != 0u) {
            // vu_later_out (its condition is the flag's)
            VuVtx a = vu_rec_vertex(kick - 2u);
            VuVtx b = vu_rec_vertex(kick - 1u);
            VuVtx c = vu_rec_vertex(kick);
            precise float z = o.pos.z + vu_later_bias(a, b, c) * g_z.x * o.pos.w;
            o.pos.z = z;
        }
    }
    // the planes hold where vu_attr_planes makes them
    uint planes = exact & (g_vp.x != 0.0 ? 1u : 0u);
    if (planes != 0u) {
        o = vu_rec_planes(o, a0, vu_tri.x + kick, vu_tri.y + kick);
    }
    return vu_fused_out(o, mode); // a fused pair's kick: r.w (the merge build)
}

// The pre-pass's targets: the record's three planes
struct VuPreIn {
    float4 pos : SV_Position;
    VK_LOC(0) nointerpolation uint4 r0 : TEXCOORD0;
    VK_LOC(1) nointerpolation uint4 r1 : TEXCOORD1;
    VK_LOC(2) nointerpolation uint4 r2 : TEXCOORD2;
};

struct VuPreTargets {
    VK_LOC(0) uint4 r0 : SV_Target0;
    VK_LOC(1) uint4 r1 : SV_Target1;
    VK_LOC(2) uint4 r2 : SV_Target2;
};

VuPreTargets vu_pre_ps(VuPreIn i)
{
    VuPreTargets o;
    o.r0 = i.r0;
    o.r1 = i.r1;
    o.r2 = i.r2;
    return o;
}
