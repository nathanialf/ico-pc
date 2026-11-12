/* rd_even.h: the pictures' evenness, one 10 s line (rd_even.c;
 * docs/port/PLATFORM_DIAGNOSTICS.md "The evenness line").
 *
 * What the owner sees as "not as smooth as the number suggests" is either
 * when the pictures reach the glass (a 120 Hz panel holds each picture for
 * a whole number of refreshes, so an even content step shows as a speed
 * that changes picture to picture) or how far the content moves between
 * two pictures (the present clock's resets and clamps).  The line measures
 * both from stamps the port already takes:
 *
 *   - each present's record (rd_perf.c): its frame and alpha, so its
 *     content in ticks (frame + alpha); the GPU's end on the CPU's clock
 *     (the frame slot's fence wait two pictures later, rd_perf_clock.c: its
 *     end when the wait blocked 0.05 ms or more, exact; its start when it
 *     blocked less, a bound; none when the work had finished before the
 *     wait, the GPU ahead of the CPU); the alpha's
 *     sample and the hand-over (the drain before it, rd_thread.c
 *     rd_present_post), stamped with the clock reads the drain already
 *     makes;
 *   - the present clock's own events (rd_present_clock.c, the game
 *     thread): its value before the clamp, its resets by direction.
 *
 * No clock is read for it.  The render thread's part is about a dozen
 * relaxed atomic adds a picture into the 10 s sums; the clock's part one to
 * three a present.  The window's 10 s path takes and formats the sums
 * (rd.h rd_even_line, window_host.c perf_log). */
#ifndef ICO_RD_EVEN_H
#define ICO_RD_EVEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the GPU ends' gaps in bins of a quarter of a ms up to 64 ms (the last bin
 * every longer one): the refresh is the window's, known when the line is
 * formatted, so the gaps are counted finely and put on the refresh grid
 * there by each bin's middle */
#define RD_EVEN_GAP_BIN_MS 0.25
#define RD_EVEN_GAP_BINS 256
/* the alpha's sample to the GPU's end in bins of a ms up to 128 ms */
#define RD_EVEN_LAT_BIN_MS 1.0
#define RD_EVEN_LAT_BINS 128
/* a frame number that went back or moved on by more than this between two
 * presents restarts the chain (a load, a new run): no step, no gap */
#define RD_EVEN_FRAME_JUMP_MAX 8
/* the steps and sizes are kept in ten-thousandths of a tick */
#define RD_EVEN_UNITS 10000.0

/* one present's stamps when its GPU record is collected (rd_perf.c
 * rd__perf_collect_gpu, the thread that replays) */
typedef struct RdEvenPic {
    uint32_t frame;  /* the newer frame's number (RdPerfRecord.frame) */
    float alpha;     /* rd_present's alpha (RdPerfRecord.alpha, >= 0) */
    double askMs;    /* the alpha's sample: the drain's start (0: not handed over) */
    double handMs;   /* the hand-over: the drain's end (0: not handed over) */
    double gpuEndMs; /* the GPU's end on rd's monotonic ms clock (0: none) */
    bool gpuExact;   /* the slot wait blocked: gpuEndMs is the end, else a bound */
} RdEvenPic;

void rd_even_picture(const RdEvenPic *p);

/* the present clock's note (rd_present_clock.c rd_present_clock_alpha, a
 * timed call): raw its value before the clamp to [0, 0.999]; reset 0 none,
 * 1 a reset that moved the clock forward (or kept it), -1 one that would
 * have moved it back; heldMs > 0 when the clock was held at its last value
 * within a frame pair instead of going back that much (0 otherwise) */
void rd_even_clock_note(double raw, int reset, double heldMs);

/* the re-base to the hand-over (rd_thread.c presentRebase, rd_rebase.h):
 * the content's moment to the hand-over at the sample (beforeMs, the drain
 * less the clock's lead) and after the re-base (afterMs), the shift, and
 * whether the value was held at the frame's last one */
#define RD_EVEN_REB_BIN_MS 0.5
#define RD_EVEN_REB_BINS 128
#define RD_EVEN_REB_FROM_MS (-16.0) /* the first bin's start; the ends catch the rest */
void rd_even_rebase_note(double beforeMs, double afterMs, double shiftMs, bool held);

/* the hand-over's stamps for the picture about to be made on this thread
 * (rd_thread.c presentJob before rd_present, 0 and 0 after it), taken by
 * the replay's record (rd_perf.c rd__perf_begin: 0 and 0 when none) */
void rd_even_hand_note(double askMs, double handMs);
void rd_even_hand_take(double *askMs, double *handMs);

/* the 10 s sums, taken (and emptied) by rd_even_line */
typedef struct RdEvenWindow {
    uint64_t pictures;
    /* the GPU ends' gaps with both ends exact, by bin, with the content's
     * steps over them (sum and sum of squares, in RD_EVEN_UNITS) */
    uint32_t gap[RD_EVEN_GAP_BINS];
    int64_t gapStep[RD_EVEN_GAP_BINS];
    uint64_t gapStep2[RD_EVEN_GAP_BINS];
    uint64_t bound; /* gaps with a bound at one end (or not increasing) */
    uint64_t noEnd; /* gaps with no GPU end at one end */
    /* every step between two presents in a chain */
    uint64_t steps, stepSum2, back, breaks;
    int64_t stepSum;
    uint32_t stepMax, backMax; /* RD_EVEN_UNITS */
    /* the alpha's sample to the GPU's end (exact ends), the hand-over to
     * it, the drain */
    uint32_t lat[RD_EVEN_LAT_BINS], latHand[RD_EVEN_LAT_BINS];
    uint64_t latN, latHandN, drainUs, drainN;
    /* the present clock's */
    uint64_t samples, clampLo, clampHi, resetFwd, resetBack, held;
    uint32_t clampLoMax, clampHiMax; /* RD_EVEN_UNITS */
    uint32_t heldMaxUs;              /* the largest hold, us */
    /* the re-base's */
    uint32_t rebBefore[RD_EVEN_REB_BINS], rebAfter[RD_EVEN_REB_BINS], rebShift[RD_EVEN_REB_BINS];
    uint64_t rebN, rebHeld;
} RdEvenWindow;

void rd_even_take(RdEvenWindow *w);
/* "evenness in 10 s: ..." (no "window: "); refreshMs the display's refresh
 * (0 unknown: the gaps are not put on a grid); false with no picture and
 * no clock sample */
bool rd_even_format(const RdEvenWindow *w, double refreshMs, char *out, size_t size);
/* the window's 10 s line (declared in rd.h too: window_host.c perf_log),
 * written to stderr with "window: " before it */
void rd_even_line(double refreshMs);

#endif
