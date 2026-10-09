/* vu1_cluster.c: the skinned programs of ico2/vusrc/cluster.vsm. Vertex:
 * pos, normal, weights (int VU address of bone 0, w0, int address of bone 1,
 * w1; pac_makeClusterStrip writes bone * 4 + 16), ST, colour. The bones are
 * VU memory 16.. (SET_CLUSTER_MATRIX); vf09..vf12 = mem[4..7], the common
 * block's world to GS screen matrix; vf13..vf16 = L1, vf17..vf20 = L2
 * (SET_CLUSTER_LIGHT). */
#include "vu1_ref_internal.h"

static float (*bone(Vu1Ref *r, float addr))[4]
{
    return &r->mem[vu_int(addr) & (VU1_MEM_QW - 1)];
}

/* The blended position and normal (cluster.vsm:186-223; the same chains at
 * :336-369 and :496-536): P = B0 * (pos, 1) * w0 + B1 * (pos, 1) * w1, w 1;
 * N = B0 rot * n * w0 + B1 rot * n * w1 (the w term vf00 * n.w adds only to
 * the unused w lane), w 1. */
static void skin(Vu1Ref *r, const float *pos, const float *nrm, const float *wt, Vec4 P, Vec4 N)
{
    Vec4 p0, p1, n0, n1;
    float (*b0)[4] = bone(r, wt[0]);
    float (*b1)[4] = bone(r, wt[2]);
    vu_matr(p0, b0, pos, 1.0f);
    vu_matr(p1, b1, pos, 1.0f);
    vu_mat(n0, b0[0], b0[1], b0[2], r->vf[0], nrm, nrm[3]);
    vu_mat(n1, b1[0], b1[1], b1[2], r->vf[0], nrm, nrm[3]);
    for (int i = 0; i < 3; i++) {
        float a = p0[i] * wt[1];
        P[i] = a + p1[i] * wt[3];
        a = n0[i] * wt[1];
        N[i] = a + n1[i] * wt[3];
    }
    P[3] = 1.0f;
    N[3] = 1.0f;
}

void vu1ref_cluster(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    memcpy(out->tag, in[0], 16);
    out->fanCount = 0;
    if (code != 20 && code != 22 && code != 24) {
        out->count = 0;
        return;
    }
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 5 * k], *nrm = in[2 + 5 * k], *wt = in[3 + 5 * k],
                    *st = in[4 + 5 * k], *col = in[5 + 5 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 P, N, S, L, C, K;
        skin(r, pos, nrm, wt, P, N);
        /* :226-229 vf28 = M (vf09..vf12) * P; :232-235 vf30 = L1 * N (w 1) */
        vu_matr(S, &r->vf[9], P, P[3]);
        vu_matr(L, &r->vf[13], N, N[3]);
        /* :235 div q = 1 / S.w; :243 mulq.xyz */
        float q = vu_rcp(S[3]);
        S[0] = S[0] * q;
        S[1] = S[1] * q;
        S[2] = S[2] * q;
        /* :241 max(L, 0); :246-250 C = max(L2 * L, 0) with L.w (code 20);
         * :388-401 code 22: vf00 instead of vf20 (no ambient), then
         * squared twice; :552-568 code 24: with vf20, squared once */
        vu_max0(L);
        if (code == 22) {
            vu_mat(C, r->vf[17], r->vf[18], r->vf[19], r->vf[0], L, L[3]);
        } else {
            vu_mat(C, r->vf[17], r->vf[18], r->vf[19], r->vf[20], L, L[3]);
        }
        vu_max0(C);
        if (code != 20) {
            for (int i = 0; i < 3; i++) {
                C[i] = C[i] * C[i];
            }
        }
        if (code == 22) {
            for (int i = 0; i < 3; i++) {
                C[i] = C[i] * C[i];
            }
        }
        /* :257 mul.xyz vf25 = col * C; :262 minii.xyzw 255; :266 ftoi0;
         * code 24 also scales alpha by mem[2].w (:562 mul.w vf25, vf25, vf23,
         * vf23 = mem[2], the cluster packet's fade alpha) */
        v4_copy(K, col);
        for (int i = 0; i < 3; i++) {
            K[i] = col[i] * C[i];
        }
        if (code == 24) {
            K[3] = col[3] * r->mem[2][3];
        }
        vu_min255(K);
        vu_ftoi0_4(o->rgba, K);
        /* :255 mulq.xyz vf24 = ST * q, vf24.w = 0 (:242). No UV offset in
         * codes 20/22; code 24 adds mem[2].xy first (:505, :596). */
        Vec4 t;
        v4_copy(t, st);
        if (code == 24) {
            t[0] = st[0] + r->mem[2][0];
            t[1] = st[1] + r->mem[2][1];
        }
        for (int i = 0; i < 3; i++) {
            o->stq[i] = t[i] * q;
        }
        o->stq[3] = 0.0f;
        /* :253-265 region test against mem[0] and mem[1] (the common block's
         * (0, 0, 0, 1) and (4095, 4095, 0, 16777215)); :238-242 the strip
         * flag ST.w < 1; :266-282 the ADC counter vi07 */
        int inside = vu_inside(S, r->mem[0], r->mem[1]);
        int adc = vu_adc_counter(&r->vi[7], st[3] - 1.0f < 0.0f, inside);
        o->inside = inside;
        o->clipFlags = 0;
        vu_xyz(o, S, adc ? 0x8000 : 0);
    }
    out->count = n;
    r->vi[6] = 0;
}
