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
 *   waits   rd__sq_note_wait: another thread waited for the queue to empty
 *           (the render thread at its next picture's first graphics call,
 *           the game thread at a drain); rd__sq_note_on_render: a list the
 *           render thread ended and submitted itself;
 *   block   rd__sq_block_take: the block's numbers, the sums cleared.
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
    uint32_t cl;      /* the list (RhiCommandList.id), recorded and left open */
    bool present;     /* its image is presented after the submit */
    uint32_t replay;  /* its replay's perf record (RdPerfRecord.replay) */
    uint64_t seq;     /* rd__sq_post's number */
    double submitMs;  /* the end and the submit (rhi_end_commands, rhi_submit) */
    double presentMs; /* the present (rhi_present) */
} RdSubmitJob;

/* the 10 s block (rd__sq_block_take): the jobs finished and their means in
   ms, the longest job, the most jobs queued and running at once, the other
   threads' waits for the queue (byRender: the render thread's), the jobs
   whose finished times were dropped unread, the lists the render thread
   ended and submitted itself instead (rd__sq_note_on_render: a call of its
   own needed the backend before its job's end) */
typedef struct RdSubmitBlock {
    unsigned jobs;
    double submitMs, presentMs, longestMs;
    unsigned depthMax;
    unsigned waits, renderWaits;
    double waitMs, renderWaitMs, waitMaxMs;
    unsigned dropped, onRender;
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
void rd__sq_note_wait(RdSubmitQ *q, double ms, bool byRender);
void rd__sq_note_on_render(RdSubmitQ *q);
void rd__sq_block_take(RdSubmitQ *q, RdSubmitBlock *out);

#endif /* RD_SUBMIT_QUEUE_H */
