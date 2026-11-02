/* rd_thread.c: the renderer on the window's render thread (rd_thread.h). */
#include "rd_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
#include "rd_internal.h"
#include "rd_submit.h"

static RdThreadHost s_host;
static bool s_on;

/* inside one of the render thread's jobs (a depth: a job's own calls run
 * where they are); the job's hand-over still open (rd__reads_done) */
static _Thread_local int t_onR;
static _Thread_local bool t_handed;
/* the outer job's start (rd__now_ms) and the reads of the last picture
 * handed over, from that start to rd__reads_done (rd_thread_take_reads_ns;
 * both the render thread's own) */
static _Thread_local double t_jobStartMs;
static _Thread_local uint64_t t_readsNs;

/* the hand-over's lock: the count of pictures whose reads have not ended,
 * the game thread's waits for them, the textures to retire, the block's
 * sums and the render thread's counts as last published */
static IcoMutex *s_lk;
static IcoCond *s_cv;
static int s_owned;
static RhiTexture *s_retire;
static uint32_t s_retireN, s_retireCap;
static RdThreadBlock s_block;
static RdStats s_rStats; /* the render thread's own counts (rd__stats_here) */
static RdStats s_rSnap;  /* as its last reads' end left them */
static RdStats s_view;   /* rd_get_stats's answer on the game thread */

/* the game thread's: the picture handed over (one at a time), its
 * result, rd_submit_hold, the waits for the picture before */
typedef struct Tramp {
    void (*fn)(void *arg);
    void *arg;
    bool handed;
} Tramp;

typedef struct ReplayArgs {
    const RdFrame *f;
    int keep;
    bool present, ok;
} ReplayArgs;

static ReplayArgs s_picReplay;

typedef struct PresentArgs {
    float alpha;
    double raw;
    bool ok;
} PresentArgs;

static PresentArgs s_picPresent;
static Tramp s_picTramp;
static bool s_async, s_asyncPresent, s_lastOk = true, s_hold;
static uint64_t s_waitedNs;
static uint32_t s_failedPresents; /* rd_thread_failed_presents */

static void lk(void)
{
    if (s_lk) {
        ico_mutex_lock(s_lk);
    }
}

static void ulk(void)
{
    if (s_lk) {
        ico_mutex_unlock(s_lk);
    }
}

/* the render thread's counts as the game thread will read them (its lock
 * held) */
static void publishLocked(void)
{
    s_rSnap = s_rStats;
    s_rSnap.pipelines = rd__pipeline_count();
}

static void retireDrain(void)
{
    lk();
    RhiTexture *list = s_retire;
    const uint32_t n = s_retireN;
    s_retire = NULL;
    s_retireN = s_retireCap = 0;
    ulk();
    for (uint32_t i = 0; i < n; i++) {
        rhi_destroy_texture(list[i]);
    }
    free(list);
    if (n) {
        lk();
        s_block.retired += n;
        ulk();
    }
}

static void handOver(void)
{
    lk();
    __atomic_add_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    ulk();
}

static void giveBack(void)
{
    lk();
    __atomic_sub_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    if (s_cv) {
        ico_cond_broadcast(s_cv);
    }
    ulk();
}

/* a job on the render thread: its reads handed over or not, the textures
 * the game thread destroyed since the last job destroyed first */
static void runTramp(Tramp *t, bool picture)
{
    const bool outer = t_onR == 0;
    const bool handed = t_handed;
    t_onR++;
    t_handed = t->handed;
    if (outer) {
        t_jobStartMs = rd__now_ms();
        retireDrain();
        if (picture) {
            s_rStats.draws = 0; /* the picture's draws, as rd_begin_frame's count was */
        }
    }
    t->fn(t->arg);
    rd__reads_done();
    if (outer) {
        rd__overlay_hand_over_drop(); /* a picture whose present drew nothing */
        lk();
        publishLocked();
        if (picture) {
            s_block.pictures++;
        }
        ulk();
    }
    t_handed = handed;
    t_onR--;
}

static void pictureTramp(void *arg)
{
    runTramp(arg, true);
}

static void callTramp(void *arg)
{
    runTramp(arg, false);
}

void rd_set_thread_host(const RdThreadHost *host)
{
    if (s_on) {
        (void)rd_thread_drain();
        /* the render thread's counts back into g_rd.stats: the counters
         * the window takes deltas of keep going */
        g_rd.stats.draws += s_rStats.draws;
        g_rd.stats.pipelineCreates += s_rStats.pipelineCreates;
        g_rd.stats.textureUploads += s_rStats.textureUploads;
        g_rd.stats.tempTargets += s_rStats.tempTargets;
        g_rd.stats.tempReused += s_rStats.tempReused;
        memset(&s_rStats, 0, sizeof(s_rStats));
    }
    s_on = false;
    s_host = (RdThreadHost){0};
    retireDrain(); /* here: nothing is posted any more */
    if (host != NULL && host->post != NULL && host->drain != NULL) {
        if (!s_lk) {
            s_lk = ico_mutex_create();
            s_cv = ico_cond_create();
        }
        if (!s_lk || !s_cv) {
            rd__log("render thread: no lock could be made: the replays stay on this thread");
            return;
        }
        rd__perf_lock_init();
        s_host = *host;
        memset(&s_rStats, 0, sizeof(s_rStats));
        lk();
        publishLocked();
        memset(&s_block, 0, sizeof(s_block));
        ulk();
        s_owned = 0;
        s_async = false;
        s_lastOk = true;
        s_on = true;
    }
}

bool rd_thread_host_on(void)
{
    return s_on;
}

bool rd_thread_on_render(void)
{
    return t_onR != 0;
}

uint64_t rd_thread_drain(void)
{
    if (!s_on || t_onR) {
        return 0;
    }
    const uint64_t ns = s_host.drain(s_host.user);
    if (s_async) {
        s_lastOk = s_asyncPresent ? s_picPresent.ok : s_picReplay.ok;
        if (s_asyncPresent && !s_lastOk) {
            s_failedPresents++;
        }
        s_async = false;
        s_asyncPresent = false;
    }
    return ns;
}

uint32_t rd_thread_failed_presents(void)
{
    return s_failedPresents;
}

void rd_submit_hold(bool wait)
{
    s_hold = wait;
}

bool rd_submit_async(void)
{
    return s_async;
}

uint64_t rd_submit_waited_ns(void)
{
    return s_waitedNs;
}

uint64_t rd_thread_take_reads_ns(void)
{
    const uint64_t ns = t_readsNs;
    t_readsNs = 0;
    return ns;
}

void rd_thread_block_take(RdThreadBlock *out)
{
    lk();
    *out = s_block;
    memset(&s_block, 0, sizeof(s_block));
    ulk();
    if (out->pictures) {
        out->interpMs /= out->pictures;
        out->walkMs /= out->pictures;
        out->submitMs /= out->pictures;
        out->waitMs /= out->pictures;
    }
}

/* ------------------------------------------------------------ texLock */

void rd__texlock_wait(void)
{
    if (!s_on || t_onR || __atomic_load_n(&s_owned, __ATOMIC_ACQUIRE) == 0) {
        return;
    }
    const double t0 = rd__now_ms();
    lk();
    while (__atomic_load_n(&s_owned, __ATOMIC_ACQUIRE) != 0) {
        ico_cond_wait(s_cv, s_lk);
    }
    const double ms = rd__now_ms() - t0;
    s_block.readWaits++;
    s_block.readWaitMs += ms;
    s_block.readWaitMaxMs = ms > s_block.readWaitMaxMs ? ms : s_block.readWaitMaxMs;
    ulk();
}

void rd__reads_done(void)
{
    if (!t_handed) {
        return;
    }
    t_handed = false;
    {
        const double ms = rd__now_ms() - t_jobStartMs;
        t_readsNs = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u;
    }
    lk();
    publishLocked();
    __atomic_sub_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    ico_cond_broadcast(s_cv);
    ulk();
}

void rd__retire_texture(RhiTexture t)
{
    if (!t.id) {
        return;
    }
    if (!s_on || t_onR) {
        rhi_destroy_texture(t);
        return;
    }
    bool kept = false;
    lk();
    if (s_retireN == s_retireCap) {
        const uint32_t cap = s_retireCap ? s_retireCap * 2u : 64u;
        RhiTexture *n = realloc(s_retire, (size_t)cap * sizeof(*n));
        if (n) {
            s_retire = n;
            s_retireCap = cap;
        }
    }
    if (s_retireN < s_retireCap) {
        s_retire[s_retireN++] = t;
        kept = true;
    }
    ulk();
    if (!kept) {
        rhi_destroy_texture(t); /* no memory for the list: the call guard waits for the thread */
    }
}

bool rd__render_call(void (*fn)(void *arg), void *arg)
{
    if (!s_on) {
        return false;
    }
    if (t_onR) {
        fn(arg);
        return true;
    }
    Tramp t = {fn, arg, false};
    return s_host.post(s_host.user, callTramp, &t, false, true);
}

/* ---------------------------------------------------------- the replays */

static void replayJob(void *arg)
{
    ReplayArgs *r = arg;
    r->ok = rd__replay_frame_here(r->f, r->keep, r->present);
}

bool rd__replay_frame(const RdFrame *f, int keep, bool present)
{
    if (!s_on || t_onR) {
        return rd__replay_frame_here(f, keep, present);
    }
    ReplayArgs r = {f, keep, present, false};
    Tramp t = {replayJob, &r, false};
    if (present) {
        (void)rd_thread_drain();
        rd__overlay_hand_over(); /* the UI's layers on this thread (rd_present.c) */
    }
    if (s_host.post(s_host.user, pictureTramp, &t, present, true)) {
        return r.ok;
    }
    retireDrain();
    r.ok = rd__replay_frame_here(f, keep, present);
    rd__overlay_hand_over_drop();
    return r.ok;
}

/* a picture waits for its end when its result is read right after it, and
 * every picture under a host whose posts all wait (sync) */
static bool pictureWaits(void)
{
    return s_hold || s_host.sync || rd__capture_armed() || rd__blended_dump_armed();
}

/* the picture before finished: its result, the wait counted (the
 * window's display wait, present_once) */
void rd__thread_picture_begin(void)
{
    s_waitedNs += rd_thread_drain();
}

bool rd__replay_frame_async(const RdFrame *f, int keep, bool present)
{
    if (!s_on || t_onR) {
        return rd__replay_frame_here(f, keep, present);
    }
    rd__thread_picture_begin();
    const bool wait = pictureWaits();
    if (present) {
        rd__overlay_hand_over(); /* the UI's layers on this thread (rd_present.c) */
    }
    s_picReplay = (ReplayArgs){f, keep, present, false};
    s_picTramp = (Tramp){replayJob, &s_picReplay, !wait};
    if (!wait) {
        handOver();
    }
    if (s_host.post(s_host.user, pictureTramp, &s_picTramp, present, wait)) {
        s_async = !wait;
        s_asyncPresent = false;
        if (wait) {
            s_lastOk = s_picReplay.ok;
            return s_picReplay.ok;
        }
        return true;
    }
    if (!wait) {
        giveBack();
    }
    retireDrain();
    s_lastOk = rd__replay_frame_here(f, keep, present);
    rd__overlay_hand_over_drop(); /* nothing posted holds it */
    return s_lastOk;
}

/* ------------------------------------------------------- the presents */

/* the present clock's value the poster took, for rd_present on the
 * render thread (rd__thread_clock_raw) */
static _Thread_local bool t_rawSet;
static _Thread_local double t_raw;

static void presentJob(void *arg)
{
    PresentArgs *p = arg;
    const bool set = t_rawSet;
    const double raw = t_raw;
    t_rawSet = true;
    t_raw = p->raw;
    p->ok = rd_present(p->alpha);
    t_rawSet = set;
    t_raw = raw;
}

double rd__thread_clock_raw(void)
{
    if (t_rawSet) {
        const double r = t_raw;
        t_rawSet = false; /* taken: a second present in the job finds none */
        return r;
    }
    return rd__present_clock_raw_take();
}

bool rd_present_post(float alpha)
{
    if (!s_on || t_onR) {
        return rd_present(alpha);
    }
    rd__thread_picture_begin();
    const RdFrame *cur = rd__last_frame();
    if (!rd_interpolation_active() || !g_rd.hasDevice || g_rd.videoShown || cur == NULL ||
        !cur->closed) {
        /* nothing to hand over: its answer here, the render thread idle */
        return rd_present(alpha);
    }
    const bool wait = pictureWaits();
    /* the UI's layers on this thread, before the hand-over: their
     * callbacks share port/ui's font state with the step's own text
     * (rd_present.c s_pre) */
    rd__overlay_hand_over();
    s_picPresent = (PresentArgs){alpha, rd__present_clock_raw_take(), false};
    s_picTramp = (Tramp){presentJob, &s_picPresent, !wait};
    if (!wait) {
        handOver();
    }
    if (s_host.post(s_host.user, pictureTramp, &s_picTramp, true, wait)) {
        s_async = !wait;
        s_asyncPresent = !wait;
        if (wait) {
            s_lastOk = s_picPresent.ok;
            return s_picPresent.ok;
        }
        return true;
    }
    if (!wait) {
        giveBack();
    }
    retireDrain();
    presentJob(&s_picPresent);
    if (!s_async) {
        rd__overlay_hand_over_drop(); /* no picture handed over holds it */
    }
    s_lastOk = s_picPresent.ok;
    return s_lastOk;
}

/* --------------------------------------------------------------- stats */

RdStats *rd__stats_here(void)
{
    return s_on && t_onR ? &s_rStats : &g_rd.stats;
}

const RdStats *rd__thread_stats(void)
{
    lk();
    s_view = g_rd.stats;
    s_view.draws += s_rSnap.draws;
    s_view.pipelineCreates += s_rSnap.pipelineCreates;
    s_view.textureUploads += s_rSnap.textureUploads;
    s_view.tempTargets += s_rSnap.tempTargets;
    s_view.tempReused += s_rSnap.tempReused;
    s_view.pipelines = s_rSnap.pipelines;
    ulk();
    return &s_view;
}

void rd__thread_note_replay(double walkMs, double submitMs, double waitMs)
{
    if (!s_on || !t_onR) {
        return;
    }
    lk();
    s_block.walkMs += walkMs;
    s_block.submitMs += submitMs;
    s_block.waitMs += waitMs;
    ulk();
}

void rd__thread_note_interp(double ms)
{
    if (!s_on || !t_onR) {
        return;
    }
    lk();
    s_block.interpMs += ms;
    ulk();
}

/* ------------------------------------------- the renderer's own thread */

#define LOC_JOBS 8u

static struct {
    IcoThread *th;
    IcoMutex *m;
    IcoCond *c;
    Tramp *jobs[LOC_JOBS];
    void (*fn[LOC_JOBS])(void *arg);
    uint32_t head, count;
    uint64_t posted, done;
    bool quit;
    int busy;
    RdThreadMode mode;
} s_loc;

static void locMain(void *u)
{
    (void)u;
    ico_mutex_lock(s_loc.m);
    for (;;) {
        while (s_loc.count == 0 && !s_loc.quit) {
            ico_cond_wait(s_loc.c, s_loc.m);
        }
        if (s_loc.count == 0) {
            break;
        }
        void (*fn)(void *arg) = s_loc.fn[s_loc.head];
        void *arg = s_loc.jobs[s_loc.head];
        s_loc.head = (s_loc.head + 1u) % LOC_JOBS;
        s_loc.count--;
        ico_mutex_unlock(s_loc.m);
        fn(arg);
        ico_mutex_lock(s_loc.m);
        s_loc.done++;
        if (s_loc.count == 0) {
            __atomic_store_n(&s_loc.busy, 0, __ATOMIC_RELEASE);
        }
        ico_cond_broadcast(s_loc.c);
    }
    ico_mutex_unlock(s_loc.m);
}

static bool locPost(void *u, void (*fn)(void *arg), void *arg, bool picture, bool wait)
{
    (void)u;
    (void)picture;
    ico_mutex_lock(s_loc.m);
    if (s_loc.count == LOC_JOBS || s_loc.quit) {
        ico_mutex_unlock(s_loc.m);
        return false;
    }
    const uint32_t at = (s_loc.head + s_loc.count) % LOC_JOBS;
    s_loc.fn[at] = fn;
    s_loc.jobs[at] = arg;
    s_loc.count++;
    const uint64_t seq = ++s_loc.posted;
    __atomic_store_n(&s_loc.busy, 1, __ATOMIC_RELEASE);
    ico_cond_broadcast(s_loc.c);
    if (wait || s_loc.mode == RD_THREAD_SYNC) {
        while (s_loc.done < seq) {
            ico_cond_wait(s_loc.c, s_loc.m);
        }
    }
    ico_mutex_unlock(s_loc.m);
    return true;
}

static uint64_t locDrain(void *u)
{
    (void)u;
    const double t0 = ico_thread_now_ms();
    ico_mutex_lock(s_loc.m);
    while (s_loc.done < s_loc.posted) {
        ico_cond_wait(s_loc.c, s_loc.m);
    }
    ico_mutex_unlock(s_loc.m);
    return (uint64_t)((ico_thread_now_ms() - t0) * 1e6);
}

/* the RHI call guard: a call from any other thread waits for the jobs */
static bool locBusy(void *u)
{
    (void)u;
    return t_onR == 0 && __atomic_load_n(&s_loc.busy, __ATOMIC_ACQUIRE) != 0;
}

static void locWait(void *u)
{
    (void)locDrain(u);
}

bool rd_thread_mode_parse(const char *s, RdThreadMode *out)
{
    if (s == NULL) {
        return false;
    }
    if (strcmp(s, "off") == 0) {
        *out = RD_THREAD_OFF;
    } else if (strcmp(s, "sync") == 0) {
        *out = RD_THREAD_SYNC;
    } else if (strcmp(s, "on") == 0) {
        *out = RD_THREAD_ON;
    } else {
        return false;
    }
    return true;
}

bool rd_thread_local_start(RdThreadMode mode)
{
    static const RhiCallGuard guard = {NULL, locBusy, locWait};
    const RdThreadHost host = {NULL, locPost, locDrain, mode == RD_THREAD_SYNC};
    if (mode == RD_THREAD_OFF || s_loc.th != NULL) {
        return false;
    }
    if (g_rd.hasDevice && rhi_backend() == RHI_BACKEND_OPENGL) {
        rd__log("render thread: not with OpenGL here (its context is the calling thread's)");
        return false;
    }
    memset(&s_loc, 0, sizeof(s_loc));
    s_loc.mode = mode;
    s_loc.m = ico_mutex_create();
    s_loc.c = ico_cond_create();
    s_loc.th = s_loc.m && s_loc.c ? ico_thread_create(locMain, NULL) : NULL;
    if (s_loc.th == NULL) {
        if (s_loc.c) {
            ico_cond_destroy(s_loc.c);
        }
        if (s_loc.m) {
            ico_mutex_destroy(s_loc.m);
        }
        memset(&s_loc, 0, sizeof(s_loc));
        rd__log("render thread: the thread could not be made");
        return false;
    }
    rhi_set_call_guard(&guard);
    rd_set_thread_host(&host);
    rd__log("render thread: %s (the replays on a thread of their own%s)",
            mode == RD_THREAD_SYNC ? "sync" : "on",
            mode == RD_THREAD_SYNC ? ", each waited for" : "");
    return true;
}

void rd_thread_local_stop(void)
{
    if (s_loc.th == NULL) {
        return;
    }
    rd_set_thread_host(NULL); /* drains first */
    rhi_set_call_guard(NULL);
    ico_mutex_lock(s_loc.m);
    s_loc.quit = true;
    ico_cond_broadcast(s_loc.c);
    ico_mutex_unlock(s_loc.m);
    ico_thread_join(s_loc.th);
    ico_cond_destroy(s_loc.c);
    ico_mutex_destroy(s_loc.m);
    memset(&s_loc, 0, sizeof(s_loc));
}

static RdThreadMode s_want;

void rd_thread_local_want(RdThreadMode mode)
{
    s_want = mode;
}

void rd__thread_local_resume(void)
{
    if (s_want != RD_THREAD_OFF && !g_rd.windowed && g_rd.hasDevice && s_loc.th == NULL) {
        (void)rd_thread_local_start(s_want);
    }
}

void rd__thread_local_pause(void)
{
    rd_thread_local_stop();
}
