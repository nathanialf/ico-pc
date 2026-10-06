/*
 * port/math/matrix.c
 *
 * seki/src/Matrix.c's vector and matrix routines that do not touch the
 * current matrix (matrix_stack.c has those), and VU0's R-register random
 * numbers. Each body follows the assembly's order of operations; the
 * comments say where a field is left alone or comes out as a constant.
 */
#include <string.h>

#include "ico_math.h"
#include "../../ico2/seki/include/Matrix.h"

typedef float vec4[4];

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* a x b with the products in VU0's vopmula/vopmsub pairing. */
static void cross3(float *out, const float *a, const float *b)
{
    float x = a[1] * b[2] - b[1] * a[2];
    float y = a[2] * b[0] - b[2] * a[0];
    float z = a[0] * b[1] - b[0] * a[1];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

float _Sqrt(float x)
{
    return ps2_sqrt(x);
}

/* xyz times 1/sqrt(x*x + y*y + z*z) (vrsqrt); w copied. */
void _NormalizeVector(void *dst, void *src)
{
    const float *s = src;
    float *d = dst;
    float q = ps2_rsqrt(1.0f, dot3(s, s));
    float w = s[3];

    d[0] = s[0] * q;
    d[1] = s[1] * q;
    d[2] = s[2] * q;
    d[3] = w;
}

/* The PS2 function returned through $f0 with no C return statement. */
float _InnerProduct(void *a, void *b)
{
    return dot3(a, b);
}

/* dst = a x b, w = 0. */
void _OuterProduct(void *dst, void *a, void *b)
{
    float *d = dst;
    float c[3];

    cross3(c, a, b);
    d[0] = c[0];
    d[1] = c[1];
    d[2] = c[2];
    d[3] = 0.0f;
}

void _AddVector(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float *d = dst;
    vec4 r = {p[0] + q[0], p[1] + q[1], p[2] + q[2], p[3] + q[3]};
    memcpy(d, r, sizeof r);
}

/* xyz summed, w is a's. */
void _AddVectorXYZ(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    vec4 r = {p[0] + q[0], p[1] + q[1], p[2] + q[2], p[3]};
    memcpy(dst, r, sizeof r);
}

void _SubVector(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    vec4 r = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3] - q[3]};
    memcpy(dst, r, sizeof r);
}

/* xyz subtracted, w is a's. */
void _SubVectorXYZ(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    vec4 r = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3]};
    memcpy(dst, r, sizeof r);
}

void _ScaleVector(void *dst, void *src, float s)
{
    const float *p = src;
    vec4 r = {p[0] * s, p[1] * s, p[2] * s, p[3] * s};
    memcpy(dst, r, sizeof r);
}

/* xyz scaled, w copied. */
void _ScaleVectorXYZ(void *dst, void *src, float s)
{
    const float *p = src;
    vec4 r = {p[0] * s, p[1] * s, p[2] * s, p[3]};
    memcpy(dst, r, sizeof r);
}

/* a * b per field for xyz, w is a's. */
void _ScaleVector2XYZ(void *dst, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    vec4 r = {p[0] * q[0], p[1] * q[1], p[2] * q[2], p[3]};
    memcpy(dst, r, sizeof r);
}

/* All four fields to 28.4 fixed point. */
void _FTOI4Vector(void *dst, void *src)
{
    const float *p = src;
    int32_t r[4] = {ps2_ftoi4(p[0]), ps2_ftoi4(p[1]), ps2_ftoi4(p[2]), ps2_ftoi4(p[3])};
    memcpy(dst, r, sizeof r);
}

void _FTOI0Vector(void *dst, void *src)
{
    const float *p = src;
    int32_t r[4] = {ps2_ftoi(p[0]), ps2_ftoi(p[1]), ps2_ftoi(p[2]), ps2_ftoi(p[3])};
    memcpy(dst, r, sizeof r);
}

void _CopyVector(void *dst, void *src)
{
    memmove(dst, src, 16);
}

void _CopyIVector(void *dst, void *src)
{
    memmove(dst, src, 16);
}

/* (0, 0, 0, 1). */
void _UnitVector(void *dst)
{
    static const vec4 unit = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(dst, unit, sizeof unit);
}

/* dst = a*t + b*(1 - t), all four fields: note that t weights a. */
void _InterVector(void *dst, void *a, void *b, float t)
{
    const float *p = a;
    const float *q = b;
    float u = 1.0f - t;
    vec4 r = {p[0] * t + q[0] * u, p[1] * t + q[1] * u, p[2] * t + q[2] * u, p[3] * t + q[3] * u};
    memcpy(dst, r, sizeof r);
}

/* As _InterVector for xyz; w is a's. */
void _InterVectorXYZ(void *dst, void *a, void *b, float t)
{
    const float *p = a;
    const float *q = b;
    float u = 1.0f - t;
    vec4 r = {p[0] * t + q[0] * u, p[1] * t + q[1] * u, p[2] * t + q[2] * u, p[3]};
    memcpy(dst, r, sizeof r);
}

float _GetNorm(void *v)
{
    return ps2_sqrt(dot3(v, v));
}

float _GetLength(void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    vec4 d = {p[0] - q[0], p[1] - q[1], p[2] - q[2], 0.0f};
    return ps2_sqrt(dot3(d, d));
}

float _GetLengthXY(void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float dx = p[0] - q[0];
    float dy = p[1] - q[1];
    return ps2_sqrt(dx * dx + dy * dy);
}

float _GetLengthXZ(void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float dx = p[0] - q[0];
    float dz = p[2] - q[2];
    return ps2_sqrt(dx * dx + dz * dz);
}

void _CopyMatrix(void *dst, const void *src)
{
    memmove(dst, src, 64);
}

/* Row i of dst is a applied to row i of b. dst may alias a or b. */
void _MulMatrix(void *dst, void *a, void *b)
{
    float ma[4][4];
    float mb[4][4];
    float out[4][4];
    int i;

    memcpy(ma, a, sizeof ma);
    memcpy(mb, b, sizeof mb);
    for (i = 0; i < 4; i++) {
        ico_apply_matrix(out[i], (const float (*)[4])ma, mb[i]);
    }
    memcpy(dst, out, sizeof out);
}

void _ApplyMatrix(void *dst, void *m, void *v)
{
    float mm[4][4];

    memcpy(mm, m, sizeof mm);
    ico_apply_matrix((float *)dst, (const float (*)[4])mm, (const float *)v);
}

void _UnitMatrix(void *dst)
{
    static const float identity[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    memcpy(dst, identity, sizeof identity);
}

/* The first three rows of the identity; row 3 (the translation) is kept. */
void _UnitRotation(void *dst)
{
    static const float rows[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    memcpy(dst, rows, sizeof rows);
}

void _TransposeMatrix(void *dst, void *src)
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

/* Inverse of a rigid transform: rows 0-2 become the transposed 3x3 with
   w = 0; row 3 is -(R^T t) for xyz and keeps src's w. */
void _InversMatrix(void *dst, void *src)
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

/* Rows 0-2: the three vectors negated and normalised (xyz), transposed into
   columns. Row 3 is (s0.w, s1.w, s2.w, 1): each source's w passes through
   the transpose. */
void _MakeNormalLightMatrix(void *dst, void *s0, void *s1, void *s2)
{
    const float *src[3] = {s0, s1, s2};
    float n[3][4];
    float d[4][4];
    int i;

    for (i = 0; i < 3; i++) {
        float x = src[i][0] * -1.0f;
        float y = src[i][1] * -1.0f;
        float z = src[i][2] * -1.0f;
        float q = ps2_rsqrt(1.0f, x * x + y * y + z * z);
        n[i][0] = x * q;
        n[i][1] = y * q;
        n[i][2] = z * q;
        n[i][3] = src[i][3];
    }
    for (i = 0; i < 4; i++) {
        d[i][0] = n[0][i];
        d[i][1] = n[1][i];
        d[i][2] = n[2][i];
        d[i][3] = (i == 3) ? 1.0f : 0.0f;
    }
    memcpy(dst, d, sizeof d);
}

void _MakeLightColorMatrix(void *dst, void *s0, void *s1, void *s2, void *s3)
{
    float d[4][4];

    memcpy(d[0], s0, 16);
    memcpy(d[1], s1, 16);
    memcpy(d[2], s2, 16);
    memcpy(d[3], s3, 16);
    memcpy(dst, d, sizeof d);
}

/* --- R, VU0's random number register ------------------------------------
 * A 23-bit LFSR read as a float in [1, 2): bits 0x3F800000 | state.
 * Advance: take bits 4 and 22, shift left one, XOR the two into bit 0
 * (float-semantics.md, "The R register"; the polynomial is PCSX2's and is
 * unverified on hardware). */
static uint32_t vu0R = 0x3F800000u;

static uint32_t r_bits(uint32_t x)
{
    return 0x3F800000u | (x & 0x007FFFFFu);
}

static float r_next(void)
{
    uint32_t taps = ((vu0R >> 4) ^ (vu0R >> 22)) & 1u;
    vu0R = r_bits((vu0R << 1) ^ taps);
    return ps2_bits_float(vu0R);
}

void ico_vu0_random_set(uint32_t bits)
{
    vu0R = r_bits(bits);
}

uint32_t ico_vu0_random_get(void)
{
    return vu0R;
}

/* vrinit with seed + 1, then vrxor with seed + seed (both VU adds). */
void _InitRandom(float seed)
{
    float a = seed + 1.0f;
    float b = seed + seed;
    vu0R = r_bits(ps2_float_bits(a));
    vu0R = r_bits(vu0R ^ ps2_float_bits(b));
}

/* One advance, R - 1.0: exact, (R & 0x7FFFFF) / 2^23. */
float _GetRandom(void)
{
    return r_next() - 1.0f;
}

/* No caller. Three advances, one each for x, y, z, minus 1. The PS2 also
   stored a stale vf1.w into dst[3]; the host leaves dst[3] alone. */
void _GetRandomVector(void *dst)
{
    float *d = dst;
    float x = r_next();
    float y = r_next();
    float z = r_next();

    d[0] = x - 1.0f;
    d[1] = y - 1.0f;
    d[2] = z - 1.0f;
}

/* No caller. vrnext.xyz advances once and writes the same value to x, y
   and z. dst[3] as in _GetRandomVector. */
void _GetRandomVector0(void *dst)
{
    float *d = dst;
    float r = r_next() - 1.0f;

    d[0] = r;
    d[1] = r;
    d[2] = r;
}

/* --- _RemakeNormal's helpers (static inline in Matrix.c) ----------------- */

/* x*x + z*z + y*y: the asm adds z before y. */
static float sq_len_xzy(const float *v)
{
    return v[0] * v[0] + v[2] * v[2] + v[1] * v[1];
}

/* The per-field squares with x replaced by sq_len_xzy, which is what the
   assembly leaves in the register it later scales (see _MakeNormal4). */
static void squares(float *out, const float *v)
{
    out[0] = sq_len_xzy(v);
    out[1] = v[1] * v[1];
    out[2] = v[2] * v[2];
}

static const float *vertex(void *verts, int i)
{
    return (const float *)((char *)verts + i * 16);
}

static void edge(float *e, void *verts, int i0, int i)
{
    const float *a = vertex(verts, i0);
    const float *b = vertex(verts, i);
    e[0] = b[0] - a[0];
    e[1] = b[1] - a[1];
    e[2] = b[2] - a[2];
}

static void scale3(float *out, const float *v, float q)
{
    out[0] = v[0] * q;
    out[1] = v[1] * q;
    out[2] = v[2] * q;
}

/* Normal of a triangle: the two edges from v0, each normalised, crossed. */
static void makeNormal3(void *dst, void *verts, int i0, int i1, int i2)
{
    float e1[3], e2[3], n1[3], n2[3];
    float *d = dst;

    edge(e1, verts, i0, i1);
    edge(e2, verts, i0, i2);
    scale3(n1, e1, ps2_rsqrt(1.0f, sq_len_xzy(e1)));
    scale3(n2, e2, ps2_rsqrt(1.0f, sq_len_xzy(e2)));
    cross3(d, n1, n2);
    d[3] = 0.0f;
}

/* Quad: n1 x n2 + n2 x n3, halved. As on the PS2, the third "normalised
   edge" n3 is the third edge's squares (squares()) times its 1/length, not
   the edge itself: the assembly scales the register holding the squares
   (vmulq $vf23, $vf18, Q). */
static void makeNormal4(void *dst, void *verts, int i0, int i1, int i2, int i3)
{
    float e1[3], e2[3], e3[3], s3[3], n1[3], n2[3], n3[3], c1[3], c2[3];
    float *d = dst;
    float k = 0.5f;
    int i;

    edge(e1, verts, i0, i1);
    edge(e2, verts, i0, i2);
    edge(e3, verts, i0, i3);
    scale3(n1, e1, ps2_rsqrt(1.0f, sq_len_xzy(e1)));
    scale3(n2, e2, ps2_rsqrt(1.0f, sq_len_xzy(e2)));
    squares(s3, e3);
    scale3(n3, s3, ps2_rsqrt(1.0f, s3[0]));
    cross3(c1, n1, n2);
    cross3(c2, n2, n3);
    for (i = 0; i < 3; i++) {
        d[i] = (c1[i] + c2[i]) * k;
    }
    d[3] = 0.0f;
}

/* Pentagon: n1 x n2 + n2 x n3 + n3 x n4 + n4 x n1, quartered; n3 and n4
   come from the squares as in makeNormal4. */
static void makeNormal5(void *dst, void *verts, int i0, int i1, int i2, int i3, int i4)
{
    float e1[3], e2[3], e3[3], e4[3], s3[3], s4[3];
    float n1[3], n2[3], n3[3], n4[3], c1[3], c2[3], c3[3], c4[3];
    float *d = dst;
    float k = 0.25f;
    int i;

    edge(e1, verts, i0, i1);
    edge(e2, verts, i0, i2);
    edge(e3, verts, i0, i3);
    edge(e4, verts, i0, i4);
    scale3(n1, e1, ps2_rsqrt(1.0f, sq_len_xzy(e1)));
    scale3(n2, e2, ps2_rsqrt(1.0f, sq_len_xzy(e2)));
    squares(s3, e3);
    scale3(n3, s3, ps2_rsqrt(1.0f, s3[0]));
    squares(s4, e4);
    scale3(n4, s4, ps2_rsqrt(1.0f, s4[0]));
    cross3(c1, n1, n2);
    cross3(c2, n2, n3);
    cross3(c3, n3, n4);
    cross3(c4, n4, n1);
    for (i = 0; i < 3; i++) {
        d[i] = (c1[i] + c2[i] + c3[i] + c4[i]) * k;
    }
    d[3] = 0.0f;
}

void _RemakeNormal(void *dst, void *verts, int *idx)
{
    int i;

    for (i = 0; idx[i] != -1; i++) {}

    i--;

    switch (i) {
    case 2:
        makeNormal3(dst, verts, idx[0], idx[1], idx[2]);
        break;
    case 3:
        makeNormal4(dst, verts, idx[0], idx[1], idx[2], idx[3]);
        break;
    case 4:
        makeNormal5(dst, verts, idx[0], idx[1], idx[2], idx[3], idx[4]);
        break;
    }
}
