/*
 * port/platform/android/gpu_sysfs_android.c
 *
 * The kernel GPU driver's nodes read on Android (gpu_sysfs_android.h).
 * Each read is open, read and close, as diag_host.c reads /proc/meminfo
 * (ico_diag_device_memory): a node's value is made at the read, and a file
 * kept open would return the first value again.  The reads run on a small
 * thread of their own ("ico-gpu-sysfs"), never on the watchdog's: a node
 * that blocks (a KGSL or devfreq node behind a lock the driver holds
 * through a fault's recovery, the moments the sampler is there to watch)
 * must not stop the GPU hang limit, nor be taken for system sleep.
 */
#include "gpu_sysfs_android.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "cpu_place.h"
#include "diag_host.h"
#include "ico_thread.h"

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static GpuSysfsRing s_ring; /* under s_lock */
static GpuSysfsInfo s_info;
static int s_started;   /* the probe made (main thread) */
static int s_sampling;  /* a sampled node readable: the hook set */
static int s_clockNode; /* the node the clock is read from, -1 none */
static int s_busyNode;  /* the node busy is read from, -1 none */
static char s_paths[GPU_SYSFS_NODES][160];
static char s_cpuPaths[GPU_SYSFS_CPUS][80]; /* each core's scaling_cur_freq */
/* the sampler thread (main thread starts and joins it); s_stop under
   s_waitLock, s_wake wakes it to stop */
static SDL_Thread *s_thread;
static SDL_Mutex *s_waitLock;
static SDL_Condition *s_wake;
static int s_stop;

/* the node's first integer (last: its last, MediaTek GED's
   current_freqency); text (NULL: not kept) its first textCap - 1 bytes,
   newlines as blanks, trailing ones dropped: 0, or the errno of the open
   or the read (a read of nothing or no digit: EINVAL) */
static int read_node_text(const char *path, long long *out, bool last, char *text, size_t textCap)
{
    char buf[64];
    ssize_t got;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return errno != 0 ? errno : EIO;
    }
    got = read(fd, buf, sizeof buf);
    const int e = got < 0 ? (errno != 0 ? errno : EIO) : 0;
    close(fd);
    if (e != 0) {
        return e;
    }
    if (text != NULL && textCap > 0) {
        size_t n = (size_t)got < textCap - 1 ? (size_t)got : textCap - 1;

        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\r')) {
            n--;
        }
        for (size_t i = 0; i < n; i++) {
            text[i] = buf[i] == '\n' || buf[i] == '\r' || buf[i] == '"' ? ' ' : buf[i];
        }
        text[n] = '\0';
    }
    if (last) {
        return gpu_sysfs_parse_last(buf, (size_t)got, out) ? 0 : EINVAL;
    }
    return gpu_sysfs_parse(buf, (size_t)got, out) ? 0 : EINVAL;
}

static int read_node(const char *path, long long *out)
{
    return read_node_text(path, out, false, NULL, 0);
}

/* the devfreq node: under the KGSL directory, else the one in
   /sys/class/devfreq whose name holds "kgsl" */
static void devfreq_path(char *out, size_t size)
{
    DIR *d;
    struct dirent *e;

    snprintf(out, size, "%s/devfreq/cur_freq", GPU_SYSFS_DIR);
    if (access(out, F_OK) == 0) {
        return;
    }
    d = opendir("/sys/class/devfreq");
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, "kgsl") != NULL) {
            snprintf(out, size, "/sys/class/devfreq/%s/cur_freq", e->d_name);
            break;
        }
    }
    closedir(d);
}

/* the GPU's devfreq entry when there is no KGSL directory: the best name
   (gpu_sysfs_devfreq_name_rank, the lowest name on a tie) into dir; false
   for none */
static bool devfreq_gpu_dir(char *dir, size_t size)
{
    DIR *d = opendir(GPU_SYSFS_DEVFREQ_DIR);
    struct dirent *e;
    char best[96] = "";
    int bestRank = 0;

    if (d == NULL) {
        return false;
    }
    while ((e = readdir(d)) != NULL) {
        const int rank = gpu_sysfs_devfreq_name_rank(e->d_name);

        if (rank > bestRank || (rank == bestRank && rank > 0 && strcmp(e->d_name, best) < 0)) {
            bestRank = rank;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);
    if (bestRank == 0) {
        return false;
    }
    snprintf(dir, size, "%s/%s", GPU_SYSFS_DEVFREQ_DIR, best);
    return true;
}

static bool is_hz(int node)
{
    return node == GPU_SYSFS_GPUCLK || node == GPU_SYSFS_DEVFREQ || node == GPU_SYSFS_MAX_GPUCLK ||
           node == GPU_SYSFS_DF_CUR || node == GPU_SYSFS_DF_MAX;
}

/* the nodes a clock is read from, in the order tried, and the busy ones */
static bool is_clock(int node)
{
    return node == GPU_SYSFS_GPUCLK || node == GPU_SYSFS_CLOCK_MHZ || node == GPU_SYSFS_DEVFREQ ||
           node == GPU_SYSFS_DF_CUR || node == GPU_SYSFS_GED_FREQ;
}

static bool is_busy(int node)
{
    return node == GPU_SYSFS_BUSY || node == GPU_SYSFS_DF_LOAD || node == GPU_SYSFS_MALI_UTIL ||
           node == GPU_SYSFS_GED_UTIL;
}

/* a read node's value as the sample keeps it */
static int32_t value_of(int node, long long v)
{
    if (node == GPU_SYSFS_GED_FREQ) {
        /* kHz (not verified on a device: the open line quotes its text) */
        return v < 0 ? -1 : gpu_sysfs_mhz((v + 500) / 1000, false);
    }
    if (is_clock(node) || node == GPU_SYSFS_MAX_GPUCLK || node == GPU_SYSFS_DF_MAX) {
        return gpu_sysfs_mhz(v, is_hz(node));
    }
    if (node == GPU_SYSFS_TEMP) {
        /* millidegrees on KGSL; a driver that gives degrees keeps them */
        return (int32_t)(v > 1000 || v < -1000 ? v / 1000 : v);
    }
    return v < 0 ? -1 : v > 2147483647ll ? 2147483647 : (int32_t)v;
}

/* the sampler thread's step: one sample of the readable nodes */
static void sample(void)
{
    GpuSysfsSample s;
    long long v;

    s.tMs = ico_thread_now_ms();
    s.clockMhz = s.busyPct = s.resets = s.thermal = -1;
    s.tempC = GPU_SYSFS_NO_TEMP;
    memset(s.cpuMhz, 0, sizeof(s.cpuMhz));
    if (s_clockNode >= 0 &&
        read_node_text(s_paths[s_clockNode], &v, s_clockNode == GPU_SYSFS_GED_FREQ, NULL, 0) == 0) {
        s.clockMhz = value_of(s_clockNode, v);
    }
    if (s_busyNode >= 0 && read_node(s_paths[s_busyNode], &v) == 0) {
        s.busyPct = value_of(s_busyNode, v);
    }
    if (s_info.state[GPU_SYSFS_RESETS] == 1 && read_node(s_paths[GPU_SYSFS_RESETS], &v) == 0) {
        s.resets = value_of(GPU_SYSFS_RESETS, v);
    }
    if (s_info.state[GPU_SYSFS_THERMAL] == 1 && read_node(s_paths[GPU_SYSFS_THERMAL], &v) == 0) {
        s.thermal = value_of(GPU_SYSFS_THERMAL, v);
    }
    if (s_info.state[GPU_SYSFS_TEMP] == 1 && read_node(s_paths[GPU_SYSFS_TEMP], &v) == 0) {
        s.tempC = value_of(GPU_SYSFS_TEMP, v);
    }
    for (int c = 0; c < GPU_SYSFS_CPUS; c++) {
        /* the CPU cores' clocks (kHz) beside the GPU's */
        if ((s_info.cpuMask & (1u << c)) && read_node(s_cpuPaths[c], &v) == 0 && v > 0) {
            const long long mhz = (v + 500) / 1000;

            s.cpuMhz[c] = (uint16_t)(mhz > 65535 ? 65535 : mhz);
        }
    }
    pthread_mutex_lock(&s_lock);
    gpu_sysfs_ring_add(&s_ring, &s);
    pthread_mutex_unlock(&s_lock);
}

/* every GPU_SYSFS_PERIOD_MS until gpu_sysfs_android_stop; nothing read
   while the app is in the background (the watchdog paused, diag_host.h
   ico_diag_watchdog_paused) */
static int sampler_main(void *u)
{
    (void)u;
    /* the process's cores, not its creator's (cpu_place.h cpu_place_thread_free) */
    (void)cpu_place_thread_free();
    SDL_LockMutex(s_waitLock);
    while (!s_stop) {
        SDL_WaitConditionTimeout(s_wake, s_waitLock, (Sint32)GPU_SYSFS_PERIOD_MS);
        if (s_stop) {
            break;
        }
        SDL_UnlockMutex(s_waitLock);
        if (!ico_diag_watchdog_paused()) {
            sample();
        }
        SDL_LockMutex(s_waitLock);
    }
    SDL_UnlockMutex(s_waitLock);
    return 0;
}

/* the sampler thread started when a node is sampled and it is not running */
static void sampler_start(void)
{
    if (!s_sampling || s_thread != NULL) {
        return;
    }
    if (s_waitLock == NULL) {
        s_waitLock = SDL_CreateMutex();
        s_wake = SDL_CreateCondition();
    }
    if (s_waitLock == NULL || s_wake == NULL) {
        fprintf(stderr, "window: kernel GPU driver: no sampler (SDL: %s)\n", SDL_GetError());
        return;
    }
    s_stop = 0;
    s_thread = SDL_CreateThread(sampler_main, "ico-gpu-sysfs", NULL);
    if (s_thread == NULL) {
        fprintf(stderr, "window: kernel GPU driver: no sampler thread (SDL: %s)\n", SDL_GetError());
    }
}

void gpu_sysfs_android_stop(void)
{
    if (s_thread == NULL) {
        return;
    }
    SDL_LockMutex(s_waitLock);
    s_stop = 1;
    SDL_SignalCondition(s_wake);
    SDL_UnlockMutex(s_waitLock);
    SDL_WaitThread(s_thread, NULL);
    s_thread = NULL;
}

/* the cores' clocks: which cores' scaling_cur_freq read, cpu0's errno
   when none did; the ring's cores and their top speeds (cpu_place.h, the
   speed classes) */
static void probe_cpus(void)
{
    CpuClasses c;
    int count = 0;

    for (int i = 0; i < GPU_SYSFS_CPUS; i++) {
        long long v = 0;
        int e;

        snprintf(s_cpuPaths[i], sizeof(s_cpuPaths[i]),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        e = read_node(s_cpuPaths[i], &v);
        if (e == 0) {
            s_info.cpuMask |= 1u << i;
            count = i + 1;
        } else if (i == 0) {
            s_info.cpuErr = -e;
        }
    }
    if (s_info.cpuMask != 0) {
        s_info.cpuErr = 0;
    }
    (void)cpu_place_classes_read(NULL, &c);
    if (c.count > count) {
        count = c.count;
    }
    gpu_sysfs_ring_cpus(&s_ring, count, c.khz);
}

void gpu_sysfs_android_start(GpuSysfsInfo *info)
{
    if (s_started) {
        *info = s_info;
        sampler_start(); /* a window opened again after a stop */
        return;
    }
    s_started = 1;
    memset(&s_info, 0, sizeof(s_info));
    s_info.maxMhz = s_info.minLevel = s_info.maxLevel = s_info.resets = -1;
    s_clockNode = s_busyNode = -1;
    /* KGSL first (Qualcomm); else the devfreq entry of the GPU's (a Mali);
       else neither, the candidates' paths alone */
    s_info.dirPresent = access(GPU_SYSFS_DIR, F_OK) == 0;
    if (s_info.dirPresent) {
        s_info.kind = GPU_SYSFS_KIND_KGSL;
        snprintf(s_info.dir, sizeof(s_info.dir), "%s", GPU_SYSFS_DIR);
    } else if (devfreq_gpu_dir(s_info.dir, sizeof(s_info.dir))) {
        s_info.kind = GPU_SYSFS_KIND_DEVFREQ;
        s_info.dirPresent = 1;
    } else {
        s_info.kind = GPU_SYSFS_KIND_NONE;
        s_info.dir[0] = '\0';
    }
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        long long v = 0;
        int e;
        /* the texts of the formats not verified on a device, quoted by the
           open line */
        const bool quote = i == GPU_SYSFS_DF_LOAD || gpu_sysfs_node_absolute(i);

        if (!gpu_sysfs_node_in(s_info.kind, i)) {
            continue;
        }
        if (i == GPU_SYSFS_DEVFREQ) {
            devfreq_path(s_paths[i], sizeof(s_paths[i]));
        } else if (gpu_sysfs_node_absolute(i)) {
            snprintf(s_paths[i], sizeof(s_paths[i]), "%s", gpu_sysfs_node_name(i));
        } else {
            snprintf(s_paths[i], sizeof(s_paths[i]), "%s/%s", s_info.dir, gpu_sysfs_node_name(i));
        }
        e = read_node_text(s_paths[i], &v, i == GPU_SYSFS_GED_FREQ, quote ? s_info.first[i] : NULL,
                           sizeof(s_info.first[i]));
        s_info.state[i] = e == 0 ? 1 : e == ENOENT ? 0 : -e;
        if (e != 0) {
            continue;
        }
        if (i == GPU_SYSFS_MAX_GPUCLK || i == GPU_SYSFS_DF_MAX) {
            s_info.maxMhz = value_of(i, v);
        } else if (i == GPU_SYSFS_MIN_PWRLEVEL) {
            s_info.minLevel = value_of(i, v);
        } else if (i == GPU_SYSFS_MAX_PWRLEVEL) {
            s_info.maxLevel = value_of(i, v);
        } else if (i == GPU_SYSFS_RESETS) {
            s_info.resets = value_of(i, v);
        }
    }
    /* the first readable in the enum's order: gpuclk, clock_mhz,
       devfreq/cur_freq on KGSL; cur_freq, then GED's on the others */
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (s_info.state[i] == 1 && gpu_sysfs_node_in(s_info.kind, i)) {
            if (s_clockNode < 0 && is_clock(i)) {
                s_clockNode = i;
            }
            if (s_busyNode < 0 && is_busy(i)) {
                s_busyNode = i;
            }
        }
    }
    gpu_sysfs_ring_reset(&s_ring);
    probe_cpus();
    *info = s_info;
    if (gpu_sysfs_any_sampled(&s_info)) {
        s_sampling = 1;
        sampler_start();
    }
}

bool gpu_sysfs_android_block(double sinceMs, GpuSysfsBlock *out)
{
    if (!s_sampling) {
        return false;
    }
    pthread_mutex_lock(&s_lock);
    gpu_sysfs_ring_block(&s_ring, sinceMs, out);
    pthread_mutex_unlock(&s_lock);
    return true;
}

bool gpu_sysfs_android_around(double t0Ms, double t1Ms, int core, GpuSysfsAround *out)
{
    if (!s_sampling) {
        return false;
    }
    pthread_mutex_lock(&s_lock);
    gpu_sysfs_ring_around(&s_ring, t0Ms, t1Ms, core, out);
    pthread_mutex_unlock(&s_lock);
    return true;
}
