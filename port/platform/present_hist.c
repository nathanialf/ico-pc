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
