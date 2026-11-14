/* rd_health_test.c: the picture-health report's rule (rd_health.h, CPU),
 * fed the patterns of the four logs it was checked against: issue 179's
 * v0.11.6 Mali-G52 runs meet it at replay 164; the same phone's v0.11.5 run,
 * issue 123's Adreno 740 runs (isolated hangs, a few restarts), an idle
 * GPU's restarts and unjudged pictures never do; the window's threshold;
 * met once a run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_health.h"
#include "rd_perf_clock.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* issue 179's v0.11.6 runs: every 3D picture from replay 149 on unfinished
   (stall lines 1 to 32 at replays 149 to 180 in both logs, then every 10th
   to the end); met at the 16th, once */
static void test_mali(void)
{
    RdHealth h;
    rd_health_reset(&h);
    uint32_t metAt = 0, met = 0, fails = 0, judged = 0;
    for (uint32_t r = 149; r < 2000; r++) {
        if (rd_health_pic(&h, true, RD_RESET_HANG, &fails, &judged)) {
            met++;
            metAt = r;
            CHECK(fails == 16 && judged == 16);
        }
    }
    CHECK(met == 1 && metAt == 164 && h.met);
    /* the same with restarts alone (counted with work queued) */
    rd_health_reset(&h);
    met = 0;
    for (uint32_t r = 1; r < 100; r++) {
        if (rd_health_pic(&h, true, RD_RESET_RESTART, NULL, NULL)) {
            met++;
            metAt = r;
        }
    }
    CHECK(met == 1 && metAt == 16);
}

/* the logs where it must not be met */
static void test_quiet(void)
{
    RdHealth h;
    /* issue 179's v0.11.5 run: no stamp unwritten, no restart (its 110
       stall lines are waits of 100 ms or more: no sign) */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 8000; r++) {
        CHECK(!rd_health_pic(&h, true, RD_RESET_NONE, NULL, NULL));
    }
    /* issue 123's Adreno 740, v0.11.6: 13 hangs in about 325 s at its
       picture rate (replays 2074 ... 14734, the closest 3 within 6
       replays), a counted restart beside the 10910 hang, at most 4 restarts
       in a 10 s block (here all 4 beside the closest hangs), and its 8
       start-up restarts (replays 8 to 26) counted too */
    static const uint32_t hangs[] = {2074,  6160,  8148,  8237,  10905, 10910, 11089,
                                     11311, 11970, 14437, 14729, 14731, 14734};
    static const uint32_t restarts[] = {8,  11,    17,    19,    21,    24,   25,
                                        26, 10911, 14728, 14730, 14732, 14733};
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 16300; r++) {
        int sign = RD_RESET_NONE;
        for (size_t i = 0; i < sizeof hangs / sizeof hangs[0]; i++) {
            sign = hangs[i] == r ? RD_RESET_HANG : sign;
        }
        for (size_t i = 0; i < sizeof restarts / sizeof restarts[0]; i++) {
            sign = restarts[i] == r ? RD_RESET_RESTART : sign;
        }
        CHECK(!rd_health_pic(&h, true, sign, NULL, NULL));
    }
    /* an idle GPU's restarts (the app away, a pause, a capture) on every
       picture */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 500; r++) {
        CHECK(!rd_health_pic(&h, true, RD_RESET_IDLE, NULL, NULL));
    }
    /* pictures not judged (2D, not presented) never count */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 500; r++) {
        CHECK(!rd_health_pic(&h, false, RD_RESET_HANG, NULL, NULL));
    }
    /* the window: 15 of 32 never, the 16th of 32 does (every other picture
       unfinished: the 16th at the 31st picture) */
    rd_health_reset(&h);
    uint32_t fails = 0, judged = 0, metAt = 0;
    for (uint32_t r = 0; r < 64; r++) {
        if (rd_health_pic(&h, true, r % 2 == 0 ? RD_RESET_HANG : RD_RESET_NONE, &fails, &judged)) {
            CHECK(metAt == 0);
            metAt = r + 1;
        }
    }
    CHECK(metAt == 31 && fails == 16 && judged == 31);
}

int main(void)
{
    test_mali();
    test_quiet();
    if (failures) {
        printf("rd_health_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_health_test: ok\n");
    return 0;
}
