/*
 * port/platform/window_lines.h
 *
 * Log line writers of the window's 10 s block (window_host.c is at its file
 * size gate): the slow-replay line and the memory line.  They read the
 * renderer's records and the RHI's counters only; no window state.
 */
#ifndef ICO_PLATFORM_WINDOW_LINES_H
#define ICO_PLATFORM_WINDOW_LINES_H

#include "rd.h"

/* A replay over a refresh: where its time went, with its own counters.
   `note` ends the line ("" or "; the block's longest, ...") */
void window_slow_replay_line(const RdPerfRecord *r, unsigned vsync, const char *note);

/* The block's "window: memory:" line: the graphics allocations, the
   process's and the device's memory, the pipeline pool */
void window_memory_line(void);

#endif /* ICO_PLATFORM_WINDOW_LINES_H */
