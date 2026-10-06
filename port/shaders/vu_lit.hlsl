// vu_lit.hlsl: normal_l. vu_lit_vs = codes 32 (region test) and 36
// (scissor) by vu_draw.z's clip mode (RD_PROG_LIT); vu_lit_spec_vs = code
// 34 (RD_PROG_LIT_SPEC); vu_reflect_vs = code 38 (RD_PROG_REFLECT).
// Vertex: pos, normal, ST, colour.
#include "vu_common.hlsli"

#define VU_LIT 0u
#define VU_LIT_SPEC 1u
#define VU_REFLECT 2u

// The diffuse colour (normal_l.vsm:154-195, :525-562): l = max(L1 * n, 0)
// with n.w; c = max(L2 * l, 0) with l.w (L2's fourth column is ambient);
// rgb = col * c, alpha as sent, every lane min 255, ftoi0. Specular (code
// 34, :259-300): the fourth column is vf00 (no ambient) and c is raised to
// the fourth power after the clamp.
uint4 vu_lit_colour(float4 n, float4 col, bool spec)
{
    float4 l = vu_matm(28u, n, n.w);
    l = max(l, 0.0);
    float4 c4 = spec ? float4(0.0, 0.0, 0.0, 1.0) : vu_mem[35];
    float4 c = vu_mat(vu_mem[32], vu_mem[33], vu_mem[34], c4, l, l.w);
    c = max(c, 0.0);
    if (spec) {
        precise float3 s = c.xyz * c.xyz;
        s = s * s;
        c.xyz = s;
    }
    precise float3 rgb = col.xyz * c.xyz;
    return vu_rgba(min(float4(rgb, col.w), 255.0));
}

// START_NORMAL_REF (:365-471): the view-space reflection mapped to ST
// through the inverse view (mem[12..14]); returns S, T, 1 before the divide.
float3 vu_reflect_st(float4 pos, float4 n)
{
    float4 v = vu_matm(24u, pos, 1.0);
    // :392, :399-401 |v|^2 as (x*x + y*y) + z*z; :405 rsqrt
    precise float d2 = v.x * v.x;
    d2 = d2 + v.y * v.y;
    d2 = d2 + v.z * v.z;
    precise float rq = d2 == 0.0 ? 3.40282347e38 : 1.0 / sqrt(abs(d2));
    // :393-396 the normal in view space: MV rotation * n (+ vf00 * n.w)
    float4 nv = vu_mat(vu_mem[24], vu_mem[25], vu_mem[26], float4(0.0, 0.0, 0.0, 1.0), n, n.w);
    precise float3 d = v.xyz * rq;
    // :416, :420-422 dn = (d.x*n.x + d.y*n.y) + d.z*n.z
    precise float3 t = d * nv.xyz;
    precise float dn = t.x + t.y;
    dn = dn + t.z;
    // :426-428 r = (n * dn + n * dn) + d * dn
    precise float3 r = nv.xyz * dn;
    r = r + nv.xyz * dn;
    r = r + d * dn;
    // :433-436 inverse view rotation * r (+ vf00 * 1)
    float4 w = vu_mat(vu_mem[12], vu_mem[13], vu_mem[14], float4(0.0, 0.0, 0.0, 1.0),
                      float4(r, 1.0), 1.0);
    // :440 z + z; :444 xy + 2z; :450-452 xy * 0.125 + 1.0 * 0.25
    precise float z2 = w.z + w.z;
    precise float2 st = w.xy + z2;
    st = st * 0.125;
    st = st + 0.25;
    return float3(st, 1.0);
}

VuVtx vu_lit_vertex(uint v, uint mode, uint variant, bool full)
{
    VuVtx o = vu_vtx_init();
    uint at = vu_vertex_qw(v);
    float4 pos = vu_stream[at];
    float4 n = vu_stream[at + 1u];
    float4 st = vu_stream[at + 2u];
    float4 col = vu_stream[at + 3u];
    bool scissor = mode == VU_CLIP_SCISSOR;
    o.h = vu_matm(16u, pos, scissor ? pos.w : 1.0);
    if (scissor) {
        o.clip = vu_matm(20u, pos, pos.w);
    }
    float q = vu_divide(o);
    if (!scissor) {
        o.inside = vu_inside(o.p, VU_LO0, VU_HI4094);
    }
    if (full) {
        if (variant == VU_REFLECT) {
            // :456 mulx.xyz vf21 = (S, T, 1) * (0 + q); colour as sent
            precise float3 s = vu_reflect_st(pos, n) * q;
            o.stq = s;
            o.rgba = vu_rgba(col);
        } else {
            // :164 add.xy ST + UV offset; :176 mulq.xyz
            precise float3 s = float3(st.xy + vu_mem[2].xy, st.z) * q;
            o.stq = s;
            o.rgba = vu_lit_colour(n, col, variant == VU_LIT_SPEC);
        }
    }
    return o;
}

VuVSOut vu_lit_main(uint vid, uint variant)
{
    // codes 34 and 38 have no scissor variant: always the region test
    uint mode = variant == VU_LIT && (vu_draw.z & VU_CLIP_MASK) == VU_CLIP_SCISSOR
                    ? VU_CLIP_SCISSOR
                    : VU_CLIP_REGION;
    if ((vu_draw.z & VU_F_PROBE) != 0u) {
        VuVtx v = vu_lit_vertex(vu_probe_vertex(vid), mode, variant, true);
        return vu_probe_out(vid, vu_probe_field(v, (vid / 3u) % VU_PROBE_FIELDS));
    }
    uint kick = vid >> 2;
    uint corner = vid & 3u;
    VuVtx a = vu_lit_vertex(kick - 2u, mode, variant, false);
    VuVtx b = vu_lit_vertex(kick - 1u, mode, variant, false);
    VuVtx c = vu_lit_vertex(kick, mode, variant, false);
    VuVtx me = vu_lit_vertex(kick - 2u + corner, mode, variant, true);
    return vu_triangle_out(a, b, c, me, mode);
}

VuVSOut vu_lit_vs(uint vid : SV_VertexID)
{
    return vu_lit_main(vid, VU_LIT);
}

VuVSOut vu_lit_spec_vs(uint vid : SV_VertexID)
{
    return vu_lit_main(vid, VU_LIT_SPEC);
}

VuVSOut vu_reflect_vs(uint vid : SV_VertexID)
{
    return vu_lit_main(vid, VU_REFLECT);
}
