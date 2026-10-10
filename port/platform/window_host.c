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
#include "config.h"
#include "diag_host.h"
#include "font.h" /* port/ui: ico_window_progress's text and bar */
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h"
#include "ico_credits.h"
#include "input.h"
#include "input_record.h"
#include "input_sdl.h"
#include "mouse_look.h"
#include "options.h"
#include "pace_policy.h"
#include "photo_mode.h"
#include "photo_ui.h"
#include "pointer.h"
#include "rd.h"
#include "rd_tex.h"
#include "../fmv/rd_video.h"
#include "rhi.h"
#include "sched.h"
#include "settings.h"
#include "texpack.h"
#include "touch_ui.h"
#include "trace_host.h"
#include "ui_host.h"
#include "ui_mouse.h"
#include "video_options.h"
#include "window_host.h"
#include "window_lifecycle.h"
#include "window_video.h"
#ifdef __ANDROID__
#include "android/gpu_driver_android.h"
#include "android/host_android.h"
#endif

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull
/* Slow-step lines a stats block logs at most (the rest are
   counted in its first line) */
#define SLOW_STEP_LINES 5

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

/* the mode Alt+Enter goes back to from Windowed (the last
   non-windowed one the options or the window had; Fullscreen to begin with) */
static int s_lastFullMode = ICO_WINDOW_FULLSCREEN;

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
    Uint64 lastPresent;
    Uint64 cost;       /* how long the last rd_present took */
    PaceHist paceHist; /* the last presents' costs (pace_policy.h) */
    bool paceSlow;     /* pace_slow_present after the last present */
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
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
    int mailbox; /* rhi_prefer_mailbox as last applied: vsync on and presenting between ticks */
    /* the slow-step lines ([dev] slow_step_ms, 0 off): the threshold,
       the block's count and lines, ico_window_pump's time, the renderer's
       texture decodes and pipeline creations at the last pace's end */
    double slowMs, pumpMs;
    unsigned slowSteps, slowLogged;
    uint32_t decodes, pipeCreates;
    /* the present clock alpha is taken from (rd.h RdPresentClock) */
    RdPresentClock clock;
    int legacyAlpha; /* ICO_RD_S2_LEGACY=1 (read in video_settings) */
    /* F11, the stats line every second until this time (0: off) */
    Uint64 fastUntil;
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
   true written one line each into logs/ico-pc-perf.csv */
static struct {
    int csvTried;
    FILE *csv;
    unsigned n, gpuN;
    double total, maxTotal, interp, wait, acquire, upload, walk, bind, submit, present, readback,
        fence;
    double gpu, maxGpu, gpuList[GPU_LISTS], gpuUpload, gpuPresent;
    double gpuPost[RD_PERF_POST_COUNT]; /* per picture effect (rd.h RD_PERF_POST_*) */
    unsigned gpuPostPartial;            /* replays whose effects were not all timed apart */
    uint64_t draws, passes, pipeBinds, groupBinds, groups, barriers, copies, bytes, meshBytes;
    uint64_t texUploads, meshUploads, dateSnaps, exact, pipeCreates;
    uint64_t bufCreated, bufDestroyed, texCreated, texDestroyed, allocs, fenceWaits, waitIdles,
        readbacks;
} s_perf;

/* The renderer's settings from the display options and the window's pixel
   size. */
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
       the present's alpha the older way (rd.h RdPresentClock) */
    {
        const char *e = getenv("ICO_RD_S2_LEGACY");

        s_pres.legacyAlpha = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    s_pres.framerate = ico_video_framerate();
    rs->interpolate = (uint8_t)(s_pres.framerate != ICO_FRAMERATE_ORIGINAL);
    /* presenting between ticks with vsync on, the swapchain prefers the
       mailbox mode: no tearing, and a present never waits for the display,
       so it cannot hold the simulation back */
    s_pres.mailbox = rs->vsync && rs->interpolate;
    rhi_prefer_mailbox(s_pres.mailbox != 0);
}

/* The window is fullscreen now (SDL's flag, which follows the window
   manager's answer) */
static int window_fullscreen(void)
{
    return s_window != NULL && (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

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
    ico_video_get(&o);
    /* on a change of the options only (never a resize's forced call: a
       refused request would be made again at every size event), and
       compared against the window, not the last request: an option that
       already matches what the window is asks for nothing */
    int request = !force && o.windowMode != window_mode_now();
    if (o.windowMode != ICO_WINDOW_WINDOWED) {
        s_lastFullMode = o.windowMode;
    }
#ifdef __ANDROID__
    /* Android: always fullscreen (immersive, the window's creation
       flag); the option follows the window (window_fullscreen_event) */
    request = 0;
#endif
    if (request) {
        /* fullscreen at the desktop resolution, a frameless window over the
           display, or the framed window; the resize event follows */
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
    const int mailbox = s_pres.mailbox;
    video_settings(&rs, w, h);
    rd_set_settings(&rs);
    if (mailbox != s_pres.mailbox && w > 0 && h > 0) {
        /* the present mode follows the frame rate option (the swapchain
           is recreated) */
        rd_resize_output((uint32_t)w, (uint32_t)h);
    }
}

/* The touch overlay's zones from the window's pixel size and
   its safe area (a notch, rounded corners, the system bars), which SDL
   gives in points: scaled to pixels.  On open and when either changes; the
   Touch size row is followed by input_sdl.c. */
static void touch_layout_update(void)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_Rect r;

    if (s_window == NULL || !SDL_GetWindowSizeInPixels(s_window, &pw, &ph) || pw <= 0 || ph <= 0) {
        return;
    }
    if (SDL_GetWindowSize(s_window, &ww, &wh) && ww > 0 && wh > 0 &&
        SDL_GetWindowSafeArea(s_window, &r) && r.w > 0 && r.h > 0) {
        const double kx = (double)pw / ww, ky = (double)ph / wh;

        ico_input_sdl_set_touch_layout(pw, ph, (int)(r.x * kx + 0.5), (int)(r.y * ky + 0.5),
                                       (int)(r.w * kx + 0.5), (int)(r.h * ky + 0.5));
    } else {
        ico_input_sdl_set_touch_layout(pw, ph, 0, 0, 0, 0);
    }
}

/* The window's pixel size the renderer was opened at or last
   given (the size event's path, window_pixel_size); read by
   window_size_recheck, which only Android needs */
#ifdef __ANDROID__
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
    rd_resize_output((uint32_t)w, (uint32_t)h);
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
#ifdef __ANDROID__
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

#ifdef __ANDROID__
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

#ifdef __ANDROID__
    (void)mode;
    return vk | SDL_WINDOW_FULLSCREEN;
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
    s_pres.cost = 0;
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
static bool SDLCALL lifecycle_watch(void *userdata, SDL_Event *event)
{
    const IcoLifecycleEvent e = ico_lifecycle_from_sdl(event->type);

    (void)userdata;
    if (e != ICO_LIFECYCLE_NONE && s_open) {
        ico_lifecycle_on(e, &k_lifecycle_ops);
    }
    return true;
}

#ifdef __ANDROID__
/* the start-up screen's state and callback (the callback is below, with the
   progress view) */
typedef struct PipeScreen {
    uint64_t startNs, lastNs;
    int drawn;
} PipeScreen;

static void pipeline_progress(void *ctx, uint32_t done, uint32_t total);

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

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    /* [video] backend (config.toml) or backend= (ico-pc.ini): "vulkan", the
       default, or "d3d12" where the build has it */
    const char *backend = ico_config_get_string("video.backend", "vulkan");
    if (!rhi_create_backend(backend)) {
        fprintf(stderr, "window: renderer backend \"%s\" is not in this build; using vulkan\n",
                backend);
        backend = "vulkan";
        rhi_create_backend(backend);
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "window: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    IcoVideoOptions start;

    ico_video_get(&start);
    s_videoSerial = ico_video_serial();
    /* fullscreen from the config is asked for at creation, not
       after: SDL's X11 backend then writes _NET_WM_STATE before the window
       is mapped, which KWin and gamescope honour (borderless at the desktop
       resolution; no exclusive mode) */
    s_window = SDL_CreateWindow("ICO", WINDOW_W, WINDOW_H, window_flags(start.windowMode));
    if (start.windowMode != ICO_WINDOW_WINDOWED) {
        s_lastFullMode = start.windowMode;
    }
    if (s_window == NULL) {
        fprintf(stderr, "window: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    /* the replay, the present and the FMV picture run on the host stack in
       the host FP mode, not on the game's 256 KB fiber stacks */
    rd_set_host_call(ico_sched_call_on_host);
    /* the Vulkan pipeline cache in the per-user folder: a later start
       creates the renderer's pipelines from it (rhi_set_pipeline_cache_path;
       D3D12 ignores it). Android: the app's cache folder, which the system
       may empty when space runs low (android_paths.h) */
    {
        char pref[ICO_PATH_MAX], cache[ICO_PATH_MAX + 32];

#ifdef __ANDROID__
        if (ico_android_cache_dir(pref, sizeof(pref)) != 0) {
            ico_host_pref_dir(pref, sizeof(pref));
        }
#else
        ico_host_pref_dir(pref, sizeof(pref));
#endif
        snprintf(cache, sizeof(cache), "%s/pipelines.vkcache", pref);
        rhi_set_pipeline_cache_path(cache);
    }
    /* the window system's answer before the size is read */
    SDL_SyncWindow(s_window);
    s_fullscreen = window_fullscreen();
#ifndef __ANDROID__
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
    bool up = rd_init(gsW, gsH, &rs, s_window);
#ifdef __ANDROID__
    if (!up && customDriver) {
        ico_gpu_driver_android_init_failed();
        up = rd_init(gsW, gsH, &rs, s_window);
    }
#endif
    if (!up) {
        fprintf(stderr,
                "window: no usable %s device (rd_init failed; the rhi lines above say "
                "why)\n",
                backend);
#ifdef __ANDROID__
        choose_driver();
#endif
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
    /* the whole reachable pipeline set before the first frame and after
       rd_init (which makes the device), so the game never waits on a
       pipeline compile (tens of ms each on a GPU driver); the time is
       logged */
#ifdef __ANDROID__
    {
        static PipeScreen screen;

        /* a start with a cold driver cache can take many seconds on a
           phone: say what is going on (nothing is drawn when it is quick).
           The PC keeps its silent start. */
        rd_set_pipeline_progress(pipeline_progress, &screen);
        rd_precreate_pipelines();
        rd_set_pipeline_progress(NULL, NULL);
    }
#else
    rd_precreate_pipelines();
#endif
    /* which way of reading the depth gives the fog its distances on this
       GPU, tried on the real scene size; logged ("fog: depth path") */
    rd_fog_selftest();
    {
        IcoVideoOptions o;
        char res[32], fr[16];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height, framerate %s), vsync %s, texture pack %s, "
                "dump textures %s, model pack %s, dump models %s, depth %s\n",
                w, h, s_fullscreen ? " fullscreen" : "",
                rhi_backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan", rhi_adapter_name(),
                video_preset_label(&o), ico_video_resolution_name(&o, res, sizeof(res)),
                ico_video_aspect_name(o.aspect), ico_video_filter_name(o.filter),
                o.fullHeight ? "full" : "half",
                ico_video_framerate_name(ico_video_framerate(), fr, sizeof(fr)),
                o.vsync ? "on" : "off", o.texturePack ? "on" : "off",
                o.dumpTextures && ico_opt_developer_mode() ? "on" : "off",
                o.modelPack ? "on" : "off", o.dumpModels && ico_opt_developer_mode() ? "on" : "off",
                rhi_limits()->depthStencilFormatName);
        fprintf(stderr, "window: present mode %s%s\n", rhi_present_mode_name(),
                rhi_present_mailbox() ? " (vsync without waiting on the display)" : "");
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
#ifdef __ANDROID__
    /* the Graphics driver page, and "Quit game" for the
       title's Quit row (a phone has no desktop to quit to); both before the
       game's first frame builds the menus */
    ui_settings_set_gpu_driver_host(ico_gpu_driver_android_host());
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
    ui_touch_set_source(ico_input_sdl_touch_overlay);
    /* SDL delivers the app's lifecycle only to a watch, as
       it happens (SDL sends these on mobile systems only) */
#ifdef __ANDROID__
    if (!SDL_AddEventWatch(lifecycle_watch, NULL)) {
        fprintf(stderr, "window: SDL_AddEventWatch: %s\n", SDL_GetError());
    }
#else
    (void)lifecycle_watch;
#endif
    return 0;
}

/* mode is ICO_CAPTURE_* (mouse_look.h): STICK and DELTA hide and
   hold the pointer (relative mode), OFF frees it; the device layer routes
   the motion by mode */
static void set_capture(int mode)
{
#ifdef __ANDROID__
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
       the menus: the left button is Cross again (it skips the movie) */
    if (mpegPlay != 0) {
        ico_pointer_set_menu(0);
    }
#ifndef __ANDROID__
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
#ifdef __ANDROID__
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

/* The renderer's device was removed, reset or hung (the driver
   crashed or was updated, the GPU was unplugged): nothing more can be drawn,
   so instead of a frozen window the player gets one message and the session
   ends through the normal quit path (atexit closes the window). The backend
   logged the reason. */
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
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC",
                                 "The graphics card stopped responding (its driver was reset or "
                                 "updated, or the card was removed).\n\nThe game has to close. "
                                 "Your last save is kept; logs/ico-pc.log says why.",
                                 s_window);
    }
    return 1;
}

/* F12, the frame on the screen as an rd dump and a PNG in
   <pref>/dumps, for a bug report */
static void frame_dump(void)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX];
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
                             sizeof(png)) != 0) {
        fprintf(stderr, "window: F12: the dumps folder %s is too deep for the file names\n", dir);
        return;
    }
    const int ok = rd_dump_on_demand(dump, png);
    /* the recording up to this moment, for the same report */
    ico_input_record_flush();
    fprintf(stderr, "window: F12 at vsync %u, Main tick %u, frame %u: %s %s and %s\n",
            ico_host_vsync_count(), ico_host_main_ticks(), (unsigned)rd_frame_number(),
            ok ? "wrote" : "could not write all of", dump, png);
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
            ui_photo_capture_done(r > 0, base != NULL ? base + 1 : path);
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

    s_fullscreen = window_fullscreen();
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
}

int ico_window_pump(void)
{
    SDL_Event e;
    IcoHotkey hotkey;
    int quit = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (device_lost_quit()) {
        return 0;
    }
    if (s_quitLatched) {
        s_quitLatched = 0;
        quit = 1;
    }

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            hotkey = ico_hotkey_for(e.key.key, e.key.repeat);
            if (hotkey == ICO_HOTKEY_FRAME_DUMP) {
                frame_dump();
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
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            /* a drag sends one per size: print when the fullscreen
                   flag changed or a second has passed since the last line */
            static int s_sizeFs = -1;
            static Uint64 s_sizeAt;
            int fs = window_fullscreen() ? 1 : 0;
            Uint64 now = SDL_GetTicksNS();
            int print = fs != s_sizeFs || now - s_sizeAt >= 1000000000ull;
#ifdef __ANDROID__
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
        default:
            ico_input_sdl_event(&e);
            break;
        }
    }
    first_pump_recheck();
    /* the Settings menu's changes; forced when the
       renderer's output followed a swapchain rebuilt at another size, so
       aspect "auto" and resolution "window" follow it as on a resize */
    video_apply(rd_output_followed(NULL, NULL) ? 1 : 0);
    /* a hard camera cut or stage change the game signalled during the
       step that just ran: the frame it recorded is not blended from the
       one before (the step closes the previous frame before the game's
       threads run, so the open frame is the one the cut belongs to) */
    if (ico_video_cut_serial() != s_pres.cutSerial) {
        s_pres.cutSerial = ico_video_cut_serial();
        rd_camera_cut();
    }
    photo_pump();                /* photo mode's captures */
    set_capture(capture_mode()); /* play and photo mode */
    pointer_visibility();        /* the pointer in the menus */
    ico_input_sdl_update();
    /* the popups' clock; the presenter draws them on its overlay at each
       present (port/ui/ui_host.c) */
    ui_host_vsync(ico_host_main_ticks());
    s_pres.pumpMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    return !quit;
}

/* --- the start-up screens, in the game's window ------------------------- */

typedef struct ProgressView {
    const char *title;
    const char *phase;
    int pct;
    int allowCancel; /* the bar's "how to stop" line is shown */
} ProgressView;

/* the layout grid's 4:3 picture (port/ui/font.h): the title, the phase
   line, the bar and (Android) how to stop */
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
#ifdef __ANDROID__
        if (v->allowCancel) {
            ui_draw_text(UI_GRID_CX, 300.0f, 16.0f, body,
                         "Press Back to stop (you can start again later)",
                         UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
        }
#endif
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
   the next ico_window_pump. */
static int progress_present(const char *title, const char *phase, int pct, int allowCancel)
{
    static int s_cancelLogged;
    SDL_Event e;
    int cancel = 0, closing = 0;
    void *prevUser = NULL, *prevTopUser = NULL;
    RdOverlayFn prev, prevTop;
    ProgressView v;

    if (s_open) {
        while (SDL_PollEvent(&e)) {
            progress_event(&e, &cancel, &closing);
        }
    } else {
        static const Uint32 k_taken[] = {SDL_EVENT_QUIT, SDL_EVENT_WINDOW_CLOSE_REQUESTED,
                                         SDL_EVENT_KEY_DOWN};
        size_t i;

        SDL_PumpEvents();
        for (i = 0; i < sizeof(k_taken) / sizeof(k_taken[0]); i++) {
            while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, k_taken[i], k_taken[i]) == 1) {
                progress_event(&e, &cancel, &closing);
            }
        }
    }
    if (s_open) {
        first_pump_recheck();
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
    int stop;

    if (!s_open) {
        return 0;
    }
    stop = progress_present(title, phase, pct, 1);
    /* a quit or close from an earlier screen that could not stop (the
       start-up graphics screen) stops this one too */
    return stop || s_quitLatched;
}

#ifdef __ANDROID__
/* The start-up screen while the graphics are prepared (rd_set_pipeline_progress).
   With the graphics already prepared (the driver's saved cache, a start that
   follows another) this takes a few tens of milliseconds and nothing is
   drawn: the screen appears only when the wait is already noticeable, and
   then at most ten times a second. */
#define PIPE_SCREEN_AFTER_MS 250.0
#define PIPE_SCREEN_EVERY_MS 100.0

static void pipeline_progress(void *ctx, uint32_t done, uint32_t total)
{
    PipeScreen *p = ctx;
    const uint64_t now = SDL_GetTicksNS();
    char phase[64];

    if (done == 0) {
        p->startNs = now;
        p->drawn = 0;
        return;
    }
    if ((double)(now - p->startNs) / 1e6 < PIPE_SCREEN_AFTER_MS ||
        (p->drawn && (double)(now - p->lastNs) / 1e6 < PIPE_SCREEN_EVERY_MS)) {
        return;
    }
    snprintf(phase, sizeof(phase), "Preparing graphics (%u of %u)", (unsigned)done,
             (unsigned)total);
    p->drawn = 1;
    progress_present("Starting ICO", phase, total > 0 ? (int)((uint64_t)done * 100u / total) : -1,
                     0);
    /* the present itself took time: the next draw is measured from its end */
    p->lastNs = SDL_GetTicksNS();
}
#endif

/* logs/ico-pc-perf.csv, opened on the first record when [dev] perf_log
   is true */
static void perf_csv_open(void)
{
    char dir[ICO_PATH_MAX], logs[ICO_PATH_MAX], path[ICO_PATH_MAX];

    s_perf.csvTried = 1;
    if (!ico_config_get_bool("dev.perf_log", 0)) {
        return;
    }
    ico_host_exe_dir(dir, sizeof(dir));
    if (ico_path_join(logs, sizeof(logs), dir, "logs") != 0 ||
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
    fprintf(s_perf.csv, ",gpu_effects_partial\n");
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
    fprintf(f, ",%u\n", r->gpuPostPartial);
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

/* The finished records into the block's sums and the CSV */
static void perf_drain(void)
{
    RdPerfRecord r;

    while (rd_perf_pop(&r)) {
        auto_res_sample(&r);
        if (!s_perf.csvTried) {
            perf_csv_open();
        }
        if (s_perf.csv != NULL) {
            perf_csv_line(&r);
        }
        s_perf.n++;
        s_perf.total += r.totalMs;
        s_perf.maxTotal = r.totalMs > s_perf.maxTotal ? r.totalMs : s_perf.maxTotal;
        s_perf.interp += r.interpMs;
        s_perf.wait += r.waitMs;
        s_perf.acquire += r.acquireMs;
        s_perf.upload += r.uploadMs;
        s_perf.walk += r.walkMs;
        s_perf.bind += r.bindMs;
        s_perf.submit += r.submitMs;
        s_perf.present += r.presentMs;
        s_perf.readback += r.readbackMs;
        s_perf.fence += r.fenceWaitMs;
        if (r.gpuValid) {
            s_perf.gpuN++;
            s_perf.gpu += r.gpuMs;
            s_perf.maxGpu = r.gpuMs > s_perf.maxGpu ? r.gpuMs : s_perf.maxGpu;
            s_perf.gpuUpload += r.gpuUploadMs;
            s_perf.gpuPresent += r.gpuPresentMs;
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
        s_perf.exact += r.exactBlends;
        s_perf.pipeCreates += r.pipelineCreates;
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
}

/* A new block's sums (the CSV stays open) */
static void perf_reset(void)
{
    FILE *csv = s_perf.csv;
    const int tried = s_perf.csvTried;

    memset(&s_perf, 0, sizeof(s_perf));
    s_perf.csv = csv;
    s_perf.csvTried = tried;
    s_pres.stepSumMs = s_pres.stepMaxMs = 0.0;
    s_pres.stepCount = 0;
}

/* The block's second line: where a replay's time went, on average */
static void perf_log(void)
{
    const double n = s_perf.n ? (double)s_perf.n : 1.0;
    const double g = s_perf.gpuN ? (double)s_perf.gpuN : 1.0;
    char gpu[160] = "no GPU timestamps";

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
    fprintf(stderr,
            "window: %u replays: CPU %.2f ms (max %.2f): interp %.2f, wait %.2f, acquire %.2f, "
            "upload %.2f, walk %.2f, bind %.2f, submit %.2f, present %.2f, readback %.2f "
            "(fence waits %.2f); %s; simulation step %.2f ms (max %.2f)\n",
            s_perf.n, s_perf.total / n, s_perf.maxTotal, s_perf.interp / n, s_perf.wait / n,
            s_perf.acquire / n, s_perf.upload / n, s_perf.walk / n, s_perf.bind / n,
            s_perf.submit / n, s_perf.present / n, s_perf.readback / n, s_perf.fence / n, gpu,
            s_pres.stepCount ? s_pres.stepSumMs / s_pres.stepCount : 0.0, s_pres.stepMaxMs);
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
        fprintf(stderr, "window: GPU ms per replay: lists 0-12 %s; effects: %s%s; present %.2f\n",
                lists, posts, partial, s_perf.gpuPresent / g);
    }
    fprintf(stderr,
            "window: per replay %.0f draws, %.0f passes, %.0f pipeline and %.0f bind group binds, "
            "%.0f bind groups, %.0f barriers, %.0f copies, %.0f KB uploaded (%.0f KB meshes), "
            "%.1f textures and %.1f meshes uploaded, %.1f DATE snapshots, %.1f exact blends; in "
            "the block %llu buffers and %llu textures created, %llu and %llu destroyed, %llu "
            "memory allocations, %llu pipelines created, %llu fence waits, %llu wait-idles, %llu "
            "readbacks\n",
            s_perf.draws / n, s_perf.passes / n, s_perf.pipeBinds / n, s_perf.groupBinds / n,
            s_perf.groups / n, s_perf.barriers / n, s_perf.copies / n, s_perf.bytes / n / 1024.0,
            s_perf.meshBytes / n / 1024.0, s_perf.texUploads / n, s_perf.meshUploads / n,
            s_perf.dateSnaps / n, s_perf.exact / n, (unsigned long long)s_perf.bufCreated,
            (unsigned long long)s_perf.texCreated, (unsigned long long)s_perf.bufDestroyed,
            (unsigned long long)s_perf.texDestroyed, (unsigned long long)s_perf.allocs,
            (unsigned long long)s_perf.pipeCreates, (unsigned long long)s_perf.fenceWaits,
            (unsigned long long)s_perf.waitIdles, (unsigned long long)s_perf.readbacks);
    {
        /* the graphics allocations against the device's limit (a
           phone GPU allows 4096) and the process's memory (a phone ends a
           process that holds too much without a word) */
        RhiStats st;
        long rss = -1, peak = -1;

        char gfx[160] = "graphics allocations not counted";

        rhi_get_stats(&st);
        ico_diag_process_memory(&rss, &peak);
        if (st.memoryLimit != 0) { /* Vulkan counts them */
            snprintf(gfx, sizeof(gfx),
                     "%llu graphics allocations alive (most %llu; the device allows %llu), "
                     "%.0f MB (most %.0f MB)",
                     (unsigned long long)st.memoryLive, (unsigned long long)st.memoryPeak,
                     (unsigned long long)st.memoryLimit, (double)st.memoryLiveBytes / 1048576.0,
                     (double)st.memoryPeakBytes / 1048576.0);
        }
        if (rss >= 0) {
            fprintf(stderr, "window: memory: %s; the process holds %ld MB (most %ld MB)\n", gfx,
                    rss / 1024, (peak >= 0 ? peak : rss) / 1024);
        } else {
            fprintf(stderr, "window: memory: %s\n", gfx);
        }
    }
    perf_reset();
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
    fprintf(stderr,
            "window: %u presents, %u movie presents%s and %u game frames (%u frame numbers) in "
            "%.1f s: %.1f "
            "presented fps, %.1f game fps; %u vsyncs (%.1f Hz simulated), %u resyncs dropping "
            "%.0f ms; longest replay %.1f ms of %u; %u steps over %.0f ms; framerate %s, present "
            "%s, display %.1f Hz (%.2f presents a refresh), window %dx%d%s\n",
            s_pres.statPresents, movie - s_pres.statMovie, movieFailed, s_pres.statFrames,
            fn - s_pres.statFrameNo, sec, s_pres.statPresents / sec, s_pres.statFrames / sec,
            s_pres.statVsyncs, s_pres.statVsyncs / sec, s_pres.statResyncs,
            (double)s_pres.statDropped / 1e6, maxMs, replays, s_pres.slowSteps, s_pres.slowMs,
            ico_video_framerate_name(s_pres.framerate, fr, sizeof(fr)), rhi_present_mode_name(),
            dm != NULL ? (double)dm->refresh_rate : 0.0,
            dm != NULL && dm->refresh_rate > 1.0f ? s_pres.statPresents / sec / dm->refresh_rate
                                                  : 0.0,
            pw, ph, window_fullscreen() ? " fullscreen" : "");
    perf_drain();
    perf_log();
    s_pres.statAt = now;
    s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
    s_pres.statDropped = 0;
    s_pres.statFrameNo = fn;
    s_pres.statMovie = movie;
    s_pres.statMovieFail = movieFail;
    s_pres.slowSteps = s_pres.slowLogged = 0;
}

/* The start-up's presents, one log line each: the boot shows black (keep
   frames, then stage 1 under a full fade) until the first sign's backdrop,
   and this names any present that was not.  For game frames BOOT_LOG_FIRST
   to BOOT_LOG_LAST, and the first BOOT_LOG_AFTER_FULL after the first frame
   that is not a keep frame: the frame, its keep flag, the fade it recorded
   (1 + alpha), its snap, the present's alpha, DISPLAY at its centre and
   its quarters' centres (a readback, so only in that window; never again
   once past it) and "first" on a tick's first present. */
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
    uint8_t px[RD_DISPLAY_PROBE_POINTS][4];
    char pts[RD_DISPLAY_PROBE_POINTS * 20] = "";
    size_t n = 0;

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
    if (rd_display_probe(px)) {
        for (int k = 0; k < RD_DISPLAY_PROBE_POINTS && n < sizeof(pts); k++) {
            const int w =
                snprintf(pts + n, sizeof(pts) - n, " (%u,%u,%u)", px[k][0], px[k][1], px[k][2]);

            n += w > 0 ? (size_t)w : 0;
        }
    } else {
        snprintf(pts, sizeof(pts), " unread");
    }
    fprintf(stderr, "boot: frame %u keep %u fade %u snap %u t %.3f display%s%s\n", pi.frame,
            pi.keep, pi.fade, pi.snap, (double)alpha, pts, pi.firstOfTick ? " first" : "");
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
    fprintf(stderr,
            "window: slow step %.1f ms at vsync %u (Main tick %u, stage %d%s): step %.1f "
            "(vsync callbacks %.1f, audio %.1f, game threads %.1f in %lu switches, achievements "
            "%.1f), pump %.1f, other %.1f; %u disc reads (%u sectors), %u texture decodes, %u "
            "pipelines created, memory card %s\n",
            ms, ico_host_vsync_count(), ico_host_main_ticks(), p.stage,
            p.loading ? ", loading" : "", p.totalMs, p.hooksMs, p.audioMs, p.threadsMs, p.switches,
            p.achMs, s_pres.pumpMs, other > 0.0 ? other : 0.0, p.cdReads, p.cdSectors,
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

        s_pres.stepSumMs += ms;
        s_pres.stepMaxMs = ms > s_pres.stepMaxMs ? ms : s_pres.stepMaxMs;
        s_pres.stepCount++;
        if (s_pres.slowMs > 0.0 && ms > s_pres.slowMs) {
            slow_step(ms);
        }
    }
    pace(hz);
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
}

static void pace(int hz)
{
    /* PAL 50 Hz: 20 ms; NTSC 59.94 Hz: 16.683 ms (host_loop.c's simulated
       periods) */
    const Uint64 period = hz == 50 ? 20000000ull : 16683333ull;
    Uint64 now;

    int newFrame = 0;

    s_deadline += period;
    s_pres.statVsyncs++;
    {
        const uint32_t fn = rd_frame_number();

        if (fn != s_pres.frame) {
            newFrame = 1;
            s_pres.frame = fn;
            s_pres.tickPrev = s_pres.tickAt;
            s_pres.tickAt = s_deadline - period;
            if (!s_pollRestarted) {
                /* the first game frame (the opening scene follows it)
                   restarts the surface-size poll's every-present spell */
                s_pollRestarted = 1;
                rhi_surface_poll_restart();
            }
            s_pres.statFrames++;
        }
    }
    if (!rd_interpolation_active()) {
        /* framerate "original": rd_end_frame presented the frame once; the
           picture is held until the next.  That present's start-up log line
           (the frame whole: t 1), its readback inside the wait */
        if (newFrame) {
            boot_log(1.0f);
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
    Uint64 refresh = period;
    Uint64 gap;
    {
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
        refresh = dm != NULL && dm->refresh_rate > 1.0f ? (Uint64)(1e9 / (double)dm->refresh_rate)
                                                        : period;

        /* "uncapped" with vsync on (s_pres.mailbox: rhi_prefer_mailbox):
           a little less than one display refresh between presents, in
           mailbox mode as under FIFO, so the presents average about one a
           refresh (the log's "presents a refresh" figure).
           Two a refresh in mailbox mode drew a picture the display never
           showed for every one it did, which kept a handheld's GPU near its
           limit; a phone also lost the time for the game.  Without vsync
           "uncapped" is back to back (pace_policy.h pace_present_gap) */
        gap =
            pace_present_gap(s_pres.framerate,
                             s_pres.framerate == ICO_FRAMERATE_UNCAPPED && s_pres.mailbox, refresh);
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    for (;;) {
        now = SDL_GetTicksNS();
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        /* nor a further present of a frame already shown that would
           end past the deadline (its cost the last present's): the next
           simulation step is never late for one */
        if (s_pres.presentedFrame == s_pres.frame &&
            (now >= s_deadline || s_pres.paceSlow || now + s_pres.cost > s_deadline)) {
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        if (gap && s_pres.lastPresent && now - s_pres.lastPresent < gap) {
            const Uint64 wake = s_pres.lastPresent + gap;

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
        float a =
            rd_present_clock_alpha(&s_pres.clock, (double)now / 1e6, (double)s_pres.tickAt / 1e6,
                                   (double)tick / 1e6, (double)gap / 1e6);

        if (s_pres.legacyAlpha) {
            /* ICO_RD_S2_LEGACY=1: the raw measured time, without the clock */
            const double r =
                now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
            a = (float)(r > 0.999 ? 0.999 : r);
        }
        const Uint64 t0 = now;
        ico_diag_present_enter();
        const int ok = rd_present(a);
        ico_diag_present_leave();
        now = SDL_GetTicksNS();
        s_pres.cost = now - t0;
        if (ok) {
            /* the cost smoothed over the last presents, with hysteresis;
               a present is slower with an effects program loaded */
            s_pres.paceSlow = pace_slow_present(&s_pres.paceHist, s_pres.cost, refresh, period,
                                                rhi_injector_name() != NULL);
            /* after the cost: the start-up log's readback is not the present's */
            boot_log(a);
        }
        if (!ok) {
            /* a movie on the output, or nothing closed yet */
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        s_pres.lastPresent = t0;
        s_pres.presentedFrame = s_pres.frame;
        s_pres.statPresents++;
#ifdef __ANDROID__
        ico_gpu_driver_android_presented(); /* the driver's trial */
#endif
    }
    now = SDL_GetTicksNS();
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
#ifdef __ANDROID__
    SDL_RemoveEventWatch(lifecycle_watch, NULL);
    /* a normal end: a driver package still on trial did not crash, so the
       next start does not treat it as failed */
    ico_gpu_driver_android_end();
#endif
    set_capture(ICO_CAPTURE_OFF);
    ico_input_sdl_shutdown();
    ui_settings_set_window_mode_query(NULL);
    ui_settings_set_texture_pack_count(NULL);
    ui_settings_set_touch_query(NULL);
#ifdef __ANDROID__
    ui_settings_set_gpu_driver_host(NULL);
    ui_settings_set_quit_is_game(NULL);
#endif
    ui_touch_set_source(NULL);
    ui_host_shutdown();
    rd_set_host_call(NULL);
    rd_shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
