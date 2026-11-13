/* rd_as_made_timer.h: an own timer for the moment of each picture made as
 * fast as the GPU allows (rd_as_made_timer.c; docs/port/DISPLAY.md "The
 * alpha").  A variant: off by default (RD_AS_MADE_TIMER_DEFAULT 0, the
 * tests' switch ICO_RD_AS_MADE_TIMER=1), computed beside the drawn rule at
 * every picture as made so the on-glass line can compare it.
 *
 * Why not the present clock: the present clock (rd_present_clock.c) steps
 * each present by the running mean of the present intervals and pulls an
 * eighth of the error towards the measured time, resetting when the error
 * is over half a tick.  With pictures as made the step rule holds a further
 * picture now and then, so an interval is now and then a whole picture's
 * slot longer than the mean: an eighth of it is taken, the rest stays as
 * a lag, the lags add up, the error passes half a tick and the clock
 * resets forward (up to a fifth of the presents on the owner's phone,
 * v0.11.5 rc18-rc19), and a lagging clock at a tick's first picture is
 * before the frame's close: a clamp at 0 (up to a quarter of the presents).
 *
 * The timer: each picture's moment c = the moment before + I, where I is
 * the mean hand-over interval over the last RD_AMT_WINDOW_TICKS ticks (the
 * hand-overs' pattern repeats every tick, pinned to the step grid, so a
 * whole-tick window removes it), corrected towards the hand-over rule's
 * moment m (the hand-over less the mean drain, rd_rebase.h): the error
 * e = m - (c + I) beyond half an interval is taken as whole slots (k = the
 * nearest whole number of intervals: a held picture's slot, which the
 * present clock did not have) and the rest pulled RD_AMT_GAIN of the way.
 * An error over half a tick (a stall, a resync) puts the timer on m.  The
 * step is at least RD_AMT_MIN_STEP of the interval: never back, never the
 * same moment. */
#ifndef ICO_RD_AS_MADE_TIMER_H
#define ICO_RD_AS_MADE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#ifndef RD_AS_MADE_TIMER_DEFAULT
#define RD_AS_MADE_TIMER_DEFAULT 0
#endif

#define RD_AMT_WINDOW_TICKS 2.0
#define RD_AMT_GAIN 0.25
#define RD_AMT_MIN_STEP 0.25
#define RD_AMT_HIST 64
/* the lead: the largest of the last 16 fresh pictures' distances below
   the frame's close, the timer without a lead (0 when they all had room) */
#define RD_AMT_NEED 16
/* the chain's restarts, as the evenness line's (rd_even.h) */
#define RD_AMT_FRAME_JUMP 8
#define RD_AMT_GAP_RESET_MS 1000.0

typedef struct RdAsMadeTimer {
    bool have;
    uint32_t frame;
    double c;                 /* the timer's moment (ms, the target's clock) */
    double lead;              /* the lead added to the target: its fresh pictures' need (below) */
    double need[RD_AMT_NEED]; /* the last fresh pictures' distance below the close */
    uint32_t needN, needAt;
    double hand; /* the last hand-over */
    double hist[RD_AMT_HIST];
    uint32_t histN, histAt;
} RdAsMadeTimer;

typedef struct RdAsMadeTimerOut {
    double momentMs;   /* the timer's moment */
    double intervalMs; /* I */
    double errorMs;    /* m - the moment (after the step) */
    uint32_t slots;    /* k: whole intervals taken */
    bool reset;        /* put on m (a restart or an error over half a tick) */
    bool window;       /* held inside the frame pair's tick (no clamp of the alpha) */
    double leadMs;     /* the lead in force */
} RdAsMadeTimerOut;

void rd_as_made_timer_reset(RdAsMadeTimer *t);
/* targetMs the hand-over rule's moment, handMs the hand-over, frame the
   newer frame's number, tickAtMs the frame's close (the moment of alpha 0),
   tickMs the tick */
void rd_as_made_timer_step(RdAsMadeTimer *t, double targetMs, double handMs, uint32_t frame,
                           double tickAtMs, double tickMs, RdAsMadeTimerOut *out);

/* the switch: RD_AS_MADE_TIMER_DEFAULT, ICO_RD_AS_MADE_TIMER=0/1 over it
   (read once) */
bool rd_as_made_timer_on(void);

#endif
