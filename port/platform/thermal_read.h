/*
 * port/platform/thermal_read.h
 *
 * The phone's heat and clock state for the log (issue 179: whether the
 * boat scene's slow stretches are the phone throttling; its GPU clock and
 * load are not readable there).  Read once a second on Android's log
 * thread beside the battery (window_stall.c asks_tick, power_read.h),
 * read only, each read a small file:
 *   - the system's thermal status (AThermal_getCurrentThermalStatus, API
 *     30) and headroom (AThermal_getThermalHeadroom, API 31: 1.0 is the
 *     SEVERE throttling threshold), through libandroid.so looked up at run
 *     time as the performance hints are (window_stall.c);
 *   - up to THERMAL_ZONES_MAX of /sys/class/thermal/thermal_zone*: those
 *     whose type names the GPU, the CPU, the SoC, the battery or the skin
 *     (in that order of choice), their temperature;
 *   - each CPU cluster's clock (scaling_cur_freq), its allowed most
 *     (scaling_max_freq: lowered below cpuinfo_max_freq is the throttle's
 *     signature) and its hardware most, from cpufreq's policy folders, or
 *     each core's cpufreq folder where there are no policies (cores with
 *     the same hardware most in a row taken as one cluster).
 * What is readable is said once:
 *   thermal: status and headroom from the system (API 31); zones read: 3
 *     of 42 (gpu0-usr 43.2 C, cpu-1-0-usr 45.0 C, battery 31.0 C; the
 *     others' types are not GPU, CPU, SoC, battery or skin); CPU clusters
 *     read: cores 0-5 (most 1800 MHz), cores 6-7 (most 2000 MHz)
 * or each part's reason ("not readable (errno 13)", "API 29: the thermal
 * API is API 30 and later").  A read that fails after working is silent.
 *
 * The values show in perf_secs' per-second line, in each stall's table
 * row and in one line a 10 s block (perf_secs.h).
 *
 * The parsers, the choice, the scans over a folder tree (root "" on the
 * device; a fixture tree in the test) and the texts are here
 * (port/platform/test/thermal_read_test.c); the system calls and the
 * thread are window_stall.c's.
 */
#ifndef ICO_PLATFORM_THERMAL_READ_H
#define ICO_PLATFORM_THERMAL_READ_H

#include <stdbool.h>
#include <stddef.h>

#define THERMAL_ZONES_MAX 4    /* zones read a second */
#define THERMAL_ZONES_SCAN 128 /* thermal_zoneN looked at */
#define THERMAL_CLUSTERS 4     /* CPU clusters read a second */
#define THERMAL_CPUS 32        /* cpuN looked at */
#define THERMAL_PATH 160

/* The thermal status (ATHERMAL_STATUS_*, 0 none .. 6 shutdown) */
#define THERMAL_STATUS_NONE_READ (-2) /* not read */

/* the zones chosen */
typedef struct ThermalZones {
    int err;           /* 0, or the errno of the folder (no zone chosen) */
    unsigned seen;     /* thermal_zoneN folders found */
    unsigned readable; /* of them with a readable type and temperature */
    unsigned n;        /* chosen */
    char type[THERMAL_ZONES_MAX][24];
    char path[THERMAL_ZONES_MAX][THERMAL_PATH]; /* the temp file */
} ThermalZones;

/* the CPU clusters found */
typedef struct ThermalCpus {
    int err;     /* 0, or the errno met (no cluster) */
    bool policy; /* from cpufreq's policy folders (else each core's) */
    unsigned n;
    int first[THERMAL_CLUSTERS], last[THERMAL_CLUSTERS];
    int hwMaxMhz[THERMAL_CLUSTERS];
    char dir[THERMAL_CLUSTERS][THERMAL_PATH]; /* the folder with scaling_cur_freq */
} ThermalCpus;

/* one second's reading */
typedef struct ThermalSample {
    int status;      /* THERMAL_STATUS_NONE_READ, or 0..6 */
    double headroom; /* < 0 not read */
    int hottest;     /* the hottest zone read (its index), -1 none */
    double hottestC;
    unsigned zones;
    double zoneC[THERMAL_ZONES_MAX]; /* < -100: not read this second */
    unsigned clusters;
    int curMhz[THERMAL_CLUSTERS], maxMhz[THERMAL_CLUSTERS]; /* <= 0 not read */
} ThermalSample;

void thermal_sample_none(ThermalSample *s);

/* a zone type's rank of choice (0 gpu, 1 cpu, 2 soc, 3 battery, 4 skin;
   case ignored, anywhere in the type), -1 none of these */
int thermal_type_rank(const char *type);
/* a temperature file's text: millidegrees (most kernels; a value of 1000
   or more), else whole degrees; false with no number */
bool thermal_temp_c(const char *text, double *c);
/* a cpufreq file's kHz as MHz; false with no number */
bool thermal_khz_mhz(const char *text, int *mhz);
/* a cpu list ("0-3", "4 5 6 7", "0-1,4"): its lowest and highest core;
   false with none */
bool thermal_cpu_list(const char *text, int *first, int *last);

/* the scans under root ("" the device's own folders) */
void thermal_zones_scan(const char *root, ThermalZones *z);
void thermal_cpus_scan(const char *root, ThermalCpus *c);
/* this second's zones and clusters into s (the status and headroom are
   the caller's); true when any value was read */
bool thermal_sample_read(const ThermalZones *z, const ThermalCpus *c, ThermalSample *s);

/* The once line (after "window: "): api the SDK level (0 not Android),
   statusWhy/headroomWhy "" when read (else why not) */
size_t thermal_read_open_text(const ThermalZones *z, const ThermalCpus *c,
                              const ThermalSample *first, int api, const char *statusWhy,
                              const char *headroomWhy, char *out, size_t cap);
/* a second's compact token ("1/0.67/45.2/1200:1800,2000:2000": status,
   headroom, hottest C, each cluster's clock:its allowed most; "-" not
   read) */
size_t thermal_token(const ThermalSample *s, char *out, size_t cap);
/* the block's line (after "window: ") over its seconds' samples (n, those
   with known false left out): "thermal in the block: status 0-1
   (changed), headroom 0.61-0.67, hottest zone 44.0-46.5 C; cores 0-5
   600-1800 MHz, allowed 1800-1800 MHz of 1800; cores 6-7 ...; the allowed
   most changed: no"; 0 with no sample */
size_t thermal_read_block_text(const ThermalSample *s, const bool *known, unsigned n,
                               const ThermalCpus *c, char *out, size_t cap);

#endif
