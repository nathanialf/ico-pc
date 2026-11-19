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
 *            RD_SUBMIT_DEPTH unread the oldest is dropped and counted; the
 *            present timed apart (the lock, the driver's call, the poll,
 *            the end: issue 123) comes with them;
 *   present  the longest present of the jobs finished since the last take,
 *            once; a job that presents nothing leaves it 0;
 *   block    the jobs' means, the longest, the depth, the waits by the
 *            render thread and the others, the lists submitted on the
 *            render thread, the pictures begun beside the other slot's
 *            job; the sums cleared by the take, the depth carried over for
 *            the jobs still queued;
 *   slots    the jobs queued and running of each frame slot (the render
 *            thread's wait for the slot it recycles, the slots apart): a
 *            running job counted until it finishes, a queued one of the
 *            other slot not counted for this one;
 *   tail     a presenting picture's two lists (issue 179: only the
 *            present's own list waits for the window's image): the job
 *            carries both through the queue, and its tail ends and submits
 *            the scene's list, then the present's, then presents; a job of
 *            one list ends and submits it and presents only when asked;
 *   gpu end  the GPU's end first's timed presents (rd__sq_gf_class): the
 *            classes from the evidence (issue 179's v0.11.12 log line 562,
 *            the owner's Pixel's short calls, a long call while the picture
 *            before ran), the round's first, the edges (2 ms, 1 ms); a
 *            failed wait (VK_NOT_READY, a loss, no such wait) is unknown
 *            whatever the times; the block counts each finished job's
 *            sample by class, the last unknown's result, and none where no
 *            job had one.
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
    j.slot = (uint8_t)(replay % 2u);
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
    {
        /* the present timed apart, as rd_submit_thread.c tail fills it */
        RdSubmitJob j;
        CHECK(rd__sq_next(&q, &j) && j.cl == 13 && j.seq == 3);
        j.submitMs = 4.0;
        j.presentMs = 1.5;
        j.presentSplit = true;
        j.presentLockMs = 0.25;
        j.presentCallMs = 1.125;
        j.presentPollMs = 0.0625;
        j.presentEndMs = 123456.5;
        rd__sq_finish(&q, &j);
    }
    CHECK(rd__sq_idle(&q) && q.done == 3 && !runOne(&q, 0.0, 0.0, NULL));
    /* the times: the depth's newest two kept, the oldest dropped */
    RdSubmitJob f;
    CHECK(rd__sq_take_finished(&q, &f) && f.seq == 2 && f.submitMs == 2.0 && f.replay == 102);
    CHECK(rd__sq_take_finished(&q, &f) && f.seq == 3 && f.presentMs == 1.5 && f.replay == 103);
    CHECK(f.presentSplit && f.presentLockMs == 0.25 && f.presentCallMs == 1.125 &&
          f.presentPollMs == 0.0625 && f.presentEndMs == 123456.5);
    CHECK(!rd__sq_take_finished(&q, &f));
    /* the longest present since the last take: 1.5 ms, then none */
    CHECK(rd__sq_take_present_ns(&q) == 1500000u);
    CHECK(rd__sq_take_present_ns(&q) == 0);
    RdSubmitBlock bl;
    rd__sq_note_wait(&q, 2.0, true);
    rd__sq_note_wait(&q, 1.0, false);
    rd__sq_note_wait(&q, -1.0, false); /* a clock step back counts as 0 */
    rd__sq_note_on_render(&q);
    rd__sq_note_overlap(&q);
    rd__sq_note_overlap(&q);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.overlapped == 2);
    CHECK(bl.jobs == 3 && bl.depthMax == RD_SUBMIT_DEPTH && bl.dropped == 1);
    CHECK(bl.submitMs == 3.0 && bl.presentMs > 0.66 && bl.presentMs < 0.67);
    CHECK(bl.longestMs == 5.5);
    CHECK(bl.waits == 3 && bl.renderWaits == 1 && bl.waitMs == 3.0 && bl.renderWaitMs == 2.0);
    CHECK(bl.waitMaxMs == 2.0 && bl.onRender == 1);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.jobs == 0 && bl.waits == 0 && bl.depthMax == 0 && bl.submitMs == 0.0);
    CHECK(bl.overlapped == 0);
}

/* the slots apart (rd_submit_thread.h): picture N of slot 0 running, N+1
   of slot 1 queued; the render thread's picture N+2 recycles slot 0 and
   waits for N alone, N+3 recycles slot 1 and waits for N+1 */
static void testSlots(void)
{
    RdSubmitQ q;
    rd__sq_init(&q);
    CHECK(rd__sq_slot_jobs(&q, 0) == 0 && rd__sq_slot_jobs(&q, 1) == 0);
    const RdSubmitJob n0 = job(31, true, 40); /* slot 0 */
    const RdSubmitJob n1 = job(32, true, 41); /* slot 1 */
    CHECK(rd__sq_post(&q, &n0) == 1);
    CHECK(rd__sq_slot_jobs(&q, 0) == 1 && rd__sq_slot_jobs(&q, 1) == 0);
    RdSubmitJob j;
    CHECK(rd__sq_next(&q, &j) && j.slot == 0);
    /* running: still counted for its slot */
    CHECK(rd__sq_slot_jobs(&q, 0) == 1);
    CHECK(rd__sq_post(&q, &n1) == 2);
    CHECK(rd__sq_slot_jobs(&q, 0) == 1 && rd__sq_slot_jobs(&q, 1) == 1);
    j.submitMs = 4.0;
    j.presentMs = 2.0;
    rd__sq_finish(&q, &j);
    /* slot 0 free while slot 1's job is queued: N+2 may begin */
    CHECK(rd__sq_slot_jobs(&q, 0) == 0 && rd__sq_slot_jobs(&q, 1) == 1 && !rd__sq_idle(&q));
    CHECK(runOne(&q, 4.0, 2.0, NULL));
    CHECK(rd__sq_slot_jobs(&q, 1) == 0 && rd__sq_idle(&q));
    /* a slot past the ring's entries counts none */
    CHECK(rd__sq_slot_jobs(&q, 7) == 0);
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

static void testTail(void)
{
    RdSubmitQ q;
    rd__sq_init(&q);
    RdSubmitJob two = job(41, true, 50);
    two.cl2 = 42;
    CHECK(rd__sq_post(&q, &two) == 1);
    RdSubmitJob j;
    CHECK(rd__sq_next(&q, &j) && j.cl == 41 && j.cl2 == 42 && j.present);
    RdSqStep st[RD_SQ_TAIL_MAX];
    uint32_t n = rd__sq_tail_steps(&j, st);
    CHECK(n == 5);
    CHECK(st[0].what == RD_SQ_END && st[0].cl == 41);
    CHECK(st[1].what == RD_SQ_SUBMIT && st[1].cl == 41);
    CHECK(st[2].what == RD_SQ_END && st[2].cl == 42);
    CHECK(st[3].what == RD_SQ_SUBMIT && st[3].cl == 42);
    CHECK(st[4].what == RD_SQ_PRESENT && st[4].cl == 0);
    rd__sq_finish(&q, &j);
    RdSubmitJob f;
    CHECK(rd__sq_take_finished(&q, &f) && f.cl == 41 && f.cl2 == 42);
    /* one list (a replay that does not present, or a present that found no
       second list): its end and submit, no present */
    const RdSubmitJob one = job(43, false, 51);
    n = rd__sq_tail_steps(&one, st);
    CHECK(n == 2 && st[0].what == RD_SQ_END && st[0].cl == 43 && st[1].what == RD_SQ_SUBMIT &&
          st[1].cl == 43);
    /* one list that presents (a backend's present of a picture recorded in
       one list): end, submit, present */
    const RdSubmitJob onePresent = job(44, true, 52);
    n = rd__sq_tail_steps(&onePresent, st);
    CHECK(n == 3 && st[1].what == RD_SQ_SUBMIT && st[2].what == RD_SQ_PRESENT);
}

/* one job through the queue with its sample (RdSubmitJob.gfSample) */
static void runSample(RdSubmitQ *q, uint8_t cls, int32_t result)
{
    const RdSubmitJob a = job(61, true, 70);
    CHECK(rd__sq_post(q, &a) != 0);
    RdSubmitJob j;
    CHECK(rd__sq_next(q, &j));
    j.gfSample = cls;
    j.gfResult = result;
    rd__sq_finish(q, &j);
}

static void testGpuFirst(void)
{
    /* line 562: a 140.3 ms call, the picture before found ended at once
       after it: at the end of GPU work */
    CHECK(rd__sq_gf_class(140.3, true, 0, 0.05) == RD_GF_WAITED);
    /* a present that returns at its own end (the picture before ended
       earlier still) */
    CHECK(rd__sq_gf_class(45.0, true, 0, 0.0) == RD_GF_WAITED);
    /* a 5 ms call while the picture before still ran 20 ms: early */
    CHECK(rd__sq_gf_class(5.0, true, 0, 20.0) == RD_GF_EARLY);
    /* the owner's Pixel: a 0.6 ms call is short, whatever else */
    CHECK(rd__sq_gf_class(0.6, true, 0, 0.0) == RD_GF_SHORT);
    CHECK(rd__sq_gf_class(0.6, false, 1, 0.0) == RD_GF_SHORT);
    /* the round's first long call: no picture before */
    CHECK(rd__sq_gf_class(140.3, false, 0, 0.0) == RD_GF_NO_PREV);
    /* the edges: 2 ms is long, a wait of 1 ms is early */
    CHECK(rd__sq_gf_class(RD_GPU_FIRST_CALL_MS, true, 0, 0.99) == RD_GF_WAITED);
    CHECK(rd__sq_gf_class(1.99, true, 0, 0.0) == RD_GF_SHORT);
    CHECK(rd__sq_gf_class(2.0, true, 0, RD_GPU_FIRST_NEAR_MS) == RD_GF_EARLY);
    /* a failed wait: unknown, never a class (VK_NOT_READY 1, VK_TIMEOUT 2,
       VK_ERROR_DEVICE_LOST -4, rhi.h RHI_WAIT_NONE) */
    CHECK(rd__sq_gf_class(140.3, true, 1, 0.05) == RD_GF_UNKNOWN);
    CHECK(rd__sq_gf_class(5.0, true, 2, 20.0) == RD_GF_UNKNOWN);
    CHECK(rd__sq_gf_class(45.0, true, -4, 0.0) == RD_GF_UNKNOWN);
    CHECK(rd__sq_gf_class(45.0, true, INT32_MIN, 0.0) == RD_GF_UNKNOWN);

    RdSubmitQ q;
    rd__sq_init(&q);
    RdSubmitBlock bl;
    runSample(&q, RD_GF_NONE, 0);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.jobs == 1 && bl.gfJobs == 0 && bl.gfTimed == 0 && bl.gfCounted == 0);
    runSample(&q, RD_GF_WAITED, 0);
    runSample(&q, RD_GF_WAITED, 0);
    runSample(&q, RD_GF_EARLY, 0);
    runSample(&q, RD_GF_SHORT, 0);
    runSample(&q, RD_GF_NO_PREV, 0);
    runSample(&q, RD_GF_UNKNOWN, -4);
    runSample(&q, RD_GF_UNKNOWN, 1);
    runSample(&q, RD_GF_NOT_ORIGINAL, 0);
    runSample(&q, RD_GF_NO_PRESENT, 0);
    runSample(&q, RD_GF_NONE, 0);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.jobs == 10 && bl.gfJobs == 9 && bl.gfTimed == 7 && bl.gfShort == 1);
    CHECK(bl.gfCounted == 3 && bl.gfWaited == 2 && bl.gfUnknown == 2 && bl.gfLastResult == 1);
    CHECK(bl.gfNotOriginal == 1 && bl.gfNoPresent == 1);
    rd__sq_block_take(&q, &bl);
    CHECK(bl.gfJobs == 0 && bl.gfTimed == 0 && bl.gfUnknown == 0 && bl.gfLastResult == 0);
    /* a post clears what a job brought in its sample */
    RdSubmitJob dirty = job(62, true, 71);
    dirty.gfSample = RD_GF_WAITED;
    dirty.gfResult = 7;
    CHECK(rd__sq_post(&q, &dirty) != 0);
    RdSubmitJob j;
    CHECK(rd__sq_next(&q, &j) && j.gfSample == RD_GF_NONE && j.gfResult == 0);
    rd__sq_finish(&q, &j);
}

int main(void)
{
    testOrderDepth();
    testGpuFirst();
    testCarry();
    testSlots();
    testTail();
    printf("rd_submit_queue_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
