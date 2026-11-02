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
 *   one picture     a picture waits for the one before to finish (the
 *                   drain); a picture whose capture or blended dump is
 *                   armed, every picture while rd_submit_hold is on, and
 *                   every replay through rd__replay_frame wait for their
 *                   end.
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
 *                   the render thread at the start of its next job, after
 *                   the submit of the list that may still use it.
 *   stats           RdStats as the game thread reads it (rd_get_stats):
 *                   the record's counts live, the replays' as the render
 *                   thread left them at its last reads' end.
 * Without a host every call runs where it is made, as before.
 */
#ifndef RD_THREAD_H
#define RD_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include "rhi.h"

typedef struct RdFrame RdFrame;

/* The render thread.  post: fn(arg) run there after what was posted before
 * it; picture: a picture's job (the window's last present and the
 * heartbeat's picture count), else a call between pictures; wait: returns
 * once it ran.  False when not taken (no thread, parked, the queue full):
 * the caller runs fn itself.  drain: returns once everything posted has
 * finished, the ns it waited. */
typedef struct RdThreadHost {
    void *user;
    bool (*post)(void *user, void (*fn)(void *arg), void *arg, bool picture, bool wait);
    uint64_t (*drain)(void *user);
} RdThreadHost;

/* Set (or NULL: removed) on the game thread with the render thread idle;
 * removing it destroys the textures still to retire, here */
void rd_set_thread_host(const RdThreadHost *host);
bool rd_thread_host_on(void);
/* The calling thread is running one of the render thread's jobs */
bool rd_thread_on_render(void);
/* Everything posted has finished (the game thread; 0 and nothing done on
 * the render thread or without a host); the ns waited */
uint64_t rd_thread_drain(void);

/* The present on the render thread (the window's present_once): rd_present
 * (alpha) whole there, the interpolation included; returns at once unless
 * the picture waits (see above).  True when a picture was handed over or
 * presented; a handed-over picture's own result comes one present late
 * (rd_thread_last_ok).  Without a host, or with nothing to present,
 * rd_present here. */
bool rd_present_post(float alpha);
/* The result of the last picture handed over, once it finished */
bool rd_thread_last_ok(void);

/* The block's numbers for the window's 10 s line (render_thread.c), since
 * the last take: the pictures the render thread ran and their means in ms
 * (the interpolation, the uploads and the walk, the submit and the present,
 * the waits for the GPU and the display inside them), the game thread's
 * waits for the render thread's reads (texLock) and the GPU textures
 * retired */
typedef struct RdThreadBlock {
    unsigned pictures;
    double interpMs, walkMs, submitMs, waitMs;
    unsigned readWaits;
    double readWaitMs, readWaitMaxMs;
    unsigned retired;
} RdThreadBlock;

void rd_thread_block_take(RdThreadBlock *out);

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
/* The render thread: this job's reads of the renderer's state end (the
 * game thread's calls go on); once a job, later calls do nothing */
void rd__reads_done(void);
/* rhi_destroy_texture now, or with a host on the game thread at the render
 * thread's next job */
void rd__retire_texture(RhiTexture t);
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
void rd__thread_note_interp(double ms);
/* rd_init's end and rd_shutdown's start: rd_thread_local_want's thread
 * started (a headless renderer with a device) and ended */
void rd__thread_local_resume(void);
void rd__thread_local_pause(void);

#endif /* RD_THREAD_H */
