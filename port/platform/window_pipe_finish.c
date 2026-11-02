/*
 * port/platform/window_pipe_finish.c
 *
 * The free-memory floor's driver class and the background finisher at the
 * window's start (window_pipe_finish.h).
 */
#include "window_pipe_finish.h"

#include <stdio.h>
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
