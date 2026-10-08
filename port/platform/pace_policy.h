/*
 * port/platform/pace_policy.h
 *
 * Package R2: the window's "is a present slow" decision, free of SDL so a
 * unit test checks it. A present with vsync on blocks up to one display
 * refresh, so slow means a cost above max(refresh, period) + period / 2; with
 * an effects program loaded (ReShade, vkBasalt: rhi_InjectorName() != NULL)
 * each present also runs the program's passes, so the threshold is
 * max(refresh, period) + refresh + period / 2.
 *
 * The cost is smoothed (the median of the last 8 presents, so one spike
 * changes nothing) and the state has hysteresis: it turns on after 3 presents
 * in a row above the threshold and off when the median falls below 80 % of it.
 */
#ifndef ICO_PLATFORM_PACE_POLICY_H
#define ICO_PLATFORM_PACE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PACE_HIST_LEN 8
#define PACE_ENTER_COUNT 3

/* Zero-initialise before the first call. */
typedef struct PaceHist {
    uint64_t cost[PACE_HIST_LEN];
    unsigned count; /* valid entries, up to PACE_HIST_LEN */
    unsigned next;  /* where the next cost goes */
    unsigned over;  /* consecutive presents with the median above the threshold */
    bool slow;      /* the state last returned */
} PaceHist;

/* The slow threshold in nanoseconds. */
uint64_t pace_SlowThreshold(uint64_t refreshNs, uint64_t periodNs, bool injector);

/* Records one present's cost and returns whether presents are slow now. */
bool pace_SlowPresent(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                      bool injector);

#endif /* ICO_PLATFORM_PACE_POLICY_H */
