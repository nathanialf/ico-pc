/*
 * port/math/test/newlib_test.c
 *
 * Checks port/math/newlib: ico_rand's sequence from the start-up seed,
 * ico_qsort's order of equal keys on a hand-traced input, and a few libm
 * values against the mathematically correct ones. The libm checks run
 * twice, in the host's default floating-point environment and in the
 * simulation's (port/platform/fpenv.h: round toward zero, flush-to-zero),
 * which is how the game calls them.
 *
 * Build (from the tree root):
 *   gcc -std=gnu11 -O2 -ffp-contract=off -fno-strict-aliasing -fwrapv
 *       -Iport/platform port/math/test/newlib_test.c port/math/newlib/rand.c
 *       port/math/newlib/qsort.c port/math/newlib/ico_libm.c
 *       port/platform/fpenv.c -lm -o newlib_test
 *
 * Returns 0 when every check passes.
 */
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../newlib/ico_newlib.h"
#include "fpenv.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* Distance in ulps between two finite floats of the same sign. */
static uint32_t ulp_dist(float a, float b)
{
    int32_t ia = (int32_t)fbits(a), ib = (int32_t)fbits(b);
    if (ia < 0)
        ia = (int32_t)(0x80000000u - (uint32_t)ia);
    if (ib < 0)
        ib = (int32_t)(0x80000000u - (uint32_t)ib);
    return ia > ib ? (uint32_t)(ia - ib) : (uint32_t)(ib - ia);
}

/* ---- rand ---------------------------------------------------------------- */

static void test_rand(void)
{
    uint32_t s = 1; /* _REENT_INIT's seed, sce/libc/reent/impure.c */
    int i;

    for (i = 0; i < 1000; i++) {
        int got = ico_rand();
        s = s * 0x41C64E6Du + 0x3039u;
        CHECK(got == (int)(s & 0x7FFFFFFFu), "rand #%d: got %d want %u", i, got,
              (unsigned)(s & 0x7FFFFFFFu));
    }
    /* the first values written out: 1*0x41C64E6D + 0x3039 = 0x41C67EA6 */
    ico_srand(1);
    CHECK(ico_rand() == 1103527590, "rand after srand(1)");
    CHECK(ico_rand() == 377401575, "rand #2 after srand(1)");
    CHECK(ico_rand() == 662824084, "rand #3 after srand(1)");
    ico_srand(1); /* leave the generator as the game finds it */
}

/* ---- qsort --------------------------------------------------------------- */

typedef struct {
    int key;
    char tag;
} Ent;

static int cmp_ent(const void *a, const void *b)
{
    const Ent *x = a, *y = b;
    return x->key < y->key ? -1 : x->key > y->key;
}

static void test_qsort(void)
{
    /*
     * n = 8: one partition step, then insertion sort of the left part.
     * Traced by hand through sce/libc/stdlib/qsort.c:
     *   med3(a[0], a[4], a[7]) on keys 2, 2, 2 picks a[7] (2d); swap to a[0]:
     *     2d 1a 2b 1b 2c 0a 1c 2a
     *   the left scan moves the keys equal to the pivot to the front
     *   (2b, 2c, 2a in turn) and stops at pb = 8:
     *     2d 2b 2c 2a 1a 0a 1c 1b
     *   vecswap(a, pb - 4, 4):        1a 0a 1c 1b 2d 2b 2c 2a
     *   recursion on the first 4 (n < 7, insertion sort, stable):
     *     0a 1a 1c 1b
     * so the equal keys come out 1a 1c 1b and 2d 2b 2c 2a: not stable,
     * and not what glibc's merge sort gives (1a 1b 1c, 2a 2b 2c 2d).
     */
    Ent v[8] = {{2, 'a'}, {1, 'a'}, {2, 'b'}, {1, 'b'}, {2, 'c'}, {0, 'a'}, {1, 'c'}, {2, 'd'}};
    static const char want[] = "0a1a1c1b2d2b2c2a";
    char got[17];
    int i;

    ico_qsort(v, 8, sizeof v[0], cmp_ent);
    for (i = 0; i < 8; i++) {
        got[2 * i] = (char)('0' + v[i].key);
        got[2 * i + 1] = v[i].tag;
    }
    got[16] = 0;
    CHECK(strcmp(got, want) == 0, "qsort n=8: got %s want %s", got, want);

    /* n < 7 is a plain insertion sort, so stable */
    {
        Ent w[5] = {{1, 'a'}, {0, 'a'}, {1, 'b'}, {0, 'b'}, {1, 'c'}};
        ico_qsort(w, 5, sizeof w[0], cmp_ent);
        CHECK(w[0].tag == 'a' && w[1].tag == 'b' && w[2].tag == 'a' && w[3].tag == 'b' &&
                  w[4].tag == 'c' && w[0].key == 0 && w[2].key == 1,
              "qsort n=5 not the insertion-sort order");
    }

    /* n > 40 (the ninther path): sorted, and the order is reproducible.
       The tag order is printed so a run on another host can be compared. */
    {
        Ent big[64];
        uint32_t s = 12345, h = 2166136261u;
        for (i = 0; i < 64; i++) {
            s = s * 0x41C64E6Du + 0x3039u;
            big[i].key = (int)((s >> 16) % 5);
            big[i].tag = (char)i;
        }
        ico_qsort(big, 64, sizeof big[0], cmp_ent);
        for (i = 1; i < 64; i++)
            CHECK(big[i - 1].key <= big[i].key, "qsort n=64 not sorted at %d", i);
        for (i = 0; i < 64; i++)
            h = (h ^ (uint8_t)big[i].tag) * 16777619u;
        printf("qsort n=64 tag-order FNV-1a: %08x\n", (unsigned)h);
    }
}

/* ---- libm ---------------------------------------------------------------- */

typedef struct {
    const char *name;
    float got;
    double want;
} Val;

#define NVALS 18
static float refs[NVALS];

static void check_vals(const char *env, uint32_t tol)
{
    Val vals[NVALS] = {
        {"atan2f(1, 1)", ico_atan2f(1.0f, 1.0f), 0.78539816339744830962},
        {"atan2f(-1, -1)", ico_atan2f(-1.0f, -1.0f), -2.35619449019234492885},
        {"atan2f(1, 2)", ico_atan2f(1.0f, 2.0f), 0.46364760900080611621},
        {"acosf(0.5)", ico_acosf(0.5f), 1.04719755119659774615},
        {"acosf(-0.5)", ico_acosf(-0.5f), 2.09439510239319549231},
        {"acosf(0.25)", ico_acosf(0.25f), 1.31811607165281796574},
        {"asinf(0.5)", ico_asinf(0.5f), 0.52359877559829887308},
        {"asinf(0.25)", ico_asinf(0.25f), 0.25268025514207865349},
        {"sinf(1)", ico_sinf(1.0f), 0.84147098480789650665},
        {"sinf(0.5)", ico_sinf(0.5f), 0.47942553860420300027},
        {"sinf(100)", ico_sinf(100.0f), -0.50636564110975879061},
        {"sinf(1e6)", ico_sinf(1000000.0f), -0.34999350217129295211},
        {"cosf(1)", ico_cosf(1.0f), 0.54030230586813971740},
        {"cosf(3)", ico_cosf(3.0f), -0.98999249660044545727},
        {"fmodf(5.5, 2)", ico_fmodf(5.5f, 2.0f), 1.5},
        {"fmodf(-7.25, 3)", ico_fmodf(-7.25f, 3.0f), -1.25},
        {"sqrtf(2)", ico_sqrtf(2.0f), 1.41421356237309504880},
        {"sqrtf(0.5)", ico_sqrtf(0.5f), 0.70710678118654752440},
    };
    size_t i;

    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        /* refs[i] is (float)want rounded to nearest: main fills it before
           entering the simulation environment. */
        float ref = refs[i];
        uint32_t d = ulp_dist(vals[i].got, ref);
        printf("  [%s] %-16s = %.9g (0x%08x), %u ulp\n", env, vals[i].name, (double)vals[i].got,
               (unsigned)fbits(vals[i].got), (unsigned)d);
        CHECK(d <= tol, "[%s] %s: %.9g is %u ulp from %.9g", env, vals[i].name, (double)vals[i].got,
              (unsigned)d, (double)ref);
    }
}

/* fmodf(1, 0), the X/Open domain case: the wrapper's 0/0 raises invalid,
   which the fptrap preset's simulation environment traps on. Run it with
   the exceptions held (the rounding and flush modes stay) and restore the
   environment, flags included. */
static float fmodf_domain(void)
{
    fenv_t held;
    float r;

    feholdexcept(&held);
    r = ico_fmodf(1.0f, 0.0f);
    fesetenv(&held);
    return r;
}

/* Results that are bit patterns, not approximations: the constants ee-gcc
   folded, the X/Open wrapper results, exact operations. Any mode. */
static void check_exact(const char *env)
{
    static const float inf = __builtin_inff();

    struct {
        const char *name;
        float got;
        uint32_t want;
    } ex[] = {
        {"atan2f(1, 0)", ico_atan2f(1.0f, 0.0f), 0x3fc90fdb},
        {"atan2f(-1, 0)", ico_atan2f(-1.0f, 0.0f), 0xbfc90fdb},
        {"atan2f(0, -1)", ico_atan2f(0.0f, -1.0f), 0x40490fda},
        {"atan2f(-0, -1)", ico_atan2f(-0.0f, -1.0f), 0xc0490fda},
        {"atan2f(inf, inf)", ico_atan2f(inf, inf), 0x3f490fdb},
        {"atan2f(inf, -inf)", ico_atan2f(inf, -inf), 0x4016cbe4},
        {"atan2f(1, -inf)", ico_atan2f(1.0f, -inf), 0x40490fda},
        {"atan2f(-1, inf)", ico_atan2f(-1.0f, inf), 0x80000000},
        {"atan2f(1e30, 1e-10)", ico_atan2f(1e30f, 1e-10f), 0x3fc90fdc},
        {"atan2f(0, 0)", ico_atan2f(0.0f, 0.0f), 0x00000000},
        {"atan2f(0, -0)", ico_atan2f(0.0f, -0.0f), 0x00000000},
        {"acosf(1)", ico_acosf(1.0f), 0x00000000},
        {"acosf(-1)", ico_acosf(-1.0f), 0x40490fdb},
        {"acosf(1e-20)", ico_acosf(1e-20f), 0x3fc90fdb},
        {"acosf(2)", ico_acosf(2.0f), 0x00000000},
        {"asinf(-3)", ico_asinf(-3.0f), 0x00000000},
        {"asinf(1e-10)", ico_asinf(1e-10f), 0x2edbe6ff},
        {"fmodf(1, 0)", fmodf_domain(), 0x7fb00000},
        {"fmodf(6, 2)", ico_fmodf(6.0f, 2.0f), 0x00000000},
        {"fmodf(-6, 2)", ico_fmodf(-6.0f, 2.0f), 0x80000000},
        {"sqrtf(4)", ico_sqrtf(4.0f), 0x40000000},
        {"sqrtf(2)", ico_sqrtf(2.0f), 0x3fb504f3},
        {"sqrtf(0)", ico_sqrtf(0.0f), 0x00000000},
        {"sinf(0)", ico_sinf(0.0f), 0x00000000},
        {"cosf(0)", ico_cosf(0.0f), 0x3f800000},
    };

    size_t i;

    for (i = 0; i < sizeof ex / sizeof ex[0]; i++) {
        CHECK(fbits(ex[i].got) == ex[i].want, "[%s] %s: 0x%08x want 0x%08x", env, ex[i].name,
              (unsigned)fbits(ex[i].got), (unsigned)ex[i].want);
    }
}

int main(void)
{
    static const double want[NVALS] = {
        0.78539816339744830962,
        -2.35619449019234492885,
        0.46364760900080611621,
        1.04719755119659774615,
        2.09439510239319549231,
        1.31811607165281796574,
        0.52359877559829887308,
        0.25268025514207865349,
        0.84147098480789650665,
        0.47942553860420300027,
        -0.50636564110975879061,
        -0.34999350217129295211,
        0.54030230586813971740,
        -0.98999249660044545727,
        1.5,
        -1.25,
        1.41421356237309504880,
        0.70710678118654752440,
    };
    size_t i;

    /* Line-buffered, so a float trap (fptrap preset) leaves the lines
       before it in the ctest log. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    ico_fpenv_host_enter();
    for (i = 0; i < NVALS; i++)
        refs[i] = (float)want[i];

    test_rand();
    test_qsort();

    printf("libm, host default environment (round to nearest):\n");
    check_vals("rn", 2);
    check_exact("rn");

    ico_fpenv_sim_enter();
    printf("libm, simulation environment (round toward zero, FTZ/DAZ):\n");
    check_vals("rz", 3);
    check_exact("rz");
    /* atanf's huge-argument sum is computed at run time, as on the EE:
       atan2f(y, 1) with |y| >= 2^34 reaches it. RTZ gives 0x3fc90fda,
       round to nearest 0x3fc90fdb. */
    {
        float a = ico_atan2f(1e20f, 1.0f);
        CHECK(fbits(a) == 0x3fc90fda, "[rz] atan2f(1e20, 1): 0x%08x want 0x3fc90fda",
              (unsigned)fbits(a));
    }
    ico_fpenv_host_enter();
    {
        float a = ico_atan2f(1e20f, 1.0f);
        CHECK(fbits(a) == 0x3fc90fdb, "[rn] atan2f(1e20, 1): 0x%08x want 0x3fc90fdb",
              (unsigned)fbits(a));
    }

    if (failures)
        printf("%d check(s) failed\n", failures);
    else
        printf("all checks passed\n");
    return failures != 0;
}
