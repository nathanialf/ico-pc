/* vu1_mesh.c: the procedural grid programs of ico2/vusrc/mesh.vsm
 * (prim_DispMesh3D, Primitive.c). One batch is one strip: in[1] is the
 * batch colour (prim_makePacketMesh3D: Mesh3D.col with alpha 128 sent as
 * 127), then per vertex pos, [normal,] ST. No strip flag is read; the GIF
 * tag's PRE starts each batch afresh. */
#include "vu1_ref_internal.h"

void vu1ref_mesh(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    int lit = code == 22 || code == 24;
    int vq = lit ? 3 : 2;
    const float *col = in[1];
    Vec4 uv;
    memcpy(out->tag, in[0], 16);
    out->fanCount = 0;
    if (code != 20 && code != 22 && code != 24) {
        out->count = 0;
        return;
    }
    /* :162 / :255 / :352 vf23 = mem[2] (the UV offset) */
    v4_copy(uv, r->mem[2]);
    for (int k = 0; k < n; k++) {
        const float *pos = in[2 + vq * k];
        const float *st = in[2 + vq * k + vq - 1];
        VuGsVertex *o = &out->v[k];
        Vec4 p, K;
        /* :172-175 vf28 = M * pos (vf04 * vf00w); :179 div; :188 mulq.xyz */
        vu_matr(p, &r->vf[1], pos, 1.0f);
        float q = vu_rcp(p[3]);
        p[0] = p[0] * q;
        p[1] = p[1] * q;
        p[2] = p[2] * q;
        if (lit) {
            /* :168-171 L = L1 (vf05..vf08) * n with n.w; :176 max(L, 0);
             * :184-187 C = L2 (vf09..vf12) * L with L.w, not clamped;
             * code 24 squares it (:284). :190-196 alpha moved, rgb =
             * col * C, every lane min 255; :200 ftoi0 */
            const float *nrm = in[2 + vq * k + 1];
            Vec4 L, C;
            vu_mat(L, r->vf[5], r->vf[6], r->vf[7], r->vf[8], nrm, nrm[3]);
            vu_max0(L);
            vu_mat(C, r->vf[9], r->vf[10], r->vf[11], r->vf[12], L, L[3]);
            if (code == 24) {
                for (int i = 0; i < 3; i++) {
                    C[i] = C[i] * C[i];
                }
            }
            v4_copy(K, col);
            for (int i = 0; i < 3; i++) {
                K[i] = col[i] * C[i];
            }
            vu_min255(K);
            vu_ftoi0_4(o->rgba, K);
        } else {
            /* :368 ftoi0 of the batch colour, no clamp */
            vu_ftoi0_4(o->rgba, col);
        }
        /* :177 add.xy ST + vf23; :189 mulq.xyz vf21; vf21.w = 0 (:163) */
        o->stq[0] = (st[0] + uv[0]) * q;
        o->stq[1] = (st[1] + uv[1]) * q;
        o->stq[2] = st[2] * q;
        o->stq[3] = 0.0f;
        /* :197-204 region test against vf13/vf14 from SET_MESH_MATRIX;
         * :200-213 the ADC counter vi05, no strip flag */
        int inside = vu_inside_region(p, r->vf[13], r->vf[14]);
        int adc = vu_adc_counter(&r->vi[5], 0, inside);
        o->inside = inside;
        o->clipFlags = 0;
        vu_xyz(o, p, adc ? 0x8000 : 0);
    }
    out->count = n;
    r->vi[4] = 0;
}
