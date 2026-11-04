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
 *            and a short buffer cut.
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
    gpu_sysfs_ring_around(&r, 10120.0, 10472.0, &a);
    CHECK(a.hasBefore && a.before.tMs == 10000.0 && a.hasAfter && a.after.tMs == 10500.0);
    CHECK(a.during == 1 && a.duringClockMin == 285 && a.duringBusyMax == 9 && a.duringResets == 3);
    gpu_sysfs_around_text(&a, 2941, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 2941): 0.12 s "
                        "before: clock 905 MHz, busy 74 %, resets 3, thermal power level 0; 1 "
                        "during: clock 285 MHz, busy 9 %, resets 3; 0.03 s after: clock 285 MHz, "
                        "busy 12 %, resets 3, thermal power level 0"));
    /* a sample at the very start and end counts as before and after */
    gpu_sysfs_ring_around(&r, 10250.0, 10500.0, &a);
    CHECK(a.before.tMs == 10250.0 && a.after.tMs == 10500.0 && a.during == 0);
    /* none before; none after yet; two during with a range */
    gpu_sysfs_ring_around(&r, 9000.0, 11000.0, &a);
    CHECK(!a.hasBefore && !a.hasAfter && a.during == 4);
    gpu_sysfs_around_text(&a, 7, line, sizeof line);
    CHECK(text_is(line, "kernel GPU driver around the present that blocked (replay 7): no sample "
                        "before; 4 during: clock 285-905 MHz, busy 9-74 %, resets 3; no sample "
                        "after"));
    /* nothing read in a sample */
    gpu_sysfs_ring_reset(&r);
    s = smp(4.0, -1, -1, -1, -1);
    gpu_sysfs_ring_add(&r, &s);
    gpu_sysfs_ring_around(&r, 10.0, 20.0, &a);
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

int main(void)
{
    parse();
    ring_block();
    around();
    open_texts();
    printf("gpu_sysfs_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
