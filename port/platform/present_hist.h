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

#endif /* ICO_PLATFORM_PRESENT_HIST_H */
