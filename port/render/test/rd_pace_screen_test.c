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
#include "rd_pace_even.h"
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
    RdPaceScreenIn in = {6000.0, 100, 5990.0, TICK, 6025.0, 0.3125, false, 0.0};
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
            hand, frame, close, tk, 5990.0 + 2.0 * PD * i + 1.0, (hand - close) / tk, false, 0.0};
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
    const RdPaceScreenIn rb = {6600.0, 90, 6590.0, 33.0, 6620.0, 0.25, false, 0.0};
    rd_pace_screen_place(&m, &rb, &o);
    CHECK(o.restarted && o.how == RD_PS_PLACED && o.raw == 0.25);
    /* two pictures on one refresh: the second marked, the same moment (the
       first is replaced: no content time of its own) */
    const double hard0 = o.hardMs;
    const RdPaceScreenIn r1 = {6601.0, 90, 6590.0, 33.0, 6620.5, 0.4, false, 0.0};
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
    /* v0.11.6-rc5's regimes (PACE4): */
    double pd; /* the display's true refresh (0: PD; the phone's fit 8.3526-8.3551) */
    double tailShare, tailLo,
        tailHi;  /* a share of pictures whose GPU time is longer by U(lo, hi) */
    int keepRun; /* kept ticks come in runs of this many (a fade, a load; 0: one at a time) */
    double keepG, keepFirstG; /* a kept picture's GPU time, its tick's first's (0: as a scene's) */
    int leadTick;             /* the latch's lead from this tick (0 never) ... */
    double leadTo;            /* ... this (2.5 before) */
    int gTick;                /* the GPU's time from this tick (0 never) ... */
    double gLo2, gHi2;        /* ... in this range */
    double gSwing, gSwingS;   /* the GPU's time swings by +-gSwing over a period of gSwingS s */
    double step2Lo, step2Hi;  /* the other step's CPU (0: 0.2 ms; the phone's 3.2 ms a step) */
    /* the GPU slower after it idled (its clocks down): its time longer by
       this share per ms of idle before it on average (from none to twice
   that, picture to picture), at most 10 ms of it */
    double dvfs;
    /* a picture held for its desired time is replaced by the next one
       queued before it is shown (mailbox's rule, if Android's compositor
       applies it to held pictures) */
    int dropHeld;
} Cfg;

/* how a run is made: the screen model's opts, the kept frames as rc5 did
   (sampled, placed and scored; their present stepped through the GPU model
   without a measurement) or as now (RD_PS_KEPT, not in the GPU model), the
   even cadence */
typedef struct Mode {
    uint32_t opts;
    bool keepFix;
    bool even;
} Mode;

static const Mode kRc5 = {0u, false, false};
static const Mode kNow = {RD_PS_OPT_ALL, true, false};
static const Mode kEven = {RD_PS_OPT_ALL, true, true};

#define NR 4
static const char *const kRule[NR] = {"hand-over", "GPU end", "own timer", "screen"};
#define SIMN 4096
#define TICKS 300u
#define WARM 150u       /* pictures before the measures (the windows fill) */
#define OFFSET 5123.456 /* the display's clock less rd's */

typedef struct Pic {
    double hand, sub, g, end, shown, t0, endPred, hardMs, queued;
    double raw[NR], moment[NR];
    uint32_t frame;
    int how;
    bool visible, kept, exact, fed, held, aimed, late;
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
    uint32_t how[RD_PS_HOW_COUNT], heldLo, heldHi, heldBack, replaced, measured;
    uint32_t scored, hit, late, early, further;
    double predRms; /* the GPU model's predicted end against the end */
    double rate;    /* the screen's content over the shown time, first to last */
    double rateTol; /* its bound from the code: the lag's range and a tick over the span */
    double lagMin, lagMax;
    bool sameOther; /* every picture not placed drawn as otherwise to the bit */
    /* the even cadence */
    uint32_t aimed, lateAims, earlyHand, changes, notAimed, nMin, nMax, nLast, shownOff,
        nAt[RD_PE_N_MAX + 1];
    double fpsShown;       /* shown pictures a second (after WARM) */
    uint32_t shownEarlier; /* aimed pictures shown before their refresh */
    uint32_t lost;         /* aimed pictures never shown */
    double offAtMs;        /* when the self-check turned the cadence off (0 never) */
    uint32_t heldUnfit,
        maxRounds;     /* holds without a usable fit; the most holds before one hand-over */
    double firstAimMs; /* the run's time of its first aimed picture (0 none) */
} Out;

/* the display's refresh at or after t (rd's clock) */
static double gridAt(double t, double phase, double pd, double changeAt)
{
    if (changeAt > 0.0 && t > changeAt) {
        const double base = phase + ceil((changeAt - phase) / pd) * pd;
        return base + ceil((t - base) / (1000.0 / 90.0)) * (1000.0 / 90.0);
    }
    return phase + ceil((t - phase) / pd) * pd;
}

/* the even cadence's inputs from the two models at rd's time now */
static void evenIn(RdPaceScreen *sc, const RdPaceGpu *m, double now, RdPaceEvenIn *in)
{
    memset(in, 0, sizeof(*in));
    in->fit = rd_pace_screen_even_latch(sc, now, &in->xMs);
    const RdPaceScreenFit *f = &sc->fit;
    in->nowMs = now;
    in->tickMs = TICK;
    in->anchorMs = f->anchorMs;
    in->periodMs = f->periodMs;
    (void)rd_pace_gpu_model_figures(m, &in->gMs, &in->dMs, &in->prevEndMs);
}

static void run(const Cfg *c, const Mode *md, Out *o)
{
    memset(o, 0, sizeof(*o));
    s_seed = c->seed;
    RdPaceGpu m;
    rd_pace_gpu_model_reset(&m);
    RdAsMadeTimer tm;
    rd_as_made_timer_reset(&tm);
    RdPaceScreen sc;
    rd_pace_screen_reset(&sc);
    sc.opts = md->opts;
    RdPaceEven ev;
    rd_pace_even_reset(&ev);
    RdRebase rb[2];
    memset(rb, 0, sizeof rb);
    const double pd = c->pd > 0.0 ? c->pd : PD;
    const double fb = 3.0 * pd;
    const double phase = U(0.0, pd);
    const double changeAt = c->changeTick ? 1000.0 + c->changeTick * TICK : 0.0;
    uint32_t n = 0, fedAt = 0;
    double J = 0.0, Eprev = 0.0;
    const double P = TICK / 2.0;
    double err2 = 0.0, errN = 0.0;
    o->lagMin = 1e300;
    o->lagMax = -1e300;
    o->sameOther = true;
    o->nMin = 99u;

    typedef struct Pend {
        RdPaceGpuSample s;
        double avail;
    } Pend;

    static Pend pend[SIMN];
    uint32_t pendN = 0, pendAt = 0, lastN = 0, rounds = 0;
    bool keptRun = false;
    int runLeft = 0;
    for (uint32_t k = 0; k < TICKS && n < SIMN - 8; k++) {
        const double t0 = 1000.0 + (double)k * TICK;
        const double step = U(c->stepLo, c->stepHi);
        const double step2 = c->step2Hi > 0.0 ? U(c->step2Lo, c->step2Hi) : 0.2;
        bool kept;
        if (c->keepRun > 0) {
            /* runs of kept ticks at the share asked */
            if (runLeft <= 0) {
                keptRun = c->keepShare > 0.0 && U(0.0, 1.0) < c->keepShare;
                runLeft = c->keepRun;
            }
            runLeft--;
            kept = keptRun;
        } else {
            kept = c->keepShare > 0.0 && U(0.0, 1.0) < c->keepShare;
        }
        const double lead = c->leadTick && (int)k >= c->leadTick ? c->leadTo : 2.5;
        bool first = true;
        for (int j = 0; j < 2; j++) {
            const double ps = t0 + j * P, pe = ps + P;
            const double st = j == 0 ? step : step2;
            double x = J > ps + st ? J : ps + st;
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
                while (fedAt + 1u < n && s_p[fedAt].shown + fb <= x) {
                    const Pic *q = &s_p[fedAt];
                    const bool stale = c->staleTick && q->shown > 1000.0 + c->staleTick * TICK;
                    /* rc5 sampled the kept frames' presents too */
                    const bool sampled = !q->kept || !md->keepFix;
                    if (q->visible && sampled && q->endPred > 0.0 && !c->noTimes && !stale) {
                        const RdPaceScreenSample smp = {q->endPred, q->shown + OFFSET, REP,
                                                        q->exact ? q->end : 0.0};
                        rd_pace_screen_add(&sc, &smp);
                        if (md->even && q->aimed) {
                            (void)rd_pace_even_lost(&ev, false);
                            rd_pace_even_shown(
                                &ev, rd_pace_screen_score(q->hardMs, q->shown + OFFSET, REP));
                        }
                    }
                    fedAt++;
                }
                /* the even cadence: the hand-over held to the plan's (the
                   window's pacer), not past this step */
                if (md->even && !c->noTimes) {
                    RdPaceEvenIn ei;
                    evenIn(&sc, &m, x, &ei);
                    /* the pacer's deadline, and for a further picture the
                       time from which the step rule would hold it (the pacer
                       asks the rule at the hand-over the plan wants:
                       window_present.c even_block_at) */
                    ei.stepEndMs = pe;
                    ei.ruleEndMs = !first ? t0 + TICK - c->reads : 0.0;
                    RdPaceEvenPlan pl;
                    rd_pace_even_plan(&ev, &ei, &pl);
                    o->earlyHand += pl.early && pl.holdMs > 0.01 && n >= WARM ? 1u : 0u;
                    if (pl.holdMs > 0.01 && (pl.how != RD_PE_AIMED || !ei.fit)) {
                        o->heldUnfit++;
                    }
                    if (pl.how == RD_PE_AIMED && pl.holdMs > 0.01) {
                        if (x + pl.holdMs + 0.3 > pe + 1e-9) {
                            break; /* asked again in the next step */
                        }
                        /* the pacer's sleep overshoots (SDL_DelayPrecise on
                           the phone: by a fraction of a ms) */
                        x += pl.holdMs + U(0.05, 0.8);
                        rounds++;
                        o->maxRounds = rounds > o->maxRounds ? rounds : o->maxRounds;
                        continue; /* the times back and the step rule again at the new time */
                    }
                }
                const double r = U(c->rLo, c->rHi);
                double gpu =
                    c->gTick && (int)k >= c->gTick ? U(c->gLo2, c->gHi2) : U(c->gLo, c->gHi);
                if (c->gSwing > 0.0) {
                    gpu += c->gSwing * sin(2.0 * acos(-1.0) * (x - 1000.0) / (1000.0 * c->gSwingS));
                }
                if (c->tailShare > 0.0 && U(0.0, 1.0) < c->tailShare) {
                    gpu += U(c->tailLo, c->tailHi);
                }
                if (kept && c->keepG > 0.0) {
                    gpu = first ? c->keepFirstG : c->keepG;
                }
                Pic *p = &s_p[n];
                memset(p, 0, sizeof(*p));
                p->hand = x;
                p->sub = x + r - c->dLess;
                p->g = gpu;
                p->frame = k;
                p->t0 = t0;
                p->kept = kept;
                if (c->dvfs > 0.0 && p->sub > Eprev && n > 0) {
                    const double idle = p->sub - Eprev;
                    /* the governor's clock picture to picture: from as
                       made to twice the mean slowdown */
                    gpu *= 1.0 + U(0.0, 2.0 * c->dvfs) * (idle < 10.0 ? idle : 10.0);
                }
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
                p->shown = gridAt(p->end + lead + U(-0.3, 0.3), phase, pd, changeAt);
                p->visible = true;
                p->queued = x + r; /* its present queued (the submit thread's) */
                /* the GPU model: rc5 stepped a kept frame's present through
                   it (never measured); now it is left out */
                RdPaceGpuPlace pl;
                memset(&pl, 0, sizeof pl);
                if (!kept || !md->keepFix) {
                    rd_pace_gpu_model_place(&m, x, k, &pl);
                }
                double target = 0.0;
                if (md->even && !c->noTimes) {
                    RdPaceEvenIn ei;
                    evenIn(&sc, &m, x, &ei);
                    bool late = false;
                    target = rd_pace_even_take(&ev, &ei, pl.predEndMs, kept, &late);
                    if (n >= WARM && (target > 0.0 || kept)) {
                        o->lateAims += late ? 1u : 0u;
                    }
                    p->aimed = target > 0.0;
                    p->late = late;
                    rounds = 0;
                    if (p->aimed && o->firstAimMs == 0.0) {
                        o->firstAimMs = x - 1000.0;
                    }
                    if (n >= WARM) {
                        o->aimed += p->aimed ? 1u : 0u;
                        o->notAimed += !p->aimed && !kept ? 1u : 0u;
                        if (ev.n != 0u) {
                            o->nMin = ev.n < o->nMin ? ev.n : o->nMin;
                            o->nMax = ev.n > o->nMax ? ev.n : o->nMax;
                            o->nAt[ev.n <= RD_PE_N_MAX ? ev.n : 0]++;
                            o->changes += lastN != 0u && ev.n != lastN ? 1u : 0u;
                        }
                    }
                    lastN = ev.n != 0u ? ev.n : lastN;
                    o->nLast = ev.n;
                    if (p->aimed && ev.desire) {
                        /* the desired present time: not shown before the
                           refresh it was aimed at (a time half a refresh
                           before it, rd_pace_even.h) */
                        const double at = gridAt(target - OFFSET - 0.5 * pd, phase, pd, changeAt);
                        p->held = at > p->shown;
                        p->shown = at > p->shown ? at : p->shown;
                    }
                }
                /* mailbox: the picture before replaced where this one makes
                   the same refresh; with dropHeld also one still held for its
                   desired time when this one is queued */
                if (n > 0) {
                    Pic *q = &s_p[n - 1u];
                    if (p->shown <= q->shown) {
                        q->visible = false;
                        p->shown = q->shown;
                    } else if (c->dropHeld && q->held && p->queued < q->shown - lead) {
                        q->visible = false;
                    }
                    /* the self-check (rd_pace_even_live.c: an aim passed by a
                       later one never seen) */
                    if (md->even && q->aimed && !q->visible) {
                        o->lost++;
                        if (rd_pace_even_lost(&ev, true) && o->offAtMs == 0.0) {
                            o->offAtMs = x - 1000.0;
                        }
                    }
                }
                if (!kept || !md->keepFix) {
                    double sh;
                    bool held;
                    const double rawHand =
                        rd_rebase_raw(&rb[0], (x - t0) / TICK, TICK, 0.0, k, &sh, &held);
                    const double rawGpu = rd_rebase_raw(&rb[1], (x + pl.shiftMs - t0) / TICK, TICK,
                                                        0.0, k, &sh, &held);
                    RdAsMadeTimerOut to;
                    rd_as_made_timer_step(&tm, t0 + rawHand * TICK, x, k, t0, TICK, &to);
                    const RdPaceScreenIn in = {x,       k,     t0,    TICK, pl.predEndMs,
                                               rawHand, false, target};
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
                    if (n >= WARM && !kept) {
                        o->how[sp.how]++;
                        o->heldLo += sp.heldLo ? 1u : 0u;
                        o->heldHi += sp.heldHi ? 1u : 0u;
                        o->heldBack += sp.heldBack ? 1u : 0u;
                        o->replaced += sp.replaced ? 1u : 0u;
                        o->measured += sp.measured ? 1u : 0u;
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
                        if (n >= WARM && !kept) {
                            o->clampLo[q] += p->raw[q] < 0.0 ? 1u : 0u;
                            o->clampHi[q] += p->raw[q] > 0.999 ? 1u : 0u;
                        }
                        p->moment[q] = ((double)k + rd_rebase_alpha(p->raw[q])) * TICK;
                    }
                } else {
                    /* now: the screen's rule says a kept frame (the value
                       drawn otherwise, not sampled, not scored) */
                    const RdPaceScreenIn in = {x, k, t0, TICK, 0.0, (x - t0) / TICK, true, 0.0};
                    RdPaceScreenPlace sp;
                    rd_pace_screen_place(&sc, &in, &sp);
                    if (sp.how != RD_PS_KEPT || sp.raw != in.rawOther) {
                        o->sameOther = false;
                    }
                    if (n >= WARM) {
                        o->how[sp.how]++;
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
    double firstShownAt = 0.0, lastShownAt = 0.0;
    uint32_t shownAfter = 0;
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
        firstShownAt = shownAfter == 0u ? b->shown : firstShownAt;
        lastShownAt = b->shown;
        shownAfter++;
        if (lastShown >= 0 && !b->kept && !s_p[lastShown].kept) {
            const Pic *a = &s_p[lastShown];
            const double gap = b->shown - a->shown;
            const double kr = floor(gap / pd + 0.5);
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
                o->shownOff += b->aimed && s != 0 ? 1u : 0u;
                o->shownEarlier += b->aimed && s < 0 ? 1u : 0u;
            }
            firstRate = firstRate < 0 ? (int)i : firstRate;
            lastRate = (int)i;
        }
        lastShown = (int)i;
    }
    /* the kept frames' presents as rc5 scored them (the score over every
       placed picture) */
    if (!md->keepFix) {
        for (uint32_t i = WARM; i < n; i++) {
            const Pic *b = &s_p[i];
            if (b->kept && b->visible && b->hardMs > 0.0) {
                const int s = rd_pace_screen_score(b->hardMs, b->shown + OFFSET, REP);
                o->scored++;
                o->hit += s == 0 ? 1u : 0u;
                o->late += s == 1 ? 1u : 0u;
                o->early += s == -1 ? 1u : 0u;
                o->further += s > 1 || s < -1 ? 1u : 0u;
            }
        }
    }
    o->fpsShown = lastShownAt > firstShownAt && shownAfter > 1u
                      ? 1000.0 * (double)(shownAfter - 1u) / (lastShownAt - firstShownAt)
                      : 0.0;
    if (firstRate >= 0 && lastRate > firstRate) {
        const Pic *a = &s_p[firstRate], *b = &s_p[lastRate];
        const double span = b->shown - a->shown;
        o->rate = (b->moment[3] - a->moment[3]) / span;
        /* the content is g - L inside the tick: against the glass it differs
           by the lag's change, the expectation's distance from the refresh
           (under half a refresh) and the holds (a tick at most) at each end */
        o->rateTol = ((o->lagMax > o->lagMin ? o->lagMax - o->lagMin : 0.0) + pd + TICK) / span;
    }
}

static void print(const Cfg *c, const Out *o)
{
    printf("  %s: %u pictures, %.1f fps (%.1f shown a second), %u shown; gaps 1/2/3/4+ "
           "refreshes %u/%u/%u/%u; the GPU model's predicted end against the end %.2f ms rms\n",
           c->name, o->pics, o->fps, o->fpsShown, o->shown, o->gaps[1], o->gaps[2], o->gaps[3],
           o->gaps[4], o->predRms);
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
    printf("    the screen: placed %u (no times %u, no end %u, few %u, stale %u, off grid %u, no "
           "fit %u, kept %u); held at the close %u, at the newest frame %u, at the moment before "
           "%u; on the picture before's refresh %u; by the measured errors %u; score "
           "over %u: on it %u (%.1f %%), one late %u, one early %u, further %u; the lag %.2f to "
           "%.2f ms (less the clocks' offset); content over the glass %.4f (bound %.4f)\n",
           o->how[RD_PS_PLACED], o->how[RD_PS_NO_TIMES], o->how[RD_PS_NO_END], o->how[RD_PS_FEW],
           o->how[RD_PS_STALE], o->how[RD_PS_OFF_GRID], o->how[RD_PS_NO_FIT], o->how[RD_PS_KEPT],
           o->heldLo, o->heldHi, o->heldBack, o->replaced, o->measured, o->scored, o->hit,
           share((double)o->hit, (double)o->scored), o->late, o->early, o->further,
           o->how[RD_PS_PLACED] ? o->lagMin - OFFSET : 0.0,
           o->how[RD_PS_PLACED] ? o->lagMax - OFFSET : 0.0, o->rate, o->rateTol);
    if (o->aimed || o->notAimed) {
        printf(
            "    the even cadence: N %u-%u (the last %u; pictures at N 1/2/3/4: %u/%u/%u/%u), %u "
            "changes; %u aimed, %u not, %u late at the hand-over, %u holds cut before the "
            "step, %u shown off their refresh (%u earlier)\n",
            o->nMin, o->nMax, o->nLast, o->nAt[1], o->nAt[2], o->nAt[3], o->nAt[4], o->changes,
            o->aimed, o->notAimed, o->lateAims, o->earlyHand, o->shownOff, o->shownEarlier);
    }
}

/* one table row: the screen's measure, the shares, the score */
static void row(const char *what, const Out *o)
{
    printf(
        "    | %-34s | %.3f | %4.1f %% | %4.1f %% | %5.1f %% (%u: %u late, %u early, %u further) | "
        "%.1f |\n",
        what, statDev(&o->glass[3]), share(o->glass[3].above15, o->glass[3].n),
        share(o->glass[3].below075, o->glass[3].n), share((double)o->hit, (double)o->scored),
        o->scored, o->late, o->early, o->further, o->fpsShown);
}

/* the rc4 regimes.  Play (L:2148-2498): 688-742 pictures in 10 s, 171-243
   held one refresh and 338-496 two; the GPU model's predicted end against
   the exact end 0.81-1.87 ms deviation.  The opening (L:903-1158): 443-534,
   two and three refreshes.  A scene with kept frames (L:1852, 1962: 607-613
   pairs, 245 hand-overs) */
static const Cfg kPlay = {.name = "play, one and two refreshes (rc4 L:2148-2498)",
                          .gLo = 13.2,
                          .gHi = 15.4,
                          .rLo = 7.0,
                          .rHi = 9.0,
                          .dLess = 0.2,
                          .stepLo = 2.4,
                          .stepHi = 4.0,
                          .reads = 6.0,
                          .seed = 31u};
static const Cfg kPlayWide = {.name = "play at rc4's prediction error (GPU 12.0-16.6 ms)",
                              .gLo = 12.0,
                              .gHi = 16.6,
                              .rLo = 7.0,
                              .rHi = 9.0,
                              .dLess = 0.2,
                              .stepLo = 2.4,
                              .stepHi = 4.0,
                              .reads = 6.0,
                              .seed = 32u};
static const Cfg kOpening = {.name = "the opening, two and three refreshes (rc4 L:903-1158)",
                             .gLo = 20.4,
                             .gHi = 22.8,
                             .rLo = 9.0,
                             .rHi = 10.4,
                             .dLess = 0.2,
                             .stepLo = 3.4,
                             .stepHi = 4.0,
                             .reads = 8.4,
                             .seed = 33u};
static const Cfg kKept = {.name = "kept frames (rc4 L:1852, 1962)",
                          .gLo = 13.2,
                          .gHi = 15.4,
                          .rLo = 7.0,
                          .rHi = 9.0,
                          .dLess = 0.2,
                          .stepLo = 2.4,
                          .stepHi = 4.0,
                          .reads = 6.0,
                          .keepShare = 0.6,
                          .seed = 34u};
static const Cfg kOne = {.name = "one a tick",
                         .gLo = 30.0,
                         .gHi = 32.5,
                         .rLo = 9.0,
                         .rHi = 10.4,
                         .dLess = 0.2,
                         .stepLo = 4.3,
                         .stepHi = 5.8,
                         .reads = TICK,
                         .seed = 35u};
static const Cfg kExact = {.name = "play with an exact prediction (GPU 14.3 ms every picture)",
                           .gLo = 14.3,
                           .gHi = 14.3,
                           .rLo = 7.0,
                           .rHi = 9.0,
                           .dLess = 0.2,
                           .stepLo = 2.4,
                           .stepHi = 4.0,
                           .reads = 6.0,
                           .seed = 36u};

static void testSims(void)
{
    printf(" simulations (10 s, a 120 Hz mailbox display; speeds 1.00 real time):\n");
    const Cfg *const all[] = {&kPlay, &kPlayWide, &kOpening, &kKept, &kOne, &kExact};

    enum { N = sizeof all / sizeof all[0] };

    /* the holds at the tick's edges (at its close; both), the simulated
       counts rounded up (v0.11.6 PACE4: with the measured errors 3/6, 3/6,
       3/6, 1/9, 0/0, 0/29): where the pictures of a frame pair are shown
       further apart than a tick (the hand-overs' spread in the tick and the
       latency's to the glass together) no lag keeps them all inside it */
    static const uint32_t kLoMax[N] = {3, 4, 6, 1, 1, 2};
    static const uint32_t kHeldMax[N] = {6, 10, 14, 10, 2, 40};
    Out res[N];
    for (size_t i = 0; i < N; i++) {
        run(all[i], &kNow, &res[i]);
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

/* v0.11.6-rc5's regimes (the owner's phone, evidence/rc5/ico-pc.log; the
   rule not drawn, its score and fit on the on-glass line):
     play, stage 3 (L:2306-2486): 69-72 fps, gaps 1/2 190-239/459-501, GPU
       13.8-14.3 ms a picture, the prediction's error 0.9-2.2 ms, 87-92 % on
       the refresh predicted, the period 8.3526-8.3533 ms;
     the cage, stage 4 (L:2862-3215): 52-61 fps, GPU 14.9-17.9 ms, the
       prediction's error 1.9-3.1 ms, 74-82 % on it, one late against one
       early 1.3-2.0 to 1, the period 8.3530-8.3551;
     kept frames in runs (op2 L:1945 203 of 624, deja L:2109 784 of 1004,
       the sacrifice scene L:2216 418 of 858): 49-72 % on it with 43-105
       further, the latch at -32.29 and -21.61 ms (37 and 41 of 64) against
       the exact end's +8.52 and +4.60;
     one picture about every refresh (deja's 503 of 623 gaps one refresh,
       the GPU 6.5 ms a picture with its kept frames). */
static const Cfg kR5Play = {.name = "rc5 play, stage 3 (L:2306-2486)",
                            .gLo = 13.0,
                            .gHi = 15.0,
                            .rLo = 7.0,
                            .rHi = 9.0,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 6.0,
                            .seed = 51u,
                            .pd = 8.3532,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4};
static const Cfg kR5Cage = {.name = "rc5 the cage, stage 4 (L:2948-3215)",
                            .gLo = 14.6,
                            .gHi = 15.8,
                            .rLo = 7.5,
                            .rHi = 9.5,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 6.0,
                            .seed = 52u,
                            .pd = 8.3540,
                            .tailShare = 0.10,
                            .tailLo = 1.5,
                            .tailHi = 5.0,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4};
static const Cfg kR5Deja = {.name = "rc5 kept frames in runs, deja (L:2109)",
                            .gLo = 12.0,
                            .gHi = 14.0,
                            .rLo = 3.0,
                            .rHi = 5.0,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 4.0,
                            .keepShare = 0.63,
                            .seed = 53u,
                            .pd = 8.3504,
                            .keepRun = 10,
                            .keepG = 1.6,
                            .keepFirstG = 5.0,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4};
static const Cfg kR5Sac = {.name = "rc5 kept frames in runs, the sacrifice scene (L:2216)",
                           .gLo = 13.0,
                           .gHi = 15.5,
                           .rLo = 5.0,
                           .rHi = 7.0,
                           .dLess = 0.2,
                           .stepLo = 2.4,
                           .stepHi = 4.0,
                           .reads = 6.0,
                           .keepShare = 0.5,
                           .seed = 54u,
                           .pd = 8.3510,
                           .keepRun = 15,
                           .keepG = 1.6,
                           .keepFirstG = 5.0,
                           .step2Lo = 2.6,
                           .step2Hi = 3.4};
static const Cfg kR5Op2 = {.name = "rc5 kept frames in runs, op2 (L:1945)",
                           .gLo = 13.5,
                           .gHi = 15.5,
                           .rLo = 6.0,
                           .rHi = 8.0,
                           .dLess = 0.2,
                           .stepLo = 2.4,
                           .stepHi = 4.0,
                           .reads = 6.0,
                           .keepShare = 0.33,
                           .seed = 55u,
                           .pd = 8.3523,
                           .keepRun = 10,
                           .keepG = 1.6,
                           .keepFirstG = 5.0,
                           .step2Lo = 2.6,
                           .step2Hi = 3.4};
static const Cfg kR5Fast = {.name = "about one a refresh (GPU 6.5-8.0 ms)",
                            .gLo = 6.5,
                            .gHi = 8.0,
                            .rLo = 3.0,
                            .rHi = 5.0,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 4.0,
                            .seed = 56u,
                            .pd = 8.3504,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4};
static const Cfg kR5Lead = {.name = "the latch's lead 2.5 to 6.5 ms at tick 150",
                            .gLo = 13.0,
                            .gHi = 15.0,
                            .rLo = 7.0,
                            .rHi = 9.0,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 6.0,
                            .seed = 57u,
                            .pd = 8.3532,
                            .leadTick = 150,
                            .leadTo = 6.5,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4};
static const Cfg kR5Regime = {.name =
                                  "the opening's GPU (20.4-22.8) to play's (13.2-15.4) at tick 150",
                              .gLo = 20.4,
                              .gHi = 22.8,
                              .rLo = 7.0,
                              .rHi = 9.0,
                              .dLess = 0.2,
                              .stepLo = 2.4,
                              .stepHi = 4.0,
                              .reads = 6.0,
                              .seed = 58u,
                              .pd = 8.3532,
                              .gTick = 150,
                              .gLo2 = 13.2,
                              .gHi2 = 15.4,
                              .step2Lo = 2.6,
                              .step2Hi = 3.4};

#define R5N 8
static const Cfg *const kR5[R5N] = {&kR5Play, &kR5Cage, &kR5Deja, &kR5Sac,
                                    &kR5Op2,  &kR5Fast, &kR5Lead, &kR5Regime};

static void testRc5(void)
{
    printf(" rc5's regimes, each change on in turn (the screen's measure on the glass: "
           "deviation, above 1.5, below 0.75; on the refresh predicted; shown a second):\n");

    static const struct {
        const char *what;
        Mode md;
    } kSteps[] = {
        {"rc5's rule", {0u, false, false}},
        {"+ kept frames left out", {0u, true, false}},
        {"+ hysteresis", {RD_PS_OPT_HYST, true, false}},
        {"+ measured errors (all: now)", {RD_PS_OPT_ALL, true, false}},
    };

    enum { S = sizeof kSteps / sizeof kSteps[0] };

    static Out res[R5N][S];
    for (int i = 0; i < R5N; i++) {
        printf("  %s\n", kR5[i]->name);
        for (int k = 0; k < S; k++) {
            run(kR5[i], &kSteps[k].md, &res[i][k]);
            row(kSteps[k].what, &res[i][k]);
        }
        print(kR5[i], &res[i][S - 1]);
        const Out *now = &res[i][S - 1], *was = &res[i][0];
        CHECK(now->back[3] == 0u && now->sameOther && now->how[RD_PS_PLACED] > 0u);
        CHECK(fabs(now->rate - 1.0) <= now->rateTol);
        /* no regime evener under rc5's rule than now (within .01) */
        CHECK(statDev(&now->glass[3]) <= statDev(&was->glass[3]) + 0.01);
    }
    /* the kept frames' scenes (deja, the sacrifice scene, op2): rc5's rule
       a refresh or more off for over a hundred pictures (the log: 43-105
       further), now none, the hits up by twenty points or more, the
       deviation down by .04 or more (simulated .434 to .373, .413 to .363,
       .383 to .304) */
    for (int i = 2; i <= 4; i++) {
        const Out *now = &res[i][S - 1], *was = &res[i][0];
        CHECK(was->further > 100u && now->further == 0u);
        CHECK(share((double)now->hit, (double)now->scored) >=
              share((double)was->hit, (double)was->scored) + 20.0);
        CHECK(statDev(&now->glass[3]) < statDev(&was->glass[3]) - 0.04);
    }
    /* the measured errors in the cage (a skewed error): evener than the
       Gaussian spread's expectation (simulated .342 to .319) */
    CHECK(statDev(&res[1][S - 1].glass[3]) < statDev(&res[1][S - 2].glass[3]) - 0.01);
}

/* the even cadence (rd_pace_even.h) against the screen's rule alone, over
   the same regimes: the content's speed on the glass, the pictures shown a
   second, N and its changes, the aims late at the hand-over and the
   pictures shown off their refresh; a picture time that straddles two
   refreshes (16.1-17.1 ms swinging 0.5 ms each way over 4 s) and one that
   straddles one (7.9-8.9) */
static const Cfg kStraddle2 = {
    .name = "a picture time across two refreshes (GPU 16.1-17.1, a 4 s swing)",
    .gLo = 16.1,
    .gHi = 17.1,
    .rLo = 7.0,
    .rHi = 9.0,
    .dLess = 0.2,
    .stepLo = 2.4,
    .stepHi = 4.0,
    .reads = 6.0,
    .seed = 61u,
    .pd = 8.3532,
    .gSwing = 0.5,
    .gSwingS = 4.0,
    .step2Lo = 2.6,
    .step2Hi = 3.4};
static const Cfg kStraddle1 = {.name =
                                   "a picture time across one refresh (GPU 7.9-8.9, a 4 s swing)",
                               .gLo = 7.9,
                               .gHi = 8.9,
                               .rLo = 3.0,
                               .rHi = 5.0,
                               .dLess = 0.2,
                               .stepLo = 2.4,
                               .stepHi = 4.0,
                               .reads = 4.0,
                               .seed = 62u,
                               .pd = 8.3532,
                               .gSwing = 0.5,
                               .gSwingS = 4.0,
                               .step2Lo = 2.6,
                               .step2Hi = 3.4};

/* v0.11.6-rc8 (the even cadence on the phone, the opening, evidence/rc8
   L:827-1038): as made the opening's pictures took 16.7-19.2 ms of GPU
   with 0.2-0.8 ms idle before each (rc6 L:820-1118); held to four
   refreshes the GPU idled 8.0-10.1 ms before each and each took 23.7-25.4
   ms (its clocks down), the GPU's ends exact for 8-26 of 282-308 pictures
   (only those the render thread still waited for), and 160-181 of 247-266
   aimed pictures were shown off their refresh, twice as often earlier as
   later */
static const Cfg kR8Open = {.name = "rc8 the opening held: the GPU slower after an idle",
                            .gLo = 16.0,
                            .gHi = 21.0,
                            .rLo = 9.0,
                            .rHi = 10.4,
                            .dLess = 0.2,
                            .stepLo = 3.4,
                            .stepHi = 4.0,
                            .reads = 8.4,
                            .seed = 63u,
                            .pd = 8.3525,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4,
                            .dvfs = 0.035};

/* a compositor that replaces a picture held for its desired time by the
   next one queued before it is shown (mailbox's rule applied to a held
   picture: not known for Android, so the self-check) */
static const Cfg kR8Drop = {.name = "play, a held picture replaced by the next one queued",
                            .gLo = 13.0,
                            .gHi = 15.0,
                            .rLo = 7.0,
                            .rHi = 9.0,
                            .dLess = 0.2,
                            .stepLo = 2.4,
                            .stepHi = 4.0,
                            .reads = 6.0,
                            .seed = 64u,
                            .pd = 8.3532,
                            .step2Lo = 2.6,
                            .step2Hi = 3.4,
                            .dropHeld = 1};

static void testEven(void)
{
    printf(" the even cadence against the screen's rule alone (the screen's measure on the "
           "glass: deviation, above 1.5, below 0.75; on the refresh aimed or predicted; shown a "
           "second):\n");
    const Cfg *const all[] = {&kR5Play,    &kR5Cage,    &kOpening, &kR5Fast,   &kOne,   &kR5Sac,
                              &kStraddle2, &kStraddle1, &kR5Lead,  &kR5Regime, &kR8Open};

    enum { N = sizeof all / sizeof all[0] };

    static Out rule[N], even[N];
    for (size_t i = 0; i < N; i++) {
        printf("  %s\n", all[i]->name);
        run(all[i], &kNow, &rule[i]);
        run(all[i], &kEven, &even[i]);
        row("the screen's rule alone", &rule[i]);
        row("the even cadence", &even[i]);
        print(all[i], &even[i]);
        const Out *e = &even[i];
        /* every regime: never back, the content over the glass at real time
           within its bound, aimed at all, evener than the screen's rule
           alone, N changed at most 3 times in the 10 s */
        CHECK(e->back[3] == 0u && e->sameOther && e->aimed > 0u);
        CHECK(fabs(e->rate - 1.0) <= e->rateTol);
        CHECK(statDev(&e->glass[3]) <= statDev(&rule[i].glass[3]) + 1e-9);
        CHECK(e->changes <= 3u);
    }

    /* the rates: N the fewest refreshes the picture time fits with
       RD_PE_UP_MS (at most the tick's four), shown 1000 / (N P) a second
       within 0.3: play, about one a refresh, across one refresh, one a
       tick, the opening, across two refreshes and rc8's opening at 2, 1,
       2, 4, 3, 3 and 3 (with the desired present time three holds: a
       hand-over the step rule would hold is moved before it); deviation
       under .06 at two or one, under .15 at three (the pictures moved
       before the tick's close drawn from the frame pair before), under .01
       at four */
    static const struct {
        int at;
        uint32_t n;
        double dev;
    } kRate[] = {{0, 2, 0.06}, {3, 1, 0.06}, {7, 2, 0.06}, {4, 4, 0.01},
                 {2, 3, 0.15}, {6, 3, 0.15}, {10, 3, 0.15}};

    for (size_t i = 0; i < sizeof kRate / sizeof kRate[0]; i++) {
        const Cfg *c = all[kRate[i].at];
        const Out *e = &even[kRate[i].at];
        const double pd = c->pd > 0.0 ? c->pd : PD;
        if (!(fabs(e->fpsShown - 1000.0 / ((double)kRate[i].n * pd)) < 0.3 &&
              statDev(&e->glass[3]) < kRate[i].dev && e->nLast == kRate[i].n)) {
            printf("FAIL rate %s: N %u, %.1f a second, deviation %.3f\n", c->name, e->nLast,
                   e->fpsShown, statDev(&e->glass[3]));
            failures++;
        }
    }
    /* with the desired present time no aimed picture is shown before its
       refresh, in any regime; rc8's opening (the GPU slower and uneven
       after an idle) evener than the screen's rule alone (rc8 on the phone
       was worse: .45-.48 against rc6's .20-.26) */
    for (size_t i = 0; i < N; i++) {
        CHECK(even[i].shownEarlier == 0u);
    }
    CHECK(statDev(&even[10].glass[3]) < statDev(&rule[10].glass[3]) - 0.1);
    /* the self-check: where held pictures are replaced, off within the
       first second of aims (RD_PE_LOST_MAX never shown of RD_PE_LOST_WIN),
       the pictures as made from there (shown a second as the screen's rule
       alone's within 3), nothing held without an aim */
    {
        Out r, e;
        run(&kR8Drop, &kNow, &r);
        run(&kR8Drop, &kEven, &e);
        printf("  %s: lost %u, off at %.0f ms (first aim %.0f ms); shown a second %.1f against "
               "%.1f alone\n",
               kR8Drop.name, e.lost, e.offAtMs, e.firstAimMs, e.fpsShown, r.fpsShown);
        CHECK(e.offAtMs > 0.0 && e.offAtMs < e.firstAimMs + 1000.0 && e.lost >= RD_PE_LOST_MAX);
        CHECK(fabs(e.fpsShown - r.fpsShown) < 3.0 && e.heldUnfit == 0u);
    }
    /* every regime: nothing held without a usable fit, never more than two
       waits before a hand-over (the refresh committed at the first ask: a
       sleep's overshoot does not move it on), the first aimed picture by
       a second, or by the time RD_PS_MIN + 8 pictures were made at the
       screen's rule's rate where that is longer (the fit's 32 shown
       pictures first, as made, each time back three refreshes late: 1.33 s
       at 30 a second) */
    for (size_t i = 0; i < N; i++) {
        CHECK(even[i].heldUnfit == 0u && even[i].maxRounds <= 2u);
        /* (not the sacrifice scene: it starts in a run of kept frames) */
        const double slow = (double)(RD_PS_MIN + 8u) * 1000.0 / rule[i].fps;
        const double by = slow > 1000.0 ? slow : 1000.0;
        if (!(all[i] == &kR5Sac || (even[i].firstAimMs > 0.0 && even[i].firstAimMs < by))) {
            printf("FAIL first aim %s: %.0f ms\n", all[i]->name, even[i].firstAimMs);
            failures++;
        }
    }
    printf("  the first aimed picture at %.0f ms (play), %.0f ms (the opening); the most waits "
           "before a hand-over %u, %u\n",
           even[0].firstAimMs, even[2].firstAimMs, even[0].maxRounds, even[2].maxRounds);
    /* the cage (a tail of slow pictures): evener by .05 or more (simulated
       .319 to .241, at two refreshes, 59.4 shown a second) */
    CHECK(statDev(&even[1].glass[3]) < statDev(&rule[1].glass[3]) - 0.05);
    (void)kRc5;
}

/* the even cadence's parts: N for a picture time (its band), the plan's
   refresh and hold, a hand-over too late for its refresh, the bias, the
   floor, the fall-backs, the texts word for word */
static void testEvenParts(void)
{
    const double p = 8.3532;
    /* N: the fewest refreshes with t + 0.25 ms; up past 2P - 0.25 */
    CHECK(rd_pace_even_n_for(8.0, p, RD_PE_UP_MS) == 1u);
    CHECK(rd_pace_even_n_for(8.2, p, RD_PE_UP_MS) == 2u);
    CHECK(rd_pace_even_n_for(16.4, p, RD_PE_UP_MS) == 2u);
    CHECK(rd_pace_even_n_for(16.5, p, RD_PE_UP_MS) == 3u);
    CHECK(rd_pace_even_n_for(100.0, p, RD_PE_UP_MS) == RD_PE_N_MAX);
    RdPaceEven e;
    rd_pace_even_reset(&e);
    RdPaceEvenIn in = {.nowMs = 1000.0,
                       .fit = true,
                       .anchorMs = 6000.0,
                       .periodMs = p,
                       .xMs = 5003.0,
                       .gMs = 14.0,
                       .dMs = 8.0,
                       .prevEndMs = 990.0,
                       .stepEndMs = 0.0,
                       .tickMs = TICK};
    RdPaceEvenPlan pl;
    rd_pace_even_plan(&e, &in, &pl);
    /* the first: the refresh whose aim the earliest end (1022) makes, the
       hold to put the end RD_PE_AIM P before its latch */
    const double early = 1000.0 + 8.0 + 14.0;
    CHECK(pl.how == RD_PE_AIMED && pl.n == 2u && !pl.changed);
    const double sh = rd_pace_even_aim_share(&e);
    CHECK(sh == RD_PE_AIM_DESIRED); /* desired present times after a reset */
    CHECK(pl.targetMs >= early + in.xMs + sh * p - 1e-9 &&
          pl.targetMs < early + in.xMs + sh * p + p);
    CHECK(fabs(1000.0 + pl.holdMs + 22.0 + in.xMs - (pl.targetMs - sh * p)) < 1e-9);
    /* the desired time half a refresh before the refresh */
    CHECK(rd_pace_even_desired_ms(pl.targetMs, p) == pl.targetMs - 0.5 * p);
    /* idempotent until taken */
    RdPaceEvenPlan p2;
    rd_pace_even_plan(&e, &in, &p2);
    CHECK(p2.targetMs == pl.targetMs && p2.holdMs == pl.holdMs);
    bool late;
    const double r0 = rd_pace_even_take(&e, &in, 1000.0 + pl.holdMs + 22.0, false, &late);
    CHECK(r0 == pl.targetMs && !late);
    /* the next: N refreshes on; handed over 9 ms later than planned (over
       the aim's refresh less RD_PE_LATE_MS): its end
       past the refresh less RD_PE_LATE_MS, a refresh later and late */
    in.nowMs = 1010.0;
    in.prevEndMs = 1000.0 + pl.holdMs + 22.0;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(fabs(pl.targetMs - (r0 + 2.0 * p)) < 1e-6);
    const double planned = in.nowMs + pl.holdMs + 22.0;
    const double r1 = rd_pace_even_take(&e, &in, planned + 9.0, false, &late);
    CHECK(late && fabs(r1 - (r0 + 3.0 * p)) < 1e-6);
    /* the bias: a late one aims RD_PE_BIAS_STEP_MS earlier, an on-time one
       back by RD_PE_BIAS_DECAY_MS */
    rd_pace_even_shown(&e, 1);
    CHECK(fabs(e.biasMs - RD_PE_BIAS_STEP_MS) < 1e-12);
    rd_pace_even_shown(&e, 0);
    CHECK(fabs(e.biasMs - (RD_PE_BIAS_STEP_MS - RD_PE_BIAS_DECAY_MS)) < 1e-12);
    /* RD_PE_MISS_UP late aims: N up and floored while the picture time stays */
    e.missBits = (1u << RD_PE_MISS_UP) - 1u;
    in.nowMs = 1100.0;
    in.prevEndMs = 1080.0;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(pl.n == 3u && pl.changed && e.floorN == 3u);
    for (int i = 0; i < 400; i++) {
        in.nowMs += 10.0;
        rd_pace_even_plan(&e, &in, &pl);
        (void)rd_pace_even_take(&e, &in, 0.0, false, &late);
    }
    CHECK(e.n == 3u); /* 4 s later, the same picture time: still 3 */
    in.gMs = 12.5;    /* the picture time moved by over RD_PE_FLOOR_T_MS: down after the hold */
    for (int i = 0; i < 250; i++) {
        in.nowMs += 10.0;
        rd_pace_even_plan(&e, &in, &pl);
        (void)rd_pace_even_take(&e, &in, 0.0, false, &late);
    }
    CHECK(e.n == 2u);
    /* a picture time across the band (2P - 1.0 to 2P - 0.25): N kept */
    in.gMs = 2.0 * p - 0.6;
    for (int i = 0; i < 300; i++) {
        in.nowMs += 10.0;
        rd_pace_even_plan(&e, &in, &pl);
        (void)rd_pace_even_take(&e, &in, 0.0, false, &late);
        CHECK(pl.n == 2u);
    }
    /* the fall-backs: no fit, no GPU time, slower than the game's tick */
    in.fit = false;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(pl.how == RD_PE_NO_FIT && pl.holdMs == 0.0 && !e.planned);
    in.fit = true;
    in.gMs = 0.0;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(pl.how == RD_PE_NO_GPU && pl.holdMs == 0.0);
    in.gMs = 40.0;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(pl.how == RD_PE_SLOW && pl.holdMs == 0.0);
    /* a hand-over due 2 ms into the game's next step: made before it */
    rd_pace_even_reset(&e);
    in = (RdPaceEvenIn){.nowMs = 1000.0,
                        .fit = true,
                        .anchorMs = 6000.0,
                        .periodMs = p,
                        .xMs = 5003.0,
                        .gMs = 14.0,
                        .dMs = 8.0,
                        .prevEndMs = 990.0,
                        .stepEndMs = 0.0,
                        .tickMs = TICK};
    rd_pace_even_plan(&e, &in, &pl);
    /* asked again 5 ms before it is due (the refresh committed), with the
       game's step due 2 ms before it: handed over before the step */
    const double due = 1000.0 + pl.holdMs;
    in.nowMs = due - 5.0;
    in.stepEndMs = due - 2.0;
    rd_pace_even_plan(&e, &in, &pl);
    CHECK(pl.early && fabs(in.nowMs + pl.holdMs - (due - 2.0 - RD_PE_STEP_MARGIN_MS)) < 1e-9);

    /* the texts */
    char line[2048];
    (void)rd_pace_even_start_text(true, false, "off by default", line, sizeof line);
    CHECK_STR(line,
              "rd: an even number of refreshes a picture: off by default (off by default); with "
              "pictures as made, every picture shown for the same N refreshes: N the fewest "
              "with the picture time (the larger of the GPU's median span and the hand-over to "
              "the first submit) + 0.25 ms, up at once or when 8 of the last 32 aims were late "
              "(then not down until the picture time moves by 1.00 ms), down to the fewest it "
              "fits with 0.25 ms to spare after 2000 ms of it; each hand-over held until its "
              "predicted GPU end sits 1.00 of a refresh before its refresh's latch (the screen's "
              "grid and latch), its present asking that refresh less half a refresh as its "
              "desired time (shown no earlier), the content drawn for that refresh; off for the "
              "run where 4 of the last 32 such pictures were never shown or their acquire and "
              "present call took 3.0 ms more than without; it lowers the pictures a second "
              "where the GPU could make more; not used (ICO_RD_PACE_EVEN=1 turns it on)");
    RdPaceEvenWindow w;
    memset(&w, 0, sizeof w);
    CHECK(!rd_pace_even_format(&w, line, sizeof line) && line[0] == '\0');
    w.how[RD_PE_AIMED] = 590;
    w.how[RD_PE_NO_FIT] = 10;
    w.changes = 1;
    w.held = 580;
    w.heldUs = 580u * 3250u;
    w.early = 12;
    w.lateAims = 4;
    w.shown = 588;
    w.shownOff = 6;
    w.shownLate = 5;
    w.shownEarly = 1;
    w.n = 2;
    w.periodNs = 8353200;
    CHECK(rd_pace_even_format(&w, line, sizeof line));
    CHECK_STR(line,
              "; the even cadence: 2 refreshes a picture (the last; 59.9 a second), 1 changes; "
              "590 pictures aimed (580 held before the hand-over, 3.25 ms on average; 12 before "
              "the game's step), 4 late for their refresh at the hand-over; not aimed: the "
              "screen's grid or latch not fitted 10; shown off their refresh 6 of 588 (5 later, 1 "
              "earlier)");
}

/* ------------------------------------------------------------------- 4 */

static void testFallbacks(void)
{
    Out o;
    printf(" fall-backs:\n");
    const Cfg none = {.name = "no display times",
                      .gLo = 13.2,
                      .gHi = 15.4,
                      .rLo = 7.0,
                      .rHi = 9.0,
                      .dLess = 0.2,
                      .stepLo = 2.4,
                      .stepHi = 4.0,
                      .reads = 6.0,
                      .seed = 41u,
                      .noTimes = 1};
    run(&none, &kNow, &o);
    print(&none, &o);
    /* every picture the hand-over rule's to the bit, by the reason */
    CHECK(o.sameOther && o.how[RD_PS_PLACED] == 0u && o.how[RD_PS_NO_TIMES] > 0u);
    for (uint32_t i = WARM; i < o.pics; i++) {
        if (!s_p[i].kept) {
            CHECK(memcmp(&s_p[i].raw[3], &s_p[i].raw[0], sizeof(double)) == 0);
        }
    }
    const Cfg stale = {.name = "times that stop at tick 150",
                       .gLo = 13.2,
                       .gHi = 15.4,
                       .rLo = 7.0,
                       .rHi = 9.0,
                       .dLess = 0.2,
                       .stepLo = 2.4,
                       .stepHi = 4.0,
                       .reads = 6.0,
                       .seed = 42u,
                       .staleTick = 150};
    run(&stale, &kNow, &o);
    print(&stale, &o);
    CHECK(o.sameOther && o.how[RD_PS_PLACED] > 0u && o.how[RD_PS_STALE] > 0u);
    const Cfg change = {.name = "the display at 90 Hz from tick 150",
                        .gLo = 13.2,
                        .gHi = 15.4,
                        .rLo = 7.0,
                        .rHi = 9.0,
                        .dLess = 0.2,
                        .stepLo = 2.4,
                        .stepHi = 4.0,
                        .reads = 6.0,
                        .seed = 43u,
                        .changeTick = 150};
    run(&change, &kNow, &o);
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
    const RdPaceScreenIn in = {1030.0, 1, 1010.0, TICK, 1060.0, 0.4, false, 0.0};
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
    testRc5();
    testEvenParts();
    testEven();
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
