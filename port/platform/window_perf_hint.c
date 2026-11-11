/*
 * port/platform/window_perf_hint.c
 *
 * The window's side of Android's performance hints (window_perf_hint.h,
 * perf_hint.h).
 */
#include "window_perf_hint.h"

#ifdef __ANDROID__
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "cpu_place.h"
#include "diag_host.h"
#include "pace_policy.h"
#include "perf_hint.h"
#include "rd.h"
#include "render_thread.h"
#include "rhi.h"
#include "thread_stat.h"
#include "video_options.h"
#include "window_present.h"
#include "android/host_android.h"
#include "android/perf_hint_android.h"

#define s_pres g_windowPres

/* the hand-over (perf_hint.h; the game thread writes, the hint thread
   calls) and the hint thread */
static PerfHint s_h;
static PerfHintAndroidInfo s_info;
static SDL_Thread *s_thread;
static SDL_Semaphore *s_sem;
static int s_stop;    /* atomic: the close asks the thread to end */
static int s_refused; /* atomic: the hint thread's: the session refused, off for the run */
static int s_made;    /* atomic: sessions made (the 10 s line once one was) */
static int s_basis;   /* atomic: the target's basis (perf_hint.h PERF_HINT_BASIS_*) */
static unsigned s_wakeMs;
static int s_gameMode;
/* the game thread's */
static bool s_on, s_tried;
static uint64_t s_target;
static int64_t s_lastGpuNs;
static int32_t s_gameTid;

static const char *const k_names[PERF_HINT_MAX_THREADS] = {"game", "render", "submit"};

static void on_line(bool again)
{
    PerfHintOnText t;
    char line[640];

    memset(&t, 0, sizeof(t));
    t.api = s_info.api;
    t.gameMode = s_gameMode;
    t.basis = __atomic_load_n(&s_basis, __ATOMIC_RELAXED);
    t.again = again;
    t.withGpu = s_h.ops.withGpu;
    t.names = k_names;
    t.tids = s_h.sessTids;
    t.n = s_h.sessN;
    t.targetNs = s_h.sessTargetNs;
    t.rateNs = s_info.rateNs;
    t.wakeMs = s_wakeMs;
    t.madeMs = s_h.madeMs;
    perf_hint_on_text(&t, line, sizeof(line));
    if (again) {
        ico_log_limited("window: performance hints again", "window: %s", line);
    } else {
        fprintf(stderr, "window: %s\n", line);
    }
}

/* The hint thread: every call on the session, at the system's preferred
   update rate while the app is in front; asleep in the background until
   woken.  Never pinned: on the process's cores (cpu_place.h
   cpu_place_thread_free), at the normal priority. */
static int SDLCALL hint_main(void *u)
{
    (void)u;
    (void)cpu_place_thread_free();
    for (;;) {
        const bool active = __atomic_load_n(&s_h.active, __ATOMIC_ACQUIRE) != 0;

        (void)SDL_WaitSemaphoreTimeout(s_sem,
                                       active || s_h.session != NULL ? (Sint32)s_wakeMs : -1);
        if (__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
            break;
        }
        const unsigned ev = perf_hint_step(&s_h);

        if (ev & PERF_HINT_EV_MADE) {
            on_line(s_h.made > 1);
            __atomic_store_n(&s_made, (int)s_h.made, __ATOMIC_RELEASE);
        }
        if (ev & PERF_HINT_EV_CLOSED) {
            ico_log_limited("window: performance hints closed",
                            "window: performance hints: session closed (the app in the "
                            "background; made again on its return)");
        }
        if (ev & PERF_HINT_EV_REFUSED) {
            fprintf(stderr,
                    "window: performance hints: off (the system refused the session: "
                    "APerformanceHint_createSession answered none in %.2f ms, the device has "
                    "no hint support; API %d; game mode %s); the port runs as before\n",
                    s_h.madeMs, s_info.api, perf_hint_game_mode_words(s_gameMode));
            __atomic_store_n(&s_refused, 1, __ATOMIC_RELEASE);
            break;
        }
    }
    perf_hint_shutdown(&s_h);
    return 0;
}

void window_perf_hint_open(void)
{
    PerfHintOps ops;

    if (s_tried) {
        return;
    }
    s_tried = true;
    s_gameTid = (int32_t)thread_stat_tid();
    /* GameManager.getGameMode, for the line only (IcoActivity.gameMode) */
    s_gameMode = ico_android_game_mode();
    if (!perf_hint_android_ops(&ops, &s_info)) {
        fprintf(stderr,
                "window: performance hints: off (%s; game mode %s); the port runs as before\n",
                s_info.why, perf_hint_game_mode_words(s_gameMode));
        return;
    }
    perf_hint_init(&s_h, &ops);
    s_wakeMs = perf_hint_wake_ms(s_info.rateNs);
    s_stop = s_refused = s_made = 0;
    s_lastGpuNs = 0;
    s_target = 0;
    s_sem = SDL_CreateSemaphore(0);
    s_thread = s_sem != NULL ? SDL_CreateThread(hint_main, "ico-perf-hint", NULL) : NULL;
    if (s_thread == NULL) {
        fprintf(stderr,
                "window: performance hints: off (no hint thread: SDL: %s; game mode %s); the port "
                "runs as before\n",
                SDL_GetError(), perf_hint_game_mode_words(s_gameMode));
        if (s_sem != NULL) {
            SDL_DestroySemaphore(s_sem);
            s_sem = NULL;
        }
        return;
    }
    s_on = true;
    window_perf_hint_tick(); /* the target and the ids known at once */
}

void window_perf_hint_tick(void)
{
    if (!s_on || __atomic_load_n(&s_refused, __ATOMIC_ACQUIRE)) {
        return;
    }
    /* what window_present.c window_present_pace plans the next picture
       with: Original's frame (no interpolation), the vsync lock's period /
       n, the timer path's present gap (as it computes it), the refresh */
    {
        const uint64_t period = s_pres.paceHz == 50 ? 20000000ull : 16683333ull;
        const uint64_t refresh = window_refresh_ns(period);
        const bool mailbox = rhi_present_mailbox();
        PerfHintPace p;
        int basis = PERF_HINT_BASIS_PERIOD;

        memset(&p, 0, sizeof(p));
        p.original = !rd_interpolation_active();
        p.lockN = s_pres.lockN;
        p.frameSteps = s_pres.frameSteps;
        p.periodNs = period;
        p.refreshNs = refresh;
        p.gapNs = s_pres.fifoLock
                      ? 0
                      : pace_present_gap(s_pres.framerate,
                                         s_pres.framerate == ICO_FRAMERATE_UNCAPPED && mailbox,
                                         mailbox, refresh);
        s_target = perf_hint_target_ns(&p, &basis);
        __atomic_store_n(&s_basis, basis, __ATOMIC_RELAXED);
        perf_hint_set_target(&s_h, s_target);
    }
    /* the game thread, and the render and submit threads once their ids
       are known (0: not running; -1 running, not known yet) */
    {
        long render = 0, submit = 0;
        int32_t tids[PERF_HINT_MAX_THREADS];
        uint32_t n = 0;

        ico_render_thread_tids(&render, &submit);
        tids[n++] = s_gameTid;
        if (render > 0) {
            tids[n++] = (int32_t)render;
        }
        if (submit > 0 && render > 0) {
            tids[n++] = (int32_t)submit;
        }
        perf_hint_set_threads(&s_h, tids, render < 0 || submit < 0 ? 0u : n);
    }
}

void window_perf_hint_picture(const struct RdPerfRecord *r)
{
    PerfHintRecord in;
    PerfHintWork w;

    if (!s_on) {
        return;
    }
    in.presented = r->presented != 0;
    in.gpuValid = r->gpuValid != 0;
    in.totalMs = r->totalMs;
    in.presentMs = r->presentMs;
    in.fenceWaitMs = r->fenceWaitMs;
    in.acquireMs = r->acquireMs;
    in.startMs = r->startMs;
    in.gpuMs = in.gpuValid ? rd_perf_pace_gpu_ms(r) : 0.0;
    if (perf_hint_work(&in, &s_lastGpuNs, &w) && !__atomic_load_n(&s_refused, __ATOMIC_RELAXED)) {
        (void)perf_hint_push(&s_h, &w);
    }
}

void window_perf_hint_active(bool active)
{
    if (!s_on) {
        return;
    }
    perf_hint_set_active(&s_h, active);
    SDL_SignalSemaphore(s_sem); /* the thread steps now (the close, or the session again) */
}

void window_perf_hint_block(void)
{
    PerfHintStats st;
    char line[640];

    if (!s_on || __atomic_load_n(&s_made, __ATOMIC_ACQUIRE) == 0 ||
        __atomic_load_n(&s_refused, __ATOMIC_ACQUIRE)) {
        return;
    }
    perf_hint_take(&s_h, &st);
    perf_hint_block_text(&st, s_h.ops.withGpu, s_target, line, sizeof(line));
    fprintf(stderr, "window: %s\n", line);
}

void window_perf_hint_close(void)
{
    if (s_thread != NULL) {
        __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
        SDL_SignalSemaphore(s_sem);
        SDL_WaitThread(s_thread, NULL); /* the session closed on its way out */
        s_thread = NULL;
    }
    if (s_sem != NULL) {
        SDL_DestroySemaphore(s_sem);
        s_sem = NULL;
    }
    if (s_on) {
        perf_hint_android_release();
    }
    s_on = false;
    s_tried = false; /* a window opened again asks again */
}

#else /* not Android: no hints */

void window_perf_hint_open(void) {}

void window_perf_hint_tick(void) {}

void window_perf_hint_picture(const struct RdPerfRecord *r)
{
    (void)r;
}

void window_perf_hint_active(bool active)
{
    (void)active;
}

void window_perf_hint_block(void) {}

void window_perf_hint_close(void) {}

#endif
