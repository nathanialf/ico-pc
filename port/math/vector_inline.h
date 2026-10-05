/*
 * port/math/vector_inline.h
 *
 * Small vector routines that game headers and sources define as static
 * inline functions over VU0 inline assembly (sugipon/include/sugiCommon.h,
 * sugipon/src/clothAnimation.c). Their ICO_HOST bodies call these. Sums are
 * in the assembly's order (x, then y, then z); fields the assembly does not
 * write are copied from the source it loaded. docs/port/MATH.md.
 */
#ifndef ICO_MATH_VECTOR_INLINE_H
#define ICO_MATH_VECTOR_INLINE_H

#include "ps2float.h"

/* dot(pos.xyz, plane.xyz) + plane.w. */
static inline float ico_plane_distance(const void *pos, const void *plane)
{
    const float *p = pos;
    const float *n = plane;
    return p[0] * n[0] + p[1] * n[1] + p[2] * n[2] + n[3];
}

/* |a - b|^2 over xyz. */
static inline float ico_distance_squared(const void *a, const void *b)
{
    const float *p = a;
    const float *q = b;
    float dx = p[0] - q[0];
    float dy = p[1] - q[1];
    float dz = p[2] - q[2];
    return dx * dx + dy * dy + dz * dz;
}

/* |a - b|^2 over x and z. */
static inline float ico_distance_squared_xz(const void *a, const void *b)
{
    const float *p = a;
    const float *q = b;
    float dx = p[0] - q[0];
    float dz = p[2] - q[2];
    return dx * dx + dz * dz;
}

/* v.x^2 + v.z^2. */
static inline float ico_xz_length_square(const void *v)
{
    const float *p = v;
    return p[0] * p[0] + p[2] * p[2];
}

/* d = a - b (all four fields); returns 1/|d.xyz| (vrsqrt). */
static inline float ico_sub_inv_length(void *d, const void *a, const void *b)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] - q[0], p[1] - q[1], p[2] - q[2], p[3] - q[3]};
    float inv = ps2_rsqrt(1.0f, r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    __builtin_memcpy(d, r, sizeof r);
    return inv;
}

/* d = (a.xyz + b.xyz * k, a.w). */
static inline void ico_scale_add_xyz(void *d, const void *a, const void *b, float k)
{
    const float *p = a;
    const float *q = b;
    float r[4] = {p[0] + q[0] * k, p[1] + q[1] * k, p[2] + q[2] * k, p[3]};
    __builtin_memcpy(d, r, sizeof r);
}

/* d = (s.x * k, s.y, s.z * k, s.w). */
static inline void ico_scale_xz(void *d, const void *s, float k)
{
    const float *p = s;
    float r[4] = {p[0] * k, p[1], p[2] * k, p[3]};
    __builtin_memcpy(d, r, sizeof r);
}

#endif /* ICO_MATH_VECTOR_INLINE_H */
