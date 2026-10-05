/*
 * port/math/ps2float.c
 *
 * The out-of-line PS2 float helpers (ps2float.h). They call the host's
 * sqrtf, which IEEE 754 requires to be correctly rounded in the current
 * rounding mode (round toward zero on the sim thread), so every host gives
 * the same bits. This file is compiled without port/compat on its include
 * path, so sqrtf is the host's and not the newlib copy the game's sqrtf
 * calls map to (port/compat/ico_libc.h).
 */
#include "ps2float.h"

#include <math.h>

float ps2_sqrt(float x)
{
    if (ps2_is_zero(x)) {
        return 0.0f;
    }
    return sqrtf(fabsf(x));
}

float ps2_rsqrt(float a, float b)
{
    if (ps2_is_zero(b)) {
        if (ps2_is_zero(a)) {
            /* PCSX2's vrsqrt 0/0: a signed zero (float-semantics.md, open
               question 5). */
            return ps2_bits_float((ps2_float_bits(a) ^ ps2_float_bits(b)) & 0x80000000u);
        }
        return ps2_fmax_signed(a, b);
    }
    return a / sqrtf(fabsf(b));
}
