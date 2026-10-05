/*
 * port/math/libvu0.c
 *
 * The host's libvu0: the sceVu0* entry points behind the signatures of
 * port/compat/libvu0.h, written in C from the instruction sequences of this
 * repository's clean-room reconstruction (sce/libvu0/libvu0.c, MIT).
 * docs/port/MATH.md has the notes per routine.
 *
 * On the PS2 these routines used VU0 registers vf4-vf9, which are also where
 * seki/src/Matrix.c keeps its current matrix; on the host they touch nothing
 * but their arguments (MATH.md, "Register side effects").
 */
#include <string.h>

#include "ico_math.h"
#include "../compat/libvu0.h"

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void sceVu0ApplyMatrix(void *dst, void *m, void *v)
{
    float mm[4][4];

    memcpy(mm, m, sizeof mm);
    ico_apply_matrix((float *)dst, (const float (*)[4])mm, (const float *)v);
}

/* Row i of dst is m0 applied to row i of m1. dst may alias m0 or m1. */
void sceVu0MulMatrix(void *dst, void *m0, void *m1)
{
    float a[4][4];
    float b[4][4];
    float out[4][4];
    int i;

    memcpy(a, m0, sizeof a);
    memcpy(b, m1, sizeof b);
    for (i = 0; i < 4; i++) {
        ico_apply_matrix(out[i], (const float (*)[4])a, b[i]);
    }
    memcpy(dst, out, sizeof out);
}

/* dst = a x b, w = 0. */
void sceVu0OuterProduct(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4];

    r[0] = p[1] * q[2] - q[1] * p[2];
    r[1] = p[2] * q[0] - q[2] * p[0];
    r[2] = p[0] * q[1] - q[0] * p[1];
    r[3] = 0.0f;
    memcpy(dst, r, sizeof r);
}

float sceVu0InnerProduct(void *a, void *b)
{
    return dot3(a, b);
}

/* xyz times 1/sqrt(len2), computed as vsqrt then vdiv 1/x (two roundings,
   unlike Matrix.c's _NormalizeVector, which uses vrsqrt); w = 0. */
void sceVu0Normalize(void *dst, void *src)
{
    const float *s = src;
    float q = ps2_div(1.0f, ps2_sqrt(dot3(s, s)));
    float r[4] = {s[0] * q, s[1] * q, s[2] * q, 0.0f};
    memcpy(dst, r, sizeof r);
}

void sceVu0TransposeMatrix(void *dst, void *src)
{
    float s[4][4];
    float t[4][4];
    int i;
    int j;

    memcpy(s, src, sizeof s);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            t[i][j] = s[j][i];
        }
    }
    memcpy(dst, t, sizeof t);
}

/* Inverse of a rigid transform, as seki/src/Matrix.c's _InversMatrix (the
   same instruction sequence). */
void sceVu0InversMatrix(void *dst, void *src)
{
    float s[4][4];
    float d[4][4];
    int i;

    memcpy(s, src, sizeof s);
    for (i = 0; i < 3; i++) {
        d[i][0] = s[0][i];
        d[i][1] = s[1][i];
        d[i][2] = s[2][i];
        d[i][3] = 0.0f;
    }
    for (i = 0; i < 3; i++) {
        d[3][i] = 0.0f - (d[0][i] * s[3][0] + d[1][i] * s[3][1] + d[2][i] * s[3][2]);
    }
    d[3][3] = s[3][3];
    memcpy(dst, d, sizeof d);
}

/* All four fields times 1/q (vdiv, then vmulq). */
void sceVu0DivVector(void *dst, void *src, float q)
{
    const float *s = src;
    float k = ps2_div(1.0f, q);
    float r[4] = {s[0] * k, s[1] * k, s[2] * k, s[3] * k};
    memcpy(dst, r, sizeof r);
}

void sceVu0DivVectorXYZ(void *dst, void *src, float q)
{
    const float *s = src;
    float k = ps2_div(1.0f, q);
    float r[4] = {s[0] * k, s[1] * k, s[2] * k, s[3]};
    memcpy(dst, r, sizeof r);
}

/* dst = a*t + b*(1 - t), all four fields. */
void sceVu0InterVector(void *dst, void *a, void *b, float t)
{
    const float *p = a;
    const float *q = b;
    float u = 1.0f - t;
    float r[4] = {p[0] * t + q[0] * u, p[1] * t + q[1] * u, p[2] * t + q[2] * u,
                  p[3] * t + q[3] * u};
    memcpy(dst, r, sizeof r);
}

/* As sceVu0InterVector for xyz; w is a's. */
void sceVu0InterVectorXYZ(void *dst, void *a, void *b, float t)
{
    const float *p = a;
    const float *q = b;
    float u = 1.0f - t;
    float r[4] = {p[0] * t + q[0] * u, p[1] * t + q[1] * u, p[2] * t + q[2] * u, p[3]};
    memcpy(dst, r, sizeof r);
}

void sceVu0AddVector(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] + q[0], p[1] + q[1], p[2] + q[2], p[3] + q[3]};
    memcpy(dst, r, sizeof r);
}

void sceVu0SubVector(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3] - q[3]};
    memcpy(dst, r, sizeof r);
}

void sceVu0MulVector(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] * q[0], p[1] * q[1], p[2] * q[2], p[3] * q[3]};
    memcpy(dst, r, sizeof r);
}

void sceVu0ScaleVector(void *dst, void *src, float scale)
{
    const float *s = src;
    float r[4] = {s[0] * scale, s[1] * scale, s[2] * scale, s[3] * scale};
    memcpy(dst, r, sizeof r);
}

/* xyz scaled, w copied. */
void sceVu0ScaleVectorXYZ(void *dst, void *src, float scale)
{
    const float *s = src;
    float r[4] = {s[0] * scale, s[1] * scale, s[2] * scale, s[3]};
    memcpy(dst, r, sizeof r);
}

/* Rows 0-2 copied; row 3's xyz plus tv's xyz, w kept. */
void sceVu0TransMatrix(void *dst, void *src, void *tv)
{
    float m[4][4];
    const float *t = tv;

    memcpy(m, src, sizeof m);
    m[3][0] = m[3][0] + t[0];
    m[3][1] = m[3][1] + t[1];
    m[3][2] = m[3][2] + t[2];
    memcpy(dst, m, sizeof m);
}

void sceVu0CopyVector(void *dst, void *src)
{
    memmove(dst, src, 16);
}

void sceVu0CopyMatrix(void *dst, void *src)
{
    memmove(dst, src, 64);
}

void sceVu0CopyVectorXYZ(void *dst, void *src)
{
    ((float *)dst)[0] = ((float *)src)[0];
    ((float *)dst)[1] = ((float *)src)[1];
    ((float *)dst)[2] = ((float *)src)[2];
}

void sceVu0FTOI4Vector(void *dst, void *src)
{
    const float *s = src;
    int32_t r[4] = {ps2_ftoi4(s[0]), ps2_ftoi4(s[1]), ps2_ftoi4(s[2]), ps2_ftoi4(s[3])};
    memcpy(dst, r, sizeof r);
}

void sceVu0FTOI0Vector(void *dst, void *src)
{
    const float *s = src;
    int32_t r[4] = {ps2_ftoi(s[0]), ps2_ftoi(s[1]), ps2_ftoi(s[2]), ps2_ftoi(s[3])};
    memcpy(dst, r, sizeof r);
}

/* 28.4 fixed point to float: the conversion, then times 1/16 (exact). */
void sceVu0ITOF4Vector(void *dst, void *src)
{
    int32_t s[4];
    float r[4];
    int i;

    memcpy(s, src, sizeof s);
    for (i = 0; i < 4; i++) {
        r[i] = ps2_itof(s[i]) * 0.0625f;
    }
    memcpy(dst, r, sizeof r);
}

void sceVu0ITOF0Vector(void *dst, void *src)
{
    int32_t s[4];
    float r[4];
    int i;

    memcpy(s, src, sizeof s);
    for (i = 0; i < 4; i++) {
        r[i] = ps2_itof(s[i]);
    }
    memcpy(dst, r, sizeof r);
}

void sceVu0UnitMatrix(void *m)
{
    static const float identity[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    memcpy(m, identity, sizeof identity);
}

/* --- Rotations ---------------------------------------------------------
 * sceVu0RotMatrix[XYZ] reduce the angle to a sine argument with the EE FPU,
 * evaluate the sine with libvu0's odd polynomial (_sceVu0ecossin) and get
 * the cosine as sqrt(1 - sin^2), signed by the branch taken.
 */

/* _sceVu0ecossin's coefficients: 1/9!, -1/7!, 1/5!, -1/3! (sceVu0SinCoeff). */
static const float sinCoeff[4] = {2.601887e-06f, -0.00019807414f, 0.0083330255f, -0.16666657f};

/* Returns sin(angle) in *s and cos(angle) in *c, as the PS2 computes them.
 * For angle < 0 the polynomial runs on pi/2 + angle and the sine is
 * -sqrt(1 - p^2); otherwise on pi/2 - angle and +sqrt(1 - p^2). The
 * polynomial value p (the cosine) is a + c3*a^3 + c5*a^5 + c7*a^7 + c9*a^9,
 * each power built by repeated multiplication by a^2 in the order below. */
static void ecossin(float angle, float *s, float *c)
{
    const float halfPi = 1.57079637f; /* 0x3FC90FDB */
    int negative = angle < 0.0f;
    float a = negative ? halfPi + angle : halfPi - angle;
    float a2 = a * a;
    float t3 = sinCoeff[3] * a * a2;
    float t5 = sinCoeff[2] * a * a2 * a2;
    float t7 = sinCoeff[1] * a * a2 * a2 * a2;
    float t9 = sinCoeff[0] * a * a2 * a2 * a2 * a2;
    float p = a + t3 + t5 + t7 + t9;
    float r = ps2_sqrt(1.0f - p * p);

    *c = p;
    *s = negative ? 0.0f - r : r;
}

/* dst row i = r applied to src row i, for each of the four rows. */
static void rot_apply(void *dst, void *src, const float (*r)[4])
{
    float s[4][4];
    float out[4][4];
    int i;

    memcpy(s, src, sizeof s);
    for (i = 0; i < 4; i++) {
        ico_apply_matrix(out[i], r, s[i]);
    }
    memcpy(dst, out, sizeof out);
}

void sceVu0RotMatrixZ(void *d, void *s, float a)
{
    float sn;
    float cs;

    ecossin(a, &sn, &cs);
    {
        float r[4][4] = {
            {cs, sn, 0.0f, 0.0f},
            {0.0f - sn, cs, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        rot_apply(d, s, (const float (*)[4])r);
    }
}

void sceVu0RotMatrixX(void *d, void *s, float a)
{
    float sn;
    float cs;

    ecossin(a, &sn, &cs);
    {
        float r[4][4] = {
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, cs, sn, 0.0f},
            {0.0f, 0.0f - sn, cs, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        rot_apply(d, s, (const float (*)[4])r);
    }
}

void sceVu0RotMatrixY(void *d, void *s, float a)
{
    float sn;
    float cs;

    ecossin(a, &sn, &cs);
    {
        float r[4][4] = {
            {cs, 0.0f, 0.0f - sn, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {sn, 0.0f, cs, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        rot_apply(d, s, (const float (*)[4])r);
    }
}

void sceVu0RotMatrix(void *dst, void *src, float *fa)
{
    sceVu0RotMatrixZ(dst, src, fa[2]);
    sceVu0RotMatrixY(dst, dst, fa[1]);
    sceVu0RotMatrixX(dst, dst, fa[0]);
}

/* Each field clamped to [min, max]: vmax with min, then vmini with max. */
void sceVu0ClampVector(void *dst, void *src, float min, float max)
{
    const float *s = src;
    float r[4];
    int i;

    for (i = 0; i < 4; i++) {
        r[i] = ps2_min(ps2_max(s[i], min), max);
    }
    memcpy(dst, r, sizeof r);
}

/* Project: apply m, x/y/z times 1/w, then 28.4 integers in all four
   fields, or with mode != 0, integer z and w (vftoi0). */
void sceVu0RotTransPers(void *dst, void *m, void *src, int mode)
{
    float mm[4][4];
    float v[4];
    float q;
    int32_t r[4];

    memcpy(mm, m, sizeof mm);
    ico_apply_matrix(v, (const float (*)[4])mm, (const float *)src);
    q = ps2_div(1.0f, v[3]);
    v[0] = v[0] * q;
    v[1] = v[1] * q;
    v[2] = v[2] * q;
    r[0] = ps2_ftoi4(v[0]);
    r[1] = ps2_ftoi4(v[1]);
    if (mode) {
        r[2] = ps2_ftoi(v[2]);
        r[3] = ps2_ftoi(v[3]);
    } else {
        r[2] = ps2_ftoi4(v[2]);
        r[3] = ps2_ftoi4(v[3]);
    }
    memcpy(dst, r, sizeof r);
}

/* sceVu0RotTransPers over n vertices (16 bytes each). */
void sceVu0RotTransPersN(void *dst, void *m, void *src, int n, int mode)
{
    int i;

    for (i = 0; i < n; i++) {
        sceVu0RotTransPers((char *)dst + i * 16, m, (char *)src + i * 16, mode);
    }
}

/* The C members, as in sce/libvu0/libvu0.c. */
void sceVu0CameraMatrix(void *m, void *p, void *zd, void *yd)
{
    float buf[0x50 / 4];
    char *b = (char *)buf;

    sceVu0UnitMatrix(b);
    sceVu0OuterProduct(b + 0x40, yd, zd);
    sceVu0Normalize(b, b + 0x40);
    sceVu0Normalize(b + 0x20, zd);
    sceVu0OuterProduct(b + 0x10, b + 0x20, b);
    sceVu0TransMatrix(b, b, p);
    sceVu0InversMatrix(m, b);
}

void sceVu0NormalLightMatrix(void *m, void *l0, void *l1, void *l2)
{
    float buf[4];

    sceVu0ScaleVector(buf, l0, -1.0f);
    sceVu0Normalize(m, buf);
    sceVu0ScaleVector(buf, l1, -1.0f);
    sceVu0Normalize((char *)m + 0x10, buf);
    sceVu0ScaleVector(buf, l2, -1.0f);
    sceVu0Normalize((char *)m + 0x20, buf);
    {
        float fzero = 0.0f;
        *(float *)((char *)m + 0x38) = fzero;
        *(float *)((char *)m + 0x3C) = 1.0f;
        *(float *)((char *)m + 0x34) = fzero;
        *(float *)((char *)m + 0x30) = fzero;
    }
    sceVu0TransposeMatrix(m, m);
}

void sceVu0LightColorMatrix(void *m, void *c0, void *c1, void *c2, void *c3)
{
    sceVu0CopyVector((void *)m, c0);
    sceVu0CopyVector((char *)m + 0x10, c1);
    sceVu0CopyVector((char *)m + 0x20, c2);
    sceVu0CopyVector((char *)m + 0x30, c3);
}

/* VIF0 reset: no VU0 hardware on the host. */
void sceVpu0Reset(void) {}
