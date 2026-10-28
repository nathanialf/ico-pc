/*
 * port/platform/test/pace_policy_test.c
 *
 * The slow-present decision (pace_policy.h): the threshold, entering after 3
 * presents above it and leaving below 80 % of it, no flicker on a borderline
 * sequence, one spike changing nothing, the injector's higher threshold.
 * Resolution "auto"'s steps (testAutoResolution).  The gap between presents
 * (testPresentGap).  The vsync lock's n and alphas (testLockTable), its
 * period's sanity (testGridPeriod: n refreshes within 1 % of the game's
 * period, a display 1 % off refused); its loop is pace_loop_test's.
 * The vblank clock's laws on a synthetic display (testVblankLaws): the 1 %
 * bound on the measured refresh, and the phase law's sign (the steps
 * follow a display that moves later or earlier, a sixteenth of a refresh
 * after its vblanks).
 * The late-present skip (testLateSkip*: a whole period behind skips, with
 * or without sound, less does not; slow presents skip past the deadline;
 * never two frames in a row, the same frame again allowed; never a frame
 * already presented, never before the deadline; the vsync lock's use of it
 * is pace_loop_test's testLateSkipLock; testLateSkipLoop: the window's loop under a fake clock, a 30
 * frames a second game on 39 ms presents keeps real time with no resync and
 * every other frame at least, the one-step rule fell behind).
 * The present allowance (testBudgetRule: the arm, the drop, hold, climb,
 * GPU gate, excuse and limits; testBudgetUnbound: the timer path's start
 * unbound, 15 presents a step never refused, a drop to max at most and the
 * climb back unbound; testBudgetHitch130, testBudgetSustained,
 * testBudgetGlWait126: the reporters' sequences from their logs;
 * testBudgetLines: the window's lines word for word; testBudgetTimerLoop: the timer path with a GPU queue,
 * nothing binds on a GPU that keeps up, back-to-back cheap presents
 * unchanged, the in-betweens dropped before a
 * step ends half a period late on one that does not, FIFO's display wait
 * excused: the whole-number allowance, vsync on or no GPU time);
 * testBudgetPaced: the timer path with vsync off paced by the GPU's time
 * (12 ms a picture 1,1,2 a step, 9 ms 1,2,2,2,1, 17 ms never two, 1 ms not
 * held, no timestamps as before, the late-step safety net and the straight
 * climb); testBudgetPacedLoop: the same under the fake clock, no step a
 * quarter of a period late, the presents the GPU's budget.
 */
#include <stdio.h>
#include <string.h>
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
   own period, except one above the refresh in mailbox mode, which presents
   as "uncapped" does (the clamp: 120 on 60 Hz); a limit equal to the
   refresh or below it, and any limit under FIFO, keep their own;
   "uncapped" without vsync has no gap */
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
        /* the clamp: 120 on 60 Hz in mailbox mode, once a refresh; on a
           screen reported at 30 Hz sixty a second; under FIFO 120's own */
        {120, false, true, 16666667ull, 16666667ull - 16666667ull / 16},
        {120, false, true, 33333333ull, 16666667ull - 16666667ull / 16},
        {120, false, false, 16666667ull, 8333333ull},
        /* equal (60 on 60.00, 144 on 144) or below the refresh: its own */
        {60, false, true, 16666667ull, 16666666ull},
        {144, false, true, 6944444ull, 6944444ull},
        {60, false, true, 8333333ull, 16666666ull},
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

/* The mode a numeric cap takes with vsync on: a cap equal to the
   refresh within 0.5 Hz takes FIFO with no timer gap; below it, mailbox
   with the cap's own gap; above it, mailbox and one present a refresh (the
   clamp; under FIFO its own gap) */
static void testCapEqualsRefresh(void)
{
    const uint64_t r6000 = 16666667ull, r5994 = 16683350ull, r120 = 8333333ull, r11988 = 8341675ull,
                   r144 = 6944444ull;

    /* equal: 60 on 60.00 and 59.94, 120 on 120.00 and 119.88, 144 on 144 */
    CHECK(pace_cap_equals_refresh(60, r6000));
    CHECK(pace_cap_equals_refresh(60, r5994));
    CHECK(pace_cap_equals_refresh(120, r120));
    CHECK(pace_cap_equals_refresh(120, r11988));
    CHECK(pace_cap_equals_refresh(144, r144));
    /* the window's rule: equal -> gap 0 (FIFO); otherwise the cap's gap */
    CHECK(pace_present_gap(-1, true, true, r6000) != 0); /* uncapped: unchanged */
    /* above the refresh: not equal; in mailbox mode one present a refresh
       (the clamp), under FIFO the cap's own gap */
    CHECK(!pace_cap_equals_refresh(144, r6000));
    CHECK(!pace_cap_equals_refresh(144, r120));
    CHECK(!pace_cap_equals_refresh(240, r144));
    CHECK(pace_cap_above_refresh(144, true, r6000) && !pace_cap_above_refresh(144, false, r6000));
    CHECK(pace_present_gap(144, false, true, r6000) == r6000 - r6000 / 16);
    CHECK(pace_present_gap(240, false, true, r144) == r144 - r144 / 16);
    CHECK(pace_present_gap(144, false, false, r6000) == 1000000000ull / 144);
    /* equal (60 on 59.94 and 60.00, 144 on a reported 143) or below: no clamp */
    CHECK(!pace_cap_above_refresh(60, true, r6000) && !pace_cap_above_refresh(60, true, r5994));
    CHECK(!pace_cap_above_refresh(144, true, (uint64_t)(1e9 / 143.0)));
    CHECK(!pace_cap_above_refresh(60, true, r120) && !pace_cap_above_refresh(0, true, r6000));
    CHECK(!pace_cap_above_refresh(-1, true, r6000) && !pace_cap_above_refresh(120, true, 0));
    /* below the refresh: 60 on 120, 59 on 60, 30 on 60 */
    CHECK(!pace_cap_equals_refresh(60, r120));
    CHECK(!pace_cap_equals_refresh(59, r6000));
    CHECK(!pace_cap_equals_refresh(30, r6000));
    CHECK(pace_present_gap(60, false, true, r120) == 16666666ull);
    /* the edges: 0.5 Hz either side of 60 still equal, a whole Hz above not */
    CHECK(pace_cap_equals_refresh(60, 1000000000ull * 2 / 119));
    CHECK(pace_cap_equals_refresh(60, 1000000000ull * 2 / 121 + 1));
    CHECK(!pace_cap_equals_refresh(60, 1000000000ull / 61));
    /* What SDL 3.4 reports on Windows (src/video/windows/SDL_windowsmodes.c
       WIN_GetRefreshRate): the mode's whole dmDisplayFrequency, except 29,
       59 and 119, which become (x + 1) * 1000 / 1001 (unless DXGI's closest
       mode gives the exact rate).  A whole report of 100 Hz or more may be
       a rate cut down (143.9 reads 143): up to 1 Hz above it equals.  Below
       100 Hz a whole report is the true rate (59 is a 59.00 Hz mode, as on
       Linux), and a cap of 60 locked to it would run the game 1.7 % slow.
         Windows mode   SDL reports        cap  locks
         59             60000/1001 59.94   60   yes
         60             60                 60   yes
         59 (true)      59                 60   no
         119            120000/1001        120  yes
         143            143                144  yes
         239            239                240  yes
         99             99                 100  no
         74             74                 75   no */
    {
        static const struct {
            uint64_t num, den;
            int cap;
            bool locks;
        } k_table[] = {
            {60000, 1001, 60, true},   {60, 1, 60, true},       {59, 1, 60, false},
            {120000, 1001, 120, true}, {143, 1, 144, true},     {239, 1, 240, true},
            {99, 1, 100, false},       {74, 1, 75, false},      {58, 1, 60, false},
            {142, 1, 144, false},      {30000, 1001, 30, true}, {59951, 1000, 60, true},
        };

        for (unsigned i = 0; i < sizeof k_table / sizeof k_table[0]; i++) {
            const uint64_t ns = 1000000000ull * k_table[i].den / k_table[i].num;

            if (pace_cap_equals_refresh(k_table[i].cap, ns) != k_table[i].locks) {
                printf("FAIL cap %d on %llu/%llu Hz: want %s\n", k_table[i].cap,
                       (unsigned long long)k_table[i].num, (unsigned long long)k_table[i].den,
                       k_table[i].locks ? "locked" : "not locked");
                fails++;
            }
        }
    }
    /* Windows reports 143.9 Hz as 143.0 and 239.8 as 239.0;
       within 1.0 Hz above a whole number, 0.5 Hz otherwise */
    CHECK(pace_cap_equals_refresh(144, (uint64_t)(1e9 / 143.0)));
    CHECK(!pace_cap_equals_refresh(144, (uint64_t)(1e9 / 142.0)));
    CHECK(pace_cap_equals_refresh(240, (uint64_t)(1e9 / 239.0)));
    CHECK(pace_cap_equals_refresh(144, (uint64_t)(1e9 / 143.9)));
    CHECK(!pace_cap_equals_refresh(144, (uint64_t)(1e9 / 143.4)));
    CHECK(pace_cap_equals_refresh(60, (uint64_t)(1e9 / 59.94)));
    CHECK(pace_cap_equals_refresh(60, (uint64_t)(1e9 / 60.0)));
    CHECK(pace_cap_equals_refresh(120, (uint64_t)(1e9 / 119.88)));
    CHECK(!pace_cap_equals_refresh(143, (uint64_t)(1e9 / 144.0)));
    /* no cap */
    CHECK(!pace_cap_equals_refresh(0, r6000));
    CHECK(!pace_cap_equals_refresh(-1, r6000));
    CHECK(!pace_cap_equals_refresh(60, 0));
}

/* The limiter's grid under a fake clock.  pace() in window_host.c loops per
   simulated vsync: a game step (stepNs, no presents), then presents due on
   the grid until the vsync's deadline; a present of a frame already shown
   that would end past the deadline is skipped.  newGrid 0 is the old
   scheduling (due at the last present's start + gap), 1 the grid. */
static double simulate(int cap, int newGrid, uint64_t stepNs, uint64_t costNs, double seconds)
{
    const uint64_t period = 16683333ull, gap = 1000000000ull / (uint64_t)cap;
    const uint64_t end = (uint64_t)(seconds * 1e9);
    uint64_t t = 1000000000ull, deadline = t, last = 0, next = 0;
    unsigned presents = 0;

    while (t < 1000000000ull + end) {
        int shown = 0;

        deadline += period;
        t += stepNs;
        for (;;) {
            const uint64_t due = newGrid ? next : (last ? last + gap : 0);

            if (shown && t + costNs > deadline) {
                break;
            }
            if (due && t < due) {
                if (due >= deadline) {
                    break;
                }
                t = due;
                continue;
            }
            last = t;
            next = pace_next_present(next, t, gap);
            t += costNs;
            presents++;
            shown = 1;
        }
        if (t < deadline) {
            t = deadline;
        }
    }
    return presents / seconds;
}

static void testLimiterGrid(void)
{
    uint64_t n = 0;

    CHECK(pace_next_present(0, 5000, 100) == 5100);
    CHECK(pace_next_present(1000, 1050, 100) == 1100); /* on the grid */
    CHECK(pace_next_present(1000, 1100, 100) == 1100); /* a gap late: still the grid */
    CHECK(pace_next_present(1000, 1101, 100) == 1201); /* more than a gap: restart */
    for (int i = 0; i < 1000; i++) {
        n = pace_next_present(n, n ? n + 30 : 1, 100); /* late by 30 every time: no drift */
    }
    CHECK(n == 1 + 100 * 1000);

    /* 4K-like costs: a 3 ms step and a 2.5 ms present up to 165 presents a
       second, a 1 ms present at 240 */
    static const int caps[] = {60, 120, 144, 165, 240};

    for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
        const double g =
            simulate(caps[i], 1, 3000000ull, caps[i] > 165 ? 1000000ull : 2500000ull, 10.0);

        if (g < caps[i] - 0.5 || g > caps[i] + 0.5) {
            printf("FAIL limiter cap %d: %.2f presents/s\n", caps[i], g);
            fails++;
        }
    }
    {
        const double g = simulate(144, 1, 3000000ull, 2500000ull, 10.0);
        const double o = simulate(144, 0, 3000000ull, 2500000ull, 10.0);

        CHECK(g >= 143.5 && g <= 144.5);
        CHECK(o < 143.0); /* the old scheduling fell short */
    }
}

/* The vsync lock's n (presents a simulated vsync) for the caps a display
   runs at, under NTSC (59.94) and PAL (50); 0: no whole multiple, the
   window steps on the game's timer.  The k-th vblank's alpha after a frame
   closed (frames every two vsyncs) is k / (n * 2). */
static void testLockTable(void)
{
    static const struct {
        int cap;
        unsigned ntsc, pal;
    } t[] = {{60, 1, 0}, {120, 2, 0}, {144, 0, 0}, {240, 4, 0}, {50, 0, 1}, {100, 0, 2}};

    const uint64_t ntsc = 16683333ull, pal = 20000000ull;

    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        const unsigned a = pace_lock_presents(t[i].cap, ntsc),
                       b = pace_lock_presents(t[i].cap, pal);

        if (a != t[i].ntsc || b != t[i].pal) {
            printf("FAIL lock n cap %d: %u/%u, want %u/%u\n", t[i].cap, a, b, t[i].ntsc, t[i].pal);
            fails++;
        }
        /* the alphas of a two-vsync frame: k / (2n), the last exactly 1 */
        const unsigned n = a ? a : b;

        for (unsigned k = 1; n && k <= 2 * n; k++) {
            CHECK(pace_lock_alpha(k, n, 2) == (float)k / (float)(2 * n));
        }
        CHECK(!n || pace_lock_alpha(2 * n, n, 2) == 1.0f);
        CHECK(!n || pace_lock_alpha(2 * n + 1, n, 2) == 1.0f); /* a late frame holds */
    }
    CHECK(pace_lock_alpha(1, 1, 2) == 0.5f);
    CHECK(pace_lock_alpha(3, 2, 2) == 0.75f);
    CHECK(pace_lock_alpha(1, 4, 2) == 0.125f);
    CHECK(pace_lock_alpha(1, 1, 3) == 1.0f / 3.0f); /* a three-vsync frame */
    CHECK(pace_lock_alpha(1, 0, 2) == 0.0f);
    CHECK(pace_lock_presents(0, ntsc) == 0);
    CHECK(pace_lock_presents(-1, ntsc) == 0);
}

/* The grid's sanity (pace_grid_period_ok): n refreshes within 1 % of the
   simulation's period, else no lock.  The display's own report decides it;
   a display 1 % or more off n simulated rates is refused. */
static void testGridPeriod(void)
{
    const uint64_t ntsc = 16683333ull, pal = 20000000ull;

    CHECK(pace_grid_period_ok(1, 16666667ull, ntsc));         /* 60.00 Hz: 0.1 % */
    CHECK(pace_grid_period_ok(1, 16683350ull, ntsc));         /* 59.94 */
    CHECK(pace_grid_period_ok(2, 8333333ull, ntsc));          /* 120 */
    CHECK(pace_grid_period_ok(2, 8341675ull, ntsc));          /* 119.88 */
    CHECK(pace_grid_period_ok(4, 1000000000ull / 239, ntsc)); /* 240 on a reported 239: 0.3 % */
    CHECK(pace_grid_period_ok(1, pal, pal));
    CHECK(pace_grid_period_ok(2, pal / 2, pal));
    /* 1 % off (rounded away): refused, either way; 0.99 % kept */
    CHECK(!pace_grid_period_ok(1, (ntsc * 101 + 99) / 100, ntsc));
    CHECK(!pace_grid_period_ok(1, ntsc * 99 / 100, ntsc));
    CHECK(pace_grid_period_ok(1, ntsc * 10099 / 10000, ntsc));
    CHECK(pace_grid_period_ok(1, ntsc * 9901 / 10000, ntsc));
    CHECK(!pace_grid_period_ok(2, ntsc * 1011 / 2000, ntsc)); /* 1.1 % */
    /* nothing to lock */
    CHECK(!pace_grid_period_ok(0, 16666667ull, ntsc));
    CHECK(!pace_grid_period_ok(1, 0, ntsc));
    CHECK(!pace_grid_period_ok(1, 16666667ull, 0));
    /* 60 on PAL's 50: no n (pace_lock_presents), and one refresh is 17 %
       off the period anyway */
    CHECK(pace_lock_presents(60, pal) == 0);
    CHECK(!pace_grid_period_ok(1, 16666667ull, pal));
}

/* A host pause (pace_lock_pause): the grid moves by it, so the next vsync
   is on time, not a resync nor a burst of steps */
static void testLockPause(void)
{
    PaceLock L;
    const uint64_t r60 = 16666667ull, ntsc = 16683333ull;

    pace_lock_reset(&L, 1, r60, ntsc, 1000000000ull, false, PACE_CLOCK_KIND_NONE);
    L.lastReturn = 1000000000ull + 100000ull;
    L.rateFirst = 1000000000ull;
    L.rateLast = 1000000000ull;
    L.pendCount = 1;
    L.pendSlot[0] = 1000000000.0;
    pace_lock_pause(&L, 140000000ull);
    CHECK(L.due == 1140000000ull);
    CHECK(L.lastReturn == 1140100000ull);
    CHECK(L.rateFirst == 1140000000ull && L.rateLast == 1140000000ull);
    CHECK(L.pendSlot[0] == 1140000000.0);
    CHECK(L.resyncs == 0 && L.droppedNs == 0);
}

/* A synthetic display for the vblank clock: vblank k at phase + k *
   period, noted exactly; presents return at once */
typedef struct Synth {
    uint64_t now;
    double phase, period;
    uint64_t k; /* the next vblank to note */
} Synth;

static uint64_t s_now(void *u)
{
    return ((Synth *)u)->now;
}

static bool s_present(void *u, float a)
{
    (void)u;
    (void)a;
    return true;
}

static void s_sleep(void *u, uint64_t ns)
{
    Synth *d = u;

    if (ns > d->now) {
        d->now = ns;
    }
}

static int s_vblank(void *u, uint64_t *ns)
{
    Synth *d = u;
    const double v = d->phase + (double)d->k * d->period;

    if (v > (double)d->now) {
        return 0;
    }
    *ns = (uint64_t)v;
    d->k++;
    return 1;
}

/* steps vsyncs, each 3 ms of work; returns the last due time */
static uint64_t synth_run(PaceLock *L, Synth *d, unsigned vsyncs)
{
    const PaceLockIo io = {d, s_now, s_present, s_sleep, NULL, NULL, NULL, s_vblank, NULL, NULL};
    uint64_t due = 0;

    for (unsigned v = 0; v < vsyncs; v++) {
        due = pace_lock_vsync(L, &io, v % 2 == 0);
        d->now += 3 * MS;
    }
    return due;
}

/* the due time's offset after the vblank before it (ns) */
static double synth_offset(const Synth *d, uint64_t due)
{
    double x = (double)due - d->phase;

    return x - d->period * (double)(int64_t)(x / d->period);
}

static void testVblankLaws(void)
{
    const uint64_t rep = 16680600ull, ntsc = 16683333ull, t0 = 5000000000ull;
    PaceLock L;

    /* the measured refresh 1.5 % off the report: another output, left at
       its first use, the slot back on the report; 0.9 % off: believed,
       the slot on it */
    for (int i = 0; i < 2; i++) {
        Synth d = {t0, (double)t0 - 3e6, (double)rep * (i ? 1.009 : 1.015), 0};

        while (d.phase + (double)d.k * d.period <= (double)t0) {
            d.k++;
        }
        pace_lock_reset(&L, 1, rep, ntsc, t0, false, PACE_CLOCK_KIND_VBLANK);
        (void)synth_run(&L, &d, 3000);
        if (i == 0) {
            CHECK(L.clock == PACE_CLOCK_LEFT_PERIOD);
            CHECK(L.slotNs == (double)rep);
        } else {
            CHECK(L.clock == PACE_CLOCK_ON);
            CHECK(L.slotNs > d.period - 1.0 && L.slotNs < d.period + 1.0);
            CHECK(L.measuredNs > d.period - 1.0 && L.measuredNs < d.period + 1.0);
        }
    }
    /* the phase: vblanks 5 ms after the steps; once PACE_VB_MIN are in the
       grid moves at once to a sixteenth of a refresh after them.  Then the
       display a fifth of a ms later, then earlier: the steps follow it
       (moved later, then earlier) and sit a sixteenth after it again */
    {
        Synth d = {t0, (double)t0 + 5e6, (double)rep, 1};
        const double want = (double)rep / PACE_VB_OFFSET_DIV;
        uint64_t due;
        double moved;

        pace_lock_reset(&L, 1, rep, ntsc, t0, false, PACE_CLOCK_KIND_VBLANK);
        due = synth_run(&L, &d, PACE_VB_MIN + 4);
        CHECK(L.phaseSet && L.phaseSnaps == 1);
        CHECK(synth_offset(&d, due) > want - 1000.0 && synth_offset(&d, due) < want + 1000.0);
        due = synth_run(&L, &d, 600);
        CHECK(synth_offset(&d, due) > want - 100.0 && synth_offset(&d, due) < want + 100.0);
        /* the steps move later (nudges above 0) within 40 steps; the
           period law takes up a part of the move later (one interval 200 us
           long is in the mean) and the nudges give it back, the steps a
           sixteenth after the vblanks throughout */
        moved = L.nudgeSum;
        d.phase += 200000.0;
        (void)synth_run(&L, &d, 40);
        printf("vblank phase: a sixteenth %.1f us; the display 200 us later: the steps moved "
               "%+.1f us in 40 steps\n",
               want / 1e3, (L.nudgeSum - moved) / 1e3);
        CHECK(L.nudgeSum - moved > 150000.0 && L.nudgeSum - moved < 210000.0);
        due = synth_run(&L, &d, 600);
        CHECK(synth_offset(&d, due) > want - 10000.0 && synth_offset(&d, due) < want + 10000.0);
        /* earlier: the steps move earlier */
        moved = L.nudgeSum;
        d.phase -= 400000.0;
        (void)synth_run(&L, &d, 40);
        CHECK(L.nudgeSum - moved < -300000.0);
        due = synth_run(&L, &d, 600);
        CHECK(synth_offset(&d, due) > want - 10000.0 && synth_offset(&d, due) < want + 10000.0);
        CHECK(L.clock == PACE_CLOCK_ON && L.phaseSnaps == 1 && L.skipped == 0);
    }
}

/* The first present on a swapchain made since the last one
   (pace_first_present_pause): its cost over a refresh, once; any other
   present, or a first one within a refresh, nothing */
static void testFirstPresentPause(void)
{
    uint64_t seen = 0;
    const uint64_t r60 = 16666667ull;

    /* the first swapchain's first present (the open): within a refresh */
    CHECK(pace_first_present_pause(1, &seen, 5000000ull, r60) == 0 && seen == 1);
    /* the same swapchain: a slow present is the game's, not a pause */
    CHECK(pace_first_present_pause(1, &seen, 153400000ull, r60) == 0);
    /* a rebuild (a logged 153.4 ms first present): the excess over a refresh,
       once */
    CHECK(pace_first_present_pause(2, &seen, 153400000ull, r60) == 153400000ull - r60);
    CHECK(seen == 2);
    CHECK(pace_first_present_pause(2, &seen, 153400000ull, r60) == 0);
    /* two rebuilds before a present: one first present */
    CHECK(pace_first_present_pause(4, &seen, 20000000ull, r60) == 20000000ull - r60);
    CHECK(pace_first_present_pause(4, &seen, 20000000ull, r60) == 0 && seen == 4);
}

/* The display's intervals for the log (pace_intervals_*): one-refresh
   intervals give the mean, min and max; an early wake-up (under half a
   slot) is dropped and the next interval counts from the time before it;
   2 and 3 slots count as that many refreshes and late; 4 slots is missed
   and out of the mean; the reset keeps last.  pace_ppm's sign: a longer
   measured refresh than the report is positive.  pace_shown_add: repeats
   from refreshes past pictures, a count that goes back and a long gap
   start again. */
static void testIntervals(void)
{
    const uint64_t slot = 16680600ull;
    PaceIntervals s = {0};
    uint64_t t = 1000 * MS;

    CHECK(pace_intervals_mean(&s) == 0.0);
    pace_intervals_add(&s, t, slot); /* the first time: no interval */
    CHECK(s.intervals == 0 && s.n == 0 && s.last == t);
    for (int i = 0; i < 10; i++) {
        t += slot + (i % 2 ? 100000ull : 0ull) - 50000ull; /* 16.6306 and 16.7306 */
        pace_intervals_add(&s, t, slot);
    }
    CHECK(s.intervals == 10 && s.n == 10 && s.late == 0 && s.missed == 0);
    CHECK(s.min == slot - 50000ull && s.max == slot + 50000ull);
    CHECK(pace_intervals_mean(&s) > (double)slot - 1.0 && pace_intervals_mean(&s) < slot + 1.0);
    /* an early wake-up 3 ms after the last: dropped, last stays */
    pace_intervals_add(&s, t + 3 * MS, slot);
    CHECK(s.intervals == 10 && s.last == t);
    t += slot;
    pace_intervals_add(&s, t, slot);
    CHECK(s.intervals == 11 && s.n == 11 && s.late == 0);
    /* two slots: late, two refreshes; three: late, three */
    t += 2 * slot;
    pace_intervals_add(&s, t, slot);
    CHECK(s.intervals == 12 && s.n == 13 && s.late == 1 && s.missed == 0);
    t += 3 * slot + 2 * MS;
    pace_intervals_add(&s, t, slot);
    CHECK(s.intervals == 13 && s.n == 16 && s.late == 2 && s.missed == 0);
    CHECK(s.max == 3 * slot + 2 * MS);
    /* four slots (a pause): late and missed, not in the mean */
    const double sum = s.sum;

    t += 4 * slot;
    pace_intervals_add(&s, t, slot);
    CHECK(s.intervals == 14 && s.n == 16 && s.late == 3 && s.missed == 1 && s.sum == sum);
    /* 1.4 slots is one refresh, 1.5 is late */
    t += slot * 14 / 10;
    pace_intervals_add(&s, t, slot);
    CHECK(s.n == 17 && s.late == 3);
    t += slot * 3 / 2;
    pace_intervals_add(&s, t, slot);
    CHECK(s.n == 19 && s.late == 4);
    /* a time that went back: a new start, no interval */
    pace_intervals_add(&s, t - MS, slot);
    CHECK(s.intervals == 16 && s.last == t - MS);
    /* the reset keeps last: the next interval counts */
    t -= MS;
    pace_intervals_reset(&s);
    CHECK(s.n == 0 && s.intervals == 0 && s.late == 0 && s.min == 0 && s.max == 0);
    CHECK(s.last == t);
    pace_intervals_add(&s, t + slot, slot);
    CHECK(s.n == 1 && s.min == slot && pace_intervals_mean(&s) == (double)slot);

    /* ppm: 59.940 Hz measured against 59.951 reported: about +183 ppm (the
       grid on the report runs faster); 60.000 against it: negative */
    {
        const double rep = 1e9 / 59.951;
        const double p = pace_ppm(1e9 / 59.940, rep);
        const double q = pace_ppm(1e9 / 60.0, rep);

        CHECK(p > 180.0 && p < 186.0);
        CHECK(q < -810.0 && q > -820.0);
        CHECK(pace_ppm(0.0, rep) == 0.0 && pace_ppm(rep, 0.0) == 0.0);
        CHECK(pace_ppm(rep, rep) == 0.0);
    }

    /* the pictures shown */
    {
        PaceShown d = {0};

        pace_shown_add(&d, 100, 1000); /* the first sample */
        CHECK(d.have && d.shown == 0 && d.refreshes == 0 && d.repeats == 0);
        pace_shown_add(&d, 101, 1001);
        pace_shown_add(&d, 101, 1001); /* nothing new: nothing */
        pace_shown_add(&d, 102, 1002);
        CHECK(d.shown == 2 && d.refreshes == 2 && d.repeats == 0);
        pace_shown_add(&d, 103, 1004); /* a picture shown twice */
        CHECK(d.shown == 3 && d.refreshes == 4 && d.repeats == 1);
        pace_shown_add(&d, 106, 1007); /* three a refresh apart, sampled late */
        CHECK(d.shown == 6 && d.refreshes == 7 && d.repeats == 1);
        pace_shown_add(&d, 108, 1008); /* more pictures than refreshes: no repeat */
        CHECK(d.shown == 8 && d.refreshes == 8 && d.repeats == 1);
        /* a new swapchain: the counts go back, a new start */
        pace_shown_add(&d, 2, 1010);
        CHECK(d.resets == 1 && d.shown == 8 && d.refreshes == 8);
        pace_shown_add(&d, 3, 1012);
        CHECK(d.shown == 9 && d.refreshes == 10 && d.repeats == 2);
        /* a gap past PACE_SHOWN_GAP refreshes: a new start */
        pace_shown_add(&d, 4, 1012 + PACE_SHOWN_GAP + 1);
        CHECK(d.resets == 2 && d.shown == 9 && d.repeats == 2);
        pace_shown_add(&d, 5, 1012 + PACE_SHOWN_GAP + 2);
        CHECK(d.shown == 10 && d.repeats == 2 && d.resets == 2);
    }
}

/* the late-present skip: pace_late_skip's arguments (newFrame,
   skippedLast, paceSlow, now, deadline, period); times in ms past a
   deadline at 100 ms */
static PaceSkip skipAt(bool newFrame, bool skippedLast, bool slow, uint64_t pastMs10)
{
    const uint64_t deadline = 100 * MS;

    return pace_late_skip(newFrame, skippedLast, slow, deadline + pastMs10 * MS / 10, deadline,
                          PERIOD);
}

/* late: the next step a whole period (16.683 ms) or more behind its due
   time skips without slow presents and whatever the audio queue holds (the
   device's clock keeps none); less than a period behind does not */
static void testLateSkipLate(void)
{
    CHECK(skipAt(true, false, false, 167) == PACE_SKIP_LATE);
    CHECK(skipAt(true, false, false, 500) == PACE_SKIP_LATE);
    CHECK(skipAt(true, false, false, 166) == PACE_SKIP_NONE);
    CHECK(skipAt(true, false, false, 0) == PACE_SKIP_NONE);
    /* exactly a period behind is late */
    CHECK(pace_late_skip(true, false, false, 100 * MS + PERIOD, 100 * MS, PERIOD) ==
          PACE_SKIP_LATE);
    CHECK(pace_late_skip(true, false, false, 100 * MS + PERIOD - 1, 100 * MS, PERIOD) ==
          PACE_SKIP_NONE);
    /* late and slow: named late */
    CHECK(skipAt(true, false, true, 200) == PACE_SKIP_LATE);
    /* no period: never late */
    CHECK(pace_late_skip(true, false, false, 500 * MS, 100 * MS, 0) == PACE_SKIP_NONE);
}

/* slow: past the deadline by less than a period, presents slow */
static void testLateSkipSlow(void)
{
    CHECK(skipAt(true, false, true, 0) == PACE_SKIP_SLOW);
    CHECK(skipAt(true, false, true, 100) == PACE_SKIP_SLOW);
    CHECK(skipAt(true, false, false, 100) == PACE_SKIP_NONE);
}

/* never two frames in a row; the same frame again is not two */
static void testLateSkipNeverTwice(void)
{
    /* the frame after a skipped one is presented, late or slow */
    CHECK(skipAt(true, true, true, 500) == PACE_SKIP_NONE);
    CHECK(skipAt(true, true, false, 500) == PACE_SKIP_NONE);
    CHECK(skipAt(true, true, true, 0) == PACE_SKIP_NONE);
    /* pace_skipped_last: frame 7 skipped, no present since */
    CHECK(!pace_skipped_last(false, 7, 7));
    CHECK(!pace_skipped_last(false, 7, 8));
    CHECK(!pace_skipped_last(true, 7, 7)); /* frame 7 still the newest: it may stay skipped */
    CHECK(pace_skipped_last(true, 7, 8));  /* frame 8 closed: it is presented */
    /* a new frame every step, always late: every other one presented */
    {
        bool open = false;
        uint32_t skipped = 0;
        int skips = 0;

        for (uint32_t f = 1; f <= 10; f++) {
            if (skipAt(true, pace_skipped_last(open, skipped, f), false, 400) != PACE_SKIP_NONE) {
                open = true;
                skipped = f;
                skips++;
            } else {
                open = false; /* presented */
            }
        }
        CHECK(skips == 5);
    }
}

static void testLateSkipPresentedFrame(void)
{
    /* a frame already presented is never skipped here (the paths below
       leave it) */
    CHECK(skipAt(false, false, true, 500) == PACE_SKIP_NONE);
    CHECK(skipAt(false, false, false, 500) == PACE_SKIP_NONE);
    CHECK(skipAt(false, false, true, 0) == PACE_SKIP_NONE);
}

static void testLateSkipBeforeDeadline(void)
{
    /* before the deadline a new frame is presented, slow or not */
    CHECK(pace_late_skip(true, false, true, 99 * MS, 100 * MS, PERIOD) == PACE_SKIP_NONE);
    CHECK(pace_late_skip(true, false, false, 99 * MS, 100 * MS, PERIOD) == PACE_SKIP_NONE);
    CHECK(pace_late_skip(true, false, true, 0, 100 * MS, PERIOD) == PACE_SKIP_NONE);
}

/* window_host.c pace's timer path under a fake clock (a cap of 60 in
   mailbox mode on a 120 Hz display, no lock): `steps` simulation steps of
   stepNs each, a game frame closing every `every` steps, each present
   costing presentNs; the lag dropped past 100 ms (RESYNC_NS).  The rule:
   pace_late_skip with pace_skipped_last (SKIP_RULE_NOW), with a skip flag
   only a present clears, so a frame stays skipped for one step at most
   (SKIP_RULE_ONE_STEP, the rule before), or without the late arm (period
   0: SKIP_RULE_NO_LATE, the slow arm alone, as with the audio's arm inert
   on the device's clock) */
enum { SKIP_RULE_NOW, SKIP_RULE_ONE_STEP, SKIP_RULE_NO_LATE };

typedef struct SkipRun {
    unsigned frames, presents, late, slow, resyncs;
    unsigned unshownRun; /* the most frames in a row never presented */
} SkipRun;

static SkipRun skipLoop(unsigned steps, unsigned every, uint64_t stepNs, uint64_t presentNs,
                        int rule)
{
    const uint64_t refresh = 8333333ull, gap = 16666667ull, resyncNs = 100 * MS;
    PaceHist h = {0};
    SkipRun r = {0};
    uint64_t t = 0, deadline = 0, next = 0;
    uint32_t frame = 0, presented = 0, skipFrame = 0;
    bool skipOpen = false, slow = false;

    for (unsigned k = 0; k < steps; k++) {
        t += stepNs;
        if (k % every == 0) {
            frame++;
            r.frames++;
        }
        deadline += PERIOD;
        for (;;) {
            const bool newFrame = presented != frame;
            const bool last = rule == SKIP_RULE_ONE_STEP
                                  ? skipOpen
                                  : pace_skipped_last(skipOpen, skipFrame, frame);
            const PaceSkip why = pace_late_skip(newFrame, last, slow, t, deadline,
                                                rule == SKIP_RULE_NO_LATE ? 0 : PERIOD);

            if (why != PACE_SKIP_NONE) {
                skipOpen = true;
                skipFrame = frame;
                r.late += why == PACE_SKIP_LATE;
                r.slow += why == PACE_SKIP_SLOW;
                break;
            }
            if (!newFrame && (t >= deadline || slow || t + presentNs > deadline)) {
                t = t < deadline ? deadline : t;
                break;
            }
            if (next != 0 && t < next) {
                if (next >= deadline) {
                    t = t < deadline ? deadline : t;
                    break;
                }
                t = next;
                continue;
            }
            {
                const uint64_t start = t;

                t += presentNs;
                slow = pace_slow_present(&h, presentNs, refresh, PERIOD, false);
                if (newFrame && frame - presented - 1 > r.unshownRun) {
                    r.unshownRun = frame - presented - 1;
                }
                presented = frame;
                skipOpen = false;
                r.presents++;
                next = pace_next_present(next, start, gap);
            }
        }
        if (t > deadline && t - deadline > resyncNs) {
            r.resyncs++;
            deadline = t;
        }
    }
    return r;
}

/* the loop: a 30 frames a second game on a renderer whose presents take
   39 ms (a GPU-bound phone at 2x: two steps and a present are 44 ms against
   33.4) keeps real time by leaving frames unpresented, never two in a row;
   the rule that skipped a frame for one step only presented every frame
   and fell behind until the pacer dropped the lag.  At 60 frames a second
   with 20 ms presents (not slow) the late arm alone keeps real time, and
   without it the game falls behind.  A fast renderer never skips. */
static void testLateSkipLoop(void)
{
    const uint64_t step = 2400000ull;
    SkipRun r = skipLoop(600, 2, step, 39 * MS, SKIP_RULE_NOW);
    SkipRun old = skipLoop(600, 2, step, 39 * MS, SKIP_RULE_ONE_STEP);

    CHECK(r.resyncs == 0);
    CHECK(r.unshownRun == 1);
    CHECK(r.presents < r.frames && r.presents >= r.frames / 2);
    CHECK(r.late > 0 && r.slow > 0);
    CHECK(old.presents == old.frames && old.resyncs > 0);
    printf("pace_policy_test: 30 fps, 39 ms presents: %u of %u frames presented, %u late and %u "
           "slow skips, %u resyncs (skipped for one step only: %u presented, %u resyncs)\n",
           r.presents, r.frames, r.late, r.slow, r.resyncs, old.presents, old.resyncs);

    r = skipLoop(600, 1, step, 20 * MS, SKIP_RULE_NOW);
    old = skipLoop(600, 1, step, 20 * MS, SKIP_RULE_NO_LATE);
    CHECK(r.resyncs == 0 && r.unshownRun == 1 && r.slow == 0 && r.late > 0);
    CHECK(old.resyncs > 0 && old.presents == old.frames);
    printf("pace_policy_test: 60 fps, 20 ms presents: %u of %u frames presented, %u late skips, "
           "%u resyncs (without the late arm: %u presented, %u resyncs)\n",
           r.presents, r.frames, r.late, r.resyncs, old.presents, old.resyncs);

    r = skipLoop(600, 2, step, 5 * MS, SKIP_RULE_NOW);
    CHECK(r.late == 0 && r.slow == 0 && r.resyncs == 0 && r.unshownRun == 0);
}

/* the present allowance's steps: each makes `presents` presents (the first
   of a new frame when newFirst, the rest of a frame already shown) and ends
   lateNs late; the allowance's changes */
static unsigned budgetSteps(PaceBudget *b, unsigned steps, unsigned presents, bool newFirst,
                            uint64_t lateNs, uint64_t waitNs, uint64_t gpuNs)
{
    unsigned changes = 0;

    for (unsigned k = 0; k < steps; k++) {
        for (unsigned i = 0; i < presents; i++) {
            pace_budget_present(b, newFirst && i == 0);
        }
        changes += pace_budget_step(b, lateNs, PERIOD, REFRESH, waitNs, gpuNs, 3 * MS);
    }
    return changes;
}

/* the paced allowance's steps as the window drives them: each step tries
   `want` presents, the first of a new frame every newEvery steps, the
   in-betweens through pace_budget_allows_paced with the GPU's time gpuNs
   and a step of stepNs, stopping at the first refused (counted, as the
   window counts it); each step ends lateNs late.  The presents made, each
   step's in counts[] when given */
static unsigned pacedSteps(PaceBudget *b, unsigned steps, unsigned want, unsigned newEvery,
                           uint64_t gpuNs, uint64_t stepNs, uint64_t lateNs, unsigned *counts)
{
    unsigned total = 0;

    for (unsigned k = 0; k < steps; k++) {
        const bool newFrame = newEvery != 0 && k % newEvery == 0;
        unsigned made = 0;

        for (unsigned i = 0; i < want; i++) {
            const bool fresh = newFrame && i == 0;

            if (!pace_budget_allows_paced(b, fresh, gpuNs, stepNs, PERIOD)) {
                b->refused++;
                break;
            }
            pace_budget_present(b, fresh);
            made++;
        }
        (void)pace_budget_step(b, lateNs, PERIOD, REFRESH, 0, gpuNs, stepNs);
        if (counts != NULL) {
            counts[k] = made;
        }
        total += made;
    }
    return total;
}

/* The present allowance (pace_budget_*): a late step with an optional
   present arms it, a second late step while armed (within PACE_ALLOW_HOLD
   steps) drops it to one below that step's presents and holds it
   PACE_ALLOW_HOLD steps; every PACE_ALLOW_CLIMB_STEP steps on time after
   the hold it goes straight back to max when the GPU has the time for max
   presents, else up by one when it has the time for one more, else the
   climb is refused; half a period is late; the display's own wait less
   than a refresh late is excused; late steps of new frames only arm
   nothing, but one while armed confirms the drop (one below the arming
   step's presents); a new frame is always allowed; never above max.
   pace_display_wait's choice; pace_hist_add's median. */
static void testBudgetRule(void)
{
    PaceBudget b;

    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(b.allow == PACE_ALLOW_MAX && b.lowest == PACE_ALLOW_MAX && b.drops == 0 && b.arm == 0);
    /* one late step (a new frame and an in-between, 10 ms late): armed only */
    pace_budget_present(&b, true);
    pace_budget_present(&b, false);
    CHECK(!pace_budget_step(&b, 10 * MS, PERIOD, REFRESH, 0, 0, 3 * MS));
    CHECK(b.allow == PACE_ALLOW_MAX && b.arm == PACE_ALLOW_HOLD && b.arms == 1 && b.drops == 0);
    CHECK(b.lateMaxNs == 10 * MS && b.presents == 0 && b.optional == 0);
    /* the second while armed (the next step, 11 ms late): 1, held */
    pace_budget_present(&b, true);
    pace_budget_present(&b, false);
    CHECK(pace_budget_step(&b, 11 * MS, PERIOD, REFRESH, 0, 0, 3 * MS));
    CHECK(b.allow == 1 && b.hold == PACE_ALLOW_HOLD && b.drops == 1 && b.lowest == 1);
    CHECK(b.dropPresents == 2 && b.dropLateNs == 11 * MS && b.lateMaxNs == 11 * MS);
    CHECK(b.dropAgo == 1 && b.dropPrevLateNs == 10 * MS && b.dropPrevPresents == 2);
    CHECK(b.sinceDrop == 0 && b.arm == PACE_ALLOW_HOLD && b.presents == 0);
    /* at 1: an in-between first is allowed, none after a present; a new
       frame always */
    CHECK(pace_budget_allows(&b, false));
    pace_budget_present(&b, true);
    CHECK(!pace_budget_allows(&b, false) && pace_budget_allows(&b, true));
    CHECK(!pace_budget_step(&b, 0, PERIOD, REFRESH, 0, 0, 3 * MS));
    /* held: 59 more on time, then PACE_ALLOW_CLIMB_STEP - 1 on time, still
       1; the next goes straight back to max (no GPU time: the climb probes) */
    CHECK(budgetSteps(&b, 59, 1, true, 0, 0, 0) == 0 && b.allow == 1 && b.hold == 0);
    CHECK(budgetSteps(&b, PACE_ALLOW_CLIMB_STEP - 1, 1, true, 0, 0, 0) == 0 && b.allow == 1);
    CHECK(budgetSteps(&b, 1, 1, true, 0, 0, 0) == 1 && b.allow == PACE_ALLOW_MAX && b.climbs == 1);
    CHECK(b.gateK == PACE_ALLOW_MAX && b.sinceDrop == PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP);
    CHECK(b.arm == 0);
    /* the GPU gate: 12 ms a picture and a 3 ms step leave no room for a
       second present (2 * 12 + 3 > 16.68): two late steps drop to 1 and
       the climb is refused, asking for 2 */
    CHECK(budgetSteps(&b, 2, 2, true, 10 * MS, 0, 12 * MS) == 1 && b.allow == 1 && b.drops == 2);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 12 * MS) == 0);
    CHECK(b.allow == 1 && b.climbs == 1 && b.climbRefusals == 1 && b.gateK == 2);
    /* 6 ms a picture: not max (8 * 6 + 3 > 16.68) but one more (2 * 6 + 3) */
    CHECK(budgetSteps(&b, PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 6 * MS) == 1 && b.allow == 2);
    CHECK(b.gateK == 2 && b.climbs == 2);
    /* 1 ms a picture (8 * 1 + 3 < 16.68): straight back to max */
    CHECK(budgetSteps(&b, PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 1 * MS) == 1);
    CHECK(b.allow == PACE_ALLOW_MAX && b.gateK == PACE_ALLOW_MAX && b.climbs == 3);
    /* under half a period is not late (nothing armed); half a period is */
    CHECK(budgetSteps(&b, 2, 2, true, PERIOD / 2 - 1, 0, 0) == 0 && b.arm == 0 && b.arms == 2);
    CHECK(budgetSteps(&b, 2, 2, true, PERIOD / 2, 0, 0) == 1 && b.allow == 1 && b.arms == 3);
    /* the display's wait (5 ms, a quarter of a refresh or more) with the
       step less than a refresh late: excused, nothing armed; 3 ms is not
       the display's; 20 ms late is a refresh or more, not excused */
    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 2, 2, true, 10 * MS, 5 * MS, 0) == 0 && b.arms == 0 && b.drops == 0);
    CHECK(budgetSteps(&b, 2, 2, true, 7 * MS, 5 * MS, 0) == 0 && b.arms == 0);
    CHECK(budgetSteps(&b, 2, 2, true, 10 * MS, 3 * MS, 0) == 1 && b.allow == 1);
    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 2, 2, true, 20 * MS, 5 * MS, 0) == 1 && b.allow == 1);
    /* late steps of new frames only (a hitch's catch-up): nothing armed */
    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 2, 1, true, 30 * MS, 0, 0) == 0 && b.allow == PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 2, 0, true, 30 * MS, 0, 0) == 0 && b.arms == 0 && b.drops == 0);
    /* a late step of new frames only while armed: the lateness lasts, one
       below the arming step's presents (3 - 1); at 2 another lowers
       nothing and is no drop */
    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 1, 3, true, 10 * MS, 0, 0) == 0 && b.arms == 1);
    CHECK(budgetSteps(&b, 1, 1, true, 12 * MS, 0, 0) == 1 && b.allow == 2);
    CHECK(b.dropPresents == 1 && b.dropAgo == 1 && b.dropPrevPresents == 3);
    CHECK(budgetSteps(&b, 1, 1, true, 12 * MS, 0, 0) == 0 && b.drops == 1 && b.allow == 2);
    /* late steps with one in-between: 0; a new frame still presented */
    pace_budget_reset(&b, PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 2, 1, false, 10 * MS, 0, 0) == 1 && b.allow == 0 && b.lowest == 0);
    CHECK(pace_budget_allows(&b, true) && !pace_budget_allows(&b, false));
    /* the block: lowest from the allowance now, the latest step cleared */
    pace_budget_block(&b);
    CHECK(b.lowest == 0 && b.lateMaxNs == 0);
    /* the arm lasts PACE_ALLOW_HOLD steps: a late step 61 steps after the
       one before arms again, one 60 steps after drops */
    pace_budget_reset(&b, 2);
    CHECK(budgetSteps(&b, 1, 2, true, 10 * MS, 0, 0) == 0 && b.arm == PACE_ALLOW_HOLD);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD, 1, true, 0, 0, 0) == 0 && b.arm == 0);
    CHECK(budgetSteps(&b, 1, 2, true, 10 * MS, 0, 0) == 0 && b.arms == 2 && b.drops == 0);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD - 1, 1, true, 0, 0, 0) == 0 && b.arm == 1);
    CHECK(budgetSteps(&b, 1, 2, true, 10 * MS, 0, 0) == 1 && b.allow == 1);
    CHECK(b.dropAgo == PACE_ALLOW_HOLD && b.drops == 1);
    /* never above max: the lock's n = 2 back to 2 and no further */
    CHECK(budgetSteps(&b, 2000, 1, true, 0, 0, 0) == 1 && b.allow == 2 && b.max == 2);
    CHECK(b.climbs == 1 && b.climbRefusals == 0);
    /* pace_display_wait: the acquire and the present, or with the fence
       waits where they hold the display's wait (OpenGL); never above the
       present's time */
    CHECK(pace_display_wait(1 * MS, 8 * MS, 20 * MS, false) == 1 * MS);
    CHECK(pace_display_wait(1 * MS, 8 * MS, 20 * MS, true) == 9 * MS);
    CHECK(pace_display_wait(1 * MS, 30 * MS, 20 * MS, true) == 20 * MS);
    CHECK(pace_display_wait(25 * MS, 0, 20 * MS, false) == 20 * MS);
    /* pace_hist_add: the median of the last PACE_HIST_LEN */
    {
        PaceHist g = {0};

        CHECK(pace_hist_add(&g, 10 * MS) == 10 * MS);
        CHECK(pace_hist_add(&g, 12 * MS) == 11 * MS);
        for (int i = 0; i < PACE_HIST_LEN; i++) {
            (void)pace_hist_add(&g, i == 0 ? 40 * MS : 12 * MS);
        }
        CHECK(pace_hist_add(&g, 12 * MS) == 12 * MS && g.count == PACE_HIST_LEN && !g.slow);
    }
}

/* The timer path's allowance (pace_budget_reset_unbound): unbound at the
   start, so 15 presents a step (a new frame and 14 in-betweens) with no late
   step are never refused and nothing changes; two late steps of 15 drop it
   to max (8), not 14, and the climb after the hold makes it unbound again
   (no GPU time: the climb probes, max + 1 presents' worth asked); two late
   steps of 2 with 4 ms a picture go straight back to unbound; a drop from a
   late step of 5 to 1 with 3 ms a picture climbs one at a time to 4 (5 * 3
   + 3 is not under 16.68) and is refused there, and unbound once the GPU
   takes 1 ms. */
static void testBudgetUnbound(void)
{
    PaceBudget b;
    unsigned refused = 0, changes = 0;

    pace_budget_reset_unbound(&b, PACE_ALLOW_MAX);
    CHECK(b.allow == PACE_ALLOW_NONE && b.lowest == PACE_ALLOW_NONE && b.max == PACE_ALLOW_MAX);
    for (unsigned k = 0; k < 600; k++) {
        for (unsigned i = 0; i < 15; i++) {
            if (!pace_budget_allows(&b, i == 0)) {
                refused++;
            }
            pace_budget_present(&b, i == 0);
        }
        changes += pace_budget_step(&b, 0, PERIOD, REFRESH, 0, 0, 3 * MS) ? 1u : 0u;
    }
    CHECK(refused == 0 && changes == 0 && b.allow == PACE_ALLOW_NONE && b.drops == 0);
    CHECK(b.climbs == 0 && b.lowest == PACE_ALLOW_NONE);
    /* two late steps of 15: held to max, not 14 */
    CHECK(budgetSteps(&b, 2, 15, true, 10 * MS, 0, 0) == 1 && b.allow == PACE_ALLOW_MAX);
    CHECK(b.drops == 1 && b.lowest == PACE_ALLOW_MAX && b.dropPresents == 15 && b.dropMost == 15);
    /* the hold and PACE_ALLOW_CLIMB_STEP - 1 on time: still max; the next: unbound */
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1, 1, true, 0, 0, 0) == 0);
    CHECK(b.allow == PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 1, 1, true, 0, 0, 0) == 1 && b.allow == PACE_ALLOW_NONE);
    CHECK(b.climbs == 1 && b.gateK == PACE_ALLOW_MAX + 1 && b.dropMost == 0);
    /* unbound: no climb counted on time */
    CHECK(budgetSteps(&b, 1000, 1, true, 0, 0, 0) == 0 && b.climbs == 1);
    /* two late steps of 2 and 4 ms a picture (2 * 4 + 3 < 16.68): 1, then
       straight back to unbound */
    CHECK(budgetSteps(&b, 2, 2, true, 10 * MS, 0, 4 * MS) == 1 && b.allow == 1);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1, 1, true, 0, 0, 4 * MS) == 0);
    CHECK(b.allow == 1);
    CHECK(budgetSteps(&b, 1, 1, true, 0, 0, 4 * MS) == 1 && b.allow == PACE_ALLOW_NONE);
    CHECK(b.gateK == 2 && b.climbs == 2);
    /* a late step of 5, then one of 2: 1, the most presents 5; at 3 ms a
       picture one at a time to 4, then refused (5 * 3 + 3 > 16.68) */
    CHECK(budgetSteps(&b, 1, 5, true, 10 * MS, 0, 3 * MS) == 0 && b.arm == PACE_ALLOW_HOLD);
    CHECK(budgetSteps(&b, 1, 2, true, 10 * MS, 0, 3 * MS) == 1 && b.allow == 1 && b.dropMost == 5);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 3 * MS) == 1);
    CHECK(b.allow == 2 && b.gateK == 2);
    CHECK(budgetSteps(&b, 2 * PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 3 * MS) == 2 && b.allow == 4);
    CHECK(budgetSteps(&b, PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 3 * MS) == 0 && b.allow == 4);
    CHECK(b.climbRefusals == 1 && b.gateK == 5);
    /* the GPU at 1 ms (5 * 1 + 3 < 16.68): unbound */
    CHECK(budgetSteps(&b, PACE_ALLOW_CLIMB_STEP, 1, true, 0, 0, 1 * MS) == 1);
    CHECK(b.allow == PACE_ALLOW_NONE && b.gateK == 5 && b.dropMost == 0);
    /* the block's lowest from an unbound allowance: unbound */
    pace_budget_block(&b);
    CHECK(b.lowest == PACE_ALLOW_NONE);
    /* the lock's allowance (pace_budget_reset) never goes unbound */
    pace_budget_reset(&b, 2);
    CHECK(!b.unbind && budgetSteps(&b, 2000, 1, true, 0, 0, 0) == 0 && b.allow == 2);
}

/* The reporters' sequences (the evidence logs, build-host/tmp/v092/
   evidence/), replayed through the allowance as the window drives it:
   each step wants `want` presents, a new frame's first every `newEvery`
   steps (0: none, every present of a frame already shown), a present past
   the allowance refused (counted); a step that made at least lateFrom
   presents (and one or more) ends lateNs late, its last present having
   waited waitNs for the display.  The period, the refresh, the GPU time a
   presented replay and the simulation step's time are the sequence's. */
typedef struct BudgetSeq {
    PaceBudget b;
    unsigned k, refused, changes;
    uint64_t periodNs, refreshNs, gpuNs, stepNs;
} BudgetSeq;

static void seqSteps(BudgetSeq *q, unsigned steps, unsigned want, unsigned newEvery,
                     uint64_t lateNs, unsigned lateFrom, uint64_t waitNs)
{
    for (unsigned s = 0; s < steps; s++, q->k++) {
        const bool newFrame = newEvery != 0 && q->k % newEvery == 0;
        unsigned made = 0;

        for (unsigned i = 0; i < want; i++) {
            if (!pace_budget_allows(&q->b, newFrame && i == 0)) {
                q->refused++;
                q->b.refused++; /* as the window counts them */
                continue;
            }
            pace_budget_present(&q->b, newFrame && i == 0);
            made++;
        }
        q->changes += pace_budget_step(&q->b, made > 0 && made >= lateFrom ? lateNs : 0,
                                       q->periodNs, q->refreshNs, waitNs, q->gpuNs, q->stepNs)
                          ? 1u
                          : 0u;
    }
}

/* the window's climb line for a sequence (window_host.c budget_lines) */
static void seqClimbLine(const char *who, const BudgetSeq *q)
{
    const PaceBudget *b = &q->b;

    if (b->allow == PACE_ALLOW_NONE) {
        printf("pace_policy_test: %s: presents back to not held after %u steps (GPU %.1f ms x %u + "
               "step %.1f ms under the period %.1f ms)\n",
               who, b->sinceDrop, (double)q->gpuNs / 1e6, b->gateK, (double)q->stepNs / 1e6,
               (double)q->periodNs / 1e6);
    } else {
        printf(
            "pace_policy_test: %s: presents at %u a step, %u steps since the drop (GPU %.1f ms x "
            "%u + step %.1f ms against the period %.1f ms; %u climbs, %u refused)\n",
            who, b->allow, b->sinceDrop, (double)q->gpuNs / 1e6, b->gateK, (double)q->stepNs / 1e6,
            (double)q->periodNs / 1e6, b->climbs, b->climbRefusals);
    }
}

/* issue 130 (evidence/130/ico-pc-previous-v091.log, RTX 4090, uncapped,
   immediate, 360 Hz): block 1 made 6.7 presents a step with the GPU at
   1.2 ms a presented replay in block 2; line 909's step ended 8.7 ms late
   after 2 presents (the display's wait 0.1 ms), line 982's 12.6 ms after
   2.  v0.9.1 held the presents to 1 a step at the first and needed 8
   climbs of 180 steps (28 s on time) to be unbound again.  Now: one such
   step only arms (600 steps of 6 presents either side, nothing refused,
   unbound); two within 60 steps drop to 1, and 120 steps on time later it
   is unbound again (2 * 1.2 + 0.5 < 16.68), nothing refused after; with
   the GPU at 9 ms (2 * 9 + 0.5 > 16.68) it stays at 1, a refused climb
   every 60 steps. */
static void testBudgetHitch130(void)
{
    const uint64_t refresh360 = 2777778ull, wait = 100000ull, gpu = 1200000ull;
    BudgetSeq q = {.periodNs = PERIOD, .refreshNs = refresh360, .gpuNs = gpu, .stepNs = MS / 2};

    pace_budget_reset_unbound(&q.b, PACE_ALLOW_MAX);
    seqSteps(&q, 600, 6, 2, 0, 0, wait);
    seqSteps(&q, 1, 2, 0, 8700000ull, 2, wait);
    seqSteps(&q, 600, 6, 2, 0, 0, wait);
    printf("pace_policy_test: issue 130, one step 8.7 ms late among 1200 of 6 presents: allowance "
           "%s, %u armed, %u drops, %u refused\n",
           q.b.allow == PACE_ALLOW_NONE ? "not held" : "held", q.b.arms, q.b.drops, q.refused);
    CHECK(q.b.allow == PACE_ALLOW_NONE && q.refused == 0 && q.changes == 0);
    CHECK(q.b.arms == 1 && q.b.drops == 0 && q.b.lowest == PACE_ALLOW_NONE);
    for (int g = 0; g < 2; g++) {
        unsigned refused;

        q = (BudgetSeq){.periodNs = PERIOD,
                        .refreshNs = refresh360,
                        .gpuNs = g ? 9 * MS : gpu,
                        .stepNs = MS / 2};
        pace_budget_reset_unbound(&q.b, PACE_ALLOW_MAX);
        seqSteps(&q, 600, 6, 2, 0, 0, wait);
        seqSteps(&q, 1, 2, 0, 8700000ull, 2, wait);
        seqSteps(&q, 30, 6, 2, 0, 0, wait);
        seqSteps(&q, 1, 2, 0, 12600000ull, 2, wait);
        printf("pace_policy_test: issue 130, a second late step %u steps after the first: presents "
               "held to %u a step (the step %.1f ms late after %u presents, the one before %.1f "
               "ms after %u)\n",
               q.b.dropAgo, q.b.allow, (double)q.b.dropLateNs / 1e6, q.b.dropPresents,
               (double)q.b.dropPrevLateNs / 1e6, q.b.dropPrevPresents);
        CHECK(q.b.allow == 1 && q.b.drops == 1 && q.b.dropAgo == 31 && q.refused == 0);
        CHECK(q.b.dropPrevLateNs == 8700000ull && q.b.dropMost == 2);
        seqSteps(&q, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1, 6, 2, 0, 0, wait);
        CHECK(q.b.allow == 1 && q.refused == 5 * (PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1));
        seqSteps(&q, 1, 6, 2, 0, 0, wait);
        seqClimbLine(g ? "issue 130, GPU 9 ms" : "issue 130", &q);
        refused = q.refused;
        seqSteps(&q, 600, 6, 2, 0, 0, wait);
        if (!g) {
            CHECK(q.b.allow == PACE_ALLOW_NONE && q.b.climbs == 1 && q.b.gateK == 2);
            CHECK(q.b.climbRefusals == 0 && q.refused == refused && q.b.drops == 1);
        } else {
            CHECK(q.b.allow == 1 && q.b.climbs == 0 && q.b.gateK == 2);
            CHECK(q.b.climbRefusals == 11 && q.refused == refused + 5 * 600);
        }
    }
}

/* Lateness that lasts is still held.  The issue-130 item-3 case (v0.8.6:
   cap 120, two presents a step empty the audio's lead): every step that
   makes two presents ends 9 ms late, the GPU 9 ms a picture: held to 1 at
   the second step and kept there (2 * 9 + 0.5 > 16.68: a refused climb
   every 60 steps), one drop in all.  Issue 139 (evidence/139: Frame rate
   60, 4x supersampling on an RTX 2000 Ada, the vsync lock n = 1): the GPU
   19 ms a picture and a 2 ms step, every step with a present 10 ms late:
   armed at the first in-between's step, held to 0 at the next late step
   (the new frame's: the lateness lasts) and kept there (1 * 19 + 2 >
   16.67): exactly the game's own 30 frames a second. */
static void testBudgetSustained(void)
{
    const uint64_t lockPeriod = 16667ull * 1000;
    BudgetSeq q = {.periodNs = PERIOD, .refreshNs = 2777778ull, .gpuNs = 9 * MS, .stepNs = MS / 2};

    pace_budget_reset_unbound(&q.b, PACE_ALLOW_MAX);
    seqSteps(&q, 600, 2, 2, 9 * MS, 2, 0);
    seqClimbLine("item 3, cap 120", &q);
    CHECK(q.b.allow == 1 && q.b.drops == 1 && q.b.lowest == 1 && q.b.dropAgo == 1);
    CHECK(q.b.climbs == 0 && q.b.climbRefusals == 8 && q.b.gateK == 2 && q.refused == 598);
    q = (BudgetSeq){
        .periodNs = lockPeriod, .refreshNs = lockPeriod, .gpuNs = 19 * MS, .stepNs = 2 * MS};
    pace_budget_reset(&q.b, 1);
    seqSteps(&q, 600, 1, 2, 10 * MS, 1, 0);
    seqClimbLine("issue 139, the lock at 1", &q);
    CHECK(q.b.allow == 0 && q.b.drops == 1 && q.b.lowest == 0 && q.b.dropAgo == 1);
    CHECK(q.b.climbs == 0 && q.b.climbRefusals == 8 && q.b.gateK == 1 && q.refused == 299);
}

/* Issue 126 (evidence/126/ico-pc-opengl-v091.log, GTX 750 Ti, OpenGL,
   vsync lock n = 1 at 60): line 413's step ended 8.9 ms late after 1
   present whose fences waited 8.3 ms and whose swap 0.1 ms, GPU 3.6 ms a
   replay.  On OpenGL the fence wait is FIFO's queue (pace_display_wait
   with fenceHoldsDisplay: 8.4 ms, a quarter of a refresh or more, and the
   step less than a refresh late): two such steps two apart are excused,
   nothing armed, the lock's 1 kept.  The same numbers from Vulkan, whose
   fence waits are the GPU's (0.1 ms the display's), drop to 0 at the
   second, and 120 steps on time later back to 1 (1 * 3.6 + 2 < 16.67).
   The GPU-bound OpenGL blocks (line 343, GPU 22 ms; line 429's step 22.0
   ms late, the fence 14.6 ms, the swap 1.6 ms) are a refresh or more late,
   not excused: held to 0, the game's own 30, as the card's time says. */
static void testBudgetGlWait126(void)
{
    const uint64_t lockPeriod = 16667ull * 1000;

    for (int gl = 1; gl >= 0; gl--) {
        BudgetSeq q = {
            .periodNs = lockPeriod, .refreshNs = lockPeriod, .gpuNs = 3600000ull, .stepNs = 2 * MS};
        const uint64_t wait = pace_display_wait(100000ull, 8300000ull, 20800000ull, gl != 0);

        pace_budget_reset(&q.b, 1);
        seqSteps(&q, 100, 1, 2, 0, 0, 0);
        seqSteps(&q, 1, 1, 0, 8900000ull, 1, wait);
        seqSteps(&q, 1, 1, 0, 0, 0, 0);
        seqSteps(&q, 1, 1, 0, 8900000ull, 1, wait);
        printf("pace_policy_test: issue 126 line 413 twice on %s: the display's wait %.1f ms, "
               "allowance %u (%u armed, %u drops)\n",
               gl ? "OpenGL" : "Vulkan", (double)wait / 1e6, q.b.allow, q.b.arms, q.b.drops);
        if (gl) {
            CHECK(wait == 8400000ull);
            CHECK(q.b.allow == 1 && q.b.arms == 0 && q.b.drops == 0 && q.changes == 0);
        } else {
            CHECK(wait == 100000ull);
            CHECK(q.b.allow == 0 && q.b.arms == 1 && q.b.drops == 1 && q.b.dropAgo == 2);
            seqSteps(&q, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1, 1, 2, 0, 0, 0);
            CHECK(q.b.allow == 0 && q.refused == 60);
            seqSteps(&q, 1, 1, 2, 0, 0, 0);
            seqClimbLine("issue 126 on Vulkan", &q);
            CHECK(q.b.allow == 1 && q.b.climbs == 1 && q.b.gateK == 1);
        }
    }
    {
        BudgetSeq q = {
            .periodNs = lockPeriod, .refreshNs = lockPeriod, .gpuNs = 22 * MS, .stepNs = 2 * MS};
        const uint64_t wait = pace_display_wait(1600000ull, 14600000ull, 22700000ull, true);

        pace_budget_reset(&q.b, 1);
        seqSteps(&q, 600, 1, 2, 22 * MS, 1, wait);
        seqClimbLine("issue 126, GPU-bound on OpenGL", &q);
        CHECK(wait == 16200000ull && q.b.allow == 0 && q.b.drops == 1);
        CHECK(q.b.climbRefusals == 8 && q.refused == 299);
    }
}

/* The window's lines (pace_budget_event_text, pace_budget_block_text,
   pace_lock_timing_text) word for word on the reporters' sequences: issue
   130's drop at its second late step, its climb back to unbound at 1.2 ms
   of GPU and its refused climb at 9 ms; issue 126's GPU-bound OpenGL drop
   naming the fence waits; issue 139's 10 s line; the lock's clock part. */
static bool textIs(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("pace_policy_test: got  \"%s\"\npace_policy_test: want \"%s\"\n", got, want);
        return false;
    }
    printf("pace_policy_test: window: %s\n", got);
    return true;
}

static void testBudgetLines(void)
{
    const uint64_t lockPeriod = 16667ull * 1000, wait = 100000ull;
    char t[400];

    for (int g = 0; g < 2; g++) {
        BudgetSeq q = {.periodNs = PERIOD,
                       .refreshNs = 2777778ull,
                       .gpuNs = g ? 9 * MS : 1200000ull,
                       .stepNs = MS / 2};
        PaceBudgetWaits w = {22 * MS, 21500000ull, wait, 1 * MS, MS / 2, PERIOD, false};

        pace_budget_reset_unbound(&q.b, PACE_ALLOW_MAX);
        seqSteps(&q, 600, 6, 2, 0, 0, wait);
        seqSteps(&q, 1, 2, 0, 8700000ull, 2, wait);
        seqSteps(&q, 30, 6, 2, 0, 0, wait);
        seqSteps(&q, 1, 2, 0, 12600000ull, 2, wait);
        pace_budget_event_text(&q.b, PACE_BUDGET_TEXT_DROP, &w, t, sizeof(t));
        CHECK(textIs(t, "presents held to 1 a step: the step ended 12.6 ms late after 2 presents, "
                        "31 steps after one 8.7 ms late after 2 (the last present 22.0 ms, waited "
                        "21.5 ms for the GPU and 0.1 ms for the display; GPU 1.0 ms a replay); "
                        "back to not held after 120 steps on time if the GPU allows"));
        seqSteps(&q, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP, 6, 2, 0, 0, wait);
        w.gpuNs = q.gpuNs;
        if (!g) {
            pace_budget_event_text(&q.b, PACE_BUDGET_TEXT_CLIMB, &w, t, sizeof(t));
            CHECK(textIs(t, "presents back to not held (where they started) after 120 steps (GPU "
                            "1.2 ms x 2 + step 0.5 ms under the period 16.7 ms)"));
        } else {
            pace_budget_event_text(&q.b, PACE_BUDGET_TEXT_STAY, &w, t, sizeof(t));
            CHECK(textIs(t, "presents stay at 1 a step: GPU 9.0 ms x 2 + step 0.5 ms is not under "
                            "the period 16.7 ms (120 steps since the drop; back to not held when "
                            "it is)"));
        }
    }
    {
        BudgetSeq q = {
            .periodNs = lockPeriod, .refreshNs = lockPeriod, .gpuNs = 22 * MS, .stepNs = 2 * MS};
        const PaceBudgetWaits w = {22700000ull, 14600000ull, 16200000ull, 22 * MS,
                                   2 * MS,      lockPeriod,  true};

        pace_budget_reset(&q.b, 1);
        seqSteps(&q, 3, 1, 2, 22 * MS, 1, w.displayNs);
        pace_budget_event_text(&q.b, PACE_BUDGET_TEXT_DROP, &w, t, sizeof(t));
        CHECK(textIs(t, "presents held to 0 a step: the step ended 22.0 ms late after 1 presents, "
                        "1 steps after one 22.0 ms late after 1 (the last present 22.7 ms, waited "
                        "14.6 ms for the GPU and 16.2 ms for the display, the fence waits "
                        "included: OpenGL; GPU 22.0 ms a replay); back to 1 a step after 120 steps "
                        "on time if the GPU allows"));
    }
    {
        BudgetSeq q = {
            .periodNs = lockPeriod, .refreshNs = lockPeriod, .gpuNs = 19 * MS, .stepNs = 2 * MS};

        pace_budget_reset(&q.b, 1);
        seqSteps(&q, 600, 1, 2, 10 * MS, 1, 0);
        pace_budget_block_text(&q.b, 0, 0, 0, 0, q.gpuNs, "no GPU timestamps", t, sizeof(t));
        CHECK(textIs(t, "; presents held to 0 a step (lowest 0, 299 refused, 1 drops, 0 climbs, "
                        "climbs refused 8); GPU 19.0 ms a timed replay"));
        pace_budget_block(&q.b);
        pace_budget_block_text(&q.b, q.b.refused, q.b.drops, q.b.climbs, q.b.climbRefusals, 0,
                               "no GPU timestamps", t, sizeof(t));
        CHECK(textIs(t, "; presents held to 0 a step (lowest 0, 0 refused, 0 drops, 0 climbs, "
                        "climbs refused 0); no GPU timestamps"));
    }
    {
        PaceBudget b;

        pace_budget_reset_unbound(&b, PACE_ALLOW_MAX);
        pace_budget_block_text(&b, 0, 0, 0, 0, 0, "GPU not timed yet", t, sizeof(t));
        CHECK(textIs(t, "; presents not held (lowest not held, 0 refused, 0 drops, 0 climbs, "
                        "climbs refused 0); GPU not timed yet"));
    }
    {
        /* paced by the GPU (12 ms a picture, a 0.5 ms step): the budget's
           line, the 10 s part, the drop's ending and the straight climb;
           1 ms: not held */
        PaceBudget b;
        PaceBudgetWaits w = {22 * MS, 21500000ull, wait, 12 * MS, MS / 2, PERIOD, false};

        pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
        (void)pacedSteps(&b, 120, 8, 1, 12 * MS, MS / 2, 0, NULL);
        pace_budget_event_text(&b, PACE_BUDGET_TEXT_PACED, &w, t, sizeof(t));
        CHECK(textIs(t, "presents paced by the GPU: 1.3 a step (GPU 12.0 ms a picture, period "
                        "16.7 ms)"));
        pace_budget_block_text(&b, 0, 0, 0, 0, 12 * MS, "GPU not timed yet", t, sizeof(t));
        CHECK(textIs(t, "; presents paced to 1.3 a step (lowest not held, 120 refused, 0 drops, 0 "
                        "climbs, climbs refused 0); GPU 12.0 ms a timed replay"));
        pace_budget_present(&b, true);
        pace_budget_present(&b, false);
        (void)pace_budget_step(&b, 10 * MS, PERIOD, REFRESH, 0, 12 * MS, MS / 2);
        pace_budget_present(&b, true);
        pace_budget_present(&b, false);
        (void)pace_budget_step(&b, 11 * MS, PERIOD, REFRESH, 0, 12 * MS, MS / 2);
        pace_budget_event_text(&b, PACE_BUDGET_TEXT_DROP, &w, t, sizeof(t));
        CHECK(textIs(t, "presents held to 1 a step: the step ended 11.0 ms late after 2 presents, "
                        "1 steps after one 10.0 ms late after 2 (the last present 22.0 ms, waited "
                        "21.5 ms for the GPU and 0.1 ms for the display; GPU 12.0 ms a replay); "
                        "back to paced by the GPU after 60 steps on time"));
        pace_budget_block_text(&b, 120, 0, 0, 0, 12 * MS, "GPU not timed yet", t, sizeof(t));
        CHECK(textIs(t, "; presents held to 1 a step (lowest 1, 0 refused, 1 drops, 0 climbs, "
                        "climbs refused 0); GPU 12.0 ms a timed replay"));
        (void)pacedSteps(&b, PACE_ALLOW_HOLD, 8, 1, 12 * MS, MS / 2, 0, NULL);
        pace_budget_event_text(&b, PACE_BUDGET_TEXT_CLIMB, &w, t, sizeof(t));
        CHECK(textIs(t, "presents back to paced by the GPU (where they started) after 60 steps "
                        "(GPU 12.0 ms a picture, step 0.5 ms, period 16.7 ms)"));
        pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
        (void)pacedSteps(&b, 10, 8, 1, 1 * MS, MS / 2, 0, NULL);
        w.gpuNs = 1 * MS;
        pace_budget_event_text(&b, PACE_BUDGET_TEXT_PACED, &w, t, sizeof(t));
        CHECK(textIs(t, "presents paced by the GPU: not held (GPU 1.0 ms a picture, period 16.7 "
                        "ms)"));
        pace_budget_block_text(&b, 0, 0, 0, 0, 1 * MS, "GPU not timed yet", t, sizeof(t));
        CHECK(textIs(t, "; presents not held (lowest not held, 0 refused, 0 drops, 0 climbs, "
                        "climbs refused 0); GPU 1.0 ms a timed replay"));
    }
    {
        PaceLock L = {0};

        pace_lock_timing_text(&L, "", 0, 0.0, 0, 0, t, sizeof(t));
        CHECK(textIs(t, "display clock off"));
        L.clockKind = PACE_CLOCK_KIND_VBLANK;
        L.clock = PACE_CLOCK_ON;
        L.measuredNs = L.slotNs = 16664500.0;
        L.refreshNs = 16666667ull;
        L.vbPhaseNs = 1.0e6;
        pace_lock_timing_text(&L, "\\\\.\\DISPLAY1", 0, 0.0, 0, 0, t, sizeof(t));
        CHECK(textIs(t, "display clock vblank (output \\\\.\\DISPLAY1, measured 16.6645 ms, slot "
                        "-130 ppm, phase +1.00 ms)"));
    }
}

/* window_host.c pace's timer path with the present allowance under a fake
   clock: a game frame every two steps of 3 ms, presents on a gap's grid
   (8.33 ms: a cap of 120 without the clamp, as under D3D12's FIFO; 0: back
   to back, FIFO's blocking present the timer), each present's CPU time 1 ms
   and its wait for the GPU's picture two before (RHI_FRAMES_IN_FLIGHT 2:
   max(0, ready[k-2] - now)), the GPU finishing pictures in order, gpuNs
   each (ready[k] = max(ready[k-1], submit) + gpuNs); the lag dropped past
   100 ms.  fifoPastNs: a present of a frame already shown returns that long
   past the deadline (FIFO blocking to a vblank), a wait for the display, or
   for the GPU with waitAsGpu.  budget false: the rule before.  paced: the
   timer path with vsync off (pace_budget_reset_paced, the GPU's time
   gpuNs known); else the whole-number allowance (vsync on). */
typedef struct BudgetRun {
    unsigned frames, presents, shown, refused, resyncs, drops, climbs, allow, lowest;
    uint64_t lateMax; /* the latest step after the first second */
} BudgetRun;

static BudgetRun budgetLoop(unsigned steps, uint64_t gpuNs, uint64_t gapNs, bool budget,
                            uint64_t fifoPastNs, bool waitAsGpu, bool paced)
{
    const uint64_t own = 1 * MS, stepNs = 3 * MS, resyncNs = 100 * MS;
    PaceHist h = {0};
    PaceBudget b;
    BudgetRun r = {0};
    uint64_t t = 0, deadline = 0, next = 0, cost = 0, waitNs = 0;
    uint64_t ready[4] = {0, 0, 0, 0};
    unsigned made = 0;
    uint32_t frame = 0, presented = 0, skipFrame = 0;
    bool skipOpen = false, slow = false;

    if (paced) {
        pace_budget_reset_paced(&b, PACE_ALLOW_MAX); /* as window_host.c timer_budget_reset */
    } else {
        pace_budget_reset_unbound(&b, PACE_ALLOW_MAX); /* vsync on: the whole-number rule */
    }
    for (unsigned k = 0; k < steps; k++) {
        t += stepNs;
        if (k % 2 == 0) {
            frame++;
            r.frames++;
        }
        deadline += PERIOD;
        for (;;) {
            const bool newFrame = presented != frame;

            if (pace_late_skip(newFrame, pace_skipped_last(skipOpen, skipFrame, frame), slow, t,
                               deadline, PERIOD) != PACE_SKIP_NONE) {
                skipOpen = true;
                skipFrame = frame;
                break;
            }
            if (!newFrame) {
                /* the cost guard, then the allowance as the last term */
                const bool old = t >= deadline || slow || t + cost > deadline;

                const bool allowed =
                    paced ? pace_budget_allows_paced(&b, false, gpuNs, stepNs, PERIOD)
                          : pace_budget_allows(&b, false);

                if (old || (budget && !allowed)) {
                    r.refused += old ? 0u : 1u;
                    t = t < deadline ? deadline : t;
                    break;
                }
            }
            if (gapNs != 0 && next != 0 && t < next) {
                if (next >= deadline) {
                    t = t < deadline ? deadline : t;
                    break;
                }
                t = next;
                continue;
            }
            {
                const uint64_t start = t;
                const uint64_t two = made >= 2 ? ready[(made - 2) % 4] : 0;
                const uint64_t prev = made >= 1 ? ready[(made - 1) % 4] : 0;
                const uint64_t fence = two > t ? two - t : 0;
                const uint64_t submit = t + fence + own;

                waitNs = fifoPastNs != 0 && !newFrame && submit < deadline + fifoPastNs
                             ? deadline + fifoPastNs - submit
                             : 0;
                cost = fence + own + waitNs;
                t += cost;
                ready[made % 4] = (prev > submit ? prev : submit) + gpuNs;
                made++;
                slow = pace_slow_present(&h, cost, REFRESH, PERIOD, false);
                r.shown += newFrame ? 1u : 0u;
                presented = frame;
                skipOpen = false;
                r.presents++;
                pace_budget_present(&b, newFrame);
                next = gapNs != 0 ? pace_next_present(next, start, gapNs) : 0;
            }
        }
        {
            const uint64_t late = t > deadline ? t - deadline : 0;

            if (k >= 60 && late > r.lateMax) {
                r.lateMax = late;
            }
            if (budget) {
                (void)pace_budget_step(&b, late, PERIOD, REFRESH, waitAsGpu ? 0 : waitNs, gpuNs,
                                       stepNs);
            }
        }
        if (t > deadline && t - deadline > resyncNs) {
            r.resyncs++;
            deadline = t;
        }
    }
    r.drops = b.drops;
    r.climbs = b.climbs;
    r.allow = b.allow;
    r.lowest = b.lowest;
    r.refused += b.refused;
    return r;
}

/* The timer path with the allowance, 10 s each.  A GPU of 4 ms or 12 ms a
   picture: the steps never end half a period late, nothing binds, the
   presents are the rule before's to the one (at 12 ms the GPU's own pace
   holds them to 1.4 a step, the steps at most 3.5 ms late in this model).
   20 ms a picture: the rule before ends steps more than half a period late
   (11.6 ms); the allowance drops the in-betweens to one a step and the
   steps end under half a period late, every frame presented, no resync.
   FIFO returns 10 ms past the deadline: the display's own wait, excused,
   nothing dropped; the same wait counted as the GPU's drops them. */
static void testBudgetTimerLoop(void)
{
    const uint64_t gap = 1000000000ull / 120;

    for (int g = 0; g < 2; g++) {
        const uint64_t gpu = g ? 12 * MS : 4 * MS;
        const BudgetRun old = budgetLoop(600, gpu, gap, false, 0, false, false);
        const BudgetRun r = budgetLoop(600, gpu, gap, true, 0, false, false);

        CHECK(r.presents == old.presents && r.drops == 0 && r.refused == 0);
        CHECK(r.resyncs == 0 && old.resyncs == 0 && r.lateMax < PERIOD / 2);
        CHECK(r.shown == r.frames && r.allow == PACE_ALLOW_NONE);
    }
    {
        /* Uncapped without vsync: back to back (gap 0), 1 ms a present and a
           GPU of 1 ms, about 13 presents a step held only by the cost guard
           (more than PACE_ALLOW_MAX, which bound them before the allowance
           started unbound): the presents are the rule before's to the one */
        const BudgetRun old = budgetLoop(600, 1 * MS, 0, false, 0, false, false);
        const BudgetRun r = budgetLoop(600, 1 * MS, 0, true, 0, false, false);

        printf("pace_policy_test: back-to-back presents of 1 ms: %u presents (%.1f a step), the "
               "rule before %u, %u refused, %u drops\n",
               r.presents, (double)r.presents / 600.0, old.presents, r.refused, r.drops);
        CHECK(r.presents == old.presents && r.refused == 0 && r.drops == 0);
        CHECK(r.presents > 600u * PACE_ALLOW_MAX && r.allow == PACE_ALLOW_NONE);
    }
    {
        const BudgetRun old = budgetLoop(600, 20 * MS, gap, false, 0, false, false);
        const BudgetRun r = budgetLoop(600, 20 * MS, gap, true, 0, false, false);

        printf("pace_policy_test: a GPU of 20 ms a picture at 120 a second: the rule before %u "
               "presents, latest step %.1f ms; the allowance %u presents, latest step %.1f ms, "
               "held to %u (%u drops, %u refused), %u resyncs\n",
               old.presents, (double)old.lateMax / 1e6, r.presents, (double)r.lateMax / 1e6,
               r.allow, r.drops, r.refused, r.resyncs);
        CHECK(old.lateMax >= PERIOD / 2);
        CHECK(r.lateMax < PERIOD / 2);
        CHECK(r.resyncs == 0 && r.shown == r.frames);
        CHECK(r.drops >= 1 && r.allow <= 1 && r.refused > 0);
        CHECK(r.presents <= 600 && r.presents >= r.frames);
    }
    {
        const BudgetRun old = budgetLoop(600, 2 * MS, 0, false, 10 * MS, false, false);
        const BudgetRun r = budgetLoop(600, 2 * MS, 0, true, 10 * MS, false, false);
        const BudgetRun gpu = budgetLoop(600, 2 * MS, 0, true, 10 * MS, true, false);

        CHECK(r.drops == 0 && r.presents == old.presents && r.refused == 0);
        CHECK(gpu.drops > 0 && gpu.presents < old.presents);
        CHECK(r.resyncs == 0 && gpu.resyncs == 0 && gpu.shown == gpu.frames);
    }
}

/* The timer path paced by the GPU (pace_budget_reset_paced; issue 130,
   v0.9.4): with the GPU's time known an in-between starts while (presents
   + 1) * GPU + step fits the period and what the step before left unused,
   so the presents a step average (period - step) / GPU.  A new frame every
   step, 8 presents wanted, a 0.5 ms step, 120 steps (2 s):
   12 ms a picture: 1,1,2 a step, 161 presents (80 a second; a whole number
   a step held it to 1, 60 a second), the budget 1.3 a step;
   9 ms: 1,2,2,2,1, 215 (107 a second), 1.8 a step;
   17 ms with a game frame every two steps: never two in a step, 114 (57 a
   second, the GPU's own 58: 114 x 17 ms is under 120 periods), 1.0 a step;
   1 ms: 8 a step never refused, the budget "not held".
   No step is late in any of them, nothing drops.  No GPU time (0: no
   timestamps): the whole-number allowance as testBudgetUnbound (15 a step
   never refused, two late steps of 15 drop to 8, the probe after 120 steps
   unbound again).  The safety net: a step 10 ms late leaves no time
   carried and arms; a second late step drops to 1 held 60 steps (no
   in-between at 1 whatever the time carried) and the 60th is straight back
   (the budget the gate, gateK 0); a step past its due time by less than
   half a period halves the time carried. */
static void testBudgetPaced(void)
{
    const uint64_t step = MS / 2;
    unsigned c[120];
    PaceBudget b;

    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    CHECK(b.paced && b.unbind && b.allow == PACE_ALLOW_NONE && b.max == PACE_ALLOW_MAX);
    CHECK(b.carryNs == 0 && b.pacedTenths == PACE_PACED_UNKNOWN && b.pacedChanges == 0);
    CHECK(pacedSteps(&b, 120, 8, 1, 12 * MS, step, 0, c) == 161);
    CHECK(c[0] == 1 && c[1] == 1 && c[2] == 2 && c[3] == 1 && c[4] == 1 && c[5] == 2);
    CHECK(b.pacedTenths == 13 && b.pacedChanges == 1 && b.carryNs <= PERIOD);
    CHECK(b.arms == 0 && b.drops == 0 && b.allow == PACE_ALLOW_NONE && b.refused == 120);
    printf("pace_policy_test: paced, 12 ms a picture: %u %u %u %u %u %u a step, %.1f a second\n",
           c[0], c[1], c[2], c[3], c[4], c[5], 161.0 / 120.0 * 1e9 / (double)PERIOD);

    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    CHECK(pacedSteps(&b, 120, 8, 1, 9 * MS, step, 0, c) == 215);
    CHECK(c[0] == 1 && c[1] == 2 && c[2] == 2 && c[3] == 2 && c[4] == 1);
    CHECK(b.pacedTenths == 18 && b.arms == 0 && b.drops == 0);

    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    CHECK(pacedSteps(&b, 120, 8, 2, 17 * MS, step, 0, c) == 114);
    for (unsigned k = 0; k < 120; k++) {
        CHECK(c[k] <= 1);
    }
    CHECK(114u * 17 * MS < 120u * PERIOD && b.pacedTenths == 10 && b.drops == 0);

    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    CHECK(pacedSteps(&b, 120, 8, 1, 1 * MS, step, 0, NULL) == 960 && b.refused == 0);
    CHECK(b.pacedTenths == PACE_PACED_HIGH && b.allow == PACE_ALLOW_NONE);

    /* no GPU time: the whole-number allowance and its probing climb */
    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    CHECK(pacedSteps(&b, 600, 15, 1, 0, step, 0, NULL) == 15u * 600 && b.refused == 0);
    CHECK(b.pacedTenths == PACE_PACED_UNKNOWN && b.carryNs == 0);
    CHECK(budgetSteps(&b, 2, 15, true, 10 * MS, 0, 0) == 1 && b.allow == PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB_STEP - 1, 1, true, 0, 0, 0) == 0);
    CHECK(b.allow == PACE_ALLOW_MAX);
    CHECK(budgetSteps(&b, 1, 1, true, 0, 0, 0) == 1 && b.allow == PACE_ALLOW_NONE);
    CHECK(b.climbs == 1 && b.gateK == PACE_ALLOW_MAX + 1);

    /* the safety net at 12 ms a picture */
    pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
    (void)pacedSteps(&b, 30, 8, 1, 12 * MS, step, 0, NULL);
    pace_budget_present(&b, true);
    pace_budget_present(&b, false);
    CHECK(!pace_budget_step(&b, 10 * MS, PERIOD, REFRESH, 0, 12 * MS, step));
    CHECK(b.arms == 1 && b.drops == 0 && b.carryNs == 0 && b.allow == PACE_ALLOW_NONE);
    pace_budget_present(&b, true);
    pace_budget_present(&b, false);
    CHECK(pace_budget_step(&b, 11 * MS, PERIOD, REFRESH, 0, 12 * MS, step));
    CHECK(b.allow == 1 && b.drops == 1 && b.hold == PACE_ALLOW_HOLD && b.carryNs == 0);
    CHECK(pacedSteps(&b, PACE_ALLOW_HOLD - 1, 8, 1, 12 * MS, step, 0, NULL) == PACE_ALLOW_HOLD - 1);
    CHECK(b.allow == 1 && b.hold == 1 && b.carryNs == PERIOD && b.climbs == 0);
    CHECK(pacedSteps(&b, 1, 8, 1, 12 * MS, step, 0, NULL) == 1);
    CHECK(b.allow == PACE_ALLOW_NONE && b.climbs == 1 && b.gateK == 0);
    CHECK(b.sinceDrop == PACE_ALLOW_HOLD && b.dropMost == 0 && b.climbRefusals == 0);
    /* back home with a period carried: two at once */
    CHECK(pacedSteps(&b, 1, 8, 1, 12 * MS, step, 0, NULL) == 2);
    /* past its due time by less than half a period: half the time carried */
    {
        PaceBudget e;

        pace_budget_reset_paced(&b, PACE_ALLOW_MAX);
        (void)pacedSteps(&b, 4, 1, 1, 12 * MS, step, 0, NULL);
        e = b;
        (void)pacedSteps(&b, 1, 1, 1, 12 * MS, step, 0, NULL);
        (void)pacedSteps(&e, 1, 1, 1, 12 * MS, step, 3 * MS, NULL);
        CHECK(b.carryNs > 0 && e.carryNs == b.carryNs / 2 && e.arms == 0);
    }
}

/* The timer path paced by the GPU under the same fake clock (Uncapped
   without vsync: back to back, gap 0; a 3 ms step, so the budget is
   (16.68 - 3) / GPU presents a step).  12, 9, 17 and 20 ms a picture: no
   step ends a quarter of a period late, nothing drops, every frame
   presented, no resync, and the presents are the budget's (at least
   600 x (period - step) / GPU less 5) without asking the GPU for more than
   the time there was (presents x GPU within 601 periods); at 20 ms the
   whole-number allowance ended steps 13 ms late before its drop.  25 ms (a
   GPU-bound phone's picture): the new frames only, the game's own 30.
   1 ms: the presents of the whole-number rule to the one (13 a step, held
   only by the cost guard). */
static void testBudgetPacedLoop(void)
{
    static const unsigned gpus[] = {12, 9, 17, 20};
    const uint64_t stepNs = 3 * MS;

    for (unsigned i = 0; i < sizeof(gpus) / sizeof(gpus[0]); i++) {
        const uint64_t gpu = gpus[i] * MS;
        const BudgetRun old = budgetLoop(600, gpu, 0, true, 0, false, false);
        const BudgetRun r = budgetLoop(600, gpu, 0, true, 0, false, true);

        printf("pace_policy_test: paced, back to back, GPU %u ms a picture: %u presents (%.2f a "
               "step), latest step %.1f ms, %u drops; the whole-number allowance %u presents, "
               "latest step %.1f ms, %u drops\n",
               gpus[i], r.presents, (double)r.presents / 600.0, (double)r.lateMax / 1e6, r.drops,
               old.presents, (double)old.lateMax / 1e6, old.drops);
        CHECK(r.lateMax < PERIOD / 4 && r.drops == 0 && r.resyncs == 0);
        CHECK(r.shown == r.frames && r.allow == PACE_ALLOW_NONE);
        CHECK((uint64_t)r.presents * gpu <= 601u * PERIOD);
        CHECK((uint64_t)r.presents + 5 >= 600u * (PERIOD - stepNs) / gpu);
        if (gpus[i] == 20) {
            CHECK(old.lateMax >= PERIOD / 2 && old.drops >= 1);
        }
    }
    {
        const BudgetRun r = budgetLoop(600, 25 * MS, 0, true, 0, false, true);

        CHECK(r.presents == r.frames && r.shown == r.frames && r.lateMax < PERIOD / 4);
        CHECK(r.drops == 0 && r.resyncs == 0);
    }
    {
        const BudgetRun old = budgetLoop(600, 1 * MS, 0, true, 0, false, false);
        const BudgetRun r = budgetLoop(600, 1 * MS, 0, true, 0, false, true);

        CHECK(r.presents == old.presents && r.refused == 0 && r.drops == 0);
    }
}

int main(void)
{
    PaceHist h = {0};
    testBudgetRule();
    testBudgetUnbound();
    testBudgetHitch130();
    testBudgetSustained();
    testBudgetGlWait126();
    testBudgetLines();
    testBudgetTimerLoop();
    testBudgetPaced();
    testBudgetPacedLoop();
    testLateSkipLate();
    testLateSkipSlow();
    testLateSkipNeverTwice();
    testLateSkipPresentedFrame();
    testLateSkipBeforeDeadline();
    testLateSkipLoop();
    testLockPause();
    testIntervals();
    testFirstPresentPause();
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
    testCapEqualsRefresh();
    testLimiterGrid();
    testLockTable();
    testGridPeriod();
    testVblankLaws();

    if (fails) {
        printf("pace_policy_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_policy_test: ok\n");
    return 0;
}
