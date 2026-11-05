/* cpu_place.h: which core a thread runs on, the cores' speeds, a thread's
 * priority and the cores it may run on, for the window's 10 s line and its
 * start lines (render_thread.c, rd_submit_thread.c).
 *
 * A thread samples the core it is on once a picture or once a step
 * (cpu_place_now into its own CpuPlaceHist, no lock: each bin an atomic
 * count); the game thread takes the three histograms every 10 s and says
 * the core each thread ran on most, and with cores of more than one speed
 * the share of each thread's samples on the slowest ones (a render thread
 * kept on a phone's little cores shows there).  The speeds are the
 * kernel's cpuinfo_max_freq, read once (Linux and Android; elsewhere none
 * are known and the slowest-core part is left out).  The core is
 * sched_getcpu on Linux and Android, GetCurrentProcessorNumber on Windows,
 * not known on macOS.
 *
 * cpu_place_pin keeps the calling thread to a set of cores
 * (sched_setaffinity; Linux and Android only: elsewhere it does nothing and
 * says so).  The window uses it on Android only (cpu_place_plan: the game
 * and render threads, or the render and submit threads), with [dev]
 * big_cores = false as the diagnostic escape; the threads it leaves free
 * are put back on the process's cores at their entry
 * (cpu_place_thread_free).
 */
#ifndef ICO_CPU_PLACE_H
#define ICO_CPU_PLACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CPU_PLACE_MAX 64

typedef struct CpuPlaceHist {
    uint32_t n[CPU_PLACE_MAX];
    uint32_t unknown; /* samples with no core known (or past CPU_PLACE_MAX) */
} CpuPlaceHist;

/* the calling thread's core now; -1 when not known */
int cpu_place_now(void);
/* one sample (any thread may note into its own histogram while another
   takes it) */
void cpu_place_note(CpuPlaceHist *h, int cpu);
/* the samples since the last take into out, h emptied */
void cpu_place_take(CpuPlaceHist *h, CpuPlaceHist *out);
/* the core with the most samples (the lowest on a tie) and its count; -1
   and 0 with none */
int cpu_place_most(const CpuPlaceHist *h, uint32_t *count);
/* the samples in all, the unknown ones included */
uint32_t cpu_place_total(const CpuPlaceHist *h);
/* the samples on the cores of mask */
uint32_t cpu_place_in(const CpuPlaceHist *h, uint64_t mask);

typedef struct CpuClasses {
    int count;                   /* cores 0..count-1 looked at (0: none known) */
    uint32_t khz[CPU_PLACE_MAX]; /* each core's top speed; 0 not known */
} CpuClasses;

/* each core's cpufreq/cpuinfo_max_freq under root (NULL:
   /sys/devices/system/cpu), cpu0 up; count is one past the last core with
   a speed; the count returned (0 off Linux) */
int cpu_place_classes_read(const char *root, CpuClasses *c);
/* "0-3 1.8 GHz, 4-6 2.6 GHz, 7 3.1 GHz" (runs of cores of one speed, in
   core order); "not known" with none; the length written */
size_t cpu_place_classes_text(const CpuClasses *c, char *out, size_t cap);
/* the cores of the slowest speed, and those faster than it; both 0 when
   every known core has one speed or none is known */
uint64_t cpu_place_slow_mask(const CpuClasses *c);
uint64_t cpu_place_fast_mask(const CpuClasses *c);
/* The cores for the render thread and the submit thread when the fastest
   speed is a single core's and other cores are faster than the slowest
   (a phone's prime core: "0-1 2.2 GHz, 2-6 3.1 GHz, 7 3.8 GHz"): render
   that core alone, submit the other faster cores (the next class), true.
   Else both the faster cores (cpu_place_fast_mask; 0 with one speed or
   none known), false: two fastest cores or more, or the fastest the only
   one faster than the slowest (the submit thread is not put on the
   slowest cores, nor two threads on one core) */
bool cpu_place_split(const CpuClasses *c, uint64_t *render, uint64_t *submit);

/* Who keeps to which cores (the Android window's rule, a pure function of
   the cores' speeds).  Masks of 0: that thread is not kept to any.
   With 3 or more faster cores: cpu_place_split's masks for the render and
   the submit thread, the game thread free (CPU_PLAN_MANY_FAST).  With two
   faster cores (the Helio G85: 6 x 1.8 GHz and 2 x 2.0 GHz, where the
   game's own step, saturated, ran on the slow cores 15-62 % of the samples
   while the render and submit threads held the two): the game thread and
   the render thread both keep to those two cores, the submit thread free
   (CPU_PLAN_TWO_FAST).  With one faster core: the render thread alone on
   it, the game and submit threads free (CPU_PLAN_ONE_FAST; two threads
   would share the core).  No faster core: nothing kept (CPU_PLAN_NONE). */
typedef enum CpuPlanKind {
    CPU_PLAN_NONE = 0,
    CPU_PLAN_ONE_FAST,
    CPU_PLAN_TWO_FAST,
    CPU_PLAN_MANY_FAST
} CpuPlanKind;

typedef struct CpuPlan {
    CpuPlanKind kind;
    int fastCount; /* the cores faster than the slowest */
    int coreCount; /* the cores with a speed known */
    bool split;    /* MANY_FAST only: cpu_place_split's answer */
    uint64_t game, render, submit;
} CpuPlan;

void cpu_place_plan(const CpuClasses *c, CpuPlan *out);
/* "0-3, 7"; "none" for 0; the length written */
size_t cpu_place_mask_text(uint64_t mask, char *out, size_t cap);

/* the 10 s line's part: "on cores G:4 (78 %) R:7 (64 %) S:5 (90 %)", a
   thread with no samples "G:-", one with no core known "G:not known"; with
   slow != 0 then ", on the slowest cores (0-3) G 0 % R 12 % S 3 %"; s
   NULL leaves S out; the length written */
size_t cpu_place_part(char *out, size_t cap, const CpuPlaceHist *g, const CpuPlaceHist *r,
                      const CpuPlaceHist *s, uint64_t slow);

/* the calling thread kept to the cores of mask; false with why set (not
   Linux, an empty mask, the system's refusal: "errno N: its words") */
bool cpu_place_pin(uint64_t mask, char *why, size_t cap);
/* the cores the calling thread may run on; 0 when not known */
uint64_t cpu_place_allowed(void);

/* What a pin came to, by the call's answer and the cores read back after
   it (cpu_place_allowed): a phone's kernel may answer yes and keep the
   thread to other cores (a phone with one 3.8 GHz core: asked for core 7
   alone, the call succeeded, the thread may run on 2-7 and ran on cores 4
   and 6; why is not known, a vendor's scheduler hook on the call the
   likely one) */
typedef enum CpuPinResult {
    CPU_PIN_KEPT = 0, /* the cores read back are the ones asked for */
    CPU_PIN_REFUSED,  /* the call failed (its errno in why) */
    CPU_PIN_OTHER,    /* the call succeeded, the cores read back differ */
    CPU_PIN_UNKNOWN   /* the call succeeded, nothing could be read back */
} CpuPinResult;

/* the result of a pin of asked: called its answer, readBack the cores
   read back (0: none) */
CpuPinResult cpu_place_pin_result(bool called, uint64_t asked, uint64_t readBack);
/* cpu_place_pin, then the cores read back into *got (0: not known) and
   the result; why as cpu_place_pin's on CPU_PIN_REFUSED */
CpuPinResult cpu_place_pin_checked(uint64_t mask, uint64_t *got, char *why, size_t cap);
/* The cores a thread the rule leaves free runs on.  A new thread starts
   with its creator's cores (pthread_create(3): "inherits ... CPU affinity
   mask"; bionic's pthread_create goes through clone(2), which copies it
   the same way): once the game thread is kept to the two faster cores,
   every thread it makes afterwards (the submit thread, the texture decode
   pool, the pipeline workers, the pack loader, the save-state writers)
   would be kept there with it.  The window keeps the process's cores
   (cpu_place_allowed on the game thread before any pin) with
   cpu_place_keep_free; every long-lived worker calls cpu_place_thread_free
   at its entry (ico_thread_create's threads do it in ico_thread.c; the
   SDL-made ones at their own entry, the RHI's through rhi_thread_begin).
   0 forgets (nothing is reset); the mask kept, 0 with none */
void cpu_place_keep_free(uint64_t mask);
uint64_t cpu_place_free_mask(void);

typedef enum CpuFreeResult {
    CPU_FREE_NONE = 0, /* no mask kept (no pin in this process), or not Linux */
    CPU_FREE_ALREADY,  /* the calling thread already has the kept cores */
    CPU_FREE_RESET,    /* it was kept to fewer and now has the kept cores */
    CPU_FREE_FAILED    /* the call failed or the cores read back differ */
} CpuFreeResult;

/* the calling thread back on the kept cores when it has other ones; the
   resets and the failures are counted (cpu_place_free_counts), the first 8
   failures and then every 100th said on stderr ("cpu: a worker thread
   could not be put back on cores ...") */
CpuFreeResult cpu_place_thread_free(void);
void cpu_place_free_counts(unsigned *resets, unsigned *failures);

/* the calling thread's scheduling as the system has it: "nice -10" on
   Linux and Android (with ", real-time" under a real-time policy), "" where
   not known; the length written */
size_t cpu_place_priority_text(char *out, size_t cap);

/* The calling thread's CPU time so far in ns (CLOCK_THREAD_CPUTIME_ID on
   Linux, Android and macOS, GetThreadTimes on Windows); 0 where not known.
   The window reads it at the end of one pace and at the start of the next
   (window_host.c ico_window_pace): the game step's CPU time against its
   wall time.  A step far longer than its CPU time waited (for the render
   thread, a present, a core); one near it was busy. */
uint64_t cpu_place_thread_cpu_ns(void);
/* "CPU 9.8 ms of it" (the slow-step line's "slow step 64.1 ms (CPU 9.8 ms
   of it)", the 10 s game-thread line's "step 38.4 ms (CPU 12.1 ms of it;
   ..."); "" when cpuMs is 0 or less (not known); the length written */
size_t cpu_place_cpu_text(double cpuMs, char *out, size_t cap);

/* A core list as the kernel writes it ("0-5,7", Cpus_allowed_list) to a
   mask; 0 for an empty or malformed text (cores past CPU_PLACE_MAX
   dropped) */
uint64_t cpu_place_list_parse(const char *text);

/* The process's threads and the cores each may run on, read from
   /proc/self/task (root NULL) or a fake root: per thread its id, its name
   (comm) and Cpus_allowed_list (status; mask 0 when not read).  Linux and
   Android; elsewhere none (the count 0) */
#define CPU_THREADS_MAX 160

typedef struct CpuThread {
    long tid;
    char name[20];
    uint64_t mask;
} CpuThread;

typedef struct CpuThreads {
    int n;     /* held, by thread id */
    int total; /* seen (more than CPU_THREADS_MAX: the rest only counted) */
    CpuThread t[CPU_THREADS_MAX];
} CpuThreads;

int cpu_place_threads_read(const char *root, CpuThreads *out);
/* The threads grouped by the cores they may run on (issue 179: which
   threads share the two cores the game and render threads are kept to):
     "threads: 31; kept to cores 6-7: ico_pc (the game thread), ico-render;
     on 0-7: ico-submit, mali-cmar-backend, mali-utility-wo, mali-mem-purge,
     ico-gpu-sysfs and 24 more"
   The cores every thread together may run on are "on 0-7" (last but the
   threads whose cores were not read, "cores not known"); a narrower set is
   "kept to cores 6-7" ("kept to core 7" for one), the narrowest first.  In
   a group the game thread (gameTid; 0 none) comes first, then by thread
   id; one name repeated is said once with its count ("ico-tex (4)"); at
   most CPU_THREADS_NAMES names a group, then "and N more" threads.
   "threads: none read" with none; the length written */
#define CPU_THREADS_NAMES 5
size_t cpu_place_threads_format(const CpuThreads *t, long gameTid, char *out, size_t cap);
/* cpu_place_threads_read then _format */
size_t cpu_place_threads_text(const char *root, long gameTid, char *out, size_t cap);

#endif
