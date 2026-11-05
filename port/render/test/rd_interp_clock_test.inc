/* rd_interp_test.c's cases for the present clock's step
 * (rd_present_clock.c) and Motion blur: Smooth's FIX' from time
 * (rd_blur_smooth.c rd__blur_smooth_fix_dt), issue 193: the reporter's
 * Smooth at Frame rate 60 read 64 presents a tick (a clock step stuck at
 * 0.5 ms) and drew FIX 32 as 125 at 1.7 presents a tick.  Included after
 * testPresentClock. */

/* FIX' from time (rd__blur_smooth_fix_dt, issue 193): at dt = tick / k
 * the k form for every FIX and k 1..64; 1.7 presents a tick keep the
 * game's FIX / 128 a tick within the rounding, uneven gaps (the
 * reporter's 0.5 ms present among 14..20 ms ones) the share their span
 * keeps; dt held to [tick / 64, tick], no tick: FIX as recorded */
static void testSmoothFixDt(void)
{
    const double tick = 33.3667;
    uint32_t badFix = 0, badK = 0, bad = 0;
    for (uint32_t fix = 0; fix <= 128; fix++) {
        for (uint32_t k = 1; k <= RD_BLUR_SMOOTH_K_MAX; k++) {
            if (rd__blur_smooth_fix_dt(fix, tick / (double)k, tick) !=
                rd__blur_smooth_fix(fix, k)) {
                bad++;
                badFix = fix;
                badK = k;
            }
        }
    }
    CHECK(bad == 0, "FIX' from time at dt = tick / k: %u differ from the k form (FIX %u, k %u)",
          bad, badFix, badK);
    double worst = 0.0;
    for (uint32_t fix = 8; fix <= 96; fix += 8) {
        const uint8_t fp = rd__blur_smooth_fix_dt(fix, tick / 1.7, tick);
        const double share = pow((double)fp / 128.0, 1.7), want = (double)fix / 128.0;
        worst = fabs(share / want - 1.0) > worst ? fabs(share / want - 1.0) : worst;
    }
    CHECK(rd__blur_smooth_fix_dt(32, tick / 1.7, tick) == 57 && worst < 0.02,
          "FIX' from time at 1.7 presents a tick: FIX 32 drawn as %u, the share a tick "
          "within %.2f %% of the game's for FIX 8..96",
          rd__blur_smooth_fix_dt(32, tick / 1.7, tick), worst * 100.0);
    const double gaps[] = {0.5, 19.0, 14.0, 20.0, 17.5, 0.5, 16.0, 18.4};
    double keep = 1.0, span = 0.0;
    for (size_t i = 0; i < sizeof(gaps) / sizeof(gaps[0]); i++) {
        keep *= (double)rd__blur_smooth_fix_dt(32, gaps[i], tick) / 128.0;
        span += gaps[i] < tick / 64.0 ? tick / 64.0 : gaps[i];
    }
    const double want = pow(0.25, span / tick);
    CHECK(fabs(keep / want - 1.0) < 0.05,
          "FIX' from time over uneven gaps: the trail keeps %.5f over %.1f ms (the game's "
          "share %.5f)",
          keep, span, want);
    CHECK(rd__blur_smooth_fix_dt(32, 0.1, tick) == rd__blur_smooth_fix(32, 64) &&
              rd__blur_smooth_fix_dt(32, -1.0, tick) == rd__blur_smooth_fix(32, 64) &&
              rd__blur_smooth_fix_dt(32, 500.0, tick) == 32 &&
              rd__blur_smooth_fix_dt(32, tick, tick) == 32 &&
              rd__blur_smooth_fix_dt(32, 10.0, 0.0) == 32 &&
              rd__blur_smooth_fix_dt(0, 10.0, tick) == 0 &&
              rd__blur_smooth_fix_dt(128, 10.0, tick) == 128,
          "FIX' from time held: dt 0.1 %u, -1 %u, 500 %u, a tick %u, no tick %u, FIX 0 %u, "
          "128 %u",
          rd__blur_smooth_fix_dt(32, 0.1, tick), rd__blur_smooth_fix_dt(32, -1.0, tick),
          rd__blur_smooth_fix_dt(32, 500.0, tick), rd__blur_smooth_fix_dt(32, tick, tick),
          rd__blur_smooth_fix_dt(32, 10.0, 0.0), rd__blur_smooth_fix_dt(0, 10.0, tick),
          rd__blur_smooth_fix_dt(128, 10.0, tick));
}

/* The present clock's step after a doubled present and a stuck step (issue
 * 193): the step is learnt again, and Motion blur: Smooth's presents a tick
 * (rd__present_clock_k) follow the real rate, never the cap. */
static void testPresentClockStep(void)
{
    const double tick = 33.3667, cadence = 16.6833, nominal = 1000.0 / 60.0;
    double gap[2];
    /* (1) a steady 60 Hz cadence, one present 0.5 ms after its neighbour,
     * then the cadence again: the step within 10 % of the cadence inside
     * 32 presents, at most 2 resets for it */
    RdPresentClock c;
    memset(&c, 0, sizeof(c));
    double now = 1000.0;
    for (int i = 0; i < 200; i++, now += cadence) {
        (void)rd_present_clock_alpha(&c, now, floor(now / tick) * tick, tick, nominal);
    }
    const uint32_t before = c.resets;
    const double last = now - cadence;
    (void)rd_present_clock_alpha(&c, last + 0.5, floor(last / tick) * tick, tick, nominal);
    const double stepDoubled = c.stepMs;
    int within = -1;
    for (int i = 0; i < 32; i++, now += cadence) {
        (void)rd_present_clock_alpha(&c, now, floor(now / tick) * tick, tick, nominal);
        if (within < 0 && fabs(c.stepMs - cadence) < 0.1 * cadence) {
            within = i;
        }
    }
    const double raw = rd__present_clock_take(gap);
    CHECK(within >= 0 && fabs(c.stepMs - cadence) < 0.1 * cadence && c.resets - before <= 2u &&
              fabs(stepDoubled - cadence) < 0.1 * cadence,
          "present clock: a doubled present in a 60 Hz cadence: the step %.3f ms after it, %.3f "
          "ms 32 presents on (back within 10 %% at present %d), %u resets",
          stepDoubled, c.stepMs, within, c.resets - before);
    CHECK(rd__present_clock_k(raw, 0u) == 2u && fabs(gap[0] - cadence) < 1e-6 && gap[1] == tick,
          "present clock: 2 presents a tick at 60 Hz against a 30 Hz tick (%u), the time since "
          "the present before %.4f ms and the tick %.4f ms noted",
          rd__present_clock_k(raw, 0u), gap[0], gap[1]);
    CHECK(isnan(rd__present_clock_take(gap)) && gap[0] == 0.0 && gap[1] == 0.0,
          "present clock: taken once (then no value, no time)");

    /* (2) the reporter's timing (Frame rate 60, FIFO: 49.7 presented fps
     * against 28.8 game fps, a slow replay of 116 ms): presents 18..22 ms
     * apart, one 0.5 ms after its neighbour every 7th (none next to the
     * pause), and every 50th a
     * 120 ms one with a present 0.5 ms after it (the clock reset at the
     * measured time, then a doubled present over half a tick from it: the
     * reset that took 0.5 ms as the step before v0.10.4, k 66 held to 64).
     * At every tick's first present k is 2, the step stays in the cadence
     * and no more than the pause and the doubled present reset in a row */
    memset(&c, 0, sizeof(c));
    uint32_t seed = 193u, kMin = 1000u, kMax = 0u, run = 0u, runMax = 0u;
    double tickAt = -1.0;
    now = 5000.0;
    for (int i = 0; i < 600; i++) {
        seed = seed * 1664525u + 1013904223u;
        const double u = (double)(seed >> 8) / 16777216.0;
        const bool doubled = i % 50 == 49 || (i % 7 == 6 && i % 50 != 0 && i % 50 < 47);
        now += i % 50 == 48 ? 120.0 : (doubled ? 0.5 : 18.0 + 4.0 * u);
        const double at = floor(now / tick) * tick;
        const uint32_t r0 = c.resets;
        (void)rd_present_clock_alpha(&c, now, at, tick, nominal);
        run = c.resets != r0 ? run + 1u : 0u;
        runMax = run > runMax ? run : runMax;
        const double rv = rd__present_clock_take(NULL);
        if (at != tickAt && i >= 8) {
            const uint32_t k = rd__present_clock_k(rv, 0u);
            kMin = k < kMin ? k : kMin;
            kMax = k > kMax ? k : kMax;
        }
        tickAt = at;
    }
    CHECK(kMin == 2u && kMax == 2u && runMax <= 2u && c.stepMs > 18.0 && c.stepMs < 22.0,
          "present clock: the reporter's timing: %u to %u presents a tick, the step %.3f ms, "
          "%u resets in a row at most",
          kMin, kMax, c.stepMs, runMax);

    /* (3) a step already stuck at 0.5 ms (the v0.10.2 state), with the
     * Frame rate's gap and without one (Uncapped): one present at the
     * cadence learns it again */
    for (int nom = 0; nom < 2; nom++) {
        memset(&c, 0, sizeof(c));
        (void)rd_present_clock_alpha(&c, 9000.0, 9000.0, tick, nom ? nominal : 0.0);
        c.stepMs = 0.5;
        (void)rd_present_clock_alpha(&c, 9018.0, 9000.0, tick, nom ? nominal : 0.0);
        const double rv = rd__present_clock_take(NULL);
        CHECK(fabs(c.stepMs - 18.0) < 1e-9 && rd__present_clock_k(rv, 0u) == 2u,
              "present clock: a stuck 0.5 ms step learnt again %s (%.3f ms, %u presents a tick)",
              nom ? "with the frame rate's gap" : "without one", c.stepMs,
              rd__present_clock_k(rv, 0u));
    }
    /* (4) a pause keeps the step; the vsync lock (no clock value) counts */
    memset(&c, 0, sizeof(c));
    now = 20000.0;
    for (int i = 0; i < 40; i++, now += cadence) {
        (void)rd_present_clock_alpha(&c, now, floor(now / tick) * tick, tick, nominal);
    }
    const double stepKept = c.stepMs;
    (void)rd_present_clock_alpha(&c, now + 500.0, floor((now + 500.0) / tick) * tick, tick,
                                 nominal);
    (void)rd__present_clock_take(gap);
    CHECK(c.stepMs == stepKept && gap[0] > 500.0 && rd__present_clock_k(NAN, 3u) == 3u,
          "present clock: a 500 ms pause keeps the step (%.3f ms, was %.3f), its time %.1f ms "
          "noted; no clock value: the counted presents (%u)",
          c.stepMs, stepKept, gap[0], rd__present_clock_k(NAN, 3u));
}

/* The present clock across doubled presents (the v0.10.4 review): presents
 * 18..22 ms apart (the issue 193 reporter's rate) at Frame rate 60, one
 * 0.5 ms after its neighbour every 7th or every 50th, over 3000 presents.
 * The clock never goes back and the alpha never drops within a tick, with
 * at most 2 resets.  Before the fix a doubled present moved the clock a
 * whole step, ahead of the measured time, and the next one reset it back:
 * 413 resets and 156 steps back (9.25 ms at worst) at every 7th. */
static void testPresentClockDoubled(void)
{
    const double tick = 33.3667, nominal = 1000.0 / 60.0;
    static const int every[] = {7, 50};
    for (size_t e = 0; e < sizeof(every) / sizeof(every[0]); e++) {
        RdPresentClock c;
        memset(&c, 0, sizeof(c));
        uint32_t seed = 193u;
        double now = 5000.0, lastClock = -1.0, lastAt = -1.0, worst = 0.0;
        float lastA = -1.0f;
        int back = 0, dropped = 0;
        for (int i = 0; i < 3000; i++) {
            seed = seed * 1664525u + 1013904223u;
            const double u = (double)(seed >> 8) / 16777216.0;
            now += i % every[e] == every[e] - 1 ? 0.5 : 18.0 + 4.0 * u;
            const double at = floor(now / tick) * tick;
            const float a = rd_present_clock_alpha(&c, now, at, tick, nominal);
            if (lastClock >= 0.0 && c.clockMs < lastClock) {
                back++;
                worst = lastClock - c.clockMs > worst ? lastClock - c.clockMs : worst;
            }
            if (at == lastAt && a < lastA) {
                dropped++;
            }
            lastClock = c.clockMs;
            lastAt = at;
            lastA = a;
        }
        (void)rd__present_clock_take(NULL);
        CHECK(back == 0 && dropped == 0 && c.resets <= 2u,
              "present clock: a doubled present every %d of 3000 at 18..22 ms: the clock went "
              "back %d times (%.3f ms at worst), the alpha dropped within a tick %d times, %u "
              "resets",
              every[e], back, worst, dropped, c.resets);
    }
}

/* The present clock at a rate slower than four nominals (the v0.10.4
 * review): a 120 Hz FIFO phone at 60 then 25 presented fps, a 60 Hz
 * display at 30 then 12, and Uncapped (no nominal) at 25 from the start,
 * each present +-1 ms.  The step is learnt (within 10 % of the interval)
 * and the last 250 of 600 presents reset at most twice.  Before the fix
 * the step was held to four nominals (a tick without one) and every one
 * of them reset: the raw measured alpha, read in the log as issue 193's
 * stuck clock. */
static void testPresentClockSlow(void)
{
    const double tick = 33.3667;

    static const struct {
        double nominal, fast, slow;
    } cases[] = {
        {1000.0 / 120.0, 16.6833, 40.0},
        {1000.0 / 60.0, 33.3667, 83.0},
        {0.0, 40.0, 40.0},
    };

    for (size_t n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
        RdPresentClock c;
        memset(&c, 0, sizeof(c));
        uint32_t seed = 194u, r0 = 0u;
        double now = 1000.0;
        for (int i = 0; i < 600; i++) {
            seed = seed * 1664525u + 1013904223u;
            const double u = (double)(seed >> 8) / 16777216.0;
            now += (i < 300 ? cases[n].fast : cases[n].slow) + 2.0 * (u - 0.5);
            (void)rd_present_clock_alpha(&c, now, floor(now / tick) * tick, tick, cases[n].nominal);
            r0 = i == 349 ? c.resets : r0;
        }
        (void)rd__present_clock_take(NULL);
        CHECK(c.resets - r0 <= 2u && fabs(c.stepMs - cases[n].slow) < 0.1 * cases[n].slow,
              "present clock: nominal %.3f ms, %.1f then %.1f ms a present: the step %.3f ms, "
              "%u resets in the last 250 presents",
              cases[n].nominal, cases[n].fast, cases[n].slow, c.stepMs, c.resets - r0);
    }
}
