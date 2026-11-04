/*
 * port/platform/window_stall.c
 *
 * The window's lines around a present that blocked (window_stall.h).
 */
#include "window_stall.h"
#include <stdio.h>
#include <string.h>
#include "diag_host.h"
#include "present_hist.h"

/* the present that blocked, waiting for the record after it */
static struct {
    int pending;
    unsigned replay;
    double ms;
} s_after;

static void stall_rec(const RdPerfRecord *r, PresentStallRec *o)
{
    memset(o, 0, sizeof(*o));
    o->replay = r->replay;
    o->presentMs = r->presentMs;
    o->split = r->presentSplit != 0;
    o->lockMs = r->presentLockMs;
    o->callMs = r->presentCallMs;
    o->pollMs = r->presentPollMs;
    o->waitMs = r->waitMs;
    o->fenceMs = r->fenceWaitMs;
    o->acquireMs = r->acquireMs;
    o->gpuValid = r->gpuValid != 0;
    o->gpuMs = r->gpuMs;
    o->gapValid = r->gpuGapValid != 0;
    o->gapMs = (double)r->gpuGapNs / 1e6;
}

const char *window_stall_split(const RdPerfRecord *r, unsigned blockGpuN, double blockGpuMs)
{
    static char text[320];
    PresentStallRec o;

    stall_rec(r, &o);
    present_split_text(&o, blockGpuN, blockGpuMs, text, sizeof(text));
    return text;
}

void window_stall_blocked(const RdPerfRecord *r)
{
    s_after.pending = 1;
    s_after.replay = r->replay;
    s_after.ms = r->presentMs;
}

void window_stall_after(const RdPerfRecord *r)
{
    char text[320];
    PresentStallRec o;

    if (!s_after.pending) {
        return;
    }
    s_after.pending = 0;
    stall_rec(r, &o);
    present_after_text(s_after.replay, s_after.ms, &o, text, sizeof(text));
    ico_log_limited("window: after the present that blocked", "window: %s", text);
}
