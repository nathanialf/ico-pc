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
 * on a thread of its own (started with the window, joined at its close,
 * idle while the app is in the background) into a ring
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
 *
 * Issue 179: a phone with another GPU (a Mali on a MediaTek Helio G85) has
 * no KGSL directory.  The probe then takes the /sys/class/devfreq entry
 * whose name says it is the GPU's (gpu_sysfs_devfreq_name_rank: "mali",
 * else "gpu" but not a bus's or a bandwidth's) and reads its cur_freq
 * (Hz), load and once max_freq (Hz); beside it, on any phone without KGSL,
 * the Mali driver's /sys/class/misc/mali0/device/utilization and
 * MediaTek's GED /sys/kernel/ged/hal/gpu_utilization and
 * /sys/kernel/ged/hal/current_freqency (the kernel's spelling).  Those
 * names and their texts are not verified on a device: the open line says
 * per node readable, not readable (the errno) or not present, and quotes
 * the first text of load and of the three paths, so one log says what the
 * phone has.  The CPU cores' clocks (cpufreq/scaling_cur_freq, kHz) are
 * sampled with them, on every phone (KGSL too), and the block line says
 * them by speed class (the cores of one cpuinfo_max_freq):
 *   kernel GPU driver: /sys/class/devfreq/13040000.mali: readable cur_freq,
 *     load ("45@650000000Hz"), max_freq; not present
 *     /sys/class/misc/mali0/device/utilization, ...; the fastest clock 950
 *     MHz; CPU clocks of cores 0-7 sampled with it; sampled every 250 ms
 *   kernel GPU driver: clock 285-950 MHz (average 612) over 40 samples,
 *     busy 71 % on average; CPU clocks: cores 0-5 1000-1800 MHz (average
 *     1500), cores 6-7 1600-2000 MHz (average 1900)
 * and the around line ends with the clock of the core the game thread was
 * on: "; CPU core 6 (the game thread's): 2000 MHz before, 1600-2000 MHz
 * during, 2000 MHz after".
 */
#ifndef ICO_PLATFORM_GPU_SYSFS_H
#define ICO_PLATFORM_GPU_SYSFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GPU_SYSFS_DIR "/sys/class/kgsl/kgsl-3d0"
#define GPU_SYSFS_DEVFREQ_DIR "/sys/class/devfreq"
#define GPU_SYSFS_RING 64u
#define GPU_SYSFS_PERIOD_MS 250u
/* the CPU cores whose clocks are sampled (cpu0 up) */
#define GPU_SYSFS_CPUS 16

/* where the GPU's nodes are: KGSL's directory (0, so a zeroed info is
   KGSL's), a devfreq entry of the GPU's, or neither (the candidates' paths
   alone) */
typedef enum GpuSysfsKind {
    GPU_SYSFS_KIND_KGSL = 0,
    GPU_SYSFS_KIND_DEVFREQ,
    GPU_SYSFS_KIND_NONE
} GpuSysfsKind;

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
    /* a devfreq entry's (GPU_SYSFS_KIND_DEVFREQ) */
    GPU_SYSFS_DF_CUR,  /* cur_freq, Hz */
    GPU_SYSFS_DF_LOAD, /* load: its first number, a percentage */
    /* the candidates' full paths (KIND_DEVFREQ and KIND_NONE) */
    GPU_SYSFS_MALI_UTIL, /* the Mali driver's utilization, a percentage */
    GPU_SYSFS_GED_UTIL,  /* MediaTek GED's gpu_utilization: its first number */
    GPU_SYSFS_GED_FREQ,  /* GED's current_freqency: its last number, kHz */
    GPU_SYSFS_DF_MAX,    /* the devfreq entry's max_freq, Hz, once */
    GPU_SYSFS_NODES
};

/* a node's name under its directory ("gpuclk", "devfreq/cur_freq",
   "cur_freq"), or the full path of a candidate ("/sys/kernel/ged/hal/
   gpu_utilization") */
const char *gpu_sysfs_node_name(int node);
/* the node is one of kind's (the probe reads and the open line says only
   those) */
bool gpu_sysfs_node_in(int kind, int node);
/* the node is read every sample (not once) */
bool gpu_sysfs_node_sampled(int node);
/* the node's name is a full path (a candidate) */
bool gpu_sysfs_node_absolute(int node);
/* A devfreq entry's name as the GPU's: 2 for one holding "mali", 1 for
   one holding "gpu" but not "bw" nor "bus" (Qualcomm's "gpubw" is the
   bus's bandwidth), 0 otherwise; case-insensitive.  The probe takes the
   highest, the lowest name on a tie */
int gpu_sysfs_devfreq_name_rank(const char *name);
bool gpu_sysfs_devfreq_name_ok(const char *name);

/* The first integer in a node's text (leading blanks skipped, a sign, then
   digits: "42 %\n" is 42, "905000000\n" 905000000); false for none (empty,
   no digit first).  buf need not end in a NUL: len bytes are read at most. */
bool gpu_sysfs_parse(const char *buf, size_t len, long long *out);
/* The last integer in the text ("13 650000\n" is 650000); false for none */
bool gpu_sysfs_parse_last(const char *buf, size_t len, long long *out);
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
    int kind;       /* GpuSysfsKind */
    /* the directory the relative names are under ("": GPU_SYSFS_DIR) */
    char dir[128];
    /* the first text read of load and of the candidates (unverified
       formats: the open line quotes it), "" for none */
    char first[GPU_SYSFS_NODES][24];
    /* the cores whose scaling_cur_freq was readable (bit n: cpun), and
       when none was the errno of cpu0's as a negative number (0: none
       tried) */
    uint32_t cpuMask;
    int cpuErr;
} GpuSysfsInfo;

/* any sampled node of the info's kind readable (the clock, busy, resets,
   thermal or temp): the GPU parts of the block and around lines */
bool gpu_sysfs_any_readable(const GpuSysfsInfo *info);
/* that, or a CPU clock readable: the sampler runs and the block line is
   written */
bool gpu_sysfs_any_sampled(const GpuSysfsInfo *info);

/* One sample: tMs on the monotonic ms clock; -1 for a node not read
   (tempC: GPU_SYSFS_NO_TEMP) */
#define GPU_SYSFS_NO_TEMP (-1000)

typedef struct GpuSysfsSample {
    double tMs;
    int32_t clockMhz, busyPct, resets, thermal, tempC;
    uint16_t cpuMhz[GPU_SYSFS_CPUS]; /* each core's clock; 0 not read */
} GpuSysfsSample;

typedef struct GpuSysfsRing {
    GpuSysfsSample s[GPU_SYSFS_RING];
    uint32_t next, count; /* the slot the next sample takes; the samples held */
    /* the cores sampled (0..cpuCount-1) and each one's top speed, kHz
       (cpuinfo_max_freq; 0 not known): the block line's speed classes */
    int cpuCount;
    uint32_t cpuKhz[GPU_SYSFS_CPUS];
} GpuSysfsRing;

/* the ring emptied, its cores forgotten */
void gpu_sysfs_ring_reset(GpuSysfsRing *r);
/* the cores sampled and their top speeds (count past GPU_SYSFS_CPUS
   kept at it) */
void gpu_sysfs_ring_cpus(GpuSysfsRing *r, int count, const uint32_t *khz);
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
    /* per core the samples with its clock, their range and sum (MHz), and
       the ring's cores and top speeds */
    unsigned cpuN[GPU_SYSFS_CPUS];
    int32_t cpuMin[GPU_SYSFS_CPUS], cpuMax[GPU_SYSFS_CPUS];
    double cpuSum[GPU_SYSFS_CPUS];
    int cpuCount;
    uint32_t cpuKhz[GPU_SYSFS_CPUS];
} GpuSysfsBlock;

void gpu_sysfs_ring_block(const GpuSysfsRing *r, double sinceMs, GpuSysfsBlock *out);

/* Around [t0Ms, t1Ms] (a present's start and end): the last sample at or
   before t0 (hasBefore), the first at or after t1 (hasAfter), and the ones
   between: their count and the clock's range, the busy range and the last
   resets (-1 unknown); and core's clock (the game thread's core; -1 or
   past GPU_SYSFS_CPUS: none) in the samples between (-1 unknown) */
typedef struct GpuSysfsAround {
    bool hasBefore, hasAfter;
    GpuSysfsSample before, after;
    double t0Ms, t1Ms;
    unsigned during;
    int32_t duringClockMin, duringClockMax, duringBusyMin, duringBusyMax, duringResets;
    int core;
    int32_t duringCpuMin, duringCpuMax;
} GpuSysfsAround;

void gpu_sysfs_ring_around(const GpuSysfsRing *r, double t0Ms, double t1Ms, int core,
                           GpuSysfsAround *out);

/* The three texts without "window: " (the length snprintf would write; a
   short buffer is cut and terminated) */
int gpu_sysfs_open_text(const GpuSysfsInfo *info, char *buf, size_t size);
int gpu_sysfs_block_text(const GpuSysfsBlock *b, char *buf, size_t size);
int gpu_sysfs_around_text(const GpuSysfsAround *a, unsigned replay, char *buf, size_t size);

#endif /* ICO_PLATFORM_GPU_SYSFS_H */
