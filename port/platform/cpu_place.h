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
 * says so).  The window uses it on Android only, for the render thread and
 * the submit thread (the render thread alone on a single fastest core, the
 * submit thread on the next ones: cpu_place_split), with [dev] big_cores =
 * false as the diagnostic escape.
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
/* "0-3, 7"; "none" for 0; the length written */
size_t cpu_place_mask_text(uint64_t mask, char *out, size_t cap);

/* the 10 s line's part: "on cores G:4 (78 %) R:7 (64 %) S:5 (90 %)", a
   thread with no samples "G:-", one with no core known "G:not known"; with
   slow != 0 then ", on the slowest cores (0-3) G 0 % R 12 % S 3 %"; s
   NULL leaves S out; the length written */
size_t cpu_place_part(char *out, size_t cap, const CpuPlaceHist *g, const CpuPlaceHist *r,
                      const CpuPlaceHist *s, uint64_t slow);

/* the calling thread kept to the cores of mask; false with why set (not
   Linux, an empty mask, the system's refusal) */
bool cpu_place_pin(uint64_t mask, char *why, size_t cap);
/* the cores the calling thread may run on; 0 when not known */
uint64_t cpu_place_allowed(void);
/* the calling thread's scheduling as the system has it: "nice -10" on
   Linux and Android (with ", real-time" under a real-time policy), "" where
   not known; the length written */
size_t cpu_place_priority_text(char *out, size_t cap);

#endif
