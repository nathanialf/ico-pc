/*
 * port/render/test/rd_as_made_timer_test.c
 *
 * The own timer for pictures as made (rd_as_made_timer.h) beside the two
 * other rules, CPU only:
 *   1. the timer's edges: its first picture on the hand-over rule's moment,
 *      a steady interval stepped exactly, a held picture's slot taken whole,
 *      the pull, an error over half a tick (a reset), the minimum step, the
 *      restarts, the decision (on Android by default, the hand-over rule on a
 *      desktop, the tests' switch over it) and the switch;
 *   2. 10 s simulations per regime (the window asks when the render thread
 *      is free, a further picture held when its reads would end past the
 *      next closing step, the GPU's start the later of the first submit and
 *      the end before, a 120 Hz mailbox display that shows each picture at
 *      the first refresh at or after its GPU end, a picture replaced when the
 *      next one reaches the same refresh) with the three rules computed over
 *      the same pictures: the hand-over less the mean drain (rd_rebase.c),
 *      the predicted GPU end (rd_pace_gpu.c), the own timer: every step's
 *      deviation, the clamps, the largest step, and the content's speed
 *      against the hand-overs, the GPU's ends and the display (mean,
 *      deviation, shares above 1.25 and 1.5 and below 0.75); the timer never
 *      back, no repeat, no clamp in steady state, its distance from the
 *      hand-over rule's moment bounded (the drift);
 *   3. a rate change (two to three pictures a tick and back) and a stall:
 *      no step back, the timer's largest step bounded, back within its
 *      bound;
 *   4. the rc1 log's runs after a warm-up.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_as_made_timer.h"
#include "rd_pace_gpu.h"
#include "rd_rebase.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define TICK 33.3667
#define REFRESH (1000.0 / 120.0)

/* ------------------------------------------------------------------- 1 */

/* a step with the frame's tick around the target (no hold inside it) */
static void st(RdAsMadeTimer *t, double target, double hand, uint32_t frame, RdAsMadeTimerOut *o)
{
    rd_as_made_timer_step(t, target, hand, frame, target - 16.0, TICK, o);
}

static void testEdges(void)
{
    RdAsMadeTimer t;
    RdAsMadeTimerOut o;
    rd_as_made_timer_reset(&t);
    /* the first: on the target */
    st(&t, 1000.0, 1005.0, 10, &o);
    CHECK(o.reset && o.momentMs == 1000.0);
    /* a steady 16 ms: the interval 16 and the moment 1016 exactly */
    st(&t, 1016.0, 1021.0, 10, &o);
    CHECK(!o.reset && o.intervalMs == 16.0 && o.momentMs == 1016.0 && o.slots == 0);
    st(&t, 1032.0, 1037.0, 11, &o);
    CHECK(o.intervalMs == 16.0 && o.momentMs == 1032.0);
    /* a jitter of 4 ms: a quarter of it taken */
    st(&t, 1052.0, 1057.0, 11, &o);
    CHECK(o.intervalMs == 52.0 / 3.0 &&
          fabs(o.momentMs - (1032.0 + 52.0 / 3.0 + 0.25 * (1052.0 - 1032.0 - 52.0 / 3.0))) < 1e-9);
    /* a held slot: the target 2 intervals on: k 1 */
    rd_as_made_timer_reset(&t);
    for (int i = 0; i < 6; i++) {
        st(&t, 1000.0 + 16.0 * i, 1005.0 + 16.0 * i, 10 + (uint32_t)i / 2, &o);
    }
    st(&t, 1000.0 + 16.0 * 7, 1005.0 + 16.0 * 7, 13, &o);
    CHECK(o.slots == 1 && !o.reset);
    /* an error over half a tick: on the target */
    rd_as_made_timer_reset(&t);
    for (int i = 0; i < 4; i++) {
        st(&t, 1000.0 + 16.0 * i, 1005.0 + 16.0 * i, 10, &o);
    }
    rd_as_made_timer_step(&t, 1048.0 - 30.0, 1069.0, 11, 1040.0, TICK, &o);
    /* behind by 46 (over half a tick): put on the target, which would go
       back: the minimum step, a quarter of the interval 16 */
    CHECK(o.reset && o.momentMs == 1048.0 + 0.25 * 16.0);
    /* the restarts: frame back, a gap over a second */
    st(&t, 900.0, 1070.0, 10, &o);
    CHECK(o.reset && o.momentMs == 900.0);
    st(&t, 2100.0, 2100.0, 11, &o);
    CHECK(o.reset && o.momentMs == 2100.0);
    /* the decision: the tests' switch over everything; the default on
       Android only, a desktop the hand-over rule (v0.11.5's) */
    {
        const char *why = NULL;
        CHECK(rd_as_made_timer_decide(-1, true, true, &why) &&
              strcmp(why, "Android: each picture's moment from its own timer") == 0);
        CHECK(!rd_as_made_timer_decide(-1, true, false, &why) &&
              strcmp(why, "not Android: the hand-over rule kept, the timer measured on a phone "
                          "only; computed and reported") == 0);
        CHECK(!rd_as_made_timer_decide(-1, false, true, &why) &&
              strcmp(why, "RD_AS_MADE_TIMER_DEFAULT 0: computed and reported") == 0);
        CHECK(rd_as_made_timer_decide(1, false, false, &why) &&
              strcmp(why, "ICO_RD_AS_MADE_TIMER=1: the tests' switch") == 0);
        CHECK(!rd_as_made_timer_decide(0, true, true, &why) &&
              strcmp(why, "ICO_RD_AS_MADE_TIMER=0: the tests' switch") == 0);
    }
    /* the switch: the decision with this build's platform */
    {
        const char *e = getenv("ICO_RD_AS_MADE_TIMER");
        const int env = e != NULL && e[0] != '\0' ? (e[0] != '0' ? 1 : 0) : -1;
#ifdef __ANDROID__
        const bool android = true;
#else
        const bool android = false;
#endif
        const char *why = NULL;
        CHECK(rd_as_made_timer_on() ==
              rd_as_made_timer_decide(env, RD_AS_MADE_TIMER_DEFAULT != 0, android, &why));
        CHECK(strcmp(rd_as_made_timer_why(), why) == 0);
    }
}

/* ------------------------------------------------------------------- 2 */

static uint32_t s_seed;

static double U(double lo, double hi)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((double)(s_seed >> 8) / 16777216.0);
}

typedef struct Cfg {
    const char *name;
    double gLo, gHi, rLo, rHi, dLess, stepLo, stepHi, reads;
    int open; /* the brief's cage hand-overs */
    uint32_t ticks;
    uint32_t seed;
    int change;  /* 1: GPU 9.5-10.5 ms in ticks 100-199 (three a tick), else the cfg's */
    int stallAt; /* a tick with a 200 ms stall before its first ask (0 none) */
} Cfg;

#define NR 3
static const char *const kRule[NR] = {"hand-over", "GPU end", "timer"};
#define SIMN 4096

typedef struct Pic {
    double hand, sub, g, end, shown, t0;
    double content[NR]; /* ticks: frame + alpha */
    double raw[NR];
    double moment[NR]; /* ms */
    uint32_t frame;
    bool visible;
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

typedef struct Out {
    uint32_t pics, shown;
    double fps;
    Stat step[NR], hand[NR], gpu[NR], glass[NR];
    uint32_t clampLo[NR], clampHi[NR], back[NR], repeat[NR];
    double largest[NR];
    double timerDrift; /* the largest |timer - hand-over rule| ms after the warm-up */
    uint32_t timerResets, timerSlots, timerWindow;
} Out;

static void run(const Cfg *c, Out *o, const double *logF, const double *logA, const double *logG,
                int logN)
{
    memset(o, 0, sizeof(*o));
    s_seed = c->seed;
    RdPaceGpu m;
    rd_pace_gpu_model_reset(&m);
    RdAsMadeTimer tm;
    rd_as_made_timer_reset(&tm);
    RdRebase rb[NR];
    memset(rb, 0, sizeof rb);
    uint32_t n = 0;
    double J = 0.0, Eprev = 0.0;
    const double P = TICK / 2.0;

    typedef struct Pend {
        RdPaceGpuSample s;
        double avail;
    } Pend;

    static Pend pend[SIMN];
    uint32_t pendN = 0, pendAt = 0;
    const uint32_t extra = logN > 0 ? (uint32_t)(logF[logN - 1] - logF[0]) + 1u : 0u;
    for (uint32_t k = 0; k < c->ticks + extra && n < SIMN - 8; k++) {
        const double t0 = 1000.0 + (double)k * TICK;
        const double step = U(c->stepLo, c->stepHi);
        const bool logPart = logN > 0 && k >= c->ticks;
        double asks[8];
        int nAsk = 0, ai = 0;
        if (logPart) {
            for (int i = 0; i < logN; i++) {
                if ((uint32_t)(logF[i] - logF[0]) + c->ticks == k) {
                    asks[nAsk++] = t0 + logA[i] * TICK;
                }
            }
        } else if (c->open) {
            const double a1 = U(0.15, 0.35), a2b = a1 + U(0.29, 0.45);
            asks[nAsk++] = t0 + a1 * TICK;
            asks[nAsk++] = t0 + (a2b > 0.506 ? a2b : 0.506) * TICK;
        }
        if (c->stallAt && k == (uint32_t)c->stallAt) {
            J = J > t0 + 200.0 ? J : t0 + 200.0;
        }
        bool first = true;
        for (int j = 0; j < 2; j++) {
            const double ps = t0 + j * P, pe = ps + P;
            double x = J > ps + (j == 0 ? step : 0.2) ? J : ps + (j == 0 ? step : 0.2);
            for (;;) {
                if (c->open || logPart) {
                    if (ai >= nAsk || asks[ai] >= pe) {
                        break;
                    }
                    x = asks[ai++];
                } else {
                    if (x + 0.3 > pe) {
                        break;
                    }
                    if (!first && x + c->reads > t0 + TICK) {
                        j = 2;
                        break;
                    }
                }
                while (pendAt < pendN && pend[pendAt].avail <= x) {
                    rd_pace_gpu_model_note(&m, &pend[pendAt].s, NULL);
                    pendAt++;
                }
                const double r = U(c->rLo, c->rHi);
                double gpu = (c->change && k >= 100 && k < 200) ? U(9.5, 10.5) : U(c->gLo, c->gHi);
                if (logPart) {
                    for (int i = 0; i < logN; i++) {
                        if (fabs(t0 + logA[i] * TICK - x) < 1e-9) {
                            gpu = logG[i];
                        }
                    }
                }
                Pic *p = &s_p[n];
                memset(p, 0, sizeof(*p));
                p->hand = x;
                p->sub = x + r - c->dLess;
                p->g = gpu;
                p->frame = k;
                p->t0 = t0;
                p->end = (p->sub > Eprev ? p->sub : Eprev) + gpu;
                double jobEnd = x + r;
                if (n > 0) {
                    Pic *pp = &s_p[n - 1u];
                    const bool blocked = pp->end > jobEnd;
                    const bool exact = blocked && pp->end - jobEnd >= 0.05;
                    pend[pendN].s =
                        (RdPaceGpuSample){pp->hand, pp->sub, pp->g,
                                          blocked ? (exact ? pp->end : jobEnd) : 0.0, exact};
                    pend[pendN].avail = blocked ? pp->end : jobEnd;
                    pendN++;
                    if (blocked) {
                        jobEnd = pp->end;
                    }
                }
                /* the three rules over the same picture (no drain here: the
                   hand-over rule's moment is the hand-over) */
                RdPaceGpuPlace pl;
                rd_pace_gpu_model_place(&m, x, k, &pl);
                double sh;
                bool held;
                const double rawHand =
                    rd_rebase_raw(&rb[0], (x - t0) / TICK, TICK, 0.0, k, &sh, &held);
                const double rawGpu =
                    rd_rebase_raw(&rb[1], (x + pl.shiftMs - t0) / TICK, TICK, 0.0, k, &sh, &held);
                RdAsMadeTimerOut to;
                rd_as_made_timer_step(&tm, t0 + rawHand * TICK, x, k, t0, TICK, &to);
                o->timerWindow += to.window && n >= 64u ? 1u : 0u;
                o->timerResets += to.reset ? 1u : 0u;
                o->timerSlots += to.slots;
                if (n >= 64u && fabs(to.errorMs) > o->timerDrift) {
                    o->timerDrift = fabs(to.errorMs);
                }
                p->raw[0] = rawHand;
                p->raw[1] = rawGpu;
                p->raw[2] = (to.momentMs - t0) / TICK;
                for (int q = 0; q < NR; q++) {
                    if (n >= 64u) {
                        o->clampLo[q] += p->raw[q] < 0.0 ? 1u : 0u;
                        o->clampHi[q] += p->raw[q] > 0.999 ? 1u : 0u;
                    }
                    p->content[q] = (double)k + rd_rebase_alpha(p->raw[q]);
                    p->moment[q] = p->content[q] * TICK;
                }
                Eprev = p->end;
                J = jobEnd;
                n++;
                first = false;
                if (!(c->open || logPart)) {
                    x = J;
                }
            }
        }
    }
    /* the display: the first refresh at or after the GPU's end; a picture
       replaced when the next reaches the same refresh */
    for (uint32_t i = 0; i < n; i++) {
        s_p[i].shown = ceil(s_p[i].end / REFRESH) * REFRESH;
        s_p[i].visible = true;
        if (i > 0 && s_p[i].shown <= s_p[i - 1u].shown) {
            s_p[i - 1u].visible = false;
            s_p[i].shown = s_p[i - 1u].shown;
        }
    }
    o->pics = n;
    o->fps = (double)n / ((double)(c->ticks + extra) * TICK / 1000.0);
    int lastShown = -1;
    for (uint32_t i = 64u; i < n; i++) {
        for (int q = 0; q < NR; q++) {
            const double d = s_p[i].content[q] - s_p[i - 1u].content[q];
            statAdd(&o->step[q], d);
            if (d > o->largest[q]) {
                o->largest[q] = d;
            }
            o->back[q] += d < 0.0 ? 1u : 0u;
            o->repeat[q] += d == 0.0 ? 1u : 0u;
            statAdd(&o->hand[q],
                    (s_p[i].moment[q] - s_p[i - 1u].moment[q]) / (s_p[i].hand - s_p[i - 1u].hand));
            statAdd(&o->gpu[q],
                    (s_p[i].moment[q] - s_p[i - 1u].moment[q]) / (s_p[i].end - s_p[i - 1u].end));
        }
        if (s_p[i].visible) {
            if (lastShown >= 64) {
                const Pic *a = &s_p[lastShown], *b = &s_p[i];
                for (int q = 0; q < NR; q++) {
                    statAdd(&o->glass[q], (b->moment[q] - a->moment[q]) / (b->shown - a->shown));
                }
            }
            lastShown = (int)i;
            o->shown++;
        }
    }
}

static void print(const Cfg *c, const Out *o)
{
    printf("  %s: %u pictures, %.1f fps, %u shown; the timer's resets %u, slots taken %u, its "
           "largest distance from the hand-over rule %.2f ms, held in the frame's tick %u\n",
           c->name, o->pics, o->fps, o->shown, o->timerResets, o->timerSlots, o->timerDrift,
           o->timerWindow);
    for (int q = 0; q < NR; q++) {
        printf("    %-9s every step %.3f (%.3f), largest %.3f, clamps %u/%u, back %u, repeats %u; "
               "speed against the hand-overs %.2f (%.2f), the GPU's ends %.2f (%.2f), the "
               "display %.2f (%.3f) above 1.25 %.1f %% above 1.5 %.1f %% below 0.75 %.1f %%\n",
               kRule[q], statMean(&o->step[q]), statDev(&o->step[q]), o->largest[q], o->clampLo[q],
               o->clampHi[q], o->back[q], o->repeat[q], statMean(&o->hand[q]), statDev(&o->hand[q]),
               statMean(&o->gpu[q]), statDev(&o->gpu[q]), statMean(&o->glass[q]),
               statDev(&o->glass[q]),
               100.0 * o->glass[q].above125 / (o->glass[q].n > 0 ? o->glass[q].n : 1.0),
               100.0 * o->glass[q].above15 / (o->glass[q].n > 0 ? o->glass[q].n : 1.0),
               100.0 * o->glass[q].below075 / (o->glass[q].n > 0 ? o->glass[q].n : 1.0));
    }
}

/* the timer's guarantees in every regime */
static void common(const Cfg *c, const Out *o, bool steady)
{
    if (o->back[2] || o->repeat[2]) {
        printf("FAIL %s: the timer back %u, repeats %u\n", c->name, o->back[2], o->repeat[2]);
        failures++;
    }
    /* held inside its frame's tick (the timer's own clamp, no alpha clamp):
       at most 20 in 10 s in steady state (the simulations give 0-18) */
    if (steady && o->timerWindow > 20u) {
        printf("FAIL %s: the timer held in its frame's tick %u times\n", c->name, o->timerWindow);
        failures++;
    }
    if (steady && (o->clampLo[2] || o->clampHi[2])) {
        printf("FAIL %s: the timer clamped %u/%u in steady state\n", c->name, o->clampLo[2],
               o->clampHi[2]);
        failures++;
    }
    /* its distance from the hand-over rule's moment: at most half a tick
       (the reset), so over any span its speed is real time's within a tick
       over the span */
    if (o->timerDrift > 0.5 * TICK + 1e-9) {
        printf("FAIL %s: the timer %.2f ms from the hand-over rule\n", c->name, o->timerDrift);
        failures++;
    }
}

static const Cfg kCage = {
    "cage (2 a tick)", 14.8, 15.8, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 0, 300, 11u, 0, 0};
static const Cfg kOpen = {
    "the brief's cage", 14.8, 15.6, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 1, 300, 12u, 0, 0};
static const Cfg kStage3 = {
    "stages 3/5 (2-3 a tick)", 12.3, 14.5, 7.0, 9.0, 0.2, 2.4, 4.0, 6.0, 0, 300, 13u, 0, 0};
static const Cfg kThree = {
    "three a tick", 9.5, 10.5, 6.0, 8.0, 0.2, 2.4, 4.0, 5.0, 0, 300, 14u, 0, 0};
static const Cfg kMixed = {"opening (1-2 a tick, GPU-bound)",
                           18.0,
                           21.0,
                           9.0,
                           10.4,
                           0.2,
                           3.4,
                           4.0,
                           8.4,
                           0,
                           300,
                           15u,
                           0,
                           0};
static const Cfg kOne = {"one a tick", 30.0, 32.5, 9.0, 10.4, 0.2, 4.3,
                         5.8,          TICK, 0,    300, 16u,  0,   0};
static const Cfg kChange = {
    "two to three a tick and back", 14.8, 15.8, 6.0, 8.0, 0.2, 2.4, 4.0, 5.0, 0, 300, 18u, 1, 0};
static const Cfg kStall = {
    "a 200 ms stall", 14.8, 15.8, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 0, 300, 19u, 0, 150};

static void testSims(void)
{
    Out o;
    printf(" simulations (10 s, 120 Hz mailbox display; steps in ticks, speeds 1.00 real time):\n");
    const Cfg *const steady[] = {&kCage, &kOpen, &kStage3, &kThree, &kMixed, &kOne};
    for (size_t i = 0; i < sizeof steady / sizeof steady[0]; i++) {
        run(steady[i], &o, NULL, NULL, NULL, 0);
        print(steady[i], &o);
        common(steady[i], &o, true);
        /* the timer's steps evener than the hand-overs' wherever more than
           one picture a tick is made */
        if (steady[i] != &kOne) {
            CHECK(statDev(&o.step[2]) < statDev(&o.step[0]));
        }
    }
    run(&kChange, &o, NULL, NULL, NULL, 0);
    print(&kChange, &o);
    common(&kChange, &o, false);
    /* through the change no step over twice the slowest regime's interval */
    CHECK(o.largest[2] < 1.0);
    run(&kStall, &o, NULL, NULL, NULL, 0);
    print(&kStall, &o);
    common(&kStall, &o, false);
}

/* ------------------------------------------------------------------- 4 */

static void testLogRuns(void)
{
    static const struct {
        const char *name;
        double frame[5], alpha[5], gpu[5];
    } kRun[] = {
        {"L:3846-3851",
         {10143, 10143, 10144, 10144, 10145},
         {0.23, 0.58, 0.28, 0.56, 0.22},
         {14.6, 14.3, 15.7, 14.3, 14.9}},
        {"L:4109-4113",
         {11043, 11043, 11044, 11044, 11045},
         {0.35, 0.79, 0.22, 0.64, 0.15},
         {14.5, 14.2, 14.7, 14.2, 14.6}},
        {"L:4275-4280",
         {11643, 11643, 11644, 11644, 11645},
         {0.22, 0.53, 0.33, 0.70, 0.37},
         {16.5, 16.2, 19.8, 16.6, 16.4}},
    };

    static const Cfg kWarm = {"", 14.8, 15.6, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 1, 300, 21u, 0, 0};
    printf(" the rc1 log's runs (steps in ticks: hand-over / GPU end / timer):\n");
    for (size_t r = 0; r < sizeof kRun / sizeof kRun[0]; r++) {
        Out o;
        run(&kWarm, &o, kRun[r].frame, kRun[r].alpha, kRun[r].gpu, 5);
        printf("  %s:", kRun[r].name);
        for (int q = 0; q < NR; q++) {
            printf(" %s", q ? "/" : "");
            for (uint32_t i = o.pics - 4u; i < o.pics; i++) {
                printf(" %.3f", s_p[i].content[q] - s_p[i - 1u].content[q]);
            }
        }
        double dev[NR];
        for (int q = 0; q < NR; q++) {
            double s = 0, s2 = 0;
            for (uint32_t i = o.pics - 4u; i < o.pics; i++) {
                const double d = s_p[i].content[q] - s_p[i - 1u].content[q];
                s += d;
                s2 += d * d;
                CHECK(q != 2 || d > 0.0);
            }
            dev[q] = sqrt(fmax(0.0, s2 / 4.0 - (s / 4.0) * (s / 4.0)));
        }
        printf("; deviation %.3f / %.3f / %.3f\n", dev[0], dev[1], dev[2]);
        /* today's steps are the log's alphas' */
        CHECK(fabs((s_p[o.pics - 4u].content[0] - s_p[o.pics - 5u].content[0]) -
                   (kRun[r].alpha[1] - kRun[r].alpha[0])) < 1e-6);
        /* the uneven runs: the timer's steps evener than the log's */
        if (dev[0] > 0.1) {
            CHECK(dev[2] < dev[0] / 2.0);
        }
    }
}

int main(void)
{
    printf("rd_as_made_timer_test\n");
    testEdges();
    testSims();
    testLogRuns();
    if (failures) {
        printf("rd_as_made_timer_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_as_made_timer_test: ok\n");
    return 0;
}
