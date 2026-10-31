/*
 * port/platform/window_disc.c
 *
 * The window's side of the disc reader (window_disc.h).
 */
#include "window_disc.h"
#include <stdio.h>
#include "host_loop.h"

static struct {
    unsigned vsync; /* the vsync count of the step last seen */
    Uint64 waitNs;  /* that step's wait for the disc */
    unsigned reads, sectors, hits, waitSteps;
    double waitMs, waitMaxMs;   /* the block's */
    const PaceBudget *budgetOf; /* the allowance discExcusedAt counts from */
    unsigned discExcusedAt;
} s_disc;

void window_disc_step(void)
{
    IcoStepProfile p;
    const unsigned v = ico_host_vsync_count();

    if (v == s_disc.vsync) {
        s_disc.waitNs = 0; /* no step since the last pace */
        return;
    }
    s_disc.vsync = v;
    ico_host_step_profile(&p);
    s_disc.waitNs = p.cdWaitMs > 0.0 ? (Uint64)(p.cdWaitMs * 1e6) : 0;
    s_disc.reads += p.cdReads;
    s_disc.sectors += p.cdSectors;
    s_disc.hits += p.cdHits;
    if (p.cdWaitMs > 0.0) {
        s_disc.waitSteps++;
        s_disc.waitMs += p.cdWaitMs;
        s_disc.waitMaxMs = p.cdWaitMs > s_disc.waitMaxMs ? p.cdWaitMs : s_disc.waitMaxMs;
    }
}

Uint64 window_disc_wait_ns(void)
{
    return s_disc.waitNs;
}

void window_disc_block(const PaceBudget *b)
{
    unsigned excused = 0;

    if (b != NULL) {
        /* a reset allowance (a new lock, a frame rate change) counts afresh */
        if (b != s_disc.budgetOf || b->discExcused < s_disc.discExcusedAt) {
            s_disc.budgetOf = b;
            s_disc.discExcusedAt = 0;
        }
        excused = b->discExcused - s_disc.discExcusedAt;
        s_disc.discExcusedAt = b->discExcused;
    }
    if (s_disc.reads > 0) {
        char waited[96];

        if (s_disc.waitSteps > 0) {
            snprintf(waited, sizeof(waited), "waited %.0f ms in %u steps (longest %.0f ms)",
                     s_disc.waitMs, s_disc.waitSteps, s_disc.waitMaxMs);
        } else {
            snprintf(waited, sizeof(waited), "never waited");
        }
        fprintf(stderr,
                "window: disc: %u reads (%u KB), %u from read-ahead; the game thread %s; %u late "
                "steps excused by disc waits\n",
                s_disc.reads, s_disc.sectors * 2u, s_disc.hits, waited, excused);
    }
    s_disc.reads = s_disc.sectors = s_disc.hits = s_disc.waitSteps = 0;
    s_disc.waitMs = s_disc.waitMaxMs = 0.0;
}
