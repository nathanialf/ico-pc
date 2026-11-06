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
 *           the block, kept for rd__sq_take_finished (the oldest dropped,
 *           and counted, past RD_SUBMIT_DEPTH unread), its present time
 *           for rd__sq_take_present_ns;
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
 *           submit cl, end and submit cl2, present.
 *
 * Pure C (no threads, no graphics calls): the caller holds its own lock
 * around every call.  The unit test is port/render/test/rd_submit_queue_test.c.
 */
#ifndef RD_SUBMIT_QUEUE_H
#define RD_SUBMIT_QUEUE_H

#include <stdbool.h>
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
    uint8_t slot;     /* its frame slot (rhi_frame_slot when it was recorded) */
    bool present;     /* its image is presented after the submit */
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
} RdSubmitJob;

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
} RdSubmitBlock;

typedef struct RdSubmitQ {
    RdSubmitJob ring[RD_SUBMIT_DEPTH];
    uint32_t head, count; /* count: queued and running */
    bool running;
    uint64_t posted, done;
    RdSubmitJob fin[RD_SUBMIT_DEPTH];
    uint32_t finN;
    uint64_t presentNs; /* the longest present finished since rd__sq_take_present_ns */
    RdSubmitBlock block;
} RdSubmitQ;

void rd__sq_init(RdSubmitQ *q);
uint64_t rd__sq_post(RdSubmitQ *q, const RdSubmitJob *job);
bool rd__sq_next(RdSubmitQ *q, RdSubmitJob *out);
void rd__sq_finish(RdSubmitQ *q, const RdSubmitJob *job);
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
