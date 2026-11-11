/*
 * port/platform/window_perf_rank.h
 *
 * The window's performance lines by scene (port/render/rd_perf_rank.h,
 * issues 179, 211 and 123; docs/port/ANDROID_DIAGNOSTICS.md "The
 * performance lines"): the 10 s block's perf scene, classes, rates and
 * slowest lines, the ranked line when the block was below the game's
 * rate, and the run's performance profile every 300 s of blocks and at the
 * end.  Kept out of window_host.c (at its file size gate), which calls it
 * from perf_drain (the records and the timed pieces as they pop), from
 * window_pace_log (the block's end) and from ico_window_close.  The
 * window's thread only: no lock, nothing on the render or submit thread,
 * nothing asked of the GPU.
 */
#ifndef ICO_PLATFORM_WINDOW_PERF_RANK_H
#define ICO_PLATFORM_WINDOW_PERF_RANK_H

#include <stdbool.h>

#include "rd.h"
#include "window_scene.h"

/* a record as it pops; vuInvocations its vertex work (window_scene_vu_invocations) */
void window_perf_rank_record(const RdPerfRecord *r, double vuInvocations);
/* a timed replay's pieces (rd_perf_passes_of) */
void window_perf_rank_pieces(const RdPerfRecord *r, const RdPerfPass *p, uint32_t n);

/* The block's end (window_pace_log, before perf_log): the lines, the
   block into the run's profile, the block emptied; NULL empties it alone
   (the records before the first block) */
typedef struct WindowPerfBlock {
    double seconds, presentedFps, gameFps;
    int stage;        /* stage_no */
    SceneKind kind;   /* the block's dominant layout kind */
    int paceHz;       /* the simulation's rate (50 or 60; 0 none yet) */
    bool original;    /* Frame rate Original (no interpolation) */
    double displayHz; /* the display's refresh (the target with interpolation) */
    double stepCpuMs; /* the game step's CPU a step */
} WindowPerfBlock;

void window_perf_rank_block(const WindowPerfBlock *in);
/* the run's profile, once (ico_window_close) */
void window_perf_rank_run_end(void);

#endif /* ICO_PLATFORM_WINDOW_PERF_RANK_H */
