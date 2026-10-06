/* vu1_particle.c: START_PARTICLE (ico2/vusrc/particle.vsm:79-149), the
 * billboard sprites of prim_DispParticle (Primitive.c). vf01..vf04 = the
 * caller's world to GS screen matrix (particleEffect.c passes
 * matrixptr + 0x100), vf05..vf08 = the screen matrix matrixptr + 0xC0. */
#include "vu1_ref_internal.h"

void vu1ref_Particle(Vu1Ref *r, const float (*in)[4], uint32_t inQw, VuParticleOut *out)
{
    if (inQw < 6) {
        memset(out, 0, sizeof(*out));
        return;
    }
    /* :80-91: vi07 = count; vf23, vf24 = the two tags; vf26, vf27 = the clip
     * window corners; vf29 = (size scale, du, dv, -); vi03 = output 544 */
    int32_t n = vu_int(in[0][0]);
    if (n > (int32_t)((inQw - 6) / 2)) {
        n = (int32_t)((inQw - 6) / 2);
    }
    const float *clipMin = in[3], *clipMax = in[4], *k = in[5];
    memcpy(out->tag[0], in[1], 16);
    memcpy(out->tag[1], in[2], 16);
    out->count = 0;
    int last = -1;
    for (int i = 0; i < n && out->count < VU_PARTICLES_MAX; i++) {
        const float *a = in[6 + 2 * i], *b = in[7 + 2 * i];
        /* :100 subx.w vf00, vf31, vf00x; :104 fmand vi11, 1 (the w zero
         * flag); :105 ibeq: a particle whose alpha is 0 is skipped */
        if (ps2_is_zero(b[3] - 0.0f)) {
            continue;
        }
        Vec4 c, s, h, v0, v1;
        /* :97-98 vf10 = (x, y, z, 1); :101-104 vf09 = M * vf10 */
        v4_copy(c, a);
        c[3] = 1.0f;
        vu_matr(h, &r->vf[1], c, 1.0f);
        /* :99-100, :105 vf28 = (size * k.x, size * k.x, 0, 1); :109-112
         * vf12 = S * vf28: the half extent in homogeneous screen units */
        s[0] = (0.0f + a[3]) * k[0];
        s[1] = (0.0f + a[3]) * k[0];
        s[2] = 0.0f;
        s[3] = 1.0f;
        Vec4 e;
        vu_matr(e, &r->vf[5], s, s[3]);
        /* :108 div q = 1 / h.w; :110-117 corners h -/+ e on xy; :120-121
         * mulq.xyz (z divided too, w stays h.w) */
        float q = vu_rcp(h[3]);
        v4_copy(v0, h);
        v4_copy(v1, h);
        v0[0] = h[0] - e[0];
        v0[1] = h[1] - e[1];
        v1[0] = h[0] + e[0];
        v1[1] = h[1] + e[1];
        for (int j = 0; j < 3; j++) {
            v0[j] = v0[j] * q;
            v1[j] = v1[j] * q;
        }
        /* :125-133 both corners strictly inside (clipMin, clipMax) on x, y
         * and w, else the particle is skipped (:133 ibne to CLIP_PARTICLE) */
        if (!vu_inside(v0, clipMin, clipMax) || !vu_inside(v1, clipMin, clipMax)) {
            continue;
        }
        VuGsSprite *o = &out->s[out->count++];
        o->index = i;
        /* :118-119, :124 RGBA = ftoi0(grey, grey, grey, alpha) */
        Vec4 col = {0.0f + b[2], 0.0f + b[2], 0.0f + b[2], b[3] + 0.0f};
        vu_ftoi0_4(o->rgba, col);
        /* :106-108, :113-115, :122-123 ST0 = (u, v, 1, 0), ST1 = (u + du,
         * v + dv, 1, 0): Q = 1, not divided */
        float st0[4] = {0.0f + b[0], 0.0f + b[1], 0.0f + 1.0f, 0.0f - 0.0f};
        float st1[4] = {st0[0] + k[1], st0[1] + k[2], st0[2], st0[3]};
        v4_copy(o->st[0], st0);
        v4_copy(o->st[1], st1);
        /* :125-130 mfir.w vf09/vf12 = 0, ftoi4.xyz: both corners kick */
        for (int j = 0; j < 3; j++) {
            o->xyz[0][j] = ps2_ftoi4(v0[j]);
            o->xyz[1][j] = ps2_ftoi4(v1[j]);
        }
        o->xyz[0][3] = 0;
        o->xyz[1][3] = 0;
        last = 544 + 6 * (out->count - 1);
    }
    /* :144-145 ibeq vi05, BEGIN_PARTICLE with sq vf24, 0(vi04) in the delay
     * slot: the EOP tag is written over the last particle's tag, or, when
     * no particle was stored, to wherever vi04 points from before (the
     * normal and mesh loops leave their counter vi04 at 0: VU memory 0, the
     * common block's (0, 0, 0, 1), which cluster's region test reads) */
    if (last >= 0) {
        r->vi[4] = last;
    }
    memcpy(r->mem[r->vi[4] & (VU1_MEM_QW - 1)], in[2], 16);
}
