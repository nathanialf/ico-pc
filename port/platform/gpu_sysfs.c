/*
 * port/platform/gpu_sysfs.c
 *
 * The kernel GPU driver's state, the pure part (gpu_sysfs.h).
 */
#include "gpu_sysfs.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const s_names[GPU_SYSFS_NODES] = {"gpuclk",
                                                     "clock_mhz",
                                                     "devfreq/cur_freq",
                                                     "gpu_busy_percentage",
                                                     "reset_count",
                                                     "thermal_pwrlevel",
                                                     "temp",
                                                     "max_gpuclk",
                                                     "min_pwrlevel",
                                                     "max_pwrlevel",
                                                     "cur_freq",
                                                     "load",
                                                     "/sys/class/misc/mali0/device/utilization",
                                                     "/sys/kernel/ged/hal/gpu_utilization",
                                                     "/sys/kernel/ged/hal/current_freqency",
                                                     "max_freq"};

const char *gpu_sysfs_node_name(int node)
{
    return node >= 0 && node < GPU_SYSFS_NODES ? s_names[node] : "?";
}

bool gpu_sysfs_node_absolute(int node)
{
    return node >= GPU_SYSFS_MALI_UTIL && node <= GPU_SYSFS_GED_FREQ;
}

bool gpu_sysfs_node_in(int kind, int node)
{
    if (node < 0 || node >= GPU_SYSFS_NODES) {
        return false;
    }
    switch (kind) {
    case GPU_SYSFS_KIND_KGSL:
        return node <= GPU_SYSFS_MAX_PWRLEVEL;
    case GPU_SYSFS_KIND_DEVFREQ:
        return node >= GPU_SYSFS_DF_CUR;
    case GPU_SYSFS_KIND_NONE:
        return gpu_sysfs_node_absolute(node);
    default:
        return false;
    }
}

bool gpu_sysfs_node_sampled(int node)
{
    return (node >= GPU_SYSFS_GPUCLK && node <= GPU_SYSFS_TEMP) ||
           (node >= GPU_SYSFS_DF_CUR && node <= GPU_SYSFS_GED_FREQ);
}

/* the name holds word, whatever the case */
static bool holds(const char *name, const char *word)
{
    const size_t n = strlen(word);

    for (const char *p = name; *p != '\0'; p++) {
        size_t k = 0;

        while (k < n && p[k] != '\0' &&
               (p[k] >= 'A' && p[k] <= 'Z' ? p[k] - 'A' + 'a' : p[k]) == word[k]) {
            k++;
        }
        if (k == n) {
            return true;
        }
    }
    return false;
}

int gpu_sysfs_devfreq_name_rank(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    if (holds(name, "mali")) {
        return 2;
    }
    if (holds(name, "gpu") && !holds(name, "bw") && !holds(name, "bus")) {
        return 1;
    }
    return 0;
}

bool gpu_sysfs_devfreq_name_ok(const char *name)
{
    return gpu_sysfs_devfreq_name_rank(name) > 0;
}

bool gpu_sysfs_parse(const char *buf, size_t len, long long *out)
{
    size_t i = 0;
    int neg = 0;
    long long v = 0;

    while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')) {
        i++;
    }
    if (i < len && (buf[i] == '-' || buf[i] == '+')) {
        neg = buf[i] == '-';
        i++;
    }
    if (i >= len || buf[i] < '0' || buf[i] > '9') {
        return false;
    }
    for (; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) {
        if (v < 100000000000000000ll) { /* past 1e17 the digits are dropped, never overflow */
            v = v * 10 + (buf[i] - '0');
        }
    }
    *out = neg ? -v : v;
    return true;
}

bool gpu_sysfs_parse_last(const char *buf, size_t len, long long *out)
{
    size_t i = 0, last = len;

    while (i < len) {
        if (buf[i] >= '0' && buf[i] <= '9' && (i == 0 || buf[i - 1] < '0' || buf[i - 1] > '9')) {
            last = i;
        }
        i++;
    }
    if (last == len) {
        return false;
    }
    /* a sign right before it counts */
    if (last > 0 && buf[last - 1] == '-') {
        last--;
    }
    return gpu_sysfs_parse(buf + last, len - last, out);
}

int32_t gpu_sysfs_mhz(long long v, bool isHz)
{
    if (v < 0) {
        return -1;
    }
    if (isHz) {
        v = (v + 500000) / 1000000;
    }
    return v > 2147483647ll ? 2147483647 : (int32_t)v;
}

bool gpu_sysfs_any_readable(const GpuSysfsInfo *info)
{
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (info->state[i] == 1 && gpu_sysfs_node_sampled(i) && gpu_sysfs_node_in(info->kind, i)) {
            return true;
        }
    }
    return false;
}

bool gpu_sysfs_any_sampled(const GpuSysfsInfo *info)
{
    return gpu_sysfs_any_readable(info) || info->cpuMask != 0;
}

void gpu_sysfs_ring_reset(GpuSysfsRing *r)
{
    memset(r, 0, sizeof(*r));
}

void gpu_sysfs_ring_cpus(GpuSysfsRing *r, int count, const uint32_t *khz)
{
    r->cpuCount = count < 0 ? 0 : count > GPU_SYSFS_CPUS ? GPU_SYSFS_CPUS : count;
    for (int i = 0; i < GPU_SYSFS_CPUS; i++) {
        r->cpuKhz[i] = i < r->cpuCount && khz != NULL ? khz[i] : 0u;
    }
}

void gpu_sysfs_ring_add(GpuSysfsRing *r, const GpuSysfsSample *s)
{
    r->s[r->next] = *s;
    r->next = (r->next + 1u) % GPU_SYSFS_RING;
    if (r->count < GPU_SYSFS_RING) {
        r->count++;
    }
}

/* the k-th oldest sample held */
static const GpuSysfsSample *at(const GpuSysfsRing *r, uint32_t k)
{
    return &r->s[(r->next + GPU_SYSFS_RING - r->count + k) % GPU_SYSFS_RING];
}

void gpu_sysfs_ring_block(const GpuSysfsRing *r, double sinceMs, GpuSysfsBlock *out)
{
    int32_t resetsBefore = -1, resetsFirst = -1, resetsLast = -1;
    double clockSum = 0.0, busySum = 0.0;

    memset(out, 0, sizeof(*out));
    out->clockMin = out->clockMax = -1;
    out->resetsIn = out->resetsAll = -1;
    out->thermalMax = -1;
    out->tempMax = GPU_SYSFS_NO_TEMP;
    out->cpuCount = r->cpuCount;
    for (int c = 0; c < GPU_SYSFS_CPUS; c++) {
        out->cpuKhz[c] = r->cpuKhz[c];
        out->cpuMin[c] = out->cpuMax[c] = -1;
    }
    for (uint32_t k = 0; k < r->count; k++) {
        const GpuSysfsSample *s = at(r, k);

        if (!(s->tMs > sinceMs)) {
            if (s->resets >= 0) {
                resetsBefore = s->resets;
            }
            continue;
        }
        out->n++;
        for (int c = 0; c < r->cpuCount; c++) {
            const int32_t m = s->cpuMhz[c];

            if (m == 0) {
                continue;
            }
            out->cpuMin[c] = out->cpuN[c] == 0 || m < out->cpuMin[c] ? m : out->cpuMin[c];
            out->cpuMax[c] = m > out->cpuMax[c] ? m : out->cpuMax[c];
            out->cpuSum[c] += m;
            out->cpuN[c]++;
        }
        if (s->clockMhz >= 0) {
            out->clockMin =
                out->clockN == 0 || s->clockMhz < out->clockMin ? s->clockMhz : out->clockMin;
            out->clockMax = s->clockMhz > out->clockMax ? s->clockMhz : out->clockMax;
            clockSum += s->clockMhz;
            out->clockN++;
        }
        if (s->busyPct >= 0) {
            busySum += s->busyPct;
            out->busyN++;
        }
        if (s->resets >= 0) {
            if (resetsFirst < 0) {
                resetsFirst = s->resets;
            }
            resetsLast = s->resets;
        }
        if (s->thermal > out->thermalMax) {
            out->thermalMax = s->thermal;
        }
        if (s->tempC != GPU_SYSFS_NO_TEMP && s->tempC > out->tempMax) {
            out->tempMax = s->tempC;
        }
    }
    out->clockMean = out->clockN ? clockSum / out->clockN : 0.0;
    out->busyMean = out->busyN ? busySum / out->busyN : 0.0;
    if (resetsLast >= 0) {
        out->resetsAll = resetsLast;
        out->resetsIn = resetsLast - (resetsBefore >= 0 ? resetsBefore : resetsFirst);
    }
}

void gpu_sysfs_ring_around(const GpuSysfsRing *r, double t0Ms, double t1Ms, int core,
                           GpuSysfsAround *out)
{
    memset(out, 0, sizeof(*out));
    out->t0Ms = t0Ms;
    out->t1Ms = t1Ms;
    out->duringClockMin = out->duringClockMax = -1;
    out->duringBusyMin = out->duringBusyMax = -1;
    out->duringResets = -1;
    out->core = core >= 0 && core < r->cpuCount ? core : -1;
    out->duringCpuMin = out->duringCpuMax = -1;
    for (uint32_t k = 0; k < r->count; k++) {
        const GpuSysfsSample *s = at(r, k);

        if (s->tMs <= t0Ms) {
            out->before = *s;
            out->hasBefore = true;
        } else if (s->tMs >= t1Ms) {
            if (!out->hasAfter) {
                out->after = *s;
                out->hasAfter = true;
            }
        } else {
            out->during++;
            if (s->clockMhz >= 0) {
                out->duringClockMin = out->duringClockMin < 0 || s->clockMhz < out->duringClockMin
                                          ? s->clockMhz
                                          : out->duringClockMin;
                out->duringClockMax =
                    s->clockMhz > out->duringClockMax ? s->clockMhz : out->duringClockMax;
            }
            if (s->busyPct >= 0) {
                out->duringBusyMin = out->duringBusyMin < 0 || s->busyPct < out->duringBusyMin
                                         ? s->busyPct
                                         : out->duringBusyMin;
                out->duringBusyMax =
                    s->busyPct > out->duringBusyMax ? s->busyPct : out->duringBusyMax;
            }
            if (s->resets >= 0) {
                out->duringResets = s->resets;
            }
            if (out->core >= 0 && s->cpuMhz[out->core] != 0) {
                const int32_t m = s->cpuMhz[out->core];

                out->duringCpuMin =
                    out->duringCpuMin < 0 || m < out->duringCpuMin ? m : out->duringCpuMin;
                out->duringCpuMax = m > out->duringCpuMax ? m : out->duringCpuMax;
            }
        }
    }
}

/* ---------------------------------------------------------------- texts */

/* a bounded appender: the length that would be written is kept whatever
   the room */
typedef struct Out {
    char *buf;
    size_t size, len;
} Out;

#ifdef __GNUC__
static void put(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    int w;

    va_start(ap, fmt);
    w = vsnprintf(o->len < o->size ? o->buf + o->len : NULL,
                  o->len < o->size ? o->size - o->len : 0, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->len += (size_t)w;
    }
}

static int done(Out *o)
{
    if (o->size != 0 && o->len == 0) {
        o->buf[0] = '\0';
    }
    return (int)o->len;
}

static const char *errno_name(int e)
{
    static char other[24];

    switch (e) {
    case EACCES:
        return "EACCES";
    case EPERM:
        return "EPERM";
    case ENOENT:
        return "ENOENT";
    default:
        snprintf(other, sizeof other, "errno %d", e);
        return other;
    }
}

/* "0-7", "0-5, 7" (the cores of mask) */
static void mask_words(Out *o, uint32_t mask)
{
    const char *sep = "";

    for (int i = 0; i < 32;) {
        int j = i;

        if (!(mask & (1u << i))) {
            i++;
            continue;
        }
        while (j + 1 < 32 && (mask & (1u << (j + 1)))) {
            j++;
        }
        if (j > i) {
            put(o, "%s%d-%d", sep, i, j);
        } else {
            put(o, "%s%d", sep, i);
        }
        sep = ", ";
        i = j + 1;
    }
}

/* a node in the open line: its name, and the first text read where the
   probe kept it ("load (\"45@650000000Hz\")") */
static void node_words(Out *o, const GpuSysfsInfo *info, int i)
{
    put(o, "%s", s_names[i]);
    if (info->first[i][0] != '\0') {
        put(o, " (\"%s\")", info->first[i]);
    }
}

int gpu_sysfs_open_text(const GpuSysfsInfo *info, char *buf, size_t size)
{
    Out o = {buf, size, 0};
    int readable = 0, unreadable = 0, sameErr = 0;
    const char *sep;
    const bool none = info->kind == GPU_SYSFS_KIND_NONE;

    if (info->kind == GPU_SYSFS_KIND_DEVFREQ) {
        put(&o, "kernel GPU driver: %s", info->dir);
    } else {
        put(&o, "kernel GPU driver: %s", info->dir[0] != '\0' ? info->dir : GPU_SYSFS_DIR);
    }
    if (none) {
        put(&o, " not present, no GPU entry in %s", GPU_SYSFS_DEVFREQ_DIR);
    } else if (!info->dirPresent) {
        put(&o, " not present");
        goto cpus;
    }
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (!gpu_sysfs_node_in(info->kind, i)) {
            continue;
        }
        if (info->state[i] == 1) {
            readable++;
        } else if (info->state[i] < 0) {
            sameErr = unreadable == 0 || sameErr == info->state[i] ? info->state[i] : 1;
            unreadable++;
        }
    }
    if (!none && readable == 0 && unreadable == 0) {
        put(&o, ": none of its nodes present");
        goto cpus;
    }
    if (info->kind == GPU_SYSFS_KIND_KGSL && readable == 0 && sameErr < 0) {
        /* KGSL's directory denied whole (SELinux): one word for all */
        put(&o, ": not readable (%s)", errno_name(-sameErr));
        goto cpus;
    }
    sep = none ? "; readable " : ": readable ";
    if (readable) {
        for (int i = 0; i < GPU_SYSFS_NODES; i++) {
            if (info->state[i] == 1 && gpu_sysfs_node_in(info->kind, i)) {
                put(&o, "%s", sep);
                node_words(&o, info, i);
                if (i == GPU_SYSFS_RESETS && info->resets >= 0) {
                    put(&o, " (%d so far)", (int)info->resets);
                }
                sep = ", ";
            }
        }
    }
    if (unreadable) {
        sep = readable || none ? "; not readable " : ": not readable ";
        for (int i = 0; i < GPU_SYSFS_NODES; i++) {
            if (info->state[i] < 0 && gpu_sysfs_node_in(info->kind, i)) {
                put(&o, "%s%s (%s)", sep, s_names[i], errno_name(-info->state[i]));
                sep = ", ";
            }
        }
    }
    sep = readable || unreadable || none ? "; not present " : ": not present ";
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (info->state[i] == 0 && gpu_sysfs_node_in(info->kind, i)) {
            put(&o, "%s%s", sep, s_names[i]);
            sep = ", ";
        }
    }
    sep = "; ";
    if (info->maxMhz >= 0) {
        put(&o, "%sthe fastest clock %d MHz", sep, (int)info->maxMhz);
        sep = ", ";
    }
    if (info->minLevel >= 0 && info->maxLevel >= 0) {
        /* KGSL numbers its power levels from 0, the fastest: max_pwrlevel is
           the fastest allowed, min_pwrlevel the slowest */
        put(&o, "%spower levels %d (fastest) to %d", sep, (int)info->maxLevel, (int)info->minLevel);
    }
cpus:
    if (info->cpuMask != 0) {
        put(&o, "; CPU clocks of cores ");
        mask_words(&o, info->cpuMask);
        if (gpu_sysfs_any_readable(info)) {
            put(&o, " sampled with it");
        } else {
            put(&o, " sampled every %u ms", GPU_SYSFS_PERIOD_MS);
        }
    } else if (info->cpuErr < 0) {
        put(&o, "; CPU clocks not readable (%s)", errno_name(-info->cpuErr));
    }
    if (gpu_sysfs_any_readable(info)) {
        put(&o, "; sampled every %u ms", GPU_SYSFS_PERIOD_MS);
    }
    return done(&o);
}

/* "; CPU clocks: cores 0-5 1000-1800 MHz (average 1500), cores 6-7 2000
   MHz" (the runs of cores of one top speed with a sample; nothing with
   none) */
static void cpu_block_words(Out *o, const GpuSysfsBlock *b)
{
    const char *sep = "; CPU clocks: ";

    for (int i = 0; i < b->cpuCount;) {
        int j = i, lo = -1, hi = -1;
        unsigned n = 0;
        double sum = 0.0;

        while (j + 1 < b->cpuCount && b->cpuKhz[j + 1] == b->cpuKhz[i]) {
            j++;
        }
        for (int c = i; c <= j; c++) {
            if (b->cpuN[c] == 0) {
                continue;
            }
            lo = lo < 0 || b->cpuMin[c] < lo ? b->cpuMin[c] : lo;
            hi = b->cpuMax[c] > hi ? b->cpuMax[c] : hi;
            sum += b->cpuSum[c];
            n += b->cpuN[c];
        }
        if (n != 0) {
            if (j > i) {
                put(o, "%scores %d-%d ", sep, i, j);
            } else {
                put(o, "%score %d ", sep, i);
            }
            if (lo == hi) {
                put(o, "%d MHz", lo);
            } else {
                put(o, "%d-%d MHz (average %.0f)", lo, hi, sum / n);
            }
            sep = ", ";
        }
        i = j + 1;
    }
}

int gpu_sysfs_block_text(const GpuSysfsBlock *b, char *buf, size_t size)
{
    Out o = {buf, size, 0};

    put(&o, "kernel GPU driver: ");
    if (b->n == 0) {
        put(&o, "no sample in the block");
        return done(&o);
    }
    if (b->clockN == 0) {
        put(&o, "%u samples", b->n);
    } else if (b->clockMin == b->clockMax) {
        put(&o, "clock %d MHz over %u samples", (int)b->clockMin, b->n);
    } else {
        put(&o, "clock %d-%d MHz (average %.0f) over %u samples", (int)b->clockMin,
            (int)b->clockMax, b->clockMean, b->n);
    }
    if (b->busyN) {
        put(&o, ", busy %.0f %% on average", b->busyMean);
    }
    if (b->resetsAll >= 0) {
        put(&o, ", resets %d in the block (%d in all)", (int)b->resetsIn, (int)b->resetsAll);
    }
    if (b->thermalMax >= 0) {
        put(&o, ", thermal power level at most %d", (int)b->thermalMax);
    }
    if (b->tempMax != GPU_SYSFS_NO_TEMP) {
        put(&o, ", temperature at most %d C", (int)b->tempMax);
    }
    cpu_block_words(&o, b);
    return done(&o);
}

/* "clock 905 MHz, busy 74 %, resets 3, thermal power level 0, 47 C" (the
   parts read) */
static void sample_words(Out *o, const GpuSysfsSample *s)
{
    const char *sep = "";

    if (s->clockMhz >= 0) {
        put(o, "clock %d MHz", (int)s->clockMhz);
        sep = ", ";
    }
    if (s->busyPct >= 0) {
        put(o, "%sbusy %d %%", sep, (int)s->busyPct);
        sep = ", ";
    }
    if (s->resets >= 0) {
        put(o, "%sresets %d", sep, (int)s->resets);
        sep = ", ";
    }
    if (s->thermal >= 0) {
        put(o, "%sthermal power level %d", sep, (int)s->thermal);
        sep = ", ";
    }
    if (s->tempC != GPU_SYSFS_NO_TEMP) {
        put(o, "%s%d C", sep, (int)s->tempC);
        sep = ", ";
    }
    if (sep[0] == '\0') {
        put(o, "nothing read");
    }
}

/* "285 MHz" or "285-905 MHz" */
static void range_words(Out *o, const char *what, int32_t lo, int32_t hi, const char *unit,
                        const char **sep)
{
    if (lo < 0) {
        return;
    }
    if (lo == hi) {
        put(o, "%s%s %d %s", *sep, what, (int)lo, unit);
    } else {
        put(o, "%s%s %d-%d %s", *sep, what, (int)lo, (int)hi, unit);
    }
    *sep = ", ";
}

int gpu_sysfs_around_text(const GpuSysfsAround *a, unsigned replay, char *buf, size_t size)
{
    Out o = {buf, size, 0};

    put(&o, "kernel GPU driver around the present that blocked (replay %u): ", replay);
    if (a->hasBefore) {
        put(&o, "%.2f s before: ", (a->t0Ms - a->before.tMs) / 1000.0);
        sample_words(&o, &a->before);
    } else {
        put(&o, "no sample before");
    }
    if (a->during) {
        const char *sep = ": ";

        put(&o, "; %u during", a->during);
        range_words(&o, "clock", a->duringClockMin, a->duringClockMax, "MHz", &sep);
        range_words(&o, "busy", a->duringBusyMin, a->duringBusyMax, "%", &sep);
        if (a->duringResets >= 0) {
            put(&o, "%sresets %d", sep, (int)a->duringResets);
        }
    }
    if (a->hasAfter) {
        put(&o, "; %.2f s after: ", (a->after.tMs - a->t1Ms) / 1000.0);
        sample_words(&o, &a->after);
    } else {
        put(&o, "; no sample after");
    }
    if (a->core >= 0) {
        /* the game thread's core's clock: did the CPU slow down there? */
        const char *sep = ": ";
        const int c = a->core;

        if ((a->hasBefore && a->before.cpuMhz[c] != 0) || a->duringCpuMin >= 0 ||
            (a->hasAfter && a->after.cpuMhz[c] != 0)) {
            put(&o, "; CPU core %d (the game thread's)", c);
            if (a->hasBefore && a->before.cpuMhz[c] != 0) {
                put(&o, "%s%d MHz before", sep, (int)a->before.cpuMhz[c]);
                sep = ", ";
            }
            if (a->duringCpuMin >= 0) {
                if (a->duringCpuMin == a->duringCpuMax) {
                    put(&o, "%s%d MHz during", sep, (int)a->duringCpuMin);
                } else {
                    put(&o, "%s%d-%d MHz during", sep, (int)a->duringCpuMin, (int)a->duringCpuMax);
                }
                sep = ", ";
            }
            if (a->hasAfter && a->after.cpuMhz[c] != 0) {
                put(&o, "%s%d MHz after", sep, (int)a->after.cpuMhz[c]);
            }
        }
    }
    return done(&o);
}
