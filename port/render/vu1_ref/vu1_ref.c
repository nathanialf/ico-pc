/* vu1_ref.c: VU1 state and the upload routines (the SET_* entries), shared
 * by the program references. Line numbers are ico2/vusrc files. */
#include "vu1_ref_internal.h"

float vu1ref_wideX = 1.0f;

void vu1ref_SetWideX(float f)
{
    vu1ref_wideX = f;
}

void vu1ref_Init(Vu1Ref *r)
{
    memset(r, 0, sizeof(*r));
    r->vf[0][3] = 1.0f;
}

void vu1ref_LoadCommon(Vu1Ref *r, const float qw[16][4])
{
    memcpy(r->mem[0], qw, 16 * sizeof(Vec4));
}

void vu1ref_SetUVOffset(Vu1Ref *r, const float qw[4])
{
    /* vu1_common.h:57-62: lq vf30, 2; move.xy vf30, vf31; sq vf30, 2 */
    r->mem[2][0] = qw[0];
    r->mem[2][1] = qw[1];
}

void vu1ref_NormalSetMatrix(Vu1Ref *r, const float qw[12][4])
{
    /* normal_c.vsm:54-81 (normal_l.vsm:59-86 is the same) */
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
    memcpy(r->mem[16], qw, 12 * sizeof(Vec4));
    v4_copy(r->mem[37], r->mem[3]);
    v4_copy(r->vf[31], r->mem[3]);
}

void vu1ref_NormalSetLight(Vu1Ref *r, const float qw[8][4])
{
    /* normal_c.vsm:88-105 */
    memcpy(r->mem[28], qw, 8 * sizeof(Vec4));
    v4_copy(r->vf[31], qw[7]);
}

void vu1ref_ClusterSetMatrix(Vu1Ref *r, const float (*qw)[4])
{
    /* cluster.vsm:69-87: vi06 = qw[0].x; mem[2].w = qw[0].w; then a
     * do-while copies vi06 quadwords from qw[1] to mem[16..] (decrement,
     * load, branch while non-zero, store in the delay slot) */
    int32_t n = vu_int(qw[0][0]);
    if (n <= 0) {
        n = 1; /* the VU would wrap a 16-bit count of 0; the game never sends one */
    }
    r->vf[30][0] = r->mem[2][0];
    r->vf[30][1] = r->mem[2][1];
    r->vf[30][2] = r->mem[2][2];
    r->vf[30][3] = qw[0][3];
    r->mem[2][3] = qw[0][3];
    int dst = 16;
    int src = 1;
    do {
        n -= 1;
        v4_copy(r->vf[28], qw[src++]);
        v4_copy(r->mem[dst++ & (VU1_MEM_QW - 1)], r->vf[28]);
    } while (n != 0);
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[9 + i], r->mem[4 + i]);
    }
    r->vi[6] = 0;
}

void vu1ref_ClusterSetLight(Vu1Ref *r, const float qw[8][4])
{
    /* cluster.vsm:98-107 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[13 + i], qw[i]);
    }
}

void vu1ref_MeshSetMatrix(Vu1Ref *r, const float qw[4][4])
{
    /* mesh.vsm:85-94: vf14.w = 16777214, vf13 = 0, vf14.xy = 4094 (z kept) */
    r->vf[14][3] = 16777214.0f;
    memset(r->vf[13], 0, sizeof(Vec4));
    r->vf[14][0] = 4094.0f;
    r->vf[14][1] = 4094.0f;
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

void vu1ref_MeshSetLight(Vu1Ref *r, const float qw[8][4])
{
    /* mesh.vsm:102-111 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[5 + i], qw[i]);
    }
}

void vu1ref_ParticleSetMatrix(Vu1Ref *r, const float qw[8][4])
{
    /* particle.vsm:43-52, then falls into BEGIN_PARTICLE (E bit) */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

int vu1ref_Kicks(const VuBatchOut *out, int *kicks)
{
    int n = 0;
    for (int k = 2; k < out->count; k++) {
        if (!vu_gs_adc(&out->v[k])) {
            kicks[n++] = k;
        }
    }
    return n;
}

int vu1ref_StaticKicks(const float *stw, const int *batchFirst, int count, int *kicks)
{
    int n = 0;
    for (int k = 0; k < count; k++) {
        if (k - batchFirst[k] < 2) {
            continue;
        }
        if (stw && (stw[k] < 1.0f || stw[k - 1] < 1.0f)) {
            continue;
        }
        kicks[n++] = k;
    }
    return n;
}
