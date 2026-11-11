/*
 * port/platform/window_perf_hint.h
 *
 * The window's side of Android's performance hints (perf_hint.h; docs/port/
 * PLATFORM.md "Performance hints"): the hint thread ("ico-perf-hint", which
 * makes every call on the session, at the system's preferred update rate),
 * the target from what the pacer plans, the threads' ids, each presented
 * picture's work from its performance record, the lifecycle and the 10 s
 * line.  Every call here is on the game thread and costs no lock and no
 * system call there (atomic stores into perf_hint.h's hand-over) except
 * open and close.  Off Android every call does nothing and writes nothing.
 *
 * The lines (Android):
 *   window: performance hints: on (API 35; session of 3 threads: game ...)
 *   window: performance hints: off (API level 31: ...; game mode ...)
 *   window: performance hints: on again (...)               (after the background)
 *   window: performance hints: session closed (the app in the background) ...
 *   window: performance hints: 612 reports (actual CPU ... target changed 0 times)  (10 s)
 */
#ifndef ICO_PLATFORM_WINDOW_PERF_HINT_H
#define ICO_PLATFORM_WINDOW_PERF_HINT_H

#include <stdbool.h>

struct RdPerfRecord;

/* After the render thread's start at the window's open: the NDK's calls
   looked up, the game mode read for the line, the hint thread started (or
   the off line); nothing when already open */
void window_perf_hint_open(void);
/* Each drain of the performance records (window_host.c perf_drain): the
   pacer's target and the threads' ids handed over */
void window_perf_hint_tick(void);
/* One drained record: a presented picture's work handed over */
void window_perf_hint_picture(const struct RdPerfRecord *r);
/* The lifecycle (the render thread's park and unpark): false the session
   closed for the background, true made again */
void window_perf_hint_active(bool active);
/* The 10 s block's line (once a session was made) */
void window_perf_hint_block(void);
/* The window's close: the hint thread joined, the session closed */
void window_perf_hint_close(void);

#endif /* ICO_PLATFORM_WINDOW_PERF_HINT_H */
