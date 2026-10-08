/*
 * port/platform/test/pace_policy_test.c
 *
 * The slow-present decision (pace_policy.h): the threshold, entering after 3
 * presents above it and leaving below 80 % of it, no flicker on a borderline
 * sequence, one spike changing nothing, the injector's higher threshold.
 */
#include <stdio.h>
#include "pace_policy.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define REFRESH (16667ull * 1000)
#define PERIOD (16683ull * 1000)

static bool feed(PaceHist *h, uint64_t ms10, bool inj)
{
    return pace_SlowPresent(h, ms10 * MS / 10, REFRESH, PERIOD, inj);
}

int main(void)
{
    PaceHist h = {0};
    int i, flips;
    bool last;

    /* max(refresh, period) + period / 2, and that plus a refresh with an effects program */
    CHECK(pace_SlowThreshold(REFRESH, PERIOD, false) == PERIOD + PERIOD / 2);
    CHECK(pace_SlowThreshold(33333ull * 1000, PERIOD, false) == 33333ull * 1000 + PERIOD / 2);
    CHECK(pace_SlowThreshold(REFRESH, PERIOD, true) == PERIOD + REFRESH + PERIOD / 2);
    /* 144 Hz and 50 Hz displays against the 60 Hz game period: never below
       the plain threshold (2 * refresh + period / 2 was, at 144 Hz) */
    CHECK(pace_SlowThreshold(6944444ull, PERIOD, true) == PERIOD + 6944444ull + PERIOD / 2);
    CHECK(pace_SlowThreshold(6944444ull, PERIOD, true) >
          pace_SlowThreshold(6944444ull, PERIOD, false));
    CHECK(pace_SlowThreshold(20000000ull, PERIOD, true) == PERIOD + 20000000ull + PERIOD / 2);
    CHECK(pace_SlowThreshold(20000000ull, PERIOD, true) >
          pace_SlowThreshold(20000000ull, PERIOD, false));

    /* a steady vsync-blocked present (one refresh) is never slow */
    for (i = 0; i < 40; i++) {
        CHECK(!feed(&h, 167, false));
    }

    /* one spike among normal presents changes nothing */
    CHECK(!feed(&h, 1000, false));
    for (i = 0; i < 10; i++) {
        CHECK(!feed(&h, 167, false));
    }

    /* a sustained slow renderer (30 ms against a 25 ms threshold) turns it
       on within a few presents and keeps it on */
    h = (PaceHist){0};
    for (i = 0; i < 8; i++) {
        feed(&h, 167, false);
    }
    flips = 0;
    for (i = 0; i < 20; i++) {
        flips += feed(&h, 300, false) ? 1 : 0;
    }
    CHECK(h.slow);
    CHECK(flips >= 10);

    /* back to normal: off once the median is below 80 % of the threshold
       (20 ms), and not before */
    for (i = 0; i < 20; i++) {
        feed(&h, 167, false);
    }
    CHECK(!h.slow);

    /* hysteresis: in the slow state, a cost between 80 % and 100 % of the
       threshold (22 ms against 25 ms) stays slow ... */
    h = (PaceHist){0};
    for (i = 0; i < 12; i++) {
        feed(&h, 300, false);
    }
    CHECK(h.slow);
    for (i = 0; i < 20; i++) {
        CHECK(feed(&h, 220, false));
    }
    /* ... and in the normal state the same cost stays normal */
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        CHECK(!feed(&h, 220, false));
    }

    /* a borderline sequence around the threshold (24 and 26 ms alternating)
       does not flicker: at most one change in 40 presents */
    h = (PaceHist){0};
    flips = 0;
    last = false;
    for (i = 0; i < 40; i++) {
        const bool s = feed(&h, i % 2 ? 260 : 240, false);
        flips += s != last;
        last = s;
    }
    CHECK(flips <= 1);

    /* the same with random-looking jitter straddling the threshold */
    h = (PaceHist){0};
    flips = 0;
    last = false;
    for (i = 0; i < 80; i++) {
        static const unsigned c[] = {230, 270, 250, 260, 240, 280, 220, 255};
        const bool s = feed(&h, c[i % 8], false);
        flips += s != last;
        last = s;
    }
    CHECK(flips <= 1);

    /* an effects program: 30 ms is slow without one, not slow with one
       (threshold 2 * 16.7 + 8.3 = 41.7 ms); 50 ms is slow with one */
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        CHECK(!feed(&h, 300, true));
    }
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        feed(&h, 500, true);
    }
    CHECK(h.slow);

    if (fails) {
        printf("pace_policy_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_policy_test: ok\n");
    return 0;
}
