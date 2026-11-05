/* rd_submit_thread.h: the pictures' submit and present on a thread of
 * their own (docs/port/RENDER.md "The render thread").
 *
 * The render thread (rd_thread.h) makes a picture handed over to it: the
 * interpolation, the fence wait, the acquire, the uploads and the walk,
 * then the list's end, its submit and the present.  With the submit thread
 * the last three leave it: the replay's end (rd__submit_frame) keeps the
 * list open (rd__submit_defer) and the job's end posts it here
 * (rd__submit_post_pending), so the render thread is free for the next
 * picture as soon as its walk ends, and the game thread's next present
 * waits for that much only (the drain of rd_present_post).
 *
 * The rules:
 *   the slots       with the frame slots apart (RhiLimits.slotOverlap:
 *                   Vulkan with the timeline sync; ICO_RD_SUBMIT_OVERLAP=0
 *                   turns it off) each job carries its list's frame slot
 *                   (RdSubmitJob.slot), and the render thread's next
 *                   picture waits only for the jobs of the slot its
 *                   rhi_wait_frame recycles, the picture two before
 *                   (rd__submit_drain -> rd_thread_submit_wait_slot); the
 *                   picture before goes on being ended, submitted and
 *                   presented here while the render thread recycles its own
 *                   slot, acquires its image and records its list.  The
 *                   slot guard (rhi.h rhi_set_slot_guard; slotFree below)
 *                   lets those slot calls through the call guard; every
 *                   other call that changes the device's state (a texture
 *                   or pipeline made or destroyed, a readback, a submit, a
 *                   swapchain rebuild) still waits for this thread as
 *                   below.  The backend's partition of its state between
 *                   the two threads: port/rhi/vk/vk_cmd.c "Frame slots".
 *   one at a time   otherwise (Direct3D 12, Vulkan's fence sync, and the
 *                   calls the slot guard does not let through) the
 *                   backend's calls never run on two threads at once: the
 *                   render thread's next call that changes the device's
 *                   state (its next picture's fence wait, rd__wait_frame,
 *                   is the first) waits for the submit thread first (the
 *                   host's call guard asks rd_thread_submit_busy; the wait
 *                   is rd_thread_submit_drain), and every other thread's
 *                   call waits for both threads.  What overlaps the submit
 *                   and the present there is the next picture's
 *                   interpolation, which makes no graphics call, and the
 *                   game thread's step.  Direct3D 12 keys its frame slot,
 *                   the acquired image and the submit's counters on one
 *                   frame index (d3d12_cmd.c).
 *   which pictures  only a picture handed over without waiting (rd_thread.h
 *                   the hand-over), with no capture to finish
 *                   (rd__capture_pending); every picture that waits for its
 *                   end (sync, rd_submit_hold, a capture, the blended dump,
 *                   rd__replay_frame) and every picture made on the game
 *                   thread (the render thread parked, no host) is ended,
 *                   submitted and presented where it was made, as before.
 *   the order       a list kept open is submitted before anything else the
 *                   render thread submits: a call of its own that needs the
 *                   backend before the job ends ends, submits and presents
 *                   it there first (rd_thread_submit_drain, counted as
 *                   "on the render thread"), so the GPU's work and the
 *                   presents keep their order: the bytes of every picture
 *                   are the same as with the submit on the render thread.
 *                   With the slots apart the render thread submits nothing
 *                   beside this thread (a submit is not a slot call), so
 *                   the queue still takes picture N's list and present
 *                   before anything of picture N+1; only N+1's acquire may
 *                   come before N's present (the swapchain's lock orders
 *                   the two calls; vk_swapchain.c).
 *   the depth       at most RD_SUBMIT_DEPTH jobs queued and running
 *                   (rd_submit_queue.h); with the rule above there is one.
 *   the times       a job's submit and present times go into its replay's
 *                   perf record (rd__perf_submit_times) when a thread that
 *                   waited for it takes them: the window's replays line
 *                   keeps its submit and present columns.
 *   which backends  Vulkan and Direct3D 12 (RhiLimits.singleThreadCommands
 *                   false); OpenGL and Direct3D 11 keep the three calls on
 *                   the render thread.  ICO_RD_SUBMIT_THREAD=0 keeps them
 *                   there too (the tests' other arm; no setting).
 *
 * The host (rd_thread.h RdThreadHost.submit) asks for it; rd_set_thread_host
 * starts it with the host and ends it with the host's removal.
 */
#ifndef RD_SUBMIT_THREAD_H
#define RD_SUBMIT_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include "cpu_place.h"
#include "rd_submit_queue.h"
#include "rhi.h"

/* rd_set_thread_host's: the thread started (begin(user) first on it, NULL:
 * nothing; the window raises its priority there), one line naming why
 * not; and ended, everything posted finished first */
bool rd__submit_thread_start(void (*begin)(void *user), void *user);
void rd__submit_thread_stop(void);

/* The thread runs */
bool rd_thread_submit_on(void);
/* The calling thread is the submit thread */
bool rd_thread_on_submit(void);
/* The host's call guard (rhi.h rhi_set_call_guard), for the calling
 * thread: busy: a call that changes the device's state must wait for the
 * submit thread first (rd_thread_submit_drain): it has work, or, on the
 * render thread, a list is kept for it; stale: rhi_get_stats would read
 * counters the submit thread is writing.  Both false on the submit thread
 * and with no thread. */
bool rd_thread_submit_busy(void);
bool rd_thread_submit_stale(void);
/* The submit thread runs with the frame slots apart (any thread; the
 * window's pacing: the render thread's next picture then waits for the
 * picture two before, not for the submit thread's job on the one before,
 * pace_policy.h PaceRtPipe) */
bool rd_thread_submit_slots_apart(void);
/* The render thread's frame slots apart from the submit thread's: the
 * calling thread is the render thread, the slots are apart (rd_submit_thread.h
 * "the slots") and it keeps no list for the submit thread */
bool rd_thread_submit_apart(void);
/* The render thread, its next picture's fence wait (rd__submit_drain):
 * returns once no job of frame slot `slot` is queued or running, the ns it
 * waited, the finished jobs' times put into their records; the job of the
 * other slot may go on.  Not apart (rd_thread_submit_apart):
 * rd_thread_submit_drain */
uint64_t rd_thread_submit_wait_slot(uint32_t slot);
/* The finished jobs' times into their records, without waiting (any
 * thread but the submit thread) */
void rd_thread_submit_reap(void);
/* Returns once the submit thread has nothing left, the ns it waited: on
 * the render thread a list kept for it is ended, submitted and presented
 * there first; the finished jobs' times then go into their records.  Any
 * thread but the submit thread; the game thread only with the render
 * thread idle (after its drain).  0 with no thread. */
uint64_t rd_thread_submit_drain(void);
/* A job is queued or running (the window's heartbeat: no lock) */
bool rd_thread_submit_unfinished(void);
/* The tests' seam (rd_tex_test): fn() on the submit thread inside a job
 * that presents, before its present call (a driver's present that blocks,
 * held as long as fn does); NULL: none */
void rd__submit_test_hook(void (*fn)(void));
/* The longest present call of the jobs finished since the last take, ns
 * (0: none; render_thread.c's present estimate, pace_rt_further_hold) */
uint64_t rd_thread_submit_take_present_ns(void);
/* The longest job (a picture's list ended and submitted, and its present)
 * finished since the last take, ns (0: none; render_thread.c's submit
 * estimate: the window predicts when the render thread's next picture may
 * make its first graphics call, pace_policy.h PaceRtPipe) */
uint64_t rd_thread_submit_take_job_ns(void);
/* The 10 s block (the window's line) */
void rd_thread_submit_block_take(RdSubmitBlock *out);
/* The cores the submit thread ran its jobs on since the last take (one
 * sample a job, cpu_place.h), for the 10 s line; empty with no thread */
void rd_thread_submit_take_cores(CpuPlaceHist *out);

/* The render thread, rd__submit_frame before the reads end: the list kept
 * open for the submit thread (true), else false and the caller ends,
 * submits and presents it as before.  present: an image was acquired for
 * the replay */
bool rd__submit_defer(RhiCommandList cl, bool present);
/* The render thread, a job's end (rd_thread.c runTramp): the list kept,
 * posted */
void rd__submit_post_pending(void);

#endif /* RD_SUBMIT_THREAD_H */
