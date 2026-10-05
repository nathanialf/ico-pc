/*
 * port/math/cloth.c
 *
 * The VU0 routines sugipon/src/clothAnimation.c defines with external
 * linkage. None has a caller outside that file (its own code uses the
 * static inline copies), but the symbols are the game's.
 * docs/port/MATH.md.
 */
#include "ico_math.h"
#include "vector_inline.h"

void FSqrtInv(void);
float getXZLength(void *v);
float getXZInvLength(void *v);
float getXZLengthSquare(void *v);
float subAndGetInvLength(void *d, const void *a, const void *b);
void scaleAndAddVectorXYZ(void *d, const void *a, const void *b, float k);
void scaleVectorXZ(void *d, const void *s, float k);
void tensionMoveNoReduce(void *out, void *a, void *b, float k);
void tensionMove(void *out, void *a, void *b, float k, float lim);

/* Declared void (void), but the assembly computes 1/sqrt of whatever is in
   $f12 into Q and $f0: a float-in, float-out routine under a void
   prototype. It has no callers, and with no argument there is nothing to
   compute, so the host version does nothing. */
void FSqrtInv(void) {}

float getXZLength(void *v)
{
    return ps2_sqrt(ico_xz_length_square(v));
}

float getXZInvLength(void *v)
{
    return ps2_rsqrt(1.0f, ico_xz_length_square(v));
}

float getXZLengthSquare(void *v)
{
    return ico_xz_length_square(v);
}

float subAndGetInvLength(void *d, const void *a, const void *b)
{
    return ico_sub_inv_length(d, a, b);
}

void scaleAndAddVectorXYZ(void *d, const void *a, const void *b, float k)
{
    ico_scale_add_xyz(d, a, b, k);
}

void scaleVectorXZ(void *d, const void *s, float k)
{
    ico_scale_xz(d, s, k);
}

/* out = (b.xyz + (a - b).xyz * (k / |a - b|), b.w); k / |a - b| is k times
   the vrsqrt result (an FPU mul.s). */
void tensionMoveNoReduce(void *out, void *a, void *b, float k)
{
    float buf[4];
    float inv = ico_sub_inv_length(buf, a, b);

    ico_scale_add_xyz(out, b, buf, k * inv);
}

/* As tensionMoveNoReduce, only when 1/|a - b| < lim; otherwise out is not
   written. */
void tensionMove(void *out, void *a, void *b, float k, float lim)
{
    float buf[4];
    float inv = ico_sub_inv_length(buf, a, b);

    if (inv < lim) {
        ico_scale_add_xyz(out, b, buf, k * inv);
    }
}
