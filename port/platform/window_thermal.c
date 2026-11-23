/*
 * port/platform/window_thermal.c
 *
 * The window's side of the phone's heat and clock state
 * (window_thermal.h).
 */
#include "window_thermal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ico_thread.h" /* ico_thread_now_ms */
#include "perf_secs.h"
#include "power_read.h" /* power_fresh */
#include "sys_cell.h"
#ifdef __ANDROID__
#include <dlfcn.h>
#include <SDL3/SDL.h>
#endif

/* the last reading: {status, headroom, hottest, hottest C, clusters, the
   clusters' clocks, their allowed most} (THERMAL_CLUSTERS 4: 13 values) */
static SysCell s_cell;
/* written by the log thread before s_ready's release, read after it */
static ThermalCpus s_cpus;
#ifdef __ANDROID__
static char s_open[1024]; /* the once line */
#endif
static int s_ready; /* __atomic */

#ifdef __ANDROID__ /* the log thread's, on Android alone */
static void pack(const ThermalSample *s, double *v)
{
    v[0] = (double)s->status;
    v[1] = s->headroom;
    v[2] = (double)s->hottest;
    v[3] = s->hottestC;
    v[4] = (double)s->clusters;
    for (int i = 0; i < THERMAL_CLUSTERS; i++) {
        v[5 + i] = (double)s->curMhz[i];
        v[5 + THERMAL_CLUSTERS + i] = (double)s->maxMhz[i];
    }
}
#endif

#define PACKED (5 + 2 * THERMAL_CLUSTERS)

static void unpack(const double *v, ThermalSample *s)
{
    thermal_sample_none(s);
    s->status = (int)v[0];
    s->headroom = v[1];
    s->hottest = (int)v[2];
    s->hottestC = v[3];
    s->clusters = (unsigned)v[4];
    for (int i = 0; i < THERMAL_CLUSTERS; i++) {
        s->curMhz[i] = (int)v[5 + i];
        s->maxMhz[i] = (int)v[5 + THERMAL_CLUSTERS + i];
    }
}

#ifdef __ANDROID__
/* android/thermal.h's calls (API 30, the headroom API 31), looked up in
   libandroid.so as the performance hints' are (perf_hint_android.c): the
   port's minimum is API 26.  Neither needs a permission (thermal.h: only
   the listener calls can answer EPERM) */
typedef struct AThermalManager AThermalManager;
typedef AThermalManager *(*AcquireFn)(void);
typedef int (*StatusFn)(AThermalManager *);
typedef float (*HeadroomFn)(AThermalManager *, int);

static struct {
    bool looked;
    AThermalManager *manager;
    StatusFn status;
    HeadroomFn headroom;
    char statusWhy[96], headroomWhy[96];
} s_ndk;

static void ndk_lookup(int api)
{
    s_ndk.looked = true;
    if (api < 30) {
        snprintf(s_ndk.statusWhy, sizeof s_ndk.statusWhy,
                 "not asked (API %d: the thermal API is API 30 and later)", api);
        snprintf(s_ndk.headroomWhy, sizeof s_ndk.headroomWhy,
                 "not asked (API %d: the headroom is API 31 and later)", api);
        return;
    }
    void *lib = dlopen("libandroid.so", RTLD_NOW);
    AcquireFn acquire = lib != NULL ? (AcquireFn)dlsym(lib, "AThermal_acquireManager") : NULL;
    s_ndk.status = lib != NULL ? (StatusFn)dlsym(lib, "AThermal_getCurrentThermalStatus") : NULL;
    s_ndk.headroom =
        lib != NULL && api >= 31 ? (HeadroomFn)dlsym(lib, "AThermal_getThermalHeadroom") : NULL;
    s_ndk.manager = acquire != NULL ? acquire() : NULL;
    if (s_ndk.manager == NULL) {
        s_ndk.status = NULL;
        s_ndk.headroom = NULL;
        snprintf(s_ndk.statusWhy, sizeof s_ndk.statusWhy, "not asked (%s)",
                 lib == NULL ? "libandroid.so not opened" : "no thermal manager");
        snprintf(s_ndk.headroomWhy, sizeof s_ndk.headroomWhy, "%s", s_ndk.statusWhy);
        return;
    }
    if (s_ndk.status == NULL) {
        snprintf(s_ndk.statusWhy, sizeof s_ndk.statusWhy,
                 "not asked (no AThermal_getCurrentThermalStatus)");
    }
    if (s_ndk.headroom == NULL) {
        snprintf(s_ndk.headroomWhy, sizeof s_ndk.headroomWhy,
                 api < 31 ? "not asked (API %d: the headroom is API 31 and later)"
                          : "not asked (no AThermal_getThermalHeadroom, API %d)",
                 api);
    }
}

void window_thermal_tick(double atMs, double javaHeadroom, int javaStatus)
{
    static ThermalZones s_zones;
    static bool s_scanned, s_any;
    ThermalSample s;
    thermal_sample_none(&s);
    const int api = SDL_GetAndroidSDKVersion();
    if (!s_ndk.looked) {
        ndk_lookup(api);
    }
    if (s_ndk.status != NULL) {
        const int st = s_ndk.status(s_ndk.manager);
        s.status = st >= 0 && st <= 6 ? st : THERMAL_STATUS_NONE_READ;
    } else if (javaStatus >= 0) {
        s.status = javaStatus;
    }
    if (javaHeadroom >= 0.0 && !isnan(javaHeadroom)) {
        s.headroom = javaHeadroom; /* PowerManager's answer this second: the same service's */
    } else if (s_ndk.headroom != NULL) {
        const float h = s_ndk.headroom(s_ndk.manager, 0);
        s.headroom = !isnan(h) && h >= 0.0f ? (double)h : -1.0;
    }
    if (!s_scanned) {
        /* the first tick: which zones and clusters (bounded: at most
           THERMAL_ZONES_SCAN zones and THERMAL_CPUS cores looked at once) */
        s_scanned = true;
        thermal_zones_scan("", &s_zones);
        thermal_cpus_scan("", &s_cpus);
    }
    const bool any = thermal_sample_read(&s_zones, &s_cpus, &s);
    if (!s_any) {
        s_any = true;
        (void)thermal_read_open_text(&s_zones, &s_cpus, &s, api,
                                     s_ndk.status != NULL || javaStatus >= 0 ? "" : s_ndk.statusWhy,
                                     s.headroom >= 0.0 ? "" : s_ndk.headroomWhy, s_open,
                                     sizeof s_open);
        __atomic_store_n(&s_ready, 1, __ATOMIC_RELEASE);
    }
    if (!any) {
        return; /* nothing this second: silent */
    }
    double v[PACKED];
    pack(&s, v);
    sys_cell_put(&s_cell, atMs, 0.0, v, PACKED);
}
#else
void window_thermal_tick(double atMs, double javaHeadroom, int javaStatus)
{
    (void)atMs;
    (void)javaHeadroom;
    (void)javaStatus; /* read on Android alone */
}
#endif

static double s_seen;
static ThermalSample s_last; /* the game thread's copy (window_thermal_take) */

void window_thermal_take(void)
{
    double v[PACKED];
    if (sys_cell_get_new(&s_cell, v, PACKED, &s_seen) != PACKED) {
        return;
    }
    unpack(v, &s_last);
    perf_secs_window_phone(&s_last, ico_thread_now_ms());
}

/* any thread (the stall rows, the where lines, which the lifecycle thread
   can reach too: REV4R N5): a consistent copy from the cell itself
   (sys_cell's sequence), not window_thermal_take's game-thread copy */
bool window_thermal_now(ThermalSample *out)
{
    double v[PACKED], at = -1.0, took = 0.0;
    if (sys_cell_get(&s_cell, v, PACKED, &at, &took) != PACKED || at < 0.0 ||
        !power_fresh(at, ico_thread_now_ms())) {
        return false;
    }
    unpack(v, out);
    return true;
}

const ThermalCpus *window_thermal_cpus(void)
{
    return __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) && s_cpus.n > 0 ? &s_cpus : NULL;
}

void window_thermal_open_line(void)
{
    static int s_said; /* __atomic: the line once, whichever thread asks first */
    if (__atomic_load_n(&s_said, __ATOMIC_ACQUIRE)) {
        return;
    }
#ifdef __ANDROID__
    if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) {
        return; /* not read yet */
    }
    if (__atomic_exchange_n(&s_said, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    fprintf(stderr, "window: %s\n", s_open);
#else
    if (__atomic_exchange_n(&s_said, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    fprintf(stderr, "window: thermal: not read here (the thermal API, the thermal zones and the "
                    "CPU clocks are read on Android alone)\n");
#endif
}
