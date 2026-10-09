// vu_prelit.hlsl: normal_c (RD_PROG_PRELIT), codes 32 (region test), 34
// (no test) and 36 (scissor), chosen by vu_draw.z's clip mode. Vertex: pos,
// ST, colour (pac_makeNormalStrip). Also vu_ps, the pixel shader of every
// mesh program (vu_texa_ps for the 24- and 16-bit textures under AEM,
// package TEXA), and vu_probe_ps, the tests' probe output.
#include "vu_common.hlsli"

// One vertex of START_NORMAL_C / _NOCLIP (normal_c.vsm:135-269) or
// START_SCISSOR_C (:314-399). full = false: position and tests only.
VuVtx vu_prelit_vertex(uint v, uint mode, bool full)
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
        o.clip = vu_matm(20u, pos, pos.w);
    }
    float q = vu_divide(o);
    if (mode == VU_CLIP_REGION) {
        o.inside = vu_inside(o.p, VU_LO0, VU_HI4094);
    }
    if (full) {
        // :157 add.xyz vf21 = ST + (UV offset xy, 0); :172 mulq.xyz
        precise float3 s = (st.xyz + float3(vu_mem[2].xy, 0.0)) * q;
        o.stq = s;
        // :173 ftoi0 of the colour as sent
        o.rgba = vu_rgba(col);
    }
    return o;
}

VuVSOut vu_prelit_vs(uint vid : SV_VertexID)
{
    uint mode = vu_draw.z & VU_CLIP_MASK;
    if ((vu_draw.z & VU_F_PROBE) != 0u) {
        VuVtx v = vu_prelit_vertex(vu_probe_vertex(vid), mode, true);
        return vu_probe_out(vid, vu_probe_field(v, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint kick = (vid & VU_INDEX_MASK) >> 2;
    uint corner = vid & 3u;
    VuVtx a = vu_prelit_vertex(kick - 2u, mode, false);
    VuVtx b = vu_prelit_vertex(kick - 1u, mode, false);
    VuVtx c = vu_prelit_vertex(kick, mode, false);
    VuVtx me = vu_prelit_vertex(kick - 2u + corner, mode, true);
    VuVSOut o = vu_triangle_out(a, b, c, me, mode);
    if ((vid & VU_INDEX_LATER) != 0u) {
        o = vu_later_out(o, a, b, c);
    }
    return o;
}

DualOut vu_ps(VuVSOut i)
{
    return vu_pixel(i);
}

DualOut vu_texa_ps(VuVSOut i)
{
    return vu_pixel_texa(i);
}

uint4 vu_probe_ps(VuVSOut i) : SV_Target0
{
    uint v = uint(i.col.x + 0.5) | (uint(i.col.y + 0.5) << 16);
    return uint4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24);
}
