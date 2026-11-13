/* rd_glass.c: the content's speed on the glass (rd_glass.h): the pairs, the
 * join of pictures and display times by id, the 10 s sums and the line.
 * No RHI call here (rd_glass_live.c reads the display's times). */
#include "rd_glass.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rd_pace_rules.h"
#include "rd_pace_screen.h"

_Static_assert(RD_GLASS_RULES == 1 + RD_RULE_COUNT, "the drawn measure and one a rule");
_Static_assert(sizeof(((RdGlassPic *)0)->alpha) / sizeof(float) == RD_RULE_COUNT,
               "a picture's alpha for every rule");

#define DEFAULT_TICK_MS 33.3667

/* ------------------------------------------------------------- the pair */

static void statAdd(RdGlassStat *s, double r)
{
    const double u = floor(r * RD_GLASS_UNITS + 0.5);
    const int64_t v = u > 1.0e8 ? 100000000 : (u < -1.0e8 ? -100000000 : (int64_t)u);
    s->n++;
    s->sum += v;
    s->sum2 += (uint64_t)(v * v);
    s->above125 += r > 1.25 ? 1u : 0u;
    s->above15 += r > 1.5 ? 1u : 0u;
    s->below075 += r < 0.75 ? 1u : 0u;
}

/* a picture's content moment by measure m (0 the drawn, 1 + rule), ms; NAN
   when not computed */
static double content(const RdGlassPic *p, int m)
{
    const double tick = p->tickMs > 0.0 ? p->tickMs : DEFAULT_TICK_MS;
    const double a = m == RD_GLASS_DRAWN ? (double)p->drawn : (double)p->alpha[m - 1];
    return a < 0.0 ? NAN : ((double)p->frame + a) * tick;
}

bool rd_glass_pair(RdGlassWindow *w, const RdGlassPic *a, uint64_t aNs, const RdGlassPic *b,
                   uint64_t bNs, uint64_t refreshNs)
{
    const int64_t df = (int64_t)b->frame - (int64_t)a->frame;
    const double gap = (double)(int64_t)(bNs - aNs) / 1e6;
    if (df < 0 || df > RD_GLASS_FRAME_JUMP || !(gap > 0.0) || gap > RD_GLASS_GAP_MAX_MS) {
        return false;
    }
    int cls = RD_GLASS_CLASSES; /* not known */
    if (refreshNs != 0) {
        const double k = floor(gap / ((double)refreshNs / 1e6) + 0.5);
        cls = k >= (double)(RD_GLASS_CLASSES - 1) ? RD_GLASS_CLASSES - 1 : (int)k;
        w->gaps[cls]++;
        w->refreshNs = refreshNs;
    }
    w->pairs++;
    w->drawnRule = b->rule;
    const double dh = b->handMs > 0.0 && a->handMs > 0.0 ? b->handMs - a->handMs : 0.0;
    const double de = b->gpuEndMs > 0.0 && a->gpuEndMs > 0.0 && a->gpuExact && b->gpuExact
                          ? b->gpuEndMs - a->gpuEndMs
                          : 0.0;
    for (int m = 0; m < RD_GLASS_RULES; m++) {
        const double ca = content(a, m), cb = content(b, m);
        if (isnan(ca) || isnan(cb)) {
            continue;
        }
        const double step = cb - ca;
        statAdd(&w->glass[m][RD_GLASS_CLASSES], step / gap);
        if (cls < RD_GLASS_CLASSES) {
            statAdd(&w->glass[m][cls], step / gap);
        }
        if (dh > 0.0) {
            statAdd(&w->hand[m], step / dh);
        }
        if (de > 0.0) {
            statAdd(&w->gpu[m], step / de);
        }
    }
    return true;
}

/* ------------------------------------------------------------- the join */

#define JOIN 128u

static struct {
    uint32_t id;
    bool pic, time;
    RdGlassPic p;
    uint64_t ns, refreshNs, earliestNs, marginNs;
} s_slot[JOIN];

static struct {
    bool have;
    RdGlassPic p;
    uint64_t ns;
} s_last;

/* the 10 s sums: the collecting thread adds, the window's thread takes,
   under a spin (one add a picture, one take every 10 s) */
static RdGlassWindow s_sum;
static int s_spin;

static void lock(void)
{
    while (__atomic_exchange_n(&s_spin, 1, __ATOMIC_ACQUIRE)) {}
}

static void unlock(void)
{
    __atomic_store_n(&s_spin, 0, __ATOMIC_RELEASE);
}

/* a slot with both in: paired with the shown picture before; the screen's
   prediction scored and the picture sent to its model */
static void done(uint32_t i)
{
    const RdGlassPic *p = &s_slot[i].p;
    const double actualMs = (double)s_slot[i].ns / 1e6;
    const double refMs = (double)s_slot[i].refreshNs / 1e6;
    if (p->screenEndMs > 0.0) {
        const RdPaceScreenSample smp = {p->screenEndMs, actualMs, refMs,
                                        p->gpuExact ? p->gpuEndMs : 0.0};
        rd_pace_screen_seen(&smp);
    }
    lock();
    if (p->screenAtMs > 0.0 && refMs > 0.0) {
        const int k = rd_pace_screen_score(p->screenAtMs, actualMs, refMs);
        s_sum.scored++;
        s_sum.hit += k == 0 ? 1u : 0u;
        s_sum.late += k == 1 ? 1u : 0u;
        s_sum.early += k == -1 ? 1u : 0u;
        s_sum.further += k > 1 || k < -1 ? 1u : 0u;
    }
    if (s_slot[i].marginNs != 0 || s_slot[i].earliestNs != 0) {
        s_sum.marginN++;
        s_sum.marginSumUs += (int64_t)(s_slot[i].marginNs / 1000u);
        if (s_slot[i].earliestNs != 0 && s_slot[i].earliestNs < s_slot[i].ns &&
            2u * (s_slot[i].ns - s_slot[i].earliestNs) >= s_slot[i].refreshNs) {
            s_sum.earlyN++;
            s_sum.earlySumUs += (int64_t)((s_slot[i].ns - s_slot[i].earliestNs) / 1000u);
        }
    }
    s_sum.shown++;
    if (s_last.have && s_slot[i].id > s_last.p.id) {
        s_sum.unseen += s_slot[i].id - s_last.p.id - 1u;
        (void)rd_glass_pair(&s_sum, &s_last.p, s_last.ns, &s_slot[i].p, s_slot[i].ns,
                            s_slot[i].refreshNs);
    }
    unlock();
    s_last.have = true;
    s_last.p = s_slot[i].p;
    s_last.ns = s_slot[i].ns;
    s_slot[i].pic = s_slot[i].time = false;
}

static uint32_t slotOf(uint32_t id)
{
    const uint32_t i = id % JOIN;
    if (s_slot[i].id != id) {
        s_slot[i].id = id;
        s_slot[i].pic = s_slot[i].time = false;
    }
    return i;
}

void rd_glass_join(const RdGlassPic *p)
{
    if (p->id == 0) {
        return;
    }
    lock();
    s_sum.pictures++;
    unlock();
    const uint32_t i = slotOf(p->id);
    s_slot[i].p = *p;
    s_slot[i].pic = true;
    if (s_slot[i].time) {
        done(i);
    }
}

void rd_glass_time(uint32_t id, uint64_t actualNs, uint64_t refreshNs, uint64_t earliestNs,
                   uint64_t marginNs)
{
    if (id == 0) {
        return;
    }
    const uint32_t i = slotOf(id);
    s_slot[i].ns = actualNs;
    s_slot[i].refreshNs = refreshNs;
    s_slot[i].earliestNs = earliestNs;
    s_slot[i].marginNs = marginNs;
    s_slot[i].time = true;
    if (s_slot[i].pic) {
        done(i);
    }
}

void rd_glass_take(RdGlassWindow *w)
{
    lock();
    *w = s_sum;
    memset(&s_sum, 0, sizeof(s_sum));
    unlock();
}

void rd_glass_reset(void)
{
    memset(s_slot, 0, sizeof(s_slot));
    memset(&s_last, 0, sizeof(s_last));
    lock();
    memset(&s_sum, 0, sizeof(s_sum));
    unlock();
}

/* ------------------------------------------------------------- the line */

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

static double mean(const RdGlassStat *s)
{
    return s->n ? (double)s->sum / (double)s->n / RD_GLASS_UNITS : 0.0;
}

static double dev(const RdGlassStat *s)
{
    if (!s->n) {
        return 0.0;
    }
    const double m = (double)s->sum / (double)s->n;
    const double v = (double)s->sum2 / (double)s->n - m * m;
    return v > 0.0 ? sqrt(v) / RD_GLASS_UNITS : 0.0;
}

static double share(uint64_t k, uint64_t n)
{
    return n ? 100.0 * (double)k / (double)n : 0.0;
}

static const char *ruleName(int r)
{
    switch (r) {
    case RD_RULE_HAND:
        return "the hand-over";
    case RD_RULE_GPU:
        return "the GPU's predicted end";
    case RD_RULE_TIMER:
        return "the own timer";
    case RD_RULE_SCREEN:
        return "the screen's predicted refresh";
    default:
        return "the present clock";
    }
}

static void measure(Out *o, const RdGlassWindow *w, int m)
{
    const RdGlassStat *all = &w->glass[m][RD_GLASS_CLASSES];
    put(&o[0],
        "mean %.2f (deviation %.3f), above 1.25 %.1f %%, above 1.5 %.1f %%, below 0.75 %.1f %%",
        mean(all), dev(all), share(all->above125, all->n), share(all->above15, all->n),
        share(all->below075, all->n));
    if (w->refreshNs != 0) {
        static const char *const kCls[RD_GLASS_CLASSES] = {
            "under half a refresh", "1 refresh", "2 refreshes", "3 refreshes", "4 or more"};
        put(&o[0], "; by the display's gap:");
        int first = 1;
        for (int c = 0; c < RD_GLASS_CLASSES; c++) {
            if (w->glass[m][c].n) {
                put(&o[0], "%s %s %.2f (%.3f)", first ? "" : ",", kCls[c], mean(&w->glass[m][c]),
                    dev(&w->glass[m][c]));
                first = 0;
            }
        }
    }
    put(&o[0],
        "; against the hand-overs %.2f (%.3f) over %llu, against the GPU's ends %.2f (%.3f) "
        "over %llu",
        mean(&w->hand[m]), dev(&w->hand[m]), (unsigned long long)w->hand[m].n, mean(&w->gpu[m]),
        dev(&w->gpu[m]), (unsigned long long)w->gpu[m].n);
}

bool rd_glass_format(const RdGlassWindow *w, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (w->pictures == 0) {
        return false;
    }
    Out o = {out, size, 0};
    put(&o,
        "on the glass in 10 s: %llu pictures with a display id, %llu shown with the display's "
        "time, %llu presented with no time back (replaced before shown, or not reported), %llu "
        "pairs",
        (unsigned long long)w->pictures, (unsigned long long)w->shown,
        (unsigned long long)w->unseen, (unsigned long long)w->pairs);
    if (w->refreshNs != 0) {
        put(&o,
            "; the display's gaps by refreshes of %.2f ms: under half %u, 1 %u, 2 %u, 3 %u, 4 or "
            "more %u",
            (double)w->refreshNs / 1e6, w->gaps[0], w->gaps[1], w->gaps[2], w->gaps[3], w->gaps[4]);
    } else {
        put(&o, "; the display's refresh not known");
    }
    if (w->pairs == 0) {
        return true;
    }
    put(&o,
        "; the content's speed on the glass (its step over the display's gap, 1.00 real "
        "time), drawn (%s): ",
        ruleName(w->drawnRule));
    measure(&o, w, RD_GLASS_DRAWN);
    for (int r = 0; r < RD_RULE_COUNT; r++) {
        if (w->glass[1 + r][RD_GLASS_CLASSES].n) {
            put(&o, "; by %s%s: ", ruleName(r), r == w->drawnRule ? " (drawn)" : " (not drawn)");
            measure(&o, w, 1 + r);
        }
    }
    if (w->marginN) {
        put(&o,
            "; the display's own figures over %llu shown: present margin %.2f ms on average, "
            "earliest present half a refresh or more before the time shown %llu",
            (unsigned long long)w->marginN, (double)w->marginSumUs / 1000.0 / (double)w->marginN,
            (unsigned long long)w->earlyN);
        if (w->earlyN) {
            put(&o, " (by %.2f ms on average)", (double)w->earlySumUs / 1000.0 / (double)w->earlyN);
        }
    }
    return true;
}

void rd_glass_line(void)
{
    static RdGlassWindow w;
    static RdPaceScreenWindow sw;
    static char line[6144];
    rd_glass_take(&w);
    rd_pace_screen_take(&sw);
    const bool any = rd_glass_format(&w, line, sizeof line);
    if (!any) {
        /* no picture with a display id (no VK_GOOGLE_display_timing): the
           screen's rule's fall-backs alone, where it ran */
        (void)snprintf(line, sizeof line, "on the glass in 10 s: no picture with a display id");
    }
    const size_t at = strlen(line);
    if (rd_pace_screen_format(&sw, w.scored, w.hit, w.late, w.early, w.further, line + at,
                              sizeof line - at) ||
        any) {
        fprintf(stderr, "window: %s\n", line);
    }
}
