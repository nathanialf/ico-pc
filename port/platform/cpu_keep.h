/* cpu_keep.h: the cores the render, game and submit threads keep to after
 * the start, kept so while the game runs (Android; render_thread.c drives
 * it from the log thread's once-a-second tick).
 *
 * The owner's Pixel 10 Pro Fold (0-1 2.2 GHz, 2-6 3.1 GHz, 7 3.8 GHz)
 * asks for the render thread on core 7 alone at its start.  In 20 of 25
 * starts the kernel answered yes and read back 2-7 (rc11:235); in the
 * starts where core 7 held (v0.11.5-rc1, rc9, rc10, rc12) the bridge's
 * reads were 6.0-6.4 ms and it ran 67-71 fps evenly, against 8.3-8.6 ms
 * and 48-59 fps with back-offs (design/BRIDGE_STABILITY.md).  Why the
 * kernel widens the pin is not in the code's reach (a vendor scheduler
 * hook on sched_setaffinity is the likely one): the keeper asks again at
 * the moments the answer can differ and logs every answer.  In rc12 (core
 * 7 kept) the cage's slow steps were the game thread's: 9-14 ms with the
 * camera moving on whichever of cores 2-6 the system gave it, sharing them
 * with the submit thread, so the game thread gets a core of its own too.
 *
 * The rule (cpu_keep_choose), from the cores' top speeds only, never from
 * their numbers: F is the allowed cores faster than the device's slowest,
 * best first (the higher speed, then the higher number):
 *   ALONE: the render thread on the best core of F not refused, alone; the
 *     game thread on the next best not refused, alone; the submit thread
 *     on the rest of F (none left: on the others' cores); every other
 *     thread of the game's on the allowed cores less those two.
 *   PAIR (two one-core pins widened in a row: the system widens one-core
 *     pins): the render thread on the two best cores of F, the game thread
 *     on the next best alone, the submit thread on the rest.
 *   FAST (a pair widened too): the start's rule as before this keeper
 *     (cpu_place_plan on the allowed cores, the render thread on all of F).
 * It applies where three or more cores are faster than the slowest (the
 * Pixel's 1 + 5 over 2, a 4 + 3 + 1 phone, 4 bigs over 4 littles): the
 * render and game threads alone still leave the submit thread a faster
 * core.  The game thread is SDL's "ico-main" (main_android.c), not
 * Android's UI thread: input and the lifecycle's callbacks arrive on the
 * UI thread, which the keeper never moves.
 *
 * With one or two faster cores (cpu_keep_few; the Helio G85's 2 + 6 of
 * issue 179) the rule is its own (the few-cores rule).  Before it the
 * start's rule kept the game and render threads to both faster cores and
 * nothing after: every thread the game thread made afterwards inherited
 * them (pthread_create(3)), so the v0.11.5 log (moto g24 power) had the
 * game, render, AudioTrack and SDLAudio threads all on cores 6-7; the
 * game and render threads each waited 0.2-1.0 s per 10 s for a core (the
 * submit thread, free on 0-7, 0.0-0.2 s; the audio threads' waits were not
 * logged), while the six slow cores were shared by everything else.  The picture is GPU-bound there (the GPU
 * 97-100 % busy, the render thread's CPU 16-21 ms of a 110 ms picture),
 * so what the two fast cores must give is short waits to the two threads
 * that feed the GPU, not more throughput:
 *   ALONE: the render thread on the best faster core alone, the game
 *     thread on the other alone (one faster core: the game thread on every
 *     allowed core, as before); the submit thread and every other thread
 *     of the process on the cores neither has (the slow cores): the audio
 *     threads, the log thread, the workers, the driver's threads and the
 *     threads made later, which the log thread moves there once a second
 *     when it finds them on a held core (cpu_keep_held,
 *     cpu_place_sweep);
 *   PAIR (a one-core pin widened, or the game thread's refused): the
 *     render and game threads share the two faster cores, the rest as
 *     ALONE;
 *   FAST: the start's rule as before (all cores for the others).
 * The other candidate, no pin at all on such devices (the kernel's
 * scheduler deciding), has no log on this class; the nearest one, the
 * rule before the start's present one (render and submit threads on the
 * two, the game thread left to the scheduler), had the game's step on the
 * slow cores 15-62 % of its samples (cpu_place.h CpuPlanKind): with
 * the fast cores busy, this phone's scheduler ran a free busy thread on
 * the slow ones.  No pin
 * would also leave the audio threads free to take the fast cores (the
 * owner's decision for v0.11.6: audio off them).  The few-cores rule
 * keeps the driver's threads (mali-cmar-backend and its kin, which feed
 * the GPU) off the two cores too: they share the six slow cores with the
 * audio threads and the workers, not the game and render threads' two.
 *
 * The keeper (cpu_keep_tick), once a second on the log thread:
 *   - reads back the render and game threads' cores; one that differs from
 *     what was read back after the last pin is a change the system made
 *     (counted, its from and to kept, a try due);
 *   - tries when asked (the start, a 3D scene begun, focus gained, back
 *     from the background, the process's or the cpuset's cores changed,
 *     the submit thread started), and every CPU_KEEP_RETRY_MS while the
 *     render thread is not on the ideal core alone;
 *   - a try asks the ideal (the rule with nothing refused) first; when
 *     the render thread's pin is not kept, the core is refused and the
 *     rule walks from the level in force; then the submit and game threads
 *     are pinned, the log thread itself, and the free mask for the other
 *     threads (ops->keep_free).
 * Pure: the system's calls through CpuKeepOps (cpu_keep_test's fake
 * kernel widens a one-core pin as the Pixel's did).
 */
#ifndef ICO_CPU_KEEP_H
#define ICO_CPU_KEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu_place.h"

/* the time between tries while the render thread is not on the ideal core */
#define CPU_KEEP_RETRY_MS 10000.0

typedef enum CpuKeepLevel { CPU_KEEP_ALONE = 0, CPU_KEEP_PAIR, CPU_KEEP_FAST } CpuKeepLevel;

typedef struct CpuKeepSets {
    CpuKeepLevel level;
    uint64_t render; /* never 0 once chosen */
    uint64_t game;   /* 0: the game thread on `others` */
    uint64_t submit; /* 0: the submit thread on `others` */
    uint64_t others; /* every other thread of the game's (the free mask) */
} CpuKeepSets;

/* three or more cores faster than the slowest: the keeper's rule applies
   (CPU_KEEP_MIN_FAST) */
enum { CPU_KEEP_MIN_FAST = 3 };

bool cpu_keep_applies(const CpuClasses *c);
/* one or two cores faster than the slowest: the few-cores rule
   (CPU_KEEP_MIN_FAST - 1 at most); cpu_keep_applies and this are never
   both true, and neither on cores of one speed */
bool cpu_keep_few(const CpuClasses *c);
/* the sets at a level from the allowed cores (0: every core with a speed),
   the refused ones (the render thread's one-core pins the system did not
   keep: neither thread is put there alone) and the game thread's own
   (its one-core pins not kept: not its core again); false when nothing can
   be chosen at that level.  Under the few-cores rule: ALONE as the header
   says (with two faster cores and the other one refused to the game
   thread, the game thread on both); PAIR the render and game threads on
   both faster cores (false with one); FAST as for three or more */
bool cpu_keep_choose(const CpuClasses *c, uint64_t allowed, uint64_t refused, uint64_t gameRefused,
                     CpuKeepLevel level, CpuKeepSets *out);

/* why a try was made (bits) */
enum {
    CPU_KEEP_WHY_START = 1u,    /* the first tick with the render thread's id, or a new thread */
    CPU_KEEP_WHY_SCENE = 2u,    /* a 3D scene began (the game's play layout) */
    CPU_KEEP_WHY_FOCUS = 4u,    /* the window gained the focus */
    CPU_KEEP_WHY_RESUME = 8u,   /* back from the background (the render thread goes on) */
    CPU_KEEP_WHY_CHANGED = 16u, /* the system changed the render or game thread's cores */
    CPU_KEEP_WHY_ALLOWED = 32u, /* the cores the process or the cpuset allows changed */
    CPU_KEEP_WHY_RETRY = 64u,   /* the 10 s retry */
    CPU_KEEP_WHY_THREAD = 128u  /* the submit thread started */
};

typedef struct CpuKeepOps {
    void *u;
    /* keep thread tid (0: the calling thread) to mask; false with *err */
    bool (*pin)(void *u, long tid, uint64_t mask, int *err);
    /* the cores tid (0: the calling thread) may run on; 0 not known */
    uint64_t (*read)(void *u, long tid);
    /* the cores every other thread of the game's runs on from now (may be
       NULL) */
    void (*keep_free)(void *u, uint64_t mask);
} CpuKeepOps;

/* one pin: asked, read back after it, what it came to, the call's errno */
typedef struct CpuKeepPin {
    uint64_t asked, got;
    CpuPinResult res;
    int err;
    bool done; /* made in the last try */
} CpuKeepPin;

typedef struct CpuKeep {
    bool on;
    bool few; /* the few-cores rule (cpu_keep_few) */
    CpuClasses classes;
    long renderTid, submitTid;         /* the threads the state is of (0: none yet) */
    bool have;                         /* sets applied */
    CpuKeepSets sets;                  /* in force */
    uint64_t allowed;                  /* as last given */
    uint64_t refused;                  /* one-core pins not kept since the ideal last held */
    uint64_t gameRefused;              /* the game thread's one-core pins not kept (the run's) */
    uint64_t renderExpect, gameExpect; /* read back after the last pins */
    uint64_t renderSeen, gameSeen;     /* the last reads */
    unsigned pending;                  /* why bits not yet tried */
    double lastTryMs;
    /* the last try */
    unsigned why;
    CpuKeepPin ideal, render, game, submit;
    bool idealKept;
    /* since the start */
    unsigned tries, kept, changes;
    uint64_t changeFrom, changeTo; /* the last change the system made (render or game) */
    bool changeGame;               /* that change was the game thread's */
    /* in the block (cpu_keep_block_reset) */
    unsigned blockTries, blockKept, blockChanges, blockReads, blockOnSet;
} CpuKeep;

/* off when neither rule applies (cpu_keep_applies, cpu_keep_few) */
void cpu_keep_init(CpuKeep *k, const CpuClasses *c);
/* One tick at nowMs (ms on any steady clock), why: the requests since the
   last tick, allowed: the cores the process and its cpuset allow (0: not
   known: every core with a speed); the threads' ids (0 or less: not
   running or not known yet: the render thread's needed for anything).
   true when a try was made (its line due) */
bool cpu_keep_tick(CpuKeep *k, const CpuKeepOps *ops, double nowMs, unsigned why, uint64_t allowed,
                   long renderTid, long submitTid, long gameTid);

/* "the start, focus gained" ("none" for 0); the length written */
size_t cpu_keep_why_text(unsigned why, char *out, size_t cap);
/* The try's line after "window: render thread cores: " (the same text
   for the same outcome: the window prints it when it changed), sys the
   system's view of the render thread (its cpuset, its clamps; "" none):
     asked 7, read back 2-7, cpuset /top-app (cores 0-7), the process may
     run on 0-7; re-pin refused (the system said yes and keeps it to cores
     2-7); on 6 alone instead (kept); the game thread on 5 alone (kept),
     the submit thread on 2-4 (kept), every other thread on 0-4, 7; why:
     the start
   the length written */
size_t cpu_keep_try_text(const CpuKeep *k, const char *sys, char *out, size_t cap);
/* The block's line after "window: render thread cores: ":
     now on 7 alone (the ideal), the game thread on 6 alone, the submit
     thread on 2-5; read back as set at 10 of 10 reads; tries in the block
     1 (the ideal kept in 1), since the start 3 (kept in 1); changed by the
     system 0 times in the block, 1 since the start (the last: the render
     thread's from 7 to 2-7); cpuset /top-app (cores 0-7)
   "off (...)" when the rule does not apply; the length written */
size_t cpu_keep_block_text(const CpuKeep *k, const char *sys, char *out, size_t cap);
void cpu_keep_block_reset(CpuKeep *k);

/* The few-cores rule's sweep: the faster cores the render and game
   threads hold in the sets in force, from which every other thread of the
   process is moved to sets.others (render_thread.c, the log thread's
   tick, cpu_place_sweep); 0 (no sweep) under the rule for three or more,
   with no sets yet, or when the others' cores would include a held one
   (FAST) */
uint64_t cpu_keep_held(const CpuKeep *k);

/* The few-cores rule's start line, after "window: cores after the start: "
   (render_thread.c place_init), from the ideal sets on the allowed cores
   (0: every core with a speed):
     kept (2 cores faster than the slowest): the render thread on 7 alone,
     the game thread on 6 alone, the submit thread and every other thread
     of the process on 0-5 (the audio threads, the log thread, the
     workers, the driver's threads and those made later: the log thread
     moves a thread found on 6-7 there once a second; Android's UI thread
     is left to the system); where the system widens a one-core pin, the
     render and the game thread share 6-7
   One faster core: "the game thread on every core it may (0-7)" and no
   widening clause.  "" when the few-cores rule does not apply; the length
   written */
size_t cpu_keep_few_text(const CpuClasses *c, uint64_t allowed, char *out, size_t cap);

#endif
