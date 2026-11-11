/* rd_loss_pick.c: which frames a device-loss dump writes (rd_loss_pick.h). */
#include "rd_loss_pick.h"
#include <stddef.h>

static bool isHeld(const RdLossInput *in, uint32_t frame)
{
    for (uint32_t i = 0; frame && i < in->heldCount; i++) {
        if (in->held[i] == frame) {
            return true;
        }
    }
    return false;
}

/* the frame of the replay that came before `frame` in the pass ring's table,
 * 0 when `frame` is the oldest listed or not listed */
static uint32_t replayedBefore(const RdLossInput *in, uint32_t frame)
{
    for (uint32_t i = 1; i < in->replayedCount; i++) {
        if (in->replayed[i] == frame) {
            return in->replayed[i - 1];
        }
    }
    return 0;
}

static uint32_t newestBelow(const RdLossInput *in, uint32_t frame)
{
    uint32_t best = 0;

    for (uint32_t i = 0; i < in->heldCount; i++) {
        if (in->held[i] < frame && in->held[i] > best) {
            best = in->held[i];
        }
    }
    return best;
}

void rd_loss_choose(const RdLossInput *in, RdLossChoice *out)
{
    *out = (RdLossChoice){0};
    if (in->waitFrame != 0) {
        out->how = RD_LOSS_HOW_WAIT;
        out->replay = in->waitReplay;
        out->frame = in->waitFrame;
    } else if (in->openFrame != 0) {
        out->how = RD_LOSS_HOW_OPEN;
        out->replay = in->openReplay;
        out->frame = in->openFrame;
    } else if (in->newestFrame != 0) {
        /* a vkQueueSubmit that failed has no number.  A picture the pass
         * ring lists after the newest numbered one was recorded and never
         * numbered: its submit is the one that failed.  Otherwise no
         * picture is missing, the submit was none of a picture's and the
         * newest numbered picture is the one the GPU last had */
        if (in->ringReplay > in->newestReplay && in->ringFrame != 0) {
            out->how = RD_LOSS_HOW_NEXT;
            out->replay = in->ringReplay;
            out->frame = in->ringFrame;
        } else {
            out->how = RD_LOSS_HOW_SIDE;
            out->replay = in->newestReplay;
            out->frame = in->newestFrame;
        }
    } else if (in->heldCount > 0) {
        out->how = RD_LOSS_HOW_NEWEST;
        out->frame = in->held[in->heldCount - 1];
    }
    if (out->frame == 0) {
        return;
    }
    out->before = replayedBefore(in, out->frame);
    if (out->before == 0) {
        out->before = newestBelow(in, out->frame);
    }
    out->frameHeld = isHeld(in, out->frame);
    out->beforeHeld = isHeld(in, out->before);
    if (out->frameHeld) {
        out->writeFrame = out->frame;
    } else {
        /* the held frame nearest the lost one, a newer one first (the ring
         * keeps the newest) */
        uint32_t best = 0, bestGap = UINT32_MAX;

        for (uint32_t i = 0; i < in->heldCount; i++) {
            const uint32_t h = in->held[i];
            const uint32_t gap = h > out->frame ? h - out->frame : out->frame - h;

            if (gap < bestGap || (gap == bestGap && h > best)) {
                best = h;
                bestGap = gap;
            }
        }
        out->writeFrame = best;
    }
    if (out->writeFrame == out->frame && out->beforeHeld) {
        out->writeBefore = out->before;
    } else if (out->writeFrame != 0) {
        out->writeBefore = newestBelow(in, out->writeFrame);
    }
}

bool rd_loss_keep_needed(uint32_t frame, uint32_t openFrame)
{
    return frame != 0 && openFrame != 0 && frame >= openFrame;
}

bool rd_loss_keep_release(uint32_t frame, uint32_t openFrame)
{
    return openFrame == 0 || frame + 2u <= openFrame;
}

bool rd_loss_keep_stalled(uint64_t waitingFor, uint64_t waitingNs)
{
    return waitingFor != 0 && waitingNs >= RD_LOSS_KEEP_STALL_NS;
}

RdLossKeep rd_loss_keep_decide(RdLossThrottle *t, uint64_t seen, uint32_t frame, uint32_t openFrame,
                               uint64_t waitingFor, uint64_t waitingNs)
{
    if (!rd_loss_keep_needed(frame, openFrame)) {
        return RD_LOSS_KEEP_NOT_OPEN;
    }
    if (!rd_loss_keep_stalled(waitingFor, waitingNs)) {
        return RD_LOSS_KEEP_NO_STALL;
    }
    if (seen < t->pauseUntil) {
        return RD_LOSS_KEEP_PAUSED;
    }
    /* the oldest of the last RD_LOSS_KEEP_PER_WINDOW attempts inside the
       window: none left */
    if (t->attempts >= RD_LOSS_KEEP_PER_WINDOW && seen - t->at[t->next] < RD_LOSS_KEEP_WINDOW) {
        return RD_LOSS_KEEP_THROTTLED;
    }
    t->at[t->next] = seen;
    t->next = (t->next + 1u) % RD_LOSS_KEEP_PER_WINDOW;
    t->attempts++;
    return RD_LOSS_KEEP_TAKE;
}

void rd_loss_keep_big(RdLossThrottle *t, uint64_t seen)
{
    t->pauseUntil = seen + RD_LOSS_KEEP_BIG_PAUSE;
}

uint32_t rd_loss_keep_slot(const uint32_t *frames, uint32_t n)
{
    uint32_t best = 0;

    for (uint32_t i = 0; i < n; i++) {
        if (frames[i] == 0) {
            return i;
        }
        if (frames[i] < frames[best]) {
            best = i;
        }
    }
    return best;
}
