/*
 * port/platform/window_pipe_finish.h
 *
 * The start-up set's free-memory floor by driver and the background
 * finisher at the window's start (port/platform/memory_floor.h,
 * port/render/rd.h rd_pipelines_background_start).  Its own file because
 * window_host.c is near its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_PIPE_FINISH_H
#define ICO_PLATFORM_WINDOW_PIPE_FINISH_H

/* After rd_init made the device, before the start-up set: the floor's
   driver class from the device (rhi_limits' vendor id and driver text;
   diag_host.h ico_diag_set_memory_floor_driver) */
void window_pipe_floor_driver(void);
/* After the start-up set and ico_diag_startup_activity_budget_stop: a set
   that stopped at the free-memory floor (not at the system's signal) on
   another driver than Qualcomm's own is finished in the background
   (rd_pipelines_background_start with ico_diag_background_memory); on
   Qualcomm's own driver the rest stays to first use (issue 161: the floor
   there guards against that driver's compiler, which a background set
   would run again).  One line either way. */
void window_pipe_finish_start(void);

#endif
