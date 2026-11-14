/*
 * port/platform/clock_trap.h
 *
 * One picture a tick held by the step rule, told to the system (the clock
 * lift), and the lines that time it (docs/port/ANDROID_DIAGNOSTICS.md "The
 * clock lift and the worst second").
 *
 * The owner's Pixel 10 Pro Fold (v0.11.6-rc13, Uncapped at 120 Hz, the cage
 * room) fell to exactly one picture a tick for two 10 s blocks
 * (build-host/tmp/v0117/evidence/owner-cage/ico-pc.log :4233, :5258) while
 * the game held 30 of 30.  The step rule (pace_policy.c pace_rt_step_trace,
 * the PACE_RT_TEST_NEXT test) holds a further picture whose reads would end
 * past the next closing step's slack: with the closing step planned at
 * 12.5 ms and a picture's reads at 16.8 ms (:4235, :4236) no further
 * picture fits a frame of two 16.68 ms steps (S + 2R > 3P - S - margin),
 * so one a tick is that rule's answer at those costs; at the neighbouring
 * blocks' reads of 8-10 ms it gives about two.  What changed was the cost:
 * the render thread's own CPU a picture 7.8-9.2 ms in the cage blocks
 * either side, 15.7 ms in both slow ones, for the same pictures, while its
 * CPU time a second stayed 47-55 % of a core (fps x own CPU) and the
 * cores 2-6 clock averaged 884 and 896 MHz against 997-1277; the GPU's
 * work 13.2-16.2 ms a picture against 20.6-21.4, its busy share 82-97 %
 * against 63-64 %.  The governors saw the same load either way: two steady
 * states, two pictures a tick at the higher clocks, one at the lower, and
 * nothing in the port moves the system from the lower one.  The performance
 * hint said "over target" in both (the 7.81 ms target against 9-10 ms and
 * 22-23 ms, :4166, :4253): it cannot tell them apart.
 *
 * The attempt: a second at one picture a tick with the step rule holding
 * at least half its asks (the pictures were wanted, the rule judged them
 * too slow) asks the system once for a heavier CPU and GPU load
 * (APerformanceHint_notifyWorkloadIncrease, Android 16 and later, the hint
 * thread's call: window_perf_hint.c), at most once every
 * CLOCK_TRAP_LIFT_GAP_MS.  Nothing here changes a picture, the step rule or
 * the frame rate.
 *
 * The diagnostics: the block's worst second (the fewest pictures a tick)
 * with the step rule's asks and holds in it, a picture's CPU, and the CPU
 * clock samples inside it by core cluster; and the clock dip named on the
 * ranked line when a cluster's average falls under CLOCK_TRAP_DIP_SHARE of
 * the median of the blocks before.
 *
 * Pure: no SDL, no Android (port/platform/test/clock_trap_test.c).  The
 * window's side is window_clock_trap.c.
 */
#ifndef ICO_PLATFORM_CLOCK_TRAP_H
#define ICO_PLATFORM_CLOCK_TRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gpu_sysfs.h"

/* a second with fewer game frames is not judged (a load, a pause, a menu) */
#define CLOCK_TRAP_MIN_FRAMES 20u
/* a lift asked at most once in this many ms (the system rate-limits its own
   too: its answer is counted) */
#define CLOCK_TRAP_LIFT_GAP_MS 2000.0
/* nothing for this long since the second's start: the next second starts at
   the next event (the app in the background, a pause) */
#define CLOCK_TRAP_GAP_RESET_MS 2000.0
/* a cluster's block average under this share of the median of the blocks
   before is a dip; the samples it needs; the blocks kept */
#define CLOCK_TRAP_DIP_SHARE 0.8
#define CLOCK_TRAP_DIP_MIN_SAMPLES 8u
#define CLOCK_TRAP_HIST 6u
#define CLOCK_TRAP_CLUSTERS 4

/* One second of the game thread's view: the pictures presented (drained),
   their game frames (a frame number new against the one before), the step
   rule's asks and holds, the pictures' CPU (perf_hint.h perf_hint_work's:
   the record's time less its waits), whether an ask was on the as-made
   path (vsync off, uncapped) */
typedef struct ClockTrapSecond {
    double t0Ms;
    unsigned pictures, frames, asks, held;
    double cpuMs;
    unsigned cpuN;
    bool asMade;
} ClockTrapSecond;

typedef struct ClockTrapBlock {
    bool haveWorst;
    ClockTrapSecond worst;
    unsigned seconds, oneATick, lifts;
    double cpuMs;
    unsigned cpuN;
} ClockTrapBlock;

typedef struct ClockTrap {
    ClockTrapSecond cur;
    bool open;
    uint32_t lastFrame;
    bool haveFrame;
    double lastLiftMs;
    bool lifted;
    ClockTrapBlock block;
} ClockTrap;

enum {
    CLOCK_TRAP_EV_CLOSED = 1, /* a second closed (*closed filled) */
    CLOCK_TRAP_EV_LIFT = 2    /* and the lift is due for it */
};

void clock_trap_reset(ClockTrap *c);

/* The rule: an as-made second of at least CLOCK_TRAP_MIN_FRAMES frames with
   at most 1.1 pictures a frame (pictures x 10 <= frames x 11), at least half
   a frame's worth of asks (asks x 2 >= frames: the pictures were wanted)
   and at least half of them held by the step rule (held x 2 >= asks) */
bool clock_trap_one_a_tick(const ClockTrapSecond *s);

/* A presented picture drained at tMs (frame its game frame; cpuMs its CPU
   time, below 0 not known); an ask of the step rule at tMs (held: held by
   it; asMade: on the as-made path).  Each first closes the seconds tMs has
   passed: CLOCK_TRAP_EV_* bits, *closed the last second closed (not NULL) */
unsigned clock_trap_picture(ClockTrap *c, double tMs, uint32_t frame, double cpuMs,
                            ClockTrapSecond *closed);
unsigned clock_trap_ask(ClockTrap *c, double tMs, bool held, bool asMade, ClockTrapSecond *closed);

/* a worse than b: fewer pictures a frame, then more CPU a picture */
bool clock_trap_worse(const ClockTrapSecond *a, const ClockTrapSecond *b);

/* The block's numbers since the last take, then zeroed */
void clock_trap_block_take(ClockTrap *c, ClockTrapBlock *out);

/* --- the clusters (runs of cores of one top speed, gpu_sysfs.h's) -------- */

typedef struct ClockTrapClusters {
    int n;
    int first[CLOCK_TRAP_CLUSTERS], last[CLOCK_TRAP_CLUSTERS];
    double mean[CLOCK_TRAP_CLUSTERS]; /* MHz over the block's samples */
    unsigned samples[CLOCK_TRAP_CLUSTERS];
} ClockTrapClusters;

/* from the block's per-core sums (the first CLOCK_TRAP_CLUSTERS runs) */
void clock_trap_clusters(const GpuSysfsBlock *b, ClockTrapClusters *out);

typedef struct ClockTrapHist {
    ClockTrapClusters h[CLOCK_TRAP_HIST];
    unsigned n, at;
} ClockTrapHist;

void clock_trap_hist_push(ClockTrapHist *h, const ClockTrapClusters *c);

typedef struct ClockTrapDip {
    int first, last;
    double mean, median;
    unsigned blocks;
} ClockTrapDip;

/* The clusters of now under CLOCK_TRAP_DIP_SHARE of the median of the same
   cores' averages in the blocks kept (at least two of them, now with at
   least CLOCK_TRAP_DIP_MIN_SAMPLES samples): their count (at most cap) */
int clock_trap_dips(const ClockTrapHist *h, const ClockTrapClusters *now, ClockTrapDip *out,
                    int cap);

/* --- the lines (without "window: "; the length snprintf gives) ----------- */

/* What the performance hint said in the block (window_perf_hint.c); known
   false off Android or without a session */
typedef struct ClockTrapHint {
    bool known;
    double targetMs, cpuMs, gpuMs;
    unsigned reports, over;
    unsigned liftsAsked, liftsTaken;
} ClockTrapHint;

/* The ranked line's clause, from "; the CPU clocks under their level: " on
   (nothing for no dip) */
int clock_trap_dip_text(const ClockTrapDip *d, int n, const ClockTrapHint *hint, char *out,
                        size_t cap);

/* "worst second of the block: ..." with the ring's CPU clock samples inside
   it (ring NULL: not sampled here) and the second's start against the
   block's (blockStartMs) */
int clock_trap_worst_text(const ClockTrapBlock *b, double blockStartMs, const GpuSysfsRing *ring,
                          char *out, size_t cap);

/* "clock lift asked: ..." (the event line on the game thread) */
int clock_trap_lift_text(const ClockTrapSecond *s, char *out, size_t cap);

#endif /* ICO_PLATFORM_CLOCK_TRAP_H */
