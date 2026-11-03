/*
 * port/platform/render_thread.h
 *
 * The window's render thread (window build; docs/port/PLATFORM.md "The
 * render thread").  The game thread records the frames; the render thread
 * makes every picture: the present whole, the interpolation, the walk, the
 * submit and the present (port/render/rd_thread.h, through the RdThreadHost
 * this file installs), and runs the renderer's calls between pictures.
 * Every backend: with OpenGL the context is this thread's for its jobs and
 * a game-thread RHI call takes it over (the call guard's wait).
 *
 * The rules: while the render thread works on a job, every RHI call from
 * another thread that changes the device's state waits for it first
 * (rhi.h rhi_set_call_guard), so the backend never runs on two threads at
 * once; the renderer's own rules (the hand-over of its state, one picture
 * at a time, the retire list) are rd_thread.h's; the window's surface is
 * made again with the render thread parked (the lifecycle table's park
 * step).  The state the two share is frame_mailbox.h's, under one mutex.
 * The render thread has an 8 MB stack and runs at SDL's high priority.
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

/* The game thread's plan for a further present of a frame already shown
   (window_present.c; pace_policy.h pace_rt_further_hold): when the render
   thread's picture in progress started (sinceNs, SDL_GetTicksNS's clock; 0:
   none), and the estimates of how long a picture handed over holds the
   renderer's state (readsNs: the step's calls wait for its reads), takes in
   all (pictureNs: the next present waits for it) and waits in its present
   call (presentNs: the display's or the GPU's backlog); and of the reads
   the render thread's own (cpuNs: less the waits for the slot's fence, the
   submit thread and the acquire), the interpolation before the first
   graphics call (interpNs) and the submit thread's job (submitNs, 0 with
   none), by which the game thread predicts a further picture's reads
   (pace_policy.h PaceRtPipe).  False with the thread off or in sync mode
   (every picture waited for: the present's own cost holds it) and before
   the first picture handed over. */
typedef struct IcoRtPlan {
    uint64_t sinceNs, readsNs, pictureNs, presentNs;
    uint64_t cpuNs, interpNs, submitNs;
} IcoRtPlan;

bool ico_render_thread_plan(IcoRtPlan *out);
/* A further present held back by that plan (why: pace_policy.h
   PaceRtHold, not PACE_RT_GO), counted for the 10 s line */
void ico_render_thread_note_held(int why);
/* One game step on the timer path, for the 10 s line (window_present.c):
   its first renderer call waited readWaitNs for a picture's reads (0:
   none: not started late by the pictures), it missed its period (ended
   missNs past its deadline), it was planned at the whole period after a
   miss (backedOff: pace_policy.h PaceStepEst), the step's time the rule
   plans further presents by (planNs) */
void ico_render_thread_note_step(uint64_t readWaitNs, bool missed, uint64_t missNs, bool backedOff,
                                 uint64_t planNs);

/* The 10 s line (window_host.c window_pace_log): the game thread's mean
   step and pump per call in ms, its own cost of closing a frame
   (rd_frame_close_take), the slot waits it made (waits for the render
   thread, split by whose: a new frame's present, a further present's, the
   rest) and the read waits, then the render thread's block with its CPU
   time a picture (pictureCpuMs: the window's replays line):
     window: game thread: step 3.1 ms (closing a frame 0.4 ms of it, longest 1.2 ms, 300
     frames), pump 0.4 ms, slot waits 12 (8.2 ms; 9 (7.9 ms) by a new frame's present, 2
     (0.3 ms) by a further present's, 1 other), read waits 0 (0.0 ms, longest 0.0 ms);
     render thread: CPU 9.3 ms a picture, idle 71 %, waited for a frame 14.2 ms, heartbeat
     max 4.9 ms (590 pictures); ...; 12 further presents held back (12 would have made the
     step miss its period, 0 while a picture ran past its estimate, 0 while the presents
     waited for the display or the GPU; a picture holds the renderer for 11.5 ms (10.9 ms
     of its own, 3.2 ms before its first graphics call), takes 11.6 ms, its present waits
     0.3 ms, the submit thread's part 3.2 ms; the step planned at 6.1 ms); steps started
     late by the pictures: 280 (longest 9.6 ms); steps that missed their period: 0 (latest
     0.0 ms past it; 0 steps planned at the whole period after them)
   or "...; render thread: off (CPU 9.3 ms a picture on the game thread)" with no thread */
void ico_render_thread_block_line(double stepMs, double pictureCpuMs, double pumpMs);

#endif /* ICO_PLATFORM_RENDER_THREAD_H */
