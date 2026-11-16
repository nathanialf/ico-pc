/*
 * port/platform/stall_coincide.h
 *
 * Issue 179: what a stall coincided with (docs/port/
 * ANDROID_DIAGNOSTICS.md, "What a stall coincided with").  A stall is a
 * pass of a timed replay over 8 times its block's median for that pass
 * and over 40 ms, or a present over 100 ms.  At each one the window writes
 * one line from the last samples the log thread made (no file is read for
 * it on the game or render thread):
 *   stall: list 0 SCENE 283.3 ms against the block's median 48.0 ms
 *     (replay 2709); at that moment: the game thread on core 6 (909 MHz),
 *     the render thread on core 7 (1443 MHz); in the last second the core
 *     waits: game 60 ms, render 40 ms, submit 5 ms, SDL audio 3 ms, system
 *     audio 1 ms; the driver's threads: mali-cmar-backend ran 120 ms,
 *     waited 30 ms; memory 501 MB available against the floor 641 MB
 *     (under it); pressure some 12.3 %, full 4.1 % (avg10; some 3.2 % a
 *     second before: rising); thermal headroom 0.80; coincided with: memory
 *     under the floor, memory pressure rising
 * and at each 10 s block, beside the attempt ledger:
 *   stalls by what coincided: 3 in the block, 0 of them hangs (the GPU's
 *     own sign of a reset with a stamp unwritten) and 3 other; all 3 by
 *     class: a core wait over 100 ms 1, memory under the floor 3, memory
 *     pressure rising 0, the GPU clock low 0 (not readable), the GPU's own
 *     sign of a reset 0, none of these 0
 * Issue 123 (the Adreno 740's hangs): the line names the hung replay's
 * stamps and the submissions around the stall (which one held the first
 * unwritten stamp, each one's submit and signal from the stall's start),
 * a counter restart after a span with no GPU work is said and not counted
 * as a sign, and every stall gets its line while stalls are rare
 * (stall_line_due: STALL_RARE_LINES a block, STALL_RUN_LINES a run).
 * The rules, the medians, the parsers and the texts are pure
 * (port/platform/test/stall_coincide_test.c).
 */
#ifndef ICO_PLATFORM_STALL_COINCIDE_H
#define ICO_PLATFORM_STALL_COINCIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STALL_PASS_TIMES 8.0   /* a pass over this many times its block median */
#define STALL_PASS_MS 40.0     /* and over this */
#define STALL_PRESENT_MS 100.0 /* a present over this */
#define STALL_MEDIAN_MIN 5u    /* a pass's median needs this many of it in the block */
#define STALL_WAIT_MS 100.0    /* a core wait over this in the last second (a tenth) */
#define STALL_GPU_LOW 0.5      /* the GPU clock under this share of its fastest */
/* a timed piece "grew" at this many times its block median or more (the
   one-piece and whole-GPU classes below) */
#define STALL_GROWN 2.0

/* The block's GPU times of each kind of pass (a key: the piece's kind,
   target and list), the last STALL_RING of each, for their medians; and
   (window_stall.c) the timed records' own pieces (13 lists, the effects,
   the pre-pass, the stretch before list 0, the present) and triangles:
   room for both */
#define STALL_KEYS 64
#define STALL_RING 64

typedef struct StallMedians {
    struct {
        uint32_t key;
        uint32_t n; /* added (the ring holds the last STALL_RING) */
        float ms[STALL_RING];
    } k[STALL_KEYS];

    uint32_t used;
} StallMedians;

void stall_medians_reset(StallMedians *m);
/* the median of key's times so far in the block (-1 with fewer than
   STALL_MEDIAN_MIN), then ms added to them (a full table drops the new
   key: -1) */
double stall_medians_add(StallMedians *m, uint32_t key, double ms);
/* the same median without adding (-1 likewise, or the key not held) */
double stall_medians_peek(const StallMedians *m, uint32_t key);
/* a pass that is a stall: over STALL_PASS_TIMES its median (known, above
   0) and over STALL_PASS_MS */
bool stall_pass_spike(double ms, double median);

/* The per-stall lines (issue 123: v0.11.6's run-wide cap of 8, then every
   100th, left the reporter's stalls 9-13 with block tallies alone; its log
   of 2026-11-14 holds 13 present stalls of 365-486 ms in about 325 s, at
   most 4 stalls in one 10 s block).  A stall's line is written:
   - while stalls are rare: a block's first STALL_RARE_LINES (four times the
     reporter's busiest block) while the run has written fewer than
     STALL_RUN_LINES such lines (the reporter's rate, 13 in 325 s, is 144 an
     hour: over three hours of it);
   - past that (stalls frequent, or the run's rare lines spent): v0.11.6's
     rule over those stalls of the run, the first STALL_EXCESS_FIRST, then
     every STALL_EXCESS_EVERY-th.
   A line left out is counted in the block's tally (leftOut, said at the end
   of the tally line) and the tally counts every stall, so the totals stay
   whole */
#define STALL_RARE_LINES 16u
#define STALL_RUN_LINES 500u
#define STALL_EXCESS_FIRST 8u
#define STALL_EXCESS_EVERY 100u

/* the run's per-stall lines (stall_line_due) */
typedef struct StallLineBudget {
    unsigned long long rare;   /* lines written by the rare rule */
    unsigned long long excess; /* stalls past it (v0.11.6's rule's count) */
} StallLineBudget;

/* What the samples said at a stall: -1 (or < 0) where not known */
#define STALL_WAIT_THREADS 5 /* game, render, submit, SDL audio, system audio */
#define STALL_DRIVERS 4
#define STALL_SUBS 8 /* submissions named at a stall: two a replay over the three (rd_perf_sub.h) */

typedef struct StallMoment {
    int gameCore, renderCore;
    int gameMhz, renderMhz;
    bool haveWaits;
    double waitMs[STALL_WAIT_THREADS]; /* < 0: that thread not known */
    int drivers;
    char driverName[STALL_DRIVERS][16];
    double driverRunMs[STALL_DRIVERS], driverWaitMs[STALL_DRIVERS];
    long availMb, floorMb;
    /* memory pressure (/proc/pressure/memory avg10, %): 1 read, 0 not
       readable (pressureErr its errno), -1 not asked (not Android) */
    int pressure, pressureErr;
    double some10, full10, some10Before;
    double headroom;       /* < 0 not known */
    int gpuMhz, gpuMaxMhz; /* < 0 not readable */
    /* the GPU's own sign of a reset (rd_perf_clock.h rd_perf_reset_sign,
       the records of the stalled replay and the two before it): the newest
       record with a sign counted (resetReplay, 0 none) and its kind: it ran
       its first list twice, or the GPU's timestamp counter went back
       (restarted) with the GPU kept busy before it; and the newest with
       recorded stamps that came back unwritten after its work was
       signalled finished (hangReplay, 0 none: stampsMissing of them, the
       first named in firstMissing): a hang, the class the tally counts
       apart */
    uint32_t resetReplay;
    bool ranTwice, clockBack;
    uint32_t hangReplay;
    unsigned stampsMissing;
    char firstMissing[48];
    /* a counter restart after a span with no GPU work (a pause, a capture,
       the app away): not a sign; idleReplay 0 none, idleMs the span where
       measured (-1: the game's note of a span not presenting alone) */
    uint32_t idleReplay;
    double idleMs;
    /* the submissions around the stall (rd_perf_sub.h, the records of the
       stalled replay and the two before it; window_stall.c): subs of them,
       their times relative to the stall's start */
    unsigned subs;

    struct StallSub {
        uint32_t replay;
        uint8_t index, of; /* 1-based in its replay's submissions, of them */
        uint8_t lists;     /* command lists (0: an empty batch) */
        uint8_t present;   /* it signals its replay's present's semaphore */
        uint8_t stalledOn; /* the stalled present is its replay's: that present waits on it */
        uint8_t hung;      /* it holds its replay's first unwritten stamp */
        uint8_t named;     /* the contents below are known (a stalled replay's verdict) */
        uint8_t presentStamps;
        int8_t listLo, listHi; /* game lists whose ends it carries (-1 none) */
        uint16_t pieces, stamps, missing;
        double submitMs; /* relative to the stall's start (under 0: before it) */
        /* its signal: kind 1 seen at signalMs (its slot's wait's end), 2 at
           or before signalMs (that wait's start: it had finished), 3 at or
           before its replay's last submission's (signalMs that one's, kind
           1 or 2 there), 0 not known */
        uint8_t signalKind;
        double signalMs;
        /* its replay under the game's pause (rd.h rd_hold_replay_kind): 0
           not paused or not known, 1 drawn from the held scene (lists 11-12
           over SCENE's copy), 2 drawn in full */
        uint8_t scene;
    } sub[STALL_SUBS];

    /* Issue 179's stalls all read "none of these" (v0.11.7 log): what the
       stall line already had, judged.  The stalled replay's pieces timed
       pass by pass against their block medians (stall_pieces_judge;
       pieceN 0: the replay not timed pass by pass, or no median known):
       the piece that grew most (pieceName, pieceFactor its time over its
       median), the median of the others' factors (othersFactor, 0 with no
       other), every piece's factors' range and the pieces at STALL_GROWN or
       more (grown) */
    unsigned pieceN, grown;
    char pieceName[48];
    double pieceFactor, othersFactor, factorMin, factorMax;
    /* the GPU idle before the stalled picture's first stamp after the one
       before's last (RdPerfRecord.gpuGapNs: under 0 begun before it ended);
       idleKnown 0 not timed after another */
    bool idleKnown;
    double idleBeforeMs;
    /* the stalled picture's VU triangles and the block's median of them
       (trisKnown 0: no median yet) */
    bool trisKnown;
    double tris, trisMedian;
} StallMoment;

/* The pieces judged into m (pieceN, grown, the factors; the name left to
   the caller): n pieces' times ms[] and their block medians median[] (a
   piece with no median above 0 left out).  The index of the piece with the
   largest factor, -1 with none */
int stall_pieces_judge(StallMoment *m, const double *ms, const double *median, unsigned n);

typedef struct StallSub StallSub;

/* the moment's coincidence classes (bits 1 << STALL_CLASS_*);
   NONE alone when no other holds */
enum {
    STALL_CLASS_WAIT = 0, /* a core wait over STALL_WAIT_MS in the last second */
    STALL_CLASS_MEMORY,   /* memory under the floor */
    STALL_CLASS_PRESSURE, /* memory pressure rising (some avg10 above the second before's) */
    STALL_CLASS_GPU,      /* the GPU clock under STALL_GPU_LOW of its fastest, where readable */
    STALL_CLASS_RESET,    /* the GPU's own sign of a reset (resetReplay or hangReplay set) */
    /* one piece grew: of two or more pieces with medians, the most grown at
       STALL_GROWN times its median or more, the others' median factor under
       STALL_GROWN */
    STALL_CLASS_ONE_PIECE,
    /* the GPU slow as a whole: two or more pieces, every one at STALL_GROWN
       times its median or more */
    STALL_CLASS_GPU_WHOLE,
    STALL_CLASS_NONE,
    STALL_CLASSES
};

unsigned stall_classes(const StallMoment *m);

typedef struct StallTally {
    unsigned stalls;
    unsigned cls[STALL_CLASSES];
    bool gpuReadable; /* a moment had the GPU's clock */
    unsigned hangs;   /* of stalls, those with a hang (StallMoment.hangReplay) */
    /* the block's per-stall lines written and left out (stall_line_due) */
    unsigned written, leftOut;
} StallTally;

void stall_tally_add(StallTally *t, const StallMoment *m);
/* whether a stall's own line is written (the rules above; t->written or
   t->leftOut counts it) */
bool stall_line_due(StallTally *t, StallLineBudget *b);
/* "stalls by what coincided: 4 in the block, 3 of them hangs (...) and 1
   other; all 4 by class: ..." (after "window: "); the length written */
size_t stall_tally_text(const StallTally *t, char *out, size_t cap);
/* the moment's part after "at that moment: " and its classes; the length
   written */
size_t stall_moment_text(const StallMoment *m, char *out, size_t cap);

/* /proc/pressure/memory: "some avg10=0.00 avg60=... \nfull avg10=..."
   (len bytes, no NUL needed): both avg10s; false unless both are there */
bool stall_pressure_parse(const char *buf, size_t len, double *some10, double *full10);
/* /proc/self/status's RssAnon, RssFile and RssShmem (kB); false unless
   all three are there */
bool stall_status_parse(const char *buf, size_t len, long *anonKb, long *fileKb, long *shmemKb);
/* /proc/self/status's VmSwap (kB): the process's anonymous pages the
   kernel moved out to swap (zram on Android), which leave VmRSS while
   still allocated; false where the line is not there */
bool stall_status_swap(const char *buf, size_t len, long *swapKb);
/* a thread name like the graphics driver's (mali-..., kgsl..., adreno...,
   QGL..., pvr..., IMG..., gpu..., Turnip's tu_...): the threads the moment
   names (the process's own threads and SDL's are not) */
bool stall_driver_name(const char *name);

#endif
