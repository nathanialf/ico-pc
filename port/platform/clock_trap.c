/*
 * port/platform/clock_trap.c
 *
 * One picture a tick told to the system, and its lines (clock_trap.h).
 */
#include "clock_trap.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void clock_trap_reset(ClockTrap *c)
{
    memset(c, 0, sizeof(*c));
}

void clock_trap_target(ClockTrap *c, double hz)
{
    c->targetHz = hz > 0.0 ? hz : 0.0;
}

bool clock_trap_judged(const ClockTrapSecond *s, bool whole)
{
    if (s->targetHz > 0.0) {
        return whole && s->pictures > 0u;
    }
    return s->frames >= CLOCK_TRAP_MIN_FRAMES;
}

bool clock_trap_one_a_tick(const ClockTrapSecond *s)
{
    return s->asMade && s->frames >= CLOCK_TRAP_MIN_FRAMES &&
           (uint64_t)s->pictures * 10u <= (uint64_t)s->frames * 11u &&
           (uint64_t)s->asks * 2u >= s->frames && (uint64_t)s->held * 2u >= s->asks;
}

bool clock_trap_worse(const ClockTrapSecond *a, const ClockTrapSecond *b)
{
    if (a->targetHz > 0.0 && b->targetHz > 0.0) {
        const double sa = a->pictures / a->targetHz, sb = b->pictures / b->targetHz;

        if (sa != sb) {
            return sa < sb;
        }
    } else {
        const uint64_t pa = (uint64_t)a->pictures * b->frames,
                       pb = (uint64_t)b->pictures * a->frames;

        if (pa != pb) {
            return pa < pb;
        }
    }
    const double ca = a->cpuN ? a->cpuMs / a->cpuN : 0.0, cb = b->cpuN ? b->cpuMs / b->cpuN : 0.0;

    return ca > cb;
}

/* the open second closed (whole: by an event within the gap of its
   start): judged into the block, the one-a-tick rule */
static unsigned close_second(ClockTrap *c, ClockTrapSecond *closed, bool whole)
{
    unsigned ev = CLOCK_TRAP_EV_CLOSED;
    ClockTrapBlock *b = &c->block;
    const ClockTrapSecond *s = &c->cur;

    if (closed != NULL) {
        *closed = *s;
    }
    if (s->frames >= CLOCK_TRAP_MIN_FRAMES) {
        b->seconds++;
    }
    if (clock_trap_judged(s, whole)) {
        b->judged++;
        if (!b->haveWorst || clock_trap_worse(s, &b->worst)) {
            b->worst = *s;
            b->haveWorst = true;
        }
    }
    if (clock_trap_one_a_tick(s)) {
        b->oneATick++;
        ev |= CLOCK_TRAP_EV_ONE;
    }
    return ev;
}

/* the seconds tMs has passed closed (the last one's events), the open one
   started where it belongs */
static unsigned advance(ClockTrap *c, double tMs, ClockTrapSecond *closed)
{
    unsigned ev = 0;

    if (!c->open) {
        memset(&c->cur, 0, sizeof(c->cur));
        c->cur.t0Ms = tMs;
        c->open = true;
        return 0;
    }
    if (tMs < c->cur.t0Ms + 1000.0) {
        return 0;
    }
    const bool whole = tMs - c->cur.t0Ms < CLOCK_TRAP_GAP_RESET_MS;

    ev = close_second(c, closed, whole);
    const double next = whole ? c->cur.t0Ms + 1000.0 : tMs;

    memset(&c->cur, 0, sizeof(c->cur));
    c->cur.t0Ms = next;
    return ev;
}

unsigned clock_trap_picture(ClockTrap *c, double tMs, uint32_t frame, double cpuMs,
                            ClockTrapSecond *closed)
{
    const unsigned ev = advance(c, tMs, closed);

    c->cur.pictures++;
    c->cur.targetHz = c->targetHz;
    c->block.pictures++;
    if (!c->haveFrame || frame != c->lastFrame) {
        c->cur.frames++;
        c->block.frames++;
        c->lastFrame = frame;
        c->haveFrame = true;
    }
    if (cpuMs >= 0.0) {
        c->cur.cpuMs += cpuMs;
        c->cur.cpuN++;
        c->block.cpuMs += cpuMs;
        c->block.cpuN++;
    }
    return ev;
}

unsigned clock_trap_ask(ClockTrap *c, double tMs, bool held, bool asMade, ClockTrapSecond *closed)
{
    const unsigned ev = advance(c, tMs, closed);

    c->cur.asks++;
    c->cur.held += held ? 1u : 0u;
    c->cur.asMade |= asMade;
    return ev;
}

void clock_trap_block_take(ClockTrap *c, ClockTrapBlock *out)
{
    *out = c->block;
    memset(&c->block, 0, sizeof(c->block));
}

/* --- the clusters --------------------------------------------------------- */

void clock_trap_clusters(const GpuSysfsBlock *b, ClockTrapClusters *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < b->cpuCount && out->n < CLOCK_TRAP_CLUSTERS;) {
        int j = i;
        unsigned n = 0;
        double sum = 0.0;

        while (j + 1 < b->cpuCount && b->cpuKhz[j + 1] == b->cpuKhz[i]) {
            j++;
        }
        for (int k = i; k <= j; k++) {
            n += b->cpuN[k];
            sum += b->cpuSum[k];
        }
        if (n != 0) {
            out->first[out->n] = i;
            out->last[out->n] = j;
            out->mean[out->n] = sum / n;
            out->samples[out->n] = n;
            out->n++;
        }
        i = j + 1;
    }
}

void clock_trap_hist_push(ClockTrapHist *h, const ClockTrapClusters *c)
{
    h->h[h->at] = *c;
    h->at = (h->at + 1u) % CLOCK_TRAP_HIST;
    h->n += h->n < CLOCK_TRAP_HIST ? 1u : 0u;
}

int clock_trap_dips(const ClockTrapHist *h, const ClockTrapClusters *now, ClockTrapDip *out,
                    int cap)
{
    int got = 0;

    for (int i = 0; i < now->n && got < cap; i++) {
        double v[CLOCK_TRAP_HIST];
        unsigned m = 0;

        if (now->samples[i] < CLOCK_TRAP_DIP_MIN_SAMPLES) {
            continue;
        }
        for (unsigned k = 0; k < h->n; k++) {
            const ClockTrapClusters *e = &h->h[k];

            for (int j = 0; j < e->n; j++) {
                if (e->first[j] == now->first[i] && e->last[j] == now->last[i]) {
                    v[m++] = e->mean[j];
                    break;
                }
            }
        }
        if (m < 2u) {
            continue;
        }
        /* the median (insertion sort of at most CLOCK_TRAP_HIST) */
        for (unsigned a = 1; a < m; a++) {
            const double x = v[a];
            unsigned b = a;

            while (b > 0 && v[b - 1] > x) {
                v[b] = v[b - 1];
                b--;
            }
            v[b] = x;
        }
        const double med = (m & 1u) ? v[m / 2u] : 0.5 * (v[m / 2u - 1u] + v[m / 2u]);

        if (now->mean[i] < CLOCK_TRAP_DIP_SHARE * med) {
            out[got].first = now->first[i];
            out[got].last = now->last[i];
            out[got].mean = now->mean[i];
            out[got].median = med;
            out[got].blocks = m;
            got++;
        }
    }
    return got;
}

/* --- the lines ------------------------------------------------------------ */

typedef struct Out {
    char *buf;
    size_t cap, len;
    int total;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(o->len < o->cap ? o->buf + o->len : NULL,
                            o->len < o->cap ? o->cap - o->len : 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    o->total += n;
    o->len = o->len + (size_t)n < o->cap ? o->len + (size_t)n : o->cap;
}

static Out out_of(char *buf, size_t cap)
{
    Out o = {buf, cap, 0, 0};

    if (cap > 0) {
        buf[0] = '\0';
    }
    return o;
}

static void cores_words(Out *o, int first, int last)
{
    if (last > first) {
        put(o, "cores %d-%d", first, last);
    } else {
        put(o, "core %d", first);
    }
}

/* "; the CPU clocks in that second (N samples, MHz): cores a-b x y, ..."
   for the second from t0Ms (ring NULL or no cores: not sampled) */
static void ring_text(Out *o, const GpuSysfsRing *ring, double t0Ms)
{
    if (ring == NULL || ring->cpuCount == 0) {
        put(o, "; the CPU clocks not sampled here");
        return;
    }
    unsigned inside = 0;

    for (uint32_t k = 0; k < ring->count; k++) {
        const uint32_t at = (ring->next + GPU_SYSFS_RING - ring->count + k) % GPU_SYSFS_RING;
        const double t = ring->s[at].tMs;

        inside += t >= t0Ms && t < t0Ms + 1000.0;
    }
    put(o, "; the CPU clocks in that second (%u samples, MHz):", inside);
    const char *sep = " ";

    for (int i = 0; i < ring->cpuCount;) {
        int j = i;

        while (j + 1 < ring->cpuCount && ring->cpuKhz[j + 1] == ring->cpuKhz[i]) {
            j++;
        }
        put(o, "%s", sep);
        cores_words(o, i, j);
        unsigned shown = 0;

        for (uint32_t k = 0; k < ring->count; k++) {
            const uint32_t at = (ring->next + GPU_SYSFS_RING - ring->count + k) % GPU_SYSFS_RING;
            const GpuSysfsSample *s = &ring->s[at];
            unsigned n = 0;
            double sum = 0.0;

            if (!(s->tMs >= t0Ms && s->tMs < t0Ms + 1000.0)) {
                continue;
            }
            for (int c = i; c <= j && c < GPU_SYSFS_CPUS; c++) {
                if (s->cpuMhz[c] != 0) {
                    sum += s->cpuMhz[c];
                    n++;
                }
            }
            if (n != 0) {
                put(o, " %.0f", sum / n);
                shown++;
            }
        }
        if (shown == 0) {
            put(o, " none");
        }
        sep = ", ";
        i = j + 1;
    }
}

int clock_trap_dip_text(const ClockTrapDip *d, int n, const ClockTrapHint *hint, char *out,
                        size_t cap)
{
    if (n <= 0) {
        return 0; /* out untouched */
    }
    Out o = out_of(out, cap);

    put(&o, "; the CPU clocks under their level: ");
    for (int i = 0; i < n; i++) {
        put(&o, "%s", i ? ", " : "");
        cores_words(&o, d[i].first, d[i].last);
        put(&o, " averaged %.0f MHz against %.0f (the median of the %u blocks before; %.0f %%)",
            d[i].mean, d[i].median, d[i].blocks,
            d[i].median > 0.0 ? 100.0 * d[i].mean / d[i].median : 0.0);
    }
    if (hint != NULL && hint->known) {
        put(&o, "; the performance hint's target %.2f ms a picture, its reports %.2f ms CPU",
            hint->targetMs, hint->cpuMs);
        if (hint->gpuMs > 0.0) {
            put(&o, " / %.2f ms GPU", hint->gpuMs);
        }
        put(&o, " (over target in %u of %u)", hint->over, hint->reports);
    } else {
        put(&o, "; no performance hint session");
    }
    return o.total;
}

int clock_trap_worst_text(const ClockTrapBlock *b, double blockStartMs, const GpuSysfsRing *ring,
                          char *out, size_t cap)
{
    Out o = out_of(out, cap);
    const ClockTrapSecond *w = &b->worst;

    put(&o, "worst second of the block: ");
    if (!b->haveWorst) {
        put(&o,
            "none judged (no whole second with a picture at a known rate, and fewer than %u "
            "game frames in every second)",
            CLOCK_TRAP_MIN_FRAMES);
        return o.total;
    }
    put(&o, "%u pictures for %u game frames (%.2f a tick", w->pictures, w->frames,
        w->frames ? (double)w->pictures / w->frames : 0.0);
    if (w->targetHz > 0.0) {
        put(&o, "; %.0f %% of the %.1f a second in force; %u whole seconds judged",
            100.0 * w->pictures / w->targetHz, w->targetHz, b->judged);
    }
    put(&o,
        "), from %.1f s into the block; the step rule held %u of %u further pictures asked; a "
        "picture's CPU ",
        w->t0Ms > blockStartMs ? (w->t0Ms - blockStartMs) / 1000.0 : 0.0, w->held, w->asks);
    if (w->cpuN) {
        put(&o, "%.1f ms", w->cpuMs / w->cpuN);
    } else {
        put(&o, "not known");
    }
    if (b->cpuN) {
        put(&o, " (the block's %.1f)", b->cpuMs / b->cpuN);
    }
    ring_text(&o, ring, w->t0Ms);
    put(&o, "; %u of %u seconds at one a tick behind the step rule", b->oneATick, b->seconds);
    return o.total;
}

int clock_trap_second_text(const ClockTrapSecond *s, const GpuSysfsRing *ring, char *out,
                           size_t cap)
{
    Out o = out_of(out, cap);

    put(&o, "%u pictures for %u game frames (%.2f a tick), a picture's CPU ", s->pictures,
        s->frames, s->frames ? (double)s->pictures / s->frames : 0.0);
    if (s->cpuN) {
        put(&o, "%.1f ms", s->cpuMs / s->cpuN);
    } else {
        put(&o, "not known");
    }
    put(&o, "; the step rule held %u of %u further pictures asked", s->held, s->asks);
    ring_text(&o, ring, s->t0Ms);
    return o.total;
}

/* the median of n values (sorted in place; n at least 1) */
static double median_of(double *v, unsigned m)
{
    for (unsigned a = 1; a < m; a++) {
        const double x = v[a];
        unsigned b = a;

        while (b > 0 && v[b - 1] > x) {
            v[b] = v[b - 1];
            b--;
        }
        v[b] = x;
    }
    return (m & 1u) ? v[m / 2u] : 0.5 * (v[m / 2u - 1u] + v[m / 2u]);
}

double clock_trap_cpu_median(const ClockTrapHist *h, unsigned *blocks)
{
    double v[CLOCK_TRAP_HIST];
    unsigned m = 0;

    for (unsigned k = 0; k < h->n && k < CLOCK_TRAP_HIST; k++) {
        if (h->h[k].cpuMs > 0.0) {
            v[m++] = h->h[k].cpuMs;
        }
    }
    if (blocks != NULL) {
        *blocks = m;
    }
    return m > 0u ? median_of(v, m) : 0.0;
}

bool clock_trap_cpu_raised(const ClockTrapSecond *s, double median, unsigned blocks)
{
    if (s->cpuN == 0u) {
        return false;
    }
    return blocks < 2u || s->cpuMs / s->cpuN >= CLOCK_TRAP_DUE_CPU_RISE * median;
}

bool clock_trap_due(const ClockTrapHist *h, const ClockTrapClusters *now, unsigned pictures,
                    unsigned frames, double cpuMs, int core, double coreMhz, ClockTrapDue *out)
{
    double v[CLOCK_TRAP_HIST];
    unsigned m = 0;

    memset(out, 0, sizeof(*out));
    out->pictures = pictures;
    out->frames = frames;
    out->cpuMs = cpuMs;
    out->core = core;
    out->coreMhz = coreMhz;
    out->clFirst = out->clLast = -1;
    out->cpuMedian = clock_trap_cpu_median(h, &m);
    out->cpuBlocks = m;
    /* the render thread's cluster in the block and its median before */
    for (int i = 0; now != NULL && core >= 0 && i < now->n; i++) {
        if (core < now->first[i] || core > now->last[i]) {
            continue;
        }
        unsigned n = 0;

        out->clFirst = now->first[i];
        out->clLast = now->last[i];
        for (unsigned k = 0; k < h->n; k++) {
            const ClockTrapClusters *e = &h->h[k];

            for (int j = 0; j < e->n; j++) {
                if (e->first[j] == now->first[i] && e->last[j] == now->last[i]) {
                    v[n++] = e->mean[j];
                    break;
                }
            }
        }
        out->clBlocks = n;
        out->clMedian = n > 0u ? median_of(v, n) : 0.0;
        break;
    }
    out->due = frames >= CLOCK_TRAP_MIN_FRAMES &&
               (uint64_t)pictures * 10u <= (uint64_t)frames * CLOCK_TRAP_DUE_TICK_TENTHS &&
               cpuMs > 0.0 && m >= 2u && cpuMs >= CLOCK_TRAP_DUE_CPU_RISE * out->cpuMedian;
    return out->due;
}

int clock_trap_due_text(const ClockTrapDue *d, char *out, size_t cap)
{
    Out o = out_of(out, cap);

    put(&o,
        "; at %.2f pictures a tick with a picture's CPU %.1f ms against %.1f (the median of the "
        "%u blocks before)",
        d->frames ? (double)d->pictures / d->frames : 0.0, d->cpuMs, d->cpuMedian, d->cpuBlocks);
    if (d->core < 0) {
        put(&o, "; the render thread's core not known");
        return o.total;
    }
    put(&o, "; the render thread on core %d", d->core);
    if (d->coreMhz > 0.0) {
        put(&o, " averaged %.0f MHz in the block", d->coreMhz);
    } else {
        put(&o, ", its clock not sampled");
    }
    if (d->clFirst >= 0 && d->clMedian > 0.0) {
        put(&o, ", its cluster ");
        cores_words(&o, d->clFirst, d->clLast);
        put(&o, " at %.0f before", d->clMedian);
        if (d->coreMhz > 0.0) {
            put(&o, " (%.0f %%)", 100.0 * d->coreMhz / d->clMedian);
        }
    }
    return o.total;
}
