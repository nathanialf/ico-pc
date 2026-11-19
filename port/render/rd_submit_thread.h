/* rd_submit_thread.h: the pictures' submit and present on a thread of
 * their own (docs/port/RENDER.md "The render thread").
 *
 * The render thread (rd_thread.h) makes a picture handed over to it: the
 * interpolation, the fence wait, the acquire, the uploads and the walk,
 * then the lists' ends, their submits and the present (a presenting
 * picture has two lists, its scene's and the present's pass's own, so only
 * the second waits for the window's image: rd_submit.c).  With the submit
 * thread the submits and the present leave it: the replay's end
 * (rd__submit_frame) ends the scene's list before it begins the
 * present's, keeps the last list open (rd__submit_defer) and the job's
 * end posts both here
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
 *   the GPU's end first
 *                   (issue 179; the device table's PRESENT_FREE, the row
 *                   android: every Android device; with the slots apart
 *                   only) a job of a picture Frame rate Original handed
 *                   over (rd_thread.h rd__thread_original_job) may wait
 *                   after its submit for its work to end on the GPU
 *                   (rhi_wait_submitted), outside the swapchain's lock,
 *                   and present after it.  On the Mali-G52 the driver's
 *                   present returns at the end of GPU work (the picture
 *                   before's, v0.11.12's log), and with the lock held
 *                   through that call the game's gate counted the picture
 *                   before as busy and skipped the next frame whenever a
 *                   picture's GPU time passed the tick.  On a driver
 *                   whose present returns early (the owner's Pixel,
 *                   PowerVR: 0.8 ms calls with the GPU work still running)
 *                   the wait would only add the GPU's remaining time
 *                   before every present.  So the verdict is the device's,
 *                   taken at run time: the first Original presents are
 *                   made without the wait and the driver's call of each
 *                   is timed, nothing else done before or around it.  A
 *                   call under 2 ms is not counted and costs nothing (a
 *                   driver whose present returns early does not decide,
 *                   the rule off, until RD_GPU_FIRST_LATE timed presents,
 *                   half of them or more short, make the verdict off).  After a call of 2 ms or more the
 *                   submit thread makes one unbounded wait for the
 *                   previous present job's submission (rhi_last_submitted
 *                   after its submits, rhi_wait_value_ns), never for the
 *                   picture's own (that wait would leave the next present
 *                   nothing of the picture before to wait for): back
 *                   within 1 ms, the picture before had ended by the
 *                   call's return, so the call lasted to the end of GPU
 *                   work (issue 179's Mali-G52, its reporter's log line
 *                   562: 140.3 ms, back at the picture before's signal);
 *                   blocked 1 ms or more, early; failed, unknown (its
 *                   result kept for the 10 s line).  The classes:
 *                   rd_submit_queue.h rd__sq_gf_class.  At
 *                   RD_GPU_FIRST_SAMPLES counted the rule is on where
 *                   RD_GPU_FIRST_NEED or more returned at the end of GPU
 *                   work, else off, with one line; after it no wait is
 *                   made again.  The timeline counter's instant read
 *                   (rhi_submitted_finished: no sample was ever counted
 *                   on the Mali-G52 with it as the input) is made only
 *                   after the wait, for the first 8 samples' lines, and
 *                   only logged.  Taken anew at every start of the thread
 *                   (a renderer switch, a new device) and where the
 *                   present mode changed since (a FIFO present waits for
 *                   the display, not the GPU).  With the rule on, while
 *                   the job is in that wait with no job behind it
 *                   (rd_thread_submit_gpu_wait_alone) the gate does not
 *                   count it (rd_thread.c rd__thread_picture_busy) and the
 *                   hand-over does not wait for it (rd__replay_frame_post):
 *                   the next picture's interpolation, fence wait, acquire
 *                   (beside the pending present: vk_swapchain.c, the
 *                   swapchain having images to spare), uploads and walk
 *                   run meanwhile, and its job is posted behind this one,
 *                   so its submit follows this present: the GPU never holds
 *                   two pictures (the queue's order, the slots' rule below).
 *                   The render thread's slot ahead then waits for this job
 *                   (the picture before's slot), so at most one picture is
 *                   recorded ahead.  A call that is not a slot call still
 *                   waits for the whole job through the call guard (a
 *                   texture made or retired, a pipeline made, a readback;
 *                   the game thread's calls).  The other frame rates are
 *                   untouched (their pacing reads the job's times as they
 *                   are).
 *   the depth       at most RD_SUBMIT_DEPTH jobs queued and running
 *                   (rd_submit_queue.h); with the rule above there is one,
 *                   with the GPU's end first two (the job in its wait and
 *                   the next picture's).
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
/* The tests' seam (rd_tex_test): fn() on the submit thread inside every
 * job, after its submit and before its present call, while the job is
 * still unfinished (rd_thread_submit_unfinished true): a driver's present
 * that blocks, held as long as fn does, also where the job has no present
 * (the tests' offscreen output has no swapchain); NULL: none */
void rd__submit_test_hook(void (*fn)(void));
/* The GPU's end first (above) is wanted for this run (the device table's
 * choice at the start, the frame slots apart); its verdict from the first
 * presents: RD_GPU_FIRST_UNDECIDED while sampling, _ON, _OFF (also where
 * it is not wanted).  RD_GPU_FIRST_SAMPLES counted samples, on at
 * RD_GPU_FIRST_NEED or more at the work's end: 16 is about 0.7 s of the
 * Mali-G52's Original pictures (22 a second in its v0.11.11 log) and
 * enough that a stray sample cannot turn it; 14 of 16 (seven in eight)
 * keeps a driver that waits for the GPU only now and then (a mixed
 * driver) off, where the wait would cost most presents time they did not
 * spend before */
#define RD_GPU_FIRST_SAMPLES 16u
#define RD_GPU_FIRST_NEED 14u
/* the samples' lines one by one (counted and unknown ones), the first this
 * many of the run; the line "no verdict after" this many timed Original
 * presents, once a run and present mode */
#define RD_GPU_FIRST_LINES 8u
#define RD_GPU_FIRST_LATE 256u

enum { RD_GPU_FIRST_UNDECIDED = -1, RD_GPU_FIRST_OFF = 0, RD_GPU_FIRST_ON = 1 };

bool rd_thread_submit_gpu_first(void);
int rd_thread_submit_gpu_first_verdict(void);
/* The submit thread's running job waits for its picture's GPU end before
 * its present, and no job is queued behind it (any thread, no lock; the
 * game's gate and the hand-over, rd_thread.c) */
bool rd_thread_submit_gpu_wait_alone(void);

/* The tests' seams (rd_tex_test): a faked GPU and driver on the submit
 * thread (NULL: none).  submitted() after a job's submit (its work begins);
 * waitEnd() inside the GPU's end first's wait (returns at the faked end);
 * present() where the driver's present call is, every job (it may wait for
 * the faked end: a driver whose present waits for the GPU); sampleWait()
 * the verdict's wait after a long present, in place of the backend's
 * (rhi_wait_value_ns): returns at the faked end of the picture before's
 * work, true (a job counts as presenting while present is set).  Any
 * member may be NULL: sampleWait NULL, the samples wait on the real
 * backend's submissions.  The rule wanted (1) or not (0) at the next
 * start, -1 the device table's.  The jobs that made the wait, ever.  The
 * verdict's tally since the thread's start (or a present mode change):
 * the Original presents timed, of them short (under 2 ms), counted, of
 * those waited, unknown; the samples' waits made. */
typedef struct RdSubmitTestGpu {
    void (*submitted)(void);
    void (*waitEnd)(void);
    void (*present)(void);
    bool (*sampleWait)(void);
} RdSubmitTestGpu;

typedef struct RdGpuFirstTally {
    unsigned timed, shortCalls, counted, waited, unknown, waits;
} RdGpuFirstTally;

void rd__submit_test_gpu(const RdSubmitTestGpu *fake);
void rd__submit_test_gpu_first(int force);
void rd__submit_gpu_first_tally(RdGpuFirstTally *out);
uint64_t rd__submit_gpu_first_waits(void);
/* of those, the backend's waits that returned true (rhi_wait_submitted) */
uint64_t rd__submit_gpu_first_waited(void);
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

/* The render thread, rd__submit_frame before the reads end: the lists
 * kept for the submit thread (true), else false and the caller ends,
 * submits and presents them as before.  cl the scene's list (left open
 * when it is the only one, else ended), cl2 the present's own (left open;
 * 0: none); present: an image was acquired for the replay */
bool rd__submit_defer(RhiCommandList cl, RhiCommandList cl2, bool present);
/* A job's ends and submits, in order (rd_submit_queue.h rd__sq_tail_steps:
 * cl, then cl2), without its present: the submit thread's tail and
 * rd__submit_frame's own path */
void rd__submit_lists(const RdSubmitJob *j);
/* The render thread, a job's end (rd_thread.c runTramp): the list kept,
 * posted */
void rd__submit_post_pending(void);

#endif /* RD_SUBMIT_THREAD_H */
