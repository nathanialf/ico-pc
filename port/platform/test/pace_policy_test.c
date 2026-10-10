/*
 * port/platform/test/pace_policy_test.c
 *
 * The slow-present decision (pace_policy.h): the threshold, entering after 3
 * presents above it and leaving below 80 % of it, no flicker on a borderline
 * sequence, one spike changing nothing, the injector's higher threshold.
 * Resolution "auto"'s steps (testAutoResolution).  The gap between presents
 * (testPresentGap).
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
    return pace_slow_present(h, ms10 * MS / 10, REFRESH, PERIOD, inj);
}

/* The window's loop over pace_auto_resolution_step: a sample
   per present at 60 a second for sec seconds, each cost ms10 tenths of a
   ms; every 2 s a step decision, a new window, the step's time kept */
static int autoRun(PaceSamples *s, uint64_t *clock, int scale, float winScale, uint64_t ms10,
                   int sec, int *steps)
{
    if (s->firstNs == 0) {
        pace_samples_reset(s, *clock);
    }
    for (int i = 0; i < sec * 60; i++) {
        *clock += 16666667ull;
        pace_samples_add(s, *clock, ms10 * MS / 10);
        if (s->lastNs - s->firstNs >= PACE_AUTO_WINDOW_NS) {
            const int next = pace_auto_resolution_step(s, scale, winScale);
            if (next != scale) {
                scale = next;
                s->lastStepNs = *clock;
                ++*steps;
            }
            pace_samples_reset(s, *clock);
        }
    }
    return scale;
}

static void testAutoResolution(void)
{
    PaceSamples s = {0};
    uint64_t clock = 1000 * MS;
    int steps = 0, scale;

    s.budgetNs = 16666667ull; /* 1/60 s: 70 % is 11.67 ms */
    /* under the budget's 70 %: never a step, however long */
    scale = autoRun(&s, &clock, 0, 2.41f, 110, 20, &steps);
    CHECK(scale == 0 && steps == 0);
    /* over it: nothing before 2 s of samples, then one step; a 2400 x 1080
       window (2.41x) goes to 2x, not 3x (larger than the window) */
    pace_samples_reset(&s, clock);
    for (int i = 0; i < 100; i++) {
        clock += 16666667ull;
        pace_samples_add(&s, clock, 14 * MS);
    }
    CHECK(pace_auto_resolution_step(&s, 0, 2.41f) == 0); /* 1.65 s */
    pace_samples_reset(&s, clock);
    steps = 0;
    scale = autoRun(&s, &clock, 0, 2.41f, 140, 2, &steps);
    CHECK(scale == 2 && steps == 1);
    /* a 4K window (4.82x): window -> 3x */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        int n = 0;
        k.budgetNs = 16666667ull;
        CHECK(autoRun(&k, &c, 0, 4.82f, 140, 2, &n) == 3 && n == 1);
        /* then one step per 2 s: 6 s of slow presents reach 1x, no faster */
        CHECK(autoRun(&k, &c, 3, 4.82f, 140, 2, &n) == 2 && n == 2);
        CHECK(autoRun(&k, &c, 2, 4.82f, 140, 2, &n) == 1 && n == 3);
        /* the floor: 1x stays 1x */
        CHECK(autoRun(&k, &c, 1, 4.82f, 300, 10, &n) == 1 && n == 3);
    }
    /* at most one step per 2 s: a step at time t, then a full window of
       slow samples (over 2 s of them) ending under 2 s after it changes
       nothing */
    {
        PaceSamples k = {0};
        k.budgetNs = 16666667ull;
        k.lastStepNs = 10000 * MS;
        for (int i = 0; i < 66; i++) {
            pace_samples_add(&k, 8800 * MS + (uint64_t)i * 33 * MS, 20 * MS); /* to 10.945 s */
        }
        CHECK(pace_auto_resolution_step(&k, 3, 4.82f) == 3); /* 0.95 s after the step */
        pace_samples_add(&k, 12000 * MS, 20 * MS);
        CHECK(pace_auto_resolution_step(&k, 3, 4.82f) == 2); /* 2 s after it */
    }
    /* never back up: fast presents at 1x or 2x leave the scale */
    steps = 0;
    CHECK(autoRun(&s, &clock, 2, 2.41f, 10, 20, &steps) == 2 && steps == 0);
    CHECK(autoRun(&s, &clock, 1, 2.41f, 10, 20, &steps) == 1 && steps == 0);
    /* a window at 1x or below has no step below it */
    CHECK(autoRun(&s, &clock, 0, 1.0f, 300, 10, &steps) == 0 && steps == 0);
    /* a fixed 30 a second limit: its period is the budget (70 %: 23.3 ms) */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        int n = 0;
        k.budgetNs = 33333333ull;
        CHECK(autoRun(&k, &c, 0, 2.41f, 200, 6, &n) == 0 && n == 0);
        CHECK(autoRun(&k, &c, 0, 2.41f, 250, 2, &n) == 2 && n == 1);
    }
    /* the median: one slow present in eight changes nothing */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        k.budgetNs = 16666667ull;
        for (int i = 0; i < 160; i++) {
            c += 16666667ull;
            pace_samples_add(&k, c, i % 8 == 0 ? 40 * MS : 5 * MS);
        }
        CHECK(pace_samples_median(&k) == 5 * MS);
        CHECK(pace_auto_resolution_step(&k, 0, 2.41f) == 0);
    }
}

/* pace_present_gap: "uncapped" with vsync presents a sixteenth of a refresh
   early, in mailbox mode as under FIFO, on every platform, and never fewer
   than sixty a second whatever refresh the display reports; a limit is its
   own period; "uncapped" without vsync has no gap */
static void testPresentGap(void)
{
    static const struct {
        int cap;
        bool uncappedVsync, mailbox;
        uint64_t refresh, gap;
    } t[] = {
        /* 60 Hz, 90 Hz (a Steam Deck OLED), 144 Hz */
        {-1, true, true, 16666667ull, 16666667ull - 16666667ull / 16},
        {-1, true, true, 11111111ull, 11111111ull - 11111111ull / 16},
        {-1, true, true, 6944444ull, 6944444ull - 6944444ull / 16},
        /* a screen reported at 30 Hz presents as if it ran at 60 in mailbox
           mode; under FIFO a present would block, so it keeps its rate */
        {-1, true, true, 33333333ull, 16666667ull - 16666667ull / 16},
        {-1, true, false, 33333333ull, 33333333ull - 33333333ull / 16},
        {-1, true, false, 20000000ull, 20000000ull - 20000000ull / 16},
        {-1, false, true, 11111111ull, 0},
        {0, false, false, 16666667ull, 0},
        {120, false, false, 11111111ull, 8333333ull},
        {60, false, false, 6944444ull, 16666666ull},
    };

    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        const uint64_t g =
            pace_present_gap(t[i].cap, t[i].uncappedVsync, t[i].mailbox, t[i].refresh);
        if (g != t[i].gap) {
            printf("FAIL present gap row %u: %llu, want %llu\n", i, (unsigned long long)g,
                   (unsigned long long)t[i].gap);
            fails++;
        }
    }
    /* a little under a refresh: never more than 16 presents in 15 refreshes,
       and a late present still leaves the next refresh a new picture */
    for (uint64_t r = 4000000ull; r <= 33333334ull; r += 1234567ull) {
        const uint64_t g = pace_present_gap(-1, true, true, r);
        const uint64_t e = r < 16666667ull ? r : 16666667ull;
        CHECK(g >= e * 15 / 16);
        CHECK(g < e);
        const uint64_t f = pace_present_gap(-1, true, false, r);
        CHECK(f >= r * 15 / 16);
        CHECK(f < r);
    }
}

int main(void)
{
    PaceHist h = {0};
    int i, flips;
    bool last;

    /* max(refresh, period) + period / 2, and that plus a refresh with an effects program */
    CHECK(pace_slow_threshold(REFRESH, PERIOD, false) == PERIOD + PERIOD / 2);
    CHECK(pace_slow_threshold(33333ull * 1000, PERIOD, false) == 33333ull * 1000 + PERIOD / 2);
    CHECK(pace_slow_threshold(REFRESH, PERIOD, true) == PERIOD + REFRESH + PERIOD / 2);
    /* 144 Hz and 50 Hz displays against the 60 Hz game period: never below
       the plain threshold (2 * refresh + period / 2 was, at 144 Hz) */
    CHECK(pace_slow_threshold(6944444ull, PERIOD, true) == PERIOD + 6944444ull + PERIOD / 2);
    CHECK(pace_slow_threshold(6944444ull, PERIOD, true) >
          pace_slow_threshold(6944444ull, PERIOD, false));
    CHECK(pace_slow_threshold(20000000ull, PERIOD, true) == 20000000ull + 20000000ull + PERIOD / 2);
    CHECK(pace_slow_threshold(20000000ull, PERIOD, true) >
          pace_slow_threshold(20000000ull, PERIOD, false));

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

    testAutoResolution();
    testPresentGap();

    if (fails) {
        printf("pace_policy_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_policy_test: ok\n");
    return 0;
}
