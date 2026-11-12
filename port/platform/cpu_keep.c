/* cpu_keep.c: the render, game and submit threads' cores kept while the
 * game runs (cpu_keep.h). */
#include "cpu_keep.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint64_t bit(int i)
{
    return (uint64_t)1 << i;
}

static int popcount64(uint64_t m)
{
    int n = 0;

    for (; m != 0; m &= m - 1) {
        n++;
    }
    return n;
}

/* the best core of m: the highest top speed, then the highest number;
   -1 with none of a known speed */
static int best_core(const CpuClasses *c, uint64_t m)
{
    int b = -1;

    for (int i = 0; i < c->count && i < CPU_PLACE_MAX; i++) {
        if ((m & bit(i)) && c->khz[i] != 0 && (b < 0 || c->khz[i] >= c->khz[b])) {
            b = i;
        }
    }
    return b;
}

static uint64_t known_mask(const CpuClasses *c)
{
    uint64_t m = 0;

    for (int i = 0; i < c->count && i < CPU_PLACE_MAX; i++) {
        if (c->khz[i] != 0) {
            m |= bit(i);
        }
    }
    return m;
}

bool cpu_keep_applies(const CpuClasses *c)
{
    return popcount64(cpu_place_fast_mask(c)) >= CPU_KEEP_MIN_FAST;
}

bool cpu_keep_choose(const CpuClasses *c, uint64_t allowed, uint64_t refused, uint64_t gameRefused,
                     CpuKeepLevel level, CpuKeepSets *out)
{
    const uint64_t a = allowed != 0 ? allowed : known_mask(c);
    const uint64_t f = cpu_place_fast_mask(c) & a;

    memset(out, 0, sizeof(*out));
    out->level = level;
    if (!cpu_keep_applies(c) || f == 0) {
        return false;
    }
    if (level == CPU_KEEP_FAST) {
        /* the start's rule on the allowed cores, the render thread on
           every faster one (render_thread.c place_thread_begin's
           fall-back before this keeper) */
        CpuPlan p;

        cpu_place_plan(c, &p);
        out->render = f;
        out->game = p.kind == CPU_PLAN_TWO_FAST ? f : 0;
        out->submit = p.submit & a;
        out->others = a;
        return true;
    }
    if (level == CPU_KEEP_ALONE) {
        const int r = best_core(c, f & ~refused);

        if (r < 0) {
            return false;
        }
        out->render = bit(r);
    } else {
        const int r1 = best_core(c, f);
        const int r2 = r1 >= 0 ? best_core(c, f & ~bit(r1)) : -1;

        if (r2 < 0) {
            return false;
        }
        out->render = bit(r1) | bit(r2);
    }
    {
        const int g = best_core(c, f & ~out->render & ~refused & ~gameRefused);

        out->game = g >= 0 ? bit(g) : 0;
    }
    out->submit = f & ~out->render & ~out->game;
    out->others = a & ~out->render & ~out->game;
    if (out->others == 0) {
        out->others = a & ~out->render;
    }
    if (out->others == 0) {
        out->others = a;
    }
    return true;
}

void cpu_keep_init(CpuKeep *k, const CpuClasses *c)
{
    memset(k, 0, sizeof(*k));
    k->classes = *c;
    k->on = cpu_keep_applies(c);
}

static bool pin_kept(const CpuKeepPin *p)
{
    return p->res == CPU_PIN_KEPT || p->res == CPU_PIN_UNKNOWN;
}

static CpuKeepPin pin_read(const CpuKeepOps *ops, long tid, uint64_t mask)
{
    CpuKeepPin p;
    bool ok;

    memset(&p, 0, sizeof(p));
    p.asked = mask;
    p.done = true;
    ok = ops->pin(ops->u, tid, mask, &p.err);
    p.got = ok ? ops->read(ops->u, tid) : 0;
    p.res = cpu_place_pin_result(ok, mask, p.got);
    return p;
}

/* what a pin left the thread on, for the change watch: the cores read
   back, or the ones asked when none could be read */
static uint64_t pin_expect(const CpuKeepPin *p)
{
    return p->got != 0 ? p->got : p->asked;
}

/* a read of a thread's cores against what its last pin left: a change the
   system made is counted once (until the next differs) */
static void watch(CpuKeep *k, uint64_t now, uint64_t expect, uint64_t *seen, bool game)
{
    if (now == 0 || expect == 0) {
        return;
    }
    if (now != expect && now != *seen) {
        k->changes++;
        k->blockChanges++;
        k->changeFrom = *seen != 0 ? *seen : expect;
        k->changeTo = now;
        k->changeGame = game;
        k->pending |= CPU_KEEP_WHY_CHANGED;
    }
    *seen = now;
}

bool cpu_keep_tick(CpuKeep *k, const CpuKeepOps *ops, double nowMs, unsigned why, uint64_t allowed,
                   long renderTid, long submitTid, long gameTid)
{
    CpuKeepSets ideal, s;
    bool chosen = false;

    if (!k->on) {
        return false;
    }
    k->pending |= why;
    if (renderTid <= 0) {
        return false; /* the requests wait for the render thread */
    }
    if (renderTid != k->renderTid) {
        /* the first tick, or the render thread made again (a backend
           switch): nothing it had is known */
        k->renderTid = renderTid;
        k->submitTid = 0;
        k->have = false;
        k->renderSeen = k->gameSeen = 0;
        k->pending |= CPU_KEEP_WHY_START;
    }
    if (allowed != k->allowed) {
        if (k->have) {
            k->pending |= CPU_KEEP_WHY_ALLOWED;
        }
        k->allowed = allowed;
    }
    if (k->have && submitTid > 0 && submitTid != k->submitTid) {
        k->pending |= CPU_KEEP_WHY_THREAD;
    }
    if (k->have) {
        const uint64_t r = ops->read(ops->u, renderTid);

        k->blockReads++;
        k->blockOnSet += r != 0 && r == k->renderExpect ? 1u : 0u;
        watch(k, r, k->renderExpect, &k->renderSeen, false);
        if (gameTid > 0) {
            watch(k, ops->read(ops->u, gameTid), k->gameExpect, &k->gameSeen, true);
        }
    }
    if (!cpu_keep_choose(&k->classes, allowed, 0, k->gameRefused, CPU_KEEP_ALONE, &ideal)) {
        return false; /* no faster core allowed: nothing to keep */
    }
    if (k->have) {
        /* on the ideal core alone and read back so: a scene, the focus or
           a return asks nothing more; a change, new cores allowed or a
           new thread still try */
        const unsigned must =
            CPU_KEEP_WHY_START | CPU_KEEP_WHY_CHANGED | CPU_KEEP_WHY_ALLOWED | CPU_KEEP_WHY_THREAD;
        const bool inIdeal = k->sets.level == CPU_KEEP_ALONE && k->sets.render == ideal.render &&
                             k->sets.game == ideal.game && pin_kept(&k->render) &&
                             k->renderSeen == k->renderExpect;

        if (inIdeal && (k->pending & must) == 0) {
            k->pending = 0;
            return false;
        }
        if (k->pending == 0) {
            if (nowMs - k->lastTryMs < CPU_KEEP_RETRY_MS) {
                return false;
            }
            k->pending |= CPU_KEEP_WHY_RETRY;
        }
    }
    /* a try */
    k->why = k->pending;
    k->pending = 0;
    k->lastTryMs = nowMs;
    k->tries++;
    k->blockTries++;
    memset(&k->render, 0, sizeof(k->render));
    memset(&k->game, 0, sizeof(k->game));
    memset(&k->submit, 0, sizeof(k->submit));
    k->ideal = pin_read(ops, renderTid, ideal.render);
    k->idealKept = pin_kept(&k->ideal);
    if (k->idealKept) {
        k->refused = 0;
        k->kept++;
        k->blockKept++;
        k->render = k->ideal;
        s = ideal;
        chosen = true;
    } else {
        /* the walk, from the level in force (the start: ALONE), at most
           one pin a level step and three after the ideal */
        CpuKeepLevel lv = k->have ? k->sets.level : CPU_KEEP_ALONE;

        k->refused |= ideal.render;
        for (int n = 0; n < 4 && !chosen; n++) {
            if (lv == CPU_KEEP_ALONE && popcount64(k->refused) >= 2) {
                lv = CPU_KEEP_PAIR; /* two one-core pins widened: the system widens them */
            }
            if (!cpu_keep_choose(&k->classes, allowed, k->refused, k->gameRefused, lv, &s)) {
                if (lv == CPU_KEEP_FAST) {
                    break;
                }
                lv = lv == CPU_KEEP_ALONE ? CPU_KEEP_PAIR : CPU_KEEP_FAST;
                continue;
            }
            k->render = pin_read(ops, renderTid, s.render);
            if (pin_kept(&k->render) || lv == CPU_KEEP_FAST) {
                chosen = true;
            } else if (lv == CPU_KEEP_ALONE) {
                k->refused |= s.render;
            } else {
                lv = CPU_KEEP_FAST;
            }
        }
    }
    if (!chosen) {
        k->have = false;
        return true;
    }
    k->sets = s;
    k->have = true;
    k->renderExpect = k->renderSeen = pin_expect(&k->render);
    /* the other threads off the render and game threads' cores first (the
       registered workers, the log thread), then the game and submit
       threads (a registered submit thread is skipped by the caller's
       keep_free, and pinned after it here) */
    if (ops->keep_free != NULL) {
        ops->keep_free(ops->u, s.others);
    }
    {
        int err = 0;

        (void)ops->pin(ops->u, 0, s.others, &err); /* the calling (log) thread */
    }
    if (gameTid > 0) {
        k->game = pin_read(ops, gameTid, s.game != 0 ? s.game : s.others);
        k->gameExpect = k->gameSeen = pin_expect(&k->game);
        if (s.game != 0 && popcount64(s.game) == 1 && !pin_kept(&k->game)) {
            /* the game thread's own core widened: another one at the next
               tick (a core refused to the game thread is not asked for it
               again in the run: at most one try a faster core) */
            k->gameRefused |= s.game;
            k->pending |= CPU_KEEP_WHY_CHANGED;
        }
    }
    if (submitTid > 0) {
        k->submit = pin_read(ops, submitTid, s.submit != 0 ? s.submit : s.others);
        k->submitTid = submitTid;
    }
    return true;
}

/* append to out at *len (the text cut at cap, *len the length it would
   have) */
#if defined(__GNUC__)
static void put(char *out, size_t cap, size_t *len, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
#endif
static void put(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(*len < cap ? out + *len : NULL, *len < cap ? cap - *len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *len += (size_t)n;
    }
}

static size_t done(char *out, size_t cap, size_t len)
{
    if (cap > 0 && len >= cap) {
        out[cap - 1] = '\0';
        return cap - 1;
    }
    return len;
}

size_t cpu_keep_why_text(unsigned why, char *out, size_t cap)
{
    static const char *const words[8] = {"the start",
                                         "a 3D scene began",
                                         "focus gained",
                                         "back from the background",
                                         "the system changed the threads' cores",
                                         "the cores the process may run on changed",
                                         "the 10 s retry",
                                         "the submit thread started"};
    size_t len = 0;
    bool any = false;

    if (cap > 0) {
        out[0] = '\0';
    }
    for (int i = 0; i < 8; i++) {
        if (why & (1u << i)) {
            put(out, cap, &len, "%s%s", any ? ", " : "", words[i]);
            any = true;
        }
    }
    if (!any) {
        put(out, cap, &len, "none");
    }
    return done(out, cap, len);
}

/* "kept", "not kept: the system keeps it to cores 2-7", "refused (errno
   22)", "kept (not read back)" */
static void pin_words(char *out, size_t cap, size_t *len, const CpuKeepPin *p)
{
    char m[96];

    (void)cpu_place_mask_text(p->got, m, sizeof(m));
    if (p->res == CPU_PIN_KEPT) {
        put(out, cap, len, "kept");
    } else if (p->res == CPU_PIN_UNKNOWN) {
        put(out, cap, len, "kept (not read back)");
    } else if (p->res == CPU_PIN_OTHER) {
        put(out, cap, len, "not kept: the system keeps it to cores %s", m);
    } else {
        put(out, cap, len, "refused (errno %d)", p->err);
    }
}

/* "7 alone", "the two fastest 6-7", "the faster cores 2-7" */
static void render_words(char *out, size_t cap, size_t *len, const CpuKeepSets *s)
{
    char m[96];

    (void)cpu_place_mask_text(s->render, m, sizeof(m));
    if (s->level == CPU_KEEP_ALONE) {
        put(out, cap, len, "%s alone", m);
    } else if (s->level == CPU_KEEP_PAIR) {
        put(out, cap, len, "the two fastest %s", m);
    } else {
        put(out, cap, len, "the faster cores %s", m);
    }
}

/* "the game thread on 6 alone, the submit thread on 2-5, every other
   thread on 0-5" (the sets, no results) */
static void sets_words(char *out, size_t cap, size_t *len, const CpuKeepSets *s)
{
    char g[96], sm[96], o[96];

    (void)cpu_place_mask_text(s->game != 0 ? s->game : s->others, g, sizeof(g));
    (void)cpu_place_mask_text(s->submit != 0 ? s->submit : s->others, sm, sizeof(sm));
    (void)cpu_place_mask_text(s->others, o, sizeof(o));
    put(out, cap, len, "the game thread on %s%s, the submit thread on %s, every other thread on %s",
        g, s->game != 0 && popcount64(s->game) == 1 ? " alone" : "", sm, o);
}

size_t cpu_keep_try_text(const CpuKeep *k, const char *sys, char *out, size_t cap)
{
    size_t len = 0;
    char a[96], b[96], w[256];

    if (cap > 0) {
        out[0] = '\0';
    }
    (void)cpu_place_mask_text(k->ideal.asked, a, sizeof(a));
    (void)cpu_place_mask_text(k->ideal.got, b, sizeof(b));
    put(out, cap, &len, "asked %s, read back %s", a, k->ideal.got != 0 ? b : "nothing");
    if (sys != NULL && sys[0] != '\0') {
        put(out, cap, &len, ", %s", sys);
    }
    (void)cpu_place_mask_text(k->allowed, a, sizeof(a));
    put(out, cap, &len, ", the process may run on %s; re-pin ", k->allowed != 0 ? a : "not known");
    if (k->idealKept) {
        put(out, cap, &len, "kept");
    } else if (k->ideal.res == CPU_PIN_OTHER) {
        put(out, cap, &len, "refused (the system said yes and keeps it to cores %s)", b);
    } else {
        put(out, cap, &len, "refused (errno %d)", k->ideal.err);
    }
    if (!k->have) {
        put(out, cap, &len, "; no cores could be kept");
    } else {
        if (!k->idealKept) {
            put(out, cap, &len, "; on ");
            render_words(out, cap, &len, &k->sets);
            put(out, cap, &len, " instead (");
            pin_words(out, cap, &len, &k->render);
            put(out, cap, &len, ")");
        }
        (void)cpu_place_mask_text(k->game.asked, a, sizeof(a));
        if (k->game.done) {
            put(out, cap, &len, "; the game thread on %s%s (", a,
                k->sets.game != 0 && popcount64(k->sets.game) == 1 ? " alone" : "");
            pin_words(out, cap, &len, &k->game);
            put(out, cap, &len, ")");
        } else {
            put(out, cap, &len, "; the game thread not known");
        }
        (void)cpu_place_mask_text(k->submit.asked, a, sizeof(a));
        if (k->submit.done) {
            put(out, cap, &len, ", the submit thread on %s (", a);
            pin_words(out, cap, &len, &k->submit);
            put(out, cap, &len, ")");
        } else {
            put(out, cap, &len, ", the submit thread not started yet");
        }
        (void)cpu_place_mask_text(k->sets.others, a, sizeof(a));
        put(out, cap, &len, ", every other thread on %s", a);
    }
    (void)cpu_keep_why_text(k->why, w, sizeof(w));
    put(out, cap, &len, "; why: %s", w);
    return done(out, cap, len);
}

size_t cpu_keep_block_text(const CpuKeep *k, const char *sys, char *out, size_t cap)
{
    size_t len = 0;
    char a[96], b[96];

    if (cap > 0) {
        out[0] = '\0';
    }
    if (!k->on) {
        put(out, cap, &len, "off (fewer than three cores faster than the slowest)");
        return done(out, cap, len);
    }
    if (!k->have) {
        put(out, cap, &len, "none kept yet (%u tries)", k->tries);
    } else {
        put(out, cap, &len, "now on ");
        render_words(out, cap, &len, &k->sets);
        put(out, cap, &len, "%s, ",
            k->sets.level == CPU_KEEP_ALONE && k->idealKept ? " (the ideal)" : "");
        sets_words(out, cap, &len, &k->sets);
        put(out, cap, &len, "; read back as set at %u of %u reads", k->blockOnSet, k->blockReads);
    }
    put(out, cap, &len,
        "; tries in the block %u (the ideal kept in %u), since the start %u (kept in %u)",
        k->blockTries, k->blockKept, k->tries, k->kept);
    put(out, cap, &len, "; changed by the system %u times in the block, %u since the start",
        k->blockChanges, k->changes);
    if (k->changes != 0) {
        (void)cpu_place_mask_text(k->changeFrom, a, sizeof(a));
        (void)cpu_place_mask_text(k->changeTo, b, sizeof(b));
        put(out, cap, &len, " (the last: the %s thread's from %s to %s)",
            k->changeGame ? "game" : "render", a, b);
    }
    if (sys != NULL && sys[0] != '\0') {
        put(out, cap, &len, "; %s", sys);
    }
    return done(out, cap, len);
}

void cpu_keep_block_reset(CpuKeep *k)
{
    k->blockTries = k->blockKept = k->blockChanges = k->blockReads = k->blockOnSet = 0;
}
