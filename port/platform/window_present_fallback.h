/*
 * port/platform/window_present_fallback.h
 *
 * The window's side of the FIFO fallback (port/platform/present_fallback.h
 * has the decision): presents that keep blocking in the driver under
 * mailbox with vsync off make every later swapchain FIFO for the run
 * (rhi.h rhi_present_fifo_fallback).  Run-time state only: the vsync option
 * stays as the player set it and config.toml is not written.  Its own file
 * because window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_PRESENT_FALLBACK_H
#define ICO_PLATFORM_WINDOW_PRESENT_FALLBACK_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

/* ico_window_open, before the backend: [dev] present_fallback
   (ICO_PRESENT_FALLBACK, host_config.c) read, a line for off and fifo */
void window_present_fallback_open(void);
/* The backend's flag as the run has it (video_settings sets it on every
   backend made): the switch taken, or fifo from the start */
bool window_present_fallback_on(void);
/* ", FIFO fallback armed|taken after N blocks|set by [dev]
   present_fallback=fifo" or "" for the present mode line and the summary */
void window_present_fallback_suffix(char *out, size_t size);
/* window_host.c present_blocked: a present's time in the driver's present
   call (over 50 ms) noted at nowNs; the switch's line (the switch itself
   at the step's end) or the kept line once */
void window_present_fallback_note(double presentMs, Uint64 nowNs);
/* ico_window_pace at the step's end: the switch present_blocked asked for,
   the swapchain made again at its own size (rd_resize_output, as
   video_apply on a present mode change) and the present mode line again;
   returns the rebuild's time for the pause (s_pres.pausePendingNs), 0 when
   nothing was pending */
Uint64 window_present_fallback_apply(void);
/* The 10 s present-blocked line's suffix at nowNs ("; K since presents went
   FIFO N s ago", "; k of 3 over 200 ms in 30 s toward FIFO" or "") */
void window_present_fallback_block_suffix(Uint64 nowNs, char *out, size_t size);

#endif
