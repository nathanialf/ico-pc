/*
 * port/platform/render_thread.c
 *
 * The window's render thread (render_thread.h): the submit and the present
 * of the pictures the game thread records, under frame_mailbox.h's rules.
 */
#include "render_thread.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "diag_host.h"
#include "rd_submit.h"
#include "rhi.h"

static struct {
    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_Condition *cond; /* any change of the mailbox: both sides wait on it */
    SDL_ThreadID tid;    /* the render thread's */
    FrameMailbox mb;
    FmMode mode;
    /* a job handed over and not finished (the RHI guard's busy); written
       under the lock, read without it */
    int busy;
    /* the heartbeat without the lock (diag_host.c's watchdog thread): the
       picture in progress, since when (0: none), the last finished */
    unsigned long long beatSeq, beatDone;
    Uint64 beatSince;
    /* the game thread's waits for the render thread in the 10 s block */
    unsigned slotWaits;
    Uint64 slotWaitNs;
} s_rt;

static int SDLCALL rt_main(void *u)
{
    (void)u;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH)) {
        /* a phone's scheduler may still move it to a little core */
        fprintf(stderr, "window: render thread: the high priority was refused (%s)\n",
                SDL_GetError());
    }
    SDL_LockMutex(s_rt.lock);
    for (;;) {
        FmJob job;
        const FmNext next = fm_next(&s_rt.mb, &job, SDL_GetTicksNS());

        if (next == FM_NEXT_QUIT) {
            break;
        }
        if (next != FM_NEXT_JOB) {
            /* idle or parked: said to a game thread waiting for either */
            SDL_BroadcastCondition(s_rt.cond);
            SDL_WaitCondition(s_rt.cond, s_rt.lock);
            continue;
        }
        const Uint64 t0 = SDL_GetTicksNS();

        __atomic_store_n(&s_rt.beatSeq, (unsigned long long)job.seq, __ATOMIC_RELAXED);
        __atomic_store_n(&s_rt.beatSince, t0 != 0 ? t0 : 1, __ATOMIC_RELEASE);
        SDL_UnlockMutex(s_rt.lock);
        job.fn(job.arg);
        const Uint64 t1 = SDL_GetTicksNS();
        FmPresent p;

        memset(&p, 0, sizeof(p));
        if (job.kind == FM_JOB_PICTURE) {
            p.t0 = t0;
            p.t1 = t1;
            p.costNs = t1 - t0;
            p.shown = rhi_present_shown();
        }
        SDL_LockMutex(s_rt.lock);
        if (job.kind == FM_JOB_PICTURE) {
            fm_present_note(&s_rt.mb, &p);
        }
        fm_finish(&s_rt.mb, &job, t1);
        __atomic_store_n(&s_rt.beatSince, (Uint64)0, __ATOMIC_RELAXED);
        __atomic_store_n(&s_rt.beatDone, (unsigned long long)job.seq, __ATOMIC_RELEASE);
        if (fm_idle(&s_rt.mb)) {
            /* what the job wrote is the game thread's once it reads this */
            __atomic_store_n(&s_rt.busy, 0, __ATOMIC_RELEASE);
        }
        SDL_BroadcastCondition(s_rt.cond);
    }
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    return 0;
}

/* the RHI guard (rhi.h rhi_set_call_guard): busy on any thread but this one */
static bool rt_busy(void *u)
{
    (void)u;
    return __atomic_load_n(&s_rt.busy, __ATOMIC_ACQUIRE) != 0 &&
           SDL_GetCurrentThreadID() != s_rt.tid;
}

uint64_t ico_render_thread_drain(void)
{
    if (s_rt.thread == NULL || !rt_busy(NULL)) {
        return 0;
    }
    const Uint64 t0 = SDL_GetTicksNS();

    SDL_LockMutex(s_rt.lock);
    while (!fm_idle(&s_rt.mb)) {
        SDL_WaitCondition(s_rt.cond, s_rt.lock);
    }
    SDL_UnlockMutex(s_rt.lock);
    const Uint64 waited = SDL_GetTicksNS() - t0;

    __atomic_fetch_add(&s_rt.slotWaits, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_rt.slotWaitNs, waited, __ATOMIC_RELAXED);
    return waited;
}

static void rt_wait(void *u)
{
    (void)u;
    (void)ico_render_thread_drain();
}

bool ico_render_thread_post(void (*fn)(void *arg), void *arg, FmJobKind kind, bool wait)
{
    if (s_rt.thread == NULL || fn == NULL || SDL_GetCurrentThreadID() == s_rt.tid) {
        return false;
    }
    SDL_LockMutex(s_rt.lock);
    if (fm_idle(&s_rt.mb)) {
        /* the counters as the hand-over leaves them (rhi_get_stats while
           the job runs); this thread alone is in the RHI now */
        rhi_call_guard_snapshot();
    }
    const uint64_t seq = fm_post(&s_rt.mb, fn, arg, kind);

    if (seq == 0) {
        SDL_UnlockMutex(s_rt.lock);
        return false;
    }
    __atomic_store_n(&s_rt.busy, 1, __ATOMIC_RELEASE);
    SDL_BroadcastCondition(s_rt.cond);
    if (wait) {
        while (!fm_done_up_to(&s_rt.mb, seq)) {
            SDL_WaitCondition(s_rt.cond, s_rt.lock);
        }
    }
    SDL_UnlockMutex(s_rt.lock);
    return true;
}

/* rd_submit.h RdSubmitHost: a replay's submit and present */
static bool rt_submit_post(void *u, void (*job)(void *arg), void *arg, bool wait)
{
    (void)u;
    return ico_render_thread_post(job, arg, FM_JOB_PICTURE, wait || s_rt.mode == FM_MODE_SYNC);
}

static uint64_t rt_submit_drain(void *u)
{
    (void)u;
    return ico_render_thread_drain();
}

/* diag_host.h ico_diag_set_render_heartbeat: no lock (the watchdog's thread) */
static int rt_heartbeat(unsigned long long *seq, unsigned long long *done)
{
    const Uint64 since = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);

    *seq = since != 0 ? __atomic_load_n(&s_rt.beatSeq, __ATOMIC_RELAXED) : 0;
    *done = __atomic_load_n(&s_rt.beatDone, __ATOMIC_ACQUIRE);
    return 1;
}

FmMode ico_render_thread_start(void)
{
    static const RhiCallGuard guard = {NULL, rt_busy, rt_wait};
    static const RdSubmitHost host = {NULL, rt_submit_post, rt_submit_drain};
    const RhiLimits *l = rhi_limits();
    const char *why = "";
    FmMode mode;

    if (s_rt.thread != NULL) {
        return s_rt.mode;
    }
    mode = fm_mode_choose(ico_config_get_string("dev.render_thread", "auto"),
                          l == NULL || l->singleThreadCommands, true, &why);
    if (mode != FM_MODE_OFF) {
        s_rt.lock = SDL_CreateMutex();
        s_rt.cond = SDL_CreateCondition();
        fm_init(&s_rt.mb, SDL_GetTicksNS());
        s_rt.busy = 0;
        s_rt.beatSeq = s_rt.beatDone = 0;
        s_rt.beatSince = 0;
        s_rt.slotWaits = 0;
        s_rt.slotWaitNs = 0;
        s_rt.thread = s_rt.lock != NULL && s_rt.cond != NULL
                          ? SDL_CreateThread(rt_main, "ico render", NULL)
                          : NULL;
        if (s_rt.thread == NULL) {
            fprintf(stderr, "window: render thread: off (the thread could not be made: %s)\n",
                    SDL_GetError());
            if (s_rt.cond != NULL) {
                SDL_DestroyCondition(s_rt.cond);
            }
            if (s_rt.lock != NULL) {
                SDL_DestroyMutex(s_rt.lock);
            }
            s_rt.cond = NULL;
            s_rt.lock = NULL;
            s_rt.mode = FM_MODE_OFF;
            return FM_MODE_OFF;
        }
        s_rt.tid = SDL_GetThreadID(s_rt.thread);
        s_rt.mode = mode;
        rhi_set_call_guard(&guard);
        rd_set_submit_host(&host);
        ico_diag_set_render_heartbeat(rt_heartbeat);
    }
    s_rt.mode = mode;
    if (mode == FM_MODE_ON) {
        fprintf(stderr,
                "window: render thread: on (submit and present off the game thread; %s, %s)\n",
                rhi_backend_kind_name(rhi_backend()), why);
    } else if (mode == FM_MODE_SYNC) {
        fprintf(stderr,
                "window: render thread: sync (submit and present on the render thread, %s)\n", why);
    } else {
        fprintf(stderr, "window: render thread: off (%s)\n", why);
    }
    return mode;
}

void ico_render_thread_stop(void)
{
    if (s_rt.thread == NULL) {
        return;
    }
    (void)ico_render_thread_drain();
    /* nothing more handed over: the renderer and the RHI back on this
       thread alone */
    rd_set_submit_host(NULL);
    rhi_set_call_guard(NULL);
    ico_diag_set_render_heartbeat(NULL);
    SDL_LockMutex(s_rt.lock);
    fm_quit(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    SDL_WaitThread(s_rt.thread, NULL);
    s_rt.thread = NULL;
    SDL_DestroyCondition(s_rt.cond);
    SDL_DestroyMutex(s_rt.lock);
    s_rt.cond = NULL;
    s_rt.lock = NULL;
    s_rt.mode = FM_MODE_OFF;
}

FmMode ico_render_thread_mode(void)
{
    return s_rt.thread != NULL ? s_rt.mode : FM_MODE_OFF;
}

void ico_render_thread_park(void)
{
    static unsigned s_lines;

    if (s_rt.thread == NULL) {
        return;
    }
    SDL_LockMutex(s_rt.lock);
    fm_park(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    while (!fm_is_parked(&s_rt.mb)) {
        SDL_WaitCondition(s_rt.cond, s_rt.lock);
    }
    SDL_UnlockMutex(s_rt.lock);
    if (s_lines++ < 16) {
        fprintf(stderr, "window: render thread: parked (the pictures are submitted and presented "
                        "on the game thread until it goes on)\n");
    }
}

void ico_render_thread_unpark(void)
{
    static unsigned s_lines;

    if (s_rt.thread == NULL) {
        return;
    }
    SDL_LockMutex(s_rt.lock);
    fm_unpark(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    if (s_lines++ < 16) {
        fprintf(stderr, "window: render thread: goes on\n");
    }
}

bool ico_render_thread_last_present(FmPresent *out)
{
    bool have;

    if (s_rt.thread == NULL) {
        return false;
    }
    SDL_LockMutex(s_rt.lock);
    have = fm_last_present(&s_rt.mb, out);
    SDL_UnlockMutex(s_rt.lock);
    return have;
}

void ico_render_thread_block_line(double stepMs, double recordMs, double pumpMs)
{
    const unsigned waits = __atomic_exchange_n(&s_rt.slotWaits, 0u, __ATOMIC_RELAXED);
    const Uint64 waitNs = __atomic_exchange_n(&s_rt.slotWaitNs, (Uint64)0, __ATOMIC_RELAXED);
    char rt[192];

    if (s_rt.thread != NULL) {
        FmBlock b;

        SDL_LockMutex(s_rt.lock);
        fm_block_take(&s_rt.mb, SDL_GetTicksNS(), &b);
        SDL_UnlockMutex(s_rt.lock);
        snprintf(rt, sizeof(rt),
                 "render thread: idle %.0f %%, waited for a frame %.1f ms, heartbeat max %.1f ms "
                 "(%u pictures%s)",
                 b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures,
                 s_rt.mode == FM_MODE_SYNC ? ", sync" : "");
    } else {
        snprintf(rt, sizeof(rt), "render thread: off");
    }
    fprintf(stderr,
            "window: game thread: step %.1f ms, record %.1f ms, pump %.1f ms, slot waits %u "
            "(%.1f ms); %s\n",
            stepMs, recordMs, pumpMs, waits, (double)waitNs / 1e6, rt);
}
