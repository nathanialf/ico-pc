// vu_grid.hlsl: mesh, the procedural grids of prim_DispMesh3D (cloth,
// flags, the queen's barrier). vu_grid_vs = code 20 (RD_PROG_GRID),
// vu_grid_lit_vs = code 22 and vu_grid_spec_vs = code 24 (RD_PROG_GRID_LIT;
// 24 needs debug_specular_flag). The stream keeps the VU batches as they
// are: per batch (one strip) the GIF tag and the colour (vu_batch.x = 2;
// 3 and vu_batch.y = 1 to bind a Mesh3D packet buffer as it is, with its
// VIF and MSCNT qwords), then per vertex pos, [normal,] ST; vu_draw.w =
// vertices per batch. The
// matrix is vu_mem[16..19] (SET_MESH_MATRIX's vf01..vf04), the lights
// vu_mem[28..35] (SET_MESH_LIGHT). Always the region test.
#include "vu_common.hlsli"

#define VU_GRID 0u
#define VU_GRID_LIT 1u
#define VU_GRID_SPEC 2u

VuVtx vu_grid_vertex(uint v, uint variant, bool full)
{
    VuVtx o = vu_vtx_init();
    uint at = vu_vertex_qw(v);
    bool lit = variant != VU_GRID;
    float4 pos = vu_stream[at];
    float4 st = vu_stream[at + (lit ? 2u : 1u)];
    // mesh.vsm:172-175 M * pos (vf00.w), :179 div, :188 mulq
    o.h = vu_matm(16u, pos, 1.0);
    float q = vu_divide(o);
    o.inside = vu_inside(o.p, VU_LO0, VU_HI4094);
    if (full) {
        float4 col = vu_stream[vu_batch_qw(v) + vu_batch.x - 1u]; // the last header qword
        if (lit) {
            // :168-176 l = max(L1 * n, 0); :184-187 c = L2 * l with the
            // ambient column, not clamped; code 24 squares it (:284)
            float4 n = vu_stream[at + 1u];
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
        precise float3 s = float3(st.xy + vu_mem[2].xy, st.z) * q;
        o.stq = s;
    }
    return o;
}

VuVSOut vu_grid_main(uint vid, uint variant)
{
    if ((vu_draw.z & VU_F_PROBE) != 0u) {
        VuVtx v = vu_grid_vertex(vu_probe_vertex(vid), variant, true);
        return vu_probe_out(vid, vu_probe_field(v, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint kick = vid >> 2;
    uint corner = vid & 3u;
    VuVtx a = vu_grid_vertex(kick - 2u, variant, false);
    VuVtx b = vu_grid_vertex(kick - 1u, variant, false);
    VuVtx c = vu_grid_vertex(kick, variant, false);
    VuVtx me = vu_grid_vertex(kick - 2u + corner, variant, true);
    return vu_triangle_out(a, b, c, me, VU_CLIP_REGION);
}

VuVSOut vu_grid_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(vid, VU_GRID);
}

VuVSOut vu_grid_lit_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(vid, VU_GRID_LIT);
}

VuVSOut vu_grid_spec_vs(uint vid : SV_VertexID)
{
    return vu_grid_main(vid, VU_GRID_SPEC);
}
