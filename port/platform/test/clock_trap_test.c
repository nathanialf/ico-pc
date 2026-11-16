/*
 * port/platform/test/clock_trap_test.c
 *
 * One picture a tick behind the step rule (clock_trap.h; the owner's Pixel
 * in the cage room, build-host/tmp/v0117/evidence/owner-cage/ico-pc.log):
 *   rate     the step rule's pictures a second at a picture's reads of 8 to
 *            30 ms in the cage's frame (a 12.5 ms closing step, a 1.2 ms
 *            step between, 16.683 ms steps, pace_policy.c
 *            pace_rt_further_trace on the as-made path): two a tick while
 *            2R <= 3P - 2S - margin (R <= 11.524999 ms), one past it, to the
 *            nanosecond at the bound;
 *   seconds  pictures, game frames, asks and holds by second; a gap of two
 *            seconds restarts the seconds at the next event;
 *   rule     one a tick (at most 1.1 pictures a frame, at least 20 frames,
 *            asks at least half the frames, at least half of them held, as
 *            made) at its edges, said at every such second's close;
 *   worst    the block's worst second: fewer pictures a frame, then more
 *            CPU a picture; the first of equals kept;
 *   target   the worst second judged by presented pictures against the
 *            rate in force (issue 179's v0.11.7 log, the 20-frame rule's
 *            "none judged" on a 25 fps disc's dips): a 25 a second trace
 *            with one second at 12 (the worst, 48 %, which the rule without
 *            a rate never judges), a 60 a second trace at two a tick with
 *            one second at 45 (75 %), a second followed by a gap not judged,
 *            the line word for word;
 *   dips     the cores' clusters from the block's sums, and the dip against
 *            the median of the six blocks before over the owner's log's
 *            cores 2-6 averages from :3048 to :5449: a dip at :4251 (884
 *            against 1197) and :5276 (896 against 1132.5) and nowhere else;
 *   lines    the ranked clause and the worst-second line word for word, a
 *            short buffer cut and terminated;
 *   due      the ranked line names the clocks only for a block at or near
 *            one picture a tick (at most 1.5) with its picture CPU at 1.25
 *            times the median of the blocks before or more (v0.11.7-rc1's
 *            clause fired on good blocks at 1.6-3.4 a tick): the edges, the
 *            render thread's core and its cluster's level, the clause word
 *            for word; the block's pictures and frames counted;
 *   second   one second's text (the pin trace's, pin_release.h), with the
 *            ring's samples and without; the blocks' CPU median and a
 *            second's CPU raised against it.
 */
#include <stdio.h>
#include <string.h>

#include "clock_trap.h"
#include "pace_policy.h"

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

#define MS 1000000ull
#define PERIOD 16683333ull /* the NTSC step (pace_policy.h, window_perf_hint.c) */

/* --- the step rule's rate ----------------------------------------------- */

/* The step rule's answer to the further picture of a frame in the cage's
   steady state, asked the moment the render thread is free: the closing
   step due at base runs closeNs, the frame's own picture is posted at its
   end and reads readsNs, the further picture is posted when those reads
   end (the earliest it can be: a later ask only moves its reads' end
   later, so a hold here is a hold at every ask of the frame), the next
   closing step due two periods after base, the step after it a step
   between frames of betweenNs.  The phone's pipe as the log shows it: the
   submit thread on (its job before the next picture is the submit
   thread's, :4246 "the CPU adds the submit thread's submit and present"),
   no GPU fence predicted (gpuNs 0: the reads as measured set the end, as
   in 301 of the 411 holds at :4236), a picture's job its reads (no tail). */
static PaceRtHold cage_ask(uint64_t closeNs, uint64_t betweenNs, uint64_t readsNs)
{
    const uint64_t base = 1000000000ull;
    PaceRtAsk q;
    PaceRtPipe p;
    PaceRtTrace t;

    memset(&q, 0, sizeof(q));
    memset(&p, 0, sizeof(p));
    q.nowNs = base + closeNs + readsNs;
    q.dueNs = base + 2u * PERIOD;
    q.periodNs = PERIOD;
    q.stepNs = closeNs;
    q.afterNs = betweenNs;
    q.nextBetween = false;
    q.sinceNs = 0; /* the render thread free */
    q.readsNs = readsNs;
    q.pictureNs = readsNs;
    q.presentNs = 0;
    q.refreshNs = 8333333ull;
    q.cpuNs = 0;
    q.submitOn = true;
    q.gpuNs = 0;
    q.paced = true;
    q.asMade = true;
    return pace_rt_further_trace(&q, &p, &t);
}

/* pictures a second in that steady state: 30 frames a second, each its own
   picture and the further one where the rule hands it over */
static unsigned cage_rate(uint64_t closeNs, uint64_t betweenNs, uint64_t readsNs)
{
    return 30u * (cage_ask(closeNs, betweenNs, readsNs) == PACE_RT_GO ? 2u : 1u);
}

static void test_rate(void)
{
    const uint64_t S = 12500000ull, B = 1200000ull;

    /* the bound: S + 2R <= 2P + (P - (S + 2 ms)), so R <= (3P - 2S - 2 ms)
       / 2 = (50049999 - 25000000 - 2000000) / 2 = 11524999.5 ns */
    static const struct {
        uint64_t readsNs;
        unsigned fps;
    } k[] = {
        {8 * MS, 60u},      /* the opening's reads */
        {9500000ull, 60u},  /* the cage's neighbouring blocks (:4320 "reads 9.5 ms") */
        {11 * MS, 60u},     /* the step after: 58.0 ms against its limit 63.53 */
        {11524999ull, 60u}, /* at the bound: the reads end 1 ns inside the slack */
        {11525000ull, 30u}, /* 1 ns past it */
        {12 * MS, 30u},     /* the brief's rows */
        {16 * MS, 30u},     {16800000ull, 30u}, /* :4236 "reads 16.8 ms" */
        {20 * MS, 30u},     {24 * MS, 30u},     {30 * MS, 30u},
    };

    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        const unsigned fps = cage_rate(S, B, k[i].readsNs);

        if (fps != k[i].fps) {
            printf("FAIL rate: reads %.6f ms: %u pictures a second, want %u\n",
                   (double)k[i].readsNs / 1e6, fps, k[i].fps);
            failures++;
        }
    }
    /* the hold is the next step's test, never the step after's */
    {
        const uint64_t base = 1000000000ull;
        PaceRtAsk q;
        PaceRtPipe p;
        PaceRtTrace t;

        memset(&q, 0, sizeof(q));
        memset(&p, 0, sizeof(p));
        q.nowNs = base + S + 16800000ull;
        q.dueNs = base + 2u * PERIOD;
        q.periodNs = PERIOD;
        q.stepNs = S;
        q.afterNs = B;
        q.readsNs = q.pictureNs = 16800000ull;
        q.refreshNs = 8333333ull;
        q.submitOn = true;
        q.paced = q.asMade = true;
        CHECK(pace_rt_further_trace(&q, &p, &t) == PACE_RT_MISS);
        CHECK(t.test == PACE_RT_TEST_NEXT);
        CHECK(t.slackNs == PERIOD - (S + PACE_RT_STEP_MARGIN_NS));
        CHECK(t.reads.endNs == base + S + 2u * 16800000ull);
    }
    /* a closing step of 6 ms: the next step's bound (50049999 - 12000000 -
       2000000) / 2 = 18024999.5 is past the step after's: that one starts
       when the next frame's picture's reads end, 2S + 3R after base, and
       may start by 4P - B - 2 ms = 63533332, so 3R <= 51533332 and R <=
       17177777: the 16.8 ms reads fit, 1 ns past that bound does not */
    CHECK(cage_rate(6 * MS, B, 16800000ull) == 60u);
    CHECK(cage_rate(6 * MS, B, 17177777ull) == 60u);
    CHECK(cage_rate(6 * MS, B, 17177778ull) == 30u);
    CHECK(cage_rate(6 * MS, B, 20 * MS) == 30u);
    {
        /* 1 ns past it: held by the step after's test */
        const uint64_t base = 1000000000ull;
        PaceRtAsk q;
        PaceRtPipe p;
        PaceRtTrace t;

        memset(&q, 0, sizeof(q));
        memset(&p, 0, sizeof(p));
        q.nowNs = base + 6 * MS + 17177778ull;
        q.dueNs = base + 2u * PERIOD;
        q.periodNs = PERIOD;
        q.stepNs = 6 * MS;
        q.afterNs = B;
        q.readsNs = q.pictureNs = 17177778ull;
        q.refreshNs = 8333333ull;
        q.submitOn = true;
        q.paced = q.asMade = true;
        CHECK(pace_rt_further_trace(&q, &p, &t) == PACE_RT_MISS);
        CHECK(t.test == PACE_RT_TEST_AFTER && t.startBNs == base + 63533334ull);
    }
}

/* --- the seconds and the rule ---------------------------------------------- */

static ClockTrapSecond sec(unsigned pictures, unsigned frames, unsigned asks, unsigned held,
                           bool asMade)
{
    ClockTrapSecond s;

    memset(&s, 0, sizeof(s));
    s.pictures = pictures;
    s.frames = frames;
    s.asks = asks;
    s.held = held;
    s.asMade = asMade;
    return s;
}

static void test_rule(void)
{
    ClockTrapSecond s = sec(30, 30, 55, 41, true);

    CHECK(clock_trap_one_a_tick(&s));
    s = sec(33, 30, 55, 41, true); /* 1.1 a tick */
    CHECK(clock_trap_one_a_tick(&s));
    s = sec(34, 30, 55, 41, true);
    CHECK(!clock_trap_one_a_tick(&s));
    s = sec(22, 20, 10, 5, true); /* 20 frames, 1.1, asks half, held half */
    CHECK(clock_trap_one_a_tick(&s));
    s = sec(19, 19, 55, 41, true); /* fewer frames than the minimum */
    CHECK(!clock_trap_one_a_tick(&s));
    s = sec(30, 30, 14, 14, true); /* asks under half the frames: not wanted */
    CHECK(!clock_trap_one_a_tick(&s));
    s = sec(30, 30, 15, 8, true);
    CHECK(clock_trap_one_a_tick(&s));
    s = sec(30, 30, 15, 7, true); /* under half held */
    CHECK(!clock_trap_one_a_tick(&s));
    s = sec(30, 30, 55, 41, false); /* paced: never */
    CHECK(!clock_trap_one_a_tick(&s));
    s = sec(58, 30, 86, 18, true); /* the neighbouring blocks: two a tick */
    CHECK(!clock_trap_one_a_tick(&s));
}

/* one second of the cage at one a tick from ms: 55 asks (41 held) 17 ms
   apart from +2, 30 pictures each a new frame 33 ms apart from +5, in
   time order; cpuMs each picture's CPU; the seconds closed at one a tick
   counted */
static unsigned feed_second(ClockTrap *c, double ms, uint32_t *frame, double cpuMs,
                            unsigned picturesPerFrame, unsigned *ones)
{
    unsigned ev = 0;
    unsigned a = 0, k = 0;
    const unsigned pictures = 30u * picturesPerFrame;
    ClockTrapSecond closed;

    while (a < 55u || k < pictures) {
        const double ta = ms + 2.0 + 17.0 * a;
        const double tp = ms + 5.0 + (33.0 / picturesPerFrame) * k;
        unsigned e;

        if (a < 55u && (k >= pictures || ta <= tp)) {
            e = clock_trap_ask(c, ta, a < 41u, true, &closed);
            a++;
        } else {
            if (k % picturesPerFrame == 0) {
                (*frame)++;
            }
            e = clock_trap_picture(c, tp, *frame, cpuMs, &closed);
            k++;
        }
        *ones += (e & CLOCK_TRAP_EV_ONE) ? 1u : 0u;
        ev |= e;
    }
    return ev;
}

static void test_seconds(void)
{
    ClockTrap c;
    ClockTrapBlock b;
    uint32_t frame = 100;
    unsigned ones = 0;
    const double base = 50000.0;

    clock_trap_reset(&c);
    for (unsigned s = 0; s < 10u; s++) {
        (void)feed_second(&c, base + 1000.0 * s, &frame, 22.6, 1u, &ones);
    }
    /* seconds 0-8 closed (the tenth is open), each at one a tick */
    CHECK(ones == 9u);
    clock_trap_block_take(&c, &b);
    CHECK(b.seconds == 9u && b.oneATick == 9u);
    CHECK(b.haveWorst && b.worst.pictures == 30u && b.worst.frames == 30u);
    CHECK(b.worst.asks == 55u && b.worst.held == 41u && b.worst.asMade);
    CHECK(b.worst.t0Ms == base + 2.0); /* the first event's time, the first of equals */
    CHECK(b.cpuN == 300u);
    CHECK(b.pictures == 300u && b.frames == 300u); /* every picture, every new frame */
    /* the block taken: zeroed */
    clock_trap_block_take(&c, &b);
    CHECK(b.seconds == 0u && !b.haveWorst && b.pictures == 0u && b.frames == 0u);
    /* a gap: the open second (the tenth) closes at the next event, the next
       starts there; it was at one a tick */
    {
        ClockTrapSecond closed;
        const unsigned e = clock_trap_picture(&c, base + 15000.0, ++frame, 22.6, &closed);

        CHECK((e & CLOCK_TRAP_EV_CLOSED) && (e & CLOCK_TRAP_EV_ONE));
        CHECK(closed.t0Ms == base + 9002.0 && closed.pictures == 30u);
        CHECK(c.cur.t0Ms == base + 15000.0 && c.cur.pictures == 1u);
    }
    clock_trap_block_take(&c, &b);
    CHECK(b.seconds == 1u && b.oneATick == 1u);
    /* two seconds at two a tick, then one at one a tick: the worst is the
       one a tick, the only one said */
    clock_trap_reset(&c);
    ones = 0;
    (void)feed_second(&c, base, &frame, 9.5, 2u, &ones);
    (void)feed_second(&c, base + 1000.0, &frame, 9.5, 2u, &ones);
    (void)feed_second(&c, base + 2000.0, &frame, 22.6, 1u, &ones);
    (void)feed_second(&c, base + 3000.0, &frame, 9.5, 2u, &ones);
    clock_trap_block_take(&c, &b);
    CHECK(ones == 1u && b.seconds == 3u && b.oneATick == 1u);
    CHECK(b.worst.pictures == 30u && b.worst.t0Ms == base + 2002.0);
    /* worse: fewer a frame first, then more CPU */
    {
        ClockTrapSecond x = sec(30, 30, 0, 0, true), y = sec(31, 30, 0, 0, true);

        CHECK(clock_trap_worse(&x, &y) && !clock_trap_worse(&y, &x));
        y = sec(30, 30, 0, 0, true);
        x.cpuMs = 30.0;
        x.cpuN = 2;
        y.cpuMs = 20.0;
        y.cpuN = 2;
        CHECK(clock_trap_worse(&x, &y) && !clock_trap_worse(&y, &x));
    }
}

/* n pictures in the second from ms, each a new frame (perFrame 1) or two to
   a frame (perFrame 2), 1000 / n ms apart from +0, each with 10 ms of CPU */
static void feed_pictures(ClockTrap *c, double ms, unsigned n, unsigned perFrame, uint32_t *frame)
{
    ClockTrapSecond closed;

    for (unsigned k = 0; k < n; k++) {
        if (k % perFrame == 0) {
            (*frame)++;
        }
        (void)clock_trap_picture(c, ms + (1000.0 / n) * k, *frame, 10.0, &closed);
    }
}

static void test_target(void)
{
    ClockTrap c;
    ClockTrapBlock b;
    uint32_t frame = 1000;
    const double base = 70000.0;
    char line[512];

    /* a 25 fps disc at Frame rate Original: 25 a second, second 4 at 12 */
    clock_trap_reset(&c);
    clock_trap_target(&c, 25.0);
    for (unsigned s = 0; s < 10u; s++) {
        feed_pictures(&c, base + 1000.0 * s, s == 4u ? 12u : 25u, 1u, &frame);
    }
    clock_trap_block_take(&c, &b);
    /* seconds 0-8 closed (the tenth open): every one judged, the dip worst;
       the 20-frame count leaves the dip out */
    CHECK(b.judged == 9u && b.seconds == 8u && b.oneATick == 0u);
    CHECK(b.haveWorst && b.worst.pictures == 12u && b.worst.frames == 12u);
    CHECK(b.worst.t0Ms == base + 4000.0 && b.worst.targetHz == 25.0);
    (void)clock_trap_worst_text(&b, base, NULL, line, sizeof line);
    CHECK_STR(line, "worst second of the block: 12 pictures for 12 game frames (1.00 a tick; 48 % "
                    "of the 25.0 a second in force; 9 whole seconds judged), from 4.0 s into the "
                    "block; the step rule held 0 of 0 further pictures asked; a picture's CPU 10.0 "
                    "ms (the block's 10.0); the CPU clocks not sampled here; 0 of 8 seconds at one "
                    "a tick behind the step rule");
    /* the same trace without a rate: v0.11.7's rule, the dip never judged */
    clock_trap_reset(&c);
    for (unsigned s = 0; s < 10u; s++) {
        feed_pictures(&c, base + 1000.0 * s, s == 4u ? 12u : 25u, 1u, &frame);
    }
    clock_trap_block_take(&c, &b);
    CHECK(b.judged == 8u && b.seconds == 8u);
    CHECK(b.haveWorst && b.worst.pictures == 25u && b.worst.t0Ms == base);
    /* 60 a second on a 60 Hz display at two a tick: second 2 at 45 */
    clock_trap_reset(&c);
    clock_trap_target(&c, 60.0);
    for (unsigned s = 0; s < 4u; s++) {
        feed_pictures(&c, base + 1000.0 * s, s == 2u ? 45u : 60u, s == 2u ? 1u : 2u, &frame);
    }
    clock_trap_block_take(&c, &b);
    CHECK(b.judged == 3u && b.haveWorst && b.worst.pictures == 45u);
    CHECK(b.worst.t0Ms == base + 2000.0);
    /* a second closed after a gap of 2 s or more (the app away) is not
       judged; the one after it is */
    clock_trap_reset(&c);
    clock_trap_target(&c, 25.0);
    feed_pictures(&c, base, 3u, 1u, &frame);
    feed_pictures(&c, base + 5000.0, 25u, 1u, &frame);
    feed_pictures(&c, base + 6000.0, 25u, 1u, &frame);
    clock_trap_block_take(&c, &b);
    CHECK(b.judged == 1u && b.haveWorst && b.worst.pictures == 25u);
    CHECK(b.worst.t0Ms == base + 5000.0);
    /* worse with a rate: the smaller share, whatever the frames */
    {
        ClockTrapSecond x = sec(20, 20, 0, 0, false), y = sec(50, 25, 0, 0, false);

        x.targetHz = 25.0; /* 80 % */
        y.targetHz = 60.0; /* 83 % */
        CHECK(clock_trap_worse(&x, &y) && !clock_trap_worse(&y, &x));
        CHECK(clock_trap_judged(&x, true) && !clock_trap_judged(&x, false));
        x.pictures = 0;
        CHECK(!clock_trap_judged(&x, true));
    }
}

/* --- the clusters and the dip ----------------------------------------------- */

/* a block of 40 samples a core: cores 0-1, 2-6 and 7 at three top speeds */
static GpuSysfsBlock owner_block(double low, double mid, double top)
{
    GpuSysfsBlock b;

    memset(&b, 0, sizeof(b));
    b.n = 40;
    b.cpuCount = 8;
    for (int i = 0; i < 8; i++) {
        const double m = i < 2 ? low : (i < 7 ? mid : top);

        b.cpuKhz[i] = i < 2 ? 2246000u : (i < 7 ? 3052000u : 2707000u);
        b.cpuN[i] = 40;
        b.cpuSum[i] = 40.0 * m;
        b.cpuMin[i] = 400;
        b.cpuMax[i] = 3052;
    }
    return b;
}

static void test_dips(void)
{
    /* the cores 2-6 averages of the owner's log's blocks from :3048 to
       :5449 in order (the "kernel GPU driver" lines); the other clusters
       steady */
    static const double k[] = {1150, 962,  954,  1070, 1112, 1007, 1191, 1089, 1131,
                               1182, 1231, 1212, 1277, 884,  1270, 1145, 1159, 1117,
                               1235, 1118, 1028, 1147, 1059, 1162, 896,  997,  1069};
    ClockTrapHist h;
    unsigned dips = 0;

    memset(&h, 0, sizeof(h));
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        const GpuSysfsBlock b = owner_block(850.0, k[i], 900.0);
        ClockTrapClusters now;
        ClockTrapDip d[CLOCK_TRAP_CLUSTERS];

        clock_trap_clusters(&b, &now);
        CHECK(now.n == 3 && now.first[1] == 2 && now.last[1] == 6 && now.samples[1] == 200u);
        const int n = clock_trap_dips(&h, &now, d, CLOCK_TRAP_CLUSTERS);

        if (i == 13) {
            /* :4251: 884 against the median of 1089 1131 1182 1231 1212 1277 */
            CHECK(n == 1 && d[0].first == 2 && d[0].last == 6 && d[0].mean == 884.0);
            CHECK(d[0].median == 1197.0 && d[0].blocks == 6u);
        } else if (i == 24) {
            /* :5276: 896 against 1118 1028 1147 1059 1162 and 1235 */
            CHECK(n == 1 && d[0].mean == 896.0 && d[0].median == 1132.5);
        } else if (n != 0) {
            printf("FAIL dips: block %zu (%.0f MHz) dipped\n", i, k[i]);
            failures++;
        }
        dips += (unsigned)n;
        clock_trap_hist_push(&h, &now);
    }
    CHECK(dips == 2u && h.n == CLOCK_TRAP_HIST);
    /* too few samples, too few blocks before */
    {
        ClockTrapHist e;
        ClockTrapClusters now;
        ClockTrapDip d[CLOCK_TRAP_CLUSTERS];
        GpuSysfsBlock b = owner_block(850.0, 1200.0, 900.0);

        memset(&e, 0, sizeof(e));
        clock_trap_clusters(&b, &now);
        clock_trap_hist_push(&e, &now);
        b = owner_block(850.0, 500.0, 900.0);
        clock_trap_clusters(&b, &now);
        CHECK(clock_trap_dips(&e, &now, d, CLOCK_TRAP_CLUSTERS) == 0); /* one block before */
        clock_trap_hist_push(&e, &now);
        b = owner_block(850.0, 1200.0, 900.0);
        clock_trap_clusters(&b, &now);
        clock_trap_hist_push(&e, &now);
        b = owner_block(850.0, 300.0, 900.0);
        for (int i = 2; i < 7; i++) {
            b.cpuN[i] = 1; /* 5 samples in the cluster */
            b.cpuSum[i] = 300.0;
        }
        clock_trap_clusters(&b, &now);
        CHECK(now.samples[1] == 5u);
        CHECK(clock_trap_dips(&e, &now, d, CLOCK_TRAP_CLUSTERS) == 0);
    }
}

/* --- the lines --------------------------------------------------------------- */

static void test_lines(void)
{
    char b[1024];
    ClockTrapDip d;
    ClockTrapHint hint;

    d.first = 2;
    d.last = 6;
    d.mean = 884.0;
    d.median = 1197.0;
    d.blocks = 6;
    memset(&hint, 0, sizeof(hint));
    hint.known = true;
    hint.targetMs = 7.8125;
    hint.cpuMs = 22.56;
    hint.gpuMs = 20.73;
    hint.reports = 300;
    hint.over = 294;
    (void)clock_trap_dip_text(&d, 1, &hint, b, sizeof b);
    CHECK_STR(b, "; the CPU clocks under their level: cores 2-6 averaged 884 MHz against 1197 "
                 "(the median of the 6 blocks before; 74 %); the performance hint's target "
                 "7.81 ms a picture, its reports 22.56 ms CPU / 20.73 ms GPU (over target in "
                 "294 of 300)");
    hint.known = false;
    (void)clock_trap_dip_text(&d, 1, &hint, b, sizeof b);
    CHECK_STR(b, "; the CPU clocks under their level: cores 2-6 averaged 884 MHz against 1197 "
                 "(the median of the 6 blocks before; 74 %); no performance hint session");
    b[0] = 'x';
    CHECK(clock_trap_dip_text(&d, 0, &hint, b, sizeof b) == 0 && b[0] == 'x');
    /* a short buffer: cut and terminated, the whole length returned */
    {
        char s[16];
        const int n = clock_trap_dip_text(&d, 1, &hint, s, sizeof s);

        CHECK(n > 100 && strlen(s) == 15u);
    }

    /* the worst second, with the ring's samples inside it */
    {
        ClockTrapBlock blk;
        GpuSysfsRing ring;
        static const uint16_t lo[5] = {700, 460, 812, 1286, 533};
        static const uint16_t mid[5] = {1200, 400, 884, 2092, 1100};
        static const uint16_t top[5] = {900, 883, 2707, 1008, 900};
        static const double at[5] = {-100.0, 100.0, 350.0, 600.0, 850.0};
        const double t0 = 50002.0;

        memset(&blk, 0, sizeof(blk));
        blk.haveWorst = true;
        blk.worst = sec(30, 30, 55, 41, true);
        blk.worst.t0Ms = t0;
        blk.worst.cpuMs = 678.0;
        blk.worst.cpuN = 30;
        blk.seconds = 9;
        blk.oneATick = 9;
        blk.cpuMs = 6780.0;
        blk.cpuN = 300;
        memset(&ring, 0, sizeof(ring));
        ring.cpuCount = 8;
        for (int i = 0; i < 8; i++) {
            ring.cpuKhz[i] = i < 2 ? 2246000u : (i < 7 ? 3052000u : 2707000u);
        }
        for (int s = 0; s < 5; s++) {
            ring.s[s].tMs = t0 + at[s];
            for (int i = 0; i < 8; i++) {
                ring.s[s].cpuMhz[i] = i < 2 ? lo[s] : (i < 7 ? mid[s] : top[s]);
            }
        }
        ring.count = 5;
        ring.next = 5;
        (void)clock_trap_worst_text(&blk, t0 - 3200.0, &ring, b, sizeof b);
        CHECK_STR(b, "worst second of the block: 30 pictures for 30 game frames (1.00 a tick), "
                     "from 3.2 s into the block; the step rule held 41 of 55 further pictures "
                     "asked; a picture's CPU 22.6 ms (the block's 22.6); the CPU clocks in that "
                     "second (4 samples, MHz): cores 0-1 460 812 1286 533, cores 2-6 400 884 "
                     "2092 1100, core 7 883 2707 1008 900; 9 of 9 seconds at one a tick behind "
                     "the step rule");
        (void)clock_trap_worst_text(&blk, t0 - 3200.0, NULL, b, sizeof b);
        CHECK_STR(b, "worst second of the block: 30 pictures for 30 game frames (1.00 a tick), "
                     "from 3.2 s into the block; the step rule held 41 of 55 further pictures "
                     "asked; a picture's CPU 22.6 ms (the block's 22.6); the CPU clocks not "
                     "sampled here; 9 of 9 seconds at one a tick behind the step rule");
        memset(&blk, 0, sizeof(blk));
        (void)clock_trap_worst_text(&blk, 0.0, NULL, b, sizeof b);
        CHECK_STR(b, "worst second of the block: none judged (no whole second with a picture "
                     "at a known rate, and fewer than 20 game frames in every second)");
    }
}

/* --- when the ranked line names the clocks, and one second's text --------- */

/* the worst-second test's ring: 5 samples around a second from t0, four in it */
static void owner_ring(GpuSysfsRing *ring, double t0)
{
    static const uint16_t lo[5] = {700, 460, 812, 1286, 533};
    static const uint16_t mid[5] = {1200, 400, 884, 2092, 1100};
    static const uint16_t top[5] = {900, 883, 2707, 1008, 900};
    static const double at[5] = {-100.0, 100.0, 350.0, 600.0, 850.0};

    memset(ring, 0, sizeof(*ring));
    ring->cpuCount = 8;
    for (int i = 0; i < 8; i++) {
        ring->cpuKhz[i] = i < 2 ? 2246000u : (i < 7 ? 3052000u : 2707000u);
    }
    for (int s = 0; s < 5; s++) {
        ring->s[s].tMs = t0 + at[s];
        for (int i = 0; i < 8; i++) {
            ring->s[s].cpuMhz[i] = i < 2 ? lo[s] : (i < 7 ? mid[s] : top[s]);
        }
    }
    ring->count = 5;
    ring->next = 5;
}

static void test_due(void)
{
    /* six blocks before: cores 2-6 at the :4251 dip's six (median 1197),
       a picture's CPU 9.0 to 10.0 ms (median 9.5) */
    static const double mid[6] = {1089, 1131, 1182, 1231, 1212, 1277};
    static const double cpu[6] = {9.0, 9.2, 9.4, 9.6, 9.8, 10.0};
    ClockTrapHist h;
    ClockTrapClusters now;
    ClockTrapDue d;
    char b[1024];

    memset(&h, 0, sizeof(h));
    for (int i = 0; i < 6; i++) {
        const GpuSysfsBlock blk = owner_block(850.0, mid[i], 900.0);

        clock_trap_clusters(&blk, &now);
        now.cpuMs = cpu[i];
        clock_trap_hist_push(&h, &now);
    }
    {
        const GpuSysfsBlock blk = owner_block(850.0, 884.0, 900.0);

        clock_trap_clusters(&blk, &now);
    }
    /* the rc13 slow block: 300 pictures for 300 frames, 22.6 ms, the render
       thread on core 6 at 870 MHz */
    CHECK(clock_trap_due(&h, &now, 300, 300, 22.6, 6, 870.0, &d));
    CHECK(d.cpuMedian == 9.5 && d.cpuBlocks == 6u && d.clFirst == 2 && d.clLast == 6);
    CHECK(d.clMedian == 1197.0 && d.clBlocks == 6u);
    (void)clock_trap_due_text(&d, b, sizeof b);
    CHECK_STR(b, "; at 1.00 pictures a tick with a picture's CPU 22.6 ms against 9.5 (the median "
                 "of the 6 blocks before); the render thread on core 6 averaged 870 MHz in the "
                 "block, its cluster cores 2-6 at 1197 before (73 %)");
    /* rc1's false clause: a good block after a resume, 754 pictures for 300
       frames (2.5 a tick): never, whatever the CPU */
    CHECK(!clock_trap_due(&h, &now, 754, 300, 22.6, 6, 870.0, &d));
    /* the tick edge: 1.5 a tick due, one picture more not */
    CHECK(clock_trap_due(&h, &now, 450, 300, 22.6, 6, 870.0, &d));
    CHECK(!clock_trap_due(&h, &now, 451, 300, 22.6, 6, 870.0, &d));
    /* the CPU edge: 1.25 x 9.5 = 11.875 due, under it not; not known: not */
    CHECK(clock_trap_due(&h, &now, 300, 300, 11.875, 6, 870.0, &d));
    CHECK(!clock_trap_due(&h, &now, 300, 300, 11.8, 6, 870.0, &d));
    CHECK(!clock_trap_due(&h, &now, 300, 300, 0.0, 6, 870.0, &d));
    /* fewer than 20 frames (a load, a pause) */
    CHECK(!clock_trap_due(&h, &now, 19, 19, 22.6, 6, 870.0, &d));
    /* the render thread on core 7 (its own cluster, 900 before); its core
       not known; its clock not sampled */
    CHECK(clock_trap_due(&h, &now, 300, 300, 22.6, 7, 880.0, &d));
    CHECK(d.clFirst == 7 && d.clLast == 7 && d.clMedian == 900.0);
    (void)clock_trap_due_text(&d, b, sizeof b);
    CHECK(strstr(b, "; the render thread on core 7 averaged 880 MHz in the block, its cluster "
                    "core 7 at 900 before (98 %)") != NULL);
    CHECK(clock_trap_due(&h, &now, 300, 300, 22.6, -1, 0.0, &d));
    (void)clock_trap_due_text(&d, b, sizeof b);
    CHECK_STR(b, "; at 1.00 pictures a tick with a picture's CPU 22.6 ms against 9.5 (the median "
                 "of the 6 blocks before); the render thread's core not known");
    CHECK(clock_trap_due(&h, &now, 300, 300, 22.6, 6, 0.0, &d));
    (void)clock_trap_due_text(&d, b, sizeof b);
    CHECK(strstr(b, "; the render thread on core 6, its clock not sampled, its cluster cores 2-6 "
                    "at 1197 before") != NULL &&
          strstr(b, "%") == NULL);
    /* one block before with a CPU known: not due */
    {
        ClockTrapHist e;

        memset(&e, 0, sizeof(e));
        now.cpuMs = 9.5;
        clock_trap_hist_push(&e, &now);
        now.cpuMs = 0.0;
        clock_trap_hist_push(&e, &now); /* a block with no CPU known */
        CHECK(!clock_trap_due(&e, &now, 300, 300, 22.6, 6, 870.0, &d) && d.cpuBlocks == 1u);
    }
}

static void test_second(void)
{
    char b[1024];
    GpuSysfsRing ring;
    ClockTrapSecond s = sec(41, 30, 40, 12, true);
    const double t0 = 50002.0;

    s.t0Ms = t0;
    s.cpuMs = 504.3;
    s.cpuN = 41;
    owner_ring(&ring, t0);
    (void)clock_trap_second_text(&s, &ring, b, sizeof b);
    CHECK_STR(b, "41 pictures for 30 game frames (1.37 a tick), a picture's CPU 12.3 ms; the "
                 "step rule held 12 of 40 further pictures asked; the CPU clocks in that second "
                 "(4 samples, MHz): cores 0-1 460 812 1286 533, cores 2-6 400 884 2092 1100, "
                 "core 7 883 2707 1008 900");
    s.cpuN = 0;
    (void)clock_trap_second_text(&s, NULL, b, sizeof b);
    CHECK_STR(b, "41 pictures for 30 game frames (1.37 a tick), a picture's CPU not known; the "
                 "step rule held 12 of 40 further pictures asked; the CPU clocks not sampled "
                 "here");
    /* the blocks' CPU median over the blocks with one known: none gives 0;
       8.0, 9.0, 0 (not known) and 10.0 give 9.0 of 3 */
    {
        ClockTrapHist h;
        ClockTrapClusters c;
        unsigned n = 99;

        memset(&h, 0, sizeof(h));
        memset(&c, 0, sizeof(c));
        CHECK(clock_trap_cpu_median(&h, &n) == 0.0 && n == 0u);
        c.cpuMs = 8.0;
        clock_trap_hist_push(&h, &c);
        c.cpuMs = 9.0;
        clock_trap_hist_push(&h, &c);
        c.cpuMs = 0.0;
        clock_trap_hist_push(&h, &c);
        c.cpuMs = 10.0;
        clock_trap_hist_push(&h, &c);
        CHECK(clock_trap_cpu_median(&h, &n) == 9.0 && n == 3u);
        CHECK(clock_trap_cpu_median(&h, NULL) == 9.0);
    }
    /* raised: 1.25 x 8.0 = 10.0 at the edge; fewer than two blocks: any CPU
       known; none known: never */
    s.cpuMs = 300.0;
    s.cpuN = 30; /* 10.0 ms */
    CHECK(clock_trap_cpu_raised(&s, 8.0, 2));
    s.cpuMs = 299.0;
    CHECK(!clock_trap_cpu_raised(&s, 8.0, 2));
    CHECK(clock_trap_cpu_raised(&s, 8.0, 1));
    CHECK(clock_trap_cpu_raised(&s, 0.0, 0));
    s.cpuN = 0;
    CHECK(!clock_trap_cpu_raised(&s, 8.0, 0));
}

int main(void)
{
    test_rate();
    test_rule();
    test_seconds();
    test_target();
    test_dips();
    test_lines();
    test_due();
    test_second();
    if (failures) {
        printf("clock_trap_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("clock_trap_test: ok\n");
    return 0;
}
