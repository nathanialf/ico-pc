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
 * Its range is a quarter of the nominal interval (the frame rate cap's gap,
 * a refresh under FIFO) to four times it, or without a nominal (Uncapped)
 * a 64th of a tick to a tick.  A present less than a quarter of the
 * nominal after the one before is a doubled present (the compositor's, a
 * late present right after a blocked one), not a cadence: it moves neither
 * the mean nor, on a reset, the step, and it moves the clock by its own
 * interval, not a step.  A reset while presents go on (an
 * error over half a tick, an interval over four steps) takes the step from
 * that interval when it is within the range; a longer one is a pause and
 * keeps the step.
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

#include "rd_internal.h"

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
        /* a step learned under another nominal (the frame rate changed) */
        step = step < lo ? lo : (step > hi ? hi : step);
    }
    const double since = nowMs - c->lastMs;
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
        if (c->presents != 0 && since > 0.0 && !doubled && since <= hi) {
            /* the interval, while presents go on: within the range (a
             * doubled present is below it, a pause above) */
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
    c->presents++;
    c->lastMs = nowMs;
    s_clockTickMs = tickMs; /* Motion blur: Smooth's presents a tick */
    s_clockStepMs = c->stepMs;
    s_clockSinceMs = since; /* and its weight for this present */
    s_clockSinceTickMs = tickMs;
    double a = (c->clockMs - tickAtMs) / tickMs;
    s_clockRaw = a; /* before the clamp: the alpha histogram's "clamped from below" */
    a = a < 0.0 ? 0.0 : (a > 0.999 ? 0.999 : a);
    return (float)a;
}
