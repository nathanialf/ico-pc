/*
 * port/platform/pace_policy.c
 *
 * The slow-present decision (pace_policy.h).
 */
#include "pace_policy.h"

uint64_t pace_slow_threshold(uint64_t refreshNs, uint64_t periodNs, bool injector)
{
    if (injector) {
        /* one refresh more than the plain threshold */
        return (refreshNs > periodNs ? refreshNs : periodNs) + refreshNs + periodNs / 2;
    }
    return (refreshNs > periodNs ? refreshNs : periodNs) + periodNs / 2;
}

uint64_t pace_present_gap(int cap, bool uncappedVsync, uint64_t refreshNs)
{
    if (uncappedVsync) {
        /* a sixteenth of a refresh early: the sleep's overshoot and a game
           step that a due present lands in only make a present later, and
           the gap counts from the last present's start, so a gap of exactly
           one refresh averaged more than a refresh */
        return refreshNs - refreshNs / 16;
    }
    return cap > 0 ? 1000000000ull / (uint64_t)cap : 0;
}

/* the median of n costs (n <= PACE_AUTO_SAMPLES; 0 with none) */
static uint64_t median_of(const uint64_t *cost, unsigned n)
{
    uint64_t v[PACE_AUTO_SAMPLES];
    unsigned i, j;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        v[i] = cost[i];
    }
    for (i = 1; i < n; i++) {
        const uint64_t x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    /* an even count: the mean of the middle two */
    return n % 2 ? v[n / 2] : v[n / 2 - 1] / 2 + v[n / 2] / 2;
}

static uint64_t median(const PaceHist *h)
{
    return median_of(h->cost, h->count);
}

bool pace_slow_present(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                       bool injector)
{
    const uint64_t thr = pace_slow_threshold(refreshNs, periodNs, injector);
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

/* resolution "auto": the cost samples */
void pace_samples_reset(PaceSamples *s, uint64_t nowNs)
{
    s->count = s->next = 0;
    s->firstNs = s->lastNs = nowNs;
}

void pace_samples_add(PaceSamples *s, uint64_t nowNs, uint64_t costNs)
{
    if (s->firstNs == 0) {
        s->firstNs = nowNs;
    }
    s->lastNs = nowNs;
    s->cost[s->next] = costNs;
    s->next = (s->next + 1) % PACE_AUTO_SAMPLES;
    if (s->count < PACE_AUTO_SAMPLES) {
        s->count++;
    }
}

uint64_t pace_samples_median(const PaceSamples *s)
{
    return median_of(s->cost, s->count);
}

int pace_auto_resolution_step(const PaceSamples *s, int currentScale, float windowScale)
{
    if (s->count < PACE_AUTO_MIN_SAMPLES || s->lastNs - s->firstNs < PACE_AUTO_WINDOW_NS ||
        s->budgetNs == 0) {
        return currentScale;
    }
    if (s->lastStepNs != 0 && s->lastNs - s->lastStepNs < PACE_AUTO_WINDOW_NS) {
        return currentScale;
    }
    /* over 70 % of the budget */
    if (pace_samples_median(s) * 10 <= s->budgetNs * 7) {
        return currentScale;
    }
    if (currentScale <= 0) {
        /* the window's size: the largest of 3x, 2x, 1x below it (a window
           at 1x or less has nothing below it: the scene is never smaller
           than the game's own) */
        for (int n = 3; n >= 1; n--) {
            if ((float)n < windowScale - 0.05f) {
                return n;
            }
        }
        return currentScale;
    }
    if (currentScale > 3) {
        return 3;
    }
    return currentScale > 1 ? currentScale - 1 : 1;
}
