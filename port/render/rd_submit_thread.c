/* rd_submit_thread.c: the pictures' submit and present on a thread of their
 * own (rd_submit_thread.h). */
#include "rd_submit_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
#include "rd_device_rows.h" /* rd__device_value: the GPU's end first */
#include "rd_internal.h"

static struct {
    IcoThread *th;
    IcoMutex *m;
    IcoCond *c; /* any change of the queue: both sides wait on it */
    RdSubmitQ q;
    bool quit;
    /* the queue not idle (the guard's busy and the heartbeat, no lock);
       written under the lock */
    int busy;
    void (*begin)(void *user);
    void *user;
    /* the longest job (its end, submit and present) finished since the last
       take, ns (rd_thread_submit_take_job_ns); under the lock */
    uint64_t jobMaxNs;
    /* the frame slots apart (rd_submit_thread.h "the slots"): set at the
       start, before the thread and the slot guard exist */
    bool apart;
    /* the pictures begun beside the other slot's job and the slot waits,
       ever (the lines' rate limit and the end's line); under the lock */
    uint64_t overlaps, slotWaits;
    double slotWaitMs;
    /* the GPU's end first (rd_submit_thread.h): wanted for this run's Frame
       rate Original jobs (the device table, the slots apart); set at the
       start, before the thread exists */
    bool gpuFirst;
    /* its verdict from the first presents (RD_GPU_FIRST_*; atomic: the
       tests' read), the samples so far (counted: waited and early,
       rd_submit_queue.h rd__sq_gf_class; atEnd: of those, waited) and the
       present mode it was taken under; the submit thread's own but the
       verdict */
    int verdict;
    unsigned counted, atEnd, verdicts;
    char verdictMode[32];
    /* while it is not taken: the submission of the last present job (the
       next long present's wait); the round's tally since the start or a
       present mode change (roundMode; timed: the Original presents timed,
       the rest by RD_GF_*, the last unknown's result, the samples' waits
       made; atomic where the tests read them), the sample lines written
       and the round's late line */
    uint64_t prevValue;
    char roundMode[32];
    unsigned timed, shortCalls, unknown, notOriginal, noPresent, waits;
    /* the long calls whose picture before had already finished (not
       counted), the reads of it before a call that failed (the call still
       eligible) */
    unsigned prevDone, readFailed;
    int32_t lastResult;
    unsigned lines;
    bool lateLogged;
    /* the ahead order (rd_submit_thread.h "ahead"): off for the run
       (RD_AHEAD_OFF_*, rd_submit_queue.h; atomic: the gate, the 10 s line
       and the tests read it; set by the start or by the submit thread) and
       its start line written; the present-call guard (the submit thread's);
       the waiter thread and its condition (made at the order's first job),
       and under the lock: a wait asked for (armed, for value), its return
       (done: result, endMs on rd's ms clock), the end asked (wQuit) */
    int aheadOff;
    bool aheadLogged;
    RdSqGuard guard;
    IcoThread *wth;
    IcoCond *wc;
    bool wArmed, wDone, wQuit;
    uint64_t wValue;
    int32_t wResult;
    double wEndMs;
} s_st;

/* the ahead order's run-wide reads (no lock): in effect for the gate (set
   by the submit thread as it takes a job in that order, cleared as it
   takes one in the one-at-a-time order or turns the order off), the tests'
   choice (rd__submit_test_ahead: 0 held off, -1 none), the previous run's
   end that holds it off for this start (rd_thread_submit_ahead_prev; kept
   across the thread's restarts); the counts ever (rd__submit_ahead_tally:
   the waiter's waits, the presents made with a later picture submitted,
   the jobs submitted in the order and of them before the picture before's
   GPU end, the times it was turned off) */
static int s_aheadLive;
static int s_aheadForce = -1;
static char s_aheadPrev[160];
static uint64_t s_aheadWaits, s_aheadPresents, s_aheadSubmits, s_aheadEarly, s_aheadOffs;
/* the order's off reason at the last start or since (the tests' read after
   the thread ended; RD_AHEAD_OFF_*) */
static int s_aheadLastOff;

/* the running job waits for its GPU end before its present, with no job
   queued behind it (rd_thread_submit_gpu_wait_alone, read with no lock):
   stored under the lock, as the queue's count it reads; the jobs that made
   the wait, ever (the tests' read, rd__submit_gpu_first_waits) */
static int s_waitAlone;
static uint64_t s_gpuFirstWaits;
/* of those, the waits the backend made and that returned true */
static uint64_t s_gpuFirstWaited;

/* the jobs queued or running of each frame slot (rd__sq_slot_jobs's
   count), for the slot guard's question without the lock (asked at every
   slot call of the render thread while this thread works): added at a
   post and taken at a finish, both under the lock as the queue's own; the
   finish's release store makes the job's writes the render thread's once
   it reads 0 with acquire */
static uint32_t s_slotJobs[RHI_FRAMES_IN_FLIGHT];

/* the cores the jobs ran on (cpu_place.h: atomic bins, no lock; kept
   across a restart of the thread) */
static CpuPlaceHist s_cores;

/* the calling thread is the submit thread; the ahead order's waiter (its
   one call, rhi_wait_value_ns, is the submit thread's as far as the call
   guard goes: rd_thread_on_submit) */
static _Thread_local bool t_onS;
static _Thread_local bool t_onW;
/* the render thread's list kept open for it (rd__submit_defer), posted at
   its job's end (rd__submit_post_pending) */
static _Thread_local bool t_kept;
static _Thread_local RdSubmitJob t_job;

/* the tests' seam (rd__submit_test_hook): kept apart from s_st, which a
   start clears */
static void (*s_testHook)(void);
/* and the faked GPU and driver (rd__submit_test_gpu), the rule's choice
   forced (rd__submit_test_gpu_first: -1 the device table's) */
static const RdSubmitTestGpu *s_gpuFake;
static int s_gpuFirstForce = -1;

void rd__submit_test_hook(void (*fn)(void))
{
    __atomic_store_n(&s_testHook, fn, __ATOMIC_RELEASE);
}

void rd__submit_test_gpu(const RdSubmitTestGpu *fake)
{
    __atomic_store_n(&s_gpuFake, fake, __ATOMIC_RELEASE);
}

int rd_thread_submit_gpu_first_verdict(void)
{
    return s_st.th != NULL && s_st.gpuFirst ? __atomic_load_n(&s_st.verdict, __ATOMIC_ACQUIRE)
                                            : RD_GPU_FIRST_OFF;
}

void rd__submit_test_gpu_first(int force)
{
    s_gpuFirstForce = force < 0 ? -1 : force >= 2 ? 2 : force != 0;
}

void rd__submit_gpu_first_tally(RdGpuFirstTally *out)
{
    out->timed = __atomic_load_n(&s_st.timed, __ATOMIC_ACQUIRE);
    out->shortCalls = __atomic_load_n(&s_st.shortCalls, __ATOMIC_ACQUIRE);
    out->counted = __atomic_load_n(&s_st.counted, __ATOMIC_ACQUIRE);
    out->waited = __atomic_load_n(&s_st.atEnd, __ATOMIC_ACQUIRE);
    out->unknown = __atomic_load_n(&s_st.unknown, __ATOMIC_ACQUIRE);
    out->waits = __atomic_load_n(&s_st.waits, __ATOMIC_ACQUIRE);
    out->prevDone = __atomic_load_n(&s_st.prevDone, __ATOMIC_ACQUIRE);
    out->readFailed = __atomic_load_n(&s_st.readFailed, __ATOMIC_ACQUIRE);
}

uint64_t rd__submit_gpu_first_waits(void)
{
    return __atomic_load_n(&s_gpuFirstWaits, __ATOMIC_ACQUIRE);
}

uint64_t rd__submit_gpu_first_waited(void)
{
    return __atomic_load_n(&s_gpuFirstWaited, __ATOMIC_ACQUIRE);
}

/* the lists' ends and submits in rd__sq_tail_steps's order (the scene's,
   then the present's own); the present, its last step, is the caller's,
   timed apart */
void rd__submit_lists(const RdSubmitJob *j)
{
    RdSqStep st[RD_SQ_TAIL_MAX];
    const uint32_t n = rd__sq_tail_steps(j, st);
    for (uint32_t k = 0; k < n; k++) {
        if (st[k].what == RD_SQ_END) {
            rhi_end_commands((RhiCommandList){st[k].cl});
        } else if (st[k].what == RD_SQ_SUBMIT) {
            rhi_submit((RhiCommandList){st[k].cl});
        }
    }
}

/* the GPU's end first: the job's wait for it begins (on) or ends; alone
   when no job is queued behind the running one */
static void gpuWaitPhase(bool on)
{
    if (!on) {
        __atomic_store_n(&s_waitAlone, 0, __ATOMIC_RELEASE);
        return;
    }
    ico_mutex_lock(s_st.m);
    __atomic_store_n(&s_waitAlone, s_st.q.count == 1u ? 1 : 0, __ATOMIC_RELEASE);
    ico_mutex_unlock(s_st.m);
}

/* the one wait after a long present (rd_submit_thread.h "the GPU's end
   first"), for the previous present job's submission `prev`: the test's
   fake, else the backend's unbounded wait; 0 when it ended, else its
   result (rhi.h rhi_wait_value_ns) */
static int32_t gpuSampleWait(const RdSubmitTestGpu *fake, uint64_t prev)
{
    __atomic_add_fetch(&s_st.waits, 1u, __ATOMIC_RELEASE);
    if (fake != NULL && fake->sampleWait != NULL) {
        return fake->sampleWait() ? 0 : RHI_WAIT_NONE;
    }
    uint64_t ns = 0;
    return rhi_wait_value_ns(prev, &ns);
}

static const char *gpuClassName(uint8_t cls)
{
    return cls == RD_GF_WAITED  ? "returned at the end of GPU work"
           : cls == RD_GF_EARLY ? "early"
                                : "unknown (the wait failed)";
}

/* the round's tally into a line's words (the verdict's and the late one's) */
static void gpuTallyText(char *out, size_t size)
{
    snprintf(out, size,
             "%u presents timed: %u under %.1f ms (not counted), %u long calls with the picture "
             "before already finished (not counted), %u counted (%u returned at the end of GPU "
             "work, %u early), %u unknown (last result %d), %u reads of the picture before "
             "failed; %u jobs not Original, %u Original jobs without a present or a driver call",
             s_st.timed, s_st.shortCalls, RD_GPU_FIRST_CALL_MS, s_st.prevDone, s_st.counted,
             s_st.atEnd, s_st.counted - s_st.atEnd, s_st.unknown, (int)s_st.lastResult,
             s_st.readFailed, s_st.notOriginal, s_st.noPresent);
}

/* a timed present's class into the round's tally (RD_GF_*), and whether
   its read of the picture before failed */
static void gpuCount(uint8_t cls, int32_t result, int prevRead)
{
    __atomic_add_fetch(&s_st.timed, 1u, __ATOMIC_RELEASE);
    if (prevRead == -1) {
        __atomic_add_fetch(&s_st.readFailed, 1u, __ATOMIC_RELEASE);
    }
    if (cls == RD_GF_PREV_DONE) {
        __atomic_add_fetch(&s_st.prevDone, 1u, __ATOMIC_RELEASE);
    } else if (cls == RD_GF_SHORT) {
        __atomic_add_fetch(&s_st.shortCalls, 1u, __ATOMIC_RELEASE);
    } else if (cls == RD_GF_UNKNOWN) {
        __atomic_add_fetch(&s_st.unknown, 1u, __ATOMIC_RELEASE);
        s_st.lastResult = result;
    } else if (cls == RD_GF_WAITED || cls == RD_GF_EARLY) {
        if (cls == RD_GF_WAITED) {
            __atomic_add_fetch(&s_st.atEnd, 1u, __ATOMIC_RELEASE);
        }
        __atomic_add_fetch(&s_st.counted, 1u, __ATOMIC_RELEASE);
    }
}

/* the window's counts cleared (gpuVerdictTake), the verdict left undecided:
   the picture before's value, the round's mode and its late line kept */
static void gpuWindowClear(void)
{
    __atomic_store_n(&s_st.counted, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.atEnd, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.timed, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.shortCalls, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.unknown, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.prevDone, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_st.readFailed, 0u, __ATOMIC_RELEASE);
    s_st.notOriginal = s_st.noPresent = 0;
    s_st.lastResult = 0;
}

/* at RD_GPU_FIRST_SAMPLES counted the verdict: on with RD_GPU_FIRST_NEED
   or more at the end of GPU work, else off; its line.  The counts are a
   window of RD_GPU_FIRST_LATE timed presents: one that ends without a
   verdict (mostly short calls, or long ones too few) starts again from
   zero and the verdict stays undecided (the rule off for now), so the rare
   long call of a present that returns early (the owner's Pixel: the
   longest 1 to 4 ms of 300 a block) never adds up to 16, and a run that
   begins light still decides in its later GPU-bound scenes (the Mali-G52's
   stage 42, 155 s in); the first window's end has one line a round (a run
   and present mode) */
static void gpuVerdictTake(void)
{
    char tally[512];
    const char *mode = rhi_present_mode_name();
    if (s_st.counted < RD_GPU_FIRST_SAMPLES) {
        if (s_st.timed >= RD_GPU_FIRST_LATE) {
            if (!s_st.lateLogged) {
                s_st.lateLogged = true;
                gpuTallyText(tally, sizeof(tally));
                if (s_st.shortCalls * 2u >= s_st.timed) {
                    rd__log("submit thread: no verdict on the GPU's end first after %u Frame rate "
                            "Original presents (present mode %s; %s): the present calls are short "
                            "(%u of %u under %.1f ms): nothing decided, counted again from here "
                            "(every %u timed presents; the presents stay as they are meanwhile)",
                            s_st.timed, s_st.roundMode, tally, s_st.shortCalls, s_st.timed,
                            RD_GPU_FIRST_CALL_MS, (unsigned)RD_GPU_FIRST_LATE);
                } else {
                    rd__log("submit thread: no verdict on the GPU's end first after %u Frame rate "
                            "Original presents (present mode %s; %s): too few counted for one: "
                            "nothing decided, counted again from here (every %u timed presents; "
                            "the presents stay as they are meanwhile)",
                            s_st.timed, s_st.roundMode, tally, (unsigned)RD_GPU_FIRST_LATE);
                }
            }
            gpuWindowClear();
        }
        return;
    }
    const bool on = s_st.atEnd >= RD_GPU_FIRST_NEED;
    snprintf(s_st.verdictMode, sizeof(s_st.verdictMode), "%s", mode != NULL ? mode : "?");
    __atomic_store_n(&s_st.verdict, on ? RD_GPU_FIRST_ON : RD_GPU_FIRST_OFF, __ATOMIC_RELEASE);
    const unsigned n = ++s_st.verdicts;
    if (n <= 8u || n % 100u == 0u) {
        gpuTallyText(tally, sizeof(tally));
        rd__log("submit thread: %u of %u Frame rate Original presents returned at the end of GPU "
                "work (a call of %.1f ms or more after which the picture before's work was found "
                "ended within %.1f ms; %u needed; present mode %s; %s): %s (verdict %u of the "
                "run)",
                s_st.atEnd, s_st.counted, RD_GPU_FIRST_CALL_MS, RD_GPU_FIRST_NEAR_MS,
                (unsigned)RD_GPU_FIRST_NEED, s_st.verdictMode, tally,
                on ? "the driver's present waits for the GPU: each picture's GPU work is waited "
                     "for before its present from here on, outside the swapchain's lock"
                   : "the driver's present does not wait for the GPU: the presents stay as they "
                     "are, no wait before them",
                n);
    }
}

/* the round's tally cleared (a retake, or a present mode change while
   undecided), taken under `mode` */
static void gpuRoundClear(const char *mode)
{
    gpuWindowClear();
    s_st.prevValue = 0;
    s_st.lateLogged = false;
    snprintf(s_st.roundMode, sizeof(s_st.roundMode), "%s", mode != NULL ? mode : "?");
}

/* the verdict retaken where the present mode changed since it was taken
   (a FIFO present waits for the display, not for the GPU); while it is
   not taken, a present mode change starts the round again (its late line
   is the new mode's) */
static void gpuVerdictMode(void)
{
    const char *mode = rhi_present_mode_name();
    if (mode == NULL) {
        return;
    }
    if (__atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) == RD_GPU_FIRST_UNDECIDED) {
        if (strncmp(mode, s_st.roundMode, sizeof(s_st.roundMode)) != 0) {
            if (s_st.roundMode[0] != '\0') {
                rd__log("submit thread: the present mode is %s now (the GPU's end first's samples "
                        "were taken under %s): its samples start again",
                        mode, s_st.roundMode);
            }
            gpuRoundClear(mode);
        }
        return;
    }
    if (strncmp(mode, s_st.verdictMode, sizeof(s_st.verdictMode)) != 0) {
        rd__log("submit thread: the present mode is %s now (the verdict was taken under %s): the "
                "GPU's end first is decided again from the next presents",
                mode, s_st.verdictMode);
        gpuRoundClear(mode);
        __atomic_store_n(&s_st.verdict, RD_GPU_FIRST_UNDECIDED, __ATOMIC_RELEASE);
    }
}

/* a timed Original present while the verdict is not taken: under
   RD_GPU_FIRST_CALL_MS, or long with the picture before already finished
   as the call began (prevRead 1, the counter read just before it), nothing
   more; else the one wait for the previous present job's submission
   `prev` (0: none), its class, its line among the run's first
   RD_GPU_FIRST_LINES counted or unknown ones (the counter's instant reads
   of `prev` and `own` after the wait, read only), the verdict */
static void gpuSampleTake(RdSubmitJob *j, const RdSubmitTestGpu *fake, double callMs, uint64_t prev,
                          uint64_t own, int prevRead)
{
    int32_t r = 0;
    double waitMs = 0.0;
    const bool hasPrev = prev != 0;
    if (callMs >= RD_GPU_FIRST_CALL_MS && hasPrev && prevRead != 1) {
        const double w0 = rd__now_ms();
        r = gpuSampleWait(fake, prev);
        waitMs = rd__now_ms() - w0;
    }
    const uint8_t cls = rd__sq_gf_class(callMs, hasPrev, prevRead, r, waitMs);
    j->gfSample = cls;
    j->gfResult = r;
    j->gfPrevRead = (int8_t)prevRead;
    gpuCount(cls, r, prevRead);
    if ((cls == RD_GF_WAITED || cls == RD_GF_EARLY || cls == RD_GF_UNKNOWN) &&
        s_st.lines < RD_GPU_FIRST_LINES) {
        uint64_t p = prev, o = own;
        const int readPrev = rhi_submitted_finished(&p), readOwn = rhi_submitted_finished(&o);
        s_st.lines++;
        rd__log("submit thread: Frame rate Original sample %u (of the first %u logged): the "
                "picture before read %s as the present began; the present %.1f ms, then the wait "
                "for the picture before's GPU work %.1f ms (result %d); %s (%u counted, %u at the "
                "end of GPU work; %u long calls with the picture before already finished, %u "
                "reads failed); the counter read after the wait: the picture before's %d, its "
                "own %d (read only: 1 finished, 0 running, -1 not known)",
                s_st.lines, (unsigned)RD_GPU_FIRST_LINES,
                prevRead == 0 ? "running" : "not known (the read failed: the call still counted)",
                callMs, waitMs, (int)r, gpuClassName(cls), s_st.counted, s_st.atEnd, s_st.prevDone,
                s_st.readFailed, readPrev, readOwn);
    }
    gpuVerdictTake();
}

static void tail(RdSubmitJob *j)
{
    const double t0 = rd__now_ms();
    rd__submit_lists(j);
    const double t1 = rd__now_ms();
    j->submitMs = t1 - t0;
    j->gfSample = RD_GF_NONE;
    j->gfResult = 0;
    j->gfPrevRead = -2;
    const RdSubmitTestGpu *fake = t_onS ? __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE) : NULL;
    if (fake != NULL && fake->submitted != NULL) {
        fake->submitted(); /* the test's GPU work begins */
    }
    const bool wanted = j->gpuFirst && t_onS;
    if (wanted && j->present) {
        gpuVerdictMode();
    }
    /* the rule wanted on this run and its verdict not taken: every job of
       the submit thread is in the round's tally, and the Original presents
       are timed (a job with a present, or the test's driver) */
    const bool round = s_st.gpuFirst && t_onS &&
                       __atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) == RD_GPU_FIRST_UNDECIDED;
    const bool presents = j->present || (fake != NULL && fake->present != NULL);
    const int verdict =
        wanted ? __atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) : RD_GPU_FIRST_OFF;
    const bool first = verdict == RD_GPU_FIRST_ON;
    const bool sample = round && wanted && presents;
    uint64_t own = 0, prev = 0;
    if (round && presents) {
        /* this job's own submission (an atomic load), the next long
           present's picture before */
        own = rhi_last_submitted();
        prev = s_st.prevValue;
        s_st.prevValue = own;
    }
    if (round && !sample) {
        j->gfSample = wanted ? (uint8_t)RD_GF_NO_PRESENT : (uint8_t)RD_GF_NOT_ORIGINAL;
        if (wanted) {
            s_st.noPresent++;
        } else {
            s_st.notOriginal++;
        }
    }
    if (first) {
        /* the GPU's end first (rd_submit_thread.h): the swapchain's lock is
           not held, so the render thread's next picture acquires and
           records beside this wait, and the game's gate does not count it
           as busy (rd_thread.c rd__thread_picture_busy); the present after
           it finds the picture's work done, and the picture before's (one
           queue: its counter reaches the newest submission last), so a
           present that returns at the end of either (the Mali-G52's, issue
           179) returns at once.  The next job is this queue's, after this
           present: the GPU holds one picture at a time */
        gpuWaitPhase(true);
        if (fake != NULL && fake->waitEnd != NULL) {
            fake->waitEnd(); /* the test's GPU end */
        }
        if (rhi_wait_submitted()) {
            __atomic_add_fetch(&s_gpuFirstWaited, 1u, __ATOMIC_RELEASE);
        }
        gpuWaitPhase(false);
        __atomic_add_fetch(&s_gpuFirstWaits, 1u, __ATOMIC_RELEASE);
    }
    {
        void (*hook)(void) = __atomic_load_n(&s_testHook, __ATOMIC_ACQUIRE);
        if (hook != NULL && t_onS) {
            hook(); /* the test's job that does not end: a driver's present that blocks */
        }
    }
    /* the witness, the one thing done just before the call: has the
       picture before's work already finished (a counter read, no wait)?
       Then a long call below was long for something else and is not
       counted.  The test's fake answers by its own clock */
    int prevRead = -2;
    if (sample && prev != 0) {
        if (fake != NULL && fake->prevFinished != NULL) {
            prevRead = fake->prevFinished();
        } else {
            uint64_t pv = prev;
            prevRead = rhi_submitted_finished(&pv);
        }
    }
    const double c0 = rd__now_ms();
    if (fake != NULL && fake->present != NULL) {
        fake->present(); /* the test's driver's present call (it may wait for the GPU) */
    }
    if (j->present) {
        /* the even cadence's desired time for this present (0: any time) */
        rhi_present_desire(j->desiredMs > 0.0 ? (uint64_t)(j->desiredMs * 1e6) : 0u);
        rhi_present();
    }
    const double c1 = rd__now_ms();
    rd__present_split_take(j, j->present);
    /* the present's time without the sample's wait after it (the wait is
       its own figure, in the sample's line) */
    j->presentMs = rd__now_ms() - t1;
    if (sample && j->presentSplit == 2u) {
        /* the lock taken but no driver call made (no swapchain, no
           acquired slot, a failed signal submit): not timed, not sampled */
        j->gfSample = RD_GF_NO_PRESENT;
        s_st.noPresent++;
    } else if (sample) {
        /* the driver's call where the backend timed it apart (not the
           swapchain lock's wait before it), else the whole present */
        const double callMs = j->presentSplit == 1u ? j->presentCallMs : c1 - c0;
        gpuSampleTake(j, fake, callMs, prev, own, prevRead);
    }
}

/* a finished job's times into its replay's record and the render thread's
   block (on the thread that waited for it) */
static void apply(const RdSubmitJob *j)
{
    rd__perf_submit_times(j);
    rd__thread_note_submit(j->submitMs + j->presentMs);
}

/* ------------------------------------------------- the ahead order */

static const char *aheadWhyText(int code)
{
    switch (code) {
    case RD_AHEAD_OFF_LOST:
        return "the graphics device was lost";
    case RD_AHEAD_OFF_SUBMIT:
        return "a picture's submit failed";
    case RD_AHEAD_OFF_WAIT:
        return "a wait for a picture's GPU work failed";
    case RD_AHEAD_OFF_GUARD:
        return "4 of the last 16 present calls made with a later picture submitted took 2.0 ms "
               "or more (a present that waits for the later picture's GPU work too)";
    case RD_AHEAD_OFF_THREAD:
        return "its waiting thread could not be made";
    case RD_AHEAD_OFF_TEST:
        return "a test's choice (rd__submit_test_ahead)";
    case RD_AHEAD_OFF_PREV:
        return "a start after one that lost the graphics device, hung its driver or was ended "
               "by the watchdog";
    default:
        return NULL;
    }
}

/* the order off for the run, first reason only, with its line (what:
   the reason's numbers); the jobs already submitted finish as the
   one-at-a-time order's would (stMain).  Set before anything else is done
   with what was found: a device loss's handling (window_host.c
   device_lost_quit) drains this thread first, so it finds the order off */
static void aheadOffSet(int code, const char *what)
{
    int none = RD_AHEAD_OFF_NONE;
    if (!__atomic_compare_exchange_n(&s_st.aheadOff, &none, code, false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return;
    }
    __atomic_store_n(&s_aheadLive, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_aheadLastOff, code, __ATOMIC_RELEASE);
    __atomic_add_fetch(&s_aheadOffs, 1u, __ATOMIC_RELEASE);
    rd__log("submit thread: the next picture is not submitted ahead for the rest of this run: "
            "%s%s%s%s; "
            "back to one picture on the GPU at a time (each picture's GPU work waited for before "
            "its present, the next submitted after that present), the pictures already "
            "submitted presented first",
            aheadWhyText(code), what != NULL ? " (" : "", what != NULL ? what : "",
            what != NULL ? ")" : "");
}

/* the order wanted for job j (its post's choice, the run's state, the
   verdict on): read outside the lock on a copy (only this thread takes
   jobs).  The present mode is checked (gpuVerdictMode, as tail does) only
   with the verdict on, so a run whose verdict is not on goes through
   exactly the calls it made before this order existed */
static bool aheadWants(const RdSubmitJob *j)
{
    if (!j->gpuFirst || !t_onS || !s_st.gpuFirst ||
        __atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE) != RD_AHEAD_OFF_NONE ||
        __atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) != RD_GPU_FIRST_ON) {
        return false;
    }
    if (rhi_device_lost()) {
        aheadOffSet(RD_AHEAD_OFF_LOST, NULL); /* a loss another thread found first */
        return false;
    }
    if (j->present) {
        gpuVerdictMode();
    }
    return __atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) == RD_GPU_FIRST_ON;
}

/* the waiter thread: one unbounded wait at a time for the held job's
   submission (s_st.wValue), its result and its return's time, nothing else
   (no queue call, no present, no counter of the backend but the wait's
   own: vk_cmd.c rhi_wait_value_ns, which the submit thread makes no more
   while the order runs); a quit asked while a wait is armed waits first */
static void waiterMain(void *u)
{
    (void)u;
    t_onW = true;
    ico_mutex_lock(s_st.m);
    for (;;) {
        while (!s_st.wArmed && !s_st.wQuit) {
            ico_cond_wait(s_st.wc, s_st.m);
        }
        if (!s_st.wArmed) {
            break;
        }
        const uint64_t value = s_st.wValue;
        ico_mutex_unlock(s_st.m);
        const RdSubmitTestGpu *fake = __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE);
        if (fake != NULL && fake->waitValue != NULL) {
            fake->waitValue(value); /* the test's GPU end of that job */
        }
        uint64_t ns = 0;
        const int32_t r = rhi_wait_value_ns(value, &ns);
        const double t = rd__now_ms();
        __atomic_add_fetch(&s_aheadWaits, 1u, __ATOMIC_RELEASE);
        ico_mutex_lock(s_st.m);
        s_st.wArmed = false;
        s_st.wDone = true;
        s_st.wResult = r;
        s_st.wEndMs = t;
        ico_cond_broadcast(s_st.c);
    }
    ico_mutex_unlock(s_st.m);
}

/* the waiter made (on the submit thread, at the order's first job): false
   and the order off where it cannot be */
static bool aheadWaiter(void)
{
    if (s_st.wth != NULL) {
        return true;
    }
    if (s_st.wc == NULL) {
        s_st.wc = ico_cond_create();
    }
    s_st.wth = s_st.wc != NULL ? ico_thread_create(waiterMain, NULL) : NULL;
    if (s_st.wth == NULL) {
        aheadOffSet(RD_AHEAD_OFF_THREAD, NULL);
        return false;
    }
    return true;
}

/* the waiter asked to wait for `value` (the lock held) */
static void aheadArm(uint64_t value)
{
    s_st.wValue = value;
    s_st.wDone = false;
    s_st.wArmed = true;
    ico_cond_broadcast(s_st.wc);
}

/* the ahead order's jobs on the submit thread: the held one (submitted,
   its GPU work waited for by the waiter, or here where the order went off
   with it submitted: self), the next (submitted behind it); each one's
   submit end (its presentMs from there, as tail's); the gap after the held
   job's GPU end while no next was submitted (its times, rd__sq_gap_parts) */
typedef struct Ahead {
    RdSubmitJob held, next;
    bool haveHeld, haveNext, heldSelf;
    double heldT1, nextT1;
    uint64_t heldValue, nextValue;
    bool gapOpen, gapNotPosted;
    double gapEnd, gapWake, gapPresented, gapFinished;
} Ahead;

/* a job's ends and submits (rd__sq_tail_steps, without the present) timed
   apart, the test's GPU begun (as tail), the job's submission value
   (rhi_last_submitted: the waiter's), the check of what the submit did */
static int aheadSubmit(RdSubmitJob *j, double *endMs, double *submitMs, uint64_t *value)
{
    const RdSubmitTestGpu *fake = __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE);
    const uint64_t v0 = rhi_last_submitted();
    RdSqStep st[RD_SQ_TAIL_MAX];
    const uint32_t n = rd__sq_tail_steps(j, st);
    double ends = 0.0, subs = 0.0;
    const double t0 = rd__now_ms();
    for (uint32_t k = 0; k < n; k++) {
        const double a = rd__now_ms();
        if (st[k].what == RD_SQ_END) {
            rhi_end_commands((RhiCommandList){st[k].cl});
            ends += rd__now_ms() - a;
        } else if (st[k].what == RD_SQ_SUBMIT) {
            rhi_submit((RhiCommandList){st[k].cl});
            subs += rd__now_ms() - a;
        }
    }
    if (fake != NULL && fake->submitted != NULL) {
        const double a = rd__now_ms();
        fake->submitted(); /* the test's GPU work begins (its submit time) */
        subs += rd__now_ms() - a;
    }
    j->submitMs = rd__now_ms() - t0;
    j->gfSample = RD_GF_NONE;
    j->gfResult = 0;
    j->gfPrevRead = -2;
    *endMs = ends;
    *submitMs = subs;
    *value = rhi_last_submitted();
    return rd__sq_ahead_check_submit(v0, *value, rhi_device_lost());
}

/* the held job's present (the test's hook and driver as tail's), timed: the
   present call (the backend's split where it times it, else the whole
   call) into the guard where a later picture is submitted (later) */
static void aheadPresent(RdSubmitJob *j, double t1, bool later)
{
    const RdSubmitTestGpu *fake = __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE);
    {
        void (*hook)(void) = __atomic_load_n(&s_testHook, __ATOMIC_ACQUIRE);
        if (hook != NULL) {
            hook(); /* the test's job that does not end */
        }
    }
    if (rhi_device_lost()) {
        aheadOffSet(RD_AHEAD_OFF_LOST, NULL);
    }
    const double c0 = rd__now_ms();
    if (fake != NULL && fake->present != NULL) {
        fake->present(); /* the test's driver's present call */
    }
    if (j->present) {
        rhi_present_desire(j->desiredMs > 0.0 ? (uint64_t)(j->desiredMs * 1e6) : 0u);
        rhi_present();
    }
    const double c1 = rd__now_ms();
    rd__present_split_take(j, j->present);
    j->presentMs = rd__now_ms() - t1;
    if (!later || (j->present && j->presentSplit == 2u)) {
        return; /* no later picture, or no driver call made: not the guard's */
    }
    const double callMs = j->presentSplit == 1u ? j->presentCallMs : c1 - c0;
    __atomic_add_fetch(&s_aheadPresents, 1u, __ATOMIC_RELEASE);
    ico_mutex_lock(s_st.m);
    rd__sq_note_guard(&s_st.q, callMs);
    ico_mutex_unlock(s_st.m);
    if (rd__sq_guard_note(&s_st.guard, callMs) &&
        __atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE) == RD_AHEAD_OFF_NONE) {
        char what[96];
        snprintf(what, sizeof(what), "this call %.1f ms; %u of %u such calls so far long", callMs,
                 s_st.guard.longs, s_st.guard.calls);
        aheadOffSet(RD_AHEAD_OFF_GUARD, what);
    }
}

/* The ahead order's step (the lock held on entry and on return): true when
   it did something.  (1) The oldest job posted and not submitted, wanted
   in this order (aheadWants): ended and submitted at once, its value
   kept, the held one (the waiter armed for it) or the next (behind the
   held one; counted early when the held one's GPU end had not been seen
   by its submit's end, else its idle gap noted).  A job not wanted waits
   until no job of this order is left, then runs one at a time (stMain).
   (2) The held job's wait returned (or, the order off, made here as the
   one-at-a-time order makes it): its present, its finish, the next one
   held and the waiter armed for it.  Otherwise nothing (the caller waits
   on the queue's condition, which the waiter's return broadcasts). */
static bool aheadStep(Ahead *a)
{
    const RdSubmitJob *peek = a->haveNext ? NULL : rd__sq_peek_unsent(&s_st.q);
    if (peek != NULL) {
        const RdSubmitJob c = *peek;
        ico_mutex_unlock(s_st.m);
        const bool want = aheadWants(&c) && aheadWaiter();
        ico_mutex_lock(s_st.m);
        if (want) {
            RdSubmitJob j;
            (void)rd__sq_take_unsent(&s_st.q, &j);
            __atomic_store_n(&s_aheadLive, 1, __ATOMIC_RELEASE);
            const bool first = !s_st.aheadLogged;
            s_st.aheadLogged = true;
            ico_mutex_unlock(s_st.m);
            if (first) {
                rd__log("submit thread: the next picture is submitted ahead from here on (the "
                        "verdict on): each picture's lists are submitted as soon as it is "
                        "handed over, beside the picture before's wait for its GPU work, which a "
                        "thread of its own makes; that picture is presented once its work has "
                        "ended (the GPU holds up to two pictures, the swapchain's lock still not "
                        "held through a wait)");
            }
            const double taken = rd__now_ms();
            double endMs = 0.0, subMs = 0.0;
            uint64_t value = 0;
            const int check = aheadSubmit(&j, &endMs, &subMs, &value);
            const double t1 = rd__now_ms();
            __atomic_add_fetch(&s_aheadSubmits, 1u, __ATOMIC_RELEASE);
            if (check != RD_AHEAD_OFF_NONE) {
                char what[96];
                snprintf(what, sizeof(what), "the submission counter still at %llu after it",
                         (unsigned long long)value);
                aheadOffSet(check, check == RD_AHEAD_OFF_SUBMIT ? what : NULL);
            }
            ico_mutex_lock(s_st.m);
            if (a->haveHeld) {
                /* behind the held one: early when its GPU end was not seen
                   by the end of this submit */
                const bool early = !s_st.wDone || s_st.wEndMs >= t1;
                rd__sq_note_ahead(&s_st.q, early);
                if (early) {
                    __atomic_add_fetch(&s_aheadEarly, 1u, __ATOMIC_RELEASE);
                } else {
                    RdSqGap g;
                    rd__sq_gap_parts(s_st.wEndMs, 0.0, 0.0, 0.0, taken, taken + endMs, t1, &g);
                    rd__sq_note_gap(&s_st.q, &g, false);
                }
                a->next = j;
                a->nextT1 = t1;
                a->nextValue = value;
                a->haveNext = true;
            } else {
                if (a->gapOpen) {
                    /* the picture before already presented and finished:
                       the GPU idle from its end to this submit's */
                    RdSqGap g;
                    rd__sq_gap_parts(a->gapEnd, a->gapWake, a->gapPresented, a->gapFinished, taken,
                                     taken + endMs, t1, &g);
                    rd__sq_note_ahead(&s_st.q, false);
                    rd__sq_note_gap(&s_st.q, &g, a->gapNotPosted);
                    a->gapOpen = false;
                }
                a->held = j;
                a->heldT1 = t1;
                a->heldValue = value;
                a->haveHeld = true;
                a->heldSelf = false;
                if (__atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE) == RD_AHEAD_OFF_NONE) {
                    aheadArm(value);
                } else {
                    a->heldSelf = true; /* turned off by its own submit: waited for here */
                }
            }
            return true;
        }
        a->gapOpen = false; /* the next job is not this order's: no gap of it */
        if (!a->haveHeld) {
            return false; /* the one-at-a-time order takes it */
        }
    }
    if (!a->haveHeld || !(s_st.wDone || a->heldSelf)) {
        return false;
    }
    /* (2) the held job's GPU work ended */
    const double endSeen = a->heldSelf ? 0.0 : s_st.wEndMs;
    const int32_t result = s_st.wResult;
    const bool waited = !a->heldSelf;
    s_st.wDone = false;
    ico_mutex_unlock(s_st.m);
    const double wake = rd__now_ms();
    if (waited) {
        const int check = rd__sq_ahead_check_wait(result, rhi_device_lost());
        if (check != RD_AHEAD_OFF_NONE) {
            char what[64];
            snprintf(what, sizeof(what), "result %d", (int)result);
            aheadOffSet(check, check == RD_AHEAD_OFF_WAIT ? what : NULL);
        }
    } else {
        /* the order off with this job submitted: its wait as the
           one-at-a-time order makes it (tail's `first`) */
        const RdSubmitTestGpu *fake = __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) == RD_GPU_FIRST_ON) {
            if (fake != NULL && fake->waitEnd != NULL) {
                fake->waitEnd();
            }
            if (rhi_wait_submitted()) {
                __atomic_add_fetch(&s_gpuFirstWaited, 1u, __ATOMIC_RELEASE);
            }
            __atomic_add_fetch(&s_gpuFirstWaits, 1u, __ATOMIC_RELEASE);
        }
    }
    RdSubmitJob j = a->held;
    aheadPresent(&j, a->heldT1, a->haveNext);
    const double presented = rd__now_ms();
    cpu_place_note(&s_cores, cpu_place_now());
    ico_mutex_lock(s_st.m);
    {
        const double ms = j.submitMs + j.presentMs;
        const uint64_t ns = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u;
        s_st.jobMaxNs = ns > s_st.jobMaxNs ? ns : s_st.jobMaxNs;
    }
    rd__sq_finish_sent(&s_st.q, &j);
    __atomic_sub_fetch(&s_slotJobs[j.slot % RHI_FRAMES_IN_FLIGHT], 1u, __ATOMIC_RELEASE);
    if (rd__sq_idle(&s_st.q)) {
        __atomic_store_n(&s_st.busy, 0, __ATOMIC_RELEASE);
    }
    ico_cond_broadcast(s_st.c);
    const double finished = rd__now_ms();
    a->haveHeld = false;
    if (a->haveNext) {
        /* the next one held: its wait armed, or made here (the order off) */
        a->held = a->next;
        a->heldT1 = a->nextT1;
        a->heldValue = a->nextValue;
        a->haveHeld = true;
        a->haveNext = false;
        a->heldSelf = __atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE) != RD_AHEAD_OFF_NONE;
        if (!a->heldSelf) {
            aheadArm(a->heldValue);
        }
    } else if (waited && __atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE) == RD_AHEAD_OFF_NONE) {
        /* the GPU idle until the next picture's submit: its gap open */
        a->gapOpen = true;
        a->gapNotPosted = s_st.q.count == 0;
        a->gapEnd = endSeen;
        a->gapWake = wake;
        a->gapPresented = presented;
        a->gapFinished = finished;
    }
    return true;
}

static void stMain(void *u)
{
    (void)u;
    t_onS = true;
    if (s_st.begin != NULL) {
        s_st.begin(s_st.user);
    }
    Ahead ahead;
    memset(&ahead, 0, sizeof(ahead));
    ico_mutex_lock(s_st.m);
    for (;;) {
        RdSubmitJob j;
        if (s_st.gpuFirst && aheadStep(&ahead)) {
            continue;
        }
        if (rd__sq_next(&s_st.q, &j)) {
            if (s_st.gpuFirst) {
                /* one at a time from here: the gate counts its present */
                __atomic_store_n(&s_aheadLive, 0, __ATOMIC_RELEASE);
            }
            ico_mutex_unlock(s_st.m);
            tail(&j);
            cpu_place_note(&s_cores, cpu_place_now());
            ico_mutex_lock(s_st.m);
            {
                const double ms = j.submitMs + j.presentMs;
                const uint64_t ns = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u; /* 0 is "none" */

                s_st.jobMaxNs = ns > s_st.jobMaxNs ? ns : s_st.jobMaxNs;
            }
            rd__sq_finish(&s_st.q, &j);
            __atomic_sub_fetch(&s_slotJobs[j.slot % RHI_FRAMES_IN_FLIGHT], 1u, __ATOMIC_RELEASE);
            if (rd__sq_idle(&s_st.q)) {
                /* what the job wrote is the other threads' once they read this */
                __atomic_store_n(&s_st.busy, 0, __ATOMIC_RELEASE);
            }
            ico_cond_broadcast(s_st.c);
            continue;
        }
        if (s_st.quit && rd__sq_idle(&s_st.q)) {
            break; /* what was queued ran first */
        }
        ico_cond_wait(s_st.c, s_st.m);
    }
    ico_cond_broadcast(s_st.c);
    ico_mutex_unlock(s_st.m);
}

/* the slot guard (rhi.h rhi_set_slot_guard): the render thread's slot
   call of `slot` runs beside this thread's work when no job of that slot is
   queued or running and it keeps no list of its own for this thread */
static bool slotFree(void *u, uint32_t slot)
{
    (void)u;
    return s_st.apart && !t_onS && !t_kept && rd_thread_on_render() &&
           __atomic_load_n(&s_slotJobs[slot % RHI_FRAMES_IN_FLIGHT], __ATOMIC_ACQUIRE) == 0;
}

static bool slotPending(void *u, uint32_t slot)
{
    (void)u;
    return __atomic_load_n(&s_slotJobs[slot % RHI_FRAMES_IN_FLIGHT], __ATOMIC_ACQUIRE) != 0;
}

static bool slotWorker(void *u)
{
    (void)u;
    return t_onS;
}

/* the slots apart, and why not (NULL: apart) */
static const char *apartWhy(const RhiLimits *l)
{
    const char *env = getenv("ICO_RD_SUBMIT_OVERLAP");
    if (env != NULL && strcmp(env, "0") == 0) {
        return "ICO_RD_SUBMIT_OVERLAP=0";
    }
    if (!l->slotOverlap) {
        return rhi_backend() == RHI_BACKEND_VULKAN
                   ? "the device's frame slots are not apart: no timeline semaphore, or "
                     "ICO_VK_SLOT_OVERLAP=0"
                   : "this graphics API keys its frame slot on one index";
    }
    return NULL;
}

/* where the GPU's end first's choice came from: the device table's row
   (the android row, issue 179), or a test's */
static const char *gpuFirstWhere(void)
{
    if (s_gpuFirstForce >= 0) {
        return "a test's choice (rd__submit_test_gpu_first)";
    }
    const RhiDeviceChoices *c = rhi_device_table_current();
    size_t n = 0;
    const RhiDeviceRow *rows = rhi_device_table_rows(&n);
    if (c != NULL && c->row[RHI_DC_PRESENT_FREE] >= 0 && (size_t)c->row[RHI_DC_PRESENT_FREE] < n) {
        return rows[c->row[RHI_DC_PRESENT_FREE]].id;
    }
    return "the device table";
}

/* the GPU's end first wanted for this run (rd_submit_thread.h): the device
   table's PRESENT_FREE, or a test's choice */
static bool gpuFirstWanted(void)
{
    return s_gpuFirstForce >= 0 ? s_gpuFirstForce != 0 : rd__device_value(RHI_DC_PRESENT_FREE) != 0;
}

/* its line where it is wanted (none elsewhere: the desktop's log as
   before); on with the frame slots apart (notApart NULL), else why not */
static void gpuFirstLine(bool want, const char *notApart)
{
    if (s_st.gpuFirst) {
        rd__log(
            "submit thread: with Frame rate Original each picture's GPU work may be waited "
            "for before its present, outside the swapchain's lock (the next picture walked, "
            "acquired and recorded meanwhile, handed over instead of skipped as busy, "
            "submitted after that present: the GPU holds one picture at a time): decided "
            "from the first %u Original presents whose driver's call lasts %.1f ms or more "
            "and begins with the picture before's GPU work still running, on where %u or more of them returned at the end of GPU work (after each such "
            "call one wait for the picture before's GPU work, found ended within %.1f ms: a "
            "driver whose present waits for the GPU); shorter calls are only timed, and "
            "every %u timed presents without a verdict the counts start again (a driver "
            "whose present returns early keeps the presents as they are); %s, issue 179",
            (unsigned)RD_GPU_FIRST_SAMPLES, RD_GPU_FIRST_CALL_MS, (unsigned)RD_GPU_FIRST_NEED,
            RD_GPU_FIRST_NEAR_MS, (unsigned)RD_GPU_FIRST_LATE, gpuFirstWhere());
    } else if (want) {
        rd__log("submit thread: Frame rate Original's pictures present as before, their GPU "
                "work not waited for first (%s: the next picture's graphics calls wait for the "
                "submit thread anyway; %s)",
                notApart, gpuFirstWhere());
    }
}

/* the ahead order held off for this start, and why (its line; NULL: it may
   run once the verdict is on): the tests' choice, the loss ladder (port/rhi
   rhi_tile_rule.h ICO_LOSS_ROW: the starts in a row before this one that
   lost the graphics device or hung its driver, port/platform/diag_host.c),
   the previous run's watchdog end (rd_thread_submit_ahead_prev) */
static const char *aheadHeldOff(char *buf, size_t size)
{
    if (s_aheadForce == 0) {
        snprintf(buf, size, "a test's choice (rd__submit_test_ahead)");
        return buf;
    }
    const char *row = getenv("ICO_LOSS_ROW");
    char *end = NULL;
    const unsigned long lost = row != NULL && row[0] != '\0' ? strtoul(row, &end, 10) : 0ul;
    if (lost > 0 && end != NULL && *end == '\0') {
        snprintf(buf, size,
                 "the loss ladder: %lu start%s in a row before this one lost the graphics device "
                 "or hung its driver, ICO_LOSS_ROW %lu, nothing written to the settings",
                 lost, lost == 1ul ? "" : "s", lost);
        return buf;
    }
    if (s_aheadPrev[0] != '\0') {
        snprintf(buf, size, "%s", s_aheadPrev);
        return buf;
    }
    return NULL;
}

/* the ahead order's start line where the GPU's end first is wanted: how it
   runs once the verdict is on, or why it is held off for this start (the
   order off from here: RD_AHEAD_OFF_TEST, _PREV) */
static void aheadStartLine(const char *off)
{
    if (off != NULL) {
        rd__log("submit thread: with the verdict on, the next picture is not submitted ahead "
                "this start (%s): one picture on the GPU at a time, each one's GPU work waited "
                "for before its present",
                off);
        return;
    }
    rd__log("submit thread: with the verdict on, the next picture is submitted ahead (its lists "
            "submitted as soon as it is handed over, beside the picture before's wait for its "
            "GPU work, which a thread of its own makes; the GPU holds up to two pictures); back "
            "to one at a time for the run after %u of the last %u present calls made with a "
            "later picture submitted take %.1f ms or more, a failed wait or submit, or a lost "
            "device; a start after one that lost the device, hung its driver or was ended by "
            "the watchdog keeps one at a time",
            (unsigned)RD_AHEAD_GUARD_LONG, (unsigned)RD_AHEAD_GUARD_WINDOW, RD_GPU_FIRST_CALL_MS);
}

bool rd__submit_thread_start(void (*begin)(void *user), void *user)
{
    if (s_st.th != NULL) {
        return true;
    }
    const char *env = getenv("ICO_RD_SUBMIT_THREAD");
    const RhiLimits *l = g_rd.hasDevice ? rhi_limits() : NULL;
    const char *why = NULL;
    if (env != NULL && strcmp(env, "0") == 0) {
        why = "ICO_RD_SUBMIT_THREAD=0";
    } else if (l == NULL) {
        why = "no graphics device";
    } else if (l->singleThreadCommands) {
        why = "this graphics API takes every call on one thread";
    }
    if (why != NULL) {
        rd__log("submit thread: off (%s: the render thread submits and presents its pictures)",
                why);
        return false;
    }
    memset(&s_st, 0, sizeof(s_st));
    memset(s_slotJobs, 0, sizeof(s_slotJobs));
    rd__sq_init(&s_st.q);
    s_st.begin = begin;
    s_st.user = user;
    const char *notApart = apartWhy(l);
    s_st.apart = notApart == NULL;
    /* the GPU's end first: decided before the thread exists, read by the
       render thread's posts (rd__submit_defer) */
    const bool want = gpuFirstWanted();
    s_st.gpuFirst = want && s_st.apart;
    /* taken anew at every start (a test's 2: on from the start, the rule's
       own behaviour driven without its verdict) */
    s_st.verdict = s_gpuFirstForce == 2 ? RD_GPU_FIRST_ON : RD_GPU_FIRST_UNDECIDED;
    __atomic_store_n(&s_waitAlone, 0, __ATOMIC_RELEASE);
    /* the ahead order: held off for this start (decided before the thread
       exists, its line below) */
    char aheadWhy[256];
    const char *aheadOff = s_st.gpuFirst ? aheadHeldOff(aheadWhy, sizeof(aheadWhy)) : NULL;
    s_st.aheadOff = aheadOff == NULL    ? RD_AHEAD_OFF_NONE
                    : s_aheadForce == 0 ? RD_AHEAD_OFF_TEST
                                        : RD_AHEAD_OFF_PREV;
    __atomic_store_n(&s_aheadLastOff, s_st.aheadOff, __ATOMIC_RELEASE);
    __atomic_store_n(&s_aheadLive, 0, __ATOMIC_RELEASE);
    s_st.m = ico_mutex_create();
    s_st.c = ico_cond_create();
    s_st.th = s_st.m && s_st.c ? ico_thread_create(stMain, NULL) : NULL;
    if (s_st.th == NULL) {
        if (s_st.c) {
            ico_cond_destroy(s_st.c);
        }
        if (s_st.m) {
            ico_mutex_destroy(s_st.m);
        }
        memset(&s_st, 0, sizeof(s_st));
        rd__log("submit thread: off (the thread could not be made: the render thread submits "
                "and presents its pictures)");
        return false;
    }
    rd__log("submit thread: on (the pictures handed to the render thread are submitted and "
            "presented on a thread of their own, %s; [dev] submit_thread = false "
            "(ICO_RD_SUBMIT_THREAD=0) keeps them on the render thread)",
            rhi_backend_kind_name(rhi_backend()));
    gpuFirstLine(want, notApart);
    if (s_st.gpuFirst) {
        aheadStartLine(aheadOff);
    }
    if (s_st.apart) {
        static const RhiSlotGuard guard = {NULL, slotFree, slotPending, slotWorker};
        rhi_set_slot_guard(&guard);
        rd__log("submit thread: the frame slots apart (the render thread's next picture "
                "records while the one before is submitted and presented, and waits only for "
                "its own slot's picture two before; ICO_RD_SUBMIT_OVERLAP=0 keeps the "
                "submit thread's work and the render thread's graphics calls one at a time)");
    } else {
        rd__log("submit thread: the frame slots one at a time (%s: the render thread's next "
                "picture's first graphics call waits for the submit thread)",
                notApart);
    }
    return true;
}

void rd__submit_thread_stop(void)
{
    if (s_st.th == NULL) {
        return;
    }
    (void)rd_thread_submit_drain();
    ico_mutex_lock(s_st.m);
    s_st.quit = true;
    ico_cond_broadcast(s_st.c);
    ico_mutex_unlock(s_st.m);
    ico_thread_join(s_st.th);
    if (s_st.wth != NULL) {
        /* the ahead order's waiter: idle (the drain above waited for every
           job, each one's wait first) */
        ico_mutex_lock(s_st.m);
        s_st.wQuit = true;
        ico_cond_broadcast(s_st.wc);
        ico_mutex_unlock(s_st.m);
        ico_thread_join(s_st.wth);
    }
    if (s_st.wc != NULL) {
        ico_cond_destroy(s_st.wc);
    }
    __atomic_store_n(&s_aheadLive, 0, __ATOMIC_RELEASE);
    if (s_st.apart) {
        rhi_set_slot_guard(NULL); /* the thread idle: nothing asks it now */
    }
    rd__log("submit thread: ended (%llu pictures ended, submitted and presented there in all; "
            "%llu pictures of the render thread began while the one before was still being "
            "submitted, %llu waited for their own slot's picture two before, %.1f ms in all)",
            (unsigned long long)s_st.q.done, (unsigned long long)s_st.overlaps,
            (unsigned long long)s_st.slotWaits, s_st.slotWaitMs);
    ico_cond_destroy(s_st.c);
    ico_mutex_destroy(s_st.m);
    memset(&s_st, 0, sizeof(s_st));
}

bool rd_thread_submit_on(void)
{
    return s_st.th != NULL;
}

bool rd_thread_on_submit(void)
{
    return t_onS || t_onW;
}

bool rd_thread_submit_busy(void)
{
    if (s_st.th == NULL || t_onS) {
        return false;
    }
    return t_kept || __atomic_load_n(&s_st.busy, __ATOMIC_ACQUIRE) != 0;
}

bool rd_thread_submit_stale(void)
{
    if (s_st.th == NULL || t_onS) {
        return false;
    }
    if (s_st.apart && rd_thread_on_render()) {
        /* the counters this thread adds are the backend's late ones, added
           atomically (vk_cmd.c "Frame slots"): the render thread reads them
           live while it works */
        return false;
    }
    return __atomic_load_n(&s_st.busy, __ATOMIC_ACQUIRE) != 0;
}

bool rd_thread_submit_slots_apart(void)
{
    return s_st.th != NULL && s_st.apart;
}

bool rd_thread_submit_apart(void)
{
    return s_st.th != NULL && s_st.apart && !t_onS && !t_kept && rd_thread_on_render();
}

bool rd_thread_submit_unfinished(void)
{
    return __atomic_load_n(&s_st.busy, __ATOMIC_ACQUIRE) != 0;
}

bool rd_thread_submit_gpu_first(void)
{
    return s_st.th != NULL && s_st.gpuFirst;
}

bool rd_thread_submit_gpu_wait_alone(void)
{
    return __atomic_load_n(&s_waitAlone, __ATOMIC_ACQUIRE) != 0;
}

bool rd_thread_submit_ahead_now(void)
{
    return s_st.th != NULL && s_st.gpuFirst && __atomic_load_n(&s_aheadLive, __ATOMIC_ACQUIRE) != 0;
}

const char *rd_thread_submit_ahead_off_why(void)
{
    if (s_st.th == NULL || !s_st.gpuFirst) {
        return NULL;
    }
    return aheadWhyText(__atomic_load_n(&s_st.aheadOff, __ATOMIC_ACQUIRE));
}

void rd_thread_submit_ahead_prev(const char *why)
{
    snprintf(s_aheadPrev, sizeof(s_aheadPrev), "%s", why != NULL ? why : "");
}

void rd__submit_test_ahead(int force)
{
    s_aheadForce = force == 0 ? 0 : -1;
}

void rd__submit_ahead_tally(RdAheadTally *out)
{
    out->waits = __atomic_load_n(&s_aheadWaits, __ATOMIC_ACQUIRE);
    out->presents = __atomic_load_n(&s_aheadPresents, __ATOMIC_ACQUIRE);
    out->submits = __atomic_load_n(&s_aheadSubmits, __ATOMIC_ACQUIRE);
    out->early = __atomic_load_n(&s_aheadEarly, __ATOMIC_ACQUIRE);
    out->offs = __atomic_load_n(&s_aheadOffs, __ATOMIC_ACQUIRE);
    out->off = __atomic_load_n(&s_aheadLastOff, __ATOMIC_ACQUIRE);
}

uint64_t rd_thread_submit_drain(void)
{
    if (s_st.th == NULL || t_onS) {
        return 0;
    }
    const bool onR = rd_thread_on_render();
    const double t0 = rd__now_ms();
    RdSubmitJob fin[RD_SUBMIT_DEPTH];
    uint32_t n = 0;
    bool waited = false;
    ico_mutex_lock(s_st.m);
    while (!rd__sq_idle(&s_st.q)) {
        waited = true;
        ico_cond_wait(s_st.c, s_st.m);
    }
    while (n < RD_SUBMIT_DEPTH && rd__sq_take_finished(&s_st.q, &fin[n])) {
        n++;
    }
    const double ms = rd__now_ms() - t0;
    if (waited) {
        rd__sq_note_wait(&s_st.q, ms, onR);
    }
    ico_mutex_unlock(s_st.m);
    for (uint32_t i = 0; i < n; i++) {
        apply(&fin[i]);
    }
    if (t_kept) {
        /* the list kept for the submit thread, needed now: here, after
           everything the thread had (the GPU's order kept) */
        t_kept = false;
        tail(&t_job);
        apply(&t_job);
        ico_mutex_lock(s_st.m);
        rd__sq_note_on_render(&s_st.q);
        ico_mutex_unlock(s_st.m);
    }
    return waited ? (uint64_t)(ms * 1e6) : 0;
}

/* the finished jobs' times into their records, here (the lock held only to
   take them) */
static void reap(void)
{
    RdSubmitJob fin[RD_SUBMIT_DEPTH];
    uint32_t n = 0;
    ico_mutex_lock(s_st.m);
    while (n < RD_SUBMIT_DEPTH && rd__sq_take_finished(&s_st.q, &fin[n])) {
        n++;
    }
    ico_mutex_unlock(s_st.m);
    for (uint32_t i = 0; i < n; i++) {
        apply(&fin[i]);
    }
}

uint64_t rd_thread_submit_wait_slot(uint32_t slot)
{
    if (s_st.th == NULL || t_onS) {
        return 0;
    }
    if (!rd_thread_submit_apart()) {
        return rd_thread_submit_drain();
    }
    slot %= RHI_FRAMES_IN_FLIGHT;
    const double t0 = rd__now_ms();
    bool waited = false;
    double ms = 0.0;
    uint64_t nWait = 0;
    ico_mutex_lock(s_st.m);
    if (!rd__sq_idle(&s_st.q) && rd__sq_slot_jobs(&s_st.q, slot) < s_st.q.count) {
        /* a job of the other slot (the picture before) runs or waits: this
           picture's graphics calls begin beside it */
        rd__sq_note_overlap(&s_st.q);
        s_st.overlaps++;
    }
    while (rd__sq_slot_jobs(&s_st.q, slot) != 0) {
        waited = true;
        ico_cond_wait(s_st.c, s_st.m);
    }
    if (waited) {
        ms = rd__now_ms() - t0;
        rd__sq_note_wait(&s_st.q, ms, true);
        nWait = ++s_st.slotWaits;
        s_st.slotWaitMs += ms;
    }
    ico_mutex_unlock(s_st.m);
    if (waited && (nWait <= 8u || nWait % 100u == 0u)) {
        rd__log("submit thread: the render thread's picture waited %.2f ms for frame slot %u's "
                "picture two before to be submitted and presented (%llu such waits so far)",
                ms, (unsigned)slot, (unsigned long long)nWait);
    }
    reap();
    return waited ? (uint64_t)(ms * 1e6) : 0;
}

void rd_thread_submit_reap(void)
{
    if (s_st.th == NULL || t_onS) {
        return;
    }
    reap();
}

uint64_t rd_thread_submit_take_present_ns(void)
{
    if (s_st.th == NULL) {
        return 0;
    }
    ico_mutex_lock(s_st.m);
    const uint64_t ns = rd__sq_take_present_ns(&s_st.q);
    ico_mutex_unlock(s_st.m);
    return ns;
}

uint64_t rd_thread_submit_take_job_ns(void)
{
    if (s_st.th == NULL) {
        return 0;
    }
    ico_mutex_lock(s_st.m);
    const uint64_t ns = s_st.jobMaxNs;
    s_st.jobMaxNs = 0;
    ico_mutex_unlock(s_st.m);
    return ns;
}

void rd_thread_submit_block_take(RdSubmitBlock *out)
{
    if (s_st.th == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    ico_mutex_lock(s_st.m);
    rd__sq_block_take(&s_st.q, out);
    ico_mutex_unlock(s_st.m);
}

bool rd__submit_defer(RhiCommandList cl, RhiCommandList cl2, bool present)
{
    if (s_st.th == NULL || t_onS || t_kept || cl.id == 0 || !rd_thread_on_render() ||
        !rd__thread_handed() || rd__capture_pending()) {
        return false;
    }
    memset(&t_job, 0, sizeof(t_job));
    t_job.cl = cl.id;
    t_job.cl2 = cl2.id;
    t_job.slot = (uint8_t)(rhi_frame_slot() % RHI_FRAMES_IN_FLIGHT);
    t_job.present = present && rd__present_will_show();
    /* the GPU's end first: a picture Frame rate Original handed over */
    t_job.gpuFirst = s_st.gpuFirst && rd__thread_original_job();
    t_job.replay = g_rdPerf.replay;
    t_job.desiredMs = g_rdPerf.desiredMs;
    t_kept = true;
    return true;
}

void rd__submit_post_pending(void)
{
    if (!t_kept) {
        return;
    }
    t_kept = false;
    ico_mutex_lock(s_st.m);
    uint64_t seq;
    while ((seq = rd__sq_post(&s_st.q, &t_job)) == 0 && !s_st.quit) {
        ico_cond_wait(s_st.c, s_st.m); /* full: RD_SUBMIT_DEPTH jobs before it */
    }
    if (seq != 0) {
        __atomic_add_fetch(&s_slotJobs[t_job.slot], 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&s_st.busy, 1, __ATOMIC_RELEASE);
        /* a job behind the one in its GPU wait: that wait is no longer
           the queue's last (rd_thread_submit_gpu_wait_alone) */
        __atomic_store_n(&s_waitAlone, 0, __ATOMIC_RELEASE);
        ico_cond_broadcast(s_st.c);
        ico_mutex_unlock(s_st.m);
        return;
    }
    ico_mutex_unlock(s_st.m);
    tail(&t_job); /* the thread ends: here */
    apply(&t_job);
}

void rd_thread_submit_take_cores(CpuPlaceHist *out)
{
    cpu_place_take(&s_cores, out);
}
