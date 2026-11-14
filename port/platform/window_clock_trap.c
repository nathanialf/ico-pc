/*
 * port/platform/window_clock_trap.c
 *
 * The window's side of the clock lift and the worst second
 * (window_clock_trap.h, clock_trap.h).
 */
#include "window_clock_trap.h"

#include <stdio.h>
#include <string.h>
#include "attempt_ledger.h"
#include "clock_trap.h"
#include "diag_host.h"
#include "ico_thread.h"
#include "rd.h"
#include "render_thread.h"
#include "window_perf_hint.h"
#ifdef __ANDROID__
#include "gpu_sysfs_android.h"
#endif

/* the game thread's */
static ClockTrap s_ct;
static ClockTrapHist s_hist;
static double s_blockStartMs = -1.0;
/* this block's clusters and dips, worked out once (the ranked line comes
   before the block's own line) */
static bool s_dipDone;
static ClockTrapClusters s_now;
static ClockTrapDip s_dips[CLOCK_TRAP_CLUSTERS];
static int s_dipN;
/* whether the ranked line names the clocks (clock_trap_due), and why */
static ClockTrapDue s_due;
/* the seconds still to trace after a lift, the next one's number, the
   trace lines written in the run (CLOCK_TRAP_TRACE_MAX) */
static unsigned s_traceLeft, s_traceK, s_traceLines;

/* the cores' clock samples (Android's sampler ring; NULL elsewhere) */
static const GpuSysfsRing *ring_now(void)
{
#ifdef __ANDROID__
    static GpuSysfsRing s_ring;

    if (gpu_sysfs_android_ring(&s_ring)) {
        return &s_ring;
    }
#endif
    return NULL;
}

/* a second after a lift: its pictures, CPU and clocks */
static void trace(const ClockTrapSecond *s)
{
    char line[512];

    if (s_traceLines >= CLOCK_TRAP_TRACE_MAX) {
        if (s_traceLines++ == CLOCK_TRAP_TRACE_MAX) {
            fprintf(stderr, "window: clock lift trace: %u lines written, no more this run\n",
                    CLOCK_TRAP_TRACE_MAX);
        }
        return;
    }
    s_traceLines++;
    (void)clock_trap_trace_text(s, ++s_traceK, ring_now(), line, sizeof(line));
    fprintf(stderr, "window: %s\n", line);
}

static void lift(const ClockTrapSecond *s)
{
    char line[320];

    if (!window_perf_hint_lift()) {
        return; /* no notice on this system, or no session: the row says which */
    }
    ico_attempt_fire(ICO_ATT_CLOCK_LIFT, 1u);
    (void)clock_trap_lift_text(s, line, sizeof(line));
    ico_log_limited("window: clock lift asked", "window: %s", line);
    /* the seconds after it, one line each (a lift while tracing starts the
       count again) */
    s_traceLeft = CLOCK_TRAP_TRACE_SECONDS;
    s_traceK = 0;
}

static void events(unsigned ev, const ClockTrapSecond *closed)
{
    /* the closed second traced first: the lift's own second is the lift's
       line, the trace the ones after */
    if ((ev & CLOCK_TRAP_EV_CLOSED) && s_traceLeft > 0) {
        s_traceLeft--;
        trace(closed);
    }
    if (ev & CLOCK_TRAP_EV_LIFT) {
        lift(closed);
    }
}

void window_clock_trap_picture(const struct RdPerfRecord *r)
{
    ClockTrapSecond closed;

    if (!r->presented) {
        return;
    }
    if (s_blockStartMs < 0.0) {
        s_blockStartMs = ico_thread_now_ms();
    }
    /* the picture's CPU as the hint reports it (perf_hint.h perf_hint_work):
       the record's time less its waits */
    const double cpu = r->totalMs - r->presentMs - r->fenceWaitMs - r->acquireMs;

    events(
        clock_trap_picture(&s_ct, ico_thread_now_ms(), r->frame, cpu > 0.0 ? cpu : -1.0, &closed),
        &closed);
}

void window_clock_trap_ask(bool held, bool asMade)
{
    ClockTrapSecond closed;

    events(clock_trap_ask(&s_ct, ico_thread_now_ms(), held, asMade, &closed), &closed);
}

/* this block's clusters against the blocks before (once a block) */
static void dips(void)
{
    if (s_dipDone) {
        return;
    }
    s_dipDone = true;
    s_dipN = 0;
    memset(&s_now, 0, sizeof(s_now));
    const ClockTrapBlock *blk = &s_ct.block; /* the block so far (taken at its own line) */
    const double cpu = blk->cpuN ? blk->cpuMs / blk->cpuN : 0.0;
    const int core = ico_render_thread_render_core();
    double coreMhz = 0.0;
#ifdef __ANDROID__
    GpuSysfsBlock b;

    if (s_blockStartMs >= 0.0 && gpu_sysfs_android_block(s_blockStartMs, &b)) {
        clock_trap_clusters(&b, &s_now);
        s_dipN = clock_trap_dips(&s_hist, &s_now, s_dips, CLOCK_TRAP_CLUSTERS);
        if (core >= 0 && core < GPU_SYSFS_CPUS && b.cpuN[core] != 0) {
            coreMhz = b.cpuSum[core] / b.cpuN[core];
        }
    }
#endif
    s_now.cpuMs = cpu;
    (void)clock_trap_due(&s_hist, &s_now, blk->pictures, blk->frames, cpu, core, coreMhz, &s_due);
}

void window_clock_trap_ranked(char *line, size_t cap)
{
    ClockTrapHint hint;
    const size_t at = strlen(line);

    dips();
    /* only a block at or near one picture a tick with its picture CPU
       raised (clock_trap_due): the clusters under their level, or that
       none is, then the block's numbers and the render thread's core */
    if (!s_due.due || at + 1u >= cap) {
        return;
    }
    if (s_dipN > 0) {
        memset(&hint, 0, sizeof(hint));
        (void)window_perf_hint_peek(&hint);
        (void)clock_trap_dip_text(s_dips, s_dipN, &hint, line + at, cap - at);
    } else {
        (void)snprintf(line + at, cap - at,
                       "; the CPU clocks not under their level (no cluster under %.0f %% of its "
                       "median before)",
                       100.0 * CLOCK_TRAP_DIP_SHARE);
    }
    const size_t at2 = strlen(line);

    if (at2 + 1u < cap) {
        (void)clock_trap_due_text(&s_due, line + at2, cap - at2);
    }
}

void window_clock_trap_block(void)
{
    const GpuSysfsRing *ring = NULL;
    ClockTrapBlock b;
    char line[1024];
    const double now = ico_thread_now_ms();

    dips();
    ring = ring_now();
    clock_trap_block_take(&s_ct, &b);
    if (s_blockStartMs >= 0.0) {
        (void)clock_trap_worst_text(&b, s_blockStartMs, ring, line, sizeof(line));
        fprintf(stderr, "window: %s\n", line);
    }
    if (s_now.n > 0) {
        clock_trap_hist_push(&s_hist, &s_now);
    }
    s_dipDone = false;
    s_blockStartMs = now;
}
