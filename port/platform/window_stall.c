/*
 * port/platform/window_stall.c
 *
 * The window's lines around a present that blocked (window_stall.h).
 */
#include "window_stall.h"
#include <stdio.h>
#include <string.h>
#include "cpu_place.h"
#include "diag_host.h"
#include "gpu_sysfs.h"
#include "ico_thread.h"
#include "present_hist.h"
#include "present_stall_block.h"
#include "rd_perf_rare.h"
#include "render_thread.h" /* ico_render_thread_tick */
#include "rhi.h"
#include "thermal_text.h"
#include "log_cost.h"
#include "window_lines.h"   /* window_log_cost_present */
#include "window_present.h" /* window_refresh_ns */
#include "sys_cell.h"
#ifdef __ANDROID__
#include <sys/system_properties.h>
#include "gpu_sysfs_android.h"
#include "host_android.h"
#endif

/* the present that blocked, waiting for the record after it */
static struct {
    int pending;
    unsigned replay;
    double ms;
} s_after;

static void stall_rec(const RdPerfRecord *r, PresentStallRec *o)
{
    memset(o, 0, sizeof(*o));
    o->replay = r->replay;
    o->presentMs = r->presentMs;
    o->split = r->presentSplit; /* 0, 1 or 2 (no present call made) */
    o->lockMs = r->presentLockMs;
    o->callMs = r->presentCallMs;
    o->pollMs = r->presentPollMs;
    o->waitMs = r->waitMs;
    o->fenceMs = r->fenceWaitMs;
    o->acquireMs = r->acquireMs;
    o->gpuValid = r->gpuValid != 0;
    o->gpuMs = r->gpuMs;
    o->gapValid = r->gpuGapValid != 0;
    o->gapMs = (double)r->gpuGapNs / 1e6;
    o->gapSkipped = r->gpuGapSkipped;
    o->gapBackMs = (double)r->gpuGapBackNs / 1e6;
}

const char *window_stall_split(const RdPerfRecord *r, unsigned blockGpuN, double blockGpuMs)
{
    static char text[448];
    PresentStallRec o;

    stall_rec(r, &o);
    present_split_text(&o, blockGpuN, blockGpuMs, text, sizeof(text));
    return text;
}

/* The presents that blocked waiting for the kernel GPU driver's sample
   after them (window_stall_poll), oldest first; a full queue drops the
   oldest.  s_gpuOn: the nodes are sampled */
#define AROUND_MAX 8
#define AROUND_WAIT_MS 1000.0 /* past the present's end: written without the after sample */

static struct {
    unsigned n;

    struct {
        unsigned replay;
        double t0Ms, t1Ms;
        int core; /* the game thread's core as the record popped (-1 not known) */
    } q[AROUND_MAX];
} s_around;

static int s_gpuOn;
#ifdef __ANDROID__
static double s_gpuBlockMs = -1.0; /* the last block line's time, ico_thread_now_ms */
#endif

/* the stalls per block and per run (present_stall_block.h); s_stallT0 the
   block's start, ico_thread_now_ms (0: the first record not yet seen) */
static StallBlock s_stall;
static double s_stallT0;

/* the display's rate now (SDL's current mode, as the 10 s lines read it),
   0 not known */
static double display_hz_now(void)
{
    const Uint64 ns = window_refresh_ns(0);

    return ns > 0 ? 1e9 / (double)ns : 0.0;
}

static void arm_block_end(void); /* the bounded pass timing, below */

/* the block's stall line and its join into the run's sums; clock the
   kernel driver's GPU clock in the block, NULL when not read */
static void stall_block_close(const StallBlockClock *clock)
{
    const double now = ico_thread_now_ms();
    char text[2048];

    if (s_stallT0 <= 0.0) {
        return; /* no record yet: nothing presented, nothing to say */
    }
    if (stall_block_end(&s_stall, (now - s_stallT0) / 1000.0, display_hz_now(), clock, text,
                        sizeof(text))) {
        fprintf(stderr, "window: %s\n", text);
    }
    s_stallT0 = now;
    rd_perf_rare_block_end(); /* a stalled replay's rare pipelines: counted per block */
    arm_block_end();
}

void window_stall_run_summary(void)
{
    char text[1536];

    stall_block_close(NULL); /* the last part block (its clock not read) */
    if (stall_block_run_text(&s_stall, text, sizeof(text)) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
}

static void around_queue(const RdPerfRecord *r)
{
    if (!s_gpuOn || !(r->presentEndMs > 0.0) || !present_stall_lines(r->presentMs)) {
        return;
    }
    if (s_around.n == AROUND_MAX) {
        memmove(&s_around.q[0], &s_around.q[1], sizeof(s_around.q[0]) * (AROUND_MAX - 1));
        s_around.n--;
    }
    s_around.q[s_around.n].replay = r->replay;
    s_around.q[s_around.n].t0Ms = r->presentEndMs - r->presentMs;
    s_around.q[s_around.n].t1Ms = r->presentEndMs;
    s_around.q[s_around.n].core = cpu_place_now();
    s_around.n++;
}

/* The asks the game thread must not make (sys_cell.h): on Android the log
 * thread (host_android.c ico_android_log_mirror_set_tick, the one worker
 * every Android start has: the GPU sampler runs only where a node is
 * readable, the performance hints' thread only from API 33) asks the
 * thermal state (two JNI calls into PowerManager, binder calls: 1.6-2.8 ms
 * of the owner's Pixel's 10 s print, rc4-rc9) and reads /proc/self/status
 * and /proc/meminfo once a second; the window's lines read the last
 * answers and their age.  Without the log thread they are asked where they
 * were, on the game thread (never both: ico_host_thermal keeps statics). */
static SysCell s_thermalCell, s_memoryCell;
#ifdef __ANDROID__
static const char *s_thermalWhy; /* __atomic: a static string (Android's asks only) */
#endif
static int s_asksOn;     /* the tick is set: the game thread asks nothing */
static uint32_t s_asksN; /* __atomic: the tick's runs since the window took them */

#ifdef __ANDROID__
static void asks_tick(void)
{
    if (ico_diag_watchdog_paused()) {
        return; /* the app in the background: nothing asked, as the GPU sampler */
    }
    double t0 = ico_thread_now_ms();
    const char *why = "";
    const ThermalState t = ico_host_thermal(&why);
    double t1 = ico_thread_now_ms();
    const double tv[3] = {t.headroom, (double)t.status, (double)t.recording};
    __atomic_store_n(&s_thermalWhy, why, __ATOMIC_RELEASE);
    sys_cell_put(&s_thermalCell, t1, t1 - t0, tv, 3);
    long rss = -1, peak = -1, avail = -1, total = -1;
    t0 = t1;
    ico_diag_process_memory(&rss, &peak);
    const double dev = ico_diag_device_memory(&avail, &total) == 0 ? 1.0 : 0.0;
    t1 = ico_thread_now_ms();
    const double mv[5] = {(double)rss, (double)peak, (double)avail, (double)total, dev};
    sys_cell_put(&s_memoryCell, t1, t1 - t0, mv, 5);
    ico_render_thread_tick(); /* the threads' schedstat and list (render_thread.h) */
    __atomic_fetch_add(&s_asksN, 1u, __ATOMIC_RELAXED);
}
#endif

uint32_t window_stall_asks_take(double *thermalMs, double *memoryMs)
{
    double v[1], at, took;
    *thermalMs = sys_cell_get(&s_thermalCell, v, 1, &at, &took) ? took : 0.0;
    *memoryMs = sys_cell_get(&s_memoryCell, v, 1, &at, &took) ? took : 0.0;
    return __atomic_exchange_n(&s_asksN, 0u, __ATOMIC_RELAXED);
}

#ifdef __ANDROID__
/* the thermal state: the log thread's last answer (and its age) when the
   tick runs, else asked here as before */
static ThermalState thermal_now(const char **why, double *ageMs)
{
    *ageMs = -1.0;
    if (!s_asksOn) {
        return ico_host_thermal(why);
    }
    ThermalState t = thermal_state_none();
    double v[3], at;
    if (sys_cell_get(&s_thermalCell, v, 3, &at, NULL) == 3) {
        t.headroom = v[0];
        t.status = (int)v[1];
        t.recording = (int)v[2];
        *ageMs = ico_thread_now_ms() - at;
    }
    if (why != NULL) {
        const char *w = __atomic_load_n(&s_thermalWhy, __ATOMIC_ACQUIRE);
        *why = w != NULL ? w : "not asked yet";
    }
    return t;
}
#endif

int window_stall_memory(long *rss, long *peak, long *avail, long *total, double *ageMs)
{
    double v[5], at;
    if (!s_asksOn || sys_cell_get(&s_memoryCell, v, 5, &at, NULL) != 5) {
        return -1; /* read on the caller's thread */
    }
    *rss = (long)v[0];
    *peak = (long)v[1];
    *avail = v[4] != 0.0 ? (long)v[2] : -1;
    *total = v[4] != 0.0 ? (long)v[3] : -1;
    *ageMs = ico_thread_now_ms() - at;
    return 0;
}

/* what follows the around line's samples: the app's own words on the
   device's thermal state and screen recording where it reports them ("" on
   Android when it reports none, and everywhere else) */
static const char *around_suffix(void)
{
#ifdef __ANDROID__
    static char text[160];
    double age;
    ThermalState t = thermal_now(NULL, &age);

    thermal_around_text(&t, text, sizeof text);
    return text;
#else
    return "";
#endif
}

void window_stall_poll(void)
{
#ifdef __ANDROID__
    const double now = ico_thread_now_ms();
    unsigned keep = 0;

    for (unsigned i = 0; i < s_around.n; i++) {
        GpuSysfsAround a;
        char text[512];

        if (!gpu_sysfs_android_around(s_around.q[i].t0Ms, s_around.q[i].t1Ms, s_around.q[i].core,
                                      &a)) {
            continue; /* not sampled: dropped */
        }
        if (!a.hasAfter && now - s_around.q[i].t1Ms < AROUND_WAIT_MS) {
            s_around.q[keep++] = s_around.q[i]; /* its sample after is still to come */
            continue;
        }
        gpu_sysfs_around_text(&a, s_around.q[i].replay, text, sizeof(text));
        ico_log_limited("window: kernel GPU driver around", "window: %s%s", text, around_suffix());
    }
    s_around.n = keep;
#else
    (void)around_suffix;
    s_around.n = 0;
#endif
}

void window_stall_gpu_open(void)
{
#ifdef __ANDROID__
    static int s_said;
    GpuSysfsInfo info;
    char text[512];

    gpu_sysfs_android_start(&info); /* the sampler thread (again after a close) */
    if (s_said) {
        return;
    }
    s_said = 1;
    gpu_sysfs_open_text(&info, text, sizeof(text));
    fprintf(stderr, "window: %s\n", text);
    {
        /* what the device runs (issues 123, 179: their logs did not say;
           the performance hints need API 33): the system properties,
           android.os.Build's own sources, read once here */
        char rel[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "", man[PROP_VALUE_MAX] = "",
             model[PROP_VALUE_MAX] = "", dev[PROP_VALUE_MAX] = "";
        (void)__system_property_get("ro.build.version.release", rel);
        (void)__system_property_get("ro.build.version.sdk", sdk);
        (void)__system_property_get("ro.product.manufacturer", man);
        (void)__system_property_get("ro.product.model", model);
        (void)__system_property_get("ro.product.device", dev);
        fprintf(stderr, "window: device: Android %s (API %s), %s %s (%s)\n", rel[0] ? rel : "?",
                sdk[0] ? sdk : "?", man[0] ? man : "?", model[0] ? model : "?", dev[0] ? dev : "?");
    }
    if (ico_android_log_mirror_running()) {
        /* the thermal and /proc asks on the log thread from here */
        s_asksOn = 1;
        ico_android_log_mirror_set_tick(asks_tick);
        fprintf(stderr,
                "window: the thermal state and the memory figures are asked once a second on the "
                "log thread; the 10 s lines read the last answers\n");
    }
    s_gpuOn = gpu_sysfs_any_sampled(&info); /* the GPU's nodes or the CPU clocks */
    s_gpuBlockMs = ico_thread_now_ms();
#endif
}

void window_stall_gpu_close(void)
{
#ifdef __ANDROID__
    if (s_asksOn) {
        /* the log thread's asks end with the window (the thermal JNI calls
           and the /proc reads once a second); first, before the sampler
           stops.  A tick already running ends on its own: asks_tick reads
           only statics (sys_cell.h cells, render_thread.c's), none freed
           here or by ico_render_thread_stop, which ran before */
        ico_android_log_mirror_set_tick(NULL);
        s_asksOn = 0;
        fprintf(stderr, "window: the log thread's thermal and memory asks stopped with the "
                        "window\n");
    }
    gpu_sysfs_android_stop();
#endif
}

void window_stall_gpu_block(void)
{
    window_log_cost_mark("the acquire and present lines"); /* before it (log_cost.h) */
#ifdef __ANDROID__
    const double now = ico_thread_now_ms();
    static int s_thermalSaid;
    const char *why = "";
    ThermalState t;
    GpuSysfsBlock b;
    StallBlockClock clock = {0};
    char text[512];

    if (s_gpuOn && gpu_sysfs_android_block(s_gpuBlockMs, &b)) {
        s_gpuBlockMs = now;
        gpu_sysfs_block_text(&b, text, sizeof(text));
        fprintf(stderr, "window: %s\n", text);
        if (b.clockN > 0) {
            clock.n = b.clockN;
            clock.minMhz = (int)b.clockMin;
            clock.maxMhz = (int)b.clockMax;
            clock.meanMhz = b.clockMean;
        }
    }
    /* the thermal line each block, after the driver's; "not available"
       only once; the log thread's last answer where it asks (its age said) */
    double age;
    t = thermal_now(&why, &age);
    if (thermal_state_any(&t) || !s_thermalSaid) {
        thermal_block_text(&t, why, text, sizeof(text));
        if (age >= 0.0) {
            fprintf(stderr, "window: %s (asked %.1f s before on the log thread)\n", text,
                    age / 1000.0);
        } else {
            fprintf(stderr, "window: %s\n", text);
        }
        s_thermalSaid = 1;
    }
    stall_block_close(&clock);
#else
    stall_block_close(NULL);
#endif
    window_log_cost_part(LOG_COST_KERNEL);
}

/* The every-replay pass timing armed by a present that blocked
   (rd_perf_passes_arm): held until a stalled replay's passes were compared
   (window_stall_passes) or ARM_QUIET_BLOCKS blocks passed with no present
   blocking, then back to one replay every 250 ms; the next blocked present
   arms it again (issues 179 and 123: armed for the rest of the run) */
#define ARM_QUIET_BLOCKS 3u

static struct {
    int on;         /* armed by this file */
    int compared;   /* a stalled replay's passes compared since */
    unsigned quiet; /* blocks ended with no present blocking since */
    unsigned arms, disarms;
} s_arm;

static void arm_block_end(void)
{
    if (!s_arm.on) {
        return;
    }
    const char *why = NULL;
    if (s_arm.compared) {
        why = "the stalled replay compared";
    } else if (++s_arm.quiet >= ARM_QUIET_BLOCKS) {
        why = "no present blocked in 3 blocks";
    }
    if (why == NULL) {
        return;
    }
    (void)rd_perf_passes_arm(false);
    s_arm.on = s_arm.compared = 0;
    s_arm.quiet = 0;
    s_arm.disarms++;
    if (s_arm.disarms <= 8u || s_arm.disarms % 100u == 0u) {
        fprintf(stderr,
                "window: pass timing: back to one replay every 250 ms (%s; %u times of the run); "
                "a present that blocks arms it again\n",
                why, s_arm.disarms);
    }
}

void window_stall_blocked(const RdPerfRecord *r)
{
    around_queue(r);
    if (r->presentEndMs > 0.0) {
        window_log_cost_present(r->presentEndMs - r->presentMs, r->presentEndMs);
    }
    s_after.pending = 1;
    s_after.replay = r->replay;
    s_after.ms = r->presentMs;
    /* only where the record carries the present split (the Vulkan
       backend): on OpenGL an armed replay's stamps would each read the GL
       clock, which may hold the CPU (rd.h rd_perf_passes_arm_backend) */
    s_arm.quiet = 0; /* a present blocked: the arm holds on */
    if (rd_perf_passes_arm_backend(rhi_backend(), r->presentSplit != 0) &&
        rd_perf_passes_arm(true)) {
        char text[320];

        s_arm.on = 1;
        s_arm.compared = 0;
        s_arm.arms++;
        if (s_arm.arms <= 8u || s_arm.arms % 100u == 0u) {
            rd_perf_passes_arm_text(r->replay, r->presentMs, rhi_backend_kind_name(rhi_backend()),
                                    text, sizeof(text));
            fprintf(stderr, "window: %s\n", text);
        }
    }
}

void window_stall_after(const RdPerfRecord *r, PaceGpuGap *gap)
{
    char text[448];
    PresentStallRec o;
    /* the display's rate read only for a stall (SDL's mode, as the 10 s
       lines read it): the stall's own rate in the run's sums */
    const StallBlockRec sr = {r->presented != 0,
                              r->presentMs,
                              r->gpuValid != 0,
                              r->gpuMs,
                              r->gpuGapValid != 0,
                              (double)r->gpuGapNs / 1e6,
                              (double)r->gpuGapBackNs / 1e6,
                              r->startMs,
                              r->presented && r->presentMs >= STALL_BLOCK_MS ? display_hz_now()
                                                                             : 0.0,
                              r->gpuLateValid,
                              r->gpuLateMs,
                              r->gpuRanTwice};

    if (s_stallT0 <= 0.0) {
        s_stallT0 = ico_thread_now_ms(); /* the first block starts with the first record */
    }
    stall_block_add(&s_stall, &sr);
    if (!s_after.pending) {
        return;
    }
    s_after.pending = 0;
    if (present_stall_lines(s_after.ms)) {
        stall_rec(r, &o);
        present_after_text(s_after.replay, s_after.ms, &o, text, sizeof(text));
        ico_log_limited("window: after the present that blocked", "window: %s", text);
    }
    if (r->gpuGapValid) {
        pace_gpu_gap_stall(gap, r->gpuGapNs);
    }
}

void window_stall_passes(const RdPerfRecord *r, const RdPerfPassAgg *agg, const RdPerfPass *p,
                         uint32_t n, unsigned blockGpuN, double blockGpuMs)
{
    char text[1024];

    if (!r->presented || !present_stall_lines(r->presentMs)) {
        return;
    }
    rd_perf_pass_grow_text(agg, p, n, r->replay, r->presentMs, r->gpuValid, r->gpuMs,
                           blockGpuN ? blockGpuMs : -1.0, 5, text, sizeof(text));
    ico_log_limited("window: stalled replay", "window: %s", text);
    s_arm.compared = s_arm.on; /* the arm's purpose met: off at the block's end */
}
