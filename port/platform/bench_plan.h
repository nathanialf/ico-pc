/*
 * port/platform/bench_plan.h
 *
 * The rc-only bench's pure parts (window_bench.h is the window's side,
 * port/render/rd_bench.h the renderer's, port/render/rd_bench_run.h the
 * loop that runs one kept frame): which pictures of the opening are kept,
 * the order the variants run in, the statistics over the timed replays and
 * the lines and CSV rows they make.  Pure C (no SDL, no renderer), tested
 * by bench_plan_test.  Compiled into the program only with ICO_RC_TOOLS on
 * Android (the root CMakeLists.txt's "if(ICO_RC_TOOLS AND ANDROID)" block,
 * tools/check_rc_text.py checks it), and into rd_replay_tool and the tests.
 *
 * The schedule.  Variant 0 is the baseline (the frame as drawn); 1 .. m
 * are the others.  Each of R rounds runs every other variant once, in the
 * order 1 .. m turned by o_r = r m / R (integer division) places: o_r + 1,
 * ..., m, 1, ..., o_r.  A round begins and ends with a baseline block and
 * holds one more after every `every` variants (not after its last), so each
 * variant's block lies between two baseline blocks of its own round.  A
 * block is W warm-up replays (not timed: the variant's pipelines, its
 * uploads and the frames in flight of the block before) then K timed ones.
 *
 * The statistics.  A quantile is type 7 (linear between the order
 * statistics: h = (n - 1) p, x[floor h] + (h - floor h) (x[floor h + 1] -
 * x[floor h])).  A variant's picture time is the median and quartiles of
 * its timed replays over all rounds; its delta is the mean over rounds of
 * (its block's median - the mean of the medians of the nearest baseline
 * blocks before and after it in that round with a sample; one side alone
 * when the other has none), so a drift slower than a block cancels.  A
 * part's saving is minus its delta.  The baseline's drift is the spread of
 * its blocks' medians over the frame's run.
 */
#ifndef ICO_PLATFORM_BENCH_PLAN_H
#define ICO_PLATFORM_BENCH_PLAN_H

#include <stddef.h>
#include <stdint.h>

#define ICO_BENCH_VARIANTS 48 /* the baseline included */
#define ICO_BENCH_ROUNDS 3
#define ICO_BENCH_K_MAX 16
/* the run's numbers in the window (rd_replay_tool takes its own) */
#define ICO_BENCH_W 3
#define ICO_BENCH_K 10
#define ICO_BENCH_K_LEAST 5
#define ICO_BENCH_R 2
#define ICO_BENCH_EVERY 4
/* the time one frame's blocks may take, from which K is cut on a slow
   device, and the whole bench's cap */
#define ICO_BENCH_FRAME_BUDGET_MS 22000.0
#define ICO_BENCH_TOTAL_CAP_MS 150000.0
#define ICO_BENCH_BLOCKS (ICO_BENCH_ROUNDS * (ICO_BENCH_VARIANTS + ICO_BENCH_VARIANTS / 2 + 2))
/* frames kept a run at most, and the bytes of one */
#define ICO_BENCH_FRAMES 5
#define ICO_BENCH_FRAME_BYTES (64u << 20)

/* ------------------------------------------------------------ the picks */

/* the frames' kinds: four moments of the opening and the paused picture */
enum {
    ICO_BENCH_LAMPS_VOLUME, /* a lamps picture with a shadow volume */
    ICO_BENCH_LAMPS_PLAIN,  /* a lamps picture without one */
    ICO_BENCH_DARK,         /* a dark picture (no lamp) */
    ICO_BENCH_SUN,          /* the glow's whole chain (the sun) */
    ICO_BENCH_PAUSE,        /* the paused picture (on demand) */
    ICO_BENCH_KINDS
};

/* a picture's glow plan (the window maps RD_GLOW_LIGHT_* onto these) */
enum { ICO_BENCH_PLAN_NONE, ICO_BENCH_PLAN_DARK, ICO_BENCH_PLAN_LAMPS, ICO_BENCH_PLAN_WHOLE };

#define ICO_BENCH_LAMPS_RUN 60u /* lamps pictures in a row before a lamps frame */
#define ICO_BENCH_DARK_RUN 60u  /* dark pictures in a row before the dark frame */
#define ICO_BENCH_SUN_RUN 20u   /* whole-chain pictures in a row before the sun frame */

typedef struct IcoBenchPick {
    int runPlan;   /* the plan of the run of pictures (NONE: no run) */
    uint32_t run;  /* its pictures in a row, the last one included */
    int haveFrame; /* lastFrame holds the last picture with a plan's frame */
    uint32_t lastFrame;
    unsigned used; /* bit per kind: kept (or tried and refused) */
    int armed;     /* the kind waiting for the pump to keep it, -1 none */
    uint32_t armedFrame;
} IcoBenchPick;

void ico_bench_pick_init(IcoBenchPick *p);
/* One replay's record: in the opening or not, its plan, whether it drew a
   shadow volume, its frame.  A picture with no plan of the same frame as
   the last plan-bearing one is a later present of that tick (passed over);
   of a new frame it ends the run.  Outside the opening every run ends and
   an armed kind is dropped.  Arms the first unused kind the picture meets
   (LAMPS: more than ICO_BENCH_LAMPS_RUN lamps pictures in a row, the
   kind by its volume; DARK and SUN likewise), and while one is armed a
   later picture of the same kind moves the arm to its frame (the pump keeps
   the frame only while the renderer still holds it).  Returns the kind
   armed or moved now, -1 none. */
int ico_bench_pick_picture(IcoBenchPick *p, int inOpening, int plan, int withVolume,
                           uint32_t frame);
/* the pump kept the armed frame (ok) or could not (the kind is used either
   way, so a refused frame is not tried again); the arm is dropped */
void ico_bench_pick_done(IcoBenchPick *p, int ok);
/* "a lamps picture with a shadow volume", ... */
const char *ico_bench_kind_name(int kind);
/* "lamps", "dark", ... (RdPerfRecord.glowLight's RD_GLOW_LIGHT_* order:
   none, dark, lamps, whole, off) */
const char *ico_bench_plan_name(uint32_t glowLight);

/* ---------------------------------------------------------- the schedule */

typedef struct IcoBenchBlock {
    uint8_t variant, round;
} IcoBenchBlock;

/* The blocks of `variants` variants (the baseline as 0) over `rounds`
   rounds, a baseline after every `every` (the head comment), into out
   (max blocks); the count, 0 when it does not fit or an argument is out of
   range (variants 1 .. ICO_BENCH_VARIANTS, rounds 1 .. ICO_BENCH_ROUNDS,
   every >= 1). */
int ico_bench_schedule(int variants, int rounds, int every, IcoBenchBlock *out, int max);
/* K for blocks of w + K replays of about pictureMs each so that `blocks`
   of them take at most budgetMs: floor(budgetMs / (pictureMs blocks)) - w,
   kept to [least, most]; most when pictureMs is not above 0 */
int ico_bench_k(double pictureMs, int blocks, int w, double budgetMs, int least, int most);

/* ----------------------------------------------------------- statistics */

/* the type 7 quantile p of n values (a sorted copy; n 0: 0) */
double ico_bench_quantile(const float *v, int n, double p);

typedef struct IcoBenchStat {
    double median, q1, q3;
    int n;
} IcoBenchStat;

IcoBenchStat ico_bench_stat(const float *v, int n);

/* one timed replay */
typedef struct IcoBenchSample {
    float pictureMs; /* present to present: this replay's start less the one before's */
    float gpuMs;     /* the GPU span (first stamp to last), gpuValid */
    float waitMs;    /* the render thread's wait for the GPU (the slot two pictures back) */
    uint32_t passes, draws, tris;
    uint16_t plan, noVolume, emptyComposites, merged, crackFills, presentScene;
    uint8_t gpuValid;
    uint8_t excluded; /* a pipeline made in it, or not presented: left out */
} IcoBenchSample;

/* a variant as the lines name it */
typedef struct IcoBenchVariantInfo {
    char name[96];
    uint8_t knock;    /* a knock-out: ranked by what removing it saves */
    uint8_t sum;      /* 1 a list, 2 a part of the parts' sum check, 0 neither */
    uint32_t removed; /* commands made NOPs */
} IcoBenchVariantInfo;

/* One frame's run: the variants, the schedule, every block's timed samples */
typedef struct IcoBenchRun {
    int frame, frames; /* this frame's place, 1-based, of how many */
    int kind;
    uint32_t number; /* the frame's number */
    int w, k, rounds, every;
    int nVariants;
    IcoBenchVariantInfo info[ICO_BENCH_VARIANTS];
    int nBlocks;
    IcoBenchBlock block[ICO_BENCH_BLOCKS];
    IcoBenchSample s[ICO_BENCH_BLOCKS][ICO_BENCH_K_MAX];
    uint8_t ns[ICO_BENCH_BLOCKS];
    uint32_t excluded; /* timed samples left out */
} IcoBenchRun;

typedef struct IcoBenchResult {
    IcoBenchStat pic, gpu, wait;
    double passes, draws, tris; /* medians */
    double delta, deltaLo, deltaHi;
    int deltaRounds;     /* rounds with a delta (0: none) */
    IcoBenchSample last; /* the last timed sample (its cut counters) */
    int ran;             /* timed samples kept */
} IcoBenchResult;

typedef struct IcoBenchDrift {
    double first, last, lo, hi;
    int blocks;
} IcoBenchDrift;

/* every variant's result and the baseline's drift */
void ico_bench_results(const IcoBenchRun *run, IcoBenchResult *out, IcoBenchDrift *drift);
/* the variants that ran at least one kept timed replay */
int ico_bench_variants_ran(const IcoBenchResult *res, int n);

/* ---------------------------------------------------------------- lines */

/* The lines, each without an end of line, into out (size); the length
   written, as snprintf (cut to size).
   head      "bench: frame F of N: <kind> (frame number, C commands, B KB):
             V variants, R rounds of W warm-up and K timed replays each, a
             baseline after every E; present mode P; GPU stamps yes/no"
   variant   "bench: frame F | <name> | P passes, D draws, T triangles |
             picture M ms (Q1-Q3, n N) | GPU G ms (Q1-Q3) | wait W ms | +D
             ms against the baselines (rounds +A to +B) | removed C | cuts:
             plan lamps, no volume 1, composites skipped 3, volumes merged
             1, crack fills 1, SCENE presented 0"
   drift     "bench: frame F: the baseline over B blocks: first M ms, last
             M ms, from L to H ms (drift D ms)"
   rank      "bench: frame F: what removing each part saves (the picture
             M ms): <name> S ms, ..." from the part at `from` (sorted by
             saving, most first), at most `count` parts; *next the place
             after the last written (the caller stops at the knock-outs'
             count)
   sum       "bench: frame F: the lists' savings add up to L ms of the
             picture's M ms (P %), the parts' (the shadow window, the
             composites, the fog with the crack fill it arms, the glow's
             parts, the aura, the other effects, the reduction) to Q ms
             (P %); what neither holds is the present, the uploads, the
             passes' own loads and stores and what a part enables in a
             later pass" */
int ico_bench_head_line(char *out, size_t size, const IcoBenchRun *run, uint32_t commands,
                        uint32_t kb, const char *presentMode, int gpuStamps);
int ico_bench_variant_line(char *out, size_t size, const IcoBenchRun *run, int v,
                           const IcoBenchResult *r);
int ico_bench_drift_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchDrift *d);
/* the knock-outs with a delta, sorted by saving (most first, then the
   variant's number); the count written into order (max) */
int ico_bench_rank(const IcoBenchRun *run, const IcoBenchResult *res, int *order, int max);
int ico_bench_rank_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchResult *res,
                        const int *order, int n, int from, int count);
int ico_bench_sum_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchResult *res);
/* the CSV's header and one row a variant (the variant's name quoted) */
int ico_bench_csv_header(char *out, size_t size);
int ico_bench_csv_row(char *out, size_t size, const IcoBenchRun *run, int v,
                      const IcoBenchResult *r, const IcoBenchDrift *d);

#endif /* ICO_PLATFORM_BENCH_PLAN_H */
