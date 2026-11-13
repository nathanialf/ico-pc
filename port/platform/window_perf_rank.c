/*
 * port/platform/window_perf_rank.c
 *
 * The window's performance lines by scene (window_perf_rank.h).
 */
#include "window_perf_rank.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "ico_gamestate.h"
#include "log_cost.h"
#include "rd_perf_more.h"
#include "rd_perf_rank.h"
#include "rhi.h"
#include "window_lines.h" /* window_log_cost_part */
#include "window_stall.h" /* window_stall_tally_take */

/* the window's thread and the lifecycle's (SDL's posting thread) both
   reach the block and the run: every entry point holds the lock */
static SDL_SpinLock s_lock;
static RdPerfRankBlock s_block;
static RdPerfRankRun s_run;
static double s_runSeconds; /* the blocks' seconds since the last profile */
static unsigned s_profiles; /* profiles written */
static uint64_t s_endNs;    /* the last profile at an end (the lifecycle's, the close's) */
/* the last byte-bound fit that held this run (its ms a MB): the estimates'
   rate in a block whose own fit has no spread */
static double s_mbMs;
/* the block in progress's scene: its steps' time by kind, the stage and its
   name at the last step (the window's thread reads the game's state) */
static SceneShare s_share;
static int s_stage;
static char s_stageName[64];
/* the last block's info (the target rate for a block in progress) */
static RdPerfRankInfo s_lastInfo;
static bool s_haveLast;
/* the last block's records, and those through the submit thread */
static uint32_t s_lastN, s_lastSubN;

/* the block's presented pictures' GPU spans, timed pass by pass or not
   (rd_perf_rank_stamp_cost_text: what the stamps cost) */
#define STAMP_SPANS 1024u
static float s_stamped[STAMP_SPANS], s_plain[STAMP_SPANS];
static uint32_t s_stampedN, s_plainN;

/* the cost counters' block (rd_perf_more.h): the costs and compare lines */
static RdPerfMore s_more;

void window_perf_rank_record(const RdPerfRecord *r, double vuInvocations)
{
    SDL_LockSpinlock(&s_lock);
    rd_perf_rank_add_record(&s_block, r, vuInvocations);
    rd_perf_more_add(&s_more, r, rd_vu_prepass_on());
    /* the shadow cuts' rows count from the record (rd.h: their counters) */
    ico_attempt_fire(ICO_ATT_DATE_ONE, r->dateOneShared);
    ico_attempt_fire2(ICO_ATT_DATE_ONE, r->dateOneRefused);
    ico_attempt_fire(ICO_ATT_EMPTY_SKIP, r->emptySkipped);
    ico_attempt_fire(ICO_ATT_NO_VOLUME_BREAK, r->noVolumeBreak);
    /* the subpass read's DATE set-ups (rd_subpass_probe.c): the stencil
       bit's set-ups are the read's own only where the row is on */
    if (ico_attempt_state_of(ICO_ATT_SUBPASS) == ICO_ATT_ON) {
        ico_attempt_fire(ICO_ATT_SUBPASS, r->dateStencilTakes);
    }
    if (r->presented && r->gpuValid && !r->keep) {
        if (r->passTimed && s_stampedN < STAMP_SPANS) {
            s_stamped[s_stampedN++] = (float)r->gpuMs;
        } else if (!r->passTimed && s_plainN < STAMP_SPANS) {
            s_plain[s_plainN++] = (float)r->gpuMs;
        }
    }
    SDL_UnlockSpinlock(&s_lock);
}

void window_perf_rank_pieces(const RdPerfRecord *r, const RdPerfPass *p, uint32_t n)
{
    SDL_LockSpinlock(&s_lock);
    rd_perf_rank_add_pieces(&s_block, r, p, n);
    SDL_UnlockSpinlock(&s_lock);
}

void window_perf_rank_step(SceneKind kind, int stage, uint64_t ns)
{
    const char *name = ico_gs_stage_name();
    SDL_LockSpinlock(&s_lock);
    window_scene_note(&s_share, kind, ns);
    if (stage != s_stage ||
        strncmp(s_stageName, name != NULL ? name : "", sizeof s_stageName - 1)) {
        s_stage = stage;
        snprintf(s_stageName, sizeof s_stageName, "%s", name != NULL ? name : "");
    }
    SDL_UnlockSpinlock(&s_lock);
}

/* the scene's key (window_scene_key): the stage and its name on every kind,
   the keep frames when they were most of the block's pictures (the black
   boot, a held screen) */
static void sceneName(SceneKind kind, int stage, const char *stageName, char *buf, size_t n)
{
    const bool keep = s_block.n && 2u * s_block.keep > s_block.n;
    (void)window_scene_key(buf, n, kind, stage, stageName, keep);
}

/* the profile's lines; the caller holds the lock.  partial: the block in
   progress too (its own line), named from the steps so far */
static void profile(const char *why, bool partial)
{
    static char line[2400];
    if (s_run.count == 0 && !(partial && s_block.n)) {
        return;
    }
    s_profiles++;
    fprintf(stderr, "window: performance profile (%s, %u scenes, profile %u of the run):\n", why,
            s_run.count, s_profiles);
    for (uint32_t i = 0; i < s_run.count; i++) {
        (void)rd_perf_rank_profile_text(&s_run, i, line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
    if (partial && s_block.n) {
        char scene[RD_PERF_RANK_NAME];
        sceneName(window_scene_dominant(&s_share), s_stage, s_stageName, scene, sizeof scene);
        RdPerfRankInfo info;
        memset(&info, 0, sizeof(info));
        if (s_haveLast) {
            info = s_lastInfo;
        }
        info.scene = scene;
        info.presentedFps = rd_perf_rank_partial_fps(&s_block);
        info.gameFps = info.gameRateFps = 0.0; /* not counted between blocks */
        info.prepass = rd_vu_prepass_on();
        info.earlierMbMs = s_mbMs;
        static RdPerfRankVerdict v;
        rd_perf_rank_verdict(&s_block, &info, &v);
        (void)rd_perf_rank_partial_text(&s_block, &info, &v, line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
}

static void window_perf_rank_block_lines(const WindowPerfBlock *in);

/* The attempt ledger (attempt_ledger.h): its start line at the first
   block and again when a state changed (the first 8 changes, then every
   100th), its block line and the stalls' tally (stall_coincide.h) every
   block */
static void ledger_lines(void)
{
    static uint32_t s_serial;
    static unsigned s_starts;
    static int s_said;
    /* the start line is over 2 KB on every device class with v0.11.6's
       rows and their reasons (2054 to 2391 bytes measured over
       attempt_ledger_test's seven classes before the old robust-access
       row): a 2 KB buffer cut its last rows */
    static char text[4096];
    if (!s_said) {
        /* the slow-scene capture in two steps runs on every platform (its
           fallbacks counted by the capture, window_dump_auto.c) */
        ico_attempt_decide_capture();
    }
    {
        /* the Adreno attempt (rhi_pass_row.h): its state from the device's
           row, every block (a switch of the graphics API makes a device
           again); its count the block's growth of the passes begun as
           VkRenderPass objects, only while the row holds (the counters
           start from 0 when the backend is made again) */
        static uint64_t s_passLast;
        const char *row = rhi_limits()->passObjectsRow;
        RhiStats st;
        rhi_get_stats(&st);
        ico_attempt_decide_pass_objects(row);
        if (row != NULL) {
            ico_attempt_fire(ICO_ATT_ADRENO_A, st.passObjects >= s_passLast
                                                   ? st.passObjects - s_passLast
                                                   : st.passObjects);
        }
        s_passLast = st.passObjects;
    }
    const uint32_t serial = ico_attempt_serial();
    if (!s_said || serial != s_serial) {
        s_serial = serial;
        s_said = 1;
        const unsigned n = ++s_starts;
        if (n <= 8u || n % 100u == 0u) {
            (void)ico_attempt_start_text(text, sizeof text);
            fprintf(stderr, "window: %s%s\n", text, n > 1u ? " (a state changed)" : "");
        }
    }
    char tally[512];
    const int stalls = window_stall_tally_take(tally, sizeof tally);
    if (ico_attempt_block_text(text, sizeof text, stalls) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
    if (tally[0] != '\0') {
        fprintf(stderr, "window: %s\n", tally);
    }
}

void window_perf_rank_block(const WindowPerfBlock *in)
{
    window_log_cost_mark("the display and slow scene lines"); /* before it (log_cost.h) */
    window_perf_rank_block_lines(in);
    if (in != NULL) {
        ledger_lines();
    }
    window_log_cost_part(LOG_COST_RANK);
}

static void window_perf_rank_block_lines(const WindowPerfBlock *in)
{
    SDL_LockSpinlock(&s_lock);
    if (in == NULL || s_block.n == 0) {
        rd_perf_rank_block_reset(&s_block);
        rd_perf_more_reset(&s_more);
        window_scene_reset(&s_share);
        s_stampedN = s_plainN = 0;
        s_lastN = s_lastSubN = 0;
        SDL_UnlockSpinlock(&s_lock);
        return; /* no replays: a movie, a pause in the background */
    }
    char scene[RD_PERF_RANK_NAME];
    sceneName(in->kind, in->stage, in->stage == s_stage ? s_stageName : "", scene, sizeof scene);
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
    info.prepass = rd_vu_prepass_on();
    info.earlierMbMs = s_mbMs;

    static RdPerfRankVerdict v;
    RdPerfRankRates rates;
    rd_perf_rank_verdict(&s_block, &info, &v);
    rd_perf_rank_rates(&s_block, s_mbMs, &rates);
    if (rates.smallGood) {
        s_mbMs = rates.mbMs; /* the next blocks' rate where their own fit fails */
    }

    static char line[4096], ranked[2400];
    (void)rd_perf_rank_scene_text(&s_block, &info, &v, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    s_block.tiler = info.tiler; /* before list 0 and list 0 as one there */
    (void)rd_perf_rank_classes_text(&s_block, rd_perf_pass_label, 6, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_rates_text(&s_block, &rates, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    (void)rd_perf_rank_slowest_text(&s_block, rd_perf_pass_label, 5, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    if (rd_perf_rank_stamp_cost_text(s_stamped, s_stampedN, s_plain, s_plainN, line, sizeof line) >
        0) {
        fprintf(stderr, "window: %s\n", line); /* both kinds in the block */
    }
    (void)rd_perf_rank_ranked_text(&info, &v, ranked, sizeof ranked);
    if (v.below) {
        /* every block below the rate: the 10 s summaries are the rate limit */
        fprintf(stderr, "window: %s\n", ranked);
    }
    /* the costs a device's next change would cut, and the fields two logs
       of one scene are compared by (rd_perf_more.h) */
    if (rd_perf_more_cost_text(&s_more, line, sizeof line) > 0) {
        fprintf(stderr, "window: %s\n", line);
    }
    (void)rd_perf_more_compare_text(&s_more, scene, info.tiler, rhi_limits()->robustText, line,
                                    sizeof line);
    fprintf(stderr, "window: %s\n", line);
    rd_perf_more_reset(&s_more);
    rd_perf_rank_run_add(&s_run, &s_block, &info, &v, in->seconds, ranked);
    s_lastInfo = info;
    s_lastInfo.scene = NULL; /* the block's own buffer */
    s_haveLast = true;
    s_lastN = s_block.n;
    s_lastSubN = s_block.subN;
    s_runSeconds += in->seconds;
    if (s_runSeconds >= 300.0) {
        s_runSeconds = 0.0;
        profile("every 300 s of the run", false);
    }
    rd_perf_rank_block_reset(&s_block);
    window_scene_reset(&s_share);
    s_stampedN = s_plainN = 0;
    SDL_UnlockSpinlock(&s_lock);
}

void window_perf_rank_run_end(void)
{
    SDL_LockSpinlock(&s_lock);
    if (rd_perf_rank_profile_due(&s_endNs, SDL_GetTicksNS())) {
        profile("the run's end", true);
    }
    SDL_UnlockSpinlock(&s_lock);
}

void window_perf_rank_lifecycle(void *user, int terminating)
{
    (void)user;
    SDL_LockSpinlock(&s_lock);
    if (rd_perf_rank_profile_due(&s_endNs, SDL_GetTicksNS())) {
        profile(terminating ? "the app ending" : "the app going to the background", true);
    }
    SDL_UnlockSpinlock(&s_lock);
}

size_t window_perf_rank_cpu_note(char *out, size_t cap)
{
    SDL_LockSpinlock(&s_lock);
    const uint32_t n = s_lastN, sub = s_lastSubN;
    SDL_UnlockSpinlock(&s_lock);
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (sub == 0) {
        return 0;
    }
    const int w = snprintf(
        out, cap, "; the CPU adds the submit thread's submit and present in %u of %u replays", sub,
        n);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}
