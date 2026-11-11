/*
 * port/render/test/rd_perf_clock_test.c
 *
 * A replay's GPU clock against the CPU's (rd_perf_clock.h, issue 123), from
 * made-up slot clocks (rhi.h RhiTimestampClock):
 *   stall      the issue 123 shape: the stamps of the slot's previous use
 *              still there at the wait's start (the GPU had not begun the
 *              replay), nothing for 250 ms, the replay's own stamps written
 *              and reset again (its first list ran a second time), the
 *              counter restarted before it, the calibrated clocks: the
 *              verdict and the line word for word;
 *   estimate   the same without calibrated clocks: the start from the
 *              wait's end; and calibrated clocks read across a counter
 *              restart (the pair older than the stamps) fall back to it;
 *              the single submission wait's long wait (no trace) is
 *              reported with its end after the last submit, a short one
 *              is not;
 *   missing    recorded stamps that came back unwritten (the first one
 *              named, and the last written), every one unwritten; a list
 *              never submitted: its stamps not recorded, the lists said
 *              apart;
 *   limits     the two limiters: eight idle restarts (issue 123's start-up
 *              had seven) then a stalled replay still gets its line, the
 *              stalls' 32 then every 10th, the restarts' 8 then every
 *              100th, each on its own count;
 *   quiet      a replay that had finished when the CPU came to wait: no line
 *              due, the backend's unrecorded indices ignored;
 *   short      a small buffer is cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "rd_perf_clock.h"

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

/* |a - b| under eps (no libm for the test) */
static int near(double a, double b, double eps)
{
    return a - b < eps && b - a < eps;
}

static void nameOf(uint32_t index, void *user, char *buf, size_t n)
{
    (void)user;
    snprintf(buf, n, "S%u", index);
}

/* six stamps 0..5 recorded in order, all written, 5 ms .. 88 ms on the GPU */
static const uint8_t kOrder[6] = {0, 1, 2, 3, 4, 5};
static uint64_t s_ts[6];

static void sixStamps(RhiTimestampClock *c)
{
    memset(c, 0, sizeof(*c));
    for (uint32_t i = 0; i < 6; i++) {
        rhi_ts_add(c->recorded, i);
        rhi_ts_add(c->available, i);
    }
    const uint64_t t[6] = {5000000, 20000000, 40000000, 60000000, 80000000, 88000000};
    memcpy(s_ts, t, sizeof s_ts);
    c->submits = 2;
    c->firstSubmitNs = 1000000000ull;
    c->lastSubmitNs = 1002000000ull;
}

static RdPerfClockIn inOf(const RhiTimestampClock *c, uint64_t backNs)
{
    const RdPerfClockIn in = {c, s_ts, 6, kOrder, 6, 2021, backNs, nameOf, NULL};
    return in;
}

static void testStall(void)
{
    RhiTimestampClock c;
    sixStamps(&c);
    c.calibrated = true;
    c.gpuNs = 90000000ull;   /* the counter 90 ms since its restart ... */
    c.cpuNs = 1381000000ull; /* ... at 1.381 s on the CPU's clock */
    c.deviationNs = 2500;
    c.waitBeginNs = 1040000000ull;
    c.waitEndNs = 1380000000ull;
    c.traceN = 3;
    /* the previous use's six at +50 ms, nothing until +300 (none finished) */
    c.trace[0].atUs = 50000;
    c.trace[0].avail = 6;
    c.trace[0].stale = 1;
    c.trace[1].atUs = 300000;
    c.trace[1].avail = 3;
    c.trace[2].atUs = 302000;
    c.trace[2].avail = 1;
    c.trace[2].finished = 1;
    for (uint32_t i = 0; i < 3; i++) {
        rhi_ts_add(c.trace[1].bits, i);
    }
    c.stillUs = 250000;
    c.stillFromUs = 50000;
    c.stillAvail = 6;
    c.stillStale = true;
    c.again = true;
    c.againAtUs = 302000;
    c.againFrom = 3;
    const RdPerfClockIn in = inOf(&c, 20699400000ull);
    RdPerfClockVerdict v;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.recorded == 6 && v.missing == 0 && v.firstMissing == -1);
    CHECK(v.lateValid == 1 && near(v.lateMs, 296.0, 1e-6));
    CHECK(near(v.counterMs, 5.0, 1e-9) && near(v.waitMs, 340.0, 1e-6));
    CHECK(v.ranTwice && v.report);
    char line[2048];
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK_STR(line,
              "GPU clock: replay 2021: its first stamp 296.0 ms after its first submit (the "
              "calibrated clocks, within 2.5 us), the counter at 5.0 ms then (it went back 20699.4 "
              "ms since the replay before: a restart); 2 submissions; waited 340.0 ms for it from "
              "38.0 ms after its last submit: at +50.0 ms 6 of 6 written (the previous use's: its "
              "reset had not run, the GPU had not begun it), none of its submissions finished; "
              "nothing changed for 250.0 ms from +50.0 ms with 6 written (the previous use's); its "
              "written stamps fell from 3 at +302.0 ms: its queries were reset again, its first "
              "list ran a second time (a reset and replay of the work in flight); points 6s@+50.0 "
              "3@+300.0 1@+302.0; all 6 recorded stamps written");

    /* a trace point that is the replay's own names its last stamp */
    c.trace[0].stale = 0;
    memcpy(c.trace[0].bits, c.trace[1].bits, sizeof c.trace[0].bits);
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "at +50.0 ms 6 of 6 written (the last: S2), none of") != NULL);
}

static void testEstimate(void)
{
    RhiTimestampClock c;
    sixStamps(&c);
    c.waitBeginNs = 1040000000ull;
    c.waitEndNs = 1380000000ull;
    RdPerfClockIn in = inOf(&c, 0);
    RdPerfClockVerdict v;
    rd_perf_clock_judge(&in, &v);
    /* 1.380 s less the 83 ms span is 1.297 s: 297 ms after the submit */
    CHECK(v.lateValid == 2 && near(v.lateMs, 297.0, 1e-6));
    /* a long wait with no trace: the single submission wait's, reported */
    CHECK(v.report == true);
    CHECK(v.doneValid == 1 && near(v.doneMs, 378.0, 1e-6));
    char line[2048];
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "its first stamp about 297.0 ms after its first submit (from its wait's "
                       "end), the counter at 5.0 ms then; 2 submissions; waited 340.0 ms for it "
                       "from 38.0 ms after its last submit (no trace: the single submission wait "
                       "reads nothing of its work while it runs); its work done about 378.0 ms "
                       "after its last submit (the wait's end); all 6") != NULL);
    /* a wait under the trace's start: not reported, the old words */
    c.waitEndNs = 1070000000ull;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.report == false && v.doneValid == 1 && near(v.doneMs, 68.0, 1e-6));
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "waited 30.0 ms for it from 38.0 ms after its last submit (no trace: the "
                       "wait did not pass 50 ms); all 6") != NULL);
    /* the work finished before the wait: the wait's start bounds its end */
    c.waitEndNs = c.waitBeginNs;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.doneValid == 2 && near(v.doneMs, 38.0, 1e-6) && v.waitMs == 0.0);
    c.waitEndNs = 1380000000ull;

    /* calibrated clocks older than the stamps (the counter restarted after
       the pair): the wait's end instead */
    c.calibrated = true;
    c.gpuNs = 1000000ull;
    c.cpuNs = 1381000000ull;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.lateValid == 2 && near(v.lateMs, 297.0, 1e-6));
    /* and with no wait either: not placed */
    c.waitBeginNs = c.waitEndNs = 0;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.lateValid == 0 && v.waitMs == 0.0);
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "its first stamp not placed on the CPU's clock (the counter restarted "
                       "after it)") != NULL);
    CHECK(strstr(line, "its work had finished when the CPU came to wait for it") != NULL);
}

static void testMissing(void)
{
    RhiTimestampClock c;
    sixStamps(&c);
    /* 3 and 5 recorded, not written */
    c.available[0] &= ~((1u << 3) | (1u << 5));
    s_ts[3] = s_ts[5] = 0;
    RdPerfClockIn in = inOf(&c, 0);
    RdPerfClockVerdict v;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.missing == 2 && v.firstMissing == 3 && v.report);
    char line[2048];
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "; 2 of 6 recorded stamps came back unwritten after its work was signalled "
                       "finished (the first: S3; the last written: S4)") != NULL);
    CHECK(v.lastWritten == 4 && v.stall);
    /* written only up to S2: the stop between S2 and S3 */
    c.available[0] &= ~(1u << 4);
    s_ts[4] = 0;
    rd_perf_clock_judge(&in, &v);
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "(the first: S3; the last written: S2)") != NULL);
    /* none written */
    memset(c.available, 0, sizeof c.available);
    memset(s_ts, 0, sizeof s_ts);
    rd_perf_clock_judge(&in, &v);
    CHECK(v.missing == 6 && v.firstMissing == 0 && v.lateValid == 0 && v.lastWritten == -1);
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "(the first: S0): none written") != NULL);
    /* a list with stamps recorded and never submitted (vk_cmd.c: its stamps
       are not among the recorded, so not unwritten): said apart */
    sixStamps(&c);
    c.recorded[0] &= ~((1u << 4) | (1u << 5));
    c.available[0] &= ~((1u << 4) | (1u << 5));
    c.listsUnsubmitted = 1;
    in = inOf(&c, 0);
    rd_perf_clock_judge(&in, &v);
    CHECK(v.recorded == 4 && v.missing == 0);
    rd_perf_clock_text(&in, &v, line, sizeof line);
    CHECK(strstr(line, "; all 4 recorded stamps written; 1 of the slot's lists with stamps were "
                       "recorded and not submitted (left out)") != NULL);
}

static void testQuietAndShort(void)
{
    RhiTimestampClock c;
    sixStamps(&c);
    /* an index the renderer asked for and the backend did not record */
    const uint8_t order[7] = {0, 1, 2, 3, 4, 5, 9};
    RdPerfClockIn in = inOf(&c, 0);
    in.order = order;
    in.orderN = 7;
    RdPerfClockVerdict v;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.recorded == 6 && v.missing == 0 && !v.report && v.lateValid == 0);
    char small[16];
    memset(small, 'x', sizeof small);
    const int w = rd_perf_clock_text(&in, &v, small, sizeof small);
    CHECK(w > (int)sizeof small && strlen(small) == sizeof small - 1);
    CHECK(strncmp(small, "GPU clock: repl", sizeof small - 1) == 0);
}

static void testLimits(void)
{
    RdPerfClockLines l = {0, 0};
    RdPerfClockVerdict idle, stall, quiet;
    memset(&idle, 0, sizeof idle);
    idle.report = true; /* a restart alone */
    stall = idle;
    stall.stall = true;
    memset(&quiet, 0, sizeof quiet);
    CHECK(rd_perf_clock_due(&l, &quiet) == 0 && l.stalls == 0 && l.restarts == 0);
    /* eight restarts at start-up use up their own first 8 */
    for (uint32_t k = 1; k <= 8; k++) {
        CHECK(rd_perf_clock_due(&l, &idle) == k);
    }
    CHECK(rd_perf_clock_due(&l, &idle) == 0);
    /* the stall after them is still written, as its kind's first */
    CHECK(rd_perf_clock_due(&l, &stall) == 1);
    for (uint32_t k = 2; k <= 32; k++) {
        CHECK(rd_perf_clock_due(&l, &stall) == k);
    }
    uint32_t written = 0;
    for (uint32_t k = 33; k <= 100; k++) {
        const uint32_t d = rd_perf_clock_due(&l, &stall);
        CHECK(d == 0 || (d == k && k % 10u == 0u));
        written += d != 0;
    }
    CHECK(written == 7 && l.stalls == 100); /* 40, 50, ..., 100 */
    /* the restarts' count went on alone: 9 so far, the 100th written */
    CHECK(l.restarts == 9);
    for (uint32_t k = 10; k < 100; k++) {
        CHECK(rd_perf_clock_due(&l, &idle) == 0);
    }
    CHECK(rd_perf_clock_due(&l, &idle) == 100);
    /* a judged verdict: a long wait is a stall, a restart alone is not */
    RhiTimestampClock c;
    sixStamps(&c);
    c.waitBeginNs = c.lastSubmitNs + 1000000ull;
    c.waitEndNs = c.waitBeginNs + 150000000ull;
    RdPerfClockIn in = inOf(&c, 0);
    RdPerfClockVerdict v;
    rd_perf_clock_judge(&in, &v);
    CHECK(v.stall && v.report);
    sixStamps(&c);
    in = inOf(&c, 2000000ull);
    rd_perf_clock_judge(&in, &v);
    CHECK(!v.stall && v.report && v.lastWritten == 5);
}

int main(void)
{
    testStall();
    testEstimate();
    testMissing();
    testQuietAndShort();
    testLimits();
    printf("rd_perf_clock_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
