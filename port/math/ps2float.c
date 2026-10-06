/*
 * port/math/ps2float.c
 *
 * The out-of-line PS2 float helpers (ps2float.h): the divider and the square
 * root. Both are PCSX2 PR #12001's hardware-derived PS2Float::Div and
 * PS2Float::Sqrt (head 0392a64e, pcsx2/PS2Float.cpp): radix-2 SRT
 * recurrences on a carry-save remainder (the algorithm of DOI
 * 10.1109/ARITH.1995.465363), 25 quotient digits from the set {-1, 0, 1},
 * the digit picked from the top bits of the redundant remainder. The result
 * is the truncated quotient or root, or one ulp above it, depending on the
 * bits. Integer arithmetic only, so the host's
 * rounding mode does not reach them.
 */
#include "ps2float.h"

/* One carry-save add: a + b + c as a sum word and a carry word. */
static void ps2_csa(uint32_t a, uint32_t b, uint32_t c, uint32_t *sum, uint32_t *carry)
{
    uint32_t u = a ^ b;

    *sum = u ^ c;
    *carry = ((a & b) | (u & c)) << 1;
}

/* The next quotient digit from the redundant remainder (binary point between
   bits 24 and 25): 1 at 0.25 or above, -1 below -0.5, else 0. */
static int ps2_quotient_select(uint32_t sum, uint32_t carry)
{
    const uint32_t mask = (1u << 24) - 1u; /* bit 23 is or'd in, not added */
    int32_t test = (int32_t)(((sum & ~mask) + carry) | (sum & mask));

    if (test >= (1 << 23)) {
        return 1;
    }
    if (test < (int32_t)(~0u << 24)) {
        return -1;
    }
    return 0;
}

float ps2_div_nonzero(uint32_t a, uint32_t b)
{
    uint32_t bm = ((b & 0x7FFFFFu) | 0x800000u) << 2;
    uint32_t sum = ((a & 0x7FFFFFu) | 0x800000u) << 2;
    uint32_t carry = 0;
    uint32_t quotient = 0;
    uint32_t sign = (a ^ b) & 0x80000000u;
    int digit = 1;
    int e;
    int i;

    for (i = 0; i < 25; i++) {
        uint32_t add = digit > 0 ? ~bm : digit < 0 ? bm : 0u;
        uint32_t s;
        uint32_t c;

        quotient = (quotient << 1) + (uint32_t)digit;
        carry += digit > 0 ? 1u : 0u;
        ps2_csa(sum, carry, add, &s, &c);
        digit = digit != 0 ? ps2_quotient_select(s, c) : ps2_quotient_select(sum, carry);
        sum = s << 1;
        carry = c << 1;
    }
    e = (int)((a >> 23) & 0xFFu) - (int)((b >> 23) & 0xFFu) + 126;
    if (quotient >= (1u << 24)) {
        e++;
        quotient >>= 1;
    }
    if (e > 254) {
        return ps2_bits_float(sign | 0x7F7FFFFFu); /* Fmax (F3) */
    }
    if (e < 1) {
        return ps2_bits_float(sign);
    }
    return ps2_bits_float(sign | ((uint32_t)e << 23) | (quotient & 0x7FFFFFu));
}

float ps2_sqrt(float x)
{
    uint32_t a = ps2_float_bits(x);
    uint32_t m;
    uint32_t sum;
    uint32_t carry = 0;
    uint32_t root = 0;
    int digit = 1;
    int i;

    if (ps2_is_zero(x)) {
        return 0.0f;
    }
    m = ((a & 0x7FFFFFu) | 0x800000u) << 1;
    if ((a & 0x800000u) == 0) { /* an odd exponent once the bias is removed */
        m <<= 1;
    }
    sum = m;
    for (i = 0; i < 25; i++) {
        /* adding d to the root adds d * (2 * root + d) to its square, which
           comes off the remainder */
        uint32_t adjust = root + ((uint32_t)digit << (24 - i));
        uint32_t add = digit > 0 ? ~adjust : digit < 0 ? adjust : 0u;
        uint32_t s;
        uint32_t c;

        root += (uint32_t)digit << (25 - i);
        carry += digit > 0 ? 1u : 0u;
        ps2_csa(sum, carry, add, &s, &c);
        digit = digit != 0 ? ps2_quotient_select(s, c) : ps2_quotient_select(sum, carry);
        sum = s << 1;
        carry = c << 1;
    }
    /* sqrt(|x|): the sign is dropped */
    return ps2_bits_float(
        (((root >> 2) & 0x7FFFFFu) | ((uint32_t)((((a >> 23) & 0xFFu) + 127u) >> 1) << 23)));
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
    return ps2_div(a, ps2_sqrt(b));
}
