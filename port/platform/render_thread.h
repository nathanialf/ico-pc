/*
 * port/platform/render_thread.h
 *
 * The window's render thread (window build; docs/port/PLATFORM.md
 * "Threads").  The game thread records each replay's command list; the
 * render thread then ends it, submits it and presents the picture
 * (port/render/rd_submit.c through rd_set_submit_host), so the submit's
 * and the present's time is off the game thread.  Vulkan and Direct3D 12
 * only: OpenGL and Direct3D 11 take every call on one thread
 * (RhiLimits.singleThreadCommands) and keep the game thread's path.
 *
 * The rules: while the render thread works on a picture, every RHI call
 * from another thread that changes the device's state waits for it first
 * (rhi.h rhi_set_call_guard), so the backend never runs on two threads at
 * once; the next replay waits for the last one's submit before it waits its
 * fence (rd__wait_frame); a picture whose capture is armed (F12, a save
 * state's picture) waits for its present; the window's surface is made
 * again with the render thread parked (the lifecycle table's park step).
 * The state the two share is frame_mailbox.h's, under one mutex.  The render
 * thread runs at SDL's high priority.
 */
#ifndef ICO_PLATFORM_RENDER_THREAD_H
#define ICO_PLATFORM_RENDER_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include "frame_mailbox.h"

/* At the window's open, after the start-up set (and after a renderer
   switch, ico_window_reopen): the mode from [dev] render_thread and the
   backend, the thread started, the hooks installed; one line,
     window: render thread: on (submit and present off the game thread; Vulkan, ...)
     window: render thread: off (<why>)
   Returns the mode in force. */
FmMode ico_render_thread_start(void);
/* Every picture handed over has finished, the thread ended and the hooks
   removed (before rd_suspend, rd_shutdown) */
void ico_render_thread_stop(void);
/* The mode in force (FM_MODE_OFF: no thread) */
FmMode ico_render_thread_mode(void);
/* Returns once everything handed over has finished; the ns it waited
   (0: it was idle, or no thread) */
uint64_t ico_render_thread_drain(void);
/* Park: what is queued finishes, then the thread holds until unpark;
   meanwhile pictures are submitted on the calling thread as without it */
void ico_render_thread_park(void);
void ico_render_thread_unpark(void);
/* fn(arg) on the render thread (FM_JOB_PICTURE: a picture's submit and
   present, noted as the last present); wait: returns once it ran.  False
   when not taken (no thread, parked, the queue full): the caller runs it */
bool ico_render_thread_post(void (*fn)(void *arg), void *arg, FmJobKind kind, bool wait);
/* The last picture's present as the render thread noted it (its start and
   end on SDL_GetTicksNS's clock); false before one or with no thread */
bool ico_render_thread_last_present(FmPresent *out);

/* The 10 s line (window_host.c window_pace_log): the game thread's mean
   step, record and pump per call in ms and the slot waits it made (waits
   for the render thread), and the render thread's block:
     window: game thread: step 3.1 ms, record 7.9 ms, pump 0.4 ms, slot waits 12 (8.2 ms);
     render thread: idle 71 %, waited for a frame 14.2 ms, heartbeat max 4.9 ms (590 pictures)
   or "...; render thread: off" with no thread */
void ico_render_thread_block_line(double stepMs, double recordMs, double pumpMs);

#endif /* ICO_PLATFORM_RENDER_THREAD_H */
