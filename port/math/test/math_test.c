/*
 * port/math/test/math_test.c
 *
 * Unit tests for port/math: the PS2 float helpers checked bit for bit, the R
 * register against the vectors in docs/research/float-semantics.md, and the
 * matrix, vector and quaternion routines against double-precision
 * references (tight tolerance) or exact results where the answer is unique
 * (identity, quarter-turn rotations, pure copies).
 *
 * Runs in the simulation's FP mode (round toward zero, FTZ/DAZ), as the game
 * does. Exit status 0 when every check passes.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ico_math.h"
#include "../../compat/libvu0.h"
#include "../../../ico2/seki/include/Matrix.h"
#include "../../../ico2/sugipon/include/matrixDrive.h"
#include "../../../ico2/sugipon/include/quaternion.h"
#include "fpenv.h"

/* sugipon/src/tableSin.c is game code; the tests use exact values on the
   quarter turns and libm elsewhere (0x10000 = one turn). */
float GetTableCos(short angle);
float GetTableSin(short angle);
/* seki/src/Matrix.c defines it; no header declares it. */
void _RemakeNormal(void *dst, void *verts, int *idx);

float GetTableSin(short angle)
{
    switch ((unsigned short)angle) {
    case 0x0000:
        return 0.0f;
    case 0x4000:
        return 1.0f;
    case 0x8000:
        return 0.0f;
    case 0xC000:
        return -1.0f;
    }
    return (float)sin((unsigned short)angle * (2.0 * 3.14159265358979323846 / 65536.0));
}

float GetTableCos(short angle)
{
    return GetTableSin((short)(angle + 0x4000));
}

static int failures;
static int checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void check_bits(float got, uint32_t want, const char *what)
{
    char buf[160];
    snprintf(buf, sizeof buf, "%s: got 0x%08X want 0x%08X", what, (unsigned)ps2_float_bits(got),
             (unsigned)want);
    check(ps2_float_bits(got) == want, buf);
}

static void check_near(double got, double want, double tol, const char *what)
{
    char buf[200];
    double err = fabs(got - want);
    snprintf(buf, sizeof buf, "%s: got %.9g want %.9g (err %.3g)", what, got, want, err);
    check(err <= tol * (1.0 + fabs(want)), buf);
}

/* --- small deterministic generator for test inputs ---------------------- */
static uint32_t seed = 12345u;

static float frand(float lo, float hi)
{
    seed = seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)((seed >> 8) * (1.0 / 16777216.0));
}

static void rand_matrix(float (*m)[4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m[i][j] = frand(-4.0f, 4.0f);
        }
    }
}

/* Double references, in the game's convention: row i of a*b is a applied
   to row i of b; apply(m, v) = sum over k of m[k] * v[k]. */
static void ref_apply(double *out, const float (*m)[4], const float *v)
{
    int i, k;
    for (i = 0; i < 4; i++) {
        out[i] = 0.0;
        for (k = 0; k < 4; k++) {
            out[i] += (double)m[k][i] * v[k];
        }
    }
}

static void check_vec(const float *got, const double *want, int n, double tol, const char *what)
{
    int i;
    char buf[120];
    for (i = 0; i < n; i++) {
        snprintf(buf, sizeof buf, "%s[%d]", what, i);
        check_near(got[i], want[i], tol, buf);
    }
}

static void check_mat(const float (*got)[4], const float (*a)[4], const float (*b)[4], double tol,
                      const char *what)
{
    int i;
    double want[4];
    for (i = 0; i < 4; i++) {
        ref_apply(want, a, b[i]);
        check_vec(got[i], want, 4, tol, what);
    }
}

#define TOL 2e-6

static void test_helpers(void)
{
    /* volatile: keep the compiler from folding these at build time (it
       folds with round-to-nearest; docs/port/DIVERGENCES.md F4) */
    volatile float denorm = ps2_bits_float(0x00000123u);
    volatile float one = 1.0f;
    volatile float three = 3.0f;
    volatile float big = FLT_MAX;
    volatile float tiny = 1e-38f;

    check_bits(ps2_div(1.0f, 0.0f), 0x7F7FFFFFu, "div 1/+0 = +Fmax");
    check_bits(ps2_div(1.0f, -0.0f), 0xFF7FFFFFu, "div 1/-0 = -Fmax");
    check_bits(ps2_div(-1.0f, 0.0f), 0xFF7FFFFFu, "div -1/+0 = -Fmax");
    check_bits(ps2_div(0.0f, 0.0f), 0x7F7FFFFFu, "div 0/0 = +Fmax");
    check_bits(ps2_div(-0.0f, 0.0f), 0xFF7FFFFFu, "div -0/+0 = -Fmax");
    check_bits(ps2_div(2.0f, denorm), 0x7F7FFFFFu, "div by a denormal = Fmax");
    check_bits(ps2_div(one, three), 0x3EAAAAAAu, "div 1/3 rounds toward zero");
    check_bits(ps2_sqrt(-4.0f), 0x40000000u, "sqrt(-4) = 2");
    check_bits(ps2_sqrt(denorm), 0x00000000u, "sqrt(denormal) = +0");
    check_bits(ps2_sqrt(-0.0f), 0x00000000u, "sqrt(-0) = +0");
    check_bits(ps2_sqrt(2.0f), 0x3FB504F3u, "sqrt(2) rounds toward zero");
    check_bits(ps2_rsqrt(1.0f, 4.0f), 0x3F000000u, "rsqrt 1/sqrt(4)");
    check_bits(ps2_rsqrt(1.0f, -4.0f), 0x3F000000u, "rsqrt of a negative uses |b|");
    check_bits(ps2_rsqrt(1.0f, 0.0f), 0x7F7FFFFFu, "rsqrt 1/sqrt(0) = Fmax");
    check_bits(ps2_rsqrt(-1.0f, 0.0f), 0xFF7FFFFFu, "rsqrt -1/sqrt(0) = -Fmax");
    check_bits(ps2_rsqrt(0.0f, 0.0f), 0x00000000u, "rsqrt 0/sqrt(0) = 0");
    check(ps2_ftoi(2147483648.0f) == INT32_MAX, "ftoi 2^31 saturates");
    check(ps2_ftoi(3e9f) == INT32_MAX, "ftoi 3e9 saturates");
    check(ps2_ftoi(FLT_MAX) == INT32_MAX, "ftoi Fmax saturates");
    check(ps2_ftoi(-3e9f) == INT32_MIN, "ftoi -3e9 saturates");
    check(ps2_ftoi(-2147483648.0f) == INT32_MIN, "ftoi -2^31");
    check(ps2_ftoi(2147483520.0f) == 2147483520, "ftoi largest float below 2^31");
    check(ps2_ftoi(1.9f) == 1 && ps2_ftoi(-1.9f) == -1, "ftoi truncates");
    check(ps2_ftoi4(1.5f) == 24 && ps2_ftoi4(-0.03125f) == 0, "ftoi4");
    check(ps2_ftoi4(2e8f) == INT32_MAX, "ftoi4 saturates");
    check(ps2_ftoi(ps2_bits_float(0xFFFFFFFFu)) == INT32_MIN,
          "ftoi -NaN pattern saturates by sign");
    check(ps2_ftoi(ps2_bits_float(0x7F800000u)) == INT32_MAX, "ftoi +Inf pattern saturates");
    check(ps2_max(-2.0f, -3.0f) == -2.0f && ps2_min(-2.0f, -3.0f) == -3.0f, "max/min negatives");
    check_bits(ps2_operand(ps2_bits_float(0xFFFFFFFFu)), 0xFF7FFFFFu, "operand 0xFFFFFFFF = -Fmax");
    check_bits(ps2_operand(ps2_bits_float(0x7F800000u)), 0x7F7FFFFFu, "operand +Inf = +Fmax");
    check_bits(ps2_operand(ps2_bits_float(0x7FC00001u)), 0x7F7FFFFFu, "operand +NaN = +Fmax");
    check_bits(ps2_operand(-2.5f), 0xC0200000u, "operand passes a normal through");
    /* The sim mode itself: overflow lands on Fmax, tiny results flush. */
    check_bits(big * 2.0f, 0x7F7FFFFFu, "overflow is Fmax under round toward zero");
    check_bits(tiny * 1e-5f, 0x00000000u, "a denormal result flushes to zero");
    check_bits(denorm + 0.0f, 0x00000000u, "a denormal input reads as zero");
}

/* The multiply-add is not fused: (1 + 2^-12)^2 rounds to 1 + 2^-11 first. */
static void test_madd_not_fused(void)
{
    float a = 1.0f + 1.0f / 4096.0f;
    float m[4][4] = {{a, 0, 0, 0}, {-1.0f, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    float v[4] = {a, 1.0f, 0.0f, 0.0f};
    float out[4];

    _ApplyMatrix(out, m, v);
    check_bits(out[0], 0x3A000000u, "apply: a*a - 1 = 2^-11 (product rounded first)");
}

static void test_random(void)
{
    static const uint32_t next[6] = {0x3FA20AF3u, 0x3FC415E7u, 0x3F882BCFu,
                                     0x3F90579Eu, 0x3FA0AF3Du, 0x3FC15E7Bu};
    static const float values[6] = {0.26595914f, 0.53191841f, 0.06383693f,
                                    0.12767386f, 0.25534785f, 0.51069582f};
    float seed1 = 1.2345678f;
    int i;

    check_bits(seed1, 0x3F9E0651u, "R seed bits");
    check_bits(seed1 + 1.0f, 0x400F0328u, "R seed + 1");
    check((0x3F800000u | (ps2_float_bits(seed1 + 1.0f) & 0x7FFFFFu)) == 0x3F8F0328u,
          "R after vrinit = 0x3F8F0328");
    _InitRandom(seed1);
    check(ico_vu0_random_get() == 0x3F910579u, "R after vrxor = 0x3F910579");
    for (i = 0; i < 6; i++) {
        float r = _GetRandom();
        char buf[80];
        snprintf(buf, sizeof buf, "R step %d", i + 1);
        check(ico_vu0_random_get() == next[i], buf);
        check(r == values[i], "_GetRandom value");
        check(r == (float)(next[i] & 0x7FFFFFu) / 8388608.0f, "_GetRandom = (R & 0x7FFFFF) / 2^23");
    }
    ico_vu0_random_set(0x12345678u);
    check(ico_vu0_random_get() == (0x3F800000u | 0x00345678u), "R write keeps 23 bits");
}

static void test_matrices(void)
{
    float a[4][4], b[4][4], c[4][4], d[4][4];
    float v[4], out[4];
    double want[4];
    static const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    int n;

    for (n = 0; n < 200; n++) {
        rand_matrix(a);
        rand_matrix(b);
        _MulMatrix(c, a, b);
        check_mat((const float (*)[4])c, (const float (*)[4])a, (const float (*)[4])b, TOL,
                  "_MulMatrix");
        sceVu0MulMatrix(d, a, b);
        check(memcmp(c, d, sizeof c) == 0, "sceVu0MulMatrix == _MulMatrix");
        memcpy(d, a, sizeof d);
        _MulMatrix(d, d, b);
        check(memcmp(c, d, sizeof c) == 0, "_MulMatrix with dst == a");
        memcpy(d, b, sizeof d);
        sceVu0MulMatrix(d, a, d);
        check(memcmp(c, d, sizeof c) == 0, "sceVu0MulMatrix with dst == m1");

        v[0] = frand(-8, 8);
        v[1] = frand(-8, 8);
        v[2] = frand(-8, 8);
        v[3] = frand(-2, 2);
        _ApplyMatrix(out, a, v);
        ref_apply(want, (const float (*)[4])a, v);
        check_vec(out, want, 4, TOL, "_ApplyMatrix");
        sceVu0ApplyMatrix(d[0], a, v);
        check(memcmp(out, d[0], sizeof out) == 0, "sceVu0ApplyMatrix == _ApplyMatrix");
        {
            /* exact: the same sum written out in the asm's order */
            float e = a[0][1] * v[0] + a[1][1] * v[1] + a[2][1] * v[2] + a[3][1] * v[3];
            check(out[1] == e, "_ApplyMatrix order of operations");
        }

        /* the current matrix */
        _SetCurrentMatrix(a);
        _MulCurrentMatrixR(b);
        _GetCurrentMatrix(d);
        check(memcmp(c, d, sizeof c) == 0, "_MulCurrentMatrixR == _MulMatrix(cur, m)");
        _SetCurrentMatrix(b);
        _MulCurrentMatrixL(a);
        _GetCurrentMatrix(d);
        check(memcmp(c, d, sizeof c) == 0, "_MulCurrentMatrixL == _MulMatrix(m, cur)");
        _ApplyCurrentMatrix(out, v);
        _ApplyMatrix(d[0], c, v);
        check(memcmp(out, d[0], sizeof out) == 0, "_ApplyCurrentMatrix");
        _TransCurrentMatrix(v);
        _GetCurrentMatrixTrans(out);
        check(memcmp(out, d[0], sizeof out) == 0, "_TransCurrentMatrix");
    }

    /* identity: exact */
    rand_matrix(a);
    _UnitMatrix(b);
    check(memcmp(b, id, sizeof b) == 0, "_UnitMatrix");
    sceVu0UnitMatrix(c);
    check(memcmp(c, id, sizeof c) == 0, "sceVu0UnitMatrix");
    _MulMatrix(c, a, b);
    check(memcmp(c, a, sizeof c) == 0, "a x I == a exactly");
    _MulMatrix(c, b, a);
    check(memcmp(c, a, sizeof c) == 0, "I x a == a exactly");
    _InitCurrentMatrix();
    _GetCurrentMatrix(c);
    check(memcmp(c, id, sizeof c) == 0, "_InitCurrentMatrix is the identity");

    /* transpose */
    _TransposeMatrix(c, a);
    sceVu0TransposeMatrix(d, a);
    check(memcmp(c, d, sizeof c) == 0 && c[1][2] == a[2][1] && c[3][0] == a[0][3],
          "_TransposeMatrix");
    _SetCurrentMatrix(a);
    _TransposeCurrentMatrix();
    _GetCurrentMatrix(d);
    check(memcmp(c, d, sizeof c) == 0, "_TransposeCurrentMatrix");
    _SetCurrentMatrix(a);
    _TransposeRotationCurrentMatrix();
    _GetCurrentMatrix(d);
    check(d[0][1] == a[1][0] && d[2][0] == a[0][2] && d[0][3] == a[0][3] &&
              memcmp(d[3], a[3], 16) == 0,
          "_TransposeRotationCurrentMatrix keeps w column and row 3");

    /* push / pop */
    _InitCurrentMatrix();
    _SetCurrentMatrix(a);
    _PushCurrentMatrix();
    _SetCurrentMatrix(b);
    _PushCurrentMatrix();
    _SetCurrentMatrix(c);
    _PopCurrentMatrix();
    _GetCurrentMatrix(d);
    check(memcmp(d, b, sizeof d) == 0, "pop restores the last push");
    _PopCurrentMatrix();
    _GetCurrentMatrix(d);
    check(memcmp(d, a, sizeof d) == 0, "pop restores the first push");
    _SetCurrentMatrix(a);
    ico_vu0_registers_push();
    _SetCurrentMatrix(b);
    ico_vu0_registers_pop();
    _GetCurrentMatrix(d);
    check(memcmp(d, a, sizeof d) == 0,
          "ico_vu0_registers_push/pop (_PushVu0Registers) keep the current matrix");
}

/* Rotations: quarter turns exact, general angles against double. */
static void test_rotations(void)
{
    float a[4][4], c[4][4];
    double want[4];
    int i;

    rand_matrix(a);
    /* RotX by a quarter turn: row1' = row2, row2' = -row1 (c = 0, s = 1). */
    _SetCurrentMatrix(a);
    _RotCurrentMatrixX(0x4000);
    _GetCurrentMatrix(c);
    check(memcmp(c[0], a[0], 16) == 0 && memcmp(c[1], a[2], 16) == 0 && memcmp(c[3], a[3], 16) == 0,
          "_RotCurrentMatrixX quarter turn rows 0, 1, 3");
    for (i = 0; i < 4; i++) {
        check(c[2][i] == -a[1][i] || (c[2][i] == 0.0f && a[1][i] == 0.0f),
              "_RotCurrentMatrixX quarter turn row 2");
    }
    _SetCurrentMatrix(a);
    _RotCurrentMatrixY(0x4000);
    _GetCurrentMatrix(c);
    for (i = 0; i < 4; i++) {
        check(c[0][i] == -a[2][i] || (c[0][i] == 0.0f && a[2][i] == 0.0f),
              "_RotCurrentMatrixY quarter turn row 0 = -row 2");
    }
    check(memcmp(c[2], a[0], 16) == 0, "_RotCurrentMatrixY quarter turn row 2 = row 0");
    _SetCurrentMatrix(a);
    _RotCurrentMatrixZ(0x4000);
    _GetCurrentMatrix(c);
    check(memcmp(c[0], a[1], 16) == 0, "_RotCurrentMatrixZ quarter turn row 0 = row 1");

    /* General angle against double: R x a with the ref's rotation. */
    for (i = 0; i < 64; i++) {
        short ang = (short)(i * 1031);
        float cs = GetTableCos(ang), sn = GetTableSin(ang);
        float r[4][4] = {{1, 0, 0, 0}, {0, cs, sn, 0}, {0, -sn, cs, 0}, {0, 0, 0, 1}};
        _SetCurrentMatrix(a);
        _RotCurrentMatrixX(ang);
        _GetCurrentMatrix(c);
        check_mat((const float (*)[4])c, (const float (*)[4])a, (const float (*)[4])r, TOL,
                  "_RotCurrentMatrixX");
    }

    /* sceVu0RotMatrix*: libvu0's own sine polynomial. */
    for (i = -40; i <= 40; i++) {
        double t = i * (3.14159265358979323846 / 40.0);
        float m[4][4], id[4][4];
        sceVu0UnitMatrix(id);
        sceVu0RotMatrixZ(m, id, (float)t);
        check_near(m[0][0], cos(t), 2e-5, "sceVu0RotMatrixZ cos");
        check_near(m[0][1], sin(t), 2e-5, "sceVu0RotMatrixZ sin");
        check_near(m[1][0], -sin(t), 2e-5, "sceVu0RotMatrixZ -sin");
        sceVu0RotMatrixX(m, id, (float)t);
        check_near(m[1][1], cos(t), 2e-5, "sceVu0RotMatrixX cos");
        check_near(m[1][2], sin(t), 2e-5, "sceVu0RotMatrixX sin");
        sceVu0RotMatrixY(m, id, (float)t);
        check_near(m[0][0], cos(t), 2e-5, "sceVu0RotMatrixY cos");
        check_near(m[0][2], -sin(t), 2e-5, "sceVu0RotMatrixY -sin");
        ref_apply(want, (const float (*)[4])m, id[3]);
        check_vec(m[3], want, 4, 0, "sceVu0RotMatrixY row 3");
    }
}

static void test_vectors(void)
{
    int n;
    for (n = 0; n < 200; n++) {
        float a[4] = {frand(-9, 9), frand(-9, 9), frand(-9, 9), frand(-2, 2)};
        float b[4] = {frand(-9, 9), frand(-9, 9), frand(-9, 9), frand(-2, 2)};
        float o[4], p[4];
        double w[4];
        float t = frand(0, 1);
        double len = sqrt((double)a[0] * a[0] + (double)a[1] * a[1] + (double)a[2] * a[2]);

        check_near(_InnerProduct(a, b),
                   (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2], 4 * TOL,
                   "_InnerProduct");
        check(_InnerProduct(a, b) == sceVu0InnerProduct(a, b), "sceVu0InnerProduct");
        _OuterProduct(o, a, b);
        w[0] = (double)a[1] * b[2] - (double)a[2] * b[1];
        w[1] = (double)a[2] * b[0] - (double)a[0] * b[2];
        w[2] = (double)a[0] * b[1] - (double)a[1] * b[0];
        w[3] = 0;
        check_vec(o, w, 4, 4 * TOL, "_OuterProduct");
        sceVu0OuterProduct(p, a, b);
        check(memcmp(o, p, 16) == 0, "sceVu0OuterProduct");
        _NormalizeVector(o, a);
        w[0] = a[0] / len, w[1] = a[1] / len, w[2] = a[2] / len, w[3] = a[3];
        check_vec(o, w, 4, TOL, "_NormalizeVector (w copied)");
        sceVu0Normalize(o, a);
        w[3] = 0;
        check_vec(o, w, 4, TOL, "sceVu0Normalize (w = 0)");
        check_near(_GetNorm(a), len, TOL, "_GetNorm");
        check_near(VectorLength(a), len, TOL, "VectorLength");
        check_near(_GetLength(a, b),
                   sqrt(pow((double)a[0] - b[0], 2) + pow((double)a[1] - b[1], 2) +
                        pow((double)a[2] - b[2], 2)),
                   TOL, "_GetLength");
        _InterVector(o, a, b, t);
        for (int i = 0; i < 4; i++) {
            w[i] = (double)a[i] * t + (double)b[i] * (1.0 - t);
        }
        check_vec(o, w, 4, 8 * TOL, "_InterVector (t weights a)");
        sceVu0InterVector(p, a, b, t);
        check(memcmp(o, p, 16) == 0, "sceVu0InterVector");
        _InterVectorXYZ(o, a, b, t);
        check(o[3] == a[3], "_InterVectorXYZ keeps a.w");
        _AddVectorXYZ(o, a, b);
        check(o[0] == a[0] + b[0] && o[3] == a[3], "_AddVectorXYZ");
        _SubVector(o, a, b);
        check(o[3] == a[3] - b[3], "_SubVector");
        _ScaleVectorXYZ(o, a, t);
        check(o[1] == a[1] * t && o[3] == a[3], "_ScaleVectorXYZ");
        check(ico_plane_distance(a, b) == a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + b[3],
              "plane_distance");
    }
    {
        float v[4] = {1.5f, -2.25f, 1e10f, -1e10f};
        int32_t i4[4];
        _FTOI4Vector(i4, v);
        check(i4[0] == 24 && i4[1] == -36 && i4[2] == INT32_MAX && i4[3] == INT32_MIN,
              "_FTOI4Vector saturates");
        _FTOI0Vector(i4, v);
        check(i4[0] == 1 && i4[1] == -2, "_FTOI0Vector");
    }
}

static void test_inverse_and_projection(void)
{
    float m[4][4], inv[4][4], prod[4][4];
    static const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    int i, j;

    /* a rigid transform: rotation from a unit quaternion plus translation */
    float q[4] = {0.18257419f, 0.36514837f, 0.54772256f, 0.73029674f};
    ico_quaternion_rotation_rows(m, q);
    m[3][0] = 3.0f, m[3][1] = -2.0f, m[3][2] = 5.0f, m[3][3] = 1.0f;
    _InversMatrix(inv, m);
    _MulMatrix(prod, inv, m);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            check_near(prod[i][j], id[i][j], 1e-5, "_InversMatrix x m = I");
        }
    }
    sceVu0InversMatrix(prod, m);
    check(memcmp(prod, inv, sizeof inv) == 0, "sceVu0InversMatrix == _InversMatrix");
    _SetCurrentMatrix(m);
    _InverseCurrentMatrix();
    _GetCurrentMatrix(prod);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            check(prod[i][j] == inv[i][j], "_InverseCurrentMatrix rotation");
        }
        check_near(prod[3][i], inv[3][i], 1e-6, "_InverseCurrentMatrix translation");
    }

    /* quaternion to matrix against the textbook formula in double */
    {
        double x = q[0], y = q[1], z = q[2], w = q[3];
        double r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                          {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                          {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                check_near(m[i][j], r[i][j], 1e-6, "quaternion rotation rows");
            }
            check(m[i][3] == 0.0f, "quaternion rotation w column = 0");
        }
    }
    /* quaternion product against double */
    {
        float a[4] = {0.1f, -0.7f, 0.3f, 0.6f};
        float b[4] = {-0.5f, 0.2f, 0.4f, 0.74f};
        float o[4];
        double want[4];
        MultiQuaternion(o, a, b);
        want[0] =
            (double)b[0] * a[3] + (double)a[0] * b[3] + ((double)b[1] * a[2] - (double)a[1] * b[2]);
        want[1] =
            (double)b[1] * a[3] + (double)a[1] * b[3] + ((double)b[2] * a[0] - (double)a[2] * b[0]);
        want[2] =
            (double)b[2] * a[3] + (double)a[2] * b[3] + ((double)b[0] * a[1] - (double)a[0] * b[1]);
        want[3] =
            (double)a[3] * b[3] - ((double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2]);
        check_vec(o, want, 4, 4 * TOL, "MultiQuaternion");
        check_near(GetQuaternionCosRadian(a, b),
                   (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2] +
                       (double)a[3] * b[3],
                   4 * TOL, "GetQuaternionCosRadian");
    }

    /* perspective: 1/w then multiply, and w = 0 gives Fmax-scaled values */
    {
        float p[4] = {2.0f, 4.0f, 6.0f, 1.0f};
        float o[4];
        float pm[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 3}};
        int32_t r[4];
        volatile float w3 = 3.0f;
        _SetCurrentMatrix(pm);
        _RotTransPersCurrentMatrix(o, p);
        check(o[0] == 2.0f * ps2_div(1.0f, w3) && o[3] == 3.0f, "_RotTransPersCurrentMatrix");
        sceVu0RotTransPers(r, pm, p, 0);
        check(r[0] == ps2_ftoi4(o[0]) && r[3] == 48, "sceVu0RotTransPers ftoi4");
        sceVu0RotTransPers(r, pm, p, 1);
        check(r[2] == 1 && r[3] == 3, "sceVu0RotTransPers mode 1: integer z and w");
        pm[3][3] = 0.0f;
        _SetCurrentMatrix(pm);
        _RotTransPersCurrentMatrix(o, p);
        check_bits(o[0], 0x7F7FFFFFu, "projection with w = 0 gives Fmax (not Inf)");
    }
}

/* _RemakeNormal: triangle normal against double; the quad and pentagon
   forms keep the PS2's use of the squared edge (MATH.md), checked against
   the same formula written out. */
static void test_normals(void)
{
    float verts[5][4] = {{0, 0, 0, 1}, {2, 0, 0, 1}, {0, 4, 0, 1}, {-1, -1, 0, 1}, {1, 1, 1, 1}};
    int tri[4] = {0, 1, 2, -1};
    int quad[5] = {0, 1, 2, 3, -1};
    float n[4];

    _RemakeNormal(n, verts, tri);
    check(n[0] == 0.0f && n[1] == 0.0f && n[2] == 1.0f && n[3] == 0.0f, "_RemakeNormal triangle");
    _RemakeNormal(n, verts, quad);
    {
        /* e3 = (-1, -1, 0): squares (2, 1, 0), 1/sqrt(2) */
        float k = ps2_rsqrt(1.0f, 2.0f);
        float n3[3] = {2.0f * k, 1.0f * k, 0.0f * k};
        /* n1 = (1,0,0), n2 = (0,1,0); c1 = (0,0,1); c2 = n2 x n3 */
        float c2z = 0.0f * n3[1] - n3[0] * 1.0f;
        check(n[2] == (1.0f + c2z) * 0.5f, "_RemakeNormal quad keeps the squared third edge");
    }
}

int main(void)
{
    ico_fpenv_sim_enter();
    test_helpers();
    test_madd_not_fused();
    test_random();
    test_matrices();
    test_rotations();
    test_vectors();
    test_inverse_and_projection();
    test_normals();
    ico_fpenv_host_enter();
    printf("math_test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
