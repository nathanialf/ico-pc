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
 * The timer path's audio skip (testAudioSkip*: never two in a row, only
 * when slow or the audio low, never without audio, never a frame already
 * presented, never before the deadline; the audio on the device's clock,
 * a cushion of 0, never low).
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

/* The mode a numeric cap takes with vsync on: a cap equal to the
   refresh within 0.5 Hz takes FIFO with no timer gap; above or below it,
   mailbox (above) with the cap's own gap */
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
    /* above the refresh: not equal, the cap's own gap (mailbox) */
    CHECK(!pace_cap_equals_refresh(144, r6000));
    CHECK(!pace_cap_equals_refresh(144, r120));
    CHECK(!pace_cap_equals_refresh(240, r144));
    CHECK(pace_present_gap(144, false, true, r6000) == 1000000000ull / 144);
    CHECK(pace_present_gap(240, false, true, r144) == 1000000000ull / 240);
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
    const PaceLockIo io = {d, s_now, s_present, s_sleep, NULL, NULL, NULL, s_vblank, NULL};
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

/* the audio skip: arguments in pace_audio_skip's order (pastDeadline,
   newFrame, skippedLast, paceSlow, audioActive, queuedMs, cushionMs); a
   cushion of 100 ms: low below 50 queued */
static void testAudioSkipNeverTwice(void)
{
    /* the frame after a skipped one is presented, slow or low */
    CHECK(pace_audio_skip(true, true, false, true, true, 100, 100));
    CHECK(!pace_audio_skip(true, true, true, true, true, 100, 100));
    CHECK(!pace_audio_skip(true, true, true, false, true, 10, 100));
    CHECK(!pace_audio_skip(true, true, true, true, true, 10, 100));
    /* the window's loop: alternate frames past the deadline, slow, keep
       every other one */
    bool skippedLast = false;
    int skips = 0;
    for (int i = 0; i < 10; i++) {
        if (pace_audio_skip(true, true, skippedLast, true, true, 10, 100)) {
            skippedLast = true;
            skips++;
        } else {
            skippedLast = false; /* presented */
        }
    }
    CHECK(skips == 5);
}

static void testAudioSkipSlowOrLow(void)
{
    /* neither slow nor low: no skip */
    CHECK(!pace_audio_skip(true, true, false, false, true, 100, 100));
    CHECK(!pace_audio_skip(true, true, false, false, true, 50, 100));
    /* slow, the audio full */
    CHECK(pace_audio_skip(true, true, false, true, true, 100, 100));
    /* low: under half the cushion, in whole ms (2 * 49 < 100; 2 * 50 not) */
    CHECK(pace_audio_skip(true, true, false, false, true, 49, 100));
    CHECK(pace_audio_skip(true, true, false, false, true, 0, 100));
    CHECK(pace_audio_skip(true, true, false, false, true, 49, 99));
    CHECK(!pace_audio_skip(true, true, false, false, true, 50, 99));
    CHECK(!pace_audio_skip(true, true, false, false, true, 50, 100));
    /* odd cushion: 2 * 25 < 51 is low, 2 * 26 not */
    CHECK(pace_audio_skip(true, true, false, false, true, 25, 51));
    CHECK(!pace_audio_skip(true, true, false, false, true, 26, 51));
    /* no stream (-1) is never low; no cushion (0) is never low */
    CHECK(!pace_audio_skip(true, true, false, false, true, -1, 100));
    CHECK(!pace_audio_skip(true, true, false, false, true, 0, 0));
}

/* the audio on the device's clock (out_sdl.c, the pull path): a cushion of
   0 with any lead queued is never low, so only slow presents skip */
static void testAudioSkipPullOutput(void)
{
    for (int q = 0; q <= 50; q += 10) {
        CHECK(!pace_audio_skip(true, true, false, false, true, q, 0));
        CHECK(pace_audio_skip(true, true, false, true, true, q, 0));
        CHECK(!pace_audio_skip(true, true, true, true, true, q, 0));
    }
    CHECK(!pace_audio_skip(false, true, false, true, true, 10, 0));
}

static void testAudioSkipNoAudio(void)
{
    /* without the audio playing, slow or low, every new frame is presented */
    CHECK(!pace_audio_skip(true, true, false, true, false, 100, 100));
    CHECK(!pace_audio_skip(true, true, false, false, false, 10, 100));
    CHECK(!pace_audio_skip(true, true, false, true, false, -1, 0));
}

static void testAudioSkipPresentedFrame(void)
{
    /* a frame already presented is never skipped here (the paths below
       leave it) */
    CHECK(!pace_audio_skip(true, false, false, true, true, 100, 100));
    CHECK(!pace_audio_skip(true, false, false, false, true, 10, 100));
    CHECK(!pace_audio_skip(true, false, false, true, true, 10, 100));
}

static void testAudioSkipBeforeDeadline(void)
{
    /* before the deadline a new frame is presented, slow or low */
    CHECK(!pace_audio_skip(false, true, false, true, true, 100, 100));
    CHECK(!pace_audio_skip(false, true, false, false, true, 10, 100));
    CHECK(!pace_audio_skip(false, true, false, true, true, 10, 100));
}

int main(void)
{
    PaceHist h = {0};
    testAudioSkipNeverTwice();
    testAudioSkipSlowOrLow();
    testAudioSkipPullOutput();
    testAudioSkipNoAudio();
    testAudioSkipPresentedFrame();
    testAudioSkipBeforeDeadline();
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
