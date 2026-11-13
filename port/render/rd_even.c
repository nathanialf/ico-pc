/* rd_even.c: the pictures' evenness, one 10 s line (rd_even.h).
 *
 * The line (window_host.c perf_log, after the CPU phases' line):
 *   window: evenness in 10 s: P pictures; the GPU's ends apart (E with both
 *     ends exact, B with a bound at one, U without a GPU end) by refreshes
 *     of R ms: under half a refresh N0, 1 refresh N1, 2 refreshes N2, 3
 *     refreshes N3, 4 or more N4; the content's step over them in ticks
 *     (mean, deviation): under half a refresh M0 (D0), 1 refresh M1 (D1),
 *     ...; every step (S): mean M (deviation D), the largest L, back K times
 *     (the most X), C chain restarts; the alpha clamped at 0 Z times (the
 *     most X below), at 0.999 T times (the most X above); the present clock:
 *     N samples, resets F forward and K back, H held instead of going back
 *     within a frame pair (the most X ms); the alpha's sample to the GPU's
 *     end over N pictures: 10th A, median B, 90th C ms, the hand-over to it
 *     10th A, median B, 90th C ms (the drain before the hand-over D ms on
 *     average over N)...; the GPU's predicted end: ... (rd_pace_gpu.c
 *     rd_pace_gpu_format, with pictures as made)
 *
 * The sums are atomics (the line runs on the window's thread); the chain
 * (the present before) is the replaying thread's own, as replays never
 * overlap. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rd_even.h"
#include "rd_pace_gpu.h"

static RdEvenWindow s_sum;

/* the present before, on the thread that collects the records */
static struct {
    bool have, exact;
    uint32_t frame;
    double content, endMs;
} s_prev;

/* rd_even_hand_note's stamps for the picture this thread is making */
static _Thread_local double t_askMs, t_handMs;

static void add(uint64_t *at, uint64_t v)
{
    if (v) {
        __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
    }
}

static void add32(uint32_t *at, uint32_t v)
{
    __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
}

static void addSigned(int64_t *at, int64_t v)
{
    if (v) {
        __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
    }
}

/* the larger of *at and v, lock-free against the line's exchange */
static void keepMax(uint32_t *at, uint32_t v)
{
    uint32_t cur = __atomic_load_n(at, __ATOMIC_RELAXED);
    while (v > cur &&
           !__atomic_compare_exchange_n(at, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* ticks in RD_EVEN_UNITS, rounded */
static int64_t unitsSigned(double ticks)
{
    return (int64_t)floor(ticks * RD_EVEN_UNITS + 0.5);
}

static uint32_t units(double ticks)
{
    const double u = floor(ticks * RD_EVEN_UNITS + 0.5);
    return u <= 0.0 ? 0u : (u >= 4.0e9 ? 4000000000u : (uint32_t)u);
}

void rd_even_picture(const RdEvenPic *p)
{
    const double a = p->alpha < 0.0f ? 0.0 : (p->alpha > 1.0f ? 1.0 : (double)p->alpha);
    const double content = (double)p->frame + a;
    add(&s_sum.pictures, 1u);
    if (s_prev.have) {
        const int64_t df = (int64_t)p->frame - (int64_t)s_prev.frame;
        if (df < 0 || df > RD_EVEN_FRAME_JUMP_MAX) {
            add(&s_sum.breaks, 1u);
        } else {
            const int64_t u = unitsSigned(content - s_prev.content);
            const uint64_t u2 = (uint64_t)(u * u);
            add(&s_sum.steps, 1u);
            addSigned(&s_sum.stepSum, u);
            add(&s_sum.stepSum2, u2);
            if (u > 0) {
                keepMax(&s_sum.stepMax, (uint32_t)u);
            } else if (u < 0) {
                add(&s_sum.back, 1u);
                keepMax(&s_sum.backMax, (uint32_t)(-u));
            }
            if (s_prev.endMs > 0.0 && p->gpuEndMs > 0.0) {
                if (s_prev.exact && p->gpuExact && p->gpuEndMs > s_prev.endMs) {
                    const double b = floor((p->gpuEndMs - s_prev.endMs) / RD_EVEN_GAP_BIN_MS);
                    const int bin =
                        b >= (double)(RD_EVEN_GAP_BINS - 1) ? RD_EVEN_GAP_BINS - 1 : (int)b;
                    add32(&s_sum.gap[bin], 1u);
                    addSigned(&s_sum.gapStep[bin], u);
                    add(&s_sum.gapStep2[bin], u2);
                } else {
                    add(&s_sum.bound, 1u);
                }
            } else {
                add(&s_sum.noEnd, 1u);
            }
        }
    }
    if (p->askMs > 0.0 && p->handMs >= p->askMs) {
        add(&s_sum.drainUs, (uint64_t)floor((p->handMs - p->askMs) * 1000.0 + 0.5));
        add(&s_sum.drainN, 1u);
        if (p->gpuEndMs > 0.0 && p->gpuExact && p->gpuEndMs >= p->askMs) {
            const double b = floor((p->gpuEndMs - p->askMs) / RD_EVEN_LAT_BIN_MS);
            const int bin = b >= (double)(RD_EVEN_LAT_BINS - 1) ? RD_EVEN_LAT_BINS - 1 : (int)b;
            add32(&s_sum.lat[bin], 1u);
            add(&s_sum.latN, 1u);
            if (p->gpuEndMs >= p->handMs) {
                const double h = floor((p->gpuEndMs - p->handMs) / RD_EVEN_LAT_BIN_MS);
                const int hb = h >= (double)(RD_EVEN_LAT_BINS - 1) ? RD_EVEN_LAT_BINS - 1 : (int)h;
                add32(&s_sum.latHand[hb], 1u);
                add(&s_sum.latHandN, 1u);
            }
        }
    }
    s_prev.have = true;
    s_prev.frame = p->frame;
    s_prev.content = content;
    s_prev.endMs = p->gpuEndMs;
    s_prev.exact = p->gpuExact;
}

void rd_even_clock_note(double raw, int reset, double heldMs)
{
    add(&s_sum.samples, 1u);
    if (raw < 0.0) {
        add(&s_sum.clampLo, 1u);
        keepMax(&s_sum.clampLoMax, units(-raw));
    } else if (raw > 0.999) {
        add(&s_sum.clampHi, 1u);
        keepMax(&s_sum.clampHiMax, units(raw - 0.999));
    }
    if (reset > 0) {
        add(&s_sum.resetFwd, 1u);
    } else if (reset < 0) {
        add(&s_sum.resetBack, 1u);
    }
    if (heldMs > 0.0) {
        const double us = floor(heldMs * 1000.0 + 0.5);
        add(&s_sum.held, 1u);
        keepMax(&s_sum.heldMaxUs, us >= 4.0e9 ? 4000000000u : (uint32_t)us);
    }
}

static int rebBin(double ms)
{
    const double b = floor((ms - RD_EVEN_REB_FROM_MS) / RD_EVEN_REB_BIN_MS);
    return b <= 0.0 ? 0 : (b >= (double)(RD_EVEN_REB_BINS - 1) ? RD_EVEN_REB_BINS - 1 : (int)b);
}

void rd_even_rebase_note(double beforeMs, double afterMs, double shiftMs, bool held)
{
    add32(&s_sum.rebBefore[rebBin(beforeMs)], 1u);
    add32(&s_sum.rebAfter[rebBin(afterMs)], 1u);
    add32(&s_sum.rebShift[rebBin(shiftMs)], 1u);
    add(&s_sum.rebN, 1u);
    if (held) {
        add(&s_sum.rebHeld, 1u);
    }
}

/* the content chain, on the poster's thread */
static struct {
    bool have;
    double content, hand;
    uint32_t run;
} s_cont;

/* the smaller of *at and v, 0 at *at meaning none yet */
static void keepMin(uint32_t *at, uint32_t v)
{
    uint32_t cur = __atomic_load_n(at, __ATOMIC_RELAXED);
    while ((cur == 0u || v < cur) &&
           !__atomic_compare_exchange_n(at, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void rd_even_content_note(double contentMs, double handMs)
{
    const double gap = handMs - s_cont.hand;
    if (s_cont.have && gap > 0.0 && gap <= 1000.0) {
        const double step = contentMs - s_cont.content;
        if (gap > RD_EVEN_GAP_MIN_MS) {
            add(&s_sum.pairs, 1u);
            if (fabs(step) < RD_EVEN_REPEAT_MS) {
                add(&s_sum.repeats, 1u);
                if (s_cont.run++ == 0u) {
                    add(&s_sum.runs, 1u);
                }
                keepMax(&s_sum.runMax, s_cont.run);
            } else {
                s_cont.run = 0u;
            }
            const double ratio = step / gap;
            const double mil = floor((ratio < 0.0 ? 0.0 : ratio) * 1000.0 + 0.5);
            const uint32_t m = mil >= 4.0e9 ? 4000000000u : (uint32_t)mil;
            keepMax(&s_sum.ratioMaxMil, m);
            keepMin(&s_sum.ratioMinMil, m + 1u); /* 0 is "none" after the take */
            if (ratio < RD_EVEN_RATIO_LO || ratio > RD_EVEN_RATIO_HI) {
                add(&s_sum.outside, 1u);
            }
        }
    } else {
        s_cont.run = 0u;
    }
    s_cont.have = true;
    s_cont.content = contentMs;
    s_cont.hand = handMs;
}

void rd_even_hand_note(double askMs, double handMs)
{
    t_askMs = askMs;
    t_handMs = handMs;
}

void rd_even_hand_take(double *askMs, double *handMs)
{
    *askMs = t_askMs;
    *handMs = t_handMs;
    t_askMs = t_handMs = 0.0;
}

static uint64_t take(uint64_t *at)
{
    return __atomic_exchange_n(at, 0u, __ATOMIC_RELAXED);
}

static uint32_t take32(uint32_t *at)
{
    return __atomic_exchange_n(at, 0u, __ATOMIC_RELAXED);
}

static int64_t takeSigned(int64_t *at)
{
    return __atomic_exchange_n(at, 0, __ATOMIC_RELAXED);
}

void rd_even_take(RdEvenWindow *w)
{
    for (int b = 0; b < RD_EVEN_GAP_BINS; b++) {
        w->gap[b] = take32(&s_sum.gap[b]);
        w->gapStep[b] = takeSigned(&s_sum.gapStep[b]);
        w->gapStep2[b] = take(&s_sum.gapStep2[b]);
    }
    for (int b = 0; b < RD_EVEN_LAT_BINS; b++) {
        w->lat[b] = take32(&s_sum.lat[b]);
        w->latHand[b] = take32(&s_sum.latHand[b]);
    }
    for (int b = 0; b < RD_EVEN_REB_BINS; b++) {
        w->rebBefore[b] = take32(&s_sum.rebBefore[b]);
        w->rebAfter[b] = take32(&s_sum.rebAfter[b]);
        w->rebShift[b] = take32(&s_sum.rebShift[b]);
    }
    w->rebN = take(&s_sum.rebN);
    w->rebHeld = take(&s_sum.rebHeld);
    w->pairs = take(&s_sum.pairs);
    w->repeats = take(&s_sum.repeats);
    w->runs = take(&s_sum.runs);
    w->outside = take(&s_sum.outside);
    w->runMax = take32(&s_sum.runMax);
    w->ratioMaxMil = take32(&s_sum.ratioMaxMil);
    w->ratioMinMil = take32(&s_sum.ratioMinMil);
    w->pictures = take(&s_sum.pictures);
    w->bound = take(&s_sum.bound);
    w->noEnd = take(&s_sum.noEnd);
    w->steps = take(&s_sum.steps);
    w->stepSum = takeSigned(&s_sum.stepSum);
    w->stepSum2 = take(&s_sum.stepSum2);
    w->stepMax = take32(&s_sum.stepMax);
    w->back = take(&s_sum.back);
    w->backMax = take32(&s_sum.backMax);
    w->breaks = take(&s_sum.breaks);
    w->latN = take(&s_sum.latN);
    w->latHandN = take(&s_sum.latHandN);
    w->drainUs = take(&s_sum.drainUs);
    w->drainN = take(&s_sum.drainN);
    w->samples = take(&s_sum.samples);
    w->clampLo = take(&s_sum.clampLo);
    w->clampHi = take(&s_sum.clampHi);
    w->clampLoMax = take32(&s_sum.clampLoMax);
    w->clampHiMax = take32(&s_sum.clampHiMax);
    w->resetFwd = take(&s_sum.resetFwd);
    w->resetBack = take(&s_sum.resetBack);
    w->held = take(&s_sum.held);
    w->heldMaxUs = take32(&s_sum.heldMaxUs);
}

typedef struct Out {
    char *buf;
    size_t n, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *dst = o->at < o->n ? o->buf + o->at : NULL;
    const size_t room = o->at < o->n ? o->n - o->at : 0;
    const int w = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->at += (size_t)w;
    }
}

/* the p-th share's bin of a histogram, by the bin's middle */
static double share(const uint32_t *h, int bins, uint64_t n, double binMs, double p)
{
    uint64_t rank = (uint64_t)ceil(p * (double)n), c = 0;
    rank = rank < 1u ? 1u : rank;
    for (int b = 0; b < bins; b++) {
        c += h[b];
        if (c >= rank) {
            return ((double)b + 0.5) * binMs;
        }
    }
    return ((double)bins - 0.5) * binMs;
}

/* a mean and its deviation in ticks from sums in RD_EVEN_UNITS */
static void meanDev(uint64_t n, double sum, double sum2, double *mean, double *dev)
{
    const double m = n ? sum / (double)n : 0.0;
    const double v = n ? sum2 / (double)n - m * m : 0.0;
    *mean = m / RD_EVEN_UNITS;
    *dev = v > 0.0 ? sqrt(v) / RD_EVEN_UNITS : 0.0;
}

/* the refresh grid's classes: under half a refresh, 1, 2, 3, 4 or more */
#define GRID_CLASSES 5

bool rd_even_format(const RdEvenWindow *w, double refreshMs, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (w->pictures == 0 && w->samples == 0) {
        return false;
    }
    Out o = {out, size, 0};
    uint64_t exact = 0;
    int64_t exactSum = 0;
    double exactSum2 = 0.0;
    uint64_t cn[GRID_CLASSES] = {0};
    double cs[GRID_CLASSES] = {0.0}, cs2[GRID_CLASSES] = {0.0};
    for (int b = 0; b < RD_EVEN_GAP_BINS; b++) {
        exact += w->gap[b];
        exactSum += w->gapStep[b];
        exactSum2 += (double)w->gapStep2[b];
        if (refreshMs > 0.0 && w->gap[b] != 0) {
            const double mid = ((double)b + 0.5) * RD_EVEN_GAP_BIN_MS;
            const double k = floor(mid / refreshMs + 0.5);
            const int c = k >= (double)(GRID_CLASSES - 1) ? GRID_CLASSES - 1 : (int)k;
            cn[c] += w->gap[b];
            cs[c] += (double)w->gapStep[b];
            cs2[c] += (double)w->gapStep2[b];
        }
    }
    put(&o,
        "evenness in 10 s: %llu pictures; the GPU's ends apart (%llu with both ends exact, %llu "
        "with a bound at one, %llu without a GPU end)",
        (unsigned long long)w->pictures, (unsigned long long)exact, (unsigned long long)w->bound,
        (unsigned long long)w->noEnd);
    static const char *const kClass[GRID_CLASSES] = {"under half a refresh", "1 refresh",
                                                     "2 refreshes", "3 refreshes", "4 or more"};
    if (refreshMs > 0.0) {
        put(&o, " by refreshes of %.2f ms:", refreshMs);
        for (int c = 0; c < GRID_CLASSES; c++) {
            put(&o, "%s %s %llu", c ? "," : "", kClass[c], (unsigned long long)cn[c]);
        }
        put(&o, "; the content's step over them in ticks (mean, deviation):");
        for (int c = 0; c < GRID_CLASSES; c++) {
            double m, d;
            meanDev(cn[c], cs[c], cs2[c], &m, &d);
            if (cn[c]) {
                put(&o, "%s %s %.3f (%.3f)", c ? "," : "", kClass[c], m, d);
            } else {
                put(&o, "%s %s -", c ? "," : "", kClass[c]);
            }
        }
    } else {
        put(&o, ": the refresh not known, not put on its grid");
    }
    {
        double m, d;
        meanDev(w->steps, (double)w->stepSum, (double)w->stepSum2, &m, &d);
        /* the steps over gaps not put on the grid (a bound or no GPU end
           at one side): every step's sums less the exact gaps' */
        const uint64_t un = w->steps > exact ? w->steps - exact : 0u;
        double um, ud;
        meanDev(un, (double)(w->stepSum - exactSum), (double)w->stepSum2 - exactSum2, &um, &ud);
        put(&o, "; every step (%llu): mean %.3f (deviation %.3f)", (unsigned long long)w->steps, m,
            d);
        if (un) {
            put(&o, ", %llu of them over gaps not on the grid: mean %.3f (deviation %.3f)",
                (unsigned long long)un, um, ud);
        }
        put(&o, ", the largest %.3f, back %llu times (the most %.3f), %llu chain restarts",
            (double)w->stepMax / RD_EVEN_UNITS, (unsigned long long)w->back,
            (double)w->backMax / RD_EVEN_UNITS, (unsigned long long)w->breaks);
    }
    put(&o,
        "; the alpha clamped at 0 %llu times (the most %.3f below), at 0.999 %llu times (the "
        "most %.3f above); the present clock: %llu samples, resets %llu forward and %llu back, "
        "%llu held instead of going back within a frame pair (the most %.2f ms)",
        (unsigned long long)w->clampLo, (double)w->clampLoMax / RD_EVEN_UNITS,
        (unsigned long long)w->clampHi, (double)w->clampHiMax / RD_EVEN_UNITS,
        (unsigned long long)w->samples, (unsigned long long)w->resetFwd,
        (unsigned long long)w->resetBack, (unsigned long long)w->held,
        (double)w->heldMaxUs / 1000.0);
    if (w->latN) {
        put(&o,
            "; the alpha's sample to the GPU's end over %llu pictures: 10th %.1f, median %.1f, "
            "90th %.1f ms",
            (unsigned long long)w->latN,
            share(w->lat, RD_EVEN_LAT_BINS, w->latN, RD_EVEN_LAT_BIN_MS, 0.1),
            share(w->lat, RD_EVEN_LAT_BINS, w->latN, RD_EVEN_LAT_BIN_MS, 0.5),
            share(w->lat, RD_EVEN_LAT_BINS, w->latN, RD_EVEN_LAT_BIN_MS, 0.9));
        if (w->latHandN) {
            put(&o, ", the hand-over to it 10th %.1f, median %.1f, 90th %.1f ms",
                share(w->latHand, RD_EVEN_LAT_BINS, w->latHandN, RD_EVEN_LAT_BIN_MS, 0.1),
                share(w->latHand, RD_EVEN_LAT_BINS, w->latHandN, RD_EVEN_LAT_BIN_MS, 0.5),
                share(w->latHand, RD_EVEN_LAT_BINS, w->latHandN, RD_EVEN_LAT_BIN_MS, 0.9));
        }
    } else {
        put(&o, "; the alpha's sample to the GPU's end: not measured (no exact GPU end of a "
                "picture handed over)");
    }
    if (w->drainN) {
        put(&o, " (the drain before the hand-over %.2f ms on average over %llu)",
            (double)w->drainUs / 1000.0 / (double)w->drainN, (unsigned long long)w->drainN);
    }
    if (w->rebN) {
        /* the bins' middles from RD_EVEN_REB_FROM_MS */
#define REB(h, p)                                                                                  \
    (share((h), RD_EVEN_REB_BINS, w->rebN, RD_EVEN_REB_BIN_MS, (p)) + RD_EVEN_REB_FROM_MS)
        put(&o,
            "; re-based to the hand-over: %llu pictures (the shift 10th %.2f, median %.2f, 90th "
            "%.2f ms; %llu held at the frame's last value), the content's moment to the "
            "hand-over 10th %.2f, median %.2f, 90th %.2f ms (at the sample 10th %.2f, median "
            "%.2f, 90th %.2f)",
            (unsigned long long)w->rebN, REB(w->rebShift, 0.1), REB(w->rebShift, 0.5),
            REB(w->rebShift, 0.9), (unsigned long long)w->rebHeld, REB(w->rebAfter, 0.1),
            REB(w->rebAfter, 0.5), REB(w->rebAfter, 0.9), REB(w->rebBefore, 0.1),
            REB(w->rebBefore, 0.5), REB(w->rebBefore, 0.9));
#undef REB
    }
    if (w->pairs) {
        /* ratioMinMil holds the least ratio plus one thousandth (0: none) */
        const double lo = w->ratioMinMil ? (double)(w->ratioMinMil - 1u) / 1000.0 : 0.0;
        put(&o,
            "; the content drawn against the hand-overs over %llu pairs: %llu repeats (the same "
            "moment as the picture before) in %llu runs, the longest %u; the step over the real "
            "gap from %.3f to %.3f, outside %.1f-%.1f in %llu",
            (unsigned long long)w->pairs, (unsigned long long)w->repeats,
            (unsigned long long)w->runs, w->runMax, lo, (double)w->ratioMaxMil / 1000.0,
            RD_EVEN_RATIO_LO, RD_EVEN_RATIO_HI, (unsigned long long)w->outside);
    }
    return true;
}

void rd_even_line(double refreshMs)
{
    static RdEvenWindow w; /* 6 KB: off the window thread's stack */
    static RdPaceGpuWindow pw;
    static char line[4096];
    rd_even_take(&w);
    rd_pace_gpu_take(&pw);
    if (rd_even_format(&w, refreshMs, line, sizeof line)) {
        /* the GPU queue's model, its last clause (rd_pace_gpu.h) */
        const size_t at = strlen(line);
        (void)rd_pace_gpu_format(&pw, line + at, sizeof line - at);
        fprintf(stderr, "window: %s\n", line);
    }
}
