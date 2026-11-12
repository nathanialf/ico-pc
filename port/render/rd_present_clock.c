/*
 * port/render/rd_present_clock.c
 *
 * The present clock the window takes alpha from on the timer path (rd.h
 * RdPresentClock; docs/port/DISPLAY.md, "The alpha"), what it notes for
 * rd_present (rd_interp.c) and Motion blur: Smooth's presents a tick from
 * it (rd_blur_smooth.c; docs/port/RENDER_POST.md, "The motion blur under
 * Smooth").
 *
 * The clock's step is the running mean of the intervals between presents.
 * Within one frame pair (the same tickAt and tick) the clock never goes
 * back (below, at the hold).
 * It is held above a quarter of the nominal interval (the frame rate cap's
 * gap, a refresh under FIFO), or without a nominal (Uncapped) a 64th of a
 * tick.  A present less than a quarter of the nominal after the one before
 * is a doubled present (the compositor's, a late present right after a
 * blocked one), not a cadence: it moves neither the mean nor, on a reset,
 * the step, and it moves the clock by its own interval, not a step.  A
 * reset while presents go on (an error over half a tick, an interval over
 * four steps) takes the step from that interval when it is at most four
 * steps or four nominals (a tick without a nominal), whichever is more; a
 * longer one is a pause and keeps the step.  So a rate slower than four
 * nominals is learnt one reset at a time (a 120 Hz FIFO at 25 presented
 * fps: one reset at the change, then none).
 *
 * Issue 193 (v0.10.2, an Adreno 740 at Frame rate 60): before v0.10.4 a
 * reset took one doubled present's 0.5 ms as the step, and every later
 * present, 17 to 20 ms apart and so over four steps, reset the clock again
 * without touching the step: it stayed 0.5 ms for the rest of the run.
 * The alpha survived (each reset sets the clock to the measured time), but
 * Smooth's presents a tick read round(33.3 / 0.5) = 66, held to 64, and
 * drew the game's FIX 32 as 125 at 1.7 presents a tick: a trail that kept
 * 96 % of itself a tick instead of 25 %.
 */
#include <math.h>
#include <stdint.h>

#include "rd_even.h"
#include "rd_internal.h"
#include "rd_rebase.h"

/* the step's lower bound without a nominal interval: a tick over this */
#define CLOCK_STEPS_A_TICK_MAX 64.0

/* rd__present_clock_raw: the last rd_present_clock_alpha's value before its
 * clamp, NAN once rd_present has taken it; with it, the time since the
 * present before and the tick it timed against (rd__present_clock_take),
 * 0 when none */
static double s_clockRaw = NAN, s_clockSinceMs, s_clockSinceTickMs;

/* the clock's last tick and step: Motion blur: Smooth's presents a tick on
 * the timer path (rd__present_clock_k) and its block line's step */
static double s_clockTickMs, s_clockStepMs;

/* the clock's resets since rd__present_clock_resets_take (the poster's
 * thread counts, the render thread's block line takes) */
static uint32_t s_clockResets;

/* the clock's lead over the time asked at, its last timed call (the
 * re-base's evenness figures, rd_rebase.h) */
static double s_clockLeadMs;

double rd__present_clock_lead_ms(void)
{
    return s_clockLeadMs;
}

double rd__present_clock_raw(void)
{
    return s_clockRaw;
}

double rd__present_clock_take(double gap[2])
{
    const double r = s_clockRaw;
    if (gap) {
        gap[0] = s_clockSinceMs;
        gap[1] = s_clockSinceTickMs;
    }
    s_clockRaw = NAN;
    s_clockSinceMs = s_clockSinceTickMs = 0.0;
    return r;
}

uint32_t rd__present_clock_k(double raw, uint32_t counted)
{
    if (!isnan(raw) && s_clockTickMs > 0.0 && s_clockStepMs > 0.0) {
        const double k = floor(s_clockTickMs / s_clockStepMs + 0.5);
        return k < 1.0 ? 1u
                       : (k > (double)RD_BLUR_SMOOTH_K_MAX ? RD_BLUR_SMOOTH_K_MAX : (uint32_t)k);
    }
    return counted;
}

double rd__present_clock_step_ms(void)
{
    return s_clockStepMs;
}

uint32_t rd__present_clock_resets_take(void)
{
    return __atomic_exchange_n(&s_clockResets, 0u, __ATOMIC_RELAXED);
}

float rd_present_clock_alpha(RdPresentClock *c, double nowMs, double tickAtMs, double tickMs,
                             double nominalMs)
{
    s_clockRaw = NAN;
    s_clockSinceMs = s_clockSinceTickMs = 0.0;
    if (!(tickMs > 0.0)) {
        return 0.0f;
    }
    const bool nominal = nominalMs > 0.0;
    const double lo = nominal ? nominalMs / 4.0 : tickMs / CLOCK_STEPS_A_TICK_MAX;
    const double hi = nominal ? 4.0 * nominalMs : tickMs;
    double step = c->stepMs > 0.0 ? c->stepMs : (nominal ? nominalMs : 0.0);
    if (nominal) {
        /* a step learned under another nominal (the frame rate changed):
         * only the lower bound, the one a doubled present's step breaks (a
         * cadence slower than four nominals is a real rate, a 120 Hz FIFO
         * phone below 30 presented fps) */
        step = step < lo ? lo : step;
    }
    const double since = nowMs - c->lastMs;
    const double before = c->clockMs;
    const bool first = c->presents == 0;
    /* the same frame pair as the call before (the window passes the same
       tickAt and tick until a frame closes), time not gone back */
    const bool samePair =
        c->paired != 0 && tickAtMs == c->pairAtMs && tickMs == c->pairTickMs && since >= 0.0;
    /* a doubled present: neither the mean's nor the step's */
    const bool doubled = nominal && c->presents != 0 && since >= 0.0 && since < lo;
    bool reset = c->presents == 0 || since < 0.0 || !(step > 0.0) || since > 4.0 * step;
    if (!reset) {
        /* the running mean of the intervals, a sixteenth of the difference a
         * present */
        c->stepMs = doubled ? step : step + (since - step) / 16.0;
        /* a doubled present moves the clock by its own interval, with no
         * pull toward the measured time: a whole step put the clock a step
         * ahead of it, and the next doubled present then reset it backwards
         * (the alpha went back within a tick, up to 0.28 of one at a
         * doubled present every 7th); the pull alone, with the clock a few
         * ms ahead, took a tenth of a ms back.  The next present pulls. */
        const double predicted = c->clockMs + (doubled ? since : c->stepMs);
        const double err = nowMs - predicted;
        if (fabs(err) > 0.5 * tickMs) {
            reset = true;
        } else {
            c->clockMs = doubled ? predicted : predicted + err / 8.0;
        }
    }
    if (reset) {
        c->clockMs = nowMs;
        if (c->presents != 0 && since > 0.0 && !doubled &&
            (!(step > 0.0) || since <= (4.0 * step > hi ? 4.0 * step : hi))) {
            /* the interval, while presents go on: up to four steps or the
             * range's top, whichever is more, so a rate slower than the
             * range is learnt (a doubled present is below it, a pause
             * above); no step yet (Uncapped's first reset): any interval */
            c->stepMs = since < lo ? lo : since;
        } else if (nominal && c->stepMs > 0.0) {
            c->stepMs = step; /* the step held to the range */
        } else if (!(c->stepMs > 0.0)) {
            c->stepMs = nominal ? nominalMs : 0.0;
        }
        c->presents = 0;
        c->resets++;
        __atomic_fetch_add(&s_clockResets, 1u, __ATOMIC_RELAXED);
    }
    /* the evenness line's reset direction, before the hold below */
    const int resetDir = !reset ? 0 : (first || c->clockMs >= before ? 1 : -1);
    /* within a frame pair the content never goes back: a reset (an error
       over half a tick: the measured time that far behind the prediction,
       after a burst of quick presents had pulled the clock ahead) or the
       pull (a clock more than seven steps ahead of the measured time) that
       would put the clock below its last value keeps that value.  The
       picture shows the same moment as the one before instead of an
       earlier one; the next presents' pulls (the measured time is behind
       the clock) and the next frame's reset or pull bring it back to the
       measured time.  The clock's lead over the measured time stays under
       7/16 of a tick as before (a pull leaves at most 7/8 of an error of at
       most half a tick, and a held clock's lead only shrinks), so over any
       span the content's speed is real time's within 7/8 of a tick over the
       span.  No present is moved, held or added: the rate is not touched */
    double heldMs = 0.0;
    if (samePair && c->clockMs < before) {
        heldMs = before - c->clockMs;
        c->clockMs = before;
        c->held++;
    }
    c->paired = 1u;
    c->pairAtMs = tickAtMs;
    c->pairTickMs = tickMs;
    c->presents++;
    c->lastMs = nowMs;
    s_clockTickMs = tickMs; /* Motion blur: Smooth's presents a tick */
    s_clockStepMs = c->stepMs;
    s_clockSinceMs = since; /* and its weight for this present */
    s_clockSinceTickMs = tickMs;
    s_clockLeadMs = c->clockMs - nowMs;
    double a = (c->clockMs - tickAtMs) / tickMs;
    s_clockRaw = a; /* before the clamp: the alpha histogram's "clamped from below" */
    /* the evenness line (rd_even.h): the clamps, the resets (forward unless
       it would have moved a running clock back) and the holds */
    rd_even_clock_note(a, resetDir, heldMs);
    a = a < 0.0 ? 0.0 : (a > 0.999 ? 0.999 : a);
    return (float)a;
}
