/* rd_rebase.c: the alpha re-based to the hand-over (rd_rebase.h).
 *
 * Bounds, from the code: |shift| <= tick / 4; within one frame the value
 * never goes back (the hold); the present clock's moment stays within 7/16
 * of a tick of the time it was asked at (rd_present_clock.c), so the
 * re-based moment stays within 7/16 + 1/4 = 11/16 of a tick of it, and
 * over any span the content's speed is real time's within 11/8 of a tick
 * over the span.  With the drain d and the clock's lead L (clock - ask),
 * the content's moment is ask + L + d - mean, the hand-over ask + d: the
 * content's distance to the hand-over is mean - L, which the drain no
 * longer moves (before: d - L). */
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
