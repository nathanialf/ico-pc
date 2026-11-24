/* rd_vu_compact_test.c: the index compaction's pure parts (rd_vu_compact.h,
 * CPU): the plan's places, groups, raises and refusals, the counts' sums,
 * the counts' base, the window's line word for word and cut short, the
 * tests' switch's words, the device's class by name; the Mali-G52's form's
 * lists (rd_vu_cmp_fill), its start-up test's read-back check of every kind
 * of triangle and its words, the window's line's CPU clause. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
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

/* the Mali-G52's form: the CPU's lists (raised mod 2^32), the read-back
   check of each kind of triangle against the workgroups' counts and the
   test's geometry, and its words; the window's line's CPU clause */
static void test_form(void)
{
    /* two triangles (kicks 2 and 3), raised by 2^32 - 8: values 0..6 */
    static const uint32_t src[] = {8, 9, 10, 12, 13, 14};
    const uint32_t raise = 0u - 8u;
    uint32_t dst[6];
    rd_vu_cmp_fill(dst, src, 6, raise);
    CHECK(dst[0] == 0u && dst[2] == 2u && dst[3] == 4u && dst[5] == 6u);
    rd_vu_cmp_fill(dst, src, 0, raise); /* nothing */
    /* read back: the first as written (kept), the second a filler (13 - 8
       = 5 three times); the workgroup counted 1 kept, 1 outside */
    const uint32_t tex1[4] = {0, 1, 1, 1};
    uint32_t got[6] = {0, 1, 2, 5, 5, 5};
    RdVuCmpCheck r;
    rd_vu_cmp_check_reset(&r);
    CHECK(r.first == UINT32_MAX);
    CHECK(rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_ANY));
    CHECK(rd_vu_cmp_check_ok(&r) && r.tris == 2 && r.asWritten == 1 && r.fillers == 1 &&
          r.leftOut == 1 && r.first == UINT32_MAX);
    char b[512];
    size_t n = rd_vu_cmp_check_text(b, sizeof b, &r);
    const char *want = "2 triangles read back: 1 as the CPU wrote them, 1 fillers where the GPU "
                       "counted 1 left out";
    if (strcmp(b, want) != 0) {
        printf("got:  %s\nwant: %s\n", b, want);
        failures++;
    }
    CHECK(n == strlen(b));
    /* the same against the geometry: all kept fails (the filler), all left
       out fails (the first as written) */
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_KEPT));
    CHECK(r.unexpected == 1 && r.first == 1 && !rd_vu_cmp_check_ok(&r));
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_LEFT));
    CHECK(r.unexpected == 1 && r.first == 0);
    /* a filler store lost in part (the third cause's sign): mixed and
       missing */
    got[3] = 4; /* corner 0 as written, 1 and 2 the filler */
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_ANY));
    CHECK(r.mixed == 1 && r.missing == 1 && r.wrong == 0 && r.first == 1);
    (void)rd_vu_cmp_check_text(b, sizeof b, &r);
    CHECK(strstr(b, "; 1 mixed (a filler store lost in part); 1 filler writes missing (an "
                    "unwritten filler leaves its triangle as it was)") != NULL);
    /* a filler never written: the triangle as written, missing alone */
    got[3] = 4;
    got[4] = 5;
    got[5] = 6;
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_ANY));
    CHECK(r.mixed == 0 && r.missing == 1 && r.asWritten == 2);
    /* a stale or lost list: a value neither the CPU's nor the filler */
    got[4] = 77;
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, got, src, 6, raise, tex1, RD_VU_CMP_EXPECT_ANY));
    CHECK(r.wrong == 1);
    (void)rd_vu_cmp_check_text(b, sizeof b, &r);
    CHECK(strstr(b, "; 1 triangles with a value neither the CPU's nor a filler (a stale or lost "
                    "list)") != NULL);
    /* fillers past the count, counts not the triangles */
    uint32_t all[6] = {1, 1, 1, 5, 5, 5};
    const uint32_t tex0[4] = {1, 0, 1, 1}; /* one left out counted, two fillers */
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, all, src, 6, raise, tex0, RD_VU_CMP_EXPECT_ANY));
    CHECK(r.extra == 1 && r.badCounts == 0);
    const uint32_t texBad[4] = {0, 0, 5, 1};
    rd_vu_cmp_check_reset(&r);
    CHECK(!rd_vu_cmp_check(&r, all, src, 6, raise, texBad, RD_VU_CMP_EXPECT_ANY));
    CHECK(r.badCounts == 1);
    /* two workgroups: 65 triangles, the second's count its own */
    {
        uint32_t s2[195], g2[195];
        for (uint32_t t = 0; t < 65u; t++) {
            for (uint32_t k = 0; k < 3u; k++) {
                s2[3u * t + k] = 4u * (t + 2u) + k;
            }
        }
        rd_vu_cmp_fill(g2, s2, 195, 0);
        g2[192] = g2[194] = g2[193]; /* triangle 64: a filler */
        const uint32_t tex2[8] = {0, 0, 64, 1, 1, 0, 0, 1};
        rd_vu_cmp_check_reset(&r);
        CHECK(rd_vu_cmp_check(&r, g2, s2, 195, 0, tex2, RD_VU_CMP_EXPECT_ANY));
        CHECK(r.tris == 65 && r.fillers == 1 && r.leftOut == 1 && rd_vu_cmp_check_ok(&r));
    }
    /* the window's line with the CPU's lists: per picture over C */
    RdVuCmpCounts c;
    memset(&c, 0, sizeof(c));
    c.kept[0] = 10;
    c.pictures = 1;
    c.cpuBytes = 2u * 1024u * 512u;
    c.cpuNs = 1500000u;
    c.cpuPictures = 2;
    (void)rd_vu_cmp_block_text(b, sizeof b, &c);
    CHECK(strstr(b, "two frame slots behind); the lists written by the CPU first (the Mali-G52's "
                    "form): 512 KB and 0.75 ms a picture over 2 pictures") != NULL);
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

static void test_class(void)
{
    /* issue 179's device name and vendor (its log: "Mali-G52 MC2",
       13b5): the Mali-G52; another Mali, through MoltenVK, no name: not;
       the tests' switch on any device; the DXT by the PowerVR rule's name */
    CHECK(rd_vu_cmp_class("Mali-G52 MC2", 0x13B5u, false, false, false) == ICO_ATT_CMP_G52);
    CHECK(rd_vu_cmp_class("Mali-G52", 0x13B5u, false, false, false) == ICO_ATT_CMP_G52);
    CHECK(rd_vu_cmp_class("Mali-G57 MC2", 0x13B5u, false, false, false) == ICO_ATT_CMP_OTHER);
    CHECK(rd_vu_cmp_class("Mali-G52 MC2", 0x13B5u, true, false, false) == ICO_ATT_CMP_OTHER);
    CHECK(rd_vu_cmp_class("Mali-G52 MC2", 0x10DEu, false, false, false) == ICO_ATT_CMP_OTHER);
    CHECK(rd_vu_cmp_class(NULL, 0x13B5u, false, false, false) == ICO_ATT_CMP_OTHER);
    CHECK(rd_vu_cmp_class("llvmpipe (LLVM 15.0.7, 256 bits)", 0x10005u, false, false, true) ==
          ICO_ATT_CMP_G52);
    CHECK(rd_vu_cmp_class("PowerVR D-Series DXT-48-1536 MC1", 0x1010u, false, true, false) ==
          ICO_ATT_CMP_DXT);
    CHECK(rd_vu_cmp_class("PowerVR B-Series BXM-8-256", 0x1010u, false, false, false) ==
          ICO_ATT_CMP_OTHER);
}

int main(void)
{
    test_class();
    test_plan();
    test_counts();
    test_text();
    test_form();
    test_env();
    if (failures) {
        printf("rd_vu_compact_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_vu_compact_test: ok\n");
    return 0;
}
