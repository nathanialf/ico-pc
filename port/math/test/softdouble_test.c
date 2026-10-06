/*
 * port/math/test/softdouble_test.c: port/math/softdouble.c against the EE's
 * own soft float.
 *
 * The oracle is sce/libgcc/dp-bit.c and fp-bit.c, the reconstructions of the
 * libgcc members the game linked, compiled into this test with their
 * exported names prefixed `ref_` (port/math/CMakeLists.txt) after
 * port/math/test/dpbit_harness.cmake gives the functions that returned their
 * result in a register a host return type. Every function is compared bit
 * for bit over a fixed-seed pseudo-random sweep (a few million pairs per
 * operation, built from random bits and from chosen sign, exponent and
 * fraction classes: zeros, denormals, Infs, NaNs, exponents near the
 * denormal and overflow edges, exponent differences around the significand
 * width) and over the conversion edges (INT_MIN/INT_MAX, the float rounding
 * ties, the float denormal range).
 *
 * The test also counts how often the host's own round-to-nearest double
 * arithmetic would have differed (the "switch the rounding mode" option in
 * docs/research/float-semantics.md); that count is printed, not checked.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../softdouble.h"

long long ref_dpadd(long long a, long long b);
long long ref_dpsub(long long a, long long b);
long long ref_dpmul(long long a, long long b);
long long ref_dpdiv(long long a, long long b);
int ref_dpcmp(long long a, long long b);
long long ref_litodp(int a);
int ref_dptoli(long long a);
float ref_dptofp(long long a);
long long ref_fptodp(float a);

static uint64_t rng_state = 0x1C0DEC0DE5EEDull;

static uint64_t rng(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static long failures;
static long checks;

static void fail_d(const char *op, uint64_t a, uint64_t b, uint64_t got, uint64_t want)
{
    if (failures < 20) {
        printf("FAIL %s(%016llx, %016llx): %016llx, dp-bit %016llx\n", op, (unsigned long long)a,
               (unsigned long long)b, (unsigned long long)got, (unsigned long long)want);
    }
    failures++;
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float bitsf(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* an exponent field: random, or one of the edges */
static uint64_t pick_exp(void)
{
    static const int edges[] = {0,    1,    2,    3,    26,   27,   28,   52,   53,   54,
                                511,  512,  1020, 1021, 1022, 1023, 1024, 1025, 1074, 1075,
                                1076, 1534, 1535, 1536, 2000, 2044, 2045, 2046, 2047};
    uint64_t r = rng();

    switch (r & 3) {
    case 0:
        return edges[(r >> 8) % (sizeof edges / sizeof edges[0])];
    case 1:
        return (r >> 8) & 0x7FF;
    case 2:
        return 1023 + (int)((r >> 8) % 64) - 32;
    default:
        return (r >> 8) % 2048;
    }
}

static uint64_t pick_frac(void)
{
    uint64_t r = rng();
    const uint64_t m = 0x000FFFFFFFFFFFFFull;

    switch (r & 7) {
    case 0:
        return 0;
    case 1:
        return m;
    case 2:
        return 1ull << ((r >> 8) % 52);
    case 3:
        return m >> ((r >> 8) % 52);
    case 4:
        return (m << ((r >> 8) % 52)) & m;
    case 5:
        return rng() & 0xFF; /* low bits only: the guard and sticky edges */
    default:
        return rng() & m;
    }
}

static uint64_t pick_double(void)
{
    uint64_t r = rng();

    if ((r & 7) == 0) {
        return rng(); /* plain random bits */
    }
    return ((r >> 8) & 1) << 63 | pick_exp() << 52 | pick_frac();
}

/* b near a: an exponent difference around the significand width, or a
   value close enough to cancel */
static uint64_t pick_near(uint64_t a)
{
    uint64_t r = rng();
    int64_t ea = (int64_t)((a >> 52) & 0x7FF);
    int64_t eb = ea + (int64_t)((r >> 8) % 141) - 70;
    uint64_t sign = ((r >> 20) & 1) << 63;

    if (eb < 0) {
        eb = 0;
    }
    if (eb > 2047) {
        eb = 2047;
    }
    switch (r & 3) {
    case 0:
        return sign | ((a & 0x7FFFFFFFFFFFFFFFull) ^ (rng() & 0xFF));
    case 1:
        return sign | ((a & 0x7FFFFFFFFFFFFFFFull) ^ (1ull << ((r >> 32) % 52)));
    default:
        return sign | (uint64_t)eb << 52 | pick_frac();
    }
}

/* b so that a * b or a / b lands near the denormal or overflow edge */
static uint64_t pick_scale(uint64_t a, int div)
{
    uint64_t r = rng();
    int64_t ea = (int64_t)((a >> 52) & 0x7FF) - 1023;
    static const int targets[] = {-1022, -1023, -1030, -1074, -1075, -1080, 1023, 1024, 0};
    int64_t t = targets[(r >> 8) % (sizeof targets / sizeof targets[0])] + (int)((r >> 16) % 5) - 2;
    int64_t eb = div ? ea - t : t - ea;

    eb += 1023;
    if (eb < 0 || eb > 2046) {
        eb = (int64_t)((r >> 24) % 2046) + 1;
    }
    return ((r >> 40) & 1) << 63 | (uint64_t)eb << 52 | pick_frac();
}

static void check_pair(uint64_t a, uint64_t b)
{
    uint64_t got, want;
    int c, cw;

    got = ico_dadd(a, b);
    want = (uint64_t)ref_dpadd((long long)a, (long long)b);
    if (got != want) {
        fail_d("add", a, b, got, want);
    }
    got = ico_dsub(a, b);
    want = (uint64_t)ref_dpsub((long long)a, (long long)b);
    if (got != want) {
        fail_d("sub", a, b, got, want);
    }
    got = ico_dmul(a, b);
    want = (uint64_t)ref_dpmul((long long)a, (long long)b);
    if (got != want) {
        fail_d("mul", a, b, got, want);
    }
    got = ico_ddiv(a, b);
    want = (uint64_t)ref_dpdiv((long long)a, (long long)b);
    if (got != want) {
        fail_d("div", a, b, got, want);
    }
    c = ico_dcmp(a, b);
    cw = ref_dpcmp((long long)a, (long long)b);
    if (c != cw) {
        fail_d("cmp", a, b, (uint64_t)(int64_t)c, (uint64_t)(int64_t)cw);
    }
    checks += 5;
}

static void check_unary(uint64_t a)
{
    int32_t i = ico_d2i(a);
    int32_t iw = ref_dptoli((long long)a);
    uint32_t f = ico_d2f_bits(a);
    uint32_t fw = fbits(ref_dptofp((long long)a));

    if (i != iw) {
        fail_d("d2i", a, 0, (uint64_t)(int64_t)i, (uint64_t)(int64_t)iw);
    }
    if (f != fw) {
        fail_d("d2f", a, 0, f, fw);
    }
    checks += 2;
}

static void check_int(int32_t i)
{
    uint64_t got = ico_i2d(i);
    uint64_t want = (uint64_t)ref_litodp(i);

    if (got != want) {
        fail_d("i2d", (uint64_t)(int64_t)i, 0, got, want);
    }
    checks++;
}

static void check_float(uint32_t u)
{
    uint64_t got = ico_f2d_bits(u);
    uint64_t want = (uint64_t)ref_fptodp(bitsf(u));

    if (got != want) {
        fail_d("f2d", u, 0, got, want);
    }
    if (ico_f2d(bitsf(u)) != got) {
        fail_d("f2d(float)", u, 0, ico_f2d(bitsf(u)), got);
    }
    checks++;
}

/* how often the host's round-to-nearest doubles differ (not a failure) */
static long host_diff, host_total;

static void host_compare(uint64_t a, uint64_t b)
{
    double x = ico_dval(a), y = ico_dval(b);

    host_total += 2;
    if (ico_dbits(x * y) != ico_dmul(a, b)) {
        host_diff++;
    }
    if (ico_dbits(x / y) != ico_ddiv(a, b)) {
        host_diff++;
    }
}

int main(void)
{
    static const uint64_t specials[] = {
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull, 0x800FFFFFFFFFFFFFull,
        0x0010000000000000ull, 0x8010000000000000ull, 0x3FF0000000000000ull, 0xBFF0000000000000ull,
        0x3FEFFFFFFFFFFFFFull, 0x3FF0000000000001ull, 0x7FEFFFFFFFFFFFFFull, 0xFFEFFFFFFFFFFFFFull,
        0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000000ull, 0xFFF8000000000000ull,
        0x7FF0000000000001ull, 0xFFF7FFFFFFFFFFFFull, 0x41E0000000000000ull, 0xC1E0000000000000ull,
        0x41DFFFFFFFC00000ull, 0xC1E0000000200000ull, 0x4000000000000000ull, 0x3FB999999999999Aull,
        0x47EFFFFFE0000000ull, 0x47EFFFFFF0000000ull, 0x3810000000000000ull, 0x380FFFFFF0000000ull,
        0x36A0000000000000ull, 0x369FFFFFFFFFFFFFull,
    };
    const int ns = (int)(sizeof specials / sizeof specials[0]);
    long n;
    int i, j;
    int64_t k;

    /* every pair of specials */
    for (i = 0; i < ns; i++) {
        for (j = 0; j < ns; j++) {
            check_pair(specials[i], specials[j]);
        }
        check_unary(specials[i]);
    }

    /* random pairs: independent, near (add/sub cancellation and alignment),
       and scaled to the edges of the exponent range (mul/div) */
    for (n = 0; n < 1500000; n++) {
        uint64_t a = pick_double();
        check_pair(a, pick_double());
        check_pair(a, pick_near(a));
        check_pair(a, pick_scale(a, 0));
        check_pair(a, pick_scale(a, 1));
        check_pair(rng(), rng());
        check_unary(a);
        check_unary(rng());
    }

    /* d2i: around the int range and its saturation */
    for (k = -40; k <= 40; k++) {
        uint64_t m = (uint64_t)(1023 + 31) << 52;
        check_unary(m + (uint64_t)k);
        check_unary((m | 0x8000000000000000ull) + (uint64_t)k);
        check_unary(((uint64_t)(1023 + 30) << 52) + 0xFFFFFFFFFFFFFull + (uint64_t)k);
        check_unary(((uint64_t)(1023 + 30) << 52 | 0x8000000000000000ull) + 0xFFFFFFFFFFFFFull +
                    (uint64_t)k);
        check_unary(((uint64_t)(1023 - 1) << 52) + (uint64_t)k); /* 0.5 */
        check_unary(((uint64_t)(1023 - 1) << 52 | 0x8000000000000000ull) + (uint64_t)k);
    }

    /* d2f: the rounding ties and their neighbours at every float exponent,
       the float denormal range and the overflow edge included */
    for (k = -160; k <= 130; k++) {
        uint64_t e = (uint64_t)(1023 + k) << 52;
        uint64_t half = 1ull << 28; /* half an ulp of a float significand */
        for (i = 0; i < 64; i++) {
            uint64_t top = (rng() & 0x000FFFFFE0000000ull) | ((uint64_t)(i & 1) << 29);
            uint64_t s = (uint64_t)(i & 2) << 62;
            check_unary(s | e | top | half);
            check_unary(s | e | top | half | 1);
            check_unary(s | e | top | (half - 1));
            check_unary(s | e | top);
            check_unary(s | e | 0x000FFFFFF0000000ull);
            check_unary(s | e | 0x000FFFFFEFFFFFFFull);
        }
    }

    /* i2d: the edges and a stride through every int */
    for (k = -70; k <= 70; k++) {
        check_int((int32_t)k);
        check_int((int32_t)(INT32_MIN + k + 70));
        check_int((int32_t)(INT32_MAX - k - 70));
    }
    for (k = 0; k < (1ll << 32); k += 251) {
        check_int((int32_t)(uint32_t)k);
    }

    /* f2d: every float exponent and the specials, and a random sweep */
    for (k = 0; k < 512; k++) {
        check_float((uint32_t)k << 23);
        check_float((uint32_t)k << 23 | 1);
        check_float((uint32_t)k << 23 | 0x7FFFFF);
        check_float((uint32_t)k << 23 | 0x400000);
        check_float((uint32_t)k << 23 | 0x100000);
    }
    for (n = 0; n < 4000000; n++) {
        check_float((uint32_t)rng());
    }

    /* how the host's round to nearest compares, on normal operands */
    for (n = 0; n < 200000; n++) {
        uint64_t a = pick_double(), b = pick_double();
        uint64_t ea = (a >> 52) & 0x7FF, eb = (b >> 52) & 0x7FF;
        if (ea != 0 && ea != 0x7FF && eb != 0 && eb != 0x7FF) {
            host_compare(a, b);
        }
    }

    printf("softdouble: %ld checks, %ld failures; the host's round-to-nearest doubles differ on "
           "%ld of %ld products and quotients of normal operands\n",
           checks, failures, host_diff, host_total);
    return failures != 0;
}
