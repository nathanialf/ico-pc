/*
 * port/platform/window_stall.h
 *
 * Issue 123: the window's lines around a present that blocked in the
 * driver (a present call over 50 ms, window_host.c present_blocked), so
 * one log tells the stall's causes apart (docs/port/DISPLAY.md "The stats
 * lines", the decision table there):
 *   - the blocked line's split (window_fifo_note writes it past 250 ms):
 *     the driver's call, the swapchain lock, the surface poll, the
 *     replay's GPU span against the block's, the GPU idle before it;
 *   - the record popped next: "window: after the present that blocked
 *     (replay N, 352 ms): replay N+1 waited ... for its frame slot,
 *     acquired its image in ..., GPU span ..., the GPU idle ... before it"
 *     (the first 8, then every 100th).
 * The texts are present_hist.c's (pure, present_hist_test).  Everything
 * here runs on the window's thread, in perf_drain, from the records as
 * they pop (rd.h RdPerfRecord): no lock, nothing on the render or submit
 * thread.  Its own file because window_host.c is near its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_STALL_H
#define ICO_PLATFORM_WINDOW_STALL_H

#include <stddef.h>
#include "pace_policy.h"
#include "rd.h"

/* the present calls this long or longer are the stalls these lines follow
   (window_host.c present_blocked's count) */
#define WINDOW_STALL_MS 50.0

/* The blocked line's split for record r (present_hist.h
   present_split_text): blockGpuN timed replays in the block before it,
   their mean GPU span blockGpuMs.  A static buffer, valid to the next
   call */
const char *window_stall_split(const RdPerfRecord *r, unsigned blockGpuN, double blockGpuMs);
/* r's present blocked (over WINDOW_STALL_MS): the next record popped gets
   the after line */
void window_stall_blocked(const RdPerfRecord *r);
/* Every popped record, before window_stall_blocked of the same record:
   when the record before it blocked, the after line */
void window_stall_after(const RdPerfRecord *r);

#endif /* ICO_PLATFORM_WINDOW_STALL_H */
