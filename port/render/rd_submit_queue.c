/* rd_submit_queue.c: the submit thread's queue (rd_submit_queue.h). */
#include "rd_submit_queue.h"

#include <stdio.h>
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
    j->gfSample = RD_GF_NONE; /* the job's tail sets it */
    j->gfResult = 0;
    j->gfPrevRead = -2;
    q->count++;
    if (q->count > q->block.depthMax) {
        q->block.depthMax = q->count;
    }
    return j->seq;
}

bool rd__sq_next(RdSubmitQ *q, RdSubmitJob *out)
{
    if (q->running || q->count == 0 || q->sent != 0) {
        return false;
    }
    *out = q->ring[q->head];
    q->running = true;
    return true;
}

/* the head done with job's times: counted in the block, kept for the take */
static void finishHead(RdSubmitQ *q, const RdSubmitJob *job)
{
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
    if (job->gfSample != RD_GF_NONE) {
        q->block.gfJobs++;
        q->block.gfReadFailed += job->gfPrevRead == -1 ? 1u : 0u;
        switch (job->gfSample) {
        case RD_GF_NOT_ORIGINAL:
            q->block.gfNotOriginal++;
            break;
        case RD_GF_NO_PRESENT:
            q->block.gfNoPresent++;
            break;
        case RD_GF_SHORT:
            q->block.gfTimed++;
            q->block.gfShort++;
            break;
        case RD_GF_NO_PREV:
            q->block.gfTimed++;
            break;
        case RD_GF_PREV_DONE:
            q->block.gfTimed++;
            q->block.gfPrevDone++;
            break;
        case RD_GF_UNKNOWN:
            q->block.gfTimed++;
            q->block.gfUnknown++;
            q->block.gfLastResult = job->gfResult;
            break;
        default: /* waited or early */
            q->block.gfTimed++;
            q->block.gfCounted++;
            q->block.gfWaited += job->gfSample == RD_GF_WAITED ? 1u : 0u;
            break;
        }
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

void rd__sq_finish(RdSubmitQ *q, const RdSubmitJob *job)
{
    if (!q->running || q->count == 0) {
        return;
    }
    q->running = false;
    finishHead(q, job);
}

const RdSubmitJob *rd__sq_peek_unsent(const RdSubmitQ *q)
{
    if (q->running || q->sent >= q->count) {
        return NULL;
    }
    return &q->ring[(q->head + q->sent) % RD_SUBMIT_DEPTH];
}

bool rd__sq_take_unsent(RdSubmitQ *q, RdSubmitJob *out)
{
    const RdSubmitJob *j = rd__sq_peek_unsent(q);
    if (j == NULL) {
        return false;
    }
    *out = *j;
    q->sent++;
    return true;
}

void rd__sq_finish_sent(RdSubmitQ *q, const RdSubmitJob *job)
{
    if (q->sent == 0 || q->count == 0) {
        return;
    }
    q->sent--;
    finishHead(q, job);
}

uint32_t rd__sq_sent(const RdSubmitQ *q)
{
    return q->sent;
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

uint8_t rd__sq_gf_class(double callMs, bool hasPrev, int prevRead, int32_t result, double waitMs)
{
    if (callMs < RD_GPU_FIRST_CALL_MS) {
        return RD_GF_SHORT;
    }
    if (!hasPrev) {
        return RD_GF_NO_PREV;
    }
    if (prevRead == 1) {
        return RD_GF_PREV_DONE;
    }
    if (result != 0) {
        return RD_GF_UNKNOWN;
    }
    return waitMs < RD_GPU_FIRST_NEAR_MS ? RD_GF_WAITED : RD_GF_EARLY;
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

void rd__sq_note_ahead(RdSubmitQ *q, bool early)
{
    q->block.aheadJobs++;
    q->block.aheadEarly += early ? 1u : 0u;
}

/* b - a, never below 0 (a clock read on another thread may come first) */
static double span(double a, double b)
{
    return b > a ? b - a : 0.0;
}

void rd__sq_gap_parts(double endSeen, double wake, double presented, double finished, double taken,
                      double ended, double submitted, RdSqGap *out)
{
    memset(out, 0, sizeof(*out));
    if (wake <= 0.0 || taken < wake) {
        /* the next taken before the submit thread woke for the picture
           before: its end and submit straddle that GPU end (only what came
           after it is idle) */
        const double from = taken > endSeen ? taken : endSeen;
        out->endMs = span(from, ended);
        out->submitMs = span(ended > endSeen ? ended : endSeen, submitted);
        return;
    }
    out->wakeMs = span(endSeen, wake);
    out->presentMs = span(wake, presented);
    out->finishMs = span(presented, finished);
    out->postMs = span(finished, taken);
    out->endMs = span(taken, ended);
    out->submitMs = span(ended, submitted);
}

void rd__sq_note_gap(RdSubmitQ *q, const RdSqGap *g, bool notPosted)
{
    RdSubmitBlock *b = &q->block;
    const double ms = g->wakeMs + g->presentMs + g->finishMs + g->postMs + g->endMs + g->submitMs;
    b->gaps++;
    b->gapNotPosted += notPosted ? 1u : 0u;
    b->gapMs += ms;
    b->gapWakeMs += g->wakeMs;
    b->gapPresentMs += g->presentMs;
    b->gapFinishMs += g->finishMs;
    b->gapPostMs += g->postMs;
    b->gapEndMs += g->endMs;
    b->gapSubmitMs += g->submitMs;
}

void rd__sq_note_guard(RdSubmitQ *q, double callMs)
{
    q->block.guardTimed++;
    q->block.guardLong += callMs >= RD_GPU_FIRST_CALL_MS ? 1u : 0u;
    q->block.guardMaxMs = callMs > q->block.guardMaxMs ? callMs : q->block.guardMaxMs;
}

uint8_t rd__sq_guard_class(double callMs, int readBefore, int readAfter, bool waited,
                           int32_t waitResult, double foundMs, double returnMs, double laterStartMs)
{
    if (callMs < RD_GPU_FIRST_CALL_MS) {
        return RD_GD_SHORT;
    }
    if (readBefore == 1) {
        return RD_GD_DONE;
    }
    if (readBefore == -1 || readAfter == -1) {
        return RD_GD_FAILED;
    }
    double found = returnMs;
    if (readAfter != 1) {
        if (!waited) {
            return RD_GD_PENDING;
        }
        if (waitResult != 0) {
            return RD_GD_FAILED;
        }
        if (foundMs - returnMs >= RD_GPU_FIRST_NEAR_MS) {
            return RD_GD_SLOW;
        }
        found = foundMs > returnMs ? foundMs : returnMs;
    }
    const double span = found - laterStartMs;
    return span > 0.0 && callMs < RD_AHEAD_GUARD_SHARE * span ? RD_GD_BRIEF : RD_GD_WAITED;
}

bool rd__sq_guard_counts(uint8_t cls)
{
    return cls == RD_GD_WAITED || cls == RD_GD_FAILED;
}

void rd__sq_note_guard_class(RdSubmitQ *q, uint8_t cls)
{
    RdSubmitBlock *b = &q->block;
    b->guardWaited += cls == RD_GD_WAITED ? 1u : 0u;
    b->guardFailed += cls == RD_GD_FAILED ? 1u : 0u;
    b->guardSlow += cls == RD_GD_SLOW ? 1u : 0u;
    b->guardDone += cls == RD_GD_DONE ? 1u : 0u;
    b->guardBrief += cls == RD_GD_BRIEF ? 1u : 0u;
}

bool rd__sq_guard_note(RdSqGuard *g, bool counted)
{
    const bool isLong = counted;
    if (g->n == RD_AHEAD_GUARD_WINDOW) {
        /* the oldest call leaves the window */
        if ((g->bits >> (RD_AHEAD_GUARD_WINDOW - 1u)) & 1u) {
            g->longN--;
        }
    } else {
        g->n++;
    }
    g->bits = (uint16_t)((g->bits << 1) | (isLong ? 1u : 0u));
    g->longN = (uint8_t)(g->longN + (isLong ? 1u : 0u));
    g->calls++;
    g->longs += isLong ? 1u : 0u;
    return g->longN >= RD_AHEAD_GUARD_LONG;
}

int rd__sq_ahead_check_submit(uint64_t before, uint64_t after, bool lost)
{
    if (lost) {
        return RD_AHEAD_OFF_LOST;
    }
    return after > before ? RD_AHEAD_OFF_NONE : RD_AHEAD_OFF_SUBMIT;
}

int rd__sq_ahead_check_wait(int32_t result, bool lost)
{
    if (lost) {
        return RD_AHEAD_OFF_LOST;
    }
    return result == 0 ? RD_AHEAD_OFF_NONE : RD_AHEAD_OFF_WAIT;
}

size_t rd__sq_ahead_text(const RdSubmitBlock *b, const char *offWhy, char *out, size_t cap)
{
    size_t n = 0;
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (b->aheadJobs == 0 && b->gaps == 0 && b->guardTimed == 0 && offWhy == NULL) {
        return 0;
    }
    const double g = b->gaps ? (double)b->gaps : 1.0;
    int k = snprintf(out, cap,
                     "; the next picture submitted ahead: %u of %u submitted before the picture "
                     "before's GPU end; the GPU idle %u times (%.1f ms a time: wake-up %.1f, "
                     "present call %.1f, finish %.1f, waiting for the next picture %.1f (%u "
                     "pictures whose next was not yet posted), list end %.1f, submit %.1f); "
                     "present calls with a later picture submitted %u (%u of %.1f ms or more: "
                     "%u waited for the later picture's GPU work, %u slow, not waiting, %u with "
                     "that work already ended, %u too brief for it, %u reads or waits failed; "
                     "longest %.1f ms)",
                     b->aheadEarly, b->aheadJobs, b->gaps, b->gapMs / g, b->gapWakeMs / g,
                     b->gapPresentMs / g, b->gapFinishMs / g, b->gapPostMs / g, b->gapNotPosted,
                     b->gapEndMs / g, b->gapSubmitMs / g, b->guardTimed, b->guardLong,
                     RD_GPU_FIRST_CALL_MS, b->guardWaited, b->guardSlow, b->guardDone,
                     b->guardBrief, b->guardFailed, b->guardMaxMs);
    n = k < 0 ? 0 : (size_t)k < cap ? (size_t)k : cap - 1;
    if (offWhy != NULL && n + 1 < cap) {
        k = snprintf(out + n, cap - n, "; off for the run: %s", offWhy);
        n = k < 0 ? n : n + (size_t)k < cap ? n + (size_t)k : cap - 1;
    }
    return n;
}
