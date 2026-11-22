/*
 * port/platform/slow_arm.c
 *
 * Which replay an armed slow-scene dump takes (slow_arm.h).
 */
#include "slow_arm.h"
#include <stdio.h>
#include <string.h>

static char s_line[512];
static unsigned s_gone, s_dropped;

static void put(void)
{
    fprintf(stderr, "window: slow scene: %s\n", s_line);
}

void slow_arm_begin(SlowArmPick *a)
{
    memset(a, 0, sizeof(*a));
}

SlowArmSee slow_arm_see(SlowArmPick *a, uint32_t frame, double work, int picked, int held,
                        int bestHeld)
{
    a->looked++;
    if (work > a->heaviestWork) {
        a->heaviestWork = work;
    }
    if (picked && held) {
        return SLOW_ARM_TAKE;
    }
    if (picked) {
        const unsigned n = ++s_gone;

        a->gone++;
        if (n <= 8u || n % 100u == 0u) {
            snprintf(s_line, sizeof(s_line),
                     "frame %u had left the renderer's ring before it was looked at: the next "
                     "replay at or above the mean is taken (%u times so far)",
                     (unsigned)frame, n);
            put();
        }
        return SLOW_ARM_GONE;
    }
    if (held && (a->bestFrame == 0 || !bestHeld || work > a->bestWork)) {
        a->bestFrame = frame;
        a->bestWork = work;
    }
    return SLOW_ARM_WAIT;
}

uint32_t slow_arm_end(const SlowArmPick *a, int bestHeld, double meanWork, double *work)
{
    char gone[64] = "";

    *work = 0.0;
    if (a->gone > 0) {
        snprintf(gone, sizeof(gone), "; %u at or above it had left the ring", a->gone);
    }
    if (a->bestFrame != 0 && bestHeld) {
        *work = a->bestWork;
        snprintf(s_line, sizeof(s_line),
                 "no replay after the slow block reached its mean vertex work (%u looked at, the "
                 "heaviest %.1f thousand against the mean %.1f thousand%s): frame %u (%.1f "
                 "thousand), the heaviest seen that the renderer still holds, is taken at the "
                 "block's end",
                 a->looked, a->heaviestWork / 1000.0, meanWork / 1000.0, gone,
                 (unsigned)a->bestFrame, a->bestWork / 1000.0);
        put();
        return a->bestFrame;
    }
    const unsigned n = ++s_dropped;

    if (a->looked == 0) {
        snprintf(s_line, sizeof(s_line),
                 "the armed dump is dropped at the block's end: no replay finished while it was "
                 "armed (%u dropped so far)",
                 n);
    } else {
        snprintf(s_line, sizeof(s_line),
                 "the armed dump is dropped at the block's end: no replay reached the slow "
                 "block's mean vertex work (%u looked at, the heaviest %.1f thousand against the "
                 "mean %.1f thousand%s) and none seen is still held (%u dropped so far)",
                 a->looked, a->heaviestWork / 1000.0, meanWork / 1000.0, gone, n);
    }
    if (n <= 8u || n % 100u == 0u) {
        put();
    }
    return 0;
}

const char *slow_arm_last_line(void)
{
    return s_line;
}

unsigned slow_arm_dropped(void)
{
    return s_dropped;
}
