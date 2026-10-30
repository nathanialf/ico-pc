/*
 * port/platform/window_lines.c
 *
 * Log line writers of the window's 10 s block (window_lines.h), moved out of
 * window_host.c.
 */
#include "window_lines.h"

#include <stdio.h>
#include "diag_host.h"
#include "host_loop.h"
#include "rd_tex.h"
#include "rhi.h"

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
    if (rss >= 0) {
        fprintf(stderr, "window: memory: %s; the process holds %ld MB (most %ld MB)%s%s%s\n", gfx,
                rss / 1024, (peak >= 0 ? peak : rss) / 1024, dev, pipes, vmem);
    } else {
        fprintf(stderr, "window: memory: %s%s%s\n", gfx, pipes, vmem);
    }
}

/* a step this late past its period with sound held back is named */
#define HOLD_LATE_MS 100.0

void window_hold_line(double lateMs, unsigned long long heldMs, unsigned decodes0, unsigned pipes0)
{
    static unsigned long long s_held; /* heldMs at the last call */
    static unsigned s_events;
    const unsigned long long held = heldMs > s_held ? heldMs - s_held : 0u;
    IcoStepProfile p;

    s_held = heldMs;
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
