/* rd_vu_compact_test.c: the index compaction's pure parts (rd_vu_compact.h,
 * CPU): the plan's places, groups, raises and refusals, the counts' sums,
 * the counts' base, the window's line word for word and cut short, the
 * tests' switch's words. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_compact.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_plan(void)
{
    /* two kicks' triangles (kicks 2 and 3: values 8..10, 12..14), then one
       (kick 5: 20..22) */
    static const uint32_t src[] = {8, 9, 10, 12, 13, 14, 20, 21, 22};
    RdVuCmpPlan p;
    RdVuCmpCall a, b, c;
    rd_vu_cmp_plan_reset(&p, 4096, 0xFFFFFFu, true);
    CHECK(rd_vu_cmp_plan_call(&p, src, 0, 6, RD_VU_CMP_WHOLE, 1, &a));
    CHECK(a.first == 0 && a.count == 6 && a.at == 0 && a.group == 0);
    CHECK(a.lo == 8 && a.hi == 14);
    /* raised from 0: 8 + raise = 0 (mod 2^32) */
    CHECK((uint32_t)(a.lo + a.raise) == 0u && (uint32_t)(a.hi + a.raise) == 6u);
    CHECK(p.top == 7 && p.used == 6 && p.groups == 1 && p.calls == 1);
    CHECK(rd_vu_cmp_plan_call(&p, src, 6, 3, RD_VU_CMP_KICK, 0, &b));
    CHECK(b.at == 6 && b.group == 1 && b.lo == 20 && b.hi == 22 && b.pass == RD_VU_CMP_KICK);
    /* its values past the first call's: 7 .. 9 */
    CHECK((uint32_t)(b.lo + b.raise) == 7u && (uint32_t)(b.hi + b.raise) == 9u);
    CHECK(p.top == 10 && p.used == 9 && p.groups == 2 && b.list0 == 0);
    /* the counts' base: the texel after the lists (9 values: 3 texels) */
    CHECK(rd_vu_cmp_counts_base(&p) == 3u);
    /* an empty list, a count not of triangles, no list: refused */
    CHECK(!rd_vu_cmp_plan_call(&p, src, 0, 0, RD_VU_CMP_WHOLE, 1, &c));
    CHECK(!rd_vu_cmp_plan_call(&p, src, 0, 4, RD_VU_CMP_WHOLE, 1, &c));
    CHECK(!rd_vu_cmp_plan_call(&p, NULL, 0, 3, RD_VU_CMP_WHOLE, 1, &c));
    CHECK(p.refused == 3 && p.calls == 2 && p.used == 9);

    /* without the merge's builds: the values as they are, refused past the
       device's largest */
    rd_vu_cmp_plan_reset(&p, 4096, 13u, false);
    CHECK(rd_vu_cmp_plan_call(&p, src, 0, 3, RD_VU_CMP_CUT, 1, &a));
    CHECK(a.raise == 0 && p.top == 0);
    CHECK(!rd_vu_cmp_plan_call(&p, src, 3, 3, RD_VU_CMP_CUT, 1, &b)); /* 14 > 13 */

    /* raised past the device's largest: refused, the plan unmoved */
    rd_vu_cmp_plan_reset(&p, 4096, 10u, true);
    CHECK(rd_vu_cmp_plan_call(&p, src, 0, 6, RD_VU_CMP_WHOLE, 1, &a));  /* 0 .. 6 */
    CHECK(rd_vu_cmp_plan_call(&p, src, 6, 3, RD_VU_CMP_WHOLE, 1, &b));  /* 7 .. 9 */
    CHECK(!rd_vu_cmp_plan_call(&p, src, 6, 3, RD_VU_CMP_WHOLE, 1, &c)); /* 10 .. 12 */
    CHECK(p.top == 10 && p.calls == 2 && p.refused == 1);

    /* the plane's room: 16 values; 6 values and a texel of counts (12
       values) fit, 12 values and two texels (20) do not */
    rd_vu_cmp_plan_reset(&p, 16u, 0xFFFFFFu, true);
    CHECK(rd_vu_cmp_plan_call(&p, src, 0, 6, RD_VU_CMP_WHOLE, 1, &a));
    CHECK(!rd_vu_cmp_plan_call(&p, src, 0, 6, RD_VU_CMP_WHOLE, 1, &b));
    CHECK(p.used == 6 && p.groups == 1);

    /* 65 triangles: two workgroups of 64 */
    {
        uint32_t *big = malloc(195u * sizeof(uint32_t));
        CHECK(big != NULL);
        if (big) {
            for (uint32_t i = 0; i < 195u; i++) {
                big[i] = 8u + 4u * (i / 3u) + i % 3u;
            }
            rd_vu_cmp_plan_reset(&p, 4096, 0xFFFFFFu, true);
            CHECK(rd_vu_cmp_plan_call(&p, big, 0, 195, RD_VU_CMP_WHOLE, 1, &a));
            CHECK(p.groups == 2 && a.lo == 8 && a.hi == 8u + 4u * 64u + 2u);
            free(big);
        }
    }
}

static void test_counts(void)
{
    /* two workgroups' texels: list 0's and another list's */
    static const uint32_t t[8] = {3, 4, 5, 1, 1, 0, 2, 0};
    RdVuCmpCounts c;
    memset(&c, 0, sizeof(c));
    rd_vu_cmp_counts_add(&c, t, 2);
    CHECK(c.drop[0] == 3 && c.outside[0] == 4 && c.kept[0] == 5);
    CHECK(c.drop[1] == 1 && c.outside[1] == 0 && c.kept[1] == 2);
    CHECK(c.pictures == 1);
    CHECK(rd_vu_cmp_left_out(&c) == 8u);
    rd_vu_cmp_counts_add(&c, t, 0); /* a picture with no workgroup */
    CHECK(c.pictures == 2 && rd_vu_cmp_left_out(&c) == 8u);
}

static void test_text(void)
{
    RdVuCmpCounts c;
    memset(&c, 0, sizeof(c));
    c.drop[0] = 20;
    c.outside[0] = 40;
    c.kept[0] = 140;
    c.drop[1] = 2;
    c.kept[1] = 2;
    c.pictures = 2;
    char b[512];
    const size_t n = rd_vu_cmp_block_text(b, sizeof b, &c);
    const char *want = "window: index compaction: triangles left out as drawing nothing, a "
                       "picture: 30 of 100 in list 0 (30.0 %), by class: the game's drops 10, "
                       "outside the target or the depth range 20; the other lists 1 of 2 (50.0 "
                       "%) (2 pictures read back, two frame slots behind)";
    if (strcmp(b, want) != 0) {
        printf("got:  %s\nwant: %s\n", b, want);
        failures++;
    }
    CHECK(n == strlen(b));
    /* a short buffer: cut and terminated */
    char s[10];
    CHECK(rd_vu_cmp_block_text(s, sizeof s, &c) == 9u && strlen(s) == 9u);
    CHECK(rd_vu_cmp_block_text(s, 0, &c) == 0u);
    /* nothing judged: 0 % */
    memset(&c, 0, sizeof(c));
    (void)rd_vu_cmp_block_text(b, sizeof b, &c);
    CHECK(strstr(b, "a picture: 0 of 0 in list 0 (0.0 %)") != NULL);
}

static void test_env(void)
{
#ifdef _WIN32
    _putenv("ICO_RD_VU_COMPACT=");
    CHECK(rd_vu_cmp_env() == -1);
    _putenv("ICO_RD_VU_COMPACT=0");
    CHECK(rd_vu_cmp_env() == 0);
    _putenv("ICO_RD_VU_COMPACT=1");
    CHECK(rd_vu_cmp_env() == 1);
#else
    unsetenv("ICO_RD_VU_COMPACT");
    CHECK(rd_vu_cmp_env() == -1);
    setenv("ICO_RD_VU_COMPACT", "0", 1);
    CHECK(rd_vu_cmp_env() == 0);
    setenv("ICO_RD_VU_COMPACT", "off", 1);
    CHECK(rd_vu_cmp_env() == 0);
    setenv("ICO_RD_VU_COMPACT", "1", 1);
    CHECK(rd_vu_cmp_env() == 1);
    setenv("ICO_RD_VU_COMPACT", "on", 1);
    CHECK(rd_vu_cmp_env() == 1);
    setenv("ICO_RD_VU_COMPACT", "x", 1);
    CHECK(rd_vu_cmp_env() == -1);
    unsetenv("ICO_RD_VU_COMPACT");
#endif
}

int main(void)
{
    test_plan();
    test_counts();
    test_text();
    test_env();
    if (failures) {
        printf("rd_vu_compact_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_vu_compact_test: ok\n");
    return 0;
}
