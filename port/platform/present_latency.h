/*
 * port/platform/present_latency.h
 *
 * The present's latency in a 10 s block (docs/port/ANDROID_DIAGNOSTICS.md,
 * "The present latency line"; issue 123): from the picture's hand-over to
 * the render thread (the replay's start where it was not handed over) to
 * the present's end, its mean and longest, and the mean split into the
 * part before the present (the replay's CPU work, its slot's wait for the
 * GPU, the acquire of the window's image) and the present itself; of the
 * driver's present call, the part while the picture's own GPU work still
 * ran (a wait for the GPU, at most) and the part after its end (the
 * display's), where the picture's GPU end is known exactly (its slot's
 * wait blocked and saw it end: rd.h RdPerfRecord.gpuSignalMs with
 * gpuDoneValid 1).  One line a block, after the present stalls' line:
 *   present latency in the block: 519 presents (519 from the hand-over),
 *     mean 34.6 ms, longest 365.2 ms (replay 2075); of the mean: before
 *     the present 18.0 ms (the slot's wait for the GPU 15.6 ms, the
 *     acquire 0.0 ms), the present 16.6 ms, the driver's present call 15.4
 *     ms of it; over 518 presents with the picture's GPU end seen, the call
 *     12.0 ms while the picture's GPU work still ran and 3.4 ms after its
 *     end (the display's)
 * Pure (port/platform/test/present_latency_test.c); the window adds each
 * presented record (window_stall.c window_stall_after: a few adds) and
 * writes the line at the block's end.
 */
#ifndef ICO_PLATFORM_PRESENT_LATENCY_H
#define ICO_PLATFORM_PRESENT_LATENCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PresentLatencyRec {
    uint32_t replay;
    double fromMs; /* the hand-over (handed), else the replay's start */
    bool handed;
    double endMs;     /* the present's end; the same clock */
    double presentMs; /* the present (the swapchain lock, the driver's call, the surface poll) */
    double callMs;    /* the driver's present call, inside presentMs */
    double fenceMs;   /* the replay's wait for the GPU (its frame slot's fence) */
    double acquireMs; /* the acquire of the window's image */
    double gpuEndMs;  /* the picture's GPU end, exact; 0 not known */
} PresentLatencyRec;

typedef struct PresentLatency {
    unsigned n, handed, split;
    double sum, maxMs;
    uint32_t maxReplay;
    double before, fence, acquire, present, call;
    double callGpu, callAfter; /* over split */
} PresentLatency;

/* a presented record (endMs > 0 and fromMs > 0, end after from; others
   are left out) */
void present_latency_add(PresentLatency *p, const PresentLatencyRec *r);
/* the line after "window: " (nothing with no present: 0).  (v0.11.7's
   clause on the queue's global priority went with the priority, v0.11.8:
   the queue is made at the driver's default on every device) */
size_t present_latency_text(const PresentLatency *p, char *out, size_t cap);

#endif /* ICO_PLATFORM_PRESENT_LATENCY_H */
