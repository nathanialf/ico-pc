/* rd_submit.c: a replay's end (rd_submit.h). */
#include "rd_submit.h"

#include "rd_internal.h"
#include "rd_passring.h"

/* the list handed over and its own times; one at a time: the next replay
 * waits for it (rd__submit_drain) before anything of its own */
typedef struct SubmitJob {
    RhiCommandList cl;
    bool present;
    double submitMs, presentMs;
} SubmitJob;

static SubmitJob s_job;

static RdSubmitHost s_host;
static bool s_hostOn, s_hold, s_async;
/* the last finished hand-over's times (rd__submit_frame's g_rdPerf), and
 * the waits for it */
static double s_lastSubmitMs, s_lastPresentMs;
static uint64_t s_waitedNs;

/* the end of a replay's list, on the render thread or here */
static void submitJob(void *arg)
{
    SubmitJob *j = arg;
    double t = rd__now_ms(), t1;
    rhi_end_commands(j->cl);
    rhi_submit(j->cl);
    j->submitMs = (t1 = rd__now_ms()) - t;
    t = t1;
    if (j->present) {
        rd__present_finish();
        rd__capture_finish(); /* a capture this present copied */
    }
    j->presentMs = rd__now_ms() - t;
}

void rd_set_submit_host(const RdSubmitHost *host)
{
    rd__submit_drain();
    if (host != NULL && host->post != NULL && host->drain != NULL) {
        s_host = *host;
        s_hostOn = true;
    } else {
        s_hostOn = false;
        s_host = (RdSubmitHost){0};
    }
    s_async = false;
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

void rd__submit_drain(void)
{
    if (!s_hostOn) {
        return;
    }
    s_waitedNs += s_host.drain(s_host.user);
    if (s_async) {
        /* the render thread's times of the picture it finished */
        s_lastSubmitMs = s_job.submitMs;
        s_lastPresentMs = s_job.presentMs;
        s_async = false;
    }
}

void rd__submit_frame(RhiCommandList cl, bool present, double walkStart, double bind0)
{
    double t = walkStart, t1;
    if (present) {
        uint32_t sw = 0, sh = 0; /* the pass ring's present entry: DISPLAY onto the window */
        rhi_swapchain_size(&sw, &sh);
        const RdTargetRec *dt = rd__target_rec(RD_TARGET_DISPLAY + 1);
        rhi_cmd_checkpoint(cl,
                           rd__passring_push(RD_PASSK_PRESENT, RD_TARGET_DISPLAY + 1,
                                             dt ? dt->tw : 0, dt ? dt->th : 0, 0, sw, sh, 0, -1));
        rd__perf_pass(cl, RD_PERF_PASS_PRESENT, RD_PERF_ID_WINDOW, sw, sh, -1);
        rd__present_record(cl);
        rd__perf_stamp(cl, RD_PERF_TS_PRESENT);
    }
    rd__perf_pass_end(cl); /* the last piece's end (rd_perf.c) */
    g_rdPerf.walkMs = (t1 = rd__now_ms()) - t - (g_rdPerf.bindMs - bind0);
    rd__perf_walk_end(); /* the walk's pipelines and creations end here */
    s_job.cl = cl;
    s_job.present = present;
    s_job.submitMs = s_job.presentMs = 0.0;
    /* a capture's finish reads back and hands the picture on: the replay
     * waits for it, as for every picture while held */
    const bool wait = s_hold || (present && rd__capture_pending());
    if (s_hostOn && s_host.post(s_host.user, submitJob, &s_job, wait)) {
        s_async = !wait;
        g_rdPerf.submitMs = wait ? s_job.submitMs : s_lastSubmitMs;
        g_rdPerf.presentMs = wait ? s_job.presentMs : s_lastPresentMs;
        return;
    }
    submitJob(&s_job);
    g_rdPerf.submitMs = s_job.submitMs;
    g_rdPerf.presentMs = s_job.presentMs;
}
