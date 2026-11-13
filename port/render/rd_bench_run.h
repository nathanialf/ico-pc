/*
 * port/render/rd_bench_run.h
 *
 * The rc-only bench's loop over one kept frame (rd_bench.h has the frame
 * and its variants, port/platform/bench_plan.h the schedule, the
 * statistics and the lines): the variants made once each untimed (their
 * pipelines, uploads and targets made before any timed replay), K chosen
 * from the baseline's picture time, then the schedule's blocks, each
 * variant set at the block's start with the threads idle, W warm-up and K
 * timed replays posted one a present as play posts them.  Each replay's
 * record (rd_perf_pop, in replay order, GPU times two replays later) gives
 * its sample: the picture time as the start of its replay less the start of
 * the replay before (both of the block), the GPU span, the render thread's
 * wait for the GPU, the passes, draws and triangles executed and the cuts
 * that fired.  Three baseline replays at the end bring the last records in;
 * every switch is restored and the frame's commands made whole again.
 *
 * Not in ico_render: compiled into rd_replay_tool (its --bench) and, with
 * ICO_RC_TOOLS on Android alone, into the program (window_bench.c).
 */
#ifndef ICO_RD_BENCH_RUN_H
#define ICO_RD_BENCH_RUN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../platform/bench_plan.h"

struct RdBenchFrame;
struct RdFrame;

/* the frame's number in the bench's replays (the renderer's lines and the
   records): this plus the kept frame's own, so no record of the game's own
   replays is taken for one */
#define RD_BENCH_NUMBER_BASE 0x40000000u

enum {
    RD_BENCH_DONE,
    RD_BENCH_SKIPPED, /* the player tapped */
    RD_BENCH_LEFT,    /* the app is leaving the screen (background, quit) */
    RD_BENCH_LOST,    /* the device was lost or a replay could not be posted */
    RD_BENCH_CAPPED,  /* the whole bench's time cap */
    RD_BENCH_FAILED   /* nothing to run (no variant, no memory) */
};

typedef struct RdBenchRunCb {
    /* between replays: 0 go on, RD_BENCH_SKIPPED or RD_BENCH_LEFT to stop */
    int (*poll)(void *user);
    /* the on-screen words (at each block's start, the threads idle) */
    void (*status)(void *user, const char *text);
    /* a "bench: " line, and a CSV row */
    void (*line)(void *user, const char *text);
    void (*csv)(void *user, const char *text);
    void *user;
} RdBenchRunCb;

typedef struct RdBenchRunCfg {
    int w, k, rounds, every;
    double frameBudgetMs; /* K cut to keep one frame's blocks inside it */
    double capMs;         /* the whole bench's cap, from startMs (rd__now_ms) */
    double startMs;
    const char *presentMode;
    bool csvHeader; /* the CSV's header row first */
} RdBenchRunCfg;

typedef struct RdBenchRunOut {
    int variants, ran; /* planned, with a kept timed replay */
    int stopped;       /* RD_BENCH_* */
    double ms;
} RdBenchRunOut;

/* one frame (frame of frames, its bench_plan.h kind): returns out->stopped */
int rd_bench_run_frame(struct RdBenchFrame *b, int frame, int frames, int kind,
                       const RdBenchRunCfg *cfg, const RdBenchRunCb *cb, RdBenchRunOut *out);

/* The window's run (window_bench.c, which sees no renderer internals): each
   kept frame's dump bytes loaded (written to tmpPath, read back, removed),
   run, and freed again, in order, until one stops the bench; frames whose
   load fails are said so and passed over.  out sums the frames (variants,
   ran) and holds the first stop; the frames run in *framesRun. */
typedef struct RdBenchKept {
    const void *bytes;
    size_t len;
    int kind;
    uint32_t number;
} RdBenchKept;

int rd_bench_run_kept(const RdBenchKept *kept, int n, const char *tmpPath, const RdBenchRunCfg *cfg,
                      const RdBenchRunCb *cb, RdBenchRunOut *out, int *framesRun);
/* "done", "skipped by a tap", ... */
const char *rd_bench_stop_words(int stopped);

/* rd_replay_tool --bench K: the tool's loaded frame (no --interp) through
   the bench with W 1, K timed replays a block, 1 round, the lines on
   stdout; the presented picture of a baseline replay before it and after it
   compared byte for byte (when two baseline replays before it give the
   same bytes: a frame with a feedback effect differs replay to replay and
   is said so).  0 when every variant ran and the pictures are the same */
int rd_bench_tool(const struct RdFrame *f, int k, bool present);

#endif /* ICO_RD_BENCH_RUN_H */
