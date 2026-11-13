/* rd_pace_screen_live.c: the screen's rule in the renderer (rd_pace_screen.h).
 *
 * Threads: the shown pictures' samples come from the thread that collects
 * the records (rd_glass.c's join, rd_perf.c rd__perf_collect_gpu: the render
 * thread, or the poster's own thread for a present made in place, never
 * both at once: the poster waits for the render thread's picture before it
 * replays one itself, rd_thread.c presentDrain) over rd_pace_screen.c's
 * single-producer ring; the poster's thread (window_present.c
 * window_present_pace: rd_present_post's presentRebase and
 * rd_present_as_made, one thread) takes them before it places a picture and
 * owns the model, the switch's state and the ledger's row (and hands each
 * new shown picture to the even cadence, rd_pace_even_live.c, the same
 * thread).  The 10 s sums are atomics the window's thread takes. */
#include <stdio.h>
#include <stdlib.h>

#include "attempt_ledger.h"
#include "rd_pace_even.h"
#include "rd_pace_gpu.h"
#include "rd_pace_screen.h"

/* the placements as made with no display time back before the ledger says
   the display gives none (10 s at 60 pictures a second, as the GPU model's
   RD_PACE_GPU_NO_CLOCK) */
#define NO_TIMES 600u

bool rd_pace_screen_on(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        const char *e = getenv("ICO_RD_PACE_SCREEN");
        s_on = e != NULL && e[0] != '\0' ? (e[0] != '0') : RD_PACE_SCREEN_DEFAULT;
    }
    return s_on != 0;
}

/* the poster's */
static RdPaceScreen s_m;
static bool s_times;       /* a shown picture's time came back this run */
static uint32_t s_noTimes; /* placements as made before one did */
static int s_state = -1;   /* the row as last decided */
static uint32_t s_changes;

static void decide(bool asMade)
{
    const int clock = rd_pace_gpu_clock_state();
    const int times = s_times ? 1 : (s_noTimes >= NO_TIMES ? 0 : -1);
    /* 0 paced, 1 measured, 2 no display times, 3 no GPU end; -1 not known */
    const int st = !asMade      ? 0
                   : clock == 0 ? 3
                   : times == 0 ? 2
                   : clock < 0  ? -1
                   : times == 1 ? 1
                                : -1;
    if (st < 0 || st == s_state) {
        return;
    }
    s_state = st;
    ico_attempt_decide_pace_screen(asMade ? 1 : 0, clock, times, rd_pace_screen_on() ? 1 : 0);
    const uint32_t k = s_changes++;
    if (k < 8u || k % 100u == 0u) {
        char line[2048];
        (void)rd_pace_screen_start_text(st == 1, rd_pace_screen_on(),
                                        ico_attempt_why_of(ICO_ATT_PACE_SCREEN), line, sizeof line);
        fprintf(stderr, "%s\n", line);
    }
}

/* the ring taken; each new shown picture also to the even cadence */
static uint32_t take(void)
{
    const uint32_t n = rd_pace_screen_drain(&s_m);
    if (n != 0u) {
        s_times = true;
        const uint32_t k = n < s_m.sN ? n : s_m.sN;
        for (uint32_t i = 0; i < k; i++) {
            rd_pace_even_seen(&s_m.s[(s_m.sAt + RD_PS_WIN - k + i) % RD_PS_WIN]);
        }
    }
    return n;
}

const RdPaceScreenFit *rd_pace_screen_live_fit(double nowMs, bool *fit)
{
    (void)take();
    if (s_m.dirty) {
        rd_pace_screen_fit(&s_m);
    }
    const RdPaceScreenFit *f = &s_m.fit;
    *fit = s_m.sN >= RD_PS_MIN && f->grid && f->latch && nowMs - s_m.newestEndMs <= RD_PS_STALE_MS;
    return f;
}

int rd_pace_screen_times_state(void)
{
    return s_times ? 1 : (s_noTimes >= NO_TIMES ? 0 : -1);
}

void rd_pace_screen_step(const RdPaceScreenIn *in, RdPaceScreenPlace *out)
{
    if (take() == 0u && !s_times && s_noTimes < NO_TIMES) {
        s_noTimes++;
    }
    decide(true);
    rd_pace_screen_place(&s_m, in, out);
    rd_pace_screen_tally(out, &s_m.fit);
    if (out->how == RD_PS_PLACED) {
        ico_attempt_fire(ICO_ATT_PACE_SCREEN, 1u);
        if (out->heldLo || out->heldHi) {
            ico_attempt_fire2(ICO_ATT_PACE_SCREEN, 1u);
        }
    }
}

void rd_pace_screen_paced(void)
{
    /* the shown pictures dropped unread: the model starts again when the
       pictures are made as fast as the GPU allows again */
    rd_pace_screen_drop();
    if (s_m.sN != 0u || s_m.have) {
        rd_pace_screen_reset(&s_m);
    }
    decide(false);
}
