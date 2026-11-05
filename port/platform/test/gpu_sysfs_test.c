/*
 * port/platform/test/gpu_sysfs_test.c
 *
 * The kernel GPU driver's nodes, the pure part (gpu_sysfs.h, issue 123):
 *   parse    the nodes' texts: "905000000\n", "42 %\n", leading blanks, a
 *            sign, empty, no digit first, a 4 KB text, a buffer without a
 *            NUL read to its length only; Hz to MHz rounded, MHz kept;
 *   ring     64 samples kept, the oldest dropped;
 *   block    the samples after the block's start: the clock's range and
 *            mean, busy's mean, the resets in the block against the last
 *            before it, the thermal level's and the temperature's highest;
 *            nodes not read left out; an empty block;
 *   around   the last sample at or before a present's start, the first at
 *            or after its end, the ones between; none before, none after;
 *   texts    the open line (readable, not readable, not present, the once
 *            values; every node denied; the directory missing; nothing
 *            present), the block line and the around line, word for word,
 *            and a short buffer cut;
 *   issue 179, a phone without KGSL (a Mali on a Helio G85):
 *   names    the devfreq entry's name rule (mali, gpu but not a bus's or a
 *            bandwidth's, any case), the nodes of each kind, the last
 *            number of a text (GED's "13 650000");
 *   open     the devfreq entry's line (readable, not present, a quoted
 *            first text, a candidate denied), the line with no entry, the
 *            CPU clocks sampled with the GPU's or alone, not readable;
 *   cpus     the block's CPU clocks by speed class (a range, a steady
 *            class, a single core, a core never read), none read, and the
 *            around line's core before, during and after a present.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "gpu_sysfs.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

static GpuSysfsSample smp(double t, int clock, int busy, int resets, int thermal)
{
    GpuSysfsSample s;

    memset(&s, 0, sizeof(s));
    s.tMs = t;
    s.clockMhz = clock;
    s.busyPct = busy;
    s.resets = resets;
    s.thermal = thermal;
    s.tempC = GPU_SYSFS_NO_TEMP;
    return s;
}

static void parse(void)
{
    long long v = -7;
    static char big[4096];

    CHECK(gpu_sysfs_parse("905000000\n", 10, &v) && v == 905000000ll);
    CHECK(gpu_sysfs_parse("42 %\n", 5, &v) && v == 42);
    CHECK(gpu_sysfs_parse("  \t3\n", 5, &v) && v == 3);
    CHECK(gpu_sysfs_parse("-12", 3, &v) && v == -12);
    CHECK(gpu_sysfs_parse("+5", 2, &v) && v == 5);
    v = -7;
    CHECK(!gpu_sysfs_parse("", 0, &v) && v == -7);
    CHECK(!gpu_sysfs_parse("\n", 1, &v));
    CHECK(!gpu_sysfs_parse("%42", 3, &v));
    CHECK(!gpu_sysfs_parse("-", 1, &v));
    /* no NUL: only len bytes are read ("12" of "1234") */
    {
        const char digits[4] = {'1', '2', '3', '4'};
        CHECK(gpu_sysfs_parse(digits, 2, &v) && v == 12);
    }
    /* a 4 KB text: blanks and then a number at its end, and digits past
       any long long's room kept bounded */
    memset(big, ' ', sizeof big);
    memcpy(big + sizeof big - 3, "77\n", 3);
    CHECK(gpu_sysfs_parse(big, sizeof big, &v) && v == 77);
    memset(big, '9', sizeof big);
    CHECK(gpu_sysfs_parse(big, sizeof big, &v) && v > 0);
    CHECK(gpu_sysfs_mhz(905000000ll, true) == 905);
    CHECK(gpu_sysfs_mhz(284999999ll, true) == 285);
    CHECK(gpu_sysfs_mhz(285, false) == 285);
    CHECK(gpu_sysfs_mhz(-1, true) == -1);
    CHECK(strcmp(gpu_sysfs_node_name(GPU_SYSFS_DEVFREQ), "devfreq/cur_freq") == 0);
    CHECK(strcmp(gpu_sysfs_node_name(GPU_SYSFS_NODES), "?") == 0);
}

static void ring_block(void)
{
    GpuSysfsRing r;
    GpuSysfsBlock b;
    GpuSysfsSample s;
    char line[320];

    gpu_sysfs_ring_reset(&r);
    gpu_sysfs_ring_block(&r, 0.0, &b);
    CHECK(b.n == 0);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: no sample in the block"));
    /* 70 samples every 250 ms: the first 6 dropped */
    for (int i = 0; i < 70; i++) {
        s = smp(1000.0 + 250.0 * i, i % 2 ? 905 : 285, 70 + i % 3, i < 50 ? 3 : 4, i == 60 ? 2 : 0);
        gpu_sysfs_ring_add(&r, &s);
    }
    CHECK(r.count == 64);
    /* the block: samples after 1000 + 250 * 29 (the 30th on: 40 samples) */
    gpu_sysfs_ring_block(&r, 1000.0 + 250.0 * 29, &b);
    CHECK(b.n == 40 && b.clockN == 40 && b.clockMin == 285 && b.clockMax == 905);
    CHECK(b.clockMean == 595.0);
    CHECK(b.resetsIn == 1 && b.resetsAll == 4 && b.thermalMax == 2);
    CHECK(b.tempMax == GPU_SYSFS_NO_TEMP);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: clock 285-905 MHz (average 595) over 40 samples, busy "
                        "71 % on average, resets 1 in the block (4 in all), thermal power level "
                        "at most 2"));
    /* a block from the ring's start: no sample before it, the resets
       counted from its first */
    gpu_sysfs_ring_block(&r, 0.0, &b);
    CHECK(b.n == 64 && b.resetsIn == 1 && b.resetsAll == 4);
    /* only busy read, the clock steady, a temperature */
    gpu_sysfs_ring_reset(&r);
    for (int i = 0; i < 4; i++) {
        s = smp(100.0 * i, -1, 50, -1, -1);
        s.tempC = 40 + i;
        gpu_sysfs_ring_add(&r, &s);
    }
    gpu_sysfs_ring_block(&r, -1.0, &b);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: 4 samples, busy 50 % on average, temperature at most "
                        "43 C"));
    gpu_sysfs_ring_reset(&r);
    s = smp(10.0, 905, -1, -1, -1);
    gpu_sysfs_ring_add(&r, &s);
    gpu_sysfs_ring_block(&r, 0.0, &b);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: clock 905 MHz over 1 samples"));
}

static void around(void)
{
    GpuSysfsRing r;
    GpuSysfsAround a;
    GpuSysfsSample s;
    char line[400];

    gpu_sysfs_ring_reset(&r);
    s = smp(10000.0, 905, 74, 3, 0);
    gpu_sysfs_ring_add(&r, &s);
    s = smp(10250.0, 285, 9, 3, 0);
    gpu_sysfs_ring_add(&r, &s);
    s = smp(10500.0, 285, 12, 3, 0);
    gpu_sysfs_ring_add(&r, &s);
    s = smp(10750.0, 610, 70, 3, 0);
    gpu_sysfs_ring_add(&r, &s);
    /* a present from 10120 to 10472 ms (352 ms) */
    gpu_sysfs_ring_around(&r, 10120.0, 10472.0, -1, &a);
    CHECK(a.hasBefore && a.before.tMs == 10000.0 && a.hasAfter && a.after.tMs == 10500.0);
    CHECK(a.during == 1 && a.duringClockMin == 285 && a.duringBusyMax == 9 && a.duringResets == 3);
    gpu_sysfs_around_text(&a, 2941, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 2941): 0.12 s "
                        "before: clock 905 MHz, busy 74 %, resets 3, thermal power level 0; 1 "
                        "during: clock 285 MHz, busy 9 %, resets 3; 0.03 s after: clock 285 MHz, "
                        "busy 12 %, resets 3, thermal power level 0"));
    /* a sample at the very start and end counts as before and after */
    gpu_sysfs_ring_around(&r, 10250.0, 10500.0, 3, &a); /* no cores sampled: none */
    CHECK(a.before.tMs == 10250.0 && a.after.tMs == 10500.0 && a.during == 0);
    /* none before; none after yet; two during with a range */
    gpu_sysfs_ring_around(&r, 9000.0, 11000.0, -1, &a);
    CHECK(!a.hasBefore && !a.hasAfter && a.during == 4);
    gpu_sysfs_around_text(&a, 7, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 7): no sample "
                        "before; 4 during: clock 285-905 MHz, busy 9-74 %, resets 3; no sample "
                        "after"));
    /* nothing read in a sample */
    gpu_sysfs_ring_reset(&r);
    s = smp(4.0, -1, -1, -1, -1);
    gpu_sysfs_ring_add(&r, &s);
    gpu_sysfs_ring_around(&r, 10.0, 20.0, -1, &a);
    gpu_sysfs_around_text(&a, 1, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 1): 0.01 s "
                        "before: nothing read; no sample after"));
    /* a short buffer: cut, terminated, the whole length returned */
    {
        char small[20];
        const int n = gpu_sysfs_around_text(&a, 1, small, sizeof small);
        CHECK(n > 20 && strlen(small) == 19);
    }
}

static void open_texts(void)
{
    GpuSysfsInfo info;
    char line[512];

    memset(&info, 0, sizeof info);
    info.maxMhz = info.minLevel = info.maxLevel = info.resets = -1;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present"));
    CHECK(!gpu_sysfs_any_readable(&info));
    info.dirPresent = 1;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0: none of its nodes present"));
    /* SELinux denying every node */
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        info.state[i] = -EACCES;
    }
    info.state[GPU_SYSFS_CLOCK_MHZ] = 0;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0: not readable (EACCES)"));
    CHECK(!gpu_sysfs_any_readable(&info));
    /* the usual: the clock in Hz, busy, resets, thermal; temp denied */
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        info.state[i] = 1;
    }
    info.state[GPU_SYSFS_CLOCK_MHZ] = 0;
    info.state[GPU_SYSFS_DEVFREQ] = 0;
    info.state[GPU_SYSFS_TEMP] = -EACCES;
    info.resets = 3;
    info.maxMhz = 905;
    info.minLevel = 8;
    info.maxLevel = 0;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line,
                  "kernel GPU driver: /sys/class/kgsl/kgsl-3d0: readable gpuclk, "
                  "gpu_busy_percentage, reset_count (3 so far), thermal_pwrlevel, max_gpuclk, "
                  "min_pwrlevel, max_pwrlevel; not readable temp (EACCES); not present "
                  "clock_mhz, devfreq/cur_freq; the fastest clock 905 MHz, power levels 0 "
                  "(fastest) to 8; sampled every 250 ms"));
    CHECK(gpu_sysfs_any_readable(&info));
    /* only the once nodes readable: no sampling; another errno named */
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        info.state[i] = i >= GPU_SYSFS_MAX_GPUCLK ? 1 : -EPERM;
    }
    info.state[GPU_SYSFS_BUSY] = -5;
    info.minLevel = info.maxLevel = -1;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0: readable max_gpuclk, "
                        "min_pwrlevel, max_pwrlevel; not readable gpuclk (EPERM), clock_mhz "
                        "(EPERM), devfreq/cur_freq (EPERM), gpu_busy_percentage (errno 5), "
                        "reset_count (EPERM), thermal_pwrlevel (EPERM), temp (EPERM); the fastest "
                        "clock 905 MHz"));
    CHECK(!gpu_sysfs_any_readable(&info));
}

static void names(void)
{
    long long v = 0;

    CHECK(gpu_sysfs_devfreq_name_rank("13040000.mali") == 2);
    CHECK(gpu_sysfs_devfreq_name_rank("1c500000.MALI") == 2);
    CHECK(gpu_sysfs_devfreq_name_rank("gpufreq") == 1);
    CHECK(gpu_sysfs_devfreq_name_rank("13000000.GPU") == 1);
    CHECK(gpu_sysfs_devfreq_name_rank("soc:qcom,gpubw") == 0);
    CHECK(gpu_sysfs_devfreq_name_rank("gpu-bus") == 0);
    CHECK(gpu_sysfs_devfreq_name_rank("ddr_devfreq") == 0);
    CHECK(gpu_sysfs_devfreq_name_rank("kgsl-3d0") == 0);
    CHECK(gpu_sysfs_devfreq_name_rank("") == 0);
    CHECK(gpu_sysfs_devfreq_name_rank(NULL) == 0);
    CHECK(gpu_sysfs_devfreq_name_ok("13040000.mali") && !gpu_sysfs_devfreq_name_ok("dmc"));
    CHECK(gpu_sysfs_parse_last("13 650000\n", 10, &v) && v == 650000);
    CHECK(gpu_sysfs_parse_last("7", 1, &v) && v == 7);
    CHECK(gpu_sysfs_parse_last("idx -5", 6, &v) && v == -5);
    CHECK(gpu_sysfs_parse_last("45@650000000Hz", 14, &v) && v == 650000000ll);
    v = 3;
    CHECK(!gpu_sysfs_parse_last("none\n", 5, &v) && v == 3);
    CHECK(!gpu_sysfs_parse_last("", 0, &v));
    /* the kinds' nodes */
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_KGSL, GPU_SYSFS_GPUCLK));
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_KGSL, GPU_SYSFS_MAX_PWRLEVEL));
    CHECK(!gpu_sysfs_node_in(GPU_SYSFS_KIND_KGSL, GPU_SYSFS_DF_CUR));
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_DEVFREQ, GPU_SYSFS_DF_CUR));
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_DEVFREQ, GPU_SYSFS_GED_UTIL));
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_DEVFREQ, GPU_SYSFS_DF_MAX));
    CHECK(!gpu_sysfs_node_in(GPU_SYSFS_KIND_DEVFREQ, GPU_SYSFS_BUSY));
    CHECK(gpu_sysfs_node_in(GPU_SYSFS_KIND_NONE, GPU_SYSFS_MALI_UTIL));
    CHECK(!gpu_sysfs_node_in(GPU_SYSFS_KIND_NONE, GPU_SYSFS_DF_CUR));
    CHECK(!gpu_sysfs_node_in(GPU_SYSFS_KIND_NONE, GPU_SYSFS_NODES));
    CHECK(gpu_sysfs_node_sampled(GPU_SYSFS_DF_LOAD) && !gpu_sysfs_node_sampled(GPU_SYSFS_DF_MAX));
    CHECK(gpu_sysfs_node_absolute(GPU_SYSFS_GED_FREQ) &&
          !gpu_sysfs_node_absolute(GPU_SYSFS_DF_CUR));
    CHECK(strcmp(gpu_sysfs_node_name(GPU_SYSFS_GED_FREQ), "/sys/kernel/ged/hal/current_freqency") ==
          0);
}

static void devfreq_info(GpuSysfsInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->maxMhz = info->minLevel = info->maxLevel = info->resets = -1;
    info->kind = GPU_SYSFS_KIND_DEVFREQ;
    info->dirPresent = 1;
    snprintf(info->dir, sizeof(info->dir), "/sys/class/devfreq/13040000.mali");
}

static void open_devfreq(void)
{
    GpuSysfsInfo info;
    char line[768];

    /* the Helio G85's Mali: the clock and the fastest one, no load */
    devfreq_info(&info);
    info.state[GPU_SYSFS_DF_CUR] = 1;
    info.state[GPU_SYSFS_DF_MAX] = 1;
    info.maxMhz = 950;
    info.cpuMask = 0xffu;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/devfreq/13040000.mali: readable cur_freq, "
                        "max_freq; not present load, /sys/class/misc/mali0/device/utilization, "
                        "/sys/kernel/ged/hal/gpu_utilization, "
                        "/sys/kernel/ged/hal/current_freqency; the fastest clock 950 MHz; CPU "
                        "clocks of cores 0-7 sampled with it; sampled every 250 ms"));
    CHECK(gpu_sysfs_any_readable(&info) && gpu_sysfs_any_sampled(&info));
    /* load readable, its first text quoted; GED's denied */
    info.state[GPU_SYSFS_DF_LOAD] = 1;
    snprintf(info.first[GPU_SYSFS_DF_LOAD], sizeof(info.first[0]), "45@650000000Hz");
    info.state[GPU_SYSFS_GED_UTIL] = -EACCES;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/devfreq/13040000.mali: readable cur_freq, "
                        "load (\"45@650000000Hz\"), max_freq; not readable "
                        "/sys/kernel/ged/hal/gpu_utilization (EACCES); not present "
                        "/sys/class/misc/mali0/device/utilization, "
                        "/sys/kernel/ged/hal/current_freqency; the fastest clock 950 MHz; CPU "
                        "clocks of cores 0-7 sampled with it; sampled every 250 ms"));
    /* every node denied: each said (no one word for all off KGSL) */
    devfreq_info(&info);
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        info.state[i] = -EACCES;
    }
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/devfreq/13040000.mali: not readable "
                        "cur_freq (EACCES), load (EACCES), "
                        "/sys/class/misc/mali0/device/utilization (EACCES), "
                        "/sys/kernel/ged/hal/gpu_utilization (EACCES), "
                        "/sys/kernel/ged/hal/current_freqency (EACCES), max_freq (EACCES)"));
    CHECK(!gpu_sysfs_any_sampled(&info));
    /* no devfreq entry: the candidates alone, the CPU clocks on their own */
    memset(&info, 0, sizeof info);
    info.maxMhz = info.minLevel = info.maxLevel = info.resets = -1;
    info.kind = GPU_SYSFS_KIND_NONE;
    info.cpuMask = 0xffu;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present, no GPU entry in "
                        "/sys/class/devfreq; not present "
                        "/sys/class/misc/mali0/device/utilization, "
                        "/sys/kernel/ged/hal/gpu_utilization, "
                        "/sys/kernel/ged/hal/current_freqency; CPU clocks of cores 0-7 sampled "
                        "every 250 ms"));
    CHECK(!gpu_sysfs_any_readable(&info) && gpu_sysfs_any_sampled(&info));
    info.state[GPU_SYSFS_GED_UTIL] = 1;
    snprintf(info.first[GPU_SYSFS_GED_UTIL], sizeof(info.first[0]), "37");
    info.cpuMask = 0x3fu | 0x80u;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present, no GPU entry in "
                        "/sys/class/devfreq; readable /sys/kernel/ged/hal/gpu_utilization "
                        "(\"37\"); not present /sys/class/misc/mali0/device/utilization, "
                        "/sys/kernel/ged/hal/current_freqency; CPU clocks of cores 0-5, 7 "
                        "sampled with it; sampled every 250 ms"));
    /* KGSL missing (the old reading) with the CPU clocks denied */
    memset(&info, 0, sizeof info);
    info.maxMhz = info.minLevel = info.maxLevel = info.resets = -1;
    info.cpuErr = -EACCES;
    gpu_sysfs_open_text(&info, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present; CPU clocks not "
                        "readable (EACCES)"));
    CHECK(!gpu_sysfs_any_sampled(&info));
}

static void cpus(void)
{
    static const uint32_t g85[8] = {1800000, 1800000, 1800000, 1800000,
                                    1800000, 1800000, 2000000, 2000000};
    static const int little[4] = {1000, 1800, 1500, 1700}, big[4] = {1600, 2000, 2000, 2000};
    GpuSysfsRing r;
    GpuSysfsBlock b;
    GpuSysfsAround a;
    GpuSysfsSample s;
    char line[512];

    gpu_sysfs_ring_reset(&r);
    gpu_sysfs_ring_cpus(&r, 8, g85);
    CHECK(r.cpuCount == 8 && r.cpuKhz[7] == 2000000u);
    s = smp(100.0, -1, -1, -1, -1);
    gpu_sysfs_ring_add(&r, &s); /* before the block: left out */
    for (int k = 0; k < 4; k++) {
        s = smp(1000.0 + 250.0 * k, -1, -1, -1, -1);
        for (int c = 0; c < 6; c++) {
            s.cpuMhz[c] = (uint16_t)little[k];
        }
        s.cpuMhz[6] = s.cpuMhz[7] = (uint16_t)big[k];
        gpu_sysfs_ring_add(&r, &s);
    }
    gpu_sysfs_ring_block(&r, 500.0, &b);
    CHECK(b.n == 4 && b.cpuN[0] == 4 && b.cpuMin[0] == 1000 && b.cpuMax[7] == 2000);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: 4 samples; CPU clocks: cores 0-5 1000-1800 MHz "
                        "(average 1500), cores 6-7 1600-2000 MHz (average 1900)"));
    /* with the GPU's clock and busy: after them */
    gpu_sysfs_ring_reset(&r);
    gpu_sysfs_ring_cpus(&r, 8, g85);
    for (int k = 0; k < 4; k++) {
        s = smp(1000.0 + 250.0 * k, 950, 50, -1, -1);
        for (int c = 0; c < 8; c++) {
            s.cpuMhz[c] = c == 5 ? 0 : (uint16_t)(c < 6 ? 1800 : 2000); /* core 5 never read */
        }
        gpu_sysfs_ring_add(&r, &s);
    }
    gpu_sysfs_ring_block(&r, 0.0, &b);
    CHECK(b.cpuN[5] == 0);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: clock 950 MHz over 4 samples, busy 50 % on average; "
                        "CPU clocks: cores 0-5 1800 MHz, cores 6-7 2000 MHz"));
    /* a single core's class; the rest not read at all */
    {
        static const uint32_t prime[8] = {1800000, 1800000, 1800000, 1800000,
                                          2600000, 2600000, 2600000, 3100000};

        gpu_sysfs_ring_reset(&r);
        gpu_sysfs_ring_cpus(&r, 8, prime);
        s = smp(10.0, -1, -1, -1, -1);
        s.cpuMhz[7] = 3000;
        gpu_sysfs_ring_add(&r, &s);
        gpu_sysfs_ring_block(&r, 0.0, &b);
        gpu_sysfs_block_text(&b, line, sizeof line);
        CHECK(text_is(line, "kernel GPU driver: 1 samples; CPU clocks: core 7 3000 MHz"));
    }
    /* no CPU clock read: no CPU part */
    gpu_sysfs_ring_reset(&r);
    gpu_sysfs_ring_cpus(&r, 8, g85);
    s = smp(10.0, 905, -1, -1, -1);
    gpu_sysfs_ring_add(&r, &s);
    gpu_sysfs_ring_block(&r, 0.0, &b);
    gpu_sysfs_block_text(&b, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver: clock 905 MHz over 1 samples"));
    /* the around line: the game thread's core before, during, after */
    gpu_sysfs_ring_reset(&r);
    gpu_sysfs_ring_cpus(&r, 8, g85);
    {
        static const double t[4] = {10000.0, 10250.0, 10500.0, 10750.0};
        static const int mhz[4] = {2000, 1600, 2000, 1800};

        for (int k = 0; k < 4; k++) {
            s = smp(t[k], 950, 60, -1, -1);
            s.cpuMhz[6] = (uint16_t)mhz[k];
            gpu_sysfs_ring_add(&r, &s);
        }
    }
    gpu_sysfs_ring_around(&r, 10120.0, 10472.0, 6, &a);
    CHECK(a.core == 6 && a.duringCpuMin == 1600 && a.duringCpuMax == 1600);
    gpu_sysfs_around_text(&a, 12, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 12): 0.12 s "
                        "before: clock 950 MHz, busy 60 %; 1 during: clock 950 MHz, busy 60 %; "
                        "0.03 s after: clock 950 MHz, busy 60 %; CPU core 6 (the game thread's): "
                        "2000 MHz before, 1600 MHz during, 2000 MHz after"));
    gpu_sysfs_ring_around(&r, 9000.0, 11000.0, 6, &a);
    gpu_sysfs_around_text(&a, 13, line, sizeof line);
    CHECK(strstr(line, "; no sample after; CPU core 6 (the game thread's): 1600-2000 MHz "
                       "during") != NULL);
    /* a core not sampled, or none: no CPU part */
    gpu_sysfs_ring_around(&r, 10120.0, 10472.0, 9, &a);
    CHECK(a.core == -1);
    gpu_sysfs_around_text(&a, 12, line, sizeof line);
    CHECK(strstr(line, "CPU") == NULL);
    gpu_sysfs_ring_around(&r, 10120.0, 10472.0, 3, &a);
    gpu_sysfs_around_text(&a, 12, line, sizeof line);
    CHECK(strstr(line, "CPU") == NULL); /* core 3 never read */
}

int main(void)
{
    parse();
    ring_block();
    around();
    open_texts();
    names();
    open_devfreq();
    cpus();
    printf("gpu_sysfs_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
