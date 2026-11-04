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

static const char *const s_names[GPU_SYSFS_NODES] = {
    "gpuclk",           "clock_mhz", "devfreq/cur_freq", "gpu_busy_percentage", "reset_count",
    "thermal_pwrlevel", "temp",      "max_gpuclk",       "min_pwrlevel",        "max_pwrlevel"};

const char *gpu_sysfs_node_name(int node)
{
    return node >= 0 && node < GPU_SYSFS_NODES ? s_names[node] : "?";
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
    for (int i = GPU_SYSFS_GPUCLK; i <= GPU_SYSFS_TEMP; i++) {
        if (info->state[i] == 1) {
            return true;
        }
    }
    return false;
}

void gpu_sysfs_ring_reset(GpuSysfsRing *r)
{
    memset(r, 0, sizeof(*r));
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
    for (uint32_t k = 0; k < r->count; k++) {
        const GpuSysfsSample *s = at(r, k);

        if (!(s->tMs > sinceMs)) {
            if (s->resets >= 0) {
                resetsBefore = s->resets;
            }
            continue;
        }
        out->n++;
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

void gpu_sysfs_ring_around(const GpuSysfsRing *r, double t0Ms, double t1Ms, GpuSysfsAround *out)
{
    memset(out, 0, sizeof(*out));
    out->t0Ms = t0Ms;
    out->t1Ms = t1Ms;
    out->duringClockMin = out->duringClockMax = -1;
    out->duringBusyMin = out->duringBusyMax = -1;
    out->duringResets = -1;
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

int gpu_sysfs_open_text(const GpuSysfsInfo *info, char *buf, size_t size)
{
    Out o = {buf, size, 0};
    int readable = 0, unreadable = 0, sameErr = 0;
    const char *sep;

    put(&o, "kernel GPU driver: %s", GPU_SYSFS_DIR);
    if (!info->dirPresent) {
        put(&o, " not present");
        return done(&o);
    }
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (info->state[i] == 1) {
            readable++;
        } else if (info->state[i] < 0) {
            sameErr = unreadable == 0 || sameErr == info->state[i] ? info->state[i] : 1;
            unreadable++;
        }
    }
    if (readable == 0 && unreadable == 0) {
        put(&o, ": none of its nodes present");
        return done(&o);
    }
    if (readable == 0 && sameErr < 0) {
        put(&o, ": not readable (%s)", errno_name(-sameErr));
        return done(&o);
    }
    put(&o, ":");
    if (readable) {
        sep = " readable ";
        for (int i = 0; i < GPU_SYSFS_NODES; i++) {
            if (info->state[i] == 1) {
                put(&o, "%s%s", sep, s_names[i]);
                if (i == GPU_SYSFS_RESETS && info->resets >= 0) {
                    put(&o, " (%d so far)", (int)info->resets);
                }
                sep = ", ";
            }
        }
    }
    if (unreadable) {
        sep = readable ? "; not readable " : " not readable ";
        for (int i = 0; i < GPU_SYSFS_NODES; i++) {
            if (info->state[i] < 0) {
                put(&o, "%s%s (%s)", sep, s_names[i], errno_name(-info->state[i]));
                sep = ", ";
            }
        }
    }
    sep = "; not present ";
    for (int i = 0; i < GPU_SYSFS_NODES; i++) {
        if (info->state[i] == 0) {
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
    if (gpu_sysfs_any_readable(info)) {
        put(&o, "; sampled every %u ms", GPU_SYSFS_PERIOD_MS);
    }
    return done(&o);
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
    return done(&o);
}
