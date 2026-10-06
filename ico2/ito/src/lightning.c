#include "typedef.h"
#include <stdlib.h>
#include <libvu0.h>
#include "sugiCommon.h"
#include "itou_common.h"
#include "GifPacket.h"
#include "Texture.h"
#include "Matrix.h"
#include "tableSin.h"
#include "main.h"
#include "itou_sub.h"
#include "lightning.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include <stdio.h>
#include "MicroCode.h"

/* PC port (wave 5, R5c).  The
   strip packet below is a VIF DIRECT block (path 2) of GIF REGLIST packets:
   mc_HostDma reads it as the GIF would and the GS register decoder draws it,
   in order after the gif_* state packet.  The blend mode c comes from the
   stage's BGA lightning record (BgAnimation.c's BgaLightningDef, the short at +0x2E):
   gif_SetAlpha indexes its twelve-entry table with it, which the PS2 reads
   past the end for c outside 0..11; the host uses mode 0 there (as the
   decoder's gif_SetAlpha does) and reports the value once. */
static int lightningHostMode(int c) /* derived name */
{
    static int reported;

    if ((unsigned int)c < 12) {
        return c;
    }
    if (!reported) {
        reported = 1;
        fprintf(stderr,
                "lightning: blend mode %d outside the twelve ALPHA modes: mode 0 used "
                "(reported once)\n",
                c);
    }
    return 0;
}

/* the four control points lightning_test draws through */
typedef struct { /* field names derived */
    LightningVtx v[4];
} LightningPath; /* derived name */

typedef float LightningMtx[4][4] __attribute__((aligned(16)));

/* one strip vertex as the three GS register payloads it is sent as */
typedef struct { /* field names derived */
    unsigned long long rgbaq;
    unsigned long long uv;
    unsigned long long xyz;
} LightningGsVtx; /* derived name */

/* the strip's last two vertices, resent when a clipped strip reopens */
static LightningGsVtx lastVtx[2]; /* derived name */

/* nonzero while the open strip is being written */
static int stripOn; /* derived name */

typedef struct {
    unsigned long long NLOOP : 15;
    unsigned long long EOP : 1;
    unsigned long long pad16 : 16;
    unsigned long long id : 14;
    unsigned long long PRE : 1;
    unsigned long long PRIM : 11;
    unsigned long long FLG : 2;
    unsigned long long NREG : 4;
    unsigned long long REGS0 : 4;
    unsigned long long REGS1 : 60;
} sceGifTag;

/* the GIFtag that opened the current strip, whose NLOOP close_strip fills */
static char *stripTag; /* derived name */

/* vertices set since the draw began */
static int vtxCount; /* derived name */

/* the GS RGBAQ register carries Q as the raw float word in bits 63..32 */
static __inline__ int fbits(float f) /* derived name */
{
    return *(int *)&f;
}

/* VU0's clip flag register on the host: each judgment shifts the older ones
   up by 6 bits and the register keeps the last four (24 bits).  On the PS2
   GsBase.c's vclipw shares it; here lightning keeps its own history, which
   only changes the packets it builds. */
static unsigned int clipFlagReg; /* derived name */

/* VU0's clipping flags for one w-homogeneous point */
static __inline__ int clip_flags(LightningVtx *p) /* derived name */
{
    int flags;

    {
        /* vclipw.xyzw: +x, -x, +y, -y, +z, -z against |w|, bits 0-5 */
        float w = __builtin_fabsf(p->f[3]);
        unsigned int j = 0;

        j |= (p->f[0] > w) << 0;
        j |= (p->f[0] < -w) << 1;
        j |= (p->f[1] > w) << 2;
        j |= (p->f[1] < -w) << 3;
        j |= (p->f[2] > w) << 4;
        j |= (p->f[2] < -w) << 5;
        clipFlagReg = ((clipFlagReg << 6) | j) & 0xFFFFFFu;
        flags = (int)clipFlagReg;
    }
    return flags;
}

/* pad the open strip to a whole triangle count and write its vertex count
 * back into the GIFtag that opened it */
static __inline__ void close_strip(void) /* derived name */
{
    int n;
    unsigned long long *e;

    if (stripTag != 0) {
        e = PacketBufferStruct.ptr.d - 2;
        n = e - (unsigned long long *)stripTag;
        if (n & 1) {
            *PacketBufferStruct.ptr.d++ = 0;
        }
        ((sceGifTag *)stripTag)->NLOOP = n / 3;
    }
}

static void set_vertex(LightningVtx *dir, LightningVtx *pos, float u, int *col, float half)
{
    VECTOR uv[2] = {{0.0f, u, 1.0f, 0.0f}, {1.0f, u, 1.0f, 0.0f}};
    LightningVtx v;
    LightningVtx e[2];
    LightningVtx n;
    LightningVtx pt;
    LightningVtx t;
    LightningVtx xyz;
    LightningVtx clip;
    float q;
    int i;
    int restrip = 1; /* local debug switch, see the reopen below */

    apply_matrix_w1(&v, matrixptr + 0x80, pos);
    sceVu0OuterProduct(&n, &v, dir);
    n.f[2] = 0.0f;
    sceVu0Normalize(&n, &n);
    sceVu0ScaleVectorXYZ(&n, &n, half);
    sceVu0SubVector(&e[0], &v, &n);
    sceVu0AddVector(&e[1], &v, &n);

    for (i = 0; i < 2; i++) {
        apply_matrix_w1(&pt, matrixptr + 0xC0, &e[i]);
        /* div.s: a bolt vertex on the camera plane (view z 0, so w 0; the
           stage 47 bolts after the stage 54 exit) gives Fmax on the EE, not
           Inf */
        q = ps2_div(1.0f, pt.f[3]);
        sceVu0ScaleVectorXYZ(&pt, &pt, q);
        sceVu0ScaleVectorXYZ(&t, &uv[i], q);
        sceVu0FTOI4Vector(&xyz, &pt);
        apply_matrix_w1(&clip, matrixptr + 0x1C0, &e[i]);
        if (clip_flags(&clip) & 0x3FFFF) {
            stripOn = 0;
        } else if (stripOn == 0 && vtxCount >= 2) {
            close_strip();
            /* The strip-state reset DrawLightning2 opens with, then the
               reopened strip unless restrip was cleared from the debugger
               (the strip then stays closed: close_strip() finds no tag and
               nothing more is written).  The code never clears restrip. */
            stripOn = 0;
            stripTag = 0;
            if (restrip) {
                *PacketBufferStruct.ptr.d++ = 0x1400000000008001LL;
                *PacketBufferStruct.ptr.d++ = 0;
                *PacketBufferStruct.ptr.d++ = 84;

                *PacketBufferStruct.ptr.d++ = 0;

                stripTag = PacketBufferStruct.ptr.c;
                *PacketBufferStruct.ptr.d++ = 0x3400000000008000LL;
                *PacketBufferStruct.ptr.d++ = 1313;

                *PacketBufferStruct.ptr.d++ = lastVtx[0].rgbaq;
                *PacketBufferStruct.ptr.d++ = lastVtx[0].uv;
                *PacketBufferStruct.ptr.d++ = lastVtx[0].xyz;
                *PacketBufferStruct.ptr.d++ = lastVtx[1].rgbaq;
                *PacketBufferStruct.ptr.d++ = lastVtx[1].uv;
                *PacketBufferStruct.ptr.d++ = lastVtx[1].xyz;

                stripOn = 1;
            }
        }

        lastVtx[0] = lastVtx[1];
        lastVtx[1].rgbaq = ((long long)col[0] | ((long long)col[1] << 8) |
                            ((long long)col[2] << 16) | ((long long)col[3] << 24)) |
                           ((long long)fbits(q) << 32);
        lastVtx[1].uv = (long long)t.i[0] | ((long long)t.i[1] << 32);
        lastVtx[1].xyz =
            (long long)xyz.i[0] | ((long long)xyz.i[1] << 16) | ((long long)xyz.i[2] << 32);

        if (stripOn) {
            *PacketBufferStruct.ptr.d++ = lastVtx[1].rgbaq;
            *PacketBufferStruct.ptr.d++ = lastVtx[1].uv;
            *PacketBufferStruct.ptr.d++ = lastVtx[1].xyz;
        }
        vtxCount++;
    }
}

/* out = the 3x4 part of m applied to in */
inline void apply_m34(void *out, void *m, void *in)
{
    /* m[0]*x + m[1]*y + m[2]*z, all four fields (vmulax, vmadday, vmaddz) */
    const float (*a)[4] = (const float (*)[4])m;
    const float *v = in;
    float r[4];
    int k;

    for (k = 0; k < 4; k++) {
        r[k] = a[0][k] * v[0];
        r[k] = r[k] + a[1][k] * v[1];
        r[k] = r[k] + a[2][k] * v[2];
    }
    __builtin_memcpy(out, r, sizeof r);
}

/* a random value between lo and hi */
static __inline__ float random_range(float lo, float hi) /* derived name */
{
    return _GetRandom() * (hi - lo) + lo;
}

/* x with a random sign */
static __inline__ float random_sign(float x) /* derived name */
{
    if (_GetRandom() <= 0.5f) {
        x = -x;
    }
    return x;
}

/* the Catmull-Rom basis, halved, that turns four control points into the
   segment's cubic coefficients */
static LightningMtx catmullRom = {
    /* derived name */
    {-0.5f, 1.5f, -1.5f, 0.5f},
    {1.0f, -2.5f, 2.0f, -0.5f},
    {-0.5f, 0.0f, 0.5f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
};

#include <stdio.h>

/* The segment the bolt position s is on: (int)s, as the EE's cvt.w.s
   (saturating), kept to 0..num-2 so m[seg] and v[seg + 1] stay inside the
   arrays. s at or past the last node is the bolt's end (no segment is read
   then) and passes. Anything else outside the range is a non-finite or
   runaway s (the EE has no NaN; the stage 45 one came from BgAnimation.c's
   last-key read, fixed there); reported once. */
static int lightningSeg(float s, int num) /* derived name */
{
    static int reported;
    int seg = ps2_ftoi(s);

    if (s >= (float)(num - 1)) {
        return seg;
    }
    if (seg < 0 || seg > num - 2) {
        if (!reported) {
            reported = 1;
            fprintf(stderr, "lightning: segment %d of %d (s %g) clamped; reported once\n", seg,
                    num - 1, (double)s);
        }
        seg = seg < 0 ? 0 : num - 2;
    }
    return seg;
}

void DrawLightning2(int num, LightningVtx *v, LightningColor *col, float stepMin, float stepMax,
                    float swayStepMin, float swayStepMax, float turnMin, float turnMax,
                    float swayLimit, float width, float texLen, float seed, int c)
{
    LightningMtx m[num - 1];
    float half = 0.5f;
    float wa = width * half;
    float wb = width - wa;
    float one = 1.0f;
    float two = 2.0f;
    LightningVtx ccol;
    LightningVtx a;
    LightningVtx b;
    LightningVtx dir;
    LightningVtx cur;
    LightningVtx prev;
    LightningVtx t;
    LightningVtx q;
    LightningVtx dv;
    LightningVtx tmp;
    LightningVtx basis;
    LightningVtx pos;
    LightningVtx sa;
    LightningVtx sb;
    LightningVtx delta;
    LightningVtx nrm;
    LightningVtx avg;
    LightningVtx out;
    LightningVtx ccol2;
    LightningVtx icol;
    float s;
    float f;
    float u;
    float nu;
    float sc;
    float lim;
    float last;
    float sway;
    float d;
    float ang;
    float amp;
    float wd;
    float ui;
    float ni;
    int no;
    int n;
    int i;
    int seg;
    int first;
    int done;
    char *pk;
    char *p;
    unsigned long long *top;

    no = tex_GetTextureNo("lightning_test");
    if (no >= 0) {
        tex_TransTexture(no, 6);
    }
    if (num < 2) {
        return;
    }
    gif_StartPacketPri(6);
    if (dpk_CheckBufferSize() >= 64) {
        gif_SetAlpha(1, lightningHostMode(c), 128);
        gif_SetGsReg(78, 0x1300000C0LL);
        gif_SetGsReg(8, 1);
        gif_SetGsReg(0, 84);
    }
    gif_EndPacket();
    dl_SetDLPriority(6);
    pk = PacketBufferStruct.ptr.c;
    top = (unsigned long long *)(pk + 16);
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = pk;
    PacketBufferStruct.ptr.c = pk + 8;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = pk;
    ((GifPkWord *)(pk + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = pk + 12;
    PacketBufferStruct.ptr.d = top;
    stripOn = 0;
    stripTag = 0;
    vtxCount = 0;
    if (seed != 0.0f) {
        seed = __builtin_fabsf(seed);
        seed = seed - (int)seed + one;
        if (seed == one) {
            seed = 1.5f;
        }
        {
            /* the float's bits into the R register ("r" moved them to a GPR) */
            uint32_t bits;

            __builtin_memcpy(&bits, &seed, sizeof bits);
            ico_vu0_random_set(bits);
        }
    }
    if (stepMin < 15.0f) {
        stepMin = 15.0f;
    }
    sceVu0SubVector(&dir, &v[1], &v[0]);
    sceVu0Normalize(&dir, &dir);
    a.f[0] = random_unit();
    a.f[1] = random_unit();
    a.f[2] = random_unit();
    a.f[3] = 1.0f;
    sceVu0OuterProduct(&b, &dir, &a);
    sceVu0Normalize(&b, &b);
    sceVu0OuterProduct(&a, &b, &dir);
    {
        int np = num + 2;
        LightningVtx w[np + 2];

        sceVu0SubVector(&t, &v[0], &v[1]);
        sceVu0AddVector(&w[0], &v[0], &t);
        for (i = 0; i < num; i++) {
            sceVu0CopyVector(&w[i + 1], &v[i]);
        }
        sceVu0SubVector(&t, &v[num - 1], &v[num - 2]);
        sceVu0AddVector(&w[num + 1], &v[num - 1], &t);
        for (i = 0; i < np - 3; i++) {
            sceVu0MulMatrix(m[i], &w[i], catmullRom);
        }
    }
    sceVu0CopyVector(&cur, &v[0]);
    sceVu0ITOF0Vector(&ccol, col);
    last = (float)(num - 1);
    u = 0.0f;
    s = u;
    done = 0;
    first = 1;
    sway = 0.0f;
    ang = random_range(0.0f, 6.2831855f);
    for (;;) {
        seg = lightningSeg(s, num);
        f = s - (float)seg;
        if (dpk_CheckBufferSize() < 64) {
            goto end;
        }
        tmp.f[0] = 3.0f * f * f;
        tmp.f[1] = 2.0f * f;
        tmp.f[2] = 1.0f;
        tmp.f[3] = 0.0f;
        dv = tmp;
        sceVu0ApplyMatrix(&dir, m[seg], &dv);
        sceVu0Normalize(&dir, &dir);
        sceVu0ScaleVectorXYZ(&tmp, &a, sceVu0InnerProduct(&dir, &a));
        sceVu0SubVector(&a, &a, &tmp);
        sceVu0Normalize(&a, &a);
        sceVu0OuterProduct(&b, &dir, &a);
        /* div.s: a zero-length segment (two nodes at one place) gives Fmax on
           the EE, not Inf */
        s += ps2_div(random_range(stepMin, stepMax), _GetLength(&v[seg + 1], &v[seg]));
        seg = lightningSeg(s, num);
        f = s - (float)seg;
        lim = (float)(num - 1) - half;
        if (s < half) {
            sc = GetTableSin((short)(s * two * 1.5707964f * 10430.378f));
        } else if (lim <= s) {
            /* cvt.w.s: past a zero-length segment s is Fmax (above), the
               product overflows (-Fmax on the EE, -Inf here) and the
               conversion saturates to 0x80000000 */
            sc = GetTableSin((short)ps2_ftoi((1.0f - (s - lim) * two) * 1.5707964f * 10430.378f));
        } else {
            sc = 1.0f;
        }
        if (last <= s) {
            sceVu0CopyVector(&q, &v[num - 1]);
            done = 1;
        } else {
            basis = (LightningVtx){{f * f * f, f * f, f, 1.0f}};
            sceVu0ApplyMatrix(&pos, m[seg], &basis);
            d = random_sign(random_range(swayStepMin, swayStepMax));
            if (__builtin_fabsf(sway + d) > swayLimit) {
                d = -d;
            }
            sway += d;
            amp = sway * sc;
            ang +=
                random_sign(random_range(degrees_to_radians(turnMin), degrees_to_radians(turnMax)));
            sceVu0ScaleVectorXYZ(&sa, &a, GetTableCos((short)(ang * 10430.378f)) * amp);
            sceVu0ScaleVectorXYZ(&sb, &b, GetTableSin((short)(ang * 10430.378f)) * amp);
            sceVu0AddVector(&q, &pos, &sa);
            sceVu0AddVector(&q, &q, &sb);
        }
        sceVu0SubVector(&delta, &q, &cur);
        sceVu0Normalize(&nrm, &delta);
        if (first) {
            sceVu0CopyVector(&prev, &nrm);
        }
        sceVu0AddVector(&avg, &nrm, &prev);
        sceVu0ScaleVectorXYZ(&avg, &avg, 0.5f);
        sceVu0CopyVector(&prev, &nrm);
        apply_m34(&out, matrixptr + 0x80, &avg);
        sceVu0ScaleVectorXYZ(&ccol2, &ccol, sc * (1.0f - half) + half);
        sceVu0FTOI0Vector(&icol, &ccol2);
        wd = (sc * wb + wa) * (1.0f - _GetRandom() * half);
        set_vertex(&out, &cur, u, icol.i, wd);
        nu = u + _GetNorm(&delta) / texLen;
        if (2048.0f <= nu * 32.0f) {
            ui = (int)u;
            ni = (int)nu;
            set_vertex(&out, &cur, u - ui, icol.i, wd);
            nu -= ni;
        }
        u = nu;
        if (done) {
            set_vertex(&out, &q, u, icol.i, 0.0f);
            break;
        }
        sceVu0CopyVector(&cur, &q);
        first = 0;
    }
end:
    close_strip();
    n = PacketBufferStruct.ptr.d - top;
    if (n & 1) {
        *PacketBufferStruct.ptr.d++ = 0;
    }
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                         4) -
                        1) |
                       0x10000000);
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) | 0x50000000;
    p = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = p;
    ((GifPkWord *)p)->d = 0x60000000;
    PacketBufferStruct.ptr.c = p + 8;
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 0xC;
    ((GifPkWord *)(p + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = p + 0x10;
    if (n > 0) {
        dl_SetDLPriority(dl_GetPri());
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
        dl_CloseDma();
#ifdef ICO_RD
        mc_HostDma(5, PacketBufferStruct.dma.c, 0);
        {
            static int reported;

            if (!reported) {
                reported = 1;
                fprintf(stderr, "lightning: first bolt drawn (mode %d; reported once)\n", c);
            }
        }
#endif
    }
}

static inline int cmpr(LightningNode *self, LightningNode *other);

void DrawLightningN(int num, LightningNode *v, void *col, float stepMin, float stepMax,
                    float swayStepMin, float swayStepMax, float turnMin, float turnMax,
                    float swayLimit, float width, float texLen, float seed, int c)
{
    LightningVtx buf[num];
    int i;

    if (num >= 3) {
        qsort(&v[1], num - 1, sizeof(LightningNode), (int (*)(const void *, const void *))cmpr);
    }
    for (i = 0; i < num; i++) {
        sceVu0CopyVector(&buf[i], &v[i]);
    }
    DrawLightning2(num, buf, col, stepMin, stepMax, swayStepMin, swayStepMax, turnMin, turnMax,
                   swayLimit, width, texLen, seed, c);
}

static inline int cmpr(LightningNode *self, LightningNode *other)
{
    return self->key - other->key;
}

inline void DrawLightning(void *from, void *to, void *col, float stepMin, float stepMax,
                          float swayStepMin, float swayStepMax, float turnMin, float turnMax,
                          float swayLimit, float width, float texLen, float seed, int c)
{
    LightningVtx buf[2];
    sceVu0CopyVector(&buf[0], from);
    sceVu0CopyVector(&buf[1], to);
    DrawLightning2(2, buf, col, stepMin, stepMax, swayStepMin, swayStepMax, turnMin, turnMax,
                   swayLimit, width, texLen, seed, c);
}

inline void lightning_test(void)
{
    LightningColor col = {{0x80, 0xFF, 0xFF, 0x80}};
    LightningPath vtx = {{
        {{0.0f, 750.0f, 0.0f, 1.0f}},
        {{0.0f, 500.0f, -200.0f, 1.0f}},
        {{0.0f, 250.0f, 200.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f, 1.0f}},
    }};
    DrawLightning2(4, vtx.v, &col, 5.0f, 25.0f, 5.0f, 25.0f, 5.0f, 10.0f, 70.0f, 8.0f, 20.0f, 0.0f,
                   0);
}
