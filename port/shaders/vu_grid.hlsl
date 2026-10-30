// vu_grid.hlsl: mesh, the procedural grids of prim_DispMesh3D (cloth,
// flags, the queen's barrier). vu_grid_vs = code 20 (RD_PROG_GRID),
// vu_grid_lit_vs = code 22 and vu_grid_spec_vs = code 24 (RD_PROG_GRID_LIT;
// 24 needs debug_specular_flag). The stream keeps the VU batches as they
// are: per batch (one strip) the GIF tag and the colour (vu_batch.x = 2;
// 3 and vu_batch.y = 1 to bind a Mesh3D packet buffer as it is, with its
// VIF and MSCNT qwords), then per vertex pos, [normal,] ST; vu_draw.w =
// vertices per batch. The
// matrix is vu_mem[16..19] (SET_MESH_MATRIX's vf01..vf04), the lights
// vu_mem[28..35] (SET_MESH_LIGHT). Always the region test.  vu_grid_pre_vs,
// vu_grid_lit_pre_vs and vu_grid_spec_pre_vs: their vertices for the
// vertex pre-pass (vu_common.hlsli vu_pre_out).
#include "vu_common.hlsli"

#define VU_GRID 0u
#define VU_GRID_LIT 1u
#define VU_GRID_SPEC 2u

VuVtx vu_grid_vertex(uint v, uint variant)
{
    VuVtx o = vu_vtx_init();
    uint at = vu_vertex_qw(v);
    bool lit = variant != VU_GRID;
    float4 pos = vu_stream_f(at);
    float4 st = vu_stream_f(at + (lit ? 2u : 1u));
    // mesh.vsm:172-175 M * pos (vf00.w), :179 div, :188 mulq
    o.h = vu_matm(16u, pos, 1.0);
    float q = vu_divide(o);
    o.inside = vu_inside(o.p, o.h.w, VU_LO0, VU_HI4094);
    {
        float4 col = vu_stream_f(vu_batch_qw(v) + vu_batch.x - 1u); // the last header qword
        if (lit) {
            // :168-176 l = max(L1 * n, 0); :184-187 c = L2 * l with the
            // ambient column, not clamped; code 24 squares it (:284)
            float4 n = vu_stream_f(at + 1u);
            float4 l = vu_matm(28u, n, n.w);
            l = max(l, 0.0);
            float4 c = vu_matm(32u, l, l.w);
            if (variant == VU_GRID_SPEC) {
                precise float3 s = c.xyz * c.xyz;
                c.xyz = s;
            }
            precise float3 rgb = col.xyz * c.xyz;
            o.rgba = vu_rgba(min(float4(rgb, col.w), 255.0));
        } else {
            o.rgba = vu_rgba(col); // :368 ftoi0 of the batch colour, no clamp
        }
        // :177 add.xy ST + UV offset; :189 mulq.xyz
        precise float3 s = float3(st.xy + VU_MEM(2).xy, st.z) * q;
        o.stq = s;
    }
    return o;
}

VuVSOut vu_grid_main(uint vid, uint variant)
{
    bool probe = (vu_draw.z & VU_F_PROBE) != 0u;
    uint kick = vid >> 2;
    // the triangle's first corner, or the tests' probe vertex
    VuVtx a = vu_grid_vertex(probe ? vu_probe_vertex(vid) : kick - 2u, variant);
    if (probe) {
        return vu_probe_out(vid, vu_probe_field(a, (vid / 3u) % VU_PROBE_FIELDS));
    }
    VuVtx b = vu_grid_vertex(kick - 1u, variant);
    VuVtx c = vu_grid_vertex(kick, variant);
    // the marks: the draw's VU_MARK_JOINED (rd_replay.c, a grid of two
    // strips or more; no mark bytes)
    return vu_mesh_out(a, b, c, kick, vid & 3u, vu_mark.y, VU_CLIP_REGION, VU_LO0, VU_HI4094,
                       false);
}

VuVSOut vu_grid_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(VU_MERGE_ENTRY(vid), VU_GRID);
}

VuVSOut vu_grid_lit_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(VU_MERGE_ENTRY(vid), VU_GRID_LIT);
}

VuVSOut vu_grid_spec_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(VU_MERGE_ENTRY(vid), VU_GRID_SPEC);
}

// The vertex pre-pass: vertex vu_rec.z + id of each variant
VuPreOut vu_grid_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    return vu_pre_out(id, vu_grid_vertex(vu_rec.z + id, VU_GRID));
}

VuPreOut vu_grid_lit_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    return vu_pre_out(id, vu_grid_vertex(vu_rec.z + id, VU_GRID_LIT));
}

VuPreOut vu_grid_spec_pre_vs(uint vid : SV_VertexID)
{
    uint id = VU_PRE_ENTRY(vid);
    return vu_pre_out(id, vu_grid_vertex(vu_rec.z + id, VU_GRID_SPEC));
}
