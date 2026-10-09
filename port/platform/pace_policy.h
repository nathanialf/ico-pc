/*
 * port/platform/pace_policy.h
 *
 * The window's "is a present slow" decision, free of SDL so a
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

/* Resolution "auto" (video_options.h ICO_RES_AUTO).  The window
 * feeds each present's cost (the GPU time of a replay when the backend has
 * timestamps, else its CPU time without the acquire and the present, which
 * wait for the display) into a PaceSamples; every PACE_AUTO_WINDOW_NS it asks
 * pace_AutoResolutionStep for the scene scale and starts a new window.
 *
 * The rule: when the median cost over the window (at least
 * PACE_AUTO_WINDOW_NS of samples, at least PACE_AUTO_MIN_SAMPLES of them)
 * is above 70 % of the frame budget (1/60 s, or the frame rate cap's
 * period), the scale steps down once: the window's own size (scale 0) goes
 * to the largest of 3x, 2x, 1x below the window's scale, Nx to (N-1)x and
 * anything above 3x straight to 3x;
 * never below 1x, never back up, and at most once per PACE_AUTO_WINDOW_NS
 * (lastStepNs). */
#define PACE_AUTO_SAMPLES 256
#define PACE_AUTO_MIN_SAMPLES 8
#define PACE_AUTO_WINDOW_NS 2000000000ull

/* Zero-initialise (or pace_SamplesReset) before the first sample. */
typedef struct PaceSamples {
    uint64_t cost[PACE_AUTO_SAMPLES]; /* a ring: the newest PACE_AUTO_SAMPLES */
    unsigned count;                   /* valid entries, up to PACE_AUTO_SAMPLES */
    unsigned next;                    /* where the next cost goes */
    uint64_t firstNs;                 /* the window's start (0: its first sample's time) */
    uint64_t lastNs;                  /* the newest sample's time */
    uint64_t budgetNs;                /* the frame budget the costs are held against */
    uint64_t lastStepNs;              /* when the scale last stepped (0: never) */
} PaceSamples;

/* A new window starting at nowNs: the samples dropped; budgetNs and
   lastStepNs kept. */
void pace_SamplesReset(PaceSamples *s, uint64_t nowNs);

/* One present's cost at time nowNs (any monotonic clock in ns, above 0). */
void pace_SamplesAdd(PaceSamples *s, uint64_t nowNs, uint64_t costNs);

/* The median of the samples (0 with none). */
uint64_t pace_SamplesMedian(const PaceSamples *s);

/* The scene scale resolution "auto" should have: currentScale (0 = the
 * window's size, else N) or the next step down.  windowScale is the
 * window's size against the game's 1x (the presentation box's height over
 * 448), to know which step is below the window's own. */
int pace_AutoResolutionStep(const PaceSamples *s, int currentScale, float windowScale);

#endif /* ICO_PLATFORM_PACE_POLICY_H */
