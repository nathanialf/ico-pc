/* rd_rebase.c: the alpha from the hand-over (rd_rebase.h).
 *
 * Bounds, from the code, with the measured ask as raw: the content's
 * moment is the hand-over less the drain's mean before this present
 * (ask + d - mean), the shift held to a quarter of a tick; within one frame
 * the value never goes back (the hold).  Between two presents the moment
 * moves by the hand-overs' interval less the mean's change, which is at
 * most (the largest drain - the smallest) / 16: the content follows real
 * time picture to picture, not a smoothed estimate of it.  The window asks
 * a frame's presents only between its close (never before the deadline
 * tickAt) and the next closing step's deadline (tickAt + tick), so the
 * measured ask is in [0, 1) of the tick and only the shift can reach a
 * clamp: below 0 by at most the mean drain, above 0.999 by at most the
 * drain less the mean. */
#include "rd_rebase.h"

#include <math.h>

double rd_rebase_raw(RdRebase *s, double raw, double tickMs, double drainMs, uint32_t frame,
                     double *shiftMs, bool *held)
{
    *shiftMs = 0.0;
    *held = false;
    if (isnan(raw) || !(tickMs > 0.0) || !(drainMs >= 0.0)) {
        s->have = false;
        return raw;
    }
    const double d = drainMs > tickMs ? tickMs : drainMs;
    const double mean = s->n != 0 ? s->meanMs : d;
    const double cap = RD_REBASE_SHIFT_TICKS * tickMs;
    double shift = d - mean;
    shift = shift > cap ? cap : (shift < -cap ? -cap : shift);
    s->meanMs = mean + (d - mean) / RD_REBASE_MEAN_DIV;
    s->n++;
    double out = raw + shift / tickMs;
    if (s->have && s->frame == frame && out < s->lastRaw) {
        out = s->lastRaw;
        *held = true;
    }
    s->have = true;
    s->frame = frame;
    s->lastRaw = out;
    *shiftMs = shift;
    return out;
}

float rd_rebase_alpha(double raw)
{
    if (isnan(raw)) {
        return 0.0f;
    }
    return (float)(raw < 0.0 ? 0.0 : (raw > 0.999 ? 0.999 : raw));
}
