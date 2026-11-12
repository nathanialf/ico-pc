/* rd_cpu_phase.c: the render thread's CPU phases a picture (rd_cpu_phase.h).
 *
 * The 10 s line splits interp, upload and walk (window_host.c's "window: N
 * replays: CPU ..." line) into the phases the next CPU cuts choose among
 * (build-host/tmp/v0115/design/OPENING_SPLIT.md C.3): a picture's
 * interpolation (rd_interp.c rd__interp_frame), its upload (rd_replay.c
 * replayFrame up to the walk, rd_store_plan.c's scans) and its walk.  Each
 * group starts with a clock read (rd_cpu_start) and each phase ends with one
 * (rd_cpu_mark): 8 reads an interpolated picture, 11 an upload, 3 a walk,
 * about 22 a picture, never one a draw; rd__now_ms is clock_gettime's
 * monotonic clock (QueryPerformanceCounter on Windows), tens of
 * nanoseconds a read, so under a microsecond a picture.  The per-draw
 * look-ups (the pipeline index, the uniform blocks' cache) are counted by
 * a plain increment each on the render thread (g_rdCpu) and folded once a
 * picture.  The phases' sums are atomics the 10 s line takes; the per
 * picture accumulators are the render thread's own.
 *
 * The line (window_host.c perf_log, after the late passes' and the aura
 * paste's lines):
 *   window: CPU phases ms a picture over N pictures (R a tick): interp copy
 *     C, index I, blend B, morph M, unmatched U, hash H (K of the pictures
 *     hashed), the rest F; upload cull X, ring G, acquire and begin A, join
 *     J, meshes E, scans: layer L, fades D, the store plan's walk S, present
 *     plan P, clears K;
 *     walk pre-pass V, late uploads T, lists W; per picture P pipeline
 *     look-ups (H hits), B uniform blocks looked up (Z hits); 22.0 clock
 *     reads a picture */
#include <stdio.h>
#include <string.h>

#include "rd_cpu_phase.h"
#include "rd_internal.h"

RdCpuCounts g_rdCpu;

/* the render thread's picture */
static struct {
    double last;
    double ms[RD_CPU_PHASES];
    uint32_t reads, hashed, lastFrame;
    bool haveFrame;
} s_pic;

/* the 10 s sums (atomics: the line runs on another thread) */
static struct {
    uint64_t ns[RD_CPU_PHASES];
    uint64_t pictures, ticks, hashed, reads;
    uint64_t pipeLookups, pipeHits, uniformLookups, uniformHits;
} s_sum;

void rd_cpu_start(void)
{
    s_pic.last = rd__now_ms();
    s_pic.reads++;
}

void rd_cpu_mark(int phase)
{
    const double now = rd__now_ms();
    if (phase >= 0 && phase < RD_CPU_PHASES && now > s_pic.last) {
        s_pic.ms[phase] += now - s_pic.last;
    }
    s_pic.last = now;
    s_pic.reads++;
}

void rd_cpu_mark_hash(bool hashed)
{
    rd_cpu_mark(RD_CPU_I_HASH);
    s_pic.hashed += hashed ? 1u : 0u;
}

static void add(uint64_t *at, uint64_t v)
{
    if (v) {
        __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
    }
}

void rd_cpu_picture(uint32_t frame)
{
    rd_cpu_mark(RD_CPU_W_LISTS);
    for (int k = 0; k < RD_CPU_PHASES; k++) {
        add(&s_sum.ns[k], (uint64_t)(s_pic.ms[k] * 1.0e6));
        s_pic.ms[k] = 0.0;
    }
    add(&s_sum.pictures, 1u);
    add(&s_sum.ticks, !s_pic.haveFrame || frame != s_pic.lastFrame ? 1u : 0u);
    s_pic.haveFrame = true;
    s_pic.lastFrame = frame;
    add(&s_sum.hashed, s_pic.hashed);
    add(&s_sum.reads, s_pic.reads);
    s_pic.hashed = s_pic.reads = 0;
    add(&s_sum.pipeLookups, g_rdCpu.pipeLookups);
    add(&s_sum.pipeHits, g_rdCpu.pipeHits);
    add(&s_sum.uniformLookups, g_rdCpu.uniformLookups);
    add(&s_sum.uniformHits, g_rdCpu.uniformHits);
    memset(&g_rdCpu, 0, sizeof(g_rdCpu));
}

static uint64_t take(uint64_t *at)
{
    return __atomic_exchange_n(at, 0, __ATOMIC_RELAXED);
}

void rd_cpu_phase_take(RdCpuWindow *w)
{
    for (int k = 0; k < RD_CPU_PHASES; k++) {
        w->ns[k] = take(&s_sum.ns[k]);
    }
    w->pictures = take(&s_sum.pictures);
    w->ticks = take(&s_sum.ticks);
    w->hashed = take(&s_sum.hashed);
    w->reads = take(&s_sum.reads);
    w->pipeLookups = take(&s_sum.pipeLookups);
    w->pipeHits = take(&s_sum.pipeHits);
    w->uniformLookups = take(&s_sum.uniformLookups);
    w->uniformHits = take(&s_sum.uniformHits);
}

bool rd_cpu_phase_format(const RdCpuWindow *w, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (w->pictures == 0) {
        return false;
    }
    const double n = (double)w->pictures;
    double ms[RD_CPU_PHASES];
    for (int k = 0; k < RD_CPU_PHASES; k++) {
        ms[k] = (double)w->ns[k] / 1.0e6 / n;
    }
    snprintf(
        out, size,
        "CPU phases ms a picture over %llu pictures (%.1f a tick): interp copy %.2f, index "
        "%.2f, blend %.2f, morph %.2f, unmatched %.2f, hash %.2f (%llu of the pictures "
        "hashed), the rest %.2f; upload cull %.2f, ring %.2f, acquire and begin %.2f, join "
        "%.2f, meshes %.2f, scans: layer %.2f, fades %.2f, the store plan's walk %.2f, present "
        "plan %.2f, clears "
        "%.2f; walk pre-pass %.2f, late uploads %.2f, lists %.2f; per picture %.0f pipeline "
        "look-ups (%.0f hits), %.0f uniform blocks looked up (%.0f hits); %.1f clock reads a "
        "picture",
        (unsigned long long)w->pictures, w->ticks ? n / (double)w->ticks : 0.0, ms[RD_CPU_I_COPY],
        ms[RD_CPU_I_INDEX], ms[RD_CPU_I_BLEND], ms[RD_CPU_I_MORPH], ms[RD_CPU_I_UNMATCHED],
        ms[RD_CPU_I_HASH], (unsigned long long)w->hashed, ms[RD_CPU_I_REST], ms[RD_CPU_U_CULL],
        ms[RD_CPU_U_RING], ms[RD_CPU_U_BEGIN], ms[RD_CPU_U_JOIN], ms[RD_CPU_U_MESHES],
        ms[RD_CPU_U_LAYER], ms[RD_CPU_U_FADES], ms[RD_CPU_U_SCANS], ms[RD_CPU_U_PRESENT],
        ms[RD_CPU_U_CLEARS], ms[RD_CPU_W_PREPASS], ms[RD_CPU_W_LATE], ms[RD_CPU_W_LISTS],
        (double)w->pipeLookups / n, (double)w->pipeHits / n, (double)w->uniformLookups / n,
        (double)w->uniformHits / n, (double)w->reads / n);
    return true;
}

void rd_cpu_phase_line(void)
{
    RdCpuWindow w;
    rd_cpu_phase_take(&w);
    char line[1024];
    if (rd_cpu_phase_format(&w, line, sizeof line)) {
        fprintf(stderr, "window: %s\n", line);
    }
}
