/*
 * port/platform/test/core_share_test.c
 *
 * The share of run time on the faster cores (core_share.h): issue 179's
 * Helio G85 (cores 6-7 faster) with the submit thread mostly on the slow
 * cores, the audio threads on them alone, a driver thread, two pipeline
 * workers under one name; the Pixel's 2-7 over 0-1; nothing without a
 * fast mask or an unknown core; a full table; the stat parser with a name
 * holding spaces and brackets; a short buffer (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "core_share.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void text_is(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n got  %s\n want %s\n", line, got, want);
        failures++;
    }
}

int main(void)
{
    CoreShare s;
    char t[640];
    const uint64_t helio = 0xc0, pixel = 0xfc;

    core_share_reset(&s);
    CHECK(core_share_text(&s, helio, t, sizeof t) == 0 && t[0] == '\0');
    /* ten seconds: the submit thread one of them on core 6 */
    for (int k = 0; k < 10; k++) {
        core_share_add(&s, "submit", k == 3 ? 6 : 2, 30000000u, helio);
        core_share_add(&s, "SDL audio", 1, 40000000u, helio);
        core_share_add(&s, "mali-cmar-backe", k < 5 ? 7 : 4, 100000000u, helio);
    }
    core_share_add(&s, "pipeline workers", 6, 1000000000u, helio);
    core_share_add(&s, "pipeline workers", 0, 1000000000u, helio);
    core_share_add(&s, "system audio", -1, 5000000u, helio); /* core not known */
    core_share_add(&s, "system audio", 64, 5000000u, helio); /* past 63 */
    core_share_add(&s, "system audio", 3, 5000000u, 0);      /* no fast mask */
    (void)core_share_text(&s, helio, t, sizeof t);
    text_is(t,
            "run time on the faster cores 6-7: submit 10 % of 0.3 s, SDL audio 0 % of 0.4 s, "
            "mali-cmar-backe 50 % of 1.0 s, pipeline workers 50 % of 2.0 s (each thread's last "
            "core once a second, weighted by its run time in that second)",
            __LINE__);
    CHECK(core_share_text(&s, 0, t, sizeof t) == 0); /* cores of one speed */

    /* the Pixel: 2-7 faster */
    core_share_reset(&s);
    core_share_add(&s, "submit", 5, 200000000u, pixel);
    core_share_add(&s, "submit", 1, 200000000u, pixel);
    (void)core_share_text(&s, pixel, t, sizeof t);
    text_is(t,
            "run time on the faster cores 2-7: submit 50 % of 0.4 s (each thread's last core once "
            "a second, weighted by its run time in that second)",
            __LINE__);

    /* a full table drops a new name */
    core_share_reset(&s);
    for (int i = 0; i < CORE_SHARE_MAX + 2; i++) {
        char n[16];
        snprintf(n, sizeof n, "t%d", i);
        core_share_add(&s, n, 7, 1000000u, helio);
    }
    CHECK(s.n == CORE_SHARE_MAX);
    {
        char small[24];
        const size_t n = core_share_text(&s, helio, small, sizeof small);
        CHECK(n == strlen(small) && n == sizeof small - 1u);
    }

    /* field 39 after the command's closing bracket */
    int core = -1;
    const char *st = "1234 (mali-cmar (b) x) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 "
                     "21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 5 0\n";
    CHECK(core_share_parse_stat(st, strlen(st), &core) && core == 5);
    const char *shortSt = "1234 (x) S 1 2 3\n";
    CHECK(!core_share_parse_stat(shortSt, strlen(shortSt), &core));
    CHECK(!core_share_parse_stat("no bracket", 10, &core));

    if (failures) {
        printf("core_share_test: %d failures\n", failures);
        return 1;
    }
    printf("core_share_test: ok\n");
    return 0;
}
