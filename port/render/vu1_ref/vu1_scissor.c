/* vu1_scissor.c: the code-36 triangle decision of normal_c/normal_l
 * (START_SCISSOR_C, normal_c.vsm:333-399; START_SCISSOR_L, normal_l.vsm:524-612)
 * and SCISSOR_COMMON (ico2/vusrc/scissorcommcut.h), which clips one
 * triangle in the clip space of mem[20..23] and draws it as a fan. */
#include "vu1_ref_internal.h"

/* The clip passes in program order (scissorcommcut.h ZMINUS..YPLUS):
 * the lane (vi07 rotations) and the sign in vf30.x. */
static const struct {
    int lane;
    float sign;
} kPlanes[6] = {{2, -1.0f}, {2, 1.0f}, {0, -1.0f}, {0, 1.0f}, {1, -1.0f}, {1, 1.0f}};

typedef struct {
    Vec4 pos, col, stq;
} ClipVtx;

/* The out flag of one plane: clipw sets bit 2*lane for x > |w| (plus) and
 * 2*lane+1 for x < -|w| (minus); CLIP_INTER masks one of them. */
static int planeOut(const ClipVtx *v, int plane)
{
    unsigned f = vu_clipw(v->pos);
    unsigned bit = kPlanes[plane].sign > 0.0f ? 1u << (2 * kPlanes[plane].lane)
                                              : 2u << (2 * kPlanes[plane].lane);
    return (f & bit) != 0;
}

/* INTERPOLATE / LOOP_ROT_END: dc = cur.lane - s * cur.w, dn likewise,
 * q = dc / (dn - dc), t = |0 + q|; every lane of the three quadwords is
 * (next - cur) * t + cur. */
static void interpolate(ClipVtx *o, const ClipVtx *cur, const ClipVtx *nxt, int plane)
{
    int l = kPlanes[plane].lane;
    float s = kPlanes[plane].sign;
    float dc = cur->pos[l] - cur->pos[3] * s;
    float dn = nxt->pos[l] - nxt->pos[3] * s;
    float q = ps2_div(dc, dn - dc);
    float t = 0.0f + q;
    t = t < 0.0f ? -t : t;
    for (int i = 0; i < 4; i++) {
        o->pos[i] = (nxt->pos[i] - cur->pos[i]) * t + cur->pos[i];
        o->col[i] = (nxt->col[i] - cur->col[i]) * t + cur->col[i];
        o->stq[i] = (nxt->stq[i] - cur->stq[i]) * t + cur->stq[i];
    }
}

void vu1ref_ScissorCommon(Vu1Ref *r, const float pos[3][4], const float col[3][4],
                          const float stq[3][4], VuBatchOut *out, int atVertex)
{
    ClipVtx a[VU_FAN_MAX + 1], b[VU_FAN_MAX + 1];
    ClipVtx *src = a, *dst = b;
    int n = 3;
    for (int i = 0; i < 3; i++) {
        v4_copy(a[i].pos, pos[i]);
        v4_copy(a[i].col, col[i]);
        v4_copy(a[i].stq, stq[i]);
    }
    for (int p = 0; p < 6; p++) {
        int m = 0;
        src[n] = src[0]; /* the polygon is closed by a copy of its first vertex */
        for (int i = 0; i < n; i++) {
            const ClipVtx *cur = &src[i], *nxt = &src[i + 1];
            int co = planeOut(cur, p), no = planeOut(nxt, p);
            if (m + 2 > VU_FAN_MAX) {
                break;
            }
            if (!co) {
                dst[m++] = *cur; /* CI_NEXT_IN, CI_NEXT_OUT */
                if (no) {
                    interpolate(&dst[m++], cur, nxt, p);
                }
            } else if (!no) {
                interpolate(&dst[m++], cur, nxt, p); /* CO_NEXT_IN */
            }
        }
        if (m == 0) {
            return; /* END_ALL */
        }
        ClipVtx *t = src;
        src = dst;
        dst = t;
        n = m;
    }
    if (out->fanCount >= VU_FANS_MAX) {
        return;
    }
    /* DrawScissorPolygon: mem[37] (the common GIF tag) with x = n | 0x8000;
     * each vertex through the viewport matrix mem[8..11], divided by w on
     * all four lanes, ftoi4.xyzw; STQ = stq * q (xyz); colour ftoi0. */
    VuGsFan *f = &out->fans[out->fanCount++];
    f->n = n;
    f->beforeVertex = atVertex;
    memcpy(f->tag, r->mem[37], 16);
    f->tag[0] = (uint32_t)n | 0x8000u;
    for (int i = 0; i < n; i++) {
        Vec4 v;
        vu_matr(v, &r->mem[8], src[i].pos, src[i].pos[3]);
        float q = vu_rcp(v[3]);
        for (int j = 0; j < 4; j++) {
            v[j] = v[j] * q;
        }
        VuGsVertex *o = &f->v[i];
        for (int j = 0; j < 3; j++) {
            o->stq[j] = src[i].stq[j] * q;
        }
        o->stq[3] = 0.0f;
        vu_ftoi0_4(o->rgba, src[i].col);
        vu_xyz(o, v, ps2_ftoi4(v[3]));
        o->inside = 1;
        o->clipFlags = 0;
    }
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], r->mem[16 + i]);
    }
}

/* The planes the trivial reject tests (fcor with 0xFDF7DF ... 0xFFEFBE):
 * one flag bit set for all three vertices of the triangle. */
static int triviallyOut(unsigned f0, unsigned f1, unsigned f2)
{
    return (f0 & f1 & f2) != 0;
}

void vu1ref_ScissorStep(Vu1Ref *r, const VuScissorVtx *win[3], VuBatchOut *out, int k)
{
    int32_t *vi = r->vi;
    /* normal_c.vsm:338-339: vi04 = vi05, vi05 = vi06 */
    vi[4] = vi[5];
    vi[5] = vi[6];
    /* :343-350 clipw of vf22..vf24 (k-2, k-1, k); fcand vi01, 0x3F is 1
     * when vertex k has any flag; :353 vi06 = vi01 */
    unsigned f2 = vu_clipw(win[2]->clip);
    vi[6] = f2 != 0;
    /* :352-356 the strip flag: vi04 = vi05 = 1, vi09 = 2 */
    if (win[2]->restart) {
        vi[4] = 1;
        vi[5] = 1;
        vi[9] = 2;
    }
    int adc = 0;
    if (vi[4] + vi[5] + vi[6] > 0) {
        /* :371-372 iaddi vi09, vi09, -1 then ibgtz vi09: a conditional
         * branch reading a VI register the instruction just before it wrote
         * sees the value from before that write (the VU's branch VI delay;
         * PCSX2 microVU analyzeBranchVI). The skip therefore covers the two
         * vertices after a strip start, not one. */
        int32_t before = vi[9];
        vi[9] = vi[9] - 1;
        if (before <= 0) {
            unsigned f0 = vu_clipw(win[0]->clip), f1 = vu_clipw(win[1]->clip);
            if (!triviallyOut(f0, f1, f2)) {
                float pos[3][4], col[3][4], stq[3][4];
                for (int i = 0; i < 3; i++) {
                    v4_copy(pos[i], win[i]->clip);
                    v4_copy(col[i], win[i]->colf);
                    v4_copy(stq[i], win[i]->stq);
                }
                vu1ref_ScissorCommon(r, pos, col, stq, out, k);
            }
        }
        adc = 1; /* :399 isw.w vi11 (0x8000), 2(vi07) */
    }
    if (adc) {
        out->v[k].xyz[3] = 0x8000;
    }
}
