/*
 * port/render/rd_submit_queue.h
 *
 * The submit thread's queue (rd_submit_thread.h): the pictures the render
 * thread recorded whose list is still to be ended and submitted and whose
 * image is still to be presented, in the order posted, at most
 * RD_SUBMIT_DEPTH of them (queued and the one running); their times once
 * finished, kept until the thread that waits for them takes them (the
 * replay's perf record, the 10 s line); the 10 s block's sums.
 *
 *   post    rd__sq_post: a job at the tail, its number (1, 2, ...); 0 when
 *           the queue is full (the poster waits for a finish and posts
 *           again) or the job is empty;
 *   run     rd__sq_next: the head, when nothing runs; it stays counted until
 *           rd__sq_finish, so a queue with a job running is not idle;
 *   finish  rd__sq_finish: the running job done with its times: counted in
 *           the block (with what it gave the GPU's end first's verdict),
 *           kept for rd__sq_take_finished (the oldest dropped, and
 *           counted, past RD_SUBMIT_DEPTH unread), its present time for
 *           rd__sq_take_present_ns;
 *   slots   rd__sq_slot_jobs: the jobs queued or running of one frame slot
 *           (RdSubmitJob.slot): with the slots apart the render thread's
 *           next picture waits for those of the slot it recycles only
 *           (rd_submit_thread.h);
 *   waits   rd__sq_note_wait: another thread waited for the queue to empty,
 *           or for a slot's jobs (the render thread at its next picture's
 *           fence wait, the game thread at a drain); rd__sq_note_on_render:
 *           a list the render thread ended and submitted itself;
 *           rd__sq_note_overlap: a picture of the render thread began its
 *           graphics calls while a job of the other slot still ran;
 *   block   rd__sq_block_take: the block's numbers, the sums cleared;
 *   tail    rd__sq_tail_steps: a job's graphics calls in order (the submit
 *           thread's tail and rd_submit.c's inline path run them): end and
 *           submit cl, end and submit cl2, present;
 *   ahead   rd__sq_take_unsent / rd__sq_finish_sent: the ahead order's two
 *           jobs submitted at once, finished in order; its block counts, the
 *           present-call guard and the checks that turn it off (below).
 *
 * Pure C (no threads, no graphics calls): the caller holds its own lock
 * around every call.  The unit test is port/render/test/rd_submit_queue_test.c.
 */
#ifndef RD_SUBMIT_QUEUE_H
#define RD_SUBMIT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the pictures submitted and not yet presented the queue holds: the GPU's
   backlog stays the present allowance's business (pace_policy.h), as with
   the submit on the render thread */
#define RD_SUBMIT_DEPTH 2u

typedef struct RdSubmitJob {
    uint32_t cl; /* the list (RhiCommandList.id), recorded and left open */
    /* a presenting picture's second list, the present's pass alone (0:
       none), submitted after cl: only its submit waits for the window's
       image (rd_submit.c rd__submit_frame, port/rhi/vk/vk_cmd.c
       vkr_submit_batch) */
    uint32_t cl2;
    uint8_t slot; /* its frame slot (rhi_frame_slot when it was recorded) */
    bool present; /* its image is presented after the submit */
    /* the GPU's end waited for before the present (rd_submit_thread.h "the
       GPU's end first": a Frame rate Original picture with the rule on) */
    bool gpuFirst;
    uint32_t replay;  /* its replay's perf record (RdPerfRecord.replay) */
    uint64_t seq;     /* rd__sq_post's number */
    double submitMs;  /* the end and the submit (rhi_end_commands, rhi_submit) */
    double presentMs; /* the present (rhi_present) */
    /* the present timed apart (rhi.h rhi_present_split; presentSplit 0
       where the backend does not, 1 timed with the driver's call, 2 the
       lock's wait only, no present call made): the swapchain lock's wait,
       the driver's present call, the surface poll after it; and presentEndMs the
       present's end on rd's monotonic ms clock (rd__now_ms: the window's
       kernel GPU driver samples are on the same clock), 0 when nothing was
       presented (issue 123) */
    uint8_t presentSplit;
    double presentLockMs, presentCallMs, presentPollMs, presentEndMs;
    /* the display-times id the present carried (rhi_present_glass; 0 none) */
    uint32_t glassId;
    /* the present's desired time (the even cadence's, rd_pace_even.h; the
       display's clock, ms; 0 none): rhi_present_desire before it */
    double desiredMs;
    /* what the job gave the GPU's end first's verdict while it was not
       taken (rd_submit_thread.c tail; RD_GF_*, RD_GF_NONE where the verdict
       was taken or is not wanted) and, for RD_GF_UNKNOWN, the failed wait's
       result (rhi.h rhi_wait_value_ns) */
    uint8_t gfSample;
    int32_t gfResult;
    /* and the counter's read of the picture before as its present call
       began (rhi_submitted_finished: 1 finished, 0 running, -1 the read
       failed; -2 not read) */
    int8_t gfPrevRead;
} RdSubmitJob;

/* RdSubmitJob.gfSample: none; a job not of Frame rate Original; an
   Original job without a present; a timed present under
   RD_GPU_FIRST_CALL_MS (short: no wait, not counted); a long one with no
   present job before it (the round's first: not counted); a long one
   whose picture before had already finished as it began (prev done: not
   counted, no wait); a sample whose
   wait failed (unknown); a counted sample, its present returned at the
   end of the picture before's GPU work (waited) or not (early) */
enum {
    RD_GF_NONE,
    RD_GF_NOT_ORIGINAL,
    RD_GF_NO_PRESENT,
    RD_GF_SHORT,
    RD_GF_NO_PREV,
    RD_GF_PREV_DONE,
    RD_GF_UNKNOWN,
    RD_GF_WAITED,
    RD_GF_EARLY
};

/* the 10 s block (rd__sq_block_take): the jobs finished and their means in
   ms, the longest job, the most jobs queued and running at once, the other
   threads' waits for the queue (byRender: the render thread's), the jobs
   whose finished times were dropped unread, the lists the render thread
   ended and submitted itself instead (rd__sq_note_on_render: a call of its
   own needed the backend before its job's end), the render thread's
   pictures begun beside a job of the other slot (rd__sq_note_overlap) */
typedef struct RdSubmitBlock {
    unsigned jobs;
    double submitMs, presentMs, longestMs;
    unsigned depthMax;
    unsigned waits, renderWaits;
    double waitMs, renderWaitMs, waitMaxMs;
    unsigned dropped, onRender;
    unsigned overlapped;
    /* the jobs finished while the GPU's end first's verdict was not taken
       (gfSample not RD_GF_NONE; 0: none, the 10 s line leaves them out),
       by gfSample: the presents timed (short, no picture before, unknown,
       waited and early), of them short, counted (waited and early, and of
       those waited), unknown with the last one's result; the jobs not
       Original, without a present (or whose present made no driver call);
       the long calls whose picture before had already finished, and the
       timed presents whose read of it failed (gfPrevRead -1) */
    unsigned gfJobs, gfTimed, gfShort, gfCounted, gfWaited, gfUnknown, gfNotOriginal, gfNoPresent;
    unsigned gfPrevDone, gfReadFailed;
    int32_t gfLastResult;
    /* the next picture submitted ahead (rd_submit_thread.h "ahead"; 0
       everywhere where that order did not run in the block): the jobs
       submitted in it with a picture before in the chain (aheadJobs) and of
       them those whose submit ended before the picture before's GPU end was
       seen (aheadEarly: "p of q"); the GPU's idle gaps (a next picture
       submitted after the picture before's end: gaps, their time in all and
       its parts, rd__sq_note_gap), of them those whose next picture was not
       yet posted when the picture before was finished (gapNotPosted); the
       present calls made with a later picture submitted (guardTimed), of
       them those of RD_GPU_FIRST_CALL_MS or more (guardLong), the longest */
    unsigned aheadJobs, aheadEarly;
    unsigned gaps, gapNotPosted;
    double gapMs, gapWakeMs, gapPresentMs, gapFinishMs, gapPostMs, gapEndMs, gapSubmitMs;
    unsigned guardTimed, guardLong;
    double guardMaxMs;
} RdSubmitBlock;

typedef struct RdSubmitQ {
    RdSubmitJob ring[RD_SUBMIT_DEPTH];
    uint32_t head, count; /* count: queued and running */
    bool running;
    /* the ahead order (rd__sq_take_unsent): the jobs from the head ended and
       submitted, not yet finished (0 in the one-at-a-time order) */
    uint32_t sent;
    uint64_t posted, done;
    RdSubmitJob fin[RD_SUBMIT_DEPTH];
    uint32_t finN;
    uint64_t presentNs; /* the longest present finished since rd__sq_take_present_ns */
    RdSubmitBlock block;
} RdSubmitQ;

void rd__sq_init(RdSubmitQ *q);
uint64_t rd__sq_post(RdSubmitQ *q, const RdSubmitJob *job);
/* the head to run one at a time: none while a job runs or while jobs of the
   ahead order are submitted and unfinished (sent) */
bool rd__sq_next(RdSubmitQ *q, RdSubmitJob *out);
void rd__sq_finish(RdSubmitQ *q, const RdSubmitJob *job);

/* The ahead order (rd_submit_thread.h "ahead"): several jobs from the head
 * ended and submitted before the oldest of them is presented, each finished
 * in the order posted.
 *   peek    rd__sq_peek_unsent: the oldest job posted and not yet submitted
 *           (NULL: none, or a job runs one at a time);
 *   take    rd__sq_take_unsent: that job, counted as submitted (sent);
 *   finish  rd__sq_finish_sent: the oldest submitted job done with its times,
 *           counted as rd__sq_finish counts (the head moves on);
 *   sent    rd__sq_sent: the jobs submitted and not finished. */
const RdSubmitJob *rd__sq_peek_unsent(const RdSubmitQ *q);
bool rd__sq_take_unsent(RdSubmitQ *q, RdSubmitJob *out);
void rd__sq_finish_sent(RdSubmitQ *q, const RdSubmitJob *job);
uint32_t rd__sq_sent(const RdSubmitQ *q);

/* A job submitted in the ahead order with a picture before in the chain:
 * its submit ended before that picture's GPU end was seen (early) or not */
void rd__sq_note_ahead(RdSubmitQ *q, bool early);

/* One GPU idle gap of the ahead order: from the picture before's GPU end
 * (as its wait returned) to the end of the next picture's submit, in
 * parts: the submit thread's wake-up, the picture before's present call,
 * its finish, the wait for the next picture to be posted (notPosted: none
 * was posted when the finish ended), the next's list ends and its submits.
 * rd__sq_gap_parts: the parts from the times on rd's ms clock (endSeen the
 * wait's return; wake, presented, finished: the submit thread's times
 * after it, 0 where the next was taken before the wake-up; taken, ended,
 * submitted: the next's) */
typedef struct RdSqGap {
    double wakeMs, presentMs, finishMs, postMs, endMs, submitMs;
} RdSqGap;

void rd__sq_gap_parts(double endSeen, double wake, double presented, double finished, double taken,
                      double ended, double submitted, RdSqGap *out);
void rd__sq_note_gap(RdSubmitQ *q, const RdSqGap *g, bool notPosted);
/* a present call made with a later picture submitted, its time (the
   guard's input, RD_GPU_FIRST_CALL_MS or more is long) */
void rd__sq_note_guard(RdSubmitQ *q, double callMs);

/* The present-call guard of the ahead order: of the last
 * RD_AHEAD_GUARD_WINDOW present calls made with a later picture submitted,
 * RD_AHEAD_GUARD_LONG or more of RD_GPU_FIRST_CALL_MS or more put the run
 * back on one picture at a time (a driver whose present waits for all the
 * work submitted, not only its own picture's: there each present would
 * wait for the next picture's GPU work).  rd__sq_guard_note: one call into
 * the window, true when the window now holds that many long ones (the
 * caller turns the order off; the guard keeps counting) */
#define RD_AHEAD_GUARD_WINDOW 16u
#define RD_AHEAD_GUARD_LONG 4u

typedef struct RdSqGuard {
    uint16_t bits; /* the window's calls, newest in bit 0: 1 long */
    uint8_t n;     /* calls in the window (at most RD_AHEAD_GUARD_WINDOW) */
    uint8_t longN; /* long calls in the window */
    unsigned calls, longs;
} RdSqGuard;

bool rd__sq_guard_note(RdSqGuard *g, double callMs);

/* What the ahead order's checks found after one of its steps (the first
 * reason found, RD_AHEAD_OFF_NONE: go on): the device lost; a submit that
 * moved the submission counter by nothing (before, after: rhi_last_submitted
 * around the job's submits; the counter moves only on a submit that
 * succeeded); a wait for the held picture's GPU work that failed (result
 * not 0, rhi.h rhi_wait_value_ns) */
enum {
    RD_AHEAD_OFF_NONE,
    RD_AHEAD_OFF_LOST,
    RD_AHEAD_OFF_SUBMIT,
    RD_AHEAD_OFF_WAIT,
    RD_AHEAD_OFF_GUARD,
    RD_AHEAD_OFF_THREAD,
    RD_AHEAD_OFF_TEST,
    RD_AHEAD_OFF_PREV
};

int rd__sq_ahead_check_submit(uint64_t before, uint64_t after, bool lost);
int rd__sq_ahead_check_wait(int32_t result, bool lost);

/* The 10 s line's clause of the ahead order from b (empty where it did not
   run in the block and offWhy is NULL); offWhy: why it is off for the run
   (NULL: not off); out always terminated, its length returned */
size_t rd__sq_ahead_text(const RdSubmitBlock *b, const char *offWhy, char *out, size_t cap);
/* nothing queued or running */
bool rd__sq_idle(const RdSubmitQ *q);
/* the oldest finished job's times not yet taken (false: none) */
bool rd__sq_take_finished(RdSubmitQ *q, RdSubmitJob *out);
/* the longest present call of the jobs finished since the last take, in
   ns (0: none presented) */
uint64_t rd__sq_take_present_ns(RdSubmitQ *q);
/* the jobs queued or running of frame slot `slot` */
uint32_t rd__sq_slot_jobs(const RdSubmitQ *q, uint32_t slot);
void rd__sq_note_wait(RdSubmitQ *q, double ms, bool byRender);
void rd__sq_note_on_render(RdSubmitQ *q);
void rd__sq_note_overlap(RdSubmitQ *q);
void rd__sq_block_take(RdSubmitQ *q, RdSubmitBlock *out);

/* One timed present of the GPU's end first's verdict (rd_submit_thread.h)
 * and its class:
 *   short    the driver's call under RD_GPU_FIRST_CALL_MS: no wait is
 *            made, not counted (a present that returns early, the owner's
 *            Pixel's 0.6 ms, never pays anything and never decides: the
 *            rule stays off);
 *   no prev  a long call with no present job before it (hasPrev false):
 *            not counted;
 *   prev done a long call whose picture before had already finished as it
 *            began (prevRead 1: the counter read just before the call, no
 *            wait): not counted and no wait after it; the call was long
 *            for something else (the display, the thread descheduled, the
 *            driver's CPU work, a rebuild's first present), and the wait
 *            after it would return at once and read waited whatever made
 *            it long.  A read that failed (-1) or read running (0) leaves
 *            the call eligible: the read failing never blocks the verdict;
 *   unknown  the one wait after the call, for the picture before's
 *            submission (never the picture's own: it would leave the next
 *            present nothing to wait for), failed (result not 0, rhi.h
 *            rhi_wait_value_ns; VK_NOT_READY and VK_TIMEOUT included);
 *   waited   it returned within RD_GPU_FIRST_NEAR_MS (waitMs: the time it
 *            took): the picture before had ended by the call's return, so
 *            the call lasted to that end or to its own picture's, later
 *            (line 562 of issue 179's reporter log: 140.3 ms, back at the
 *            picture before's signal);
 *   early    it blocked RD_GPU_FIRST_NEAR_MS or more: the call returned
 *            while the picture before still ran, its length not the
 *            GPU's. */
#define RD_GPU_FIRST_CALL_MS 2.0
#define RD_GPU_FIRST_NEAR_MS 1.0
uint8_t rd__sq_gf_class(double callMs, bool hasPrev, int prevRead, int32_t result, double waitMs);

/* a job's tail, step by step (rd__sq_tail_steps) */
enum { RD_SQ_END, RD_SQ_SUBMIT, RD_SQ_PRESENT };

typedef struct RdSqStep {
    uint8_t what; /* RD_SQ_* */
    uint32_t cl;  /* the list ended or submitted (0 for the present) */
} RdSqStep;

#define RD_SQ_TAIL_MAX 5u
/* j's steps in order into out (room for RD_SQ_TAIL_MAX), their count: end
   and submit cl, then cl2's (a list already ended ends again as nothing:
   rhi_end_commands of a list not recording), then the present when
   j->present */
uint32_t rd__sq_tail_steps(const RdSubmitJob *j, RdSqStep out[RD_SQ_TAIL_MAX]);

#endif /* RD_SUBMIT_QUEUE_H */
