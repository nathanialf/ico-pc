/*
 * port/math/softdouble.h
 *
 * `double` arithmetic as the PS2 did it. The EE has no double hardware:
 * ee-gcc turned every double operation into a call to libgcc's soft float
 * (sce/libgcc/dp-bit.c, fp-bit.c), so the game's few double expressions
 * (33 functions in 20 files) rounded to
 * nearest, read denormal inputs as zero and truncated a tiny result into a
 * denormal. On the host they would be SSE doubles under the simulation
 * thread's round-toward-zero mode. These functions compute the EE's results
 * with integer arithmetic only, so the host's rounding mode and DAZ/FTZ do
 * not reach them.
 *
 * Values are the IEEE-754 bit patterns: uint64_t for a double, uint32_t or
 * float for a float. ICO_D(lit) gives the bits of a double literal (the
 * compiler converts the literal to nearest, as ee-gcc did when it put it in
 * .rodata). ico_dval/ico_dbits move bits to and from a host double without
 * arithmetic, for variadic arguments and va_arg.
 *
 * The policies, all the EE's (port/math/softdouble.c has the details):
 *   - add, sub, mul, div: round to nearest even; overflow gives Inf;
 *   - a denormal operand reads as a zero of its sign;
 *   - a result below the normal range is truncated, not rounded, into a
 *     denormal (or zero);
 *   - Inf - Inf, 0 * Inf, 0 / 0, Inf / Inf give +qNaN 0x7FF8000000000000;
 *     a NaN operand comes back quieted, its sign changed as dp-bit does;
 *   - ico_dcmp returns -1, 0 or 1, and 1 when either operand is a NaN (the
 *     compiler tested the result against 0 with the source's operator);
 *   - ico_d2i truncates and saturates (+-2^31 bounds, Inf by sign), NaN 0;
 *   - ico_d2f rounds to nearest even, truncating into a float denormal.
 */
#ifndef ICO_MATH_SOFTDOUBLE_H
#define ICO_MATH_SOFTDOUBLE_H

#include <stdint.h>

uint64_t ico_dadd(uint64_t a, uint64_t b);
uint64_t ico_dsub(uint64_t a, uint64_t b);
uint64_t ico_dmul(uint64_t a, uint64_t b);
uint64_t ico_ddiv(uint64_t a, uint64_t b);
int ico_dcmp(uint64_t a, uint64_t b);
uint64_t ico_i2d(int32_t i);
int32_t ico_d2i(uint64_t a);
uint32_t ico_d2f_bits(uint64_t a);
uint64_t ico_f2d_bits(uint32_t f);

static inline float ico_d2f(uint64_t a)
{
    uint32_t u = ico_d2f_bits(a);
    float f;
    __builtin_memcpy(&f, &u, sizeof f);
    return f;
}

static inline uint64_t ico_f2d(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, sizeof u);
    return ico_f2d_bits(u);
}

/* A host double's bits and back: no arithmetic, for va_arg and for
   variadic arguments, which C passes as double. */
static inline uint64_t ico_dbits(double d)
{
    uint64_t u;
    __builtin_memcpy(&u, &d, sizeof u);
    return u;
}

static inline double ico_dval(uint64_t u)
{
    double d;
    __builtin_memcpy(&d, &u, sizeof d);
    return d;
}

/* The bits of a double literal, for the game's unsuffixed constants. */
#define ICO_D(lit)                                                                                 \
    (((union {                                                                                     \
         double d_;                                                                                \
         uint64_t u_;                                                                              \
     }){(lit)})                                                                                    \
         .u_)

#endif /* ICO_MATH_SOFTDOUBLE_H */
