/* rd_pace_even.c: the even cadence's plan, its 10 s sums and texts
 * (rd_pace_even.h).
 *
 * Pure but for the sums' atomics: no clock read (the caller's now), no lock.
 * The state is the poster's thread's alone (rd_pace_even_live.c).
 *
 * Bounds, from the code:
 *   - N is at least 1 and at most the game's tick in refreshes (RD_PE_N_MAX
 *     without a tick); it goes up at once when the picture time needs it,
 *     and down (straight to the N the picture time fits with RD_PE_DOWN_MS
 *     to spare) only after that has held for RD_PE_DOWN_HOLD_MS: a picture
 *     time between N - 1 refreshes less RD_PE_DOWN_MS and N refreshes less
 *     RD_PE_UP_MS keeps N (the hysteresis band, 0.75 ms wide);
 *   - consecutive pictures' refreshes are N refreshes apart unless the
 *     picture could not make it (late, counted), then a whole number more;
 *   - the hold is never negative and never more than the time to the aimed
 *     end less the picture's predicted latency (D + G). */
#include "rd_pace_even.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void rd_pace_even_reset(RdPaceEven *e)
{
    memset(e, 0, sizeof(*e));
}

uint32_t rd_pace_even_n_for(double t, double p, double m)
{
    if (!(p > 0.0)) {
        return 1u;
    }
    const double k = ceil((t + m) / p - 1e-9);
    return k < 1.0 ? 1u : (k > (double)RD_PE_N_MAX ? RD_PE_N_MAX : (uint32_t)k);
}

static uint32_t bits(uint32_t v)
{
    uint32_t c = 0;
    for (; v != 0u; v &= v - 1u) {
        c++;
    }
    return c;
}

/* the earliest end of a picture handed over now (rd_pace_gpu.c endOf: the
   frames in flight leave at most the picture before ahead of it) */
static double earliestEnd(const RdPaceEvenIn *in)
{
    const double base = in->nowMs + in->dMs;
    const double cap = base + in->gMs;
    const double before = in->prevEndMs > cap ? cap : in->prevEndMs;
    return (before > base ? before : base) + in->gMs;
}

void rd_pace_even_plan(RdPaceEven *e, const RdPaceEvenIn *in, RdPaceEvenPlan *out)
{
    memset(out, 0, sizeof(*out));
    out->n = e->n;
    if (!in->fit || !(in->periodMs > 0.0)) {
        out->how = RD_PE_NO_FIT;
        e->lastTargetMs = 0.0; /* aligned again when it can aim */
        e->planned = false;
        return;
    }
    if (!(in->gMs > 0.0) || !(in->dMs > 0.0)) {
        out->how = RD_PE_NO_GPU;
        e->lastTargetMs = 0.0;
        e->planned = false;
        return;
    }
    const double p = in->periodMs;
    e->periodMs = p;
    const double t = in->gMs > in->dMs ? in->gMs : in->dMs;
    uint32_t cap = RD_PE_N_MAX;
    if (in->tickMs > 0.0) {
        const double k = floor(in->tickMs / p + 0.5);
        cap = k < 1.0 ? 1u : (k > (double)RD_PE_N_MAX ? RD_PE_N_MAX : (uint32_t)k);
    }
    if (t + RD_PE_UP_MS > (double)cap * p) {
        /* slower than the game's tick: nothing to even out */
        out->how = RD_PE_SLOW;
        e->lastTargetMs = 0.0;
        e->planned = false;
        return;
    }
    if (fabs(t - e->tRefMs) > RD_PE_REGIME_MS) {
        e->biasMs = 0.0;
        e->missBits = 0;
        e->tRefMs = t;
    }
    const uint32_t up = rd_pace_even_n_for(t, p, RD_PE_UP_MS);
    const uint32_t n0 = e->n;
    if (e->n == 0u || up > e->n) {
        e->n = up;
        e->downSinceMs = 0.0;
    } else if (bits(e->missBits) >= RD_PE_MISS_UP && e->n < cap) {
        e->n++;
        e->missBits = 0;
        e->downSinceMs = 0.0;
        e->floorN = e->n;
        e->floorTMs = t;
    } else if (rd_pace_even_n_for(t, p, RD_PE_DOWN_MS) < e->n &&
               !(rd_pace_even_n_for(t, p, RD_PE_DOWN_MS) < e->floorN &&
                 fabs(t - e->floorTMs) < RD_PE_FLOOR_T_MS)) {
        if (!(e->downSinceMs > 0.0)) {
            e->downSinceMs = in->nowMs;
        } else if (in->nowMs - e->downSinceMs >= RD_PE_DOWN_HOLD_MS) {
            /* straight to the N the picture time fits with the margin (not
               through each N between) */
            e->n = rd_pace_even_n_for(t, p, RD_PE_DOWN_MS);
            e->downSinceMs = 0.0;
            e->missBits = 0;
        }
    } else {
        e->downSinceMs = 0.0;
    }
    e->n = e->n > cap ? cap : e->n;
    out->changed = n0 != 0u && e->n != n0;
    out->n = e->n;
    const double early = earliestEnd(in);
    double r;
    bool late = false;
    if (e->lastTargetMs > 0.0) {
        /* the one before's + N refreshes, on the grid as fitted now */
        r = in->anchorMs + floor((e->lastTargetMs + (double)e->n * p - in->anchorMs) / p + 0.5) * p;
        if (early + in->xMs > r + (double)(4u * e->n) * p) {
            /* far behind (a stall, a load): aligned again, not late */
            r = 0.0;
        } else {
            for (int i = 0; i < 64 && early + in->xMs > r - RD_PE_LATE_MS; i++) {
                r += p;
                late = true;
            }
        }
    } else {
        r = 0.0;
    }
    if (!(r > 0.0)) {
        /* the first refresh whose aim the earliest end makes */
        r = in->anchorMs +
            ceil((early + in->xMs + RD_PE_AIM * p + e->biasMs - in->anchorMs) / p) * p;
    }
    const double aim = r - in->xMs - RD_PE_AIM * p - e->biasMs;
    const double hand = aim - in->gMs - in->dMs;
    out->how = RD_PE_AIMED;
    double at = hand > in->nowMs ? hand : in->nowMs;
    if (in->stepEndMs > 0.0 && at > in->stepEndMs - 0.3 &&
        at - in->stepEndMs <= (1.0 - RD_PE_AIM) * p - RD_PE_LATE_MS &&
        in->stepEndMs - 0.3 > in->nowMs - 1e-9) {
        /* due inside the game's next step: before it, at most the aim's
           early side less RD_PE_LATE_MS early (shown at R still) */
        at = in->stepEndMs - 0.3 > in->nowMs ? in->stepEndMs - 0.3 : in->nowMs;
        out->early = true;
    }
    out->holdMs = at - in->nowMs;
    out->targetMs = r;
    out->endMs = early > aim ? early : aim;
    out->late = late;
    e->planned = true;
    e->planTargetMs = r;
}

double rd_pace_even_take(RdPaceEven *e, const RdPaceEvenIn *in, double endMs, bool kept, bool *late)
{
    *late = false;
    if (!e->planned) {
        return 0.0;
    }
    e->planned = false;
    double r = e->planTargetMs;
    const double p = in->periodMs;
    /* the plan's refresh was already a later one than N on: late */
    if (e->lastTargetMs > 0.0 && p > 0.0 && r - e->lastTargetMs > ((double)e->n + 0.5) * p) {
        *late = true;
    }
    if (!kept && endMs > 0.0 && p > 0.0) {
        /* handed over later than planned (the game step, the drain): the
           refresh it can make */
        for (int i = 0; i < 64 && endMs + in->xMs > r - RD_PE_LATE_MS; i++) {
            r += p;
            *late = true;
        }
    }
    e->missBits = ((e->missBits << 1) | (*late ? 1u : 0u)) &
                  (RD_PE_MISS_WIN >= 32u ? 0xFFFFFFFFu : ((1u << RD_PE_MISS_WIN) - 1u));
    e->lastTargetMs = r;
    return kept ? 0.0 : r;
}

void rd_pace_even_shown(RdPaceEven *e, int score)
{
    if (score != 0) {
        e->missBits |= 1u;
    }
    /* shown late: aim earlier; early: later; at most half a refresh */
    const double lim = 0.5 * e->periodMs;
    if (score == 0) {
        const double d =
            e->biasMs > RD_PE_BIAS_DECAY_MS
                ? RD_PE_BIAS_DECAY_MS
                : (e->biasMs < -RD_PE_BIAS_DECAY_MS ? -RD_PE_BIAS_DECAY_MS : e->biasMs);
        e->biasMs -= d;
    }
    e->biasMs += score > 0 ? RD_PE_BIAS_STEP_MS : (score < 0 ? -RD_PE_BIAS_STEP_MS : 0.0);
    e->biasMs = e->biasMs > lim ? lim : (e->biasMs < -lim ? -lim : e->biasMs);
}

/* ----------------------------------------------------------------- sums */

static RdPaceEvenWindow s_sum;

static void add(uint64_t *at, uint64_t v)
{
    if (v) {
        __atomic_add_fetch(at, v, __ATOMIC_RELAXED);
    }
}

void rd_pace_even_tally_plan(const RdPaceEvenPlan *p, double periodMs)
{
    if (p->how == RD_PE_AIMED) {
        add(&s_sum.changes, p->changed ? 1u : 0u);
        add(&s_sum.early, p->early ? 1u : 0u);
        if (p->holdMs > 0.0) {
            add(&s_sum.held, 1u);
            add(&s_sum.heldUs, (uint64_t)(p->holdMs * 1000.0 + 0.5));
        }
        __atomic_store_n(&s_sum.n, p->n, __ATOMIC_RELAXED);
        const double ns = floor(periodMs * 1.0e6 + 0.5);
        __atomic_store_n(&s_sum.periodNs,
                         ns <= 0.0 ? 0u : (ns >= 4.0e9 ? 4000000000u : (uint32_t)ns),
                         __ATOMIC_RELAXED);
    }
}

void rd_pace_even_tally_take(int how, bool late)
{
    if (how >= 0 && how < RD_PE_HOW_COUNT) {
        add(&s_sum.how[how], 1u);
    }
    add(&s_sum.lateAims, late ? 1u : 0u);
}

void rd_pace_even_tally_shown(int score)
{
    add(&s_sum.shown, 1u);
    add(&s_sum.shownOff, score != 0 ? 1u : 0u);
    add(&s_sum.shownLate, score > 0 ? 1u : 0u);
    add(&s_sum.shownEarly, score < 0 ? 1u : 0u);
}

static uint64_t take(uint64_t *at)
{
    return __atomic_exchange_n(at, 0u, __ATOMIC_RELAXED);
}

void rd_pace_even_take_window(RdPaceEvenWindow *w)
{
    for (int i = 0; i < RD_PE_HOW_COUNT; i++) {
        w->how[i] = take(&s_sum.how[i]);
    }
    w->changes = take(&s_sum.changes);
    w->lateAims = take(&s_sum.lateAims);
    w->held = take(&s_sum.held);
    w->heldUs = take(&s_sum.heldUs);
    w->early = take(&s_sum.early);
    w->shownOff = take(&s_sum.shownOff);
    w->shownLate = take(&s_sum.shownLate);
    w->shownEarly = take(&s_sum.shownEarly);
    w->shown = take(&s_sum.shown);
    w->n = __atomic_load_n(&s_sum.n, __ATOMIC_RELAXED);
    w->periodNs = __atomic_load_n(&s_sum.periodNs, __ATOMIC_RELAXED);
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

const char *rd_pace_even_how_text(int how)
{
    switch (how) {
    case RD_PE_AIMED:
        return "aimed";
    case RD_PE_NO_FIT:
        return "the screen's grid or latch not fitted";
    case RD_PE_NO_GPU:
        return "the GPU's time not measured yet";
    case RD_PE_KEPT:
        return "a kept frame";
    case RD_PE_NO_PLAN:
        return "handed over without a plan";
    case RD_PE_SLOW:
        return "a picture longer than a game frame";
    default:
        return "?";
    }
}

bool rd_pace_even_format(const RdPaceEvenWindow *w, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    uint64_t all = 0;
    for (int i = 0; i < RD_PE_HOW_COUNT; i++) {
        all += w->how[i];
    }
    if (all == 0u && w->shown == 0u) {
        return false;
    }
    Out o = {out, size, 0};
    const double p = (double)w->periodNs / 1e6;
    put(&o, "; the even cadence: ");
    if (w->n != 0u && p > 0.0) {
        put(&o, "%u refreshes a picture (the last; %.1f a second)", w->n,
            1000.0 / ((double)w->n * p));
    } else {
        put(&o, "not started");
    }
    put(&o,
        ", %llu changes; %llu pictures aimed (%llu held before the hand-over, %.2f ms on "
        "average; %llu before the game's step), %llu late for their refresh at the hand-over",
        (unsigned long long)w->changes, (unsigned long long)w->how[RD_PE_AIMED],
        (unsigned long long)w->held, w->held ? (double)w->heldUs / 1000.0 / (double)w->held : 0.0,
        (unsigned long long)w->early, (unsigned long long)w->lateAims);
    if (all != w->how[RD_PE_AIMED]) {
        put(&o, "; not aimed:");
        int first = 1;
        for (int i = 1; i < RD_PE_HOW_COUNT; i++) {
            if (w->how[i]) {
                put(&o, "%s %s %llu", first ? "" : ",", rd_pace_even_how_text(i),
                    (unsigned long long)w->how[i]);
                first = 0;
            }
        }
    }
    put(&o, "; shown off their refresh %llu of %llu (%llu later, %llu earlier)",
        (unsigned long long)w->shownOff, (unsigned long long)w->shown,
        (unsigned long long)w->shownLate, (unsigned long long)w->shownEarly);
    return true;
}

size_t rd_pace_even_start_text(bool measured, bool on, const char *why, char *out, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    Out o = {out, size, 0};
    put(&o, "rd: an even number of refreshes a picture: %s (%s)",
        !measured ? "not applicable here" : (on ? "on" : "off by default"), why != NULL ? why : "");
    if (measured) {
        put(&o,
            "; with pictures as made, every picture shown for the same N refreshes: N the fewest "
            "with the picture time (the larger of the GPU's median span and the hand-over to "
            "the first submit) + %.2f ms, up at once or when %u of the last %u aims were late "
            "(then not down until the picture time moves by %.2f ms), down to the fewest it fits "
            "with %.2f ms to spare after %.0f ms of it; each hand-over held until its "
            "predicted GPU end sits %.2f of a refresh before its refresh's latch (the screen's "
            "grid and latch), the content drawn for that refresh; it lowers the pictures a "
            "second where the GPU could make more%s",
            RD_PE_UP_MS, RD_PE_MISS_UP, RD_PE_MISS_WIN, RD_PE_FLOOR_T_MS, RD_PE_DOWN_MS,
            RD_PE_DOWN_HOLD_MS, RD_PE_AIM, on ? "" : "; not used (ICO_RD_PACE_EVEN=1 turns it on)");
    }
    return o.at < size ? o.at : size - 1;
}
