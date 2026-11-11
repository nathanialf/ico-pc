/*
 * port/render/rd_perf_rank.h
 *
 * The performance lines of the window's 10 s block, keyed by scene, and the
 * run's performance profile (issue 179 on the Mali-G52 MC2, issues 211 and
 * 123 on the Adreno 710 and 740): where a picture's time goes, which side
 * limits it, and the candidate savings ranked from the block's own numbers,
 * so that one log of a tile-based phone ranks the next release's
 * optimisations without a question to the reporter.
 *
 * Nothing here records, waits or reads anything of the GPU: the inputs are
 * the renderer's records (rd.h RdPerfRecord: the CPU's phases, the GPU
 * timestamps every replay already writes, read where they were always
 * read; the slot wait's CPU times) and the pieces of the replays already
 * timed pass by pass (rd_perf_passes_of, one replay every 250 ms), with the
 * attachment bytes the render passes asked for, counted on the CPU
 * (port/rhi/rhi_traffic.h).  Pure C, no globals: the window keeps the
 * block and the run (port/platform/window_perf_rank.c), the unit test is
 * port/render/test/rd_perf_rank_test.c.
 *
 * The block's lines (docs/port/ANDROID_DIAGNOSTICS.md, "The performance
 * lines"):
 *   perf scene    the scene, the presented against the game's rate, the
 *                 limiting side and its evidence (the GPU's work against the
 *                 time between presents, the render thread's own CPU, the
 *                 game step's CPU), the slot wait and the time from a
 *                 picture's last submit to its work done, the passes'
 *                 traffic a picture (bytes asked, uncompressed), the draws,
 *                 binds, vertex invocations and uploads;
 *   perf classes  the timed pieces' GPU time a picture by class (a list, an
 *                 effect, the DATE snapshots, the copies, the pre-pass, the
 *                 uploads, the present), each with what it scales with: its
 *                 passes, draws, binds, bytes loaded and stored, cleared,
 *                 area texels, copied bytes;
 *   perf rates    this device's own constants from the block (no extra
 *                 work): the byte-bound passes' fit (ms a pass + ms a MB
 *                 moved, over the DATE snapshots and the passes with no draw),
 *                 the copies' ms a MB, the vertex fit (ms + ms a thousand
 *                 invocations, keep frames left out); the rate the
 *                 estimates use (this block's fit, else an earlier block's,
 *                 else none);
 *   perf slowest  the block's slowest timed picture's top pieces;
 *   perf ranked   only below the game's rate: the three largest candidate
 *                 savings, each "measured" (a cost that is its own ceiling)
 *                 or "est." (a counter times a rate above).
 * The run's profile ("performance profile", every 300 s of the run, at the
 * end, and as the app goes to the background or ends, with the block in
 * progress): per scene the blocks, the median and worst picture, the median
 * and lowest presented rate, the limiting side's count and the ranked line
 * of its worst block.
 */
#ifndef RD_PERF_RANK_H
#define RD_PERF_RANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"

/* the classes of timed pieces: a list's render passes, an effect's, the
 * shadow count's, then each other kind (rd.h RD_PERF_PASS_*) */
#define RD_PERF_RANK_LIST0 0
#define RD_PERF_RANK_POST0 (RD_PERF_RANK_LIST0 + RD_LIST_COUNT)
#define RD_PERF_RANK_SHADOW (RD_PERF_RANK_POST0 + RD_PERF_POST_COUNT)
#define RD_PERF_RANK_KIND0 (RD_PERF_RANK_SHADOW + 1)
#define RD_PERF_RANK_CLASSES (RD_PERF_RANK_KIND0 + RD_PERF_PASS_KIND_COUNT)
/* the pictures' GPU work histogram: 1 ms bins, the last everything above */
#define RD_PERF_RANK_BINS 256
/* the slowest timed picture's pieces kept */
#define RD_PERF_RANK_SLOW_MAX 256
/* the scenes the run's profile tells apart (later ones are counted only):
   the scene key carries the stage on every kind (window_scene_key), so a
   run has more of them than one a stage; 32 of about 2.5 KB each */
#define RD_PERF_RANK_SCENES 32
/* a scene's key ("stage 4 play (st04a (...))"), its terminator counted */
#define RD_PERF_RANK_NAME 96
/* a scene's blocks whose presented rate is kept for the median */
#define RD_PERF_RANK_FPS_MAX 64
/* the ranked line's text kept per scene (the worst block's) */
#define RD_PERF_RANK_TEXT 1024

/* a piece's class (RD_PERF_RANK_*) */
int rd_perf_rank_class_of(const RdPerfPass *p);

typedef struct RdPerfRankClass {
    double ms, pieces, draws, binds;
    double colorLoadKB, colorStoreKB, depthLoadKB, depthStoreKB, clearKB, copyKB, kTexels;
    RdPerfPass first; /* the class's first piece (its label) */
    bool seen;
    /* the piece before the class's pieces (the last seen), how many had one
       and how many of those ended a full-size target (the replay's largest
       render pass area): on a tile-based GPU a piece after a full-size pass
       may carry that pass's tile work (issue 123's shadow count) */
    RdPerfPass prev;
    double prevN, prevFull;
} RdPerfRankClass;

/* A 10 s block's sums.  Zero-initialise or rd_perf_rank_block_reset. */
typedef struct RdPerfRankBlock {
    uint32_t n, keep, gpuN;
    /* the CPU's phases a record (RdPerfRecord), ms summed; ownMs the
       render thread's own (its submit only where it submitted itself),
       submitMs the render thread's submits, presentMs every present call */
    double cpuMs, ownMs, walkMs, submitMs, uploadCpuMs, bindMs, interpMs, presentMs, fenceMs;
    /* the records whose submit and present ran on the submit thread
       (RdPerfRecord.presentThread RD_PRESENT_ON_SUBMIT), and those times */
    uint32_t subN;
    double subSubmitMs, subPresentMs;
    /* the records with a present, the first and last replay's start (rd's
       ms clock): the presented rate of a block in progress */
    uint32_t presents;
    double firstStartMs, lastStartMs;
    /* the GPU: span, work (span less the display wait), the parts */
    double gpuMs, workMs, prepassMs, uploadGpuMs, presentGpuMs, displayWaitMs;
    double listMs[RD_LIST_COUNT], postMs[RD_PERF_POST_COUNT];
    /* counts summed over every record */
    double draws, pipeBinds, groupBinds, passes, dateSnaps, uploadBytes, texUploads, vuInv;
    double colorLoad, colorStore, depthLoad, depthStore, clear, texels, copy, untracked;
    /* the slot wait: records with a blocking wait, their wait; those with
       a done time, its sum and the longest */
    uint32_t waitN, doneN;
    double waitMs, waitMaxMs, doneMs, doneMaxMs;
    /* the GPU work against the vertex invocations (least squares), keep
       frames left out (a held picture's work is no vertex work's); fxMin
       the smallest x above 0 and fxMax the largest, thousands (the VU
       line's spread rule, window_scene_vu_fit) */
    uint32_t fitN;
    double fx, fy, fxx, fxy, fyy, fxMin, fxMax;
    uint32_t hist[RD_PERF_RANK_BINS]; /* the records' GPU work, 1 ms bins */
    /* the timed replays' pieces */
    uint32_t timed;
    RdPerfRankClass cls[RD_PERF_RANK_CLASSES];
    /* a tile-based GPU (set by the caller before the texts, after a reset):
       the classes line gives the stretch before list 0 and list 0 as one,
       "before list 0 + list 0" (they trade at a constant sum there, issue
       179; rd_perf.c rd_perf_pass_kind_display) */
    bool tiler;
    /* the byte-bound pieces (the DATE snapshots, the render passes with no
       draw): ms against MB moved (least squares) */
    uint32_t smN;
    double sx, sy, sxx, sxy, syy;
    double copyPieceMs, copyPieceKB; /* the copy and snapshot pieces */
    /* the scene pieces' loads, for a DATE break's reload */
    double listLoadKB, listPieces;
    /* the slowest timed picture */
    double slowMs;
    uint32_t slowReplay, slowFrame, slowN;
    RdPerfPass slow[RD_PERF_RANK_SLOW_MAX];
} RdPerfRankBlock;

void rd_perf_rank_block_reset(RdPerfRankBlock *b);
/* one record (any: GPU times or not); vuInvocations its vertex work as the
   window's VU line counts it (window_scene_vu_invocations) */
void rd_perf_rank_add_record(RdPerfRankBlock *b, const RdPerfRecord *r, double vuInvocations);
/* a timed replay's pieces (rd_perf_passes_of), with its record's GPU span */
void rd_perf_rank_add_pieces(RdPerfRankBlock *b, const RdPerfRecord *r, const RdPerfPass *p,
                             uint32_t n);

/* What the window knows of the block beside the records */
typedef struct RdPerfRankInfo {
    const char *scene;   /* "stage 3 play", "title", ... */
    double presentedFps; /* the block's presents a second */
    double gameFps;      /* the game's frames a second */
    double targetFps;    /* the rate the presents aim at (Original: the game's own) */
    double gameRateFps;  /* the game's own rate (25 or 30) */
    double stepCpuMs;    /* the game step's CPU a step */
    bool tiler;          /* a tile-based GPU (the words on the stamps) */
    bool prepass;        /* the VU pre-pass on (the vertex estimate capped at its class) */
    /* the byte-bound passes' fit's ms a MB of an earlier block of this run
       (window_perf_rank.c keeps the last that held; 0 none) */
    double earlierMbMs;
} RdPerfRankInfo;

enum {
    RD_PERF_BOUND_NONE = 0, /* at the game's rate */
    RD_PERF_BOUND_GPU,
    RD_PERF_BOUND_RENDER,  /* the render thread's own CPU */
    RD_PERF_BOUND_GAME,    /* the game thread's step */
    RD_PERF_BOUND_ELSE,    /* none of the three fills the period: waits elsewhere */
    RD_PERF_BOUND_UNKNOWN, /* no presents, no records */
    RD_PERF_BOUND_COUNT
};

/* "the GPU", "the render thread's CPU", ... */
const char *rd_perf_bound_name(int bound);

/* The block's device constants (perf rates) */
/* the vertex estimate's floor on the fit (R^2) */
#define RD_PERF_RANK_VU_R2 0.8

typedef struct RdPerfRankRates {
    bool smallFit;  /* the byte-bound passes' fit holds (8 pieces or more, a spread) */
    bool smallGood; /* and its slope is above 0 with R^2 0.3 or over: its rate is used */
    double passMs;  /* its fixed ms a pass (intercept, at least 0) */
    double mbMs;    /* its ms a MB moved (slope, at least 0) */
    double smallR2;
    uint32_t smallN;
    bool copyRate; /* the copies moved 0.5 MB or more */
    double copyMbMs;
    bool vuFit; /* the vertex fit holds (8 replays, a spread) */
    double vuBaseMs, vuPerK, vuR2;
    double vuXMin, vuXMax; /* the invocations' range, thousands a picture */
    /* the fit is good enough for an estimate: a slope above 0, R^2
       RD_PERF_RANK_VU_R2 or over, the largest x twice the smallest */
    bool vuUsable;
    /* the rate the estimates use: this block's fit's, else an earlier
       block's (earlierMbMs), else 0 (none: the copies' rate is never used
       for attachment traffic, their pieces carry other passes' flushes) */
    double mbRate;
    const char *mbFrom;
} RdPerfRankRates;

/* earlierMbMs: RdPerfRankInfo.earlierMbMs */
void rd_perf_rank_rates(const RdPerfRankBlock *b, double earlierMbMs, RdPerfRankRates *out);

/* The verdict: the bound, below the rate or not, the candidates ranked */
#define RD_PERF_RANK_TOP 3
/* every class (34 can qualify) and every estimate fit; were one more to
   come, the smallest is the one dropped */
#define RD_PERF_RANK_CANDIDATES 48

typedef struct RdPerfRankCand {
    double ms;      /* a picture */
    bool estimate;  /* "est." (a counter times a rate), else "measured" */
    char text[200]; /* "list 0 20.1 ms measured (283 draws, ...)" */
} RdPerfRankCand;

typedef struct RdPerfRankVerdict {
    int bound;
    bool below; /* the presents or the game under 95 % of their rate */
    double periodMs, workMs, ownMs, stepCpuMs, tickMs;
    uint32_t count;
    RdPerfRankCand c[RD_PERF_RANK_CANDIDATES]; /* the most first */
    /* why the GPU's candidates have no vertex estimate ("" when they have
       one, or the bound is the CPU's) */
    char vertexWhy[200];
} RdPerfRankVerdict;

void rd_perf_rank_verdict(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                          RdPerfRankVerdict *out);

/* a piece's label ("list 0", "aura", "DATE snapshot"), as the window's top
   passes print it (rd_perf_pass_label) */
typedef int (*RdPerfRankLabel)(const RdPerfPass *p, char *buf, size_t n);

/* The block's lines, without "window: " and the newline; each returns the
   length written (snprintf's count, cut to fit n) */
int rd_perf_rank_scene_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                            const RdPerfRankVerdict *v, char *buf, size_t n);
int rd_perf_rank_classes_text(const RdPerfRankBlock *b, RdPerfRankLabel label, unsigned top,
                              char *buf, size_t n);
int rd_perf_rank_rates_text(const RdPerfRankBlock *b, const RdPerfRankRates *r, char *buf,
                            size_t n);
int rd_perf_rank_slowest_text(const RdPerfRankBlock *b, RdPerfRankLabel label, unsigned top,
                              char *buf, size_t n);
/* "perf ranked: <scene>: ..." (written whatever the verdict; the window
   prints it only below the rate) */
int rd_perf_rank_ranked_text(const RdPerfRankInfo *in, const RdPerfRankVerdict *v, char *buf,
                             size_t n);

/* The run's profile by scene.  Zero-initialise. */
typedef struct RdPerfRankScene {
    char name[RD_PERF_RANK_NAME];
    uint32_t blocks, pictures;
    double seconds, targetFps;
    uint32_t bound[RD_PERF_BOUND_COUNT];
    uint32_t hist[RD_PERF_RANK_BINS];
    float fps[RD_PERF_RANK_FPS_MAX];
    uint32_t fpsN;
    double lowFps;     /* the lowest block's presented rate */
    double worstRatio; /* the worst ranked block's presented over its target */
    char worst[RD_PERF_RANK_TEXT];
} RdPerfRankScene;

typedef struct RdPerfRankRun {
    uint32_t count, overflow; /* scenes, blocks of scenes past the table */
    RdPerfRankScene s[RD_PERF_RANK_SCENES];
} RdPerfRankRun;

/* the block into its scene; ranked: its ranked line (kept when it is the
   scene's worst block below the rate so far) */
void rd_perf_rank_run_add(RdPerfRankRun *run, const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                          const RdPerfRankVerdict *v, double seconds, const char *ranked);
/* scene i's profile line (without "window: "), 0 past the count */
int rd_perf_rank_profile_text(const RdPerfRankRun *run, uint32_t i, char *buf, size_t n);
/* The block in progress, for a profile written before its 10 s end (the
   app going to the background or ending, issue 179's runs swiped away):
   "performance profile: <scene> (the block in progress): ...", its
   presented rate from its records' starts (in->presentedFps unused), the
   verdict's top candidates when below the rate; 0 with no records */
int rd_perf_rank_partial_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                              const RdPerfRankVerdict *v, char *buf, size_t n);
/* the block's presented rate from its records' starts (0 under 2 records) */
double rd_perf_rank_partial_fps(const RdPerfRankBlock *b);
/* whether a profile at the lifecycle's events is due: not when one was
   written less than RD_PERF_RANK_PROFILE_GAP_NS before (the background and
   the end of one swipe); *lastNs the last one's time (0 none), set when due */
#define RD_PERF_RANK_PROFILE_GAP_NS 1000000000ull
bool rd_perf_rank_profile_due(uint64_t *lastNs, uint64_t nowNs);

/* the histogram's median and worst picture, ms (0 with none); the worst
   is its bin's upper edge, so a picture in the top bin prints as
   "256 ms or more" (rd_perf_rank_hist_more: " or more" then, else "") */
double rd_perf_rank_hist_median(const uint32_t *hist);
double rd_perf_rank_hist_worst(const uint32_t *hist);
const char *rd_perf_rank_hist_more(const uint32_t *hist);

/* What the per-pass stamps cost, from the block's own pictures (issue 179:
   on a job-manager Mali a timestamp may end a job chain): the GPU spans of
   the pictures timed pass by pass (stamped) against those with the list
   stamps only (plain), each set's median; the sets are sorted in place.
   "pass timing's cost: GPU span median 67.4 ms with every pass stamped (37
   pictures), 64.1 ms with the list stamps only (111 pictures): +3.3 ms (an
   estimate: different pictures of one block)"; 0 and nothing written when
   either set has fewer than RD_PERF_RANK_STAMP_MIN */
#define RD_PERF_RANK_STAMP_MIN 5u
int rd_perf_rank_stamp_cost_text(float *stamped, uint32_t ns, float *plain, uint32_t np, char *buf,
                                 size_t n);

#endif /* RD_PERF_RANK_H */
