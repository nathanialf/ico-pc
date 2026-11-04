/*
 * port/platform/present_hist.c
 *
 * The 10 s block's histogram of the driver's present call (present_hist.h).
 */
#include "present_hist.h"

#include <stdio.h>
#include <string.h>

static const double s_limits[PRESENT_HIST_ROWS] = {20.0, 50.0, 100.0, 300.0};

void present_hist_reset(PresentHist *h)
{
    memset(h, 0, sizeof *h);
}

int present_stall_lines(double presentMs)
{
    return presentMs >= PRESENT_STALL_LINE_MS;
}

double present_hist_limit(int row)
{
    return row >= 0 && row < PRESENT_HIST_ROWS ? s_limits[row] : 0.0;
}

void present_hist_add(PresentHist *h, double ms)
{
    if (!(ms >= 0.0)) {
        return; /* negative or NaN */
    }
    h->n++;
    h->totalMs += ms;
    if (ms > h->maxMs) {
        h->maxMs = ms;
    }
    for (int i = 0; i < PRESENT_HIST_ROWS; i++) {
        if (ms > s_limits[i]) {
            h->over[i]++;
        }
    }
}

int present_hist_text(const PresentHist *h, char *buf, size_t size)
{
    if (h->n == 0) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    return snprintf(buf, size,
                    "present calls: %u timed (over 20 ms %u, over 50 ms %u, over 100 ms %u, over "
                    "300 ms %u), longest %.0f ms, total %.0f ms",
                    h->n, h->over[0], h->over[1], h->over[2], h->over[3], h->maxMs, h->totalMs);
}

/* ", the GPU idle 301.2 ms before it (across 1 untimed replay)" and its
   kin into buf */
static void gap_words(const PresentStallRec *r, char *buf, size_t size)
{
    char across[48] = "";

    if (r->gapValid && r->gapSkipped != 0) {
        snprintf(across, sizeof across, " (across %u untimed replay%s)", r->gapSkipped,
                 r->gapSkipped == 1 ? "" : "s");
    }
    if (!r->gapValid) {
        snprintf(buf, size, ", the GPU's time before it not measured");
    } else if (r->gapMs >= 0.0) {
        snprintf(buf, size, ", the GPU idle %.1f ms before it%s", r->gapMs, across);
    } else {
        snprintf(buf, size, ", begun %.1f ms before the one before ended%s", -r->gapMs, across);
    }
}

int present_split_text(const PresentStallRec *r, unsigned blockGpuN, double blockGpuMs, char *buf,
                       size_t size)
{
    char split[128], gpu[96], gap[96];

    if (r->split) {
        snprintf(split, sizeof split,
                 "inside vkQueuePresentKHR %.1f ms, the swapchain lock %.1f ms, the surface poll "
                 "%.1f ms",
                 r->callMs, r->lockMs, r->pollMs);
    } else {
        snprintf(split, sizeof split, "the call not timed apart on this graphics API");
    }
    gap[0] = '\0';
    if (!r->gpuValid) {
        snprintf(gpu, sizeof gpu, "the replay's GPU span not timed");
    } else {
        if (blockGpuN != 0) {
            snprintf(gpu, sizeof gpu,
                     "the replay's GPU span %.1f ms against the block's %.1f ms so far", r->gpuMs,
                     blockGpuMs);
        } else {
            snprintf(gpu, sizeof gpu,
                     "the replay's GPU span %.1f ms (the block's first timed replay)", r->gpuMs);
        }
        gap_words(r, gap, sizeof gap);
    }
    return snprintf(buf, size, "%s; %s%s", split, gpu, gap);
}

int present_after_text(unsigned blockedReplay, double blockedMs, const PresentStallRec *r,
                       char *buf, size_t size)
{
    char gpu[64], gap[96];

    gap[0] = '\0';
    if (r->gpuValid) {
        snprintf(gpu, sizeof gpu, "GPU span %.1f ms", r->gpuMs);
        gap_words(r, gap, sizeof gap);
    } else {
        snprintf(gpu, sizeof gpu, "GPU span not timed");
    }
    return snprintf(buf, size,
                    "after the present that blocked (replay %u, %.0f ms): replay %u waited %.1f ms "
                    "for its frame slot (fence %.1f), acquired its image in %.1f ms, %s%s",
                    blockedReplay, blockedMs, r->replay, r->waitMs, r->fenceMs, r->acquireMs, gpu,
                    gap);
}
