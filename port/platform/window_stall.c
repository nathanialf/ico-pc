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
#include "rhi.h"
#include "thermal_text.h"
#ifdef __ANDROID__
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

/* what follows the around line's samples: the app's own words on the
   device's thermal state and screen recording where it reports them ("" on
   Android when it reports none, and everywhere else) */
static const char *around_suffix(void)
{
#ifdef __ANDROID__
    static char text[160];
    ThermalState t = ico_host_thermal(NULL);

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
    s_gpuOn = gpu_sysfs_any_sampled(&info); /* the GPU's nodes or the CPU clocks */
    s_gpuBlockMs = ico_thread_now_ms();
#endif
}

void window_stall_gpu_close(void)
{
#ifdef __ANDROID__
    gpu_sysfs_android_stop();
#endif
}

void window_stall_gpu_block(void)
{
#ifdef __ANDROID__
    const double now = ico_thread_now_ms();
    static int s_thermalSaid;
    const char *why = "";
    ThermalState t;
    GpuSysfsBlock b;
    char text[512];

    if (s_gpuOn && gpu_sysfs_android_block(s_gpuBlockMs, &b)) {
        s_gpuBlockMs = now;
        gpu_sysfs_block_text(&b, text, sizeof(text));
        fprintf(stderr, "window: %s\n", text);
    }
    /* the thermal line each block, after the driver's; "not available"
       only once */
    t = ico_host_thermal(&why);
    if (thermal_state_any(&t) || !s_thermalSaid) {
        thermal_block_text(&t, why, text, sizeof(text));
        fprintf(stderr, "window: %s\n", text);
        s_thermalSaid = 1;
    }
#endif
}

void window_stall_blocked(const RdPerfRecord *r)
{
    around_queue(r);
    s_after.pending = 1;
    s_after.replay = r->replay;
    s_after.ms = r->presentMs;
    /* only where the record carries the present split (the Vulkan
       backend): on OpenGL an armed replay's stamps would each read the GL
       clock, which may hold the CPU (rd.h rd_perf_passes_arm_backend) */
    if (rd_perf_passes_arm_backend(rhi_backend(), r->presentSplit != 0) &&
        rd_perf_passes_arm(true)) {
        char text[256];

        rd_perf_passes_arm_text(r->replay, r->presentMs, rhi_backend_kind_name(rhi_backend()), text,
                                sizeof(text));
        fprintf(stderr, "window: %s\n", text);
    }
}

void window_stall_after(const RdPerfRecord *r, PaceGpuGap *gap)
{
    char text[448];
    PresentStallRec o;

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
}
