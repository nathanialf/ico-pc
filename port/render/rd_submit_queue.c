/* rd_submit_queue.c: the submit thread's queue (rd_submit_queue.h). */
#include "rd_submit_queue.h"

#include <string.h>

void rd__sq_init(RdSubmitQ *q)
{
    memset(q, 0, sizeof(*q));
}

uint64_t rd__sq_post(RdSubmitQ *q, const RdSubmitJob *job)
{
    if (job == NULL || job->cl == 0 || q->count >= RD_SUBMIT_DEPTH) {
        return 0;
    }
    RdSubmitJob *j = &q->ring[(q->head + q->count) % RD_SUBMIT_DEPTH];
    *j = *job;
    j->seq = ++q->posted;
    j->submitMs = j->presentMs = 0.0;
    q->count++;
    if (q->count > q->block.depthMax) {
        q->block.depthMax = q->count;
    }
    return j->seq;
}

bool rd__sq_next(RdSubmitQ *q, RdSubmitJob *out)
{
    if (q->running || q->count == 0) {
        return false;
    }
    *out = q->ring[q->head];
    q->running = true;
    return true;
}

void rd__sq_finish(RdSubmitQ *q, const RdSubmitJob *job)
{
    if (!q->running || q->count == 0) {
        return;
    }
    q->running = false;
    q->head = (q->head + 1u) % RD_SUBMIT_DEPTH;
    q->count--;
    q->done = job->seq > q->done ? job->seq : q->done;
    q->block.jobs++;
    q->block.submitMs += job->submitMs;
    q->block.presentMs += job->presentMs;
    {
        const double ms = job->submitMs + job->presentMs;
        q->block.longestMs = ms > q->block.longestMs ? ms : q->block.longestMs;
    }
    if (job->present) {
        const uint64_t ns = job->presentMs > 0.0 ? (uint64_t)(job->presentMs * 1e6) : 1u;
        q->presentNs = ns > q->presentNs ? ns : q->presentNs;
    }
    if (q->finN == RD_SUBMIT_DEPTH) {
        /* the oldest unread: its record keeps the times it had */
        memmove(&q->fin[0], &q->fin[1], (RD_SUBMIT_DEPTH - 1u) * sizeof(q->fin[0]));
        q->finN--;
        q->block.dropped++;
    }
    q->fin[q->finN++] = *job;
}

bool rd__sq_idle(const RdSubmitQ *q)
{
    return q->count == 0;
}

bool rd__sq_take_finished(RdSubmitQ *q, RdSubmitJob *out)
{
    if (q->finN == 0) {
        return false;
    }
    *out = q->fin[0];
    memmove(&q->fin[0], &q->fin[1], (q->finN - 1u) * sizeof(q->fin[0]));
    q->finN--;
    return true;
}

uint64_t rd__sq_take_present_ns(RdSubmitQ *q)
{
    const uint64_t ns = q->presentNs;
    q->presentNs = 0;
    return ns;
}

uint32_t rd__sq_slot_jobs(const RdSubmitQ *q, uint32_t slot)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < q->count; i++) {
        if (q->ring[(q->head + i) % RD_SUBMIT_DEPTH].slot == slot) {
            n++;
        }
    }
    return n;
}

void rd__sq_note_wait(RdSubmitQ *q, double ms, bool byRender)
{
    if (ms < 0.0) {
        ms = 0.0;
    }
    q->block.waits++;
    q->block.waitMs += ms;
    q->block.waitMaxMs = ms > q->block.waitMaxMs ? ms : q->block.waitMaxMs;
    if (byRender) {
        q->block.renderWaits++;
        q->block.renderWaitMs += ms;
    }
}

void rd__sq_note_on_render(RdSubmitQ *q)
{
    q->block.onRender++;
}

void rd__sq_note_overlap(RdSubmitQ *q)
{
    q->block.overlapped++;
}

void rd__sq_block_take(RdSubmitQ *q, RdSubmitBlock *out)
{
    *out = q->block;
    memset(&q->block, 0, sizeof(q->block));
    /* the jobs still queued or running carry over as the next block's depth */
    q->block.depthMax = q->count;
    if (out->jobs) {
        out->submitMs /= out->jobs;
        out->presentMs /= out->jobs;
    }
}

uint32_t rd__sq_tail_steps(const RdSubmitJob *j, RdSqStep out[RD_SQ_TAIL_MAX])
{
    uint32_t n = 0;
    const uint32_t lists[2] = {j->cl, j->cl2};
    for (int k = 0; k < 2; k++) {
        if (lists[k] == 0) {
            continue;
        }
        out[n++] = (RdSqStep){RD_SQ_END, lists[k]};
        out[n++] = (RdSqStep){RD_SQ_SUBMIT, lists[k]};
    }
    if (j->present) {
        out[n++] = (RdSqStep){RD_SQ_PRESENT, 0};
    }
    return n;
}
