/*
 * port/platform/window_bench.c
 *
 * A release candidate's bench, the window's side (window_bench.h).
 * Nothing here is compiled unless ICO_RC_TOOLS is on and the target is
 * Android.
 */
#include "window_bench.h"

#if ICO_RC_TOOLS && defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../core/region.h" /* the disc's layouts: the opening, the title, the pause */
#include "audio_host.h"
#include "bench_plan.h"
#include "diag_host.h"
#include "font.h" /* port/ui: the line on the screen */
#include "host_config.h"
#include "host_fs.h"
#include "rd.h"
#include "rd_bench_run.h"
#include "rhi.h"
#include "window_present.h"

#define s_pres g_windowPres

extern int stage_no;          /* common/src/main.c: 1 the boot, the opening and the title */
extern int current_layout_id; /* common/src/layout_texture.c: the menu screen */

/* 1: an rc build keeps frames of the opening and of a pause and measures
   them at the title and at the pause.  0 here: a variant that wants the
   measurement sets it to 1 in its own branch, as WINDOW_CAPTURE_DEFAULT */
#ifndef WINDOW_BENCH_DEFAULT
#define WINDOW_BENCH_DEFAULT 0
#endif

#define BENCH_TITLE_HOLD_NS 2000000000ull /* the title menu on the screen this long first */
#define BENCH_PAUSE_HOLD_NS 1000000000ull /* the pause menu likewise */
/* the frames' bytes held at most (ICO_BENCH_FRAMES of the largest) */
#define BENCH_BYTES_HELD ((size_t)ICO_BENCH_FRAMES * ICO_BENCH_FRAME_BYTES)

static int s_started;
static IcoBenchPick s_pick;
static RdBenchKept s_kept[ICO_BENCH_FRAMES];
static int s_nKept;     /* kept this run, whether run yet or not */
static int s_firstHeld; /* the first kept frame not yet run */
static size_t s_bytesHeld;
static Uint64 s_titleSince, s_pauseSince;
static int s_pauseDone;
static FILE *s_csv;
static char s_csvName[96];
static int s_csvHead; /* the header written */
static char s_text[2][192];
static volatile int s_textAt;
static const char *s_stopWhy;
static int s_background; /* the stop was the app going to the background (its lifecycle's) */
/* the skip's rule (bench_plan.h ico_bench_input_verdict): the run's start,
   the inputs passed over and the first of them, the press that skipped */
static Uint64 s_benchStart;
static int s_passedOver;
static char s_passedFirst[128];
static char s_skipWhy[128];

static int in_opening(void)
{
    return stage_no == 1 && current_layout_id == ico_region_cur()->layout_scene;
}

static int in_title(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    return stage_no == 1 && (current_layout_id == r->layout_title_continue ||
                             current_layout_id == r->layout_title_new);
}

static int in_pause(void)
{
    return current_layout_id == ico_region_cur()->layout_pause;
}

static int plan_of(uint32_t glowLight)
{
    switch (glowLight) {
    case RD_GLOW_LIGHT_DARK:
        return ICO_BENCH_PLAN_DARK;
    case RD_GLOW_LIGHT_LAMPS:
        return ICO_BENCH_PLAN_LAMPS;
    case RD_GLOW_LIGHT_WHOLE:
        return ICO_BENCH_PLAN_WHOLE;
    default:
        return ICO_BENCH_PLAN_NONE;
    }
}

void window_bench_start(void)
{
    if (s_started) {
        return; /* once a process */
    }
    s_started = 1;
    ico_bench_pick_init(&s_pick);
    if (!WINDOW_BENCH_DEFAULT) {
        fprintf(stderr, "window: bench: off (WINDOW_BENCH_DEFAULT 0: no frame is kept or "
                        "measured in this build)\n");
        return;
    }
    fprintf(stderr,
            "window: bench: on (a release candidate, WINDOW_BENCH_DEFAULT 1): four frames of the "
            "opening are kept (a lamps picture with a shadow volume and one without, a dark "
            "picture, a sun picture) and measured at the title, and a paused picture at the pause "
            "(at most %d frames a run); each is drawn again and again in turn with parts of it "
            "left out, the title or pause held meanwhile with a line on the screen (a tap skips "
            "the rest, at most %.0f s); the bench-*.csv beside the log is sent with \"Share a bug "
            "report\"\n",
            ICO_BENCH_FRAMES, ICO_BENCH_TOTAL_CAP_MS / 1000.0);
}

void window_bench_picture(const struct RdPerfRecord *r)
{
    if (!WINDOW_BENCH_DEFAULT || s_nKept >= ICO_BENCH_FRAMES) {
        return;
    }
    (void)ico_bench_pick_picture(&s_pick, in_opening(), plan_of(r->glowLight), r->noVolumeWith > 0,
                                 r->frame);
}

/* the frame's dump bytes kept (the game thread's copy, as the slow-scene
   dump's memory copy); 1 when kept */
static int keep(int kind, uint32_t number)
{
    void *buf = NULL;
    size_t len = 0;
    uint32_t dumped = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (s_nKept >= ICO_BENCH_FRAMES) {
        return 0;
    }
    if (!rd_dump_retained_frame_mem(number, &buf, &len, &dumped) || buf == NULL) {
        fprintf(stderr,
                "window: bench: %s (frame %u) not kept: the renderer no longer holds it "
                "or could not copy it\n",
                ico_bench_kind_name(kind), number);
        return 0;
    }
    if (len > ICO_BENCH_FRAME_BYTES || s_bytesHeld + len > BENCH_BYTES_HELD) {
        fprintf(stderr,
                "window: bench: %s (frame %u) not kept: %zu bytes, over the bench's bound "
                "of %u a frame and %zu held\n",
                ico_bench_kind_name(kind), dumped, len, ICO_BENCH_FRAME_BYTES, BENCH_BYTES_HELD);
        free(buf);
        return 0;
    }
    s_kept[s_nKept].bytes = buf;
    s_kept[s_nKept].len = len;
    s_kept[s_nKept].kind = kind;
    s_kept[s_nKept].number = dumped;
    s_nKept++;
    s_bytesHeld += len;
    fprintf(stderr,
            "window: bench: kept %s: frame %u, %zu bytes, %.1f ms on the game thread "
            "(frame %d of at most %d this run)\n",
            ico_bench_kind_name(kind), dumped, len, (double)(SDL_GetTicksNS() - t0) / 1e6, s_nKept,
            ICO_BENCH_FRAMES);
    return 1;
}

/* ------------------------------------------------------------ the run */

static void bench_overlay(const RdOverlayCtx *ctx, void *user)
{
    static const uint8_t title[4] = {0x80, 0x7C, 0x70, 0x80};
    static const uint8_t body[4] = {0x66, 0x64, 0x5E, 0x80};
    static const uint8_t back[4] = {0, 0, 0, 0x60};

    (void)user;
    ui_begin_overlay(ctx);
    ui_draw_rect(96.0f, 14.0f, 544.0f, 92.0f, back);
    ui_draw_text(UI_GRID_CX, 38.0f, 20.0f, title, "Measuring the picture (a test build's bench)",
                 UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_draw_text(UI_GRID_CX, 62.0f, 16.0f, body, s_text[s_textAt],
                 UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_draw_text(UI_GRID_CX, 84.0f, 16.0f, body, "Tap to skip it; the game goes on after it",
                 UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_end_overlay();
}

static void bench_status(void *user, const char *text)
{
    const int next = 1 - s_textAt;

    (void)user;
    snprintf(s_text[next], sizeof(s_text[next]), "%s", text);
    s_textAt = next;
}

static void bench_line(void *user, const char *text)
{
    (void)user;
    fprintf(stderr, "window: %s\n", text);
}

static void bench_csv(void *user, const char *text)
{
    (void)user;
    if (s_csv != NULL) {
        fprintf(s_csv, "%s\n", text);
    }
}

static const char *input_name(Uint32 type)
{
    switch (type) {
    case SDL_EVENT_FINGER_DOWN:
        return "a finger down";
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return "a mouse button";
    case SDL_EVENT_KEY_DOWN:
        return "a key";
    default:
        return "a pad button";
    }
}

/* the input's moment against the run's start, in seconds (negative: before) */
static double input_age(Uint64 ts)
{
    return ts >= s_benchStart ? (double)(ts - s_benchStart) / 1e9
                              : -(double)(s_benchStart - ts) / 1e9;
}

/* between replays: the app leaving stops the bench with its events left
   queued for the pump; a new tap, key or button press (taken) skips the
   rest, one from before the bench or inside its first half second (the
   touch that opened the pause, its key's repeat) is taken and passed over
   (bench_plan.h ico_bench_input_verdict); the moves are dropped so the
   queue does not fill */
static int bench_poll(void *user)
{
    static const Uint32 k_leave[] = {SDL_EVENT_QUIT,
                                     SDL_EVENT_TERMINATING,
                                     SDL_EVENT_WILL_ENTER_BACKGROUND,
                                     SDL_EVENT_DID_ENTER_BACKGROUND,
                                     SDL_EVENT_WINDOW_CLOSE_REQUESTED,
                                     SDL_EVENT_WINDOW_HIDDEN,
                                     SDL_EVENT_WINDOW_MINIMIZED};
    static const Uint32 k_down[] = {SDL_EVENT_FINGER_DOWN, SDL_EVENT_MOUSE_BUTTON_DOWN,
                                    SDL_EVENT_KEY_DOWN, SDL_EVENT_GAMEPAD_BUTTON_DOWN};
    SDL_Event e;
    int tap = 0;

    (void)user;
    SDL_PumpEvents();
    for (size_t i = 0; i < sizeof(k_leave) / sizeof(k_leave[0]); i++) {
        if (SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, k_leave[i], k_leave[i]) == 1) {
            s_stopWhy = "the game left the screen";
            s_background = k_leave[i] == SDL_EVENT_TERMINATING ||
                           k_leave[i] == SDL_EVENT_WILL_ENTER_BACKGROUND ||
                           k_leave[i] == SDL_EVENT_DID_ENTER_BACKGROUND;
            return RD_BENCH_LEFT;
        }
    }
    if (SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
                       SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) == 1) {
        s_stopWhy = "the window changed size";
        return RD_BENCH_SKIPPED;
    }
    for (size_t i = 0; i < sizeof(k_down) / sizeof(k_down[0]); i++) {
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, k_down[i], k_down[i]) == 1) {
            const int repeat = k_down[i] == SDL_EVENT_KEY_DOWN && e.key.repeat;
            const int v = ico_bench_input_verdict(e.common.timestamp, s_benchStart, repeat);

            if (v == ICO_BENCH_INPUT_SKIPS) {
                if (!tap) {
                    snprintf(s_skipWhy, sizeof(s_skipWhy), "a tap: %s %.2f s after the start",
                             input_name(k_down[i]), input_age(e.common.timestamp));
                }
                tap = 1;
            } else {
                if (s_passedOver++ == 0) {
                    snprintf(s_passedFirst, sizeof(s_passedFirst), "%s %s, %.2f s from the start",
                             input_name(k_down[i]), ico_bench_input_words(v),
                             input_age(e.common.timestamp));
                }
            }
        }
    }
    SDL_FlushEvent(SDL_EVENT_FINGER_MOTION);
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    SDL_FlushEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    SDL_FlushEvent(SDL_EVENT_JOYSTICK_AXIS_MOTION);
    if (tap) {
        s_stopWhy = s_skipWhy;
        return RD_BENCH_SKIPPED;
    }
    return 0;
}

static void csv_open(void)
{
    char logs[ICO_PATH_MAX], path[ICO_PATH_MAX], stamp[32];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (s_csv != NULL) {
        return;
    }
    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    snprintf(s_csvName, sizeof(s_csvName), "bench-%s.csv", stamp);
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_path_join(path, sizeof(path), logs, s_csvName) != 0 ||
        (s_csv = ico_fopen(path, "w")) == NULL) {
        fprintf(stderr, "window: bench: the CSV %s could not be made: the lines alone\n",
                s_csvName);
        s_csvName[0] = '\0';
    }
}

/* the kept frames first .. s_nKept - 1, run now: the pump held until the
   bench ends; the pacer moved past the time, as for a swapchain rebuilt */
static void run(const char *where)
{
    char pref[ICO_PATH_MAX], tmp[ICO_PATH_MAX];
    const int first = s_firstHeld, count = s_nKept - s_firstHeld;
    void *prevUser = NULL, *prevTopUser = NULL;
    RdOverlayFn prev, prevTop;
    RdBenchRunCfg cfg;
    RdBenchRunCb cb = {bench_poll, bench_status, bench_line, bench_csv, NULL};
    RdBenchRunOut out;
    int framesRun = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (count <= 0) {
        return;
    }
    s_firstHeld = s_nKept;
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(tmp, sizeof(tmp), pref, "bench-frame.rddump") != 0) {
        fprintf(stderr, "window: bench: no path for the frames' file: not run\n");
        return;
    }
    csv_open();
    fprintf(stderr,
            "window: bench: starts %s: %d frame%s, present mode %s; the screen held at most %.0f "
            "s (a tap skips the rest)\n",
            where, count, count == 1 ? "" : "s", rhi_present_mode_name(),
            ICO_BENCH_TOTAL_CAP_MS / 1000.0);
    ico_host_log_flush();
    /* a device loss or a hang in it is the bench's, not the game's: no loss
       step (diag_host.h ico_diag_set_probe) */
    ico_diag_set_probe("the bench (a release candidate's measurement)");
    ico_diag_watchdog_pause(1);
    ico_audio_sdl_pause(1);
    s_stopWhy = NULL;
    s_background = 0;
    s_benchStart = SDL_GetTicksNS(); /* a press after this and the guard skips the rest */
    s_passedOver = 0;
    s_passedFirst[0] = '\0';
    snprintf(s_skipWhy, sizeof(s_skipWhy), "a tap");
    bench_status(NULL, "getting ready");
    prev = rd_get_present_overlay(&prevUser);
    prevTop = rd_get_present_overlay_top(&prevTopUser);
    rd_set_present_overlay(bench_overlay, NULL);
    rd_set_present_overlay_top(NULL, NULL);
    memset(&cfg, 0, sizeof(cfg));
    cfg.w = ICO_BENCH_W;
    cfg.k = ICO_BENCH_K;
    cfg.rounds = ICO_BENCH_R;
    cfg.every = ICO_BENCH_EVERY;
    cfg.frameBudgetMs = ICO_BENCH_FRAME_BUDGET_MS;
    cfg.capMs = ICO_BENCH_TOTAL_CAP_MS;
    cfg.presentMode = rhi_present_mode_name();
    cfg.csvHeader = !s_csvHead;
    s_csvHead = 1;
    cfg.startMs = 0.0; /* the cap from the run's start (rd_bench_run_kept) */
    (void)rd_bench_run_kept(&s_kept[first], count, tmp, &cfg, &cb, &out, &framesRun);
    rd_set_present_overlay(prev, prevUser);
    rd_set_present_overlay_top(prevTop, prevTopUser);
    if (!(out.stopped == RD_BENCH_LEFT && s_background)) {
        /* where the app went to the background, its own lifecycle holds
           the sound and the watchdog and gives them back */
        ico_audio_sdl_pause(0);
        ico_diag_watchdog_pause(0);
    }
    if (out.stopped != RD_BENCH_LOST) {
        ico_diag_set_probe(NULL);
    }
    {
        const Uint64 dt = SDL_GetTicksNS() - t0;

        window_pace_paused(dt);
        s_pres.paceEnd = 0;
        s_pres.paceEndCpuNs = 0;
        memset(&s_pres.paceHist, 0, sizeof(s_pres.paceHist));
        s_pres.paceSlow = false;
        s_pres.skipOpen = false;
        s_pres.cost = 0;
        s_pres.lastReturn = 0;
        window_timer_budget_reset();
        if (s_csv != NULL) {
            fflush(s_csv);
        }
        fprintf(stderr,
                "window: bench: ended (%s%s%s) after %.1f s: %d of %d frames, %d of %d variants "
                "with a timed replay; %s%s; every switch put back, the game goes on\n",
                rd_bench_stop_words(out.stopped), s_stopWhy != NULL ? ": " : "",
                s_stopWhy != NULL ? s_stopWhy : "", (double)dt / 1e9, framesRun, count, out.ran,
                out.variants, s_csvName[0] ? "the tables in " : "no CSV",
                s_csvName[0] ? s_csvName : "");
        if (s_passedOver > 0) {
            fprintf(stderr,
                    "window: bench: %d press%s passed over, not taken as a skip (the first: %s; "
                    "a skip is a new press %.1f s or more after the start)\n",
                    s_passedOver, s_passedOver == 1 ? "" : "es", s_passedFirst,
                    (double)ICO_BENCH_SKIP_GUARD_NS / 1e9);
        }
    }
    for (int i = first; i < first + count; i++) {
        free((void *)s_kept[i].bytes);
        s_bytesHeld -= s_kept[i].len;
        s_kept[i].bytes = NULL;
        s_kept[i].len = 0;
    }
    ico_host_log_flush();
}

void window_bench_pump(void)
{
    const Uint64 now = SDL_GetTicksNS();

    if (!WINDOW_BENCH_DEFAULT) {
        return;
    }
    /* an armed moment of the opening: kept while the renderer holds it */
    if (s_pick.armed >= 0 && in_opening() && rd_frame_retained(s_pick.armedFrame)) {
        ico_bench_pick_done(&s_pick, keep(s_pick.armed, s_pick.armedFrame));
    }
    /* the title after the opening: the opening's frames */
    if (in_title()) {
        s_titleSince = s_titleSince ? s_titleSince : now;
    } else {
        s_titleSince = 0;
    }
    if (s_titleSince && now - s_titleSince >= BENCH_TITLE_HOLD_NS && s_firstHeld < s_nKept) {
        run("at the title");
        s_titleSince = 0;
        return;
    }
    /* a pause: the paused picture (and any frame still held) */
    if (in_pause()) {
        s_pauseSince = s_pauseSince ? s_pauseSince : now;
    } else {
        s_pauseSince = 0;
        s_pauseDone = 0;
    }
    if (s_pauseSince && !s_pauseDone && now - s_pauseSince >= BENCH_PAUSE_HOLD_NS) {
        s_pauseDone = 1;
        if (s_nKept < ICO_BENCH_FRAMES) {
            (void)keep(ICO_BENCH_PAUSE, rd_frame_number());
        }
        run("at the pause");
    }
}
#endif
