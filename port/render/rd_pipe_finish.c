/*
 * port/render/rd_pipe_finish.c
 *
 * The background finisher's choices (rd_pipe_finish.h).
 */
#include "rd_pipe_finish.h"

#include <stddef.h>

uint32_t rd__finish_pick(uint32_t m, uint32_t *cursor, uint32_t max, RdFinishKeyFn key, void *user,
                         uint32_t *out)
{
    uint32_t i = *cursor, n = 0;
    int kind = -1;
    if (key == NULL || out == NULL) {
        return 0;
    }
    for (; i < m && n < max; i++) {
        const int k = key(user, i);
        if (k < 0) {
            continue;
        }
        if (kind >= 0 && k != kind) {
            break; /* the next batch begins with this key */
        }
        kind = k;
        out[n++] = i;
    }
    *cursor = i;
    return n;
}

RdFinishStep rd__finish_step(const RdFinishIn *in)
{
    if (in == NULL || !in->on) {
        return RD_FINISH_IDLE;
    }
    if (in->inflight) {
        return in->finished ? RD_FINISH_END_BATCH : RD_FINISH_WAIT_BATCH;
    }
    if (in->memory == 2) {
        return RD_FINISH_STOP_SIGNAL;
    }
    if (in->left == 0) {
        return RD_FINISH_DONE;
    }
    if (in->nowMs < in->holdMs) {
        return RD_FINISH_WAIT_HOLD;
    }
    if (in->nowMs < in->floorMs) {
        return RD_FINISH_WAIT_FLOOR;
    }
    if (in->memory < 0) {
        return RD_FINISH_CHECK;
    }
    if (in->memory == 1) {
        return RD_FINISH_WAIT_FLOOR;
    }
    return RD_FINISH_BEGIN;
}

const char *rd__finish_step_words(RdFinishStep s)
{
    switch (s) {
    case RD_FINISH_IDLE:
        return "not running";
    case RD_FINISH_WAIT_BATCH:
        return "a batch compiling";
    case RD_FINISH_END_BATCH:
        return "a batch compiled";
    case RD_FINISH_STOP_SIGNAL:
        return "ended at the system's low-memory signal";
    case RD_FINISH_WAIT_HOLD:
        return "waits for a late step or a slow picture to pass";
    case RD_FINISH_WAIT_FLOOR:
        return "waits for the free memory to be above the floor";
    case RD_FINISH_CHECK:
        return "reads the free memory";
    case RD_FINISH_BEGIN:
        return "begins a batch";
    case RD_FINISH_DONE:
        return "done";
    }
    return "unknown";
}
