/*
 * port/platform/pace_policy.c
 *
 * The slow-present decision (pace_policy.h).
 */
#include "pace_policy.h"

uint64_t pace_SlowThreshold(uint64_t refreshNs, uint64_t periodNs, bool injector)
{
    if (injector) {
        return 2 * refreshNs + periodNs / 2;
    }
    return (refreshNs > periodNs ? refreshNs : periodNs) + periodNs / 2;
}

static uint64_t median(const PaceHist *h)
{
    uint64_t v[PACE_HIST_LEN];
    unsigned i, j;
    for (i = 0; i < h->count; i++) {
        v[i] = h->cost[i];
    }
    for (i = 1; i < h->count; i++) {
        const uint64_t x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    /* an even count: the mean of the middle two */
    return h->count % 2 ? v[h->count / 2] : v[h->count / 2 - 1] / 2 + v[h->count / 2] / 2;
}

bool pace_SlowPresent(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                      bool injector)
{
    const uint64_t thr = pace_SlowThreshold(refreshNs, periodNs, injector);
    uint64_t med;

    h->cost[h->next] = costNs;
    h->next = (h->next + 1) % PACE_HIST_LEN;
    if (h->count < PACE_HIST_LEN) {
        h->count++;
    }
    med = median(h);
    if (h->slow) {
        if (med < thr / 10 * 8) {
            h->slow = false;
            h->over = 0;
        }
    } else if (med > thr) {
        if (++h->over >= PACE_ENTER_COUNT) {
            h->slow = true;
        }
    } else {
        h->over = 0;
    }
    return h->slow;
}
