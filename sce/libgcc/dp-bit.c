/* libgcc.a member dp-bit.o */
#include "libgcc2.h"

#define CLASS_SNAN 0
#define CLASS_QNAN 1
#define CLASS_ZERO 2
#define CLASS_NUMBER 3
#define CLASS_INFINITY 4
/* libgcc's fp-bit.c reads a zero exponent as zero when NO_DENORMALS is
   defined (`if (fraction == 0 || 1)` under #ifdef NO_DENORMALS in the public
   GCC source), and this build defined it: the EE FPU has no denormals.
   Where the developers' build defined the macro (a target makefile fragment)
   is not known; the define is here. */
#define NO_DENORMALS

/* The double number in unpacked form.  FRAC_NBITS is 64 and NGARDS is 8, so the
   implicit one sits at bit 60 and the fraction is a 64-bit field at 0x10. */
typedef struct {
    unsigned int class;
    int sign;
    int normal_exp;
    int pad;

    /* libgcc's fp_number_type keeps the fraction in a union of the 64-bit
       value and its two 32-bit halves (public GCC fp-bit.c). */
    union {
        unsigned long long ll;
        unsigned int l[2];
    } fraction;
} fp_number_type_d;

static __inline__ int isnan_d(fp_number_type_d *x)
{
    return x->class == CLASS_SNAN || x->class == CLASS_QNAN;
}

static __inline__ int isinf_d(fp_number_type_d *x)
{
    return x->class == CLASS_INFINITY;
}

static __inline__ int iszero_d(fp_number_type_d *x)
{
    return x->class == CLASS_ZERO;
}

/* fp-bit.c's nan(): the NaN this build hands back for inf - inf; its static is the
   member's whole .bss, VMA 0x736170 (USA 0x71EB70, Aug-6 prototype 0x7181F0, Jun-26 demo
   0x76CBF0), 0x18 B. */
static __inline__ fp_number_type_d *nan_d(void)
{
    static fp_number_type_d thenan;

    return &thenan;
}

typedef union {
    struct {
        unsigned long long fraction : 52;
        unsigned int exp : 11;
        unsigned int sign : 1;
    } bits;

    long long value;
} FLO_union_type_d;

long long __pack_d(void *s)
{
    fp_number_type_d *src = s;
    FLO_union_type_d dst;
    unsigned long long fraction = src->fraction.ll;
    int sign = src->sign;
    int exp = 0;

    if (isnan_d(src)) {
        exp = 0x7FF;
        fraction |= 0x8000000000000ULL;
    } else if (isinf_d(src)) {
        exp = 0x7FF;
        fraction = 0;
    } else if (iszero_d(src)) {
        exp = 0;
        fraction = 0;
    } else if (fraction == 0) {
        exp = 0;
    } else {
        if (src->normal_exp < -1022) {
            /* the exponent is too low for a normal number: the integer part
               goes to zero and the fraction keeps what bits survive */
            int shift = -1022 - src->normal_exp;
            if (shift > 56) {
                exp = 0;
                fraction = 0;
            } else {
                exp = 0;
                fraction >>= shift;
            }
            fraction >>= 8;
        } else if (src->normal_exp > 1023) {
            exp = 0x7FF;
            fraction = 0;
        } else {
            exp = src->normal_exp + 1023;
            /* halfway between two numbers: round to the one whose low bit
               is zero, otherwise add a one to the guards to round up */
            if ((fraction & 0xFF) == 0x80) {
                if (fraction & 0x100) {
                    fraction += 0x80;
                }
            } else {
                fraction += 0x7F;
            }
            if (fraction >= 0x2000000000000000ULL) {
                fraction >>= 1;
                exp += 1;
            }
            fraction >>= 8;
        }
    }
    dst.bits.fraction = fraction;
    dst.bits.exp = exp;
    dst.bits.sign = sign;
    return dst.value;
}

void __unpack_d(void *in, void *out)
{
    FLO_union_type_d src;
    fp_number_type_d *dst = out;
    unsigned long long fraction;
    int exp;
    int sign;

    src.value = *(long long *)in;
    fraction = src.bits.fraction;
    exp = src.bits.exp;
    sign = src.bits.sign;

    dst->sign = sign;
    if (exp == 0) {
        if (fraction == 0
#ifdef NO_DENORMALS
            || 1
#endif
        ) {
            dst->class = CLASS_ZERO;
        } else {
            /* a denormal has no implicit one: shift until it has one */
            dst->normal_exp = exp - 1023 + 1;
            fraction <<= 8;
            dst->class = CLASS_NUMBER;
            while (fraction < 0x1000000000000000ULL) {
                fraction <<= 1;
                dst->normal_exp--;
            }
            dst->fraction.ll = fraction;
        }
    } else if (exp == 0x7FF) {
        if (fraction == 0) {
            dst->class = CLASS_INFINITY;
        } else {
            if (fraction & 0x8000000000000ULL) {
                dst->class = CLASS_QNAN;
            } else {
                dst->class = CLASS_SNAN;
            }
            dst->fraction.ll = fraction;
        }
    } else {
        dst->normal_exp = exp - 1023;
        dst->class = CLASS_NUMBER;
        dst->fraction.ll = (fraction << 8) | 0x1000000000000000ULL;
    }
}

fp_number_type_d *_fpadd_parts(fp_number_type_d *a, fp_number_type_d *b, fp_number_type_d *tmp)
{
    long long tfraction;
    int a_normal_exp;
    int b_normal_exp;
    unsigned long long a_fraction;
    unsigned long long b_fraction;
    int diff;

    if (isnan_d(a))
        return a;
    if (isnan_d(b))
        return b;
    if (isinf_d(a)) {
        /* adding infinities with opposite signs yields a NaN */
        if (isinf_d(b) && a->sign != b->sign)
            return nan_d();
        return a;
    }
    if (isinf_d(b))
        return b;
    if (iszero_d(b)) {
        if (iszero_d(a)) {
            *tmp = *a;
            tmp->sign = a->sign & b->sign;
            return tmp;
        }
        return a;
    }
    if (iszero_d(a))
        return b;

    a_normal_exp = a->normal_exp;
    b_normal_exp = b->normal_exp;
    a_fraction = a->fraction.ll;
    b_fraction = b->fraction.ll;

    diff = a_normal_exp - b_normal_exp;
    if (diff < 0)
        diff = -diff;
    if (diff < 64) {
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
            tmp->fraction.ll = tfraction;
        } else {
            tmp->sign = 1;
            tmp->normal_exp = a_normal_exp;
            tmp->fraction.ll = -tfraction;
        }
        while (tmp->fraction.ll < 0x1000000000000000ULL && tmp->fraction.ll) {
            tmp->fraction.ll <<= 1;
            tmp->normal_exp--;
        }
    } else {
        tmp->sign = a->sign;
        tmp->normal_exp = a_normal_exp;
        tmp->fraction.ll = a_fraction + b_fraction;
    }
    tmp->class = CLASS_NUMBER;

    if (tmp->fraction.ll >= 0x2000000000000000ULL) {
        tmp->fraction.ll = (tmp->fraction.ll & 1) | (tmp->fraction.ll >> 1);
        tmp->normal_exp++;
    }
    return tmp;
}

void dpadd(long arg_a, long arg_b)
{
    fp_number_type_d x, y, z;

    __unpack_d(&arg_a, &x);
    __unpack_d(&arg_b, &y);
    __pack_d(_fpadd_parts(&x, &y, &z));
}

long long dpsub(long arg_a, long arg_b)
{
    fp_number_type_d x, y, z;

    __unpack_d(&arg_a, &x);
    __unpack_d(&arg_b, &y);
    y.sign ^= 1;
    return __pack_d(_fpadd_parts(&x, &y, &z));
}

static __inline__ fp_number_type_d *_fpmul_parts(fp_number_type_d *a, fp_number_type_d *b,
                                                 fp_number_type_d *tmp)
{
    unsigned long long low;
    unsigned long long high;
    unsigned long long nl;
    unsigned long long nh;
    unsigned long long ml;
    unsigned long long mh;
    unsigned long long pp_ll;
    unsigned long long pp_hl;
    unsigned long long pp_lh;
    unsigned long long pp_hh;
    unsigned long long ps;
    unsigned long long res0;
    unsigned long long res2;

    if (isnan_d(a)) {
        a->sign = a->sign != b->sign;
        return a;
    }
    if (isnan_d(b)) {
        b->sign = a->sign != b->sign;
        return b;
    }
    if (isinf_d(a)) {
        if (iszero_d(b)) {
            return nan_d();
        }
        a->sign = a->sign != b->sign;
        return a;
    }
    if (isinf_d(b)) {
        if (iszero_d(a)) {
            return nan_d();
        }
        b->sign = a->sign != b->sign;
        return b;
    }
    if (iszero_d(a)) {
        a->sign = a->sign != b->sign;
        return a;
    }
    if (iszero_d(b)) {
        b->sign = a->sign != b->sign;
        return b;
    }

    nl = a->fraction.ll & 0xFFFFFFFF;
    nh = a->fraction.ll >> 32;
    ml = b->fraction.ll & 0xFFFFFFFF;
    mh = b->fraction.ll >> 32;

    pp_ll = ml * nl;
    pp_hl = mh * nl;
    pp_lh = ml * nh;
    pp_hh = mh * nh;

    res2 = 0;
    ps = pp_hl + pp_lh;
    if (ps < pp_hl) {
        res2 += 1ULL << 32;
    }
    pp_hl = (ps << 32) & 0xFFFFFFFF00000000ULL;
    res0 = pp_ll + pp_hl;
    if (res0 < pp_ll) {
        res2++;
    }
    res2 += ((ps >> 32) & 0xFFFFFFFF) + pp_hh;
    high = res2;
    low = res0;

    tmp->normal_exp = a->normal_exp + b->normal_exp;
    tmp->sign = a->sign != b->sign;
    tmp->normal_exp += 4;

    while (high >= 0x2000000000000000ULL) {
        tmp->normal_exp++;
        if (high & 1) {
            low >>= 1;
            low |= 0x8000000000000000ULL;
        }
        high >>= 1;
    }
    while (high < 0x1000000000000000ULL) {
        high <<= 1;
        if (low & 0x8000000000000000ULL) {
            high |= 1;
        }
        tmp->normal_exp--;
        low <<= 1;
    }
    if ((high & 0xFF) == 0x80) {
        if (high & 0x100) {
            high += 0x80;
        } else if (low != 0) {
            high += 0x80;
        }
    }
    tmp->fraction.ll = high;
    tmp->class = CLASS_NUMBER;
    return tmp;
}

long long dpmul(long arg_a, long arg_b)
{
    fp_number_type_d x, y, z;

    __unpack_d(&arg_a, &x);
    __unpack_d(&arg_b, &y);
    return __pack_d(_fpmul_parts(&x, &y, &z));
}

void dpdiv(long arg_a, long arg_b)
{
    fp_number_type_d x, y, *p;

    void *r;
    unsigned long long m1, m2, bit, q;

    __unpack_d(&arg_a, &x);
    __unpack_d(&arg_b, &y);
    p = &x;
    if (x.class >= 2)
        goto op2check;
    r = &x;
    goto pack;
op2check:
    if (y.class < 2) {
        r = &y;
        goto pack;
    }
    x.sign = x.sign ^ y.sign;
    if ((x.class ^ 4) == 0)
        goto chk_same;
    if ((x.class ^ 2) != 0)
        goto op1_normal;
chk_same:
    r = &x;
    if (x.class != y.class)
        goto pack;
    r = nan_d();
    goto pack;
op1_normal:
    if ((y.class ^ 4) != 0)
        goto chk_zero;
    x.fraction.ll = 0;
    x.normal_exp = 0;
    r = &x;
    goto pack;
chk_zero:
    if ((y.class ^ 2) != 0)
        goto divide;
    x.class = 4;
    r = &x;
    goto pack;
divide:
    m1 = x.fraction.ll;
    m2 = y.fraction.ll;
    x.normal_exp = x.normal_exp - y.normal_exp;
    if (m1 < m2) {
        x.normal_exp = x.normal_exp - 1;
        m1 <<= 1;
    }
    q = 0;
    bit = 0x1000000000000000ULL;
    do {
        if (m1 >= m2) {
            q |= bit;
            m1 -= m2;
        }
        bit >>= 1;
        m1 <<= 1;
    } while (bit != 0);
    if ((q & 0xFF) != 0x80) {
        p->fraction.ll = q;
    } else {
        if (q & 0x100) {
            q += 0x80;
        } else {
            q = (m1 != 0) ? (q + 0x80) : q;
        }
        p->fraction.ll = q;
    }
    r = p;
pack:
    __pack_d(r);
}

int __fpcmp_parts_d(fp_number_type_d *a, fp_number_type_d *b)
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
        int af4 = a->sign;
        int bf4 = b->sign;
        if (af4 != bf4) {
            return af4 ? -1 : 1;
        }
        {
            int af8 = a->normal_exp;
            int bf8 = b->normal_exp;
            if (bf8 < af8) {
                return af4 ? -1 : 1;
            }
            if (af8 < bf8) {
                return af4 ? 1 : -1;
            }
            {
                unsigned long long af10 = a->fraction.ll;
                unsigned long long bf10 = b->fraction.ll;
                if (bf10 < af10) {
                    return af4 ? -1 : 1;
                }
                if (af10 < bf10) {
                    return af4 ? 1 : -1;
                }
                return 0;
            }
        }
    }
}

int dpcmp(long arg_a, long arg_b)
{
    fp_number_type_d x, y;

    __unpack_d(&arg_a, &x);
    __unpack_d(&arg_b, &y);
    return __fpcmp_parts_d(&x, &y);
}

long long litodp(int arg_a)
{
    fp_number_type_d in;

    in.class = CLASS_NUMBER;
    in.sign = arg_a < 0;
    if (!arg_a) {
        in.class = CLASS_ZERO;
    } else {
        in.normal_exp = 60;
        if (in.sign) {
            /* there is no positive representation of the most negative int;
               the literal is the IEEE-754 double -2147483648.0 */
            if (arg_a == (-0x7FFFFFFF) - 1) {
                return 0xC1E0000000000000LL;
            }
            in.fraction.ll = -arg_a;
        } else {
            in.fraction.ll = arg_a;
        }
        while (in.fraction.ll < 0x1000000000000000ULL) {
            in.fraction.ll <<= 1;
            in.normal_exp--;
        }
    }
    return __pack_d(&in);
}

int dptoli(long arg_a)
{
    fp_number_type_d a;
    int tmp;

    __unpack_d(&arg_a, &a);

    if (iszero_d(&a)) {
        return 0;
    }
    if (isnan_d(&a)) {
        return 0;
    }
    if (isinf_d(&a)) {
        return a.sign ? (-0x7FFFFFFF) - 1 : 0x7FFFFFFF;
    }
    if (a.normal_exp < 0) {
        return 0;
    }
    if (a.normal_exp > 30) {
        return a.sign ? (-0x7FFFFFFF) - 1 : 0x7FFFFFFF;
    }
    tmp = a.fraction.ll >> (60 - a.normal_exp);
    return a.sign ? -tmp : tmp;
}

unsigned int dptoul(long arg_a)
{
    fp_number_type_d a;

    __unpack_d(&arg_a, &a);

    if (iszero_d(&a)) {
        return 0;
    }
    if (isnan_d(&a)) {
        return 0;
    }
    if (a.sign) {
        return 0;
    }
    if (isinf_d(&a)) {
        return 0xFFFFFFFF;
    }
    if (a.normal_exp < 0) {
        return 0;
    }
    if (a.normal_exp > 31) {
        return 0xFFFFFFFF;
    }
    if (a.normal_exp > 60) {
        return a.fraction.ll << (a.normal_exp - 60);
    }
    return a.fraction.ll >> (60 - a.normal_exp);
}

void __negdf2(long long arg_a)
{
    fp_number_type_d s;

    long long t = arg_a;
    __unpack_d(&t, &s);
    s.sign = (s.sign == 0);
    __pack_d(&s);
}

int __make_dp(int class, int sign, int exp, long long frac)
{
    fp_number_type_d s;

    s.class = class;
    s.sign = sign;
    s.normal_exp = exp;
    s.fraction.ll = frac;
    __pack_d(&s);
}

float dptofp(double arg_a)
{
    fp_number_type_d buf;

    long long m;
    int hi, t;
    __unpack_d(&arg_a, &buf);
    m = buf.fraction.ll;
    hi = (int)(m >> 30);
    t = hi | 1;
    if ((m & 0x3FFFFFFF) == 0)
        t = hi;
    __make_fp(buf.class, buf.sign, buf.normal_exp, t);
}
