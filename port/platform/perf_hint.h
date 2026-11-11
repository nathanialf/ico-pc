/*
 * port/platform/perf_hint.h
 *
 * Android's performance hints (the owner's choice for v0.11.5: the system
 * clocks the CPU, and where it can the GPU, for the work a picture takes,
 * rather than for how busy the threads look).  The NDK's hint sessions
 * (android/performance_hint.h, API 33 and later, looked up at run time:
 * port/platform/android/perf_hint_android.c) are made for the threads that
 * make a picture (the game thread, the render thread, the submit thread;
 * not the audio thread), with a target duration of one picture and a
 * report of each picture's actual work.  Nothing here changes the picture,
 * the resolution or the frame rate; no call waits for the GPU.
 *
 * This file is the pure part (no Android, no SDL), run by
 * port/platform/test/perf_hint_test.c against a fake backend:
 *   - the target: the pacer's own planned time a picture
 *     (perf_hint_target_ns, from what window_present.c plans with: the
 *     vsync lock's period / n, the timer path's present gap, the display's
 *     refresh where the pacer plans no gap, Frame rate Original's frame);
 *   - a picture's work from its performance record (perf_hint_work): the
 *     CPU time of the render and submit threads' work on it, the record's
 *     time less its waits (the GPU fence waits, the image acquire, the
 *     present call, which waits for the compositor's queue), and the GPU
 *     time of the last record the renderer timed;
 *   - the hand-over: the game thread (where the records are drained,
 *     window_host.c perf_drain) pushes each picture into a ring with plain
 *     atomic stores and sets the target and the thread ids the same way:
 *     no lock, no system call; the hint thread ("ico-perf-hint",
 *     window_perf_hint.c) makes every call on the session (the NDK says a
 *     session's methods are not thread safe; its reports may be a binder
 *     call), timed for the 10 s line;
 *   - the session's life (perf_hint_step): made once the thread ids and a
 *     target are known, its threads changed when the render thread is made
 *     again (APerformanceHint_setThreads from API 34, else closed and made
 *     again), its target updated when the pacer's changes, closed while the
 *     app is in the background and made again on its return; refused once,
 *     never asked again (the port runs as before and says so once);
 *   - the line texts.
 */
#ifndef ICO_PLATFORM_PERF_HINT_H
#define ICO_PLATFORM_PERF_HINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PERF_HINT_RING 128u      /* pictures waiting for the hint thread (a power of two) */
#define PERF_HINT_MAX_THREADS 3u /* game, render, submit */

/* --- the target ---------------------------------------------------------- */

/* What the pacer plans a picture with (window_present.c window_present_pace):
   original: Frame rate Original (no interpolation: one picture a game
   frame, frameSteps steps of periodNs, 0 not known: one); lockN: the vsync
   lock's presents a step (0: not locked); gapNs: the timer path's present
   gap (pace_policy.h pace_present_gap, 0 under the FIFO lock or uncapped
   with vsync off and an immediate mode: back to back); refreshNs the
   display's refresh (0 not known); periodNs the simulation step's period
   (20 ms PAL, 16.683 ms NTSC). */
typedef struct PerfHintPace {
    bool original;
    unsigned lockN, frameSteps;
    uint64_t periodNs, gapNs, refreshNs;
} PerfHintPace;

enum {
    PERF_HINT_BASIS_ORIGINAL = 0, /* Original: a game frame */
    PERF_HINT_BASIS_LOCK,         /* the vsync lock: the period / n */
    PERF_HINT_BASIS_GAP,          /* the timer path's present gap */
    PERF_HINT_BASIS_REFRESH,      /* no gap planned: the display's refresh */
    PERF_HINT_BASIS_PERIOD        /* none of those known: the step's period */
};

/* The target in ns, in that order: Original's frame; the lock's period /
   n; the gap; the refresh; the period.  *basis (when not NULL) which. */
uint64_t perf_hint_target_ns(const PerfHintPace *p, int *basis);
/* "a game frame", "the vsync lock's period / 2", "the pacer's present
   gap", "the display's refresh", "the step's period" */
const char *perf_hint_basis_words(int basis);

/* --- a picture's work ------------------------------------------------------ */

/* From a performance record (rd.h RdPerfRecord, copied by the window):
   totalMs the replay's time with the submit thread's submit and present,
   presentMs, fenceWaitMs and acquireMs inside it, startMs the replay's
   start on CLOCK_MONOTONIC (rd's clock), gpuMs the timed GPU time
   (rd_perf_pace_gpu_ms: the span less the display wait) when gpuValid. */
typedef struct PerfHintRecord {
    bool presented, gpuValid;
    double totalMs, presentMs, fenceWaitMs, acquireMs, startMs, gpuMs;
} PerfHintRecord;

/* One picture for the session.  gpuNs: this picture's GPU time (gpuOwn)
   or the last timed picture's (0: none timed yet) */
typedef struct PerfHintWork {
    int64_t startNs, cpuNs, gpuNs;
    bool gpuOwn;
} PerfHintWork;

/* The work of a presented picture: cpuNs = (total - present - fence waits
   - acquire) clamped at 0; false (nothing to report) for a replay that
   presented nothing, a start not known, or no CPU time left.  *lastGpuNs
   is the last timed GPU time, updated by a timed record (also when the
   picture itself is not reported). */
bool perf_hint_work(const PerfHintRecord *r, int64_t *lastGpuNs, PerfHintWork *out);
/* The total a report gives: max(cpu, gpu) when the reports carry a GPU
   time (a pipelined picture is held by the slower side; the target is a
   rate), else the CPU's */
int64_t perf_hint_total_ns(const PerfHintWork *w, bool withGpu);

/* --- the backend (the NDK's, or a test's) ------------------------------- */

typedef struct PerfHintOps {
    void *user;
    /* the session, NULL refused */
    void *(*create)(void *user, const int32_t *tids, size_t n, int64_t targetNs);
    /* 0, or the call's error (EINVAL, EPIPE, ...) */
    int (*update_target)(void *user, void *session, int64_t targetNs);
    /* totalNs the picture's (perf_hint_total_ns); the GPU time used only
       with withGpu */
    int (*report)(void *user, void *session, const PerfHintWork *w, int64_t totalNs);
    /* NULL: not offered (API 33): the session is closed and made again */
    int (*set_threads)(void *user, void *session, const int32_t *tids, size_t n);
    void (*close)(void *user, void *session);
    /* the clock the calls are timed by (monotonic ns) */
    uint64_t (*now_ns)(void *user);
    bool withGpu; /* the reports carry the GPU time (API 35) */
} PerfHintOps;

/* The 10 s block's numbers (perf_hint_take: since the last take) */
typedef struct PerfHintStats {
    unsigned reports, over, gpuReports, gpuOwn, failed, dropped, background;
    unsigned targetChanges, threadChanges;
    int lastError;
    double cpuMs, gpuMs; /* sums over reports, gpuReports */
    double callMs, callMaxMs;
} PerfHintStats;

typedef struct PerfHint {
    PerfHintOps ops;
    /* the game thread's side (atomics: written there, read on the hint
       thread) */
    uint64_t targetNs;
    int active;
    uint32_t tidGen; /* odd while the ids are written */
    int32_t tids[PERF_HINT_MAX_THREADS];
    uint32_t tidN;
    int32_t wTids[PERF_HINT_MAX_THREADS]; /* the writer's last ids */
    uint32_t wTidN;
    uint32_t head, tail; /* the ring: head the game thread's, tail the hint thread's */
    PerfHintWork ring[PERF_HINT_RING];
    uint32_t droppedW; /* full ring, the writer's count (atomic) */
    /* the hint thread's */
    void *session;
    uint64_t sessTargetNs;
    uint32_t sessGen;
    int32_t sessTids[PERF_HINT_MAX_THREADS];
    uint32_t sessN;
    bool refused, everMade;
    unsigned made;
    double madeMs;     /* the last create call's time */
    uint32_t droppedR; /* droppedW already counted */
    /* the block's numbers, under the spin (the hint thread adds, the game
       thread takes) */
    int spin;
    PerfHintStats st;
} PerfHint;

/* ops copied; active, nothing known yet */
void perf_hint_init(PerfHint *h, const PerfHintOps *ops);

/* The game thread's calls: no lock, no system call. */
void perf_hint_set_target(PerfHint *h, uint64_t targetNs);
/* n ids (at most PERF_HINT_MAX_THREADS; n 0: not all known yet) */
void perf_hint_set_threads(PerfHint *h, const int32_t *tids, uint32_t n);
/* false: the app is in the background (the session closed at the next
   step), true: back (made again) */
void perf_hint_set_active(PerfHint *h, bool active);
/* false when the ring is full (counted as dropped) */
bool perf_hint_push(PerfHint *h, const PerfHintWork *w);

/* The hint thread's step: the session's life and the ring's reports.
   Returns PERF_HINT_EV_* bits for the caller's lines. */
enum {
    PERF_HINT_EV_MADE = 1,    /* a session made (the first, or again after the background) */
    PERF_HINT_EV_REFUSED = 2, /* the create call answered none: off for the run */
    PERF_HINT_EV_CLOSED = 4   /* closed for the background */
};

unsigned perf_hint_step(PerfHint *h);
/* the session closed (the thread's end) */
void perf_hint_shutdown(PerfHint *h);
/* the block's numbers since the last take (any thread) */
void perf_hint_take(PerfHint *h, PerfHintStats *out);

/* --- the lines ----------------------------------------------------------- */

/* GameManager.getGameMode: 0 unsupported, 1 standard, 2 performance, 3
   battery, 4 custom; -1 not asked (before Android 12), -2 the call failed */
const char *perf_hint_game_mode_words(int mode);

/* "performance hints: on (API 35; session of 3 threads: game 9001, render
   9040, submit 9041; target 7.81 ms a picture (the pacer's present gap);
   GPU durations reported yes; game mode standard (1); the system's update
   rate 16.67 ms, the reports made every 16 ms off the game and render
   threads; made in 0.42 ms)"; again: "on again (...)" after the
   background */
typedef struct PerfHintOnText {
    int api, gameMode, basis;
    bool again, withGpu;
    const char *const *names; /* the threads' names, n of them */
    const int32_t *tids;
    uint32_t n;
    uint64_t targetNs;
    int64_t rateNs; /* the system's preferred update rate (0 or less: none) */
    unsigned wakeMs;
    double madeMs;
} PerfHintOnText;

size_t perf_hint_on_text(const PerfHintOnText *t, char *out, size_t cap);

/* "performance hints: 612 reports (actual CPU 4.12 ms / GPU 18.40 ms of the
   7.81 ms target; over target in 598), the report calls 3.10 ms in all
   (longest 0.21 ms), target changed 0 times" and, when they happened,
   "; GPU this picture's own in 40 of 612, the last timed one's otherwise",
   "; threads changed 1 times", "; 2 failed (error 32)", "; 5 dropped (the
   queue full)", "; 30 not reported (in the background)".  withGpu false:
   "GPU not reported (API 33 and 34)".  targetNs the session's now. */
size_t perf_hint_block_text(const PerfHintStats *s, bool withGpu, uint64_t targetNs, char *out,
                            size_t cap);

/* The hint thread's wake period: the system's preferred update rate in ms,
   clamped to 4-50 (16 when it gives none) */
unsigned perf_hint_wake_ms(int64_t rateNs);

#endif /* ICO_PLATFORM_PERF_HINT_H */
