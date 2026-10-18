// vu_prelit.hlsl: normal_c (RD_PROG_PRELIT), codes 32 (region test), 34
// (no test) and 36 (scissor), chosen by vu_draw.z's clip mode. Vertex: pos,
// ST, colour (pac_makeNormalStrip). vu_prelit_pre_vs: its vertices for the
// vertex pre-pass (vu_common.hlsli vu_pre_out). Also vu_ps, the pixel
// shader of every VU draw (vu_texa_ps for the 24- and 16-bit textures under
// AEM), through the main vertex shaders (vu_tri.hlsl vu_main_vs,
// vu_particle.hlsl vu_particle_main_vs) and the programs' own entries (the
// fallback, rd_replay_vu.c vuDraw) alike: VuVSOut, the planes as flat
// varyings; and vu_probe_ps, the tests' probe output. vu_np_ps and
// vu_texa_np_ps are vu_ps and vu_texa_ps built with ICO_VU_NO_PLANES=1, the
// pixel side at a fractional scale (port/render/rd_pixel_plan.c
// rd__vu_pixel_fs).
#include "vu_common.hlsli"

// One vertex of START_NORMAL_C / _NOCLIP (normal_c.vsm:135-269) or
// START_SCISSOR_C (:314-399).
VuVtx vu_prelit_vertex(uint v, uint mode)
{
    VuVtx o = vu_vtx_init();
    uint at = vu_vertex_qw(v);
    float4 pos = vu_stream[at];
    float4 st = vu_stream[at + 1u];
    float4 col = vu_stream[at + 2u];
    // :152-155 M * pos with vf00.w; the scissor loop uses pos.w (:337)
    float w = mode == VU_CLIP_SCISSOR ? pos.w : 1.0;
    o.h = vu_matm(16u, pos, w);
    if (mode == VU_CLIP_SCISSOR) {
        o.clipf = vu_clipw(vu_matm(20u, pos, pos.w));
    }
    float q = vu_divide(o);
    if (mode == VU_CLIP_REGION) {
        o.inside = vu_inside(o.p, o.h.w, VU_LO0, VU_HI4094);
    }
    // :157 add.xyz vf21 = ST + (UV offset xy, 0); :172 mulq.xyz
    precise float3 s = (st.xyz + float3(vu_mem[2].xy, 0.0)) * q;
    o.stq = s;
    // :173 ftoi0 of the colour as sent
    o.rgba = vu_rgba(col);
    return o;
}

VuVSOut vu_prelit_vs(uint vid : SV_VertexID)
{
    uint mode = vu_draw.z & VU_CLIP_MASK;
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    uint kick = vid >> 2;
    // the triangle's first corner, or the tests' probe vertex
    VuVtx a = vu_prelit_vertex(probe ? vu_probe_vertex(vid) : kick - 2u, mode);
    if (probe) {
        return vu_probe_out(vid, vu_probe_field(a, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint marks = vu_kick_marks(kick);
    if (vu_content_hidden(marks)) {
        return vu_out_init();
    }
    VuVtx b = vu_prelit_vertex(kick - 1u, mode);
    VuVtx c = vu_prelit_vertex(kick, mode);
    return vu_mesh_out(a, b, c, kick, vid & 3u, marks, mode, VU_LO0, VU_HI4094, true);
}

// The vertex pre-pass: vertex vu_rec.z + id, the clip mode vu_prelit_vs
// takes
VuPreOut vu_prelit_pre_vs(uint id : SV_VertexID)
{
    return vu_pre_out(id, vu_prelit_vertex(vu_rec.z + id, vu_draw.z & VU_CLIP_MASK));
}

GS_PS_ATTR GS_PS_OUT vu_ps(VuVSOut i)
{
    const uint4 col = vu_pixel(i);
    GS_RETURN(col, i.pos);
}

GS_PS_ATTR GS_PS_OUT vu_texa_ps(VuVSOut i)
{
    const uint4 col = vu_pixel_texa(i);
    GS_RETURN(col, i.pos);
}

// The probe's input: the position and the colour (location 0 of VuVSOut)
struct VuProbeIn {
    float4 pos : SV_Position;
    VK_LOC(0) noperspective float4 col : COLOR0;
};

uint4 vu_probe_ps(VuProbeIn i) : SV_Target0
{
    uint v = uint(i.col.x + 0.5) | (uint(i.col.y + 0.5) << 16);
    return uint4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24);
}
