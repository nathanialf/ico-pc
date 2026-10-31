/*
 * port/platform/window_disc.h
 *
 * The window's side of the disc reader (port/data/cdvd_host.h): the step's
 * wait for the disc that the present allowance excuses (pace_policy.h, the
 * disc's excuse) and the 10 s disc line.  Its own file because
 * window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_DISC_H
#define ICO_PLATFORM_WINDOW_DISC_H

#include <SDL3/SDL.h>
#include "pace_policy.h"

/* ico_window_pace after each simulation step: the step's disc reads, the
   reads from the read-ahead and the game thread's wait for the disc
   (ico_host_step_profile), added to the 10 s block's sums; a pace with no
   step since the last (the same vsync count) waited for nothing */
void window_disc_step(void);
/* The last step's wait for the disc in ns (0: none), for pace_budget_step
   and PaceLock.discWaitNs, and the drop line's ", disc wait" */
Uint64 window_disc_wait_ns(void);
/* pace_log at the 10 s line, with the allowance in use: "window: disc: 23
   reads (736 KB), 19 from read-ahead; the game thread waited 118 ms in 4
   steps (longest 91 ms); 3 late steps excused by disc waits", nothing in a
   block without disc reads; the block's sums cleared */
void window_disc_block(const PaceBudget *b);

#endif
