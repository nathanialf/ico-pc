/* rd_submit_thread.c: the pictures' submit and present on a thread of their
 * own (rd_submit_thread.h). */
#include "rd_submit_thread.h"

#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
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
} s_st;

/* the calling thread is the submit thread */
static _Thread_local bool t_onS;
/* the render thread's list kept open for it (rd__submit_defer), posted at
   its job's end (rd__submit_post_pending) */
static _Thread_local bool t_kept;
static _Thread_local RdSubmitJob t_job;

/* the list's end, its submit and the present: rd__submit_frame's tail */
static void tail(RdSubmitJob *j)
{
    const RhiCommandList cl = {j->cl};
    const double t0 = rd__now_ms();
    rhi_end_commands(cl);
    rhi_submit(cl);
    const double t1 = rd__now_ms();
    j->submitMs = t1 - t0;
    if (j->present) {
        rhi_present();
    }
    j->presentMs = rd__now_ms() - t1;
}

/* a finished job's times into its replay's record and the render thread's
   block (on the thread that waited for it) */
static void apply(const RdSubmitJob *j)
{
    rd__perf_submit_times(j->replay, j->submitMs, j->presentMs);
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
            ico_mutex_lock(s_st.m);
            {
                const double ms = j.submitMs + j.presentMs;
                const uint64_t ns = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u; /* 0 is "none" */

                s_st.jobMaxNs = ns > s_st.jobMaxNs ? ns : s_st.jobMaxNs;
            }
            rd__sq_finish(&s_st.q, &j);
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
    rd__sq_init(&s_st.q);
    s_st.begin = begin;
    s_st.user = user;
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
            "presented on a thread of their own, %s; ICO_RD_SUBMIT_THREAD=0 keeps them on the "
            "render thread)",
            rhi_backend_kind_name(rhi_backend()));
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
    rd__log("submit thread: ended (%llu pictures ended, submitted and presented there in all)",
            (unsigned long long)s_st.q.done);
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
    return __atomic_load_n(&s_st.busy, __ATOMIC_ACQUIRE) != 0;
}

bool rd_thread_submit_unfinished(void)
{
    return __atomic_load_n(&s_st.busy, __ATOMIC_ACQUIRE) != 0;
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

bool rd__submit_defer(RhiCommandList cl, bool present)
{
    if (s_st.th == NULL || t_onS || t_kept || cl.id == 0 || !rd_thread_on_render() ||
        !rd__thread_handed() || rd__capture_pending()) {
        return false;
    }
    memset(&t_job, 0, sizeof(t_job));
    t_job.cl = cl.id;
    t_job.present = present && rd__present_will_show();
    t_job.replay = g_rdPerf.replay;
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
        __atomic_store_n(&s_st.busy, 1, __ATOMIC_RELEASE);
        ico_cond_broadcast(s_st.c);
        ico_mutex_unlock(s_st.m);
        return;
    }
    ico_mutex_unlock(s_st.m);
    tail(&t_job); /* the thread ends: here */
    apply(&t_job);
}
