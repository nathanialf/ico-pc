/*
 * port/platform/window_perf_rank.h
 *
 * The window's performance lines by scene (port/render/rd_perf_rank.h,
 * issues 179, 211 and 123; docs/port/ANDROID_DIAGNOSTICS.md "The
 * performance lines"): the 10 s block's perf scene, classes, rates and
 * slowest lines, the ranked line when the block was below the game's
 * rate, and the run's performance profile every 300 s of blocks, at the
 * end, and as the app goes to the background or ends (with the block in
 * progress).  Kept out of window_host.c (at its file size gate), which calls
 * it from perf_drain (the records and the timed pieces as they pop), from
 * scene_note_step (each step's layout kind), from window_pace_log (the
 * block's end), from the lifecycle table (window_lifecycle.h) and from
 * ico_window_close.  The lifecycle's step runs on SDL's posting thread
 * (Android's Java thread), the rest on the window's: a spin lock around
 * the block and the run.  Nothing on the render or submit thread, nothing
 * asked of the GPU.
 */
#ifndef ICO_PLATFORM_WINDOW_PERF_RANK_H
#define ICO_PLATFORM_WINDOW_PERF_RANK_H

#include <stdbool.h>
#include <stddef.h>

#include "rd.h"
#include "window_scene.h"

/* a record as it pops; vuInvocations its vertex work (window_scene_vu_invocations) */
void window_perf_rank_record(const RdPerfRecord *r, double vuInvocations);
/* a timed replay's pieces (rd_perf_passes_of) */
void window_perf_rank_pieces(const RdPerfRecord *r, const RdPerfPass *p, uint32_t n);
/* a step's layout kind and stage (the block in progress's scene key) */
void window_perf_rank_step(SceneKind kind, int stage, uint64_t ns);

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
/* the lifecycle's step (IcoLifecycleOps.perf_profile): the run's profile
   with the block in progress, as the app goes to the background or ends;
   not again within a second (one swipe away sends both) */
void window_perf_rank_lifecycle(void *user, int terminating);
/* the last block's replays whose submit and present ran on the submit
   thread, for the replays line: "" when none, else "; the CPU adds the
   submit thread's submit and present in N of M replays"; the length */
size_t window_perf_rank_cpu_note(char *out, size_t cap);

#endif /* ICO_PLATFORM_WINDOW_PERF_RANK_H */
