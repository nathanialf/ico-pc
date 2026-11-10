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
 * Three mechanisms survive the logs (docs/port/ANDROID_DIAGNOSTICS.md
 * "Issue 123's present stalls"), and each stall's entry carries the
 * figure that tells them apart:
 *   - the kernel driver's slumber needs the GPU idle for its idle timer
 *     (80 ms by default): "the replay before began N ms earlier" on the
 *     CPU's clock bounds that idle, as the GPU's counter cannot (it
 *     restarts at the stall);
 *   - a GPU fault and the driver's reset and replay: a long time the GPU
 *     ran none of the replay's work ("the present held N ms past the
 *     replay's GPU span") with the counter restarted and the clock low
 *     after it;
 *   - the present's queue alone: the same held time with no restart and
 *     a span like the block's.
 *
 * The window feeds every popped replay record (window_stall.c, from
 * window_stall_after) and closes each 10 s block.  The block's line, from
 * the run's first stall on (a block with a stall always; the blocks before
 * the first are counted into the run's sums without a line):
 *     window: present stalls: 1 over 250 ms in 10.0 s (340 ms at 120.00
 *     Hz: the replay before began 33.4 ms earlier, the present held 253.3
 *     ms past the replay's GPU span, GPU idle before it not measured (the
 *     GPU clock restarted)); GPU clock restarts 1; GPU span 27.4 ms a
 *     replay (26.7-28.7 ms, band 25-30 ms; stalled replays 87.1 ms);
 *     display 120.00 Hz; GPU clock not readable
 * The run's line at exit:
 *     window: present stalls this run: 7 over 250 ms in 3.3 min (2.1 a
 *     minute); 7 with the GPU clock restarted, 1 after a CPU-side gap of
 *     80 ms or more (6 under, 0 not known), held past the GPU span
 *     248-262 ms; by display rate: 120.00 Hz 7 in 3.3 min (2.1 a
 *     minute); by the block's GPU span: 25-30 ms 7 in 3.0 min (2.3 a
 *     minute), 20-25 ms 0 in 0.3 min (0.0 a minute)
 *
 * Pure C, no globals: the unit test is test/present_stall_block_test.c.
 */
#ifndef ICO_PLATFORM_PRESENT_STALL_BLOCK_H
#define ICO_PLATFORM_PRESENT_STALL_BLOCK_H

#include <stddef.h>

/* the kernel driver's GPU idle timeout before slumber, ms (the
   QCOM_KGSL_IDLE_TIMEOUT default; a vendor kernel may set another) */
#define STALL_BLOCK_SLUMBER_MS 80.0
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
    double startMs; /* the replay's start on the CPU's clock (RdPerfRecord.startMs), 0 not known */
    double displayHz; /* the display's rate as the record popped, 0 not known */
} StallBlockRec;

typedef struct StallBlock {
    /* the block */
    unsigned presents, stalls, restarts;
    double stallMs[STALL_BLOCK_LIST], stallGpuMs[STALL_BLOCK_LIST], stallGapMs[STALL_BLOCK_LIST];
    /* per listed stall: 1 the gap measured, 2 the clock restarted, 0 not measured */
    int stallGap[STALL_BLOCK_LIST];
    int stallGpu[STALL_BLOCK_LIST];   /* stallGpuMs valid */
    double stallHz[STALL_BLOCK_LIST]; /* the display's rate at the stall, 0 not known */
    double stallSinceMs
        [STALL_BLOCK_LIST]; /* the replay before began this long before, < 0 not known */
    unsigned spanN;         /* timed replays that did not stall */
    double spanSum, spanMin, spanMax;
    double lastStartMs; /* the record before's startMs (0 none) */
    /* the run */
    unsigned runStalls, runBlocks;
    unsigned runRestartStalls;        /* stalls whose replay saw the GPU clock restart */
    unsigned runLongGap, runShortGap; /* stalls after a CPU-side gap >= / < the idle timeout */
    unsigned runHeldN;                /* stalls with a GPU span: held time known */
    double runHeldMin, runHeldMax;
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
   known: the block's time is counted at it, each stall at its own rate)
   and starts again */
int stall_block_end(StallBlock *b, double blockSeconds, double displayHz,
                    const StallBlockClock *clock, char *buf, size_t size);
/* the run's line at exit (without "window: "); "" (0) when no block
   closed */
int stall_block_run_text(const StallBlock *b, char *buf, size_t size);

#endif /* ICO_PLATFORM_PRESENT_STALL_BLOCK_H */
