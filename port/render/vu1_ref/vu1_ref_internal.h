/* vu1_ref_internal.h: the VU operations the references are written in.
 * Every helper is one instruction or one accumulator chain, named after it,
 * so a reference reads like the program it follows. */
#ifndef PORT_RENDER_VU1_REF_INTERNAL_H
#define PORT_RENDER_VU1_REF_INTERNAL_H

#include "ps2float.h"
#include "vu1_ref.h"

#include <string.h>

typedef float Vec4[4];

static inline void v4_copy(Vec4 d, const float *s)
{
    memcpy(d, s, sizeof(Vec4));
}

/* mulax/madday/maddaz/maddw: c0 * v.x + c1 * v.y + c2 * v.z + c3 * w, each
 * product and each sum rounded on its own (no fusion), in this order. w is
 * passed separately because the programs use vf00.w (1.0) or a lane of
 * another register there. */
static inline void vu_mat(Vec4 d, const float *c0, const float *c1, const float *c2,
                          const float *c3, const float *v, float w)
{
    Vec4 a;
    for (int i = 0; i < 4; i++) {
        float t = c0[i] * v[0];
        t = t + c1[i] * v[1];
        t = t + c2[i] * v[2];
        a[i] = t + c3[i] * w;
    }
    v4_copy(d, a);
}

/* The matrix held in four consecutive VF registers / memory quadwords. */
static inline void vu_matr(Vec4 d, float (*c)[4], const float *v, float w)
{
    vu_mat(d, c[0], c[1], c[2], c[3], v, w);
}

/* maxx.xyzw d, s, vf00x: every lane at least 0. */
static inline void vu_max0(Vec4 d)
{
    for (int i = 0; i < 4; i++) {
        d[i] = ps2_max(d[i], 0.0f);
    }
}

/* minii.xyzw d, s, I with I = 255.0. */
static inline void vu_min255(Vec4 d)
{
    for (int i = 0; i < 4; i++) {
        d[i] = ps2_min(d[i], 255.0f);
    }
}

/* ftoi0.xyzw into an RGBAQ quadword. */
static inline void vu_ftoi0_4(int32_t *d, const float *s)
{
    for (int i = 0; i < 4; i++) {
        d[i] = ps2_ftoi(s[i]);
    }
}

/* div q, vf00w, x: 1 / x with the VU's divide-by-zero rule. */
static inline float vu_rcp(float x)
{
    return ps2_div(1.0f, x);
}

/* clipw.xyz v, v.w: the six flags of one vertex, bit 0 +x (x > |w|),
 * 1 -x (x < -|w|), 2 +y, 3 -y, 4 +z, 5 -z. */
static inline unsigned vu_clipw(const float *v)
{
    float w = v[3] < 0.0f ? -v[3] : v[3];
    unsigned f = 0;
    for (int i = 0; i < 3; i++) {
        if (v[i] > w) {
            f |= 1u << (2 * i);
        }
        if (v[i] < -w) {
            f |= 2u << (2 * i);
        }
    }
    return f;
}

/* The XYZ2 quadword: ftoi4 of x, y, z and the given w word (0 = kick,
 * 0x8000 = ADC, as mfir.w vf19/vf20 and isw.w put there). */
static inline void vu_xyz(VuGsVertex *o, const float *p, int32_t w)
{
    o->xyz[0] = ps2_ftoi4(p[0]);
    o->xyz[1] = ps2_ftoi4(p[1]);
    o->xyz[2] = ps2_ftoi4(p[2]);
    o->xyz[3] = w;
}

/* The batch count: NLOOP, the low 15 bits of TOP + 0's first word (ilwr.x,
 * iand 0x7FFF), at most VU_BATCH_MAX (VuBatchOut.v; a batch that large
 * would not fit the VU1 input buffer). */
static inline int vu_count(const float (*in)[4])
{
    uint32_t w;
    memcpy(&w, &in[0][0], 4);
    w &= 0x7FFFu;
    return (int)(w > VU_BATCH_MAX ? VU_BATCH_MAX : w);
}

static inline int32_t vu_int(float f)
{
    int32_t i;
    memcpy(&i, &f, 4);
    return i;
}

/* The region test of the triangle loops (sub.xyw vf00, lo, p and
 * sub.xyw vf00, p, hi, then fmand with 0xD0 = the x, y and w sign flags):
 * lo < p and p < hi on x, y and w. */
static inline int vu_inside(const float *p, const float *lo, const float *hi)
{
    return lo[0] < p[0] && lo[1] < p[1] && lo[3] < p[3] && p[0] < hi[0] && p[1] < hi[1] &&
           p[3] < hi[3];
}

/* The bounds normal_c, normal_l and mesh load into vf13/vf14 (loi
 * 0x457FF000 = 4094.0, loi 0x4B7FFFFE = 16777214.0). */
extern const float vu_lo0[4];
extern const float vu_hi4094[4];

/* vu1ref_SetWideX's factor (1 = 4:3) and the centre the wide projection
 * squeezes x about (shader_consts.h ICO_VU_REGION_CX). */
extern float vu1ref_wideX;
#define VU_REGION_CX 2048.0f

/* The x the region test compares on a wide screen (vu_common.hlsli
 * vu_region_pos): x squeezed by the wide factor f about the centre, the
 * subtract, multiply and add each rounded on its own as the shader's
 * precise expression does. Only called for f != 1. */
static inline float vu_region_x(float x, float f)
{
    float d = x - VU_REGION_CX;
    d = d * f;
    return d + VU_REGION_CX;
}

/* The region test of normal_c code 32, normal_l 32, 34 and 38 and the mesh
 * program (vu_inside against vf13/vf14) with x where the wide picture puts
 * it. A factor of exactly 1 tests p as it is: (x - 2048) * 1 + 2048 rounds
 * a tiny positive x to 0, so the remap is skipped there. The cluster
 * program's test (bounds from the common block) is not remapped. */
static inline int vu_inside_region(const float *p, const float *lo, const float *hi)
{
    const float f = vu1ref_wideX;
    if (f == 1.0f) {
        return vu_inside(p, lo, hi);
    }
    Vec4 q;
    v4_copy(q, p);
    q[0] = vu_region_x(p[0], f);
    return vu_inside(q, lo, hi);
}

/* ADC counter of the triangle loops (vi05 in normal_c/normal_l/mesh, vi07 in
 * cluster): a strip-start vertex (ST.w < 1) sets it to 3 before the
 * decrement, a vertex outside the region sets it to 3 after it; the vertex
 * gets ADC when it is outside or the counter is still positive. Returns the
 * ADC bit; *counter is the VI register. */
static inline int vu_adc_counter(int32_t *counter, int restart, int inside)
{
    if (restart) {
        *counter = 3;
    }
    *counter -= 1;
    if (!inside) {
        *counter = 3;
        return 1;
    }
    return *counter > 0;
}

/* scissorcommcut.h: clip one triangle (clip-space positions, colours as
 * floats, STQ as read from the input plus the UV offset) against the six
 * planes and draw it as a fan through the viewport matrix mem[8..11]. */
void vu1ref_ScissorCommon(Vu1Ref *r, const float pos[3][4], const float col[3][4],
                          const float stq[3][4], VuBatchOut *out, int atVertex);

/* The scissor triangle loop shared by normal_c and normal_l code 36: the
 * per-vertex values are computed by the caller; this keeps vi04..vi06 and
 * vi09 and decides kick, ADC and fan. clip is M2 * pos of this vertex. */
typedef struct VuScissorVtx {
    float clip[4]; /* mem[20..23] * pos */
    float stq[4];  /* input ST + UV offset (before the divide) */
    float colf[4]; /* the colour the loop stored, as itof0 reads it back */
    int restart;   /* ST.w < 1 */
} VuScissorVtx;

void vu1ref_ScissorStep(Vu1Ref *r, const VuScissorVtx *win[3], VuBatchOut *out, int k);

#endif
