/* rd_submit.c: a replay's end (rd_submit.h). */
#include "rd_submit.h"

#include "rd_internal.h"
#include "rd_passring.h"
#include "rd_submit_thread.h"

/* the background pipelines' step kept for the render thread's next
 * picture (rd__submit_pipe_tick; the render thread's own) */
static _Thread_local bool t_tickKept;
static _Thread_local double t_tickMs;

/* after rd__submit_drain's waits (or the slot ahead's, rd__submit_drain_kept) */
static void drainKept(bool apart)
{
    /* a picture's first graphics call: the textures retired since the
     * last job (rd_thread.h "retire"; a destroy's call guard waits for the
     * submit thread where it still works) */
    rd__thread_retire_kept();
    if (t_tickKept && (apart || !rd_thread_submit_busy())) {
        /* apart, the step runs now: its calls wait for the submit thread
         * through the call guard (while the background pipelines finish) */
        t_tickKept = false;
        rd__pipe_finish_tick(t_tickMs);
    }
}

void rd__submit_drain(void)
{
    const bool apart = rd_thread_submit_apart();
    if (apart) {
        /* the render thread with the frame slots apart (rd_submit_thread.h
         * "the slots"): the jobs of the slot the next rhi_wait_frame
         * recycles (the picture two before) finished; the picture before
         * goes on being submitted and presented beside this one */
        (void)rd_thread_submit_wait_slot((rhi_frame_slot() + 1u) % RHI_FRAMES_IN_FLIGHT);
    } else {
        /* on the game thread with the render thread on, the replays are its
         * (rd_thread.h): nothing of the last one may still run; on the
         * render thread, the submit thread's list before
         * (rd_submit_thread.h) */
        (void)rd_thread_drain();
    }
    drainKept(apart);
}

void rd__submit_drain_kept(void)
{
    drainKept(rd_thread_submit_apart());
}

void rd__submit_drain_acquire(void)
{
    if (rd_thread_submit_apart()) {
        /* the picture's own slot has no job left (rd__wait_frame waited
         * for them); the picture before may still be presented: the
         * backend's acquire waits for it where the swapchain has no image
         * to spare (vk_swapchain.c rhi_acquire_backbuffer) */
        rd_thread_submit_reap();
        return;
    }
    rd__submit_drain();
}

void rd__submit_pipe_tick(double pictureMs)
{
    if (rd_thread_on_render() && rd_thread_submit_busy()) {
        t_tickKept = true;
        t_tickMs = pictureMs;
        return;
    }
    rd__pipe_finish_tick(pictureMs);
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
    g_rd.deferText = false;
    /* a picture handed to the render thread: its end, submit and present
     * on the submit thread, posted at the job's end (rd_submit_thread.h);
     * asked before the reads end, which ends the hand-over */
    const bool kept = rd__submit_defer(cl, present);
    /* the render thread's reads of the frames, textures, meshes and
     * targets end with the walk and the present's pass: the game thread's
     * calls that change them go on from here (rd_thread.h) */
    rd__reads_done();
    if (kept) {
        /* the times come with the submit thread's (rd__perf_submit_times) */
        g_rdPerf.submitMs = g_rdPerf.presentMs = 0.0;
        rd__thread_note_replay(g_rdPerf.uploadMs + g_rdPerf.walkMs, 0.0,
                               g_rdPerf.waitMs + g_rdPerf.acquireMs);
        return;
    }
    t = rd__now_ms();
    rhi_end_commands(cl);
    rhi_submit(cl);
    g_rdPerf.submitMs = (t1 = rd__now_ms()) - t;
    t = t1;
    const bool shows = present && rd__present_will_show(); /* rhi_present is called */
    if (present) {
        rd__present_finish();
        rd__capture_finish(); /* a capture this present copied */
    }
    g_rdPerf.presentMs = rd__now_ms() - t;
    {
        /* the present timed apart, as the submit thread's (rd_submit_thread.c tail) */
        RdSubmitJob sj;
        memset(&sj, 0, sizeof(sj));
        sj.replay = g_rdPerf.replay;
        rd__present_split_take(&sj, shows);
        g_rdPerf.presentSplit = (uint8_t)sj.presentSplit;
        g_rdPerf.presentLockMs = sj.presentLockMs;
        g_rdPerf.presentCallMs = sj.presentCallMs;
        g_rdPerf.presentPollMs = sj.presentPollMs;
        g_rdPerf.presentEndMs = sj.presentEndMs;
    }
    if (present) {
        /* the window's hold on further presents while they wait (rd_thread.h) */
        rd__thread_note_present(g_rdPerf.presentMs);
    }
    rd__thread_note_replay(g_rdPerf.uploadMs + g_rdPerf.walkMs,
                           g_rdPerf.submitMs + g_rdPerf.presentMs,
                           g_rdPerf.waitMs + g_rdPerf.acquireMs);
}
