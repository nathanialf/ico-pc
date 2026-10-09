/* vu1_normal.c: normal_c (prelit) and normal_l (lit, specular, reflection)
 * triangle loops. The VU code is software pipelined (vertex k+1's transform
 * runs while vertex k is stored); the references process one vertex at a
 * time in the order of the dependent values, which gives the same results
 * because every pipelined value is read after the instruction that writes it
 * for the vertex it belongs to.
 * Line numbers are ico2/vusrc/normal_c.vsm and normal_l.vsm. */
#include "vu1_ref_internal.h"

/* BEGIN_NORMAL_C, BEGIN_NORMAL_C_NOCLIP, BEGIN_NORMAL_L*, BEGIN_NORMAL_REF:
 * vf14.w = 16777214, vf13 = 0, vf14.xy = 4094, vf23 = mem[2] (normal_c.vsm:111-116). */
static void beginBounds(Vu1Ref *r)
{
    r->vf[14][3] = 16777214.0f;
    memset(r->vf[13], 0, sizeof(Vec4));
    r->vf[14][0] = 4094.0f;
    r->vf[14][1] = 4094.0f;
    v4_copy(r->vf[23], r->mem[2]);
}

/* BEGIN_SCISSOR_C / BEGIN_SCISSOR_L (normal_c.vsm:278-285): vf31 = 0,
 * vf17 = mem[2] with w = 1 and z = 0, vf13..vf16 = mem[20..23], vi14 = 256. */
static void beginScissor(Vu1Ref *r)
{
    memset(r->vf[31], 0, sizeof(Vec4));
    v4_copy(r->vf[17], r->mem[2]);
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[13 + i], r->mem[20 + i]);
    }
    r->vf[17][3] = 1.0f;
    r->vf[17][2] = 0.0f;
    r->vi[14] = 256;
}

/* BEGIN_NORMAL_L, BEGIN_NORMAL_L_SPEC: vf05..vf12 = mem[28..35]
 * (normal_l.vsm:120-127); BEGIN_SCISSOR_L the same (:487-494).
 * BEGIN_NORMAL_REF: vf05..vf08 = mem[24..27], vf09..vf12 = mem[12..15]
 * (:330-337). */
static void loadLights(Vu1Ref *r, int a, int b)
{
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[5 + i], r->mem[a + i]);
        v4_copy(r->vf[9 + i], r->mem[b + i]);
    }
}

static void copyTag(VuBatchOut *out, const float (*in)[4])
{
    memcpy(out->tag, in[0], 16);
    out->count = 0;
    out->fanCount = 0;
}

/* The ST.w strip flag: sub.w vf00, st, vf00 and fsand 0x2 (the sign flag):
 * ST.w - 1 < 0. Packet.c writes 0 on the first vertex of each strip, 1 on
 * the others (pac_makeNormalStrip). */
static int restartFlag(const float *st)
{
    return st[3] - 1.0f < 0.0f;
}

/* -------------------------------------------------------------- normal_c */

/* START_NORMAL_C (code 32, normal_c.vsm:135-199) and START_NORMAL_C_NOCLIP
 * (code 34, :212-269): pos, ST, colour per vertex. */
static void normalCLoop(Vu1Ref *r, int clip, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    Vec4 uv;
    v4_copy(uv, r->vf[23]);
    uv[2] = 0.0f; /* move.z vf23, vf00 (:151) */
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 3 * k], *st = in[2 + 3 * k], *col = in[3 + 3 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 p, s;
        /* :152-155 mulax..maddw vf28 = M * pos, w term vf04 * vf00w (1.0) */
        vu_matr(p, &r->vf[1], pos, 1.0f);
        /* :159 div q, vf00w, vf28w; :171 mulq.xyz vf28 */
        float q = vu_rcp(p[3]);
        p[0] = p[0] * q;
        p[1] = p[1] * q;
        p[2] = p[2] * q;
        /* :157 add.xyz vf21, vf17, vf23; :172 mulq.xyz vf21; vf21.w = 0 (:149) */
        for (int i = 0; i < 3; i++) {
            s[i] = (st[i] + uv[i]) * q;
        }
        s[3] = 0.0f;
        v4_copy(o->stq, s);
        /* :173 ftoi0.xyzw vf22, vf18: the colour as sent, no clamp */
        vu_ftoi0_4(o->rgba, col);
        /* :175-180 region test (code 32 only); :166-198 the ADC counter vi05 */
        int inside = clip ? vu_inside_region(p, r->vf[13], r->vf[14]) : 1;
        int adc = vu_adc_counter(&r->vi[5], restartFlag(st), inside);
        o->inside = inside;
        o->clipFlags = 0;
        vu_xyz(o, p, adc ? 0x8000 : 0);
    }
    out->count = n;
    r->vi[4] = 0; /* the loop counter, left at 0 */
}

/* START_SCISSOR_C (code 36, normal_c.vsm:314-399). */
static void normalCScissor(Vu1Ref *r, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    VuScissorVtx win[3];
    const VuScissorVtx *w3[3];
    /* :325-327 vf22..vf24 = vf00 (0, 0, 0, 1), vi04..vi06 = -2 */
    memset(win, 0, sizeof(win));
    for (int i = 0; i < 3; i++) {
        win[i].clip[3] = 1.0f;
    }
    r->vi[4] = r->vi[5] = r->vi[6] = -2;
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 3 * k], *st = in[2 + 3 * k], *col = in[3 + 3 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 p, c;
        win[0] = win[1];
        win[1] = win[2];
        /* :334-337 vf25 = M1 * pos (w = pos.w); :339-342 vf24 = M2 * pos */
        vu_matr(p, &r->vf[1], pos, pos[3]);
        vu_matr(c, &r->vf[13], pos, pos[3]);
        /* :341 div; :348 mulq.xyzw vf27 = vf25 * q; :352 ftoi4.xyzw */
        float q = vu_rcp(p[3]);
        for (int i = 0; i < 4; i++) {
            p[i] = p[i] * q;
        }
        /* :345 add.xyz vf28 = st + vf17 (z 0); :349 mulq.xyz */
        for (int i = 0; i < 3; i++) {
            o->stq[i] = (st[i] + r->vf[17][i]) * q;
        }
        o->stq[3] = 0.0f;
        /* :347 ftoi0.xyzw vf29, vf20 */
        vu_ftoi0_4(o->rgba, col);
        vu_xyz(o, p, ps2_ftoi4(p[3]));
        v4_copy(win[2].clip, c);
        o->inside = 1;
        o->clipFlags = vu_clipw(c);
        /* SCISSOR_COMMON reads the ST back from the input with vf17.xy added
         * (scissorcommcut.h:50-52) and the colour back from the output */
        v4_copy(win[2].stq, st);
        win[2].stq[0] = st[0] + r->vf[17][0];
        win[2].stq[1] = st[1] + r->vf[17][1];
        for (int i = 0; i < 4; i++) {
            win[2].colf[i] = (float)o->rgba[i];
        }
        win[2].restart = restartFlag(st);
        w3[0] = &win[0];
        w3[1] = &win[1];
        w3[2] = &win[2];
        out->count = k + 1;
        vu1ref_scissor_step(r, w3, out, k);
    }
    out->count = n;
}

void vu1ref_normal_c(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out)
{
    copyTag(out, in);
    switch (code) {
    case 32:
        beginBounds(r);
        normalCLoop(r, 1, in, out);
        break;
    case 34:
        beginBounds(r);
        normalCLoop(r, 0, in, out);
        break;
    case 36:
        beginScissor(r);
        normalCScissor(r, in, out);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- normal_l */

/* The diffuse colour of START_NORMAL_L (:154-195) and START_SCISSOR_L
 * (:525-562): l = max(L1 * n, 0) with n.w; c = max(L2 * l, 0) with l.w
 * (the fourth column, vf12, is the ambient term); rgb = col.rgb * c,
 * alpha untouched; every lane min 255; ftoi0. */
static void litColour(Vu1Ref *r, const float *nrm, const float *col, int32_t *rgba)
{
    Vec4 l, c, k;
    vu_mat(l, r->vf[5], r->vf[6], r->vf[7], r->vf[8], nrm, nrm[3]);
    vu_max0(l);
    vu_mat(c, r->vf[9], r->vf[10], r->vf[11], r->vf[12], l, l[3]);
    vu_max0(c);
    v4_copy(k, col);
    for (int i = 0; i < 3; i++) {
        k[i] = col[i] * c[i];
    }
    vu_min255(k);
    vu_ftoi0_4(rgba, k);
}

/* START_NORMAL_L_SPEC (:241-317): l = max(L1 * n, 0); c = vf09 * l.x +
 * vf10 * l.y + vf11 * l.z + vf00 * l.w (no ambient column, :276);
 * c = max(c, 0); c = (c * c) * (c * c) per lane (:283, :287);
 * rgb = col.rgb * c, every lane min 255; ftoi0. */
static void specColour(Vu1Ref *r, const float *nrm, const float *col, int32_t *rgba)
{
    Vec4 l, c, k;
    vu_mat(l, r->vf[5], r->vf[6], r->vf[7], r->vf[8], nrm, nrm[3]);
    vu_max0(l);
    vu_mat(c, r->vf[9], r->vf[10], r->vf[11], r->vf[0], l, l[3]);
    vu_max0(c);
    for (int i = 0; i < 3; i++) {
        c[i] = c[i] * c[i];
    }
    for (int i = 0; i < 3; i++) {
        c[i] = c[i] * c[i];
    }
    v4_copy(k, col);
    for (int i = 0; i < 3; i++) {
        k[i] = col[i] * c[i];
    }
    vu_min255(k);
    vu_ftoi0_4(rgba, k);
}

/* START_NORMAL_L (code 32) and START_NORMAL_L_SPEC (code 34): pos, normal,
 * ST, colour per vertex. */
static void normalLLoop(Vu1Ref *r, int spec, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 4 * k], *nrm = in[2 + 4 * k], *st = in[3 + 4 * k],
                    *col = in[4 + 4 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 p;
        /* :159-163 vf28 = M * pos (vf04 * vf00w) */
        vu_matr(p, &r->vf[1], pos, 1.0f);
        float q = vu_rcp(p[3]);
        p[0] = p[0] * q;
        p[1] = p[1] * q;
        p[2] = p[2] * q;
        /* :164 add.xy vf17, vf17, vf23 (the UV offset); :176 mulq.xyz vf21;
         * vf21.w = 0 (:150) */
        o->stq[0] = (st[0] + r->vf[23][0]) * q;
        o->stq[1] = (st[1] + r->vf[23][1]) * q;
        o->stq[2] = st[2] * q;
        o->stq[3] = 0.0f;
        if (spec) {
            specColour(r, nrm, col, o->rgba);
        } else {
            litColour(r, nrm, col, o->rgba);
        }
        int inside = vu_inside_region(p, r->vf[13], r->vf[14]);
        int adc = vu_adc_counter(&r->vi[5], restartFlag(st), inside);
        o->inside = inside;
        o->clipFlags = 0;
        vu_xyz(o, p, adc ? 0x8000 : 0);
    }
    out->count = n;
    r->vi[4] = 0;
}

/* START_NORMAL_REF (code 38, :365-471): view-space reflection mapped to ST,
 * the colour as sent. vf05..vf08 = mem[24..27] (view * model), vf09..vf11 =
 * mem[12..14] (the inverse view's rotation columns). */
static void normalRef(Vu1Ref *r, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 4 * k], *nrm = in[2 + 4 * k], *st = in[3 + 4 * k],
                    *col = in[4 + 4 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 v, p, nv, d, t, rv, w;
        /* :384-387 vf29 = MV * pos; :388-391 vf28 = M * pos */
        vu_matr(v, &r->vf[5], pos, 1.0f);
        vu_matr(p, &r->vf[1], pos, 1.0f);
        /* :392, :399-401 |v|^2 as (x*x + y*y) + z*z through acc.w */
        float d2 = v[0] * v[0];
        d2 = d2 + v[1] * v[1];
        d2 = d2 + v[2] * v[2];
        /* :393-396 vf30 = MV rotation * n, w term vf00 * n.w */
        vu_mat(nv, r->vf[5], r->vf[6], r->vf[7], r->vf[0], nrm, nrm[3]);
        /* :399 div q = 1 / p.w; :404 addq.x vf31 = 0 + q; :405 rsqrt q */
        float qw = 0.0f + vu_rcp(p[3]);
        float rq = ps2_rsqrt(1.0f, d2);
        /* :412 mulq.xyz vf29: d = v / |v|; :413 mulx.xyz vf28 *= vf31.x */
        for (int i = 0; i < 3; i++) {
            d[i] = v[i] * rq;
            p[i] = p[i] * qw;
        }
        /* :416, :420-422 dn = (d.x*n.x + d.y*n.y) + d.z*n.z */
        for (int i = 0; i < 3; i++) {
            t[i] = d[i] * nv[i];
        }
        float dn = t[0];
        dn = dn + t[1];
        dn = dn + t[2];
        /* :426-428 r = (n * dn + n * dn) + d * dn */
        for (int i = 0; i < 3; i++) {
            float a = nv[i] * dn;
            a = a + nv[i] * dn;
            rv[i] = a + d[i] * dn;
        }
        rv[3] = 1.0f;
        /* :433-436 vf26 = inverse view rotation * r, w term vf00 * 1 */
        vu_mat(w, r->vf[9], r->vf[10], r->vf[11], r->vf[0], rv, 1.0f);
        /* :440 add.z (z + z); :444 addz.xy (xy + 2z); :445 mr32.z vf26 = 1 */
        w[2] = w[2] + w[2];
        w[0] = w[0] + w[2];
        w[1] = w[1] + w[2];
        w[2] = 1.0f;
        /* :450-452 acc.xy = vf26 * 0.125, vf26.xy = acc + vf25 (1.0, from
         * rinit/rget :440-441) * 0.25. The upper instruction of an I-bit
         * bundle reads the I loaded by the previous bundle. */
        for (int i = 0; i < 2; i++) {
            float a = w[i] * 0.125f;
            w[i] = a + 1.0f * 0.25f;
        }
        /* :456 mulx.xyz vf21 = vf26 * vf31.x; vf21.w = 0 (:378) */
        for (int i = 0; i < 3; i++) {
            o->stq[i] = w[i] * qw;
        }
        o->stq[3] = 0.0f;
        /* :402 ftoi0 of the colour as sent */
        vu_ftoi0_4(o->rgba, col);
        int inside = vu_inside_region(p, r->vf[13], r->vf[14]);
        int adc = vu_adc_counter(&r->vi[5], restartFlag(st), inside);
        o->inside = inside;
        o->clipFlags = 0;
        vu_xyz(o, p, adc ? 0x8000 : 0);
    }
    out->count = n;
    r->vi[4] = 0;
}

/* START_SCISSOR_L (code 36, :505-612): as START_SCISSOR_C with the lit
 * colour of START_NORMAL_L. */
static void normalLScissor(Vu1Ref *r, const float (*in)[4], VuBatchOut *out)
{
    int n = vu_count(in);
    VuScissorVtx win[3];
    const VuScissorVtx *w3[3];
    memset(win, 0, sizeof(win));
    for (int i = 0; i < 3; i++) {
        win[i].clip[3] = 1.0f;
    }
    r->vi[4] = r->vi[5] = r->vi[6] = -2;
    for (int k = 0; k < n; k++) {
        const float *pos = in[1 + 4 * k], *nrm = in[2 + 4 * k], *st = in[3 + 4 * k],
                    *col = in[4 + 4 * k];
        VuGsVertex *o = &out->v[k];
        Vec4 p, c;
        win[0] = win[1];
        win[1] = win[2];
        /* :529-535 vf25 = M1 * pos (w = pos.w); :540-543 vf24 = M2 * pos */
        vu_matr(p, &r->vf[1], pos, pos[3]);
        vu_matr(c, &r->vf[13], pos, pos[3]);
        float q = vu_rcp(p[3]);
        for (int i = 0; i < 4; i++) {
            p[i] = p[i] * q;
        }
        /* :545 add.xy vf21 = st + vf17; :547 mulq.xyz vf28 */
        o->stq[0] = (st[0] + r->vf[17][0]) * q;
        o->stq[1] = (st[1] + r->vf[17][1]) * q;
        o->stq[2] = st[2] * q;
        o->stq[3] = 0.0f;
        litColour(r, nrm, col, o->rgba);
        vu_xyz(o, p, ps2_ftoi4(p[3]));
        v4_copy(win[2].clip, c);
        o->inside = 1;
        o->clipFlags = vu_clipw(c);
        v4_copy(win[2].stq, st);
        win[2].stq[0] = st[0] + r->vf[17][0];
        win[2].stq[1] = st[1] + r->vf[17][1];
        for (int i = 0; i < 4; i++) {
            win[2].colf[i] = (float)o->rgba[i];
        }
        win[2].restart = restartFlag(st);
        w3[0] = &win[0];
        w3[1] = &win[1];
        w3[2] = &win[2];
        out->count = k + 1;
        vu1ref_scissor_step(r, w3, out, k);
    }
    out->count = n;
}

void vu1ref_normal_l(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out)
{
    copyTag(out, in);
    switch (code) {
    case 32:
        beginBounds(r);
        loadLights(r, 28, 32);
        normalLLoop(r, 0, in, out);
        break;
    case 34:
        beginBounds(r);
        loadLights(r, 28, 32);
        normalLLoop(r, 1, in, out);
        break;
    case 36:
        beginScissor(r);
        loadLights(r, 28, 32);
        normalLScissor(r, in, out);
        break;
    case 38:
        beginBounds(r);
        loadLights(r, 24, 12);
        normalRef(r, in, out);
        break;
    default:
        break;
    }
}
