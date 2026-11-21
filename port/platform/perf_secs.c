/*
 * port/platform/perf_secs.c
 *
 * Pictures a second by stage (perf_secs.h).
 */
#include "perf_secs.h"

#include <stdarg.h>
#include <stdio.h>
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
    size_t at = 0;
    if (cap > 0) {
        out[0] = '\0';
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
    }
    const int stage = s->stage;
    perf_secs_reset(s);
    s->stage = stage;
    s->t0Ms = nowMs;
    return at;
}

/* the window's (the game thread's: steps, drained records and the block's
   end all run there) */
static PerfSecs s_win = {-1.0, PERF_SECS_NO_STAGE, 0, 0, 0, {0}, {0}, {false}};

void perf_secs_window_step(int stage, double nowMs)
{
    perf_secs_step(&s_win, stage, nowMs);
}

void perf_secs_window_picture(double nowMs)
{
    perf_secs_picture(&s_win, nowMs);
}

size_t perf_secs_window_block_text(double nowMs, char *out, size_t cap)
{
    return perf_secs_block_text(&s_win, nowMs, out, cap);
}
