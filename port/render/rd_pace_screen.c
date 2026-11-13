/* rd_pace_screen.c: the screen's model, its ring from the collecting thread,
 * its 10 s sums and texts (rd_pace_screen.h).
 *
 * Pure but for the ring's and the sums' atomics: no clock read, no lock.
 * The model is the poster's thread's alone (rd_pace_screen_live.c feeds it).
 *
 * Bounds, from the code:
 *   - placed, the value is in [0, RD_PS_TOP] of the tick (held at an edge
 *     otherwise, counted) and the moment never below the one before in the
 *     chain (held, counted): never back;
 *   - the first placed picture of a chain is drawn at the value drawn
 *     otherwise to the bit (the lag starts there), so the rule taking over
 *     makes no jump; after it the lag moves at most RD_PS_SLEW_MS a picture,
 *     so beside the glass's own steps the content's step differs from the
 *     predicted time on the glass's step by at most that;
 *   - not placed, the value is the one drawn otherwise to the bit;
 *   - the expected time on the glass is non-decreasing in the predicted end
 *     (an expectation of the first refresh at or after it), and the
 *     predicted ends of the queue model are increasing, so two pictures
 *     predicted on one refresh get the same time there: the earlier, which
 *     mailbox replaces, takes no content time from the next. */
#include "rd_pace_screen.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ the parts */

static void sortD(double *v, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        const double x = v[i];
        uint32_t j = i;
        while (j > 0u && v[j - 1u] > x) {
            v[j] = v[j - 1u];
            j--;
        }
        v[j] = x;
    }
}

static double phi(double z)
{
    return 0.5 * erfc(-z / sqrt(2.0));
}

double rd_pace_screen_expect(double t, double a, double p, double w, double *hard)
{
    const double k = ceil((t - a) / p);
    const double h = a + k * p;
    *hard = h;
    if (!(w > 0.0)) {
        return h;
    }
    /* shown at refresh h + j p when the true t is in (h + (j-1) p, h + j p] */
    double sum = 0.0, wsum = 0.0;
    for (int j = -3; j <= 3; j++) {
        const double hi = h + (double)j * p - t, lo = hi - p;
        const double pr = phi(hi / w) - phi(lo / w);
        sum += pr;
        wsum += pr * (double)j * p;
    }
    return sum > 0.0 ? h + wsum / sum : h;
}

double rd_pace_screen_latch(const double *d, uint32_t n, double p, double prev, uint32_t *hits)
{
    *hits = 0;
    if (n == 0u) {
        return NAN;
    }
    if (n > RD_PS_WIN) {
        n = RD_PS_WIN;
    }
    double s[RD_PS_WIN], f[RD_PS_WIN];
    for (uint32_t i = 0; i < n; i++) {
        s[i] = d[i] - p;
        f[i] = d[i];
    }
    sortD(s, n);
    sortD(f, n);
    /* the coverage of x: #{s < x} - #{f < x}; its maximum is at a right end
       f[j], over (max(the last s below it, the last f below it), f[j]] */
    uint32_t best = 0, cs = 0, cf = 0;
    double mid = NAN;
    for (uint32_t j = 0; j < n; j++) {
        const double x = f[j];
        if (j > 0u && f[j - 1u] == x) {
            continue;
        }
        while (cs < n && s[cs] < x) {
            cs++;
        }
        while (cf < n && f[cf] < x) {
            cf++;
        }
        const uint32_t c = cs - cf;
        if (c == 0u) {
            continue;
        }
        double lo = s[cs - 1u];
        if (cf > 0u && f[cf - 1u] > lo) {
            lo = f[cf - 1u];
        }
        const double m = 0.5 * (lo + x);
        if (c > best || (c == best && !isnan(prev) && fabs(m - prev) < fabs(mid - prev))) {
            best = c;
            mid = m;
        }
    }
    *hits = best;
    return mid;
}

uint32_t rd_pace_screen_cover(const double *d, uint32_t n, double p, double x)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        c += d[i] - p < x && x <= d[i] ? 1u : 0u;
    }
    return c;
}

double rd_pace_screen_expect_measured(double t, double a, double p, const double *err, uint32_t n,
                                      double *hard)
{
    double k[RD_PS_WIN];
    n = n > RD_PS_WIN ? RD_PS_WIN : n;
    if (n == 0u) {
        return rd_pace_screen_expect(t, a, p, 0.0, hard);
    }
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        k[i] = ceil((t + err[i] - a) / p);
        sum += k[i];
    }
    sortD(k, n);
    /* the most frequent refresh; ties the earliest (sorted: the first run) */
    double best = k[0];
    uint32_t bestN = 0, run = 0;
    for (uint32_t i = 0; i < n; i++) {
        run = i > 0u && k[i] == k[i - 1u] ? run + 1u : 1u;
        if (run > bestN) {
            bestN = run;
            best = k[i];
        }
    }
    *hard = a + best * p;
    return a + (sum / (double)n) * p;
}

int rd_pace_screen_score(double predictedMs, double actualMs, double refreshMs)
{
    if (!(refreshMs > 0.0)) {
        return 0;
    }
    return (int)floor((actualMs - predictedMs) / refreshMs + 0.5);
}

/* ------------------------------------------------------------ the model */

void rd_pace_screen_reset(RdPaceScreen *m)
{
    memset(m, 0, sizeof(*m));
    m->opts = RD_PS_OPT_ALL;
}

void rd_pace_screen_add(RdPaceScreen *m, const RdPaceScreenSample *s)
{
    if (!(s->endPredMs > 0.0) || !(s->actualMs > 0.0)) {
        return;
    }
    m->s[m->sAt] = *s;
    m->sAt = (m->sAt + 1u) % RD_PS_WIN;
    if (m->sN < RD_PS_WIN) {
        m->sN++;
    }
    m->newestEndMs = s->endPredMs;
    m->dirty = true;
}

/* the i-th oldest sample */
static const RdPaceScreenSample *smp(const RdPaceScreen *m, uint32_t i)
{
    return &m->s[(m->sAt + RD_PS_WIN - m->sN + i) % RD_PS_WIN];
}

void rd_pace_screen_fit(RdPaceScreen *m)
{
    RdPaceScreenFit *f = &m->fit;
    const double prevX = f->latch ? f->xMs : NAN;
    const double prevLead = f->lead ? f->leadMs : NAN;
    memset(f, 0, sizeof(*f));
    m->dirty = false;
    const uint32_t n = m->sN;
    if (n < 2u) {
        return;
    }
    const RdPaceScreenSample *newest = smp(m, n - 1u);
    const double rep = newest->refreshMs;
    f->reportedMs = rep;
    if (!(rep > 0.0)) {
        return;
    }
    /* each time's whole refreshes from the newest, gap by gap by the
       reported refresh (a gap of a few refreshes rounds right even where the
       reported refresh is off the true one by a little: 8.35 against
       8.333 ms is a refresh wrong only after 250 of them); the period the
       least-squares slope of the times over them */
    const double a0 = newest->actualMs;
    double k[RD_PS_WIN], r[RD_PS_WIN];
    k[n - 1u] = 0.0;
    r[n - 1u] = 0.0;
    for (uint32_t i = n - 1u; i-- > 0u;) {
        r[i] = smp(m, i)->actualMs - a0;
        k[i] = k[i + 1u] - floor((r[i + 1u] - r[i]) / rep + 0.5);
    }
    double kb = 0.0, rb = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        kb += k[i];
        rb += r[i];
    }
    kb /= (double)n;
    rb /= (double)n;
    double sxx = 0.0, sxy = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        sxx += (k[i] - kb) * (k[i] - kb);
        sxy += (k[i] - kb) * (r[i] - rb);
    }
    if (!(sxx > 0.0)) {
        return;
    }
    const double p = sxy / sxx;
    const double base = rb - p * kb; /* the fitted grid's refresh at k 0, from a0 */
    f->periodMs = p;
    f->anchorMs = a0 + base;
    for (uint32_t i = 0; i < n; i++) {
        if (fabs(r[i] - (base + p * k[i])) > RD_PS_GRID_TOL * p) {
            f->offGrid++;
        }
    }
    if (!(p > 0.0) || fabs(p - rep) > RD_PS_PERIOD_TOL * rep ||
        f->offGrid * RD_PS_OFF_GRID_DIV > n) {
        return;
    }
    f->grid = true;
    /* the latch: each shown picture's refresh less its predicted end */
    double d[RD_PS_WIN], e[RD_PS_WIN];
    uint32_t ne = 0;
    for (uint32_t i = 0; i < n; i++) {
        const double at = f->anchorMs + p * k[i];
        d[i] = at - smp(m, i)->endPredMs;
        if (smp(m, i)->gpuEndMs > 0.0) {
            e[ne++] = at - smp(m, i)->gpuEndMs;
            f->err[f->errN++] = smp(m, i)->gpuEndMs - smp(m, i)->endPredMs;
        }
    }
    f->n = n;
    f->xMs = rd_pace_screen_latch(d, n, p, prevX, &f->hits);
    if ((m->opts & RD_PS_OPT_HYST) && !isnan(prevX)) {
        /* kept where it was unless another range agrees with RD_PS_HYST more */
        const uint32_t c = rd_pace_screen_cover(d, n, p, prevX);
        if (f->hits < c + RD_PS_HYST) {
            f->held = f->xMs != prevX;
            f->xMs = prevX;
            f->hits = c;
        }
    }
    f->latch = (double)f->hits >= RD_PS_FIT_MIN * (double)n;
    double w = sqrt(acos(-1.0) / 2.0) * p * (double)(n - f->hits) / (double)n;
    f->widthMs = w > RD_PS_WIDTH_MAX * p ? RD_PS_WIDTH_MAX * p : w;
    if (ne >= RD_PS_MIN / 4u) {
        f->leadN = ne;
        f->leadMs = rd_pace_screen_latch(e, ne, p, prevLead, &f->leadHits);
        if ((m->opts & RD_PS_OPT_HYST) && !isnan(prevLead)) {
            const uint32_t c = rd_pace_screen_cover(e, ne, p, prevLead);
            if (f->leadHits < c + RD_PS_HYST) {
                f->leadMs = prevLead;
                f->leadHits = c;
            }
        }
        f->lead = (double)f->leadHits >= RD_PS_FIT_MIN * (double)ne;
    }
    f->measured = (m->opts & RD_PS_OPT_MEASURED) && f->lead && f->errN >= RD_PS_MEAS_MIN;
}

bool rd_pace_screen_even_latch(RdPaceScreen *m, double nowMs, double *xMs)
{
    if (m->dirty) {
        rd_pace_screen_fit(m);
    }
    const RdPaceScreenFit *f = &m->fit;
    *xMs = 0.0;
    if (m->sN < RD_PS_MIN || !f->grid || nowMs - m->newestEndMs > RD_PS_STALE_MS) {
        return false;
    }
    if (f->lead && f->errN >= RD_PS_MEAS_MIN) {
        double v[RD_PS_WIN];
        const uint32_t n = f->errN > RD_PS_WIN ? RD_PS_WIN : f->errN;
        memcpy(v, f->err, n * sizeof v[0]);
        sortD(v, n);
        const double med = n % 2u ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
        *xMs = f->leadMs + med;
        return true;
    }
    if (f->latch) {
        *xMs = f->xMs;
        return true;
    }
    return false;
}

static void chainReset(RdPaceScreen *m)
{
    m->have = false;
    m->yN = 0;
    m->yAt = 0;
}

void rd_pace_screen_place(RdPaceScreen *m, const RdPaceScreenIn *in, RdPaceScreenPlace *out)
{
    memset(out, 0, sizeof(*out));
    out->raw = in->rawOther;
    if (m->have) {
        const int64_t df = (int64_t)in->frame - (int64_t)m->frame;
        const double gap = in->handMs - m->hand;
        if (df < 0 || df > RD_PS_FRAME_JUMP || !(gap > 0.0) || gap > RD_PS_GAP_RESET_MS) {
            chainReset(m);
            out->restarted = true;
        }
    }
    if (m->dirty && !in->kept) {
        rd_pace_screen_fit(m);
    }
    const RdPaceScreenFit *f = &m->fit;
    if (in->kept) {
        out->how = RD_PS_KEPT;
    } else if (m->sN == 0u) {
        out->how = RD_PS_NO_TIMES;
    } else if (!(in->endPredMs > 0.0)) {
        out->how = RD_PS_NO_END;
    } else if (m->sN < RD_PS_MIN) {
        out->how = RD_PS_FEW;
    } else if (in->handMs - m->newestEndMs > RD_PS_STALE_MS) {
        out->how = RD_PS_STALE;
    } else if (!f->grid) {
        out->how = RD_PS_OFF_GRID;
    } else if (!f->latch) {
        out->how = RD_PS_NO_FIT;
    } else if (!(in->tickMs > 0.0)) {
        out->how = RD_PS_NO_END;
    } else {
        out->how = RD_PS_PLACED;
    }
    if (out->how != RD_PS_PLACED) {
        /* the moment drawn otherwise; the next placed picture starts a
           chain there */
        chainReset(m);
        return;
    }
    const double tick = in->tickMs;
    double hard, g;
    if (in->targetMs > 0.0) {
        /* the even cadence's refresh (rd_pace_even.h): aimed at the middle
           of its window, so the refresh itself */
        hard = g = in->targetMs;
    } else if (f->measured) {
        g = rd_pace_screen_expect_measured(in->endPredMs + f->leadMs, f->anchorMs, f->periodMs,
                                           f->err, f->errN, &hard);
        out->measured = true;
    } else {
        g = rd_pace_screen_expect(in->endPredMs + f->xMs, f->anchorMs, f->periodMs, f->widthMs,
                                  &hard);
    }
    const double y = g - in->closeAtMs;
    m->y[m->yAt] = y;
    m->yAt = (m->yAt + 1u) % RD_PS_LAG_WIN;
    if (m->yN < RD_PS_LAG_WIN) {
        m->yN++;
    }
    if (!m->have) {
        m->lag = y - in->rawOther * tick; /* drawn where the moment was */
    } else if (m->yN >= RD_PS_LAG_MIN) {
        double ymin = m->y[0], ymax = m->y[0];
        for (uint32_t i = 1; i < m->yN; i++) {
            ymin = m->y[i] < ymin ? m->y[i] : ymin;
            ymax = m->y[i] > ymax ? m->y[i] : ymax;
        }
        const double lo = ymax - RD_PS_TOP * tick, hi = ymin;
        const double target = lo <= hi ? 0.5 * (lo + hi) : hi;
        double step = target - m->lag;
        step =
            step > RD_PS_SLEW_MS ? RD_PS_SLEW_MS : (step < -RD_PS_SLEW_MS ? -RD_PS_SLEW_MS : step);
        m->lag += step;
    }
    /* the chain's first picture exactly where the moment was */
    double raw = m->have ? (y - m->lag) / tick : in->rawOther;
    if (raw < 0.0) {
        raw = 0.0;
        out->heldLo = true;
    } else if (raw > RD_PS_TOP) {
        raw = RD_PS_TOP;
        out->heldHi = true;
    }
    double c = in->closeAtMs + raw * tick;
    if (m->have && c < m->content) {
        c = m->content;
        raw = (c - in->closeAtMs) / tick;
        raw = raw > RD_PS_TOP ? RD_PS_TOP : raw;
        out->heldBack = true;
    }
    out->replaced = m->have && fabs(hard - m->lastHardMs) < 0.5 * f->periodMs;
    m->have = true;
    m->frame = in->frame;
    m->hand = in->handMs;
    m->content = c;
    m->lastHardMs = hard;
    out->raw = raw;
    out->hardMs = hard;
    out->glassMs = g;
    out->lagMs = m->lag;
}

/* ------------------------------------------------------------- the ring */

#define QCAP 128u

static RdPaceScreenSample s_q[QCAP];
static uint32_t s_head, s_tail; /* __atomic: the producer's, the consumer's */

void rd_pace_screen_seen(const RdPaceScreenSample *s)
{
    if (!(s->endPredMs > 0.0)) {
        return;
    }
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= QCAP) {
        rd_pace_screen_tally_drop();
        return;
    }
    s_q[head % QCAP] = *s;
    __atomic_store_n(&s_head, head + 1u, __ATOMIC_RELEASE);
}

uint32_t rd_pace_screen_drain(RdPaceScreen *m)
{
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t n = 0;
    while (tail != head) {
        const RdPaceScreenSample smp1 = s_q[tail % QCAP];
        tail++;
        rd_pace_screen_add(m, &smp1);
        n++;
    }
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    return n;
}

void rd_pace_screen_drop(void)
{
    __atomic_store_n(&s_tail, __atomic_load_n(&s_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

/* ----------------------------------------------------------------- sums */

static RdPaceScreenWindow s_sum;

static void add(uint64_t *at, bool v)
{
    if (v) {
        __atomic_add_fetch(at, 1u, __ATOMIC_RELAXED);
    }
}

static int32_t usOf(double ms)
{
    const double u = floor(ms * 1000.0 + 0.5);
    return u > 2.0e9 ? 2000000000 : (u < -2.0e9 ? -2000000000 : (int32_t)u);
}

static uint32_t nsOf(double ms)
{
    const double u = floor(ms * 1.0e6 + 0.5);
    return u <= 0.0 ? 0u : (u >= 4.0e9 ? 4000000000u : (uint32_t)u);
}

static void put32(uint32_t *at, uint32_t v)
{
    __atomic_store_n(at, v, __ATOMIC_RELAXED);
}

static void puti(int32_t *at, int32_t v)
{
    __atomic_store_n(at, v, __ATOMIC_RELAXED);
}

void rd_pace_screen_tally(const RdPaceScreenPlace *p, const RdPaceScreenFit *f)
{
    if (p->how >= 0 && p->how < RD_PS_HOW_COUNT) {
        add(&s_sum.how[p->how], true);
    }
    add(&s_sum.restarts, p->restarted);
    add(&s_sum.heldLo, p->heldLo);
    add(&s_sum.heldHi, p->heldHi);
    add(&s_sum.heldBack, p->heldBack);
    add(&s_sum.replaced, p->replaced);
    add(&s_sum.measured, p->measured);
    put32(&s_sum.grid, f->grid ? 1u : 0u);
    put32(&s_sum.latch, f->latch ? 1u : 0u);
    put32(&s_sum.lead, f->lead ? 1u : 0u);
    put32(&s_sum.periodNs, nsOf(f->periodMs));
    put32(&s_sum.reportedNs, nsOf(f->reportedMs));
    put32(&s_sum.hits, f->hits);
    put32(&s_sum.n, f->n);
    put32(&s_sum.leadHits, f->leadHits);
    put32(&s_sum.leadN, f->leadN);
    put32(&s_sum.offGrid, f->offGrid);
    puti(&s_sum.xUs, usOf(f->latch ? f->xMs : 0.0));
    puti(&s_sum.widthUs, usOf(f->widthMs));
    puti(&s_sum.leadUs, usOf(f->lead ? f->leadMs : 0.0));
    if (p->how == RD_PS_PLACED) {
        puti(&s_sum.lagUs, usOf(p->lagMs));
    }
}

void rd_pace_screen_tally_drop(void)
{
    add(&s_sum.dropped, true);
}

static uint64_t take(uint64_t *at)
{
    return __atomic_exchange_n(at, 0u, __ATOMIC_RELAXED);
}

void rd_pace_screen_take(RdPaceScreenWindow *w)
{
    for (int i = 0; i < RD_PS_HOW_COUNT; i++) {
        w->how[i] = take(&s_sum.how[i]);
    }
    w->restarts = take(&s_sum.restarts);
    w->heldLo = take(&s_sum.heldLo);
    w->heldHi = take(&s_sum.heldHi);
    w->heldBack = take(&s_sum.heldBack);
    w->replaced = take(&s_sum.replaced);
    w->measured = take(&s_sum.measured);
    w->dropped = take(&s_sum.dropped);
    /* the last fit stays for the next block's line */
    w->grid = __atomic_load_n(&s_sum.grid, __ATOMIC_RELAXED);
    w->latch = __atomic_load_n(&s_sum.latch, __ATOMIC_RELAXED);
    w->lead = __atomic_load_n(&s_sum.lead, __ATOMIC_RELAXED);
    w->periodNs = __atomic_load_n(&s_sum.periodNs, __ATOMIC_RELAXED);
    w->reportedNs = __atomic_load_n(&s_sum.reportedNs, __ATOMIC_RELAXED);
    w->hits = __atomic_load_n(&s_sum.hits, __ATOMIC_RELAXED);
    w->n = __atomic_load_n(&s_sum.n, __ATOMIC_RELAXED);
    w->leadHits = __atomic_load_n(&s_sum.leadHits, __ATOMIC_RELAXED);
    w->leadN = __atomic_load_n(&s_sum.leadN, __ATOMIC_RELAXED);
    w->offGrid = __atomic_load_n(&s_sum.offGrid, __ATOMIC_RELAXED);
    w->xUs = __atomic_load_n(&s_sum.xUs, __ATOMIC_RELAXED);
    w->widthUs = __atomic_load_n(&s_sum.widthUs, __ATOMIC_RELAXED);
    w->leadUs = __atomic_load_n(&s_sum.leadUs, __ATOMIC_RELAXED);
    w->lagUs = __atomic_load_n(&s_sum.lagUs, __ATOMIC_RELAXED);
}

/* ---------------------------------------------------------------- texts */

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
    const int wr = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (wr > 0) {
        o->at += (size_t)wr;
    }
}

const char *rd_pace_screen_how_text(int how)
{
    switch (how) {
    case RD_PS_PLACED:
        return "placed";
    case RD_PS_NO_TIMES:
        return "no display time back";
    case RD_PS_NO_END:
        return "no predicted GPU end";
    case RD_PS_FEW:
        return "too few shown pictures yet";
    case RD_PS_STALE:
        return "the display's times stale";
    case RD_PS_OFF_GRID:
        return "the display's times off one refresh grid";
    case RD_PS_NO_FIT:
        return "the latch not predictable";
    case RD_PS_KEPT:
        return "a kept frame (a fade or a load, its GPU work not modelled)";
    default:
        return "?";
    }
}

bool rd_pace_screen_format(const RdPaceScreenWindow *w, uint64_t scored, uint64_t hit,
                           uint64_t late, uint64_t early, uint64_t further, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    uint64_t all = 0;
    for (int i = 0; i < RD_PS_HOW_COUNT; i++) {
        all += w->how[i];
    }
    if (all == 0u && scored == 0u) {
        return false;
    }
    Out o = {out, size, 0};
    put(&o, "; the screen's predicted refresh: %llu pictures placed by it, %llu drawn as otherwise",
        (unsigned long long)w->how[RD_PS_PLACED], (unsigned long long)(all - w->how[RD_PS_PLACED]));
    if (all != w->how[RD_PS_PLACED]) {
        put(&o, " (");
        int first = 1;
        for (int i = 1; i < RD_PS_HOW_COUNT; i++) {
            if (w->how[i]) {
                put(&o, "%s%s %llu", first ? "" : ", ", rd_pace_screen_how_text(i),
                    (unsigned long long)w->how[i]);
                first = 0;
            }
        }
        put(&o, ")");
    }
    put(&o,
        ", %llu restarts; held at the frame's close %llu, at the newest frame %llu, at the "
        "moment before %llu; %llu on the refresh of the picture before (replaced before shown)",
        (unsigned long long)w->restarts, (unsigned long long)w->heldLo,
        (unsigned long long)w->heldHi, (unsigned long long)w->heldBack,
        (unsigned long long)w->replaced);
    if (w->measured) {
        put(&o, "; %llu by the exact ends' measured errors", (unsigned long long)w->measured);
    }
    if (scored) {
        put(&o,
            "; the refresh predicted against the one shown over %llu pictures: %llu on it "
            "(%.1f %%), %llu one late, %llu one early, %llu further",
            (unsigned long long)scored, (unsigned long long)hit,
            100.0 * (double)hit / (double)scored, (unsigned long long)late,
            (unsigned long long)early, (unsigned long long)further);
    } else {
        put(&o, "; no shown picture with a prediction");
    }
    if (w->reportedNs) {
        put(&o, "; the display's grid: ");
        if (w->grid) {
            put(&o, "a refresh of %.4f ms measured (%.4f reported), %u of %u times off it",
                (double)w->periodNs / 1e6, (double)w->reportedNs / 1e6, w->offGrid, w->n);
        } else {
            put(&o, "not one grid (%u times off it, %.4f ms reported)", w->offGrid,
                (double)w->reportedNs / 1e6);
        }
    }
    if (w->grid) {
        if (w->latch) {
            put(&o,
                "; the latch: shown at the first refresh at or after the predicted GPU end + "
                "%.2f ms (%u of %u agree, the prediction's spread %.2f ms)",
                (double)w->xUs / 1000.0, w->hits, w->n, (double)w->widthUs / 1000.0);
        } else {
            put(&o, "; the latch not fitted (%u of %u agree)", w->hits, w->n);
        }
        if (w->lead) {
            put(&o, ", after the exact GPU end + %.2f ms (%u of %u)", (double)w->leadUs / 1000.0,
                w->leadHits, w->leadN);
        }
        if (w->how[RD_PS_PLACED]) {
            put(&o, "; the lag %.2f ms", (double)w->lagUs / 1000.0);
        }
    }
    if (w->dropped) {
        put(&o, " (%llu shown pictures dropped)", (unsigned long long)w->dropped);
    }
    return true;
}

size_t rd_pace_screen_start_text(bool measured, bool drawn, const char *why, char *out, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    Out o = {out, size, 0};
    put(&o, "rd: in-between pictures for the screen's refresh: %s (%s)",
        !measured ? "not applicable here" : (drawn ? "on" : "off by default"),
        why != NULL ? why : "");
    if (measured) {
        put(&o,
            "; each picture drawn for the time the screen will show it: its GPU end as "
            "predicted at the hand-over, plus the latch fitted over the last %d shown pictures' "
            "display times, rounded up to the display's measured refresh grid (each refresh at "
            "its probability under the fit's own spread); the lag to the content starts at the "
            "moment drawn otherwise and moves at most %.2f ms a picture to keep the last %d "
            "inside their frame's tick; never back; with no display times, stale ones, ones off "
            "one grid or a latch that fits under %.0f %%, the moment drawn otherwise%s",
            RD_PS_WIN, RD_PS_SLEW_MS, RD_PS_LAG_WIN, 100.0 * RD_PS_FIT_MIN,
            drawn ? "" : "; computed and reported, not drawn");
    }
    return o.at < size ? o.at : size - 1;
}
