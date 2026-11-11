/*
 * port/platform/perf_hint.c
 *
 * Android's performance hints, the pure part (perf_hint.h).
 */
#include "perf_hint.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* --- the target ---------------------------------------------------------- */

uint64_t perf_hint_target_ns(const PerfHintPace *p, int *basis)
{
    int b;
    uint64_t t;

    if (p->original) {
        b = PERF_HINT_BASIS_ORIGINAL;
        t = p->periodNs * (p->frameSteps > 0 ? p->frameSteps : 1u);
    } else if (p->lockN > 0) {
        b = PERF_HINT_BASIS_LOCK;
        t = p->periodNs / p->lockN;
    } else if (p->gapNs > 0) {
        b = PERF_HINT_BASIS_GAP;
        t = p->gapNs;
    } else if (p->refreshNs > 0) {
        b = PERF_HINT_BASIS_REFRESH;
        t = p->refreshNs;
    } else {
        b = PERF_HINT_BASIS_PERIOD;
        t = p->periodNs;
    }
    if (basis != NULL) {
        *basis = b;
    }
    return t;
}

const char *perf_hint_basis_words(int basis)
{
    switch (basis) {
    case PERF_HINT_BASIS_ORIGINAL:
        return "a game frame";
    case PERF_HINT_BASIS_LOCK:
        return "the vsync lock's presents";
    case PERF_HINT_BASIS_GAP:
        return "the pacer's present gap";
    case PERF_HINT_BASIS_REFRESH:
        return "the display's refresh";
    default:
        return "the step's period";
    }
}

/* --- a picture's work ------------------------------------------------------ */

bool perf_hint_work(const PerfHintRecord *r, int64_t *lastGpuNs, PerfHintWork *out)
{
    bool own = false;

    if (r->gpuValid && r->gpuMs > 0.0) {
        *lastGpuNs = (int64_t)(r->gpuMs * 1e6);
        own = true;
    }
    if (!r->presented || !(r->startMs > 0.0)) {
        return false;
    }
    const double cpuMs = r->totalMs - r->presentMs - r->fenceWaitMs - r->acquireMs;
    const int64_t cpuNs = cpuMs > 0.0 ? (int64_t)(cpuMs * 1e6) : 0;

    if (cpuNs <= 0) {
        return false;
    }
    out->startNs = (int64_t)(r->startMs * 1e6);
    out->cpuNs = cpuNs;
    out->gpuNs = *lastGpuNs;
    out->gpuOwn = own;
    return true;
}

int64_t perf_hint_total_ns(const PerfHintWork *w, bool withGpu)
{
    return withGpu && w->gpuNs > w->cpuNs ? w->gpuNs : w->cpuNs;
}

/* --- the hand-over ----------------------------------------------------------- */

static void spin_lock(int *s)
{
    while (__atomic_exchange_n(s, 1, __ATOMIC_ACQUIRE)) {}
}

static void spin_unlock(int *s)
{
    __atomic_store_n(s, 0, __ATOMIC_RELEASE);
}

void perf_hint_init(PerfHint *h, const PerfHintOps *ops)
{
    memset(h, 0, sizeof(*h));
    h->ops = *ops;
    h->active = 1;
}

void perf_hint_set_target(PerfHint *h, uint64_t targetNs)
{
    __atomic_store_n(&h->targetNs, targetNs, __ATOMIC_RELEASE);
}

void perf_hint_set_threads(PerfHint *h, const int32_t *tids, uint32_t n)
{
    if (n > PERF_HINT_MAX_THREADS) {
        n = PERF_HINT_MAX_THREADS;
    }
    if (n == h->wTidN && (n == 0 || memcmp(tids, h->wTids, n * sizeof(tids[0])) == 0)) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        h->wTids[i] = tids[i];
    }
    h->wTidN = n;
    /* a sequence lock: odd while written (one writer, the game thread) */
    const uint32_t g = __atomic_load_n(&h->tidGen, __ATOMIC_RELAXED);

    __atomic_store_n(&h->tidGen, g + 1u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (uint32_t i = 0; i < n; i++) {
        __atomic_store_n(&h->tids[i], tids[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&h->tidN, n, __ATOMIC_RELAXED);
    __atomic_store_n(&h->tidGen, g + 2u, __ATOMIC_RELEASE);
}

void perf_hint_set_active(PerfHint *h, bool active)
{
    __atomic_store_n(&h->active, active ? 1 : 0, __ATOMIC_RELEASE);
}

bool perf_hint_push(PerfHint *h, const PerfHintWork *w)
{
    const uint32_t head = __atomic_load_n(&h->head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&h->tail, __ATOMIC_ACQUIRE);

    if (head - tail >= PERF_HINT_RING) {
        (void)__atomic_fetch_add(&h->droppedW, 1u, __ATOMIC_RELAXED);
        return false;
    }
    h->ring[head & (PERF_HINT_RING - 1u)] = *w;
    __atomic_store_n(&h->head, head + 1u, __ATOMIC_RELEASE);
    return true;
}

/* the ids as last written whole; false while they are being written */
static bool read_tids(PerfHint *h, int32_t *t, uint32_t *n, uint32_t *gen)
{
    for (int tries = 0; tries < 4; tries++) {
        const uint32_t g1 = __atomic_load_n(&h->tidGen, __ATOMIC_ACQUIRE);

        if (g1 & 1u) {
            continue;
        }
        uint32_t nn = __atomic_load_n(&h->tidN, __ATOMIC_RELAXED);

        nn = nn > PERF_HINT_MAX_THREADS ? PERF_HINT_MAX_THREADS : nn;
        for (uint32_t i = 0; i < nn; i++) {
            t[i] = __atomic_load_n(&h->tids[i], __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&h->tidGen, __ATOMIC_RELAXED) == g1) {
            *n = nn;
            *gen = g1;
            return true;
        }
    }
    return false;
}

static uint64_t now_ns(PerfHint *h)
{
    return h->ops.now_ns != NULL ? h->ops.now_ns(h->ops.user) : 0;
}

static bool make(PerfHint *h, const int32_t *t, uint32_t n, uint32_t gen, uint64_t target)
{
    const uint64_t t0 = now_ns(h);
    void *s = h->ops.create(h->ops.user, t, n, (int64_t)target);

    h->madeMs = (double)(now_ns(h) - t0) / 1e6;
    if (s == NULL) {
        h->refused = true;
        return false;
    }
    h->session = s;
    h->sessGen = gen;
    h->sessN = n;
    for (uint32_t i = 0; i < n; i++) {
        h->sessTids[i] = t[i];
    }
    h->sessTargetNs = target;
    h->made++;
    h->everMade = true;
    return true;
}

/* the ring's pictures: reported with a session, else let go (counted as in
   the background when background) */
static void drain(PerfHint *h, bool background)
{
    uint32_t tail = __atomic_load_n(&h->tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&h->head, __ATOMIC_ACQUIRE);
    PerfHintStats d;

    memset(&d, 0, sizeof(d));
    while (tail != head) {
        const PerfHintWork w = h->ring[tail & (PERF_HINT_RING - 1u)];

        tail++;
        __atomic_store_n(&h->tail, tail, __ATOMIC_RELEASE);
        if (h->session == NULL) {
            d.background += background ? 1u : 0u;
            continue;
        }
        const int64_t total = perf_hint_total_ns(&w, h->ops.withGpu);
        const uint64_t t0 = now_ns(h);
        const int rc = h->ops.report(h->ops.user, h->session, &w, total);
        const double ms = (double)(now_ns(h) - t0) / 1e6;

        d.reports++;
        d.callMs += ms;
        d.callMaxMs = ms > d.callMaxMs ? ms : d.callMaxMs;
        d.cpuMs += (double)w.cpuNs / 1e6;
        if (w.gpuNs > 0) {
            d.gpuReports++;
            d.gpuMs += (double)w.gpuNs / 1e6;
            d.gpuOwn += w.gpuOwn ? 1u : 0u;
        }
        if ((uint64_t)total > h->sessTargetNs) {
            d.over++;
        }
        if (rc != 0) {
            d.failed++;
            d.lastError = rc;
        }
    }
    const uint32_t dropped = __atomic_load_n(&h->droppedW, __ATOMIC_RELAXED);

    spin_lock(&h->spin);
    h->st.reports += d.reports;
    h->st.over += d.over;
    h->st.gpuReports += d.gpuReports;
    h->st.gpuOwn += d.gpuOwn;
    h->st.failed += d.failed;
    h->st.background += d.background;
    h->st.dropped += dropped - h->droppedR;
    if (d.failed) {
        h->st.lastError = d.lastError;
    }
    h->st.cpuMs += d.cpuMs;
    h->st.gpuMs += d.gpuMs;
    h->st.callMs += d.callMs;
    h->st.callMaxMs = d.callMaxMs > h->st.callMaxMs ? d.callMaxMs : h->st.callMaxMs;
    spin_unlock(&h->spin);
    h->droppedR = dropped;
}

static void count_change(PerfHint *h, bool target, int rc)
{
    spin_lock(&h->spin);
    if (rc != 0) {
        h->st.failed++;
        h->st.lastError = rc;
    } else if (target) {
        h->st.targetChanges++;
    } else {
        h->st.threadChanges++;
    }
    spin_unlock(&h->spin);
}

unsigned perf_hint_step(PerfHint *h)
{
    unsigned ev = 0;
    int32_t t[PERF_HINT_MAX_THREADS];
    uint32_t n = 0, gen = 0;
    const bool active = __atomic_load_n(&h->active, __ATOMIC_ACQUIRE) != 0;
    const bool haveT = read_tids(h, t, &n, &gen) && n > 0;
    const uint64_t target = __atomic_load_n(&h->targetNs, __ATOMIC_ACQUIRE);

    if (!active) {
        if (h->session != NULL) {
            h->ops.close(h->ops.user, h->session);
            h->session = NULL;
            ev |= PERF_HINT_EV_CLOSED;
        }
        drain(h, true);
        return ev;
    }
    if (h->refused) {
        drain(h, false);
        return ev;
    }
    if (h->session == NULL && haveT && target > 0) {
        if (!make(h, t, n, gen, target)) {
            drain(h, false);
            return ev | PERF_HINT_EV_REFUSED;
        }
        ev |= PERF_HINT_EV_MADE;
    }
    if (h->session != NULL && haveT && gen != h->sessGen) {
        /* the render thread made again (a renderer switch): its new ids */
        const int rc =
            h->ops.set_threads != NULL ? h->ops.set_threads(h->ops.user, h->session, t, n) : -1;

        if (rc == 0) {
            h->sessGen = gen;
            h->sessN = n;
            for (uint32_t i = 0; i < n; i++) {
                h->sessTids[i] = t[i];
            }
        } else {
            /* API 33 (no setThreads), or the call refused: made again */
            h->ops.close(h->ops.user, h->session);
            h->session = NULL;
            if (!make(h, t, n, gen, h->sessTargetNs > 0 ? h->sessTargetNs : target)) {
                drain(h, false);
                return ev | PERF_HINT_EV_REFUSED;
            }
        }
        count_change(h, false, 0);
    }
    if (h->session != NULL && target > 0 && target != h->sessTargetNs) {
        const int rc = h->ops.update_target(h->ops.user, h->session, (int64_t)target);

        h->sessTargetNs = target; /* not asked again for the same target */
        count_change(h, true, rc);
    }
    drain(h, false);
    return ev;
}

void perf_hint_shutdown(PerfHint *h)
{
    if (h->session != NULL) {
        h->ops.close(h->ops.user, h->session);
        h->session = NULL;
    }
}

void perf_hint_take(PerfHint *h, PerfHintStats *out)
{
    spin_lock(&h->spin);
    *out = h->st;
    memset(&h->st, 0, sizeof(h->st));
    spin_unlock(&h->spin);
}

/* --- the lines ----------------------------------------------------------- */

static void put(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*len >= cap) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        out[*len] = '\0';
        return;
    }
    *len = (size_t)n >= cap - *len ? cap - 1 : *len + (size_t)n;
}

const char *perf_hint_game_mode_words(int mode)
{
    switch (mode) {
    case 0:
        return "unsupported";
    case 1:
        return "standard";
    case 2:
        return "performance";
    case 3:
        return "battery";
    case 4:
        return "custom";
    case -1:
        return "not asked (Android 11 or older)";
    case -2:
        return "not read (the call failed)";
    default:
        return "unknown";
    }
}

size_t perf_hint_on_text(const PerfHintOnText *t, char *out, size_t cap)
{
    size_t len = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    put(out, cap, &len,
        "performance hints: on%s (API %d; session of %u threads: ", t->again ? " again" : "",
        t->api, (unsigned)t->n);
    for (uint32_t i = 0; i < t->n; i++) {
        put(out, cap, &len, "%s%s %d", i ? ", " : "", t->names[i], (int)t->tids[i]);
    }
    put(out, cap, &len, "; target %.2f ms a picture (%s); GPU durations reported %s; game mode %s",
        (double)t->targetNs / 1e6, perf_hint_basis_words(t->basis), t->withGpu ? "yes" : "no",
        perf_hint_game_mode_words(t->gameMode));
    if (t->gameMode >= 0) {
        put(out, cap, &len, " (%d)", t->gameMode);
    }
    if (t->rateNs > 0) {
        put(out, cap, &len, "; the system's update rate %.2f ms", (double)t->rateNs / 1e6);
    } else {
        put(out, cap, &len, "; the system gives no update rate");
    }
    put(out, cap, &len,
        ", the reports made every %u ms off the game and render threads; made in %.2f ms)",
        t->wakeMs, t->madeMs);
    return len;
}

size_t perf_hint_block_text(const PerfHintStats *s, bool withGpu, uint64_t targetNs, char *out,
                            size_t cap)
{
    size_t len = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    put(out, cap, &len, "performance hints: %u reports (actual CPU %.2f ms / GPU ", s->reports,
        s->reports ? s->cpuMs / s->reports : 0.0);
    if (!withGpu) {
        put(out, cap, &len, "not reported (API 33 and 34)");
    } else if (s->gpuReports == 0) {
        put(out, cap, &len, "none timed yet");
    } else {
        put(out, cap, &len, "%.2f ms", s->gpuMs / s->gpuReports);
    }
    put(out, cap, &len,
        " of the %.2f ms target; over target in %u), the report calls %.2f ms in all (longest "
        "%.2f ms), target changed %u times",
        (double)targetNs / 1e6, s->over, s->callMs, s->callMaxMs, s->targetChanges);
    if (withGpu && s->gpuReports > 0) {
        put(out, cap, &len, "; GPU this picture's own in %u of %u, the last timed one's otherwise",
            s->gpuOwn, s->gpuReports);
    }
    if (s->threadChanges) {
        put(out, cap, &len, "; threads changed %u times", s->threadChanges);
    }
    if (s->failed) {
        put(out, cap, &len, "; %u failed (error %d)", s->failed, s->lastError);
    }
    if (s->dropped) {
        put(out, cap, &len, "; %u dropped (the queue full)", s->dropped);
    }
    if (s->background) {
        put(out, cap, &len, "; %u not reported (in the background)", s->background);
    }
    return len;
}

unsigned perf_hint_wake_ms(int64_t rateNs)
{
    if (rateNs <= 0) {
        return 16u;
    }
    const int64_t ms = rateNs / 1000000;

    return ms < 4 ? 4u : (ms > 50 ? 50u : (unsigned)ms);
}
