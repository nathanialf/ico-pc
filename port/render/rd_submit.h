/* rd_submit.h: a replay's end (rd_replay.c is at its file size gate): the
 * present's pass recorded into the replay's list, the walk's time closed,
 * then the list ended and submitted and the picture presented, with the
 * capture a present copied written out.
 *
 * With the window's render thread the whole replay runs there (rd_thread.h):
 * the end of the walk and of the present's pass is where its reads of the
 * renderer's state end (rd__reads_done), so the game thread's calls go on
 * while the list is submitted and the picture presented.  The calls below
 * are the window's view of the pictures handed over (port/platform/
 * window_present.c). */
#ifndef RD_SUBMIT_H
#define RD_SUBMIT_H

#include <stdbool.h>
#include <stdint.h>
#include "rhi.h"

/* replayFrame's end (rd_replay.c): cl the replay's list, present whether
 * the replay presents (an image acquired), walkStart the rd__now_ms the
 * walk's time counts from and bind0 g_rdPerf.bindMs then (the walk's time
 * leaves the binds out).  Sets g_rdPerf.walkMs, submitMs and presentMs. */
void rd__submit_frame(RhiCommandList cl, bool present, double walkStart, double bind0);
/* The pictures handed to the render thread finished (rd__wait_frame;
 * rd__present_acquire): nothing on the render thread itself */
void rd__submit_drain(void);

/* While on, each picture handed over waits for its present (the vsync
 * lock's display timing reads a present's id right after it,
 * port/platform/window_present.c) */
void rd_submit_hold(bool wait);
/* The last picture went to the render thread and its present is still to
 * come when the call that handed it over returned */
bool rd_submit_async(void);
/* The time the pictures waited for the one before them on the render
 * thread, in ns, summed since the start (a present's wait for the one
 * before) */
uint64_t rd_submit_waited_ns(void);

#endif /* RD_SUBMIT_H */
