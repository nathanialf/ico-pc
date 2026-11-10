/*
 * port/platform/window_dump_auto.h
 *
 * The window's side of the automatic dumps (dump_auto.h; issues 123 and
 * 179), kept out of window_host.c (at its file size gate): what the game
 * thread snapshots for the skin check's capture and the slow-scene dump,
 * and the lines it prints when the worker is done.
 */
#ifndef ICO_PLATFORM_WINDOW_DUMP_AUTO_H
#define ICO_PLATFORM_WINDOW_DUMP_AUTO_H

#include <stdint.h>
#include "window_scene.h"

/* The skin check's capture (rd.h rd_vu_skin_capture_due), polled every
   frame: once the check has failed, the frame and the tick before in
   memory, the sidecar and the log to the worker. Held until the worker is
   free when it is writing a slow-scene zip. */
void window_dump_auto_skin_pump(void);

/* The slow-scene dump of frame `frame` (window_host.c slow_scene_write):
   the frame in memory and the text line for the worker's result.
   Returns 1 when the job was handed over, 0 when it was dropped or the
   frame could not be taken (said in the log). */
int window_dump_auto_slow(uint32_t frame, const SlowSceneBlock *b, double work, double meanWork);

/* Every vsync: the worker's finished job, if any, as its log lines (the
   game thread's cost, the worker's) and, for a slow-scene zip, noted for the
   crash report. */
void window_dump_auto_report(void);

/* The end of the run: the worker joined, its last result reported */
void window_dump_auto_stop(void);

#endif /* ICO_PLATFORM_WINDOW_DUMP_AUTO_H */
