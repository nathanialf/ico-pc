/*
 * port/platform/window_lines.h
 *
 * Log line writers of the window's 10 s block (window_host.c is at its file
 * size gate): the slow-replay line, the memory line and the late step's
 * held-sound line.  They read the renderer's records, the RHI's counters
 * and the step's profile only; no window state.
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

/* Once a block: writes the line "video memory N MB of M MB: the driver may
   be paging; ..." the first time the card's local memory use is at 90 % of
   its budget or more in the block (Direct3D 11 and 12; nothing elsewhere).
   Called from the perf drain and at the block's end */
void window_video_memory_watch(void);

/* Every step (window_host.c ico_window_pace): a step that ended more than
   HOLD_LATE_MS late (lateMs: its time past the refresh period) while the
   audio held sound back (heldMs: ico_audio_sdl_held_ms, rising since the
   last call) names what the game was doing in it, from the step's profile
   (host_loop.h) and the decode and pipeline counts since the step began
   (decodes0, pipes0); the first 8 such steps, then every 100th:
     window: the game step was late by 123 ms (loading: 1 disc reads of 64
     sectors, 1 texture decodes, 0 pipelines, achievements 0.1 ms): the
     sound was held back for 98 ms (1 such steps) */
void window_hold_line(double lateMs, unsigned long long heldMs, unsigned decodes0, unsigned pipes0);

#endif /* ICO_PLATFORM_WINDOW_LINES_H */
