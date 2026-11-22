/*
 * port/platform/perf_secs.c
 *
 * Pictures a second by stage (perf_secs.h).
 */
#include "perf_secs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void perf_secs_reset(PerfSecs *s)
{
    memset(s, 0, sizeof(*s));
    s->t0Ms = -1.0;
    s->stage = PERF_SECS_NO_STAGE;
}

/* the second nowMs falls in, the seconds up to it begun (each with the
   stage now until a step names its own); UINT32_MAX before the block */
static unsigned second_at(PerfSecs *s, double nowMs)
{
    if (s->t0Ms < 0.0) {
        s->t0Ms = nowMs;
    }
    if (nowMs < s->t0Ms) {
        return 0; /* a clock read before the block's start: its first second */
    }
    const double d = (nowMs - s->t0Ms) / 1000.0;
    const unsigned i = d >= 4e9 ? 4000000000u : (unsigned)d;
    while (s->n <= i && s->n < PERF_SECS_MAX) {
        s->sec[s->n] = s->stage;
        s->pics[s->n] = 0;
        s->stageSet[s->n] = false;
        s->n++;
    }
    if (i >= PERF_SECS_MAX && i + 1u - PERF_SECS_MAX > s->overS) {
        s->overS = i + 1u - PERF_SECS_MAX;
    }
    return i;
}

void perf_secs_step(PerfSecs *s, int stage, double nowMs)
{
    s->stage = stage;
    const unsigned i = second_at(s, nowMs);
    if (i < PERF_SECS_MAX && !s->stageSet[i]) {
        s->sec[i] = stage;
        s->stageSet[i] = true;
    }
}

void perf_secs_picture(PerfSecs *s, double nowMs)
{
    const unsigned i = second_at(s, nowMs);
    if (i < PERF_SECS_MAX) {
        s->pics[i]++;
    } else {
        s->over++;
    }
}

void perf_secs_record(PerfSecs *s, const PerfSecsRec *r, double nowMs)
{
    const unsigned i = second_at(s, nowMs);
    if (r->presented && r->presentEndMs > 0.0) {
        if (s->lastEndMs > 0.0 && r->presentEndMs > s->lastEndMs) {
            const double iv = r->presentEndMs - s->lastEndMs;
            if (s->ivN < PERF_SECS_IVS) {
                s->iv[s->ivN++] = (float)iv;
            } else {
                s->ivOver++;
            }
            s->ivMax = iv > s->ivMax ? iv : s->ivMax;
            if (i < PERF_SECS_MAX && (float)iv > s->longest[i]) {
                s->longest[i] = (float)iv;
            }
        }
        s->lastEndMs = r->presentEndMs;
    }
    if (i >= PERF_SECS_MAX) {
        return; /* its second not held (the pictures line counts it) */
    }
    if (s->recN == PERF_SECS_RECS) {
        s->recOver++;
        return;
    }
    PerfSecsVal *v = &s->rec[s->recN++];
    v->sec = (uint8_t)i;
    v->tris = (float)r->tris;
    v->gpu = r->gpuValid && r->gpuMs >= 0.0 ? (float)r->gpuMs : -1.0f;
    v->red = r->gpuValid && r->reductionMs >= 0.0 ? (float)r->reductionMs : -1.0f;
    v->pres = r->gpuValid && r->presentPassMs >= 0.0 ? (float)r->presentPassMs : -1.0f;
}

static int cmpf(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* the median of n values (n > 0), sorted in place */
static double median_of(float *v, unsigned n)
{
    qsort(v, n, sizeof(float), cmpf);
    return (n & 1u) ? (double)v[n / 2u] : 0.5 * ((double)v[n / 2u - 1u] + (double)v[n / 2u]);
}

static void put(char *out, size_t cap, size_t *at, const char *fmt, ...);

/* second i's median of field f (0 gpu, 1 tris, 2 red, 3 pres) into out
   ("-" none) */
static void put_median(const PerfSecs *s, unsigned i, int f, char *out, size_t cap, size_t *at)
{
    static float tmp[PERF_SECS_RECS]; /* the game thread's (and a test's) */
    unsigned n = 0;
    for (unsigned k = 0; k < s->recN; k++) {
        if (s->rec[k].sec != i) {
            continue;
        }
        const float v = f == 0   ? s->rec[k].gpu
                        : f == 1 ? s->rec[k].tris
                        : f == 2 ? s->rec[k].red
                                 : s->rec[k].pres;
        if (v >= 0.0f) {
            tmp[n++] = v;
        }
    }
    if (n == 0) {
        put(out, cap, at, "-");
        return;
    }
    const double m = median_of(tmp, n);
    if (f == 1) {
        if (m >= 1000.0) {
            put(out, cap, at, "%.1fk", m / 1000.0);
        } else {
            put(out, cap, at, "%.0f", m);
        }
    } else if (f == 0) {
        put(out, cap, at, "%.1f", m);
    } else {
        put(out, cap, at, "%.2f", m);
    }
}

static void put_stage(char *out, size_t cap, size_t *at, int stage);

/* the per-second line over the shown seconds */
static void more_text(const PerfSecs *s, unsigned shown, char *out, size_t cap)
{
    size_t at = 0;
    out[0] = '\0';
    if (shown == 0) {
        return;
    }
    put(out, cap, &at,
        "per second (the timed pictures' medians: GPU span ms, VU triangles, reduction ms, "
        "present pass ms; then the longest present interval ms):");
    for (unsigned i = 0; i < shown; i++) {
        if (i == 0 || s->sec[i] != s->sec[i - 1]) {
            put(out, cap, &at, "%s", i ? " | " : " ");
            put_stage(out, cap, &at, s->sec[i]);
        }
        put(out, cap, &at, " ");
        for (int f = 0; f < 4; f++) {
            put_median(s, i, f, out, cap, &at);
            put(out, cap, &at, "/");
        }
        if (s->longest[i] > 0.0f) {
            put(out, cap, &at, "%.0f", (double)s->longest[i]);
        } else {
            put(out, cap, &at, "-");
        }
    }
    if (s->recOver > 0) {
        put(out, cap, &at, "; %u records past the %u held", s->recOver, PERF_SECS_RECS);
    }
}

/* the block's present intervals line */
static void intervals_text(const PerfSecs *s, double tickMs, char *out, size_t cap)
{
    static float tmp[PERF_SECS_IVS];
    size_t at = 0;
    out[0] = '\0';
    put(out, cap, &at, "present intervals (the CPU's present returns): ");
    if (s->ivN == 0) {
        put(out, cap, &at, "none in the block");
        return;
    }
    memcpy(tmp, s->iv, s->ivN * sizeof(float));
    qsort(tmp, s->ivN, sizeof(float), cmpf);
    const unsigned n = s->ivN;
    /* nearest rank: the ceil(q n)-th smallest */
    const unsigned r50 = (n + 1u) / 2u, r90 = (9u * n + 9u) / 10u;
    put(out, cap, &at, "%u in the block, p50 %.1f ms, p90 %.1f ms, longest %.1f ms", n + s->ivOver,
        (double)tmp[r50 > 0 ? r50 - 1u : 0u], (double)tmp[r90 > 0 ? r90 - 1u : 0u], s->ivMax);
    if (tickMs > 0.0) {
        unsigned over[3] = {0, 0, 0};
        for (unsigned i = 0; i < n; i++) {
            const double k = (double)(unsigned long long)((double)tmp[i] / tickMs + 0.5);
            for (int t = 0; t < 3; t++) {
                over[t] += k > (double)(t + 2) ? 1u : 0u;
            }
        }
        put(out, cap, &at,
            "; over 2 ticks %u, over 3 ticks %u, over 4 ticks %u (a tick %.1f ms; an interval in "
            "whole ticks, rounded)",
            over[0], over[1], over[2], tickMs);
    }
    if (s->ivOver > 0) {
        put(out, cap, &at, "; %u past the %u held (in the count and the longest only)", s->ivOver,
            PERF_SECS_IVS);
    }
}

static void put(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
    va_list ap;
    if (*at + 1 >= cap) {
        return;
    }
    va_start(ap, fmt);
    const int w = vsnprintf(out + *at, cap - *at, fmt, ap);
    va_end(ap);
    if (w < 0) {
        out[*at] = '\0';
        return;
    }
    *at = *at + (size_t)w < cap ? *at + (size_t)w : cap - 1;
}

static void put_stage(char *out, size_t cap, size_t *at, int stage)
{
    if (stage == PERF_SECS_NO_STAGE) {
        put(out, cap, at, "?:");
    } else {
        put(out, cap, at, "%d:", stage);
    }
}

size_t perf_secs_block_text(PerfSecs *s, double nowMs, char *out, size_t cap)
{
    return perf_secs_block_lines(s, nowMs, 0.0, out, cap, NULL, 0, NULL, 0);
}

size_t perf_secs_block_lines(PerfSecs *s, double nowMs, double tickMs, char *out, size_t cap,
                             char *more, size_t moreCap, char *intervals, size_t ivCap)
{
    size_t at = 0;
    if (cap > 0) {
        out[0] = '\0';
    }
    if (more != NULL && moreCap > 0) {
        more[0] = '\0';
    }
    if (intervals != NULL && ivCap > 0) {
        intervals[0] = '\0';
    }
    if (s->t0Ms >= 0.0 && nowMs >= s->t0Ms && (nowMs - s->t0Ms) / 1000.0 < PERF_SECS_MAX) {
        (void)second_at(s, nowMs); /* the seconds to now begun (a quiet end) */
    }
    if (cap > 0 && s->n > 0) {
        const double d = (nowMs - s->t0Ms) / 1000.0;
        const unsigned whole = d >= 4e9 ? 4000000000u : (d > 0.0 ? (unsigned)d : 0u);
        const unsigned shown = whole < s->n ? whole : s->n;
        put(out, cap, &at, "pictures a second:");
        for (unsigned i = 0; i < shown; i++) {
            if (i == 0 || s->sec[i] != s->sec[i - 1]) {
                put(out, cap, &at, "%s", i ? " | " : " ");
                put_stage(out, cap, &at, s->sec[i]);
            }
            put(out, cap, &at, " %u", s->pics[i]);
        }
        if (shown == 0) {
            put(out, cap, &at, " ");
            put_stage(out, cap, &at, s->sec[0]);
            put(out, cap, &at, " no whole second");
        }
        if (whole < s->n && (s->pics[whole] > 0 || d - (double)whole >= 0.05)) {
            put(out, cap, &at, " (and %u in the block's last %.1f s)", s->pics[whole],
                d - (double)whole);
        }
        if (s->overS > 0) {
            put(out, cap, &at, "; %u seconds past the %u held, %u pictures in them", s->overS,
                PERF_SECS_MAX, s->over);
        }
        if (more != NULL && moreCap > 0) {
            more_text(s, shown, more, moreCap);
        }
        if (intervals != NULL && ivCap > 0) {
            intervals_text(s, tickMs, intervals, ivCap);
        }
    }
    const int stage = s->stage;
    const double lastEnd = s->lastEndMs;
    perf_secs_reset(s);
    s->stage = stage;
    s->t0Ms = nowMs;
    s->lastEndMs = lastEnd; /* the next block's first interval from it */
    return at;
}

/* the window's (the game thread's: steps, drained records and the block's
   end all run there) */
static PerfSecs s_win = {.t0Ms = -1.0, .stage = PERF_SECS_NO_STAGE};

void perf_secs_window_step(int stage, double nowMs)
{
    perf_secs_step(&s_win, stage, nowMs);
}

void perf_secs_window_picture(double nowMs)
{
    perf_secs_picture(&s_win, nowMs);
}

void perf_secs_window_record(const PerfSecsRec *r, double nowMs)
{
    perf_secs_record(&s_win, r, nowMs);
}

size_t perf_secs_window_block_text(double nowMs, char *out, size_t cap)
{
    return perf_secs_block_text(&s_win, nowMs, out, cap);
}

size_t perf_secs_window_block_lines(double nowMs, double tickMs, char *out, size_t cap, char *more,
                                    size_t moreCap, char *intervals, size_t ivCap)
{
    return perf_secs_block_lines(&s_win, nowMs, tickMs, out, cap, more, moreCap, intervals, ivCap);
}
