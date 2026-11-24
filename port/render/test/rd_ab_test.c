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
 *   - once a run, in scenery: no trial on the title or the logo, the first
 *     scenery stage's trial, its verdict for every later stage;
 *   - the boot logo and title's stage is never scenery, on the scene layout
 *     too (rd_ab_scenery_of; issue 179's v0.11.15 log: the cull judged
 *     there); the scenery ending mid-trial ends it with no verdict, timed
 *     again 2 s into the next stretch of scenery;
 *   - the twins' camera rule class, a subject of its own: tried only where
 *     its own passes had work, its verdict independent of the twins' (each
 *     stops its own class);
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

static void lines(RdAb *a)
{
    char l[RD_AB_LINE];
    while (rd_ab_take_line(a, l, sizeof(l))) {
        printf("  %s\n", l);
        snprintf(s_last, sizeof(s_last), "%s", l);
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
              strstr(s_last,
                     "cheaper with it, 8 of 8 cheaper with it), 0 dropped by a long "
                     "picture, 0 by unequal work: on for the rest of the run, every stage") != NULL,
          "the verdict line: %s", s_last);
    /* the cost: the early verdict's replays without it (8 pairs: 8) and the
       ones begun before it came back (the lag) */
    CHECK(s_a.offTrial >= RD_AB_EARLY && s_a.offTrial <= RD_AB_EARLY + 2u,
          "the trial's replays without it: %llu", (unsigned long long)s_a.offTrial);
    printf("3 %% dearer with it:\n");
    s.effect = 0.03;
    CHECK(verdictOf(&s, &s_a) == RD_AB_V_OFF, "3 %% dearer: not off");
    CHECK(strstr(s_last, "dearer with it, 8 of 8 cheaper without)") != NULL &&
              strstr(s_last, ": off for the rest of the run, every stage") != NULL,
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
              strstr(s_last, "with it until it is timed again in another stage") != NULL,
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
    /* no trial before 2 s into a stage, nor before the subject had work */
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
    /* the stage ends mid-trial: no answer, timed again in the next stage */
    tag = rd_ab_begin(&s_a, 4, RD_AB_START_MS + 7.0, false);
    lines(&s_a);
    CHECK(strstr(s_last, "stage 3: no verdict (the stage ended, 0 pairs;") != NULL &&
              strstr(s_last, "with it until it is timed again in the next stage of play") != NULL,
          "%s", s_last);
    CHECK(rd_ab_verdict(&s_a, 0, 3) == RD_AB_V_NONE, "the ended stage judged");
    /* stage 5 gives the run's one verdict (off); stage 6, where it would
       save, is not timed: the run's arm, at once and later; stage 5 again
       the same */
    Stream s = {0};
    s.lag = 2;
    s.replays = 200;
    s.stepMs = 40.0;
    s.stage = 5;
    s.effect = 0.04;
    double t = run(&s_a, &s, 100000.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 5) == RD_AB_V_OFF, "stage 5 not off");
    CHECK(strstr(s_last, ": off for the rest of the run, every stage") != NULL, "%s", s_last);
    const uint32_t trials = s_a.trials;
    s.stage = 6;
    s.effect = -0.04;
    t = run(&s_a, &s, t, 1u);
    CHECK(s_a.trials == trials && rd_ab_verdict(&s_a, 0, 6) == RD_AB_V_OFF,
          "stage 6 timed again (%u trials) or not on the run's arm", (unsigned)s_a.trials);
    tag = rd_ab_begin(&s_a, 6, t, false);
    CHECK(tag.off == 1u && tag.trial == 0u, "stage 6: off %x trial %u", tag.off,
          (unsigned)tag.trial);
    tag = rd_ab_begin(&s_a, 5, t + 10000.0, false);
    CHECK(tag.off == 1u && tag.trial == 0u && s_a.trials == trials, "stage 5 tried again");
    /* the cap: a subject with no answer is tried in each next stage; every
       trial here ends with its stage (no verdict), so stages 100.. take
       trials until RD_AB_TRIALS ran, then none */
    printf("the cap:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    s.replays = 60; /* the 2 s and a few trial replays: the stage ends first */
    s.effect = 0.04;
    t = 0.0;
    for (int k = 0; k < 16; k++) {
        s.stage = 100 + k;
        t = run(&s_a, &s, t + 1.0, 1u);
    }
    (void)rd_ab_begin(&s_a, 200, t + 1.0, false); /* the last stage ends */
    lines(&s_a);
    CHECK(s_a.trials == RD_AB_TRIALS, "%u trials", (unsigned)s_a.trials);
    int judged = 0;
    for (int k = 0; k < 16; k++) {
        judged += rd_ab_verdict(&s_a, 0, 100 + k) != RD_AB_V_NONE;
    }
    CHECK(judged == 0, "%d stages with an answer", judged);
    CHECK(s_a.capSaid && strstr(s_last, "an attempt not timed yet stays with it") != NULL,
          "the cap's line not said: %s", s_last);
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

/* three subjects, as the renderer registers them (rd_ab_run.c: the cull,
   the twins, the fast pixel build): one trial at a time in that order, one
   verdict each for the run, the others at their default (with it) during a
   trial, then no further trial: every subject judged */
static void testThree(void)
{
    printf("three subjects:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    CHECK(rd_ab_subject(&s_a, "zero-area cull") == 0, "id 0");
    CHECK(rd_ab_subject(&s_a, "discard-free twins") == 1, "id 1");
    CHECK(rd_ab_subject(&s_a, "fast pixel build") == 2, "id 2");
    for (int i = 0; i < 3; i++) {
        rd_ab_allow(&s_a, i, true);
    }
    rd_ab_set_scenery(&s_a, true);
    /* the cull saves 5 %, the twins cost 5 %, the fast build saves 4 % */
    static const double kEffect[3] = {-0.05, 0.05, -0.04};
    double t = 0.0;
    Pend q[4];
    int qn = 0;
    int order[8], no = 0;
    bool others = true;
    for (int i = 0; i < 1600; i++) {
        if (qn >= 2) {
            const RdAbIn in = {q[0].tag, 7u, false, true, q[0].work, q[0].ms};
            rd_ab_collect(&s_a, &in);
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
            qn--;
        }
        const RdAbTag tag = rd_ab_begin(&s_a, 41, t, false);
        if (s_a.t.active) {
            const int sub = s_a.t.subject;
            if (no == 0 || order[no - 1] != sub) {
                if (no < 8) {
                    order[no++] = sub;
                }
            }
            /* a subject not on trial and not judged yet keeps its default */
            for (int k = 0; k < 3; k++) {
                if (k != sub && s_a.subj[k].carry == RD_AB_V_NONE && (tag.off & (1u << k))) {
                    others = false;
                }
            }
        }
        double ms = 40.0;
        for (int k = 0; k < 3; k++) {
            ms *= (tag.off & (1u << k)) ? 1.0 : 1.0 + kEffect[k];
        }
        q[qn++] = (Pend){tag, 50000.0, ms, false};
        t += 40.0;
        lines(&s_a);
    }
    CHECK(no == 3 && order[0] == 0 && order[1] == 1 && order[2] == 2,
          "the trials' order (%d trials: %d %d %d)", no, no > 0 ? order[0] : -1,
          no > 1 ? order[1] : -1, no > 2 ? order[2] : -1);
    CHECK(others, "a subject not on trial was off before its verdict");
    CHECK(rd_ab_verdict(&s_a, 0, 41) == RD_AB_V_ON, "the cull not on");
    CHECK(rd_ab_verdict(&s_a, 1, 41) == RD_AB_V_OFF, "the twins not off");
    CHECK(rd_ab_verdict(&s_a, 2, 41) == RD_AB_V_ON, "the fast pixel build not on");
    CHECK(s_a.trials == 3u && s_a.subj[2].trials == 1u && !s_a.t.active,
          "%u trials, the fast build's %u, running %d", (unsigned)s_a.trials,
          (unsigned)s_a.subj[2].trials, (int)s_a.t.active);
    /* the next stage: every subject on the run's verdict, no trial */
    t += 100000.0;
    for (int i = 0; i < 300; i++) {
        const RdAbTag tag = rd_ab_begin(&s_a, 42, t, false);
        CHECK(tag.off == 2u && tag.trial == 0u, "stage 42: off %x trial %u", tag.off,
              (unsigned)tag.trial);
        if (tag.off != 2u || tag.trial != 0u) {
            break;
        }
        const RdAbIn in = {tag, 7u, false, true, 50000.0, 40.0};
        rd_ab_collect(&s_a, &in);
        t += 40.0;
    }
    CHECK(s_a.trials == 3u, "a trial after every subject was judged (%u)", (unsigned)s_a.trials);
}

/* a replay's use (rd_ab_use_note, REV4R S1): a pass that drew through a
   build only because a pipeline was not made yet (any subject's) takes the
   replay from every subject, on either arm, for the rest of the replay; the
   old rule (each subject's own bit alone) would keep the twins' bit set when
   the fast build's pipeline was not made on the twins' off arm */
static void testUseNote(void)
{
    printf("a replay's use:\n");
    bool tainted = false;
    uint8_t u = 0;
    u = rd_ab_use_note(u, &tainted, 0, RD_AB_NOTE_USED);
    u = rd_ab_use_note(u, &tainted, 1, RD_AB_NOTE_USED);
    CHECK(u == 3u && !tainted, "used 0 and 1: %x", u);
    u = rd_ab_use_note(u, &tainted, 0, RD_AB_NOTE_UNUSED);
    CHECK(u == 2u, "unused 0: %x", u);
    /* the twins' off arm: the fast build's key not made yet */
    u = rd_ab_use_note(u, &tainted, 2, RD_AB_NOTE_UNMADE);
    CHECK(u == 0u && tainted, "after an unmade pipeline: %x", u);
    /* a later pass of the same replay cannot put a subject back */
    u = rd_ab_use_note(u, &tainted, 1, RD_AB_NOTE_USED);
    u = rd_ab_use_note(u, &tainted, -1, RD_AB_NOTE_USED);
    CHECK(u == 0u, "a subject in use again in a tainted replay: %x", u);
    /* the next replay starts clean */
    tainted = false;
    u = rd_ab_use_note(0u, &tainted, 1, RD_AB_NOTE_USED);
    CHECK(u == 2u, "the next replay: %x", u);
    /* a tainted replay's record is timed for no subject: collected with
       used 0 it adds no pair to a trial (rd_ab_collect's own rule) */
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "discard-free twins");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_set_scenery(&s_a, true);
    bool ran = false;
    for (int i = 0; i < 400; i++) {
        const RdAbTag tag = rd_ab_begin(&s_a, 9, 40.0 * i, false);
        ran = ran || s_a.t.active;
        tainted = false;
        uint8_t used = rd_ab_use_note(0u, &tainted, 0, RD_AB_NOTE_USED);
        if (tag.off & 1u) {
            /* the off arm 50 % slower, every one with an unmade pipeline */
            used = rd_ab_use_note(used, &tainted, 2, RD_AB_NOTE_UNMADE);
        }
        const RdAbIn in = {tag, used, false, true, 50000.0, (tag.off & 1u) ? 60.0 : 40.0};
        rd_ab_collect(&s_a, &in);
        lines(&s_a);
    }
    CHECK(ran, "no trial ran");
    CHECK(rd_ab_verdict(&s_a, 0, 9) == RD_AB_V_NONE,
          "a verdict from off-arm replays with an unmade pipeline: %d", rd_ab_verdict(&s_a, 0, 9));
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
    CHECK(strstr(st, "A/B: the in-play judge; may try: zero-area cull; each attempt timed once a "
                     "run, in the first stage of play or a cutscene") == st,
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
    double lastVerdictMs; /* the first picture's start after the run's verdict, -1 none */
    int verdict;          /* that verdict (RD_AB_V_ON, RD_AB_V_OFF) */
} RateRun;

static RateRun s_rr[3]; /* with it, without it, the judge */

static void rateRun(RateRun *rr, int mode, double baseMs, const double *eff, int nStages)
{
    rd_ab_init(&s_a, mode);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    /* the registry full, as the renderer's (rd_ab_run.c): the others may be
       tried but have no work in this stream */
    for (int i = 1; i < RD_AB_SUBJECTS; i++) {
        CHECK(rd_ab_subject(&s_a, "another") == i, "subject %d", i);
        rd_ab_allow(&s_a, i, true);
    }
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
        if (mode == RD_AB_AUTO && rr->lastVerdictMs < 0.0 &&
            (before == RD_AB_V_ON || before == RD_AB_V_OFF)) {
            rr->lastVerdictMs = start; /* the run's one verdict known */
            rr->verdict = before;
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

static void rateCase(const char *kind, double baseMs, const double *eff, int nStages, int flips)
{
    rateRun(&s_rr[0], RD_AB_FIXED_ON, baseMs, eff, nStages);
    rateRun(&s_rr[1], RD_AB_FIXED_OFF, baseMs, eff, nStages);
    rateRun(&s_rr[2], RD_AB_AUTO, baseMs, eff, nStages);
    const RateRun *on = &s_rr[0], *off = &s_rr[1], *j = &s_rr[2];
    const double len = RATE_MS / nStages;
    /* per stage, from the controls: each arm's pictures a ms */
    double rOn[8], rOff[8];
    for (int k = 0; k < nStages; k++) {
        rOn[k] = countIn(on, k * len, (k + 1) * len) / len;
        rOff[k] = countIn(off, k * len, (k + 1) * len) / len;
    }
    /* the rule's target (one verdict a run, timed in the first stage): the
       arm the controls show faster in the first stage, kept for every
       stage; where both arms present alike there (a tick-bound stage) the
       arm the judge chose, which the rule cannot tell from the other by
       pictures.  Its pictures over the 60 s are "best": for a stream of one
       sign the better fixed arm's total */
    const int targetOff = rOff[0] > rOn[0] ? 1 : rOn[0] > rOff[0] ? 0 : j->verdict == RD_AB_V_OFF;
    const RateRun *target = targetOff ? off : on;
    const int best = target->n;
    const int worse = on->n < off->n ? on->n : off->n;
    CHECK(j->lastVerdictMs < 0.0 || (j->verdict == RD_AB_V_OFF) == (targetOff != 0),
          "%s %.1f ms: the verdict %d is not the first stage's faster arm", kind, baseMs,
          j->verdict);
    /* the pictures the judge's undecided time cost against the target:
       its pictures on the other arm while the run had no verdict or in a
       trial, each for the time to the next picture, at the two controls'
       rates for its stage */
    double lost = 0.0;
    for (int i = 0; i < j->n; i++) {
        const Pic *p = &j->p[i];
        const int k = p->stage - 10;
        const double rT = targetOff ? rOff[k] : rOn[k];
        const double rArm = p->off ? rOff[k] : rOn[k];
        if (!p->unjudged || (p->off != 0) == (targetOff != 0) || rArm >= rT) {
            continue;
        }
        const double span = (i + 1 < j->n ? j->p[i + 1].start : p->end) - p->start;
        lost += span * (rT - rArm);
    }
    const int bound = (int)floor(best - lost) - 1;
    /* after the verdict: the judge's pictures against the target's over
       the same span */
    int after = -1, afterBest = -1;
    if (j->lastVerdictMs >= 0.0) {
        after = countIn(j, j->lastVerdictMs, RATE_MS);
        afterBest = countIn(target, j->lastVerdictMs, RATE_MS);
    }
    s_rateCases++;
    printf("  %-8s %4.1f ms, effects", kind, baseMs);
    for (int k = 0; k < nStages; k++) {
        printf(" %+.0f%%", eff[k] * 100.0);
    }
    printf(
        ": with it %d, without %d, the judge %d; the first stage's faster arm %d, bound %d (%.2f pictures on "
        "the other arm while undecided), short by %d; after the verdict %d against %d\n",
        on->n, off->n, j->n, best, bound, lost, best - j->n > 0 ? best - j->n : 0, after,
        afterBest);
    /* (a) at least the worse fixed arm: for streams of one sign.  A stream
       whose effect changes sign between stages makes the two fixed arms all
       but equal over the 60 s, and the one-verdict rule keeps the first
       stage's faster arm everywhere, which there may be the worse fixed arm
       less its own trial's cost: (b) against that arm holds it instead */
    const bool aOk = flips || j->n >= worse;
    const bool ok = aOk && j->n >= bound && after >= 0 && after + 1 >= afterBest;
    s_rateShort += !ok;
    CHECK(aOk, "%s %.1f ms: the judge %d under the worse fixed arm %d", kind, baseMs, j->n, worse);
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
            rateCase("1 stage", kMs[i], one, 1, 0);
            rateCase("4 stages", kMs[i], same, 4, 0);
            rateCase("flipping", kMs[i], flip, 4, 1);
        }
    }
    printf("  %d cases, %d short of a bound\n", s_rateCases, s_rateShort);
}

/* the one-verdict rule over scenery: no trial while the replays are not
   scenery (the title, the logo, a menu), however long; the 2 s counted from
   the scenery's start; the trial at the first stage of scenery; its verdict
   for every later stage; a second subject with work only later: its own
   one trial there; done once both have theirs */
static void testOnce(void)
{
    printf("once a run, in scenery:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    (void)rd_ab_subject(&s_a, "fake");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    Stream s = {0};
    s.lag = 2;
    s.stepMs = 40.0;
    s.effect = 0.05;
    /* the logo and the title (stage 1): 20 s, not scenery */
    rd_ab_set_scenery(&s_a, false);
    s.stage = 1;
    s.replays = 500;
    double t = run(&s_a, &s, 0.0, 1u);
    CHECK(s_a.trials == 0u, "a trial on the title: %u", (unsigned)s_a.trials);
    /* the opening's cutscene (stage 41): scenery; no trial in its first
       2 s of scenery, the cull's at them; the fake subject has no work here */
    s.stage = 41;
    s.replays = 10;
    t = run(&s_a, &s, t + 1.0, 1u); /* the stage's first pictures, still the title's layout */
    rd_ab_set_scenery(&s_a, true);
    s.replays = 45; /* 1.8 s of scenery */
    t = run(&s_a, &s, t + 1.0, 1u);
    CHECK(s_a.trials == 0u, "a trial before 2 s of scenery (%u)", (unsigned)s_a.trials);
    s.replays = 400;
    t = run(&s_a, &s, t + 1.0, 1u);
    CHECK(s_a.trials == 1u && rd_ab_verdict(&s_a, 0, 41) == RD_AB_V_OFF &&
              rd_ab_verdict(&s_a, 1, 41) == RD_AB_V_NONE && !s_a.t.active,
          "the opening: %u trials, cull %d, fake %d, running %d", (unsigned)s_a.trials,
          rd_ab_verdict(&s_a, 0, 41), rd_ab_verdict(&s_a, 1, 41), (int)s_a.t.active);
    /* the boat (stage 42), where it would save: the run's arm, no trial */
    s.stage = 42;
    s.effect = -0.05;
    t = run(&s_a, &s, t + 1.0, 1u);
    const RdAbTag tag = rd_ab_begin(&s_a, 42, t, false);
    CHECK(s_a.trials == 1u && (tag.off & 1u) && rd_ab_verdict(&s_a, 0, 42) == RD_AB_V_OFF,
          "the boat timed again or not on the run's arm (%u trials, off %x)", (unsigned)s_a.trials,
          tag.off);
    /* a later stage where the fake subject has work: its one trial; then
       both judged: done */
    s.stage = 43;
    s.effect = 0.05;
    t = run(&s_a, &s, t + 1.0, 3u);
    CHECK(s_a.trials == 2u && s_a.subj[1].trials == 1u &&
              rd_ab_verdict(&s_a, 1, 43) == RD_AB_V_OFF && !s_a.t.active,
          "the fake subject: %u trials, verdict %d, running %d", (unsigned)s_a.trials,
          rd_ab_verdict(&s_a, 1, 43), (int)s_a.t.active);
    s.stage = 44;
    (void)run(&s_a, &s, t + 1.0, 3u);
    CHECK(s_a.trials == 2u, "a trial after both were judged (%u)", (unsigned)s_a.trials);
    /* a renderer made again: both forgotten, each timed once more */
    rd_ab_forget(&s_a, "a Graphics API switch");
    CHECK(!s_a.t.active && s_a.subj[0].carry == RD_AB_V_NONE && s_a.subj[1].carry == RD_AB_V_NONE,
          "after the switch: running %d, carried %d %d", (int)s_a.t.active, s_a.subj[0].carry,
          s_a.subj[1].carry);
    s.stage = 45;
    (void)run(&s_a, &s, t + 100000.0, 3u);
    CHECK(s_a.trials == 4u && !s_a.t.active && s_a.subj[0].carry != RD_AB_V_NONE &&
              s_a.subj[1].carry != RD_AB_V_NONE,
          "after the switch: %u trials, running %d", (unsigned)s_a.trials, (int)s_a.t.active);
}

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
   every verdict forgotten, the stage begins again, a
   record of the earlier renderer not measured, the trials run still
   counted */
static void testForget(void)
{
    printf("a renderer made again:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    (void)rd_ab_subject(&s_a, "fake");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_allow(&s_a, 1, true);
    Stream s = {0};
    s.lag = 2;
    s.replays = 200;
    s.stepMs = 40.0;
    s.stage = 30;
    s.effect = 0.05;
    double t = run(&s_a, &s, 0.0, 1u);
    CHECK(rd_ab_verdict(&s_a, 0, 30) == RD_AB_V_OFF, "stage 30 not off");
    /* the fake subject's trial (work in stage 31 alone; its arms change
       nothing: no early verdict) past 16 pairs */
    s.stage = 31;
    s.replays = 40 + 50; /* 2 s, then 25 pairs */
    t = run(&s_a, &s, t, 2u);
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
                         "forgotten") == s_last,
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
    CHECK(strstr(s_last, ": off for the rest of the run, every stage") != NULL, "%s", s_last);
}

/* the boot logo and title's stage (region.h stage_title, 1 on every disc):
   never scenery, whatever its layout.  Issue 179's v0.11.15 log: with Skip
   the opening on, stage 1 sat on the scene layout (55) for about 2.07 s and
   the cull was judged there (its line 630).  The renderer's word for that
   stretch, as window_perf_rank.c computes it, keeps every trial out; the
   stream of that stretch with the word the window gave before (the scene
   layout alone: scenery) starts one, which is the case refused */
static void testTitleStage(void)
{
    printf("the boot logo and title's stage:\n");
    CHECK(!rd_ab_scenery_of(true, 1, 1), "the title stage's scene layout taken as scenery");
    CHECK(!rd_ab_scenery_of(false, 1, 1), "the title stage's title layout taken as scenery");
    CHECK(rd_ab_scenery_of(true, 41, 1) && rd_ab_scenery_of(true, 42, 1),
          "the opening or the boat not scenery");
    CHECK(!rd_ab_scenery_of(false, 42, 1), "a menu in play taken as scenery");
    CHECK(!rd_ab_scenery_of(true, -1, 1), "no stage yet taken as scenery");
    CHECK(rd_ab_scenery_of(true, 1, 2) && !rd_ab_scenery_of(true, 2, 2),
          "the title stage not read from its argument");
    for (int before = 0; before < 2; before++) {
        rd_ab_init(&s_a, RD_AB_AUTO);
        (void)rd_ab_subject(&s_a, "zero-area cull");
        rd_ab_allow(&s_a, 0, true);
        /* stage 1 on the scene layout: 4 s (past the log's 2.07 s) */
        rd_ab_set_scenery(&s_a, before ? true : rd_ab_scenery_of(true, 1, 1));
        Stream s = {0};
        s.lag = 2;
        s.stepMs = 40.0;
        s.effect = 0.05;
        s.stage = 1;
        s.replays = 100;
        (void)run(&s_a, &s, 0.0, 1u);
        if (before) {
            /* the word the window gave before: the trial starts there */
            CHECK(s_a.trials == 1u, "the old word started no trial (%u): the case is not the log's",
                  (unsigned)s_a.trials);
        } else {
            CHECK(s_a.trials == 0u && s_a.offTrial == 0u &&
                      rd_ab_verdict(&s_a, 0, 1) == RD_AB_V_NONE,
                  "a trial on the title stage: %u trials, %llu replays without it",
                  (unsigned)s_a.trials, (unsigned long long)s_a.offTrial);
        }
    }
}

/* the scenery ends mid-trial (a menu, a movie, the title): the trial ends
   with no verdict however many pairs it had, no mark for the stage, its
   line said; it is timed again 2 s into the next stretch of scenery (the
   same stage here), and gives its verdict there */
static void testSceneryEnd(void)
{
    printf("the scenery ends mid-trial:\n");
    rd_ab_init(&s_a, RD_AB_AUTO);
    (void)rd_ab_subject(&s_a, "zero-area cull");
    rd_ab_allow(&s_a, 0, true);
    rd_ab_set_scenery(&s_a, true);
    /* the cull costs nothing either way: no early verdict, 20 pairs */
    Stream s = {0};
    s.lag = 2;
    s.stepMs = 40.0;
    s.noise = 0.01;
    s.stage = 42;
    s.replays = 50 + 44;
    double t = run(&s_a, &s, 0.0, 1u);
    CHECK(s_a.t.active && s_a.t.pairs >= RD_AB_PAIRS_MIN, "no trial past 16 pairs (%u)",
          (unsigned)s_a.t.pairs);
    /* the pause menu: no scenery */
    rd_ab_set_scenery(&s_a, false);
    s.replays = 100;
    t = run(&s_a, &s, t, 1u);
    CHECK(!s_a.t.active && s_a.trials == 1u && rd_ab_verdict(&s_a, 0, 42) == RD_AB_V_NONE &&
              s_a.subj[0].stagesOn == 0u && s_a.subj[0].stagesOff == 0u,
          "the scenery ended: running %d, %u trials, verdict %d", (int)s_a.t.active,
          (unsigned)s_a.trials, rd_ab_verdict(&s_a, 0, 42));
    CHECK(strstr(s_last, "A/B: zero-area cull, stage 42: no verdict (the scenery ended, ") ==
                  s_last &&
              strstr(s_last, "with it until it is timed again in the next stretch of play or a "
                             "cutscene") != NULL,
          "%s", s_last);
    /* back in play: no trial in its first 2 s, then the trial and its
       verdict (the cull 5 % dearer here) */
    rd_ab_set_scenery(&s_a, true);
    s.noise = 0.0;
    s.effect = 0.05;
    s.replays = 45;
    t = run(&s_a, &s, t, 1u);
    CHECK(s_a.trials == 1u, "a trial before 2 s of scenery again (%u)", (unsigned)s_a.trials);
    s.replays = 200;
    (void)run(&s_a, &s, t, 1u);
    CHECK(s_a.trials == 2u && rd_ab_verdict(&s_a, 0, 42) == RD_AB_V_OFF,
          "not timed again after the scenery came back: %u trials, verdict %d",
          (unsigned)s_a.trials, rd_ab_verdict(&s_a, 0, 42));
}

/* the subjects as the renderer registers them (rd_ab_run.c k_subj): the
   cull, the twins' proven classes, the fast pixel build, the twins' camera
   rule class.  The camera class has no work in the opening (stage 41): it
   is not tried there; in the boat (stage 42) its passes had work: its own
   trial.  Its verdict and the twins' are independent: twins on and camera
   off leaves only the camera's bit at the off arm, and the other way round */
static void testCamera(void)
{
    enum { ZERO, ND, FPS, CAM };

    for (int flip = 0; flip < 2; flip++) {
        printf("the twins' camera rule class (%s):\n",
               flip ? "twins dearer, camera cheaper" : "twins cheaper, camera dearer");
        rd_ab_init(&s_a, RD_AB_AUTO);
        CHECK(rd_ab_subject(&s_a, "zero-area cull") == ZERO, "id 0");
        CHECK(rd_ab_subject(&s_a, "discard-free twins") == ND, "id 1");
        CHECK(rd_ab_subject(&s_a, "fast pixel build") == FPS, "id 2");
        CHECK(rd_ab_subject(&s_a, "discard-free twins, camera rule") == CAM, "id 3");
        CHECK(RD_AB_SUBJECTS == 5 && rd_ab_subject(&s_a, "the fifth") == 4,
              "the registry's fifth place (RD_AB_SUBJECTS %d)", RD_AB_SUBJECTS);
        for (int i = 0; i < 4; i++) {
            rd_ab_allow(&s_a, i, true);
        }
        rd_ab_set_scenery(&s_a, true);
        const double eff[4] = {0.0, flip ? 0.05 : -0.05, 0.0, flip ? -0.05 : 0.05};
        double t = 0.0;
        Pend q[4];
        int qn = 0;
        for (int stage = 41; stage <= 42; stage++) {
            /* the opening: the twins' proven classes only; the boat: both */
            const uint8_t used =
                stage == 41 ? (uint8_t)(1u << ND) : (uint8_t)(1u << ND | 1u << CAM);
            for (int i = 0; i < 600; i++) {
                if (qn >= 2) {
                    const RdAbIn in = {q[0].tag, used, false, true, q[0].work, q[0].ms};
                    rd_ab_collect(&s_a, &in);
                    memmove(q, q + 1, (size_t)(qn - 1) * sizeof(q[0]));
                    qn--;
                }
                const RdAbTag tag = rd_ab_begin(&s_a, stage, t, false);
                if (stage == 41) {
                    CHECK(!(s_a.t.active && s_a.t.subject == CAM),
                          "the camera class tried where it had no work");
                }
                double ms = 40.0;
                for (int k = 0; k < 4; k++) {
                    /* a class with no work in the stage costs nothing there */
                    if (used & (1u << k)) {
                        ms *= (tag.off & (1u << k)) ? 1.0 : 1.0 + eff[k];
                    }
                }
                q[qn++] = (Pend){tag, 50000.0, ms, false};
                t += 40.0;
                lines(&s_a);
            }
            if (stage == 41) {
                CHECK(s_a.subj[CAM].trials == 0u && s_a.subj[CAM].carry == RD_AB_V_NONE,
                      "the camera class judged in the opening");
                CHECK(s_a.subj[ND].carry == (flip ? RD_AB_V_OFF : RD_AB_V_ON),
                      "the twins' verdict in the opening: %d", s_a.subj[ND].carry);
            }
            t += 10000.0;
        }
        CHECK(s_a.subj[CAM].trials == 1u &&
                  s_a.subj[CAM].carry == (flip ? RD_AB_V_ON : RD_AB_V_OFF) &&
                  s_a.subj[ND].carry == (flip ? RD_AB_V_OFF : RD_AB_V_ON),
              "the verdicts: camera %d (%u trials), twins %d", s_a.subj[CAM].carry,
              (unsigned)s_a.subj[CAM].trials, s_a.subj[ND].carry);
        CHECK(strstr(s_last, "A/B: discard-free twins, camera rule, stage 42: ") == s_last,
              "the camera class's line: %s", s_last);
        /* every later replay: each class at its own verdict's arm */
        const RdAbTag tag = rd_ab_begin(&s_a, 43, t, false);
        const uint8_t want = flip ? (uint8_t)(1u << ND) : (uint8_t)(1u << CAM);
        CHECK(tag.off == want && tag.trial == 0u, "after both verdicts: off %x (want %x)", tag.off,
              want);
    }
}

int main(void)
{
    testTitleStage();
    testSceneryEnd();
    testCamera();
    testMeasure();
    testForget();
    testVerdicts();
    testCost();
    testStages();
    testTwoSubjects();
    testThree();
    testUseNote();
    testStop();
    testModes();
    testOnce();
    testRate();
    if (failures) {
        printf("rd_ab: %d failures\n", failures);
        return 1;
    }
    printf("rd_ab: all passed\n");
    return 0;
}
