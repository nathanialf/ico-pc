/* rd_thread.h: the renderer on the window's render thread.
 *
 * The window's render thread (port/platform/render_thread.c) runs the
 * replays: the fence wait, the acquire, the uploads, the walk, the binds,
 * the submit and the present; the game thread records.  The window gives
 * the renderer a host (rd_set_thread_host): a job posted runs there, after
 * every job posted before it.
 *
 * The rules (docs/port/RENDER.md "The render thread"):
 *   the hand-over   a picture posted without waiting hands the render
 *                   thread the renderer's state (texLock): the frames it
 *                   blends and replays, the textures, the meshes, the
 *                   targets, the settings.  Until its reads end (the walk
 *                   and the present's pass, rd__reads_done from
 *                   rd__submit_frame) every game-thread call that changes
 *                   them waits (rd__texlock_wait: the texture, mesh and
 *                   target calls, rd_begin_frame, rd_end_frame, the texture
 *                   cache's stores); the submit, the present and the GPU's
 *                   time after them are the game thread's to overlap.  The
 *                   state the render thread reads is therefore the state
 *                   the game thread had when it posted, as if the picture
 *                   ran there.
 *   the submit      a picture handed over has its list ended and
 *                   submitted and its image presented on the submit thread
 *                   (rd_submit_thread.h; Vulkan and Direct3D 12): its job
 *                   here ends with its walk, and the next picture's
 *                   interpolation runs while the submit thread works; its
 *                   first graphics call waits for that thread.
 *   one picture     a picture waits for the one before to finish (the
 *                   drain: the render thread's job, not the submit
 *                   thread's); a picture whose capture or blended dump is
 *                   armed, every picture while rd_submit_hold is on, and
 *                   every replay through rd__replay_frame wait for their
 *                   end.  The drain is not what keeps the frames apart:
 *                   the game thread's frame calls already wait for the
 *                   reads (the hand-over), so the three frame slots
 *                   (RD_FRAME_RING) are never short.  A new frame's
 *                   present that did not drain would still have its reads
 *                   run after the picture before ends, and the game's next
 *                   step waits for those reads at its first renderer call:
 *                   the wait moves, it does not go (docs/port/RENDER.md
 *                   "The render thread").
 *   the overlay     the UI's layers of a picture posted from the game
 *                   thread (the popups, the photo mode's HUD, the touch
 *                   controls, the frame rate counter) are collected there
 *                   before the post (rd_present.c rd__overlay_hand_over):
 *                   their callbacks share port/ui's font state and caches
 *                   with the game thread's own text, so they never run
 *                   while the game thread records; the picture's present
 *                   draws the batches collected.
 *   calls           rd__on_host's calls (the targets made again, the
 *                   movie's present, the fog's probe) run there too,
 *                   waited for: every GPU call moves with no change where
 *                   it is made.  A display change's pipelines stay on the
 *                   game thread with the render thread idle (rd_core.c
 *                   onGameHost): their progress screen takes the window's
 *                   events, which SDL gives the main thread only.
 *   retire          a GPU texture the game thread destroys is destroyed by
 *                   the render thread in its next job, after the submit of
 *                   the list that may still use it: a call's job at its
 *                   start, a picture's at its first graphics call (after
 *                   its interpolation, which makes none, and after that
 *                   call's wait for the submit thread), so a picture's
 *                   interpolation never waits behind the submit thread.
 *   stats           RdStats as the game thread reads it (rd_get_stats):
 *                   the record's counts live, the replays' as the render
 *                   thread left them at its last reads' end.
 * Without a host every call runs where it is made, as before.
 */
#ifndef RD_THREAD_H
#define RD_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rhi.h"

typedef struct RdFrame RdFrame;

/* The render thread.  post: fn(arg) run there after what was posted before
 * it; picture: a picture's job (the window's last present and the
 * heartbeat's picture count), else a call between pictures; wait: returns
 * once it ran.  False when not taken (no thread, parked, the queue full):
 * the caller runs fn itself.  drain: returns once everything posted has
 * finished, the ns it waited.  sync: every post waits for its job
 * ([dev] render_thread = sync), so no picture is handed over: each is
 * posted as one that waits (rd_submit_async false, the present's own
 * blocked time the window's, as with the thread off).  submit: the
 * pictures handed over are ended, submitted and presented on the submit
 * thread (rd_submit_thread.h; not with sync), the host's call guard asking
 * rd_thread_submit_busy, rd_thread_submit_stale and rd_thread_submit_drain
 * as that header says; submitBegin(user) runs first on that thread (its
 * priority; NULL: nothing).  drain is the render thread's own:
 * rd_thread_drain waits for the submit thread after it. */
typedef struct RdThreadHost {
    void *user;
    bool (*post)(void *user, void (*fn)(void *arg), void *arg, bool picture, bool wait);
    uint64_t (*drain)(void *user);
    bool sync;
    bool submit;
    void (*submitBegin)(void *user);
} RdThreadHost;

/* Set (or NULL: removed) on the game thread with the render thread idle;
 * removing it destroys the textures still to retire, here */
void rd_set_thread_host(const RdThreadHost *host);
bool rd_thread_host_on(void);
/* The calling thread is running one of the render thread's jobs */
bool rd_thread_on_render(void);
/* Everything posted has finished, the submit thread's jobs too (the game
 * thread; on the render thread the submit thread's alone, its list kept
 * for it included; 0 and nothing done without a host); the ns waited */
uint64_t rd_thread_drain(void);
/* The result of the last picture rd_present_post or rd__replay_frame_async
 * handed over, once a drain after it returned (the replay tool) */
bool rd_thread_last_ok(void);

/* The present on the render thread (the window's present_once): rd_present
 * (alpha) whole there, the interpolation included; returns at once unless
 * the picture waits (see above).  True when a picture was handed over or
 * presented; a handed-over picture's own result is known at the next drain
 * (the next present's start), where a failure is counted
 * (rd_thread_failed_presents).  Without a host, or with nothing to
 * present, rd_present here. */
bool rd_present_post(float alpha);
/* The presents rd_present_post handed over that then failed on the render
 * thread (rd_present false: a failed acquire, the ring, the
 * interpolation's memory), counted at the drain after each: the window
 * counted each as presented when it was handed over */
uint32_t rd_thread_failed_presents(void);

/* What the render thread was doing when a game-thread call began to wait
 * for a picture's reads (RdThreadBlock.readWaitBy): the picture handed
 * over and not yet begun there (the hand-over), the job's start up to its
 * fence wait (a present's interpolation), the wait for its frame slot (the
 * submit thread's jobs of that slot and the GPU's fence: a picture whose
 * slot was not waited for ahead, rd__slot_ahead_take), the swapchain's acquire,
 * the ring and the uploads, the walk and the present's pass */
typedef enum RdReadPhase {
    RD_READ_HANDOVER = 0,
    RD_READ_INTERP,
    RD_READ_FENCE,
    RD_READ_ACQUIRE,
    RD_READ_UPLOAD,
    RD_READ_WALK,
    RD_READ_PHASES
} RdReadPhase;

/* The block's numbers for the window's 10 s line (render_thread.c), since
 * the last take: the pictures the render thread ran and their means in ms
 * (the interpolation, the uploads and the walk, the submit and the present,
 * the waits for the GPU and the display inside them), the game thread's
 * waits for the render thread's reads (texLock) and the GPU textures
 * retired.  freshWaits/furtherWaits: rd_present_post's waits for the
 * picture before (the drain; the window's slot waits), those of a new
 * frame's present and those of a further present of a frame already
 * presented, with their sums in ms: a new frame's present can only wait
 * for the end of the picture before (its submit and present: the frame's
 * close already waited for its reads), so these are that picture's present
 * call and the GPU's backlog behind it */
/* aheads: the pictures whose job ended by waiting for the next picture's
 * frame slot (the slot ahead, rd__slot_ahead_take: outside the hand-over), with the waits' sum
 * and longest in ms; fenceInside: the pictures handed over that waited for
 * their own slot inside their reads (no slot waited for ahead).
 * roomAheads: the pictures whose job ended by making the next picture's
 * wait for room in the present queue (the display's wait ahead,
 * rd__room_ahead_take: Direct3D 11's frame latency wait), with the waits'
 * sum and longest in ms; roomDrains: the game thread's presents whose
 * drain waited for one (the part of the drain inside it, summed in ms:
 * the present's own wait, counted as the display's); roomInside: the
 * pictures handed over whose acquire found none made ahead where the
 * backend has the wait (it waited inside their reads) */
typedef struct RdThreadBlock {
    unsigned pictures;
    double interpMs, walkMs, submitMs, waitMs;
    unsigned readWaits;
    double readWaitMs, readWaitMaxMs;
    unsigned readWaitBy[RD_READ_PHASES];
    double readWaitByMs[RD_READ_PHASES];
    unsigned retired;
    unsigned freshWaits, furtherWaits;
    double freshWaitMs, furtherWaitMs;
    unsigned aheads, fenceInside;
    double aheadMs, aheadMaxMs;
    unsigned roomAheads, roomDrains, roomInside;
    double roomAheadMs, roomAheadMaxMs, roomDrainMs;
} RdThreadBlock;

void rd_thread_block_take(RdThreadBlock *out);
/* The block's read waits split by what the render thread was doing when
 * each began, and its slots waited for ahead, as the window's 10 s line's
 * clause after "read waits N (...)": "; the render thread as each began: the hand-over N
 * (X ms), ...; the next picture's frame slot waited for at the end of the
 * one before N times (...), N pictures waited for their slot inside their
 * reads" (the split left out with no read wait), then where the backend
 * has a display's wait made ahead "; the display's wait for room in the
 * present queue made at the end of the picture before N times (X ms in
 * all, longest Y ms; the presents waited for it N times, X ms, as the
 * display's wait), N pictures waited for it inside their reads" (left out
 * with none of those); out always terminated */
void rd_thread_read_waits_text(const RdThreadBlock *b, char *out, size_t cap);

/* The render thread, after a picture's job: how long the picture held the
 * renderer's state (the hand-over, from the job's start to its reads' end,
 * rd__reads_done), in ns; 0 when it handed nothing over (a picture that
 * waited, a call).  Taken: the next call gives 0 until another picture
 * hands its state back.  The window plans further presents by it
 * (render_thread.c, pace_policy.h pace_rt_further_late). */
uint64_t rd_thread_take_reads_ns(void);
/* The render thread, after a picture's job: how long its present call took
 * (rd_submit.c's present time: the backend's present, Vulkan's
 * vkQueuePresentKHR, and a capture's finish), in ns, the longest when the
 * job presented more than once; 0 when it presented nothing.  Taken as
 * above.  The window holds further presents back while the presents wait
 * (render_thread.c, pace_policy.h pace_rt_further_hold). */
uint64_t rd_thread_take_present_ns(void);
/* The render thread, after a picture's job, with rd_thread_take_reads_ns:
 * the waits inside its reads (waitNs: its replays' waits for the slot's
 * fence and the submit thread, and the acquire; rd_replay.c's wait time)
 * and its interpolation (interpNs, which makes no graphics call), in ns;
 * taken as above.  The window predicts a further picture's reads by the
 * reads less those waits (render_thread.c, pace_policy.h PaceRtPipe). */
void rd_thread_take_reads_parts(uint64_t *waitNs, uint64_t *interpNs);
/* The game thread's waits for a picture's reads in all since the start,
 * ns (the 10 s line's read waits, summed): the window takes a step's
 * share out of its own time and counts the steps that started late by
 * them (window_present.c) */
uint64_t rd_thread_read_wait_ns(void);

/* A render thread of the renderer's own, without SDL, for the replay tool
 * and the tests (rd_thread_ab, rd_tex_test): sync posts every job waited
 * (the game thread waits for each), on posts as the window does.  The RHI
 * call guard (rhi.h rhi_set_call_guard) is installed with it.  False (and
 * nothing started) for off, OpenGL, or a thread that could not be made */
typedef enum RdThreadMode { RD_THREAD_OFF = 0, RD_THREAD_SYNC, RD_THREAD_ON } RdThreadMode;

bool rd_thread_local_start(RdThreadMode mode);
void rd_thread_local_stop(void);
/* "off", "sync", "on" (anything else: false) */
bool rd_thread_mode_parse(const char *s, RdThreadMode *out);
/* The mode the next rd_init (and every one after it) starts the
 * renderer's own thread in, rd_shutdown ending it (the replay tool's
 * --render-thread); OFF: none (the default) */
void rd_thread_local_want(RdThreadMode mode);

/* ---------------------------------------------------- the renderer's own */

/* The game thread: the render thread's reads of a picture handed over have
 * ended (texLock; nothing on the render thread or without a host) */
void rd__texlock_wait(void);
/* The render thread inside a picture's job handed over whose reads are
 * still open (rd__submit_defer asks before rd__reads_done) */
bool rd__thread_handed(void);
/* The render thread inside a picture's open hand-over: what it does from
 * here (RdReadPhase), for the game thread's read waits' split; nothing
 * elsewhere */
void rd__thread_phase(RdReadPhase phase);
/* The slot ahead: the render thread, at the end of a picture's job handed
 * over (after its reads ended and its list went to the submit thread),
 * waits for the next picture's frame slot (the submit thread's jobs of
 * that slot, then rhi_wait_frame), so the next picture's reads begin
 * without the GPU's fence wait inside them.  Not with the submit thread on
 * and the frame slots one at a time (the wait would be the submit
 * thread's whole job on this picture), not under ICO_RD_SLOT_AHEAD=0.
 * rd__slot_ahead_take (rd__wait_frame, any thread: the render thread
 * drained first elsewhere): true once when the current frame slot is the
 * one waited for ahead, its fence and its jobs then done; false: wait as
 * before. */
bool rd__slot_ahead_take(void);
/* The display's wait ahead: the render thread, at the end of a picture's
 * job handed over that presented (after the slot ahead), makes the next
 * picture's wait for room in the present queue (rhi_wait_present_room:
 * Direct3D 11's frame latency wait), so the next picture's reads begin
 * without the display's wait inside them; the game thread's next present
 * waits for what is left of it in its drain, as the display's wait.  Not
 * with the submit thread on, not under ICO_RD_ACQUIRE_AHEAD=0 ([dev]
 * acquire_ahead = false).  rd__room_ahead_take (rd__present_acquire, any
 * thread): the record taken, and on the render thread in a picture's
 * reads where the backend has the wait and none was made ahead, counted
 * as a wait inside the reads (the backend's acquire then waits itself). */
void rd__room_ahead_take(void);
/* The tests' seam (rd_tex_test): fn() in place of rhi_wait_present_room
 * in the display's wait ahead (its answer the backend's); NULL: none */
void rd__thread_room_hook(bool (*fn)(void));
/* The tests' seam (rd_tex_test): fn(at) on the render thread inside a
 * picture's open hand-over as it enters phase `at` (RdReadPhase), and with
 * at = RD_READ_PHASES in the slot ahead before its wait; NULL: none */
void rd__thread_test_hook(void (*fn)(int at));
/* The submit thread's time of a picture (its submit and present), for the
 * block, on the thread that took it (rd_submit_thread.c) */
void rd__thread_note_submit(double ms);
/* The render thread: this job's reads of the renderer's state end (the
 * game thread's calls go on); once a job, later calls do nothing */
void rd__reads_done(void);
/* rhi_destroy_texture now, or with a host on the game thread at the render
 * thread's next job */
void rd__retire_texture(RhiTexture t);
/* The render thread at a picture's first graphics call (rd__submit_drain,
 * after its wait for the submit thread): the textures the game thread
 * retired destroyed, once a job (nothing elsewhere or later) */
void rd__thread_retire_kept(void);
/* fn(arg) on the render thread, waited for (rd__on_host); false when not
 * taken (no host: the caller runs it its own way) */
bool rd__render_call(void (*fn)(void *arg), void *arg);
/* the replay posted as a picture: waited for unless the caller is the game
 * thread's present (rd__replay_frame_async: rd_present, rd_reshow.c) */
bool rd__replay_frame_here(const RdFrame *f, int keep, bool present);
bool rd__replay_frame_async(const RdFrame *f, int keep, bool present);
/* a picture of the game thread's begins: the one before it finished (its
 * wait counted for rd_submit_waited_ns; rd_present's start) */
void rd__thread_picture_begin(void);
/* rd_present's value of the present clock (rd_interp.c s_clockRaw): the
 * one the game thread took when it posted the present, on the render
 * thread; else the clock's own, taken (NAN after) */
double rd__thread_clock_raw(void);
/* rd_get_stats on the game thread with a host: the counts as above */
const struct RdStats *rd__thread_stats(void);
/* RdStats the calling thread counts into: the render thread's own inside
 * its jobs, g_rd.stats elsewhere */
struct RdStats *rd__stats_here(void);
/* the walk's time and the submit's of a replay (rd_submit.c), for the
 * block */
void rd__thread_note_replay(double walkMs, double submitMs, double waitMs);
/* a replay's present took ms (rd_submit.c), for rd_thread_take_present_ns;
 * nothing outside the render thread's jobs */
void rd__thread_note_present(double ms);
void rd__thread_note_interp(double ms);
/* rd_init's end and rd_shutdown's start: rd_thread_local_want's thread
 * started (a headless renderer with a device) and ended */
void rd__thread_local_resume(void);
void rd__thread_local_pause(void);

#endif /* RD_THREAD_H */
