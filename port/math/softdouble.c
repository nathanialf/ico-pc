/*
 * port/math/softdouble.c
 *
 * The EE's double arithmetic with integers only (port/math/softdouble.h).
 *
 * Written from the IEEE-754 binary64 definition and the behaviour of the
 * libgcc soft float the game linked (sce/libgcc/dp-bit.c, fp-bit.c, which
 * follow GCC's GPL fp-bit.c; no code is taken from them, so this file is
 * MIT like the rest of port/). What the EE's library does differently from
 * a textbook IEEE implementation, and this file repeats:
 *
 *   - NO_DENORMALS (dp-bit.c, the define at the top): an operand with a zero
 *     exponent is a zero, whatever its fraction. fp-bit's unpacker does the
 *     same for floats.
 *   - A number is held as a 61-bit significand, the leading one at bit 60,
 *     the 52 fraction bits above 8 guard bits. Alignment shifts keep a
 *     sticky bit in bit 0, except that an exponent difference of 64 or more
 *     drops the smaller operand outright.
 *   - Packing rounds the 8 guard bits to nearest even (0x80 is the tie),
 *     after checking the exponent: below 2^-1022 the significand is shifted
 *     right and truncated into a denormal (more than 56 places: zero), above
 *     2^1023 the result is Inf. A carry out of the rounding can still make
 *     the exponent field 2047 (Inf). The fraction field keeps 52 bits, so a
 *     carry into bit 52 of a denormal is dropped.
 *   - The product and the quotient settle a guard field of exactly 0x80
 *     before packing: up when the kept LSB is 1 or any lower bit (the rest
 *     of the 122-bit product, the division remainder) is set.
 *   - Specials: a NaN operand is returned quieted with its own payload
 *     (product: sign XORed with the other operand's; difference: the
 *     subtrahend's sign flipped); Inf - Inf, 0 * Inf, 0 / 0 and Inf / Inf
 *     give the library's static NaN, +0x7FF8000000000000; 0 + 0 is -0 only
 *     when both are -0; x / Inf is a zero and x / 0 an Inf, signed.
 *   - Comparison returns 1 when either operand is a NaN; zeros of either
 *     sign are equal.
 *   - Double to int truncates; magnitudes of 2^31 and up and Infs saturate
 *     by sign, NaN gives 0. INT_MIN converts to -2^31 exactly.
 *   - Double to float takes the top 31 significand bits with a sticky bit
 *     and packs with the float rules (7 guard bits, tie 0x40, truncation
 *     below 2^-126, Inf above 2^127). A NaN's float fraction is bits 30-51
 *     of the double's fraction, the sticky bit and bit 20 set.
 *   - Float to double is exact; a float denormal reads as zero; a NaN's
 *     fraction moves up 30 places (its bit 22 drops out) and bit 51 is set.
 *
 * port/math/test/softdouble_test.c compiles the library's own sources and
 * compares every function bit for bit.
 */
#include "softdouble.h"

#define SD_FRAC_MASK 0x000FFFFFFFFFFFFFull
#define SD_QUIET 0x0008000000000000ull
#define SD_LEAD 0x1000000000000000ull  /* bit 60: the leading one */
#define SD_CARRY 0x2000000000000000ull /* bit 61 */
#define SD_DEFAULT_NAN 0x7FF8000000000000ull

enum { SD_NAN, SD_ZERO, SD_INF, SD_NUM };

typedef struct {
    int cls;
    int sign;
    int exp;      /* unbiased; the value is sig * 2^(exp - 60) */
    uint64_t sig; /* SD_NUM: leading one at bit 60; SD_NAN: the raw fraction */
} SdNum;

static SdNum sd_unpack(uint64_t a)
{
    SdNum r;
    int e = (int)((a >> 52) & 0x7FF);
    uint64_t f = a & SD_FRAC_MASK;

    r.sign = (int)(a >> 63);
    r.exp = 0;
    r.sig = 0;
    if (e == 0) {
        r.cls = SD_ZERO; /* denormals included */
    } else if (e == 0x7FF) {
        r.cls = f == 0 ? SD_INF : SD_NAN;
        r.sig = f;
    } else {
        r.cls = SD_NUM;
        r.exp = e - 1023;
        r.sig = ((f | (SD_FRAC_MASK + 1)) << 8);
    }
    return r;
}

static uint64_t sd_pack(SdNum x)
{
    uint64_t s = (uint64_t)(x.sign & 1) << 63;
    uint64_t sig = x.sig;
    int e;

    switch (x.cls) {
    case SD_NAN:
        return s | (0x7FFull << 52) | ((sig | SD_QUIET) & SD_FRAC_MASK);
    case SD_INF:
        return s | (0x7FFull << 52);
    case SD_ZERO:
        return s;
    default:
        break;
    }
    if (sig == 0) {
        return s;
    }
    if (x.exp < -1022) {
        int shift = -1022 - x.exp;
        sig = shift > 56 ? 0 : sig >> shift;
        return s | ((sig >> 8) & SD_FRAC_MASK);
    }
    if (x.exp > 1023) {
        return s | (0x7FFull << 52);
    }
    e = x.exp + 1023;
    if ((sig & 0xFF) == 0x80) {
        if (sig & 0x100) {
            sig += 0x80;
        }
    } else {
        sig += 0x7F;
    }
    if (sig >= SD_CARRY) {
        sig >>= 1;
        e++;
    }
    return s | ((uint64_t)(e & 0x7FF) << 52) | ((sig >> 8) & SD_FRAC_MASK);
}

static SdNum sd_default_nan(void)
{
    SdNum r = {SD_NAN, 0, 0, 0};
    return r;
}

/* x >> n keeping a sticky bit: bit 0 is set when any bit shifted out was. */
static uint64_t sd_shift_sticky(uint64_t x, int n)
{
    if (n == 0) {
        return x;
    }
    return (x >> n) | ((x & ((1ull << n) - 1)) != 0);
}

static int sd_clz64(uint64_t x)
{
    return __builtin_clzll(x);
}

/* A guard field of exactly 0x80 goes up when the LSB is odd or anything
   lies below the guard bits (the product's and quotient's tie rule). */
static uint64_t sd_settle_tie(uint64_t sig, int below)
{
    if ((sig & 0xFF) == 0x80 && ((sig & 0x100) || below)) {
        sig += 0x80;
    }
    return sig;
}

static SdNum sd_add_parts(SdNum a, SdNum b)
{
    SdNum r;
    uint64_t sa, sb;
    int ea, eb, d;

    if (a.cls == SD_NAN) {
        return a;
    }
    if (b.cls == SD_NAN) {
        return b;
    }
    if (a.cls == SD_INF) {
        if (b.cls == SD_INF && a.sign != b.sign) {
            return sd_default_nan();
        }
        return a;
    }
    if (b.cls == SD_INF) {
        return b;
    }
    if (b.cls == SD_ZERO) {
        if (a.cls == SD_ZERO) {
            a.sign &= b.sign;
        }
        return a;
    }
    if (a.cls == SD_ZERO) {
        return b;
    }

    sa = a.sig;
    sb = b.sig;
    ea = a.exp;
    eb = b.exp;
    d = ea > eb ? ea - eb : eb - ea;
    if (d >= 64) {
        if (ea > eb) {
            sb = 0;
        } else {
            sa = 0;
        }
    } else if (ea > eb) {
        sb = sd_shift_sticky(sb, d);
    } else {
        sa = sd_shift_sticky(sa, d);
    }
    r.cls = SD_NUM;
    r.exp = ea > eb ? ea : eb;
    if (a.sign != b.sign) {
        int64_t t = a.sign ? (int64_t)(sb - sa) : (int64_t)(sa - sb);
        r.sign = t < 0;
        r.sig = t < 0 ? (uint64_t)-t : (uint64_t)t;
        if (r.sig != 0 && r.sig < SD_LEAD) {
            int n = sd_clz64(r.sig) - 3;
            r.sig <<= n;
            r.exp -= n;
        }
    } else {
        r.sign = a.sign;
        r.sig = sa + sb;
    }
    if (r.sig >= SD_CARRY) {
        r.sig = sd_shift_sticky(r.sig, 1);
        r.exp++;
    }
    return r;
}

uint64_t ico_dadd(uint64_t a, uint64_t b)
{
    return sd_pack(sd_add_parts(sd_unpack(a), sd_unpack(b)));
}

uint64_t ico_dsub(uint64_t a, uint64_t b)
{
    SdNum y = sd_unpack(b);

    y.sign ^= 1;
    return sd_pack(sd_add_parts(sd_unpack(a), y));
}

uint64_t ico_dmul(uint64_t a, uint64_t b)
{
    SdNum x = sd_unpack(a);
    SdNum y = sd_unpack(b);
    int sign = x.sign != y.sign;
    unsigned __int128 p;
    SdNum r;

    if (x.cls == SD_NAN) {
        x.sign = sign;
        return sd_pack(x);
    }
    if (y.cls == SD_NAN) {
        y.sign = sign;
        return sd_pack(y);
    }
    if (x.cls == SD_INF) {
        if (y.cls == SD_ZERO) {
            return SD_DEFAULT_NAN;
        }
        x.sign = sign;
        return sd_pack(x);
    }
    if (y.cls == SD_INF) {
        if (x.cls == SD_ZERO) {
            return SD_DEFAULT_NAN;
        }
        y.sign = sign;
        return sd_pack(y);
    }
    if (x.cls == SD_ZERO) {
        x.sign = sign;
        return sd_pack(x);
    }
    if (y.cls == SD_ZERO) {
        y.sign = sign;
        return sd_pack(y);
    }

    /* two significands in [2^60, 2^61): the product is in [2^120, 2^122) */
    p = (unsigned __int128)x.sig * y.sig;
    r.cls = SD_NUM;
    r.sign = sign;
    if (p >> 121) {
        r.exp = x.exp + y.exp + 1;
        r.sig = (uint64_t)(p >> 61);
        r.sig = sd_settle_tie(r.sig, (p & ((((unsigned __int128)1) << 61) - 1)) != 0);
    } else {
        r.exp = x.exp + y.exp;
        r.sig = (uint64_t)(p >> 60);
        r.sig = sd_settle_tie(r.sig, (p & ((((unsigned __int128)1) << 60) - 1)) != 0);
    }
    return sd_pack(r);
}

uint64_t ico_ddiv(uint64_t a, uint64_t b)
{
    SdNum x = sd_unpack(a);
    SdNum y = sd_unpack(b);
    unsigned __int128 n;
    uint64_t m1, q, rem;

    if (x.cls == SD_NAN) {
        return sd_pack(x);
    }
    if (y.cls == SD_NAN) {
        return sd_pack(y);
    }
    x.sign ^= y.sign;
    if (x.cls == SD_INF || x.cls == SD_ZERO) {
        if (x.cls == y.cls) {
            return SD_DEFAULT_NAN;
        }
        return sd_pack(x);
    }
    if (y.cls == SD_INF) {
        return (uint64_t)(x.sign & 1) << 63;
    }
    if (y.cls == SD_ZERO) {
        x.cls = SD_INF;
        return sd_pack(x);
    }

    m1 = x.sig;
    x.exp -= y.exp;
    if (m1 < y.sig) {
        x.exp--;
        m1 <<= 1;
    }
    /* the quotient's 61 bits, leading one at bit 60 */
    n = (unsigned __int128)m1 << 60;
    q = (uint64_t)(n / y.sig);
    rem = (uint64_t)(n % y.sig);
    x.sig = sd_settle_tie(q, rem != 0);
    return sd_pack(x);
}

int ico_dcmp(uint64_t a, uint64_t b)
{
    SdNum x = sd_unpack(a);
    SdNum y = sd_unpack(b);
    int neg;

    if (x.cls == SD_NAN || y.cls == SD_NAN) {
        return 1;
    }
    if (x.cls == SD_INF) {
        if (y.cls == SD_INF) {
            return y.sign - x.sign;
        }
        return x.sign ? -1 : 1;
    }
    if (y.cls == SD_INF) {
        return y.sign ? 1 : -1;
    }
    if (x.cls == SD_ZERO) {
        if (y.cls == SD_ZERO) {
            return 0;
        }
        return y.sign ? 1 : -1;
    }
    if (y.cls == SD_ZERO) {
        return x.sign ? -1 : 1;
    }
    if (x.sign != y.sign) {
        return x.sign ? -1 : 1;
    }
    /* same sign: compare magnitudes, then flip for negatives */
    neg = x.sign;
    if (x.exp != y.exp) {
        return (x.exp > y.exp) != neg ? 1 : -1;
    }
    if (x.sig != y.sig) {
        return (x.sig > y.sig) != neg ? 1 : -1;
    }
    return 0;
}

uint64_t ico_i2d(int32_t i)
{
    SdNum r;
    uint64_t mag;
    int n;

    if (i == 0) {
        return 0;
    }
    if (i == INT32_MIN) {
        return 0xC1E0000000000000ull; /* -2^31 */
    }
    r.cls = SD_NUM;
    r.sign = i < 0;
    mag = i < 0 ? (uint64_t)-(int64_t)i : (uint64_t)i;
    n = sd_clz64(mag) - 3;
    r.sig = mag << n;
    r.exp = 60 - n;
    return sd_pack(r);
}

int32_t ico_d2i(uint64_t a)
{
    SdNum x = sd_unpack(a);
    int32_t t;

    if (x.cls == SD_ZERO || x.cls == SD_NAN) {
        return 0;
    }
    if (x.cls == SD_INF || x.exp > 30) {
        return x.sign ? INT32_MIN : INT32_MAX;
    }
    if (x.exp < 0) {
        return 0;
    }
    t = (int32_t)(x.sig >> (60 - x.exp));
    return x.sign ? -t : t;
}

uint32_t ico_d2f_bits(uint64_t a)
{
    SdNum x = sd_unpack(a);
    uint32_t s = (uint32_t)(x.sign & 1) << 31;
    uint32_t hi;
    int e;

    if (x.cls == SD_ZERO) {
        return s;
    }
    if (x.cls == SD_INF) {
        return s | 0x7F800000u;
    }
    /* the top bits with a sticky bit: a float significand with its leading
       one at bit 30 and 7 guard bits (for a NaN, the fraction's bits 30-51) */
    hi = (uint32_t)(x.sig >> 30) | ((x.sig & 0x3FFFFFFFull) != 0);
    if (x.cls == SD_NAN) {
        return s | 0x7F800000u | ((hi | 0x100000u) & 0x7FFFFFu);
    }
    if (x.exp < -126) {
        int shift = -126 - x.exp;
        hi = shift > 25 ? 0 : hi >> shift;
        return s | ((hi >> 7) & 0x7FFFFFu);
    }
    if (x.exp > 127) {
        return s | 0x7F800000u;
    }
    e = x.exp + 127;
    if ((hi & 0x7F) == 0x40) {
        if (hi & 0x80) {
            hi += 0x40;
        }
    } else {
        hi += 0x3F;
    }
    if (hi >= 0x80000000u) {
        hi >>= 1;
        e++;
    }
    return s | ((uint32_t)(e & 0xFF) << 23) | ((hi >> 7) & 0x7FFFFFu);
}

uint64_t ico_f2d_bits(uint32_t f)
{
    uint64_t s = (uint64_t)(f >> 31) << 63;
    int e = (int)((f >> 23) & 0xFF);
    uint64_t frac = f & 0x7FFFFFu;

    if (e == 0) {
        return s; /* denormals read as zero */
    }
    if (e == 0xFF) {
        if (frac == 0) {
            return s | (0x7FFull << 52);
        }
        return s | (0x7FFull << 52) | (((frac << 30) | SD_QUIET) & SD_FRAC_MASK);
    }
    return s | ((uint64_t)(e - 127 + 1023) << 52) | (frac << 29);
}
