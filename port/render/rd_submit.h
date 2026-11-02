/* rd_submit.h: a replay's end (rd_replay.c is at its file size gate): the
 * present's pass recorded into the replay's list, the walk's time closed,
 * then the list ended and submitted and the picture presented, with the
 * capture a present copied written out. */
#ifndef RD_SUBMIT_H
#define RD_SUBMIT_H

#include <stdbool.h>
#include "rhi.h"

/* replayFrame's end (rd_replay.c): cl the replay's list, present whether
 * the replay presents (an image acquired), walkStart the rd__now_ms the
 * walk's time counts from and bind0 g_rdPerf.bindMs then (the walk's time
 * leaves the binds out).  Sets g_rdPerf.walkMs, submitMs and presentMs. */
void rd__submit_frame(RhiCommandList cl, bool present, double walkStart, double bind0);

#endif /* RD_SUBMIT_H */
