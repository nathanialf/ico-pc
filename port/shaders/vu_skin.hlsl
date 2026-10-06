// vu_skin.hlsl: cluster (skinned). vu_skin_vs = code 20 (RD_PROG_SKIN),
// vu_skin_spec_vs = code 22 (RD_PROG_SKIN_SPEC), vu_skin_debug_vs = code 24
// (CLUSTER_1_SPEC, reached only with debug_specular_flag == 1). Vertex:
// pos, normal, weights (raw int VU address bone * 4 + 16, w0, address, w1),
// ST, colour (pac_makeClusterStrip). Bones: VuBoneCB = VU memory 16..255
// (SET_CLUSTER_MATRIX); world to GS screen = vu_mem[4..7]; L1, L2 in
// vu_mem[28..35] (SET_CLUSTER_LIGHT's vf13..vf20). Always the region test
// against vu_mem[0] and vu_mem[1].
#include "vu_common.hlsli"

cbuffer VuBoneCB : register(b3, space1)
{
    float4 vu_bone[240]; // VU memory 16..255
};

#define VU_SKIN 0u
#define VU_SKIN_SPEC 1u
#define VU_SKIN_DEBUG 2u

float4 vu_bone_mat(uint addr, float4 v, float w)
{
    uint i = min(addr - 16u, 236u); // 60 bones: addresses 16..252
    return vu_mat(vu_bone[i], vu_bone[i + 1u], vu_bone[i + 2u], vu_bone[i + 3u], v, w);
}

float4 vu_bone_rot(uint addr, float4 v)
{
    uint i = min(addr - 16u, 236u); // 60 bones: addresses 16..252
    return vu_mat(vu_bone[i], vu_bone[i + 1u], vu_bone[i + 2u], float4(0.0, 0.0, 0.0, 1.0), v,
                  v.w);
}

VuVtx vu_skin_vertex(uint v, uint variant, bool full)
{
    VuVtx o = vu_vtx_init();
    uint at = vu_vertex_qw(v);
    float4 pos = vu_stream[at];
    float4 n = vu_stream[at + 1u];
    float4 wt = vu_stream[at + 2u];
    float4 st = vu_stream[at + 3u];
    float4 col = vu_stream[at + 4u];
    uint b0 = asuint(wt.x), b1 = asuint(wt.z);
    // cluster.vsm:196-223 P = B0 (pos, 1) w0 + B1 (pos, 1) w1, w = 1
    float4 p0 = vu_bone_mat(b0, pos, 1.0);
    float4 p1 = vu_bone_mat(b1, pos, 1.0);
    precise float3 pb = p0.xyz * wt.y;
    pb = pb + p1.xyz * wt.w;
    // :226-229 world to GS screen, :235 div, :243 mulq
    o.h = vu_matm(4u, float4(pb, 1.0), 1.0);
    float q = vu_divide(o);
    // :253-265 lo = mem[0] < p < mem[1] = hi on x, y, w
    o.inside = vu_inside(o.p, vu_mem[0], vu_mem[1]);
    if (full) {
        // :208-223 N = B0 rot n w0 + B1 rot n w1, w = 1; :232-235 L1 * N
        float4 n0 = vu_bone_rot(b0, n);
        float4 n1 = vu_bone_rot(b1, n);
        precise float3 nb = n0.xyz * wt.y;
        nb = nb + n1.xyz * wt.w;
        float4 l = vu_matm(28u, float4(nb, 1.0), 1.0);
        l = max(l, 0.0);
        // :246-250 L2 with the ambient column; code 22 (:391) vf00 instead
        float4 c4 = variant == VU_SKIN_SPEC ? float4(0.0, 0.0, 0.0, 1.0) : vu_mem[35];
        float4 c = vu_mat(vu_mem[32], vu_mem[33], vu_mem[34], c4, l, l.w);
        c = max(c, 0.0);
        if (variant != VU_SKIN) {
            precise float3 s = c.xyz * c.xyz; // :397 / :568
            if (variant == VU_SKIN_SPEC) {
                s = s * s; // :401
            }
            c.xyz = s;
        }
        precise float3 rgb = col.xyz * c.xyz;
        // code 24 scales alpha by mem[2].w, the cluster fade (:562)
        precise float a = variant == VU_SKIN_DEBUG ? col.w * vu_mem[2].w : col.w;
        o.rgba = vu_rgba(min(float4(rgb, a), 255.0));
        // :255 STQ = ST * q, no UV offset; code 24 adds mem[2].xy (:505, :596)
        float2 uv = variant == VU_SKIN_DEBUG ? vu_mem[2].xy : float2(0.0, 0.0);
        precise float2 s2 = variant == VU_SKIN_DEBUG ? st.xy + uv : st.xy;
        precise float3 s = float3(s2, st.z) * q;
        o.stq = s;
    }
    return o;
}

VuVSOut vu_skin_main(uint vid, uint variant)
{
    if ((vu_draw.z & VU_F_PROBE) != 0u) {
        VuVtx v = vu_skin_vertex(vu_probe_vertex(vid), variant, true);
        return vu_probe_out(vid, vu_probe_field(v, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint kick = vid >> 2;
    uint corner = vid & 3u;
    VuVtx a = vu_skin_vertex(kick - 2u, variant, false);
    VuVtx b = vu_skin_vertex(kick - 1u, variant, false);
    VuVtx c = vu_skin_vertex(kick, variant, false);
    VuVtx me = vu_skin_vertex(kick - 2u + corner, variant, true);
    return vu_triangle_out(a, b, c, me, VU_CLIP_REGION);
}

VuVSOut vu_skin_vs(uint vid : SV_VertexID)
{
    return vu_skin_main(vid, VU_SKIN);
}

VuVSOut vu_skin_spec_vs(uint vid : SV_VertexID)
{
    return vu_skin_main(vid, VU_SKIN_SPEC);
}

VuVSOut vu_skin_debug_vs(uint vid : SV_VertexID)
{
    return vu_skin_main(vid, VU_SKIN_DEBUG);
}
