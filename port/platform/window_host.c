/*
 * port/platform/window_host.c
 *
 * The window, the renderer device on it and the real-time pacing
 * (window_host.h).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio_host.h"
#include "cdvd_host.h"
#include "config.h"
#include "diag_host.h"
#include "display_clock.h"
#include "dump_zip.h"
#include "font.h" /* port/ui: ico_window_progress's text and bar */
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h"
#include "ico_credits.h"
#include "ico_build_commit.h" /* ICO_BUILD_COMMIT, generated (port/platform/CMakeLists.txt) */
#include "ico_version.h" /* ICO_VERSION_LABEL, generated (CMakeLists.txt; "" in a development build) */
#include "input.h"
#include "input_record.h"
#include "input_sdl.h"
#include "mouse_look.h"
#include "options.h"
#include "pace_policy.h"
#include "perf_block.h"
#include "photo_mode.h"
#include "photo_ui.h"
#include "pointer.h"
#include "pump_watch.h"
#include "rd.h"
#include "rd_bisect_plan.h"
#include "rd_passring.h"
#include "rd_tex.h"
#include "../fmv/rd_video.h"
#include "rhi.h"
#include "sched.h"
#include "settings.h"
#include "startup_retry.h"
#include "texpack.h"
#include "touch_ui.h"
#include "trace_host.h"
#include "ui_host.h"
#include "popup.h" /* ui_popup_set_vsync_hz */
#include "ui_mouse.h"
#include "video_options.h"
#include "window_host.h"
#include "window_lifecycle.h"
#include "window_pump_win.h"
#include "window_video.h"
#ifdef __ANDROID__
#include "android/gpu_driver_android.h"
#include "android/host_android.h"
#elif !defined(ICO_IOS)
#include "steam_shortcut.h"
#endif
#if ICO_RC_TOOLS && defined(__ANDROID__)
#include "share_report.h" /* an rc build's Share a bug report (settings.h) */
#include "strings.h"
#endif

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull
/* Slow-step lines a stats block logs at most (the rest are
   counted in its first line) */
#define SLOW_STEP_LINES 5
/* Slow-replay lines (a replay over a refresh) a stats block logs at most
   (the rest are counted in its first line) */
#define SLOW_REPLAY_LINES 5

_Static_assert(SDLK_F11 == ICO_HOTKEY_KEY_F11, "hotkeys.h: SDL3's SDLK_F11");

_Static_assert(SDLK_F12 == ICO_HOTKEY_KEY_F12, "hotkeys.h: SDL3's SDLK_F12");

_Static_assert(SDL_EVENT_TERMINATING == ICO_SDL_EVENT_TERMINATING &&
                   SDL_EVENT_LOW_MEMORY == ICO_SDL_EVENT_LOW_MEMORY &&
                   SDL_EVENT_WILL_ENTER_BACKGROUND == ICO_SDL_EVENT_WILL_ENTER_BACKGROUND &&
                   SDL_EVENT_DID_ENTER_FOREGROUND == ICO_SDL_EVENT_DID_ENTER_FOREGROUND,
               "window_lifecycle.h: SDL3's lifecycle event types");

/* F12's dump (port/render/rd_dump.c; rd.h does not declare it) */
bool rd_dump_on_demand(const char *dumpPath, const char *pngPath);
/* The game's state the mouse capture follows (mouse_look.h
   ico_mouse_capture_rule): the boy, the stage (common/include/main.h), the
   layout in front (layout_texture.c), the pause (systemStatus[5]: the
   pause menu and the title procs set it; game_pause is not 0 while a
   stage runs, so it cannot gate play), loading and a movie
   (StageManager.c). */
extern void *boyGObj;
extern int data_loading;
extern int stage_no;
extern int current_layout_id;
extern int systemStatus[12];
extern int mpegPlay;

static SDL_Window *s_window;

static int s_captured; /* ICO_CAPTURE_* (mouse_look.h) */

static Uint64 s_deadline;

static int s_open;
/* a quit or close request that came in while a start-up screen could not
   stop (progress_present with allowCancel 0): the next ico_window_pump
   quits */
static int s_quitLatched;

/* the display options last applied
   (port/game/video_options.h) */
static unsigned s_videoSerial;

/* the window's real fullscreen state (SDL_WINDOW_FULLSCREEN as last read),
   not the option: the window manager can refuse a request or change it on
   its own */
static int s_fullscreen;

#if !defined(__ANDROID__) && !defined(ICO_IOS)
/* the mode Alt+Enter goes back to from Windowed (the last
   non-windowed one the options or the window had; Fullscreen to begin with);
   the phone builds are always the whole screen */
static int s_lastFullMode = ICO_WINDOW_FULLSCREEN;
#define WINDOW_LAST_FULL(m) (s_lastFullMode = (m))
#else
#define WINDOW_LAST_FULL(m) ((void)(m))
#endif

/* the options the last "display changed" line compared against (seeded
   after the startup line) */
static IcoVideoOptions s_videoLast;

/* the presentation loop (ico_window_pace) */
static int s_pollRestarted; /* the poll's restart at the first game frame, once */

static struct {
    int framerate;           /* ico_video_framerate() as last applied */
    unsigned cutSerial;      /* ico_video_cut_serial() last passed on */
    uint32_t frame;          /* rd_frame_number() last seen */
    uint32_t presentedFrame; /* the frame of the last present */
    Uint64 tickAt, tickPrev; /* the pace deadlines (simulated time) the last two frames closed at */
    Uint64 lastPresent;      /* the last present's start (the alpha clock's) */
    Uint64
        nextPresent;   /* when the next present is due (0: now); the grid pace_next_present keeps */
    Uint64 cost;       /* how long the last rd_present took */
    PaceHist paceHist; /* the last presents' costs (pace_policy.h) */
    bool paceSlow;     /* pace_slow_present after the last present */
    /* a frame pace left unpresented since the last present (pace_late_skip)
       and its number */
    bool skipOpen;
    uint32_t skipFrame;
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
    unsigned statSkipLate, statSkipSlow; /* the block's skipped presents by reason */
    /* simulated vsyncs paced, and the lag the pacer dropped (each time
       it was more than RESYNC_NS behind: the simulation ran slower than
       real time by that much) */
    unsigned statVsyncs, statResyncs;
    Uint64 statDropped;
    uint32_t statFrameNo; /* rd_frame_number() at the block's start */
    /* rd_video_presents at the block's start; a movie presents on
       its own, outside the replays the line counts */
    uint32_t statMovie, statMovieFail;
    /* the simulation step's real time (from the end of one pace to the
       start of the next), for the 10 s line */
    Uint64 paceEnd;
    double stepSumMs, stepMaxMs;
    unsigned stepCount;
    /* rd_vu_card_counts at the block's start: the card verdicts the
       steps decided (rd_mesh.c), for the 10 s line */
    uint64_t cardStrips, cardTris;
    double cardMs;
    /* the same at the last step's end, for the slow step's line */
    uint64_t stepCardStrips, stepCardTris;
    double stepCardMs;
    int mailbox; /* rhi_prefer_mailbox as last applied: vsync on and presenting between ticks */
    int vsync;   /* vsync on and presenting between ticks */
    /* vsync on and the cap equals the display's refresh but no vsync lock
       (144 on 59.94: no whole multiple of the game's rate): FIFO, a
       present a vblank, the game's own timer */
    int fifoLock;
    /* the vsync lock (pace_policy.h PaceLock): vsync on, the cap equal to
       the refresh and a whole multiple n of the simulation's rate, n
       refreshes within 1 % of its period: the simulation on a grid of the
       wall clock, n presents a step (mailbox with the display's timing of
       presents, rhi_present_timing; else FIFO) */
    int gridLock;
    int paceHz;           /* the simulation's rate the last pace had (50 or 60; 0: none yet) */
    int clockGone;        /* the display's timing stopped or was not believed: FIFO */
    int vblankGone;       /* the vblank clock stopped or was not believed (the grid on the
                             report until the display or the settings change) */
    Uint64 lockWaitNs;    /* the last locked present's wait (lock_io_present) */
    int presentModeApply; /* the lock wants another present mode: ico_window_pace applies it */
    int timingUse;        /* rhi_present_timing_use as last set (presents carry ids) */
    PaceLock lock;
    unsigned lockN;       /* n the lock was started with (0: not locked at the last vsync) */
    Uint64 lockRefresh;   /* the refresh it was started with */
    int lockMailbox;      /* the swapchain was in mailbox mode when it started */
    int lockClock;        /* PaceLock.clock last logged */
    int offScreen;        /* the window minimised, hidden, occluded or 0x0 (window_off_screen) */
    uint32_t heldFrame;   /* a new frame pace_fifo_hold held back (+1; 0: none) */
    unsigned lockResyncs; /* lock.resyncs already counted in statResyncs */
    Uint64 lockDroppedNs; /* lock.droppedNs already counted in statDropped */
    /* a host pause to take out of the pacing (pace_paused): the time a
       swapchain rebuild took (video_apply, a size event, fullscreen),
       applied before the next pace, and under the lock the first present
       on the new chain's excess over a refresh (present_once), applied
       after it */
    Uint64 pausePendingNs;
    /* RhiStats.swapchains at the last present accounted: a present on a
       newer swapchain is the first on it (pace_first_present_pause) */
    Uint64 swapchains;
    /* the lock's counts at the 10 s block's start */
    double lockNudgeAt;
    unsigned lockSeenAt, lockLateAt, lockEarlyAt, lockWaitedAt, lockSkippedAt;
    unsigned lockSkippedLateAt; /* the lock's late skips counted into statSkipLate */
    double audioRatio;          /* the audio's rate ratio last set (ico_audio_set_rate_ratio) */
    Uint64 lastReturn;          /* when the last present returned (0: none yet) */
    Uint64 lateMaxNs;           /* the block's latest step against when it was due */
    /* the slow-step lines ([dev] slow_step_ms, 0 off): the threshold,
       the block's count and lines, ico_window_pump's time, the renderer's
       texture decodes and pipeline creations at the last pace's end */
    double slowMs, pumpMs;
    /* the pump's parts: the event queue (and its events), video_apply,
       ico_input_sdl_update */
    double pumpEventsMs, pumpSettingsMs, pumpInputMs;
    unsigned pumpEvents;
    /* the block's longest pump, for the 10 s line (issue 105) */
    double pumpMaxMs;
    unsigned slowSteps, slowLogged;
    uint32_t decodes, pipeCreates;
    /* the present clock alpha is taken from (rd.h RdPresentClock) */
    RdPresentClock clock;
    int legacyAlpha; /* ICO_RD_S2_LEGACY=1 (read in video_settings) */
    /* F11, the stats line every second until this time (0: off) */
    Uint64 fastUntil;
    /* F12's presented-picture PNG while its capture is pending ("": none) */
    char presentPng[ICO_PATH_MAX];
    /* the present that took it: the record of the first present_once after
       the capture was armed (presentInfoPending until then; presentInfoHave
       once noted), the frame the dump holds and whether the zip holds the
       tick before (the -prev.rddump --interp rebuilds the present from) */
    int presentInfoPending, presentInfoHave;
    RdPresentInfo presentInfo;
    uint32_t presentDumpFrame;
    int presentDumpPrev;
    /* the present allowance on the timer path (pace_policy.h PaceBudget;
       under the lock lock.budget): the GPU time a presented replay (the
       median of the last 8, perf_drain; 0: no timestamps yet), the last
       present's waits for the display (the acquire and the present) and for
       the GPU (the fence waits), the simulation step's time (ico_window_pace) */
    PaceBudget budget;
    PaceHist gpuHist;
    Uint64 gpuNs, displayWaitNs, fenceWaitNs, stepNs;
} s_pres;

/* resolution "auto" (video_options.h ICO_RES_AUTO): the
   replays' costs (perf_drain) against the frame budget, a step down when
   they are too slow (pace_policy.h pace_auto_resolution_step, auto_res_update) */
static struct {
    int active;        /* resolution "auto" in force, the CRT filter off */
    float windowScale; /* the presentation box's height over the game's 448 lines */
    int tickBudget;    /* frame rate Original: one present a game tick, so the budget is
                          the tick (two fields at the pace's hz, set by ico_window_pace) */
    PaceSamples samples;
} s_autoRes;

/* the command lists a perf record times (RdPerfRecord.gpuListMs) */
#define GPU_LISTS                                                                                  \
    ((int)(sizeof(((const RdPerfRecord *)0)->gpuListMs) /                                          \
           sizeof(((const RdPerfRecord *)0)->gpuListMs[0])))

/* The renderer's per-replay records (rd.h RdPerfRecord): summed over
   the 10 s block for the window's second line, and with [dev] perf_log =
   true written one line each into logs/ico-pc-perf.csv; the timed replays'
   pieces (rd_perf_passes_of) summed for the block's top passes, and with
   [dev] perf_passes = true written one line each into
   logs/ico-pc-passes.csv */
static struct {
    int csvTried;
    FILE *csv;
    int passCsvTried;
    FILE *passCsv;
    RdPerfPassAgg passAgg; /* the timed replays' pieces (rd.h rd_perf_pass_agg_*) */
    /* the pieces begun by kind and the render passes by effect, the walk's
       CPU time by list, and of the bytes uploaded the textures, the uniform
       blocks, the interpolated meshes and the VU streams (rd.h
       RdPerfRecord.passKinds, passEffects, walkListMs, upload*Bytes) */
    double passKinds[RD_PERF_PASS_KIND_COUNT], passEffects[RD_PERF_POST_COUNT];
    double walkList[RD_LIST_COUNT];
    double texBytes, uniformBytes, interpBytes, streamBytes;
    unsigned gpuN;
    PerfBlock ph;        /* the CPU phases (perf_block.h) */
    unsigned slowLogged; /* slow-replay lines logged in the block */
    /* the block's longest replay over a refresh, the vsync it was drained
       at, and whether its line was written (slow_replay_longest) */
    RdPerfRecord longest;
    unsigned longestVsync;
    int longestShown;
    double gpu, maxGpu, gpuList[GPU_LISTS], gpuUpload, gpuPresent, gpuPrepass;
    double gpuPost[RD_PERF_POST_COUNT]; /* per picture effect (rd.h RD_PERF_POST_*) */
    unsigned gpuPostPartial;            /* replays whose effects were not all timed apart */
    uint64_t draws, passes, pipeBinds, groupBinds, groups, barriers, copies, bytes, meshBytes;
    uint64_t texUploads, meshUploads, dateSnaps, exact, pipeCreates;
    /* the exact blend's draws and passes, the glow's passes, the target
       snapshots copied and swapped (rd.h RdPerfRecord, issue 103) */
    uint64_t dstDraws, storagePasses, dstSwitches, glowPasses, snapshots, snapSwaps;
    /* the draws through the above-1x kernels (rd.h RdPerfRecord.a1xDraws) */
    uint64_t a1xDraws;
    uint64_t msaaResolves;
    /* the VU vertex pre-pass's vertices, draws and the mesh and grid draws
       drawn the old way (rd.h RdPerfRecord.vuPre*) */
    uint64_t vuPreVerts, vuPreDraws, vuPreFallbacks;
    /* the pixel diet's draws: the alpha/blend planner's one-pass and
       opaque draws, the frame-size plan's constant, additive and local-read
       sprites, the VU passes without the planes (rd.h RdPerfRecord) */
    uint64_t alphaOnePass, alphaOpaque, fxConstant, fxAdditive, fxLocal, vuNoPlanes;
    /* draws that read a water block at a scale other than the scene's
       (rd.h RdPerfRecord.blockOtherScaleReads, issue 106) */
    uint64_t blockOtherScale;
    uint32_t msaaSamples; /* the block's largest count (0: no MSAA) */
    uint64_t bufCreated, bufDestroyed, texCreated, texDestroyed, allocs, fenceWaits, waitIdles,
        readbacks;
} s_perf;

/* The display under the vsync lock, for the 10 s display line
   (diagnostics only: nothing paces on these).  The display clock
   (display_clock.h: Windows, a DXGI output's vblanks) runs while the lock
   holds; its vblanks and the lock's present returns go into intervals
   against the reported refresh (pace_intervals_add), the presents that
   took a quarter of a refresh or more are counted (they waited for the
   display: FIFO's queue was full), and with D3D12 the swapchain's own
   count of the pictures it showed gives the repeats (pace_shown_add). */
static struct {
    int clock;        /* 1 running, 0 not (none, not locked), -1 it stopped itself */
    unsigned vblanks; /* vblanks taken in the block */
    PaceIntervals vb, ret;
    unsigned waited;
    Uint64 waitedMaxNs;
    PaceShown shown;
    int statsSeen; /* the swapchain's counts were read in the block */
} s_disp;

static void disp_clock_stop(void);
static void disp_clock_start(void);
static void pump_describe(char *out, size_t size);

/* The renderer's settings from the display options and the window's pixel
   size. */
/* The display's refresh period in ns (0 unknown); fallback is returned when
   the display reports none */
static Uint64 display_refresh_ns(Uint64 fallback)
{
    const SDL_DisplayMode *dm =
        s_window != NULL ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window)) : NULL;

    return dm != NULL && dm->refresh_rate > 1.0f ? (Uint64)(1e9 / (double)dm->refresh_rate)
                                                 : fallback;
}

static void video_settings(RdSettings *rs, int w, int h)
{
    IcoVideoOptions o;

    ico_video_get(&o);
    ico_video_set_window(w, h);
    memset(rs, 0, sizeof(*rs));
    /* the renderer's flag: Original only when the four rows are the PS2's */
    rs->preset =
        ico_video_preset(&o) == ICO_VIDEO_ORIGINAL ? RD_PRESET_ORIGINAL : RD_PRESET_ENHANCED;
    rs->outputWidth = (uint32_t)(w > 0 ? w : WINDOW_W);
    rs->outputHeight = (uint32_t)(h > 0 ? h : WINDOW_H);
    rs->aspect = ico_video_aspect();
    rs->vsync = (uint8_t)(o.vsync != 0);
    rs->filterUpgrade = (uint8_t)o.filter;
    rs->fullHeightScene = (uint8_t)(o.fullHeight != 0);
    rs->fullPixel = (uint8_t)(o.fullPixel != 0);
    /* off: the same picture, the game's border masked over it */
    rs->borderMask = (uint8_t)(o.fullPixel == 0);
    /* Water reflections: Sharp, in either preset (at 1x it changes nothing) */
    rs->waterSharp = (uint8_t)(o.effectWaterSharp != 0);
    /* Motion blur: Smooth, in either preset; Off and Original are the
       game's (staticBlur.c reads the switch) */
    rs->motionBlurSmooth = (uint8_t)(o.effectMotionBlur != 0 && o.effectMotionBlurSmooth != 0);
    /* off under the Original preset (ico_video_anti_alias); the renderer
       draws an MSAA count the device lacks as off (rd_aa_supported_mask) */
    rs->antiAlias = (uint8_t)ico_video_anti_alias();
    rs->sceneWidth = (uint32_t)o.resW;
    rs->sceneHeight = (uint32_t)o.resH;
    rs->sceneScale = (float)o.resScale;
    /* resolution "auto": the window's size (scale 0) until
       auto_res_update lowers it; the budget is the frame rate cap's period,
       the game tick at Original (one present a tick), else 1/60 s */
    s_autoRes.active = o.resScale == ICO_RES_AUTO && !(o.crt && o.crtStrength > 0.0f);
    if (o.resScale == ICO_RES_AUTO) {
        const float boxH = (float)rs->outputWidth / (rs->aspect > 1.0f ? rs->aspect : 4.0f / 3.0f);

        rs->sceneScale = (float)ico_video_auto_scale();
        s_autoRes.windowScale =
            (boxH < (float)rs->outputHeight ? boxH : (float)rs->outputHeight) / 448.0f;
        s_autoRes.tickBudget = o.framerate == ICO_FRAMERATE_ORIGINAL;
        s_autoRes.samples.budgetNs = o.framerate > 0        ? 1000000000ull / (Uint64)o.framerate
                                     : s_autoRes.tickBudget ? 40000000ull /* PAL until paced */
                                                            : 1000000000ull / 60u;
    }
    /* the CRT filter in either preset (rd_crt.c) */
    rd_crt_settings(rs, o.crt ? (RdCrtMode)(o.crtMode + 1) : RD_CRT_OFF, o.crtStrength);
    rs->crtScanlines = o.crtScanlines;
    rs->crtMask = o.crtMask;
    rs->crtHalation = o.crtHalation;
    rs->crtBloom = o.crtBloom;
    rs->crtCurvature = o.crtCurvature;
    /* texture packs and the dump, in either preset */
    rs->texturePack = (uint8_t)(o.texturePack != 0);
    /* the dump is a Developer mode row: never in force without it (a
       config.toml saved with it on and Developer mode turned off by hand) */
    rs->dumpTextures = (uint8_t)(o.dumpTextures != 0 && ico_opt_developer_mode());
    /* the depth for an effects program (ReShade) */
    rs->effectsDepth = (uint8_t)(o.effectsDepth != 0);
    rs->modelPack = (uint8_t)(o.modelPack != 0);
    rs->dumpModels = (uint8_t)(o.dumpModels != 0 && ico_opt_developer_mode());
    /* rd presents between ticks, in both presets; ICO_RD_S2_LEGACY=1 takes
       the present's alpha from the raw measured time instead of the present
       clock (rd.h RdPresentClock) */
    {
        const char *e = getenv("ICO_RD_S2_LEGACY");

        s_pres.legacyAlpha = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    if (s_pres.framerate != ico_video_framerate()) {
        s_pres.nextPresent = 0;                            /* a new limit starts a new grid */
        pace_budget_reset(&s_pres.budget, PACE_ALLOW_MAX); /* and a new allowance */
    }
    s_pres.framerate = ico_video_framerate();
    rs->interpolate = (uint8_t)(s_pres.framerate != ICO_FRAMERATE_ORIGINAL);
    /* presenting between ticks with vsync on, the swapchain prefers the
       mailbox mode: no tearing, and a present never waits for the display,
       so it cannot hold the simulation back */
    s_pres.vsync = rs->vsync && rs->interpolate;
    /* A cap equal to the display's rate (60 on 59.94 or 60.00 Hz, 120 on
       119.88 or 120.00) that is a whole multiple of the game's rate takes
       the vsync lock (pace_policy.h; docs/port/DISPLAY.md, "The vsync
       lock"): the game steps on a grid of
       the display's refresh, n presents a step.  Mailbox when the display's
       timing of presents is in use (rhi_present_timing; the lock keeps the
       presents off the refreshes), else FIFO, which shows the presents in
       order whatever their phase.  A cap equal to the refresh with no
       whole multiple (144 on 59.94) takes FIFO and the game's own timer.
       D3D12 has no mailbox mode: its presents with vsync are always FIFO
       (d3d12_device.c rhi_prefer_mailbox). */
    {
        const Uint64 refresh = display_refresh_ns(0);
        const Uint64 simPeriod = s_pres.paceHz == 50 ? 20000000ull : 16683333ull;
        const int capEq = s_pres.vsync && pace_cap_equals_refresh(s_pres.framerate, refresh);
        const unsigned n = capEq ? pace_lock_presents(s_pres.framerate, simPeriod) : 0;
        const int grid = n > 0 && pace_grid_period_ok(n, refresh, simPeriod);
        const int gridMailbox = grid && rhi_present_timing() && !s_pres.clockGone;
        const int fifo = capEq && !gridMailbox;

        if (grid != s_pres.gridLock || fifo != s_pres.fifoLock) {
            if (grid) {
                fprintf(stderr,
                        "window: the cap equals the refresh: the game steps every %.4f ms (%u "
                        "refresh%s), present mode %s\n",
                        (double)(n * refresh) / 1e6, n, n == 1 ? "" : "es",
                        gridMailbox ? "mailbox (the display's timing in use)" : "fifo");
            } else {
                fprintf(stderr, "window: the cap %s the refresh: present mode %s\n",
                        capEq ? "equals" : "no longer equals",
                        capEq ? "fifo" : "as the options say");
            }
        }
        s_pres.gridLock = grid;
        s_pres.fifoLock = fifo && !grid; /* the timer path's FIFO (no lock) */
        s_pres.mailbox = s_pres.vsync && !fifo;
    }
    rhi_prefer_mailbox(s_pres.mailbox != 0);
}

/* The window is fullscreen now (SDL's flag, which follows the window
   manager's answer) */
static int window_fullscreen(void)
{
    return s_window != NULL && (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

/* Extras > Add to Steam: Windows and Linux. Not on macOS: steam_shortcut.c
   finds its own path through /proc and tells that Steam runs from Linux's
   pid files or Windows' registry and process list, none of which a Mac has,
   so the row is not shown there. */
#if !defined(__ANDROID__) && !defined(__APPLE__)
/* steam_shortcut.c's answers are the menu's */
_Static_assert(ICO_STEAM_ADDED == UI_STEAM_ADDED && ICO_STEAM_EXISTS == UI_STEAM_EXISTS &&
                   ICO_STEAM_RUNNING == UI_STEAM_RUNNING &&
                   ICO_STEAM_NOT_FOUND == UI_STEAM_NOT_FOUND &&
                   ICO_STEAM_NO_USER == UI_STEAM_NO_USER && ICO_STEAM_FAILED == UI_STEAM_FAILED,
               "the Steam answers match the menu's");

static int steam_add_handler(void)
{
    return ico_steam_add_this_game();
}
#endif

/* The window's mode now, from SDL's flags: fullscreen, a
   frameless window, or windowed.  Android's window is fullscreen. */
static int window_mode_now(void)
{
    if (s_window == NULL) {
        return ICO_WINDOW_WINDOWED;
    }
    const SDL_WindowFlags f = SDL_GetWindowFlags(s_window);

    if (f & SDL_WINDOW_FULLSCREEN) {
        return ICO_WINDOW_FULLSCREEN;
    }
    return (f & SDL_WINDOW_BORDERLESS) ? ICO_WINDOW_BORDERLESS : ICO_WINDOW_WINDOWED;
}

/* The swapchain's full-screen-exclusive request follows the window mode
   (rhi.h rhi_set_exclusive_fullscreen): Fullscreen lets the driver take
   exclusive presentation, Windowed and Borderless keep a composed window
   other programs can draw over or scale (Lossless Scaling, overlays).  Set
   before each mode request and at creation; the swapchain made next reads
   it.  1 when the flag changed. */
static int s_exclusiveSet = -1; /* the flag last given; -1 before the first */

static int exclusive_follow(int mode)
{
    const int allowed = mode == ICO_WINDOW_FULLSCREEN;

    if (allowed == s_exclusiveSet) {
        return 0;
    }
    s_exclusiveSet = allowed;
    rhi_set_exclusive_fullscreen(allowed != 0);
    return 1;
}

static const char *video_preset_label(const IcoVideoOptions *o)
{
    const int p = ico_video_preset(o);

    return p == ICO_VIDEO_ORIGINAL ? "Original" : (p == ICO_VIDEO_ENHANCED ? "Enhanced" : "Custom");
}

/* The CRT filter's state in one word: "off" or its mode */
static const char *video_crt_label(const IcoVideoOptions *o)
{
    return o->crt ? ico_video_crt_mode_name(o->crtMode) : "off";
}

/* One field of the "display changed" line: ", name old -> new" */
static void video_log_field(char *line, size_t size, const char *name, const char *was,
                            const char *now)
{
    const size_t len = strlen(line);

    if (strcmp(was, now) != 0 && len < size) {
        snprintf(line + len, size - len, "%s%s %s -> %s", len ? ", " : "", name, was, now);
    }
}

/* One line per change of the display options after startup,
   naming only the fields that differ, in the startup line's words; nothing
   when nothing did (a resize's forced apply) */
static void video_log_changes(const IcoVideoOptions *o)
{
    const IcoVideoOptions *p = &s_videoLast;
    char line[512] = "", a[32], b[32];

    video_log_field(line, sizeof(line), "preset", video_preset_label(p), video_preset_label(o));
    video_log_field(line, sizeof(line), "resolution", ico_video_resolution_name(p, a, sizeof(a)),
                    ico_video_resolution_name(o, b, sizeof(b)));
    video_log_field(line, sizeof(line), "aspect", ico_video_aspect_name(p->aspect),
                    ico_video_aspect_name(o->aspect));
    video_log_field(line, sizeof(line), "window mode", ico_video_window_mode_name(p->windowMode),
                    ico_video_window_mode_name(o->windowMode));
    video_log_field(line, sizeof(line), "vsync", p->vsync ? "on" : "off", o->vsync ? "on" : "off");
    video_log_field(line, sizeof(line), "texture filter", ico_video_filter_name(p->filter),
                    ico_video_filter_name(o->filter));
    video_log_field(line, sizeof(line), "height", p->fullHeight ? "full" : "half",
                    o->fullHeight ? "full" : "half");
    video_log_field(line, sizeof(line), "framerate",
                    ico_video_framerate_name(p->framerate, a, sizeof(a)),
                    ico_video_framerate_name(o->framerate, b, sizeof(b)));
    video_log_field(line, sizeof(line), "crt", video_crt_label(p), video_crt_label(o));
    snprintf(a, sizeof(a), "%.2f", (double)p->crtStrength);
    snprintf(b, sizeof(b), "%.2f", (double)o->crtStrength);
    video_log_field(line, sizeof(line), "crt strength", a, b);
    video_log_field(line, sizeof(line), "texture pack", p->texturePack ? "on" : "off",
                    o->texturePack ? "on" : "off");
    video_log_field(line, sizeof(line), "dump textures", p->dumpTextures ? "on" : "off",
                    o->dumpTextures ? "on" : "off");
    video_log_field(line, sizeof(line), "model pack", p->modelPack ? "on" : "off",
                    o->modelPack ? "on" : "off");
    video_log_field(line, sizeof(line), "dump models", p->dumpModels ? "on" : "off",
                    o->dumpModels ? "on" : "off");
    video_log_field(line, sizeof(line), "motion blur", ico_video_motion_blur_name(p),
                    ico_video_motion_blur_name(o));
    if (line[0] != '\0') {
        fprintf(stderr, "window: display changed: %s\n", line);
    }
    s_videoLast = *o;
}

/* Applies the options changed since the last call (the Settings menu's
   ico_video_set, Alt+Enter; force: a resize, for aspect "auto" and
   resolution "window"): the window mode through SDL, the rest through
   rd_set_settings at the next frame (rd recreates the targets and the
   swapchain as needed). */
static void video_apply(int force)
{
    IcoVideoOptions o;
    RdSettings rs;
    int w = 0, h = 0;

    if (!force && ico_video_serial() == s_videoSerial) {
        return;
    }
    s_videoSerial = ico_video_serial();
    if (!force) {
        /* a settings change tries the display's timing again (the lock
           leaves it when it stops or is not believed) */
        s_pres.clockGone = 0;
        s_pres.vblankGone = 0;
    }
    ico_video_get(&o);
    /* on a change of the options only (never a resize's forced call: a
       refused request would be made again at every size event), and
       compared against the window, not the last request: an option that
       already matches what the window is asks for nothing */
    int request = !force && o.windowMode != window_mode_now();
    if (o.windowMode != ICO_WINDOW_WINDOWED) {
        WINDOW_LAST_FULL(o.windowMode);
    }
#if defined(__ANDROID__) || defined(ICO_IOS)
    /* Android: always fullscreen (immersive, the window's creation
       flag); the option follows the window (window_fullscreen_event) */
    request = 0;
#endif
    int exclusiveChanged = 0;
    if (request) {
        /* the swapchain's exclusive request first: the resize event that
           follows rebuilds it */
        exclusiveChanged = exclusive_follow(o.windowMode);
        /* exclusive fullscreen at the desktop's own mode, a frameless
           window over the display, or the framed window; the resize event
           follows */
        s_fullscreen =
            ico_window_video_mode(s_window, o.windowMode, NULL, NULL) == ICO_WINDOW_FULLSCREEN;
        /* refused, or not granted yet: the option stays what was asked (it
           is what the file keeps); the ENTER/LEAVE events set it to what
           the window becomes (ico_window_pump), and the Window mode row
           shows the window's state meanwhile */
    }
    video_log_changes(&o);
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    {
        /* the output is the swapchain's image, which the
           backend makes at the surface's size; where that differs from the
           window's pixel size (Android, a resize not reported yet) the
           swapchain's size wins, so this call never takes back what the
           renderer followed (rd_output_followed) */
        uint32_t sw = 0, sh = 0;
        if (rhi_swapchain_size(&sw, &sh)) {
            w = (int)sw;
            h = (int)sh;
        }
    }
    if (exclusiveChanged && w > 0 && h > 0) {
        /* Borderless and Fullscreen at the same pixels: no size event
           follows, so the swapchain is made again here for the new
           exclusive request (a size event that does come makes it once
           more, at its size) */
        int pw = 0, ph = 0;

        SDL_GetWindowSizeInPixels(s_window, &pw, &ph);
        if (pw == w && ph == h) {
            const Uint64 t0 = SDL_GetTicksNS();

            fprintf(stderr, "window: the swapchain made again for the %s mode at %dx%d\n",
                    ico_video_window_mode_name(o.windowMode), w, h);
            rd_resize_output((uint32_t)w, (uint32_t)h);
            s_pres.pausePendingNs += SDL_GetTicksNS() - t0;
        }
    }
    const int mailbox = s_pres.mailbox;
    video_settings(&rs, w, h);
    rd_set_settings(&rs);
    if (mailbox != s_pres.mailbox && w > 0 && h > 0) {
        /* the present mode follows the frame rate option (the swapchain
           is recreated); the time that takes (the device idle, the new
           chain: ~100 ms on some drivers) is a pause of the host, not lag
           of the game */
        const Uint64 t0 = SDL_GetTicksNS();

        rd_resize_output((uint32_t)w, (uint32_t)h);
        s_pres.pausePendingNs += SDL_GetTicksNS() - t0;
    }
}

/* A pause of the host (a swapchain rebuild): the deadline, the frame's
   times and the present grid move by it, so the simulation neither drops
   that time (a resync: a jump) nor runs it off in a burst of steps */
static void pace_paused(Uint64 ns)
{
    if (ns == 0 || s_deadline == 0) {
        return; /* nothing, or the pacer not started (a size event at open) */
    }
    s_deadline += ns;
    if (s_pres.tickAt != 0) {
        s_pres.tickAt += ns;
    }
    if (s_pres.tickPrev != 0) {
        s_pres.tickPrev += ns;
    }
    if (s_pres.nextPresent != 0) {
        s_pres.nextPresent += ns;
    }
    if (s_pres.lastPresent != 0) {
        s_pres.lastPresent += ns;
    }
    if (s_pres.lastReturn != 0) {
        s_pres.lastReturn += ns;
    }
    if (s_pres.lockN != 0) {
        pace_lock_pause(&s_pres.lock, ns);
    }
}

/* What the touch layout knows of the screen besides its pixels (touch.h
   IcoTouchEnv): its density, so the controls keep a size in millimetres,
   and on Android a foldable's fold, so nothing sits on it.  Android: the
   activity's own report (ico_android_screen), whose fold bounds are in the
   window's pixels as Java sees them, scaled here to SDL's pixel size pw x
   ph (the same numbers when the surface fills the window).  Elsewhere, or
   before the first report: the display scale SDL gives, as dots per inch
   over a base of 96 (160 on Android, its density for scale 1). */
static void touch_env_build(IcoTouchEnv *env, int pw, int ph)
{
    float scale;
#ifdef __ANDROID__
    IcoAndroidScreen a;

    if (ico_android_screen(&a) > 0 && a.densityDpi > 0) {
        const float kx = a.winW > 0 ? (float)pw / (float)a.winW : 1.0f;
        const float ky = a.winH > 0 ? (float)ph / (float)a.winH : 1.0f;

        SDL_zerop(env);
        env->pxPerMm = ico_touch_px_per_mm((float)a.densityDpi) * kx;
        if ((a.fold == ICO_TOUCH_FOLD_VERTICAL || a.fold == ICO_TOUCH_FOLD_HORIZONTAL) &&
            a.winW > 0 && a.winH > 0 && a.r >= a.l && a.b >= a.t) {
            env->fold.orientation = a.fold;
            env->fold.halfOpened = a.halfOpened;
            env->fold.separating = a.separating;
            env->fold.bounds.x = (float)a.l * kx;
            env->fold.bounds.y = (float)a.t * ky;
            env->fold.bounds.w = (float)(a.r - a.l) * kx;
            env->fold.bounds.h = (float)(a.b - a.t) * ky;
        }
        return;
    }
#endif
    SDL_zerop(env);
    scale = SDL_GetWindowDisplayScale(s_window);
#ifdef __ANDROID__
    env->pxPerMm = ico_touch_px_per_mm(scale * 160.0f);
#else
    env->pxPerMm = ico_touch_px_per_mm(scale * 96.0f);
#endif
}

/* The touch overlay's zones from the window's pixel size, its safe area (a
   notch or camera hole, the system bars and gesture strips; not the
   rounded corners, which SDL does not report), which SDL gives in points:
   scaled to pixels, and the screen's density and fold (touch_env_build).
   On open, when the size, the safe area or the display scale changes, and
   on Android when the activity reports a new density or fold
   (ico_window_pump); the Touch size row is followed by input_sdl.c.  The
   screen goes first: each call lays the zones out again, and the second
   one has everything. */
static void touch_layout_update(void)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_Rect r;
    IcoTouchEnv env;
    static int s_note[7]; /* the last window and safe area seen (pw, ph, x, y, w, h; 1 once) */
    static int s_insets[5] = {-1, -1, -1, -1, -1}; /* the last insets logged (reported; l t r b) */
    static unsigned s_sizeOnly;                    /* changes of the size alone */
    int note[7] = {0, 0, 0, 0, 0, 0, 1};

    if (s_window == NULL || !SDL_GetWindowSizeInPixels(s_window, &pw, &ph) || pw <= 0 || ph <= 0) {
        return;
    }
    touch_env_build(&env, pw, ph);
    ico_input_sdl_set_touch_env(&env);
    if (SDL_GetWindowSize(s_window, &ww, &wh) && ww > 0 && wh > 0 &&
        SDL_GetWindowSafeArea(s_window, &r) && r.w > 0 && r.h > 0) {
        const double kx = (double)pw / ww, ky = (double)ph / wh;

        note[2] = (int)(r.x * kx + 0.5);
        note[3] = (int)(r.y * ky + 0.5);
        note[4] = (int)(r.w * kx + 0.5);
        note[5] = (int)(r.h * ky + 0.5);
        ico_input_sdl_set_touch_layout(pw, ph, note[2], note[3], note[4], note[5]);
    } else {
        note[6] = 0;
        ico_input_sdl_set_touch_layout(pw, ph, 0, 0, 0, 0);
    }
    note[0] = pw;
    note[1] = ph;
    /* once per change of the insets (a rotation, a fold, the bars): what the
       OS gave, since a camera hole's inset cannot be told from the bars' and
       the default layout keeps clear of all of it while Edit layout reaches
       the edges; a change of the size alone (a desktop drag-resize) the
       first 3 times, then every 100th */
    if (memcmp(note, s_note, sizeof(note)) != 0) {
        const int insets[5] = {note[6], note[6] ? note[2] : 0, note[6] ? note[3] : 0,
                               note[6] ? pw - note[2] - note[4] : 0,
                               note[6] ? ph - note[3] - note[5] : 0};
        char more[96] = "";

        memcpy(s_note, note, sizeof(note));
        if (memcmp(insets, s_insets, sizeof(insets)) != 0) {
            memcpy(s_insets, insets, sizeof(insets));
        } else if (++s_sizeOnly <= 3u || s_sizeOnly % 100u == 0u) {
            snprintf(more, sizeof(more),
                     " (the size alone changed, %u times: the first 3 and every 100th logged)",
                     s_sizeOnly);
        } else {
            return;
        }
        if (note[6]) {
            fprintf(stderr,
                    "touch: window %dx%d px, safe area %d,%d %dx%d (insets l %d t %d r %d b "
                    "%d)%s\n",
                    pw, ph, note[2], note[3], note[4], note[5], note[2], note[3],
                    pw - note[2] - note[4], ph - note[3] - note[5], more);
        } else {
            fprintf(stderr, "touch: window %dx%d px, no safe area reported%s\n", pw, ph, more);
        }
    }
}

/* The window's pixel size the renderer was opened at or last
   given (the size event's path, window_pixel_size); read by
   window_size_recheck, which only Android needs */
#if defined(__ANDROID__) || defined(ICO_IOS)
static int s_pixW, s_pixH;
#define WINDOW_PIX_NOTE(w, h) (s_pixW = (w), s_pixH = (h))
#else
#define WINDOW_PIX_NOTE(w, h) ((void)(w), (void)(h))
#endif

/* The size event's path: the renderer's output and swapchain, aspect
   "auto" and resolution "window", the touch zones */
static void window_pixel_size(int w, int h)
{
    WINDOW_PIX_NOTE(w, h);
    {
        /* the swapchain at the new size: a pause of the host (video_apply) */
        const Uint64 t0 = SDL_GetTicksNS();

        rd_resize_output((uint32_t)w, (uint32_t)h);
        s_pres.pausePendingNs += SDL_GetTicksNS() - t0;
    }
    ico_video_set_window(w, h);
    video_apply(1);
    touch_layout_update();
}

/* Android: the window's pixel size read again after the
   renderer opened and at the first pump.  The window is made before the
   system bars are hidden and the turn to landscape is done, and SDL's size
   event can come late or never: a size other than the renderer's goes
   through the size event's path, with a line */
static void window_size_recheck(const char *when)
{
#if defined(__ANDROID__) || defined(ICO_IOS)
    int w = 0, h = 0;

    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    if (w <= 0 || h <= 0 || (w == s_pixW && h == s_pixH)) {
        return;
    }
    fprintf(stderr, "window: %dx%d pixels %s (the renderer had %dx%d)\n", w, h, when, s_pixW,
            s_pixH);
    window_pixel_size(w, h);
#else
    (void)when;
#endif
}

/* the first pump of either kind (ico_window_progress runs while the disc
   is mounted, before the game's), after its events were handled */
static void first_pump_recheck(void)
{
    static int s_done;

    if (!s_done) {
        s_done = 1;
        window_size_recheck("at the first pump");
    }
}

/* The app came back to the foreground (lifecycle_watch sets it, on
   whichever thread SDL calls the watch from); the next pump of either kind
   reads the window's size again (Android: a size event can be missed while
   the app is away, and a size a few pixels off the frame's exact width
   crops the frame's border columns, issue 129), logs the window and its
   safe area as they are now, and the renderer's frame state */
static SDL_AtomicInt s_resumed;

/* Settings > Save a picture for a bug report's handler (below, with F12's
   frame_dump) */
static void report_picture_arm(void);
#if ICO_RC_TOOLS && defined(__ANDROID__)
/* Settings > Share a bug report's handler, an rc build's alone (below) */
static void share_report_arm(void);
static void share_report_pump(void);
#endif

static void resume_recheck(void)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_Rect r;

    if (SDL_GetAtomicInt(&s_resumed) == 0 || s_window == NULL) {
        return;
    }
    SDL_SetAtomicInt(&s_resumed, 0);
    window_size_recheck("after the app came back");
    if (!SDL_GetWindowSizeInPixels(s_window, &pw, &ph)) {
        pw = ph = 0;
    }
    if (pw > 0 && ph > 0 && SDL_GetWindowSize(s_window, &ww, &wh) && ww > 0 && wh > 0 &&
        SDL_GetWindowSafeArea(s_window, &r) && r.w > 0 && r.h > 0) {
        const double kx = (double)pw / ww, ky = (double)ph / wh;

        fprintf(stderr, "window: after the app came back: window %dx%d px, safe area %d,%d %dx%d\n",
                pw, ph, (int)(r.x * kx + 0.5), (int)(r.y * ky + 0.5), (int)(r.w * kx + 0.5),
                (int)(r.h * ky + 0.5));
    } else {
        fprintf(stderr, "window: after the app came back: window %dx%d px, no safe area reported\n",
                pw, ph);
    }
    rd_log_frame_state("after resume");
}

#if defined(__ANDROID__) || defined(ICO_IOS)
/* settings.h ui_settings_set_quit_is_game: Android's Quit ends the game */
static int quit_is_game(void)
{
    return 1;
}
#endif

/* The window's flags. Android: fullscreen always, which is immersive there
   (no status or navigation bar), and not resizable; the system decides the
   size. */
static SDL_WindowFlags window_flags(int mode)
{
    const SDL_WindowFlags vk = rhi_backend() == RHI_BACKEND_VULKAN ? SDL_WINDOW_VULKAN : 0;

#if defined(__ANDROID__) || defined(ICO_IOS)
    (void)mode;
    SDL_WindowFlags flags = vk | SDL_WINDOW_FULLSCREEN;
#ifdef ICO_IOS
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    return flags;
#else
    return vk | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
           (mode == ICO_WINDOW_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0) |
           (mode == ICO_WINDOW_BORDERLESS ? SDL_WINDOW_BORDERLESS : 0);
#endif
}

/* --- Android: the app's lifecycle (window_lifecycle.h) --------------------
   The operations the tables run; each is called on the thread pumping the
   events, between two frames. */
static void lc_gpu_wait_idle(void *u)
{
    (void)u;
    rhi_wait_idle();
}

static void lc_surface_release(void *u)
{
    (void)u;
    rhi_release_surface();
}

static int lc_surface_recreate(void *u)
{
    (void)u;
    return rhi_recreate_surface(s_window) ? 0 : -1;
}

static void lc_watchdog_pause(void *u, int paused)
{
    (void)u;
    ico_diag_watchdog_pause(paused);
}

static void lc_audio_pause(void *u, int paused)
{
    (void)u;
    ico_audio_sdl_pause(paused);
}

/* the time away is not caught up: the pacer starts from now, the step
   that spans it is not timed as a slow step, and the presents' history
   starts afresh (a present that spanned the time away is not slow) */
static void lc_pace_reset(void *u)
{
    (void)u;
    s_deadline = SDL_GetTicksNS();
    s_pres.paceEnd = 0;
    memset(&s_pres.paceHist, 0, sizeof(s_pres.paceHist));
    s_pres.paceSlow = false;
    s_pres.skipOpen = false;
    s_pres.cost = 0;
    s_pres.lockN = 0; /* the lock starts again from now */
    s_pres.lastReturn = 0;
    pace_budget_reset(&s_pres.budget, PACE_ALLOW_MAX);
}

static int lc_config_dirty(void *u)
{
    (void)u;
    return ico_config_dirty();
}

static int lc_config_save(void *u)
{
    (void)u;
    return ico_config_save();
}

static void lc_record_close(void *u)
{
    (void)u;
    ico_input_record_close();
}

static void lc_texture_low_memory(void *u)
{
    (void)u;
    texpack_low_memory();
}

static void lc_log(void *u, const char *line)
{
    (void)u;
    fprintf(stderr, "window: %s\n", line);
}

static void lc_log_flush(void *u)
{
    (void)u;
    ico_host_log_flush_all();
}

static const IcoLifecycleOps k_lifecycle_ops = {
    NULL,
    lc_gpu_wait_idle,
    lc_surface_release,
    lc_surface_recreate,
    lc_watchdog_pause,
    lc_audio_pause,
    lc_pace_reset,
    lc_config_dirty,
    lc_config_save,
    lc_record_close,
    lc_texture_low_memory,
    lc_log,
    lc_log_flush,
};

/* SDL's event watch: the four lifecycle events as they arrive (the queue
   would see them after SDL blocked the pump in the background) */
#ifdef ICO_IOS
static int s_background;

static void wait_foreground(void)
{
    /* Keep UIKit responsive while simulation and Metal work are paused. */
    while (s_background && !s_quitLatched) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
}
#endif

static bool SDLCALL lifecycle_watch(void *userdata, SDL_Event *event)
{
    const IcoLifecycleEvent e = ico_lifecycle_from_sdl(event->type);

    (void)userdata;
#ifdef ICO_IOS
    const int ready = s_window != NULL;
#else
    const int ready = s_open;
#endif
    /* the background mark of the start-up activity file (diag_host.h
       ico_diag_startup_activity_background): an end there is the
       system's or the user's, not one while preparing the graphics */
    if (e == ICO_LIFECYCLE_WILL_ENTER_BACKGROUND) {
        ico_diag_startup_activity_background(1);
    } else if (e == ICO_LIFECYCLE_DID_ENTER_FOREGROUND) {
        ico_diag_startup_activity_background(0);
        SDL_SetAtomicInt(&s_resumed, 1); /* the pump's resume_recheck */
    }
    if (e != ICO_LIFECYCLE_NONE && ready) {
#ifdef ICO_IOS
        if (e == ICO_LIFECYCLE_WILL_ENTER_BACKGROUND) {
            s_background = 1;
            if (ico_config_dirty()) {
                ico_config_save();
            }
        } else if (e == ICO_LIFECYCLE_DID_ENTER_FOREGROUND) {
            s_background = 0;
        } else if (e == ICO_LIFECYCLE_TERMINATING) {
            s_quitLatched = 1;
        }
#endif
        ico_lifecycle_on(e, &k_lifecycle_ops);
    } else if (e == ICO_LIFECYCLE_WILL_ENTER_BACKGROUND) {
        /* before the window loop is open (Android: the start-up set's
           progress screen, the loss bisect): the time in the background
           is not the game's, so the watchdog does not count it */
        ico_diag_watchdog_pause(1);
    } else if (e == ICO_LIFECYCLE_DID_ENTER_FOREGROUND) {
        ico_diag_watchdog_pause(0);
    }
    return true;
}

/* the start-up screen's state and callback (the callback is below, with the
   progress view) */
typedef struct PipeScreen {
    uint64_t startNs, lastNs;
    int drawn;
} PipeScreen;

static int pipeline_progress(void *ctx, uint32_t done, uint32_t total);
static int progress_present_notes(const char *title, const char *phase, int pct, int allowCancel,
                                  const char *note0, const char *note1);

#ifdef __ANDROID__
static const char k_driver_box[] =
    "ICO could not start the game's graphics with this phone's driver. It needs Vulkan 1.2. "
    "On a phone with an Adreno (Qualcomm) chip you can choose a graphics driver package "
    "instead (a .zip file, for example Turnip).";
static const char k_driver_next_start[] =
    "The game will use the driver package at the next start. Please close and open ICO again.";

/* No driver started and there is no device, so Options > Graphics driver
   cannot be reached: a box offers a driver package (the system's file
   picker; the package is installed, chosen and saved, and the game asks to
   be started again) or Quit.  The phone's own driver is not offered: it
   has just failed in this run.  The package is not started in this
   process: the phone's driver is already loaded here (the failed rd_init),
   and libadrenotools does not support loading a package after it (the
   package's namespace shares the libraries already loaded), so the next
   start loads the package first, on trial as at any start.  A file that
   is not added brings the box back.  Returns when the game should quit
   (Quit, or a package chosen for the next start). */
static void choose_driver(void)
{
    static const char *const labels[2] = {"Choose a driver file", "Quit"};
    char folder[160], why[128];

    /* Enter chooses a file, Escape quits */
    while (ico_android_message_box_buttons(s_window, k_driver_box, labels, 2, 0, 1) == 0) {
        if (!ico_gpu_driver_android_choose(folder, sizeof(folder), why, sizeof(why))) {
            fprintf(stderr, "window: no graphics driver package was added: %s\n", why);
            ico_android_message_box(why, 1);
            continue;
        }
        fprintf(stderr,
                "window: the graphics driver %s is used from the next start; the player was "
                "asked to start the game again\n",
                folder);
        ico_android_message_box(k_driver_next_start, 0);
        return;
    }
    fprintf(stderr, "window: the player quit at the graphics driver choice\n");
}
#endif

/* rd's note of the pipeline it asks the driver for, into the crash
   report's activity note (diag_host.h) */
static void window_activity(const char *note)
{
    if (note != NULL) {
        ico_diag_set_activity("%s", note);
    } else {
        ico_diag_clear_activity();
    }
}

/* The loss bisect (docs/port/RENDER.md, "The loss bisect";
   diag_host.h ico_diag_loss_bisect): the lost frame of the previous run's
   loss dump (the zip's first .rddump, taken out to a file beside it)
   replayed by a renderer of its own on this window, one pass longer each
   time with a wait for the GPU after each (rd.h rd_loss_bisect); that
   renderer is shut down after, the device lost or not, and the game's
   own starts as at any start. Diagnostics only: whatever happens, the
   game starts. user: the game's RdSettings. */
/* the start-up screen's progress function while the bisect creates the
   lost frame's pipelines (rd.h rd_loss_bisect): the same screen and event
   pump as the start-up set's, but never a present on a lost device (the
   bisect reports a loss itself; device_lost_quit's box and dump are the
   game's) */
static int bisect_progress(void *ctx, uint32_t done, uint32_t total)
{
    if (rhi_device_lost()) {
        return 1;
    }
    return pipeline_progress(ctx, done, total);
}

/* the loss dump's lost frame taken out of the zip to dump (the zip's path
   and .rddump) with its size and aspect; false with line saying why */
static bool bisect_dump_file(const char *zip, char *dump, size_t dumpSize, uint32_t *gw,
                             uint32_t *gh, float *aspect, char *line, size_t size)
{
    size_t bytes = 0;
    void *data = ico_dump_zip_extract(zip, &bytes, NULL, 0);

    if (data == NULL) {
        snprintf(line, size, "%s cannot be read or holds no frame", zip);
        return false;
    }
    snprintf(dump, dumpSize, "%s.rddump", zip);
    FILE *f = ico_fopen(dump, "wb");
    const int wrote = f != NULL && fwrite(data, 1, bytes, f) == bytes;

    if (f != NULL && fclose(f) != 0) {
        ico_remove(dump);
        f = NULL;
    }
    free(data);
    if (!wrote || f == NULL) {
        snprintf(line, size, "the frame could not be written to %s", dump);
        ico_remove(dump);
        return false;
    }
    if (!rd_dump_frame_size(dump, gw, gh, aspect)) {
        snprintf(line, size, "%s holds no frame this version reads", zip);
        ico_remove(dump);
        return false;
    }
    return true;
}

/* what the first level found, for the second (window_bisect_step) */
static struct {
    uint32_t pass, draws, passes;
} s_bisect1;

static int window_loss_bisect(const char *zip, char *line, size_t size, void *user)
{
    const RdSettings *game = (const RdSettings *)user;
    char dump[ICO_PATH_MAX + 32];
    uint32_t gw = 0, gh = 0;
    float aspect = 0.0f;
    RdLossBisect b;

    memset(&s_bisect1, 0, sizeof(s_bisect1));
    if (!bisect_dump_file(zip, dump, sizeof(dump), &gw, &gh, &aspect, line, size)) {
        return ICO_DIAG_BISECT_NOT_RUN;
    }
    /* the frame's own size and aspect, the game's other settings */
    RdSettings rs = *game;
    rs.aspect = aspect;
    rd_set_activity_fn(window_activity);
    if (!rd_init(gw, gh, &rs, s_window)) {
        snprintf(line, size, "the renderer did not start (the lines above say why)");
        ico_remove(dump);
        return ICO_DIAG_BISECT_NOT_RUN;
    }
    /* the frame's pipelines compile on worker threads behind the
       "Preparing graphics" screen, which takes the window's events */
    static PipeScreen screen;
    rd_set_pipeline_progress(bisect_progress, &screen);
    rd_loss_bisect(dump, &b);
    rd_set_pipeline_progress(NULL, NULL);
    ico_diag_set_pipelines(0, 0, 0);
    rd_shutdown();
    ico_remove(dump);
    fprintf(stderr,
            "window: loss bisect: frame %u, %u passes, %u replays in %.1f s; the bisect's "
            "renderer is shut down\n",
            (unsigned)b.frame, (unsigned)b.passes, (unsigned)b.replays, b.ms / 1000.0);
    switch (b.result) {
    case RD_BISECT_LOST:
        s_bisect1.pass = b.pass;
        s_bisect1.draws = b.draws;
        s_bisect1.passes = b.passes;
        if (b.next[0] != '\0') {
            fprintf(stderr,
                    "window: loss bisect: the replay stopped as %s began (drawing nothing "
                    "in it)\n",
                    b.next);
        }
        snprintf(line, size, "%s", b.line);
        return ICO_DIAG_BISECT_LOST;
    case RD_BISECT_CLEAN:
        snprintf(line, size,
                 "the whole frame (%u passes) ran one more pass at a time without losing the "
                 "device",
                 (unsigned)b.passes);
        return ICO_DIAG_BISECT_CLEAN;
    default:
        snprintf(line, size, "%s", b.line);
        return ICO_DIAG_BISECT_NOT_RUN;
    }
}

/* One step of the loss bisect's second level (diag_host.h
   IcoBisect2StepFn): the lost frame taken out of the zip again, a renderer
   of its own (rd_init; NOINIT when it does not start: the step runs again
   at the next start), the progress screen on it before the replay (never
   on a lost device), the replay with the step's draw window
   (rd_loss_bisect_step), then that renderer shut down, the device lost or
   not. */
static int window_bisect_step(const char *zip, uint32_t pass, uint32_t number,
                              const RdBisectStep *step, RdBisectKey *key, char *note,
                              size_t noteSize, double *ms, void *user)
{
    const RdSettings *game = (const RdSettings *)user;
    const Uint64 t0 = SDL_GetTicksNS();
    char dump[ICO_PATH_MAX + 32], phase[64];
    uint32_t gw = 0, gh = 0;
    float aspect = 0.0f;
    RdLossStep o;

    if (!bisect_dump_file(zip, dump, sizeof(dump), &gw, &gh, &aspect, note, noteSize)) {
        return ICO_DIAG_BISECT2_NOT_RUN;
    }
    RdSettings rs = *game;
    rs.aspect = aspect;
    rd_set_activity_fn(window_activity);
    if (!rd_init(gw, gh, &rs, s_window)) {
        ico_remove(dump);
        *ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
        return ICO_DIAG_BISECT2_NOINIT;
    }
    snprintf(phase, sizeof(phase), "Checking the graphics problem (step %u of %u)",
             (unsigned)number, (unsigned)RD_BISECT_PLAN_STEPS);
    if (!rhi_device_lost()) {
        progress_present_notes("Starting ICO", phase, (int)(number * 100u / RD_BISECT_PLAN_STEPS),
                               0, NULL, NULL);
    }
    static PipeScreen screen;
    rd_set_pipeline_progress(bisect_progress, &screen);
    rd_loss_bisect_step(dump, pass, step->drawFirst, step->drawLast, step->filter, step->arg, &o);
    rd_set_pipeline_progress(NULL, NULL);
    ico_diag_set_pipelines(0, 0, 0);
    rd_shutdown();
    ico_remove(dump);
    *ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
    if (o.result == RD_BISECT_NOT_RUN) {
        snprintf(note, noteSize, "%s", o.line);
        return ICO_DIAG_BISECT2_NOT_RUN;
    }
    memset(key, 0, sizeof(*key));
    key->valid = o.note.valid;
    key->vu = o.note.vu;
    key->skinned = o.note.skinned;
    key->vs = o.note.vs;
    key->clip = o.note.clip;
    key->tex = o.note.tex;
    rd_draw_note_text(&o.note, note, noteSize);
    return o.result == RD_BISECT_LOST ? ICO_DIAG_BISECT2_LOST : ICO_DIAG_BISECT2_CLEAN;
}

/* the second level's wall bound a start (diag_host.h ico_diag_loss_bisect2) */
#define WINDOW_BISECT2_BUDGET_MS 90000.0

/* the restart step this run is at (startup_retry.h; ico_window_open):
   device_lost_quit's box says when a loss step's run lost the card too */
static StartupRetry s_retry = STARTUP_RETRY_NONE;

static bool window_have_d3d12(void)
{
    const char *name;
    for (uint32_t i = 0; (name = rhi_backend_name(i)) != NULL; i++) {
        if (strcmp(name, "d3d12") == 0) {
            return true;
        }
    }
    return false;
}

/* The platform the build is for, for the summary line */
static const char *summary_platform(void)
{
#if defined(__ANDROID__)
    return "Android arm64";
#elif defined(ICO_IOS)
    return "iOS arm64";
#elif defined(__APPLE__)
    return "macOS arm64";
#elif defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    return "Windows arm64";
#elif defined(_WIN32)
    return "Windows x64";
#elif defined(__aarch64__)
    return "Linux arm64";
#else
    return "Linux x64";
#endif
}

/* The first screenful's one line of what a report needs (diag_host.h
   ico_diag_set_summary), after the device line, the self-tests and the
   window line: once a start (the report file's report.txt keeps it). */
/* [dev] joystick_directinput (ICO_JOYSTICK_DIRECTINPUT, host_config.c):
   0 when the config turned SDL's DirectInput controller scan off (issue
   105); read once before SDL_Init (ico_window_open) */
static int s_joyDirectInput = 1;

/* the summary's controller-scan part (Windows only: the scan is
   DirectInput's) */
static const char *summary_joystick_scan(void)
{
#ifdef _WIN32
    return s_joyDirectInput ? " (DirectInput scan on)"
                            : " (DirectInput scan off: [dev] joystick_directinput = false)";
#else
    return "";
#endif
}

static void log_summary(const IcoVideoOptions *o, const char *res, const char *fr)
{
    char rd[1024], pads[160]; /* rd: the blend, then the device profile's words, ... */
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    const RhiLimits *l = rhi_limits();
    /* the disc main_host.c identified (select_disc), else Android's
       window before the disc: ico_diag_set_disc logs the line again */
    const char *disc = ico_diag_disc()[0] != '\0' ? ico_diag_disc() : NULL;
    size_t used = 0;

    pads[0] = '\0';
    for (int i = 0; ids != NULL && i < n && used < sizeof(pads) - 1; i++) {
        const char *name = SDL_GetGamepadNameForID(ids[i]);
        const int k = snprintf(pads + used, sizeof(pads) - used, "%s%s", i ? ", " : "",
                               name != NULL ? name : "(unnamed)");

        if (k < 0) {
            break;
        }
        used += (size_t)k;
    }
    SDL_free(ids);
    rd_summary_text(rd, sizeof(rd));
    /* the audio's clock (audio_host.h ico_audio_mode_text): this runs
       before the audio starts, so the one the configuration asks for */
    ico_diag_set_summary(
        "version %s; %s; GPU %s (%s); %s; %s preset, resolution %s, aspect %s, framerate %s; "
        "window mode %s; %s; index values up to %u; robust access %s; controller %s%s; disc %s; %s; %s",
        ico_diag_version(), summary_platform(), rhi_adapter_name(),
        l->driverText != NULL ? l->driverText : "driver unknown",
        rhi_backend_kind_name(rhi_backend()), video_preset_label(o), res,
        ico_video_aspect_name(o->aspect), fr, ico_video_window_mode_name(o->windowMode), rd,
        (unsigned)l->maxDrawIndexedIndexValue,
        l->robustText != NULL ? l->robustText
                              : (rhi_backend() == RHI_BACKEND_D3D12 ? "D3D12's own" : "off"),
        pads[0] != '\0' ? pads : "none yet", summary_joystick_scan(),
        disc != NULL ? disc : ICO_DIAG_DISC_UNKNOWN,
        ico_host_pref_is_portable() ? "portable" : "user profile", ico_audio_mode_text());
}

/* SDL's log output (SDL_SetLogOutputFunction, any thread): every line
   into the pump's ring (printed with a slow pump's line and in the report
   file); info and above, which SDL's own output wrote to this log
   before, also here: info and warning lines each through the rate limiter
   under a key of their own (a flood of one priority cannot silence the
   other), error lines through one of their own with a high ceiling (the
   first 64, then every 1000th: a flood cannot fill the disc, and the log's
   end summary counts them), critical lines never limited */
static void SDLCALL sdl_log_output(void *user, int category, SDL_LogPriority priority,
                                   const char *message)
{
    (void)user;
    pump_watch_sdl_log(category, (int)priority, message, SDL_GetTicksNS());
    if (priority >= SDL_LOG_PRIORITY_CRITICAL) {
        ico_diag_log("window: SDL %s %s: %s", pump_watch_sdl_category(category),
                     pump_watch_sdl_priority((int)priority), message != NULL ? message : "");
    } else if (priority == SDL_LOG_PRIORITY_ERROR) {
        ico_log_limited_n("window: SDL's log, errors", 64u, 1000u, "window: SDL %s %s: %s",
                          pump_watch_sdl_category(category), pump_watch_sdl_priority((int)priority),
                          message != NULL ? message : "");
    } else if (priority == SDL_LOG_PRIORITY_WARN) {
        ico_log_limited("window: SDL's log, warnings", "window: SDL %s %s: %s",
                        pump_watch_sdl_category(category), pump_watch_sdl_priority((int)priority),
                        message != NULL ? message : "");
    } else if (priority >= SDL_LOG_PRIORITY_INFO) {
        ico_log_limited("window: SDL's log, info", "window: SDL %s %s: %s",
                        pump_watch_sdl_category(category), pump_watch_sdl_priority((int)priority),
                        message != NULL ? message : "");
    }
}

/* The report file's SDL part (diag_host.h ico_diag_set_report_extra):
   the ring as it is, oldest first. Any thread, a crash handler's too:
   no allocation. */
static void sdl_log_report(char *out, size_t size)
{
    const uint32_t n = pump_watch_sdl_count();
    int k = snprintf(out, size, "SDL's own log, the last %u of %u lines (oldest first):\n",
                     n < PUMP_WATCH_SDL_LINES ? n : PUMP_WATCH_SDL_LINES, n);

    if (k > 0 && (size_t)k < size) {
        pump_watch_sdl_text(out + k, size - (size_t)k, "  ", 0, SDL_GetTicksNS());
    }
}

/* [dev] joystick_directinput = false (ICO_JOYSTICK_DIRECTINPUT=0): SDL's
   DirectInput controller scan off (SDL_HINT_JOYSTICK_DIRECTINPUT, read by
   SDL_Init). SDL 3.4 runs that scan on this thread inside
   SDL_UpdateJoysticks after every HID arrival or removal and on the two
   timers after it (SDL_windowsjoystick.c), and its own comment says the
   enumeration "can take a minute even with no controllers" (issue 105).
   Off, XInput, HIDAPI and Raw Input still find the usual pads; a device
   only DirectInput knows (older wheels, flight sticks, some generic
   pads) is not found. SDL_HINT_JOYSTICK_THREAD (on by default) is not an
   alternative: its thread only waits for the device notifications, the
   scan itself stays in SDL_UpdateJoysticks. */
static void joystick_scan_hints(void)
{
    const char *v = getenv("ICO_JOYSTICK_DIRECTINPUT");

    s_joyDirectInput = !(v != NULL && v[0] == '0');
    if (s_joyDirectInput) {
        return;
    }
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    fprintf(stderr, "window: controllers: the DirectInput scan is off ([dev] joystick_directinput "
                    "= false): XInput, HIDAPI and Raw Input controllers are found as usual, a "
                    "device only DirectInput knows is not\n");
#else
    fprintf(stderr, "window: controllers: [dev] joystick_directinput = false has no effect "
                    "outside Windows\n");
#endif
}

/* [dev] pipeline_set = "lazy" in config.toml, for good (issues 93 and
   107: the second start in a row that ended while preparing the
   pipelines; diag_host.h ico_diag_startup_end_persist): 0 when written */
static int pipeline_set_persist_lazy(void)
{
    return ico_config_set_string("dev.pipeline_set", "lazy") == 0 && ico_config_save() == 0 ? 0
                                                                                            : -1;
}

#ifdef __ANDROID__
/* [dev] gpu_robust = true in config.toml, for good (Android: the second
   start in a row with robust access off that lost the graphics device;
   diag_host.h ICO_DIAG_END_LOSS): 0 when written */
static int gpu_robust_persist(void)
{
    return ico_config_set_bool("dev.gpu_robust", 1) == 0 && ico_config_save() == 0 ? 0 : -1;
}
#endif

/* a variable for this process, read by the renderer's backend when the
   device is made (port/rhi/vk/vk_device.c) */
static void window_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/* [dev] vu_prepass = false (the second start in a row that ended inside the
   VU vertex pre-pass's compile; diag_host.h ico_diag_startup_vu_prepass) */
static int vu_prepass_persist(void)
{
    return ico_config_set_bool("dev.vu_prepass", 0) == 0 && ico_config_save() == 0 ? 0 : -1;
}

/* the watchdog's GPU hang (below, beside device_lost_quit) */
static int window_gpu_progress(IcoDiagGpu *out);
static void window_gpu_hang(void);

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    /* [video] backend (config.toml) or backend= (ico-pc.ini): "vulkan", the
       default, or "d3d12" where the build has it */
    const char *chosen = ico_config_get_string("video.backend", NULL);
    const bool backendExplicit = chosen != NULL && chosen[0] != '\0';
    const char *backend = backendExplicit ? chosen : "vulkan";
    /* the blending the player chose: [dev] blend / blend=, or ICO_RD_BLEND
       from the environment the game was started with (a loss step's or
       the fixedblend step's run has its own ICO_RD_BLEND=fixed,
       host_config.c ico_ini_export, which is not the player's).  The
       fixed-function blend is the default, so this is a player who opted
       into an exact mode (attachment, storage, exact) or pinned fixed; the
       restart steps' fixed blend (startup_retry.h) never overrides it */
    const char *blendKey = ico_config_get_string("dev.blend", NULL);
    const char *blendEnv = getenv("ICO_RD_BLEND");
    const bool retryFixed = startup_retry_value_draws_fixed(getenv(STARTUP_RETRY_ENV));
    const bool blendExplicit = (blendKey != NULL && blendKey[0] != '\0') ||
                               (!retryFixed && blendEnv != NULL && blendEnv[0] != '\0');
    /* this run was started again after a graphics driver crash while
       preparing the shaders or after the graphics card stopped responding
       (startup_retry.h): its step, and its backend unless the player chose
       one; the fixed-function blend of a loss step or fixedblend is set
       by ico_ini_export */
    static char retryBackend[16];
    StartupRetry retry = STARTUP_RETRY_NONE;
    if (startup_retry_parse(getenv(STARTUP_RETRY_ENV), &retry, retryBackend,
                            sizeof(retryBackend)) == 0) {
        if (!backendExplicit) {
            backend = retryBackend;
        }
        fprintf(stderr, "ico_pc: restarted after %s: %s\n",
                startup_retry_is_loss(retry)
                    ? "the graphics card stopped responding"
                    : "a graphics driver crash while preparing the shaders",
                startup_retry_what(retry));
    }
    s_retry = retry;
    /* read (main_host.c read it before, for the log): nothing started
       from here inherits it */
    ico_diag_forget_startup_retry_env();
    /* the start-up activity file (diag_host.h, issue 93): a previous start
       that ended while preparing the pipelines (a crash with no report, the
       system ending the app) is named, and this run creates them as the
       frames need them. A restart after a driver crash (retry above) takes
       its own step: the file is only logged and deleted. */
    bool lazyStart = false;
    {
        char logs[ICO_PATH_MAX], act[ICO_PATH_MAX + 32];

        if (ico_host_logs_dir(logs, sizeof(logs)) == 0 &&
            ico_path_join(act, sizeof(act), logs, "startup-activity.txt") == 0) {
            lazyStart = ico_diag_startup_activity_begin(act, retry != STARTUP_RETRY_NONE) != 0;
        }
    }
    const char *cfgName = ico_config_toml_path() ? ico_config_toml_path() : "config.toml";
    /* a start that ended without a word inside a batch or pipeline of the
       VU vertex pre-pass or of the record path that draws from it
       (startup_retry.h startup_retry_note_has_vu_pre; the largest VU
       modules, vu_main_vs among them, are made only with the pre-pass):
       this start draws without it (rd_set_vu_prepass_off), the first such
       end for this start only, the second also every start after it ([dev]
       vu_prepass = false, written after rd_init); an end in the background
       is not counted (diag_host.h ico_diag_startup_activity_background).
       The cause is then out of the set, so this start builds the set as
       usual (not lazily). A restart after a crash report takes its own
       step instead (its count is 0); and a restart whose step draws without it
       (startup_retry_draws_no_prepass: the crash order's noprepass and the
       steps after it, every loss step) */
    const int preEnds = retry == STARTUP_RETRY_NONE ? ico_diag_startup_vu_prepass() : 0;
    if (preEnds > 0) {
        rd_set_vu_prepass_off(preEnds == 1 ? "for this start: the last start ended while the "
                                             "graphics driver compiled its pipelines"
                                           : "[dev] vu_prepass = false: the last two starts "
                                             "ended while the graphics driver compiled its "
                                             "pipelines");
        lazyStart = false;
    } else if (startup_retry_draws_no_prepass(retry)) {
        char why[128];

        snprintf(why, sizeof(why), "for this restart (the %s step after %s)",
                 startup_retry_name(retry),
                 startup_retry_is_loss(retry) ? "the graphics card stopped responding"
                                              : "a graphics driver crash");
        rd_set_vu_prepass_off(why);
    }
    /* issues 93 and 107: the start-up set, the careful one or none. [dev]
       pipeline_set = "lazy" or "careful" (ICO_PIPELINE_SET); a previous
       start of any version that ended in the foreground without a word
       while a pipeline was being made (not one of the pre-pass's, above):
       the first such end in a row makes this start only careful when the
       ended start made the full set, lazy when it made the careful one
       (its activity file's "set: careful"), the second also every start
       after it lazy ([dev] pipeline_set = "lazy"; the lines and the
       setting after rd_init). A previous start of this version whose set
       stopped at the system's low-memory signal (issue 128) makes this
       start lazy (ICO_DIAG_PIPESET_LAZY_LOW_MEMORY). The device table's careful rule
       (RhiLimits.carefulPipelines) is read after rd_init. */
    const char *pipeSetting = getenv("ICO_PIPELINE_SET");
    const int pipePolicy =
        ico_diag_startup_pipeline_set(retry != STARTUP_RETRY_NONE, lazyStart, pipeSetting, cfgName);
    if (!rhi_create_backend(backend)) {
        fprintf(stderr, "window: renderer backend \"%s\" is not in this build; using vulkan\n",
                backend);
        backend = "vulkan";
        rhi_create_backend(backend);
    }
    ico_diag_set_startup_retry((int)retry, window_have_d3d12(), backendExplicit, blendExplicit,
                               backend);
    /* the name the desktop shows for the game, e.g. in KDE's list of what
       keeps the computer awake (SDL's D-Bus inhibit sends it); Android and
       iOS set SDL_HINT_APP_NAME, which SDL prefers */
    SDL_SetAppMetadata("ICO PC", ICO_VERSION_LABEL[0] != '\0' ? ICO_VERSION_LABEL : NULL,
                       "com.defnf.icopc");
    /* SDL's own log into the pump's ring (pump_watch.h), the controllers'
       and the system's lines down to VERBOSE (issue 105: a stall after a
       controller change is shown beside the joystick drivers' own steps);
       its warnings and errors also into this log (sdl_log_output) */
    SDL_SetLogOutputFunction(sdl_log_output, NULL);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_SYSTEM, SDL_LOG_PRIORITY_VERBOSE);
    joystick_scan_hints();
#ifdef _WIN32
    /* The controllers' scan out of SDL_PollEvent: ico_window_pump calls
       SDL_UpdateJoysticks itself, timed apart from the system's messages
       (issue 105: the pump was held for 50 s right after a controller went
       away; SDL's Windows joystick code enumerates DirectInput devices on
       this thread after a device change). The same work at the same
       point of the pump, only named in the slow-pump line. */
    SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "window: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    IcoVideoOptions start;

    ico_video_get(&start);
    s_videoSerial = ico_video_serial();
    /* Fullscreen is an exclusive mode at the desktop's own mode
       (window_video.c ico_window_video_desktop_mode), so there is no video
       mode to restore when the window loses focus: it stays up behind the
       program that took the focus, as a borderless window does, instead of
       SDL's "auto" minimizing it (Windows, X11) */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
open_window:
    /* fullscreen from the config is asked for at creation, not
       after: SDL's X11 backend then writes _NET_WM_STATE before the window
       is mapped, which KWin and gamescope honour (borderless at the desktop
       resolution; no exclusive mode) */
    s_window = SDL_CreateWindow("ICO", WINDOW_W, WINDOW_H, window_flags(start.windowMode));
    if (start.windowMode != ICO_WINDOW_WINDOWED) {
        WINDOW_LAST_FULL(start.windowMode);
    }
    if (s_window == NULL) {
        fprintf(stderr, "window: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
#if !defined(__ANDROID__) && !defined(ICO_IOS)
    /* the first swapchain's exclusive request (rd_init makes it), and
       Fullscreen at the desktop's own mode: the flag at creation made SDL's
       desktop fullscreen, the mode makes it exclusive (no change of
       resolution or refresh rate) */
    s_exclusiveSet = -1;
    exclusive_follow(start.windowMode);
    if (start.windowMode == ICO_WINDOW_FULLSCREEN) {
        ico_window_video_desktop_mode(s_window);
    }
#endif
    /* No screensaver, screen lock or idle sleep while the game runs: a
       controller is not a desktop input device, so nothing else resets the
       desktop's idle timer (issue 86). SDL_Init already asked
       (SDL_HINT_VIDEO_ALLOW_SCREENSAVER is unset); asking again is
       harmless. On Linux SDL asks through D-Bus (org.freedesktop.ScreenSaver
       or the portal), the Wayland idle-inhibit protocol or the X11
       screensaver extension. SDL_ScreenSaverEnabled reports SDL's own state,
       not whether the desktop accepted the request: the video driver in the
       line tells which way was taken. */
    SDL_DisableScreenSaver();
    /* Windows: the window's messages for the slow-pump lines, and the
       watchdog's description of a pump the system holds (issue 105) */
    ico_pump_win_install(SDL_GetPointerProperty(SDL_GetWindowProperties(s_window),
                                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL));
    ico_diag_set_os_wait_describe(pump_describe);
    ico_diag_set_report_extra(sdl_log_report);
    fprintf(stderr, "window: video driver %s; screensaver and idle sleep %s\n",
            SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver() : "(none)",
            SDL_ScreenSaverEnabled() ? "allowed (not inhibited)" : "inhibited");
    /* the replay, the present and the FMV picture run on the host stack in
       the host FP mode, not on the game's 256 KB fiber stacks */
    rd_set_host_call(ico_sched_call_on_host);
    /* the Vulkan pipeline cache in the per-user folder: a later start of
       the same build creates the renderer's pipelines from it
       (rhi_set_pipeline_cache_path;
       D3D12 ignores it). Android: the app's cache folder, which the system
       may empty when space runs low (android_paths.h) */
    {
        char pref[ICO_PATH_MAX], cache[ICO_PATH_MAX + 96];

#ifdef __ANDROID__
        if (ico_android_cache_dir(pref, sizeof(pref)) != 0) {
            ico_host_pref_dir(pref, sizeof(pref));
        }
#else
        ico_host_pref_dir(pref, sizeof(pref));
#endif
        /* the file is this build's: another build's shader set does not
           match a cache the driver wrote for it, so the renderer drops the
           other pipelines*.vkcache files of the folder (vk_pipeline.c
           vkr_cache_drop_others). The build is the commit
           (ICO_BUILD_COMMIT, "git describe": the tag, the commits since and
           the short hash; "unknown" without git, where every such build
           shares a file), with every character a file name does not take
           made "_" */
        {
            char build[64];
            size_t i;

            snprintf(build, sizeof(build), "%s", ICO_BUILD_COMMIT);
            for (i = 0; build[i] != '\0'; i++) {
                const char c = build[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      c == '.' || c == '-')) {
                    build[i] = '_';
                }
            }
            snprintf(cache, sizeof(cache), "%s/pipelines-%s.vkcache", pref, build);
        }
        rhi_set_pipeline_cache_path(cache);
        ico_diag_set_pipeline_cache(0, cache);
    }
    /* the window system's answer before the size is read */
    SDL_SyncWindow(s_window);
    s_fullscreen = window_fullscreen();
#if !defined(__ANDROID__) && !defined(ICO_IOS)
    if (start.windowMode == ICO_WINDOW_BORDERLESS) {
        /* the frameless window over its display (Wayland: maximised) */
        ico_window_video_mode(s_window, ICO_WINDOW_BORDERLESS, NULL, NULL);
    }
#endif
    if (start.windowMode == ICO_WINDOW_FULLSCREEN && !s_fullscreen) {
        /* not granted (yet): the option is left as the file says, so a
           slow window manager never turns the setting off; an
           ENTER_FULLSCREEN event later sets the window's state
           (ico_window_pump) */
        fprintf(stderr, "window: fullscreen was asked for at creation; the window is windowed\n");
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    WINDOW_PIX_NOTE(w, h); /* window_size_recheck compares against it */
    video_settings(&rs, w, h);
#ifdef __ANDROID__
    /* the player's graphics driver (Options > Graphics
       driver), or the phone's own; one that does not start falls back to
       the phone's with a message */
    const int customDriver = ico_gpu_driver_android_start();
#endif
    /* the previous run lost the device and left its loss dump: the pass
       that loses it, found before the game starts (once). Only on a start
       with the player's own blending and backend: a restart step's run
       (the fixed-function blend, or Direct3D 12 after Vulkan) would replay
       the frame on another path than the one that lost it, and a loss it
       reproduced would add a GPU reset to the restart chain */
    bool bisectLost = false;
    {
        char logs[ICO_PATH_MAX], st[ICO_PATH_MAX + 32];

        if (ico_host_logs_dir(logs, sizeof(logs)) == 0 &&
            ico_path_join(st, sizeof(st), logs, "loss-bisect.txt") == 0) {
            ico_diag_loss_bisect2_path(st);
        }
    }
    if (retry == STARTUP_RETRY_NONE && ico_diag_loss_bisect2_pending()) {
        /* the second level a start before left unfinished goes first; a
           newer loss dump stays in logs */
        if (ico_diag_loss_dump() != NULL) {
            ico_diag_log("ico_pc: loss bisect: the unfinished one goes first; the loss dump %s "
                         "stays in logs",
                         ico_diag_loss_dump());
        }
        const int r2 =
            ico_diag_loss_bisect2(NULL, 0, 0, 0, window_bisect_step, &rs, WINDOW_BISECT2_BUDGET_MS);
        bisectLost = r2 == ICO_DIAG_BISECT2_LOST || r2 == ICO_DIAG_BISECT2_NOINIT;
    } else if (ico_diag_loss_dump() != NULL) {
        if (retry == STARTUP_RETRY_NONE) {
            char zip[ICO_PATH_MAX + 32];

            snprintf(zip, sizeof(zip), "%s", ico_diag_loss_dump());
            bisectLost = ico_diag_loss_bisect(window_loss_bisect, &rs) == ICO_DIAG_BISECT_LOST;
            if (bisectLost && s_bisect1.pass > 0 && s_bisect1.draws > 0) {
                /* the pass drew: the second level narrows it to the draw,
                   in this start (a renderer of its own a step) */
                (void)ico_diag_loss_bisect2(zip, s_bisect1.pass, s_bisect1.draws, s_bisect1.passes,
                                            window_bisect_step, &rs, WINDOW_BISECT2_BUDGET_MS);
            }
        } else {
            ico_diag_log("ico_pc: loss bisect: not run (this start changed the blending or "
                         "backend after the loss); the loss dump stays in logs: %s",
                         ico_diag_loss_dump());
        }
    }
    /* the loss order's robust step: the device line says why robust
       access is on (ICO_GPU_ROBUST=1 came from ico_ini_export; the step's
       variable is forgotten above) */
    if (retry == STARTUP_RETRY_LOSS_ROBUST) {
        window_setenv("ICO_GPU_ROBUST_AFTER_LOSS", "1");
    }
#ifdef __ANDROID__
    /* Android cannot start the game again after a device loss (no loss
       step, startup_retry.h): the next start takes the robust step's
       measure instead, after the loss bisect (which replays the lost frame
       as it was drawn). The previous start lost the device (the activity
       file's loss line, diag_host.h ICO_DIAG_END_LOSS): this start draws
       with robust access on (ICO_GPU_ROBUST=1, the device line "on, ...
       (the restart after the graphics card stopped responding)"), the
       first loss in a row for this start only, the second (a start with it
       off losing it again) also every start after it ([dev] gpu_robust =
       true written). Nothing when the player set gpu_robust (or
       ICO_GPU_ROBUST): the setting rules, and the row is cleared. */
    static bool s_lossRobustDone;
    if (!s_lossRobustDone && retry == STARTUP_RETRY_NONE &&
        ico_diag_startup_end_count(ICO_DIAG_END_LOSS) > 0) {
        const char *said = getenv("ICO_GPU_ROBUST");

        s_lossRobustDone = true;
        if (said != NULL && said[0] != '\0') {
            (void)ico_diag_startup_end_persist(ICO_DIAG_END_LOSS, "[dev] gpu_robust", NULL,
                                               cfgName);
        } else {
            const int saved =
                ico_diag_startup_end_persist(ICO_DIAG_END_LOSS, NULL, gpu_robust_persist, cfgName);

            window_setenv("ICO_GPU_ROBUST", "1");
            if (!saved) {
                window_setenv("ICO_GPU_ROBUST_AFTER_LOSS", "1");
            }
        }
    }
#endif
    rd_set_activity_fn(window_activity);
    rd_set_batch_fn(ico_diag_startup_activity_batch);
    /* before the set: the careful policy's budget reads the process's
       memory around each batch (rd_pipeline.c createKeys) */
    rd_set_memory_fn(ico_diag_process_memory);
#ifdef __ANDROID__
    /* issue 128: the system's low-memory signal (IcoActivity's
       onTrimMemory, diag_host.c ico_diag_note_low_memory) stops the
       start-up set between two batches; desktops never send it */
    rd_set_low_memory_fn(ico_diag_low_memory_stop);
#endif
    bool up = rd_init(gsW, gsH, &rs, s_window);
#ifdef __ANDROID__
    /* not after a bisect that lost the device: the player's driver did not
       fail, it cannot make a second device in the process that lost one
       (the -3 path below; the next start loads it again) */
    if (!up && customDriver && !bisectLost) {
        ico_gpu_driver_android_init_failed();
        up = rd_init(gsW, gsH, &rs, s_window);
    }
#endif
    if (!up && bisectLost) {
        /* a driver that cannot make a device again in the process that lost
           one: the bisect is done (its file deleted), so the next start is
           a normal one; this one ends with a word, not the driver box */
        fprintf(stderr, "window: the renderer did not start again after the loss bisect lost the "
                        "device; the next start runs normally\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "ICO PC",
                                 "The game checked why the graphics stopped last time, and the "
                                 "graphics driver needs a fresh start now.\n\nPlease start the "
                                 "game again.",
                                 s_window);
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return -3;
    }
    if (!up) {
        fprintf(stderr,
                "window: no usable %s device (rd_init failed; the rhi lines above say "
                "why)\n",
                backend);
#ifdef __ANDROID__
        choose_driver();
#endif
    }
    if (!up && (retry == STARTUP_RETRY_D3D12 || retry == STARTUP_RETRY_LOSS_BACKEND) &&
        !backendExplicit && rhi_backend() == RHI_BACKEND_D3D12) {
        /* the restart after a Vulkan driver crash or device loss found no
           Direct3D 12 (d3d12.dll missing, no device): nothing crashed, so
           this process goes on itself on Vulkan, after a crash with the
           pipelines built as the frames need them (the lazy step), after a
           loss with the fixed-function blend the loss step already set
           (its step kept, so a further loss ends with the box). A new
           window: Vulkan needs one made with SDL_WINDOW_VULKAN
           (window_flags) */
        fprintf(stderr, "ico_pc: Direct3D 12 could not start after the restart; using Vulkan%s\n",
                retry == STARTUP_RETRY_D3D12 ? " and building the shaders as the game needs them"
                                             : " with the simpler blending");
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        backend = "vulkan";
        rhi_create_backend(backend);
        if (retry == STARTUP_RETRY_D3D12) {
            retry = STARTUP_RETRY_LAZY;
        }
        s_retry = retry;
        ico_diag_set_startup_retry((int)retry, window_have_d3d12(), backendExplicit, blendExplicit,
                                   backend);
        goto open_window;
    }
    if (!up) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
#ifdef __ANDROID__
        return -2; /* the player chose Quit, or a driver package for the next
                      start; the box said why */
#else
        return -1;
#endif
    }
    /* whether the crash handler's first step would be to delete the file */
    ico_diag_set_pipeline_cache(rhi_pipeline_cache_loaded(), NULL);
    /* a device loss takes the loss order's robust step when robust access
       is off by the device's default, not by gpu_robust (startup_retry.h
       StartupRetryRun.robustOffDefault; Direct3D 12: NULL, its own rules) */
    ico_diag_set_robust_off_default(rhi_limits() != NULL && rhi_limits()->robustDefaultOff);
    {
        /* the watchdog's GPU hang counts as a device loss where the
           backend tracks its submissions (rhi_gpu_progress), said once */
        static int s_hangSaid;
        RhiGpuProgress gp;
        const int tracked = rhi_gpu_progress(&gp);

        ico_diag_set_gpu_hang(window_gpu_progress, window_gpu_hang);
        if (!s_hangSaid) {
            s_hangSaid = 1;
            fprintf(stderr, "%s",
                    tracked ? "window: GPU hang: the watchdog's end while the game waits for the "
                              "GPU and the work has not finished counts as a device loss (the "
                              "loss dump, the next start's loss steps)\n"
                            : "window: GPU hang: not told from a freeze on this graphics backend "
                              "(it does not track its submissions); its driver reports a hung "
                              "device as a loss\n");
        }
    }
#ifdef ICO_IOS
    s_background = 0;
#endif
#if defined(ICO_IOS) || defined(__ANDROID__)
    /* SDL delivers the app's lifecycle only to a watch, as it happens (SDL
       sends these on mobile systems only); from here, so the start-up
       set's time in the background pauses the watchdog (Android: before
       the window loop is open, lifecycle_watch) */
    if (!SDL_AddEventWatch(lifecycle_watch, NULL)) {
        fprintf(stderr, "window: SDL_AddEventWatch: %s\n", SDL_GetError());
    }
#else
    (void)lifecycle_watch;
#endif
    /* the whole reachable pipeline set before the first frame and after
       rd_init (which makes the device), so the game never waits on a
       pipeline compile (tens of ms each on a GPU driver); the time is
       logged. On a device of the table (RhiLimits.carefulPipelines: the
       Adreno 740 died inside the blind set, issues 93 and 107), by the
       settings or after a full set's end (pipePolicy) the set is made under
       the careful policy (rd_pipeline_policy_careful: small batches on one
       thread, the cache written after each, stopped at its memory budget,
       the rest then made on first use). Not after a driver crash in it
       (the lazy step), not by the settings or after a careful set's end or
       a second end in a row (pipePolicy): each pipeline is then built when
       a frame first needs it, the title comes at once, and the pipeline
       cache is written after each batch of them (rd_pipelines_lazy) */
    const char *devCareful = rhi_limits() != NULL ? rhi_limits()->carefulPipelines : NULL;
    /* an end while preparing the graphics, by kind: the first's line, the
       second's setting for good (diag_host.h ico_diag_startup_end_persist;
       no table rule holds for either kind: the device table's careful set
       is not the lazy one the setting writes) */
    int pipeSaved = 0;
    if (pipePolicy == ICO_DIAG_PIPESET_LAZY_ONCE || pipePolicy == ICO_DIAG_PIPESET_LAZY_NOW ||
        pipePolicy == ICO_DIAG_PIPESET_CAREFUL_ONCE) {
        pipeSaved = ico_diag_startup_end_persist(ICO_DIAG_END_PIPELINES, NULL,
                                                 pipeline_set_persist_lazy, cfgName);
    }
    if (preEnds >= 1) {
        (void)ico_diag_startup_end_persist(ICO_DIAG_END_VU_PRE, NULL, vu_prepass_persist, cfgName);
    }
    const bool carefulPolicy = pipePolicy == ICO_DIAG_PIPESET_FULL ||
                               pipePolicy == ICO_DIAG_PIPESET_CAREFUL_ONCE ||
                               pipePolicy == ICO_DIAG_PIPESET_CAREFUL_SETTING;
    if (retry == STARTUP_RETRY_LAZY || !carefulPolicy) {
        char why[160];

        if (retry == STARTUP_RETRY_LAZY) {
            snprintf(why, sizeof(why), "(a restart after a graphics driver crash)");
        } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_SETTING) {
            snprintf(why, sizeof(why), "by the settings ([dev] pipeline_set = \"lazy\")");
        } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_ONCE) {
            snprintf(why, sizeof(why),
                     "(the previous start ended while preparing them in the careful set; for "
                     "this start only)");
        } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_NOW && pipeSaved) {
            snprintf(why, sizeof(why),
                     "(the previous start ended while preparing them; [dev] pipeline_set = "
                     "\"lazy\" from now on)");
        } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_NOW) {
            snprintf(why, sizeof(why),
                     "(the previous start ended while preparing them; [dev] pipeline_set = "
                     "\"lazy\" could not be written, so for this start only)");
        } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_LOW_MEMORY) {
            snprintf(why, sizeof(why),
                     "(the previous start's set stopped when the system ran low on memory)");
        } else {
            snprintf(why, sizeof(why), "(the last run made them that way)");
        }
        /* each first-use pipeline's note in the file, for the whole run, so
           a further end names the pipeline; no further step follows */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_LAZY);
        rd_pipelines_lazy(why);
        /* the blend checked on this GPU (rd.h rd_blend_selftest), then the
           VU vertex pre-pass (rd_vuprepass_selftest) */
        rd_blend_selftest();
        rd_vuprepass_selftest();
    } else {
        /* the careful policy: on a device of the table, by the settings, or
           after the full set's end (for this start only) */
        char why[160] = "";
        const RdPipelineSetPolicy *careful = rd_pipeline_policy_careful();

        if (devCareful != NULL) {
            snprintf(why, sizeof(why), "on this device (%s)", devCareful);
            if (pipeSetting != NULL && strcmp(pipeSetting, "full") == 0) {
                fprintf(stderr,
                        "window: [dev] pipeline_set = \"full\" does not override the "
                        "device table (%s): the careful set\n",
                        devCareful);
            }
        } else if (pipePolicy == ICO_DIAG_PIPESET_CAREFUL_SETTING) {
            snprintf(why, sizeof(why), "by the settings ([dev] pipeline_set = \"careful\")");
        } else if (pipePolicy == ICO_DIAG_PIPESET_CAREFUL_ONCE) {
            snprintf(why, sizeof(why),
                     "(the previous start ended while preparing them; for this start only)");
        }
        if (why[0] != '\0') {
            rd_set_pipeline_policy(careful);
            fprintf(stderr,
                    "window: pipelines: careful start-up set %s: batches of %u on %u thread%s%s, "
                    "stopped past %u MB of growth\n",
                    why, careful->batch, careful->maxThreads, careful->maxThreads == 1 ? "" : "s",
                    careful->cacheEachBatch ? ", the cache written after each" : "",
                    careful->memoryBudgetMB);
            /* the file says so: an end inside it makes the next start
               lazy, not careful again */
            ico_diag_startup_activity_careful(1);
        }
        /* the note of each pipeline in the file while the set is created,
           the file deleted when it is done */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        /* which way of blending draws right on this GPU, tried before the
           set is made so the set is the chosen way's; logged ("blend: ...
           (self-test: ...)"). A crash in it is noted as the set's would be */
        rd_blend_selftest();
        /* whether the VU vertex pre-pass draws right on this GPU, tried
           before the set so the set holds the pre-pass's keys or the
           programs' ("VU pre-pass: on|off (self-test: ...)"); a crash in
           it is noted as the set's would be (its pipeline's " pre" or
           vu_tri note: startup_retry.h's noprepass step) */
        rd_vuprepass_selftest();
        static PipeScreen screen;

        /* a start with a cold driver cache can take many seconds (a phone,
           or a PC after a version that changed the shaders): the driver
           compiles on worker threads (rhi_create_pipelines) while this
           thread says what is going on (nothing is drawn when it is
           quick), with the count and an estimate, and takes the window's
           events about every 100 ms, so the system does not call the
           window "Not Responding" (Windows does after 5 s, Android asks
           whether to close the app) and a close asked meanwhile stops the
           set after the batch in flight and ends the run */
        rd_set_pipeline_progress(pipeline_progress, &screen);
        rd_precreate_pipelines();
        rd_set_pipeline_progress(NULL, NULL);
        ico_diag_set_pipelines(0, 0, 0);
        if (rd_pipelines_lazy_on()) {
            /* the careful policy's budget stopped the set: the rest of the
               run is recorded as a lazy run's, for this start only; or the
               system's low-memory signal did (issue 128): then not for this
               start only, and the next start makes no set */
            ico_diag_startup_activity_budget_stop();
        } else {
            ico_diag_startup_activity_record(0);
        }
        /* the set in the pipeline cache file now, not only at a clean exit:
           a phone's system ends the app without one, and a lost device
           skips it, so the next start would compile the set cold again
           ("This happens once after an update"); one read of the cache's
           data and one file write, its size and time logged, nothing when
           the file already holds it. Not when the set was stopped */
        if (!s_quitLatched && !rhi_device_lost()) {
            (void)rhi_pipeline_cache_save();
        }
    }
    /* a device loss from here takes the loss order's lossnoprepass step
       first when the run draws with the VU vertex pre-pass (its self-test
       above may have turned it off) */
    ico_diag_set_vu_prepass(rd_vu_prepass_on());
    /* which way of reading the depth gives the fog its distances on this
       GPU, tried on the real scene size; logged ("fog: depth path") */
    rd_fog_selftest();
    {
        IcoVideoOptions o;
        char res[32], fr[16];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height, framerate %s), window mode %s, vsync %s, "
                "texture pack %s, "
                "dump textures %s, model pack %s, dump models %s, depth %s, motion blur %s\n",
                w, h, s_fullscreen ? " fullscreen" : "", rhi_backend_kind_name(rhi_backend()),
                rhi_adapter_name(), video_preset_label(&o),
                ico_video_resolution_name(&o, res, sizeof(res)), ico_video_aspect_name(o.aspect),
                ico_video_filter_name(o.filter), o.fullHeight ? "full" : "half",
                ico_video_framerate_name(ico_video_framerate(), fr, sizeof(fr)),
                ico_video_window_mode_name(o.windowMode), o.vsync ? "on" : "off",
                o.texturePack ? "on" : "off",
                o.dumpTextures && ico_opt_developer_mode() ? "on" : "off",
                o.modelPack ? "on" : "off", o.dumpModels && ico_opt_developer_mode() ? "on" : "off",
                rhi_limits()->depthStencilFormatName, ico_video_motion_blur_name(&o));
        fprintf(stderr, "window: present mode %s%s\n", rhi_present_mode_name(),
                s_pres.gridLock         ? " (the cap equals the refresh: the vsync lock)"
                : s_pres.fifoLock       ? " (the cap equals the refresh)"
                : rhi_present_mailbox() ? " (vsync without waiting on the display)"
                                        : "");
        /* everything a report needs on the first screenful, once */
        log_summary(&o, res, fr);
        /* a program hooking the graphics API, once */
        ico_diag_set_effects_program(rhi_injector_name() != NULL);
        if (rhi_injector_name() != NULL) {
            fprintf(stderr, "window: an effects program is loaded (%s); see docs/RESHADE.md\n",
                    rhi_injector_name());
        }
        if (rhi_overlay_name() != NULL) {
            fprintf(stderr, "window: an overlay is loaded (%s)\n", rhi_overlay_name());
        }
        /* later changes are logged against this */
        s_videoLast = o;
    }
    /* the size may have changed while the renderer opened */
    window_size_recheck("after the renderer opened");
    s_pres.cutSerial = ico_video_cut_serial();
    /* [dev] slow_step_ms (default 8; 0 off) */
    s_pres.slowMs = ico_config_get_float("dev.slow_step_ms", 8.0);
    s_deadline = SDL_GetTicksNS();
    s_open = 1;
    /* the port's runtime text and popups (port/ui) */
    ui_host_init();
    /* the Window mode row shows what the window is, not the
       option (the window manager can refuse or change it) */
    ui_settings_set_window_mode_query(window_mode_now);
    /* Display > Texture pack says "None installed" without one */
    ui_settings_set_texture_pack_count(texpack_count);
#if !defined(__ANDROID__) && !defined(__APPLE__)
    /* Extras > Add to Steam (Windows and Linux; see steam_add_handler) */
    ui_settings_set_steam_handler(steam_add_handler);
#endif
    /* Main > Disc, shown with two or more discs' game data */
    ui_settings_set_disc_handler(ico_host_disc_archive_present);
    /* Main > Save a picture for a bug report (above Developer mode, always shown) */
    ui_settings_set_frame_dump_handler(report_picture_arm);
#if ICO_RC_TOOLS && defined(__ANDROID__)
    /* Main > Share a bug report, below it: an rc build on a phone alone */
    ui_settings_set_share_report_handler(share_report_arm);
#endif
#ifdef __ANDROID__
    /* the Graphics driver page, and "Quit game" for the
       title's Quit row (a phone has no desktop to quit to); both before the
       game's first frame builds the menus */
    ui_settings_set_gpu_driver_host(ico_gpu_driver_android_host());
#endif
#if defined(__ANDROID__) || defined(ICO_IOS)
    ui_settings_set_quit_is_game(quit_is_game);
#endif
    {
        char dir[ICO_PATH_MAX], path[ICO_PATH_MAX];

        ico_host_pref_dir(dir, sizeof(dir));
        ico_path_join(path, sizeof(path), dir, "config.toml");
        ico_input_sdl_init(path);
    }
    /* the touch overlay's zones, its Settings rows (with a
       touch screen) and the copy its drawing reads (port/ui/touch_ui.h) */
    touch_layout_update();
    ui_settings_set_touch_query(ico_input_sdl_touch_present);
    {
        UiTouchEditHost host;

        host.editor = ico_input_sdl_touch_editor;
        host.begin = ico_input_sdl_touch_edit_begin;
        host.end = ico_input_sdl_touch_edit_end;
        host.rule = ico_input_sdl_touch_rule;
        host.layout = ico_input_sdl_touch_layout;
        ui_settings_set_touch_edit_host(&host);
    }
    ui_touch_set_source(ico_input_sdl_touch_overlay);
    return 0;
}

/* mode is ICO_CAPTURE_* (mouse_look.h): STICK and DELTA hide and
   hold the pointer (relative mode), OFF frees it; the device layer routes
   the motion by mode */
static void set_capture(int mode)
{
#if defined(__ANDROID__) || defined(ICO_IOS)
    /* Android: no mouse to capture; touches and pads drive the camera */
    mode = ICO_CAPTURE_OFF;
#endif
    if (mode == s_captured) {
        return;
    }
    if ((mode != ICO_CAPTURE_OFF) != (s_captured != ICO_CAPTURE_OFF)) {
        SDL_SetWindowRelativeMouseMode(s_window, mode != ICO_CAPTURE_OFF);
    }
    s_captured = mode;
    ico_input_sdl_set_capture(mode);
}

/* The system pointer over the window: shown while it is free over a
   menu it works in (port/ui/ui_mouse.h) and no movie plays, hidden
   otherwise (play with the mouse camera off, a movie, a key or pad press in
   a menu); changed only when that changes.  With [input] mouse = false the
   pointer is left as the system has it.  Nothing is drawn: it is the
   system's own pointer. */
static void pointer_visibility(void)
{
    /* a movie stops the game's Main loop (and the pointer's tick) before
       the menus: the left button keeps its binding (Cross) and Cross does
       nothing in a movie; only Start skips one, on every input (a gamepad,
       a key bound to Start, touch START; main.c movie_abort_check) */
    if (mpegPlay != 0) {
        ico_pointer_set_menu(0);
    }
#if !defined(__ANDROID__) && !defined(ICO_IOS)
    static int s_shown = -1; /* -1: not managed (at the start, mouse off) */
    int want;

    if (!ico_input_live_bindings()->mouse_on) {
        if (s_shown == 0) {
            SDL_ShowCursor(); /* give it back as it was */
        }
        s_shown = -1;
        return;
    }
    want = s_captured == ICO_CAPTURE_OFF && ui_mouse_menu_active() && mpegPlay == 0;
    if (want == s_shown) {
        return;
    }
    if (want) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
    s_shown = want;
#endif
}

/* The game's state now, for the capture rule (once a pump) and Escape's
   button (at its press) */
static void capture_state(IcoCaptureState *c)
{
    const IcoBindings *b = ico_input_live_bindings();

    memset(c, 0, sizeof(*c));
    c->focus = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    c->look = b->mouse_on && b->mouse_camera;
    c->photo = ico_photo_active();
    c->boy = boyGObj != NULL;
    c->stage = stage_no;
    c->layout = current_layout_id;
    c->paused = systemStatus[5] != 0;
    c->loading = data_loading != 0;
    c->movie = mpegPlay != 0;
    c->viewer = ico_mv_active != 0;
    c->credits = ico_credits_active();
}

static int capture_mode(void)
{
    IcoCaptureState c;

    capture_state(&c);
    return ico_mouse_capture_rule(&c);
}

/* Escape, and Android's Back (SDL_HINT_ANDROID_TRAP_BACK_BUTTON): never a
   quit; Start in play (the pause menu opens), nothing on the game's
   Button configuration screen, Triangle anywhere else
   (ico_escape_target). Neither reaches the bindings. */
static int escape_key(SDL_Keycode key)
{
    return key == SDLK_ESCAPE || key == SDLK_AC_BACK;
}

static void escape_event(const SDL_Event *e)
{
    IcoCaptureState c;

    if (e->type == SDL_EVENT_KEY_UP) {
        ico_input_escape(0, NULL);
    } else if (!e->key.repeat) {
        capture_state(&c);
        ico_input_escape(1, &c);
    }
}

static void toggle_fullscreen(void)
{
    /* the window mode option flips between Windowed and the last other mode
       (in memory, not saved; the Settings menu sees it): video_apply asks
       SDL and the presenter boxes the picture (rd_resize_output follows) */
#if defined(__ANDROID__) || defined(ICO_IOS)
    /* the window is always the whole screen there and the Window mode row
       is not applied: Alt+Enter from a keyboard does nothing */
#else
    IcoVideoOptions o;
    const int now = window_mode_now();

    ico_video_get(&o);
    /* from what the window is, so a refused or WM-made change never needs
       two presses */
    if (now != ICO_WINDOW_WINDOWED) {
        s_lastFullMode = now;
    }
    o.windowMode = now == ICO_WINDOW_WINDOWED ? s_lastFullMode : ICO_WINDOW_WINDOWED;
    ico_video_set(&o);
    video_apply(0);
#endif
}

/* The loss dump (docs/port/RENDER.md, "The loss dump"): the frames the
   renderer still holds, the lost one and the one before it, as one zip in
   the logs folder, logs/loss-<date>-<time>.zip, with a copy of the log
   (rd_dump_retained, dump_zip.h ico_loss_dump_zip), so a report carries the
   frame the next start's loss bisect replays and the developers can replay
   it too. The oldest loss zips go beyond ICO_LOSS_DUMP_KEEP. Its name
   (the file name alone) into name, "" when none was written; the activity
   file notes the loss either way (diag_host.h ico_diag_note_device_loss). */
static void loss_dump(char *name, size_t name_size)
{
    char logs[ICO_PATH_MAX], zip[ICO_PATH_MAX], lost[ICO_PATH_MAX + 64], before[ICO_PATH_MAX + 64],
        stamp[32], why[160];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);
    uint32_t lostFrame = 0, beforeFrame = 0;

    name[0] = '\0';
    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_loss_dump_path(zip, sizeof(zip), logs, stamp) != 0) {
        fprintf(stderr, "window: loss dump: no usable logs folder\n");
        ico_diag_note_device_loss(NULL);
        return;
    }
    snprintf(lost, sizeof(lost), "%s/loss-%s-lost.rddump", logs, stamp);
    snprintf(before, sizeof(before), "%s/loss-%s-before.rddump", logs, stamp);
    if (!rd_dump_retained(lost, before, &lostFrame, &beforeFrame)) {
        fprintf(stderr, "window: loss dump: the renderer holds no frame to write\n");
        ico_remove(lost);
        ico_diag_note_device_loss(NULL);
        return;
    }
    if (beforeFrame == 0) {
        ico_remove(before);
    }
    ico_host_log_flush();
    if (ico_loss_dump_zip(zip, lost, beforeFrame ? before : NULL, ico_diag_log_path(), why,
                          sizeof(why)) < 0) {
        fprintf(stderr, "window: loss dump: zip not written: %s\n", why);
        ico_remove(lost);
        ico_remove(before);
        ico_diag_note_device_loss(NULL);
        return;
    }
    const int pruned = ico_loss_dump_prune(logs, ICO_LOSS_DUMP_KEEP);
    if (beforeFrame) {
        fprintf(stderr, "window: loss dump: frames %u and %u (the one before) written to %s%s\n",
                (unsigned)lostFrame, (unsigned)beforeFrame, zip,
                pruned > 0 ? "; older loss dumps deleted" : "");
    } else {
        fprintf(stderr, "window: loss dump: frame %u written to %s%s\n", (unsigned)lostFrame, zip,
                pruned > 0 ? "; older loss dumps deleted" : "");
    }
    snprintf(name, name_size, "loss-%s.zip", stamp);
    ico_diag_note_device_loss(zip);
}

/* The watchdog's GPU hang (diag_host.h ico_diag_set_gpu_hang): the
   backend's submissions as the main thread last saw them (rhi.h
   rhi_gpu_progress: no call into the driver), 0 when it does not track
   them (Direct3D 12) */
static int window_gpu_progress(IcoDiagGpu *out)
{
    RhiGpuProgress p;

    if (!rhi_gpu_progress(&p)) {
        return 0;
    }
    out->submitted = p.submitted;
    out->finished = p.finished;
    out->waiting_for = p.waitingFor;
    out->waiting_s = (double)p.waitingNs / 1e9;
    out->submitted_ago_s = (double)p.submittedAgoNs / 1e9;
    out->replay = p.replay;
    out->frame = p.frame;
    return 1;
}

/* The GPU hang's part of device_lost_quit, on the watchdog thread while
   the main thread is blocked in the driver waiting for the GPU: the index
   span, the pass ring and the loss dump (loss_dump, which notes the loss
   for the next start). CPU data only, no call into the device (it is
   hung). Nothing else writes what these read: the renderer's frames, its
   texture and mesh records and the pass ring are written by the main
   thread only (the texture pack's loader thread fills its own queue under
   its lock, texpack.c; the pipeline compiler threads run only inside a
   batch the main thread waits for, rhi_create_pipelines; the audio thread
   touches none of the renderer). The box and the report are the
   watchdog's (diag_host.c watchdog_fire). */
static void window_gpu_hang(void)
{
    char span[256];
    char dumpName[64];

    fprintf(stderr, "window: the graphics driver stopped responding (a GPU hang); what was being "
                    "drawn follows, as for a device loss\n");
    rd_index_span_text(span, sizeof(span));
    fprintf(stderr, "window: at the hang: %s\n", span);
    rd_passring_print(stderr, rd_perf_post_name);
    ico_host_log_flush();
    loss_dump(dumpName, sizeof(dumpName));
    ico_host_log_flush();
}

/* The renderer's device was removed, reset or hung (the driver
   crashed or was updated, the GPU was unplugged, an overclocked card
   stopped): nothing more can be drawn. The backend logged the reason and
   its loss report (docs/port/RHI.md, "Device loss"); the renderer's pass
   ring follows it here, so the log names what was being drawn
   (port/render/rd_passring.h). The loss dump follows (loss_dump: the
   frames as a zip in the logs folder, noted in the activity file for the
   next start's loss bisect). Then, when startup_retry.h has a loss step
   left (diag_host.c ico_diag_device_lost_restart), the session ends
   through the normal quit path without a box and the program starts again
   at exit with that step; otherwise the player gets one message and the
   session ends through the normal quit path (atexit closes the window). */
static int device_lost_quit(void)
{
    static int s_shown;

    if (!rhi_device_lost()) {
        return 0;
    }
    /* the start-up screens and the pump all ask: one line and one box */
    if (!s_shown) {
        s_shown = 1;
        fprintf(stderr, "window: the graphics device was lost; quitting\n");
        {
            /* issue 95: Mali's loss came from index spans past 2^24 - 1 */
            char span[256];

            rd_index_span_text(span, sizeof(span));
            fprintf(stderr, "window: at the loss: %s\n", span);
        }
        rd_passring_print(stderr, rd_perf_post_name);
        ico_host_log_flush();
        char dumpName[64];
        loss_dump(dumpName, sizeof(dumpName));
        ico_host_log_flush();
        /* the report file (diag_host.h): the log, the loss dump and the
           settings in one zip, the one file a report needs */
        const char *report = ico_diag_write_report("the graphics device was lost");
        if (ico_diag_device_lost_restart(rd_blend_exact() == RD_BLEND_EXACT_OFF)) {
            return 1;
        }
        /* the box is modal and the run ends when the player closes it: the
           time it stays open is not a hang (the watchdog's later limit
           would report one, the game standing since the loss), and the
           log says how long it was open; the watchdog runs again once it
           is closed, so a hang in the shutdown that follows is reported */
        ico_diag_watchdog_pause(1);
        const Uint64 t0 = SDL_GetTicksNS();

#if defined(__ANDROID__) || defined(ICO_IOS)
        /* a phone: no card, no overclock, no restart step; the player's
           next move is to start again and, if it repeats, send the log,
           which names what was being drawn (the pass ring above) */
        char text[1024];
        char logs[ICO_PATH_MAX];
        char also[160];
        if (ico_host_logs_dir(logs, sizeof(logs)) != 0) {
            logs[0] = '\0';
        }
        if (report != NULL) {
            snprintf(text, sizeof(text),
                     "The graphics driver stopped responding while the game was drawing, so the "
                     "game has to close. Your last save is kept.\n\nStart the game again. If "
                     "this happens every time at the same place, please report it at "
                     "github.com/nathanialf/ico-pc/issues and attach the newest file in logs "
                     "that starts with report- (this time: %s): it shows what was being drawn.",
                     report);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC", text, s_window);
        } else if (dumpName[0] != '\0') {
            snprintf(also, sizeof(also), " and the file %s beside it", dumpName);
        } else {
            also[0] = '\0';
        }
        if (report == NULL) {
            snprintf(text, sizeof(text),
                     "The graphics driver stopped responding while the game was drawing, so the "
                     "game has to close. Your last save is kept.\n\nStart the game again. If "
                     "this happens every time at the same place, please send the log file%s%s%s%s "
                     "with a report at github.com/nathanialf/ico-pc/issues: %s what was being "
                     "drawn.",
                     logs[0] != '\0' ? " (" : "", logs, logs[0] != '\0' ? "/ico-pc.log)" : "", also,
                     dumpName[0] != '\0' ? "together they show" : "it says");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC", text, s_window);
        }
#else
        char text[1024];
        char also[256];
        if (report != NULL) {
            snprintf(also, sizeof(also),
                     "\n\nIf you report this at github.com/nathanialf/ico-pc/issues, please "
                     "attach the newest file in logs that starts with report- (this time: %s).",
                     report);
        } else if (dumpName[0] != '\0') {
            snprintf(also, sizeof(also),
                     "\n\nThe game saved what it was drawing as %s in the logs folder, next to "
                     "the log. If you report this, please attach both files.",
                     dumpName);
        } else {
            also[0] = '\0';
        }
        snprintf(text, sizeof(text), "%s%s",
                 startup_retry_is_loss(s_retry)
                     ? "The graphics card stopped responding (its driver was reset or updated, or "
                       "the card was removed).\n\nThe game restarted itself with safer "
                       "graphics settings and the card still stopped responding.\n\nThe game "
                       "has to "
                       "close. Your last save is kept; the log says why.\n\nIf the graphics "
                       "card is overclocked, try its stock clocks first."
                     : "The graphics card stopped responding (its driver was reset or updated, or "
                       "the card was removed).\n\nThe game has to close. Your last save is "
                       "kept; the log says why.\n\nIf the graphics card is overclocked, try its "
                       "stock clocks first.",
                 also);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC", text, s_window);
#endif
        fprintf(stderr, "window: the message was closed after %.1f s\n",
                (double)(SDL_GetTicksNS() - t0) / 1e9);
        ico_diag_watchdog_pause(0);
    }
    return 1;
}

/* F12, the frame on the screen for a bug report, in <pref>/dumps: a PNG
   and a zip of the rd dump, the tick before's (rd_dump_prev_frame: with it
   rd_replay_tool --interp rebuilds the pictures between the two, where the
   presenter blends) and a copy of the log (GitHub issues refuse
   .rddump files but take a .zip; the PNG stays a loose file, it is too big
   for a zip at high scales). The dump is written first, zipped, and the
   loose .rddump deleted by ico_frame_dump_zip once the zip is written; when the zip fails the
   .rddump is kept. The log is logs/ico-pc.log, a file on Android too; with
   --console there is none and the zip holds the dump alone. */
static void frame_dump(void)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX],
        zip[ICO_PATH_MAX], prev[ICO_PATH_MAX + 8], why[160];
    char stamp[32];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, "dumps") != 0 || ico_make_dir(dir) != 0) {
        fprintf(stderr, "window: F12: no usable dumps folder under %s\n", pref);
        return;
    }
    if (ico_frame_dump_paths(dir, stamp, ico_host_vsync_count(), dump, sizeof(dump), png,
                             sizeof(png), zip, sizeof(zip)) != 0) {
        fprintf(stderr, "window: F12: the dumps folder %s is too deep for the file names\n", dir);
        return;
    }
    const int ok = rd_dump_on_demand(dump, png);
    /* the tick before, <name>-prev.rddump (none when it is not the tick
       right before, or not closed) */
    const size_t stem = strlen(dump) > 7 ? strlen(dump) - 7 : 0;
    snprintf(prev, sizeof(prev), "%.*s-prev.rddump", (int)stem, dump);
    const int havePrev = ok && rd_dump_prev_frame(prev);
    /* the picture as presented, frame-<stamp>-present.png, at the next
       present (the .png above is the whole DISPLAY frame, which at 16:9 is
       wider than the window's picture; tools/native_frame_check.py reads
       that one) */
    if (ico_frame_present_path(dir, stamp, s_pres.presentPng, sizeof(s_pres.presentPng)) == 0 &&
        !rd_capture_presented(s_pres.presentPng)) {
        fprintf(stderr, "window: F12: the presented picture could not be armed\n");
        s_pres.presentPng[0] = '\0';
    }
    s_pres.presentInfoPending = s_pres.presentPng[0] != '\0';
    s_pres.presentInfoHave = 0;
    s_pres.presentDumpFrame = rd_frame_number();
    s_pres.presentDumpPrev = 0;
    /* the recording up to this moment, for the same report */
    ico_input_record_flush();
    /* the zip is made after the dump is closed; with --console the log
       "path" is not a file, so the zip holds the dump alone */
    int zr = -1;

    snprintf(why, sizeof(why), "no dump");
    if (ok) {
        ico_host_log_flush();
        zr = ico_frame_dump_zip(zip, dump, havePrev ? prev : NULL, ico_diag_log_path(), why,
                                sizeof(why));
    }
    s_pres.presentDumpPrev = havePrev && zr >= 0;
    if (zr < 0) {
        fprintf(stderr, "window: dump: zip not written: %s%s\n", why,
                ok ? "; the .rddump is kept" : "");
    }
    fprintf(stderr, "window: F12 at vsync %u, Main tick %u, frame %u: %s %s%s%s (%s)\n",
            ico_host_vsync_count(), ico_host_main_ticks(), (unsigned)rd_frame_number(),
            ok ? "wrote" : "could not write all of", png, zr >= 0 ? " and " : "",
            zr >= 0 ? zip : "",
            havePrev ? "with the tick before: rd_replay_tool --interp rebuilds the pictures between"
                     : "without the tick before");
}

/* Settings > Save a picture for a bug report (the Main page; settings.h
   ui_settings_set_frame_dump_handler): F12's dump 3 s after the press,
   time to leave the menu, from the pump (report_picture_pump). A phone has
   no F12; the zip lands in the same dumps folder. */
static Uint64 s_reportDumpAt;

static void report_picture_arm(void)
{
    s_reportDumpAt = SDL_GetTicksNS() + 3000000000ull;
}

static void report_picture_pump(void)
{
    if (s_reportDumpAt == 0 || SDL_GetTicksNS() < s_reportDumpAt) {
        return;
    }
    s_reportDumpAt = 0;
    fprintf(stderr, "window: the picture for a bug report (Settings, 3 s after the press):\n");
    frame_dump();
}

#if ICO_RC_TOOLS && defined(__ANDROID__)
/* Settings > Share a bug report (the Main page, an rc build's row alone:
   ICO_RC_TOOLS; settings.h ui_settings_set_share_report_handler): the
   report's files (share_report.h: the log, the run before's, the newest
   dump zip and presented picture, the newest report zip) to the system's
   share menu (host_android.c ico_android_share_files), from the pump on
   SDL's main thread, whose JNI environment the call takes.  The files
   folder is out of reach on a phone without a computer.  The line naming
   the files is logged and the log flushed first, so the log shared holds
   it. */
static int s_shareArmed;

static void share_report_arm(void)
{
    s_shareArmed = 1;
}

static void share_report_pump(void)
{
    static IcoShareReport s_files;
    char logs[ICO_PATH_MAX], pref[ICO_PATH_MAX], dumps[ICO_PATH_MAX], names[768], why[256];
    const char *paths[ICO_SHARE_REPORT_MAX];

    if (!s_shareArmed) {
        return;
    }
    s_shareArmed = 0;
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0) {
        logs[0] = '\0';
    }
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dumps, sizeof(dumps), pref, "dumps") != 0) {
        dumps[0] = '\0';
    }
    ico_share_report_collect(logs, dumps, &s_files);
    ico_share_report_names(&s_files, names, sizeof(names));
    fprintf(stderr, "window: share: %d files offered (%s)\n", s_files.count, names);
    if (s_files.count == 0) {
        fprintf(stderr, "window: share: nothing to share (no log, dump or report in %s, %s)\n",
                logs[0] ? logs : "no logs folder", dumps[0] ? dumps : "no dumps folder");
        return;
    }
    ico_host_log_flush();
    for (int i = 0; i < s_files.count; i++) {
        paths[i] = s_files.path[i];
    }
    if (ico_android_share_files(ui_str(UI_STR_OPT_SHARE_REPORT), paths, s_files.count, why,
                                sizeof(why)) != 0) {
        fprintf(stderr, "window: share: the share menu did not open: %s\n", why);
    }
}
#endif

/* Shift+F12 in photo mode, the aspect sweep: the paused frame captured at
   4:3, 16:9, 32:9, 48:9 and 60:9 into <pref>/dumps/sweep-<time>.zip, each
   aspect's <aspect>.rddump and <aspect>.png (dump_zip.h ico_sweep_zip), for
   tools/native_frame_check.py.

   The game builds its frame at the width gsFrame holds, latched at each
   flip from ico_video_frame (GsBase.c gsbHostFrameLatch), so the sweep forces
   the aspect there (ico_video_frame_force_aspect) and waits.  At the next
   flip the latch makes the new width and remakes rd's scene-sized targets
   (rd_reset_scene, the 50/60 Hz switch's path), and the paused tick that
   follows draws from the photo camera (photo_view.c ico_photo_view_tick) and
   rebuilds the game's matrices at that width, +0x280 the cull's included,
   each tick: no game state is touched.  Once the last closed frame is a frame
   at the width and SWEEP_SETTLE closed frames have followed it (the feedback
   passes and the blended picture settle), the normal F12 path takes it
   (rd_dump_on_demand: the .rddump and the DISPLAY .png at the native width).
   After the last aspect, or on leaving photo mode or a timeout, the forcing
   is cleared: the next flip latches the live width back with its
   rd_reset_scene.  The window shows the sweep's frames meanwhile (a second
   or two); the presentation's aspect stays the live one. */
#define SWEEP_SETTLE 4
#define SWEEP_TIMEOUT_PUMPS 900

static struct {
    int active, idx, captured, seen, waited;
    unsigned lastNumber;
    char prefix[ICO_PATH_MAX], zip[ICO_PATH_MAX];
} s_sweep;

static void sweep_finish(const char *reason)
{
    char why[160];

    ico_video_frame_force_aspect(0.0f);
    s_sweep.active = 0;
    why[0] = '\0';
    if (reason != NULL) {
        fprintf(stderr, "window: aspect sweep: %s\n", reason);
    }
    if (s_sweep.captured > 0) {
        ico_host_log_flush();
        const int n = ico_sweep_zip(s_sweep.zip, s_sweep.prefix, why, sizeof(why));

        if (n >= 0) {
            fprintf(stderr, "window: aspect sweep: %d of %d aspects in %s\n", n, ICO_SWEEP_COUNT,
                    s_sweep.zip);
        } else {
            fprintf(stderr, "window: aspect sweep: zip not written: %s; the loose files are kept\n",
                    why);
        }
    } else {
        fprintf(stderr, "window: aspect sweep: nothing captured\n");
    }
}

static void sweep_start(void)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], stamp[32];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (s_sweep.active) {
        return;
    }
    if (!ico_photo_active()) {
        fprintf(stderr, "window: aspect sweep: Shift+F12 works in photo mode only\n");
        return;
    }
    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, "dumps") != 0 || ico_make_dir(dir) != 0 ||
        ico_sweep_paths(dir, stamp, s_sweep.prefix, sizeof(s_sweep.prefix), s_sweep.zip,
                        sizeof(s_sweep.zip)) != 0) {
        fprintf(stderr, "window: aspect sweep: no usable dumps folder under %s\n", pref);
        return;
    }
    s_sweep.active = 1;
    s_sweep.idx = 0;
    s_sweep.captured = 0;
    s_sweep.seen = 0;
    s_sweep.waited = 0;
    s_sweep.lastNumber = 0;
    fprintf(stderr, "window: aspect sweep: starts, %d aspects\n", ICO_SWEEP_COUNT);
    ico_video_frame_force_aspect(ico_sweep_aspect_value(0));
}

/* one pump of the sweep: capture the aspect in force once its frame has
   settled, then ask for the next */
static void sweep_pump(void)
{
    IcoFrame want;
    uint32_t number = 0, w = 0;
    float aspect = 0.0f;
    char dump[ICO_PATH_MAX], png[ICO_PATH_MAX];

    if (!s_sweep.active) {
        return;
    }
    if (!ico_photo_active()) {
        sweep_finish("left photo mode");
        return;
    }
    if (++s_sweep.waited > SWEEP_TIMEOUT_PUMPS) {
        fprintf(stderr, "window: aspect sweep: no frame at %s after %d vsyncs\n",
                ico_sweep_aspect_name(s_sweep.idx), SWEEP_TIMEOUT_PUMPS);
        sweep_finish("gave up");
        return;
    }
    ico_frame_make(&want, ico_sweep_aspect_value(s_sweep.idx), 448);
    if (!rd_last_frame_native(&number, &aspect, &w) || (int)w != want.w ||
        aspect < want.aspect - 1e-3f || aspect > want.aspect + 1e-3f) {
        s_sweep.seen = 0;
        return;
    }
    if (number != s_sweep.lastNumber) {
        s_sweep.lastNumber = number;
        s_sweep.seen++;
    }
    if (s_sweep.seen < SWEEP_SETTLE) {
        return;
    }
    if (snprintf(dump, sizeof(dump), "%s%s.rddump", s_sweep.prefix,
                 ico_sweep_aspect_name(s_sweep.idx)) >= (int)sizeof(dump) ||
        snprintf(png, sizeof(png), "%s%s.png", s_sweep.prefix,
                 ico_sweep_aspect_name(s_sweep.idx)) >= (int)sizeof(png)) {
        sweep_finish("the capture path is too long");
        return;
    }
    if (rd_dump_on_demand(dump, png)) {
        s_sweep.captured++;
        fprintf(stderr, "window: aspect sweep: %s at frame %u, GS width %u\n",
                ico_sweep_aspect_name(s_sweep.idx), (unsigned)number, (unsigned)w);
    } else {
        fprintf(stderr, "window: aspect sweep: %s could not be written\n",
                ico_sweep_aspect_name(s_sweep.idx));
    }
    s_sweep.idx++;
    s_sweep.seen = 0;
    s_sweep.waited = 0;
    s_sweep.lastNumber = 0;
    if (s_sweep.idx >= ICO_SWEEP_COUNT) {
        sweep_finish(NULL);
    } else {
        ico_video_frame_force_aspect(ico_sweep_aspect_value(s_sweep.idx));
    }
}

/* Cross in photo mode: the picture shown at the next present into
   <pref>/<[photo] png_dir>/ico-<time>.png (rd_capture_presented); the
   result comes back at a later pump (photo_pump) */
static void photo_capture(void)
{
    static char lastName[64];
    static int seq;
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], path[ICO_PATH_MAX], name[64];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);
    struct tm zero;

    if (tm == NULL) {
        memset(&zero, 0, sizeof(zero));
        tm = &zero;
    }
    ico_photo_file_name(name, sizeof(name), tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                        tm->tm_hour, tm->tm_min, tm->tm_sec, 1);
    /* two captures in one second: -2, -3, ... */
    if (strcmp(name, lastName) == 0) {
        ico_photo_file_name(name, sizeof(name), tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                            tm->tm_hour, tm->tm_min, tm->tm_sec, ++seq);
    } else {
        snprintf(lastName, sizeof(lastName), "%s", name);
        seq = 1;
    }
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, ico_photo_png_dir()) != 0 || ico_make_dir(dir) != 0 ||
        ico_path_join(path, sizeof(path), dir, name) != 0) {
        fprintf(stderr, "photo: no usable folder %s under %s\n", ico_photo_png_dir(), pref);
        ui_photo_capture_done(0, name);
        return;
    }
    if (!rd_capture_presented(path)) {
        fprintf(stderr, "photo: the capture could not be armed (%s)\n", path);
        ui_photo_capture_done(0, name);
    }
}

/* Cross in photo mode arms a capture of the next present
   (the paused game draws from the photo camera itself, port/game/
   photo_view.c); the captures' results become a popup */
static void photo_pump(void)
{
    if (ico_photo_active()) {
        while (ico_photo_take_capture()) {
            photo_capture();
        }
    }
    {
        char path[ICO_PATH_MAX];
        const int r = rd_capture_result(path, sizeof(path));

        if (r != 0) {
            const char *base = strrchr(path, '/');
#ifdef _WIN32
            const char *b2 = strrchr(path, '\\');

            base = b2 != NULL && (base == NULL || b2 > base) ? b2 : base;
#endif
            if (s_pres.presentPng[0] != '\0' && strcmp(path, s_pres.presentPng) == 0) {
                /* F12's presented picture: no photo popup.  The capture is
                   taken by the first present after F12 armed it, and this
                   pump can follow several more presents, so the present it
                   names is the one present_once noted then; without one
                   (interpolation off: rd_end_frame's present in the step
                   before this pump) the last present, which is it */
                RdPresentInfo pi;
                const int have = s_pres.presentInfoHave ? (pi = s_pres.presentInfo, 1)
                                                        : rd_last_present_info(&pi);
                char what[192] = "";

                if (have && pi.frame == s_pres.presentDumpFrame && pi.keep) {
                    /* a keep frame's present shows DISPLAY again: nothing
                       interpolated to rebuild */
                    snprintf(what, sizeof(what),
                             " (the present of frame %u at alpha %.9g, keep 1: the picture shown "
                             "again)",
                             pi.frame, (double)pi.alpha);
                } else if (have && pi.frame == s_pres.presentDumpFrame && s_pres.presentDumpPrev) {
                    snprintf(what, sizeof(what),
                             " (the present of frame %u at alpha %.9g, keep 0: rd_replay_tool "
                             "--interp %.9g rebuilds it from the zip)",
                             pi.frame, (double)pi.alpha, (double)pi.alpha);
                } else if (have && pi.frame == s_pres.presentDumpFrame) {
                    snprintf(what, sizeof(what),
                             " (the present of frame %u at alpha %.9g, keep 0; the zip has no "
                             "tick before to rebuild it from)",
                             pi.frame, (double)pi.alpha);
                } else if (have) {
                    snprintf(what, sizeof(what),
                             " (the present of frame %u at alpha %.9g, a later frame than the "
                             "zip's %u)",
                             pi.frame, (double)pi.alpha, s_pres.presentDumpFrame);
                }
                fprintf(stderr, "window: F12: %s %s%s\n", r > 0 ? "wrote" : "could not write", path,
                        what);
                s_pres.presentPng[0] = '\0';
                s_pres.presentInfoPending = s_pres.presentInfoHave = 0;
            } else {
                ui_photo_capture_done(r > 0, base != NULL ? base + 1 : path);
            }
        }
    }
}

/* F11, the stats lines every second for 30 s */
static void stats_fast_toggle(void)
{
    s_pres.fastUntil = ico_stats_fast_toggle(SDL_GetTicksNS(), s_pres.fastUntil);
    fprintf(stderr, "window: F11: the stats lines every %s\n",
            s_pres.fastUntil ? "second for 30 s" : "10 s again");
}

/* The window entered or left fullscreen, at our request, at
   the window manager's (its own shortcut), or a request was refused.  The
   state is read from the window, not the event (one queued before a later
   request is stale); when the option disagrees it follows the window, so
   the menu and Alt+Enter start from the truth.  video_apply then finds the
   option matching the window and requests nothing. */
static void window_fullscreen_event(int entered)
{
    IcoVideoOptions o;

    {
        /* the surface made again on the window (rhi_recreate_surface, the
           swapchain with it): a pause of the host (video_apply) */
        const Uint64 t0 = SDL_GetTicksNS();

        s_fullscreen = window_fullscreen();
        s_pres.pausePendingNs += SDL_GetTicksNS() - t0;
    }
    ico_video_get(&o);
    fprintf(stderr, "window: %s fullscreen; the window is %s, the option says %s\n",
            entered ? "entered" : "left", s_fullscreen ? "fullscreen" : "windowed",
            ico_video_window_mode_name(o.windowMode));
    /* fullscreen entered makes the option Fullscreen; left
       turns Fullscreen into Windowed.  A requested Borderless is left alone:
       it is what a fullscreen window becomes when asked to be borderless. */
    if (s_fullscreen && o.windowMode != ICO_WINDOW_FULLSCREEN) {
        o.windowMode = ICO_WINDOW_FULLSCREEN;
        ico_video_set(&o);
    } else if (!s_fullscreen && o.windowMode == ICO_WINDOW_FULLSCREEN) {
        o.windowMode = ICO_WINDOW_WINDOWED;
        ico_video_set(&o);
    }
#if !defined(__ANDROID__) && !defined(ICO_IOS)
    /* the window manager's own change: the next swapchain (its size event)
       asks for what the window now is */
    exclusive_follow(o.windowMode);
#endif
}

/* --- the event pump's long calls (issue 105) ------------------------------ */

/* the parts of the pump the slow-pump line and the watchdog name */
#define PART_POLL "the system's messages and SDL's own work (SDL_PollEvent)"
#define PART_JOYSTICKS "the controllers' scan and update (SDL_UpdateJoysticks)"

/* the watchdog's description of a pump the system holds (diag_host.h
   ico_diag_set_os_wait_describe; the watchdog's thread: reads only) */
static void pump_describe(char *out, size_t size)
{
    pump_watch_describe(out, size, SDL_GetTicksNS());
}

/* A part of the pump that began at t0 (SDL_GetTicksNS): over 1 s, a line
   naming it with the window messages and SDL events before it */
static void pump_slow(const char *part, Uint64 t0)
{
    const Uint64 now = SDL_GetTicksNS();
    char recent[1024];

    if (now - t0 <= PUMP_WATCH_SLOW_NS) {
        return;
    }
    pump_watch_recent(recent, sizeof(recent), now);
    fprintf(stderr, "window: the event pump's %s took %.1f ms at vsync %u (Main tick %u); %s\n",
            part, (double)(now - t0) / 1e6, ico_host_vsync_count(), ico_host_main_ticks(), recent);
}

/* After a slow pump's line: SDL's own log lines from 120 s before the
   part began to now (pump_watch.h; the joystick drivers' add, remove and
   warning lines), or that there were none */
static void pump_slow_sdl_log(Uint64 t0)
{
    static char text[PUMP_WATCH_SDL_LINES * (PUMP_WATCH_SDL_TEXT + 96)];
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 since = t0 > 120000000000ull ? t0 - 120000000000ull : 1;
    const int n = pump_watch_sdl_text(text, sizeof(text), "window:   ", since, now);

    if (n == 0) {
        fprintf(stderr,
                "window: SDL's own log: no line from 120 s before it to its end (%u "
                "lines this run)\n",
                (unsigned)pump_watch_sdl_count());
        return;
    }
    fprintf(stderr,
            "window: SDL's own log from 120 s before it to its end (%d lines, oldest "
            "first):\n%s",
            n, text);
}

/* SDL_PollEvent, marked as a wait the system holds (the watchdog does not
   count it, diag_host.h ico_diag_os_wait_enter) and timed */
static bool pump_poll(SDL_Event *e)
{
    const Uint64 t0 = SDL_GetTicksNS();

    pump_watch_part(PART_POLL);
    ico_diag_os_wait_enter("the window's event pump");
    const bool got = SDL_PollEvent(e);
    ico_diag_os_wait_leave();
    pump_watch_part(NULL);
    if (SDL_GetTicksNS() - t0 > PUMP_WATCH_SLOW_NS) {
        pump_slow(PART_POLL, t0);
        pump_slow_sdl_log(t0);
    }
    return got;
}

#ifdef _WIN32
/* SDL_UpdateJoysticks, out of SDL_PollEvent on Windows
   (SDL_HINT_AUTO_UPDATE_JOYSTICKS, ico_window_open): the same */
static void pump_joysticks(void)
{
    /* the HID device list's change count before and after (SDL_hidapi.h:
       on this thread a read of SDL's counter) */
    const Uint32 hid0 = SDL_hid_device_change_count();
    const Uint64 t0 = SDL_GetTicksNS();

    pump_watch_part(PART_JOYSTICKS);
    ico_diag_os_wait_enter("the window's event pump");
    SDL_UpdateJoysticks();
    ico_diag_os_wait_leave();
    pump_watch_part(NULL);
    const Uint64 now = SDL_GetTicksNS();
    if (now - t0 <= PUMP_WATCH_SLOW_NS) {
        return;
    }
    pump_slow(PART_JOYSTICKS, t0);
    {
        /* what the scan found and what changed around it (issue 105): a
           stall the DirectInput scan makes follows a HID change, and
           [dev] joystick_directinput = false is the test */
        int n = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks(&n);
        const Uint32 hid1 = SDL_hid_device_change_count();
        const Uint64 last = pump_watch_pad_last_ns();
        char ago[48];

        SDL_free(ids);
        if (last != 0) {
            snprintf(ago, sizeof(ago), "%.1f s before it ended", (double)(now - last) / 1e9);
        } else {
            snprintf(ago, sizeof(ago), "never this run");
        }
        fprintf(stderr,
                "window: the controllers' scan: %d controllers after it; the HID device list "
                "changed %u times during it (SDL_hid_device_change_count %u -> %u); the last "
                "controller arrived or left %s; the DirectInput scan is %s\n",
                n, (unsigned)(hid1 - hid0), (unsigned)hid0, (unsigned)hid1, ago,
                s_joyDirectInput ? "on (to test it: [dev] joystick_directinput = false)"
                                 : "off ([dev] joystick_directinput = false)");
    }
    pump_slow_sdl_log(t0);
}
#endif

/* The SDL events the slow-pump line lists (NULL: not listed: the ones that
   come by the hundred) */
static const char *pump_event_name(Uint32 type)
{
    switch (type) {
    case SDL_EVENT_QUIT:
        return "quit";
    case SDL_EVENT_TERMINATING:
        return "terminating";
    case SDL_EVENT_LOW_MEMORY:
        return "low memory";
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        return "app to the background";
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        return "app to the foreground";
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        return "system theme changed";
    case SDL_EVENT_DISPLAY_ORIENTATION:
        return "display orientation";
    case SDL_EVENT_DISPLAY_ADDED:
        return "display added";
    case SDL_EVENT_DISPLAY_REMOVED:
        return "display removed";
    case SDL_EVENT_DISPLAY_MOVED:
        return "display moved";
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        return "display desktop mode changed";
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        return "display mode changed";
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        return "display scale changed";
    case SDL_EVENT_WINDOW_SHOWN:
        return "window shown";
    case SDL_EVENT_WINDOW_HIDDEN:
        return "window hidden";
    case SDL_EVENT_WINDOW_EXPOSED:
        return "window exposed";
    case SDL_EVENT_WINDOW_MOVED:
        return "window moved";
    case SDL_EVENT_WINDOW_RESIZED:
        return "window resized";
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return "window pixel size changed";
    case SDL_EVENT_WINDOW_MINIMIZED:
        return "window minimized";
    case SDL_EVENT_WINDOW_MAXIMIZED:
        return "window maximized";
    case SDL_EVENT_WINDOW_RESTORED:
        return "window restored";
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        return "mouse entered";
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return "mouse left";
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return "focus gained";
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return "focus lost";
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return "close requested";
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        return "window display changed";
    case SDL_EVENT_WINDOW_OCCLUDED:
        return "window occluded";
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        return "entered fullscreen";
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        return "left fullscreen";
    case SDL_EVENT_KEY_DOWN:
        return "key down";
    case SDL_EVENT_KEY_UP:
        return "key up";
    case SDL_EVENT_KEYBOARD_ADDED:
        return "keyboard added";
    case SDL_EVENT_KEYBOARD_REMOVED:
        return "keyboard removed";
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return "mouse button";
    case SDL_EVENT_MOUSE_ADDED:
        return "mouse added";
    case SDL_EVENT_MOUSE_REMOVED:
        return "mouse removed";
    case SDL_EVENT_JOYSTICK_ADDED:
        return "joystick added";
    case SDL_EVENT_JOYSTICK_REMOVED:
        return "joystick removed";
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
        return "joystick battery";
    case SDL_EVENT_GAMEPAD_ADDED:
        return "gamepad added";
    case SDL_EVENT_GAMEPAD_REMOVED:
        return "gamepad removed";
    case SDL_EVENT_GAMEPAD_REMAPPED:
        return "gamepad remapped";
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return "gamepad button";
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
        return "audio device added";
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        return "audio device removed";
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
        return "audio device format changed";
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_DEVICE_LOST:
        return "render device reset";
    default:
        return NULL;
    }
}

/* A controller arrived or left: recorded with its name, GUID and SDL
   driver for the slow-pump lines (pump_watch_pad; a removal is named
   from its arrival), and a line */
static void pump_pad_event(const SDL_Event *e, const char *name)
{
    const SDL_JoystickID id = e->jdevice.which;
    const Uint64 now = SDL_GetTicksNS();
    char line[256];

    if (e->type == SDL_EVENT_JOYSTICK_ADDED) {
        const SDL_GUID g = SDL_GetJoystickGUIDForID(id);
        const char *pad = SDL_GetJoystickNameForID(id);
        char gs[40];
#ifdef _WIN32
        const int windows = 1;
#else
        const int windows = 0;
#endif

        SDL_GUIDToString(g, gs, sizeof(gs));
        pump_watch_pad(1, (uint32_t)id, pad != NULL ? pad : "(unnamed)", gs,
                       pump_watch_guid_driver(g.data, windows), now);
    } else {
        pump_watch_pad(0, (uint32_t)id, NULL, NULL, NULL, now);
    }
    pump_watch_pad_newest(line, sizeof(line));
    fprintf(stderr, "window: system event: %s at vsync %u: %s\n", name, ico_host_vsync_count(),
            line);
}

/* The system's events worth a line of their own as they come: the
   window's focus and visibility, the displays, the joysticks (the
   gamepads have input_sdl.c's line) */
static void pump_event_line(const SDL_Event *e, const char *name)
{
    switch (e->type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        fprintf(stderr, "window: system event: %s at vsync %u\n", name, ico_host_vsync_count());
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        pump_pad_event(e, name);
        break;
    default:
        break;
    }
}

int ico_window_pump(void)
{
    SDL_Event e;
    IcoHotkey hotkey;
    int quit = 0;
    unsigned events = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (device_lost_quit()) {
        return 0;
    }
    if (s_quitLatched) {
        s_quitLatched = 0;
        quit = 1;
    }
    /* a run without the start-up set (issues 93 and 107): the pipelines
       the last frames made closed as a batch once they pause (the pipeline
       cache written, the batch's line logged), the count for the
       low-memory line, and such a line waiting for the activity file */
    rd_pipelines_lazy_tick();
    ico_diag_set_pipelines_made(rd_pipeline_count());
    ico_diag_startup_activity_poll();

#ifdef _WIN32
    /* the display and the computer kept awake while the window is focused and
       not minimized (window_pump_win.h), and the
       controllers' scan SDL_PollEvent no longer runs (pump_joysticks) */
    ico_pump_win_keep_awake(
        t0, s_window != NULL &&
                (SDL_GetWindowFlags(s_window) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MINIMIZED)) ==
                    SDL_WINDOW_INPUT_FOCUS);
    pump_joysticks();
#endif
    while (pump_poll(&e)) {
        const Uint64 h0 = SDL_GetTicksNS();
        const char *name = pump_event_name(e.type);

        events++;
        if (name != NULL) {
            pump_watch_event(name, h0);
            pump_event_line(&e, name);
        }
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            hotkey = ico_hotkey_for_mod(e.key.key, e.key.repeat, (e.key.mod & SDL_KMOD_SHIFT) != 0);
            if (hotkey == ICO_HOTKEY_FRAME_DUMP) {
                frame_dump();
            } else if (hotkey == ICO_HOTKEY_ASPECT_SWEEP) {
                sweep_start();
            } else if (hotkey == ICO_HOTKEY_STATS_FAST) {
                stats_fast_toggle();
            } else if (e.key.key == SDLK_F11 || e.key.key == SDLK_F12) {
                /* a held key's repeats: nothing */
            } else if (escape_key(e.key.key)) {
                escape_event(&e);
            } else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) &&
                       (e.key.mod & SDL_KMOD_ALT) != 0) {
                if (!e.key.repeat) {
                    toggle_fullscreen();
                }
            } else {
                ico_input_sdl_event(&e);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (escape_key(e.key.key)) {
                escape_event(&e);
            } else {
                ico_input_sdl_event(&e);
            }
            break;
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
            /* [audio] device: the chosen device unplugged (out_sdl.c) */
            if (!e.adevice.recording) {
                ico_audio_sdl_device_removed(e.adevice.which);
            }
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            window_fullscreen_event(e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN);
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            /* a frameless window follows its display */
            if (window_mode_now() == ICO_WINDOW_BORDERLESS) {
                ico_window_video_refit(s_window);
            }
            /* the refresh may differ: whether the cap equals it, and with
               it the present mode, is decided again */
            video_apply(1);
            if (s_disp.clock != 0) {
                /* the display clock on the new display's output, and the
                   lock started again on it (pace_locked) */
                disp_clock_stop();
                disp_clock_start();
                s_pres.lockN = 0;
            }
            s_pres.vblankGone = 0;
            break;
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
            video_apply(1);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            /* a drag sends one per size: print when the fullscreen
                   flag changed or a second has passed since the last line */
            static int s_sizeFs = -1;
            static Uint64 s_sizeAt;
            int fs = window_fullscreen() ? 1 : 0;
            Uint64 now = SDL_GetTicksNS();
            int print = fs != s_sizeFs || now - s_sizeAt >= 1000000000ull;
#if defined(__ANDROID__) || defined(ICO_IOS)
            print = 1; /* every change (no drags there) */
#endif
            if (print) {
                s_sizeFs = fs;
                s_sizeAt = now;
                fprintf(stderr, "window: %dx%d pixels, flags 0x%llx (fullscreen %s)\n",
                        e.window.data1, e.window.data2,
                        (unsigned long long)SDL_GetWindowFlags(s_window), fs ? "on" : "off");
            }
        }
            if (e.window.data1 > 0 && e.window.data2 > 0) {
                /* aspect "auto" and resolution "window" follow the
                   size, and the touch zones */
                window_pixel_size(e.window.data1, e.window.data2);
            } else {
                touch_layout_update(); /* the size and the safe area */
            }
            break;
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            touch_layout_update(); /* a notch or the system bars moved */
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            touch_layout_update(); /* another density: the controls' size */
            break;
        default:
            ico_input_sdl_event(&e);
            break;
        }
        /* the port's own handling of it (a size change remakes the
           swapchain, a key may write a dump) */
        if (SDL_GetTicksNS() - h0 > PUMP_WATCH_SLOW_NS) {
            char what[96];

            snprintf(what, sizeof(what), "handling of the event %s",
                     name != NULL ? name : "(another kind)");
            pump_slow(what, h0);
        }
    }
    /* the pump's parts, for the slow-step line: which part of a long pump
       took the time */
    s_pres.pumpEventsMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    s_pres.pumpEvents = events;
#ifdef ICO_IOS
    if (!quit) {
        wait_foreground();
    }
    if (s_quitLatched) {
        return 1;
    }
#endif
#ifdef __ANDROID__
    {
        /* the activity's density and fold reports (IcoActivity): folding,
           unfolding or bending the screen does not always resize the
           window, so a new report lays the touch controls out again */
        static int s_screenGen;
        const int gen = ico_android_screen(NULL);

        if (gen != s_screenGen) {
            s_screenGen = gen;
            touch_layout_update();
        }
    }
#endif
    first_pump_recheck();
    resume_recheck();
    report_picture_pump();
#if ICO_RC_TOOLS && defined(__ANDROID__)
    share_report_pump();
#endif
    /* the Settings menu's changes; forced when the
       renderer's output followed a swapchain rebuilt at another size, so
       aspect "auto" and resolution "window" follow it as on a resize */
    {
        const Uint64 t1 = SDL_GetTicksNS();

        video_apply(rd_output_followed(NULL, NULL) ? 1 : 0);
        s_pres.pumpSettingsMs = (double)(SDL_GetTicksNS() - t1) / 1e6;
        pump_slow("settings (video_apply)", t1);
    }
    /* a hard camera cut or stage change the game signalled during the
       step that just ran: the frame it recorded is not blended from the
       one before (the step closes the previous frame before the game's
       threads run, so the open frame is the one the cut belongs to) */
    if (ico_video_cut_serial() != s_pres.cutSerial) {
        s_pres.cutSerial = ico_video_cut_serial();
        rd_camera_cut();
    }
    {
        const Uint64 t1 = SDL_GetTicksNS();

        photo_pump();                /* photo mode's captures */
        sweep_pump();                /* Shift+F12's aspect sweep */
        set_capture(capture_mode()); /* play and photo mode */
        pointer_visibility();        /* the pointer in the menus */
        pump_slow("captures and the pointer", t1);
    }
    {
        const Uint64 t1 = SDL_GetTicksNS();

        ico_input_sdl_update(); /* the pads sampled, the rumble sent */
        s_pres.pumpInputMs = (double)(SDL_GetTicksNS() - t1) / 1e6;
        pump_slow("pads' reading and rumble (ico_input_sdl_update)", t1);
    }
    /* the popups' clock, at the video mode's rate (it follows a change of
       the mode); the presenter draws them on its overlay at each present
       (port/ui/ui_host.c) */
    ui_popup_set_vsync_hz(ico_host_vsync_hz());
    ui_host_vsync(ico_host_main_ticks());
    s_pres.pumpMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    if (s_pres.pumpMs > s_pres.pumpMaxMs) {
        s_pres.pumpMaxMs = s_pres.pumpMs;
    }
    return !quit;
}

/* --- the start-up screens, in the game's window ------------------------- */

typedef struct ProgressView {
    const char *title;
    const char *phase;
    int pct;
    int allowCancel;      /* the bar's "how to stop" line is shown */
    const char *notes[2]; /* lines under the bar, NULL for none */
} ProgressView;

/* the layout grid's 4:3 picture (port/ui/font.h): the title, the phase
   line, the bar, (Android) how to stop and the notes */
static void progress_overlay(const RdOverlayCtx *ctx, void *user)
{
    const ProgressView *v = user;
    /* GS colours: the text's 0x80 is full white, a rect's RGB is as given */
    static const uint8_t title[4] = {0x80, 0x7C, 0x70, 0x80};
    static const uint8_t body[4] = {0x66, 0x64, 0x5E, 0x80};
    static const uint8_t frame[4] = {96, 96, 96, 0x80};
    static const uint8_t track[4] = {24, 24, 24, 0x80};
    static const uint8_t fill[4] = {200, 180, 120, 0x80};
    char line[256];

    ui_begin_overlay(ctx);
    if (v->title != NULL) {
        ui_draw_text(UI_GRID_CX, 196.0f, 30.0f, title, v->title,
                     UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    }
    if (v->phase != NULL) {
        if (v->pct >= 0) {
            snprintf(line, sizeof(line), "%s: %d%%", v->phase, v->pct > 100 ? 100 : v->pct);
        } else {
            snprintf(line, sizeof(line), "%s", v->phase);
        }
        ui_draw_text(UI_GRID_CX, 232.0f, 20.0f, body, line, UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    }
    if (v->pct >= 0) {
        const float x0 = 120.0f, x1 = 520.0f, y0 = 250.0f, y1 = 266.0f;
        const int pct = v->pct > 100 ? 100 : v->pct;

        ui_draw_rect(x0, y0, x1, y1, frame);
        ui_draw_rect(x0 + 2.0f, y0 + 2.0f, x1 - 2.0f, y1 - 2.0f, track);
        ui_draw_rect(x0 + 2.0f, y0 + 2.0f, x0 + 2.0f + (x1 - x0 - 4.0f) * (float)pct / 100.0f,
                     y1 - 2.0f, fill);
    }
    {
        /* the lines under the bar (or under the phase line without one) */
        float y = v->pct >= 0 ? 300.0f : 272.0f;
        int i;

#ifdef __ANDROID__
        if (v->allowCancel) {
            ui_draw_text(UI_GRID_CX, y, 16.0f, body,
                         "Press Back to stop (you can start again later)",
                         UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
            y += 24.0f;
        }
#endif
        for (i = 0; i < 2; i++) {
            if (v->notes[i] != NULL) {
                ui_draw_text(UI_GRID_CX, y, 16.0f, body, v->notes[i],
                             UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
                y += 24.0f;
            }
        }
    }
    ui_end_overlay();
}

/* One event for a progress screen: a quit or close request sets *closing
   and *cancel, Back or Escape sets *cancel; the rest is handled as the
   game's pump would once the window loop is open (s_open). */
static void progress_event(const SDL_Event *e, int *cancel, int *closing)
{
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        *cancel = 1;
        *closing = 1;
        break;
    case SDL_EVENT_KEY_DOWN:
        /* Android's Back (SDL_HINT_ANDROID_TRAP_BACK_BUTTON), Escape */
        if ((e->key.key == SDLK_AC_BACK || e->key.key == SDLK_ESCAPE) && !e->key.repeat) {
            *cancel = 1;
        }
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (s_open && e->window.data1 > 0 && e->window.data2 > 0) {
            fprintf(stderr, "window: %dx%d pixels\n", e->window.data1, e->window.data2);
            window_pixel_size(e->window.data1, e->window.data2);
        }
        break;
    default:
        /* the pads plugged in at the start, and the rest: as the game's
           pump would pass them on */
        if (s_open) {
            ico_input_sdl_event(e);
        }
        break;
    }
}

/* Draws one progress screen and handles the events that came in.  Works
   before the game's window loop is open (s_open), as long as the renderer
   is up: the start-up screen is drawn while the graphics are prepared.
   Before that point only the quit and close requests and the key presses
   are taken from SDL's queue (SDL_PeepEvents by type); everything else,
   the window's size and fullscreen changes among it, stays queued for the
   window loop's first ico_window_pump, which handles it as usual.  Returns
   1 when the player asked to stop and allowCancel is set (or the device was
   lost).  A quit or close request is also kept (s_quitLatched) whatever
   allowCancel is, so a screen whose caller carries on still ends the run at
   the next ico_window_pump.  note0 and note1: lines under the bar (NULL:
   none). */
static int progress_present_notes(const char *title, const char *phase, int pct, int allowCancel,
                                  const char *note0, const char *note1)
{
    static int s_cancelLogged;
    SDL_Event e;
    int cancel = 0, closing = 0;
    void *prevUser = NULL, *prevTopUser = NULL;
    RdOverlayFn prev, prevTop;
    ProgressView v;

#ifdef _WIN32
    pump_joysticks(); /* SDL_PollEvent no longer does it (ico_window_open) */
#endif
    if (s_open) {
        while (pump_poll(&e)) {
            progress_event(&e, &cancel, &closing);
        }
    } else {
        static const Uint32 k_taken[] = {SDL_EVENT_QUIT, SDL_EVENT_WINDOW_CLOSE_REQUESTED,
                                         SDL_EVENT_KEY_DOWN};
        size_t i;

        {
            const Uint64 t0 = SDL_GetTicksNS();

            pump_watch_part(PART_POLL);
            ico_diag_os_wait_enter("the window's event pump");
            SDL_PumpEvents();
            ico_diag_os_wait_leave();
            pump_watch_part(NULL);
            pump_slow(PART_POLL, t0);
        }
        for (i = 0; i < sizeof(k_taken) / sizeof(k_taken[0]); i++) {
            while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, k_taken[i], k_taken[i]) == 1) {
                progress_event(&e, &cancel, &closing);
            }
        }
    }
#ifdef ICO_IOS
    wait_foreground();
    if (s_quitLatched) {
        return 1;
    }
#endif
    if (s_open) {
        first_pump_recheck();
        resume_recheck();
    }
    if (device_lost_quit()) {
        return 1;
    }
    if (closing && !s_quitLatched) {
        s_quitLatched = 1;
        if (!allowCancel) {
            /* this screen cannot stop: the pump quits after it */
            fprintf(stderr, "window: close asked during \"%s\"; quitting after it\n",
                    phase != NULL ? phase : "");
        }
    }
    if (!allowCancel) {
        cancel = 0;
    }
    if (cancel && !s_cancelLogged) {
        s_cancelLogged = 1;
        fprintf(stderr, "window: stop asked during \"%s\"\n", phase != NULL ? phase : "");
    }
    v.title = title;
    v.phase = phase;
    v.pct = pct;
    v.allowCancel = allowCancel;
    v.notes[0] = note0;
    v.notes[1] = note1;
    prev = rd_get_present_overlay(&prevUser);
    /* the touch controls' layer off too */
    prevTop = rd_get_present_overlay_top(&prevTopUser);
    rd_set_present_overlay(progress_overlay, &v);
    rd_set_present_overlay_top(NULL, NULL);
    rd_present_blank();
    rd_set_present_overlay(prev, prevUser);
    rd_set_present_overlay_top(prevTop, prevTopUser);
    return cancel;
}

int ico_window_progress(const char *title, const char *phase, int pct)
{
    return ico_window_progress_notes(title, phase, pct, NULL, NULL);
}

int ico_window_progress_notes(const char *title, const char *phase, int pct, const char *note0,
                              const char *note1)
{
    int stop;

    if (!s_open) {
        return 0;
    }
    stop = progress_present_notes(title, phase, pct, 1, note0, note1);
    /* a quit or close from an earlier screen that could not stop (the
       start-up graphics screen) stops this one too */
    return stop || s_quitLatched;
}

/* The start-up screen while the graphics are prepared (rd_set_pipeline_progress).
   With the graphics already prepared (the driver's saved cache, a start that
   follows another) this takes a few tens of milliseconds and nothing is
   drawn: the screen appears only when the wait is already noticeable, and
   then at most ten times a second.  The count moves while a batch compiles
   (the renderer calls this about every 50 ms from inside it), so the
   window's events are taken meanwhile; an estimate of the time left from
   the rate so far joins once there is a rate to go by.  Returns 1 when the
   game is closing (a quit or close request came in): the renderer stops the
   set after the batch in flight. */
#define PIPE_SCREEN_AFTER_MS 250.0
#define PIPE_SCREEN_EVERY_MS 100.0
/* the estimate from this much time and this many pipelines on */
#define PIPE_ESTIMATE_AFTER_S 3.0
#define PIPE_ESTIMATE_AFTER_DONE 8u

/* "About 2 minutes left" from the rate so far; NULL while too early */
static const char *pipe_estimate(char *out, size_t size, double elapsedS, uint32_t done,
                                 uint32_t total)
{
    double left;

    if (done < PIPE_ESTIMATE_AFTER_DONE || done >= total || elapsedS < PIPE_ESTIMATE_AFTER_S) {
        return NULL;
    }
    left = elapsedS * (double)(total - done) / (double)done;
    if (left < 10.0) {
        snprintf(out, size, "A few seconds left");
    } else if (left < 55.0) {
        /* rounded up to ten seconds */
        snprintf(out, size, "About %d seconds left", ((int)left + 9) / 10 * 10);
    } else if (left < 90.0) {
        snprintf(out, size, "About a minute left");
    } else {
        snprintf(out, size, "About %d minutes left", (int)(left / 60.0 + 0.5));
    }
    return out;
}

static int pipeline_progress(void *ctx, uint32_t done, uint32_t total)
{
    PipeScreen *p = ctx;
    const uint64_t now = SDL_GetTicksNS();
    const double elapsedMs = (double)(now - p->startNs) / 1e6;
    char phase[64], left[48];
    const char *estimate;
    const char *once = NULL;
    int stop;

    /* the heartbeat line says how far the set is (diag_host.h) */
    ico_diag_set_pipelines(done, done < total ? total : 0,
                           (unsigned)(rd_precreate_longest_ms() + 0.5f));
    /* a low-memory signal meanwhile into the activity file (issue 93) */
    ico_diag_startup_activity_poll();
    if (done == 0) {
        p->startNs = now;
        p->drawn = 0;
        return s_quitLatched;
    }
    if (elapsedMs < PIPE_SCREEN_AFTER_MS ||
        (p->drawn && (double)(now - p->lastNs) / 1e6 < PIPE_SCREEN_EVERY_MS)) {
        return s_quitLatched;
    }
    snprintf(phase, sizeof(phase), "Preparing graphics (%u of %u)", (unsigned)done,
             (unsigned)total);
    estimate = pipe_estimate(left, sizeof(left), elapsedMs / 1000.0, done, total);
#ifdef __ANDROID__
    /* the driver keeps what it prepared: the next starts take seconds */
    once = "This happens once after an update";
#endif
    p->drawn = 1;
    /* 1 from a screen that cannot stop: the device was lost */
    stop = progress_present_notes("Starting ICO", phase,
                                  total > 0 ? (int)((uint64_t)done * 100u / total) : -1, 0,
                                  estimate, once);
    /* the present itself took time: the next draw is measured from its end */
    p->lastNs = SDL_GetTicksNS();
    return stop || s_quitLatched;
}

/* ico-pc-perf.csv in the logs folder, beside ico-pc.log
   (ico_host_logs_dir), opened on the first record when [dev] perf_log is
   true */
static void perf_csv_open(void)
{
    char logs[ICO_PATH_MAX], path[ICO_PATH_MAX];

    s_perf.csvTried = 1;
#ifdef ICO_IOS
    return;
#endif
    if (!ico_config_get_bool("dev.perf_log", 0)) {
        return;
    }
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_path_join(path, sizeof(path), logs, "ico-pc-perf.csv") != 0) {
        fprintf(stderr, "window: perf_log: the path is too long\n");
        return;
    }
    s_perf.csv = ico_fopen(path, "w"); /* a UTF-8 path (host_fs.h) */
    if (s_perf.csv == NULL) {
        fprintf(stderr, "window: perf_log: cannot write %s\n", path);
        return;
    }
    fprintf(stderr, "window: perf_log: one line per replay in %s\n", path);
    fprintf(s_perf.csv,
            "replay,frame,interpolated,keep,presented,total_ms,interp_ms,wait_ms,acquire_ms,"
            "upload_ms,walk_ms,bind_ms,submit_ms,present_ms,readback_ms,fence_wait_ms,"
            "buffers_created,buffers_destroyed,textures_created,textures_destroyed,mem_allocs,"
            "mem_frees,bind_groups,pipeline_binds,bind_group_binds,draws,render_passes,barriers,"
            "copies,fence_waits,wait_idles,readbacks,texture_uploads,mesh_uploads,temp_clears,"
            "date_snapshots,exact_blends,pipeline_creates,upload_bytes,mesh_upload_bytes,gpu_valid,"
            "gpu_ms,"
            "gpu_upload_ms");
    for (int l = 0; l < GPU_LISTS; l++) {
        fprintf(s_perf.csv, ",gpu_list%d_ms", l);
    }
    fprintf(s_perf.csv, ",gpu_present_ms,start_ms,alpha,first_of_tick");
    for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
        /* the effect's name with '_' for ' ' */
        char name[32];
        snprintf(name, sizeof(name), "%s", rd_perf_post_name(p));
        for (char *c = name; *c; c++) {
            *c = *c == ' ' ? '_' : *c;
        }
        fprintf(s_perf.csv, ",gpu_%s_ms", name);
    }
    fprintf(s_perf.csv,
            ",gpu_effects_partial,gpu_prepass_ms,vu_pre_vertices,vu_pre_draws,vu_pre_fallbacks");
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        fprintf(s_perf.csv, ",walk_list%d_ms", l);
    }
    fprintf(s_perf.csv, ",upload_texture_bytes,upload_uniform_bytes,upload_interp_bytes,"
                        "upload_stream_bytes,passes,pass_timed,pass_count\n");
}

static void perf_csv_line(const RdPerfRecord *r)
{
    FILE *f = s_perf.csv;

    fprintf(f,
            "%u,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,"
            "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%u,%.3f,%.3f",
            r->replay, r->frame, r->interpolated, r->keep, r->presented, r->totalMs, r->interpMs,
            r->waitMs, r->acquireMs, r->uploadMs, r->walkMs, r->bindMs, r->submitMs, r->presentMs,
            r->readbackMs, r->fenceWaitMs, r->buffersCreated, r->buffersDestroyed,
            r->texturesCreated, r->texturesDestroyed, r->memoryAllocs, r->memoryFrees,
            r->bindGroups, r->pipelineBinds, r->bindGroupBinds, r->draws, r->renderPasses,
            r->barriers, r->copies, r->fenceWaits, r->waitIdles, r->readbacks, r->textureUploads,
            r->meshUploads, r->tempClears, r->dateSnapshots, r->exactBlends, r->pipelineCreates,
            (unsigned long long)r->uploadBytes, (unsigned long long)r->meshUploadBytes, r->gpuValid,
            r->gpuMs, r->gpuUploadMs);
    for (int l = 0; l < GPU_LISTS; l++) {
        fprintf(f, ",%.3f", r->gpuListMs[l]);
    }
    fprintf(f, ",%.3f,%.3f,%.4f,%u", r->gpuPresentMs, r->startMs, (double)r->alpha, r->firstOfTick);
    for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
        fprintf(f, ",%.3f", r->gpuPostMs[p]);
    }
    fprintf(f, ",%u,%.3f,%u,%u,%u", r->gpuPostPartial, r->gpuPrepassMs, r->vuPreVertices,
            r->vuPreDraws, r->vuPreFallbacks);
    unsigned passes = 0;
    for (int k = 0; k < RD_PERF_PASS_KIND_COUNT; k++) {
        passes += r->passKinds[k];
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        fprintf(f, ",%.3f", r->walkListMs[l]);
    }
    fprintf(f, ",%llu,%llu,%llu,%llu,%u,%u,%u\n", (unsigned long long)r->uploadTextureBytes,
            (unsigned long long)r->uploadUniformBytes, (unsigned long long)r->uploadInterpBytes,
            (unsigned long long)r->uploadStreamBytes, passes, r->passTimed, r->passCount);
}

/* ico-pc-passes.csv beside ico-pc-perf.csv, opened on the first timed
   replay when [dev] perf_passes is true (rd_perf_passes_mode): one line per
   piece of every replay's GPU work (rd.h RdPerfPass) */
static void perf_pass_csv_open(void)
{
    char logs[ICO_PATH_MAX], path[ICO_PATH_MAX], head[160];

    s_perf.passCsvTried = 1;
    if (!rd_perf_passes_mode()) {
        return;
    }
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_path_join(path, sizeof(path), logs, "ico-pc-passes.csv") != 0) {
        fprintf(stderr, "window: perf_passes: the path is too long\n");
        return;
    }
    s_perf.passCsv = ico_fopen(path, "w"); /* a UTF-8 path (host_fs.h) */
    if (s_perf.passCsv == NULL) {
        fprintf(stderr, "window: perf_passes: cannot write %s\n", path);
        return;
    }
    fprintf(stderr, "window: perf_passes: one line per pass of every replay in %s\n", path);
    rd_perf_pass_csv_header(head, sizeof(head));
    fputs(head, s_perf.passCsv);
}

/* A timed replay's pieces into the block's top passes and the CSV */
static void perf_passes_add(const RdPerfRecord *r)
{
    const RdPerfPass *p;
    const uint32_t n = rd_perf_passes_of(r->replay, &p);

    if (n == 0) {
        return;
    }
    rd_perf_pass_agg_add(&s_perf.passAgg, p, n);
    if (!s_perf.passCsvTried) {
        perf_pass_csv_open();
    }
    if (s_perf.passCsv != NULL) {
        char line[192];

        for (uint32_t i = 0; i < n; i++) {
            rd_perf_pass_csv_line(r->replay, r->frame, i, &p[i], line, sizeof(line));
            fputs(line, s_perf.passCsv);
        }
    }
}

/* A presented replay's cost for resolution "auto": its GPU
   time without the uploads (the part the scene's size changes) when the
   backend has timestamps, else in mailbox mode its CPU time without the
   acquire and the present (which wait for the display); under FIFO without
   timestamps the CPU time is the display's pace, not the scene's: no
   sample.  A gap of half a second (a movie, a pause) starts a new window. */
static void auto_res_sample(const RdPerfRecord *r)
{
    const Uint64 now = SDL_GetTicksNS();
    double ms;

    if (!s_autoRes.active || !r->presented) {
        return;
    }
    if (r->gpuValid) {
        ms = r->gpuMs - r->gpuUploadMs;
    } else if (rhi_present_mailbox()) {
        ms = r->totalMs - r->acquireMs - r->presentMs;
    } else {
        return;
    }
    if (s_autoRes.samples.count && now - s_autoRes.samples.lastNs > 500000000ull) {
        pace_samples_reset(&s_autoRes.samples, now);
    }
    pace_samples_add(&s_autoRes.samples, now, ms > 0.0 ? (Uint64)(ms * 1e6) : 0);
}

/* A replay over a refresh (it cannot fit a slot): where its time went,
   with its own counters; at most SLOW_REPLAY_LINES a block (the block's
   first line counts them all), and the block's longest at its end when the
   cap kept it out (slow_replay_longest).  The records arrive two replays
   late (rd_perf.c), so the vsync is the one the record was drained at. */
static void slow_replay_line(const RdPerfRecord *r, unsigned vsync, const char *note)
{
    char gpu[32] = "no GPU timestamps";

    if (r->gpuValid) {
        snprintf(gpu, sizeof(gpu), "GPU %.1f ms", r->gpuMs);
    }
    fprintf(stderr,
            "window: slow replay %.1f ms (replay %u, frame %u, alpha %.2f, about vsync %u): interp "
            "%.1f, wait %.1f (fence %.1f), acquire %.1f, upload %.1f, walk %.1f, bind %.1f, "
            "submit %.1f, present %.1f; %u textures and %u meshes (%u KB) uploaded (%u KB in "
            "all), %u pipelines created, %u allocations, %u buffers and %u textures created, %u "
            "DATE snapshots; %s%s\n",
            r->totalMs, r->replay, r->frame, (double)r->alpha, vsync, r->interpMs, r->waitMs,
            r->fenceWaitMs, r->acquireMs, r->uploadMs, r->walkMs, r->bindMs, r->submitMs,
            r->presentMs, r->textureUploads, r->meshUploads, (unsigned)(r->meshUploadBytes / 1024u),
            (unsigned)(r->uploadBytes / 1024u), r->pipelineCreates, r->memoryAllocs,
            r->buffersCreated, r->texturesCreated, r->dateSnapshots, gpu, note);
}

static void slow_replay(const RdPerfRecord *r)
{
    const int longest = r->totalMs > s_perf.longest.totalMs;

    if (longest) {
        s_perf.longest = *r;
        s_perf.longestVsync = ico_host_vsync_count();
        s_perf.longestShown = 0;
    }
    if (s_perf.slowLogged >= SLOW_REPLAY_LINES) {
        return;
    }
    s_perf.slowLogged++;
    if (longest) {
        s_perf.longestShown = 1;
    }
    slow_replay_line(r, ico_host_vsync_count(), "");
}

/* At the block's end: its longest replay over a refresh, when the cap of
   SLOW_REPLAY_LINES kept its line out */
static void slow_replay_longest(void)
{
    if (s_perf.longest.totalMs > 0.0 && !s_perf.longestShown) {
        char note[64];

        snprintf(note, sizeof(note), "; the block's longest, past its %d lines", SLOW_REPLAY_LINES);
        slow_replay_line(&s_perf.longest, s_perf.longestVsync, note);
        s_perf.longestShown = 1;
    }
}

/* The finished records into the block's sums and the CSV */
static void perf_drain(void)
{
    RdPerfRecord r;
    double refreshMs = -1.0; /* the slow-replay threshold, read at the first record */

    while (rd_perf_pop(&r)) {
        auto_res_sample(&r);
        if (r.presented && r.gpuValid) {
            /* the present allowance's GPU gate: a presented replay's GPU
               time (two replays late), the median of the last 8 */
            s_pres.gpuNs = pace_hist_add(&s_pres.gpuHist, (Uint64)(r.gpuMs * 1e6));
        }
#ifdef ICO_IOS
        continue; /* Keep automatic resolution's samples without benchmark output. */
#endif
        if (!s_perf.csvTried) {
            perf_csv_open();
        }
        if (s_perf.csv != NULL) {
            perf_csv_line(&r);
        }
        if (r.passTimed) {
            perf_passes_add(&r);
        }
        for (int k = 0; k < RD_PERF_PASS_KIND_COUNT; k++) {
            s_perf.passKinds[k] += r.passKinds[k];
        }
        for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
            s_perf.passEffects[p] += r.passEffects[p];
        }
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            s_perf.walkList[l] += r.walkListMs[l];
        }
        s_perf.texBytes += (double)r.uploadTextureBytes;
        s_perf.uniformBytes += (double)r.uploadUniformBytes;
        s_perf.interpBytes += (double)r.uploadInterpBytes;
        s_perf.streamBytes += (double)r.uploadStreamBytes;
        {
            const double ph[PERF_PH_COUNT] = {
                [PERF_PH_INTERP] = r.interpMs,     [PERF_PH_WAIT] = r.waitMs,
                [PERF_PH_ACQUIRE] = r.acquireMs,   [PERF_PH_UPLOAD] = r.uploadMs,
                [PERF_PH_WALK] = r.walkMs,         [PERF_PH_BIND] = r.bindMs,
                [PERF_PH_SUBMIT] = r.submitMs,     [PERF_PH_PRESENT] = r.presentMs,
                [PERF_PH_READBACK] = r.readbackMs, [PERF_PH_FENCE] = r.fenceWaitMs};

            if (refreshMs < 0.0) {
                refreshMs = (double)display_refresh_ns(0) / 1e6; /* 0: none, never over */
            }
            if (perf_block_add(&s_perf.ph, ph, r.totalMs, refreshMs)) {
                slow_replay(&r);
            }
        }
        if (r.gpuValid) {
            s_perf.gpuN++;
            s_perf.gpu += r.gpuMs;
            s_perf.maxGpu = r.gpuMs > s_perf.maxGpu ? r.gpuMs : s_perf.maxGpu;
            s_perf.gpuUpload += r.gpuUploadMs;
            s_perf.gpuPresent += r.gpuPresentMs;
            s_perf.gpuPrepass += r.gpuPrepassMs;
            for (int l = 0; l < GPU_LISTS; l++) {
                s_perf.gpuList[l] += r.gpuListMs[l];
            }
            for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
                s_perf.gpuPost[p] += r.gpuPostMs[p];
            }
            s_perf.gpuPostPartial += r.gpuPostPartial;
        }
        s_perf.draws += r.draws;
        s_perf.passes += r.renderPasses;
        s_perf.pipeBinds += r.pipelineBinds;
        s_perf.groupBinds += r.bindGroupBinds;
        s_perf.groups += r.bindGroups;
        s_perf.barriers += r.barriers;
        s_perf.copies += r.copies;
        s_perf.bytes += r.uploadBytes;
        s_perf.meshBytes += r.meshUploadBytes;
        s_perf.texUploads += r.textureUploads;
        s_perf.meshUploads += r.meshUploads;
        s_perf.dateSnaps += r.dateSnapshots;
        s_perf.blockOtherScale += r.blockOtherScaleReads;
        s_perf.exact += r.exactBlends;
        s_perf.dstDraws += r.dstDraws;
        s_perf.storagePasses += r.storagePasses;
        s_perf.dstSwitches += r.dstSwitches;
        s_perf.glowPasses += r.glowPasses;
        s_perf.snapshots += r.snapshots;
        s_perf.snapSwaps += r.snapSwaps;
        s_perf.a1xDraws += r.a1xDraws;
        s_perf.vuPreVerts += r.vuPreVertices;
        s_perf.vuPreDraws += r.vuPreDraws;
        s_perf.vuPreFallbacks += r.vuPreFallbacks;
        s_perf.alphaOnePass += r.alphaOnePass;
        s_perf.alphaOpaque += r.alphaOpaque;
        s_perf.fxConstant += r.fxConstant;
        s_perf.fxAdditive += r.fxAdditive;
        s_perf.fxLocal += r.fxLocal;
        s_perf.vuNoPlanes += r.vuNoPlanes;
        s_perf.pipeCreates += r.pipelineCreates;
        s_perf.msaaResolves += r.msaaResolves;
        if (r.msaaSamples > s_perf.msaaSamples) {
            s_perf.msaaSamples = r.msaaSamples;
        }
        s_perf.bufCreated += r.buffersCreated;
        s_perf.bufDestroyed += r.buffersDestroyed;
        s_perf.texCreated += r.texturesCreated;
        s_perf.texDestroyed += r.texturesDestroyed;
        s_perf.allocs += r.memoryAllocs;
        s_perf.fenceWaits += r.fenceWaits;
        s_perf.waitIdles += r.waitIdles;
        s_perf.readbacks += r.readbacks;
    }
    if (s_perf.csv != NULL) {
        fflush(s_perf.csv);
    }
    if (s_perf.passCsv != NULL) {
        fflush(s_perf.passCsv);
    }
}

/* A new block's sums (the CSVs stay open) */
static void perf_reset(void)
{
    FILE *csv = s_perf.csv, *passCsv = s_perf.passCsv;
    const int tried = s_perf.csvTried, passTried = s_perf.passCsvTried;

    memset(&s_perf, 0, sizeof(s_perf));
    s_perf.csv = csv;
    s_perf.csvTried = tried;
    s_perf.passCsv = passCsv;
    s_perf.passCsvTried = passTried;
    s_pres.stepSumMs = s_pres.stepMaxMs = 0.0;
    s_pres.stepCount = 0;
    rd_vu_card_counts(&s_pres.cardStrips, &s_pres.cardTris, &s_pres.cardMs);
}

/* The block's second line: where a replay's time went, on average */
static void perf_log(void)
{
#ifdef ICO_IOS
    perf_reset();
    return;
#endif
    const PerfBlock *b = &s_perf.ph;
    const double n = b->n ? (double)b->n : 1.0;
    const double g = s_perf.gpuN ? (double)s_perf.gpuN : 1.0;
    char gpu[160] = "no GPU timestamps";
    char longest[160];

    perf_block_max_text(b, longest, sizeof(longest));

    if (s_perf.gpuN) {
        double scene = 0.0;

        for (int l = 0; l < 11; l++) {
            scene += s_perf.gpuList[l];
        }
        snprintf(gpu, sizeof(gpu),
                 "GPU %.2f ms (max %.2f): uploads %.2f, lists 0-10 %.2f, 11-12 %.2f, present %.2f",
                 s_perf.gpu / g, s_perf.maxGpu, s_perf.gpuUpload / g, scene / g,
                 (s_perf.gpuList[11] + s_perf.gpuList[12]) / g, s_perf.gpuPresent / g);
    }
    /* the region rule's card verdicts the steps decided on the CPU
       (rd_mesh.c), a step's average */
    uint64_t cardStrips, cardTris;
    double cardMs;
    rd_vu_card_counts(&cardStrips, &cardTris, &cardMs);
    const double steps = s_pres.stepCount ? (double)s_pres.stepCount : 1.0;
    fprintf(stderr,
            "window: %u replays: CPU %.2f ms (max %.2f): interp %.2f, wait %.2f, acquire %.2f, "
            "upload %.2f, walk %.2f, bind %.2f, submit %.2f, present %.2f, readback %.2f "
            "(fence waits %.2f); longest: %s; %s; simulation step %.2f ms (max %.2f); strip "
            "verdicts %.1f strips, %.1f triangles, %.3f ms a step\n",
            b->n, b->total / n, b->totalMax, b->sum[PERF_PH_INTERP] / n, b->sum[PERF_PH_WAIT] / n,
            b->sum[PERF_PH_ACQUIRE] / n, b->sum[PERF_PH_UPLOAD] / n, b->sum[PERF_PH_WALK] / n,
            b->sum[PERF_PH_BIND] / n, b->sum[PERF_PH_SUBMIT] / n, b->sum[PERF_PH_PRESENT] / n,
            b->sum[PERF_PH_READBACK] / n, b->sum[PERF_PH_FENCE] / n, longest, gpu,
            s_pres.stepCount ? s_pres.stepSumMs / s_pres.stepCount : 0.0, s_pres.stepMaxMs,
            (double)(cardStrips - s_pres.cardStrips) / steps,
            (double)(cardTris - s_pres.cardTris) / steps, (cardMs - s_pres.cardMs) / steps);
    if (s_perf.gpuN) {
        /* the GPU time per replay by list and by picture effect (each
           effect's time is inside its list's, the CRT filter's inside the
           present's) */
        char lists[160], posts[256], partial[64] = "";
        int at = 0;

        for (int l = 0; l < GPU_LISTS && at < (int)sizeof(lists); l++) {
            at += snprintf(lists + at, sizeof(lists) - (size_t)at, "%s%.2f", l ? " " : "",
                           s_perf.gpuList[l] / g);
        }
        at = 0;
        for (int p = 0; p < RD_PERF_POST_COUNT && at < (int)sizeof(posts); p++) {
            at += snprintf(posts + at, sizeof(posts) - (size_t)at, "%s%s %.2f", p ? ", " : "",
                           rd_perf_post_name(p), s_perf.gpuPost[p] / g);
        }
        if (s_perf.gpuPostPartial) {
            snprintf(partial, sizeof(partial), " (not all timed apart in %u replays)",
                     s_perf.gpuPostPartial);
        }
        fprintf(stderr,
                "window: GPU ms per replay: lists 0-12 %s; effects: %s%s; present %.2f; VU "
                "pre-pass %.2f\n",
                lists, posts, partial, s_perf.gpuPresent / g, s_perf.gpuPrepass / g);
    }
    if (s_perf.passAgg.replays) {
        /* the timed replays' most expensive passes, each timed between
           stamps outside the render passes (rd_perf.c, the passes) */
        char top[1024];

        rd_perf_pass_agg_text(&s_perf.passAgg, 5, top, sizeof(top));
        fprintf(stderr, "window: GPU top passes: %s\n", top);
    }
    {
        /* what a replay begins (the passes' count, for the tilers' fixed
           cost of a pass), the walk's CPU time by list and what the
           uploads carry */
        char kinds[512], walk[192];
        int at = 0;

        rd_perf_pass_kinds_text(s_perf.passKinds, s_perf.passEffects, n, kinds, sizeof(kinds));
        for (int l = 0; l < RD_LIST_COUNT && at < (int)sizeof(walk); l++) {
            at += snprintf(walk + at, sizeof(walk) - (size_t)at, "%s%.2f", l ? " " : "",
                           s_perf.walkList[l] / n);
        }
        const double other = s_perf.bytes - s_perf.texBytes - s_perf.meshBytes -
                             s_perf.uniformBytes - s_perf.streamBytes;
        fprintf(stderr, "window: passes per replay %s\n", kinds);
        fprintf(stderr,
                "window: CPU walk ms per replay by list 0-12: %s; uploaded KB per replay: "
                "textures %.0f, meshes %.0f (interpolated %.0f), uniform blocks %.0f, VU streams "
                "%.0f, other %.0f\n",
                walk, s_perf.texBytes / n / 1024.0, s_perf.meshBytes / n / 1024.0,
                s_perf.interpBytes / n / 1024.0, s_perf.uniformBytes / n / 1024.0,
                s_perf.streamBytes / n / 1024.0, (other > 0.0 ? other : 0.0) / n / 1024.0);
    }
    fprintf(stderr,
            "window: per replay %.0f draws, %.0f passes, %.0f pipeline and %.0f bind group binds, "
            "%.0f bind groups, %.0f barriers, %.0f copies, %.0f KB uploaded (%.0f KB meshes), "
            "%.1f textures and %.1f meshes uploaded, %.1f DATE snapshots, %.1f exact blends, "
            "%.0f destination-read draws, %.0f storage passes, %.0f pass switches for the exact "
            "blend, glow %.0f passes, %.1f snapshots copied and %.1f swapped, %.1f draws "
            "resampled above 1x, VU pre-pass %.0f vertices in %.0f draws (%.1f draws the old "
            "way); in the block %llu buffers and %llu textures created, %llu and %llu "
            "destroyed, %llu memory allocations, %llu pipelines created, %llu fence waits, "
            "%llu wait-idles, %llu readbacks\n",
            s_perf.draws / n, s_perf.passes / n, s_perf.pipeBinds / n, s_perf.groupBinds / n,
            s_perf.groups / n, s_perf.barriers / n, s_perf.copies / n, s_perf.bytes / n / 1024.0,
            s_perf.meshBytes / n / 1024.0, s_perf.texUploads / n, s_perf.meshUploads / n,
            s_perf.dateSnaps / n, s_perf.exact / n, s_perf.dstDraws / n, s_perf.storagePasses / n,
            s_perf.dstSwitches / n, s_perf.glowPasses / n, s_perf.snapshots / n,
            s_perf.snapSwaps / n, s_perf.a1xDraws / n, s_perf.vuPreVerts / n, s_perf.vuPreDraws / n,
            s_perf.vuPreFallbacks / n, (unsigned long long)s_perf.bufCreated,
            (unsigned long long)s_perf.texCreated, (unsigned long long)s_perf.bufDestroyed,
            (unsigned long long)s_perf.texDestroyed, (unsigned long long)s_perf.allocs,
            (unsigned long long)s_perf.pipeCreates, (unsigned long long)s_perf.fenceWaits,
            (unsigned long long)s_perf.waitIdles, (unsigned long long)s_perf.readbacks);
    /* the pixel diet (rd_alpha_plan.c, rd_fx_plan.c, rd_pixel_plan.c; the
       flags are the rd: profile line's) */
    fprintf(stderr,
            "window: pixel diet per replay: alpha plan %.1f one-pass, %.1f opaque; fx plan %.1f "
            "constant, %.1f additive, %.1f local; vu no-planes %.1f\n",
            s_perf.alphaOnePass / n, s_perf.alphaOpaque / n, s_perf.fxConstant / n,
            s_perf.fxAdditive / n, s_perf.fxLocal / n, s_perf.vuNoPlanes / n);
    if (s_perf.msaaSamples) {
        /* the anti-aliasing's cost: its resolves' GPU time is the effects'
           "MSAA resolve" above */
        fprintf(stderr, "window: msaa: %u samples, resolves %.1f per replay\n", s_perf.msaaSamples,
                (double)s_perf.msaaResolves / n);
    }
    if (s_perf.blockOtherScale) {
        /* the puddles', the pools' and the queen barrier's render-to-texture
           blocks read at their own size, not the scene's (Water reflections:
           Original, the other-size rule; rd_core.c rd__target_scale_of) */
        fprintf(stderr,
                "window: water: %.1f draws per replay read a reflection block drawn at a scale "
                "other than the scene's (%llu in the block)\n",
                (double)s_perf.blockOtherScale / n, (unsigned long long)s_perf.blockOtherScale);
    }
    {
        /* the graphics allocations against the device's limit (a
           phone GPU allows 4096) and the process's memory (a phone ends a
           process that holds too much without a word) */
        RhiStats st;
        long rss = -1, peak = -1, avail = -1, total = -1;

        char gfx[160] = "graphics allocations not counted";
        char dev[96] = "; device memory unknown";

        rhi_get_stats(&st);
        ico_diag_process_memory(&rss, &peak);
        /* and the device's (issue 128: what the system has left decides
           when it ends the app, not the app's own size) */
        if (ico_diag_device_memory(&avail, &total) == 0) {
            snprintf(dev, sizeof(dev), "; device %ld MB available of %ld MB", avail / 1024,
                     total / 1024);
        }
        if (st.memoryLimit != 0) { /* Vulkan counts them */
            snprintf(gfx, sizeof(gfx),
                     "%llu graphics allocations alive (most %llu; the device allows %llu), "
                     "%.0f MB (most %.0f MB)",
                     (unsigned long long)st.memoryLive, (unsigned long long)st.memoryPeak,
                     (unsigned long long)st.memoryLimit, (double)st.memoryLiveBytes / 1048576.0,
                     (double)st.memoryPeakBytes / 1048576.0);
        }
        /* the RHI's pipeline pool (both backends): the most alive at once
           against its size, and the creations that failed (a full pool's
           and the driver's), so a log shows a pool that ran out */
        char pipes[160];
        snprintf(pipes, sizeof(pipes),
                 "; pipelines %llu alive (most %llu of the pool's %llu), %llu creations failed "
                 "(%llu for a full pool)",
                 (unsigned long long)st.pipelinesLive, (unsigned long long)st.pipelinesPeak,
                 (unsigned long long)st.pipelinePoolSize,
                 (unsigned long long)(st.pipelineFailures + st.pipelinePoolFull),
                 (unsigned long long)st.pipelinePoolFull);
        if (rss >= 0) {
            fprintf(stderr, "window: memory: %s; the process holds %ld MB (most %ld MB)%s%s\n", gfx,
                    rss / 1024, (peak >= 0 ? peak : rss) / 1024, dev, pipes);
        } else {
            fprintf(stderr, "window: memory: %s%s\n", gfx, pipes);
        }
    }
    perf_reset();
}

/* The display clock stopped (the lock left, the window closing) */
static void disp_clock_stop(void)
{
    if (s_disp.clock != 0) {
        ico_display_clock_stop();
        s_disp.clock = 0;
    }
}

/* The display clock started on the window's output (nothing when it
   runs); the log names the output the first few times */
static void disp_clock_start(void)
{
    static unsigned s_lines;
    char name[64];

    if (s_disp.clock == 1) {
        return;
    }
    s_disp.clock = ico_display_clock_start(s_window) ? 1 : 0;
    memset(&s_disp.vb, 0, sizeof(s_disp.vb));
    if (s_disp.clock && s_lines++ < 8) {
        fprintf(stderr, "window: display clock: the vblanks of %s (measured for the log)\n",
                ico_display_clock_name(name, sizeof(name)));
    }
}

/* A new lock: the display's counts start again, the clock started */
static void disp_lock_start(void)
{
    const int clock = s_disp.clock;

    memset(&s_disp, 0, sizeof(s_disp));
    s_disp.clock = clock < 0 ? 0 : clock; /* one that stopped itself is tried again */
    disp_clock_start();
}

/* The clock's next vblank, counted for the display line: 1 taken (*ns),
   0 none yet, -1 the clock is not running (it stopped itself: stopped
   here, logged) */
static int disp_take(Uint64 refresh, uint64_t *ns)
{
    int r;

    if (s_disp.clock != 1) {
        return -1;
    }
    r = ico_display_clock_next(ns);
    if (r == 1) {
        s_disp.vblanks++;
        pace_intervals_add(&s_disp.vb, *ns, refresh);
    } else if (r < 0) {
        static unsigned s_lines;

        ico_display_clock_stop();
        s_disp.clock = -1;
        if (s_lines++ < 8) {
            fprintf(stderr, "window: display clock: stopped (a wait for a vblank failed or "
                            "returned at once)\n");
        }
    }
    return r;
}

/* Every locked vsync: the clock's vblanks taken for the display line when
   the lock does not take them (lock_io_vblank: one taker) */
static void disp_vsync(Uint64 refresh)
{
    uint64_t ns;

    if (s_pres.lock.clockKind == PACE_CLOCK_KIND_VBLANK && s_pres.lock.clock == PACE_CLOCK_ON) {
        return;
    }
    while (disp_take(refresh, &ns) == 1) {}
}

/* A present the lock made that returned at s_pres.lastReturn and waited
   s_pres.lockWaitNs (the lock's own rule: a quarter of a refresh) */
static void disp_present(Uint64 refresh)
{
    uint64_t count, refreshes, syncRefresh, syncNs;

    pace_intervals_add(&s_disp.ret, s_pres.lastReturn, refresh);
    if (s_pres.lockWaitNs * 4 >= refresh) {
        s_disp.waited++;
        if (s_pres.lockWaitNs > s_disp.waitedMaxNs) {
            s_disp.waitedMaxNs = s_pres.lockWaitNs;
        }
    }
    if (rhi_present_statistics(&count, &refreshes, &syncRefresh, &syncNs)) {
        pace_shown_add(&s_disp.shown, count, refreshes);
        s_disp.statsSeen = 1;
    }
}

/* The block's display line (under the lock) */
static void disp_log(double sec, Uint64 reported)
{
    const PaceIntervals *v = &s_disp.vb, *p = &s_disp.ret;
    const double rep = (double)reported;
    char vb[256], dxgi[96] = "";

    if (s_disp.clock == 1 && v->n > 0) {
        const double mean = pace_intervals_mean(v);

        snprintf(vb, sizeof(vb),
                 "vblanks %u in %.1f s, %.4f ms apart (min %.3f, max %.3f: %.3f Hz), reported "
                 "%.4f ms (%.3f Hz): %+.0f ppm, %u wake-ups missed",
                 s_disp.vblanks, sec, mean / 1e6, (double)v->min / 1e6, (double)v->max / 1e6,
                 1e9 / mean, rep / 1e6, rep > 0.0 ? 1e9 / rep : 0.0, pace_ppm(mean, rep),
                 v->n + 2u * v->missed - v->intervals);
    } else {
        snprintf(vb, sizeof(vb), "%s",
                 s_disp.clock == 1    ? "display clock on, no vblanks yet"
                 : s_disp.clock == -1 ? "display clock stopped"
                                      : "no display clock");
    }
    if (s_disp.statsSeen) {
        snprintf(dxgi, sizeof(dxgi), "; DXGI: %u pictures shown over %u refreshes, %u repeated",
                 s_disp.shown.shown, s_disp.shown.refreshes, s_disp.shown.repeats);
    }
    fprintf(stderr,
            "window: display: %s; presents returned %.3f ms apart (min %.3f, max %.3f), %u a "
            "refresh late, %u waited for the display (longest %.1f ms)%s\n",
            vb, pace_intervals_mean(p) / 1e6, (double)p->min / 1e6, (double)p->max / 1e6, p->late,
            s_disp.waited, (double)s_disp.waitedMaxNs / 1e6, dxgi);
}

/* A new block of the display's counts (the times last seen kept) */
static void disp_block_reset(void)
{
    pace_intervals_reset(&s_disp.vb);
    pace_intervals_reset(&s_disp.ret);
    s_disp.vblanks = s_disp.waited = 0;
    s_disp.waitedMaxNs = 0;
    s_disp.shown.shown = s_disp.shown.refreshes = s_disp.shown.repeats = 0;
    s_disp.statsSeen = 0;
}

/* Presents and frames per second, every 10 s of real time, in both
   presentation modes. "game frames" counts the frames shown (closed
   frames the pace saw); "frame numbers" also counts the ones the game
   dropped unshown (rd_discard_frame: fbKeep screens, gsb_UpdateGSSystem(1)),
   so a gap between the two is not a slowdown. The simulation's own rate
   is the vsyncs against the block's real time, and any lag the pacer had
   to drop (resyncs) says the step plus the presents took longer than real
   time; the longest replay names the frame that did. */
static void pace_log(Uint64 now)
{
    if (s_pres.statAt == 0) {
        uint32_t n;

        s_pres.statAt = now;
        s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
        s_pres.statDropped = 0;
        s_pres.statSkipLate = s_pres.statSkipSlow = 0;
        s_pres.statFrameNo = rd_frame_number();
        s_pres.statMovie = rd_video_presents(&s_pres.statMovieFail);
        rd_replay_time_max(1, &n);
        perf_drain();
        perf_reset();
        return;
    }
    if (now - s_pres.statAt < ico_stats_period_ns(now, s_pres.fastUntil)) {
        return;
    }
    perf_drain(); /* the block's last records, for its replays over a refresh */
    slow_replay_longest();
    const double sec = (double)(now - s_pres.statAt) / 1e9;
    uint32_t replays = 0;
    const double maxMs = rd_replay_time_max(1, &replays);
    const uint32_t fn = rd_frame_number();
    uint32_t movieFail;
    const uint32_t movie = rd_video_presents(&movieFail);
    char movieFailed[32] = "";

    if (!rd_interpolation_active()) {
        s_pres.statPresents = replays; /* one present per replay */
    }
    /* what the presents went to: the frame rate option, the
       swapchain's present mode, the display's refresh and the window as it
       is (the only record of them a player's log has) */
    char fr[16];
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
    int pw = 0, ph = 0;

    SDL_GetWindowSizeInPixels(s_window, &pw, &ph);
    if (movieFail != s_pres.statMovieFail) {
        snprintf(movieFailed, sizeof(movieFailed), " (%u failed)",
                 movieFail - s_pres.statMovieFail);
    }
    /* the vsync lock's state, and the audio's rate: under the lock the
       simulation runs at the display's rate over n (0.1 % fast on a 60.00
       Hz screen), and the audio plays the samples that much faster, so its
       queue neither grows nor runs dry; 1 otherwise */
    char lock[416]; /* timing (160) + the lock line (gcc -O2 counts the worst case) */
    double ratio = 1.0;

    if (s_pres.lockN > 0) {
        const PaceLock *L = &s_pres.lock;
        /* 0: the block is too short to measure (F11's one-second lines):
           the last ratio stays */
        const double r = pace_lock_rate_ratio(&s_pres.lock);
        char timing[160];

        ratio = r > 0.0 ? r : (s_pres.audioRatio > 0.0 ? s_pres.audioRatio : 1.0);
        if (L->clockKind == PACE_CLOCK_KIND_SHOWN && L->clock == PACE_CLOCK_ON) {
            snprintf(timing, sizeof(timing),
                     "display timing on: %u presents timed, phase nudge %+.3f ms, %u late, %u "
                     "early",
                     L->seen - s_pres.lockSeenAt, (L->nudgeSum - s_pres.lockNudgeAt) / 1e6,
                     L->late - s_pres.lockLateAt, L->early - s_pres.lockEarlyAt);
        } else if (L->clockKind == PACE_CLOCK_KIND_SHOWN) {
            snprintf(timing, sizeof(timing), "display timing %s",
                     L->clock == PACE_CLOCK_LEFT_ERROR ? "stopped"
                                                       : "left (its refresh was off the report)");
        } else if (L->clockKind == PACE_CLOCK_KIND_VBLANK && L->clock == PACE_CLOCK_ON) {
            char name[64];

            ico_display_clock_name(name, sizeof(name));
            if (L->measuredNs > 0.0) {
                /* the slot against the report (+: longer), and the step's
                   offset after the last vblank (+: after it) */
                snprintf(timing, sizeof(timing),
                         "display clock vblank (output %s, measured %.4f ms, slot %+.0f ppm, "
                         "phase %+.2f ms)",
                         name, L->measuredNs / 1e6, pace_ppm(L->slotNs, (double)L->refreshNs),
                         L->vbPhaseNs / 1e6);
            } else {
                snprintf(timing, sizeof(timing), "display clock vblank (output %s, measuring)",
                         name);
            }
        } else if (L->clockKind == PACE_CLOCK_KIND_VBLANK) {
            snprintf(timing, sizeof(timing), "display clock %s",
                     L->clock == PACE_CLOCK_LEFT_ERROR ? "stopped"
                                                       : "left (its interval was off the report)");
        } else {
            snprintf(timing, sizeof(timing), "display clock off");
        }
        snprintf(lock, sizeof(lock),
                 "lock held (grid %.4f ms, %u present%s a step, %s, audio rate x%.4f, %s, %u "
                 "waited for the display, %u skipped)",
                 L->slotNs * (double)L->n / 1e6, L->n, L->n == 1 ? "" : "s",
                 L->mailbox ? "mailbox" : "fifo", ratio, timing, L->waited - s_pres.lockWaitedAt,
                 L->skipped - s_pres.lockSkippedAt);
        s_pres.lockNudgeAt = L->nudgeSum;
        s_pres.lockSeenAt = L->seen;
        s_pres.lockLateAt = L->late;
        s_pres.lockEarlyAt = L->early;
        s_pres.lockWaitedAt = L->waited;
        s_pres.lockSkippedAt = L->skipped;
        /* its late skips are the 10 s line's "late" (the timer path's
           rule, pace_late_skip) */
        s_pres.statSkipLate += L->skippedLate - s_pres.lockSkippedLateAt;
        s_pres.lockSkippedLateAt = L->skippedLate;
    } else if (s_pres.fifoLock) {
        /* fifo with a cap that is not a whole multiple of the game's rate
           (144 on 59.94): nothing is locked */
        snprintf(lock, sizeof(lock), "no lock (fifo, steps on the game's own timer)");
    } else {
        snprintf(lock, sizeof(lock), "lock off");
    }
    if (ratio != s_pres.audioRatio) {
        s_pres.audioRatio = ratio;
        ico_audio_set_rate_ratio(ratio);
    }
    fprintf(
        stderr,
        "window: %u presents, %u movie presents%s and %u game frames (%u frame numbers) in "
        "%.1f s: %.1f "
        "presented fps, %.1f game fps; %u vsyncs (%.1f Hz simulated), %u resyncs dropping "
        "%.0f ms; skipped %u presents (late %u, slow %u); longest replay %.1f ms of %u, %u "
        "over a refresh; %u steps over %.0f ms; "
        "framerate %s, present "
        "%s, display %.2f Hz (%.3f presents a refresh), window %dx%d%s; step late max %.1f "
        "ms; pump max %.1f ms; %s\n",
        s_pres.statPresents, movie - s_pres.statMovie, movieFailed, s_pres.statFrames,
        fn - s_pres.statFrameNo, sec, s_pres.statPresents / sec, s_pres.statFrames / sec,
        s_pres.statVsyncs, s_pres.statVsyncs / sec, s_pres.statResyncs,
        (double)s_pres.statDropped / 1e6, s_pres.statSkipLate + s_pres.statSkipSlow,
        s_pres.statSkipLate, s_pres.statSkipSlow, maxMs, replays, s_perf.ph.over, s_pres.slowSteps,
        s_pres.slowMs, ico_video_framerate_name(s_pres.framerate, fr, sizeof(fr)),
        rhi_present_mode_name(), dm != NULL ? (double)dm->refresh_rate : 0.0,
        dm != NULL && dm->refresh_rate > 1.0f ? s_pres.statPresents / sec / dm->refresh_rate : 0.0,
        pw, ph, window_fullscreen() ? " fullscreen" : "", (double)s_pres.lateMaxNs / 1e6,
        s_pres.pumpMaxMs, lock);
    /* the audio's 10 s line (out_sdl.c; the pull path's only) */
    ico_audio_sdl_log_summary();
    if (s_pres.lockN > 0) {
        disp_log(sec, s_pres.lock.refreshNs);
    }
    disp_block_reset();
    perf_log();
    s_pres.statAt = now;
    s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
    s_pres.statDropped = 0;
    s_pres.statSkipLate = s_pres.statSkipSlow = 0;
    s_pres.statFrameNo = fn;
    s_pres.statMovie = movie;
    s_pres.statMovieFail = movieFail;
    s_pres.slowSteps = s_pres.slowLogged = 0;
    s_pres.lateMaxNs = 0;
    s_pres.pumpMaxMs = 0.0;
}

/* The start-up's presents, one log line each: the boot shows black (keep
   frames, then stage 1 under a full fade) until the first sign's backdrop,
   and this names any present that was not.  For game frames BOOT_LOG_FIRST
   to BOOT_LOG_LAST, and the first BOOT_LOG_AFTER_FULL after the first frame
   that is not a keep frame: the frame, its keep flag, the fade it recorded
   (1 + alpha), its snap, the present's alpha and "first" on a tick's first
   present.  No pixels: a readback here waited for the GPU on every present
   of the window (about a frame's GPU time each at high resolutions, the
   sign's fade-in presented unevenly); rd_boot_present_test probes DISPLAY
   (rd_display_probe) where the pixels are checked. */
#define BOOT_LOG_FIRST 100u
#define BOOT_LOG_LAST 130u
#define BOOT_LOG_AFTER_FULL 12u

static struct {
    uint32_t firstFull; /* the first frame that is not a keep frame, 0 before it */
    int done;
} s_bootLog;

static void boot_log(float alpha)
{
    RdPresentInfo pi;

    if (s_bootLog.done || !rd_last_present_info(&pi)) {
        return;
    }
    if (!pi.keep && !s_bootLog.firstFull) {
        s_bootLog.firstFull = pi.frame;
    }
    const int inFixed = pi.frame >= BOOT_LOG_FIRST && pi.frame <= BOOT_LOG_LAST;
    const int inFull = s_bootLog.firstFull && pi.frame <= s_bootLog.firstFull + BOOT_LOG_AFTER_FULL;

    if (!inFixed && !inFull) {
        if (pi.frame > BOOT_LOG_LAST && s_bootLog.firstFull &&
            pi.frame > s_bootLog.firstFull + BOOT_LOG_AFTER_FULL) {
            s_bootLog.done = 1;
        }
        return;
    }
    fprintf(stderr, "boot: frame %u keep %u fade %u snap %u t %.3f%s\n", pi.frame, pi.keep, pi.fade,
            pi.snap, (double)alpha, pi.firstOfTick ? " first" : "");
}

/* The pacer more than RESYNC_NS behind: the lag is dropped (counted) */
static void resync(Uint64 now)
{
    s_pres.statResyncs++;
    s_pres.statDropped += now - s_deadline;
    s_deadline = now;
}

static void pace(int hz);

/* A step (ico_host_step to this pace) over [dev] slow_step_ms: what ran
   in it.  "other" is the host loop's work between the step and the pump:
   the trace line, the pad recording and the log flush. */
static void slow_step(double ms)
{
    IcoStepProfile p;
    const RdTexCacheStats *tc = rdtex_stats();
    const RdStats *rs = rd_get_stats();

    s_pres.slowSteps++;
    if (s_pres.slowLogged >= SLOW_STEP_LINES) {
        return;
    }
    s_pres.slowLogged++;
    ico_host_step_profile(&p);
    const double other = ms - p.totalMs - s_pres.pumpMs;
    /* the card verdicts the step decided (rd_mesh.c), inside the game
       threads' time */
    uint64_t cardStrips, cardTris;
    double cardMs;
    rd_vu_card_counts(&cardStrips, &cardTris, &cardMs);
    fprintf(stderr,
            "window: slow step %.1f ms at vsync %u (Main tick %u, stage %d%s): step %.1f "
            "(vsync callbacks %.1f, audio %.1f, game threads %.1f in %lu switches, strip verdicts "
            "%llu strips, %llu triangles, %.2f ms, achievements "
            "%.1f), pump %.1f (events %.1f in %u, settings %.1f, input %.1f), other %.1f; %u "
            "disc reads (%u sectors), %u texture decodes, %u pipelines created, memory card %s\n",
            ms, ico_host_vsync_count(), ico_host_main_ticks(), p.stage,
            p.loading ? ", loading" : "", p.totalMs, p.hooksMs, p.audioMs, p.threadsMs, p.switches,
            (unsigned long long)(cardStrips - s_pres.stepCardStrips),
            (unsigned long long)(cardTris - s_pres.stepCardTris), cardMs - s_pres.stepCardMs,
            p.achMs, s_pres.pumpMs, s_pres.pumpEventsMs, s_pres.pumpEvents, s_pres.pumpSettingsMs,
            s_pres.pumpInputMs, other > 0.0 ? other : 0.0, p.cdReads, p.cdSectors,
            tc != NULL ? tc->decodes - s_pres.decodes : 0u,
            rs != NULL ? rs->pipelineCreates - s_pres.pipeCreates : 0u,
            p.mcPending ? "busy" : "idle");
}

/* Every 2 s of samples, resolution "auto"'s step (one down at
   most, never up), applied as an option change would be */
static void auto_res_update(void)
{
    PaceSamples *s = &s_autoRes.samples;

    if (!s_autoRes.active || s->lastNs - s->firstNs < PACE_AUTO_WINDOW_NS) {
        return;
    }
    const int cur = ico_video_auto_scale();
    const int next = pace_auto_resolution_step(s, cur, s_autoRes.windowScale);

    if (next != cur) {
        fprintf(stderr, "video: resolution auto -> %dx (presents took %.1f ms)\n", next,
                (double)pace_samples_median(s) / 1e6);
        ico_video_set_auto_scale(next);
        s->lastStepNs = s->lastNs;
        video_apply(1);
    }
    pace_samples_reset(s, s->lastNs);
}

void ico_window_pace(int hz)
{
    /* the simulation step that ran since the last pace (the host loop
       calls ico_host_step, then this) */
    const Uint64 now = SDL_GetTicksNS();

    if (s_pres.paceEnd != 0 && now > s_pres.paceEnd) {
        const double ms = (double)(now - s_pres.paceEnd) / 1e6;

        s_pres.stepNs = now - s_pres.paceEnd; /* the present allowance's GPU gate */
        s_pres.stepSumMs += ms;
        s_pres.stepMaxMs = ms > s_pres.stepMaxMs ? ms : s_pres.stepMaxMs;
        s_pres.stepCount++;
        if (s_pres.slowMs > 0.0 && ms > s_pres.slowMs) {
            slow_step(ms);
        }
    }
    rd_vu_card_counts(&s_pres.stepCardStrips, &s_pres.stepCardTris, &s_pres.stepCardMs);
    if (s_pres.pausePendingNs) {
        /* a swapchain rebuilt in the pump (video_apply): paused out */
        pace_paused(s_pres.pausePendingNs);
        s_pres.pausePendingNs = 0;
    }
    pace(hz);
    if (s_pres.pausePendingNs) {
        /* the first present after a rebuild (present_once) */
        pace_paused(s_pres.pausePendingNs);
        s_pres.pausePendingNs = 0;
    }
    if (s_pres.presentModeApply) {
        /* the vsync lock wants another present mode (the display's timing
           came up with the device, or stopped; the game's rate changed):
           the swapchain is made again */
        s_pres.presentModeApply = 0;
        video_apply(1);
    }
    perf_drain(); /* every vsync, so the record queue never overflows */
    if (s_autoRes.tickBudget) {
        /* two fields a tick: 40 ms PAL, 33.4 ms NTSC (pace's periods) */
        s_autoRes.samples.budgetNs = 2u * (hz == 50 ? 20000000ull : 16683333ull);
    }
    auto_res_update();
    {
        const RdTexCacheStats *tc = rdtex_stats();
        const RdStats *rs = rd_get_stats();

        s_pres.decodes = tc != NULL ? tc->decodes : 0;
        s_pres.pipeCreates = rs != NULL ? rs->pipelineCreates : 0;
    }
    s_pres.paceEnd = SDL_GetTicksNS();
    /* the step starts now: how late against when it was due (the
       deadline, or under the vsync lock the vblank it follows) */
    if (s_pres.paceEnd > s_deadline && s_pres.paceEnd - s_deadline > s_pres.lateMaxNs) {
        s_pres.lateMaxNs = s_pres.paceEnd - s_deadline;
    }
}

/* A game frame closed in the step before this pace: its time is closeAt
   (when that step was due) */
static int frame_seen(Uint64 closeAt)
{
    const uint32_t fn = rd_frame_number();

    if (fn == s_pres.frame) {
        return 0;
    }
    s_pres.frame = fn;
    s_pres.tickPrev = s_pres.tickAt;
    s_pres.tickAt = closeAt;
    if (!s_pollRestarted) {
        /* the first game frame (the opening scene follows it)
           restarts the surface-size poll's every-present spell */
        s_pollRestarted = 1;
        rhi_surface_poll_restart();
    }
    s_pres.statFrames++;
    return 1;
}

/* One rd_present at alpha a, timed (s_pres.cost) and counted; false when
   nothing was presented.  Its waits from the backend's counts: the
   display's (s_pres.displayWaitNs), the GPU's (fenceWaitNs) and both for
   the lock (lockWaitNs) */
static int present_once(float a, Uint64 refresh, Uint64 period)
{
    RhiStats before, st;

    rhi_get_stats(&before);
    Uint64 t0 = SDL_GetTicksNS();
    ico_diag_present_enter();
    const int ok = rd_present(a);
    ico_diag_present_leave();
    const Uint64 now = SDL_GetTicksNS();

    s_pres.cost = now - t0;
    {
        /* the first present on a swapchain made since the last one (a
           frame rate change's rebuild in video_apply, a size event,
           fullscreen, a rebuild this present found necessary): what it
           took past a refresh (the first acquire, the compositor taking
           the new chain: ~140 ms on NVIDIA's Windows driver) is the
           rebuild's, a pause of the host.  Under the lock it is paused out
           after this pace (pace_lock_vsync keeps its own due time); on
           the timer path now, before the deadline is judged (applied after
           the pace, the lag would be judged first and dropped: a resync, a
           black screen, at most frame rate changes) */
        rhi_get_stats(&st);
        const Uint64 pause =
            pace_first_present_pause(st.swapchains, &s_pres.swapchains, s_pres.cost, refresh);

        if (pause != 0 && s_pres.lockN != 0) {
            s_pres.pausePendingNs += pause;
        } else if (pause != 0) {
            pace_paused(pause);
            t0 += pause; /* the present as if it started a refresh before its return */
        }
        s_pres.cost -= pause; /* the present's own time, a refresh: what the history and the
                                 loop's judgement of the next present see */
    }
    if (!ok) {
        return 0;
    }
    /* how long it waited: the backend's own blocked time in it (Vulkan:
       the acquire, the present and the fence waits; D3D12: the frame
       latency wait, Present and the fence waits), which leaves out the
       replay's own CPU time; the whole present where the backend times
       nothing (then all of it the display's).  Never more than the present
       (a rebuild's first present is paused out above).  The acquire and the
       present are the display's (FIFO's full queue), the fence waits the
       GPU's: the present allowance excuses the first only */
    if (st.presents != before.presents && st.acquireNs + st.presentNs + st.fenceWaitNs != 0) {
        const Uint64 disp = (st.acquireNs - before.acquireNs) + (st.presentNs - before.presentNs);
        const Uint64 fence = st.fenceWaitNs - before.fenceWaitNs;

        s_pres.lockWaitNs = disp + fence < s_pres.cost ? disp + fence : s_pres.cost;
        s_pres.displayWaitNs = disp < s_pres.cost ? disp : s_pres.cost;
        s_pres.fenceWaitNs = fence < s_pres.cost ? fence : s_pres.cost;
    } else {
        s_pres.lockWaitNs = s_pres.cost;
        s_pres.displayWaitNs = s_pres.cost;
        s_pres.fenceWaitNs = 0;
    }
    if (s_pres.presentInfoPending) {
        /* the present that took F12's armed capture (photo_pump names it) */
        s_pres.presentInfoHave = rd_last_present_info(&s_pres.presentInfo);
        s_pres.presentInfoPending = 0;
    }
    /* the cost smoothed over the last presents, with hysteresis;
       a present is slower with an effects program loaded */
    s_pres.paceSlow = pace_slow_present(&s_pres.paceHist, s_pres.cost, refresh, period,
                                        rhi_injector_name() != NULL);
    /* the start-up log line, after the cost */
    boot_log(a);
    s_pres.lastPresent = t0;
    s_pres.lastReturn = now;
    s_pres.presentedFrame = s_pres.frame;
    s_pres.statPresents++;
#ifdef __ANDROID__
    ico_gpu_driver_android_presented(); /* the driver's trial */
#endif
    return 1;
}

/* the vsync lock's window side (pace_policy.h PaceLockIo) */
typedef struct LockIoCtx {
    Uint64 refresh, period;
} LockIoCtx;

static uint64_t lock_io_now(void *u)
{
    (void)u;
    return SDL_GetTicksNS();
}

static bool lock_io_present(void *u, float a)
{
    const LockIoCtx *c = u;

    /* its waits: present_once (lockWaitNs, displayWaitNs) */
    if (!present_once(a, c->refresh, c->period)) {
        return false;
    }
    /* the timer path's late skip closes with any present (pace_late_skip):
       back on it after the lock, its next frame may be skipped again */
    s_pres.skipOpen = false;
    disp_present(c->refresh); /* the display line's counts */
    return true;
}

/* the lock's rule for a present that waited for the display: the wait
   lock_io_present measured */
static uint64_t lock_io_waited(void *u, uint64_t costNs)
{
    (void)u;
    (void)costNs;
    return s_pres.lockWaitNs;
}

/* the present allowance's excuse: the last present's wait for the display
   alone (present_once) */
static uint64_t lock_io_display_waited(void *u, uint64_t costNs)
{
    (void)u;
    (void)costNs;
    return s_pres.displayWaitNs;
}

/* A drop of the present allowance (pace_budget_step returned a change and
   counted a drop): the event line, the first 8 then every 100th */
static void budget_drop_line(const PaceBudget *b)
{
    const unsigned back = b->allow < b->max ? b->allow + 1 : b->max;

    ico_log_limited("window: presents held",
                    "window: presents held to %u a step: the step ended %.1f ms late after %u "
                    "presents (the last present %.1f ms, waited %.1f ms for the GPU and %.1f ms "
                    "for the display; GPU %.1f ms a replay); back to %u after %u steps on time if "
                    "the GPU allows",
                    b->allow, (double)b->dropLateNs / 1e6, b->dropPresents,
                    (double)s_pres.cost / 1e6, (double)s_pres.fenceWaitNs / 1e6,
                    (double)s_pres.displayWaitNs / 1e6, (double)s_pres.gpuNs / 1e6, back,
                    PACE_ALLOW_HOLD + PACE_ALLOW_CLIMB);
}

/* the window on the screen: off it, nothing is presented */
static bool lock_io_visible(void *u)
{
    (void)u;
    return !s_pres.offScreen;
}

static void lock_io_sleep(void *u, uint64_t ns)
{
    const Uint64 now = SDL_GetTicksNS();

    (void)u;
    if (now < ns) {
        SDL_DelayPrecise(ns - now);
    }
}

/* the display's timing of presents (rhi_present_timing) */
static uint64_t lock_io_present_id(void *u)
{
    (void)u;
    return rhi_present_last_id();
}

static int lock_io_shown(void *u, uint64_t *id, uint64_t *ns)
{
    (void)u;
    return rhi_present_timing_next(id, ns);
}

/* the vblank clock (display_clock.h): the next vblank noted */
static int lock_io_vblank(void *u, uint64_t *ns)
{
    const LockIoCtx *c = u;

    return disp_take(c->refresh, ns);
}

/* Words for the lock's display clock (PaceLock.clockKind and .clock), for
   the lock's first line and its changes */
static const char *lock_clock_words(const PaceLock *L, char *buf, size_t len)
{
    char name[64];

    if (L->clockKind == PACE_CLOCK_KIND_SHOWN) {
        switch (L->clock) {
        case PACE_CLOCK_ON:
            return "display timing in use";
        case PACE_CLOCK_LEFT_ERROR:
            return "display timing stopped (a wait on a present failed); presenting fifo until "
                   "the display settings change";
        default:
            return "display timing left (it gave a refresh more than 1 % off the one the "
                   "display reports); presenting fifo until the display settings change";
        }
    }
    if (L->clockKind == PACE_CLOCK_KIND_VBLANK) {
        switch (L->clock) {
        case PACE_CLOCK_ON:
            snprintf(buf, len, "display clock vblank (output %s)",
                     ico_display_clock_name(name, sizeof(name)));
            return buf;
        case PACE_CLOCK_LEFT_ERROR:
            return "display clock stopped (a wait for a vblank failed); the grid on the "
                   "reported refresh until the display or the settings change";
        default:
            snprintf(buf, len,
                     "display clock left (its interval was off the report: %.4f ms against "
                     "%.4f); the grid on the reported refresh until the display or the "
                     "settings change",
                     L->measuredNs / 1e6, (double)L->refreshNs / 1e6);
            return buf;
        }
    }
    return s_pres.clockGone ? "display clock off (display timing left earlier: fifo until the "
                              "display settings change)"
                            : "display clock off";
}

/* presents carry ids for the display's timing only under the lock with the
   timing in use (rhi_present_timing_use) */
static void timing_use(int on)
{
    if (on != s_pres.timingUse) {
        s_pres.timingUse = on;
        rhi_present_timing_use(on != 0);
    }
}

/* One simulated vsync under the vsync lock (pace_policy.h): n presents in
   the step's slots, then the next step's due time on the grid.  s_deadline
   is when the step was due. */
static void pace_locked(unsigned n, Uint64 period)
{
    LockIoCtx ctx;
    const int clock = rhi_present_timing();
    const PaceLockIo io = {&ctx,
                           lock_io_now,
                           lock_io_present,
                           lock_io_sleep,
                           lock_io_visible,
                           clock ? lock_io_present_id : NULL,
                           clock ? lock_io_shown : NULL,
                           lock_io_vblank,
                           lock_io_waited,
                           lock_io_display_waited};
    char words[256];

    ctx.refresh = display_refresh_ns(period);
    ctx.period = period;
    s_pres.statVsyncs++;
    if (s_pres.lockN != n || s_pres.lockRefresh != ctx.refresh ||
        s_pres.lockMailbox != (int)rhi_present_mailbox()) {
        /* the display's counts for the log, and its vblank clock (Windows) */
        disp_lock_start();
        /* a new lock: from the last step's due time, on the present-wait
           clock (mailbox) when the backend times presents, else on the
           vblank clock when it runs (FIFO either way: video_settings),
           else on the reported refresh alone */
        const int kind = clock && !s_pres.clockGone                ? PACE_CLOCK_KIND_SHOWN
                         : s_disp.clock == 1 && !s_pres.vblankGone ? PACE_CLOCK_KIND_VBLANK
                                                                   : PACE_CLOCK_KIND_NONE;

        pace_lock_reset(&s_pres.lock, n, ctx.refresh, period, s_deadline, rhi_present_mailbox(),
                        kind);
        s_pres.lockN = n;
        s_pres.lockRefresh = ctx.refresh;
        s_pres.lockMailbox = rhi_present_mailbox();
        s_pres.lockClock = s_pres.lock.clock;
        s_pres.lockResyncs = 0;
        s_pres.lockDroppedNs = 0;
        s_pres.lockNudgeAt = 0.0;
        s_pres.lockSeenAt = s_pres.lockLateAt = s_pres.lockEarlyAt = 0;
        s_pres.lockWaitedAt = s_pres.lockSkippedAt = 0;
        s_pres.lockSkippedLateAt = 0;
        s_pres.lockWaitNs = 0;
        s_pres.nextPresent = 0;
        {
            static unsigned s_lines;
            double dwmHz;
            uint64_t dwmNs;

            if (s_lines++ < 8) {
                fprintf(stderr,
                        "window: vsync lock: the game steps every %.4f ms (%u refresh%s of "
                        "%.4f ms), %u present%s a step, present mode %s, %s\n",
                        (double)(n * ctx.refresh) / 1e6, n, n == 1 ? "" : "es",
                        (double)ctx.refresh / 1e6, n, n == 1 ? "" : "s", rhi_present_mode_name(),
                        lock_clock_words(&s_pres.lock, words, sizeof(words)));
                /* the compositor's refresh against SDL's (Windows; the
                   primary display's: a cross-check, not a clock) */
                if (ico_display_dwm_refresh(&dwmHz, &dwmNs)) {
                    fprintf(stderr,
                            "window: vsync lock: DWM reports %.3f Hz (%.4f ms); SDL reports "
                            "%.3f Hz (%.4f ms)\n",
                            dwmHz, (double)dwmNs / 1e6, 1e9 / (double)ctx.refresh,
                            (double)ctx.refresh / 1e6);
                }
            }
        }
        /* the audio at the display's nominal rate until the first
           measured block (the 10 s line) */
        s_pres.audioRatio = pace_lock_nominal_ratio(&s_pres.lock);
        ico_audio_set_rate_ratio(s_pres.audioRatio);
    }
    /* the present mode the lock wants: mailbox with the display's timing,
       FIFO without (video_settings) */
    if ((clock && !s_pres.clockGone) != s_pres.mailbox) {
        s_pres.presentModeApply = 1;
    }
    timing_use(s_pres.lock.clockKind == PACE_CLOCK_KIND_SHOWN &&
               s_pres.lock.clock == PACE_CLOCK_ON);
    /* the frame closed in the step that was due at s_deadline */
    const int newFrame = frame_seen(s_deadline);

    disp_vsync(ctx.refresh); /* the display clock's vblanks since the last vsync */
    /* the present allowance's GPU gate (pace_policy.h PaceBudget) */
    s_pres.lock.gpuNs = s_pres.gpuNs;
    s_pres.lock.stepNs = s_pres.stepNs;
    const unsigned drops = s_pres.lock.budget.drops;

    s_deadline = pace_lock_vsync(&s_pres.lock, &io, newFrame != 0);
    if (s_pres.lock.budget.drops != drops) {
        budget_drop_line(&s_pres.lock.budget);
    }
    /* the lag the lock dropped, as the resync lines count it */
    s_pres.statResyncs += s_pres.lock.resyncs - s_pres.lockResyncs;
    s_pres.statDropped += s_pres.lock.droppedNs - s_pres.lockDroppedNs;
    s_pres.lockResyncs = s_pres.lock.resyncs;
    s_pres.lockDroppedNs = s_pres.lock.droppedNs;
    if (s_pres.lock.clock != s_pres.lockClock) {
        /* the display clock stopped or was not believed: the present-wait
           clock's mailbox goes to FIFO until the next settings change; the
           vblank clock's grid stays on the report (already FIFO) until the
           display or the settings change */
        static unsigned s_lines;

        s_pres.lockClock = s_pres.lock.clock;
        if (s_lines++ < 16) {
            fprintf(stderr, "window: vsync lock: %s\n",
                    lock_clock_words(&s_pres.lock, words, sizeof(words)));
        }
        if (s_pres.lock.clock != PACE_CLOCK_ON && s_pres.lock.clockKind == PACE_CLOCK_KIND_SHOWN) {
            s_pres.clockGone = 1;
            s_pres.presentModeApply = 1;
        } else if (s_pres.lock.clock != PACE_CLOCK_ON) {
            s_pres.vblankGone = 1;
        }
    }
    /* the first presents of each lock (a few locks a session), for the
       log: diagnostics only */
    if (!s_pres.lock.traceLogged && s_pres.lock.traceCount >= PACE_LOCK_TRACE_FIRST &&
        s_pres.lock.steps >= PACE_LOCK_TRACE_FIRST + 30) {
        static unsigned s_traces;

        s_pres.lock.traceLogged = true;
        if (s_traces++ < 8) {
            char t[PACE_LOCK_TRACE_FIRST * 20];
            const unsigned k = pace_lock_trace_text(&s_pres.lock, t, sizeof t);

            fprintf(stderr,
                    "window: vsync lock, the first %u presents (ms since the one before; in "
                    "brackets ms from the time it was due to the refresh that showed it, where "
                    "the display said): %s\n",
                    k, t);
        }
    }
    pace_log(SDL_GetTicksNS());
}

/* The window is off the screen: minimised, hidden, occluded, or 0x0
   pixels (a Vulkan swapchain cannot be made); its presents do not wait for
   the display */
static int window_off_screen(void)
{
    int w = 0, h = 0;

    if (s_window == NULL) {
        return 0;
    }
    if (SDL_GetWindowFlags(s_window) &
        (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED)) {
        return 1;
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    return w <= 0 || h <= 0;
}

static void pace(int hz)
{
    /* PAL 50 Hz: 20 ms; NTSC 59.94 Hz: 16.683 ms (host_loop.c's simulated
       periods) */
    const Uint64 period = hz == 50 ? 20000000ull : 16683333ull;
    Uint64 now;

    /* off the screen: the lock does not judge the presents (they return at
       once); back on it, the lock starts again from an empty queue */
    {
        const int off = window_off_screen();

        if (off != s_pres.offScreen) {
            static unsigned s_lines; /* a window that flickers: the first few */

            if (s_lines++ < 20) {
                fprintf(stderr, "window: %s the screen\n", off ? "off" : "back on");
            }
            if (!off) {
                s_pres.lockN = 0; /* pace_locked resets the lock */
            }
            s_pres.offScreen = off;
        }
    }

    /* the vsync lock: the cap equals the refresh and is a whole multiple
       of the simulation's rate (60 or 120 on 59.94), n refreshes within 1 %
       of its period (video_settings decided it for the rate the last pace
       had: a new rate decides again) */
    if (hz != s_pres.paceHz) {
        /* video_settings took NTSC before the first pace */
        if ((hz == 50) != (s_pres.paceHz == 50)) {
            s_pres.presentModeApply = 1;
        }
        s_pres.paceHz = hz;
    }
    {
        const unsigned n = s_pres.gridLock && rd_interpolation_active()
                               ? pace_lock_presents(s_pres.framerate, period)
                               : 0;
        if (n > 0 && pace_grid_period_ok(n, display_refresh_ns(period), period)) {
            pace_locked(n, period);
            return;
        }
        if (s_pres.lockN != 0 || s_pres.budget.max == 0) {
            /* off the lock (or the first pace): the timer path's allowance
               starts again */
            pace_budget_reset(&s_pres.budget, PACE_ALLOW_MAX);
        }
        s_pres.lockN = 0;
        timing_use(0); /* the presents below as without the timing */
        disp_clock_stop();
    }
    s_pres.budget.presents = 0; /* the step's presents (pace_budget_step) */
    s_pres.budget.optional = 0;
    s_deadline += period;
    s_pres.statVsyncs++;
    const int newFrame = frame_seen(s_deadline - period);
    if (!rd_interpolation_active()) {
        /* framerate "original": rd_end_frame presented the frame once; the
           picture is held until the next.  That present's start-up log line
           (the frame whole: t 1) */
        if (newFrame) {
            boot_log(1.0f);
            /* that present ran inside the step (rd_end_frame's replay):
               the first on a swapchain made since the last one is the
               rebuild's past a refresh (as present_once), paused out
               before the deadline is judged */
            RhiStats st;

            rhi_get_stats(&st);
            pace_paused(pace_first_present_pause(st.swapchains, &s_pres.swapchains,
                                                 (Uint64)(rd_last_replay_ms() * 1e6),
                                                 display_refresh_ns(period)));
        }
        now = SDL_GetTicksNS();
        if (now < s_deadline) {
            SDL_DelayPrecise(s_deadline - now);
        } else if (now - s_deadline > RESYNC_NS) {
            resync(now);
        }
        pace_log(SDL_GetTicksNS());
        return;
    }
    /* the simulation keeps its vsync cadence in simulated time; until
       this vsync's deadline the window presents as often as vsync (and the
       framerate cap) allow, each present at alpha = the real time since the
       last frame closed over the tick, between the last two frames (one
       tick of latency). A frame closes inside the step before this call:
       its time is the start of this vsync period. */
    /* a present with vsync on blocks up to one display refresh; that wait is
       not the renderer being slow. A 60 Hz display under the 59.94 Hz
       simulation (refresh 16.67 ms against the 16.68 ms period) sits on
       that edge and would fall back to one present per game frame. Slow
       means a present costing more than a display refresh plus half a
       simulated period (pace_policy.h: the cost is smoothed, with
       hysteresis, and the limit is higher with an effects program
       loaded). */
    Uint64 gap;
    const Uint64 refresh = display_refresh_ns(period);
    {
        /* "uncapped" with vsync on (s_pres.mailbox: rhi_prefer_mailbox):
           a little less than one display refresh between presents, in
           mailbox mode as under FIFO, so the presents average about one a
           refresh (the log's "presents a refresh" figure).
           Two a refresh in mailbox mode drew a picture the display never
           showed for every one it did, which kept a handheld's GPU near its
           limit; a phone also lost the time for the game.  Without vsync
           "uncapped" is back to back.  A cap equal to the refresh has no
           gap: FIFO's blocking present is the timer (s_pres.fifoLock).
           Any other cap is its own gap, on the drift-free grid
           (pace_policy.h pace_present_gap, pace_next_present) */
        gap = s_pres.fifoLock
                  ? 0
                  : pace_present_gap(s_pres.framerate,
                                     s_pres.framerate == ICO_FRAMERATE_UNCAPPED && s_pres.mailbox,
                                     rhi_present_mailbox(), refresh);
        if (!gap) {
            s_pres.nextPresent = 0;
        }
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    for (;;) {
        now = SDL_GetTicksNS();
        /* the late-present skip (pace_policy.h pace_late_skip): a new frame
           behind the deadline is left unpresented (the next step at once;
           the next present shows the newer frame) when the next step is
           already a whole period late (the step debt: now against its due
           time, s_deadline, as "step late" measures it) or presents are
           slow; never two frames in a row (pace_skipped_last), the same
           frame again while it holds; with or without sound.  The vsync
           lock has the late arm too (pace_lock_vsync).  No step or audio is
           dropped, only presents */
        {
            const PaceSkip why =
                pace_late_skip(s_pres.presentedFrame != s_pres.frame,
                               pace_skipped_last(s_pres.skipOpen, s_pres.skipFrame, s_pres.frame),
                               s_pres.paceSlow, now, s_deadline, period);

            if (why != PACE_SKIP_NONE) {
                s_pres.skipOpen = true;
                s_pres.skipFrame = s_pres.frame;
                if (why == PACE_SKIP_LATE) {
                    s_pres.statSkipLate++;
                } else {
                    s_pres.statSkipSlow++;
                }
                break;
            }
        }
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        /* under FIFO with a cap that is not a whole multiple of the
           simulation's rate (144 on 59.94, 60 on PAL's 50) and a refresh
           shorter than the period: no present that would return on a
           vblank past the deadline.  A FIFO present returns on the
           first vblank after it is made, so on the last return's grid of
           refreshes; starting one just before the deadline blocked a whole
           refresh and ran the step late.  A new frame is held
           back (to the next vsync) at most once and never once the deadline
           has passed, so a game behind still shows each frame
           (pace_policy.h pace_fifo_hold) */
        if (s_pres.fifoLock && refresh < period &&
            pace_fifo_hold(now, s_pres.lastReturn, refresh, s_deadline,
                           s_pres.presentedFrame == s_pres.frame,
                           s_pres.heldFrame == s_pres.frame + 1u)) {
            if (s_pres.presentedFrame != s_pres.frame) {
                s_pres.heldFrame = s_pres.frame + 1u;
            }
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        /* nor a further present of a frame already shown that would
           end past the deadline (its cost the last present's): the next
           simulation step is never late for one */
        /* off the screen: no present again */
        /* nor one past the present allowance (pace_policy.h PaceBudget):
           after a step that ended half a period late, the in-betweens a
           step are held below what that step made */
        if (s_pres.presentedFrame == s_pres.frame) {
            const int guard = now >= s_deadline || s_pres.paceSlow || s_pres.offScreen ||
                              (!s_pres.fifoLock && now + s_pres.cost > s_deadline);

            if (guard || !pace_budget_allows(&s_pres.budget, false)) {
                if (!guard) {
                    s_pres.budget.refused++;
                }
                if (now < s_deadline) {
                    SDL_DelayPrecise(s_deadline - now);
                }
                break;
            }
        }
        if (gap && s_pres.nextPresent && now < s_pres.nextPresent) {
            const Uint64 wake = s_pres.nextPresent;

            if (wake >= s_deadline) {
                if (now < s_deadline) {
                    SDL_DelayPrecise(s_deadline - now);
                }
                break;
            }
            SDL_DelayPrecise(wake - now);
            continue;
        }
        /* alpha from the present clock (the measured time smoothed
           to the presents' steady cadence) against the simulated time the
           frame closed; the raw measured time jittered with the step and
           the sleeps before the present */
        float a = rd_present_clock_alpha(&s_pres.clock, (double)now / 1e6,
                                         (double)s_pres.tickAt / 1e6, (double)tick / 1e6,
                                         (double)(s_pres.fifoLock ? refresh : gap) / 1e6);

        if (s_pres.legacyAlpha) {
            /* ICO_RD_S2_LEGACY=1: the raw measured time, without the clock */
            const double r =
                now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
            a = (float)(r > 0.999 ? 0.999 : r);
        }
        const bool fresh = s_pres.presentedFrame != s_pres.frame;
        const int ok = present_once(a, refresh, period);
        now = SDL_GetTicksNS();
        if (ok) {
            s_pres.skipOpen = false;
            pace_budget_present(&s_pres.budget, fresh);
        }
        if (!ok) {
            /* a movie on the output, or nothing closed yet */
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        s_pres.nextPresent =
            gap ? pace_next_present(s_pres.nextPresent, s_pres.lastPresent, gap) : 0;
    }
    now = SDL_GetTicksNS();
    {
        /* the present allowance: the step ends now (a resync keeps it) */
        const unsigned drops = s_pres.budget.drops;

        if (pace_budget_step(&s_pres.budget, now > s_deadline ? now - s_deadline : 0, period,
                             refresh, s_pres.displayWaitNs, s_pres.gpuNs, s_pres.stepNs) &&
            s_pres.budget.drops != drops) {
            budget_drop_line(&s_pres.budget);
        }
    }
    pace_log(now);
    if (now > s_deadline && now - s_deadline > RESYNC_NS) {
        resync(now);
    }
}

void ico_window_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = 0;
    /* a clean exit: a lazy run keeps the start-up activity file, so the
       next start of this version is lazy too; any other deletes it
       (diag_host.h) */
    ico_diag_startup_activity_exit();
#if defined(__ANDROID__) || defined(ICO_IOS)
    SDL_RemoveEventWatch(lifecycle_watch, NULL);
#endif
#ifdef __ANDROID__
    /* a normal end: a driver package still on trial did not crash, so the
       next start does not treat it as failed */
    ico_gpu_driver_android_end();
#endif
    set_capture(ICO_CAPTURE_OFF);
    ico_input_sdl_shutdown();
    ui_settings_set_window_mode_query(NULL);
    ui_settings_set_texture_pack_count(NULL);
    ui_settings_set_touch_query(NULL);
    ui_settings_set_touch_edit_host(NULL);
#if !defined(__ANDROID__) && !defined(ICO_IOS)
    ui_settings_set_steam_handler(NULL);
#endif
    ui_settings_set_disc_handler(NULL);
    ui_settings_set_frame_dump_handler(NULL);
    ui_settings_set_share_report_handler(NULL);
#ifdef __ANDROID__
    ui_settings_set_gpu_driver_host(NULL);
#endif
#if defined(__ANDROID__) || defined(ICO_IOS)
    ui_settings_set_quit_is_game(NULL);
#endif
    ui_touch_set_source(NULL);
    ui_host_shutdown();
    disp_clock_stop();
    rd_set_host_call(NULL);
    rd_shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
