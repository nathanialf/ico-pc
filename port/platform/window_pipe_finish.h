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

/* After rd_init made the device, before the self-tests and the start-up
   set: the floor's driver class from the device (rhi_limits' vendor id and
   driver text; diag_host.h ico_diag_set_memory_floor_driver), and whether
   its compiles run one at a time (RhiLimits.serialPipelines; diag_host.h
   ico_diag_set_pipe_compiler_serial: the longer pipeline limit, issue 188) */
void window_pipe_floor_driver(void);
/* After the start-up set and ico_diag_startup_activity_budget_stop: a set
   that stopped at the free-memory floor (not at the system's signal) on
   another driver than Qualcomm's own is finished in the background
   (rd_pipelines_background_start with ico_diag_background_memory); on
   Qualcomm's own driver the rest stays to first use (issue 161: the floor
   there guards against that driver's compiler, which a background set
   would run again).  One line either way. */
void window_pipe_finish_start(void);

/* After window_pipe_floor_driver, before the set: pipePolicy as the window
   has it; on Qualcomm's own driver a ICO_DIAG_PIPESET_LAZY_LOW_MEMORY start
   whose previous start stopped at the free-memory floor becomes
   ICO_DIAG_PIPESET_FULL (the careful set where the device table asks for
   it) when this shader set's pipeline cache file exists and is not empty
   (memory_floor.h ico_memory_floor_retry).  One line, the decision and its
   reason, whenever the previous start stopped at the floor.  Returns the
   policy to use. */
int window_pipe_floor_retry(int pipePolicy);

/* After the start-up set (or the lazy start's self-tests), before the
   render thread starts: the skin check's background compiles under issue
   161's rule (rd.h rd_vu_skin_background): the memory function
   ico_diag_background_memory, and none at all when this start's set
   stopped at the free-memory floor or the start makes no set because the
   last one stopped for memory (pipePolicy ICO_DIAG_PIPESET_LAZY_LOW_MEMORY;
   -1 for none: the backend switch) on a driver other than
   ICO_MEMORY_FLOOR_DRIVER_OTHER, as window_pipe_finish_start rules */
void window_pipe_skin_check(int pipePolicy);

#endif
