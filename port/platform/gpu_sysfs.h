/*
 * port/platform/gpu_sysfs.h
 *
 * Issue 123: what the kernel's GPU driver was doing around a present that
 * blocked.  On Android with a Qualcomm GPU the kernel driver (KGSL) shows
 * its state under /sys/class/kgsl/kgsl-3d0/: the GPU's clock (gpuclk in
 * Hz, clock_mhz, or the devfreq node's cur_freq in Hz), how busy it was
 * (gpu_busy_percentage, "42 %"), the GPU resets so far (reset_count), the
 * power level the thermal limit holds it to (thermal_pwrlevel: 0 the
 * fastest, larger slower), its temperature (temp, millidegrees), and once
 * the fastest clock (max_gpuclk) and the power levels' range
 * (min_pwrlevel, max_pwrlevel).  Some devices' SELinux policy denies apps
 * these nodes; the open line says which were readable.
 *
 * The Android reader (android/gpu_sysfs_android.c) reads them every 250 ms
 * on the watchdog thread (diag_host.h ico_diag_set_poll_hook) into a ring
 * of GPU_SYSFS_RING samples on the monotonic ms clock (ico_thread_now_ms,
 * the clock of the renderer's rd__now_ms and RdPerfRecord.presentEndMs);
 * the window reads the ring on its own thread: the 10 s block's line, and
 * for each present that blocked the samples before, during and after it.
 *
 * This file is the pure part: the parser, the ring, the block and around
 * reductions and the three texts (port/platform/test/gpu_sysfs_test.c).
 * The lines ("window: " before each):
 *   kernel GPU driver: /sys/class/kgsl/kgsl-3d0: readable gpuclk,
 *     gpu_busy_percentage, reset_count (3 so far), thermal_pwrlevel; not
 *     readable temp (EACCES); not present clock_mhz; the fastest clock 905
 *     MHz, power levels 0 (fastest) to 8; sampled every 250 ms
 *   kernel GPU driver: clock 285-905 MHz (average 612) over 40 samples,
 *     busy 71 % on average, resets 0 in the block (3 in all), thermal power
 *     level at most 0
 *   kernel GPU driver around the present that blocked (replay N): 0.12 s
 *     before: clock 905 MHz, busy 74 %, resets 3; 1 during: clock 285 MHz,
 *     busy 9 %, resets 3; 0.09 s after: clock 285 MHz, busy 9 %, resets 3
 * A part whose node is not readable is left out.
 */
#ifndef ICO_PLATFORM_GPU_SYSFS_H
#define ICO_PLATFORM_GPU_SYSFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GPU_SYSFS_DIR "/sys/class/kgsl/kgsl-3d0"
#define GPU_SYSFS_RING 64u
#define GPU_SYSFS_PERIOD_MS 250u

/* the nodes, in the open line's order: sampled ones first, then the ones
   read once */
enum {
    GPU_SYSFS_GPUCLK,       /* Hz */
    GPU_SYSFS_CLOCK_MHZ,    /* MHz (read when gpuclk is not) */
    GPU_SYSFS_DEVFREQ,      /* devfreq/cur_freq, Hz (read when neither is) */
    GPU_SYSFS_BUSY,         /* gpu_busy_percentage */
    GPU_SYSFS_RESETS,       /* reset_count */
    GPU_SYSFS_THERMAL,      /* thermal_pwrlevel */
    GPU_SYSFS_TEMP,         /* temp, millidegrees C */
    GPU_SYSFS_MAX_GPUCLK,   /* once, Hz */
    GPU_SYSFS_MIN_PWRLEVEL, /* once */
    GPU_SYSFS_MAX_PWRLEVEL, /* once */
    GPU_SYSFS_NODES
};

/* a node's name under GPU_SYSFS_DIR ("gpuclk", "devfreq/cur_freq", ...) */
const char *gpu_sysfs_node_name(int node);

/* The first integer in a node's text (leading blanks skipped, a sign, then
   digits: "42 %\n" is 42, "905000000\n" 905000000); false for none (empty,
   no digit first).  buf need not end in a NUL: len bytes are read at most. */
bool gpu_sysfs_parse(const char *buf, size_t len, long long *out);
/* A clock node's value in MHz, rounded (isHz: gpuclk, cur_freq and
   max_gpuclk count Hz, clock_mhz MHz); -1 for a negative value */
int32_t gpu_sysfs_mhz(long long v, bool isHz);

/* What the reader found at the start: per node 1 readable, 0 not present
   (ENOENT), or the errno of a failed open or read (EACCES: SELinux) as a
   negative number; the once values (-1 unknown); dirPresent 0 when the
   directory itself is not there */
typedef struct GpuSysfsInfo {
    int dirPresent;
    int state[GPU_SYSFS_NODES];
    int32_t maxMhz, minLevel, maxLevel;
    int32_t resets; /* reset_count at the start (-1 unknown) */
} GpuSysfsInfo;

/* any sampled node readable (the clock, busy, resets, thermal or temp):
   the block and around lines are written only then */
bool gpu_sysfs_any_readable(const GpuSysfsInfo *info);

/* One sample: tMs on the monotonic ms clock; -1 for a node not read
   (tempC: GPU_SYSFS_NO_TEMP) */
#define GPU_SYSFS_NO_TEMP (-1000)

typedef struct GpuSysfsSample {
    double tMs;
    int32_t clockMhz, busyPct, resets, thermal, tempC;
} GpuSysfsSample;

typedef struct GpuSysfsRing {
    GpuSysfsSample s[GPU_SYSFS_RING];
    uint32_t next, count; /* the slot the next sample takes; the samples held */
} GpuSysfsRing;

void gpu_sysfs_ring_reset(GpuSysfsRing *r);
/* a sample in, the oldest dropped past GPU_SYSFS_RING */
void gpu_sysfs_ring_add(GpuSysfsRing *r, const GpuSysfsSample *s);

/* The samples after sinceMs (the 10 s block): their count, the clock's
   range and mean over the samples that have one (clockN), the mean busy
   over busyN, the resets in the block (the last less the last before the
   block, or the block's first when none before; -1 unknown) and the last
   (-1 unknown), the thermal level's and the temperature's highest (-1,
   GPU_SYSFS_NO_TEMP unknown) */
typedef struct GpuSysfsBlock {
    unsigned n, clockN, busyN;
    int32_t clockMin, clockMax;
    double clockMean, busyMean;
    int32_t resetsIn, resetsAll;
    int32_t thermalMax, tempMax;
} GpuSysfsBlock;

void gpu_sysfs_ring_block(const GpuSysfsRing *r, double sinceMs, GpuSysfsBlock *out);

/* Around [t0Ms, t1Ms] (a present's start and end): the last sample at or
   before t0 (hasBefore), the first at or after t1 (hasAfter), and the ones
   between: their count and the clock's range, the busy range and the last
   resets (-1 unknown) */
typedef struct GpuSysfsAround {
    bool hasBefore, hasAfter;
    GpuSysfsSample before, after;
    double t0Ms, t1Ms;
    unsigned during;
    int32_t duringClockMin, duringClockMax, duringBusyMin, duringBusyMax, duringResets;
} GpuSysfsAround;

void gpu_sysfs_ring_around(const GpuSysfsRing *r, double t0Ms, double t1Ms, GpuSysfsAround *out);

/* The three texts without "window: " (the length snprintf would write; a
   short buffer is cut and terminated) */
int gpu_sysfs_open_text(const GpuSysfsInfo *info, char *buf, size_t size);
int gpu_sysfs_block_text(const GpuSysfsBlock *b, char *buf, size_t size);
int gpu_sysfs_around_text(const GpuSysfsAround *a, unsigned replay, char *buf, size_t size);

#endif /* ICO_PLATFORM_GPU_SYSFS_H */
