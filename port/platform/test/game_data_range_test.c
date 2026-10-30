/*
 * port/platform/test/game_data_range_test.c: the game data ranges
 * (game_data_range.c) on this toolchain's compiler and linker, over a
 * stand-in game library (game_data_fixture.c) that went through the same
 * section treatment as ico_game (cmake/IcoStateSections.cmake):
 *
 *   1. the ranges exist and are consistent;
 *   2. every writable object of the stand-in lies inside them, zero-filled
 *      ones in the bss range (where the build has one), and this program's
 *      own globals and the stand-in's read-only objects lie outside;
 *   3. the renamed sections kept their contents and relocations (initial
 *      values, zero fill, a table of addresses);
 *   4. a copy of the two ranges written back restores the stand-in's
 *      globals and touches nothing of this program's (a save state's
 *      GDATA/GBSS round trip);
 *   5. ico_game_data_ranges_check's failure texts and edges on made-up
 *      ranges.
 *
 * ico_pc's own verify (game_data_verify.c, the real game's globals) runs
 * at start-up; tools/check_game_data_ranges.py checks the program's symbol
 * table at build time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_data_range.h"

extern int fixture_d;
extern int fixture_d2[64];
extern int fixture_b[1000];
extern int fixture_z[16];
extern int *fixture_ptrs[3];
extern int *const fixture_cptrs[2];
extern const int fixture_k;
int *fixture_static_bss(void);
int *fixture_static_data(void);
int *fixture_local_bss(void);
int *fixture_local_data(void);

/* this program's own: outside */
int port_d = 7;
int port_b[50];
static int port_sd = 9;
static int port_sb[20];

static int failures;

/* AddressSanitizer poisons the red zones it puts between globals, and the
   ranges hold them: the round trip's plain copy would be reported (save
   states are off in sanitizer builds) */
#if defined(__SANITIZE_ADDRESS__)
#define GDR_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GDR_TEST_ASAN 1
#endif
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "game_data_range_test:%d: %s\n", __LINE__, #c);                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int in_range(const IcoMemRange *r, const void *p, size_t n)
{
    uintptr_t a = (uintptr_t)p;
    return a >= r->lo && a <= r->hi && n <= r->hi - a;
}

/* 1-3 */
static void test_ranges(const IcoMemRange r[2])
{
    char why[128];
    const IcoMemRange *d = &r[ICO_GAME_DATA];
    const IcoMemRange *b = &r[ICO_GAME_BSS];
    CHECK(d->lo != 0 && d->lo < d->hi);
    CHECK(b->lo <= b->hi);
    printf("game_data_range_test: data %#lx..%#lx (%lu bytes), bss %#lx..%#lx (%lu bytes)\n",
           (unsigned long)d->lo, (unsigned long)d->hi, (unsigned long)(d->hi - d->lo),
           (unsigned long)b->lo, (unsigned long)b->hi, (unsigned long)(b->hi - b->lo));

    const IcoGameDataProbe in[] = {
        {"fixture_d", &fixture_d, sizeof(fixture_d)},
        {"fixture_d2", fixture_d2, sizeof(int[64])},
        {"fixture_b", fixture_b, sizeof(int[1000])},
        {"fixture_z", fixture_z, sizeof(int[16])},
        {"fixture_ptrs", fixture_ptrs, sizeof(int *[3])},
        {"fixture_sb", fixture_static_bss(), sizeof(int[37])},
        {"fixture_sd", fixture_static_data(), sizeof(int)},
        {"local_b", fixture_local_bss(), sizeof(int[9])},
        {"local_d", fixture_local_data(), sizeof(int)},
    };
    const IcoGameDataProbe out[] = {
        {"port_d", &port_d, sizeof(port_d)},
        {"port_b", port_b, sizeof(port_b)},
        {"port_sd", &port_sd, sizeof(port_sd)},
        {"port_sb", port_sb, sizeof(port_sb)},
        {"fixture_cptrs", fixture_cptrs, sizeof(int *[2])},
        {"fixture_k", &fixture_k, sizeof(fixture_k)},
        {"failures", &failures, sizeof(failures)},
    };
    int rc = ico_game_data_ranges_check(r, in, (int)(sizeof(in) / sizeof(in[0])), out,
                                        (int)(sizeof(out) / sizeof(out[0])), why, sizeof(why));
    if (rc != 0)
        fprintf(stderr, "game_data_range_test: %s\n", why);
    CHECK(rc == 0);

    /* initialised data in the data range */
    CHECK(in_range(d, &fixture_d, sizeof(int)));
    CHECK(in_range(d, fixture_d2, sizeof(int[64])));
    CHECK(in_range(d, fixture_ptrs, sizeof(int *[3])));
    CHECK(in_range(d, fixture_static_data(), sizeof(int)));
    CHECK(in_range(d, fixture_local_data(), sizeof(int)));
#if defined(_WIN32) && defined(__clang__)
    /* clang's Windows build stores the zero-filled data with the rest: the
       bss range is the one-byte bracket */
    CHECK(in_range(d, fixture_b, sizeof(int[1000])));
    CHECK(b->hi - b->lo < 64);
#else
    CHECK(in_range(b, fixture_b, sizeof(int[1000])));
    CHECK(in_range(b, fixture_z, sizeof(int[16])));
    CHECK(in_range(b, fixture_static_bss(), sizeof(int[37])));
    CHECK(in_range(b, fixture_local_bss(), sizeof(int[9])));
#endif

    /* contents and relocations kept */
    CHECK(fixture_d == 5);
    CHECK(fixture_d2[0] == 1 && fixture_d2[2] == 3 && fixture_d2[63] == 0);
    CHECK(*fixture_static_data() == 11);
    CHECK(*fixture_local_data() == 4);
    CHECK(fixture_ptrs[0] == &fixture_d);
    CHECK(fixture_ptrs[1] == fixture_b);
    CHECK(fixture_ptrs[2] == fixture_static_data());
    CHECK(fixture_cptrs[0] == &fixture_d);
    CHECK(fixture_k == 3);
    int zero = 1;
    for (int i = 0; i < 1000; i++)
        zero &= fixture_b[i] == 0;
    for (int i = 0; i < 37; i++)
        zero &= fixture_static_bss()[i] == 0;
    CHECK(zero);
}

/* 4 */
#if !defined(GDR_TEST_ASAN)
static void test_round_trip(const IcoMemRange r[2])
{
    size_t nd = r[ICO_GAME_DATA].hi - r[ICO_GAME_DATA].lo;
    size_t nb = r[ICO_GAME_BSS].hi - r[ICO_GAME_BSS].lo;
    unsigned char *sd = malloc(nd ? nd : 1);
    unsigned char *sb = malloc(nb ? nb : 1);
    CHECK(sd != NULL && sb != NULL);
    if (sd == NULL || sb == NULL) {
        free(sd);
        free(sb);
        return;
    }
    memcpy(sd, (void *)r[ICO_GAME_DATA].lo, nd);
    memcpy(sb, (void *)r[ICO_GAME_BSS].lo, nb);

    fixture_d = 50;
    fixture_d2[10] = 77;
    fixture_b[999] = 123;
    fixture_z[3] = 8;
    fixture_ptrs[0] = NULL;
    *fixture_static_bss() = 6;
    *fixture_static_data() = 12;
    fixture_local_bss()[8] = 2;
    *fixture_local_data() = 40;
    port_d = 70;
    port_b[0] = 71;
    port_sd = 72;
    port_sb[19] = 73;

    memcpy((void *)r[ICO_GAME_DATA].lo, sd, nd);
    memcpy((void *)r[ICO_GAME_BSS].lo, sb, nb);

    CHECK(fixture_d == 5);
    CHECK(fixture_d2[10] == 0);
    CHECK(fixture_b[999] == 0);
    CHECK(fixture_z[3] == 0);
    CHECK(fixture_ptrs[0] == &fixture_d);
    CHECK(*fixture_static_bss() == 0);
    CHECK(*fixture_static_data() == 11);
    CHECK(fixture_local_bss()[8] == 0);
    CHECK(*fixture_local_data() == 4);
    /* the program's own globals are not part of the game's data */
    CHECK(port_d == 70);
    CHECK(port_b[0] == 71);
    CHECK(port_sd == 72);
    CHECK(port_sb[19] == 73);
    free(sd);
    free(sb);
}
#endif

/* 5 */
static void test_check(void)
{
    static unsigned char mem[256];
    IcoMemRange r[2] = {{(uintptr_t)&mem[16], (uintptr_t)&mem[64]},
                        {(uintptr_t)&mem[128], (uintptr_t)&mem[192]}};
    char why[64];

    IcoGameDataProbe ok_in[] = {{"a", &mem[16], 48}, {"b", &mem[128], 64}, {"c", &mem[63], 1}};
    IcoGameDataProbe ok_out[] = {{"x", &mem[0], 16}, {"y", &mem[64], 64}, {"z", &mem[192], 64}};
    CHECK(ico_game_data_ranges_check(r, ok_in, 3, ok_out, 3, why, sizeof(why)) == 0);
    CHECK(why[0] == '\0');

    /* an object running past the end of the data range */
    IcoGameDataProbe past[] = {{"a", &mem[16], 4}, {"straddle", &mem[60], 8}};
    CHECK(ico_game_data_ranges_check(r, past, 2, NULL, 0, why, sizeof(why)) == -1);
    CHECK(strcmp(why, "straddle is outside the game's data") == 0);
    /* one spanning both ranges and the gap is in neither */
    IcoGameDataProbe span[] = {{"span", &mem[16], 176}};
    CHECK(ico_game_data_ranges_check(r, span, 1, NULL, 0, why, sizeof(why)) == -1);
    /* a port object overlapping the bss range's first byte */
    IcoGameDataProbe edge[] = {{"x", &mem[0], 16}, {"edge", &mem[120], 9}};
    CHECK(ico_game_data_ranges_check(r, NULL, 0, edge, 2, why, sizeof(why)) == -1);
    CHECK(strcmp(why, "edge is inside the game's data") == 0);
    /* a zero-sized port object at a range's start counts as inside it */
    IcoGameDataProbe zero[] = {{"zero", &mem[16], 0}};
    CHECK(ico_game_data_ranges_check(r, NULL, 0, zero, 1, why, sizeof(why)) == -1);
    /* an empty bss range holds nothing */
    IcoMemRange e[2] = {r[0], {(uintptr_t)&mem[128], (uintptr_t)&mem[128]}};
    IcoGameDataProbe at[] = {{"at", &mem[128], 4}};
    CHECK(ico_game_data_ranges_check(e, NULL, 0, at, 1, why, sizeof(why)) == 0);
    CHECK(ico_game_data_ranges_check(e, at, 1, NULL, 0, why, sizeof(why)) == -1);
    /* the text is cut to the buffer */
    char small[8];
    CHECK(ico_game_data_ranges_check(r, past, 2, NULL, 0, small, sizeof(small)) == -1);
    CHECK(strlen(small) == sizeof(small) - 1);
    /* no buffer at all */
    CHECK(ico_game_data_ranges_check(r, past, 2, NULL, 0, NULL, 0) == -1);
}

int main(void)
{
    IcoMemRange r[2];
    int rc = ico_game_data_ranges(r);
    CHECK(rc == 0);
    if (rc == 0) {
        test_ranges(r);
#if defined(GDR_TEST_ASAN)
        printf("game_data_range_test: no round trip under AddressSanitizer\n");
#else
        test_round_trip(r);
#endif
    }
    test_check();
    if (failures) {
        fprintf(stderr, "game_data_range_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("game_data_range_test: ok\n");
    return 0;
}
