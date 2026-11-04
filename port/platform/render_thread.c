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
       their whole time, their typical times (pace_policy.h PaceRtTyp: a
       decaying mean and half its deviation); the render thread writes
       them, the game thread reads them, no lock */
    Uint64 readsEst, pictureEst;
    /* and the present call's wait of those pictures (its recent longest,
       pace_rt_estimate: PACE_RT_BEHIND's) */
    Uint64 presentEst;
    /* and their reads less the waits inside them (the fence, the submit
       thread, the acquire), their interpolation and the submit thread's
       job, typical times too: the game thread predicts a further picture's
       reads by them (pace_policy.h PaceRtPipe) */
    Uint64 cpuEst, interpEst, submitEst;
    /* the reads' mean and deviation, for the 10 s line */
    Uint64 readsMean, readsDev;
    /* the estimates' state, the render thread's alone: reads, whole job,
       own reads, interpolation, the submit thread's job */
    PaceRtTyp readsTyp, pictureTyp, cpuTyp, interpTyp, submitTyp;
    /* the further presents the game thread held back by them in the 10 s
       block, by why (pace_policy.h PaceRtHold; the game thread's) */
    unsigned held, heldMiss, heldOverrun, heldBehind;
    /* the step rule's asks in the block and their working (pace_policy.h
       PaceRtTraceSum; the game thread's) */
    PaceRtTraceSum asks;
    /* the game thread's steps in the block (ico_render_thread_note_step):
       those whose first renderer call waited for a picture's reads and the
       longest wait, those that missed their period and the latest, the
       steps planned at the whole period after a miss, the step's own time
       as last planned */
    unsigned lateStarts, misses, backoffSteps;
    unsigned pictureMisses; /* of the misses, the pictures' (the rest the game's own) */
    /* of the pictures': under the floor (nothing changed), the margin
       widened, the third in the window (the whole period) */
    unsigned smallMisses, widens, fallbacks;
    Uint64 lateStartMaxNs, missMaxNs, stepPlanNs;
    /* the plan's parts as last noted: a step between frames, the margin's
       widening; the steps left out of the typical times in the block */
    Uint64 stepPlanBetweenNs, stepWidenNs;
    unsigned leftOut;
    /* the closing step's mean and deviation as last noted */
    Uint64 stepCloseMean, stepCloseDev;
} s_rt;

/* the cores (cpu_place.h): their speeds, read once at the first start, the
   slowest ones and the faster ones; whether the render and submit threads
   keep to the faster ones (Android, [dev] big_cores) and why not, and the
   cores each keeps to (cpu_place_split: the render thread alone on a
   single fastest core, the submit thread on the next ones; else both on
   the faster ones); the
   cores the game thread and the render thread ran on (one sample a step,
   one a picture: atomic bins, no lock), written before the threads start */
static struct {
    bool read;
    CpuClasses classes;
    uint64_t slowMask, fastMask, renderMask, submitMask;
    bool pin, split;
    CpuPlan plan;
    bool pinSubmit; /* the submit thread is kept too (not with two or fewer faster cores) */
    const char *noPin;
    CpuPlaceHist game, render;
} s_place;

/* the game thread's own pin when the rule keeps it (two faster cores: it
   and the render thread take them), the cores read back, one line; the
   start line says why a phone's threads are where they are */
static void place_game_pin(void)
{
    const CpuPlan *p = &s_place.plan;
    char fast[96], got[96], why[128], gotText[96];

    if (!s_place.pin || p->kind == CPU_PLAN_NONE) {
        return;
    }
    (void)cpu_place_mask_text(p->kind == CPU_PLAN_TWO_FAST ? p->game : p->render, fast,
                              sizeof(fast));
    if (p->kind == CPU_PLAN_MANY_FAST) {
        fprintf(stderr,
                "window: big cores: %d fast of %d: render and submit threads on them, "
                "the game thread free\n",
                p->fastCount, p->coreCount);
    } else if (p->kind == CPU_PLAN_ONE_FAST) {
        fprintf(stderr,
                "window: big cores: 1 fast of %d: the render thread alone on it (%s), "
                "the game and the submit thread free\n",
                p->coreCount, fast);
    } else {
        uint64_t read = 0;
        const CpuPinResult res = cpu_place_pin_checked(p->game, &read, why, sizeof(why));

        (void)cpu_place_mask_text(read, gotText, sizeof(gotText));
        snprintf(got, sizeof(got), "%s",
                 res == CPU_PIN_KEPT      ? "kept"
                 : res == CPU_PIN_UNKNOWN ? "kept (the cores could not be read back)"
                                          : "NOT kept");
        fprintf(stderr,
                "window: big cores: 2 fast of %d: game and render threads on them (%s), the "
                "submit thread free; the game thread %s%s%s\n",
                p->coreCount, fast, got,
                res == CPU_PIN_OTHER     ? ": the system keeps it to cores "
                : res == CPU_PIN_REFUSED ? ": "
                                         : "",
                res == CPU_PIN_OTHER ? gotText : (res == CPU_PIN_REFUSED ? why : ""));
    }
}

/* the cores' speeds and the placement rule, once (the game thread, before
   the render thread starts) */
static void place_init(void)
{
    char cls[384], allowed[96], pr[64], rm[96], sm[96], split[256];

    if (s_place.read) {
        return;
    }
    s_place.read = true;
    (void)cpu_place_classes_read(NULL, &s_place.classes);
    s_place.slowMask = cpu_place_slow_mask(&s_place.classes);
    s_place.fastMask = cpu_place_fast_mask(&s_place.classes);
    cpu_place_plan(&s_place.classes, &s_place.plan);
    s_place.split = s_place.plan.split;
    s_place.renderMask = s_place.plan.render;
    s_place.submitMask = s_place.plan.submit;
#if defined(__ANDROID__)
    if (!ico_config_get_bool("dev.big_cores", 1)) {
        s_place.noPin = "[dev] big_cores = false";
    } else if (s_place.fastMask == 0) {
        s_place.noPin = "the cores are of one speed, or their speeds are not known";
    } else {
        s_place.pin = true;
        s_place.pinSubmit = s_place.plan.submit != 0;
    }
#else
    s_place.noPin = "only on Android";
#endif
    (void)cpu_place_classes_text(&s_place.classes, cls, sizeof(cls));
    (void)cpu_place_mask_text(cpu_place_allowed(), allowed, sizeof(allowed));
    (void)cpu_place_priority_text(pr, sizeof(pr));
    (void)cpu_place_mask_text(s_place.renderMask, rm, sizeof(rm));
    (void)cpu_place_mask_text(s_place.submitMask, sm, sizeof(sm));
    split[0] = '\0';
    if (s_place.pin && s_place.plan.kind != CPU_PLAN_MANY_FAST) {
        snprintf(split, sizeof(split),
                 s_place.plan.kind == CPU_PLAN_TWO_FAST
                     ? "; the game and the render thread to the two faster cores %s, the submit "
                       "thread left to the system"
                     : "; the render thread to the one faster core %s alone, the game and the "
                       "submit thread left to the system",
                 rm);
    } else if (s_place.pin) {
        snprintf(split, sizeof(split),
                 s_place.split ? "; the render thread to the fastest core %s alone, the submit "
                                 "thread to the next faster cores %s, the game thread left to "
                                 "the system"
                               : "; the render and the submit thread to the faster cores %s%s, "
                                 "the game thread left to the system",
                 rm, s_place.split ? sm : "");
    }
    /* the game thread's own priority is not changed: said as it is */
    fprintf(stderr, "window: cores: %s; the game thread %s%s%s, may run on cores %s%s\n", cls,
            pr[0] ? "at " : "at its priority as made", pr, pr[0] ? " (not changed)" : "",
            cpu_place_allowed() != 0 ? allowed : "not known", split);
    place_game_pin();
}

/* a thread the pictures wait on (the render thread, the submit thread), at
   its start: the high priority asked and what the system made of it, and
   with the rule on the faster cores; one line */
static void place_thread_begin(const char *who, bool render)
{
    const bool granted = SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    const uint64_t mask = render ? s_place.renderMask : s_place.submitMask;
    /* which cores (cpu_place_split): the fastest alone, the next ones, or
       the faster ones */
    const bool mine = render || s_place.pinSubmit;
    const char *what = s_place.plan.kind == CPU_PLAN_TWO_FAST   ? "the two faster cores"
                       : s_place.plan.kind == CPU_PLAN_ONE_FAST ? "the one faster core"
                       : !s_place.split                         ? "the faster cores"
                       : render                                 ? "the fastest core"
                                                                : "the next faster cores";
    char err[160], pr[64], fast[96], cores[160], allowed[96], why[128], place[768];

    snprintf(err, sizeof(err), "%s", granted ? "" : SDL_GetError());
    (void)cpu_place_priority_text(pr, sizeof(pr));
    (void)cpu_place_mask_text(mask, fast, sizeof(fast));
    snprintf(cores, sizeof(cores), "%s %s%s", what, fast,
             (s_place.split || s_place.plan.kind == CPU_PLAN_ONE_FAST) && render ? " alone" : "");
    if (s_place.pin && mine) {
        /* the cores read back after the call, not the call's answer alone:
           a phone's kernel may say yes and keep the thread elsewhere
           (cpu_place.h CpuPinResult) */
        uint64_t got = 0;
        const CpuPinResult res = cpu_place_pin_checked(mask, &got, why, sizeof(why));
        char gotText[96], fb[384];

        (void)cpu_place_mask_text(got, gotText, sizeof(gotText));
        fb[0] = '\0';
        if (res != CPU_PIN_KEPT && res != CPU_PIN_UNKNOWN && s_place.split && render &&
            s_place.fastMask != mask) {
            /* the fastest core alone refused: the faster cores, as with
               no split */
            uint64_t got2 = 0;
            char why2[128], fast2[96], got2Text[96];
            const CpuPinResult res2 =
                cpu_place_pin_checked(s_place.fastMask, &got2, why2, sizeof(why2));

            (void)cpu_place_mask_text(s_place.fastMask, fast2, sizeof(fast2));
            (void)cpu_place_mask_text(got2, got2Text, sizeof(got2Text));
            snprintf(fb, sizeof(fb), "; kept to the faster cores %s instead%s%s%s", fast2,
                     res2 == CPU_PIN_KEPT    ? ""
                     : res2 == CPU_PIN_OTHER ? " (the system keeps it to cores "
                                             : " (",
                     res2 == CPU_PIN_KEPT    ? ""
                     : res2 == CPU_PIN_OTHER ? got2Text
                                             : (res2 == CPU_PIN_REFUSED ? why2 : "not read back"),
                     res2 == CPU_PIN_KEPT ? "" : ")");
        }
        if (res == CPU_PIN_KEPT || res == CPU_PIN_UNKNOWN) {
            snprintf(place, sizeof(place),
                     "kept to %s%s ([dev] big_cores = false leaves the placement to the system)",
                     cores, res == CPU_PIN_UNKNOWN ? " (the cores could not be read back)" : "");
        } else if (res == CPU_PIN_OTHER) {
            snprintf(place, sizeof(place),
                     "could not be kept to %s: the system said yes and keeps it to cores %s%s",
                     cores, gotText, fb);
        } else {
            snprintf(place, sizeof(place), "could not be kept to %s (%s)%s", cores, why, fb);
        }
    } else {
        snprintf(place, sizeof(place), "placed by the system (%s)",
                 s_place.noPin != NULL ? s_place.noPin
                 : !mine               ? "the game and render threads have the two faster cores"
                                       : "no rule");
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
    place_thread_begin("render", true);
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
                pace_rt_typ_note(&s_rt.submitTyp, sub);
                __atomic_store_n(&s_rt.submitEst, pace_rt_typ_ns(&s_rt.submitTyp),
                                 __ATOMIC_RELAXED);
            }

            if (pres != 0) {
                /* how long its present waited: the display or the GPU
                   behind (pace_rt_further_hold) */
                const Uint64 e = __atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.presentEst, pace_rt_estimate(e, pres), __ATOMIC_RELAXED);
            }
            if (reads != 0) {
                /* the typical times, not the longest (pace_policy.h
                   PaceRtTyp): the reads of its own less the waits (at
                   least 1 ns, 0 is "not known") */
                const uint64_t own = reads > waits ? reads - waits : 1u;

                pace_rt_typ_note(&s_rt.readsTyp, reads);
                pace_rt_typ_note(&s_rt.pictureTyp, t1 - t0);
                pace_rt_typ_note(&s_rt.cpuTyp, own);
                pace_rt_typ_note(&s_rt.interpTyp, interp < own ? interp : own);
                __atomic_store_n(&s_rt.readsEst, pace_rt_typ_ns(&s_rt.readsTyp), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.pictureEst, pace_rt_typ_ns(&s_rt.pictureTyp),
                                 __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.cpuEst, pace_rt_typ_ns(&s_rt.cpuTyp), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.interpEst, pace_rt_typ_ns(&s_rt.interpTyp),
                                 __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.readsMean, s_rt.readsTyp.meanNs, __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.readsDev, pace_rt_typ_dev_ns(&s_rt.readsTyp),
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
    place_thread_begin("submit", false); /* the render thread's next picture waits on it */
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
        s_rt.readsMean = s_rt.readsDev = 0;
        memset(&s_rt.readsTyp, 0, sizeof(s_rt.readsTyp));
        memset(&s_rt.pictureTyp, 0, sizeof(s_rt.pictureTyp));
        memset(&s_rt.cpuTyp, 0, sizeof(s_rt.cpuTyp));
        memset(&s_rt.interpTyp, 0, sizeof(s_rt.interpTyp));
        memset(&s_rt.submitTyp, 0, sizeof(s_rt.submitTyp));
        s_rt.stepCloseMean = s_rt.stepCloseDev = 0;
        s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
        s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = s_rt.pictureMisses = 0;
        s_rt.smallMisses = s_rt.widens = s_rt.fallbacks = 0;
        s_rt.lateStartMaxNs = s_rt.missMaxNs = s_rt.stepPlanNs = 0;
        s_rt.stepPlanBetweenNs = s_rt.stepWidenNs = 0;
        s_rt.leftOut = 0;
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
    /* the submit thread's job is before the next picture only while the
       frame slots are not apart (VKSLOTS: with them apart the next picture
       records while the one before is ended and submitted) */
    out->submitOn = rd_thread_submit_on();
    out->submitNs = rd_thread_submit_on() && !rd_thread_submit_slots_apart()
                        ? __atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED)
                        : 0;
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

void ico_render_thread_note_ask(const struct PaceRtAsk *q, const struct PaceRtTrace *t, int why)
{
    pace_rt_trace_sum_add(&s_rt.asks, q, t, (PaceRtHold)why);
    if (why == PACE_RT_MISS && (s_rt.asks.held <= 8u || s_rt.asks.held % 100u == 0)) {
        char line[1536];

        (void)pace_rt_trace_text(q, t, s_rt.asks.held, line, sizeof(line));
        fprintf(stderr, "window: step rule: %s\n", line);
    }
}

void ico_render_thread_note_step(uint64_t readWaitNs, bool missed, uint64_t missNs, bool backedOff,
                                 uint64_t planCloseNs, uint64_t planBetweenNs, uint64_t widenNs,
                                 uint64_t closeMeanNs, uint64_t closeDevNs, bool leftOut, int act)
{
    const bool byPictures = act != PACE_STEP_NONE;

    if (readWaitNs != 0) {
        s_rt.lateStarts++;
        s_rt.lateStartMaxNs = readWaitNs > s_rt.lateStartMaxNs ? readWaitNs : s_rt.lateStartMaxNs;
    }
    if (missed) {
        s_rt.misses++;
        s_rt.pictureMisses += byPictures ? 1u : 0u;
        s_rt.smallMisses += act == PACE_STEP_SMALL ? 1u : 0u;
        s_rt.widens += act == PACE_STEP_WIDEN ? 1u : 0u;
        s_rt.fallbacks += act == PACE_STEP_STRICT ? 1u : 0u;
        s_rt.missMaxNs = missNs > s_rt.missMaxNs ? missNs : s_rt.missMaxNs;
    }
    s_rt.backoffSteps += backedOff ? 1u : 0u;
    s_rt.stepPlanNs = planCloseNs;
    s_rt.stepPlanBetweenNs = planBetweenNs;
    s_rt.stepWidenNs = widenNs;
    s_rt.stepCloseMean = closeMeanNs;
    s_rt.stepCloseDev = closeDevNs;
    s_rt.leftOut += leftOut ? 1u : 0u;
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
    const unsigned pictureMisses = s_rt.pictureMisses;
    const unsigned smallMisses = s_rt.smallMisses, widens = s_rt.widens;
    const unsigned fallbacks = s_rt.fallbacks;
    const unsigned backoffSteps = s_rt.backoffSteps;
    const unsigned leftOut = s_rt.leftOut;
    const double lateStartMaxMs = (double)s_rt.lateStartMaxNs / 1e6;
    const double missMaxMs = (double)s_rt.missMaxNs / 1e6;
    RdThreadBlock tb;
    RdSubmitBlock sb;
    unsigned closes;
    double closeMs, closeMaxMs;
    char rt[1536], st[384], cores[320];
    CpuPlaceHist gc, rc, sc;

    s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
    s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = s_rt.pictureMisses = 0;
    s_rt.smallMisses = s_rt.widens = s_rt.fallbacks = 0;
    s_rt.lateStartMaxNs = s_rt.missMaxNs = 0;
    s_rt.leftOut = 0;

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
                 "typically (its mean %.1f ms and half its deviation of %.1f ms; %.1f ms of its "
                 "own, %.1f ms of it the interpolation; the rest its waits for the GPU and the "
                 "submit thread), takes %.1f ms, "
                 "its present waits %.1f ms at the longest lately, the submit thread's part %.1f "
                 "ms; the steps planned at %.1f ms closing a frame (its mean %.1f ms, its "
                 "deviation %.1f ms) and %.1f ms between frames (their typical time, %.1f "
                 "ms of it the margin widened after a miss; %u long steps of the game's own left "
                 "out)); steps started late by the pictures: %u (longest %.1f ms); steps "
                 "that missed their period: %u (%u the pictures': %u under %.0f ms, %u widened "
                 "the margin, %u the third in %u steps; %u the game's own; latest %.1f ms past "
                 "it; %u steps planned at the whole period after the pictures')",
                 pictureCpuMs, b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures,
                 s_rt.mode == FM_MODE_SYNC ? ", sync" : "", tb.interpMs, tb.walkMs, tb.submitMs,
                 tb.waitMs, tb.retired, held, heldMiss, heldOverrun, heldBehind,
                 (double)__atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.readsMean, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.readsDev, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED) / 1e6,
                 (double)s_rt.stepPlanNs / 1e6, (double)s_rt.stepCloseMean / 1e6,
                 (double)s_rt.stepCloseDev / 1e6, (double)s_rt.stepPlanBetweenNs / 1e6,
                 (double)s_rt.stepWidenNs / 1e6, leftOut, lateStarts, lateStartMaxMs, misses,
                 pictureMisses, smallMisses, (double)PACE_STEP_MISS_FLOOR_NS / 1e6, widens,
                 fallbacks, PACE_STEP_MISS_WINDOW, misses - pictureMisses, missMaxMs, backoffSteps);
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
    /* the read waits by what the render thread was doing as each began,
       the frame slots it waited for outside the hand-over and the
       display's waits it made there (rd_thread.h) */
    char rw[1024];

    rd_thread_read_waits_text(&tb, rw, sizeof(rw));
    fprintf(stderr,
            "window: game thread: step %.1f ms (closing a frame %.1f ms of it, longest %.1f ms, "
            "%u frames), pump %.1f ms, slot waits %u (%.1f ms; %u (%.1f ms) by a new frame's "
            "present, %u (%.1f ms) by a further present's, %u other), read waits %u (%.1f ms, "
            "longest %.1f ms%s); %s%s; %s\n",
            stepMs, closeMs, closeMaxMs, closes, pumpMs, waits, (double)waitNs / 1e6, tb.freshWaits,
            tb.freshWaitMs, tb.furtherWaits, tb.furtherWaitMs,
            waits > byPresents ? waits - byPresents : 0u, tb.readWaits, tb.readWaitMs,
            tb.readWaitMaxMs, rw, rt, st, cores);
    if (s_rt.asks.asks != 0) {
        /* the step rule's asks in the block: which test held them, by how
           much, what set the reads' end, the inputs' means */
        char ask[1536];

        (void)pace_rt_trace_sum_text(&s_rt.asks, 10.0, ask, sizeof(ask));
        fprintf(stderr, "window: step rule: %s\n", ask);
    }
    memset(&s_rt.asks, 0, sizeof(s_rt.asks));
}
