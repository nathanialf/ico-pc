/* rd_ab_test.c: the in-play A/B judge (port/render/rd_ab.h), pure: made-up
 * streams of replays with two or three pictures in flight (a replay's GPU
 * times come back when a later one begins, as rd__perf_collect_gpu reads
 * them), each picture's GPU span from its arm.
 *   - the verdicts: 5 % cheaper with it: on; 3 % dearer: off; no
 *     difference: off; 40 % of the pictures 3 to 5 times as long and a
 *     5 % saving: still on; the work rising 1 % a picture: no false on
 *     (and a 5 % saving still found);
 *   - the trial's cost: its replays without it bounded (the early verdict's
 *     eight, at most half of RD_AB_GIVE_UP when no pair can be made, then
 *     no verdict and kept on);
 *   - the trial 2 s into a stage, only once the subject had work there;
 *     the verdict kept for the stage (a stage visited again takes it at
 *     once); at most RD_AB_TRIALS trials, the cap's line once;
 *   - one subject on trial at a time (a fake second subject), each its own
 *     verdict;
 *   - the way back: rd_ab_stop mid-trial: every later replay without every
 *     subject, no trial, its line;
 *   - the modes on, off and alt (on, off, off, on from the first);
 *   - the carried arm (a stage without a verdict of its own starts on the
 *     run's last verdict's arm; a wrong one corrected by its trial);
 *   - the rate requirement: a GPU-bound stream at a 40 ms tick for 60 s,
 *     pictures of 33, 37.5, 48 and 60 ms, a subject saving or costing 3, 5
 *     and 10 %, in one stage, four, and four flipping the effect's sign:
 *     the judge at least the worse fixed arm; at least the best per stage
 *     less the pictures the worse arm loses during the time the judge was
 *     allowed on it (computed from the two fixed-arm runs), less 1; the
 *     better arm's pictures after the last verdict, within 1. */
#include "rd_ab.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t s_rng = 12345u;

static double rnd(void) /* 0 .. 1 */
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (double)(s_rng >> 8) / 16777216.0;
}

/* the lines so far, printed, the last kept */
static char s_last[RD_AB_LINE];
static int s_lines;

static void lines(RdAb *a)
{
    char l[RD_AB_LINE];
    while (rd_ab_take_line(a, l, sizeof(l))) {
        printf("  %s\n", l);
        snprintf(s_last, sizeof(s_last), "%s", l);
        s_lines++;
    }
}

/* a stream: effect the on arm's cost over the off arm's (-0.05: 5 %
   cheaper with it), noise each span's uniform spread (+-), outliers the
   share of pictures longer by 3 to 5 times, grow the work's rise a
   picture, fixedMs a span's part that does not follow the work, lag the
   pictures in flight, workSwing the work alternating by this share */
typedef struct Stream {
    double effect, noise, outliers, grow, fixedMs, workSwing;
    int lag;
    int replays;
    int32_t stage;
    double stepMs;
} Stream;

typedef struct Pend {
    RdAbTag tag;
    double work, ms;
    bool keep;
} Pend;

/* the stream's replays through the judge from t0 on: the time after */
static double run(RdAb *a, const Stream *s, double t0, uint8_t used)
{
    Pend q[8];
    int qn = 0;
    double work = 50000.0, t = t0;
    for (int i = 0; i < s->replays + s->lag; i++) {
        /* the replay lag pictures back read back first */
        if (qn >= s->lag || (i >= s->replays && qn > 0)) {
            const RdAbIn in = {q[0].tag, used, q[0].keep, true, q[0].work, q[0].ms};
            rd_ab_collect(a, &in);
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
            qn--;
        }
        if (i >= s->replays) {
            continue;
        }
        const RdAbTag tag = rd_ab_begin(a, s->stage, t, false);
        const bool off = (tag.off & 1u) != 0u;
        const double w = work * (1.0 + ((i & 1) ? s->workSwing : -s->workSwing));
        double ms = s->fixedMs + w * 0.0008;
        ms *= off ? 1.0 : 1.0 + s->effect;
        ms *= 1.0 + s->noise * (2.0 * rnd() - 1.0);
        if (s->outliers > 0.0 && rnd() < s->outliers) {
            ms *= 3.0 + 2.0 * rnd();
        }
        q[qn++] = (Pend){tag, w, ms, false};
        work *= 1.0 + s->grow;
        t += s->stepMs;
        lines(a);
    }
    lines(a);
    return t;
}

static int verdictOf(const Stream *s, RdAb *a)
{
    rd_ab_init(a, RD_AB_AUTO);
    CHECK(rd_ab_subject(a, "zero-area cull") == 0, "the first subject's id");
    rd_ab_allow(a, 0, true);
    (void)run(a, s, 0.0, 1u);
    return rd_ab_verdict(a, 0, s->stage);
}

static RdAb s_a; /* large: not on the stack */

static void testVerdicts(void)
{
    Stream s = {0};
    s.noise = 0.01;
    s.lag = 2;
    s.replays = 900;
    s.stage = 42;
    s.stepMs = 40.0;
    printf("5 %% cheaper with it:\n");
    s.effect = -0.05;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_ON, "5 %% cheaper: not on");
    CHECK(strstr(s_last, "A/B: zero-area cull, stage 42: 8 pairs (all one way: the early "
                         "verdict), with it ") == s_last &&
              strstr(s_last, "cheaper with it, 8 of 8 cheaper with it), 0 dropped by a long "
                             "picture, 0 by unequal work: on for this stage") != NULL,
          "the verdict line: %s", s_last);
    /* the cost: the early verdict's replays without it (8 pairs: 8) and the
       ones begun before it came back (the lag) */
    CHECK(s_a.offTrial >= RD_AB_EARLY && s_a.offTrial <= RD_AB_EARLY + 2u,
          "the trial's replays without it: %llu", (unsigned long long)s_a.offTrial);
    printf("3 %% dearer with it:\n");
    s.effect = 0.03;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_OFF, "3 %% dearer: not off");
    CHECK(strstr(s_last, "dearer with it, 8 of 8 cheaper without)") != NULL &&
              strstr(s_last, ": off for this stage") != NULL,
          "the verdict line: %s", s_last);
    printf("no difference:\n");
    s.effect = 0.0;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_OFF, "no difference: not off");
    CHECK(strstr(s_last, ": 32 pairs, with it ") != NULL, "judged at 32 pairs: %s", s_last);
    printf("40 %% outliers at 3-5x, 5 %% cheaper:\n");
    s.effect = -0.05;
    s.outliers = 0.40;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_ON, "the outliers hid the saving");
    CHECK(s_a.t.dropLong > 0u, "no pair dropped by a long picture: %u", (unsigned)s_a.t.dropLong);
    CHECK(strstr(s_last, "dropped by a long picture") != NULL && strstr(s_last, ": on for") != NULL,
          "%s", s_last);
    s.outliers = 0.0;
    printf("the work rising 1 %% a picture, no difference:\n");
    s.effect = 0.0;
    s.grow = 0.01;
    s.fixedMs = 20.0;
    s.replays = 300;
    CHECK(verdictOf(&s, &s_a) != RD_AB_V_ON, "the drift gave a false on");
    printf("the work rising 1 %% a picture, 5 %% cheaper:\n");
    s.effect = -0.05;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_ON, "the drift hid the saving");
    s.grow = 0.0;
    s.fixedMs = 0.0;
    printf("three pictures in flight, 5 %% cheaper:\n");
    s.lag = 3;
    s.replays = 900;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_ON, "three in flight: not on");
    s.effect = 0.03;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_OFF, "three in flight, dearer: not off");
}

static void testCost(void)
{
    /* no pair can be made (the work swings 30 % a replay): the trial gives
       up at RD_AB_GIVE_UP replays, half of them without it at most, no
       verdict, kept on */
    printf("no pair to make:\n");
    Stream s = {0};
    s.lag = 2;
    s.replays = 2000;
    s.stage = 7;
    s.stepMs = 40.0;
    s.workSwing = 0.15;
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 7) == RD_AB_V_KEPT, "not kept on: %d", rd_ab_verdict(&s_a, 0, 7));
    CHECK(s_a.offTrial <= RD_AB_GIVE_UP / 2u, "%llu replays without it",
          (unsigned long long)s_a.offTrial);
    CHECK(s_a.trials == 1u, "%u trials (the stage tried again)", (unsigned)s_a.trials);
    CHECK(strstr(s_last, "A/B: zero-area cull, stage 7: no verdict (600 replays, 0 pairs; 0 "
                         "dropped by a long picture, ") == s_last &&
              strstr(s_last, " by unequal work, ") != NULL && s_a.t.dropWork >= 290u &&
              strstr(s_last, "no more trials here, it stays with it, no verdict before it in the "
                             "run") != NULL,
          "%s", s_last);
    /* every later replay of the stage with it */
    const RdAbTag tag = rd_ab_begin(&s_a, 7, 1e9, false);
    CHECK(tag.off == 0u && tag.trial == 0u, "after no verdict: off 0x%x trial %u", tag.off,
          (unsigned)tag.trial);
}

static void testStages(void)
{
    printf("stages:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    /* no trial before 2 s into the stage, nor before the subject had work */
    RdAbTag tag = rd_ab_begin(&s_a, 3, 0.0, false);
    CHECK(tag.trial == 0u && tag.off == 0u, "a trial at the stage's start");
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 1.0, false);
    CHECK(tag.trial == 0u, "a trial before the subject had work");
    const RdAbIn in = {tag, 1u, false, true, 50000.0, 40.0};
    rd_ab_collect(&s_a, &in);
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 2.0, false);
    CHECK(tag.trial != 0u && tag.seq == 0u && tag.off == 0u, "the trial's first: trial %u off %x",
          (unsigned)tag.trial, tag.off);
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 3.0, false);
    CHECK(tag.seq == 1u && tag.off == 1u, "the trial's second without it");
    /* a keep frame in a trial: with it, not in the trial */
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 4.0, true);
    CHECK(tag.trial == 0u && tag.off == 0u, "a keep frame in the trial");
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 5.0, false);
    CHECK(tag.seq == 2u && tag.off == 1u, "the trial's third without it");
    tag = rd_ab_begin(&s_a, 3, RD_AB_START_MS + 6.0, false);
    CHECK(tag.seq == 3u && tag.off == 0u, "the trial's fourth with it");
    /* the stage ends mid-trial: no verdict, tried again at the next visit */
    tag = rd_ab_begin(&s_a, 4, RD_AB_START_MS + 7.0, false);
    lines(&s_a);
    CHECK(strstr(s_last, "stage 3: no verdict (the stage ended, 0 pairs;") != NULL &&
              strstr(s_last, "not judged, it stays with it, no verdict before it in the run") !=
                  NULL,
          "%s", s_last);
    CHECK(rd_ab_verdict(&s_a, 0, 3) == RD_AB_V_NONE, "the ended stage judged");
    /* stage 5 judged off, stage 6 on, back to 5: off at once, no trial */
    Stream s = {0};
    s.lag = 2;
    s.replays = 200;
    s.stepMs = 40.0;
    s.stage = 5;
    s.effect = 0.04;
    double t = run(&s_a, &s, 100000.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 5) == RD_AB_V_OFF, "stage 5 not off");
    s.stage = 6;
    s.effect = -0.04;
    t = run(&s_a, &s, t, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 6) == RD_AB_V_ON, "stage 6 not on");
    const uint32_t trials = s_a.trials;
    tag = rd_ab_begin(&s_a, 5, t, false);
    CHECK(tag.off == 1u && tag.trial == 0u, "stage 5 again: off %x trial %u", tag.off,
          (unsigned)tag.trial);
    tag = rd_ab_begin(&s_a, 5, t + 10000.0, false);
    CHECK(tag.off == 1u && tag.trial == 0u && s_a.trials == trials, "stage 5 tried again");
    tag = rd_ab_begin(&s_a, 6, t + 10001.0, false);
    CHECK(tag.off == 0u && tag.trial == 0u, "stage 6 again: off %x", tag.off);
    /* the cap: stages 100.. each judged until RD_AB_TRIALS trials ran */
    printf("the cap:\n");
    s_lines = 0;
    for (int k = 0; k < 16; k++) {
        s.stage = 100 + k;
        s.effect = 0.04;
        t = run(&s_a, &s, t + 1.0, 1u);
    }
    CHECK(s_a.trials == RD_AB_TRIALS, "%u trials", (unsigned)s_a.trials);
    int judged = 0;
    for (int k = 0; k < 16; k++) {
        judged += rd_ab_verdict(&s_a, 0, 100 + k) != RD_AB_V_NONE;
    }
    /* the trials before: stage 3 (ended), 5 and 6 */
    CHECK(judged == (int)RD_AB_TRIALS - 3, "%d stages judged past the cap's start", judged);
    CHECK(s_a.capSaid, "the cap's line not said");
}

static void testTwoSubjects(void)
{
    printf("a second subject:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    CHECK(rd_ab_subject(&s_a, "zero-area cull") == 0, "id 0");
    CHECK(rd_ab_subject(&s_a, "fake") == 1, "id 1");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    /* subject 0 saves 5 %, subject 1 costs 5 %: one trial at a time */
    double t = 0.0;
    Pend q[4];
    int qn = 0;
    bool both = false;
    for (int i = 0; i < 400; i++) {
        if (qn >= 2) {
            const RdAbIn in = {q[0].tag, 3u, false, true, q[0].work, q[0].ms};
            rd_ab_collect(&s_a, &in);
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
            qn--;
        }
        const RdAbTag tag = rd_ab_begin(&s_a, 9, t, false);
        if (tag.trial != 0u) {
            /* the subject not on trial at its default (on) */
            const uint8_t other = s_a.t.subject == 0 ? 2u : 1u;
            both = both || (tag.off & other) != 0u;
        }
        double ms = 40.0;
        ms *= (tag.off & 1u) ? 1.0 : 0.95;
        ms *= (tag.off & 2u) ? 1.0 : 1.05;
        q[qn++] = (Pend){tag, 50000.0, ms, false};
        t += 40.0;
        lines(&s_a);
    }
    CHECK(!both, "the other subject off during a trial");
    CHECK(rd_ab_verdict(&s_a, 0, 9) == RD_AB_V_ON, "subject 0 not on");
    CHECK(rd_ab_verdict(&s_a, 1, 9) == RD_AB_V_OFF, "subject 1 not off");
    CHECK(s_a.trials == 2u, "%u trials", (unsigned)s_a.trials);
    const RdAbTag tag = rd_ab_begin(&s_a, 9, t, false);
    CHECK(tag.off == 2u, "after both: off %x", tag.off);
    /* a subject not allowed (its own self-test failed) is never tried */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    Stream s = {0};
    s.lag = 2;
    s.replays = 300;
    s.stepMs = 40.0;
    s.stage = 2;
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(s_a.trials == 0u && s_a.offTrial == 0u, "a subject not allowed was tried");
    /* one never in use either */
    rd_ab_allow(&s_a, 0, true);
    s.stage = 3;
    (void)run(&s_a, &s, 1e6, 0u);
    CHECK(s_a.trials == 0u, "a subject with no work was tried");
}

static void testStop(void)
{
    printf("the way back:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    (void)rd_ab_subject(&s_a, "fake");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    Stream s = {0};
    s.lag = 2;
    s.replays = 60;
    s.stepMs = 40.0;
    s.stage = 42;
    s.noise = 0.01;
    const double t = run(&s_a, &s, 0.0, 3u);
    CHECK(s_a.t.active, "no trial running before the loss");
    rd_ab_stop(&s_a, "the graphics device was lost");
    lines(&s_a);
    CHECK(strcmp(s_last, "A/B: every trial stopped for the run (the graphics device was lost): "
                         "each attempt at its off arm from here (zero-area cull, fake)") == 0,
          "%s", s_last);
    for (int i = 0; i < 100; i++) {
        const RdAbTag tag = rd_ab_begin(&s_a, i < 50 ? 42 : 43, t + 40.0 * i + 1e5, i % 7 == 0);
        CHECK(tag.off == 3u && tag.trial == 0u, "after the stop: off %x trial %u", tag.off,
              (unsigned)tag.trial);
    }
    CHECK(!s_a.t.active && s_a.stopped, "a trial after the stop");
    rd_ab_stop(&s_a, "again");
    CHECK(!rd_ab_take_line(&s_a, s_last, sizeof(s_last)), "the stop said twice");
    char st[1024];
    (void)rd_ab_start_text(&s_a, st, sizeof(st));
    CHECK(strstr(st, "stopped: the graphics device was lost") != NULL, "%s", st);
}

static void testModes(void)
{
    printf("the modes:\n");
    CHECK(rd_ab_mode_of(NULL) == RD_AB_AUTO && rd_ab_mode_of("on") == RD_AB_FIXED_ON &&
              rd_ab_mode_of("off") == RD_AB_FIXED_OFF && rd_ab_mode_of("alt") == RD_AB_ALT &&
              rd_ab_mode_of("x") == RD_AB_AUTO,
          "ICO_RD_AB's words");
    static const uint8_t kAlt[8] = {0, 1, 1, 0, 0, 1, 1, 0};
    for (int m = RD_AB_FIXED_ON; m <= RD_AB_ALT; m++) {
        rd_ab_init(&s_a, m);
        (void)rd_ab_subject(&s_a, "zero-area cull");
        rd_ab_allow(&s_a, 0, true);
        for (int i = 0; i < 8; i++) {
            /* no stage known (the replay tool): the mode alone */
            const RdAbTag tag = rd_ab_begin(&s_a, -1, 10000.0 * i, false);
            const uint8_t want = m == RD_AB_FIXED_ON ? 0u : m == RD_AB_FIXED_OFF ? 1u : kAlt[i];
            CHECK(tag.off == want && tag.trial == 0u, "mode %d replay %d: off %x", m, i, tag.off);
        }
        char st[512];
        (void)rd_ab_stats_text(&s_a, st, sizeof(st));
        printf("  %s\n", st);
        if (m == RD_AB_ALT) {
            CHECK(strcmp(st, "A/B (alt): zero-area cull: 4 replays with it, 4 without (0 trials, "
                             "0 stages on, 0 off)") == 0,
                  "%s", st);
        }
    }
    /* the judge with no stage known: never a trial (the replay tool's
       default is today's path) */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    Stream s = {0};
    s.lag = 2;
    s.replays = 300;
    s.stepMs = 40.0;
    s.stage = -1;
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(s_a.trials == 0u && s_a.subj[0].armOff == 0u, "a trial with no stage");
    char st[1024];
    (void)rd_ab_start_text(&s_a, st, sizeof(st));
    printf("  %s\n", st);
    CHECK(strstr(st, "A/B: the in-play judge; may try: zero-area cull; a trial 2.0 s into a "
                     "stage") == st,
          "%s", st);
    /* the registry's bound */
    for (int i = 1; i < RD_AB_SUBJECTS; i++) {
        CHECK(rd_ab_subject(&s_a, "x") == i, "id %d", i);
    }
    CHECK(rd_ab_subject(&s_a, "x") == -1, "past the registry");
    {
        char small[16];
        const size_t n = rd_ab_start_text(&s_a, small, sizeof(small));
        CHECK(n == strlen(small) && n < sizeof(small), "a short buffer: %zu", n);
    }
}

/* A 60 s GPU-bound stream at a 40 ms tick: picture i starts when the GPU is
   free and a tick it has not drawn exists, and takes its arm's time (base,
   the on arm's base * (1 + its stage's effect)); its GPU times come back
   two pictures later.  nStages stages of equal length (stage 10, 11, ...),
   eff[k] the effect in stage k.  Every picture kept: its start, stage, arm
   and whether it was drawn while its stage had no verdict of its own or in
   a trial (the time the judge is allowed to be on the worse arm) */
#define RATE_MS 60000.0
#define RATE_MAX 2048

typedef struct Pic {
    double start, end;
    int stage;
    bool off, unjudged;
} Pic;

typedef struct RateRun {
    Pic p[RATE_MAX];
    int n;                /* pictures presented (ended within the 60 s) */
    double lastVerdictMs; /* the first picture's start after the last stage's verdict, -1 none */
} RateRun;

static RateRun s_rr[3]; /* with it, without it, the judge */

static void rateRun(RateRun *rr, int mode, double baseMs, const double *eff, int nStages)
{
    rd_ab_init(&s_a, mode);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    Pend q[4];
    int qn = 0;
    double end = 0.0;
    long lastTick = -1;
    rr->n = 0;
    rr->lastVerdictMs = -1.0;
    for (;;) {
        double start = end;
        long tick = (long)floor(start / 40.0);
        if (tick <= lastTick) {
            tick = lastTick + 1;
            start = 40.0 * (double)tick;
        }
        lastTick = tick;
        if (qn >= 2) {
            const RdAbIn in = {q[0].tag, 1u, false, true, q[0].work, q[0].ms};
            rd_ab_collect(&s_a, &in);
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
            qn--;
        }
        int k = (int)(start * nStages / RATE_MS);
        k = k < nStages ? k : nStages - 1;
        const int stage = 10 + k;
        const int before = rd_ab_verdict(&s_a, 0, stage);
        if (mode == RD_AB_AUTO && k == nStages - 1 && rr->lastVerdictMs < 0.0 &&
            (before == RD_AB_V_ON || before == RD_AB_V_OFF)) {
            rr->lastVerdictMs = start;
        }
        const RdAbTag tag = rd_ab_begin(&s_a, stage, start, false);
        const bool off = (tag.off & 1u) != 0u;
        const double ms = baseMs * (off ? 1.0 : 1.0 + eff[k]);
        q[qn++] = (Pend){tag, 50000.0, ms, false};
        end = start + ms;
        if (end > RATE_MS || rr->n == RATE_MAX) {
            break;
        }
        rr->p[rr->n++] = (Pic){start, end, stage, off,
                               tag.trial != 0u || (before != RD_AB_V_ON && before != RD_AB_V_OFF)};
        char l[RD_AB_LINE];
        while (rd_ab_take_line(&s_a, l, sizeof(l))) {}
    }
}

/* pictures of run rr started in [from, to) (and presented) */
static int countIn(const RateRun *rr, double from, double to)
{
    int n = 0;
    for (int i = 0; i < rr->n; i++) {
        n += rr->p[i].start >= from && rr->p[i].start < to;
    }
    return n;
}

/* one case: the three runs, the bound from the two controls, the checks */
static int s_rateCases, s_rateShort;

static void rateCase(const char *kind, double baseMs, const double *eff, int nStages)
{
    rateRun(&s_rr[0], RD_AB_FIXED_ON, baseMs, eff, nStages);
    rateRun(&s_rr[1], RD_AB_FIXED_OFF, baseMs, eff, nStages);
    rateRun(&s_rr[2], RD_AB_AUTO, baseMs, eff, nStages);
    const RateRun *on = &s_rr[0], *off = &s_rr[1], *j = &s_rr[2];
    const double len = RATE_MS / nStages;
    /* per stage, from the controls: each arm's pictures a ms; the better
       arm's count (its sum: the best a judge could do, the better fixed
       arm's total where the effect is one sign) */
    double rOn[8], rOff[8];
    int best = 0;
    for (int k = 0; k < nStages; k++) {
        const int cOn = countIn(on, k * len, (k + 1) * len),
                  cOff = countIn(off, k * len, (k + 1) * len);
        rOn[k] = cOn / len;
        rOff[k] = cOff / len;
        best += cOn > cOff ? cOn : cOff;
    }
    const int worse = on->n < off->n ? on->n : off->n;
    /* the pictures the worse arm loses against the better during the time
       the judge was allowed on it: its pictures drawn on the arm the
       controls show slower for their stage, while the stage had no verdict
       of its own or in a trial, each for the time to the next picture */
    double lost = 0.0;
    for (int i = 0; i < j->n; i++) {
        const Pic *p = &j->p[i];
        const int k = p->stage - 10;
        const double rBest = rOn[k] > rOff[k] ? rOn[k] : rOff[k];
        const double rArm = p->off ? rOff[k] : rOn[k];
        if (!p->unjudged || rArm >= rBest) {
            continue;
        }
        const double span = (i + 1 < j->n ? j->p[i + 1].start : p->end) - p->start;
        lost += span * (rBest - rArm);
    }
    const int bound = (int)floor(best - lost) - 1;
    /* after the last verdict: the judge's pictures against the better arm's
       of the last stage over the same span */
    int after = -1, afterBest = -1;
    if (j->lastVerdictMs >= 0.0) {
        after = countIn(j, j->lastVerdictMs, RATE_MS);
        const int k = nStages - 1;
        afterBest = countIn(rOn[k] > rOff[k] ? on : off, j->lastVerdictMs, RATE_MS);
        if (rOn[k] == rOff[k]) {
            const int a2 = countIn(on, j->lastVerdictMs, RATE_MS),
                      b2 = countIn(off, j->lastVerdictMs, RATE_MS);
            afterBest = a2 > b2 ? a2 : b2;
        }
    }
    s_rateCases++;
    printf("  %-8s %4.1f ms, effects", kind, baseMs);
    for (int k = 0; k < nStages; k++) {
        printf(" %+.0f%%", eff[k] * 100.0);
    }
    printf(": with it %d, without %d, the judge %d; best per stage %d, bound %d (%.2f pictures on "
           "the worse arm while unjudged), short by %d; after the last verdict %d against %d\n",
           on->n, off->n, j->n, best, bound, lost, best - j->n > 0 ? best - j->n : 0, after,
           afterBest);
    const bool ok = j->n >= worse && j->n >= bound && after >= 0 && after + 1 >= afterBest;
    s_rateShort += !ok;
    CHECK(j->n >= worse, "%s %.1f ms: the judge %d under the worse fixed arm %d", kind, baseMs,
          j->n, worse);
    CHECK(j->n >= bound, "%s %.1f ms: the judge %d under its bound %d", kind, baseMs, j->n, bound);
    CHECK(after >= 0 && after + 1 >= afterBest && after <= afterBest + 1,
          "%s %.1f ms: after the last verdict %d against the better arm's %d", kind, baseMs, after,
          afterBest);
}

static void testRate(void)
{
    printf("the rate requirement (60 s, tick 40 ms; the bound from the controls of each case):\n");
    static const double kMs[4] = {33.0, 37.5, 48.0, 60.0};
    static const double kEffect[6] = {-0.10, -0.05, -0.03, 0.03, 0.05, 0.10};
    for (int i = 0; i < 4; i++) {
        for (int e = 0; e < 6; e++) {
            const double x = kEffect[e];
            const double one[1] = {x}, same[4] = {x, x, x, x}, flip[4] = {x, -x, x, -x};
            rateCase("1 stage", kMs[i], one, 1);
            rateCase("4 stages", kMs[i], same, 4);
            rateCase("flipping", kMs[i], flip, 4);
        }
    }
    printf("  %d cases, %d short of a bound\n", s_rateCases, s_rateShort);
}

/* the carried arm: a stage without a verdict of its own starts on the arm
   of the run's last verdict, its own trial still 2 s in, its verdict
   replacing it (a stage where the carried arm is the wrong one) */
static void testCarry(void)
{
    printf("the carried arm:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    Stream s = {0};
    s.lag = 2;
    s.replays = 200;
    s.stepMs = 40.0;
    /* stage 20: no verdict before it: with it; judged off */
    RdAbTag tag = rd_ab_begin(&s_a, 20, 0.0, false);
    CHECK(tag.off == 0u, "the run's first stage started without it");
    s.stage = 20;
    s.effect = 0.05;
    double t = run(&s_a, &s, 1.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 20) == RD_AB_V_OFF, "stage 20 not off");
    CHECK(strstr(s_last, ": off for this stage (it started with it, no verdict before it in the "
                         "run)") != NULL,
          "%s", s_last);
    /* stage 21, the same cost: starts without it at once, its trial still
       runs and agrees */
    tag = rd_ab_begin(&s_a, 21, t, false);
    CHECK(tag.off == 1u && tag.trial == 0u, "stage 21 did not start without it: off %x", tag.off);
    tag = rd_ab_begin(&s_a, 21, t + 1000.0, true);
    CHECK(tag.off == 1u, "a keep frame in stage 21 not on the carried arm");
    s.stage = 21;
    t = run(&s_a, &s, t + 1001.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 21) == RD_AB_V_OFF, "stage 21 not off");
    CHECK(strstr(s_last, "(it started without it, the arm stage 20's verdict left)") != NULL, "%s",
          s_last);
    /* stage 22 saves: the carried arm (without it) is the wrong one; its
       trial corrects it, and stage 23 starts with it */
    tag = rd_ab_begin(&s_a, 22, t, false);
    CHECK(tag.off == 1u, "stage 22 did not start on the carried arm");
    s.stage = 22;
    s.effect = -0.05;
    t = run(&s_a, &s, t + 1.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 22) == RD_AB_V_ON, "stage 22 not corrected to on");
    CHECK(strstr(s_last, ": on for this stage (it started without it, the arm stage 21's verdict "
                         "left)") != NULL,
          "%s", s_last);
    tag = rd_ab_begin(&s_a, 22, t, false);
    CHECK(tag.off == 0u && tag.trial == 0u, "stage 22 after its verdict: off %x", tag.off);
    tag = rd_ab_begin(&s_a, 23, t + 1.0, false);
    CHECK(tag.off == 0u, "stage 23 did not start with it (stage 22's verdict)");
    /* a judged stage keeps its own verdict whatever the carried arm says */
    tag = rd_ab_begin(&s_a, 20, t + 2.0, false);
    CHECK(tag.off == 1u && tag.trial == 0u, "stage 20 lost its own verdict");
    /* a stage whose trial ends with no verdict stays on the carried arm */
    s.stage = 24;
    s.effect = 0.0;
    s.workSwing = 0.15;
    s.replays = 800;
    t = run(&s_a, &s, t + 3.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 24) == RD_AB_V_KEPT, "stage 24: %d", rd_ab_verdict(&s_a, 0, 24));
    CHECK(strstr(s_last,
                 "no more trials here, it stays with it, the arm stage 22's verdict left") != NULL,
          "%s", s_last);
    tag = rd_ab_begin(&s_a, 24, t, false);
    CHECK(tag.off == 0u && tag.trial == 0u, "stage 24 after no verdict: off %x", tag.off);
}

/* the measure: the scene's GPU work (rd_ab_scene_ms), not the span with
   the present's pass and the display wait.  A stream held by the display
   (every span 40 ms: the wait takes up what the scene saves) shows no
   difference in the span, the 5 % saving in the scene's work */
static void testMeasure(void)
{
    printf("the measure:\n");
    CHECK(fabs(rd_ab_scene_ms(40.0, 6.0, 3.0) - 31.0) < 1e-9, "40 - 6 - 3");
    CHECK(rd_ab_scene_ms(5.0, 6.0, 3.0) == 0.0, "nothing left");
    CHECK(fabs(rd_ab_scene_ms(40.0, -1.0, 0.0) - 40.0) < 1e-9, "no wait, no present");
    for (int whole = 0; whole < 2; whole++) {
        rd_ab_init(&s_a, RD_AB_AUTO);
        (void)rd_ab_subject(&s_a, "zero-area cull");
        rd_ab_allow(&s_a, 0, true);
        Pend q[4];
        int qn = 0;
        for (int i = 0; i < 400; i++) {
            if (qn >= 2) {
                const RdAbIn in = {q[0].tag, 1u, false, true, q[0].work, q[0].ms};
                rd_ab_collect(&s_a, &in);
                memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
                qn--;
            }
            const RdAbTag tag = rd_ab_begin(&s_a, 50, 40.0 * i, false);
            const double scene = 30.0 * ((tag.off & 1u) ? 1.0 : 0.95);
            const double present = 3.0, span = 40.0;
            const double wait = span - present - scene;
            q[qn++] =
                (Pend){tag, 50000.0, whole ? span : rd_ab_scene_ms(span, wait, present), false};
            lines(&s_a);
        }
        const int v = rd_ab_verdict(&s_a, 0, 50);
        if (whole) {
            CHECK(v == RD_AB_V_OFF, "the whole span: the saving seen (%d)", v);
        } else {
            CHECK(v == RD_AB_V_ON, "the scene's work: the saving not seen (%d)", v);
            CHECK(strstr(s_last, "ms of the scene's GPU work (the present and its display wait "
                                 "left out) a thousand vertices and triangles") != NULL,
                  "%s", s_last);
        }
    }
}

/* a renderer made again (rd_ab_forget): a trial running ends unjudged,
   every verdict and the carried arm forgotten, the stage begins again, a
   record of the earlier renderer not measured, the trials run still
   counted */
static void testForget(void)
{
    printf("a renderer made again:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    Stream s = {0};
    s.lag = 2;
    s.replays = 200;
    s.stepMs = 40.0;
    s.stage = 30;
    s.effect = 0.05;
    double t = run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 30) == RD_AB_V_OFF, "stage 30 not off");
    /* a trial past 16 pairs in stage 31 (a 1 % difference: no early verdict) */
    s.stage = 31;
    s.effect = 0.01;
    s.replays = 40 + 50; /* 2 s, then 25 pairs */
    t = run(&s_a, &s, t, 1u);
    CHECK(s_a.t.active && s_a.t.pairs >= RD_AB_PAIRS_MIN, "no trial past 16 pairs: %u",
          (unsigned)s_a.t.pairs);
    const RdAbTag old = rd_ab_begin(&s_a, 31, t, false);
    const uint32_t trials = s_a.trials;
    rd_ab_forget(&s_a, "a Graphics API switch");
    char l[RD_AB_LINE], first[RD_AB_LINE] = "";
    int n = 0;
    while (rd_ab_take_line(&s_a, l, sizeof(l))) {
        printf("  %s\n", l);
        if (n++ == 0) {
            snprintf(first, sizeof(first), "%s", l);
        }
        snprintf(s_last, sizeof(s_last), "%s", l);
    }
    CHECK(strstr(first, "stage 31: no verdict (the renderer was made again, ") != NULL, "%s",
          first);
    CHECK(strstr(s_last, "A/B: the renderer was made again (a Graphics API switch): every verdict "
                         "and the carried arm forgotten") == s_last,
          "%s", s_last);
    CHECK(rd_ab_verdict(&s_a, 0, 30) == RD_AB_V_NONE && rd_ab_verdict(&s_a, 0, 31) == RD_AB_V_NONE,
          "a verdict kept across the switch");
    CHECK(s_a.trials == trials && !s_a.t.active, "the trials' count or the trial");
    /* the earlier renderer's record read back now: not measured (no use
       seen, no span kept) */
    const RdAbIn in = {old, 1u, false, true, 50000.0, 40.0};
    rd_ab_collect(&s_a, &in);
    RdAbTag tag = rd_ab_begin(&s_a, 30, t + 1.0, false);
    CHECK(tag.off == 0u && tag.trial == 0u, "stage 30 after the switch: off %x", tag.off);
    rd_ab_collect(&s_a, &in);
    tag = rd_ab_begin(&s_a, 30, t + 1.0 + RD_AB_START_MS + 1.0, false);
    CHECK(tag.trial == 0u && s_a.usedNow == 0u, "the earlier renderer's record counted");
    /* this renderer's own: stage 30 judged again */
    s.stage = 30;
    s.effect = 0.05;
    s.replays = 200;
    (void)run(&s_a, &s, t + 5000.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 30) == RD_AB_V_OFF && s_a.trials == trials + 1u,
          "stage 30 not judged again on the new renderer");
    CHECK(strstr(s_last, "(it started with it, no verdict before it in the run)") != NULL, "%s",
          s_last);
}

/* the player's notice (rd_ab_notice_state) over a run: none before a
   trial; running during each trial and idle between (two subjects back to
   back: idle for one replay at most), a renderer made again idle; done when
   the 12th trial ends, on a stop, and once every subject is off for the
   run; never running without a
   trial (nothing allowed, no stage, the tests' fixed and alternating modes,
   a stop before any trial) */
static void testNotice(void)
{
    printf("the player's notice:\n");
    Stream s = {0};
    s.lag = 2;
    s.replays = 40;
    s.stepMs = 40.0;
    s.stage = 60;
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    (void)rd_ab_subject(&s_a, "fake");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE, "a notice before anything");
    double t = run(&s_a, &s, 0.0, 3u);
    CHECK(s_a.trials == 0u && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE,
          "a notice before any trial");
    /* the first trial's start: running; both subjects back to back, one
       replay at a time: running from the first trial's start to the
       second's verdict with no idle between them (the second starts at the
       same replay the first one's verdict is read at), idle after */
    s.replays = 1;
    s.effect = 0.05;
    t = run(&s_a, &s, t + RD_AB_START_MS, 3u);
    CHECK(s_a.trials == 1u && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_RUNNING,
          "not running at the first trial (%u trials, %d)", (unsigned)s_a.trials,
          rd_ab_notice_state(&s_a));
    int idleRun = 0, maxIdle = 0, sawSecond = 0;
    for (int i = 0; i < 800; i++) {
        t = run(&s_a, &s, t + 1.0, 3u);
        const int st = rd_ab_notice_state(&s_a);
        idleRun = s_a.trials == 1u && st == RD_AB_NOTICE_IDLE ? idleRun + 1 : 0;
        maxIdle = idleRun > maxIdle ? idleRun : maxIdle;
        sawSecond = sawSecond || s_a.trials == 2u;
        if (sawSecond && st == RD_AB_NOTICE_IDLE) {
            break;
        }
    }
    /* each call here begins a replay, then reads the replays in flight
       back (as rd__perf_begin, then rd__perf_collect_gpu): idle for the one
       replay between a verdict and the next subject's trial */
    CHECK(sawSecond && maxIdle <= 1 && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_IDLE,
          "back to back: second %d, idle replays between %d, then %d", sawSecond, maxIdle,
          rd_ab_notice_state(&s_a));
    /* later stages: running during each trial, idle after its verdict;
       a renderer made again: idle */
    s.replays = 400;
    for (int k = 0; k < 4; k++) {
        s.stage = 61 + k;
        s.replays = 60; /* the start's 2 s and the trial's beginning */
        t = run(&s_a, &s, t + 1.0, 3u);
        CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_RUNNING, "stage %d: not running (%d)",
              s.stage, rd_ab_notice_state(&s_a));
        s.replays = 400;
        t = run(&s_a, &s, t + 1.0, 3u);
        CHECK(s_a.trials < RD_AB_TRIALS && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_IDLE,
              "stage %d: %u trials, %d", s.stage, (unsigned)s_a.trials, rd_ab_notice_state(&s_a));
    }
    rd_ab_forget(&s_a, "a Graphics API switch");
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_IDLE, "the renderer made again: %d",
          rd_ab_notice_state(&s_a));
    /* stages on until the 12th trial: running while it runs, done when it
       ends */
    int sawDueAtCap = 0;
    for (int k = 0; k < 12 && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_IDLE; k++) {
        s.stage = 70 + k;
        s.replays = 1;
        for (int i = 0; i < 460; i++) {
            t = run(&s_a, &s, t + 1.0, 3u);
            sawDueAtCap = sawDueAtCap || (s_a.trials == RD_AB_TRIALS && s_a.t.active &&
                                          rd_ab_notice_state(&s_a) == RD_AB_NOTICE_RUNNING);
        }
    }
    CHECK(sawDueAtCap, "not running while the 12th trial ran");
    CHECK(s_a.trials == RD_AB_TRIALS && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_CAP,
          "done at the cap: %u trials, %d", (unsigned)s_a.trials, rd_ab_notice_state(&s_a));
    rd_ab_forget(&s_a, "a Graphics API switch");
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_CAP, "the cap's done undone");
    /* a stop mid-run: done, and stays */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    s.stage = 80;
    s.replays = 60;
    t = run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_RUNNING, "not running before the stop");
    rd_ab_stop(&s_a, "the graphics device was lost");
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_STOP, "a stop: %d",
          rd_ab_notice_state(&s_a));
    rd_ab_forget(&s_a, "a Graphics API switch");
    (void)run(&s_a, &s, t + 1.0, 1u);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_STOP, "the stop's done undone");
    /* every subject off for the run: done; one of two: still due */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    (void)rd_ab_subject(&s_a, "fake");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    s.stage = 81;
    t = run(&s_a, &s, 0.0, 3u);
    rd_ab_allow(&s_a, 0, false);
    rd_ab_gone(&s_a, 0, true);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_RUNNING ||
              rd_ab_notice_state(&s_a) == RD_AB_NOTICE_IDLE,
          "one subject gone: %d", rd_ab_notice_state(&s_a));
    rd_ab_allow(&s_a, 1, false);
    rd_ab_gone(&s_a, 1, true);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_GONE, "both gone: %d",
          rd_ab_notice_state(&s_a));
    rd_ab_gone(&s_a, 1, false);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_DONE_GONE, "a gone subject came back");
    /* a gone subject is never tried */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_gone(&s_a, 0, true);
    s.stage = 82;
    s.replays = 200;
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(s_a.trials == 0u && rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE,
          "a gone subject tried (%u trials)", (unsigned)s_a.trials);
    /* never without a trial: nothing allowed, no stage, the tests' modes, a
       stop before any trial */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    s.stage = 83;
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE, "due with nothing allowed");
    rd_ab_allow(&s_a, 0, true);
    s.stage = -1;
    (void)run(&s_a, &s, 1e6, 1u);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE, "due with no stage");
    for (int m = RD_AB_FIXED_ON; m <= RD_AB_ALT; m++) {
        rd_ab_init(&s_a, m);
        (void)rd_ab_subject(&s_a, "zero-area cull");
        rd_ab_allow(&s_a, 0, true);
        s.stage = 84;
        (void)run(&s_a, &s, 0.0, 1u);
        CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE, "due in mode %d", m);
    }
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_stop(&s_a, "the graphics device was lost");
    s.stage = 85;
    (void)run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_notice_state(&s_a) == RD_AB_NOTICE_NONE, "due after a stop before any trial");
}

int main(void)
{
    testNotice();
    testMeasure();
    testForget();
    testVerdicts();
    testCost();
    testStages();
    testTwoSubjects();
    testStop();
    testModes();
    testCarry();
    testRate();
    if (failures) {
        printf("rd_ab: %d failures\n", failures);
        return 1;
    }
    printf("rd_ab: all passed\n");
    return 0;
}
