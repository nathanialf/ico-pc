/*
 * port/platform/window_pipe_finish.c
 *
 * The free-memory floor's driver class and the background finisher at the
 * window's start (window_pipe_finish.h).
 */
#include "window_pipe_finish.h"

#include <stdio.h>
#include <SDL3/SDL_filesystem.h>
#include "diag_host.h"
#include "memory_floor.h"
#include "rd.h"
#include "rhi.h"

void window_pipe_floor_driver(void)
{
    const RhiLimits *l = rhi_limits();

    ico_diag_set_memory_floor_driver(l != NULL ? l->vendorId : 0u,
                                     l != NULL ? l->driverText : NULL);
}

void window_pipe_finish_start(void)
{
    const int driver = ico_diag_memory_floor_driver();

    if (!rd_pipelines_lazy_on() || !ico_diag_floor_stopped()) {
        return;
    }
    if (driver != ICO_MEMORY_FLOOR_DRIVER_OTHER) {
        fprintf(stderr,
                "window: pipelines: the start-up set stopped at the free-memory floor on %s: the "
                "rest is made on first use, not in the background (issue 161: that driver's "
                "compiler ran the device out of memory)\n",
                ico_memory_floor_driver_words(driver));
        return;
    }
    (void)rd_pipelines_background_start(ico_diag_background_memory);
}

/* the cache file's size in bytes, -1 when there is none */
static long long cache_file_bytes(const char *path)
{
    SDL_PathInfo info;

    if (path == NULL || path[0] == '\0' || !SDL_GetPathInfo(path, &info) ||
        info.type != SDL_PATHTYPE_FILE) {
        return -1;
    }
    return (long long)info.size;
}

int window_pipe_floor_retry(int pipePolicy)
{
    char why[200];
    const char *path = ico_diag_pipeline_cache_path();
    const long long bytes = cache_file_bytes(path);
    int retry;

    if (pipePolicy != ICO_DIAG_PIPESET_LAZY_LOW_MEMORY || !ico_diag_startup_prev_floor_stop()) {
        return pipePolicy;
    }
    retry = ico_memory_floor_retry(1, ico_diag_startup_prev_floor_retried(),
                                   ico_diag_memory_floor_driver(), bytes,
                                   rhi_pipeline_cache_loaded() ? 1 : 0, why, sizeof why);
    fprintf(stderr,
            "window: pipelines: the previous start stopped at the free-memory floor; %s (%s, %lld "
            "bytes, loaded by the renderer: %s): %s\n",
            retry ? "this start makes the set again" : "this start makes no set", why, bytes,
            rhi_pipeline_cache_loaded() ? "yes" : "no", path != NULL && path[0] ? path : "no path");
    if (!retry) {
        return pipePolicy;
    }
    ico_diag_startup_floor_retry();
    return ICO_DIAG_PIPESET_FULL;
}
