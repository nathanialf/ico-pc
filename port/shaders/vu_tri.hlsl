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

// ICO_VU_FLAT0 (vu_main_vs; docs/port/PIPELINES.md, "Flat outputs at the
// provoking corner"): 1, the shipped build, computes the flat outputs (the
// planes, VuVSOut a0 .. r) at corner 0 alone, the triangle's provoking
// vertex on every backend, and reads the later bias's own corner from the
// corner's record already read; 0 (the twins vu_all_main_vs and
// vu_merge_all_main_vs, made only under the tests' ICO_RD_VU_FLAT0=0,
// port/render/rd_vu_flat0.c) is the shader before, every corner computing
// them.  The picture is the same byte for byte (rd_vu_flat0_ab).
#ifndef ICO_VU_FLAT0
#define ICO_VU_FLAT0 1
#endif

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
        // vu_rule_beside's conditions where the camera's own verdict is a
        // drop (vu_main_vs applies them to the final one): the camera rule's
        // (VU_F_CAMERA: no card test there, so every final drop is one
        // here), else the side bands' (a card keeps the drop): vu_drop_fate
        if (r == VU_RULE_DROP) {
            const uint fate = vu_drop_fate(vu_front_facing(a, b, c, marks));
            f |= fate == 2u ? VU_TF_CAMERA : (fate == 1u ? VU_TF_BESIDE : 0u);
        }
    } else if (mode == VU_CLIP_NONE) {
        // vu_triangle_out_pos's exact for clip mode none
        uint wrap = (a.gx & 65535) == a.gx && (a.gy & 65535) == a.gy && (b.gx & 65535) == b.gx &&
                            (b.gy & 65535) == b.gy && (c.gx & 65535) == c.gx &&
                            (c.gy & 65535) == c.gy
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
        } else if (gsPos == 0u) {
            at = 2u; // only x/y flags, a corner without its GS position: the GPU clips
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

// its compute build (ICO_VU_REC_COMPUTE, vu_common.hlsli VU_PRE_CS)
#ifdef ICO_VU_REC_COMPUTE
VU_PRE_CS(vu_tri_rule_pre_cs, vu_tri_rule_pre_vs)
#endif

// slot C, the component vu_tri.w (the draw's colour mask writes it alone;
// the store builds store that lane alone, vu_rec_lane_store):
// whether tick vu_tri.w showed the triangle (vu_tick_shown), for a
// triangle of a VU_F_TICKS draw in the region mode the blended camera's
// verdict can drop, else 0; vu_main_vs reads it only where that verdict
// (slot A) is VU_RULE_DROP.  Can drop: not wholly inside, not
// VU_F_BEHIND_EYE, and not under the card test with every corner in front
// of the eye (VU_F_EXTENDED without VU_F_CAMERA, vu_strip.y, every h.w > 0: vu_region_rule_cut
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
    bool strip = (vu_draw.z & (VU_F_EXTENDED | VU_F_CAMERA)) == VU_F_EXTENDED &&
                 vu_strip.y != 0u && a.h.w > 0.0 && b.h.w > 0.0 && c.h.w > 0.0;
    if ((vu_rec.w & VU_CLIP_MASK) == VU_CLIP_REGION && (a.inside & b.inside & c.inside) == 0u &&
        (vu_draw.z & VU_F_BEHIND_EYE) == 0u && (vu_draw.z & VU_F_TICKS) != 0u && !strip) {
        float4 pic = vu_picture_ndc();
        float4 frame = vu_frame_ndc();
        s = vu_tick_shown(a, b, c, vu_tri.w, vu_lo, vu_hi, pic, frame);
    }
#ifdef ICO_VU_REC_STORES
    return vu_rec_lane_store(vu_tri.z + kick, vu_tri.w, s);
#else
    uint4 z4 = uint4(0u, 0u, 0u, 0u);
    return vu_rec_point_out(vu_tri.z + kick, uint4(s, s, 0u, 0u), z4, z4);
#endif
}

// its compute build (ICO_VU_REC_COMPUTE, vu_common.hlsli VU_PRE_CS)
#ifdef ICO_VU_REC_COMPUTE
VU_PRE_CS(vu_tri_tick_pre_cs, vu_tri_tick_pre_vs)
#endif

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

// its compute build (ICO_VU_REC_COMPUTE, vu_common.hlsli VU_PRE_CS)
#ifdef ICO_VU_REC_COMPUTE
VU_PRE_CS(vu_tri_planes_pre_cs, vu_tri_planes_pre_vs)
#endif

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
    uint beside = 0u;
    if (mode == VU_CLIP_NONE) {
        at = (me.gx & 65535) != me.gx || (me.gy & 65535) != me.gy ? 3u : 1u;
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
        if (r == VU_RULE_DROP && (f & VU_TF_CAMERA) != 0u) {
            // vu_rule_beside under the camera rule: drawn clipped over the
            // whole frame (the flag is set only under VU_F_CAMERA)
            r = VU_RULE_CLIP;
        }
        if (r == VU_RULE_DROP && (f & VU_TF_BESIDE) != 0u && (vu_draw.z & VU_F_NO_BESIDE) == 0u) {
            // vu_rule_beside (the draw's flag again: a _dstrov pass sets it
            // after the pre-pass)
            r = VU_RULE_CLIP_BESIDE;
            beside = 1u;
        }
        if (r == VU_RULE_EXACT) {
            at = 1u;
            exact = 1u;
        } else if (r == VU_RULE_CLIP || r == VU_RULE_CLIP_BESIDE) {
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
    o.plane.y = beside != 0u ? 1.0 : 0.0; // the side bands' pixels (vu_pixel_tests)
    if ((rule & VU_TRI_CHAIN) != 0u) {
        if (vu_batch.z != 0u) {
            // vu_plane_out: culled value by value, the corner's distance
            bool past = (f & VU_TF_PAST) != 0u;
            o.pos.x = past ? VU_CULLED.x : o.pos.x;
            o.pos.y = past ? VU_CULLED.y : o.pos.y;
            o.pos.z = past ? VU_CULLED.z : o.pos.z;
            o.pos.w = past ? VU_CULLED.w : o.pos.w;
            o.plane.x = vu_plane_distance(kick - 2u + corner);
        }
        if ((f & VU_TF_LATER) != 0u) {
            // vu_later_out (its condition is the flag's)
#if ICO_VU_FLAT0
            // the corner's own vertex is me, read from the same record
            // texels (vu_rec_vertex of kick - 2 + corner): the same words,
            // so the same VuVtx; the other two read here, each in its branch
            VuVtx a = me;
            VuVtx b = me;
            VuVtx c = me;
            if (corner != 0u) {
                a = vu_rec_vertex(kick - 2u);
            }
            if (corner != 1u) {
                b = vu_rec_vertex(kick - 1u);
            }
            if (corner != 2u) {
                c = vu_rec_vertex(kick);
            }
#else
            VuVtx a = vu_rec_vertex(kick - 2u);
            VuVtx b = vu_rec_vertex(kick - 1u);
            VuVtx c = vu_rec_vertex(kick);
#endif
            precise float z = o.pos.z + vu_later_bias(a, b, c) * g_z.x * o.pos.w;
            o.pos.z = z;
        }
    }
    // the planes hold where vu_attr_planes makes them
    uint planes = exact & (g_vp.x != 0.0 ? 1u : 0u);
#if ICO_VU_FLAT0
    // a0 .. r are flat (nointerpolation): the rasteriser takes them from the
    // triangle's provoking vertex, its first index value, corner 0 in every
    // list the replay draws (rd_mesh.c batchIndices, rd_replay_vu.c
    // vuRunWrite and the grids' lists: kick * 4 + 0, + 1, + 2 in that
    // order), on every backend (Vulkan's first-vertex mode, the default;
    // Direct3D's first vertex; OpenGL's glProvokingVertex(
    // GL_FIRST_VERTEX_CONVENTION), rhi/gl/gl_device.c gl_defaults).  Corners
    // 1 and 2 leave them as vu_out_init made them, nothing reads them: their
    // five loads are skipped.  Every interpolated output and the position
    // are computed as before at every corner
    planes &= corner == 0u ? 1u : 0u;
#endif
    if (planes != 0u) {
        o = vu_rec_planes(o, a0, vu_tri.x + kick, vu_tri.y + kick);
    }
    return vu_fused_out(o, mode); // a fused pair's kick: r.w, plane (the merge build)
}

#ifdef ICO_VU_REC_COMPUTE
// ------------------------------------------------ the index compaction
// vu_tri_keep_cs (built as vu_tri_keep_compute_cs; port/render/
// rd_vu_compact.c, docs/port/PIPELINES.md "The index compaction"): after
// pass T, one invocation a triangle of one draw's index list (a mesh
// command's, or one of a code 36 command's cut and kick draws), the list
// written again into the record buffer's fourth plane in its own order,
// each value raised by the draw's raise (the draw merge's, fixed when the
// pre-pass plans the draw), a triangle that provably draws nothing written
// as its corner-1 value three times.  The draw then reads that list.
//   g_aux0  x the list's first value in t0 (uint units from the bound
//           buffer's start: the mesh arena or the ring holds stream and
//           list), y its first value out in u6 (uint units from the record's
//           start), z the triangles, w the raise
//   g_aux1  x the draw's pass flags (VU_F_CUT_ONLY, VU_F_KICK_ONLY or 0),
//           y the texel of its first workgroup's counts (after the plane's
//           lists, a texel a workgroup), z 1 for a list 0 command's draw,
//           w 1 under the tests' sabotage (ICO_RD_VU_COMPACT_SABOTAGE: every
//           triangle written as a filler, so the self-test must refuse it)
// Drawing nothing, decided from the record words vu_main_vs reads with
// vu_main_vs's statements (vu_keep_position), never from the rasteriser's
// fill rule:
//   drop     every corner's verdict leaves it at VU_CULLED (hidden, the
//            region rule's drop the camera, card and beside rules keep, the
//            other scissor pass's triangle, every corner past the chain
//            plane): VU_CULLED is outside x <= w
//   outside  every corner past one plane of the bound target's clip-space
//            box (the target's share of the exact viewport, two texels
//            out, or the clip volume) or of 0 <= z <= w, by more than
//            2^-10 of the corner's magnitude: no point of the triangle is
//            inside, so the GPU's clipping leaves no fragment
// The filler (corner 1, three times) is that corner's position again, past
// the same plane: clipped, no fragment, no sample, no depth, no stencil.
// One vertex invocation where the GPU reuses an index within a triangle
// (the triangle's three before).  Counts: each workgroup stores its own
// texel, x drop, y outside, z kept, w g_aux1.z (stores, no atomics on the
// buffer, nothing to clear: rd_vu_compact.c reads them back and sums).

// a value of t0's list (uint u of the bound range)
uint vu_keep_index(uint u)
{
    uint4 q = vu_stream[u >> 2];
    uint l = u & 3u;
    return l == 0u ? q.x : (l == 1u ? q.y : (l == 2u ? q.z : q.w));
}

// a value into u6 (uint u from the record's start), one lane: the
// neighbouring lanes are other invocations' values; held to the record's
// four planes
void vu_keep_store(uint u, uint v)
{
    uint q = u >> 2;
    uint l = u & 3u;
    if (q >= vu_rec.y * 4u) {
        return;
    }
    if (l == 0u) {
        vu_rec_out[q].x = v;
    } else if (l == 1u) {
        vu_rec_out[q].y = v;
    } else if (l == 2u) {
        vu_rec_out[q].z = v;
    } else {
        vu_rec_out[q].w = v;
    }
}

// vu_main_vs's position of the draw's own index value vid under the pass
// flags passFlags, its statements in their order (the same record words, the
// same verdict, vu_corner_position, the chain plane's cull and the later
// bias); culled 1 when the verdict left the corner at VU_CULLED.  The side
// bands: as vu_main_vs with the command's VU_F_NO_BESIDE (a _dstrov pass
// adds the flag at its draw, vuDraw: a drop there, drawn here: kept)
float4 vu_keep_position(uint vid, uint passFlags, out uint culled)
{
    uint kick = vid >> 2;
    uint corner = vid & 3u;
    VuVtx me = vu_rec_vertex(kick - 2u + corner);
    uint rule = vu_rec.w;
    uint mode = rule & VU_CLIP_MASK;
    uint drawZ = vu_draw.z | passFlags;
    uint f = vu_rec_buf[vu_tri.x + kick].x;
    culled = 1u;
    if ((f & VU_TF_HIDDEN) != 0u) {
        return VU_CULLED;
    }
    uint at = 0u;
    if (mode == VU_CLIP_NONE) {
        at = (me.gx & 65535) != me.gx || (me.gy & 65535) != me.gy ? 3u : 1u;
    } else if (mode == VU_CLIP_REGION) {
        uint r = f & VU_TF_RULE_MASK;
        if (r == VU_RULE_DROP && (drawZ & VU_F_TICKS) != 0u) {
            uint4 s = vu_rec_buf[vu_tri.z + kick];
            r = s.x == 2u || s.y == 2u ? VU_RULE_CLIP
                                       : ((s.x | s.y) != 0u ? VU_RULE_STRIP : VU_RULE_DROP);
        }
        if (r == VU_RULE_STRIP) {
            r = (f & VU_TF_CARD) != 0u ? VU_RULE_DROP : VU_RULE_CLIP;
        }
        if (r == VU_RULE_DROP && (f & VU_TF_CAMERA) != 0u) {
            r = VU_RULE_CLIP;
        }
        if (r == VU_RULE_DROP && (f & VU_TF_BESIDE) != 0u && (drawZ & VU_F_NO_BESIDE) == 0u) {
            r = VU_RULE_CLIP_BESIDE;
        }
        if (r == VU_RULE_EXACT) {
            at = 1u;
        } else if (r == VU_RULE_CLIP || r == VU_RULE_CLIP_BESIDE) {
            at = 2u;
        }
    } else {
        uint cut = (f & VU_TF_CUT) != 0u ? 1u : 0u;
        if ((cut != 0u && (drawZ & VU_F_KICK_ONLY) != 0u) ||
            (cut == 0u && (drawZ & VU_F_CUT_ONLY) != 0u)) {
            // drawn by the other pass
        } else {
            at = (f >> VU_TF_AT_SHIFT) & 3u;
        }
    }
    float4 pos = vu_corner_position(me, mode, at);
    culled = at == 0u ? 1u : 0u;
    if ((rule & VU_TRI_CHAIN) != 0u) {
        // value by value, as vu_main_vs (no vector of booleans)
        bool past = vu_batch.z != 0u && (f & VU_TF_PAST) != 0u;
        pos.x = past ? VU_CULLED.x : pos.x;
        pos.y = past ? VU_CULLED.y : pos.y;
        pos.z = past ? VU_CULLED.z : pos.z;
        pos.w = past ? VU_CULLED.w : pos.w;
        culled = past ? 1u : culled;
        if ((f & VU_TF_LATER) != 0u) {
            VuVtx a = vu_rec_vertex(kick - 2u);
            VuVtx b = vu_rec_vertex(kick - 1u);
            VuVtx c = vu_rec_vertex(kick);
            precise float z = pos.z + vu_later_bias(a, b, c) * g_z.x * pos.w;
            pos.z = z;
        }
    }
    return pos;
}

// the bound target's half extents in clip space (x, y over w), two texels
// out: its share of the exact viewport about the viewport's centre moved
// by g_vpo less the jitter (common.hlsli fullscreen_triangle), else the
// viewport itself and two GS pixels
float2 vu_keep_extent()
{
    if (g_vp.x != 0.0 && g_misc.z != 0.0) {
        return g_misc.zw + abs((g_vpo.xy - g_jitter.xy) * g_vp.xy) + 2.0 * g_vp.xy;
    }
    return float2(1.0, 1.0) + 4.0 * g_target.zw;
}

// 1 when a, b and c are all on the outer side of the plane d(p) >= 0 by
// more than 2^-10 of their magnitude (da = d(a), ma = the magnitude)
uint vu_keep_past(float da, float ma, float db, float mb, float dc, float mc)
{
    const float k = 1.0 / 1024.0;
    return da < -k * ma && db < -k * mb && dc < -k * mc ? 1u : 0u;
}

// 1 when the triangle a, b, c lies wholly past one plane of the target's
// box or of 0 <= z <= w (NaN fails every comparison: kept)
uint vu_keep_outside(float4 a, float4 b, float4 c)
{
    float2 e = vu_keep_extent();
    uint o = 0u;
    o |= vu_keep_past(e.x * a.w - a.x, abs(a.x) + abs(e.x * a.w), e.x * b.w - b.x,
                      abs(b.x) + abs(e.x * b.w), e.x * c.w - c.x, abs(c.x) + abs(e.x * c.w));
    o |= vu_keep_past(e.x * a.w + a.x, abs(a.x) + abs(e.x * a.w), e.x * b.w + b.x,
                      abs(b.x) + abs(e.x * b.w), e.x * c.w + c.x, abs(c.x) + abs(e.x * c.w));
    o |= vu_keep_past(e.y * a.w - a.y, abs(a.y) + abs(e.y * a.w), e.y * b.w - b.y,
                      abs(b.y) + abs(e.y * b.w), e.y * c.w - c.y, abs(c.y) + abs(e.y * c.w));
    o |= vu_keep_past(e.y * a.w + a.y, abs(a.y) + abs(e.y * a.w), e.y * b.w + b.y,
                      abs(b.y) + abs(e.y * b.w), e.y * c.w + c.y, abs(c.y) + abs(e.y * c.w));
    o |= vu_keep_past(a.z, abs(a.z) + abs(a.w), b.z, abs(b.z) + abs(b.w), c.z,
                      abs(c.z) + abs(c.w));
    o |= vu_keep_past(a.w - a.z, abs(a.z) + abs(a.w), b.w - b.z, abs(b.z) + abs(b.w),
                      c.w - c.z, abs(c.z) + abs(c.w));
    return o;
}

groupshared uint vu_keep_n[3];

[numthreads(64, 1, 1)] void vu_tri_keep_cs(uint3 t : SV_DispatchThreadID,
                                           uint3 g : SV_GroupThreadID, uint3 w : SV_GroupID)
{
    if (g.x < 3u) {
        vu_keep_n[g.x] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (t.x < g_aux0.z) {
        uint s = g_aux0.x + 3u * t.x;
        uint i0 = vu_keep_index(s);
        uint i1 = vu_keep_index(s + 1u);
        uint i2 = vu_keep_index(s + 2u);
        uint c0, c1, c2;
        float4 p0 = vu_keep_position(i0, g_aux1.x, c0);
        float4 p1 = vu_keep_position(i1, g_aux1.x, c1);
        float4 p2 = vu_keep_position(i2, g_aux1.x, c2);
        uint drop = c0 & c1 & c2;
        uint past = drop != 0u ? 0u : vu_keep_outside(p0, p1, p2);
        uint cls = drop != 0u || g_aux1.w != 0u ? 0u : (past != 0u ? 1u : 2u);
        if (cls != 2u) {
            i0 = i1; // the filler: corner 1, past the plane its triangle is past
            i2 = i1;
        }
        uint o = g_aux0.y + 3u * t.x;
        vu_keep_store(o, i0 + g_aux0.w);
        vu_keep_store(o + 1u, i1 + g_aux0.w);
        vu_keep_store(o + 2u, i2 + g_aux0.w);
        uint ignored;
        InterlockedAdd(vu_keep_n[cls], 1u, ignored);
    }
    GroupMemoryBarrierWithGroupSync();
    uint q = g_aux1.y + w.x;
    if (g.x == 0u && q < vu_rec.y * 4u) {
        vu_rec_out[q] = uint4(vu_keep_n[0], vu_keep_n[1], vu_keep_n[2], g_aux1.z);
    }
}
#endif

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
