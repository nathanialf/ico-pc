/*
 * port/platform/window_perf_rank.c
 *
 * The window's performance lines by scene (window_perf_rank.h).
 */
#include "window_perf_rank.h"

#include <stdio.h>
#include <string.h>

#include "rd_perf_rank.h"
#include "rhi.h"

static RdPerfRankBlock s_block;
static RdPerfRankRun s_run;
static double s_runSeconds; /* the blocks' seconds since the last profile */
static unsigned s_profiles; /* profiles written */

void window_perf_rank_record(const RdPerfRecord *r, double vuInvocations)
{
    rd_perf_rank_add_record(&s_block, r, vuInvocations);
}

void window_perf_rank_pieces(const RdPerfRecord *r, const RdPerfPass *p, uint32_t n)
{
    rd_perf_rank_add_pieces(&s_block, r, p, n);
}

/* the scene's name: the stage for play and scenes, the keep frames when
   they were most of the block's pictures (the black boot, a held screen) */
static void sceneName(const WindowPerfBlock *in, char *buf, size_t n)
{
    const char *word = window_scene_kind_word(in->kind);
    const char *keep = s_block.n && 2u * s_block.keep > s_block.n ? " (keep frames)" : "";
    if (in->kind == SCENE_PLAY || in->kind == SCENE_SCENE) {
        snprintf(buf, n, "stage %d %s%s", in->stage, word, keep);
    } else {
        snprintf(buf, n, "%s%s", word, keep);
    }
}

static void profile(const char *why)
{
    char line[2400];
    s_profiles++;
    fprintf(stderr, "window: performance profile (%s, %u scenes, profile %u of the run):\n", why,
            s_run.count, s_profiles);
    for (uint32_t i = 0; i < s_run.count; i++) {
        (void)rd_perf_rank_profile_text(&s_run, i, line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
}

void window_perf_rank_block(const WindowPerfBlock *in)
{
    if (in == NULL || s_block.n == 0) {
        rd_perf_rank_block_reset(&s_block);
        return; /* no replays: a movie, a pause in the background */
    }
    char scene[48];
    sceneName(in, scene, sizeof scene);
    const double gameRate = in->paceHz > 0 ? (double)in->paceHz / 2.0 : 0.0;
    RdPerfRankInfo info;
    memset(&info, 0, sizeof(info));
    info.scene = scene;
    info.presentedFps = in->presentedFps;
    info.gameFps = in->gameFps;
    info.gameRateFps = gameRate;
    info.targetFps = in->original || in->displayHz <= 0.0 ? gameRate : in->displayHz;
    info.stepCpuMs = in->stepCpuMs;
    info.tiler = rhi_limits()->tiler;

    RdPerfRankVerdict v;
    RdPerfRankRates rates;
    rd_perf_rank_verdict(&s_block, &info, &v);
    rd_perf_rank_rates(&s_block, &rates);

    static char line[4096], ranked[2400];
    (void)rd_perf_rank_scene_text(&s_block, &info, &v, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_classes_text(&s_block, rd_perf_pass_label, 6, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_rates_text(&s_block, &rates, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_slowest_text(&s_block, rd_perf_pass_label, 5, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_ranked_text(&info, &v, ranked, sizeof ranked);
    if (v.below) {
        /* every block below the rate: the 10 s summaries are the rate limit */
        fprintf(stderr, "window: %s\n", ranked);
    }
    rd_perf_rank_run_add(&s_run, &s_block, &info, &v, in->seconds, ranked);
    s_runSeconds += in->seconds;
    if (s_runSeconds >= 300.0) {
        s_runSeconds = 0.0;
        profile("every 300 s of the run");
    }
    rd_perf_rank_block_reset(&s_block);
}

void window_perf_rank_run_end(void)
{
    if (s_run.count == 0) {
        return;
    }
    profile("the run's end");
}
