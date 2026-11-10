/*
 * port/render/test/rd_perf_gap_test.c
 *
 * The GPU's time between two timed replays (rd_perf_gap.h, issue 123):
 *   first        the first timed replay has no gap;
 *   consecutive  a replay right after the one before: its first stamp less
 *                that one's last, nothing skipped;
 *   stall        a 340 ms idle GPU (a reporter's stall: the present blocked
 *                and the next replay began late) kept;
 *   long         1.5 s kept (the old one-second cut refused it), skipped 0;
 *   untimed      two replays without timestamps between: the gap kept,
 *                skipped 2;
 *   refused      61 s refused (a clock that jumped, a run in the
 *                background), the replay the last all the same, so the
 *                next one is measured from it;
 *   negative     a first stamp written before the one before ended: the
 *                gap under 0 kept;
 *   restart      a replay number not after the last (the counters started
 *                again): no gap, measured from it after.
 */
#include <stdio.h>
#include "rd_perf_gap.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define MS 1000000ull

int main(void)
{
    RdPerfGap g = {0, 0, 0, 0};
    int64_t gap = 7;
    uint32_t skipped = 7;

    /* first */
    CHECK(!rd_perf_gap_step(&g, 10, 1000 * MS, 1027 * MS, &gap, &skipped));
    CHECK(gap == 0 && skipped == 0 && g.lastReplay == 10 && g.lastNs == 1027 * MS);
    /* consecutive: 1.5 ms idle */
    CHECK(rd_perf_gap_step(&g, 11, 1027 * MS + 1500000ull, 1055 * MS, &gap, &skipped));
    CHECK(gap == 1500000ll && skipped == 0);
    /* stall: 340 ms idle */
    CHECK(rd_perf_gap_step(&g, 12, 1395 * MS, 1422 * MS, &gap, &skipped));
    CHECK(gap == 340000000ll && skipped == 0);
    /* long: 1.5 s */
    CHECK(rd_perf_gap_step(&g, 13, 2922 * MS, 2950 * MS, &gap, &skipped));
    CHECK(gap == 1500000000ll && skipped == 0);
    /* untimed: replays 14 and 15 had no stamps */
    CHECK(rd_perf_gap_step(&g, 16, 3010 * MS, 3037 * MS, &gap, &skipped));
    CHECK(gap == 60000000ll && skipped == 2);
    /* refused: 61 s */
    CHECK(!rd_perf_gap_step(&g, 17, 64037 * MS, 64064 * MS, &gap, &skipped));
    CHECK(gap == 0 && skipped == 0 && g.lastReplay == 17 && g.lastNs == 64064 * MS);
    /* negative: begun 3 ms before the one before ended */
    CHECK(rd_perf_gap_step(&g, 18, 64061 * MS, 64090 * MS, &gap, &skipped));
    CHECK(gap == -3000000ll && skipped == 0);
    /* 60 s exactly is refused, just under it kept */
    CHECK(!rd_perf_gap_step(&g, 19, 64090 * MS + 60000 * MS, 124100 * MS, &gap, &skipped));
    CHECK(rd_perf_gap_step(&g, 20, 124100 * MS + 59999 * MS, 184100 * MS, &gap, &skipped));
    CHECK(gap == 59999000000ll);
    /* restart: replay 1 after 20 */
    CHECK(!rd_perf_gap_step(&g, 1, 200000 * MS, 200027 * MS, &gap, &skipped));
    CHECK(rd_perf_gap_step(&g, 2, 200028 * MS, 200055 * MS, &gap, &skipped));
    CHECK(gap == 1000000ll && skipped == 0);
    /* issue 123: the Adreno's timestamp counter restarts at a stall. The
       logs' stalled replays printed "begun 20699.4 ms before the one before
       ended": the time the counter had run (20.7 s) since its last restart,
       not an overlap. A replay cannot begin before the one before began. */
    {
        RdPerfGap h = {0, 0, 0, 0};
        int64_t gp = 7;
        uint32_t sk = 7;
        uint64_t back = 7;

        CHECK(!rd_perf_gap_step_ex(&h, 784, 20672 * MS, 20699 * MS, &gp, &sk, &back));
        CHECK(back == 0);
        /* replay 785: its stamps in the restarted counter, 0.4 s on */
        CHECK(!rd_perf_gap_step_ex(&h, 785, 400 * MS, 472 * MS, &gp, &sk, &back));
        CHECK(gp == 0 && sk == 0 && back == 20699 * MS - 400 * MS && h.restarts == 1);
        /* the replay after: measured against the restarted counter */
        CHECK(rd_perf_gap_step_ex(&h, 786, 473 * MS, 523 * MS, &gp, &sk, &back));
        CHECK(gp == 1 * (int64_t)MS && back == 0 && h.restarts == 1);
        /* a real overlap (begun before the one before ENDED, after it began)
           is still a gap, not a restart */
        CHECK(rd_perf_gap_step_ex(&h, 787, 500 * MS, 560 * MS, &gp, &sk, &back));
        CHECK(gp == -23 * (int64_t)MS && back == 0 && h.restarts == 1);
        /* the wrapper reports a restart as no gap */
        CHECK(!rd_perf_gap_step(&h, 788, 10 * MS, 40 * MS, &gp, &sk) && h.restarts == 2);
    }
    printf("rd_perf_gap_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
