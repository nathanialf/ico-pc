// vu_tri.hlsl: the vertex pre-pass's two other stages (docs/port/RENDER.md,
// "The vertex pre-pass"; rd_replay_vu.c rd__vu_prepass, doVuDraw).  vu_pre_ps writes
// a pre-pass point's record (vu_common.hlsli vu_pre_out) into the three
// RGBA32_UINT targets.  vu_tri_vs is the triangle stage of every mesh
// program (normal_c, normal_l, cluster, mesh): the triangle kick-2, kick-1,
// kick at index value kick * 4 + corner, as the program's own entry draws
// it, its three corners read from the record (vu_rec_vertex) instead of
// evaluated, and the program's choices from VuCB (vu_rec.w: the clip mode,
// VU_TRI_CHAIN, VU_TRI_HIDE; vu_lo, vu_hi: the region test's bounds) instead
// of written in the entry.  Built twice like every VU vertex shader
// (vu_tri_plain_vs: ICO_VU_PLAIN).
#include "vu_common.hlsli"

VuVSOut vu_tri_vs(uint vid : SV_VertexID)
{
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    uint kick = vid >> 2;
    // the triangle's first corner, or the tests' probe vertex
    VuVtx a = vu_rec_vertex(probe ? vu_probe_vertex(vid) : kick - 2u);
    if (probe) {
        return vu_probe_out(vid, vu_probe_field(a, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint rule = vu_rec.w;
    // a grid's marks are the draw's (vu_mark.z 0: vu_kick_marks gives
    // vu_mark.y, what vu_grid.hlsl passes)
    uint marks = vu_kick_marks(kick);
    if ((rule & VU_TRI_HIDE) != 0u && vu_content_hidden(marks)) {
        return vu_out_init();
    }
    VuVtx b = vu_rec_vertex(kick - 1u);
    VuVtx c = vu_rec_vertex(kick);
    return vu_mesh_out(a, b, c, kick, vid & 3u, marks, rule & VU_CLIP_MASK, vu_lo, vu_hi,
                       (rule & VU_TRI_CHAIN) != 0u);
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
