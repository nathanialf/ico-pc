/*
 * port/platform/window_lines.c
 *
 * Log line writers of the window's 10 s block (window_lines.h), moved out of
 * window_host.c.
 */
#include "window_lines.h"

#include <stdio.h>
#include "diag_host.h"
#include "rhi.h"

/* A replay over a refresh (it cannot fit a slot): where its time went,
   with its own counters; at most SLOW_REPLAY_LINES a block (the block's
   first line counts them all), and the block's longest at its end when the
   cap kept it out (window_host.c slow_replay_longest).  The records arrive
   two replays late (rd_perf.c), so the vsync is the one the record was
   drained at. */
void window_slow_replay_line(const RdPerfRecord *r, unsigned vsync, const char *note)
{
    char gpu[32] = "no GPU timestamps";

    if (r->gpuValid) {
        snprintf(gpu, sizeof(gpu), "GPU %.1f ms", r->gpuMs);
    }
    fprintf(stderr,
            "window: slow replay %.1f ms (replay %u, frame %u, alpha %.2f, about vsync %u): interp "
            "%.1f, wait %.1f (fence %.1f), acquire %.1f, upload %.1f, walk %.1f, bind %.1f, "
            "submit %.1f, present %.1f; %u textures and %u meshes (%u KB) uploaded (%u KB in "
            "all), %u pipelines created, %u allocations, %u buffers and %u textures created, %u "
            "DATE snapshots; %s%s\n",
            r->totalMs, r->replay, r->frame, (double)r->alpha, vsync, r->interpMs, r->waitMs,
            r->fenceWaitMs, r->acquireMs, r->uploadMs, r->walkMs, r->bindMs, r->submitMs,
            r->presentMs, r->textureUploads, r->meshUploads, (unsigned)(r->meshUploadBytes / 1024u),
            (unsigned)(r->uploadBytes / 1024u), r->pipelineCreates, r->memoryAllocs,
            r->buffersCreated, r->texturesCreated, r->dateSnapshots, gpu, note);
}

/* the graphics allocations against the device's limit (a phone GPU allows
   4096) and the process's memory (a phone ends a process that holds too
   much without a word) */
void window_memory_line(void)
{
    /* the graphics allocations against the device's limit (a
       phone GPU allows 4096) and the process's memory (a phone ends a
       process that holds too much without a word) */
    RhiStats st;
    long rss = -1, peak = -1, avail = -1, total = -1;

    char gfx[160] = "graphics allocations not counted";
    char dev[96] = "; device memory unknown";

    rhi_get_stats(&st);
    ico_diag_process_memory(&rss, &peak);
    /* and the device's (issue 128: what the system has left decides
       when it ends the app, not the app's own size) */
    if (ico_diag_device_memory(&avail, &total) == 0) {
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
    if (rss >= 0) {
        fprintf(stderr, "window: memory: %s; the process holds %ld MB (most %ld MB)%s%s\n", gfx,
                rss / 1024, (peak >= 0 ? peak : rss) / 1024, dev, pipes);
    } else {
        fprintf(stderr, "window: memory: %s%s\n", gfx, pipes);
    }
}
