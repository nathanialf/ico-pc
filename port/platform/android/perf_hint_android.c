/*
 * port/platform/android/perf_hint_android.c
 *
 * The NDK's performance hint calls as a perf_hint.h backend
 * (perf_hint_android.h).  Every session call is made on the hint thread
 * alone (window_perf_hint.c): the NDK says a session's methods are not
 * thread safe.
 */
#include "perf_hint_android.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <SDL3/SDL.h>

typedef struct APerfManager APerfManager;
typedef struct APerfSession APerfSession;
typedef struct AWorkDur AWorkDur;

/* android/performance_hint.h's signatures */
typedef APerfManager *(*GetManagerFn)(void);
typedef APerfSession *(*CreateFn)(APerfManager *, const int32_t *, size_t, int64_t);
typedef int64_t (*RateFn)(APerfManager *);
typedef int (*UpdateFn)(APerfSession *, int64_t);
typedef int (*ReportFn)(APerfSession *, int64_t);
typedef void (*CloseFn)(APerfSession *);
typedef int (*SetThreadsFn)(APerfSession *, const pid_t *, size_t);
typedef int (*Report2Fn)(APerfSession *, AWorkDur *);
typedef AWorkDur *(*WdCreateFn)(void);
typedef void (*WdReleaseFn)(AWorkDur *);
typedef void (*WdSetFn)(AWorkDur *, int64_t);

static struct {
    bool looked, ok;
    PerfHintAndroidInfo info;
    APerfManager *manager;
    CreateFn create;
    UpdateFn update;
    ReportFn report;
    CloseFn close;
    SetThreadsFn setThreads;
    Report2Fn report2;
    WdReleaseFn wdRelease;
    WdSetFn wdStart, wdTotal, wdCpu, wdGpu;
    AWorkDur *wd; /* the hint thread's, reused for every report */
} s_a;

static void *a_create(void *u, const int32_t *tids, size_t n, int64_t targetNs)
{
    (void)u;
    return s_a.create(s_a.manager, tids, n, targetNs);
}

static int a_update(void *u, void *s, int64_t targetNs)
{
    (void)u;
    return s_a.update((APerfSession *)s, targetNs);
}

static int a_report(void *u, void *s, const PerfHintWork *w, int64_t totalNs)
{
    (void)u;
    if (s_a.wd != NULL) {
        s_a.wdStart(s_a.wd, w->startNs);
        s_a.wdTotal(s_a.wd, totalNs);
        s_a.wdCpu(s_a.wd, w->cpuNs);
        s_a.wdGpu(s_a.wd, w->gpuNs > 0 ? w->gpuNs : 0);
        return s_a.report2((APerfSession *)s, s_a.wd);
    }
    return s_a.report((APerfSession *)s, totalNs);
}

static int a_set_threads(void *u, void *s, const int32_t *tids, size_t n)
{
    pid_t t[PERF_HINT_MAX_THREADS];

    (void)u;
    n = n > PERF_HINT_MAX_THREADS ? PERF_HINT_MAX_THREADS : n;
    for (size_t i = 0; i < n; i++) {
        t[i] = (pid_t)tids[i];
    }
    return s_a.setThreads((APerfSession *)s, t, n);
}

static void a_close(void *u, void *s)
{
    (void)u;
    s_a.close((APerfSession *)s);
}

static uint64_t a_now(void *u)
{
    struct timespec ts;

    (void)u;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void lookup(void)
{
    PerfHintAndroidInfo *in = &s_a.info;
    void *lib;
    GetManagerFn getManager;
    RateFn rate;
    WdCreateFn wdCreate;

    s_a.looked = true;
    in->api = SDL_GetAndroidSDKVersion();
    in->rateNs = -1;
    if (in->api < 33) {
        snprintf(in->why, sizeof(in->why),
                 "API level %d: the performance hint API is Android 13 (API 33) and later",
                 in->api);
        return;
    }
    lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib == NULL) {
        snprintf(in->why, sizeof(in->why), "libandroid.so not opened");
        return;
    }
    getManager = (GetManagerFn)dlsym(lib, "APerformanceHint_getManager");
    s_a.create = (CreateFn)dlsym(lib, "APerformanceHint_createSession");
    rate = (RateFn)dlsym(lib, "APerformanceHint_getPreferredUpdateRateNanos");
    s_a.update = (UpdateFn)dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
    s_a.report = (ReportFn)dlsym(lib, "APerformanceHint_reportActualWorkDuration");
    s_a.close = (CloseFn)dlsym(lib, "APerformanceHint_closeSession");
    if (getManager == NULL || s_a.create == NULL || rate == NULL || s_a.update == NULL ||
        s_a.report == NULL || s_a.close == NULL) {
        snprintf(in->why, sizeof(in->why),
                 "libandroid.so has no APerformanceHint_%s on API level %d",
                 getManager == NULL   ? "getManager"
                 : s_a.create == NULL ? "createSession"
                 : rate == NULL       ? "getPreferredUpdateRateNanos"
                 : s_a.update == NULL ? "updateTargetWorkDuration"
                 : s_a.report == NULL ? "reportActualWorkDuration"
                                      : "closeSession",
                 in->api);
        return;
    }
    /* API 34 and 35: optional, each used only when found */
    s_a.setThreads = (SetThreadsFn)dlsym(lib, "APerformanceHint_setThreads");
    s_a.report2 = (Report2Fn)dlsym(lib, "APerformanceHint_reportActualWorkDuration2");
    wdCreate = (WdCreateFn)dlsym(lib, "AWorkDuration_create");
    s_a.wdRelease = (WdReleaseFn)dlsym(lib, "AWorkDuration_release");
    s_a.wdStart = (WdSetFn)dlsym(lib, "AWorkDuration_setWorkPeriodStartTimestampNanos");
    s_a.wdTotal = (WdSetFn)dlsym(lib, "AWorkDuration_setActualTotalDurationNanos");
    s_a.wdCpu = (WdSetFn)dlsym(lib, "AWorkDuration_setActualCpuDurationNanos");
    s_a.wdGpu = (WdSetFn)dlsym(lib, "AWorkDuration_setActualGpuDurationNanos");
    if (s_a.report2 != NULL && wdCreate != NULL && s_a.wdRelease != NULL && s_a.wdStart != NULL &&
        s_a.wdTotal != NULL && s_a.wdCpu != NULL && s_a.wdGpu != NULL) {
        s_a.wd = wdCreate();
    }
    s_a.manager = getManager();
    if (s_a.manager == NULL) {
        snprintf(in->why, sizeof(in->why),
                 "no performance hint manager (APerformanceHint_getManager answered none)");
        if (s_a.wd != NULL) {
            s_a.wdRelease(s_a.wd);
            s_a.wd = NULL;
        }
        return;
    }
    in->rateNs = rate(s_a.manager);
    in->withGpu = s_a.wd != NULL;
    in->setThreads = s_a.setThreads != NULL;
    s_a.ok = true;
}

bool perf_hint_android_ops(PerfHintOps *ops, PerfHintAndroidInfo *info)
{
    if (!s_a.looked) {
        lookup();
    }
    *info = s_a.info;
    if (!s_a.ok) {
        return false;
    }
    memset(ops, 0, sizeof(*ops));
    ops->create = a_create;
    ops->update_target = a_update;
    ops->report = a_report;
    ops->set_threads = s_a.setThreads != NULL ? a_set_threads : NULL;
    ops->close = a_close;
    ops->now_ns = a_now;
    ops->withGpu = s_a.wd != NULL;
    return true;
}

void perf_hint_android_release(void)
{
    if (s_a.wd != NULL) {
        s_a.wdRelease(s_a.wd);
        s_a.wd = NULL;
    }
    /* the next ops (a window opened again) report without the GPU's time:
       not made again (the window is opened once a run on Android) */
    s_a.info.withGpu = false;
}
