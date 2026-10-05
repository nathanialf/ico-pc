/*
 * port/math/quaternion.c
 *
 * sugipon/src/quaternion.c's VU0 routines. Quaternions are (x, y, z, w)
 * with w the scalar part. docs/port/MATH.md has the notes per routine.
 */
#include <string.h>

#include "ico_math.h"
#include "../../ico2/sugipon/include/quaternion.h"

float _Sqrt(float x);

/* out = qa * qb: w = qa.w*qb.w - (qa.x*qb.x + qa.y*qb.y + qa.z*qb.z),
   xyz = (qb*qa.w + qa*qb.w) + qb x qa. out may alias either input. */
void MultiQuaternion(void *out, void *qa, void *qb)
{
    const float *a = qa;
    const float *b = qb;
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float r[4];

    r[0] = b[0] * a[3] + a[0] * b[3] + (b[1] * a[2] - a[1] * b[2]);
    r[1] = b[1] * a[3] + a[1] * b[3] + (b[2] * a[0] - a[2] * b[0]);
    r[2] = b[2] * a[3] + a[2] * b[3] + (b[0] * a[1] - a[0] * b[1]);
    r[3] = a[3] * b[3] - dot;
    memcpy(out, r, sizeof r);
}

/* Rows 0-2 of the rotation matrix of unit quaternion q, w column 0. The
   assembly scales q by sqrt(2) first (the quatToMatrixScale table's
   {1, 1, 1, 1.41421356}) so the usual 2*a*b terms come out of single
   products. Row 3 is the caller's. */
void ico_quaternion_rotation_rows(float (*m)[4], const float *q)
{
    const float sqrt2 = 1.41421356f;
    float x = q[0] * sqrt2;
    float y = q[1] * sqrt2;
    float z = q[2] * sqrt2;
    float w = q[3] * sqrt2;
    float r[3][4];

    r[0][0] = 1.0f - (z * z + y * y);
    r[0][1] = x * y - z * w;
    r[0][2] = z * x + y * w;
    r[0][3] = 0.0f;
    r[1][0] = x * y + z * w;
    r[1][1] = 1.0f - (x * x + z * z);
    r[1][2] = y * z - x * w;
    r[1][3] = 0.0f;
    r[2][0] = z * x - y * w;
    r[2][1] = y * z + x * w;
    r[2][2] = 1.0f - (y * y + x * x);
    r[2][3] = 0.0f;
    memcpy(m, r, sizeof r);
}

void GetMatrixFromQuaternionRotElem(void *mtx, void *q)
{
    ico_quaternion_rotation_rows((float (*)[4])mtx, (const float *)q);
}

/* x*x + y*y + z*z + w*w, summed in that order. */
static float dot4(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

float GetQuaternionCosRadian(void *qa, void *qb)
{
    return dot4(qa, qb);
}

/* Squared magnitude (4-term dot), then _Sqrt. */
float GetQuaternionMagnitude(void *q)
{
    return _Sqrt(dot4(q, q));
}

/* The squared magnitude RegularizeQuaternion scales by (its C remainder
   stays in the game source). */
float ico_quaternion_norm2(const float *q)
{
    return dot4(q, q);
}
