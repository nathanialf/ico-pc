/* libgcc.a member fp-bit.o */
#include "libgcc2.h"

#define CLASS_SNAN 0
#define CLASS_QNAN 1
#define CLASS_ZERO 2
#define CLASS_NUMBER 3
#define CLASS_INFINITY 4
#define IMPLICIT_1 0x40000000
#define IMPLICIT_2 0x80000000

typedef struct {
    unsigned int class;
    int sign;
    int normal_exp;
    unsigned int fraction;
} fp_number_type;

/* The single-float number in unpacked form.  FRAC_NBITS is 32, NGARDS is 7,
   so the implicit one sits at bit 30 and the overflow bit at bit 31.  The
   file static shares its name with the double build's global in dp-bit.o. */

static __inline__ int isnan(fp_number_type *x)
{
    return x->class == CLASS_SNAN || x->class == CLASS_QNAN;
}

static __inline__ int isinf(fp_number_type *x)
{
    return x->class == CLASS_INFINITY;
}

static __inline__ int iszero(fp_number_type *x)
{
    return x->class == CLASS_ZERO;
}

/* fp-bit.c's nan(): the NaN this build hands back for inf - inf; its static is the
   member's whole .bss, VMA 0x736188 (USA 0x71EB88, Aug-6 prototype 0x718208, Jun-26 demo
   0x76CC08), 0x10 B. */
static __inline__ fp_number_type *nan(void)
{
    static fp_number_type thenan;

    return &thenan;
}

typedef union {
    struct {
        unsigned int fraction : 23;
        unsigned int exp : 8;
        unsigned int sign : 1;
    } bits;

    float value;
} FLO_union_type;

float __pack_f(void *s)
{
    fp_number_type *src = s;
    FLO_union_type dst;
    unsigned int fraction = src->fraction;
    int sign = src->sign;
    int exp = 0;

    if (isnan(src)) {
        exp = 0xFF;
        fraction |= 0x100000;
    } else if (isinf(src)) {
        exp = 0xFF;
        fraction = 0;
    } else if (iszero(src)) {
        exp = 0;
        fraction = 0;
    } else if (fraction == 0) {
        exp = 0;
    } else {
        if (src->normal_exp < -126) {
            /* the exponent is too low for a normal number: the integer part
               goes to zero and the fraction keeps what bits survive */
            int shift = -126 - src->normal_exp;
            if (shift > 25) {
                exp = 0;
                fraction = 0;
            } else {
                exp = 0;
                fraction >>= shift;
            }
            fraction >>= 7;
        } else if (src->normal_exp > 127) {
            exp = 0xFF;
            fraction = 0;
        } else {
            exp = src->normal_exp + 127;
            /* halfway between two numbers: round to the one whose low bit
               is zero, otherwise add a one to the guards to round up */
            if ((fraction & 0x7F) == 0x40) {
                if (fraction & 0x80) {
                    fraction += 0x40;
                }
            } else {
                fraction += 0x3F;
            }
            if (fraction >= IMPLICIT_2) {
                fraction >>= 1;
                exp += 1;
            }
            fraction >>= 7;
        }
    }
    dst.bits.fraction = fraction;
    dst.bits.exp = exp;
    dst.bits.sign = sign;
    return dst.value;
}

void __unpack_f(void *in, void *out)
{
    FLO_union_type *src = in;
    fp_number_type *dst = out;
    unsigned int fraction;
    int exp;
    int sign;

    fraction = src->bits.fraction;
    exp = src->bits.exp;
    sign = src->bits.sign;

    dst->sign = sign;
    if (exp == 0) {
        dst->class = CLASS_ZERO;
    } else if (exp == 0xFF) {
        if (fraction == 0) {
            dst->class = CLASS_INFINITY;
        } else {
            if (fraction & 0x100000) {
                dst->class = CLASS_QNAN;
            } else {
                dst->class = CLASS_SNAN;
            }
            dst->fraction = fraction;
        }
    } else {
        dst->normal_exp = exp - 127;
        dst->class = CLASS_NUMBER;
        dst->fraction = (fraction << 7) | IMPLICIT_1;
    }
}

static fp_number_type *_fpadd_parts(fp_number_type *a, fp_number_type *b, fp_number_type *tmp)
{
    int tfraction;
    int a_normal_exp;
    int b_normal_exp;
    unsigned int a_fraction;
    unsigned int b_fraction;
    int diff;

    if (isnan(a))
        return a;
    if (isnan(b))
        return b;
    if (isinf(a)) {
        /* adding infinities with opposite signs yields a NaN */
        if (isinf(b) && a->sign != b->sign)
            return nan();
        return a;
    }
    if (isinf(b))
        return b;
    if (iszero(b)) {
        if (iszero(a)) {
            *tmp = *a;
            tmp->sign = a->sign & b->sign;
            return tmp;
        }
        return a;
    }
    if (iszero(a))
        return b;

    a_normal_exp = a->normal_exp;
    b_normal_exp = b->normal_exp;
    a_fraction = a->fraction;
    b_fraction = b->fraction;

    diff = a_normal_exp - b_normal_exp;
    if (diff < 0)
        diff = -diff;
    if (diff < 32) {
        while (a_normal_exp > b_normal_exp) {
            b_normal_exp++;
            b_fraction = (b_fraction & 1) | (b_fraction >> 1);
        }
        while (b_normal_exp > a_normal_exp) {
            a_normal_exp++;
            a_fraction = (a_fraction & 1) | (a_fraction >> 1);
        }
    } else {
        if (a_normal_exp > b_normal_exp) {
            b_fraction = 0;
            b_normal_exp = a_normal_exp;
        } else {
            a_fraction = 0;
            a_normal_exp = b_normal_exp;
        }
    }

    if (a->sign != b->sign) {
        if (a->sign)
            tfraction = -a_fraction + b_fraction;
        else
            tfraction = a_fraction - b_fraction;

        if (tfraction >= 0) {
            tmp->sign = 0;
            tmp->normal_exp = a_normal_exp;
            tmp->fraction = tfraction;
        } else {
            tmp->sign = 1;
            tmp->normal_exp = a_normal_exp;
            tmp->fraction = -tfraction;
        }
        while (tmp->fraction < IMPLICIT_1 && tmp->fraction) {
            tmp->fraction <<= 1;
            tmp->normal_exp--;
        }
    } else {
        tmp->sign = a->sign;
        tmp->normal_exp = a_normal_exp;
        tmp->fraction = a_fraction + b_fraction;
    }
    tmp->class = CLASS_NUMBER;

    if (tmp->fraction >= IMPLICIT_2) {
        tmp->fraction = (tmp->fraction & 1) | (tmp->fraction >> 1);
        tmp->normal_exp++;
    }
    return tmp;
}

float fpadd(float arg_a, float arg_b)
{
    fp_number_type a;
    fp_number_type b;
    fp_number_type tmp;
    fp_number_type *res;

    __unpack_f(&arg_a, &a);
    __unpack_f(&arg_b, &b);
    res = _fpadd_parts(&a, &b, &tmp);
    return __pack_f(res);
}

float fpsub(float arg_a, float arg_b)
{
    fp_number_type a;
    fp_number_type b;
    fp_number_type tmp;
    fp_number_type *res;

    __unpack_f(&arg_a, &a);
    __unpack_f(&arg_b, &b);
    b.sign ^= 1;
    res = _fpadd_parts(&a, &b, &tmp);
    return __pack_f(res);
}

static __inline__ fp_number_type *_fpmul_parts(fp_number_type *a, fp_number_type *b,
                                               fp_number_type *tmp)
{
    unsigned long long prod;
    unsigned int high;
    unsigned int low;

    if (isnan(a)) {
        a->sign = a->sign != b->sign;
        return a;
    }
    if (isnan(b)) {
        b->sign = a->sign != b->sign;
        return b;
    }
    if (isinf(a)) {
        if (iszero(b))
            return nan();
        a->sign = a->sign != b->sign;
        return a;
    }
    if (isinf(b)) {
        if (iszero(a))
            return nan();
        b->sign = a->sign != b->sign;
        return b;
    }
    if (iszero(a)) {
        a->sign = a->sign != b->sign;
        return a;
    }
    if (iszero(b)) {
        b->sign = a->sign != b->sign;
        return b;
    }

    tmp->normal_exp = a->normal_exp + b->normal_exp + 2;
    tmp->sign = a->sign != b->sign;

    /* the product of the two fractions is a 64 bit number */
    prod = (unsigned long long)a->fraction * b->fraction;
    high = prod >> 32;
    low = prod;

    while (high >= IMPLICIT_2) {
        tmp->normal_exp++;
        if (high & 1) {
            low >>= 1;
            low |= IMPLICIT_2;
        }
        high >>= 1;
    }

    while (high < IMPLICIT_1) {
        high <<= 1;
        if (low & IMPLICIT_2)
            high |= 1;
        tmp->normal_exp--;
        low <<= 1;
    }

    if ((high & 0x7F) == 0x40) {
        if (high & 0x80) {
            /* exactly half way, so round to even */
            high += 0x40;
        } else if (low) {
            /* not really half way, there are more bits below */
            high += 0x40;
        }
    }

    tmp->fraction = high;
    tmp->class = CLASS_NUMBER;
    return tmp;
}

float fpmul(float arg_a, float arg_b)
{
    fp_number_type a;
    fp_number_type b;
    fp_number_type tmp;
    float au;
    float bu;

    au = arg_a;
    bu = arg_b;
    __unpack_f(&au, &a);
    __unpack_f(&bu, &b);
    return __pack_f(_fpmul_parts(&a, &b, &tmp));
}

static __inline__ fp_number_type *_fpdiv_parts(fp_number_type *a, fp_number_type *b)
{
    unsigned int bit;
    unsigned int numerator;
    unsigned int denominator;
    unsigned int quotient;

    if (isnan(a))
        return a;
    if (isnan(b))
        return b;

    a->sign = a->sign ^ b->sign;

    if (isinf(a) || iszero(a)) {
        /* dividing two infinities, or two zeroes, yields a NaN */
        if (a->class == b->class)
            return nan();
        return a;
    }

    if (isinf(b)) {
        a->fraction = 0;
        a->normal_exp = 0;
        return a;
    }
    if (iszero(b)) {
        a->class = CLASS_INFINITY;
        return a;
    }

    a->normal_exp = a->normal_exp - b->normal_exp;
    numerator = a->fraction;
    denominator = b->fraction;

    if (numerator < denominator) {
        /* the quotient would be less than one */
        numerator *= 2;
        a->normal_exp--;
    }

    bit = IMPLICIT_1;
    quotient = 0;
    while (bit) {
        if (numerator >= denominator) {
            quotient |= bit;
            numerator -= denominator;
        }
        bit >>= 1;
        numerator *= 2;
    }

    if ((quotient & 0x7F) == 0x40) {
        if (quotient & 0x80) {
            /* exactly half way, so round to even */
            quotient += 0x40;
        } else if (numerator) {
            /* not really half way, there are more bits below */
            quotient += 0x40;
        }
    }

    a->fraction = quotient;
    return a;
}

float fpdiv(float arg_a, float arg_b)
{
    fp_number_type a;
    fp_number_type b;
    float au;
    float bu;

    au = arg_a;
    bu = arg_b;
    __unpack_f(&au, &a);
    __unpack_f(&bu, &b);
    return __pack_f(_fpdiv_parts(&a, &b));
}

int __fpcmp_parts_f(fp_number_type *a, fp_number_type *b)
{
    unsigned int at = a->class;
    unsigned int bt;
    if (at < 2) {
        return 1;
    }
    bt = b->class;
    if (bt < 2) {
        return 1;
    }
    if ((at ^ 4) == 0) {
        if ((bt ^ 4) == 0) {
            return b->sign - a->sign;
        }
        return a->sign ? -1 : 1;
    }
    if ((bt ^ 4) == 0) {
        if (b->sign == 0) {
            return -1;
        }
        return 1;
    }
    if ((at ^ 2) == 0) {
        if ((bt ^ 2) == 0) {
            return 0;
        }
        if (b->sign != 0) {
            return 1;
        }
        return -1;
    }
    if ((bt ^ 2) == 0) {
        return a->sign ? -1 : 1;
    }
    {
        int asign = a->sign;
        int bsign = b->sign;
        if (asign != bsign) {
            return asign ? -1 : 1;
        }
        {
            int aexp = a->normal_exp;
            int bexp = b->normal_exp;
            if (bexp < aexp) {
                return asign ? -1 : 1;
            }
            if (aexp < bexp) {
                return asign ? 1 : -1;
            }
            {
                unsigned int afrac = a->fraction;
                unsigned int bfrac = b->fraction;
                if (bfrac < afrac) {
                    return asign ? -1 : 1;
                }
                if (afrac < bfrac) {
                    return asign ? 1 : -1;
                }
                return 0;
            }
        }
    }
}

int fpcmp(float arg_a, float arg_b)
{
    fp_number_type a;
    fp_number_type b;

    __unpack_f(&arg_a, &a);
    __unpack_f(&arg_b, &b);
    return __fpcmp_parts_f(&a, &b);
}

float sitofp(int arg_a)
{
    fp_number_type in;

    in.class = CLASS_NUMBER;
    in.sign = arg_a < 0;
    if (!arg_a) {
        in.class = CLASS_ZERO;
    } else {
        in.normal_exp = 30;
        if (in.sign) {
            /* there is no positive representation of the most negative int */
            if (arg_a == (-0x7FFFFFFF) - 1) {
                return (float)((-0x7FFFFFFF) - 1);
            }
            in.fraction = -arg_a;
        } else {
            in.fraction = arg_a;
        }
        while (in.fraction < IMPLICIT_1) {
            in.fraction <<= 1;
            in.normal_exp--;
        }
    }
    return __pack_f(&in);
}

int fptosi(float arg_a)
{
    fp_number_type a;
    float au[4];
    int tmp;

    au[0] = arg_a;
    __unpack_f(au, &a);

    if (iszero(&a)) {
        return 0;
    }
    if (isnan(&a)) {
        return 0;
    }
    if (isinf(&a)) {
        return a.sign ? (-0x7FFFFFFF) - 1 : 0x7FFFFFFF;
    }
    if (a.normal_exp < 0) {
        return 0;
    }
    if (a.normal_exp > 30) {
        return a.sign ? (-0x7FFFFFFF) - 1 : 0x7FFFFFFF;
    }
    tmp = a.fraction >> (30 - a.normal_exp);
    return a.sign ? -tmp : tmp;
}

unsigned int fptoui(float arg_a)
{
    fp_number_type a;
    float au[4];

    au[0] = arg_a;
    __unpack_f(au, &a);

    if (iszero(&a)) {
        return 0;
    }
    if (isnan(&a)) {
        return 0;
    }
    /* it is a negative number */
    if (a.sign) {
        return 0;
    }
    if (isinf(&a)) {
        return 0xFFFFFFFF;
    }
    if (a.normal_exp < 0) {
        return 0;
    }
    if (a.normal_exp > 31) {
        return 0xFFFFFFFF;
    }
    if (a.normal_exp > 30) {
        return a.fraction << (a.normal_exp - 30);
    }
    return a.fraction >> (30 - a.normal_exp);
}

float __negsf2(float arg_a)
{
    fp_number_type o;
    float in[4];
    in[0] = arg_a;
    __unpack_f(in, &o);
    o.sign = (o.sign == 0);
    return __pack_f(&o);
}

void __make_fp(int class, int sign, int exp, int frac)
{
    fp_number_type buf;
    buf.class = class;
    buf.sign = sign;
    buf.normal_exp = exp;
    buf.fraction = frac;
    __pack_f(&buf);
}

int fptodp(float arg_a)
{
    fp_number_type in;
    float local1[4];
    long long a3_val;
    local1[0] = arg_a;
    __unpack_f(local1, &in);
    a3_val = (long long)in.fraction << 32;
    return __make_dp(in.class, in.sign, in.normal_exp,
                     (long long)((unsigned long long)a3_val >> 2));
}
