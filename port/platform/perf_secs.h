/*
 * port/platform/perf_secs.h
 *
 * Pictures a second by stage (issue 179, docs/port/PLATFORM_DIAGNOSTICS.md
 * "Pictures a second"): one line a 10 s block,
 *   pictures a second: 42: 17 18 9 19 | 40: 12 11 12 (and 4 in the block's
 *     last 0.6 s)
 * each whole second of the block the presented pictures in it, under the
 * stage the game was in at the second's first step (a second with no step
 * keeps the stage before it).  The reporter's v0.11.13 log had the block's
 * presented fps and its worst second only: a block of 17.4 fps could not
 * say whether it was 17 a second throughout or 25 with a hitch, and its
 * blocks straddled the stage changes of the opening.
 *
 * The block's cut: a stage change in the block ends it once it is at
 * least ICO_STATS_CUT_NS (2 s) old (hotkeys.h ico_stats_period_ns asks
 * perf_secs_window_cut_due), so a block holds one stage after its first
 * 2 s.
 *
 * The cost: a step compares the stage and maybe sets a second's stage, a
 * presented picture adds one to its second (a division and an add, on the
 * game thread where the records are drained); no lock, no system call.
 *
 * Pure (port/platform/test/perf_secs_test.c); the window's instance is
 * fed by window_perf_rank.c (the steps), window_clock_trap.c (the
 * presented pictures) and writes its line at the block's end
 * (window_perf_rank_block).
 */
#ifndef ICO_PLATFORM_PERF_SECS_H
#define ICO_PLATFORM_PERF_SECS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the seconds a block holds (a 10 s block's, with room for a block the
   pump held long: a pause, the app away); later ones are counted apart */
#define PERF_SECS_MAX 64u

typedef struct PerfSecs {
    double t0Ms;    /* the block's start (below 0: none yet) */
    int stage;      /* the stage now (PERF_SECS_NO_STAGE: none seen yet) */
    bool changed;   /* the stage changed in the block */
    unsigned n;     /* seconds begun (at most PERF_SECS_MAX held) */
    unsigned over;  /* pictures in seconds past PERF_SECS_MAX */
    unsigned overS; /* those seconds */
    int sec[PERF_SECS_MAX];
    unsigned pics[PERF_SECS_MAX];
    bool stageSet[PERF_SECS_MAX]; /* a step named the second's stage */
} PerfSecs;

#define PERF_SECS_NO_STAGE (-2147483647 - 1)

void perf_secs_reset(PerfSecs *s);
/* A game step at nowMs in stage (the game's stage_no) */
void perf_secs_step(PerfSecs *s, int stage, double nowMs);
/* A presented picture, drained at nowMs */
void perf_secs_picture(PerfSecs *s, double nowMs);
/* Whether the block holds a stage change (the block's cut) */
bool perf_secs_cut_due(const PerfSecs *s);
/* The block's line (after "window: ") at nowMs, its whole seconds by
   stage and the pictures of the part second at the end; "" (0) with no
   second begun.  The next block starts at nowMs in the stage now.  The
   length written. */
size_t perf_secs_block_text(PerfSecs *s, double nowMs, char *out, size_t cap);

/* The window's instance (the game thread's) */
void perf_secs_window_step(int stage, double nowMs);
void perf_secs_window_picture(double nowMs);
bool perf_secs_window_cut_due(void);
size_t perf_secs_window_block_text(double nowMs, char *out, size_t cap);

#endif
