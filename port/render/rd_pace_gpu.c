/* rd_pace_gpu.c: the GPU queue's model and its 10 s sums (rd_pace_gpu.h).
 *
 * Pure: no clock read, no lock.  The model is the poster's thread's alone
 * (rd_pace_gpu_live.c feeds it); the sums are atomics the window's thread
 * takes for the evenness line (rd_even.c rd_even_line).
 *
 * Bounds, from the code:
 *   - with an empty queue at every picture (q 0) the shift is exactly 0;
 *     with no further picture among the last RD_PACE_GPU_FURTHER_SPAN the
 *     shift is 0 too, unless the ratio hold moves it, which it cannot in a
 *     run of such pictures (the moment before was its hand-over: the step
 *     is the gap itself, inside [0.5, 2] of it);
 *   - |shift| <= the bound B = max(p90 - mean, mean - p10) of the window of
 *     measured hand-over to end times, unless the ratio hold moves it;
 *   - between two pictures of a chain the moment moves by RD_PACE_GPU_RATIO_LO
 *     to RD_PACE_GPU_RATIO_HI times the hand-overs' gap (> 0): never back,
 *     never the same moment;
 *   - over a run of placed pictures with neither the bound nor the ratio
 *     in force, the shifts sum to RD_PACE_GPU_MEAN_DIV * (mean q at the end
 *     - mean q at the start) (each shift is q - mean, and the mean moves by
 *     a sixteenth of it): the mean moment over a block of N such pictures is
 *     the hand-overs' within 16 * (the largest q - the smallest) / N. */
#include "rd_pace_gpu.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- model */

double rd_pace_gpu_share(const double *v, uint32_t n, double p)
{
    if (n == 0u) {
        return 0.0;
    }
    if (n > RD_PACE_GPU_LAT) {
        n = RD_PACE_GPU_LAT;
    }
    double s[RD_PACE_GPU_LAT];
    memcpy(s, v, (size_t)n * sizeof(double));
    for (uint32_t i = 1; i < n; i++) {
        const double x = s[i];
        uint32_t j = i;
        while (j > 0u && s[j - 1u] > x) {
            s[j] = s[j - 1u];
            j--;
        }
        s[j] = x;
    }
    uint32_t rank = (uint32_t)ceil(p * (double)n);
    rank = rank < 1u ? 1u : (rank > n ? n : rank);
    return s[rank - 1u];
}

double rd_pace_gpu_median(const double *v, uint32_t n)
{
    return rd_pace_gpu_share(v, n, 0.5);
}

static void push(double *v, uint32_t cap, uint32_t *n, uint32_t *at, double x)
{
    v[*at] = x;
    *at = (*at + 1u) % cap;
    if (*n < cap) {
        (*n)++;
    }
}

void rd_pace_gpu_model_reset(RdPaceGpu *m)
{
    memset(m, 0, sizeof(*m));
}

/* the i-th oldest picture of the ring */
static RdPaceGpuPic *pic(RdPaceGpu *m, uint32_t i)
{
    return &m->ring[(m->ringAt + RD_PACE_GPU_RING - m->ringN + i) % RD_PACE_GPU_RING];
}

/* the GPU's start and end of a picture handed over at hand after one that
   ended at prevEnd (0: none known), with the lead D and the span G */
static double endOf(double hand, double prevEnd, double d, double g, double *q)
{
    const double base = hand + d;
    const double start = prevEnd > base ? prevEnd : base;
    *q = start - base;
    return start + g;
}

/* the pictures after the i-th predicted again from the i-th's end (the
   measured ones keep theirs) */
static void reprop(RdPaceGpu *m, uint32_t i)
{
    if (m->spanN == 0u || m->leadN == 0u) {
        return;
    }
    const double g = rd_pace_gpu_median(m->span, m->spanN);
    const double d = rd_pace_gpu_median(m->lead, m->leadN);
    for (uint32_t j = i + 1u; j < m->ringN; j++) {
        RdPaceGpuPic *p = pic(m, j);
        if (!p->measured) {
            double q;
            p->end = endOf(p->hand, pic(m, j - 1u)->end, d, g, &q);
        }
    }
}

void rd_pace_gpu_model_note(RdPaceGpu *m, const RdPaceGpuSample *s, RdPaceGpuNoted *out)
{
    RdPaceGpuNoted nd = {false, false, 0.0};
    if (s->handMs > 0.0) {
        if (s->submitMs > s->handMs) {
            push(m->lead, RD_PACE_GPU_MED, &m->leadN, &m->leadAt, s->submitMs - s->handMs);
        }
        if (s->spanMs > 0.0) {
            push(m->span, RD_PACE_GPU_MED, &m->spanN, &m->spanAt, s->spanMs);
        }
        uint32_t i = 0;
        while (i < m->ringN && pic(m, i)->hand != s->handMs) {
            i++;
        }
        if (i < m->ringN) {
            /* a picture of the ring: its end measured (stamped), or from its
               first submit and span after the picture before's end, under
               its bound; the pictures after it predicted again */
            RdPaceGpuPic *p = pic(m, i);
            nd.matched = true;
            double end = p->end;
            if (s->exact && s->endMs > 0.0) {
                if (p->predEnd > 0.0) {
                    nd.errValid = true;
                    nd.errMs = p->predEnd - s->endMs;
                }
                end = s->endMs;
            } else {
                if (s->submitMs > 0.0 && s->spanMs > 0.0) {
                    const double prev = i > 0u ? pic(m, i - 1u)->end : 0.0;
                    end = (prev > s->submitMs ? prev : s->submitMs) + s->spanMs;
                }
                if (s->endMs > 0.0 && (end > s->endMs || end <= 0.0)) {
                    end = s->endMs; /* the bound: the GPU had ended by then */
                }
            }
            p->end = end;
            p->measured = true;
            if (end > s->handMs && (s->exact || (s->submitMs > 0.0 && s->spanMs > 0.0))) {
                push(m->lat, RD_PACE_GPU_LAT, &m->latN, &m->latAt, end - s->handMs);
            }
            reprop(m, i);
        } else if (s->exact && s->endMs >= s->handMs) {
            push(m->lat, RD_PACE_GPU_LAT, &m->latN, &m->latAt, s->endMs - s->handMs);
        }
    }
    if (out != NULL) {
        *out = nd;
    }
}

void rd_pace_gpu_model_place(RdPaceGpu *m, double handMs, uint32_t frame, RdPaceGpuPlace *out)
{
    memset(out, 0, sizeof(*out));
    if (m->have) {
        const int64_t df = (int64_t)frame - (int64_t)m->frame;
        const double gap = handMs - m->hand;
        if (df < 0 || df > RD_PACE_GPU_FRAME_JUMP || !(gap > 0.0) ||
            gap > RD_PACE_GPU_GAP_RESET_MS) {
            rd_pace_gpu_model_reset(m);
            out->restarted = true;
        }
    }
    const bool further = m->have && frame == m->frame;
    m->furtherBits =
        ((m->furtherBits << 1) | (further ? 1u : 0u)) & ((1u << RD_PACE_GPU_FURTHER_SPAN) - 1u);

    double raw = 0.0, q = 0.0, e = 0.0;
    if (m->spanN != 0u && m->leadN != 0u) {
        const double g = rd_pace_gpu_median(m->span, m->spanN);
        const double d = rd_pace_gpu_median(m->lead, m->leadN);
        e = endOf(handMs, m->ringN != 0u ? pic(m, m->ringN - 1u)->end : 0.0, d, g, &q);
        if (m->qN == 0u) {
            m->meanQ = q;
        } else {
            raw = q - m->meanQ;
            m->meanQ += raw / RD_PACE_GPU_MEAN_DIV;
        }
        m->qN++;
    }
    m->ring[m->ringAt] = (RdPaceGpuPic){handMs, e, e, false};
    m->ringAt = (m->ringAt + 1u) % RD_PACE_GPU_RING;
    if (m->ringN < RD_PACE_GPU_RING) {
        m->ringN++;
    }

    double shift = 0.0;
    out->rawMs = raw;
    out->queueMs = q;
    out->predEndMs = e;
    if (m->spanN < RD_PACE_GPU_MED || m->leadN < RD_PACE_GPU_MED || m->latN < RD_PACE_GPU_LAT_MIN) {
        out->how = RD_PACE_GPU_NOT_READY;
    } else if (m->furtherBits == 0u) {
        out->how = RD_PACE_GPU_ONE_A_TICK;
    } else {
        double sum = 0.0;
        for (uint32_t i = 0; i < m->latN; i++) {
            sum += m->lat[i];
        }
        const double mean = sum / (double)m->latN;
        const double p10 = rd_pace_gpu_share(m->lat, m->latN, 0.1);
        const double p90 = rd_pace_gpu_share(m->lat, m->latN, 0.9);
        double b = p90 - mean > mean - p10 ? p90 - mean : mean - p10;
        b = b > 0.0 ? b : 0.0;
        shift = raw > b ? b : (raw < -b ? -b : raw);
        out->atBound = fabs(raw) > b;
        out->boundMs = b;
        out->latP10Ms = p10;
        out->latP90Ms = p90;
        out->how = RD_PACE_GPU_PLACED;
    }
    if (m->have) {
        /* the step from the moment before within [LO, HI] times the gap */
        const double gap = handMs - m->hand;
        const double lo = m->content + RD_PACE_GPU_RATIO_LO * gap - handMs;
        const double hi = m->content + RD_PACE_GPU_RATIO_HI * gap - handMs;
        if (shift < lo) {
            shift = lo;
            out->limited = true;
        } else if (shift > hi) {
            shift = hi;
            out->limited = true;
        }
    }
    m->have = true;
    m->frame = frame;
    m->hand = handMs;
    m->content = handMs + shift;
    out->shiftMs = shift;
}

/* ----------------------------------------------------------------- sums */

static RdPaceGpuWindow s_sum;

static void add(uint64_t *at, uint64_t v)
{
    if (v) {
        __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
    }
}

static uint32_t usOf(double ms)
{
    const double u = floor(ms * 1000.0 + 0.5);
    return u <= 0.0 ? 0u : (u >= 4.0e9 ? 4000000000u : (uint32_t)u);
}

void rd_pace_gpu_tally(const RdPaceGpuPlace *p)
{
    if (p->restarted) {
        add(&s_sum.restarts, 1u);
    }
    if (p->limited) {
        add(&s_sum.limited, 1u);
    }
    if (p->how == RD_PACE_GPU_ONE_A_TICK) {
        add(&s_sum.oneATick, 1u);
        return;
    }
    if (p->how == RD_PACE_GPU_NOT_READY) {
        add(&s_sum.notReady, 1u);
        return;
    }
    add(&s_sum.placed, 1u);
    if (p->atBound) {
        add(&s_sum.atBound, 1u);
    }
    const double b = floor((p->shiftMs - RD_PACE_GPU_SHIFT_FROM_MS) / RD_PACE_GPU_SHIFT_BIN_MS);
    const int bin =
        b <= 0.0
            ? 0
            : (b >= (double)(RD_PACE_GPU_SHIFT_BINS - 1) ? RD_PACE_GPU_SHIFT_BINS - 1 : (int)b);
    __atomic_add_fetch(&s_sum.shift[bin], 1u, __ATOMIC_RELAXED);
    add(&s_sum.queueUs, usOf(p->queueMs));
    __atomic_store_n(&s_sum.boundUs, usOf(p->boundMs), __ATOMIC_RELAXED);
    __atomic_store_n(&s_sum.latP10Us, usOf(p->latP10Ms), __ATOMIC_RELAXED);
    __atomic_store_n(&s_sum.latP90Us, usOf(p->latP90Ms), __ATOMIC_RELAXED);
}

void rd_pace_gpu_tally_err(double errMs)
{
    const double u = floor(errMs * 1000.0 + 0.5);
    const int64_t us = u > 1.0e9 ? 1000000000 : (u < -1.0e9 ? -1000000000 : (int64_t)u);
    add(&s_sum.errN, 1u);
    if (us != 0) {
        __atomic_add_fetch(&s_sum.errSumUs, us, __ATOMIC_RELAXED);
        add(&s_sum.errSum2, (uint64_t)(us * us));
    }
}

void rd_pace_gpu_tally_drop(void)
{
    add(&s_sum.dropped, 1u);
}

static uint64_t take(uint64_t *at)
{
    return __atomic_exchange_n(at, 0u, __ATOMIC_RELAXED);
}

void rd_pace_gpu_take(RdPaceGpuWindow *w)
{
    w->placed = take(&s_sum.placed);
    w->oneATick = take(&s_sum.oneATick);
    w->notReady = take(&s_sum.notReady);
    w->restarts = take(&s_sum.restarts);
    w->atBound = take(&s_sum.atBound);
    w->limited = take(&s_sum.limited);
    w->dropped = take(&s_sum.dropped);
    for (int b = 0; b < RD_PACE_GPU_SHIFT_BINS; b++) {
        w->shift[b] = __atomic_exchange_n(&s_sum.shift[b], 0u, __ATOMIC_RELAXED);
    }
    w->queueUs = take(&s_sum.queueUs);
    w->errN = take(&s_sum.errN);
    w->errSumUs = __atomic_exchange_n(&s_sum.errSumUs, 0, __ATOMIC_RELAXED);
    w->errSum2 = take(&s_sum.errSum2);
    /* the last values stay for the next block's line */
    w->boundUs = __atomic_load_n(&s_sum.boundUs, __ATOMIC_RELAXED);
    w->latP10Us = __atomic_load_n(&s_sum.latP10Us, __ATOMIC_RELAXED);
    w->latP90Us = __atomic_load_n(&s_sum.latP90Us, __ATOMIC_RELAXED);
}

/* ---------------------------------------------------------------- lines */

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

/* the p-th share's bin middle of the shift histogram, ms */
static double shiftShare(const uint32_t *h, uint64_t n, double p)
{
    uint64_t rank = (uint64_t)ceil(p * (double)n), c = 0;
    rank = rank < 1u ? 1u : rank;
    for (int b = 0; b < RD_PACE_GPU_SHIFT_BINS; b++) {
        c += h[b];
        if (c >= rank) {
            return ((double)b + 0.5) * RD_PACE_GPU_SHIFT_BIN_MS + RD_PACE_GPU_SHIFT_FROM_MS;
        }
    }
    return ((double)RD_PACE_GPU_SHIFT_BINS - 0.5) * RD_PACE_GPU_SHIFT_BIN_MS +
           RD_PACE_GPU_SHIFT_FROM_MS;
}

bool rd_pace_gpu_format(const RdPaceGpuWindow *w, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (w->placed + w->oneATick + w->notReady == 0u) {
        return false;
    }
    Out o = {out, size, 0};
    put(&o,
        "; the GPU's predicted end: %llu pictures placed by it (%llu with one a tick, %llu before "
        "its measurements, %llu restarts)",
        (unsigned long long)w->placed, (unsigned long long)w->oneATick,
        (unsigned long long)w->notReady, (unsigned long long)w->restarts);
    if (w->placed) {
        put(&o,
            ", the shift 10th %.2f, median %.2f, 90th %.2f ms (the queue's wait %.2f ms on "
            "average), %llu at the bound (the last %.2f ms, from the hand-over to the measured "
            "end 10th %.2f, 90th %.2f ms)",
            shiftShare(w->shift, w->placed, 0.1), shiftShare(w->shift, w->placed, 0.5),
            shiftShare(w->shift, w->placed, 0.9), (double)w->queueUs / 1000.0 / (double)w->placed,
            (unsigned long long)w->atBound, (double)w->boundUs / 1000.0,
            (double)w->latP10Us / 1000.0, (double)w->latP90Us / 1000.0);
    }
    put(&o, ", %llu held to %.1f-%.1f times the hand-overs' gap", (unsigned long long)w->limited,
        RD_PACE_GPU_RATIO_LO, RD_PACE_GPU_RATIO_HI);
    if (w->errN) {
        const double m = (double)w->errSumUs / (double)w->errN;
        const double v = (double)w->errSum2 / (double)w->errN - m * m;
        put(&o,
            "; predicted against the measured end over %llu pictures: mean %.2f ms "
            "(deviation %.2f)",
            (unsigned long long)w->errN, m / 1000.0, v > 0.0 ? sqrt(v) / 1000.0 : 0.0);
    } else {
        put(&o, "; predicted against the measured end: no exact end");
    }
    if (w->dropped) {
        put(&o, " (%llu measurements dropped)", (unsigned long long)w->dropped);
    }
    return true;
}

size_t rd_pace_gpu_start_text(bool on, const char *why, char *out, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    Out o = {out, size, 0};
    put(&o, "rd: in-between pictures for the GPU's predicted end: %s (%s)",
        on ? "on" : "not applicable here", why != NULL ? why : "");
    if (on) {
        put(&o,
            "; the GPU per picture the median of the last %d measured spans, the hand-over to "
            "the first submit the median of the last %d, the queue's wait from the hand-overs; "
            "the shift the wait less its running mean, held to the measured spread of the "
            "hand-over to the GPU's end over the last %d measured ends and to %.1f-%.1f times the "
            "hand-overs' gap; one picture a tick or an empty queue keep the hand-over's moment",
            RD_PACE_GPU_MED, RD_PACE_GPU_MED, RD_PACE_GPU_LAT, RD_PACE_GPU_RATIO_LO,
            RD_PACE_GPU_RATIO_HI);
    }
    return o.at < size ? o.at : size - 1;
}
