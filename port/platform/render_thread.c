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
#include "pace_policy.h"
#include "rd_thread.h"
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
    /* the pictures handed over as the game thread plans further presents
       by them (ico_render_thread_plan): their reads (the hand-over) and
       their whole time, estimated (pace_policy.h pace_rt_estimate); the
       render thread writes them, the game thread reads them, no lock */
    Uint64 readsEst, pictureEst;
    /* the further presents the game thread held back by them in the 10 s
       block (the game thread's) */
    unsigned held;
} s_rt;

/* OpenGL's context is current on one thread at a time: the render thread's
   for its jobs; a game-thread RHI call takes it over (the call guard's
   wait, gl_take) and the next job takes it back.  owner: GL_NONE, GL_GAME
   (any thread but the render thread) or GL_RENDER */
enum { GL_NONE = 0, GL_GAME, GL_RENDER };

static struct {
    bool on;
    SDL_Window *win;
    SDL_GLContext ctx;
    int owner;
    unsigned takes, fails;
} s_gl;

static int gl_owner(void)
{
    return __atomic_load_n(&s_gl.owner, __ATOMIC_ACQUIRE);
}

static void gl_set_owner(int owner)
{
    __atomic_store_n(&s_gl.owner, owner, __ATOMIC_RELEASE);
}

/* on the thread that is to make the calls: the context current there */
static void gl_bind(int owner)
{
    if (!SDL_GL_MakeCurrent(s_gl.win, s_gl.ctx)) {
        if (s_gl.fails++ < 8) {
            fprintf(stderr,
                    "window: render thread: the OpenGL context could not be made current "
                    "on the %s thread (%s)\n",
                    owner == GL_RENDER ? "render" : "game", SDL_GetError());
        }
    }
    gl_set_owner(owner);
}

/* the thread that has it lets it go */
static void gl_unbind(void)
{
    (void)SDL_GL_MakeCurrent(s_gl.win, NULL);
    gl_set_owner(GL_NONE);
}

static void gl_release_job(void *u)
{
    (void)u;
    if (gl_owner() == GL_RENDER) {
        gl_unbind();
    }
}

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
            if (next == FM_NEXT_PARKED && s_gl.on && gl_owner() == GL_RENDER) {
                /* the surface is made again on the game thread meanwhile */
                gl_unbind();
            }
            /* idle or parked: said to a game thread waiting for either */
            SDL_BroadcastCondition(s_rt.cond);
            SDL_WaitCondition(s_rt.cond, s_rt.lock);
            continue;
        }
        const Uint64 t0 = SDL_GetTicksNS();

        /* the watchdog's heartbeat counts pictures only: a call between
           pictures (the targets made again, a movie's picture) runs on this
           thread as it ran on the game thread, under the watchdog's own
           limit */
        if (job.kind == FM_JOB_PICTURE) {
            __atomic_store_n(&s_rt.beatSeq, (unsigned long long)job.seq, __ATOMIC_RELAXED);
            __atomic_store_n(&s_rt.beatSince, t0 != 0 ? t0 : 1, __ATOMIC_RELEASE);
        }
        SDL_UnlockMutex(s_rt.lock);
        if (s_gl.on && gl_owner() != GL_RENDER) {
            gl_bind(GL_RENDER); /* the game thread let it go when it posted */
        }
        job.fn(job.arg);
        const Uint64 t1 = SDL_GetTicksNS();
        FmPresent p;

        if (job.kind == FM_JOB_PICTURE) {
            /* a picture handed over (rd_thread.h): how long it held the
               renderer's state and how long it took, for the game thread's
               next further presents */
            const uint64_t reads = rd_thread_take_reads_ns();

            if (reads != 0) {
                const Uint64 r = __atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED);
                const Uint64 w = __atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.readsEst, pace_rt_estimate(r, reads), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.pictureEst, pace_rt_estimate(w, t1 - t0), __ATOMIC_RELAXED);
            }
        }

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

/* the RHI guard (rhi.h rhi_set_call_guard): busy on any thread but this one
   while a job runs, and with OpenGL while the context is not that thread's */
static bool rt_busy(void *u)
{
    (void)u;
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        return false;
    }
    return __atomic_load_n(&s_rt.busy, __ATOMIC_ACQUIRE) != 0 || (s_gl.on && gl_owner() != GL_GAME);
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

/* OpenGL: the context to the calling thread (the render thread idle) */
static void gl_take(void)
{
    if (gl_owner() == GL_RENDER) {
        (void)ico_render_thread_post(gl_release_job, NULL, FM_JOB_CALL, true);
    }
    if (gl_owner() == GL_RENDER) {
        return; /* not taken (parked meanwhile: it let the context go) */
    }
    gl_bind(GL_GAME);
    s_gl.takes++;
    if (s_gl.takes <= 8 || s_gl.takes % 100 == 0) {
        fprintf(stderr,
                "window: render thread: the game thread took the OpenGL context for its own "
                "calls (%u times; the render thread's next picture takes it back)\n",
                s_gl.takes);
    }
}

static void rt_wait(void *u)
{
    (void)u;
    (void)ico_render_thread_drain();
    if (s_gl.on && gl_owner() != GL_GAME) {
        gl_take();
    }
}

bool ico_render_thread_post(void (*fn)(void *arg), void *arg, FmJobKind kind, bool wait)
{
    if (s_rt.thread == NULL || fn == NULL || SDL_GetCurrentThreadID() == s_rt.tid) {
        return false;
    }
    if (s_gl.on && gl_owner() == GL_GAME) {
        gl_unbind(); /* the render thread makes it current for the job */
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

/* rd_thread.h RdThreadHost: the replays, the presents and the renderer's
   calls */
static bool rt_host_post(void *u, void (*fn)(void *arg), void *arg, bool picture, bool wait)
{
    (void)u;
    return ico_render_thread_post(fn, arg, picture ? FM_JOB_PICTURE : FM_JOB_CALL,
                                  wait || s_rt.mode == FM_MODE_SYNC);
}

static uint64_t rt_host_drain(void *u)
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
    RdThreadHost host = {NULL, rt_host_post, rt_host_drain, false};
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
        s_rt.readsEst = s_rt.pictureEst = 0;
        s_rt.held = 0;
        memset(&s_gl, 0, sizeof(s_gl));
        if (rhi_backend() == RHI_BACKEND_OPENGL) {
            /* made current here at rhi_init: the render thread's from now */
            s_gl.win = SDL_GL_GetCurrentWindow();
            s_gl.ctx = SDL_GL_GetCurrentContext();
            s_gl.on = s_gl.win != NULL && s_gl.ctx != NULL;
            s_gl.owner = GL_GAME;
        }
        if (s_rt.lock != NULL && s_rt.cond != NULL) {
            /* the walk's own stack: the main thread's size on every platform */
            const SDL_PropertiesID props = SDL_CreateProperties();

            SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                                   (void *)rt_main);
            SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "ico render");
            SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 8 * 1024 * 1024);
            s_rt.thread = props != 0 ? SDL_CreateThreadWithProperties(props) : NULL;
            if (props != 0) {
                SDL_DestroyProperties(props);
            }
        } else {
            s_rt.thread = NULL;
        }
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
        host.sync = mode == FM_MODE_SYNC; /* every picture waited: none counted as handed over */
        rd_set_thread_host(&host);
        ico_diag_set_render_heartbeat(rt_heartbeat);
    }
    s_rt.mode = mode;
    if (mode == FM_MODE_ON) {
        fprintf(stderr,
                "window: render thread: on (interpolation, walk, submit and present off the game "
                "thread; [dev] render_thread = off keeps them on it; %s, %s)\n",
                rhi_backend_kind_name(rhi_backend()), why);
    } else if (mode == FM_MODE_SYNC) {
        fprintf(stderr,
                "window: render thread: sync (interpolation, walk, submit and present on the "
                "render thread, %s)\n",
                why);
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
    if (s_gl.on && gl_owner() != GL_GAME) {
        gl_take(); /* the context back on this thread for good */
    }
    /* nothing more handed over: the renderer and the RHI back on this
       thread alone (the textures still to retire destroyed here) */
    rd_set_thread_host(NULL);
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
    s_gl.on = false;
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

bool ico_render_thread_plan(uint64_t nowNs, uint64_t *busyLeftNs, uint64_t *readsNs,
                            uint64_t *pictureNs)
{
    if (s_rt.thread == NULL || s_rt.mode != FM_MODE_ON) {
        return false;
    }
    const Uint64 reads = __atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED);
    const Uint64 whole = __atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED);

    if (whole == 0) {
        return false; /* no picture handed over yet */
    }
    /* the picture in progress, if any (the heartbeat's start: pictures only) */
    const Uint64 since = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);

    *busyLeftNs = pace_rt_busy_left(nowNs, since, whole);
    *readsNs = reads;
    *pictureNs = whole;
    return true;
}

void ico_render_thread_note_held(void)
{
    s_rt.held++;
}

void ico_render_thread_block_line(double stepMs, double recordMs, double pumpMs)
{
    const unsigned waits = __atomic_exchange_n(&s_rt.slotWaits, 0u, __ATOMIC_RELAXED);
    const Uint64 waitNs = __atomic_exchange_n(&s_rt.slotWaitNs, (Uint64)0, __ATOMIC_RELAXED);
    const unsigned held = s_rt.held;
    RdThreadBlock tb;
    char rt[480];

    s_rt.held = 0;

    rd_thread_block_take(&tb);
    if (s_rt.thread != NULL) {
        FmBlock b;

        SDL_LockMutex(s_rt.lock);
        fm_block_take(&s_rt.mb, SDL_GetTicksNS(), &b);
        SDL_UnlockMutex(s_rt.lock);
        /* per picture of its own: the interpolation, the uploads and the
           walk, the submit and the present, and the GPU's and the display's
           waits inside them */
        snprintf(rt, sizeof(rt),
                 "render thread: idle %.0f %%, waited for a frame %.1f ms, heartbeat max %.1f ms "
                 "(%u pictures%s); interp %.1f ms, walk %.1f ms, submit %.1f ms, GPU and display "
                 "waits %.1f ms a picture; %u textures destroyed for the game thread; %u further "
                 "presents held back (each would have run into the next step: a picture holds "
                 "the renderer for %.1f ms, takes %.1f ms)",
                 b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures,
                 s_rt.mode == FM_MODE_SYNC ? ", sync" : "", tb.interpMs, tb.walkMs, tb.submitMs,
                 tb.waitMs, tb.retired, held,
                 (double)__atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED) / 1e6);
    } else {
        snprintf(rt, sizeof(rt), "render thread: off");
    }
    /* reads waits: the game thread's calls that changed the renderer's
       state and waited for the render thread's reads of a picture (its
       interpolation and walk; rd_thread.h) */
    fprintf(stderr,
            "window: game thread: step %.1f ms, record %.1f ms, pump %.1f ms, slot waits %u "
            "(%.1f ms), read waits %u (%.1f ms, longest %.1f ms); %s\n",
            stepMs, recordMs, pumpMs, waits, (double)waitNs / 1e6, tb.readWaits, tb.readWaitMs,
            tb.readWaitMaxMs, rt);
}
