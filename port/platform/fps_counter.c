/*
 * port/platform/fps_counter.c
 *
 * The Frame rate counter's number (fps_counter.h).
 */
#include "fps_counter.h"

#include "gpu_hang.h"

#define SECOND_NS 1000000000ull

void fps_present(FpsCounter *c)
{
    c->presents++;
}

void fps_gap(FpsCounter *c)
{
    __atomic_store_n(&c->gap, 1, __ATOMIC_RELEASE);
}

/* the second starts again at now_ns from total, the value kept */
static void restart(FpsCounter *c, uint64_t now_ns, unsigned total)
{
    c->markAt = now_ns;
    c->mark = total;
}

void fps_tick(FpsCounter *c, uint64_t now_ns, int interp, uint32_t replayCount)
{
    /* the 10 s line zeroes the renderer's count: a smaller one is a zeroing,
       and what it holds is what came since */
    c->replays += replayCount >= c->lastCount ? replayCount - c->lastCount : replayCount;
    c->lastCount = replayCount;
    interp = interp != 0;
    const unsigned total = interp ? c->presents : c->replays;
    const uint64_t gapNs =
        c->gapNs != 0 ? c->gapNs : (uint64_t)(ICO_GPU_HANG_MIN_S * (double)SECOND_NS);
    const uint64_t lastTick = c->tickAt;
    const int noted = __atomic_exchange_n(&c->gap, 0, __ATOMIC_ACQ_REL) != 0;

    c->tickAt = now_ns;
    if (c->markAt == 0 || interp != c->interp) {
        restart(c, now_ns, total);
        c->interp = interp;
        return;
    }
    if (noted || (lastTick != 0 && now_ns > lastTick && now_ns - lastTick >= gapNs)) {
        /* the span held time the game was not presenting (noted by the
           window, or a gap between two ticks no wait for the GPU the run
           lives through can make): no value from it */
        restart(c, now_ns, total);
        c->restarts++;
        return;
    }
    if (now_ns < c->markAt || now_ns - c->markAt < SECOND_NS) {
        return;
    }
    const uint64_t dt = now_ns - c->markAt;

    /* total - mark presents over dt ns, rounded to the nearest */
    c->value = (int)(((uint64_t)(total - c->mark) * SECOND_NS + dt / 2) / dt);
    restart(c, now_ns, total);
}

int fps_value(const FpsCounter *c)
{
    return c->value;
}

unsigned fps_restarts(const FpsCounter *c)
{
    return c->restarts;
}
