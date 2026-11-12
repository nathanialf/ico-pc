/* rd_rebase.h: the alpha re-based to the hand-over (rd_rebase.c;
 * docs/port/DISPLAY.md "The alpha").
 *
 * On the timer path the window takes the alpha from the present clock at
 * the ask, and rd_present_post then waits for the picture before (the
 * drain) before it hands this one over: about 1.6 ms before a tick's first
 * picture and 7 ms before a further one on the owner's 120 Hz phone.  The
 * picture's work starts at the hand-over, so its content is moved by the
 * drain's difference from its running mean: content = the clock's moment +
 * (drain - mean drain).  The mean alpha stays where the clock puts it; each
 * picture's content keeps a steady distance from the moment its work
 * starts.  The clock itself (its step, resets, holds) is untouched: the
 * re-base is applied after it, to the value handed over. */
#ifndef ICO_RD_REBASE_H
#define ICO_RD_REBASE_H

#include <stdbool.h>
#include <stdint.h>

/* the shift is held to a quarter of a tick each way (a drain of a whole
 * stall is not a cadence) and a drain over a tick is taken as a tick */
#define RD_REBASE_SHIFT_TICKS 0.25
/* the running mean takes a sixteenth of the difference a present, as the
 * present clock's step does */
#define RD_REBASE_MEAN_DIV 16.0

typedef struct RdRebase {
    double meanMs;  /* the drain's running mean */
    uint32_t n;     /* drains folded */
    bool have;      /* a re-based present of frame `frame` came before */
    uint32_t frame; /* the frame pair of the last re-based present */
    double lastRaw; /* its re-based value */
} RdRebase;

/* raw: the present clock's value before the clamp ((clock - tickAt) /
 * tick, NAN when the clock did not time this present); tickMs the tick;
 * drainMs the wait before the hand-over; frame the newer frame's number
 * (the pair: tickAt changes when it does).  Returns the re-based value,
 * raw + shift / tick, never below the last re-based value of the same
 * frame (*held then); *shiftMs the shift applied.  NAN raw, no tick or a
 * negative drain: raw unchanged, nothing folded, the pair forgotten. */
double rd_rebase_raw(RdRebase *s, double raw, double tickMs, double drainMs, uint32_t frame,
                     double *shiftMs, bool *held);
/* the alpha of a re-based value: clamped to [0, 0.999] as the clock's */
float rd_rebase_alpha(double raw);

/* the present clock's lead over the time it was asked at (clock - now, ms)
 * at its last timed call (rd_present_clock.c; the poster's thread) */
double rd__present_clock_lead_ms(void);

#endif
