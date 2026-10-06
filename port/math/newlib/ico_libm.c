/*
 * port/math/newlib/ico_libm.c
 *
 * Host copies of the newlib libm functions the game calls, so their results
 * are the PS2's on every host: ico_atan2f, ico_acosf, ico_asinf, ico_sinf,
 * ico_cosf, ico_fmodf, ico_sqrtf. Each section names the member of the
 * EE's libm.a it was copied from (this repository's reconstructions under
 * sce/libm/). They are fdlibm's single-precision code as newlib carries it
 * (newlib/libm/math, newlib/libm/common), whose notice is:
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * (newlib's float versions add: "Conversion to float by Ian Lance Taylor,
 * Cygnus Support, ian@cygnus.com.")
 *
 * Everything but the public ico_* entry points is static, so nothing here
 * can collide with the host libm, and nothing here calls it.
 *
 * Kept from the EE copies: every operation, its order, its operand types
 * (float throughout) and every constant. Changed, without changing any
 * result:
 *   - shifts that can move a bit into or past the sign bit are done on
 *     uint32_t (ee-gcc's shifts wrap; in ISO C they are undefined);
 *   - the wrappers return the X/Open results without filling a struct
 *     exception, calling matherr or setting errno (see "wrappers" below).
 * Changed so the host compiler cannot change a result:
 *   - expressions of constants only. ee-gcc folded most of them at compile
 *     time in round-to-nearest; the EE then never computes them in its
 *     round-toward-zero mode. A host compiler may or may not fold the same
 *     expression (it depends on the optimisation level), so this copy spells
 *     the folded ones as the literal the EE object holds and forces the one
 *     the EE computes at run time (atanf's huge-argument result) to be
 *     computed at run time. The section of each says what the EE object
 *     shows (the objects under build/sce/libm/, mips-linux-gnu-objdump -d).
 *
 * Changed to give the EE's result: every float addition and subtraction in
 * sinf (__kernel_sinf, __kernel_cosf, __ieee754_rem_pio2f up to the
 * large-argument path), atanf, atan2f, acosf and asinf goes through
 * ps2_add/ps2_sub (port/math/ps2float.h), the EE adder, which drops the
 * smaller operand's bits below one guard bit; IEEE round toward zero does
 * not. sinf's table argument just under pi/2 is the
 * case that shows it: 1 - 4.7e-9 is 1.0 on the EE and 0x3F7FFFFF under IEEE
 * RTZ. Left as IEEE: __kernel_rem_pio2f and floorf (sinf's arguments above
 * 2^7 * pi/2 only, which the game never passes), fmodf (exact).
 *
 * Not reproduced: the EE FPU's own division (the copies use the host's
 * IEEE division, under the simulation's rounding mode) and its lack of
 * infinities and NaNs, except in acosf's and asinf's domain errors
 * (ico_domain_error). The rest is package 1A's port/math helpers' concern;
 * these functions see only finite inputs in the game.
 */
#include <stdint.h>

#include "ico_newlib.h"
#include "../ps2float.h"

#if defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ != 0
#error "port/math/newlib needs float maths without excess precision (x86: -msse2 -mfpmath=sse)"
#endif

typedef union {
    float value;
    uint32_t word;
} ico_ieee_float_shape_type;

#define GET_FLOAT_WORD(i, d)                                                                       \
    do {                                                                                           \
        ico_ieee_float_shape_type gf_u;                                                            \
        gf_u.value = (d);                                                                          \
        (i) = gf_u.word;                                                                           \
    } while (0)
#define SET_FLOAT_WORD(d, i)                                                                       \
    do {                                                                                           \
        ico_ieee_float_shape_type sf_u;                                                            \
        sf_u.word = (uint32_t)(i);                                                                 \
        (d) = sf_u.value;                                                                          \
    } while (0)

/* Left shifts that may reach the sign bit, done unsigned. */
#define ICO_SHL(v, n) ((int32_t)((uint32_t)(v) << (n)))

static float ico___kernel_sinf(float x, float y, int iy);
static float ico___kernel_cosf(float x, float y);
static int ico___kernel_rem_pio2f(float *x, float *y, int e0, int nx, int prec,
                                  const int32_t *ipio2);

/* ---- sf_fabs.c (libm.a member sf_fabs.o) ------------------------------- */

static float ico_fabsf(float x)
{
    uint32_t ix;
    GET_FLOAT_WORD(ix, x);
    SET_FLOAT_WORD(x, ix & 0x7fffffff);
    return x;
}

/* ---- sf_isnan.c (libm.a member sf_isnan.o) ----------------------------- */

static int ico_isnanf(float x)
{
    int32_t hx;
    GET_FLOAT_WORD(hx, x);
    hx &= 0x7fffffff;
    hx = 0x7f800000 - hx;
    return (int)((uint32_t)hx >> 31);
}

/* ---- common/sf_copysign.c (libm.a member sf_copysign.o) ---------------- */

static float ico_copysignf(float x, float y)
{
    uint32_t ix, iy;
    GET_FLOAT_WORD(ix, x);
    GET_FLOAT_WORD(iy, y);
    SET_FLOAT_WORD(x, (ix & 0x7fffffff) | (iy & 0x80000000));
    return x;
}

/* ---- common/sf_scalbn.c (libm.a member sf_scalbn.o) -------------------- */

static const float ico_scalbn_two25 = 3.355443200e+07f, /* 0x4c000000 */
    ico_scalbn_twom25 = 2.9802322388e-08f,              /* 0x33000000 */
    ico_scalbn_huge = 1.0e+30f, ico_scalbn_tiny = 1.0e-30f;

static float ico_scalbnf(float x, int n)
{
    int32_t k, ix;

    GET_FLOAT_WORD(ix, x);
    k = (ix & 0x7f800000) >> 23; /* extract exponent */
    if (k == 0) {                /* 0 or subnormal x */
        if ((ix & 0x7fffffff) == 0)
            return x; /* +-0 */
        x *= ico_scalbn_two25;
        GET_FLOAT_WORD(ix, x);
        k = ((ix & 0x7f800000) >> 23) - 25;
        if (n < -50000)
            return ico_scalbn_tiny * x; /*underflow*/
    }
    if (k == 0xff)
        return x + x; /* NaN or Inf */
    k = k + n;
    if (k > 0xfe)
        return ico_scalbn_huge * ico_copysignf(ico_scalbn_huge, x); /* overflow  */
    if (k > 0) {                                                    /* normal result */
        SET_FLOAT_WORD(x, (ix & 0x807fffff) | ICO_SHL(k, 23));
        return x;
    }
    if (k <= -25) {
        if (n > 50000) /* in case integer overflow in n+k */
            return ico_scalbn_huge * ico_copysignf(ico_scalbn_huge, x); /*overflow*/
        else
            return ico_scalbn_tiny * ico_copysignf(ico_scalbn_tiny, x); /*underflow*/
    }
    k += 25; /* subnormal result */
    SET_FLOAT_WORD(x, (ix & 0x807fffff) | ICO_SHL(k, 23));
    return x * ico_scalbn_twom25;
}

/* ---- sf_floor.c (libm.a member sf_floor.o) ----------------------------- */

static const float ico_floor_huge = 1.0e30f;

static float ico_floorf(float x)
{
    int32_t i0, j0;
    uint32_t i;

    GET_FLOAT_WORD(i0, x);
    j0 = ((i0 >> 23) & 0xff) - 0x7f;
    if (j0 < 23) {
        if (j0 < 0) {                              /* raise inexact if x != 0 */
            if (ico_floor_huge + x > (float)0.0) { /* return 0*sign(x) if |x|<1 */
                if (i0 >= 0) {
                    i0 = 0;
                } else if ((i0 & 0x7fffffff) != 0) {
                    i0 = (int32_t)0xbf800000;
                }
            }
        } else {
            i = (0x007fffff) >> j0;
            if (((uint32_t)i0 & i) == 0)
                return x;                          /* x is integral */
            if (ico_floor_huge + x > (float)0.0) { /* raise inexact flag */
                if (i0 < 0)
                    i0 += (0x00800000) >> j0;
                i0 = (int32_t)((uint32_t)i0 & (~i));
            }
        }
    } else {
        if (j0 == 0x80)
            return x + x; /* inf or NaN */
        else
            return x; /* x is integral */
    }
    SET_FLOAT_WORD(x, i0);
    return x;
}

/* ---- ef_sqrt.c (libm.a member ef_sqrt.o) --------------------------------
 * fdlibm's bit-by-bit square root, kept as the EE runs it.
 *
 * Rounding: fdlibm decides the rounding of the last bit with
 *     z = one - tiny; if (z >= one) { z = one + tiny;
 *                                     if (z > one) q += 2; else q += (q & 1); }
 * ee-gcc folded those constant expressions in round-to-nearest, so the EE
 * object (build/sce/libm/math/ef_sqrt.o, 0x110-0x118: beqz a1 / andi
 * v0,a0,1 / addu a0,a0,v0) only does "if (ix != 0) q += q & 1", whatever
 * the FPU's rounding mode. Evaluated at run time under the simulation's
 * round-toward-zero, the probe would take the other branch, so this copy
 * spells out the folded form. */

static float ico___ieee754_sqrtf(float x)
{
    int32_t ix, s, q, m, t, i;
    uint32_t r;
    float z;

    GET_FLOAT_WORD(ix, x);
    if ((ix & 0x7F800000) == 0x7F800000) {
        return x * x + x;
    }
    m = ix >> 23;
    if (ix <= 0) {
        if ((ix & 0x7FFFFFFF) == 0) {
            return x;
        }
        if (ix < 0) {
            return (x - x) / (x - x);
        }
    }
    if (m == 0) {
        for (i = 0; (ix & 0x800000) == 0; i++) {
            ix = ICO_SHL(ix, 1);
        }
        m -= i - 1;
    }
    m -= 0x7F;
    ix = (ix & 0x7FFFFF) | 0x800000;
    ix = ICO_SHL(ix, m & 1);
    m >>= 1;
    ix = ICO_SHL(ix, 1);
    q = s = 0;
    r = 0x1000000;
    do {
        t = s + (int32_t)r;
        if (t <= ix) {
            s = t + (int32_t)r;
            ix -= t;
            q += (int32_t)r;
        }
        r >>= 1;
        ix = ICO_SHL(ix, 1);
    } while (r != 0);
    if (ix != 0) {
        q += (q & 1); /* the folded rounding probe, see above */
    }
    ix = (q >> 1) + 0x3F000000;
    ix += ICO_SHL(m, 23);
    SET_FLOAT_WORD(z, ix);
    return z;
}

/* ---- kf_sin.c (libm.a member kf_sin.o) --------------------------------- */

static const float ico_ksin_half = 5.0000000000e-01f, /* 0x3f000000 */
    ico_ksin_S1 = -1.6666667163e-01f,                 /* 0xbe2aaaab */
    ico_ksin_S2 = 8.3333337680e-03f,                  /* 0x3c088889 */
    ico_ksin_S3 = -1.9841270114e-04f,                 /* 0xb9500d01 */
    ico_ksin_S4 = 2.7557314297e-06f,                  /* 0x3638ef1b */
    ico_ksin_S5 = -2.5050759689e-08f,                 /* 0xb2d72f34 */
    ico_ksin_S6 = 1.5896910177e-10f;                  /* 0x2f2ec9d3 */

static float ico___kernel_sinf(float x, float y, int iy)
{
    float z, r, v;
    int32_t ix;

    GET_FLOAT_WORD(ix, x);
    ix &= 0x7fffffff;
    if (ix < 0x32000000) {
        if ((int)x == 0) {
            return x;
        }
    }
    z = x * x;
    v = z * x;
    r = ps2_add(ico_ksin_S2,
                z * ps2_add(ico_ksin_S3,
                            z * ps2_add(ico_ksin_S4, z * ps2_add(ico_ksin_S5, z * ico_ksin_S6))));
    if (iy == 0) {
        return ps2_add(x, v * ps2_add(ico_ksin_S1, z * r));
    }
    return ps2_sub(x, ps2_sub(ps2_sub(z * ps2_sub(ico_ksin_half * y, v * r), y), v * ico_ksin_S1));
}

/* ---- kf_cos.c (libm.a member kf_cos.o) --------------------------------- */

static const float ico_kcos_one = 1.0000000000e+00f, /* 0x3f800000 */
    ico_kcos_C1 = 4.1666667908e-02f,                 /* 0x3d2aaaab */
    ico_kcos_C2 = -1.3888889225e-03f,                /* 0xbab60b61 */
    ico_kcos_C3 = 2.4801587642e-05f,                 /* 0x37d00d01 */
    ico_kcos_C4 = -2.7557314297e-07f,                /* 0xb493f27c */
    ico_kcos_C5 = 2.0875723372e-09f,                 /* 0x310f74f6 */
    ico_kcos_C6 = -1.1359647598e-11f;                /* 0xad47d74e */

static float ico___kernel_cosf(float x, float y)
{
    float a, hz, z, r, qx;
    int32_t ix;

    GET_FLOAT_WORD(ix, x);
    ix &= 0x7fffffff;
    if (ix < 0x32000000) {
        if ((int)x == 0) {
            return ico_kcos_one;
        }
    }
    z = x * x;
    r = z *
        ps2_add(ico_kcos_C1,
                z * ps2_add(ico_kcos_C2,
                            z * ps2_add(ico_kcos_C3,
                                        z * ps2_add(ico_kcos_C4,
                                                    z * ps2_add(ico_kcos_C5, z * ico_kcos_C6)))));
    if (ix < 0x3e99999a) {
        return ps2_sub(ico_kcos_one, ps2_sub((float)0.5 * z, ps2_sub(z * r, x * y)));
    } else {
        if (ix > 0x3f480000) {
            qx = (float)0.28125;
        } else {
            SET_FLOAT_WORD(qx, ix - 0x01000000);
        }
        hz = ps2_sub((float)0.5 * z, qx);
        a = ps2_sub(ico_kcos_one, qx);
        return ps2_sub(a, ps2_sub(hz, ps2_sub(z * r, x * y)));
    }
}

/* ---- kf_rem_pio2.c (libm.a member kf_rem_pio2.o) ----------------------- */

static const int32_t ico_krem_init_jk[] = {
    4,
    7,
    9,
};

static const float ico_krem_PIo2[] = {
    1.5703125f,
    0.000457763671875f,
    2.5987625122070312e-05f,
    7.543712854385376e-08f,
    6.002665031701326e-11f,
    7.389644451905042e-13f,
    5.384581669432009e-15f,
    5.637851296924623e-18f,
    8.300922883092143e-20f,
    3.2756352257099896e-22f,
    6.333101564859118e-25f,
};

static const float ico_krem_zero = 0.0f, ico_krem_one = 1.0f,
                   ico_krem_two8 = 2.5600000000e+02f, /* 0x43800000 */
    ico_krem_twon8 = 3.9062500000e-03f;               /* 0x3b800000 */

/* gcc -m32 -O2 reports fq[0] (prec 1 and 2) as maybe uninitialised; the
   loop before the switch always fills fq[0..jz], and jz >= 0. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
static int ico___kernel_rem_pio2f(float *x, float *y, int e0, int nx, int prec,
                                  const int32_t *ipio2)
{
    int32_t jz, jx, jv, jp, jk, carry, n, iq[20], i, j, k, m, q0, ih;
    float z, fw, f[20], fq[20], q[20];

    /* initialize jk*/
    jk = ico_krem_init_jk[prec];
    jp = jk;

    /* determine jx,jv,q0, note that 3>q0 */
    jx = nx - 1;
    jv = (e0 - 3) / 8;
    if (jv < 0)
        jv = 0;
    q0 = e0 - 8 * (jv + 1);

    /* set up f[0] to f[jx+jk] where f[jx+jk] = ipio2[jv+jk] */
    j = jv - jx;
    m = jx + jk;
    for (i = 0; i <= m; i++, j++)
        f[i] = (j < 0) ? ico_krem_zero : (float)ipio2[j];

    /* compute q[0],q[1],...q[jk] */
    for (i = 0; i <= jk; i++) {
        for (j = 0, fw = 0.0f; j <= jx; j++)
            fw += x[j] * f[jx + i - j];
        q[i] = fw;
    }

    jz = jk;
recompute:
    /* distill q[] into iq[] reversingly */
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
        fw = (float)((int32_t)(ico_krem_twon8 * z));
        iq[i] = (int32_t)(z - ico_krem_two8 * fw);
        z = q[j - 1] + fw;
    }

    /* compute n */
    z = ico_scalbnf(z, q0);             /* actual value of z */
    z -= 8.0f * ico_floorf(z * 0.125f); /* trim off integer >= 8 */
    n = (int32_t)z;
    z -= (float)n;
    ih = 0;
    if (q0 > 0) { /* need iq[jz-1] to determine n */
        i = (iq[jz - 1] >> (8 - q0));
        n += i;
        iq[jz - 1] -= ICO_SHL(i, 8 - q0);
        ih = iq[jz - 1] >> (7 - q0);
    } else if (q0 == 0)
        ih = iq[jz - 1] >> 8;
    else if (z >= 0.5f)
        ih = 2;

    if (ih > 0) { /* q > 0.5 */
        n += 1;
        carry = 0;
        for (i = 0; i < jz; i++) { /* compute 1-q */
            j = iq[i];
            if (carry == 0) {
                if (j != 0) {
                    carry = 1;
                    iq[i] = 0x100 - j;
                }
            } else
                iq[i] = 0xff - j;
        }
        if (q0 > 0) { /* rare case: chance is 1 in 12 */
            switch (q0) {
            case 1:
                iq[jz - 1] &= 0x7f;
                break;
            case 2:
                iq[jz - 1] &= 0x3f;
                break;
            }
        }
        if (ih == 2) {
            z = ico_krem_one - z;
            if (carry != 0)
                z -= ico_scalbnf(ico_krem_one, q0);
        }
    }

    /* check if recomputation is needed */
    if (z == ico_krem_zero) {
        j = 0;
        for (i = jz - 1; i >= jk; i--)
            j |= iq[i];
        if (j == 0) { /* need recomputation */
            for (k = 1; iq[jk - k] == 0; k++)
                ; /* k = no. of terms needed */

            for (i = jz + 1; i <= jz + k; i++) { /* add q[jz+1] to q[jz+k] */
                f[jx + i] = (float)ipio2[jv + i];
                for (j = 0, fw = 0.0f; j <= jx; j++)
                    fw += x[j] * f[jx + i - j];
                q[i] = fw;
            }
            jz += k;
            goto recompute;
        }
    }

    /* chop off zero terms */
    if (z == ico_krem_zero) {
        jz -= 1;
        q0 -= 8;
        while (iq[jz] == 0) {
            jz--;
            q0 -= 8;
        }
    } else { /* break z into 8-bit if necessary */
        z = ico_scalbnf(z, -q0);
        if (z >= ico_krem_two8) {
            fw = (float)((int32_t)(ico_krem_twon8 * z));
            iq[jz] = (int32_t)(z - ico_krem_two8 * fw);
            jz += 1;
            q0 += 8;
            iq[jz] = (int32_t)fw;
        } else
            iq[jz] = (int32_t)z;
    }

    /* convert integer "bit" chunk to floating-point value */
    fw = ico_scalbnf(ico_krem_one, q0);
    for (i = jz; i >= 0; i--) {
        q[i] = fw * (float)iq[i];
        fw *= ico_krem_twon8;
    }

    /* compute PIo2[0,...,jp]*q[jz,...,0] */
    for (i = jz; i >= 0; i--) {
        for (fw = 0.0f, k = 0; k <= jp && k <= jz - i; k++)
            fw += ico_krem_PIo2[k] * q[i + k];
        fq[jz - i] = fw;
    }

    /* compress fq[] into y[] */
    switch (prec) {
    case 0:
        fw = 0.0f;
        for (i = jz; i >= 0; i--)
            fw += fq[i];
        y[0] = (ih == 0) ? fw : -fw;
        break;
    case 1:
    case 2:
        fw = 0.0f;
        for (i = jz; i >= 0; i--)
            fw += fq[i];
        y[0] = (ih == 0) ? fw : -fw;
        fw = fq[0] - fw;
        for (i = 1; i <= jz; i++)
            fw += fq[i];
        y[1] = (ih == 0) ? fw : -fw;
        break;
    case 3: /* painful */
        for (i = jz; i > 0; i--) {
            fw = fq[i - 1] + fq[i];
            fq[i] += fq[i - 1] - fw;
            fq[i - 1] = fw;
        }
        for (i = jz; i > 1; i--) {
            fw = fq[i - 1] + fq[i];
            fq[i] += fq[i - 1] - fw;
            fq[i - 1] = fw;
        }
        for (fw = 0.0f, i = jz; i >= 2; i--)
            fw += fq[i];
        if (ih == 0) {
            y[0] = fq[0];
            y[1] = fq[1];
            y[2] = fw;
        } else {
            y[0] = -fq[0];
            y[1] = -fq[1];
            y[2] = -fw;
        }
    }
    return n & 7;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* ---- ef_rem_pio2.c (libm.a member ef_rem_pio2.o) ----------------------- */

/* the 24 bits of 2/pi the large-argument path hands to __kernel_rem_pio2f */
static const int32_t ico_rem_two_over_pi[] = {
    0xA2, 0xF9, 0x83, 0x6E, 0x4E, 0x44, 0x15, 0x29, 0xFC, 0x27, 0x57, 0xD1, 0xF5, 0x34, 0xDD, 0xC0,
    0xDB, 0x62, 0x95, 0x99, 0x3C, 0x43, 0x90, 0x41, 0xFE, 0x51, 0x63, 0xAB, 0xDE, 0xBB, 0xC5, 0x61,
    0xB7, 0x24, 0x6E, 0x3A, 0x42, 0x4D, 0xD2, 0xE0, 0x06, 0x49, 0x2E, 0xEA, 0x09, 0xD1, 0x92, 0x1C,
    0xFE, 0x1D, 0xEB, 0x1C, 0xB1, 0x29, 0xA7, 0x3E, 0xE8, 0x82, 0x35, 0xF5, 0x2E, 0xBB, 0x44, 0x84,
    0xE9, 0x9C, 0x70, 0x26, 0xB4, 0x5F, 0x7E, 0x41, 0x39, 0x91, 0xD6, 0x39, 0x83, 0x53, 0x39, 0xF4,
    0x9C, 0x84, 0x5F, 0x8B, 0xBD, 0xF9, 0x28, 0x3B, 0x1F, 0xF8, 0x97, 0xFF, 0xDE, 0x05, 0x98, 0x0F,
    0xEF, 0x2F, 0x11, 0x8B, 0x5A, 0x0A, 0x6D, 0x1F, 0x6D, 0x36, 0x7E, 0xCF, 0x27, 0xCB, 0x09, 0xB7,
    0x4F, 0x46, 0x3F, 0x66, 0x9E, 0x5F, 0xEA, 0x2D, 0x75, 0x27, 0xBA, 0xC7, 0xEB, 0xE5, 0xF1, 0x7B,
    0x3D, 0x07, 0x39, 0xF7, 0x8A, 0x52, 0x92, 0xEA, 0x6B, 0xFB, 0x5F, 0xB1, 0x1F, 0x8D, 0x5D, 0x08,
    0x56, 0x03, 0x30, 0x46, 0xFC, 0x7B, 0x6B, 0xAB, 0xF0, 0xCF, 0xBC, 0x20, 0x9A, 0xF4, 0x36, 0x1D,
    0xA9, 0xE3, 0x91, 0x61, 0x5E, 0xE6, 0x1B, 0x08, 0x65, 0x99, 0x85, 0x5F, 0x14, 0xA0, 0x68, 0x40,
    0x8D, 0xFF, 0xD8, 0x80, 0x4D, 0x73, 0x27, 0x31, 0x06, 0x06, 0x15, 0x56, 0xCA, 0x73, 0xA8, 0xC9,
    0x60, 0xE2, 0x7B, 0xC0, 0x8C, 0x6B,
};

/* the high words of the first 32 multiples of pi/2 */
static const int32_t ico_rem_npio2_hw[] = {
    0x3FC90F00, 0x40490F00, 0x4096CB00, 0x40C90F00, 0x40FB5300, 0x4116CB00, 0x412FED00, 0x41490F00,
    0x41623100, 0x417B5300, 0x418A3A00, 0x4196CB00, 0x41A35C00, 0x41AFED00, 0x41BC7E00, 0x41C90F00,
    0x41D5A000, 0x41E23100, 0x41EEC200, 0x41FB5300, 0x4203F200, 0x420A3A00, 0x42108300, 0x4216CB00,
    0x421D1400, 0x42235C00, 0x4229A500, 0x422FED00, 0x42363600, 0x423C7E00, 0x4242C700, 0x42490F00,
};

static const float ico_rem_zero = 0.0000000000e+00f, /* 0x00000000 */
    ico_rem_half = 5.0000000000e-01f,                /* 0x3F000000 */
    ico_rem_two8 = 2.5600000000e+02f,                /* 0x43800000 */
    ico_rem_invpio2 = 6.3661980629e-01f,             /* 0x3F22F984 */
    ico_rem_pio2_1 = 1.5707855225e+00f,              /* 0x3FC90F80 */
    ico_rem_pio2_1t = 1.0804334124e-05f,             /* 0x37354443 */
    ico_rem_pio2_2 = 1.0804273188e-05f,              /* 0x37354400 */
    ico_rem_pio2_2t = 6.0770999344e-11f,             /* 0x2E85A308 */
    ico_rem_pio2_3 = 6.0770943833e-11f,              /* 0x2E85A300 */
    ico_rem_pio2_3t = 6.1232342629e-17f;             /* 0x248D3132 */

static int32_t ico___ieee754_rem_pio2f(float x, float *y)
{
    float z, w, t, r, fn;
    float tx[3];
    int32_t e0, i, j, nx, n, ix, hx;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff;
    if (ix <= 0x3f490fd8) { /* |x| ~<= pi/4, no need for reduction */
        y[0] = x;
        y[1] = 0;
        return 0;
    }
    if (ix < 0x4016cbe4) { /* |x| < 3pi/4, special case with n = +-1 */
        if (hx > 0) {
            z = ps2_sub(x, ico_rem_pio2_1);
            if ((ix & 0xfffffff0) != 0x3fc90fd0) { /* 24+24 bit pi OK */
                y[0] = ps2_sub(z, ico_rem_pio2_1t);
                y[1] = ps2_sub(ps2_sub(z, y[0]), ico_rem_pio2_1t);
            } else { /* near pi/2, use 24+24+24 bit pi */
                z = ps2_sub(z, ico_rem_pio2_2);
                y[0] = ps2_sub(z, ico_rem_pio2_2t);
                y[1] = ps2_sub(ps2_sub(z, y[0]), ico_rem_pio2_2t);
            }
            return 1;
        } else { /* negative x */
            z = ps2_add(x, ico_rem_pio2_1);
            if ((ix & 0xfffffff0) != 0x3fc90fd0) { /* 24+24 bit pi OK */
                y[0] = ps2_add(z, ico_rem_pio2_1t);
                y[1] = ps2_add(ps2_sub(z, y[0]), ico_rem_pio2_1t);
            } else { /* near pi/2, use 24+24+24 bit pi */
                z = ps2_add(z, ico_rem_pio2_2);
                y[0] = ps2_add(z, ico_rem_pio2_2t);
                y[1] = ps2_add(ps2_sub(z, y[0]), ico_rem_pio2_2t);
            }
            return -1;
        }
    }
    if (ix <= 0x43490f80) { /* |x| ~<= 2^7*(pi/2), medium size */
        t = ico_fabsf(x);
        n = (int32_t)ps2_add(t * ico_rem_invpio2, ico_rem_half);
        fn = (float)n;
        r = ps2_sub(t, fn * ico_rem_pio2_1);
        w = fn * ico_rem_pio2_1t; /* 1st round good to 40 bit */
        if (n < 32 && (int32_t)(ix & 0xffffff00) != ico_rem_npio2_hw[n - 1]) {
            y[0] = ps2_sub(r, w); /* quick check no cancellation */
        } else {
            uint32_t high;
            j = ix >> 23;
            y[0] = ps2_sub(r, w);
            GET_FLOAT_WORD(high, y[0]);
            i = j - (int32_t)((high >> 23) & 0xff);
            if (i > 8) { /* 2nd iteration needed, good to 57 */
                t = r;
                w = fn * ico_rem_pio2_2;
                r = ps2_sub(t, w);
                w = ps2_sub(fn * ico_rem_pio2_2t, ps2_sub(ps2_sub(t, r), w));
                y[0] = ps2_sub(r, w);
                GET_FLOAT_WORD(high, y[0]);
                i = j - (int32_t)((high >> 23) & 0xff);
                if (i > 25) { /* 3rd iteration needed, 74 bits accuracy */
                    t = r;    /* will cover all possible cases */
                    w = fn * ico_rem_pio2_3;
                    r = ps2_sub(t, w);
                    w = ps2_sub(fn * ico_rem_pio2_3t, ps2_sub(ps2_sub(t, r), w));
                    y[0] = ps2_sub(r, w);
                }
            }
        }
        y[1] = ps2_sub(ps2_sub(r, y[0]), w);
        if (hx < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        } else {
            return n;
        }
    }
    /*
     * all other (large) arguments
     */
    if (ix >= 0x7f800000) { /* x is inf or NaN */
        y[0] = y[1] = x - x;
        return 0;
    }
    /* set z = scalbn(|x|, ilogb(x) - 7) */
    e0 = (ix >> 23) - 134; /* e0 = ilogb(z) - 7; */
    SET_FLOAT_WORD(z, ix - ICO_SHL(e0, 23));
    for (i = 0; i < 2; i++) {
        tx[i] = (float)((int32_t)(z));
        z = (z - tx[i]) * ico_rem_two8;
    }
    tx[2] = z;
    nx = 3;
    while (tx[nx - 1] == ico_rem_zero) {
        nx--; /* skip zero term */
    }
    n = ico___kernel_rem_pio2f(tx, y, e0, nx, 2, ico_rem_two_over_pi);
    if (hx < 0) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

/* ---- sf_sin.c (libm.a member sf_sin.o) --------------------------------- */

float ico_sinf(float x)
{
    float y[2];
    int32_t n;
    int32_t ix;

    GET_FLOAT_WORD(ix, x);
    ix &= 0x7fffffff;

    if (ix <= 0x3f490fd8) {
        return ico___kernel_sinf(x, 0.0f, 0);
    } else if (ix >= 0x7f800000) {
        return x - x;
    } else {
        n = ico___ieee754_rem_pio2f(x, y);
        switch (n & 3) {
        case 0:
            return ico___kernel_sinf(y[0], y[1], 1);
        case 1:
            return ico___kernel_cosf(y[0], y[1]);
        case 2:
            return -ico___kernel_sinf(y[0], y[1], 1);
        default:
            return -ico___kernel_cosf(y[0], y[1]);
        }
    }
}

/* ---- sf_cos.c: NOT linked in the EE build -------------------------------
 * No game or sce object references cosf and the link map has no member
 * defining it (build/ico.pal.map lists sf_sin.o and kf_cos.o but no
 * sf_cos.o; build/ico.syms.elf has no cosf symbol). This is newlib's
 * sf_cos.c, the mirror of sf_sin.c above, written in the same style and
 * unverified against any EE binary. */

float ico_cosf(float x)
{
    float y[2], z = 0.0f;
    int32_t n, ix;

    GET_FLOAT_WORD(ix, x);
    ix &= 0x7fffffff;

    if (ix <= 0x3f490fd8) {
        return ico___kernel_cosf(x, z);
    } else if (ix >= 0x7f800000) {
        return x - x;
    } else {
        n = ico___ieee754_rem_pio2f(x, y);
        switch (n & 3) {
        case 0:
            return ico___kernel_cosf(y[0], y[1]);
        case 1:
            return -ico___kernel_sinf(y[0], y[1], 1);
        case 2:
            return -ico___kernel_cosf(y[0], y[1]);
        default:
            return ico___kernel_sinf(y[0], y[1], 1);
        }
    }
}

/* ---- sf_atan.c (libm.a member sf_atan.o) --------------------------------
 * The huge-argument result atanhi[3] + atanlo[3] is computed at run time on
 * the EE (build/sce/libm/math/sf_atan.o 0x54-0x80: two lwc1 from .rodata,
 * then add.s, or neg.s and sub.s), so under round-toward-zero it is
 * 0x3fc90fda, not the round-to-nearest 0x3fc90fdb a host compiler would
 * fold it to. The volatile reads keep it a run-time sum. Only atan2f calls
 * atanf in the game, and only with hx == 0x3f800000 (x == 1). */

static const float ico_atan_atanhi[] = {
    0.46364760398864746f,
    0.7853981256484985f,
    0.9827936887741089f,
    1.570796251296997f,
};

static const float ico_atan_atanlo[] = {
    5.01215824399992e-09f,
    3.774894707930798e-08f,
    3.447321716976148e-08f,
    7.549789415861596e-08f,
};

static const float ico_atan_aT[] = {
    0.3333333432674408f,   -0.20000000298023224f, 0.1428571492433548f,   -0.1111111044883728f,
    0.09090887010097504f,  -0.07691875845193863f, 0.06661073118448257f,  -0.05833570286631584f,
    0.049768779426813126f, -0.03653157129883766f, 0.016285819932818413f,
};

static const float ico_atan_one = 1.0f, ico_atan_huge = 1.0e30f;

static float ico_atanf(float x)
{
    float w, s1, s2, z;
    int32_t ix, hx, id;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x50800000) {
        volatile float hi3 = ico_atan_atanhi[3];
        volatile float lo3 = ico_atan_atanlo[3];
        if (ix > 0x7f800000) {
            return x + x;
        }
        if (hx > 0) {
            return ps2_add(hi3, lo3);
        } else {
            return ps2_sub(-hi3, lo3);
        }
    }
    if (ix < 0x3ee00000) {
        if (ix < 0x31000000) {
            if (ps2_add(ico_atan_huge, x) > ico_atan_one) {
                return x;
            }
        }
        id = -1;
    } else {
        x = ico_fabsf(x);
        if (ix < 0x3f980000) {
            if (ix < 0x3f300000) {
                id = 0;
                x = ps2_sub((float)2.0 * x, ico_atan_one) / ps2_add((float)2.0, x);
            } else {
                id = 1;
                x = ps2_sub(x, ico_atan_one) / ps2_add(x, ico_atan_one);
            }
        } else {
            if (ix < 0x401c0000) {
                id = 2;
                x = ps2_sub(x, (float)1.5) / ps2_add(ico_atan_one, (float)1.5 * x);
            } else {
                id = 3;
                x = -(float)1.0 / x;
            }
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * ps2_add(ico_atan_aT[0],
                     w * ps2_add(ico_atan_aT[2],
                                 w * ps2_add(ico_atan_aT[4],
                                             w * ps2_add(ico_atan_aT[6],
                                                         w * ps2_add(ico_atan_aT[8],
                                                                     w * ico_atan_aT[10])))));
    s2 = w * ps2_add(ico_atan_aT[1],
                     w * ps2_add(ico_atan_aT[3],
                                 w * ps2_add(ico_atan_aT[5],
                                             w * ps2_add(ico_atan_aT[7], w * ico_atan_aT[9]))));
    if (id < 0) {
        return ps2_sub(x, x * ps2_add(s1, s2));
    }
    z = ps2_sub(ico_atan_atanhi[id], ps2_sub(ps2_sub(x * ps2_add(s1, s2), ico_atan_atanlo[id]), x));
    return (hx < 0) ? -z : z;
}

/* ---- ef_atan2.c (libm.a member ef_atan2.o) ------------------------------
 * ee-gcc folded every constant-only return; the EE object loads these
 * words (build/sce/libm/math/ef_atan2.o):
 *   pi_o_2 + tiny          0x3fc90fdb  (0xb0)   -pi_o_2 - tiny     0xbfc90fdb (0xc4)
 *   pi_o_4 + tiny          0x3f490fdb  (0x120)  -pi_o_4 - tiny     0xbf490fdb (0x134)
 *   3.0 * pi_o_4 + tiny    0x4016cbe4  (0x148)  -3.0 * pi_o_4 - tiny 0xc016cbe4 (0x15c)
 *   pi + tiny              0x40490fda  (0x1c0)  -pi - tiny         0xc0490fda (0x1d4)
 *   pi_o_2 + 0.5 * pi_lo   0x3fc90fdc  (0x200)
 * Those are the round-to-nearest values, written out below. */

static const float ico_atan2_zero = 0.0f;
static const float ico_atan2_pi = 3.1415925026e+00f;      /* 0x40490fda */
static const float ico_atan2_pi_lo = 1.5099578832e-07f;   /* 0x34222168 */
static const float ico_atan2_pio2_tiny = 0x1.921fb6p+0f;  /* 0x3fc90fdb: pi_o_2 + tiny */
static const float ico_atan2_pio4_tiny = 0x1.921fb6p-1f;  /* 0x3f490fdb: pi_o_4 + tiny */
static const float ico_atan2_3pio4_tiny = 0x1.2d97c8p+1f; /* 0x4016cbe4: 3.0 * pi_o_4 + tiny */
static const float ico_atan2_pi_tiny = 0x1.921fb4p+1f;    /* 0x40490fda: pi + tiny */
static const float ico_atan2_pio2_hlo = 0x1.921fb8p+0f;   /* 0x3fc90fdc: pi_o_2 + 0.5 * pi_lo */

static float ico___ieee754_atan2f(float y, float x)
{
    float z;
    int32_t k, m, hx, hy, ix, iy;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff;
    GET_FLOAT_WORD(hy, y);
    iy = hy & 0x7fffffff;
    if ((ix > 0x7f800000) || (iy > 0x7f800000)) {
        return x + y;
    }
    if (hx == 0x3f800000) {
        return ico_atanf(y);
    }
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);

    if (iy == 0) {
        switch (m) {
        case 0:
        case 1:
            return y;
        case 2:
            return ico_atan2_pi_tiny;
        case 3:
            return -ico_atan2_pi_tiny;
        }
    }
    if (ix == 0) {
        return (hy < 0) ? -ico_atan2_pio2_tiny : ico_atan2_pio2_tiny;
    }

    if (ix == 0x7f800000) {
        if (iy == 0x7f800000) {
            switch (m) {
            case 0:
                return ico_atan2_pio4_tiny;
            case 1:
                return -ico_atan2_pio4_tiny;
            case 2:
                return ico_atan2_3pio4_tiny;
            case 3:
                return -ico_atan2_3pio4_tiny;
            }
        } else {
            switch (m) {
            case 0:
                return ico_atan2_zero;
            case 1:
                return -ico_atan2_zero;
            case 2:
                return ico_atan2_pi_tiny;
            case 3:
                return -ico_atan2_pi_tiny;
            }
        }
    }

    if (iy == 0x7f800000) {
        return (hy < 0) ? -ico_atan2_pio2_tiny : ico_atan2_pio2_tiny;
    }

    k = (iy - ix) >> 23;
    if (k > 60) {
        z = ico_atan2_pio2_hlo;
    } else if (hx < 0 && k < -60) {
        z = 0.0f;
    } else {
        z = ico_atanf(ico_fabsf(y / x));
    }
    switch (m) {
    case 0:
        return z;
    case 1: {
        uint32_t zh;
        GET_FLOAT_WORD(zh, z);
        SET_FLOAT_WORD(z, zh ^ 0x80000000);
    }
        return z;
    case 2:
        return ps2_sub(ico_atan2_pi, ps2_sub(z, ico_atan2_pi_lo));
    default:
        return ps2_sub(ps2_sub(z, ico_atan2_pi_lo), ico_atan2_pi);
    }
}

/* ---- ef_acos.c (libm.a member ef_acos.o) --------------------------------
 * Folded by ee-gcc (build/sce/libm/math/ef_acos.o):
 *   pi + 2.0 * pio2_lo     0x40490fdb  (0x40)
 *   pio2_hi + pio2_lo      0x3fc90fdb  (0x9c) */

static const float ico_acos_one = 1.0000000000e+00f;
static const float ico_acos_pi = 3.1415925026e+00f;
static const float ico_acos_pio2_hi = 1.5707962513e+00f;
static const float ico_acos_pio2_lo = 7.5497894159e-08f;
static const float ico_acos_pi_2lo = 0x1.921fb6p+1f;  /* 0x40490fdb: pi + 2.0 * pio2_lo */
static const float ico_acos_pio2_hl = 0x1.921fb6p+0f; /* 0x3fc90fdb: pio2_hi + pio2_lo */
static const float ico_acos_pS0 = 1.6666667163e-01f;
static const float ico_acos_pS1 = -3.2556581497e-01f;
static const float ico_acos_pS2 = 2.0121252537e-01f;
static const float ico_acos_pS3 = -4.0055535734e-02f;
static const float ico_acos_pS4 = 7.9153501429e-04f;
static const float ico_acos_pS5 = 3.4793309169e-05f;
static const float ico_acos_qS1 = -2.4033949375e+00f;
static const float ico_acos_qS2 = 2.0209457874e+00f;
static const float ico_acos_qS3 = -6.8828397989e-01f;
static const float ico_acos_qS4 = 7.7038154006e-02f;

#define ICO_ACOS_P(z)                                                                              \
    ((z) *                                                                                         \
     ps2_add(ico_acos_pS0,                                                                         \
             (z) * ps2_add(ico_acos_pS1,                                                           \
                           (z) * ps2_add(ico_acos_pS2,                                             \
                                         (z) * ps2_add(ico_acos_pS3,                               \
                                                       (z) * ps2_add(ico_acos_pS4,                 \
                                                                     (z) * ico_acos_pS5))))))
#define ICO_ACOS_Q(z)                                                                              \
    ps2_add(ico_acos_one,                                                                          \
            (z) * ps2_add(ico_acos_qS1,                                                            \
                          (z) * ps2_add(ico_acos_qS2,                                              \
                                        (z) * ps2_add(ico_acos_qS3, (z) * ico_acos_qS4))))

/* The domain-error value of acosf's and asinf's cores, (x - x) / (x - x),
 * as the EE computes it: ef_acos.o 0x64 sub.s then 0x70 div.s, ef_asin.o
 * 0x6c and 0x78, both at run time. The FPU reads an exponent-255 pattern
 * as a number, so x - x is +0, and div.s gives +Fmax for 0 / 0 (ps2_div).
 * The host's IEEE 0 / 0 is NaN and raises invalid (the fptrap preset's
 * stop in the leg IK, motMan_getFinalMatrix.c.inc:1008, x = 0x3f8007a8 at
 * a stage 6 boot). The wrappers return 0.0f for |x| > 1 whatever the core
 * gives, so this value reaches a caller only for an exponent-255 pattern
 * (ico_isnanf), where it is the EE's +Fmax. */
static float ico_domain_error(float x)
{
    float d = ps2_operand(x) - ps2_operand(x);

    return ps2_div(d, d);
}

static float ico___ieee754_acosf(float x)
{
    float z, p, q, r, w, s, c, df;
    int32_t hx, ix;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff;
    if (ix == 0x3f800000) {
        if (hx > 0) {
            return 0.0f;
        } else {
            return ico_acos_pi_2lo;
        }
    } else if (ix > 0x3f800000) {
        return ico_domain_error(x); /* (x - x) / (x - x) */
    }
    if (ix < 0x3f000000) {
        if (ix <= 0x23000000) {
            return ico_acos_pio2_hl;
        }
        z = x * x;
        p = ICO_ACOS_P(z);
        q = ICO_ACOS_Q(z);
        r = p / q;
        return ps2_sub(ico_acos_pio2_hi, ps2_sub(x, ps2_sub(ico_acos_pio2_lo, x * r)));
    } else if (hx < 0) {
        z = ps2_add(ico_acos_one, x) * (float)0.5;
        p = ICO_ACOS_P(z);
        q = ICO_ACOS_Q(z);
        s = ico___ieee754_sqrtf(z);
        r = p / q;
        w = ps2_sub(r * s, ico_acos_pio2_lo);
        return ps2_sub(ico_acos_pi, (float)2.0 * ps2_add(s, w));
    } else {
        int32_t idf;
        z = ps2_sub(ico_acos_one, x) * (float)0.5;
        s = ico___ieee754_sqrtf(z);
        df = s;
        GET_FLOAT_WORD(idf, df);
        SET_FLOAT_WORD(df, idf & 0xfffff000);
        c = ps2_sub(z, df * df) / ps2_add(s, df);
        p = ICO_ACOS_P(z);
        q = ICO_ACOS_Q(z);
        r = p / q;
        w = ps2_add(r * s, c);
        return (float)2.0 * ps2_add(df, w);
    }
}

/* ---- ef_asin.c (libm.a member ef_asin.o) --------------------------------
 * No constant-only expressions (pio2_hi and pio2_lo are each multiplied
 * by x at run time, ef_asin.o 0x38-0x4c). */

static const float ico_asin_one = 1.0000000000e+00f;     /* 0x3F800000 */
static const float ico_asin_huge = 1.0000000150e+30f;    /* 0x7149F2CA */
static const float ico_asin_pio2_hi = 1.5707962513e+00f; /* 0x3FC90FDA */
static const float ico_asin_pio2_lo = 7.5497894159e-08f; /* 0x33A22168 */
static const float ico_asin_pio4_hi = 7.8539818525e-01f; /* 0x3F490FDB */
/* asin's pS0-pS5 and qS1-qS4 are acos's coefficients (0x3E2AAAAB ... 0x3D9DC62E), so its
   polynomials are ICO_ACOS_P and ICO_ACOS_Q. */

static float ico___ieee754_asinf(float x)
{
    /* t starts at 0 only to quiet -Wmaybe-uninitialized: fdlibm reads it
       uninitialised when |x| < 2**-27 and huge + x <= one, which no finite
       x reaches. */
    float t = 0.0f, w, p, q, c, r, s;
    int32_t hx, ix;

    GET_FLOAT_WORD(hx, x);
    ix = hx & 0x7fffffff;
    if (ix == 0x3f800000) {
        /* asin(1) = +-pi/2 with inexact */
        return ps2_add(x * ico_asin_pio2_hi, x * ico_asin_pio2_lo);
    } else if (ix > 0x3f800000) {   /* |x| >= 1 */
        return ico_domain_error(x); /* (x - x) / (x - x), asin(x) = NaN */
    } else if (ix < 0x3f000000) {   /* |x| < 0.5 */
        if (ix < 0x32000000) {      /* if |x| < 2**-27 */
            if (ps2_add(ico_asin_huge, x) > ico_asin_one) {
                return x; /* return x with inexact if x != 0 */
            }
        } else
            t = x * x;
        p = ICO_ACOS_P(t);
        q = ICO_ACOS_Q(t);
        w = p / q;
        return ps2_add(x, x * w);
    }
    /* 1 > |x| >= 0.5 */
    w = ps2_sub(ico_asin_one, ico_fabsf(x));
    t = w * (float)0.5;
    p = ICO_ACOS_P(t); /* asin's pS and qS are acos's */
    q = ICO_ACOS_Q(t);
    s = ico___ieee754_sqrtf(t);
    if (ix >= 0x3F79999A) { /* if |x| > 0.975 */
        w = p / q;
        t = ps2_sub(ico_asin_pio2_hi, ps2_sub((float)2.0 * ps2_add(s, s * w), ico_asin_pio2_lo));
    } else {
        int32_t iw;
        w = s;
        GET_FLOAT_WORD(iw, w);
        SET_FLOAT_WORD(w, iw & 0xfffff000);
        c = ps2_sub(t, w * w) / ps2_add(s, w);
        r = p / q;
        p = ps2_sub((float)2.0 * s * r, ps2_sub(ico_asin_pio2_lo, (float)2.0 * c));
        q = ps2_sub(ico_asin_pio4_hi, (float)2.0 * w);
        t = ps2_sub(ico_asin_pio4_hi, ps2_sub(p, q));
    }
    if (hx > 0) {
        return t;
    } else {
        return -t;
    }
}

/* ---- ef_fmod.c (libm.a member ef_fmod.o) ------------------------------- */

static const float ico_fmod_Zero[] = {
    0.0f,
    -0.0f,
};

static float ico___ieee754_fmodf(float x, float y)
{
    uint32_t n;
    int32_t hx, hy, hz, ix, iy, sx, i;

    GET_FLOAT_WORD(hx, x);
    GET_FLOAT_WORD(hy, y);
    sx = (int32_t)((uint32_t)hx & 0x80000000);
    hx ^= sx;
    hy &= 0x7fffffff;

    if (hy == 0 || hx >= 0x7f800000 || hy > 0x7f800000) {
        return (x * y) / (x * y);
    }
    if (hx < hy) {
        return x;
    }
    if (hx == hy) {
        return ico_fmod_Zero[(uint32_t)sx >> 31];
    }

    if (hx < 0x00800000) {
        for (ix = -126, i = ICO_SHL(hx, 8); i > 0; i = ICO_SHL(i, 1)) {
            ix -= 1;
        }
    } else {
        ix = (hx >> 23) - 127;
    }

    if (hy < 0x00800000) {
        for (iy = -126, i = ICO_SHL(hy, 8); i >= 0; i = ICO_SHL(i, 1)) {
            iy -= 1;
        }
    } else {
        iy = (hy >> 23) - 127;
    }

    if (ix >= -126) {
        hx = 0x00800000 | (0x007fffff & hx);
    } else {
        n = (uint32_t)(-126 - ix);
        hx = ICO_SHL(hx, n);
    }
    if (iy >= -126) {
        hy = 0x00800000 | (0x007fffff & hy);
    } else {
        n = (uint32_t)(-126 - iy);
        hy = ICO_SHL(hy, n);
    }

    /* fix point fmod: n shift-and-subtract steps */
    n = (uint32_t)(ix - iy);
    while (n-- > 0) {
        hz = hx - hy;
        if (hz < 0) {
            hx = hx + hx;
        } else {
            if (hz == 0) {
                return ico_fmod_Zero[(uint32_t)sx >> 31];
            }
            hx = hz + hz;
        }
    }
    hz = hx - hy;
    if (hz >= 0) {
        hx = hz;
    }

    if (hx == 0) {
        return ico_fmod_Zero[(uint32_t)sx >> 31];
    }
    while (hx < 0x00800000) {
        hx = hx + hx;
        iy -= 1;
    }
    if (iy >= -126) {
        hx = ((hx - 0x00800000) | ICO_SHL(iy + 127, 23));
        SET_FLOAT_WORD(x, hx | sx);
    } else {
        n = (uint32_t)(-126 - iy);
        hx >>= n;
        SET_FLOAT_WORD(x, hx | sx);
    }
    return x;
}

/* ---- wrappers: wf_atan2.c, wf_acos.c, wf_asin.c, wf_fmod.c ------------
 * (libm.a members wf_atan2.o, wf_acos.o, wf_asin.o, wf_fmod.o)
 *
 * The EE's _LIB_VERSION is _XOPEN_ (sce/libm/common/s_lib_ver.c), and its
 * matherr returns 0 for every argument (sce/libm/common/s_matherr.c), so
 * for a domain error each wrapper sets errno to EDOM (33) and returns
 * (float)exc.retval:
 *   atan2f(+-0, +-0)   0.0f (not the core's +-0 or +-pi)
 *   acosf(|x| > 1)     0.0f
 *   asinf(|x| > 1)     0.0f
 *   fmodf(x, 0)        dptofp(0.0 / 0.0): ee-gcc folds 0.0 / 0.0 to the
 *                      double 0x7FF8000000000000 (wf_fmod.o .rodata 0x8);
 *                      libgcc's dptofp (sce/libgcc/dp-bit.c:654, then
 *                      fp-bit.c's __pack_f) packs it as 0x7FB00000.
 *                      That is a signalling NaN to x86, and the i386 ABI
 *                      returns floats on the x87 stack, whose load quiets
 *                      it: callers on 32-bit x86 receive 0x7FF00000.
 * A NaN argument returns the core's result untouched. errno is not set
 * here: nothing in the game reads it. The fmodf value is derived from the
 * reconstructed libgcc, not observed on hardware; the game's one fmodf
 * call divides by a non-zero constant (ico2/ito/src/queen.c:1436). */

float ico_atan2f(float y, float x)
{
    float z;

    z = ico___ieee754_atan2f(y, x);
    if (ico_isnanf(x) || ico_isnanf(y))
        return z;
    if (x == 0.0f && y == 0.0f) {
        return 0.0f; /* (float)exc.retval */
    } else
        return z;
}

float ico_acosf(float x)
{
    float z;

    z = ico___ieee754_acosf(x);
    if (ico_isnanf(x))
        return z;
    if (ico_fabsf(x) > 1.0f) {
        return 0.0f; /* (float)exc.retval */
    } else
        return z;
}

float ico_asinf(float x)
{
    float z;

    z = ico___ieee754_asinf(x);
    if (ico_isnanf(x))
        return z;
    if (ico_fabsf(x) > (float)1.0) {
        return 0.0f; /* (float)exc.retval */
    } else
        return z;
}

float ico_fmodf(float x, float y)
{
    float z;

    z = ico___ieee754_fmodf(x, y);
    if (ico_isnanf(y) || ico_isnanf(x)) {
        return z;
    }
    if (y == (float)0.0) {
        SET_FLOAT_WORD(z, 0x7FB00000); /* (float)exc.retval, see above */
        return z;
    }
    return z;
}

/* ---- sqrtf --------------------------------------------------------------
 * The EE build links ef_sqrt.o (__ieee754_sqrtf, called by acosf and
 * asinf) but no sqrtf wrapper: no game or sce object references sqrtf.
 * ico_sqrtf is the core alone. For x < 0 it returns (x - x) / (x - x),
 * the host's default NaN, where newlib's X/Open wf_sqrt.c would return
 * (float)(0.0 / 0.0); for x >= 0 the two agree. */

float ico_sqrtf(float x)
{
    return ico___ieee754_sqrtf(x);
}
