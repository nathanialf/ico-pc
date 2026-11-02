/*
 * port/render/test/rd_submit_queue_test.c
 *
 * The submit thread's queue (rd_submit_queue.h), driven on one thread as
 * rd_submit_thread.c drives it under its lock:
 *   order    jobs run in the order posted, numbered 1, 2, ...; an empty
 *            job (no list) is refused;
 *   depth    at most RD_SUBMIT_DEPTH queued and running: a third post is
 *            refused until one finishes; the running job is counted until
 *            it finishes (the queue is not idle while it runs), and a
 *            second next while one runs takes nothing;
 *   times    the finished jobs' times are taken oldest first, once; past
 *            RD_SUBMIT_DEPTH unread the oldest is dropped and counted;
 *   present  the longest present of the jobs finished since the last take,
 *            once; a job that presents nothing leaves it 0;
 *   block    the jobs' means, the longest, the depth, the waits by the
 *            render thread and the others, the lists submitted on the
 *            render thread; the sums cleared by the take, the depth
 *            carried over for the jobs still queued.
 */
#include <stdio.h>
#include <string.h>
#include "rd_submit_queue.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static RdSubmitJob job(uint32_t cl, bool present, uint32_t replay)
{
    RdSubmitJob j;
    memset(&j, 0, sizeof(j));
    j.cl = cl;
    j.present = present;
    j.replay = replay;
    return j;
}

/* the thread's step: the head run with these times */
static bool runOne(RdSubmitQ *q, double submitMs, double presentMs, RdSubmitJob *ran)
{
    RdSubmitJob j;
    if (!rd__sq_next(q, &j)) {
        return false;
    }
    j.submitMs = submitMs;
    j.presentMs = presentMs;
    rd__sq_finish(q, &j);
    if (ran) {
        *ran = j;
    }
    return true;
}

static void testOrderDepth(void)
{
    RdSubmitQ q;
    rd__sq_init(&q);
    CHECK(rd__sq_idle(&q));
    const RdSubmitJob empty = job(0, true, 1);
    CHECK(rd__sq_post(&q, &empty) == 0 && rd__sq_idle(&q));
    const RdSubmitJob a = job(11, true, 101), b = job(12, false, 102), c = job(13, true, 103);
    CHECK(rd__sq_post(&q, &a) == 1);
    CHECK(rd__sq_post(&q, &b) == 2);
    CHECK(rd__sq_post(&q, &c) == 0); /* RD_SUBMIT_DEPTH queued */
    CHECK(!rd__sq_idle(&q));
    RdSubmitJob r;
    CHECK(rd__sq_next(&q, &r) && r.cl == 11 && r.seq == 1 && r.replay == 101);
    {
        RdSubmitJob r2;
        CHECK(!rd__sq_next(&q, &r2)); /* one at a time */
    }
    CHECK(rd__sq_post(&q, &c) == 0); /* the running job still counts */
    r.submitMs = 3.0;
    r.presentMs = 0.5;
    rd__sq_finish(&q, &r);
    CHECK(!rd__sq_idle(&q) && q.done == 1);
    CHECK(rd__sq_post(&q, &c) == 3);
    RdSubmitJob ran;
    CHECK(runOne(&q, 2.0, 0.0, &ran) && ran.cl == 12 && ran.seq == 2);
    CHECK(runOne(&q, 4.0, 1.5, &ran) && ran.cl == 13 && ran.seq == 3);
    CHECK(rd__sq_idle(&q) && q.done == 3 && !runOne(&q, 0.0, 0.0, NULL));
    /* the times: the depth's newest two kept, the oldest dropped */
    RdSubmitJob f;
    CHECK(rd__sq_take_finished(&q, &f) && f.seq == 2 && f.submitMs == 2.0 && f.replay == 102);
    CHECK(rd__sq_take_finished(&q, &f) && f.seq == 3 && f.presentMs == 1.5 && f.replay == 103);
    CHECK(!rd__sq_take_finished(&q, &f));
    /* the longest present since the last take: 1.5 ms, then none */
    CHECK(rd__sq_take_present_ns(&q) == 1500000u);
    CHECK(rd__sq_take_present_ns(&q) == 0);
    RdSubmitBlock bl;
    rd__sq_note_wait(&q, 2.0, true);
    rd__sq_note_wait(&q, 1.0, false);
    rd__sq_note_wait(&q, -1.0, false); /* a clock step back counts as 0 */
    rd__sq_note_on_render(&q);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.jobs == 3 && bl.depthMax == RD_SUBMIT_DEPTH && bl.dropped == 1);
    CHECK(bl.submitMs == 3.0 && bl.presentMs > 0.66 && bl.presentMs < 0.67);
    CHECK(bl.longestMs == 5.5);
    CHECK(bl.waits == 3 && bl.renderWaits == 1 && bl.waitMs == 3.0 && bl.renderWaitMs == 2.0);
    CHECK(bl.waitMaxMs == 2.0 && bl.onRender == 1);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.jobs == 0 && bl.waits == 0 && bl.depthMax == 0 && bl.submitMs == 0.0);
}

static void testCarry(void)
{
    RdSubmitQ q;
    rd__sq_init(&q);
    const RdSubmitJob a = job(21, false, 7);
    CHECK(rd__sq_post(&q, &a) == 1);
    RdSubmitBlock bl;
    rd__sq_block_take(&q, &bl);
    CHECK(bl.depthMax == 1 && bl.jobs == 0);
    /* the queued job is the next block's depth too; a job that presents
       nothing gives no present time */
    CHECK(runOne(&q, 1.0, 0.25, NULL));
    CHECK(rd__sq_take_present_ns(&q) == 0);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.depthMax == 1 && bl.jobs == 1 && bl.submitMs == 1.0);
    /* a present of 0 ms still says it presented (1 ns) */
    const RdSubmitJob b = job(22, true, 8);
    CHECK(rd__sq_post(&q, &b) == 2);
    CHECK(runOne(&q, 1.0, 0.0, NULL));
    CHECK(rd__sq_take_present_ns(&q) == 1u);
}

int main(void)
{
    testOrderDepth();
    testCarry();
    printf("rd_submit_queue_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
