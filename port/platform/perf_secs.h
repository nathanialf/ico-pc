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
 * blocks straddled the stage changes of the opening.  The blocks stay
 * 10 s whatever the stage does (hotkeys.h ico_stats_period_ns): every
 * per-block rule (the pass timing's disarm, the CPU pin's release history,
 * the slow-scene capture's judgement) keeps its time; a block that holds a
 * stage change shows it in this line, each second under its own stage.
 *
 * The cost: a step compares the stage and maybe sets a second's stage, a
 * presented picture adds one to its second (a division and an add, on the
 * game thread where the records are drained); no lock, no system call.
 *
 * Issue 179's v0.11.14 log: the boat scene's stretches at 7-12 pictures a
 * second against 16-22 between, and whether the GPU slowed as a whole (its
 * clock is not readable on that phone).  Two more lines a block, after
 * that one, from every drained record (window_stall.c window_stall_after):
 *   per second (the timed pictures' medians: GPU span ms, VU triangles,
 *     reduction ms, present pass ms; then the longest present interval
 *     ms): 42: 45.1/61.2k/2.40/2.68/81 46.0/62.0k/2.41/2.66/120 | 40: ...
 *   present intervals (the CPU's present returns): 208 in the block, p50
 *     40.1 ms, p90 80.3 ms, longest 148.2 ms; over 2 ticks 31, over 3
 *     ticks 12, over 4 ticks 5 (a tick 20.0 ms; an interval in whole
 *     ticks, rounded)
 * The reduction and the present pass are the same work every picture at
 * one size: their times a second are a measure of the GPU's speed that
 * needs no clock node (where the present pass also holds the wait for the
 * display's image, a Mali's, its time grows with that wait: the reduction
 * is the cleaner one).  A present interval: from one presented picture's
 * present return to the next's, on the CPU's clock (not the display's
 * timing); the percentiles by nearest rank; "-" a value no picture of the
 * second had.  The tick is the game's pace (WindowPerfBlock.paceHz: 50 Hz
 * PAL, so Original's 25 a second is 2 ticks a picture); with none known
 * the ticks are left out.
 *
 * Pure (port/platform/test/perf_secs_test.c); the window's instance is
 * fed by window_perf_rank.c (the steps), window_clock_trap.c (the
 * presented pictures), window_stall.c (every record) and writes its lines
 * at the block's end (window_perf_rank_block).
 */
#ifndef ICO_PLATFORM_PERF_SECS_H
#define ICO_PLATFORM_PERF_SECS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thermal_read.h" /* the phone's heat and clocks a second */

/* the seconds a block holds (a 10 s block's, with room for a block the
   pump held long: a pause, the app away); later ones are counted apart */
#define PERF_SECS_MAX 64u
/* the records and present intervals a block holds (past them counted) */
#define PERF_SECS_RECS 2048u
#define PERF_SECS_IVS 2048u

/* a drained record's part (window_stall.c): gpuValid its GPU times known */
typedef struct PerfSecsRec {
    bool presented;
    double presentEndMs; /* its present's return (0 not known) */
    bool gpuValid;
    double gpuMs, reductionMs, presentPassMs;
    double tris; /* the VU triangles */
} PerfSecsRec;

typedef struct PerfSecsVal {
    uint8_t sec;
    float gpu, tris, red, pres; /* gpu, red, pres < 0: not timed */
} PerfSecsVal;

typedef struct PerfSecs {
    double t0Ms;    /* the block's start (below 0: none yet) */
    int stage;      /* the stage now (PERF_SECS_NO_STAGE: none seen yet) */
    unsigned n;     /* seconds begun (at most PERF_SECS_MAX held) */
    unsigned over;  /* pictures in seconds past PERF_SECS_MAX */
    unsigned overS; /* those seconds */
    int sec[PERF_SECS_MAX];
    unsigned pics[PERF_SECS_MAX];
    bool stageSet[PERF_SECS_MAX]; /* a step named the second's stage */
    /* the block's records (their second, their times) and present
       intervals; the longest interval of each second; the last present's
       return, kept across blocks (0 none) */
    unsigned recN, recOver;
    PerfSecsVal rec[PERF_SECS_RECS];
    float longest[PERF_SECS_MAX];
    unsigned ivN, ivOver;
    float iv[PERF_SECS_IVS];
    double ivMax, lastEndMs;
    /* the phone's heat and clocks (thermal_read.h), the last reading in
       each second */
    ThermalSample phone[PERF_SECS_MAX];
    bool phoneSet[PERF_SECS_MAX];
} PerfSecs;

#define PERF_SECS_NO_STAGE (-2147483647 - 1)

void perf_secs_reset(PerfSecs *s);
/* A game step at nowMs in stage (the game's stage_no) */
void perf_secs_step(PerfSecs *s, int stage, double nowMs);
/* A presented picture, drained at nowMs */
void perf_secs_picture(PerfSecs *s, double nowMs);
/* The block's line (after "window: ") at nowMs, its whole seconds by
   stage and the pictures of the part second at the end; "" (0) with no
   second begun.  The next block starts at nowMs in the stage now.  The
   length written. */
size_t perf_secs_block_text(PerfSecs *s, double nowMs, char *out, size_t cap);
/* A drained record at nowMs (every one: its triangles; a timed one its
   GPU times; a presented one its present interval) */
void perf_secs_record(PerfSecs *s, const PerfSecsRec *r, double nowMs);
/* perf_secs_block_text's line into out and the two more into more and
   intervals ("" each with nothing to say; NULL or 0 leaves one out), the
   tick tickMs (<= 0 not known); the first line's length */
size_t perf_secs_block_lines(PerfSecs *s, double nowMs, double tickMs, char *out, size_t cap,
                             char *more, size_t moreCap, char *intervals, size_t ivCap);

/* The phone's reading at nowMs (the log thread's, as the game thread saw
   it): its second's (the last wins) */
void perf_secs_phone(PerfSecs *s, const ThermalSample *t, double nowMs);
/* Before perf_secs_block_lines (which empties the block): the phone's
   line a second into perSec,
     phone a second (thermal status/headroom/hottest zone C/each CPU
       cluster's clock:allowed most MHz): 42: 1/0.67/46.5/1200:1800,2000:1500
       - 1/0.68/46.9/... | 40: ...
   ("-" a second with no reading) and the block's thermal line into block
   (thermal_read.h thermal_read_block_text; c the clusters' cores, NULL
   none); "" each with no reading in the block.  The length of perSec */
size_t perf_secs_phone_lines(PerfSecs *s, double nowMs, const ThermalCpus *c, char *perSec,
                             size_t cap, char *block, size_t blockCap);

/* The window's instance (the game thread's) */
void perf_secs_window_phone(const ThermalSample *t, double nowMs);
size_t perf_secs_window_phone_lines(double nowMs, const ThermalCpus *c, char *perSec, size_t cap,
                                    char *block, size_t blockCap);
void perf_secs_window_step(int stage, double nowMs);
void perf_secs_window_picture(double nowMs);
void perf_secs_window_record(const PerfSecsRec *r, double nowMs);
size_t perf_secs_window_block_text(double nowMs, char *out, size_t cap);
size_t perf_secs_window_block_lines(double nowMs, double tickMs, char *out, size_t cap, char *more,
                                    size_t moreCap, char *intervals, size_t ivCap);

#endif
