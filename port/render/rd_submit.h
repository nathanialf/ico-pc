/* rd_submit.h: a replay's end (rd_replay.c is at its file size gate): the
 * present's pass recorded into the replay's list, the walk's time closed,
 * then the list ended and submitted and the picture presented, with the
 * capture a present copied written out.
 *
 * The end and the submit, the present and the capture's finish can run on
 * the window's render thread (port/platform/render_thread.c): the window
 * gives the renderer a host (rd_set_submit_host) and each replay's list is
 * handed to it once recorded; the next replay waits for that hand-over to
 * finish before it waits its own fence (rd__submit_drain from
 * rd__wait_frame), so the frame slot it reuses and the backend's frame are
 * free.  A picture whose capture is armed (F12, a save state's picture),
 * and every picture while rd_submit_hold is on, waits for its present
 * before the replay returns.  Without a host everything runs here, as
 * before. */
#ifndef RD_SUBMIT_H
#define RD_SUBMIT_H

#include <stdbool.h>
#include <stdint.h>
#include "rhi.h"

/* replayFrame's end (rd_replay.c): cl the replay's list, present whether
 * the replay presents (an image acquired), walkStart the rd__now_ms the
 * walk's time counts from and bind0 g_rdPerf.bindMs then (the walk's time
 * leaves the binds out).  Sets g_rdPerf.walkMs, submitMs and presentMs
 * (handed over: the last picture's, the render thread's own times, one
 * replay late). */
void rd__submit_frame(RhiCommandList cl, bool present, double walkStart, double bind0);
/* The last replay's hand-over finished (rd__wait_frame; rd__present_acquire) */
void rd__submit_drain(void);

/* The window's render thread.  post: job(arg) run there, wait: returned
 * once it ran; false when not taken (the caller runs it).  drain: returns
 * once everything handed over has run, the ns it waited.  NULL: none (the
 * default) */
typedef struct RdSubmitHost {
    void *user;
    bool (*post)(void *user, void (*job)(void *arg), void *arg, bool wait);
    uint64_t (*drain)(void *user);
} RdSubmitHost;

void rd_set_submit_host(const RdSubmitHost *host);
/* While on, each picture handed over waits for its present (the vsync
 * lock's display timing reads a present's id right after it,
 * port/platform/window_present.c) */
void rd_submit_hold(bool wait);
/* The last replay's picture went to the render thread and its present is
 * still to come when the replay returned */
bool rd_submit_async(void);
/* The time the replays waited for the render thread at their starts, in
 * ns, summed since the start (a present's wait for the one before) */
uint64_t rd_submit_waited_ns(void);

#endif /* RD_SUBMIT_H */
