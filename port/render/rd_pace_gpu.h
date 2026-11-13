/* rd_pace_gpu.h: each in-between picture drawn for the moment its GPU work
 * is predicted to end (rd_pace_gpu.c; docs/port/DISPLAY.md "The alpha").
 *
 * With pictures as made (vsync off, uncapped, the render thread on: rd.h
 * rd_present_as_made) the handed-over picture's moment is its hand-over
 * less the drain's running mean (rd_rebase.h).  A picture reaches the
 * screen after its GPU work, which starts at the later of its first submit
 * and the end of the picture before it on the GPU.  Where the hand-overs are
 * pinned to the game's step grid (two pictures a tick, the first after the
 * closing step, the second after the half-tick deadline) the GPU's queue
 * spaces them evenly and the hand-overs' uneven spacing (0.3 then 0.7 of a
 * tick on the owner's 120 Hz phone in the cage) is drawn into the content.
 *
 * The model, from what the port already measures for each picture (its
 * GPU span, its first submit and the GPU's end where the slot wait blocked,
 * rd_perf.c rd__perf_collect_gpu; no new wait, no clock read):
 *
 *   G   the median of the last RD_PACE_GPU_MED measured GPU spans (a
 *       pipeline-compile replay or a stall is an outlier the median skips);
 *   D   the median of the last RD_PACE_GPU_MED hand-over to first submit
 *       times (the render thread's record and the submit thread's turn);
 *   E   the predicted end of each picture: start = max(hand + D, E of the
 *       picture before), E = start + G; a picture whose end was measured is
 *       re-anchored there (its end where exact; max(first submit, E before)
 *       + its span, under its bound, otherwise) and the pictures after it
 *       predicted again;
 *   q   the queue's wait, start - (hand + D) >= 0, and q's running mean
 *       (a sixteenth of the difference a picture, as the drain's).
 *
 * The moment drawn is the predicted end less the mean latency, which is the
 * hand-over plus (q - mean q): with an empty queue (q 0 at every picture)
 * the shift is exactly 0 and the moment is today's.  The shift is held
 * within the measured spread of the hand-over to the GPU's end (the larger
 * of 90th - mean and mean - 10th over the last RD_PACE_GPU_LAT pictures'
 * measured ends: the stamped end where the slot wait blocked, else the
 * first submit or the picture before's end, whichever is later, plus the
 * span, under the bound where there is one; every picture, as the stamped
 * ones alone are the pictures the render thread waited for and miss those
 * the GPU was ahead of), so a misprediction cannot move a moment further
 * than the real latencies themselves differ, and the moments' step against
 * the hand-overs' gap is
 * held within [RD_PACE_GPU_RATIO_LO, RD_PACE_GPU_RATIO_HI] (the evenness
 * line's own "step over the real gap" range): never back, never a repeat.
 * With no further picture among the last RD_PACE_GPU_FURTHER_SPAN (one
 * picture a tick) the shift is 0: the moments are today's to the bit.
 *
 * Nothing here moves, delays, drops or adds a picture: it is arithmetic on
 * the value handed over (rd_thread.c presentRebase). */
#ifndef ICO_RD_PACE_GPU_H
#define ICO_RD_PACE_GPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the pictures kept for re-anchoring: a picture's end is measured at its
   slot's recycle, two pictures later with RHI_FRAMES_IN_FLIGHT 2 */
#define RD_PACE_GPU_RING 8
/* the median windows of the GPU span and the hand-over to the first submit
   (odd: the median is one sample; up to 7 outliers in a row skipped) */
#define RD_PACE_GPU_MED 15
/* the window of measured hand-over to GPU end times that sets the bound
   (about half a second at 60 pictures a second), and the samples it needs
   before the model places a picture (the 10th and 90th are then distinct
   ranks) */
#define RD_PACE_GPU_LAT 32
#define RD_PACE_GPU_LAT_MIN 10
/* q's running mean: a sixteenth of the difference a picture (rd_rebase.h
   RD_REBASE_MEAN_DIV) */
#define RD_PACE_GPU_MEAN_DIV 16.0
/* the pictures looked back over for a further one (one a tick: none) */
#define RD_PACE_GPU_FURTHER_SPAN 16
/* the content chain's restarts (rd_even.h RD_EVEN_FRAME_JUMP_MAX, the
   content note's second): a frame number back or on by more than 8, or a
   hand-over gap not positive or over a second, restarts the model (a
   stage load, a pause, a stall): every window and the queue emptied */
#define RD_PACE_GPU_FRAME_JUMP 8
#define RD_PACE_GPU_GAP_RESET_MS 1000.0
/* the moments' step against the hand-overs' gap (rd_even.h
   RD_EVEN_RATIO_LO, RD_EVEN_RATIO_HI) */
#define RD_PACE_GPU_RATIO_LO 0.5
#define RD_PACE_GPU_RATIO_HI 2.0
/* pictures handed over as made with no measurement back before the
   ledger says the backend has none (OpenGL, Direct3D 11, timestamps off) */
#define RD_PACE_GPU_NO_CLOCK 64

/* one picture's measurement (the replaying thread, rd__perf_collect_gpu),
   on rd's monotonic ms clock */
typedef struct RdPaceGpuSample {
    double handMs;   /* its hand-over (RdPerfRecord.evenHandMs, > 0) */
    double submitMs; /* its first submit (RhiTimestampClock.firstSubmitNs; 0 unknown) */
    double spanMs;   /* its GPU span, first stamp to last (0 unknown) */
    double endMs;    /* the GPU's end (0 none: the work had finished before the wait) */
    bool exact;      /* endMs the end (the wait blocked 0.05 ms or more), else a bound */
} RdPaceGpuSample;

typedef struct RdPaceGpuPic {
    double hand, predEnd, end; /* predEnd as predicted at the hand-over; end the best now */
    bool measured;
} RdPaceGpuPic;

/* the model (one thread: the poster's) */
typedef struct RdPaceGpu {
    double span[RD_PACE_GPU_MED], lead[RD_PACE_GPU_MED], lat[RD_PACE_GPU_LAT];
    uint32_t spanN, spanAt, leadN, leadAt, latN, latAt;
    RdPaceGpuPic ring[RD_PACE_GPU_RING];
    uint32_t ringN, ringAt; /* ringAt: the next write */
    double meanQ;
    uint32_t qN;
    uint32_t furtherBits; /* the last RD_PACE_GPU_FURTHER_SPAN pictures: 1 further */
    bool have;            /* a picture placed before in this chain */
    uint32_t frame;
    double hand, content; /* the last picture's hand-over and moment (hand + shift) */
} RdPaceGpu;

/* how a picture's moment was decided */
enum {
    RD_PACE_GPU_PLACED = 0, /* by the model */
    RD_PACE_GPU_ONE_A_TICK, /* no further picture lately: 0 */
    RD_PACE_GPU_NOT_READY   /* its windows not full yet (after a restart): 0 */
};

typedef struct RdPaceGpuPlace {
    double shiftMs;            /* applied to the hand-over (0 unless placed or held to the ratio) */
    double rawMs;              /* q - mean q before the bound */
    double boundMs;            /* the bound in force (0 when not placed) */
    double queueMs;            /* q */
    double predEndMs;          /* E (0 when G or D is not known yet) */
    double latP10Ms, latP90Ms; /* the measured spread the bound came from */
    int how;                   /* RD_PACE_GPU_* */
    bool restarted;            /* the chain restarted at this picture */
    bool atBound;              /* |raw| over the bound */
    bool limited;              /* held to the ratio range */
} RdPaceGpuPlace;

/* the measurement's verdict (rd_pace_gpu_model_note) */
typedef struct RdPaceGpuNoted {
    bool matched;  /* a picture of the ring */
    bool errValid; /* exact end of a picture predicted with G and D: errMs set */
    double errMs;  /* predicted end - measured end */
} RdPaceGpuNoted;

void rd_pace_gpu_model_reset(RdPaceGpu *m);
/* a measurement into the windows and the ring (out may be NULL) */
void rd_pace_gpu_model_note(RdPaceGpu *m, const RdPaceGpuSample *s, RdPaceGpuNoted *out);
/* the picture handed over at handMs of frame `frame` (the newer frame's
   number, rd_rebase_raw's): its shift (out->shiftMs) */
void rd_pace_gpu_model_place(RdPaceGpu *m, double handMs, uint32_t frame, RdPaceGpuPlace *out);
/* the p-th share's sample of n (rank ceil(p * n), at least 1; n at most
   RD_PACE_GPU_LAT; 0 for none) and the median (the share 0.5: the middle
   sample of an odd n) */
double rd_pace_gpu_share(const double *v, uint32_t n, double p);
double rd_pace_gpu_median(const double *v, uint32_t n);

/* ------------------------------------------------------- the renderer's */

/* the replaying thread: a handed-over picture's measurement (dropped, and
   counted, when the poster has not taken the 32 before it) */
void rd_pace_gpu_measure(const RdPaceGpuSample *s);
/* the poster's thread, pictures as made, the clock timed this present:
   the shift in ms for the picture handed over at handMs of frame `frame`
   (the measurements taken first; the ledger's row decided; the 10 s sums) */
double rd_pace_gpu_shift(double handMs, uint32_t frame);
/* the poster's thread, the pictures paced (rd_present_as_made(false), at
   every step): the model emptied, the ledger's row not applicable.  A
   present as made that the clock did not time (or ICO_RD_S2_LEGACY=1) is
   not re-based and leaves the model as it is */
void rd_pace_gpu_paced(void);

/* the 10 s sums, taken (and emptied) by rd_pace_gpu_take */
#define RD_PACE_GPU_SHIFT_BIN_MS 0.5
#define RD_PACE_GPU_SHIFT_BINS 64
#define RD_PACE_GPU_SHIFT_FROM_MS (-16.0)

typedef struct RdPaceGpuWindow {
    uint64_t placed, oneATick, notReady, restarts, atBound, limited, dropped;
    uint32_t shift[RD_PACE_GPU_SHIFT_BINS]; /* the placed pictures' shifts */
    uint64_t queueUs;                       /* the placed pictures' q, summed */
    uint64_t errN;
    int64_t errSumUs;
    uint64_t errSum2;            /* us^2 */
    uint32_t boundUs;            /* the last placed picture's bound */
    uint32_t latP10Us, latP90Us; /* and its spread */
} RdPaceGpuWindow;

/* into the sums (rd_pace_gpu_live.c, the poster's thread): a picture's
   placing, a prediction's error against an exact end, a measurement
   dropped */
void rd_pace_gpu_tally(const RdPaceGpuPlace *p);
void rd_pace_gpu_tally_err(double errMs);
void rd_pace_gpu_tally_drop(void);
void rd_pace_gpu_take(RdPaceGpuWindow *w);
/* "; the GPU's predicted end: ..." appended at out (the evenness line's
   last clause); false and "" when no picture was handed over as made */
bool rd_pace_gpu_format(const RdPaceGpuWindow *w, char *out, size_t size);
/* the start line's text ("rd: in-between pictures for the GPU's predicted
   end: ..."), on: the row's state is on */
size_t rd_pace_gpu_start_text(bool on, const char *why, char *out, size_t size);

#endif
