/*
 * port/math/matrix_stack.c
 *
 * seki/src/Matrix.c's "current matrix" routines. The PS2 kept the current
 * matrix in VU0 registers vf4-vf7 and pushed it to VU0 data memory through
 * vi15; here it is ico_current_matrix and a 64-entry stack.
 *
 * Every routine that combines the current matrix with another matrix does
 * what the assembly did: it builds the other matrix in full (zeros and ones
 * included) and runs the four-term multiply-add on it, so the rounding and
 * the order of operations are the PS2's.
 */
#include <string.h>

#include "ico_math.h"
#include "../../ico2/seki/include/Matrix.h"

float GetTableCos(short angle);
float GetTableSin(short angle);

float ico_current_matrix[4][4];

/* VU0 data memory is 4 KB, 256 quadwords, addressed modulo its size; a
   pushed matrix is four quadwords, so the push stack wraps after 64. */
#define MATRIX_STACK_DEPTH 64
static float matrixStack[MATRIX_STACK_DEPTH][4][4];
static unsigned int matrixStackTop;

void ico_apply_matrix(float *out, const float (*m)[4], const float *v)
{
    float x = v[0];
    float y = v[1];
    float z = v[2];
    /* VU0 reads an exponent-255 word as a number (+-Fmax; no Inf or NaN).
       Callers often write only x, y and z, leaving w a stale word (the
       chain sway, the blended motion root): times a zero translation row
       the PS2 gets 0, the host would get NaN. Read w as VU0 does. */
    float w = ps2_operand(v[3]);
    int i;

    for (i = 0; i < 4; i++) {
        out[i] = m[0][i] * x + m[1][i] * y + m[2][i] * z + m[3][i] * w;
    }
}

void ico_apply_matrix_ps2(float *out, const float (*m)[4], const float *v)
{
    float x = v[0];
    float y = v[1];
    float z = v[2];
    float w = ps2_operand(v[3]);
    int i;

    for (i = 0; i < 4; i++) {
        float acc = ps2_mul(m[0][i], x);            /* vmulax */
        acc = ps2_add(acc, ps2_mul(m[1][i], y));    /* vmadday */
        acc = ps2_add(acc, ps2_mul(m[2][i], z));    /* vmaddaz */
        out[i] = ps2_add(acc, ps2_mul(m[3][i], w)); /* vmaddw */
    }
}

void ico_apply_matrix_w1(float *out, const float (*m)[4], const float *v)
{
    static const float one = 1.0f;
    float x = v[0];
    float y = v[1];
    float z = v[2];
    int i;

    for (i = 0; i < 4; i++) {
        out[i] = m[0][i] * x + m[1][i] * y + m[2][i] * z + m[3][i] * one;
    }
}

/* cur = cur x r, as the rotate, scale and multiply routines compute it:
   row i of the result is cur applied to row i of r. */
static void current_mul_right(const float (*r)[4])
{
    float out[4][4];
    int i;

    for (i = 0; i < 4; i++) {
        ico_apply_matrix(out[i], (const float (*)[4])ico_current_matrix, r[i]);
    }
    memcpy(ico_current_matrix, out, sizeof out);
}

static void set_identity(float (*m)[4])
{
    static const float identity[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    memcpy(m, identity, sizeof identity);
}

void _InitCurrentMatrix(void)
{
    set_identity(ico_current_matrix);
    matrixStackTop = 0;
}

/* No caller; the same as _InitCurrentMatrix. */
void _UnitCurrentMatrix(void)
{
    set_identity(ico_current_matrix);
    matrixStackTop = 0;
}

void _PushCurrentMatrix(void)
{
    memcpy(matrixStack[matrixStackTop % MATRIX_STACK_DEPTH], ico_current_matrix,
           sizeof ico_current_matrix);
    matrixStackTop++;
}

void _PopCurrentMatrix(void)
{
    matrixStackTop--;
    memcpy(ico_current_matrix, matrixStack[matrixStackTop % MATRIX_STACK_DEPTH],
           sizeof ico_current_matrix);
}

/* Translation row = cur applied to v (all four fields, v[3] included). */
void _TransCurrentMatrix(void *v)
{
    ico_apply_matrix(ico_current_matrix[3], (const float (*)[4])ico_current_matrix,
                     (const float *)v);
}

void _SetTransCurrentMatrix(void *v)
{
    memcpy(ico_current_matrix[3], v, 4 * sizeof(float));
}

void _ClearTransCurrentMatrix(void)
{
    ico_current_matrix[3][0] = 0.0f;
    ico_current_matrix[3][1] = 0.0f;
    ico_current_matrix[3][2] = 0.0f;
    ico_current_matrix[3][3] = 1.0f;
}

void _RotCurrentMatrixX(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    float r[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, c, s, 0.0f},
        {0.0f, 0.0f - s, c, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    current_mul_right((const float (*)[4])r);
}

void _RotCurrentMatrixY(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    float r[4][4] = {
        {c, 0.0f, 0.0f - s, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {s, 0.0f, c, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    current_mul_right((const float (*)[4])r);
}

void _RotCurrentMatrixZ(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    float r[4][4] = {
        {c, s, 0.0f, 0.0f},
        {0.0f - s, c, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    current_mul_right((const float (*)[4])r);
}

void _ScaleCurrentMatrix(float sx, float sy, float sz)
{
    float r[4][4] = {
        {sx, 0.0f, 0.0f, 0.0f},
        {0.0f, sy, 0.0f, 0.0f},
        {0.0f, 0.0f, sz, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    current_mul_right((const float (*)[4])r);
}

void _GetCurrentMatrix(void *dst)
{
    memcpy(dst, ico_current_matrix, sizeof ico_current_matrix);
}

void _GetCurrentMatrixTrans(void *dst)
{
    memcpy(dst, ico_current_matrix[3], 4 * sizeof(float));
}

void _SetCurrentMatrix(void *m)
{
    memcpy(ico_current_matrix, m, sizeof ico_current_matrix);
}

/* cur = cur x m (row i: cur applied to m's row i). */
void _MulCurrentMatrixR(void *m)
{
    float r[4][4];

    memcpy(r, m, sizeof r);
    current_mul_right((const float (*)[4])r);
}

/* cur = m x cur (row i: m applied to cur's row i). */
void _MulCurrentMatrixL(void *m)
{
    float l[4][4];
    int i;

    memcpy(l, m, sizeof l);
    for (i = 0; i < 4; i++) {
        ico_apply_matrix(ico_current_matrix[i], (const float (*)[4])l, ico_current_matrix[i]);
    }
}

void _ApplyCurrentMatrix(void *dst, void *src)
{
    ico_apply_matrix((float *)dst, (const float (*)[4])ico_current_matrix, (const float *)src);
}

/* Apply, then x, y and z times 1/w (vdiv then vmulq: two roundings, not
   x/w). w is stored as computed. */
void _RotTransPersCurrentMatrix(void *dst, void *src)
{
    float v[4];
    float q;

    ico_apply_matrix(v, (const float (*)[4])ico_current_matrix, (const float *)src);
    q = ps2_div(1.0f, v[3]);
    v[0] = v[0] * q;
    v[1] = v[1] * q;
    v[2] = v[2] * q;
    memcpy(dst, v, sizeof v);
}

/* The last two projected points of _RotTransCurrentMatrix (vf11 and vf12
   on the PS2), as the 28.4 integers it stores. */
static int32_t rotTransPrev[2][2];

/* No caller in the game. Projects like _RotTransPersCurrentMatrix, stores x,
   y and z as vftoi4 integers (w stays the float), and shifts the point into
   the two-point history the PS2 kept in vf11/vf12. The assembly also forms
   the cross product of the last two edges in $f0, but its declared type is
   void and it reads the 28.4 integers as floats (denormals, so zero on the
   PS2), so nothing observable comes of it. */
void _RotTransCurrentMatrix(void *dst, void *src)
{
    float v[4];
    int32_t out[4];
    float q;

    ico_apply_matrix(v, (const float (*)[4])ico_current_matrix, (const float *)src);
    q = ps2_div(1.0f, v[3]);
    out[0] = ps2_ftoi4(v[0] * q);
    out[1] = ps2_ftoi4(v[1] * q);
    out[2] = ps2_ftoi4(v[2] * q);
    memcpy(&out[3], &v[3], sizeof(float));
    memcpy(dst, out, sizeof out);
    rotTransPrev[1][0] = rotTransPrev[0][0];
    rotTransPrev[1][1] = rotTransPrev[0][1];
    rotTransPrev[0][0] = out[0];
    rotTransPrev[0][1] = out[1];
}

void _TransposeCurrentMatrix(void)
{
    float t[4][4];
    int i;
    int j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            t[i][j] = ico_current_matrix[j][i];
        }
    }
    memcpy(ico_current_matrix, t, sizeof t);
}

/* The 3x3 rotation part only; the w column and the translation row stay. */
void _TransposeRotationCurrentMatrix(void)
{
    float t[3][3];
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            t[i][j] = ico_current_matrix[j][i];
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            ico_current_matrix[i][j] = t[i][j];
        }
    }
}

/* Inverse of a rigid transform: transpose the 3x3 rotation (the w column of
   rows 0-2 stays), then the translation row becomes the transposed rotation
   applied to (-tx, -ty, -tz, 1). Its w is rows 0-2's w column applied to
   -t, plus 1. */
void _InverseCurrentMatrix(void)
{
    float t[4];

    t[0] = ico_current_matrix[3][0] * -1.0f;
    t[1] = ico_current_matrix[3][1] * -1.0f;
    t[2] = ico_current_matrix[3][2] * -1.0f;
    t[3] = 1.0f;
    _TransposeRotationCurrentMatrix();
    {
        static const float unitW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float (*m)[4] = ico_current_matrix;
        int i;

        for (i = 0; i < 4; i++) {
            m[3][i] = m[0][i] * t[0] + m[1][i] * t[1] + m[2][i] * t[2] + unitW[i] * t[3];
        }
    }
}

/* _PushVu0Registers / _PopVu0Registers (seki/src/Matrix.c). On the PS2
   they save vf1-vf31 to VU0 memory and load them back, around code that
   would clobber them: the scheduler and sound threads, which preempt the
   game, and iosPadGetStick, which the game calls directly and which runs
   FSqrt and sceVu0Normalize (both write vf4, a current-matrix row, on the
   PS2). On the host the only thing left of vf1-vf31 is the current matrix
   (every other routine keeps its values in C locals), so that is what is
   saved. Q, R, I and the integer registers were not saved on the PS2
   either. The PS2 pairs nest up to 5 deep (Matrix.c asserts at 6). */
#define VU0_SAVE_DEPTH 8
static float vu0Save[VU0_SAVE_DEPTH][4][4];
static unsigned int vu0SaveTop;

void ico_vu0_registers_push(void)
{
    memcpy(vu0Save[vu0SaveTop % VU0_SAVE_DEPTH], ico_current_matrix, sizeof ico_current_matrix);
    vu0SaveTop++;
}

void ico_vu0_registers_pop(void)
{
    vu0SaveTop--;
    memcpy(ico_current_matrix, vu0Save[vu0SaveTop % VU0_SAVE_DEPTH], sizeof ico_current_matrix);
}
