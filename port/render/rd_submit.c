/* rd_submit.c: a replay's end (rd_submit.h). */
#include "rd_submit.h"

#include "rd_internal.h"
#include "rd_passring.h"

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
    t = t1;
    rhi_end_commands(cl);
    rhi_submit(cl);
    g_rdPerf.submitMs = (t1 = rd__now_ms()) - t;
    t = t1;
    if (present) {
        rd__present_finish();
        rd__capture_finish(); /* a capture this present copied */
    }
    g_rdPerf.presentMs = rd__now_ms() - t;
}
