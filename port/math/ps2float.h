/*
 * port/math/ps2float.h
 *
 * The few PS2 float behaviours the port reproduces by hand. Everything else
 * is ordinary C float arithmetic: the simulation thread runs with
 * round-toward-zero and flush-to-zero/denormals-are-zero
 * (port/platform/fpenv.c), and the build uses -ffp-contract=off, so
 * `a * b + c` rounds the product and then the sum, as VU0's multiply-add
 * does (docs/research/float-semantics.md, "VU0 (macro mode)").
 *
 * What plain C would get wrong, and these helpers fix:
 *   - division by zero: IEEE gives Inf or NaN; the EE FPU and VU0 give
 *     +-Fmax with the sign of dividend XOR divisor, 0/0 included.
 *   - square root of a negative number: IEEE gives NaN; VU0 takes sqrt(|x|).
 *   - float to int out of range: x86's cvttss2si gives 0x80000000 for
 *     both signs; the PS2 saturates by sign.
 * Overflow needs no helper: under round-toward-zero an overflowing result is
 * already FLT_MAX (0x7F7FFFFF), the port's Fmax (the hardware's 0x7FFFFFFF
 * is not a host float: docs/port/DIVERGENCES.md F3).
 *
 * The zero tests look at the exponent bits, so a denormal divisor counts as
 * zero even where DAZ is off (unit tests, other threads), as on the PS2.
 */
#ifndef ICO_MATH_PS2FLOAT_H
#define ICO_MATH_PS2FLOAT_H

#include <float.h>
#include <stdint.h>

static inline uint32_t ps2_float_bits(float x)
{
    uint32_t u;
    __builtin_memcpy(&u, &x, sizeof u);
    return u;
}

static inline float ps2_bits_float(uint32_t u)
{
    float x;
    __builtin_memcpy(&x, &u, sizeof x);
    return x;
}

/* True for +-0 and for denormals, which the PS2 reads as zero. */
static inline int ps2_is_zero(float x)
{
    return (ps2_float_bits(x) & 0x7F800000u) == 0;
}

/* +-Fmax with the sign of a XOR b. */
static inline float ps2_fmax_signed(float a, float b)
{
    return ps2_bits_float(((ps2_float_bits(a) ^ ps2_float_bits(b)) & 0x80000000u) | 0x7F7FFFFFu);
}

/* An operand as the PS2 reads it: the FPU and VU0 have no Inf or NaN, an
   exponent of 255 is an ordinary exponent (about 2^128), so such a bit
   pattern becomes +-Fmax on the host (the hardware's value is not a host
   float: DIVERGENCES.md F3). Only for values that can come from raw memory,
   such as the allocator's 0xFFFFFFFF fill of freed blocks. */
static inline float ps2_operand(float x)
{
    uint32_t u = ps2_float_bits(x);

    if ((u & 0x7F800000u) == 0x7F800000u) {
        return ps2_bits_float((u & 0x80000000u) | 0x7F7FFFFFu);
    }
    return x;
}

/* a / b: VU0 `vdiv` and the EE's `div.s`. A zero divisor gives +-Fmax,
   0/0 included. */
static inline float ps2_div(float a, float b)
{
    if (ps2_is_zero(b)) {
        return ps2_fmax_signed(a, b);
    }
    return a / b;
}

/* a + b and a - b as the EE adder computes them (DIVERGENCES.md F1), after
   PCSX2's hardware-derived PS2Float::Add/Sub/DoAdd (PR #12001,
   docs/research/float-semantics.md): the operand with the smaller exponent
   keeps one bit below its alignment shift (its lower bits are masked off),
   the mantissas are summed as integers with six extra low bits, and the sum
   is truncated. An effective subtraction can therefore come out one ulp
   larger in magnitude than IEEE round toward zero, and an operand 25 or
   more binades below the other drops entirely (1.0f - 2^-30 is 0x3F800000
   on the PS2, 0x3F7FFFFF under IEEE RTZ). A zero or denormal operand reads
   as zero, as under DAZ. Not used for ordinary game arithmetic, which stays
   plain C under round toward zero (F1); see MATH.md for where it is. */
static inline float ps2_add_aligned(uint32_t a, uint32_t b)
{
    int ea = (int)((a >> 23) & 0xFFu);
    int eb = (int)((b >> 23) & 0xFFu);
    int d;
    int32_t ma, mb, man;
    uint32_t am;
    int p;
    int e;

    if (ea < eb) {
        uint32_t t = a;
        int te = ea;

        a = b;
        b = t;
        ea = eb;
        eb = te;
    }
    d = ea - eb;
    if (d >= 25) {
        return ps2_bits_float(a);
    }
    ma = (int32_t)((a & 0x7FFFFFu) | 0x800000u) * 64;
    mb = (int32_t)((b & 0x7FFFFFu) | 0x800000u) * 64;
    if (a & 0x80000000u) {
        ma = -ma;
    }
    /* an arithmetic shift: a negative mantissa rounds toward minus infinity */
    mb = (b & 0x80000000u) ? -(int32_t)(((uint32_t)mb + ((1u << d) - 1u)) >> d) : mb >> d;
    man = ma + mb;
    if (man == 0) {
        return 0.0f;
    }
    am = man < 0 ? (uint32_t)-man : (uint32_t)man;
    p = 31 - __builtin_clz(am);
    e = ea - 6 + (p - 23);
    am = p > 23 ? am >> (p - 23) : am << (23 - p);
    if (e > 254) {
        return man < 0 ? -FLT_MAX : FLT_MAX;
    }
    if (e < 1) {
        return man < 0 ? -0.0f : 0.0f;
    }
    return ps2_bits_float((man < 0 ? 0x80000000u : 0u) | ((uint32_t)e << 23) | (am & 0x7FFFFFu));
}

static inline float ps2_add(float fa, float fb)
{
    uint32_t a = ps2_float_bits(fa);
    uint32_t b = ps2_float_bits(fb);
    int d = (int)((a >> 23) & 0xFFu) - (int)((b >> 23) & 0xFFu);

    if ((a & 0x7F800000u) == 0 || (b & 0x7F800000u) == 0) {
        return fa + fb;
    }
    if (d > 0 && d < 25) {
        b &= 0xFFFFFFFFu << (d - 1);
    } else if (d < 0 && d > -25) {
        a &= 0xFFFFFFFFu << (-d - 1);
    }
    return ps2_add_aligned(a, b);
}

static inline float ps2_sub(float a, float b)
{
    return ps2_add(a, -b);
}

/* VU0 `vsqrt`: sqrt(|x|), so a negative input is not NaN. port/math/ps2float.c. */
float ps2_sqrt(float x);

/* VU0 `vrsqrt`: a / sqrt(|b|), rounded in two steps (the hardware's single
   iterative step can differ in the last bit: DIVERGENCES.md F2). b == 0
   gives +-Fmax, or +-0 when a is also 0. port/math/ps2float.c. */
float ps2_rsqrt(float a, float b);

/* Float to int, truncating, saturating at +-2^31 by sign (EE `cvt.w.s`,
   VU0 `vftoi0`). */
static inline int32_t ps2_ftoi(float x)
{
    const uint32_t u = ps2_float_bits(x);
    /* an exponent-255 pattern is a huge number to the EE: it saturates by
       its sign (the host's conversion gives INT32_MIN and raises invalid) */
    const int huge = (u & 0x7F800000u) == 0x7F800000u;
    const int sat = huge || x >= 2147483648.0f;
    /* every lane is brought in range before the one conversion: a
       vectorised convert (Shadow.c's 24 lanes) raises invalid on the lanes a
       branch would have skipped, which the fptrap build reports */
    float c = sat ? 0.0f : x;
    int32_t r;

    c = c < -2147483648.0f ? -2147483648.0f : c;
    r = (int32_t)c;
    return sat ? ((u & 0x80000000u) != 0 ? INT32_MIN : INT32_MAX) : r;
}

/* VU0 `vftoi4`: 28.4 fixed point. The scaling by 16 is exact (an overflow
   lands on Fmax, which saturates). */
static inline int32_t ps2_ftoi4(float x)
{
    return ps2_ftoi(x * 16.0f);
}

/* VU0 `vitof0`: int to float, rounded toward zero by the sim thread's mode. */
static inline float ps2_itof(int32_t i)
{
    return (float)i;
}

/* VU0 `vmax` / `vmini` on one field: an ordinary float compare (no NaN). */
static inline float ps2_max(float a, float b)
{
    return a < b ? b : a;
}

static inline float ps2_min(float a, float b)
{
    return b < a ? b : a;
}

#endif /* ICO_MATH_PS2FLOAT_H */
