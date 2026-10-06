/*
 * port/math/ps2float.h
 *
 * The few PS2 float behaviours the port reproduces by hand. Everything else
 * is ordinary C float arithmetic: the simulation thread runs with
 * round-toward-zero and flush-to-zero/denormals-are-zero
 * (port/platform/fpenv.c), and the build uses -ffp-contract=off, so
 * `a * b + c` rounds the product and then the sum, as VU0's multiply-add
 * does.
 *
 * What plain C would get wrong, and these helpers fix:
 *   - division by zero: IEEE gives Inf or NaN; the EE FPU and VU0 give
 *     +-Fmax with the sign of dividend XOR divisor, 0/0 included.
 *   - square root of a negative number: IEEE gives NaN; VU0 takes sqrt(|x|).
 *   - float to int out of range: x86's cvttss2si gives 0x80000000 for
 *     both signs; the PS2 saturates by sign.
 * Overflow needs no helper: under round-toward-zero an overflowing result is
 * already FLT_MAX (0x7F7FFFFF), the port's Fmax (the hardware's 0x7FFFFFFF
 * is not a host float).
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
   float). Only for values that can come from raw memory,
   such as the allocator's 0xFFFFFFFF fill of freed blocks. */
static inline float ps2_operand(float x)
{
    uint32_t u = ps2_float_bits(x);

    if ((u & 0x7F800000u) == 0x7F800000u) {
        return ps2_bits_float((u & 0x80000000u) | 0x7F7FFFFFu);
    }
    return x;
}

/* The quotient of two nonzero operands as the divider computes it
   (port/math/ps2float.c). */
float ps2_div_nonzero(uint32_t a, uint32_t b);

/* a / b: VU0 `vdiv` and the EE's `div.s`. A zero divisor gives +-Fmax,
   0/0 included. Otherwise the divider's own quotient (PCSX2 PR #12001's
   hardware-derived PS2Float::Div, a radix-2 SRT divider with a carry-save
   remainder): the truncated quotient T or T + 1, depending on the bits, so
   neither IEEE round toward zero nor round to nearest. A zero or denormal dividend gives a zero with the sign of a XOR b. */
static inline float ps2_div(float a, float b)
{
    uint32_t ua = ps2_float_bits(a);
    uint32_t ub = ps2_float_bits(b);

    if (ps2_is_zero(b)) {
        return ps2_fmax_signed(a, b);
    }
    if (ps2_is_zero(a)) {
        return ps2_bits_float((ua ^ ub) & 0x80000000u);
    }
    return ps2_div_nonzero(ua, ub);
}

/* The 24 x 24-bit mantissa product as the multiplier forms it (PCSX2 PR
   #12001's MulMantissa, from hardware tests): eight radix-4 Booth partial
   products of a, recoded from b, summed in a carry-save tree whose low 15
   bits are discarded before the final add; bit 15 of that sum replaces bit
   15 of the exact product, borrowing from the bits above. So the product is
   at most one unit of bit 15 below the exact one, and the operands are not
   interchangeable: 1.0 * x (a = 1.0) usually comes out one ulp below x,
   x * 1.0 never does. */
static inline uint64_t ps2_mul_mantissa(uint32_t a, uint32_t b)
{
    uint64_t full = (uint64_t)a * b;
    uint32_t data[8];
    uint32_t negate[8];
    uint32_t s0, c0, s1, c1, s2, c2, s3, c3, s4, c4, s5, c5, u, lo;
    int k;

    for (k = 0; k < 8; k++) {
        uint32_t test = (k != 0 ? b >> (k * 2 - 1) : b << 1) & 7u;
        uint32_t m = a << (k * 2);
        uint32_t neg = (test >= 4u && test <= 6u) ? ~0u : 0u;
        uint32_t pos = 1u << (k * 2);

        m += (test == 3u || test == 4u) ? m : 0u;
        m ^= neg & (0u - pos);
        m &= (test >= 1u && test <= 6u) ? ~0u : 0u;
        data[k] = m;
        negate[k] = neg & pos;
    }
#define PS2_ADD3(s, c, x, y, z)                                                                    \
    do {                                                                                           \
        u = (x) ^ (y);                                                                             \
        s = u ^ (z);                                                                               \
        c = ((u & (z)) | ((x) & (y))) << 1;                                                        \
    } while (0)
    PS2_ADD3(s0, c0, data[1], data[2], data[3]);
    PS2_ADD3(s1, c1, data[4] & ~0x7FFu, data[5] & ~0xFFFu, data[6]);
    c1 |= negate[6] | (data[5] & 0x800u);
    data[7] |= (data[5] & 0x400u) + negate[5];
    PS2_ADD3(s2, c2, data[0], s0, c0);
    PS2_ADD3(s3, c3, data[7], s1, c1);
    PS2_ADD3(s4, c4, c2, s3, c3);
    PS2_ADD3(s5, c5, s2, s4, c4);
#undef PS2_ADD3
    c5 += negate[7];
    s5 &= ~0x7FFFu;
    c5 &= ~0x7FFFu;
    lo = s5 + c5;
    return full - ((lo ^ full) & 0x8000u);
}

/* a * b as the multiplier computes it: EE `mul.s` (fs = a, ft = b) and the
   multiply of VU0 `vmul`/`vmadd`/`vmsub` (fs = a, ft = b, the broadcast
   field), after PCSX2 PR #12001's PS2Float::Mul/DoMul: the product above
   truncated to 24 bits. Not commutative (ps2_mul_mantissa); pass the
   operands in the instruction's order. A zero or denormal operand gives a
   zero with the sign of a XOR b; an overflow gives +-Fmax (0x7F7FFFFF, F3),
   an underflow a signed zero. */
static inline float ps2_mul(float fa, float fb)
{
    uint32_t a = ps2_float_bits(fa);
    uint32_t b = ps2_float_bits(fb);
    uint32_t sign = (a ^ b) & 0x80000000u;
    int e;
    uint32_t m;

    if ((a & 0x7F800000u) == 0 || (b & 0x7F800000u) == 0) {
        return ps2_bits_float(sign);
    }
    e = (int)((a >> 23) & 0xFFu) + (int)((b >> 23) & 0xFFu) - 127;
    m = (uint32_t)(ps2_mul_mantissa((a & 0x7FFFFFu) | 0x800000u, (b & 0x7FFFFFu) | 0x800000u) >>
                   23);
    if (m > 0xFFFFFFu) {
        m >>= 1;
        e++;
    }
    if (e > 254) {
        return ps2_bits_float(sign | 0x7F7FFFFFu);
    }
    if (e < 1) {
        return ps2_bits_float(sign);
    }
    return ps2_bits_float(sign | ((uint32_t)e << 23) | (m & 0x7FFFFFu));
}

/* a + b and a - b as the EE adder computes them, after
   PCSX2's hardware-derived PS2Float::Add/Sub/DoAdd (PR #12001): the operand with the smaller exponent
   keeps one bit below its alignment shift (its lower bits are masked off),
   the mantissas are summed as integers with six extra low bits, and the sum
   is truncated. An effective subtraction can therefore come out one ulp
   larger in magnitude than IEEE round toward zero, and an operand 25 or
   more binades below the other drops entirely (1.0f - 2^-30 is 0x3F800000
   on the PS2, 0x3F7FFFFF under IEEE RTZ). A zero or denormal operand reads
   as zero, as under DAZ. Not used for ordinary game arithmetic, which stays
   plain C under round toward zero. */
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
   iterative step can differ in the last bit). b == 0
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
