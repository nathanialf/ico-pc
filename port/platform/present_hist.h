/*
 * port/platform/present_hist.h
 *
 * Issue 123: how long the driver's present call took, counted in a 10 s
 * block by size, so a stutter's size and count show in the log without the
 * "present blocked" line's cap (window_present_fallback.c names the first
 * 8 and then every 100th past 250 ms, window_host.c counts the ones over
 * 50 ms).  The window adds every presented replay's present time
 * (RdPerfRecord.presentMs, the submit thread's own timing of its present
 * call) and writes one line at the block's end:
 *     window: present calls: 756 timed (over 20 ms 247, over 50 ms 9, over
 *     100 ms 5, over 300 ms 5), longest 365 ms, total 1840 ms
 * A call is counted in every row it is over (a 365 ms call is in all four).
 *
 * Pure C, no globals: the unit test is test/present_hist_test.c.
 */
#ifndef ICO_PLATFORM_PRESENT_HIST_H
#define ICO_PLATFORM_PRESENT_HIST_H

#include <stddef.h>

/* the rows' limits in ms: over 20, 50, 100 and 300 */
#define PRESENT_HIST_ROWS 4

typedef struct PresentHist {
    unsigned n;                       /* present calls timed */
    unsigned over[PRESENT_HIST_ROWS]; /* of them over each row's limit */
    double maxMs, totalMs;
} PresentHist;

void present_hist_reset(PresentHist *h);
/* one call's time in ms; a negative or NaN time is not counted */
void present_hist_add(PresentHist *h, double ms);
/* the row's limit in ms (0 for a row out of range) */
double present_hist_limit(int row);
/* the block's line without the "window: " prefix; "" when nothing was
   timed.  Returns snprintf's count */
int present_hist_text(const PresentHist *h, char *buf, size_t size);

/* ------------------------------------------------------------------------
 * The lines around a present that blocked (issue 123; window_stall.c writes
 * them from the replays' records, rd.h RdPerfRecord, as they pop):
 *
 * present_split_text: what follows the "a present blocked N ms in the
 * driver's present call (replay N; ...)" line's parenthesis (after ": "):
 *     inside vkQueuePresentKHR 351.6 ms, the swapchain lock 0.0 ms, the
 *     surface poll 0.0 ms, other 0.2 ms; the replay's GPU span 71.3 ms
 *     against the block's 27.0 ms so far, the GPU idle 0.4 ms before it
 * "other" is the present's time outside the three (presentMs less them:
 * the present-signal submit, a retired swapchain's destruction, a rebuild).
 * The lock taken but no call made (split 2) reads "no present call made:
 * the swapchain lock 351.5 ms, other 0.5 ms"; the call not timed apart
 * (no split: OpenGL, Direct3D) reads "the call not timed apart on this
 * graphics API"; no GPU span "the replay's GPU
 * span not timed"; none in the block before it "(the block's first timed
 * replay)"; the gap: "the GPU idle X ms before it", "begun X ms before the
 * one before ended" (under 0) or "the GPU's time before it not measured",
 * with " (across K untimed replays)" when replays without timestamps came
 * between (rd_perf_gap.h).
 *
 * present_after_text: the record popped next, the replay after the one
 * that blocked ("window: " before it):
 *     after the present that blocked (replay 2941, 352 ms): replay 2942
 *     waited 0.3 ms for its frame slot (fence 0.2), acquired its image in
 *     298.7 ms, GPU span 26.9 ms, the GPU idle 301.2 ms before it
 *
 * Both return snprintf's count; a short buffer is cut and terminated.  The
 * decision they serve (docs/port/DISPLAY.md "The stats lines"): a call
 * that holds the whole time with the lock and the poll at 0 is the
 * driver's; a GPU span far over the block's is the GPU running slow (the
 * clock, another client); an idle GPU of about the stall's length before
 * the next replay is a queue draining.
 */
typedef struct PresentStallRec {
    unsigned replay;
    double presentMs;
    int split; /* 1: lockMs, callMs, pollMs valid; 2: lockMs only, no present call made; 0 none */
    double lockMs, callMs, pollMs;
    double waitMs, fenceMs, acquireMs; /* the frame slot's wait, of it the fence, the acquire */
    int gpuValid;
    double gpuMs;
    int gapValid;        /* gapMs measured (rd.h RdPerfRecord.gpuGapValid) */
    double gapMs;        /* the GPU idle before it; under 0 begun before the one before ended */
    unsigned gapSkipped; /* replays without timestamps between (rd.h RdPerfRecord.gpuGapSkipped) */
} PresentStallRec;

/* blockGpuN, blockGpuMs: the block's timed replays before this one and
   their mean GPU span */
int present_split_text(const PresentStallRec *r, unsigned blockGpuN, double blockGpuMs, char *buf,
                       size_t size);
int present_after_text(unsigned blockedReplay, double blockedMs, const PresentStallRec *r,
                       char *buf, size_t size);

/* The present time from which a present that blocked gets its own lines
 * (the after line, the kernel GPU driver's around line, the stalled
 * replay's passes; window_stall.c): 250 ms, the blocked line's own size.
 * The presents over 50 ms still arm the per-pass timing, count in the 10 s
 * lines and add their gap to the stall gaps; only the lines, each the
 * first 8 then every 100th, wait for a present this long, so the 50-250 ms
 * presents (17 in 110 s of a reporter's log, half of each line's 8) do not
 * spend them before the real stalls */
#define PRESENT_STALL_LINE_MS 250.0
/* 1 when a present of presentMs gets those lines (at PRESENT_STALL_LINE_MS
   or more; never for NaN) */
int present_stall_lines(double presentMs);

#endif /* ICO_PLATFORM_PRESENT_HIST_H */
