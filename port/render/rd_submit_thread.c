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
       tests' read), the samples so far (counted: the picture's work still
       running when the driver's present call began; atEnd: of those,
       finished when it returned) and the present mode it was taken under;
       the submit thread's own but the verdict */
    int verdict;
    unsigned counted, atEnd, verdicts;
    char verdictMode[32];
} s_st;

/* the running job waits for its GPU end before its present, with no job
   queued behind it (rd_thread_submit_gpu_wait_alone, read with no lock):
   stored under the lock, as the queue's count it reads; the jobs that made
   the wait, ever (the tests' read, rd__submit_gpu_first_waits) */
static int s_waitAlone;
static uint64_t s_gpuFirstWaits;

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

/* the calling thread is the submit thread */
static _Thread_local bool t_onS;
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
    s_gpuFirstForce = force < 0 ? -1 : force != 0;
}

uint64_t rd__submit_gpu_first_waits(void)
{
    return __atomic_load_n(&s_gpuFirstWaits, __ATOMIC_ACQUIRE);
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

/* the job's GPU work finished now (1), not (0), not known (-1): the
   test's fake, else the backend's counter read (*mark the job's
   submission, taken at the first ask) */
static int gpuDone(const RdSubmitTestGpu *fake, uint64_t *mark)
{
    if (fake != NULL && fake->finished != NULL) {
        return fake->finished();
    }
    return rhi_submitted_finished(mark);
}

/* one sample of the verdict (rd_submit_thread.h "the GPU's end first"):
   before/after the job's work finished as the driver's present call began
   and as it returned.  Counted only where it was still running as the call
   began: a picture whose work ended before the call says nothing of the
   driver (a light one ends before any present), and one counted cannot
   read "at its end" unless the call itself lasted until that end.  At
   RD_GPU_FIRST_SAMPLES counted the verdict: on with RD_GPU_FIRST_NEED or
   more at their end, else off; its line */
static void gpuSample(int before, int after)
{
    if (before != 0 || after < 0) {
        return;
    }
    s_st.counted++;
    s_st.atEnd += after == 1 ? 1u : 0u;
    if (s_st.counted < RD_GPU_FIRST_SAMPLES) {
        return;
    }
    const bool on = s_st.atEnd >= RD_GPU_FIRST_NEED;
    const char *mode = rhi_present_mode_name();
    snprintf(s_st.verdictMode, sizeof(s_st.verdictMode), "%s", mode != NULL ? mode : "?");
    __atomic_store_n(&s_st.verdict, on ? RD_GPU_FIRST_ON : RD_GPU_FIRST_OFF, __ATOMIC_RELEASE);
    const unsigned n = ++s_st.verdicts;
    if (n <= 8u || n % 100u == 0u) {
        rd__log("submit thread: %u of %u Frame rate Original presents whose picture's GPU work "
                "was still running as the driver's present call began returned at that work's "
                "end (%u needed; present mode %s): %s (verdict %u of the run)",
                s_st.atEnd, s_st.counted, (unsigned)RD_GPU_FIRST_NEED, s_st.verdictMode,
                on ? "the driver's present waits for the GPU: each picture's GPU work is waited "
                     "for before its present from here on, outside the swapchain's lock"
                   : "the driver's present does not wait for the GPU: the presents stay as they "
                     "are, no wait before them",
                n);
    }
}

/* the verdict retaken where the present mode changed since it was taken
   (a FIFO present waits for the display, not for the GPU) */
static void gpuVerdictMode(void)
{
    if (__atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) == RD_GPU_FIRST_UNDECIDED) {
        return;
    }
    const char *mode = rhi_present_mode_name();
    if (mode != NULL && strncmp(mode, s_st.verdictMode, sizeof(s_st.verdictMode)) != 0) {
        rd__log("submit thread: the present mode is %s now (the verdict was taken under %s): the "
                "GPU's end first is decided again from the next presents",
                mode, s_st.verdictMode);
        s_st.counted = s_st.atEnd = 0;
        __atomic_store_n(&s_st.verdict, RD_GPU_FIRST_UNDECIDED, __ATOMIC_RELEASE);
    }
}

static void tail(RdSubmitJob *j)
{
    const double t0 = rd__now_ms();
    rd__submit_lists(j);
    const double t1 = rd__now_ms();
    j->submitMs = t1 - t0;
    const RdSubmitTestGpu *fake = t_onS ? __atomic_load_n(&s_gpuFake, __ATOMIC_ACQUIRE) : NULL;
    if (fake != NULL && fake->submitted != NULL) {
        fake->submitted(); /* the test's GPU work begins */
    }
    const bool wanted = j->gpuFirst && t_onS;
    if (wanted && j->present) {
        gpuVerdictMode();
    }
    const int verdict =
        wanted ? __atomic_load_n(&s_st.verdict, __ATOMIC_RELAXED) : RD_GPU_FIRST_OFF;
    const bool first = verdict == RD_GPU_FIRST_ON;
    /* the verdict's samples: the presents made without the wait while it is
       not taken (a job with a present, or the test's driver) */
    const bool sample =
        verdict == RD_GPU_FIRST_UNDECIDED && (j->present || (fake != NULL && fake->present));
    uint64_t mark = 0;
    const int before = sample ? gpuDone(fake, &mark) : -1;
    if (first) {
        /* the GPU's end first (rd_submit_thread.h): the swapchain's lock is
           not held, so the render thread's next picture acquires and
           records beside this wait, and the game's gate does not count it
           as busy (rd_thread.c rd__thread_picture_busy); the present after
           it finds the picture's work done (the Mali-G52's present returns
           at that end, issue 179).  The next job is this queue's, after
           this present: the GPU holds one picture at a time */
        gpuWaitPhase(true);
        if (fake != NULL && fake->waitEnd != NULL) {
            fake->waitEnd(); /* the test's GPU end */
        }
        (void)rhi_wait_submitted();
        gpuWaitPhase(false);
        __atomic_add_fetch(&s_gpuFirstWaits, 1u, __ATOMIC_RELEASE);
    }
    {
        void (*hook)(void) = __atomic_load_n(&s_testHook, __ATOMIC_ACQUIRE);
        if (hook != NULL && t_onS) {
            hook(); /* the test's job that does not end: a driver's present that blocks */
        }
    }
    if (fake != NULL && fake->present != NULL) {
        fake->present(); /* the test's driver's present call (it may wait for the GPU) */
    }
    if (j->present) {
        /* the even cadence's desired time for this present (0: any time) */
        rhi_present_desire(j->desiredMs > 0.0 ? (uint64_t)(j->desiredMs * 1e6) : 0u);
        rhi_present();
    }
    if (sample) {
        gpuSample(before, gpuDone(fake, &mark));
    }
    j->presentMs = rd__now_ms() - t1;
    rd__present_split_take(j, j->present);
}

/* a finished job's times into its replay's record and the render thread's
   block (on the thread that waited for it) */
static void apply(const RdSubmitJob *j)
{
    rd__perf_submit_times(j);
    rd__thread_note_submit(j->submitMs + j->presentMs);
}

static void stMain(void *u)
{
    (void)u;
    t_onS = true;
    if (s_st.begin != NULL) {
        s_st.begin(s_st.user);
    }
    ico_mutex_lock(s_st.m);
    for (;;) {
        RdSubmitJob j;
        if (rd__sq_next(&s_st.q, &j)) {
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
        if (s_st.quit) {
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
        rd__log("submit thread: with Frame rate Original each picture's GPU work may be waited "
                "for before its present, outside the swapchain's lock (the next picture walked, "
                "acquired and recorded meanwhile, handed over instead of skipped as busy, "
                "submitted after that present: the GPU holds one picture at a time): decided "
                "from the first %u Original presents whose picture's GPU work is still running "
                "as the driver's present call begins, on where %u or more of them return at "
                "that work's end (a driver whose present waits for the GPU); %s, issue 179",
                (unsigned)RD_GPU_FIRST_SAMPLES, (unsigned)RD_GPU_FIRST_NEED, gpuFirstWhere());
    } else if (want) {
        rd__log("submit thread: Frame rate Original's pictures present as before, their GPU "
                "work not waited for first (%s: the next picture's graphics calls wait for the "
                "submit thread anyway; %s)",
                notApart, gpuFirstWhere());
    }
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
    s_st.verdict = RD_GPU_FIRST_UNDECIDED; /* taken anew at every start */
    __atomic_store_n(&s_waitAlone, 0, __ATOMIC_RELEASE);
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
    return t_onS;
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
