/*
 * port/platform/pin_release.h
 *
 * The render and game threads' single-core pins let go while the game is
 * stuck at one picture a tick, and taken again once it has passed
 * (docs/port/ANDROID_DIAGNOSTICS.md "The pins let go at one picture a
 * tick").  Pure: the game thread feeds it each closed second
 * (clock_trap.h) and each drained picture's cores (window_clock_trap.c);
 * the keeper moves the threads (cpu_keep.h cpu_keep_release, through
 * render_thread.c's atomic).
 *
 * Why (the owner's Pixel 10 Pro Fold, Uncapped at 120 Hz, the cage room):
 * stretches of exactly one picture a tick with the render thread's own CPU
 * at 15-24 ms a picture against 7-9 ms for the same scene, the render
 * thread's core sampled at about 883 MHz, came with the render thread
 * pinned to core 6 alone (v0.11.6-rc13, owner-cage :4233, :5258) and to
 * core 7 alone (v0.11.7-rc1 :5321-:5415, rc2 owner-rc2b :4249-:4457).  No
 * stage 4 block of v0.11.6 or later ran with the render thread on several
 * cores, so the logs cannot say whether a scheduler free to place it would
 * leave that state; Android's guidance is that a game should not set its
 * threads' affinity at all (developer.android.com, the Performance Hint API
 * and the thread scheduling pages).  The single-core pin was measured to
 * help on this phone (core 7 alone against 2-7: v0.11.5 rc9 against rc11,
 * the bridge's reads 6.0-6.4 ms against 8.3-8.6), so it is not taken away:
 * it is let go in the stuck state only, and the log compares the two.
 *
 * The rule, at each closed second:
 *   stuck: clock_trap_one_a_tick (as made, at least 20 game frames, at most
 *     1.1 pictures a frame, asks at least half the frames, at least half of
 *     them held by the step rule) with the second's picture CPU raised
 *     (clock_trap_cpu_raised: 1.25 times the blocks' median, the ranked
 *     line's own rise);
 *   pinned and stuck, where the keeper's rule allows it: let go
 *     (PIN_RELEASE_EV_RELEASE);
 *   let go: each judged second (at least 20 game frames) that is not stuck
 *     counts toward PIN_RELEASE_STABLE_S, a stuck one starts the count
 *     again; at the count, taken again (PIN_RELEASE_EV_RESTORE).
 *   Seconds of fewer frames (a load, a pause, a menu) count neither way.
 * PIN_RELEASE_STABLE_S is the keeper's own 10 s (CPU_KEEP_RETRY_MS) and the
 * 10 s block: the let-go state lasts at least one whole block once it has
 * recovered, so a block line shows it.
 *
 * The trace (PIN_RELEASE_EV_TRACE): every stuck second, every second while
 * let go, and PIN_RELEASE_TRACE_AFTER seconds after the pins are taken
 * again, one line each, at most PIN_RELEASE_TRACE_MAX lines a run.
 */
#ifndef ICO_PLATFORM_PIN_RELEASE_H
#define ICO_PLATFORM_PIN_RELEASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clock_trap.h"

#define PIN_RELEASE_STABLE_S 10u
#define PIN_RELEASE_TRACE_AFTER 5u
#define PIN_RELEASE_TRACE_MAX 300u
#define PIN_RELEASE_CORES 16

/* the threads' cores over a span (a second, a block): the samples on each
   core (the game thread's, one a drained picture: the render thread's
   core at its last picture, the game thread's own now), those with no core
   known, and the moves (a sample on another core than the one before) */
typedef struct PinCores {
    uint32_t render[PIN_RELEASE_CORES], game[PIN_RELEASE_CORES];
    uint32_t renderUnknown, gameUnknown;
    uint32_t renderMoves, gameMoves;
} PinCores;

typedef struct PinRelease {
    bool released;
    unsigned good;       /* judged seconds not stuck since the release or the last stuck one */
    unsigned heldS;      /* seconds closed since the release */
    unsigned heldStuck;  /* of them stuck */
    unsigned traceAfter; /* seconds still traced after the pins came back */
    unsigned traceK;     /* the trace's seconds in the current state */
    unsigned traceLines; /* written this run */
    int label;           /* the last second's state for the trace (pin_release.c) */
    unsigned labelN;     /* its second's number in that state */
    int lastRender, lastGame;
    PinCores sec, block;
    /* the block's counts (taken by pin_release_block_text) and the run's */
    unsigned blkReleases, blkRestores, blkReleasedS, blkStuckPinned, blkStuckReleased;
    unsigned runReleases, runRestores;
} PinRelease;

enum {
    PIN_RELEASE_EV_RELEASE = 1,   /* let the pins go now */
    PIN_RELEASE_EV_RESTORE = 2,   /* take them again now */
    PIN_RELEASE_EV_TRACE = 4,     /* trace this second (pin_release_trace_text) */
    PIN_RELEASE_EV_STUCK = 8,     /* the second was stuck */
    PIN_RELEASE_EV_TRACE_END = 16 /* the trace's lines spent: "no more this run" once */
};

void pin_release_reset(PinRelease *p);
/* one drained picture: the render thread's core at its last picture and
   the game thread's now (-1: not known) */
void pin_release_note(PinRelease *p, int renderCore, int gameCore);
/* one closed second (clock_trap.h CLOCK_TRAP_EV_CLOSED), the blocks'
   picture CPU median and their count (clock_trap_cpu_median), allowed: the
   keeper's rule lets the pins go here (pin release on).  The second's
   cores are kept in *cores (not NULL) for the trace and the second's
   counts emptied.  PIN_RELEASE_EV_* bits; past PIN_RELEASE_TRACE_MAX
   lines no TRACE bit */
unsigned pin_release_second(PinRelease *p, const ClockTrapSecond *s, double cpuMedian,
                            unsigned cpuBlocks, bool allowed, PinCores *cores);
/* the second judged stuck (the rule above) */
bool pin_release_stuck(const ClockTrapSecond *s, double cpuMedian, unsigned cpuBlocks);

/* --- the lines (without "window: "; the length snprintf gives) ----------- */

/* "pins let go: 30 pictures for 30 game frames in a second (one a tick)
   with a picture's CPU 21.3 ms against 8.5 (the blocks' median): the
   render and game threads may run on any faster core until 10 seconds
   pass without one picture a tick; 1 this run" (median 0: "(no blocks'
   level yet)") */
int pin_release_release_text(const PinRelease *p, const ClockTrapSecond *s, double cpuMedian,
                             char *out, size_t cap);
/* "pins taken again: 10 seconds without one picture a tick; let go for 24
   s, 12 of them at one a tick; 1 this run" */
int pin_release_restore_text(const PinRelease *p, char *out, size_t cap);

/* The trace line: "pin trace (let go, second 3): <clock_trap_second_text>;
   the render thread on core 7 30, core 6 1 (core changes 2), may run on
   2-7; the game thread on core 6 31 (core changes 0), may run on 2-7; their
   cores' clocks
   in that second (MHz): core 7 1305 1305 883 1305, core 6 898 608 760
   1209".  The state: "pinned, at one a tick", "let go, second N",
   "pinned again, second N of 5".  renderAllowed / gameAllowed the cores
   each may run on (0 not known); ring NULL: "their cores' clocks not
   sampled here" */
int pin_release_trace_text(const PinRelease *p, const ClockTrapSecond *s, const PinCores *c,
                           uint64_t renderAllowed, uint64_t gameAllowed, const GpuSysfsRing *ring,
                           char *out, size_t cap);

/* The 10 s block's clause after the worst-second line: "; the render
   thread's cores: 7 98 % (1180 MHz), 6 2 % (900 MHz), core changes 4; the
   game thread's: 6 100 % (950 MHz), core changes 0; pins let go 1, taken
   again 0, let go for 6 s of the block (at one a tick 3 s pinned, 2 s let
   go)"
   (coreMhz each core's block average, 0 not sampled: "(clock not
   sampled)"; ruleWords for the keeper's rule when the pins cannot be let
   go: "; pins not let go here (<ruleWords>)"); the block's counts taken */
int pin_release_block_text(PinRelease *p, const double *coreMhz, int coreCount,
                           const char *ruleWords, char *out, size_t cap);

#endif /* ICO_PLATFORM_PIN_RELEASE_H */
