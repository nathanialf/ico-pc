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
#include "cpu_place.h"
#include "diag_host.h"
#include "pace_policy.h"
#include "rd.h"
#include "rd_submit_thread.h"
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
    /* and the present call's wait of those pictures (the same estimate) */
    Uint64 presentEst;
    /* and their reads less the waits inside them (the fence, the submit
       thread, the acquire), their interpolation and the submit thread's
       job: the game thread predicts a further picture's reads by them
       (pace_policy.h PaceRtPipe) */
    Uint64 cpuEst, interpEst, submitEst;
    /* the further presents the game thread held back by them in the 10 s
       block, by why (pace_policy.h PaceRtHold; the game thread's) */
    unsigned held, heldMiss, heldOverrun, heldBehind;
    /* the game thread's steps in the block (ico_render_thread_note_step):
       those whose first renderer call waited for a picture's reads and the
       longest wait, those that missed their period and the latest, the
       steps planned at the whole period after a miss, the step's own time
       as last planned */
    unsigned lateStarts, misses, backoffSteps;
    Uint64 lateStartMaxNs, missMaxNs, stepPlanNs;
} s_rt;

/* the cores (cpu_place.h): their speeds, read once at the first start, the
   slowest ones and the faster ones; whether the render and submit threads
   keep to the faster ones (Android, [dev] big_cores) and why not; the
   cores the game thread and the render thread ran on (one sample a step,
   one a picture: atomic bins, no lock), written before the threads start */
static struct {
    bool read;
    CpuClasses classes;
    uint64_t slowMask, fastMask;
    bool pin;
    const char *noPin;
    CpuPlaceHist game, render;
} s_place;

/* the cores' speeds and the placement rule, once (the game thread, before
   the render thread starts) */
static void place_init(void)
{
    char cls[384], allowed[96], pr[64];

    if (s_place.read) {
        return;
    }
    s_place.read = true;
    (void)cpu_place_classes_read(NULL, &s_place.classes);
    s_place.slowMask = cpu_place_slow_mask(&s_place.classes);
    s_place.fastMask = cpu_place_fast_mask(&s_place.classes);
#if defined(__ANDROID__)
    if (!ico_config_get_bool("dev.big_cores", 1)) {
        s_place.noPin = "[dev] big_cores = false";
    } else if (s_place.fastMask == 0) {
        s_place.noPin = "the cores are of one speed, or their speeds are not known";
    } else {
        s_place.pin = true;
    }
#else
    s_place.noPin = "only on Android";
#endif
    (void)cpu_place_classes_text(&s_place.classes, cls, sizeof(cls));
    (void)cpu_place_mask_text(cpu_place_allowed(), allowed, sizeof(allowed));
    (void)cpu_place_priority_text(pr, sizeof(pr));
    /* the game thread's own priority is not changed: said as it is */
    fprintf(stderr, "window: cores: %s; the game thread %s%s%s, may run on cores %s\n", cls,
            pr[0] ? "at " : "at its priority as made", pr, pr[0] ? " (not changed)" : "",
            cpu_place_allowed() != 0 ? allowed : "not known");
}

/* a thread the pictures wait on (the render thread, the submit thread), at
   its start: the high priority asked and what the system made of it, and
   with the rule on the faster cores; one line */
static void place_thread_begin(const char *who)
{
    const bool granted = SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    char err[160], pr[64], fast[96], allowed[96], why[128], place[320];

    snprintf(err, sizeof(err), "%s", granted ? "" : SDL_GetError());
    (void)cpu_place_priority_text(pr, sizeof(pr));
    (void)cpu_place_mask_text(s_place.fastMask, fast, sizeof(fast));
    if (s_place.pin) {
        if (cpu_place_pin(s_place.fastMask, why, sizeof(why))) {
            snprintf(place, sizeof(place),
                     "kept to the faster cores %s ([dev] big_cores = false leaves the placement "
                     "to the system)",
                     fast);
        } else {
            snprintf(place, sizeof(place), "could not be kept to the faster cores %s (%s)", fast,
                     why);
        }
    } else {
        snprintf(place, sizeof(place), "placed by the system (%s)",
                 s_place.noPin != NULL ? s_place.noPin : "no rule");
    }
    (void)cpu_place_mask_text(cpu_place_allowed(), allowed, sizeof(allowed));
    fprintf(stderr, "window: %s thread: the high priority %s%s%s%s%s; %s; may run on cores %s\n",
            who, granted ? "granted" : "refused (", err, granted ? "" : ")", pr[0] ? ", " : "", pr,
            place, cpu_place_allowed() != 0 ? allowed : "not known");
}

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
    /* a phone's scheduler may still move it to a little core: the 10 s
       line's cores say where it ran */
    place_thread_begin("render");
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
            cpu_place_note(&s_place.render, cpu_place_now()); /* the 10 s line's cores */
        }

        if (job.kind == FM_JOB_PICTURE) {
            /* a picture handed over (rd_thread.h): how long it held the
               renderer's state and how long it took, for the game thread's
               next further presents */
            const uint64_t reads = rd_thread_take_reads_ns();
            const uint64_t pres = rd_thread_take_present_ns();
            const uint64_t sub = rd_thread_submit_take_job_ns();
            uint64_t waits = 0, interp = 0;

            rd_thread_take_reads_parts(&waits, &interp);
            if (sub != 0) {
                /* the submit thread's job finished since (the picture
                   before's, one picture late) */
                const Uint64 e = __atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.submitEst, pace_rt_estimate(e, sub), __ATOMIC_RELAXED);
            }

            if (pres != 0) {
                /* how long its present waited: the display or the GPU
                   behind (pace_rt_further_hold) */
                const Uint64 e = __atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.presentEst, pace_rt_estimate(e, pres), __ATOMIC_RELAXED);
            }
            if (reads != 0) {
                const Uint64 r = __atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED);
                const Uint64 w = __atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.readsEst, pace_rt_estimate(r, reads), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.pictureEst, pace_rt_estimate(w, t1 - t0), __ATOMIC_RELAXED);
                /* the reads of its own: less the waits (at least 1 ns, 0 is
                   "not known") */
                const Uint64 c = __atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED);
                const Uint64 i = __atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED);
                const uint64_t own = reads > waits ? reads - waits : 1u;

                __atomic_store_n(&s_rt.cpuEst, pace_rt_estimate(c, own), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.interpEst, pace_rt_estimate(i, interp < own ? interp : own),
                                 __ATOMIC_RELAXED);
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

/* the render thread's part of the guard: a job runs, or with OpenGL the
   context is not the game thread's */
static bool rt_render_busy(void)
{
    return __atomic_load_n(&s_rt.busy, __ATOMIC_ACQUIRE) != 0 || (s_gl.on && gl_owner() != GL_GAME);
}

/* the RHI guard (rhi.h rhi_set_call_guard): busy on any thread but this one
   while a job runs, and with OpenGL while the context is not that thread's;
   the submit thread's (rd_submit_thread.h) calls never wait, this thread's
   wait for the submit thread's work, every other thread's for both */
static bool rt_busy(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        return rd_thread_submit_busy();
    }
    return rt_render_busy() || rd_thread_submit_busy();
}

/* rhi_get_stats's question: the counters written by another thread now */
static bool rt_stale(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        return rd_thread_submit_stale();
    }
    return rt_busy(u);
}

/* the render thread's jobs finished (the submit thread's may go on) */
static uint64_t rt_drain_render(void)
{
    if (s_rt.thread == NULL || !rt_render_busy()) {
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

uint64_t ico_render_thread_drain(void)
{
    if (s_rt.thread == NULL) {
        return 0;
    }
    /* the render thread idle first: only it posts to the submit thread */
    const uint64_t waited = rt_drain_render();

    return waited + rd_thread_submit_drain();
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
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        (void)rd_thread_submit_drain(); /* its list kept for the submit thread first */
        return;
    }
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
    if (fm_idle(&s_rt.mb) && !rd_thread_submit_stale()) {
        /* the counters as the hand-over leaves them (rhi_get_stats while
           the job runs); this thread alone is in the RHI now (with the
           submit thread at work, the last ones kept) */
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
    return rt_drain_render(); /* rd_thread_drain waits for the submit thread after it */
}

/* the submit thread's first call (rd_submit_thread.h): this thread's priority */
static void rt_submit_begin(void *u)
{
    (void)u;
    place_thread_begin("submit"); /* the render thread's next picture waits on it */
}

/* diag_host.h ico_diag_set_render_heartbeat: no lock (the watchdog's
   thread); with no picture in progress here, the submit thread's job still
   to finish is the last picture's (its submit and present) */
static int rt_heartbeat(unsigned long long *seq, unsigned long long *done)
{
    const Uint64 since = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);

    *seq = since != 0 ? __atomic_load_n(&s_rt.beatSeq, __ATOMIC_RELAXED) : 0;
    *done = __atomic_load_n(&s_rt.beatDone, __ATOMIC_ACQUIRE);
    if (since == 0 && *done != 0 && rd_thread_submit_unfinished()) {
        *seq = *done;
        *done = *seq - 1;
    }
    return 1;
}

FmMode ico_render_thread_start(void)
{
    static const RhiCallGuard guard = {NULL, rt_busy, rt_wait, rt_stale};
    RdThreadHost host = {NULL, rt_host_post, rt_host_drain, false, true, rt_submit_begin};
    const RhiLimits *l = rhi_limits();
    const char *why = "";
    FmMode mode;

    if (s_rt.thread != NULL) {
        return s_rt.mode;
    }
    place_init(); /* before either thread starts: they read the rule */
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
        s_rt.readsEst = s_rt.pictureEst = s_rt.presentEst = 0;
        s_rt.cpuEst = s_rt.interpEst = s_rt.submitEst = 0;
        s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
        s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = 0;
        s_rt.lateStartMaxNs = s_rt.missMaxNs = s_rt.stepPlanNs = 0;
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
    ico_diag_set_render_heartbeat(NULL);
    rd_set_thread_host(NULL); /* the submit thread ended too */
    rhi_set_call_guard(NULL);
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
    /* and the submit thread's last present, before the surface goes */
    (void)rd_thread_submit_drain();
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

bool ico_render_thread_plan(IcoRtPlan *out)
{
    if (s_rt.thread == NULL || s_rt.mode != FM_MODE_ON) {
        return false;
    }
    const Uint64 whole = __atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED);

    if (whole == 0) {
        return false; /* no picture handed over yet */
    }
    /* the picture in progress, if any (the heartbeat's start: pictures only) */
    out->sinceNs = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);
    out->readsNs = __atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED);
    out->pictureNs = whole;
    out->presentNs = __atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED);
    out->cpuNs = __atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED);
    out->interpNs = __atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED);
    out->submitNs = rd_thread_submit_on() ? __atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED) : 0;
    return true;
}

void ico_render_thread_note_held(int why)
{
    s_rt.held++;
    if (why == PACE_RT_OVERRUN) {
        s_rt.heldOverrun++;
    } else if (why == PACE_RT_BEHIND) {
        s_rt.heldBehind++;
    } else {
        s_rt.heldMiss++; /* PACE_RT_MISS */
    }
}

void ico_render_thread_note_step(uint64_t readWaitNs, bool missed, uint64_t missNs, bool backedOff,
                                 uint64_t planNs)
{
    if (readWaitNs != 0) {
        s_rt.lateStarts++;
        s_rt.lateStartMaxNs = readWaitNs > s_rt.lateStartMaxNs ? readWaitNs : s_rt.lateStartMaxNs;
    }
    if (missed) {
        s_rt.misses++;
        s_rt.missMaxNs = missNs > s_rt.missMaxNs ? missNs : s_rt.missMaxNs;
    }
    s_rt.backoffSteps += backedOff ? 1u : 0u;
    s_rt.stepPlanNs = planNs;
}

void ico_render_thread_note_game_core(void)
{
    cpu_place_note(&s_place.game, cpu_place_now());
}

void ico_render_thread_block_line(double stepMs, double pictureCpuMs, double pumpMs)
{
    const unsigned waits = __atomic_exchange_n(&s_rt.slotWaits, 0u, __ATOMIC_RELAXED);
    const Uint64 waitNs = __atomic_exchange_n(&s_rt.slotWaitNs, (Uint64)0, __ATOMIC_RELAXED);
    const unsigned held = s_rt.held, heldMiss = s_rt.heldMiss;
    const unsigned heldOverrun = s_rt.heldOverrun, heldBehind = s_rt.heldBehind;
    const unsigned lateStarts = s_rt.lateStarts, misses = s_rt.misses;
    const unsigned backoffSteps = s_rt.backoffSteps;
    const double lateStartMaxMs = (double)s_rt.lateStartMaxNs / 1e6;
    const double missMaxMs = (double)s_rt.missMaxNs / 1e6;
    RdThreadBlock tb;
    RdSubmitBlock sb;
    unsigned closes;
    double closeMs, closeMaxMs;
    char rt[1280], st[384], cores[320];
    CpuPlaceHist gc, rc, sc;

    s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
    s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = 0;
    s_rt.lateStartMaxNs = s_rt.missMaxNs = 0;

    /* the cores each thread ran on most in the block (cpu_place.h), and
       their share on the slowest cores where the cores differ */
    cpu_place_take(&s_place.game, &gc);
    cpu_place_take(&s_place.render, &rc);
    rd_thread_submit_take_cores(&sc);
    (void)cpu_place_part(cores, sizeof(cores), &gc, &rc, rd_thread_submit_on() ? &sc : NULL,
                         s_place.slowMask);
    rd_thread_block_take(&tb);
    rd_thread_submit_block_take(&sb);
    rd_frame_close_take(&closes, &closeMs, &closeMaxMs);
    /* the submit thread (rd_submit_thread.h): per picture its submit and
       present, the most pictures queued and running at once, the render
       thread's waits for it (its next picture's first graphics call) and
       the others' (the game thread's drains), the lists the render thread
       ended and submitted itself (a call of its own needed the backend
       first) */
    if (rd_thread_submit_on()) {
        snprintf(st, sizeof(st),
                 "; submit thread: %.1f ms a picture (submit %.1f, present %.1f; %u pictures, "
                 "longest %.1f ms), queue depth max %u, the render thread waited for it %u times "
                 "(%.1f ms), the others %u times (%.1f ms, longest %.1f ms), %u submitted on the "
                 "render thread",
                 sb.submitMs + sb.presentMs, sb.submitMs, sb.presentMs, sb.jobs, sb.longestMs,
                 sb.depthMax, sb.renderWaits, sb.renderWaitMs, sb.waits - sb.renderWaits,
                 sb.waitMs - sb.renderWaitMs, sb.waitMaxMs, sb.onRender);
    } else {
        snprintf(st, sizeof(st), "; submit thread: off");
    }
    if (s_rt.thread != NULL) {
        FmBlock b;

        SDL_LockMutex(s_rt.lock);
        fm_block_take(&s_rt.mb, SDL_GetTicksNS(), &b);
        SDL_UnlockMutex(s_rt.lock);
        /* per picture of its own: its CPU time (the window's replays line),
           the interpolation, the uploads and the walk, the submit and the
           present, and the GPU's and the display's waits inside them */
        snprintf(rt, sizeof(rt),
                 "render thread: CPU %.1f ms a picture, idle %.0f %%, waited for a frame %.1f ms, "
                 "heartbeat max %.1f ms (%u pictures%s); interp %.1f ms, walk %.1f ms, submit "
                 "%.1f ms, GPU and display waits %.1f ms a picture; %u textures destroyed for the "
                 "game thread; %u further presents held back (%u would have made the step miss "
                 "its period, %u while a picture ran past its estimate, %u while the presents "
                 "waited for the display or the GPU; a picture holds the renderer for %.1f ms "
                 "(%.1f ms of its own, %.1f ms of it the interpolation; the rest its waits for "
                 "the GPU and the submit thread), takes %.1f ms, "
                 "its present waits %.1f ms, the submit thread's part %.1f ms; the step planned "
                 "at %.1f ms); steps started late by the pictures: %u (longest %.1f ms); steps "
                 "that missed their period: %u (latest %.1f ms past it; %u steps planned at the "
                 "whole period after them)",
                 pictureCpuMs, b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures,
                 s_rt.mode == FM_MODE_SYNC ? ", sync" : "", tb.interpMs, tb.walkMs, tb.submitMs,
                 tb.waitMs, tb.retired, held, heldMiss, heldOverrun, heldBehind,
                 (double)__atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED) / 1e6,
                 (double)s_rt.stepPlanNs / 1e6, lateStarts, lateStartMaxMs, misses, missMaxMs,
                 backoffSteps);
    } else {
        snprintf(rt, sizeof(rt), "render thread: off (CPU %.1f ms a picture on the game thread)",
                 pictureCpuMs);
    }
    /* slot waits: the game thread's waits for everything posted to end
       (ico_render_thread_drain), by whose: a new frame's present (its
       picture before still in its submit and present: the frame's close
       already waited for that picture's reads), a further present's, the
       rest (a graphics call's, a settings change's, a load's); read waits:
       the game thread's calls that changed the renderer's state and waited
       for the render thread's reads of a picture (its interpolation and
       walk; rd_thread.h) */
    const unsigned byPresents = tb.freshWaits + tb.furtherWaits;

    fprintf(stderr,
            "window: game thread: step %.1f ms (closing a frame %.1f ms of it, longest %.1f ms, "
            "%u frames), pump %.1f ms, slot waits %u (%.1f ms; %u (%.1f ms) by a new frame's "
            "present, %u (%.1f ms) by a further present's, %u other), read waits %u (%.1f ms, "
            "longest %.1f ms); %s%s; %s\n",
            stepMs, closeMs, closeMaxMs, closes, pumpMs, waits, (double)waitNs / 1e6, tb.freshWaits,
            tb.freshWaitMs, tb.furtherWaits, tb.furtherWaitMs,
            waits > byPresents ? waits - byPresents : 0u, tb.readWaits, tb.readWaitMs,
            tb.readWaitMaxMs, rt, st, cores);
}
