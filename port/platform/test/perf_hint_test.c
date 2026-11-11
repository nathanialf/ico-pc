/*
 * port/platform/test/perf_hint_test.c
 *
 * Android's performance hints, the pure part (perf_hint.h), against a fake
 * backend and a fake clock: the target from what the pacer plans (each
 * basis); a picture's work from its record (the waits taken out, the last
 * timed GPU time carried); the session made once the ids and a target are
 * known, its reports and their numbers, a target change asked once, a
 * thread change through setThreads and, without it (API 33), closed and
 * made again; closed in the background and made again on return; refused
 * once and never asked again; a full ring dropped and counted; a failed
 * report counted; the lines word for word; a short buffer; the wake period;
 * and a game thread pushing while the hint thread reports, on two real
 * threads (no picture lost, none reported twice).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ico_thread.h"
#include "perf_hint.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the fake backend ------------------------------------------------------ */

typedef struct Fake {
    int refuse;
    int reportRc;
    unsigned creates, closes, updates, setThreads, reports;
    int32_t tids[PERF_HINT_MAX_THREADS];
    size_t n;
    int64_t createTarget, target, lastTotal, lastCpu, lastGpu;
    uint64_t clock;
    int session; /* the session's address */
} Fake;

static void *f_create(void *u, const int32_t *tids, size_t n, int64_t targetNs)
{
    Fake *f = u;

    f->creates++;
    if (f->refuse) {
        return NULL;
    }
    f->n = n;
    memcpy(f->tids, tids, n * sizeof(tids[0]));
    f->createTarget = f->target = targetNs;
    return &f->session;
}

static int f_update(void *u, void *s, int64_t targetNs)
{
    Fake *f = u;

    CHECK(s == &f->session);
    f->updates++;
    f->target = targetNs;
    return 0;
}

static int f_report(void *u, void *s, const PerfHintWork *w, int64_t totalNs)
{
    Fake *f = u;

    CHECK(s == &f->session);
    f->reports++;
    f->lastTotal = totalNs;
    f->lastCpu = w->cpuNs;
    f->lastGpu = w->gpuNs;
    return f->reportRc;
}

static int f_set_threads(void *u, void *s, const int32_t *tids, size_t n)
{
    Fake *f = u;

    CHECK(s == &f->session);
    f->setThreads++;
    f->n = n;
    memcpy(f->tids, tids, n * sizeof(tids[0]));
    return 0;
}

static void f_close(void *u, void *s)
{
    Fake *f = u;

    CHECK(s == &f->session);
    f->closes++;
}

/* every read 0.1 ms on */
static uint64_t f_now(void *u)
{
    Fake *f = u;

    f->clock += 100000u;
    return f->clock;
}

static PerfHintOps fake_ops(Fake *f, bool setThreads, bool withGpu)
{
    PerfHintOps o;

    memset(&o, 0, sizeof(o));
    o.user = f;
    o.create = f_create;
    o.update_target = f_update;
    o.report = f_report;
    o.set_threads = setThreads ? f_set_threads : NULL;
    o.close = f_close;
    o.now_ns = f_now;
    o.withGpu = withGpu;
    return o;
}

static PerfHintWork work(int64_t cpuNs, int64_t gpuNs, bool own)
{
    PerfHintWork w;

    w.startNs = 1000000000;
    w.cpuNs = cpuNs;
    w.gpuNs = gpuNs;
    w.gpuOwn = own;
    return w;
}

/* --- the target ------------------------------------------------------------ */

static void testTarget(void)
{
    PerfHintPace p;
    int basis = -1;

    memset(&p, 0, sizeof(p));
    p.periodNs = 16683333u;
    p.refreshNs = 8333333u;
    p.gapNs = 7812500u; /* 120 Hz in mailbox: 8333333 - 8333333 / 16 */
    p.lockN = 2;
    p.original = true;
    p.frameSteps = 2;
    CHECK(perf_hint_target_ns(&p, &basis) == 33366666u && basis == PERF_HINT_BASIS_ORIGINAL);
    p.frameSteps = 0; /* not known: one step */
    CHECK(perf_hint_target_ns(&p, &basis) == 16683333u && basis == PERF_HINT_BASIS_ORIGINAL);
    p.original = false;
    CHECK(perf_hint_target_ns(&p, &basis) == 8341666u && basis == PERF_HINT_BASIS_LOCK);
    p.lockN = 0;
    CHECK(perf_hint_target_ns(&p, &basis) == 7812500u && basis == PERF_HINT_BASIS_GAP);
    p.gapNs = 0; /* uncapped back to back, or the FIFO lock */
    CHECK(perf_hint_target_ns(&p, &basis) == 8333333u && basis == PERF_HINT_BASIS_REFRESH);
    p.refreshNs = 0;
    p.periodNs = 20000000u;
    CHECK(perf_hint_target_ns(&p, &basis) == 20000000u && basis == PERF_HINT_BASIS_PERIOD);
    CHECK(perf_hint_target_ns(&p, NULL) == 20000000u);
    CHECK_STR(perf_hint_basis_words(PERF_HINT_BASIS_ORIGINAL), "a game frame");
    CHECK_STR(perf_hint_basis_words(PERF_HINT_BASIS_LOCK), "the vsync lock's presents");
    CHECK_STR(perf_hint_basis_words(PERF_HINT_BASIS_GAP), "the pacer's present gap");
    CHECK_STR(perf_hint_basis_words(PERF_HINT_BASIS_REFRESH), "the display's refresh");
    CHECK_STR(perf_hint_basis_words(PERF_HINT_BASIS_PERIOD), "the step's period");
}

/* --- a picture's work -------------------------------------------------------- */

static void testWork(void)
{
    PerfHintRecord r;
    PerfHintWork w;
    int64_t lastGpu = 0;

    memset(&r, 0, sizeof(r));
    r.presented = true;
    r.totalMs = 10.0;
    r.presentMs = 3.0;
    r.fenceWaitMs = 2.0;
    r.acquireMs = 0.5;
    r.startMs = 1000.0;
    /* no GPU time yet */
    CHECK(perf_hint_work(&r, &lastGpu, &w));
    CHECK(w.cpuNs == 4500000 && w.gpuNs == 0 && !w.gpuOwn && w.startNs == 1000000000);
    /* a timed one: its own */
    r.gpuValid = true;
    r.gpuMs = 12.25;
    CHECK(perf_hint_work(&r, &lastGpu, &w));
    CHECK(lastGpu == 12250000 && w.gpuNs == 12250000 && w.gpuOwn);
    /* the next untimed: the last timed one's */
    r.gpuValid = false;
    CHECK(perf_hint_work(&r, &lastGpu, &w));
    CHECK(w.gpuNs == 12250000 && !w.gpuOwn);
    /* nothing presented: not reported, but its GPU time kept */
    r.presented = false;
    r.gpuValid = true;
    r.gpuMs = 20.0;
    CHECK(!perf_hint_work(&r, &lastGpu, &w));
    CHECK(lastGpu == 20000000);
    /* all waits: nothing left */
    r.presented = true;
    r.gpuValid = false;
    r.totalMs = 5.0;
    CHECK(!perf_hint_work(&r, &lastGpu, &w));
    /* no start */
    r.totalMs = 10.0;
    r.startMs = 0.0;
    CHECK(!perf_hint_work(&r, &lastGpu, &w));
    /* the totals */
    w = work(4500000, 12250000, false);
    CHECK(perf_hint_total_ns(&w, true) == 12250000);
    CHECK(perf_hint_total_ns(&w, false) == 4500000);
    w = work(9000000, 2000000, false);
    CHECK(perf_hint_total_ns(&w, true) == 9000000);
}

/* --- the session --------------------------------------------------------------- */

static const int32_t k_tids[3] = {100, 101, 102};

static void testSession(void)
{
    Fake f;
    PerfHint *h = calloc(1, sizeof(*h));
    PerfHintOps o;
    PerfHintStats s;
    PerfHintWork w;
    char line[512];

    memset(&f, 0, sizeof(f));
    o = fake_ops(&f, true, true);
    perf_hint_init(h, &o);
    /* nothing known: no session, the pictures let go uncounted */
    w = work(4500000, 0, false);
    CHECK(perf_hint_push(h, &w) && perf_hint_push(h, &w));
    CHECK(perf_hint_step(h) == 0 && f.creates == 0);
    perf_hint_set_threads(h, k_tids, 3);
    CHECK(perf_hint_step(h) == 0 && f.creates == 0); /* no target yet */
    perf_hint_set_target(h, 8333333u);
    CHECK(perf_hint_step(h) == PERF_HINT_EV_MADE);
    CHECK(f.creates == 1 && f.n == 3 && f.tids[2] == 102 && f.createTarget == 8333333);
    CHECK(h->made == 1 && h->everMade);
    perf_hint_take(h, &s);
    CHECK(s.reports == 0 && s.background == 0 && s.dropped == 0);
    /* three pictures: the GPU's own once, then the last timed one's */
    w = work(4500000, 12250000, true);
    CHECK(perf_hint_push(h, &w));
    w.gpuOwn = false;
    CHECK(perf_hint_push(h, &w) && perf_hint_push(h, &w));
    CHECK(perf_hint_step(h) == 0);
    CHECK(f.reports == 3 && f.lastTotal == 12250000 && f.lastCpu == 4500000);
    perf_hint_take(h, &s);
    CHECK(s.reports == 3 && s.over == 3 && s.gpuReports == 3 && s.gpuOwn == 1);
    perf_hint_block_text(&s, true, h->sessTargetNs, line, sizeof(line));
    CHECK_STR(line,
              "performance hints: 3 reports (actual CPU 4.50 ms / GPU 12.25 ms of the 8.33 ms "
              "target; over target in 3), the report calls 0.30 ms in all (longest 0.10 "
              "ms), target changed 0 times; GPU this picture's own in 1 of 3, the last timed "
              "one's otherwise");
    /* the take emptied it */
    perf_hint_take(h, &s);
    CHECK(s.reports == 0 && s.callMs == 0.0);
    /* a picture under the target: not over */
    w = work(4000000, 6000000, true);
    CHECK(perf_hint_push(h, &w));
    (void)perf_hint_step(h);
    perf_hint_take(h, &s);
    CHECK(s.reports == 1 && s.over == 0);
    /* the target changed: asked once */
    perf_hint_set_target(h, 16683333u);
    (void)perf_hint_step(h);
    (void)perf_hint_step(h);
    CHECK(f.updates == 1 && f.target == 16683333);
    /* the render thread made again: setThreads, no new session */
    {
        const int32_t t2[3] = {100, 201, 202};

        perf_hint_set_threads(h, t2, 3);
        perf_hint_set_threads(h, t2, 3); /* the same again: no new generation */
        (void)perf_hint_step(h);
        CHECK(f.setThreads == 1 && f.creates == 1 && f.tids[1] == 201);
        (void)perf_hint_step(h);
        CHECK(f.setThreads == 1);
    }
    perf_hint_take(h, &s);
    CHECK(s.targetChanges == 1 && s.threadChanges == 1 && s.failed == 0);
    /* the background: closed, its pictures counted, made again on return */
    perf_hint_set_active(h, false);
    CHECK(perf_hint_step(h) == PERF_HINT_EV_CLOSED && f.closes == 1 && h->session == NULL);
    w = work(4500000, 0, false);
    CHECK(perf_hint_push(h, &w) && perf_hint_push(h, &w));
    CHECK(perf_hint_step(h) == 0);
    perf_hint_take(h, &s);
    CHECK(s.background == 2 && s.reports == 0);
    perf_hint_set_active(h, true);
    CHECK(perf_hint_step(h) == PERF_HINT_EV_MADE && f.creates == 2 && h->made == 2);
    CHECK(f.createTarget == 16683333);
    /* a failed report counted with its error */
    f.reportRc = 32;
    CHECK(perf_hint_push(h, &w));
    (void)perf_hint_step(h);
    perf_hint_take(h, &s);
    CHECK(s.failed == 1 && s.lastError == 32);
    f.reportRc = 0;
    /* a full ring: the last dropped and counted */
    for (unsigned i = 0; i < PERF_HINT_RING; i++) {
        CHECK(perf_hint_push(h, &w));
    }
    CHECK(!perf_hint_push(h, &w));
    (void)perf_hint_step(h);
    perf_hint_take(h, &s);
    CHECK(s.reports == PERF_HINT_RING && s.dropped == 1);
    /* the end */
    perf_hint_shutdown(h);
    CHECK(f.closes == 2 && h->session == NULL);
    free(h);
}

/* API 33: no setThreads: a thread change closes the session and makes
   another; the reports without the GPU */
static void testApi33(void)
{
    Fake f;
    PerfHint *h = calloc(1, sizeof(*h));
    PerfHintOps o;
    PerfHintStats s;
    PerfHintWork w;
    char line[512];
    const int32_t t2[2] = {100, 301};

    memset(&f, 0, sizeof(f));
    o = fake_ops(&f, false, false);
    perf_hint_init(h, &o);
    perf_hint_set_threads(h, k_tids, 2);
    perf_hint_set_target(h, 16683333u);
    CHECK(perf_hint_step(h) == PERF_HINT_EV_MADE && f.n == 2);
    perf_hint_set_threads(h, t2, 2);
    CHECK(perf_hint_step(h) == 0);
    CHECK(f.creates == 2 && f.closes == 1 && f.tids[1] == 301 && f.createTarget == 16683333);
    w = work(4500000, 30000000, true); /* the GPU time not reported: the CPU's total */
    CHECK(perf_hint_push(h, &w));
    (void)perf_hint_step(h);
    CHECK(f.lastTotal == 4500000);
    perf_hint_take(h, &s);
    CHECK(s.over == 0 && s.threadChanges == 1);
    perf_hint_block_text(&s, false, h->sessTargetNs, line, sizeof(line));
    CHECK_STR(line, "performance hints: 1 reports (actual CPU 4.50 ms / GPU not reported (API 33 "
                    "and 34) of the 16.68 ms target; over target in 0), the report calls 0.10 ms "
                    "in all (longest 0.10 ms), target changed 0 times; threads changed 1 times");
    perf_hint_shutdown(h);
    free(h);
}

/* refused: off for the run, never asked again */
static void testRefused(void)
{
    Fake f;
    PerfHint *h = calloc(1, sizeof(*h));
    PerfHintOps o;
    PerfHintWork w = work(4500000, 0, false);

    memset(&f, 0, sizeof(f));
    f.refuse = 1;
    o = fake_ops(&f, true, true);
    perf_hint_init(h, &o);
    perf_hint_set_threads(h, k_tids, 3);
    perf_hint_set_target(h, 8333333u);
    CHECK(perf_hint_step(h) == PERF_HINT_EV_REFUSED && h->refused && !h->everMade);
    CHECK(perf_hint_push(h, &w));
    CHECK(perf_hint_step(h) == 0 && f.creates == 1 && f.reports == 0);
    perf_hint_set_active(h, false);
    perf_hint_set_active(h, true);
    CHECK(perf_hint_step(h) == 0 && f.creates == 1);
    perf_hint_shutdown(h);
    CHECK(f.closes == 0);
    free(h);
}

/* --- the lines ------------------------------------------------------------------- */

static void testLines(void)
{
    static const char *const names[3] = {"game", "render", "submit"};
    const int32_t tids[3] = {9001, 9040, 9041};
    PerfHintOnText t;
    PerfHintStats s;
    char line[512], small[16];

    memset(&t, 0, sizeof(t));
    t.api = 35;
    t.gameMode = 1;
    t.basis = PERF_HINT_BASIS_REFRESH;
    t.withGpu = true;
    t.names = names;
    t.tids = tids;
    t.n = 3;
    t.targetNs = 8333333u;
    t.rateNs = 16666667;
    t.wakeMs = 16;
    t.madeMs = 0.42;
    perf_hint_on_text(&t, line, sizeof(line));
    CHECK_STR(line, "performance hints: on (API 35; session of 3 threads: game 9001, render 9040, "
                    "submit 9041; target 8.33 ms a picture (the display's refresh); GPU durations "
                    "reported yes; game mode standard (1); the system's update rate 16.67 ms, the "
                    "reports made every 16 ms off the game and render threads; made in 0.42 ms)");
    t.again = true;
    t.api = 33;
    t.withGpu = false;
    t.gameMode = -1;
    t.n = 1;
    t.rateNs = -1;
    t.basis = PERF_HINT_BASIS_GAP;
    perf_hint_on_text(&t, line, sizeof(line));
    CHECK_STR(line, "performance hints: on again (API 33; session of 1 threads: game 9001; target "
                    "8.33 ms a picture (the pacer's present gap); GPU durations reported no; game "
                    "mode not asked (Android 11 or older); the system gives no update rate, the "
                    "reports made every 16 ms off the game and render threads; made in 0.42 ms)");
    /* a block with everything */
    memset(&s, 0, sizeof(s));
    s.reports = 4;
    s.cpuMs = 10.0;
    s.gpuReports = 0;
    s.over = 1;
    s.callMs = 0.5;
    s.callMaxMs = 0.25;
    s.targetChanges = 2;
    s.failed = 3;
    s.lastError = 22;
    s.dropped = 5;
    s.background = 6;
    perf_hint_block_text(&s, true, 16683333u, line, sizeof(line));
    CHECK_STR(line, "performance hints: 4 reports (actual CPU 2.50 ms / GPU none timed yet of the "
                    "16.68 ms target; over target in 1), the report calls 0.50 ms in all (longest "
                    "0.25 ms), target changed 2 times; 3 failed (error 22); 5 dropped (the queue "
                    "full); 6 not reported (in the background)");
    /* a short buffer: cut, ended */
    CHECK(perf_hint_block_text(&s, true, 16683333u, small, sizeof(small)) == sizeof(small) - 1);
    CHECK(strlen(small) == sizeof(small) - 1);
    CHECK(perf_hint_on_text(&t, small, sizeof(small)) == sizeof(small) - 1);
    CHECK(perf_hint_block_text(&s, true, 1, small, 0) == 0);
    /* the game modes */
    CHECK_STR(perf_hint_game_mode_words(0), "unsupported");
    CHECK_STR(perf_hint_game_mode_words(2), "performance");
    CHECK_STR(perf_hint_game_mode_words(3), "battery");
    CHECK_STR(perf_hint_game_mode_words(4), "custom");
    CHECK_STR(perf_hint_game_mode_words(-2), "not read (the call failed)");
    CHECK_STR(perf_hint_game_mode_words(9), "unknown");
    /* the wake period */
    CHECK(perf_hint_wake_ms(-1) == 16u);
    CHECK(perf_hint_wake_ms(0) == 16u);
    CHECK(perf_hint_wake_ms(1000000) == 4u);
    CHECK(perf_hint_wake_ms(16666667) == 16u);
    CHECK(perf_hint_wake_ms(200000000) == 50u);
}

/* --- two threads ----------------------------------------------------------------- */

#define THREAD_PICTURES 20000u

static PerfHint *s_th;
static int s_done;

static void producer(void *u)
{
    (void)u;
    for (unsigned i = 1; i <= THREAD_PICTURES; i++) {
        const PerfHintWork w = work((int64_t)i, 0, false);

        while (!perf_hint_push(s_th, &w)) {}
        if (i % 1000u == 0) {
            const int32_t t[3] = {100, (int32_t)(1000 + i), 102};

            perf_hint_set_threads(s_th, t, 3);
            perf_hint_set_target(s_th, 8000000u + i);
        }
    }
    __atomic_store_n(&s_done, 1, __ATOMIC_RELEASE);
}

static int64_t s_sum;
static unsigned s_got;

static int t_report(void *u, void *s, const PerfHintWork *w, int64_t totalNs)
{
    (void)u;
    (void)s;
    (void)totalNs;
    s_sum += w->cpuNs;
    s_got++;
    return 0;
}

static void testThreads(void)
{
    Fake f;
    PerfHintOps o;
    PerfHintStats s, all;
    IcoThread *th;

    memset(&f, 0, sizeof(f));
    o = fake_ops(&f, true, false);
    o.report = t_report;
    s_th = calloc(1, sizeof(*s_th));
    perf_hint_init(s_th, &o);
    perf_hint_set_threads(s_th, k_tids, 3);
    perf_hint_set_target(s_th, 8000000u);
    CHECK(perf_hint_step(s_th) == PERF_HINT_EV_MADE);
    memset(&all, 0, sizeof(all));
    th = ico_thread_create(producer, NULL);
    CHECK(th != NULL);
    if (th == NULL) {
        free(s_th);
        return;
    }
    for (;;) {
        const int done = __atomic_load_n(&s_done, __ATOMIC_ACQUIRE);

        (void)perf_hint_step(s_th);
        perf_hint_take(s_th, &s);
        all.reports += s.reports;
        all.dropped += s.dropped;
        if (done) {
            (void)perf_hint_step(s_th);
            perf_hint_take(s_th, &s);
            all.reports += s.reports;
            break;
        }
    }
    ico_thread_join(th);
    CHECK(all.reports == THREAD_PICTURES && s_got == THREAD_PICTURES);
    /* the producer spins on a full ring: each refused push counted as dropped */
    (void)all.dropped;
    CHECK(s_sum == (int64_t)THREAD_PICTURES * (THREAD_PICTURES + 1) / 2);
    CHECK(f.creates == 1 && f.tids[1] == (int32_t)(1000 + THREAD_PICTURES));
    CHECK(f.target == 8000000 + THREAD_PICTURES);
    perf_hint_shutdown(s_th);
    free(s_th);
}

int main(void)
{
    testTarget();
    testWork();
    testSession();
    testApi33();
    testRefused();
    testLines();
    testThreads();
    if (failures) {
        printf("perf_hint_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("perf_hint_test: ok\n");
    return 0;
}
