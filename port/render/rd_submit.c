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

/* The present's own list (rd__submit_frame), begun after the scene's list
 * ended: id 0 when the backend has no list left (VKR_MAX_CMD_LISTS a frame
 * slot), logged the first time and every 100th; the picture is then not
 * presented and its image is the next acquire's (vk_swapchain.c) */
static RhiCommandList presentList(void)
{
    static unsigned s_none;
    const RhiCommandList cl2 = rhi_begin_commands();
    if (cl2.id == 0 && s_none++ % 100u == 0) {
        rd__log("present: no command list left for the present's own list (%u times so far): "
                "this picture is not presented",
                s_none);
    }
    return cl2;
}

void rd__submit_frame(RhiCommandList cl, bool present, double walkStart, double bind0)
{
    double t = walkStart, t1;
    RhiCommandList cl2 = {0};
    RhiCommandList pl = cl;  /* the list the present's pass is recorded into */
    uint32_t sw = 0, sh = 0; /* the pass ring's present entry: DISPLAY onto the window */
    static unsigned s_shared;
    bool split = present;
    if (split && !rhi_commands_left()) {
        /* no list left for the present's own (VKR_MAX_CMD_LISTS a slot):
         * the present's pass goes into the scene's list as before v0.10.5,
         * so the picture is presented and its acquired image is not left
         * for a present that never comes (the other slot could acquire a
         * second image meanwhile and present this one undrawn) */
        split = false;
        if (s_shared++ % 100u == 0) {
            rd__log("present: no command list left for the present's own list (%u times so far): "
                    "its pass shares the scene's list this picture",
                    s_shared);
        }
    }
    if (split) {
        /* A presenting picture is two lists: the scene's, which ends here,
         * and the present's pass alone, so that only the second's submit
         * waits for the window's image (port/rhi/vk/vk_cmd.c
         * vkr_submit_batch: a semaphore wait holds every colour pass of its
         * batch, and with one list the scene's off-screen passes waited for
         * the presentation engine too: issue 179, 38-84 ms of a 88-143 ms
         * scene picture on a Mali).  The display wait's piece runs from the
         * scene list's last command to the present's list's first
         * (RdPerfRecord.gpuDisplayWaitMs).  The scene list ends before the
         * present's begins: two lists recording at once take the global
         * barriers (vk_cmd.c rhi_begin_commands), and the end is a slot
         * call, so the render thread does not wait for the submit thread's
         * picture before (rhi.h rhi_set_slot_guard).  The DATE and MSAA
         * epochs closed before (rd_replay.c), the present's reads go through
         * barriers of their own (rd__present_record), and the second list's
         * first barrier orders it after the first (vk_cmd.c "Hazards") */
        rhi_swapchain_size(&sw, &sh);
        rd__perf_pass(cl, RD_PERF_PASS_WAIT, RD_PERF_ID_WINDOW, sw, sh, -1);
        rhi_end_commands(cl);
        cl2 = presentList();
        pl = cl2;
        /* without it the display wait's piece stays open, untimed (its end
           stamp would go into an ended list) */
        present = cl2.id != 0;
    }
    if (present) {
        const RdTargetRec *dt = rd__target_rec(RD_TARGET_DISPLAY + 1);
        rd__perf_stamp(pl, RD_PERF_TS_PRESENT_BEGIN);
        rhi_cmd_checkpoint(pl,
                           rd__passring_push(RD_PASSK_PRESENT, RD_TARGET_DISPLAY + 1,
                                             dt ? dt->tw : 0, dt ? dt->th : 0, 0, sw, sh, 0, -1));
        rd__perf_pass(pl, RD_PERF_PASS_PRESENT, RD_PERF_ID_WINDOW, sw, sh, -1);
        rd__present_record(pl);
        rd__perf_stamp(pl, RD_PERF_TS_PRESENT);
        rd__perf_pass_end(pl); /* the last piece's end (rd_perf.c) */
    } else if (!split) {
        rd__perf_pass_end(cl);
    }
    g_rdPerf.walkMs = (t1 = rd__now_ms()) - t - (g_rdPerf.bindMs - bind0);
    rd__perf_walk_end(); /* the walk's pipelines and creations end here */
    g_rd.deferText = false;
    /* a picture handed to the render thread: its end, submit and present
     * on the submit thread, posted at the job's end (rd_submit_thread.h);
     * asked before the reads end, which ends the hand-over */
    const bool kept = rd__submit_defer(cl, cl2, present);
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
    {
        /* the lists' ends and submits in the submit thread's order */
        RdSubmitJob lj;
        memset(&lj, 0, sizeof(lj));
        lj.cl = cl.id;
        lj.cl2 = cl2.id;
        rd__submit_lists(&lj);
    }
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
