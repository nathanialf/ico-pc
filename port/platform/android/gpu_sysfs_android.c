/*
 * port/platform/android/gpu_sysfs_android.c
 *
 * The kernel GPU driver's nodes read on Android (gpu_sysfs_android.h).
 * Each read is open, read and close, as diag_host.c reads /proc/meminfo
 * (ico_diag_device_memory): a node's value is made at the read, and a file
 * kept open would return the first value again.
 */
#include "gpu_sysfs_android.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "diag_host.h"
#include "ico_thread.h"

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static GpuSysfsRing s_ring; /* under s_lock */
static GpuSysfsInfo s_info;
static int s_started;   /* the probe made (main thread) */
static int s_sampling;  /* a sampled node readable: the hook set */
static int s_clockNode; /* the node the clock is read from, -1 none */
static char s_paths[GPU_SYSFS_NODES][160];

/* the node's first integer: 0, or the errno of the open or the read (a
   read of nothing or no digit: EINVAL) */
static int read_node(const char *path, long long *out)
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
    return gpu_sysfs_parse(buf, (size_t)got, out) ? 0 : EINVAL;
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

static bool is_hz(int node)
{
    return node == GPU_SYSFS_GPUCLK || node == GPU_SYSFS_DEVFREQ || node == GPU_SYSFS_MAX_GPUCLK;
}

/* a read node's value as the sample keeps it */
static int32_t value_of(int node, long long v)
{
    if (node == GPU_SYSFS_GPUCLK || node == GPU_SYSFS_CLOCK_MHZ || node == GPU_SYSFS_DEVFREQ ||
        node == GPU_SYSFS_MAX_GPUCLK) {
        return gpu_sysfs_mhz(v, is_hz(node));
    }
    if (node == GPU_SYSFS_TEMP) {
        /* millidegrees on KGSL; a driver that gives degrees keeps them */
        return (int32_t)(v > 1000 || v < -1000 ? v / 1000 : v);
    }
    return v < 0 ? -1 : v > 2147483647ll ? 2147483647 : (int32_t)v;
}

/* the watchdog thread's poll: one sample of the readable nodes */
static void sample(void)
{
    GpuSysfsSample s;
    long long v;

    s.tMs = ico_thread_now_ms();
    s.clockMhz = s.busyPct = s.resets = s.thermal = -1;
    s.tempC = GPU_SYSFS_NO_TEMP;
    if (s_clockNode >= 0 && read_node(s_paths[s_clockNode], &v) == 0) {
        s.clockMhz = value_of(s_clockNode, v);
    }
    if (s_info.state[GPU_SYSFS_BUSY] == 1 && read_node(s_paths[GPU_SYSFS_BUSY], &v) == 0) {
        s.busyPct = value_of(GPU_SYSFS_BUSY, v);
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
    pthread_mutex_lock(&s_lock);
    gpu_sysfs_ring_add(&s_ring, &s);
    pthread_mutex_unlock(&s_lock);
}

void gpu_sysfs_android_start(GpuSysfsInfo *info)
{
    if (s_started) {
        *info = s_info;
        return;
    }
    s_started = 1;
    memset(&s_info, 0, sizeof(s_info));
    s_info.maxMhz = s_info.minLevel = s_info.maxLevel = s_info.resets = -1;
    s_clockNode = -1;
    s_info.dirPresent = access(GPU_SYSFS_DIR, F_OK) == 0;
    if (s_info.dirPresent) {
        for (int i = 0; i < GPU_SYSFS_NODES; i++) {
            long long v = 0;
            int e;

            if (i == GPU_SYSFS_DEVFREQ) {
                devfreq_path(s_paths[i], sizeof(s_paths[i]));
            } else {
                snprintf(s_paths[i], sizeof(s_paths[i]), "%s/%s", GPU_SYSFS_DIR,
                         gpu_sysfs_node_name(i));
            }
            e = read_node(s_paths[i], &v);
            s_info.state[i] = e == 0 ? 1 : e == ENOENT ? 0 : -e;
            if (e != 0) {
                continue;
            }
            if (i == GPU_SYSFS_MAX_GPUCLK) {
                s_info.maxMhz = value_of(i, v);
            } else if (i == GPU_SYSFS_MIN_PWRLEVEL) {
                s_info.minLevel = value_of(i, v);
            } else if (i == GPU_SYSFS_MAX_PWRLEVEL) {
                s_info.maxLevel = value_of(i, v);
            } else if (i == GPU_SYSFS_RESETS) {
                s_info.resets = value_of(i, v);
            }
        }
        for (int i = GPU_SYSFS_GPUCLK; i <= GPU_SYSFS_DEVFREQ; i++) {
            if (s_info.state[i] == 1) {
                s_clockNode = i;
                break;
            }
        }
    }
    *info = s_info;
    if (gpu_sysfs_any_readable(&s_info)) {
        gpu_sysfs_ring_reset(&s_ring);
        s_sampling = 1;
        ico_diag_set_poll_hook(sample);
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

bool gpu_sysfs_android_around(double t0Ms, double t1Ms, GpuSysfsAround *out)
{
    if (!s_sampling) {
        return false;
    }
    pthread_mutex_lock(&s_lock);
    gpu_sysfs_ring_around(&s_ring, t0Ms, t1Ms, out);
    pthread_mutex_unlock(&s_lock);
    return true;
}
