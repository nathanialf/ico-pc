/*
 * port/platform/clock_trap.h
 *
 * One picture a tick held by the step rule: the seconds that judge it (the
 * pins let go on them, pin_release.h), and the lines that time it
 * (docs/port/ANDROID_DIAGNOSTICS.md "One picture a tick and the worst
 * second").
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
 * A second at one picture a tick with the step rule holding at least half
 * its asks (the pictures were wanted, the rule judged them too slow) is
 * CLOCK_TRAP_EV_ONE at its close.  v0.11.7-rc2 told the system of each
 * such second (APerformanceHint_notifyWorkloadIncrease, every 2 s at
 * most): every call was taken on the owner's Pixel and the seconds after
 * stayed at one a tick with the same clocks (owner-rc2b :4233-:4422); the
 * call is documented to act for one cycle and not for an ongoing heavy
 * load, so it was taken out.  The attempt since is pin_release.h's.
 * Nothing here changes a picture, the step rule or the frame rate.
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

/* a second with fewer game frames is not at one a tick (a load, a pause, a
   menu); nor judged for the worst second where the rate in force is not
   known (below) */
#define CLOCK_TRAP_MIN_FRAMES 20u
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
    /* the pictures a second the presents aimed at, at the second's last
       picture (clock_trap_target; 0 not known) */
    double targetHz;
} ClockTrapSecond;

/* The worst second (issue 179's v0.11.7 log): the 20-frame rule skipped
   every second of a 25 fps disc's dips (12-13 pictures a second, "none
   judged") and a second at one picture a frame read 1.00 a tick however
   few frames the game made.  A second is judged by its presented pictures
   against the rate in force (Frame rate Original's game frames a second,
   a cap, the refresh: slow_judge.h) when that rate is known: every whole
   second (closed by an event within CLOCK_TRAP_GAP_RESET_MS of its start,
   so not one the app spent away) with a picture; the worst has the
   smallest share of the rate, then more CPU a picture.  Where the rate is
   not known, v0.11.7's rule: seconds of CLOCK_TRAP_MIN_FRAMES frames or
   more, the fewest pictures a frame. */

typedef struct ClockTrapBlock {
    bool haveWorst;
    ClockTrapSecond worst;
    /* seconds of CLOCK_TRAP_MIN_FRAMES frames or more, those of them at
       one a tick; judged the seconds judged for the worst */
    unsigned seconds, oneATick, judged;
    double cpuMs;
    unsigned cpuN;
    unsigned pictures, frames; /* the block's (every picture drained, every new frame) */
} ClockTrapBlock;

typedef struct ClockTrap {
    ClockTrapSecond cur;
    bool open;
    uint32_t lastFrame;
    bool haveFrame;
    ClockTrapBlock block;
    double targetHz; /* clock_trap_target's */
} ClockTrap;

enum {
    CLOCK_TRAP_EV_CLOSED = 1, /* a second closed (*closed filled) */
    CLOCK_TRAP_EV_ONE = 2     /* and it was at one a tick (clock_trap_one_a_tick) */
};

void clock_trap_reset(ClockTrap *c);
/* The pictures a second the presents aim at from now (0 not known): each
   picture's second takes it */
void clock_trap_target(ClockTrap *c, double hz);

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

/* a worse than b: both with a rate in force, the smaller share of it
   (pictures / targetHz); else fewer pictures a frame; then more CPU a
   picture */
bool clock_trap_worse(const ClockTrapSecond *a, const ClockTrapSecond *b);
/* whether a closed second is judged for the worst (whole: closed by an
   event within CLOCK_TRAP_GAP_RESET_MS of its start) */
bool clock_trap_judged(const ClockTrapSecond *s, bool whole);

/* The block's numbers since the last take, then zeroed */
void clock_trap_block_take(ClockTrap *c, ClockTrapBlock *out);

/* --- the clusters (runs of cores of one top speed, gpu_sysfs.h's) -------- */

typedef struct ClockTrapClusters {
    int n;
    int first[CLOCK_TRAP_CLUSTERS], last[CLOCK_TRAP_CLUSTERS];
    double mean[CLOCK_TRAP_CLUSTERS]; /* MHz over the block's samples */
    unsigned samples[CLOCK_TRAP_CLUSTERS];
    double cpuMs; /* the block's picture CPU (ClockTrapBlock's mean); 0 not known */
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

/* --- when the ranked line names the clocks ---------------------------------- */

/* v0.11.7-rc1 on the owner's Pixel: the dip clause fired on good blocks
   after a capture or a resume (47-102 presented fps, 1.6-3.4 pictures a
   tick: :2966, :3204, :3321, :3450, :3567) and not where it mattered.  It
   is said only for a block at or near one picture a tick
   (pictures x 10 <= frames x CLOCK_TRAP_DUE_TICK_TENTHS) whose picture
   CPU is CLOCK_TRAP_DUE_CPU_RISE times the median of the blocks kept or
   more (at least two with a CPU known): the rc13 log's slow blocks, 1.0 a
   tick at 22.6 ms against 9-10 ms, are such blocks. */
#define CLOCK_TRAP_DUE_TICK_TENTHS 15u
#define CLOCK_TRAP_DUE_CPU_RISE 1.25

typedef struct ClockTrapDue {
    bool due;
    unsigned pictures, frames;
    double cpuMs, cpuMedian;
    unsigned cpuBlocks;
    /* the render thread's core at its last picture (-1 not known), its
       average clock in the block (0 not sampled), its cluster's cores and
       that cluster's median over the blocks kept (0 none) */
    int core;
    double coreMhz;
    int clFirst, clLast;
    double clMedian;
    unsigned clBlocks;
} ClockTrapDue;

/* The block's pictures, frames and picture CPU (cpuMs 0: not known)
   against the blocks kept; now the block's clusters, core the render
   thread's core and coreMhz its block average (0 not sampled): out
   filled, out->due returned */
bool clock_trap_due(const ClockTrapHist *h, const ClockTrapClusters *now, unsigned pictures,
                    unsigned frames, double cpuMs, int core, double coreMhz, ClockTrapDue *out);

/* "; at 1.02 pictures a tick with a picture's CPU 22.6 ms against 9.4 (the
   median of the 6 blocks before); the render thread on core 6 averaged 870
   MHz in the block, its cluster cores 2-6 at 1197 before (73 %)" */
int clock_trap_due_text(const ClockTrapDue *d, char *out, size_t cap);

/* --- the lines (without "window: "; the length snprintf gives) ----------- */

/* What the performance hint said in the block (window_perf_hint.c); known
   false off Android or without a session */
typedef struct ClockTrapHint {
    bool known;
    double targetMs, cpuMs, gpuMs;
    unsigned reports, over;
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

/* One second for the pin trace (pin_release.h): "41 pictures for 30 game
   frames (1.37 a tick), a picture's CPU 12.3 ms; the step rule held 12 of
   40 further pictures asked; the CPU clocks in that second (4 samples,
   MHz): cores 0-1 ..., cores 2-6 ..., core 7 ..." (ring NULL: "the CPU
   clocks not sampled here") */
int clock_trap_second_text(const ClockTrapSecond *s, const GpuSysfsRing *ring, char *out,
                           size_t cap);

/* The median of the blocks' picture CPU kept in h (those with one known),
   their count in *blocks; 0 with none */
double clock_trap_cpu_median(const ClockTrapHist *h, unsigned *blocks);

/* A second's picture CPU raised: at least CLOCK_TRAP_DUE_CPU_RISE times
   the blocks' median when two or more blocks have one, else any CPU known
   (the first blocks of a run have no level to rise from); false with the
   second's CPU not known */
bool clock_trap_cpu_raised(const ClockTrapSecond *s, double median, unsigned blocks);

#endif /* ICO_PLATFORM_CLOCK_TRAP_H */
