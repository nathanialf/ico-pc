/*
 * port/platform/window_present.c
 *
 * The window's presentation loop (window_present.h): the pace after each
 * simulation step, under the vsync lock or on the timer path, and the one
 * present it makes at a time.
 */
#include "window_present.h"

#include <stdio.h>
#include <string.h>
#include "audio_host.h"
#include "diag_host.h"
#include "display_clock.h"
#include "host_loop.h"
#include "rd.h"
#include "rd_submit.h"
#include "rd_tex.h"
#include "rd_thread.h"
#include "render_thread.h"
#include "rhi.h"
#include "ui_fps.h"
#include "video_options.h"
#include "window_disc.h"
#include "window_lines.h"
#ifdef __ANDROID__
#include "android/gpu_driver_android.h"
#endif

/* the names window_host.c's code has for the shared state */
#define s_pres g_windowPres
#define s_deadline g_windowDeadline

WindowPres g_windowPres;
Uint64 g_windowDeadline;

static int s_pollRestarted; /* the poll's restart at the first game frame, once */

/* The pacer more than RESYNC_NS behind: the lag is dropped (counted) */
static void resync(Uint64 now)
{
    s_pres.statResyncs++;
    s_pres.statDropped += now - s_deadline;
    s_deadline = now;
}

/* A game frame closed in the step before this pace: its time is closeAt
   (when that step was due) */
static int frame_seen(Uint64 closeAt)
{
    const uint32_t fn = rd_frame_number();

    if (fn == s_pres.frame) {
        return 0;
    }
    s_pres.frame = fn;
    s_pres.tickPrev = s_pres.tickAt;
    s_pres.tickAt = closeAt;
    if (!s_pollRestarted) {
        /* the first game frame (the opening scene follows it)
           restarts the surface-size poll's every-present spell */
        s_pollRestarted = 1;
        rhi_surface_poll_restart();
    }
    s_pres.statFrames++;
    return 1;
}

/* The rule's question at now from the render thread's estimates p */
static void rt_ask(PaceRtAsk *q, const IcoRtPlan *p, Uint64 now, Uint64 refresh, Uint64 period,
                   bool asMade)
{
    memset(q, 0, sizeof(*q));
    q->nowNs = now;
    q->dueNs = s_deadline;
    q->periodNs = period;
    /* each step by its own kind: the next one and the one after, closing a
       frame or between frames (pace_policy.h pace_step_plan_ns) */
    {
        const bool next = pace_step_closes(s_pres.sinceClose, 1, s_pres.frameSteps);
        const bool after = pace_step_closes(s_pres.sinceClose, 2, s_pres.frameSteps);

        q->stepNs = pace_step_plan_ns(&s_pres.stepEst, next, period);
        q->afterNs = pace_step_plan_ns(&s_pres.stepEst, after, period);
        q->nextBetween = !next;
    }
    q->sinceNs = p->sinceNs;
    q->readsNs = p->readsNs;
    q->pictureNs = p->pictureNs;
    q->presentNs = p->presentNs;
    q->refreshNs = refresh;
    q->cpuNs = p->cpuNs;
    q->interpNs = p->interpNs;
    q->submitNs = p->submitNs;
    q->submitOn = p->submitOn;
    q->gpuNs = s_pres.gpuNs;
    q->paced = s_pres.budget.paced;
    q->asMade = asMade;
}

/* A further present of a frame already shown, handed to the render thread
   now (pace_policy.h pace_rt_further_ask): held when it would make a game
   step miss its period (PACE_RT_MISS: the next step, whose first renderer
   call waits for its reads, or the one after it, behind the next new
   frame's own picture, would start later than its slack past its due
   time), not whenever it would touch the next step.  Its cost on this
   thread (s_pres.cost) is only the hand-over; the render thread's own
   times are its estimates (render_thread.h ico_render_thread_plan), the
   step's its own (s_pres.stepEst), the pipeline's the prediction from the
   presents handed over (s_pres.rtPipe) with the GPU's time a picture
   (s_pres.gpuNs).  Paced (vsync off or mailbox), also held while the
   picture in progress runs past its estimate (PACE_RT_OVERRUN: the drain
   would hold this thread for an end not known) and, unless the pictures
   go as made (vsync off, uncapped: pace_rt_as_made), while the presents
   wait for the display or the GPU (PACE_RT_BEHIND).  Never with the thread
   off or in sync mode, where the cost is the whole picture.  Every ask
   and the rule's working go to the 10 s line and the decision trace
   (render_thread.h ico_render_thread_note_ask).  The hold's why,
   PACE_RT_GO for none */
static PaceRtHold rt_further_hold(Uint64 now, Uint64 refresh, Uint64 period, bool asMade)
{
    IcoRtPlan p;
    PaceRtAsk q;
    PaceRtTrace t;

    if (!ico_render_thread_plan(&p)) {
        return PACE_RT_GO;
    }
    rt_ask(&q, &p, now, refresh, period, asMade);
    const PaceRtHold why = pace_rt_further_trace(&q, &s_pres.rtPipe, &t);

    if (why == PACE_RT_GO || why == PACE_RT_MISS) {
        ico_render_thread_note_ask(&q, &t, (int)why);
    }
    return why;
}

/* A present handed to the render thread at now: the pipeline's prediction
   (pace_policy.h PaceRtPipe) takes it, its reads' end by the estimates */
static void rt_pipe_posted(Uint64 now, Uint64 refresh, Uint64 period)
{
    IcoRtPlan p;
    PaceRtAsk q;

    if (!ico_render_thread_plan(&p)) {
        memset(&s_pres.rtPipe, 0, sizeof(s_pres.rtPipe));
        return;
    }
    rt_ask(&q, &p, now, refresh, period, false);
    /* the fence of the picture before only with the submit thread off
       (pace_policy.h pace_rt_pipe_behind) */
    pace_rt_pipe_post(&s_pres.rtPipe,
                      pace_rt_pipe_reads_end(&s_pres.rtPipe, now, &q, pace_rt_pipe_behind(&q)), &q);
}

/* The step that ran before this pace (the timer path, s_deadline its
   deadline now): its own time (the step less its waits for a picture's
   reads) into the step's estimate, a miss of its period into the plan
   (pace_policy.h PaceStepEst: nothing under the floor, the margin widened
   over it, the whole period after the third in the window), both into the
   10 s line; a miss with the
   further presents planned by the render thread is named, the first 8 then
   every 100th */
static void step_note(Uint64 period, bool closed)
{
    const Uint64 now = SDL_GetTicksNS();
    const uint64_t waits = rd_thread_read_wait_ns();
    const uint64_t waited = waits >= s_pres.readWaitAt ? waits - s_pres.readWaitAt : 0;
    const Uint64 prevEnd = s_pres.stepEndAt;

    s_pres.readWaitAt = waits;
    s_pres.stepEndAt = now;
    if (s_pres.paceEnd == 0) {
        return; /* no step measured yet */
    }
    const uint64_t own = s_pres.stepNs > waited ? s_pres.stepNs - waited : 0;
    const bool missed = now > s_deadline;
    const uint64_t late = missed ? now - s_deadline : 0;
    const bool backedOff = s_pres.stepEst.backoff != 0;
    /* whose miss: the pictures' only when their holds on this step (the
       presents' run past its due time, its waits for a picture's reads)
       made it, by at least the floor (2 ms); a long step of the game's own
       (a load, a compile, a bug report's capture, the disc), the lateness
       one carries into the next start, or a hold under the floor, is the
       game's and changes nothing */
    const uint64_t presentsLate =
        pace_step_start_late(s_pres.paceEnd, s_deadline - period, prevEnd);
    const bool byPictures = pace_step_by_pictures(missed, late, presentsLate, waited);
    /* the step's typical time by its kind (closing a frame or between
       frames), the game's loading work left out: disc reads, texture
       decodes, pipelines made in the step (window_host.c keeps their counts
       at the last pace's end; window_hold_line names the same) */
    bool load;
    {
        IcoStepProfile prof;
        const RdTexCacheStats *tc = rdtex_stats();
        const RdStats *rs = rd_get_stats();

        ico_host_step_profile(&prof);
        load = prof.cdReads != 0 || prof.loading != 0 ||
               (tc != NULL && tc->decodes != s_pres.decodes) ||
               (rs != NULL && rs->pipelineCreates != s_pres.pipeCreates);
    }
    PaceStepTyp *typ = &s_pres.stepEst.typ[closed ? 1 : 0];
    const unsigned skipped0 = typ->skipped;

    pace_step_typ_note(typ, own, period, load);
    const PaceStepAct act = pace_step_est_note(&s_pres.stepEst, own, missed, late, byPictures);
    const PaceStepEst *e = &s_pres.stepEst;

    ico_render_thread_note_step(
        waited, missed, late, backedOff, pace_step_plan_ns(&s_pres.stepEst, true, period),
        pace_step_plan_ns(&s_pres.stepEst, false, period), e->widenNs, e->typ[1].meanNs,
        pace_step_typ_dev_ns(&e->typ[1]), typ->skipped != skipped0, (int)act);
    if (!missed || !rd_interpolation_active() || ico_render_thread_mode() != FM_MODE_ON) {
        return;
    }
    /* what the miss did to the plan (pace_policy.h PaceStepEst) */
    if (act == PACE_STEP_STRICT) {
        ico_log_limited("window: step rule back-off",
                        "window: step rule: back-off %u steps after the third miss the pictures "
                        "caused within %u steps (late by %.1f ms, %.1f ms of it waiting for a "
                        "picture, %.1f ms the presents ran past the step's start): the pictures "
                        "between frames planned at the whole period; %u such fall-backs so far, "
                        "%u misses of the pictures' (%u under %.0f ms), %u the game's own",
                        e->backoff, PACE_STEP_MISS_WINDOW, (double)late / 1e6, (double)waited / 1e6,
                        (double)presentsLate / 1e6, e->backoffs, e->pictureMisses, e->smallMisses,
                        (double)PACE_STEP_MISS_FLOOR_NS / 1e6, e->gameMisses);
    } else if (act == PACE_STEP_WIDEN) {
        ico_log_limited("window: step rule margin",
                        "window: step rule: the margin widened by %.1f ms to %.1f ms after a miss "
                        "the pictures caused (late by %.1f ms, %.1f ms of it waiting for a "
                        "picture, %.1f ms the presents ran past the step's start; %u of the "
                        "pictures' over %.0f ms in the last %u steps): the next pictures are "
                        "held that much earlier, the widening falling an eighth a step; %u "
                        "misses of the pictures' so far (%u under %.0f ms), %u the game's own",
                        (double)late / 1e6, (double)e->widenNs / 1e6, (double)late / 1e6,
                        (double)waited / 1e6, (double)presentsLate / 1e6, e->recent,
                        (double)PACE_STEP_MISS_FLOOR_NS / 1e6, PACE_STEP_MISS_WINDOW,
                        e->pictureMisses, e->smallMisses, (double)PACE_STEP_MISS_FLOOR_NS / 1e6,
                        e->gameMisses);
    } else if (act == PACE_STEP_SMALL) {
        ico_log_limited("window: step rule small miss",
                        "window: step rule: a miss the pictures caused under %.0f ms (late by %.1f "
                        "ms, %.1f ms of it waiting for a picture, %.1f ms the presents ran past "
                        "the step's start): inside the rule's margin, nothing changed; %u such "
                        "misses so far, %u of the pictures' in all, %u the game's own",
                        (double)PACE_STEP_MISS_FLOOR_NS / 1e6, (double)late / 1e6,
                        (double)waited / 1e6, (double)presentsLate / 1e6, e->smallMisses,
                        e->pictureMisses, e->gameMisses);
    } else {
        ico_log_limited("window: step missed its period",
                        "window: the game step missed its period by %.1f ms (it took %.1f "
                        "ms, %.1f ms of it waiting for a picture, %.1f ms the presents ran past "
                        "its start): the game's own time, not the pictures': the pictures "
                        "between frames are planned as before (%u such misses so far, %u the "
                        "pictures')",
                        (double)late / 1e6, (double)s_pres.stepNs / 1e6, (double)waited / 1e6,
                        (double)presentsLate / 1e6, e->gameMisses, e->pictureMisses);
    }
}

/* One rd_present at alpha a, timed (s_pres.cost) and counted; false when
   nothing was presented.  Its waits from the backend's counts: the
   display's (s_pres.displayWaitNs), the GPU's (fenceWaitNs) and both for
   the lock (lockWaitNs) */
static int present_once(float a, Uint64 refresh, Uint64 period)
{
    RhiStats before, st;
    /* the replay's waits for the render thread's picture before (rd_submit.h) */
    const uint64_t handWait0 = rd_submit_waited_ns();
    /* the presents handed over that failed there, known at this one's
       drain (rd_thread.h) */
    const uint32_t failed0 = rd_thread_failed_presents();

    /* under the vsync lock with the display's timing the lock reads the
       present's id right after it: those pictures wait for their present
       on the render thread */
    rd_submit_hold(s_pres.timingUse != 0);
    rhi_get_stats(&before);
    Uint64 t0 = SDL_GetTicksNS();
    ico_diag_present_enter();
    /* the present whole on the render thread, the interpolation included
       (rd_thread.h): returns once it is handed over, after the one before
       it finished */
    const int ok = rd_present_post(a);
    ico_diag_present_leave();
    const Uint64 now = SDL_GetTicksNS();
    {
        /* the picture handed over before this one failed on the render
           thread: it was counted as presented, so the block's count takes
           it back (the frame rate counter and the lock's slot keep it) */
        static unsigned s_failLines;
        const uint32_t lost = rd_thread_failed_presents() - failed0;

        if (lost != 0) {
            s_pres.statPresents = s_pres.statPresents > lost ? s_pres.statPresents - lost : 0;
            s_failLines += lost;
            if (s_failLines <= 8 || s_failLines % 100 == 0) {
                fprintf(stderr,
                        "window: render thread: a picture handed over was not presented (%u so "
                        "far): taken out of the block's present count\n",
                        rd_thread_failed_presents());
            }
        }
    }

    s_pres.cost = now - t0;
    {
        /* the first present on a swapchain made since the last one (a
           frame rate change's rebuild in video_apply, a size event,
           fullscreen, a rebuild this present found necessary): what it
           took past a refresh (the first acquire, the compositor taking
           the new chain: ~140 ms on NVIDIA's Windows driver) is the
           rebuild's, a pause of the host.  Under the lock it is paused out
           after this pace (pace_lock_vsync keeps its own due time); on
           the timer path now, before the deadline is judged (applied after
           the pace, the lag would be judged first and dropped: a resync, a
           black screen, at most frame rate changes) */
        rhi_get_stats(&st);
        const Uint64 pause =
            pace_first_present_pause(st.swapchains, &s_pres.swapchains, s_pres.cost, refresh);

        if (pause != 0 && s_pres.lockN != 0) {
            s_pres.pausePendingNs += pause;
        } else if (pause != 0) {
            window_pace_paused(pause);
            t0 += pause; /* the present as if it started a refresh before its return */
        }
        s_pres.cost -= pause; /* the present's own time, a refresh: what the history and the
                                 loop's judgement of the next present see */
    }
    if (!ok) {
        return 0;
    }
    /* how long it waited: the backend's own blocked time in it (Vulkan:
       the acquire, the present and the fence waits; D3D12: the frame
       latency wait, Present and the fence waits), which leaves out the
       replay's own CPU time; the whole present where the backend times
       nothing (then all of it the display's).  Never more than the present
       (a rebuild's first present is paused out above).  The acquire and the
       present are the display's (FIFO's full queue), the fence waits the
       GPU's: the present allowance excuses the first only, or the whole
       wait where the fence waits hold the display's (pace_display_wait:
       OpenGL with vsync on, issue 126) */
    s_pres.fenceHoldsDisplay = st.fenceWaitsHoldDisplay && s_pres.vsync;
    if (rd_submit_async()) {
        /* handed to the render thread, its present still to come (the
           counters are the hand-over's, rhi.h rhi_set_call_guard): the
           display's wait is this present's acquire and its wait for the
           picture before it (that one's submit and present: FIFO's full
           queue), the GPU's the fence waits; never more than the present */
        const Uint64 disp = (st.acquireNs - before.acquireNs) + (rd_submit_waited_ns() - handWait0);
        const Uint64 fence = st.fenceWaitNs - before.fenceWaitNs;

        s_pres.lockWaitNs = disp + fence < s_pres.cost ? disp + fence : s_pres.cost;
        s_pres.displayWaitNs =
            pace_display_wait(disp, fence, s_pres.cost, s_pres.fenceHoldsDisplay);
        s_pres.fenceWaitNs = fence < s_pres.cost ? fence : s_pres.cost;
    } else if (st.presents != before.presents &&
               st.acquireNs + st.presentNs + st.fenceWaitNs != 0) {
        const Uint64 disp = (st.acquireNs - before.acquireNs) + (st.presentNs - before.presentNs);
        const Uint64 fence = st.fenceWaitNs - before.fenceWaitNs;

        s_pres.lockWaitNs = disp + fence < s_pres.cost ? disp + fence : s_pres.cost;
        s_pres.displayWaitNs =
            pace_display_wait(disp, fence, s_pres.cost, s_pres.fenceHoldsDisplay);
        s_pres.fenceWaitNs = fence < s_pres.cost ? fence : s_pres.cost;
    } else {
        s_pres.lockWaitNs = s_pres.cost;
        s_pres.displayWaitNs = s_pres.cost;
        s_pres.fenceWaitNs = 0;
    }
    if (s_pres.presentInfoPending) {
        /* the present that took F12's armed capture (photo_pump names it) */
        s_pres.presentInfoHave = rd_last_present_info(&s_pres.presentInfo);
        s_pres.presentInfoPending = 0;
    }
    /* the cost smoothed over the last presents, with hysteresis;
       a present is slower with an effects program loaded */
    s_pres.paceSlow = pace_slow_present(&s_pres.paceHist, s_pres.cost, refresh, period,
                                        rhi_injector_name() != NULL);
    /* the start-up log line, after the cost */
    window_boot_log(a);
    s_pres.lastPresent = t0;
    s_pres.lastReturn = now;
    s_pres.presentedFrame = s_pres.frame;
    s_pres.statPresents++;
    ui_fps_present();
#ifdef __ANDROID__
    ico_gpu_driver_android_presented(); /* the driver's trial */
#endif
    return 1;
}

/* the vsync lock's window side (pace_policy.h PaceLockIo) */
typedef struct LockIoCtx {
    Uint64 refresh, period;
} LockIoCtx;

static uint64_t lock_io_now(void *u)
{
    (void)u;
    return SDL_GetTicksNS();
}

static bool lock_io_present(void *u, float a)
{
    const LockIoCtx *c = u;

    /* its waits: present_once (lockWaitNs, displayWaitNs) */
    if (!present_once(a, c->refresh, c->period)) {
        return false;
    }
    /* the timer path's late skip closes with any present (pace_late_skip):
       back on it after the lock, its next frame may be skipped again */
    s_pres.skipOpen = false;
    window_disp_present(c->refresh); /* the display line's counts */
    return true;
}

/* the lock's rule for a present that waited for the display: the wait
   lock_io_present measured */
static uint64_t lock_io_waited(void *u, uint64_t costNs)
{
    (void)u;
    (void)costNs;
    return s_pres.lockWaitNs;
}

/* the present allowance's excuse: the last present's wait for the display
   alone (present_once) */
static uint64_t lock_io_display_waited(void *u, uint64_t costNs)
{
    (void)u;
    (void)costNs;
    return s_pres.displayWaitNs;
}

/* The present allowance's events after a pace_budget_step (its counts
   before given): the drop, climb, refused-climb and GPU budget lines
   (pace_budget_event_text), the first 8 then every 100th */
static void budget_lines(const PaceBudget *b, unsigned drops, unsigned climbs, unsigned refusals,
                         unsigned paced, Uint64 period)
{
    const PaceBudgetWaits w = {s_pres.cost,
                               s_pres.fenceWaitNs,
                               s_pres.displayWaitNs,
                               s_pres.gpuNs,
                               s_pres.stepNs,
                               period,
                               s_pres.fenceHoldsDisplay,
                               true,
                               window_disc_wait_ns()};
    char t[400];

    if (b->drops != drops) {
        pace_budget_event_text(b, PACE_BUDGET_TEXT_DROP, &w, t, sizeof(t));
        ico_log_limited("window: presents held", "window: %s", t);
    }
    if (b->climbs != climbs) {
        pace_budget_event_text(b, PACE_BUDGET_TEXT_CLIMB, &w, t, sizeof(t));
        ico_log_limited("window: presents back", "window: %s", t);
    }
    if (b->climbRefusals != refusals) {
        pace_budget_event_text(b, PACE_BUDGET_TEXT_STAY, &w, t, sizeof(t));
        ico_log_limited("window: presents stay", "window: %s", t);
    }
    if (b->pacedChanges != paced) {
        pace_budget_event_text(b, PACE_BUDGET_TEXT_PACED, &w, t, sizeof(t));
        ico_log_limited("window: presents paced", "window: %s", t);
    }
}

/* the window on the screen: off it, nothing is presented */
static bool lock_io_visible(void *u)
{
    (void)u;
    return !s_pres.offScreen;
}

static void lock_io_sleep(void *u, uint64_t ns)
{
    const Uint64 now = SDL_GetTicksNS();

    (void)u;
    if (now < ns) {
        SDL_DelayPrecise(ns - now);
    }
}

/* the display's timing of presents (rhi_present_timing) */
static uint64_t lock_io_present_id(void *u)
{
    (void)u;
    return rhi_present_last_id();
}

static int lock_io_shown(void *u, uint64_t *id, uint64_t *ns)
{
    (void)u;
    return rhi_present_timing_next(id, ns);
}

/* the vblank clock (display_clock.h): the next vblank noted */
static int lock_io_vblank(void *u, uint64_t *ns)
{
    const LockIoCtx *c = u;

    return window_disp_take(c->refresh, ns);
}

/* Words for the lock's display clock (PaceLock.clockKind and .clock), for
   the lock's first line and its changes */
static const char *lock_clock_words(const PaceLock *L, char *buf, size_t len)
{
    char name[64];

    if (L->clockKind == PACE_CLOCK_KIND_SHOWN) {
        switch (L->clock) {
        case PACE_CLOCK_ON:
            return "display timing in use";
        case PACE_CLOCK_LEFT_ERROR:
            return "display timing stopped (a wait on a present failed); presenting fifo until "
                   "the display settings change";
        default:
            return "display timing left (it gave a refresh more than 1 % off the one the "
                   "display reports); presenting fifo until the display settings change";
        }
    }
    if (L->clockKind == PACE_CLOCK_KIND_VBLANK) {
        switch (L->clock) {
        case PACE_CLOCK_ON:
            snprintf(buf, len, "display clock vblank (output %s)",
                     ico_display_clock_name(name, sizeof(name)));
            return buf;
        case PACE_CLOCK_LEFT_ERROR:
            return "display clock stopped (a wait for a vblank failed); the grid on the "
                   "reported refresh until the display or the settings change";
        default:
            snprintf(buf, len,
                     "display clock left (its interval was off the report: %.4f ms against "
                     "%.4f); the grid on the reported refresh until the display or the "
                     "settings change",
                     L->measuredNs / 1e6, (double)L->refreshNs / 1e6);
            return buf;
        }
    }
    return s_pres.clockGone ? "display clock off (display timing left earlier: fifo until the "
                              "display settings change)"
                            : "display clock off";
}

/* presents carry ids for the display's timing only under the lock with the
   timing in use (rhi_present_timing_use) */
static void timing_use(int on)
{
    if (on != s_pres.timingUse) {
        s_pres.timingUse = on;
        rhi_present_timing_use(on != 0);
    }
}

/* One simulated vsync under the vsync lock (pace_policy.h): n presents in
   the step's slots, then the next step's due time on the grid.  s_deadline
   is when the step was due. */
static void pace_locked(unsigned n, Uint64 period)
{
    LockIoCtx ctx;
    const int clock = rhi_present_timing();
    const PaceLockIo io = {&ctx,
                           lock_io_now,
                           lock_io_present,
                           lock_io_sleep,
                           lock_io_visible,
                           clock ? lock_io_present_id : NULL,
                           clock ? lock_io_shown : NULL,
                           lock_io_vblank,
                           lock_io_waited,
                           lock_io_display_waited};
    char words[256];

    ctx.refresh = window_refresh_ns(period);
    ctx.period = period;
    s_pres.statVsyncs++;
    if (s_pres.lockN != n || s_pres.lockRefresh != ctx.refresh ||
        s_pres.lockMailbox != (int)rhi_present_mailbox()) {
        /* the display's counts for the log, and its vblank clock (Windows) */
        window_disp_lock_start();
        /* a new lock: from the last step's due time, on the present-wait
           clock (mailbox) when the backend times presents, else on the
           vblank clock when it runs (FIFO either way: video_settings),
           else on the reported refresh alone */
        const int kind = clock && !s_pres.clockGone                       ? PACE_CLOCK_KIND_SHOWN
                         : window_disp_clock() == 1 && !s_pres.vblankGone ? PACE_CLOCK_KIND_VBLANK
                                                                          : PACE_CLOCK_KIND_NONE;

        pace_lock_reset(&s_pres.lock, n, ctx.refresh, period, s_deadline, rhi_present_mailbox(),
                        kind);
        s_pres.lockN = n;
        s_pres.lockRefresh = ctx.refresh;
        s_pres.lockMailbox = rhi_present_mailbox();
        s_pres.lockClock = s_pres.lock.clock;
        s_pres.lockResyncs = 0;
        s_pres.lockDroppedNs = 0;
        s_pres.lockNudgeAt = 0.0;
        s_pres.lockSeenAt = s_pres.lockLateAt = s_pres.lockEarlyAt = 0;
        s_pres.lockWaitedAt = s_pres.lockSkippedAt = 0;
        s_pres.lockSkippedLateAt = 0;
        if (s_pres.budgetAtOf == &s_pres.lock.budget) {
            /* the lock's allowance started again (pace_lock_reset) */
            s_pres.budgetRefusedAt = s_pres.budgetDropsAt = s_pres.budgetClimbsAt = 0;
            s_pres.budgetClimbRefusalsAt = 0;
        }
        s_pres.lockWaitNs = 0;
        s_pres.nextPresent = 0;
        {
            static unsigned s_lines;
            double dwmHz;
            uint64_t dwmNs;

            if (s_lines++ < 8) {
                fprintf(stderr,
                        "window: vsync lock: the game steps every %.4f ms (%u refresh%s of "
                        "%.4f ms), %u present%s a step, present mode %s, %s\n",
                        (double)(n * ctx.refresh) / 1e6, n, n == 1 ? "" : "es",
                        (double)ctx.refresh / 1e6, n, n == 1 ? "" : "s", rhi_present_mode_name(),
                        lock_clock_words(&s_pres.lock, words, sizeof(words)));
                /* the compositor's refresh against SDL's (Windows; the
                   primary display's: a cross-check, not a clock) */
                if (ico_display_dwm_refresh(&dwmHz, &dwmNs)) {
                    fprintf(stderr,
                            "window: vsync lock: DWM reports %.3f Hz (%.4f ms); SDL reports "
                            "%.3f Hz (%.4f ms)\n",
                            dwmHz, (double)dwmNs / 1e6, 1e9 / (double)ctx.refresh,
                            (double)ctx.refresh / 1e6);
                }
            }
        }
        /* the audio at the display's nominal rate until the first
           measured block (the 10 s line) */
        s_pres.audioRatio = pace_lock_nominal_ratio(&s_pres.lock);
        ico_audio_set_rate_ratio(s_pres.audioRatio);
    }
    /* the present mode the lock wants: mailbox with the display's timing,
       FIFO without (video_settings) */
    if ((clock && !s_pres.clockGone) != s_pres.mailbox) {
        s_pres.presentModeApply = 1;
    }
    timing_use(s_pres.lock.clockKind == PACE_CLOCK_KIND_SHOWN &&
               s_pres.lock.clock == PACE_CLOCK_ON);
    /* the frame closed in the step that was due at s_deadline */
    const int newFrame = frame_seen(s_deadline);

    window_disp_vsync(ctx.refresh); /* the display clock's vblanks since the last vsync */
    /* the present allowance's GPU gate (pace_policy.h PaceBudget) */
    s_pres.lock.gpuNs = s_pres.gpuNs;
    s_pres.lock.stepNs = s_pres.stepNs;
    s_pres.lock.discWaitNs = window_disc_wait_ns();
    const unsigned drops = s_pres.lock.budget.drops, climbs = s_pres.lock.budget.climbs;
    const unsigned refusals = s_pres.lock.budget.climbRefusals;

    s_deadline = pace_lock_vsync(&s_pres.lock, &io, newFrame != 0);
    /* the gate's period: the lock's step (pace_lock_vsync) */
    budget_lines(&s_pres.lock.budget, drops, climbs, refusals, s_pres.lock.budget.pacedChanges,
                 (Uint64)(s_pres.lock.slotNs * (double)s_pres.lock.n));
    /* the lag the lock dropped, as the resync lines count it */
    s_pres.statResyncs += s_pres.lock.resyncs - s_pres.lockResyncs;
    s_pres.statDropped += s_pres.lock.droppedNs - s_pres.lockDroppedNs;
    s_pres.lockResyncs = s_pres.lock.resyncs;
    s_pres.lockDroppedNs = s_pres.lock.droppedNs;
    if (s_pres.lock.clock != s_pres.lockClock) {
        /* the display clock stopped or was not believed: the present-wait
           clock's mailbox goes to FIFO until the next settings change; the
           vblank clock's grid stays on the report (already FIFO) until the
           display or the settings change */
        static unsigned s_lines;

        s_pres.lockClock = s_pres.lock.clock;
        if (s_lines++ < 16) {
            fprintf(stderr, "window: vsync lock: %s\n",
                    lock_clock_words(&s_pres.lock, words, sizeof(words)));
        }
        if (s_pres.lock.clock != PACE_CLOCK_ON && s_pres.lock.clockKind == PACE_CLOCK_KIND_SHOWN) {
            s_pres.clockGone = 1;
            s_pres.presentModeApply = 1;
        } else if (s_pres.lock.clock != PACE_CLOCK_ON) {
            s_pres.vblankGone = 1;
        }
    }
    /* the first presents of each lock (a few locks a session), for the
       log: diagnostics only */
    if (!s_pres.lock.traceLogged && s_pres.lock.traceCount >= PACE_LOCK_TRACE_FIRST &&
        s_pres.lock.steps >= PACE_LOCK_TRACE_FIRST + 30) {
        static unsigned s_traces;

        s_pres.lock.traceLogged = true;
        if (s_traces++ < 8) {
            char t[PACE_LOCK_TRACE_FIRST * 20];
            const unsigned k = pace_lock_trace_text(&s_pres.lock, t, sizeof t);

            fprintf(stderr,
                    "window: vsync lock, the first %u presents (ms since the one before; in "
                    "brackets ms from the time it was due to the refresh that showed it, where "
                    "the display said): %s\n",
                    k, t);
        }
    }
    window_pace_log(SDL_GetTicksNS());
}

/* The window is off the screen: minimised, hidden, occluded, or 0x0
   pixels (a Vulkan swapchain cannot be made); its presents do not wait for
   the display */
static int window_off_screen(void)
{
    int w = 0, h = 0;

    SDL_Window *const win = window_sdl();

    if (win == NULL) {
        return 0;
    }
    if (SDL_GetWindowFlags(win) &
        (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED)) {
        return 1;
    }
    SDL_GetWindowSizeInPixels(win, &w, &h);
    return w <= 0 || h <= 0;
}

void window_present_pace(int hz)
{
    /* PAL 50 Hz: 20 ms; NTSC 59.94 Hz: 16.683 ms (host_loop.c's simulated
       periods) */
    const Uint64 period = hz == 50 ? 20000000ull : 16683333ull;
    Uint64 now;

    s_pres.capAboveNs = 0; /* the timer path below sets it while the clamp holds */
    /* off the screen: the lock does not judge the presents (they return at
       once); back on it, the lock starts again from an empty queue */
    {
        const int off = window_off_screen();

        if (off != s_pres.offScreen) {
            static unsigned s_lines; /* a window that flickers: the first few */

            if (s_lines++ < 20) {
                fprintf(stderr, "window: %s the screen\n", off ? "off" : "back on");
            }
            if (!off) {
                s_pres.lockN = 0; /* pace_locked resets the lock */
            }
            s_pres.offScreen = off;
        }
    }

    /* the vsync lock: the cap equals the refresh and is a whole multiple
       of the simulation's rate (60 or 120 on 59.94), n refreshes within 1 %
       of its period (video_settings decided it for the rate the last pace
       had: a new rate decides again) */
    if (hz != s_pres.paceHz) {
        /* video_settings took NTSC before the first pace */
        if ((hz == 50) != (s_pres.paceHz == 50)) {
            s_pres.presentModeApply = 1;
        }
        s_pres.paceHz = hz;
    }
    {
        const unsigned n = s_pres.gridLock && rd_interpolation_active()
                               ? pace_lock_presents(s_pres.framerate, period)
                               : 0;
        if (n > 0 && pace_grid_period_ok(n, window_refresh_ns(period), period)) {
            pace_locked(n, period);
            return;
        }
        if (s_pres.lockN != 0 || s_pres.budget.max == 0) {
            /* off the lock (or the first pace): the timer path's allowance
               starts again */
            window_timer_budget_reset();
        }
        s_pres.lockN = 0;
        timing_use(0); /* the presents below as without the timing */
        window_disp_clock_stop();
    }
    /* vsync off, uncapped, the render thread on: pictures as made, the
       allowance a measurement (pace_policy.h pace_rt_as_made); in or out
       of it the allowance starts again */
    const bool asMade =
        pace_rt_as_made(s_pres.vsync != 0, s_pres.framerate == ICO_FRAMERATE_UNCAPPED,
                        s_pres.fifoLock != 0, ico_render_thread_mode() == FM_MODE_ON);
    if ((int)asMade != s_pres.asMade) {
        s_pres.asMade = asMade;
        window_timer_budget_reset();
        fprintf(stderr,
                asMade ? "window: vsync off, uncapped: pictures between frames as the render "
                         "thread and the GPU make them (%u in flight at most); each held only "
                         "when it would make a game step miss its period\n"
                       : "window: pictures between frames paced by the present allowance and "
                         "the display (%u in flight at most)\n",
                (unsigned)RHI_FRAMES_IN_FLIGHT);
    }
    s_pres.budget.asMade = asMade ? (unsigned)RHI_FRAMES_IN_FLIGHT : 0u;
    s_pres.budget.presents = 0; /* the step's presents (pace_budget_step) */
    s_pres.budget.optional = 0;
    /* paced by the GPU's time: vsync off or mailbox in use (pace_policy.h) */
    s_pres.budget.paced = pace_budget_paced_mode(s_pres.vsync, rhi_present_mailbox());
    s_deadline += period;
    s_pres.statVsyncs++;
    const int newFrame = frame_seen(s_deadline - period);

    step_note(period, newFrame != 0);
    /* the game's frame in steps: the closes apart (pace_policy.h
       pace_step_closes); past four (a load, a pause) not known, every
       step then planned as one that closes a frame */
    if (newFrame) {
        const unsigned n = s_pres.tickPrev != 0 && s_pres.tickAt > s_pres.tickPrev
                               ? (unsigned)((s_pres.tickAt - s_pres.tickPrev + period / 2) / period)
                               : 0u;

        s_pres.frameSteps = n <= 4u ? n : 0u;
        s_pres.sinceClose = 0;
    } else {
        s_pres.sinceClose++;
    }
    if (!rd_interpolation_active()) {
        /* framerate "original": rd_end_frame presented the frame once; the
           picture is held until the next.  That present's start-up log line
           (the frame whole: t 1) */
        RdPresentInfo pi;
        const bool whole =
            newFrame && rd_last_present_info(&pi) && pi.frame == s_pres.frame && !pi.keep;

        if (newFrame) {
            window_boot_log(1.0f);
            /* that present ran inside the step (rd_end_frame's replay):
               the first on a swapchain made since the last one is the
               rebuild's past a refresh (as present_once), paused out
               before the deadline is judged */
            RhiStats st;

            rhi_get_stats(&st);
            window_pace_paused(pace_first_present_pause(st.swapchains, &s_pres.swapchains,
                                                        (Uint64)(rd_last_replay_ms() * 1e6),
                                                        window_refresh_ns(period)));
        }
        now = SDL_GetTicksNS();
        window_orig_step(now >= s_deadline, whole ? rd_last_replay_ms() : -1.0);
        if (now < s_deadline) {
            SDL_DelayPrecise(s_deadline - now);
        } else if (now - s_deadline > RESYNC_NS) {
            resync(now);
        }
        window_pace_log(SDL_GetTicksNS());
        return;
    }
    /* the simulation keeps its vsync cadence in simulated time; until
       this vsync's deadline the window presents as often as vsync (and the
       framerate cap) allow, each present at alpha = the real time since the
       last frame closed over the tick, between the last two frames (one
       tick of latency). A frame closes inside the step before this call:
       its time is the start of this vsync period. */
    /* a present with vsync on blocks up to one display refresh; that wait is
       not the renderer being slow. A 60 Hz display under the 59.94 Hz
       simulation (refresh 16.67 ms against the 16.68 ms period) sits on
       that edge and would fall back to one present per game frame. Slow:
       a present costing over a refresh plus half a period (pace_policy.h:
       smoothed, with hysteresis, higher with an effects program). */
    Uint64 gap;
    const Uint64 refresh = window_refresh_ns(period);
    {
        /* "uncapped" under a mailbox swapchain (vsync on, or off with no
           immediate mode: Android's compositor blocks a present once its
           queue is full): one a refresh.  Immediate: back to back
           (pace_present_gap) */
        const int uncappedMailbox =
            s_pres.framerate == ICO_FRAMERATE_UNCAPPED && rhi_present_mailbox();
        gap = s_pres.fifoLock ? 0
                              : pace_present_gap(s_pres.framerate, uncappedMailbox != 0,
                                                 rhi_present_mailbox(), refresh);
        if (!gap) {
            s_pres.nextPresent = 0;
        }
        if (uncappedMailbox && !s_pres.vsync && !s_pres.mailboxOffLogged) {
            s_pres.mailboxOffLogged = 1;
            fprintf(stderr, "window: vsync off: the display offers no immediate mode; uncapped "
                            "presents once a refresh (mailbox)\n");
        }
        /* a cap above the refresh in mailbox mode presents once a refresh
           as "uncapped" does (pace_policy.h pace_cap_above_refresh): the
           display shows no more, and the rest took the shown ones' GPU time */
        if (!s_pres.fifoLock &&
            pace_cap_above_refresh(s_pres.framerate, rhi_present_mailbox(), refresh)) {
            static unsigned s_lines;

            s_pres.capAboveNs = refresh;
            if (!s_pres.capAboveLogged && s_lines < 16) {
                s_lines++;
                fprintf(stderr,
                        "window: frame rate %d is above the display's %.2f Hz: presenting once a "
                        "refresh (mailbox)\n",
                        s_pres.framerate, 1e9 / (double)refresh);
            }
            s_pres.capAboveLogged = 1;
        }
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    for (;;) {
        now = SDL_GetTicksNS();
        /* the late-present skip (pace_policy.h pace_late_skip): a new frame
           behind the deadline is left unpresented (the next step at once;
           the next present shows the newer frame) when the next step is
           already a whole period late (the step debt: now against its due
           time, s_deadline, as "step late" measures it) or presents are
           slow; never two frames in a row (pace_skipped_last), the same
           frame again while it holds; with or without sound.  The vsync
           lock has the late arm too (pace_lock_vsync).  No step or audio is
           dropped, only presents */
        {
            const PaceSkip why =
                pace_late_skip(s_pres.presentedFrame != s_pres.frame,
                               pace_skipped_last(s_pres.skipOpen, s_pres.skipFrame, s_pres.frame),
                               s_pres.paceSlow, now, s_deadline, period);

            if (why != PACE_SKIP_NONE) {
                s_pres.skipOpen = true;
                s_pres.skipFrame = s_pres.frame;
                if (why == PACE_SKIP_LATE) {
                    s_pres.statSkipLate++;
                } else {
                    s_pres.statSkipSlow++;
                }
                break;
            }
        }
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        /* under FIFO with a cap that is not a whole multiple of the
           simulation's rate (144 on 59.94, 60 on PAL's 50) and a refresh
           shorter than the period: no present that would return on a
           vblank past the deadline.  A FIFO present returns on the
           first vblank after it is made, so on the last return's grid of
           refreshes; starting one just before the deadline blocked a whole
           refresh and ran the step late.  A new frame is held
           back (to the next vsync) at most once and never once the deadline
           has passed, so a game behind still shows each frame
           (pace_policy.h pace_fifo_hold) */
        if (s_pres.fifoLock && refresh < period &&
            pace_fifo_hold(now, s_pres.lastReturn, refresh, s_deadline,
                           s_pres.presentedFrame == s_pres.frame,
                           s_pres.heldFrame == s_pres.frame + 1u)) {
            if (s_pres.presentedFrame != s_pres.frame) {
                s_pres.heldFrame = s_pres.frame + 1u;
            }
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        /* nor a further present of a frame already shown that would
           end past the deadline (its cost the last present's; with the
           render thread, one that would make a step miss its period:
           rt_further_hold): the game's step keeps its period */
        /* off the screen: no present again */
        /* nor one past the present allowance (pace_policy.h PaceBudget):
           after a step that ended half a period late, the in-betweens a
           step are held below what that step made; paced, one more only
           while the GPU has the time for it (pace_policy.h).  Not with the
           pictures as made (vsync off, uncapped): there the GPU's own
           back-pressure (the slot fences) is the limit and the allowance
           only measures */
        if (s_pres.presentedFrame == s_pres.frame) {
            const int guard = now >= s_deadline || s_pres.paceSlow || s_pres.offScreen ||
                              (!s_pres.fifoLock && now + s_pres.cost > s_deadline);
            const PaceRtHold rtWhy =
                guard ? PACE_RT_GO : rt_further_hold(now, refresh, period, asMade);
            const int rtLate = rtWhy != PACE_RT_GO;

            if (rtLate) {
                ico_render_thread_note_held((int)rtWhy);
            }
            if (guard || rtLate ||
                (!asMade &&
                 !pace_budget_allows_paced(&s_pres.budget, false, s_pres.gpuNs, period))) {
                if (!guard && !rtLate) {
                    s_pres.budget.refused++;
                }
                if (now < s_deadline) {
                    SDL_DelayPrecise(s_deadline - now);
                }
                break;
            }
        }
        if (gap && s_pres.nextPresent && now < s_pres.nextPresent) {
            const Uint64 wake = s_pres.nextPresent;

            if (wake >= s_deadline) {
                if (now < s_deadline) {
                    SDL_DelayPrecise(s_deadline - now);
                }
                break;
            }
            SDL_DelayPrecise(wake - now);
            continue;
        }
        /* alpha from the present clock (the measured time smoothed
           to the presents' steady cadence) against the simulated time the
           frame closed; the raw measured time jittered with the step and
           the sleeps before the present */
        float a = rd_present_clock_alpha(&s_pres.clock, (double)now / 1e6,
                                         (double)s_pres.tickAt / 1e6, (double)tick / 1e6,
                                         (double)(s_pres.fifoLock ? refresh : gap) / 1e6);

        if (s_pres.legacyAlpha) {
            /* ICO_RD_S2_LEGACY=1: the raw measured time, without the clock */
            const double r =
                now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
            a = (float)(r > 0.999 ? 0.999 : r);
        }
        const bool fresh = s_pres.presentedFrame != s_pres.frame;
        const int ok = present_once(a, refresh, period);
        now = SDL_GetTicksNS();
        if (ok) {
            s_pres.skipOpen = false;
            pace_budget_present(&s_pres.budget, fresh);
            rt_pipe_posted(now, refresh, period);
        }
        if (!ok) {
            /* a movie on the output, or nothing closed yet */
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        s_pres.nextPresent =
            gap ? pace_next_present(s_pres.nextPresent, s_pres.lastPresent, gap) : 0;
    }
    now = SDL_GetTicksNS();
    {
        /* the present allowance: the step ends now (a resync keeps it) */
        const unsigned drops = s_pres.budget.drops, climbs = s_pres.budget.climbs;
        const unsigned refusals = s_pres.budget.climbRefusals;
        const unsigned paced = s_pres.budget.pacedChanges;

        (void)pace_budget_step(&s_pres.budget, now > s_deadline ? now - s_deadline : 0, period,
                               refresh, s_pres.displayWaitNs, s_pres.gpuNs, s_pres.stepNs,
                               window_disc_wait_ns());
        if (!asMade) {
            /* as made the allowance holds nothing: its drop, climb and
               pace lines would say it did (its 10 s part says what it measured) */
            budget_lines(&s_pres.budget, drops, climbs, refusals, paced, period);
        }
    }
    window_pace_log(now);
    if (now > s_deadline && now - s_deadline > RESYNC_NS) {
        resync(now);
    }
}
