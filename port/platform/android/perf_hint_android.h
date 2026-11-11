/*
 * port/platform/android/perf_hint_android.h
 *
 * The NDK's performance hint calls (android/performance_hint.h) as a
 * perf_hint.h backend, every symbol looked up in libandroid.so at run time
 * so the APK keeps its minSdk (26): API 33 the session, its target and the
 * plain report (APerformanceHint_reportActualWorkDuration: the CPU's time);
 * API 34 APerformanceHint_setThreads; API 35 the report with the GPU's time
 * (APerformanceHint_reportActualWorkDuration2 with an AWorkDuration).
 * APerformanceHint_setPreferPowerEfficiency (35) is not called: its default
 * (off) is what the port wants.  Compiled into libmain.so only.
 */
#ifndef ICO_PLATFORM_PERF_HINT_ANDROID_H
#define ICO_PLATFORM_PERF_HINT_ANDROID_H

#include <stdbool.h>
#include <stdint.h>
#include "perf_hint.h"

typedef struct PerfHintAndroidInfo {
    int api;        /* the device's API level */
    int64_t rateNs; /* APerformanceHint_getPreferredUpdateRateNanos (-1: none) */
    bool withGpu, setThreads;
    char why[192]; /* off: why */
} PerfHintAndroidInfo;

/* The calls looked up (once a run) and the manager asked for: true with
   *ops filled, else false with info->why.  On the game thread (the
   manager's lookup may be a binder call, once). */
bool perf_hint_android_ops(PerfHintOps *ops, PerfHintAndroidInfo *info);
/* The report's AWorkDuration released (after the session is closed) */
void perf_hint_android_release(void);

#endif /* ICO_PLATFORM_PERF_HINT_ANDROID_H */
