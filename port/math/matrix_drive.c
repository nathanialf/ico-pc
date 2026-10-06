/*
 * port/math/matrix_drive.c
 *
 * sugipon/src/matrixDrive.c's VU0 and quadword-copy routines.
 * docs/port/MATH.md has the notes per routine.
 */
#include <string.h>

#include "ico_math.h"
#include "../compat/libvu0.h"
#include "../../ico2/sugipon/include/matrixDrive.h"

void CopyVector(void *dst, void *src)
{
    memmove(dst, src, 16);
}

void CopyIVector(void *dst, void *src)
{
    memmove(dst, src, 16);
}

void CopyMatrix(void *dst, void *src)
{
    memmove(dst, src, 64);
}

void ico_set_transpose_matrix_ps2(float *dst, const float *src)
{
    float t[4][4];
    float v[4] = {-src[12], -src[13], -src[14], 0.0f};
    int i;
    int j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            t[i][j] = src[j * 4 + i];
        }
    }
    t[0][3] = t[1][3] = t[2][3] = 0.0f;
    ico_apply_matrix_ps2(t[3], (const float (*)[4])t, v);
    t[3][3] = 1.0f;
    memcpy(dst, t, sizeof t);
}

/* The PS2 wrote through the uncached alias of dst (dst | 0x20000000);
   the host has one memory view. */
void CopyMatrixUncached(void *dst, void *src)
{
    memmove(dst, src, 64);
}

/* xyz summed, w is a's. */
void AddVectorXYZ(void *d, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] + q[0], p[1] + q[1], p[2] + q[2], p[3]};
    memcpy(d, r, sizeof r);
}

void SubVectorXYZ(void *d, void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3]};
    memcpy(d, r, sizeof r);
}

/* Identity rotation, translation row (row 3) kept. */
void UnitRotation(void *m)
{
    float t[4];

    memcpy(t, (char *)m + 48, sizeof t);
    sceVu0UnitMatrix(m);
    memcpy((char *)m + 48, t, sizeof t);
}

float FSqrt(float x)
{
    return ps2_sqrt(x);
}

static float len2(const float *v)
{
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

float VectorLength(void *v)
{
    return ps2_sqrt(len2(v));
}

float VectorLengthSquare(void *v)
{
    return len2(v);
}

float GetPointDistance(void *a, void *b)
{
    const float *p = a;
    const float *q = b;
    float d[4] = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3] - q[3]};
    return ps2_sqrt(len2(d));
}
