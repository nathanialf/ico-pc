/*
 * port/platform/window_clock_trap.c
 *
 * The window's side of the worst second and of the pins let go at one
 * picture a tick (window_clock_trap.h, clock_trap.h, pin_release.h).
 */
#include "window_clock_trap.h"

#include <stdio.h>
#include <string.h>
#include "attempt_ledger.h"
#include "clock_trap.h"
#include "cpu_place.h"
#include "diag_host.h"
#include "ico_thread.h"
#include "pin_release.h"
#include "rd.h"
#include "render_thread.h"
#include "window_perf_hint.h"
#include "window_present.h" /* window_slow_target_hz */
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
/* the pins let go at one picture a tick (pin_release.h) */
static PinRelease s_pr;
static bool s_prInit;

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

/* the keeper's rule (render_thread.h): 1 the pins may be let go */
static int rule_now(void)
{
#ifdef __ANDROID__
    return ico_render_thread_pin_release_rule();
#else
    return 0;
#endif
}

static void pr_init(void)
{
    if (!s_prInit) {
        pin_release_reset(&s_pr);
        s_prInit = true;
    }
}

/* a closed second: the pins' rule, its trace line, the let-go and
   taken-again lines and the keeper told */
static void second(const ClockTrapSecond *closed)
{
    PinCores cores;
    unsigned blocks = 0;
    const double med = clock_trap_cpu_median(&s_hist, &blocks);
    const unsigned ev = pin_release_second(&s_pr, closed, med, blocks, rule_now() == 1, &cores);
    char line[1536];

    if (ev & PIN_RELEASE_EV_TRACE) {
        const long rtid = ico_render_thread_render_tid();

        (void)pin_release_trace_text(&s_pr, closed, &cores,
                                     rtid > 0 ? cpu_place_allowed_tid(rtid) : 0u,
                                     cpu_place_allowed(), ring_now(), line, sizeof(line));
        fprintf(stderr, "window: %s\n", line);
    }
    if (ev & PIN_RELEASE_EV_TRACE_END) {
        fprintf(stderr, "window: pin trace: %u lines written, no more this run\n",
                PIN_RELEASE_TRACE_MAX);
    }
    if (ev & PIN_RELEASE_EV_RELEASE) {
        ico_render_thread_pin_release(true);
        ico_attempt_fire(ICO_ATT_PIN_RELEASE, 1u);
        (void)pin_release_release_text(&s_pr, closed, blocks >= 2u ? med : 0.0, line, sizeof(line));
        ico_log_limited("window: pins let go", "window: %s", line);
    }
    if (ev & PIN_RELEASE_EV_RESTORE) {
        ico_render_thread_pin_release(false);
        ico_attempt_fire2(ICO_ATT_PIN_RELEASE, 1u);
        (void)pin_release_restore_text(&s_pr, line, sizeof(line));
        ico_log_limited("window: pins taken again", "window: %s", line);
    }
}

static void events(unsigned ev, const ClockTrapSecond *closed)
{
    pr_init();
    if (ev & CLOCK_TRAP_EV_CLOSED) {
        second(closed);
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

    /* the rate in force the worst second is judged against (clock_trap.h) */
    clock_trap_target(&s_ct, window_slow_target_hz());
    events(
        clock_trap_picture(&s_ct, ico_thread_now_ms(), r->frame, cpu > 0.0 ? cpu : -1.0, &closed),
        &closed);
    /* this picture's cores into its second (after the second it closed):
       the render thread's at its last picture, the game thread's now */
    pin_release_note(&s_pr, ico_render_thread_render_core(), cpu_place_now());
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
    char line[2048];
    const double now = ico_thread_now_ms();
    double coreMhz[PIN_RELEASE_CORES];
    int coreCount = 0;
    const int rule = rule_now();

    dips();
    pr_init();
    ring = ring_now();
    clock_trap_block_take(&s_ct, &b);
    memset(coreMhz, 0, sizeof(coreMhz));
#ifdef __ANDROID__
    {
        GpuSysfsBlock gb;

        if (s_blockStartMs >= 0.0 && gpu_sysfs_android_block(s_blockStartMs, &gb)) {
            for (int i = 0; i < gb.cpuCount && i < PIN_RELEASE_CORES && i < GPU_SYSFS_CPUS; i++) {
                coreMhz[i] = gb.cpuN[i] != 0 ? gb.cpuSum[i] / gb.cpuN[i] : 0.0;
                coreCount = i + 1;
            }
        }
    }
    ico_attempt_decide_pin_release(1, rule);
#else
    ico_attempt_decide_pin_release(0, rule);
#endif
    if (s_blockStartMs >= 0.0) {
        (void)clock_trap_worst_text(&b, s_blockStartMs, ring, line, sizeof(line));
        const size_t at = strlen(line);

        (void)pin_release_block_text(&s_pr, coreMhz, coreCount,
                                     rule == 1   ? NULL
                                     : rule == 2 ? "the few-cores rule keeps its pins"
                                                 : "nothing pinned",
                                     line + at, sizeof(line) - at);
        fprintf(stderr, "window: %s\n", line);
    }
    if (s_now.n > 0) {
        clock_trap_hist_push(&s_hist, &s_now);
    }
    s_dipDone = false;
    s_blockStartMs = now;
}
