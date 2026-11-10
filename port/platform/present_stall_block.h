/*
 * port/platform/present_stall_block.h
 *
 * Issue 123 (Adreno 740 on Qualcomm's driver): presents that block about
 * 340 ms inside vkQueuePresentKHR, every pass of the stalled replay about
 * 3x its block's mean and the GPU's timestamp counter restarted at each
 * one (rd_perf_gap.h).  The hypothesis is a GPU power-state change at the
 * stall, and the v0.10.2 deep dive saw the stalls only while the GPU span
 * sat in a 26-35 ms band.  This file makes that measurable from a log
 * alone: per 10 s block, and once a run at exit, the stalls against the
 * block's GPU span band and the display's rate.
 *
 * The window feeds every popped replay record (window_stall.c, from
 * window_stall_after) and closes each 10 s block.  The block's line, from
 * the run's first stall on (a block with a stall always; the blocks before
 * the first are counted into the run's sums without a line):
 *     window: present stalls: 2 over 250 ms in 10.0 s (340, 338 ms; GPU
 *     idle before the stalled replay: not measured (the GPU clock
 *     restarted), 0.9 ms; GPU clock restarts 2); GPU span 27.4 ms a
 *     replay (26.7-28.7 ms, band 25-30 ms; stalled replays 85.1, 88.5 ms);
 *     display 120.00 Hz, frame rate pin 59.94 Hz asked; GPU clock not
 *     readable
 * The run's line at exit:
 *     window: present stalls this run: 7 over 250 ms in 3.3 min (2.1 a
 *     minute); by display rate: 120.00 Hz 7 in 3.3 min (2.1 a minute); by
 *     the block's GPU span: 25-30 ms 7 in 3.0 min (2.3 a minute), 20-25 ms
 *     0 in 0.3 min (0.0 a minute)
 *
 * Pure C, no globals: the unit test is test/present_stall_block_test.c.
 */
#ifndef ICO_PLATFORM_PRESENT_STALL_BLOCK_H
#define ICO_PLATFORM_PRESENT_STALL_BLOCK_H

#include <stddef.h>

/* a present this long or longer is a stall (present_hist.h
   PRESENT_STALL_LINE_MS: the blocked line's own size) */
#define STALL_BLOCK_MS 250.0
/* the stalls a block's line lists one by one (the count is whole) */
#define STALL_BLOCK_LIST 6
/* display rates the run tells apart (a later one past the last is
   counted with the last) */
#define STALL_BLOCK_RATES 6
/* the GPU span bands: 5 ms each, the last 60 ms and over */
#define STALL_BLOCK_BAND_MS 5.0
#define STALL_BLOCK_BANDS 13

/* one replay's record, as the window has it (rd.h RdPerfRecord) */
typedef struct StallBlockRec {
    int presented;
    double presentMs;
    int gpuValid;
    double gpuMs;
    int gapValid;     /* gapMs measured (RdPerfRecord.gpuGapValid) */
    double gapMs;     /* the GPU idle before it; under 0 begun before the one before ended */
    double gapBackMs; /* the GPU's timestamp clock went back this much (a restart), 0 not */
} StallBlockRec;

typedef struct StallBlock {
    /* the block */
    unsigned presents, stalls, restarts;
    double stallMs[STALL_BLOCK_LIST], stallGpuMs[STALL_BLOCK_LIST], stallGapMs[STALL_BLOCK_LIST];
    /* per listed stall: 1 the gap measured, 2 the clock restarted, 0 not measured */
    int stallGap[STALL_BLOCK_LIST];
    int stallGpu[STALL_BLOCK_LIST]; /* stallGpuMs valid */
    unsigned spanN;                 /* timed replays that did not stall */
    double spanSum, spanMin, spanMax;
    /* the run */
    unsigned runStalls, runBlocks;
    double runSeconds;
    unsigned rateN;
    double rateHz[STALL_BLOCK_RATES], rateSeconds[STALL_BLOCK_RATES];
    unsigned rateStalls[STALL_BLOCK_RATES];
    double rateUnknownSeconds;
    unsigned rateUnknownStalls;
    double bandSeconds[STALL_BLOCK_BANDS];
    unsigned bandStalls[STALL_BLOCK_BANDS];
    double bandNoneSeconds; /* blocks without a timed replay */
    unsigned bandNoneStalls;
} StallBlock;

void stall_block_init(StallBlock *b);
/* one popped record (any: presented or not, timed or not) */
void stall_block_add(StallBlock *b, const StallBlockRec *r);
/* the block's GPU span band index (0 .. STALL_BLOCK_BANDS - 1), -1 no
   timed replay that did not stall */
int stall_block_band(const StallBlock *b);

/* The GPU clock as the kernel driver reported it in the block (n samples
   with a clock, 0: not readable) */
typedef struct StallBlockClock {
    unsigned n;
    int minMhz, maxMhz;
    double meanMhz;
} StallBlockClock;

/* The block's end, after blockSeconds: its line (without "window: ") into
   buf when the run has had a stall (returns 1), else buf "" (0); the block
   then joins the run's sums (displayHz the display's rate now, 0 not
   known; askedMhz the frame rate pin's ask, 0 none) and starts again */
int stall_block_end(StallBlock *b, double blockSeconds, double displayHz, int askedMhz,
                    const StallBlockClock *clock, char *buf, size_t size);
/* the run's line at exit (without "window: "); "" (0) when no block
   closed */
int stall_block_run_text(const StallBlock *b, char *buf, size_t size);

#endif /* ICO_PLATFORM_PRESENT_STALL_BLOCK_H */
