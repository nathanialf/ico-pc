/*
 * port/platform/window_lines.c
 *
 * Log line writers of the window's 10 s block (window_lines.h), moved out of
 * window_host.c.
 */
#include "window_lines.h"
#include "window_scene.h"
#include "window_stall.h" /* window_stall_memory */

#include <stdio.h>
#include "audio_host.h"
#include "diag_host.h"
#include "dump_auto.h" /* ico_dump_auto_held_bytes */
#include "ico_thread.h"
#include "log_cost.h"
#include "pace_policy.h"
#include "host_loop.h"
#include "rd_tex.h"
#include "rd_vu_rec_early.h" /* rd_vu_record_memory, rd_loss_keep_held_bytes */
#include "rhi.h"
#ifdef __ANDROID__
#include "host_android.h"
#elif !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

/* The adapter's local video memory (RhiStats videoMem*; Direct3D 11 and
   12): usage and budget in whole MB, false when the adapter does not say */
static bool video_memory(const RhiStats *st, unsigned long long *useMb,
                         unsigned long long *budgetMb)
{
    if (st->videoMemBudget == 0) {
        return false;
    }
    *useMb = st->videoMemUsage >> 20;
    *budgetMb = st->videoMemBudget >> 20;
    return true;
}

/* Set when this block's 90 % line was written (window_memory_line clears it) */
static bool s_pagingWarned;

void window_video_memory_watch(void)
{
    RhiStats st;
    unsigned long long use, budget;

    if (s_pagingWarned) {
        return;
    }
    rhi_get_stats(&st);
    if (video_memory(&st, &use, &budget) &&
        st.videoMemUsage / 10u >= st.videoMemBudget / 10u * 9u) {
        s_pagingWarned = true;
        fprintf(stderr,
                "window: video memory %llu MB of %llu MB: the driver may be paging; lower the "
                "resolution or close other programs\n",
                use, budget);
    }
}

/* A replay over a refresh (it cannot fit a slot): where its time went,
   with its own counters; at most SLOW_REPLAY_LINES a block (the block's
   first line counts them all), and the block's longest at its end when the
   cap kept it out (window_host.c slow_replay_longest).  The records arrive
   two replays late (rd_perf.c), so the vsync is the one the record was
   drained at. */
void window_slow_replay_line(const RdPerfRecord *r, unsigned vsync, const char *note)
{
    char gpu[32] = "no GPU timestamps";
    char vm[64] = "";

    if (r->gpuValid) {
        snprintf(gpu, sizeof(gpu), "GPU %.1f ms", r->gpuMs);
    }
    if (r->fenceWaitMs > 50.0) {
        /* a long wait for the GPU: whether the card was out of memory */
        RhiStats st;
        unsigned long long use, budget;

        rhi_get_stats(&st);
        if (video_memory(&st, &use, &budget)) {
            snprintf(vm, sizeof(vm), ", video memory %llu/%llu MB", use, budget);
        }
    }
    fprintf(stderr,
            "window: slow replay %.1f ms (replay %u, frame %u, alpha %.2f, about vsync %u): interp "
            "%.1f, wait %.1f (fence %.1f), acquire %.1f, upload %.1f, walk %.1f, bind %.1f, "
            "submit %.1f, present %.1f; %u textures and %u meshes (%u KB) uploaded (%u KB in "
            "all), %u pipelines created, %u allocations, %u buffers and %u textures created, %u "
            "DATE snapshots; %s%s%s\n",
            r->totalMs, r->replay, r->frame, (double)r->alpha, vsync, r->interpMs, r->waitMs,
            r->fenceWaitMs, r->acquireMs, r->uploadMs, r->walkMs, r->bindMs, r->submitMs,
            r->presentMs, r->textureUploads, r->meshUploads, (unsigned)(r->meshUploadBytes / 1024u),
            (unsigned)(r->uploadBytes / 1024u), r->pipelineCreates, r->memoryAllocs,
            r->buffersCreated, r->texturesCreated, r->dateSnapshots, gpu, vm, note);
}

/* the graphics allocations against the device's limit (a phone GPU allows
   4096) and the process's memory (a phone ends a process that holds too
   much without a word) */
/* The block's memory extremes (Android, the log thread's once-a-second
   reads; issue 179's process swung 538-1067 MB with the
   graphics steady at 335 MB): what holds the process's memory, so a swing
   can be put down to its part ("window: memory in the block: ..."; none
   where the log thread does not read them) */
/* The VU record's memory every block, every platform (issue 179: 1024 rows
   held, 96 MB over two frame slots, on a phone under its free-memory floor
   whose pictures needed at most 256): what it holds, what the block's
   pictures needed, its growths in play, the upload rings beside it (the
   words: rd_early_plan.h rd_vu_record_memory_text) */
static void window_vu_record_line(void)
{
    RdVuRecMem m;
    rd_vu_record_memory(&m);
    if (m.heldRows == 0 && m.runNeedRows == 0) {
        return; /* no pre-pass ran yet */
    }
    char line[400];
    (void)rd_vu_record_memory_text(line, sizeof line, &m);
    fprintf(stderr, "%s\n", line);
}

static void memory_block_line(const RhiStats *st)
{
    WindowMemBlock b;
    if (!window_stall_memory_block(&b)) {
        return;
    }
    char floorText[96] = "";
    if (b.minAvailMb >= 0 && b.floorMb > 0) {
        snprintf(floorText, sizeof floorText,
                 " against the floor %ld MB (under it at %u of %u reads)", b.floorMb, b.underFloor,
                 b.reads);
    }
    char swapText[96] = "; swapped out not read";
    if (b.swapLo >= 0) {
        snprintf(swapText, sizeof swapText, "; swapped out %ld-%ld MB (VmSwap: still allocated)",
                 b.swapLo, b.swapHi);
    }
    fprintf(stderr,
            "window: memory in the block: the device's least %ld MB available%s; the process "
            "%ld-%ld MB resident: anonymous %ld-%ld MB, file-backed %ld-%ld MB, shared %ld-%ld MB "
            "(/proc/self/status, once a second)%s; the C heap %.0f MB in use and %.0f MB mapped "
            "(at the last read); beside them the graphics allocations %.0f MB, the dumps' capture "
            "blocks %.1f MB, the loss dump's frame copies %.1f MB\n",
            b.minAvailMb, floorText, b.lo[0], b.hi[0], b.lo[1], b.hi[1], b.lo[2], b.hi[2], b.lo[3],
            b.hi[3], swapText, b.heapMb, b.mappedMb, (double)st->memoryLiveBytes / 1048576.0,
            (double)ico_dump_auto_held_bytes() / 1048576.0,
            (double)rd_loss_keep_held_bytes() / 1048576.0);
}

void window_memory_line(void)
{
    /* the graphics allocations against the device's limit (a
       phone GPU allows 4096) and the process's memory (a phone ends a
       process that holds too much without a word) */
    RhiStats st;
    long rss = -1, peak = -1, avail = -1, total = -1;

    window_log_cost_mark("the passes, walk and pixel lines"); /* before it (log_cost.h) */

    char gfx[160] = "graphics allocations not counted";
    char dev[96] = "; device memory unknown";

    rhi_get_stats(&st);
    /* the log thread's last reads where it makes them (window_stall.c:
       /proc off the game thread), else here */
    double age = -1.0;
    const int cell = window_stall_memory(&rss, &peak, &avail, &total, &age);
    if (cell != 0) {
        ico_diag_process_memory(&rss, &peak);
    }
    /* and the device's (issue 128: what the system has left decides
       when it ends the app, not the app's own size) */
    if ((cell == 0 && avail >= 0) || (cell != 0 && ico_diag_device_memory(&avail, &total) == 0)) {
        snprintf(dev, sizeof(dev), "; device %ld MB available of %ld MB", avail / 1024,
                 total / 1024);
    }
    if (st.memoryLimit != 0) { /* Vulkan counts them */
        snprintf(gfx, sizeof(gfx),
                 "%llu graphics allocations alive (most %llu; the device allows %llu), "
                 "%.0f MB (most %.0f MB)",
                 (unsigned long long)st.memoryLive, (unsigned long long)st.memoryPeak,
                 (unsigned long long)st.memoryLimit, (double)st.memoryLiveBytes / 1048576.0,
                 (double)st.memoryPeakBytes / 1048576.0);
    }
    /* the RHI's pipeline pool (both backends): the most alive at once
       against its size, and the creations that failed (a full pool's
       and the driver's), so a log shows a pool that ran out */
    char pipes[160];
    snprintf(pipes, sizeof(pipes),
             "; pipelines %llu alive (most %llu of the pool's %llu), %llu creations failed "
             "(%llu for a full pool)",
             (unsigned long long)st.pipelinesLive, (unsigned long long)st.pipelinesPeak,
             (unsigned long long)st.pipelinePoolSize,
             (unsigned long long)(st.pipelineFailures + st.pipelinePoolFull),
             (unsigned long long)st.pipelinePoolFull);
    /* Direct3D 11 and 12: the card's memory against the budget the system
       gives this process (issue 126: a full card pages, the GPU then waits) */
    char vmem[96] = "";
    unsigned long long use, budget;

    if (video_memory(&st, &use, &budget)) {
        snprintf(vmem, sizeof(vmem), "; video memory %llu MB of a budget of %llu MB (local)", use,
                 budget);
    }
    window_video_memory_watch();
    s_pagingWarned = false; /* the next block says it again */
    window_log_cost_part(LOG_COST_MEMORY);
    if (rss >= 0) {
        fprintf(stderr, "window: memory: %s; the process holds %ld MB (most %ld MB)%s%s%s\n", gfx,
                rss / 1024, (peak >= 0 ? peak : rss) / 1024, dev, pipes, vmem);
    } else {
        fprintf(stderr, "window: memory: %s%s%s\n", gfx, pipes, vmem);
    }
    memory_block_line(&st);
    window_vu_record_line();
}

/* a step this late past its period with sound held back is named */
#define HOLD_LATE_MS 100.0

/* ------------------------------------------- the logging's own cost
 * (log_cost.h): the block's events and the log's writes, one line at the
 * block's end */
static LogCost s_logCost;
static int s_logCostInit;
static double s_logPrintT0;
static long long s_logBytesAt = -1;

static LogCost *log_cost(void)
{
    if (!s_logCostInit) {
        log_cost_reset(&s_logCost);
        s_logCostInit = 1;
    }
    return &s_logCost;
}

/* the log's bytes so far, every thread's (-1 not known): the file's
   offset where stderr is one; Android's are the log thread's count */
static long long log_bytes_now(void)
{
#if defined(__ANDROID__)
    return -1;
#elif defined(_WIN32)
    return (long long)_ftelli64(stderr);
#else
    struct stat st;

    if (fstat(STDERR_FILENO, &st) != 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }
    const off_t at = lseek(STDERR_FILENO, 0, SEEK_CUR);
    return at >= 0 ? (long long)at : -1;
#endif
}

static void window_log_cost_late_span(double t0Ms, double t1Ms)
{
    log_cost_late(log_cost(), t0Ms, t1Ms);
}

void window_log_cost_present(double t0Ms, double t1Ms)
{
    log_cost_present(log_cost(), t0Ms, t1Ms);
}

void window_log_cost_job(double gameMs, double t0Ms, double t1Ms)
{
    log_cost_job(log_cost(), gameMs, t0Ms, t1Ms);
}

static double s_logPartAt; /* the last part's end, 0 outside the print */

void window_log_cost_begin(void)
{
    LogCost *c = log_cost();
    s_logPrintT0 = s_logPartAt = ico_thread_now_ms();
    c->partValid = 1;
}

void window_log_cost_mark(const char *what)
{
    if (s_logPartAt <= 0.0) {
        return; /* outside the block's print */
    }
    const double now = ico_thread_now_ms();
    s_logCost.partMs[LOG_COST_OTHER] += now - s_logPartAt;
    log_cost_other(&s_logCost, what, now - s_logPartAt);
    s_logPartAt = now;
}

void window_log_cost_part(int part)
{
    if (s_logPartAt <= 0.0 || part < 0 || part >= LOG_COST_PARTS) {
        return; /* outside the block's print */
    }
    const double now = ico_thread_now_ms();
    s_logCost.partMs[part] += now - s_logPartAt;
    s_logPartAt = now;
}

void window_log_cost_end(void)
{
    LogCost *c = log_cost();
    const double t1 = ico_thread_now_ms();
    char text[1024];

    window_log_cost_mark("the rest"); /* the rest of the print */
    s_logPartAt = 0.0;
    c->asks = window_stall_asks_take(&c->askThermalMs, &c->askMemoryMs);
    if (s_logPrintT0 > 0.0 && t1 >= s_logPrintT0) {
        c->printValid = 1;
        c->printMs = t1 - s_logPrintT0;
        c->print.t0 = s_logPrintT0;
        c->print.t1 = t1;
    }
#ifdef __ANDROID__
    IcoLogMirrorStats m;

    if (ico_android_log_mirror_take(&m) == 0) {
        c->bytes = (long long)m.bytes;
        log_cost_writes(c, m.writes, m.writeMs, m.longestMs, m.longestFamily, m.logcatMs);
        for (unsigned i = 0; i < m.slowN; i++) {
            log_cost_slow_write(c, m.slowT0[i], m.slowT1[i]);
        }
        c->slow += m.slow > m.slowN ? m.slow - m.slowN : 0u; /* counted past the kept */
        log_cost_pipe(c, (m.pipeMax + 1023u) / 1024u, m.pipeCap / 1024u);
    }
#else
    const long long at = log_bytes_now();

    c->bytes = at >= 0 && s_logBytesAt >= 0 && at >= s_logBytesAt ? at - s_logBytesAt : -1;
#endif
    log_cost_text(c, text, sizeof(text));
    fprintf(stderr, "window: %s\n", text);
    log_cost_reset(c);
    s_logBytesAt = log_bytes_now(); /* the next block's bytes from here */
    s_logPrintT0 = 0.0;
}

void window_hold_line(double lateMs, unsigned long long heldMs, unsigned decodes0, unsigned pipes0)
{
    static unsigned long long s_held; /* heldMs at the last call */
    static unsigned s_events;
    const unsigned long long held = heldMs > s_held ? heldMs - s_held : 0u;
    IcoStepProfile p;

    s_held = heldMs;
    if (lateMs >= LOG_COST_LATE_MS) {
        /* the logging's cost line compares its slow writes with it */
        const double now = ico_thread_now_ms();

        window_log_cost_late_span(now - lateMs, now);
    }
    if (lateMs > HOLD_LATE_MS) {
        /* the stopped set's background batches wait (rd.h) */
        rd_pipelines_background_hold();
    }
    if (lateMs <= HOLD_LATE_MS || held == 0) {
        return;
    }
    s_events++;
    if (s_events > 8 && s_events % 100 != 0) {
        return;
    }
    ico_host_step_profile(&p);
    const RdTexCacheStats *tc = rdtex_stats();
    const RdStats *rs = rd_get_stats();
    fprintf(stderr,
            "window: the game step was late by %.0f ms (loading: %u disc reads of %u sectors, %u "
            "texture decodes, %u pipelines, achievements %.1f ms): the sound was held back for "
            "%llu ms (%u such steps)\n",
            lateMs, p.cdReads, p.cdSectors, tc != NULL ? tc->decodes - decodes0 : 0u,
            rs != NULL ? rs->pipelineCreates - pipes0 : 0u, p.achMs, held, s_events);
}

/* a block that held this much sound back with no step past HOLD_LATE_MS */
#define HOLD_BLOCK_MS 200ull

void window_hold_block_line(unsigned long long heldMs, double stepAvgMs, double stepMaxLateMs,
                            double periodMs, double pictureMs, const char *rateWords)
{
    static unsigned s_blocks;

    if (heldMs <= HOLD_BLOCK_MS || stepMaxLateMs > HOLD_LATE_MS) {
        return;
    }
    s_blocks++;
    if (s_blocks > 8 && s_blocks % 30 != 0) {
        return;
    }
    fprintf(stderr,
            "window: the game step was late (steady: steps average %.1f ms against a period of "
            "%.1f ms at %s (the picture takes %.1f ms on this GPU)): the sound was held back for "
            "%llu ms in this block (%u such blocks)\n",
            stepAvgMs, periodMs, rateWords, pictureMs, heldMs, s_blocks);
}

/* Frame rate Original's counts for the block (window_orig_*): the steps
   late, the frames not presented by reason */
static unsigned s_origLate, s_origSkipLate, s_origSkipBusy;
static unsigned long long s_heldAt; /* ico_audio_sdl_held_ms at the last block's end */

/* the simulation's period at hz (host_loop.c's: PAL 20 ms, NTSC 16.683 ms) */
static uint64_t period_ns(int hz)
{
    return hz == 50 ? 20000000ull : 16683333ull;
}

int window_orig_gate(int keep, bool busy, uint64_t nowNs, uint64_t dueNs, int hz)
{
    switch (pace_original_gate(keep != 0, busy, nowNs, dueNs, period_ns(hz))) {
    case PACE_ORIG_LATE:
        s_origSkipLate++;
        return RD_GATE_LATE;
    case PACE_ORIG_BUSY:
        s_origSkipBusy++;
        return RD_GATE_BUSY;
    default:
        return RD_GATE_PRESENT;
    }
}

void window_orig_step(bool late)
{
    s_origLate += late ? 1u : 0u;
}

unsigned window_orig_skip_busy(void)
{
    return s_origSkipBusy;
}

const char *window_orig_words(bool original)
{
    static char words[128];

    words[0] = '\0';
    if (original) {
        snprintf(words, sizeof(words),
                 "; steps late %u (frames not presented %u: the step late %u, the render thread "
                 "busy %u)",
                 s_origLate, s_origSkipLate + s_origSkipBusy, s_origSkipLate, s_origSkipBusy);
    }
    return words;
}

void window_hold_block(double stepSumMs, unsigned steps, double stepMaxMs, int hz,
                       double replaySumMs, unsigned replays, bool original, const char *rateName)
{
    const unsigned long long held = ico_audio_sdl_held_ms();
    const double periodMs = (double)period_ns(hz) / 1e6;
    /* the replays' block mean, the submit thread's submit and present
       included (rd_perf.c rd__perf_submit_times): at Original the picture
       is handed over, its time no longer the step's own */
    const double picture = replays ? replaySumMs / replays : 0.0;

    window_hold_block_line(held > s_heldAt ? held - s_heldAt : 0u, steps ? stepSumMs / steps : 0.0,
                           stepMaxMs - periodMs, periodMs, picture,
                           original ? "Original" : rateName);
    s_heldAt = held;
    s_origLate = s_origSkipLate = s_origSkipBusy = 0;
}

const char *window_tiler_words(void)
{
    return window_scene_tiler_words(rhi_limits()->tiler); /* window_scene.c: the text */
}
