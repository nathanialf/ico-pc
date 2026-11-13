/*
 * port/render/test/rd_pace_screen_test.c
 *
 * The screen's refresh for pictures as made (rd_pace_screen.h), CPU only:
 *   1. the parts: the expected time on the glass (the refresh itself with
 *      no spread, between two with one, never decreasing), the latch's fit
 *      (the range most samples agree with, the one nearest the last), the
 *      grid's fit (the period measured, not the one reported; off one grid
 *      refused), the score;
 *   2. the placement: the chain's first picture at the value drawn
 *      otherwise to the bit, the lag's step at most RD_PS_SLEW_MS, never
 *      back, held at the tick's edges, a restart, every fall-back with the
 *      value drawn otherwise to the bit;
 *   3. 10 s simulations of the owner's phone's regimes (v0.11.6-rc4: play
 *      at a mix of one and two refreshes a picture, the opening at two and
 *      three, a scene with kept frames, one a tick, the prediction exact):
 *      the window asks when the render thread is free, a further picture
 *      held when its reads would end past the next closing step, the GPU's
 *      start the later of the first submit and the end before, the GPU
 *      queue's model (rd_pace_gpu.c) fed two pictures late, and a display
 *      model: a refresh grid of 1000/120 ms at its own phase (8.35 ms
 *      reported, as the phone reports), a latch lead with a jitter, the
 *      display's clock offset from rd's, mailbox (a picture shown at the
 *      first refresh whose latch follows its GPU end, replaced when the next
 *      one makes the same refresh), each shown picture's time back three
 *      refreshes after it was shown.  The content's speed on the glass
 *      (its step over the display's gap) for the hand-over rule, the GPU's
 *      predicted end, the own timer and the screen's refresh over the same
 *      pictures, the screen's score, its holds, and its content's rate over
 *      the run against real time;
 *   4. the fall-backs over a run: no display times, times that stop, a
 *      display that changes its refresh;
 *   5. the renderer's side: the start line and the clause word for word,
 *      the ledger's row.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_as_made_timer.h"
#include "rd_pace_gpu.h"
#include "rd_pace_screen.h"
#include "rd_rebase.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define TICK 33.3667
#define PD (1000.0 / 120.0) /* the display's true refresh */
#define REP 8.35            /* as the phone reports it (rc4: "refreshes of 8.35 ms") */

/* ------------------------------------------------------------------- 1 */

static void testParts(void)
{
    double hard;
    /* no spread: the first refresh at or after t */
    CHECK(rd_pace_screen_expect(1003.0, 1000.0, 8.0, 0.0, &hard) == 1008.0 && hard == 1008.0);
    CHECK(rd_pace_screen_expect(1008.0, 1000.0, 8.0, 0.0, &hard) == 1008.0);
    CHECK(rd_pace_screen_expect(999.0, 1000.0, 8.0, 0.0, &hard) == 1000.0);
    /* a spread: just past a refresh, about half on it and half on the next */
    const double g = rd_pace_screen_expect(1000.0, 1000.0, 8.0, 1.0, &hard);
    CHECK(hard == 1000.0 && g > 1003.0 && g < 1005.0);
    /* deep inside a refresh with a small spread: the refresh itself */
    CHECK(fabs(rd_pace_screen_expect(1004.0, 1000.0, 8.0, 0.5, &hard) - 1008.0) < 1e-6);
    /* never decreasing over a sweep, and within the hard refreshes around */
    double last = -1.0;
    int dec = 0;
    for (int i = 0; i <= 400; i++) {
        const double t = 990.0 + 0.1 * i;
        const double e = rd_pace_screen_expect(t, 1000.0, 8.0, 2.0, &hard);
        dec += e < last - 1e-12 ? 1 : 0;
        last = e;
    }
    CHECK(dec == 0);

    /* the latch: samples whose refresh less the end is in [3, 3 + 8): all
       agree on x in (max d - 8, min d] around 3 */
    double d[RD_PS_WIN];
    uint32_t seed = 7u;
    double dmin = 1e9, dmax = -1e9;
    for (int i = 0; i < 40; i++) {
        seed = seed * 1664525u + 1013904223u;
        d[i] = 3.0 + 7.9 * ((double)(seed >> 8) / 16777216.0);
        dmin = d[i] < dmin ? d[i] : dmin;
        dmax = d[i] > dmax ? d[i] : dmax;
    }
    uint32_t hits;
    double x = rd_pace_screen_latch(d, 40, 8.0, NAN, &hits);
    CHECK(hits == 40 && x > dmax - 8.0 && x <= dmin && fabs(x - 3.0) < 8.0 - (dmax - dmin));
    /* two disjoint ranges of equal agreement: the one nearest the last */
    const double two[4] = {1.0, 1.5, 21.0, 21.5};
    x = rd_pace_screen_latch(two, 4, 8.0, 20.0, &hits);
    CHECK(hits == 2 && x > 13.0 && x <= 21.0);
    x = rd_pace_screen_latch(two, 4, 8.0, NAN, &hits);
    CHECK(hits == 2 && x <= 1.0);
    CHECK(isnan(rd_pace_screen_latch(two, 0, 8.0, NAN, &hits)) && hits == 0);

    /* the grid: times on 1000/120 ms reported as 8.35: the period measured */
    RdPaceScreen m;
    rd_pace_screen_reset(&m);
    for (int i = 0; i < RD_PS_WIN; i++) {
        const double at = 5000.0 + PD * (2.0 * i + (i % 3 == 0 ? 1.0 : 0.0));
        const RdPaceScreenSample s = {at - 4.0 - 0.05 * (i % 7), at, REP, 0.0};
        rd_pace_screen_add(&m, &s);
    }
    rd_pace_screen_fit(&m);
    CHECK(m.fit.grid && fabs(m.fit.periodMs - PD) < 1e-6 && m.fit.offGrid == 0);
    CHECK(fabs(fmod(m.fit.anchorMs - 5000.0 + 1e3 * PD, PD)) < 1e-6 ||
          fabs(fmod(m.fit.anchorMs - 5000.0 + 1e3 * PD, PD) - PD) < 1e-6);
    /* the ends 4.00-4.30 ms before their refresh: x in (4.30 - P, 4.00] */
    CHECK(m.fit.latch && m.fit.hits == RD_PS_WIN && m.fit.xMs > 4.3 - PD && m.fit.xMs <= 4.0 &&
          m.fit.widthMs == 0.0);
    /* a refresh rate that changes (every other gap 1.5 refreshes): refused */
    rd_pace_screen_reset(&m);
    for (int i = 0; i < RD_PS_WIN; i++) {
        const double at = 5000.0 + PD * (1.5 * i);
        const RdPaceScreenSample s = {at - 4.0, at, REP, 0.0};
        rd_pace_screen_add(&m, &s);
    }
    rd_pace_screen_fit(&m);
    CHECK(!m.fit.grid);

    /* the score in refreshes */
    CHECK(rd_pace_screen_score(1000.0, 1000.0, REP) == 0);
    CHECK(rd_pace_screen_score(1000.0, 1000.0 + PD, REP) == 1);
    CHECK(rd_pace_screen_score(1000.0, 1000.0 - PD, REP) == -1);
    CHECK(rd_pace_screen_score(1000.0, 1000.0 + 3.0 * PD, REP) == 3);
}

/* ------------------------------------------------------------------- 2 */

/* a model with RD_PS_WIN shown pictures on the grid, each ended 4 ms
   before its refresh (the newest predicted end at 5000 + ...) */
static void filled(RdPaceScreen *m, double newestEnd)
{
    rd_pace_screen_reset(m);
    for (int i = RD_PS_WIN - 1; i >= 0; i--) {
        const double at = newestEnd + 4.0 - PD * (2.0 * i);
        const RdPaceScreenSample s = {at - 4.0, at + 123.456, REP, 0.0};
        rd_pace_screen_add(m, &s);
    }
}

static void testPlace(void)
{
    RdPaceScreen m;
    RdPaceScreenPlace o;
    /* the fall-backs: the value drawn otherwise to the bit, the reason */
    rd_pace_screen_reset(&m);
    RdPaceScreenIn in = {6000.0, 100, 5990.0, TICK, 6025.0, 0.3125};
    rd_pace_screen_place(&m, &in, &o);
    CHECK(o.how == RD_PS_NO_TIMES && o.raw == 0.3125);
    filled(&m, 5990.0);
    in.endPredMs = 0.0;
    rd_pace_screen_place(&m, &in, &o);
    CHECK(o.how == RD_PS_NO_END && o.raw == 0.3125);
    in.endPredMs = 6025.0;
    in.handMs = 5990.0 + RD_PS_STALE_MS + 1.0;
    rd_pace_screen_place(&m, &in, &o);
    CHECK(o.how == RD_PS_STALE && o.raw == 0.3125);
    rd_pace_screen_reset(&m);
    for (int i = 0; i < RD_PS_MIN - 1; i++) {
        const RdPaceScreenSample s = {5000.0 + 2.0 * PD * i, 5004.0 + 2.0 * PD * i, REP, 0.0};
        rd_pace_screen_add(&m, &s);
    }
    in.handMs = 6000.0;
    rd_pace_screen_place(&m, &in, &o);
    CHECK(o.how == RD_PS_FEW && o.raw == 0.3125);
    /* no refresh reported: off one grid */
    rd_pace_screen_reset(&m);
    for (int i = 0; i < RD_PS_WIN; i++) {
        const RdPaceScreenSample s = {5000.0 + 2.0 * PD * i, 5004.0 + 2.0 * PD * i, 0.0, 0.0};
        rd_pace_screen_add(&m, &s);
    }
    in.handMs = 5000.0 + 2.0 * PD * RD_PS_WIN;
    rd_pace_screen_place(&m, &in, &o);
    CHECK(o.how == RD_PS_OFF_GRID && o.raw == 0.3125);

    /* placed: the first at the value drawn otherwise to the bit; the next
       pictures follow the refreshes (two refreshes apart: 16.67 ms of
       content each, against hand-overs 14 ms apart), the lag moving at most
       RD_PS_SLEW_MS a picture, never back */
    filled(&m, 5990.0);
    double c0 = 0.0, lag0 = 0.0;
    int back = 0, slewOver = 0, holds = 0;
    bool heldPrev = false;
    double stepMin = 1e9, stepMax = -1e9;
    for (int i = 0; i < 40; i++) {
        /* the display's time of a picture shown before, on the same grid */
        const double at = 5994.0 + 2.0 * PD * (i + 1);
        const RdPaceScreenSample fresh = {at - 4.0, at + 123.456, REP, 0.0};
        rd_pace_screen_add(&m, &fresh);
        /* two pictures a tick of four refreshes, handed over 2 refreshes
           apart, 10 ms after the close and 14 ms later */
        const double tk = 4.0 * PD;
        const uint32_t frame = 100u + (uint32_t)i / 2u;
        const double close = 5990.0 + tk * (double)(frame - 100u);
        const double hand = close + (i % 2 == 0 ? 10.0 : 24.0);
        const RdPaceScreenIn pi = {
            hand, frame, close, tk, 5990.0 + 2.0 * PD * i + 1.0, (hand - close) / tk};
        rd_pace_screen_place(&m, &pi, &o);
        CHECK(o.how == RD_PS_PLACED);
        const double c = close + o.raw * tk;
        if (i == 0) {
            CHECK(o.raw == pi.rawOther);
        } else {
            back += c < c0 ? 1 : 0;
            slewOver += fabs(o.lagMs - lag0) > RD_PS_SLEW_MS + 1e-9 ? 1 : 0;
            /* the content's step is the refresh step (16.67) less the
               lag's step, unless held */
            const bool heldNow = o.heldLo || o.heldHi || o.heldBack;
            if (!heldNow && !heldPrev) {
                const double st = c - c0;
                stepMin = st < stepMin ? st : stepMin;
                stepMax = st > stepMax ? st : stepMax;
            }
        }
        c0 = c;
        lag0 = o.lagMs;
        heldPrev = o.heldLo || o.heldHi || o.heldBack;
        holds += heldPrev ? 1 : 0;
    }
    CHECK(back == 0 && slewOver == 0);
    printf(" placement: the content's steps %.4f to %.4f ms beside held ones (%d held), the "
           "refreshes 2 x %.4f ms apart\n",
           stepMin, stepMax, holds, PD);
    CHECK(holds == 0);
    CHECK(stepMin >= 2.0 * PD - RD_PS_SLEW_MS - 1e-9 && stepMax <= 2.0 * PD + RD_PS_SLEW_MS + 1e-9);
    /* a frame number back: a restart, the value drawn otherwise again */
    const RdPaceScreenIn rb = {6600.0, 90, 6590.0, 33.0, 6620.0, 0.25};
    rd_pace_screen_place(&m, &rb, &o);
    CHECK(o.restarted && o.how == RD_PS_PLACED && o.raw == 0.25);
    /* two pictures on one refresh: the second marked, the same moment (the
       first is replaced: no content time of its own) */
    const double hard0 = o.hardMs;
    const RdPaceScreenIn r1 = {6601.0, 90, 6590.0, 33.0, 6620.5, 0.4};
    rd_pace_screen_place(&m, &r1, &o);
    CHECK(o.how == RD_PS_PLACED && o.replaced && o.hardMs == hard0 && !o.heldLo);
}

/* ------------------------------------------------------------------- 3 */

static uint32_t s_seed;

static double U(double lo, double hi)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((double)(s_seed >> 8) / 16777216.0);
}

typedef struct Cfg {
    const char *name;
    double gLo, gHi, rLo, rHi, dLess, stepLo, stepHi, reads;
    double keepShare; /* the share of ticks whose pictures are kept frames */
    uint32_t seed;
    int noTimes;    /* no display times back */
    int staleTick;  /* the times stop after this tick (0 never) */
    int changeTick; /* the display at 90 Hz from this tick (0 never) */
} Cfg;

#define NR 4
static const char *const kRule[NR] = {"hand-over", "GPU end", "own timer", "screen"};
#define SIMN 4096
#define TICKS 300u
#define WARM 150u /* pictures before the measures (the windows fill) */
#define FB (3.0 * PD)
#define OFFSET 5123.456 /* the display's clock less rd's */

typedef struct Pic {
    double hand, sub, g, end, shown, t0, endPred, hardMs;
    double raw[NR], moment[NR];
    uint32_t frame;
    int how;
    bool visible, kept, exact, fed, held;
} Pic;

static Pic s_p[SIMN];

typedef struct Stat {
    double n, s, s2, above125, above15, below075;
} Stat;

static void statAdd(Stat *st, double r)
{
    st->n += 1.0;
    st->s += r;
    st->s2 += r * r;
    st->above125 += r > 1.25 ? 1.0 : 0.0;
    st->above15 += r > 1.5 ? 1.0 : 0.0;
    st->below075 += r < 0.75 ? 1.0 : 0.0;
}

static double statMean(const Stat *st)
{
    return st->n > 0 ? st->s / st->n : 0.0;
}

static double statDev(const Stat *st)
{
    const double m = statMean(st);
    const double v = st->n > 0 ? st->s2 / st->n - m * m : 0.0;
    return v > 0.0 ? sqrt(v) : 0.0;
}

static double share(double k, double n)
{
    return n > 0.0 ? 100.0 * k / n : 0.0;
}

typedef struct Out {
    uint32_t pics, shown, gaps[5];
    double fps;
    Stat glass[NR], byGap[NR][3]; /* byGap: 1, 2, 3 refreshes */
    uint32_t clampLo[NR], clampHi[NR], back[NR];
    uint32_t how[RD_PS_HOW_COUNT], heldLo, heldHi, heldBack, replaced;
    uint32_t scored, hit, late, early, further;
    double predRms; /* the GPU model's predicted end against the end */
    double rate;    /* the screen's content over the shown time, first to last */
    double rateTol; /* its bound from the code: the lag's range and a tick over the span */
    double lagMin, lagMax;
    bool sameOther; /* every picture not placed drawn as otherwise to the bit */
} Out;

/* the display's refresh at or after t (rd's clock) */
static double gridAt(double t, double phase, double changeAt)
{
    if (changeAt > 0.0 && t > changeAt) {
        const double base = phase + ceil((changeAt - phase) / PD) * PD;
        return base + ceil((t - base) / (1000.0 / 90.0)) * (1000.0 / 90.0);
    }
    return phase + ceil((t - phase) / PD) * PD;
}

static void run(const Cfg *c, Out *o)
{
    memset(o, 0, sizeof(*o));
    s_seed = c->seed;
    RdPaceGpu m;
    rd_pace_gpu_model_reset(&m);
    RdAsMadeTimer tm;
    rd_as_made_timer_reset(&tm);
    RdPaceScreen sc;
    rd_pace_screen_reset(&sc);
    RdRebase rb[2];
    memset(rb, 0, sizeof rb);
    const double phase = U(0.0, PD);
    const double changeAt = c->changeTick ? 1000.0 + c->changeTick * TICK : 0.0;
    uint32_t n = 0, fedAt = 0;
    double J = 0.0, Eprev = 0.0;
    const double P = TICK / 2.0;
    double err2 = 0.0, errN = 0.0;
    o->lagMin = 1e300;
    o->lagMax = -1e300;
    o->sameOther = true;

    typedef struct Pend {
        RdPaceGpuSample s;
        double avail;
    } Pend;

    static Pend pend[SIMN];
    uint32_t pendN = 0, pendAt = 0;
    for (uint32_t k = 0; k < TICKS && n < SIMN - 8; k++) {
        const double t0 = 1000.0 + (double)k * TICK;
        const double step = U(c->stepLo, c->stepHi);
        const bool kept = c->keepShare > 0.0 && U(0.0, 1.0) < c->keepShare;
        bool first = true;
        for (int j = 0; j < 2; j++) {
            const double ps = t0 + j * P, pe = ps + P;
            double x = J > ps + (j == 0 ? step : 0.2) ? J : ps + (j == 0 ? step : 0.2);
            for (;;) {
                if (x + 0.3 > pe) {
                    break;
                }
                if (!first && x + c->reads > t0 + TICK) {
                    j = 2;
                    break;
                }
                while (pendAt < pendN && pend[pendAt].avail <= x) {
                    rd_pace_gpu_model_note(&m, &pend[pendAt].s, NULL);
                    pendAt++;
                }
                /* the display's times back three refreshes after shown, for
                   pictures whose successor exists (replaced ones get none) */
                while (fedAt + 1u < n && s_p[fedAt].shown + FB <= x) {
                    const Pic *q = &s_p[fedAt];
                    const bool stale = c->staleTick && q->shown > 1000.0 + c->staleTick * TICK;
                    if (q->visible && !q->kept && q->endPred > 0.0 && !c->noTimes && !stale) {
                        const RdPaceScreenSample smp = {q->endPred, q->shown + OFFSET, REP,
                                                        q->exact ? q->end : 0.0};
                        rd_pace_screen_add(&sc, &smp);
                    }
                    fedAt++;
                }
                const double r = U(c->rLo, c->rHi);
                const double gpu = U(c->gLo, c->gHi);
                Pic *p = &s_p[n];
                memset(p, 0, sizeof(*p));
                p->hand = x;
                p->sub = x + r - c->dLess;
                p->g = gpu;
                p->frame = k;
                p->t0 = t0;
                p->kept = kept;
                p->end = (p->sub > Eprev ? p->sub : Eprev) + gpu;
                double jobEnd = x + r;
                if (n > 0) {
                    Pic *pp = &s_p[n - 1u];
                    const bool blocked = pp->end > jobEnd;
                    pp->exact = blocked && pp->end - jobEnd >= 0.05;
                    if (!pp->kept) {
                        pend[pendN].s = (RdPaceGpuSample){
                            pp->hand, pp->sub, pp->g,
                            blocked ? (pp->exact ? pp->end : jobEnd) : 0.0, pp->exact};
                        pend[pendN].avail = blocked ? pp->end : jobEnd;
                        pendN++;
                    }
                    if (blocked) {
                        jobEnd = pp->end;
                    }
                }
                /* the display: the first refresh whose latch (its lead and
                   a jitter) follows the end; mailbox replaces the one before
                   when both make the same refresh */
                p->shown = gridAt(p->end + 2.5 + U(-0.3, 0.3), phase, changeAt);
                p->visible = true;
                if (n > 0 && p->shown <= s_p[n - 1u].shown) {
                    s_p[n - 1u].visible = false;
                    p->shown = s_p[n - 1u].shown;
                }
                if (!kept) {
                    RdPaceGpuPlace pl;
                    rd_pace_gpu_model_place(&m, x, k, &pl);
                    double sh;
                    bool held;
                    const double rawHand =
                        rd_rebase_raw(&rb[0], (x - t0) / TICK, TICK, 0.0, k, &sh, &held);
                    const double rawGpu = rd_rebase_raw(&rb[1], (x + pl.shiftMs - t0) / TICK, TICK,
                                                        0.0, k, &sh, &held);
                    RdAsMadeTimerOut to;
                    rd_as_made_timer_step(&tm, t0 + rawHand * TICK, x, k, t0, TICK, &to);
                    const RdPaceScreenIn in = {x, k, t0, TICK, pl.predEndMs, rawHand};
                    RdPaceScreenPlace sp;
                    rd_pace_screen_place(&sc, &in, &sp);
                    p->endPred = pl.predEndMs;
                    p->how = sp.how;
                    p->hardMs = sp.how == RD_PS_PLACED ? sp.hardMs : 0.0;
                    p->raw[0] = rawHand;
                    p->raw[1] = rawGpu;
                    p->raw[2] = (to.momentMs - t0) / TICK;
                    p->raw[3] = sp.raw;
                    if (sp.how != RD_PS_PLACED && memcmp(&sp.raw, &rawHand, sizeof rawHand) != 0) {
                        o->sameOther = false;
                    }
                    if (n >= WARM) {
                        o->how[sp.how]++;
                        o->heldLo += sp.heldLo ? 1u : 0u;
                        o->heldHi += sp.heldHi ? 1u : 0u;
                        o->heldBack += sp.heldBack ? 1u : 0u;
                        o->replaced += sp.replaced ? 1u : 0u;
                        if (sp.how == RD_PS_PLACED) {
                            o->lagMin = sp.lagMs < o->lagMin ? sp.lagMs : o->lagMin;
                            o->lagMax = sp.lagMs > o->lagMax ? sp.lagMs : o->lagMax;
                        }
                        if (pl.predEndMs > 0.0) {
                            err2 += (pl.predEndMs - p->end) * (pl.predEndMs - p->end);
                            errN += 1.0;
                        }
                    }
                    for (int q = 0; q < NR; q++) {
                        if (n >= WARM) {
                            o->clampLo[q] += p->raw[q] < 0.0 ? 1u : 0u;
                            o->clampHi[q] += p->raw[q] > 0.999 ? 1u : 0u;
                        }
                        p->moment[q] = ((double)k + rd_rebase_alpha(p->raw[q])) * TICK;
                    }
                }
                Eprev = p->end;
                J = jobEnd;
                n++;
                first = false;
                x = J;
            }
        }
    }
    o->pics = n;
    o->fps = (double)n / ((double)TICKS * TICK / 1000.0);
    o->predRms = errN > 0.0 ? sqrt(err2 / errN) : 0.0;
    int lastShown = -1, lastMade = -1, firstRate = -1, lastRate = -1;
    for (uint32_t i = WARM; i < n; i++) {
        const Pic *b = &s_p[i];
        if (!b->kept) {
            if (lastMade >= 0 && s_p[lastMade].frame + RD_PS_FRAME_JUMP >= b->frame) {
                for (int q = 0; q < NR; q++) {
                    o->back[q] += b->moment[q] < s_p[lastMade].moment[q] ? 1u : 0u;
                }
            }
            lastMade = (int)i;
        }
        if (!b->visible) {
            continue;
        }
        o->shown++;
        if (lastShown >= 0 && !b->kept && !s_p[lastShown].kept) {
            const Pic *a = &s_p[lastShown];
            const double gap = b->shown - a->shown;
            const double kr = floor(gap / PD + 0.5);
            const int cls = kr >= 4.0 ? 4 : (int)kr;
            o->gaps[cls]++;
            for (int q = 0; q < NR; q++) {
                const double sp = (b->moment[q] - a->moment[q]) / gap;
                statAdd(&o->glass[q], sp);
                if (cls >= 1 && cls <= 3) {
                    statAdd(&o->byGap[q][cls - 1], sp);
                }
            }
        }
        if (!b->kept) {
            if (b->hardMs > 0.0) {
                const int s = rd_pace_screen_score(b->hardMs, b->shown + OFFSET, REP);
                o->scored++;
                o->hit += s == 0 ? 1u : 0u;
                o->late += s == 1 ? 1u : 0u;
                o->early += s == -1 ? 1u : 0u;
                o->further += s > 1 || s < -1 ? 1u : 0u;
            }
            firstRate = firstRate < 0 ? (int)i : firstRate;
            lastRate = (int)i;
        }
        lastShown = (int)i;
    }
    if (firstRate >= 0 && lastRate > firstRate) {
        const Pic *a = &s_p[firstRate], *b = &s_p[lastRate];
        const double span = b->shown - a->shown;
        o->rate = (b->moment[3] - a->moment[3]) / span;
        /* the content is g - L inside the tick: against the glass it differs
           by the lag's change, the expectation's distance from the refresh
           (under half a refresh) and the holds (a tick at most) at each end */
        o->rateTol = ((o->lagMax > o->lagMin ? o->lagMax - o->lagMin : 0.0) + PD + TICK) / span;
    }
}

static void print(const Cfg *c, const Out *o)
{
    printf("  %s: %u pictures, %.1f fps, %u shown; gaps 1/2/3/4+ refreshes %u/%u/%u/%u; the "
           "GPU model's predicted end against the end %.2f ms rms\n",
           c->name, o->pics, o->fps, o->shown, o->gaps[1], o->gaps[2], o->gaps[3], o->gaps[4],
           o->predRms);
    for (int q = 0; q < NR; q++) {
        printf("    %-9s on the glass %.2f (deviation %.3f), above 1.25 %.1f %%, above 1.5 %.1f "
               "%%, below 0.75 %.1f %%; 1 refresh %.2f, 2 %.2f, 3 %.2f; clamps %u/%u, back %u\n",
               kRule[q], statMean(&o->glass[q]), statDev(&o->glass[q]),
               share(o->glass[q].above125, o->glass[q].n),
               share(o->glass[q].above15, o->glass[q].n),
               share(o->glass[q].below075, o->glass[q].n), statMean(&o->byGap[q][0]),
               statMean(&o->byGap[q][1]), statMean(&o->byGap[q][2]), o->clampLo[q], o->clampHi[q],
               o->back[q]);
    }
    printf(
        "    the screen: placed %u (no times %u, no end %u, few %u, stale %u, off grid %u, no "
        "fit %u); held at the close %u, at the newest frame %u, at the moment before %u; on "
        "the picture before's refresh %u; score over %u: on it %u (%.1f %%), one late %u, "
        "one early %u, further %u; the lag %.2f to %.2f ms (less the clocks' offset); content over the glass %.4f "
        "(bound %.4f)\n",
        o->how[RD_PS_PLACED], o->how[RD_PS_NO_TIMES], o->how[RD_PS_NO_END], o->how[RD_PS_FEW],
        o->how[RD_PS_STALE], o->how[RD_PS_OFF_GRID], o->how[RD_PS_NO_FIT], o->heldLo, o->heldHi,
        o->heldBack, o->replaced, o->scored, o->hit, share((double)o->hit, (double)o->scored),
        o->late, o->early, o->further, o->how[RD_PS_PLACED] ? o->lagMin - OFFSET : 0.0,
        o->how[RD_PS_PLACED] ? o->lagMax - OFFSET : 0.0, o->rate, o->rateTol);
}

/* the rc4 regimes.  Play (L:2148-2498): 688-742 pictures in 10 s, 171-243
   held one refresh and 338-496 two; the GPU model's predicted end against
   the exact end 0.81-1.87 ms deviation.  The opening (L:903-1158): 443-534,
   two and three refreshes.  A scene with kept frames (L:1852, 1962: 607-613
   pairs, 245 hand-overs) */
static const Cfg kPlay = {"play, one and two refreshes (rc4 L:2148-2498)",
                          13.2,
                          15.4,
                          7.0,
                          9.0,
                          0.2,
                          2.4,
                          4.0,
                          6.0,
                          0.0,
                          31u,
                          0,
                          0,
                          0};
static const Cfg kPlayWide = {"play at rc4's prediction error (GPU 12.0-16.6 ms)",
                              12.0,
                              16.6,
                              7.0,
                              9.0,
                              0.2,
                              2.4,
                              4.0,
                              6.0,
                              0.0,
                              32u,
                              0,
                              0,
                              0};
static const Cfg kOpening = {"the opening, two and three refreshes (rc4 L:903-1158)",
                             20.4,
                             22.8,
                             9.0,
                             10.4,
                             0.2,
                             3.4,
                             4.0,
                             8.4,
                             0.0,
                             33u,
                             0,
                             0,
                             0};
static const Cfg kKept = {
    "kept frames (rc4 L:1852, 1962)", 13.2, 15.4, 7.0, 9.0, 0.2, 2.4, 4.0, 6.0, 0.6, 34u, 0, 0, 0};
static const Cfg kOne = {"one a tick", 30.0, 32.5, 9.0, 10.4, 0.2, 4.3,
                         5.8,          TICK, 0.0,  35u, 0,    0,   0};
static const Cfg kExact = {"play with an exact prediction (GPU 14.3 ms every picture)",
                           14.3,
                           14.3,
                           7.0,
                           9.0,
                           0.2,
                           2.4,
                           4.0,
                           6.0,
                           0.0,
                           36u,
                           0,
                           0,
                           0};

static void testSims(void)
{
    printf(" simulations (10 s, a 120 Hz mailbox display; speeds 1.00 real time):\n");
    const Cfg *const all[] = {&kPlay, &kPlayWide, &kOpening, &kKept, &kOne, &kExact};

    enum { N = sizeof all / sizeof all[0] };

    /* the holds at the tick's edges (at its close; both), the simulated
       counts rounded up: where the pictures of a frame pair are shown
       further apart than a tick (the hand-overs' spread in the tick and the
       latency's to the glass together) no lag keeps them all inside it */
    static const uint32_t kLoMax[N] = {2, 4, 6, 1, 1, 2};
    static const uint32_t kHeldMax[N] = {6, 10, 14, 4, 2, 40};
    Out res[N];
    for (size_t i = 0; i < N; i++) {
        run(all[i], &res[i]);
        print(all[i], &res[i]);
        const Out *r = &res[i];
        /* every regime: the screen's moments never back, its content over the
           glass at real time within the bound from the code, every picture
           not placed drawn as otherwise to the bit, the holds bounded */
        CHECK(r->back[3] == 0u);
        CHECK(fabs(r->rate - 1.0) <= r->rateTol);
        CHECK(r->sameOther);
        CHECK(r->how[RD_PS_PLACED] > 0u);
        CHECK(r->heldLo <= kLoMax[i] && r->heldLo + r->heldHi <= kHeldMax[i]);
    }
    const Out *play = &res[0], *wide = &res[1], *open = &res[2], *kept = &res[3], *one = &res[4],
              *exact = &res[5];
    /* play at the prediction's error of rc4's better blocks (1.0 ms rms):
       evener on the glass than the hand-over and the own timer (simulated
       .317 against .445 and .377), its share above 1.5 under half the own
       timer's and under the hand-over's (12.0 % against 26.2 % and 19.9 %),
       the refresh predicted 88.6 % of the time */
    CHECK(statDev(&play->glass[3]) < statDev(&play->glass[0]) &&
          statDev(&play->glass[3]) < statDev(&play->glass[2]) && statDev(&play->glass[3]) < 0.33);
    CHECK(play->glass[3].above15 < 0.5 * play->glass[2].above15 &&
          play->glass[3].above15 < play->glass[0].above15);
    CHECK(play->hit * 100u >= play->scored * 85u);
    /* at rc4's worst blocks' error (2.15 ms rms) no evener than the own
       timer (simulated .423 against .397): within 0.05 of it */
    CHECK(statDev(&wide->glass[3]) < statDev(&wide->glass[2]) + 0.05);
    /* the opening at two and three refreshes: evener than both (simulated
       .182 against .344 and .234) */
    CHECK(statDev(&open->glass[3]) < statDev(&open->glass[0]) &&
          statDev(&open->glass[3]) < statDev(&open->glass[2]) && statDev(&open->glass[3]) < 0.19);
    CHECK(open->hit * 100u >= open->scored * 85u);
    /* kept frames (the GPU model blind to their work): evener than the
       hand-over and the own timer (.393 against .440 and .453) */
    CHECK(statDev(&kept->glass[3]) < statDev(&kept->glass[0]) &&
          statDev(&kept->glass[3]) < statDev(&kept->glass[2]));
    /* one a tick (four refreshes a picture): as even as the others */
    CHECK(statDev(&one->glass[3]) <= statDev(&one->glass[2]) + 0.005);
    /* an exact prediction: under half the own timer's deviation (.190
       against .398), the misses the latch's own jitter's, the rest the
       holds at the tick's edges */
    CHECK(statDev(&exact->glass[3]) < 0.5 * statDev(&exact->glass[2]) &&
          exact->hit * 100u >= exact->scored * 95u);
}

/* ------------------------------------------------------------------- 4 */

static void testFallbacks(void)
{
    Out o;
    printf(" fall-backs:\n");
    const Cfg none = {
        "no display times", 13.2, 15.4, 7.0, 9.0, 0.2, 2.4, 4.0, 6.0, 0.0, 41u, 1, 0, 0};
    run(&none, &o);
    print(&none, &o);
    /* every picture the hand-over rule's to the bit, by the reason */
    CHECK(o.sameOther && o.how[RD_PS_PLACED] == 0u && o.how[RD_PS_NO_TIMES] > 0u);
    for (uint32_t i = WARM; i < o.pics; i++) {
        if (!s_p[i].kept) {
            CHECK(memcmp(&s_p[i].raw[3], &s_p[i].raw[0], sizeof(double)) == 0);
        }
    }
    const Cfg stale = {"times that stop at tick 150",
                       13.2,
                       15.4,
                       7.0,
                       9.0,
                       0.2,
                       2.4,
                       4.0,
                       6.0,
                       0.0,
                       42u,
                       0,
                       150,
                       0};
    run(&stale, &o);
    print(&stale, &o);
    CHECK(o.sameOther && o.how[RD_PS_PLACED] > 0u && o.how[RD_PS_STALE] > 0u);
    const Cfg change = {"the display at 90 Hz from tick 150",
                        13.2,
                        15.4,
                        7.0,
                        9.0,
                        0.2,
                        2.4,
                        4.0,
                        6.0,
                        0.0,
                        43u,
                        0,
                        0,
                        150};
    run(&change, &o);
    print(&change, &o);
    CHECK(o.sameOther && o.how[RD_PS_PLACED] > 0u && o.how[RD_PS_OFF_GRID] > 0u);
}

/* ------------------------------------------------------------------- 5 */

static void testTexts(void)
{
    char line[2048];
    (void)rd_pace_screen_start_text(true, false, "off by default", line, sizeof line);
    CHECK_STR(line,
              "rd: in-between pictures for the screen's refresh: off by default (off by default); "
              "each picture drawn for the time the screen will show it: its GPU end as predicted "
              "at the hand-over, plus the latch fitted over the last 64 shown pictures' display "
              "times, rounded up to the display's measured refresh grid (each refresh at its "
              "probability under the fit's own spread); the lag to the content starts at the "
              "moment drawn otherwise and moves at most 0.25 ms a picture to keep the last 64 "
              "inside their frame's tick; never back; with no display times, stale ones, ones "
              "off one grid or a latch that fits under 50 %, the moment drawn otherwise; "
              "computed and reported, not drawn (ICO_RD_PACE_SCREEN=1 draws it)");
    (void)rd_pace_screen_start_text(false, false, "x", line, sizeof line);
    CHECK_STR(line, "rd: in-between pictures for the screen's refresh: not applicable here (x)");
    char small[8];
    CHECK(rd_pace_screen_start_text(false, false, "x", small, sizeof small) == 7 &&
          strlen(small) == 7);

    RdPaceScreenWindow w;
    memset(&w, 0, sizeof w);
    CHECK(!rd_pace_screen_format(&w, 0, 0, 0, 0, 0, line, sizeof line) && line[0] == '\0');
    w.how[RD_PS_PLACED] = 690;
    w.how[RD_PS_FEW] = 12;
    w.restarts = 1;
    w.heldHi = 3;
    w.replaced = 2;
    w.grid = w.latch = w.lead = 1;
    w.periodNs = 8333333;
    w.reportedNs = 8350000;
    w.hits = 58;
    w.n = 64;
    w.leadHits = 30;
    w.leadN = 31;
    w.xUs = 3250;
    w.widthUs = 1180;
    w.leadUs = 2500;
    w.lagUs = 41500;
    CHECK(rd_pace_screen_format(&w, 680, 600, 41, 37, 2, line, sizeof line));
    CHECK_STR(line,
              "; the screen's predicted refresh: 690 pictures placed by it, 12 drawn as otherwise "
              "(too few shown pictures yet 12), 1 restarts; held at the frame's close 0, at the "
              "newest frame 3, at the moment before 0; 2 on the refresh of the picture before "
              "(replaced before shown); the refresh predicted against the one shown over 680 "
              "pictures: 600 on it (88.2 %), 41 one late, 37 one early, 2 further; the display's "
              "grid: a refresh of 8.3333 ms measured (8.3500 reported), 0 of 64 times off it; the "
              "latch: shown at the first refresh at or after the predicted GPU end + 3.25 ms (58 "
              "of 64 agree, the prediction's spread 1.18 ms), after the exact GPU end + 2.50 ms "
              "(30 of 31); the lag 41.50 ms");
}

/* the renderer's side: the start line, the row (a slot clock from the GPU
   model's samples, the display's times from the ring) */
static void testLive(void)
{
    if (getenv("ICO_RD_PACE_SCREEN") != NULL) {
        printf(" the live check skipped: the tests' switch is set\n");
        return;
    }
    ico_attempt_reset();
    rd_pace_screen_paced();
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_NA);
    /* the GPU model's clock: one measurement with a span and a first submit */
    const RdPaceGpuSample gs = {1000.0, 1009.5, 15.0, 1027.0, true};
    rd_pace_gpu_measure(&gs);
    double would;
    (void)rd_pace_gpu_shift(1010.0, 1, &would);
    CHECK(rd_pace_gpu_clock_state() == 1);
    /* a shown picture's time back: the row off by default; the value drawn
       the one otherwise (too few shown pictures) */
    const RdPaceScreenSample s = {1027.0, 6000.0, REP, 1027.0};
    rd_pace_screen_seen(&s);
    RdPaceScreenPlace o;
    const RdPaceScreenIn in = {1030.0, 1, 1010.0, TICK, 1060.0, 0.4};
    rd_pace_screen_step(&in, &o);
    CHECK(o.how == RD_PS_FEW && o.raw == 0.4);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_OFF_DEFAULT);
    CHECK(rd_pace_screen_on() == (RD_PACE_SCREEN_DEFAULT != 0));
    RdPaceScreenWindow w;
    rd_pace_screen_take(&w);
    CHECK(w.how[RD_PS_FEW] == 1u);
    rd_pace_screen_paced();
    rd_pace_gpu_paced();
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_NA);
    ico_attempt_reset();
}

int main(void)
{
    printf("rd_pace_screen_test\n");
    testParts();
    testPlace();
    testSims();
    testFallbacks();
    testTexts();
    testLive();
    if (failures) {
        printf("rd_pace_screen_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pace_screen_test: ok\n");
    return 0;
}
