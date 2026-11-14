/*
 * port/platform/window_clock_trap.h
 *
 * The window's side of the clock lift and the worst second (clock_trap.h):
 * every call on the game thread (the records' drain, the step rule's asks,
 * the 10 s block), no lock.  The lift itself is the hint thread's call
 * (window_perf_hint.c); off Android the seconds and the worst-second line
 * are kept, nothing is asked of the system and no clock is sampled.
 *
 * The lines:
 *   window: clock lift asked: 30 pictures for 30 game frames in a second (one
 *     a tick), the step rule held 41 of 55 further pictures asked, a
 *     picture's CPU 22.6 ms: the system told of a heavier CPU and GPU load
 *     (the first 8, then every 100th)
 *   window: worst second of the block: ... (every 10 s block)
 *   "; the CPU clocks under their level: ..." at the end of the ranked line
 *     (window_perf_rank.c) when a cluster's clock dipped
 */
#ifndef ICO_PLATFORM_WINDOW_CLOCK_TRAP_H
#define ICO_PLATFORM_WINDOW_CLOCK_TRAP_H

#include <stdbool.h>
#include <stddef.h>

struct RdPerfRecord;

/* one drained record (window_perf_hint.c window_perf_hint_picture): a
   presented picture into its second */
void window_clock_trap_picture(const struct RdPerfRecord *r);
/* one ask of the step rule (render_thread.c ico_render_thread_note_ask):
   held by it, on the as-made path */
void window_clock_trap_ask(bool held, bool asMade);
/* the ranked line's clause appended to line (cap its whole size) */
void window_clock_trap_ranked(char *line, size_t cap);
/* the 10 s block's line (window_perf_hint.c window_perf_hint_block) */
void window_clock_trap_block(void);

#endif /* ICO_PLATFORM_WINDOW_CLOCK_TRAP_H */
