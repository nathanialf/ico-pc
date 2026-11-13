/*
 * port/platform/stall_coincide.c
 *
 * What a stall coincided with (stall_coincide.h): pure.
 */
#include "stall_coincide.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void stall_medians_reset(StallMedians *m)
{
    memset(m, 0, sizeof(*m));
}

static int cmpf(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

double stall_medians_add(StallMedians *m, uint32_t key, double ms)
{
    uint32_t i = 0;
    while (i < m->used && m->k[i].key != key) {
        i++;
    }
    if (i == m->used) {
        if (m->used == STALL_KEYS) {
            return -1.0;
        }
        memset(&m->k[i], 0, sizeof(m->k[i]));
        m->k[i].key = key;
        m->used++;
    }
    double median = -1.0;
    const uint32_t held = m->k[i].n < STALL_RING ? m->k[i].n : STALL_RING;
    if (held >= STALL_MEDIAN_MIN) {
        float s[STALL_RING];
        memcpy(s, m->k[i].ms, held * sizeof(float));
        qsort(s, held, sizeof(float), cmpf);
        median = (held & 1u) ? (double)s[held / 2u]
                             : 0.5 * ((double)s[held / 2u - 1u] + (double)s[held / 2u]);
    }
    m->k[i].ms[m->k[i].n % STALL_RING] = (float)ms;
    m->k[i].n++;
    return median;
}

bool stall_reset_sign(unsigned stampsMissing, bool ranTwice, uint64_t clockBackNs)
{
    return stampsMissing != 0 || ranTwice || clockBackNs != 0;
}

bool stall_pass_spike(double ms, double median)
{
    return median > 0.0 && ms > STALL_PASS_TIMES * median && ms > STALL_PASS_MS;
}

unsigned stall_classes(const StallMoment *m)
{
    unsigned bits = 0;
    if (m->haveWaits) {
        for (int i = 0; i < STALL_WAIT_THREADS; i++) {
            if (m->waitMs[i] > STALL_WAIT_MS) {
                bits |= 1u << STALL_CLASS_WAIT;
            }
        }
    }
    if (m->availMb >= 0 && m->floorMb > 0 && m->availMb < m->floorMb) {
        bits |= 1u << STALL_CLASS_MEMORY;
    }
    if (m->pressure == 1 && m->some10Before >= 0.0 && m->some10 > m->some10Before) {
        bits |= 1u << STALL_CLASS_PRESSURE;
    }
    if (m->gpuMhz > 0 && m->gpuMaxMhz > 0 && (double)m->gpuMhz < STALL_GPU_LOW * m->gpuMaxMhz) {
        bits |= 1u << STALL_CLASS_GPU;
    }
    if (m->resetReplay != 0) {
        bits |= 1u << STALL_CLASS_RESET;
    }
    return bits ? bits : 1u << STALL_CLASS_NONE;
}

void stall_tally_add(StallTally *t, const StallMoment *m)
{
    const unsigned bits = stall_classes(m);
    t->stalls++;
    for (int c = 0; c < STALL_CLASSES; c++) {
        t->cls[c] += (bits >> c) & 1u;
    }
    t->gpuReadable = t->gpuReadable || m->gpuMhz > 0;
}

typedef struct Out {
    char *out;
    size_t cap, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(o->out + o->at, o->cap - o->at, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->at = o->at + (size_t)w < o->cap ? o->at + (size_t)w : o->cap - 1u;
    }
}

static const char *const k_class[STALL_CLASSES] = {
    "a core wait over 100 ms", "memory under the floor",        "memory pressure rising",
    "the GPU clock low",       "the GPU's own sign of a reset", "none of these"};

size_t stall_tally_text(const StallTally *t, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    put(&o, "stalls by what coincided: %u in the block", t->stalls);
    if (t->stalls == 0) {
        return o.at;
    }
    for (int c = 0; c < STALL_CLASSES; c++) {
        put(&o, "%s%s %u", c ? ", " : " (", k_class[c], t->cls[c]);
        if (c == STALL_CLASS_GPU && !t->gpuReadable) {
            put(&o, " (not readable)");
        }
    }
    put(&o, ")");
    return o.at;
}

static const char *const k_who[STALL_WAIT_THREADS] = {"game", "render", "submit", "SDL audio",
                                                      "system audio"};

size_t stall_moment_text(const StallMoment *m, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (m->gameCore >= 0) {
        put(&o, "the game thread on core %d", m->gameCore);
        if (m->gameMhz > 0) {
            put(&o, " (%d MHz)", m->gameMhz);
        }
    } else {
        put(&o, "the game thread's core not known");
    }
    if (m->renderCore >= 0) {
        put(&o, ", the render thread on core %d", m->renderCore);
        if (m->renderMhz > 0) {
            put(&o, " (%d MHz)", m->renderMhz);
        }
    }
    if (m->haveWaits) {
        const char *sep = "; in the last second the core waits: ";
        for (int i = 0; i < STALL_WAIT_THREADS; i++) {
            if (m->waitMs[i] >= 0.0) {
                put(&o, "%s%s %.0f ms", sep, k_who[i], m->waitMs[i]);
                sep = ", ";
            }
        }
    } else {
        put(&o, "; the core waits not sampled");
    }
    if (m->drivers > 0) {
        put(&o, "; the driver's threads:");
        for (int i = 0; i < m->drivers && i < STALL_DRIVERS; i++) {
            put(&o, "%s %s ran %.0f ms, waited %.0f ms", i ? "," : "", m->driverName[i],
                m->driverRunMs[i], m->driverWaitMs[i]);
        }
    }
    if (m->availMb >= 0) {
        put(&o, "; memory %ld MB available", m->availMb);
        if (m->floorMb > 0) {
            put(&o, " against the floor %ld MB (%s)", m->floorMb,
                m->availMb < m->floorMb ? "under it" : "above it");
        }
    } else {
        put(&o, "; device memory not known");
    }
    if (m->pressure == 1) {
        put(&o, "; pressure some %.1f %%, full %.1f %% (avg10", m->some10, m->full10);
        if (m->some10Before >= 0.0) {
            put(&o, "; some %.1f %% a second before: %s", m->some10Before,
                m->some10 > m->some10Before ? "rising" : "not rising");
        }
        put(&o, ")");
    } else if (m->pressure == 0) {
        put(&o, "; memory pressure not readable (errno %d)", m->pressureErr);
    }
    if (m->headroom >= 0.0) {
        put(&o, "; thermal headroom %.2f", m->headroom);
    }
    if (m->gpuMhz > 0) {
        put(&o, "; the GPU clock %d MHz", m->gpuMhz);
        if (m->gpuMaxMhz > 0) {
            put(&o, " of %d", m->gpuMaxMhz);
        }
    }
    if (m->resetReplay != 0) {
        put(&o, "; the GPU's own sign of a reset at replay %u:", m->resetReplay);
        const char *in = " ";
        if (m->stampsMissing) {
            put(&o, "%s%u stamps came back unwritten after its work was signalled finished", in,
                m->stampsMissing);
            in = ", ";
        }
        if (m->ranTwice) {
            put(&o, "%sits first list ran twice", in);
            in = ", ";
        }
        if (m->clockBack) {
            put(&o, "%sthe GPU's clock restarted", in);
        }
    }
    const unsigned bits = stall_classes(m);
    const char *sep = "; coincided with: ";
    for (int c = 0; c < STALL_CLASSES; c++) {
        if ((bits >> c) & 1u) {
            put(&o, "%s%s", sep, k_class[c]);
            sep = ", ";
        }
    }
    return o.at;
}

/* the number after "<word>=" in line at s (len bytes), true when found */
static bool numAfter(const char *s, size_t len, const char *word, double *out)
{
    const size_t w = strlen(word);
    for (size_t i = 0; i + w < len; i++) {
        if (memcmp(s + i, word, w) == 0) {
            char tmp[32];
            size_t k = 0;
            for (size_t j = i + w;
                 j < len && k + 1 < sizeof tmp && (isdigit((unsigned char)s[j]) || s[j] == '.');
                 j++) {
                tmp[k++] = s[j];
            }
            tmp[k] = '\0';
            if (k == 0) {
                return false;
            }
            *out = strtod(tmp, NULL);
            return true;
        }
    }
    return false;
}

/* the line beginning with prefix (at a line's start), its offset and
   length; false when none */
static bool lineOf(const char *buf, size_t len, const char *prefix, size_t *at, size_t *n)
{
    const size_t p = strlen(prefix);
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && buf[e] != '\n') {
            e++;
        }
        if (e - i >= p && memcmp(buf + i, prefix, p) == 0) {
            *at = i;
            *n = e - i;
            return true;
        }
        i = e + 1;
    }
    return false;
}

bool stall_pressure_parse(const char *buf, size_t len, double *some10, double *full10)
{
    size_t a, n, b, m;
    if (!lineOf(buf, len, "some ", &a, &n) || !lineOf(buf, len, "full ", &b, &m)) {
        return false;
    }
    return numAfter(buf + a, n, "avg10=", some10) && numAfter(buf + b, m, "avg10=", full10);
}

static bool kbOf(const char *buf, size_t len, const char *key, long *out)
{
    size_t a, n;
    if (!lineOf(buf, len, key, &a, &n)) {
        return false;
    }
    size_t i = a + strlen(key);
    while (i < a + n && (buf[i] == ' ' || buf[i] == '\t')) {
        i++;
    }
    long v = 0;
    bool any = false;
    while (i < a + n && isdigit((unsigned char)buf[i])) {
        v = v * 10 + (buf[i] - '0');
        i++;
        any = true;
    }
    *out = v;
    return any;
}

bool stall_status_parse(const char *buf, size_t len, long *anonKb, long *fileKb, long *shmemKb)
{
    return kbOf(buf, len, "RssAnon:", anonKb) && kbOf(buf, len, "RssFile:", fileKb) &&
           kbOf(buf, len, "RssShmem:", shmemKb);
}

bool stall_driver_name(const char *name)
{
    static const char *const k[] = {"mali", "kgsl", "adreno", "qgl", "pvr", "img", "gpu", "tu_"};
    char low[16];
    size_t n = 0;
    while (name[n] != '\0' && n + 1 < sizeof low) {
        low[n] = (char)tolower((unsigned char)name[n]);
        n++;
    }
    low[n] = '\0';
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (strncmp(low, k[i], strlen(k[i])) == 0) {
            return true;
        }
    }
    return false;
}
