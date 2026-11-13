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
 *   stalls by what coincided: 3 in the block (a core wait over 100 ms 1,
 *     memory under the floor 3, memory pressure rising 0, the GPU clock
 *     low 0 (not readable), the GPU's own sign of a reset 0, none of these 0)
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

/* The block's GPU times of each kind of pass (a key: the piece's kind,
   target and list), the last STALL_RING of each, for their medians */
#define STALL_KEYS 32
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
/* a pass that is a stall: over STALL_PASS_TIMES its median (known, above
   0) and over STALL_PASS_MS */
bool stall_pass_spike(double ms, double median);
/* the GPU's sign of a reset in one replay's record: stamps unwritten after
   its work was signalled finished, its first list run twice, or the
   timestamp counter gone back */
bool stall_reset_sign(unsigned stampsMissing, bool ranTwice, uint64_t clockBackNs);

/* What the samples said at a stall: -1 (or < 0) where not known */
#define STALL_WAIT_THREADS 5 /* game, render, submit, SDL audio, system audio */
#define STALL_DRIVERS 4

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
    /* the GPU's own sign of a reset (rd_perf_clock.h, the record of the
       stalled replay or the two before it): its recorded stamps came back
       unwritten after its work was signalled finished (stampsMissing of
       them), it ran its first list twice, or the GPU's timestamp counter went
       back (restarted); resetReplay the replay that showed it, 0 none */
    uint32_t resetReplay;
    unsigned stampsMissing;
    bool ranTwice, clockBack;
} StallMoment;

/* the moment's coincidence classes (bits 1 << STALL_CLASS_*);
   NONE alone when no other holds */
enum {
    STALL_CLASS_WAIT = 0, /* a core wait over STALL_WAIT_MS in the last second */
    STALL_CLASS_MEMORY,   /* memory under the floor */
    STALL_CLASS_PRESSURE, /* memory pressure rising (some avg10 above the second before's) */
    STALL_CLASS_GPU,      /* the GPU clock under STALL_GPU_LOW of its fastest, where readable */
    STALL_CLASS_RESET,    /* the GPU's own sign of a reset (resetReplay set: issue 123's hangs) */
    STALL_CLASS_NONE,
    STALL_CLASSES
};

unsigned stall_classes(const StallMoment *m);

typedef struct StallTally {
    unsigned stalls;
    unsigned cls[STALL_CLASSES];
    bool gpuReadable; /* a moment had the GPU's clock */
} StallTally;

void stall_tally_add(StallTally *t, const StallMoment *m);
/* "stalls by what coincided: 3 in the block (...)" (after "window: "); the
   length written */
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
