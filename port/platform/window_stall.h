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
 *   - the record popped next (its line from a present of 250 ms on,
 *     present_hist.h PRESENT_STALL_LINE_MS, as the around and
 *     stalled-replay lines: each the first 8 then every 100th, which the
 *     50-250 ms presents would otherwise spend; its gap from 50 ms on):
 *     "window: after the present that blocked (replay N, 352 ms): replay
 *     N+1 waited ... for its frame slot, acquired its image in ..., GPU
 *     span ..., the GPU idle ... before it"
 *     (the first 8, then every 100th), its gap kept for the 10 s
 *     'GPU between replays' line ("across the 3 stalls: the GPU was idle
 *     ... before the next replay");
 *   - from the first stall on Vulkan (a record with the present split)
 *     every replay timed pass by pass, and a stalled replay's passes
 *     against the block's: "window: stalled replay N (present ..., GPU
 *     ... against the block's ...): ...";
 *   - on Android, the kernel GPU driver's clock, load, resets and thermal
 *     level sampled every 250 ms (gpu_sysfs.h): which nodes were readable
 *     at the open, the 10 s block's line, and the samples before, during
 *     and after each present that blocked;
 *   - on Android, the app's own thermal headroom and status and whether
 *     the screen is being recorded (thermal_text.h, host_android.h
 *     ico_host_thermal): the block's "window: thermal: ..." line after the
 *     driver's, and the end of each around line.
 * The texts are present_hist.c's and rd_perf.c's (pure, present_hist_test
 * and rd_perf_test's checkPassTexts).  Everything
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
   the after line (from PRESENT_STALL_LINE_MS on) and its gap noted, and
   its around line is queued (from PRESENT_STALL_LINE_MS on); the first one
   with a present split (the Vulkan backend, rd_perf_passes_arm_backend)
   also times every replay pass by pass from here (rd_perf_passes_arm, its
   line once naming the backend), so the next stall's replay has its
   passes */
void window_stall_blocked(const RdPerfRecord *r);
/* Every popped record, before window_stall_blocked of the same record:
   when the record before it blocked, the after line (from
   PRESENT_STALL_LINE_MS on), and its gap into the
   block's stall gaps (pace_gpu_gap_stall: the 10 s 'GPU between replays'
   line's "across the N stalls") */
void window_stall_after(const RdPerfRecord *r, PaceGpuGap *gap);
/* A timed replay's pieces p (n) before they join the block's agg: when its
   present blocked PRESENT_STALL_LINE_MS or more, "window: stalled replay
   N (present ..., GPU ... against the block's ...): <its top passes
   against the block's means>; ..."
   (rd.h rd_perf_pass_grow_text; the first 8, then every 100th).
   blockGpuN, blockGpuMs as window_stall_split's */
void window_stall_passes(const RdPerfRecord *r, const RdPerfPassAgg *agg, const RdPerfPass *p,
                         uint32_t n, unsigned blockGpuN, double blockGpuMs);

/* The kernel GPU driver's nodes (gpu_sysfs.h; Android only, nothing
   elsewhere): at the window's open, once a run, the line saying which were
   readable ("window: kernel GPU driver: /sys/class/kgsl/kgsl-3d0: readable
   ..." or "... not readable (EACCES)") and, when any is, the sampler's
   own thread started (again at each later open) */
void window_stall_gpu_open(void);
/* The window's close: the sampler's thread stopped and joined (Android) */
void window_stall_gpu_close(void);
/* The 10 s block's line ("window: kernel GPU driver: clock A-B MHz ..."),
   when a node is sampled */
void window_stall_gpu_block(void);
/* At each drain: the around lines of the presents that blocked whose
   sample after them came (or that waited a second for it): "window: kernel
   GPU driver around the present that blocked (replay N): 0.12 s before:
   clock 905 MHz, busy 74 %, resets 3; 0.09 s after: ..." (the first 8, then
   every 100th) */
void window_stall_poll(void);

#endif /* ICO_PLATFORM_WINDOW_STALL_H */
