/*
 * port/platform/fps_counter.c
 *
 * The Frame rate counter's number (fps_counter.h).
 */
#include "fps_counter.h"

#define SECOND_NS 1000000000ull

void fps_present(FpsCounter *c)
{
    c->presents++;
}

void fps_tick(FpsCounter *c, uint64_t now_ns, int interp, uint32_t replayCount)
{
    /* the 10 s line zeroes the renderer's count: a smaller one is a zeroing,
       and what it holds is what came since */
    c->replays += replayCount >= c->lastCount ? replayCount - c->lastCount : replayCount;
    c->lastCount = replayCount;
    interp = interp != 0;
    const unsigned total = interp ? c->presents : c->replays;

    if (c->markAt == 0 || interp != c->interp) {
        c->markAt = now_ns;
        c->mark = total;
        c->interp = interp;
        return;
    }
    if (now_ns < c->markAt || now_ns - c->markAt < SECOND_NS) {
        return;
    }
    const uint64_t dt = now_ns - c->markAt;

    /* total - mark presents over dt ns, rounded to the nearest */
    c->value = (int)(((uint64_t)(total - c->mark) * SECOND_NS + dt / 2) / dt);
    c->markAt = now_ns;
    c->mark = total;
}

int fps_value(const FpsCounter *c)
{
    return c->value;
}
