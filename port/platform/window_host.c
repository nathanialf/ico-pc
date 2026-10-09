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
#include "video_options.h"
#include "window_host.h"
#include "window_lifecycle.h"
#include "window_video.h"
#ifdef __ANDROID__
#include "android/host_android.h"
#endif

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull
/* Package Q1: slow-step lines a stats block logs at most (the rest are
   counted in its first line) */
#define SLOW_STEP_LINES 5

_Static_assert(SDLK_F11 == ICO_HOTKEY_KEY_F11, "hotkeys.h: SDL3's SDLK_F11");

_Static_assert(SDLK_F12 == ICO_HOTKEY_KEY_F12, "hotkeys.h: SDL3's SDLK_F12");

_Static_assert(SDL_EVENT_TERMINATING == ICO_SDL_EVENT_TERMINATING &&
                   SDL_EVENT_LOW_MEMORY == ICO_SDL_EVENT_LOW_MEMORY &&
                   SDL_EVENT_WILL_ENTER_BACKGROUND == ICO_SDL_EVENT_WILL_ENTER_BACKGROUND &&
                   SDL_EVENT_DID_ENTER_FOREGROUND == ICO_SDL_EVENT_DID_ENTER_FOREGROUND,
               "window_lifecycle.h: SDL3's lifecycle event types");

/* Package Q1: F12's dump (port/render/rd_dump.c; rd.h is outside this
   package, so it is declared here) */
bool rd_DumpOnDemand(const char *dumpPath, const char *pngPath);
/* The game's state the mouse capture follows (I17a, mouse_look.h
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

/* renderer wave 7 (R7a): the display options last applied
   (port/game/video_options.h) */
static unsigned s_videoSerial;

/* the window's real fullscreen state (SDL_WINDOW_FULLSCREEN as last read),
   not the option: the window manager can refuse a request or change it on
   its own (v0.3.1, P3) */
static int s_fullscreen;

/* v0.4.3 (I17c): the mode Alt+Enter goes back to from Windowed (the last
   non-windowed one the options or the window had; Fullscreen to begin with) */
static int s_lastFullMode = ICO_WINDOW_FULLSCREEN;

/* the options the last "display changed" line compared against (seeded
   after the startup line) */
static IcoVideoOptions s_videoLast;

/* renderer wave 7 (R7b): the presentation loop (ico_window_pace) */
static int s_pollRestarted; /* N4: the poll's restart at the first game frame, once */

static struct {
    int framerate;           /* ico_video_framerate() as last applied */
    unsigned cutSerial;      /* ico_video_cut_serial() last passed on */
    uint32_t frame;          /* rd_FrameNumber() last seen */
    uint32_t presentedFrame; /* the frame of the last present */
    Uint64 tickAt, tickPrev; /* the pace deadlines (simulated time) the last two frames closed at */
    Uint64 lastPresent;
    Uint64 cost;       /* how long the last rd_Present took */
    PaceHist paceHist; /* the last presents' costs (pace_policy.h) */
    bool paceSlow;     /* pace_SlowPresent after the last present */
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
    /* F2: simulated vsyncs paced, and the lag the pacer dropped (each time
       it was more than RESYNC_NS behind: the simulation ran slower than
       real time by that much) */
    unsigned statVsyncs, statResyncs;
    Uint64 statDropped;
    uint32_t statFrameNo; /* rd_FrameNumber() at the block's start */
    /* v0.4.0: rd_VideoPresents at the block's start; a movie presents on
       its own, outside the replays the line counts */
    uint32_t statMovie, statMovieFail;
    /* P1: the simulation step's real time (from the end of one pace to the
       start of the next), for the 10 s line */
    Uint64 paceEnd;
    double stepSumMs, stepMaxMs;
    unsigned stepCount;
    int mailbox; /* rhi_PreferMailbox as last applied: vsync on and presenting between ticks */
    /* Q1: the slow-step lines ([dev] slow_step_ms, 0 off): the threshold,
       the block's count and lines, ico_window_pump's time, the renderer's
       texture decodes and pipeline creations at the last pace's end */
    double slowMs, pumpMs;
    unsigned slowSteps, slowLogged;
    uint32_t decodes, pipeCreates;
    /* S2: the present clock alpha is taken from (rd.h RdPresentClock) */
    RdPresentClock clock;
    int legacyAlpha; /* ICO_RD_S2_LEGACY=1 (read in video_settings) */
    /* Q1: F11, the stats line every second until this time (0: off) */
    Uint64 fastUntil;
} s_pres;

/* v0.4.2 (N2): resolution "auto" (video_options.h ICO_RES_AUTO): the
   replays' costs (perf_drain) against the frame budget, a step down when
   they are too slow (pace_policy.h pace_AutoResolutionStep, auto_res_update) */
static struct {
    int active;        /* resolution "auto" in force, the CRT filter off */
    float windowScale; /* the presentation box's height over the game's 448 lines */
    int tickBudget;    /* frame rate Original: one present a game tick, so the budget is
                          the tick (two fields at the pace's hz, set by ico_window_pace) */
    PaceSamples samples;
} s_autoRes;

/* P1: the renderer's per-replay records (rd.h RdPerfRecord): summed over
   the 10 s block for the window's second line, and with [dev] perf_log =
   true written one line each into logs/ico-pc-perf.csv */
static struct {
    int csvTried;
    FILE *csv;
    unsigned n, gpuN;
    double total, maxTotal, interp, wait, acquire, upload, walk, bind, submit, present, readback,
        fence;
    double gpu, maxGpu, gpuList[13], gpuUpload, gpuPresent;
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
    rs->sceneWidth = (uint32_t)o.resW;
    rs->sceneHeight = (uint32_t)o.resH;
    rs->sceneScale = (float)o.resScale;
    /* v0.4.2 (N2): resolution "auto": the window's size (scale 0) until
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
    /* package CRT: the filter in either preset (rd_crt.c) */
    rd_CrtSettings(rs, o.crt ? (RdCrtMode)(o.crtMode + 1) : RD_CRT_OFF, o.crtStrength);
    rs->crtScanlines = o.crtScanlines;
    rs->crtMask = o.crtMask;
    rs->crtHalation = o.crtHalation;
    rs->crtBloom = o.crtBloom;
    rs->crtCurvature = o.crtCurvature;
    /* v0.4.0: texture packs and the dump, in either preset */
    rs->texturePack = (uint8_t)(o.texturePack != 0);
    /* the dump is a Developer mode row: never in force without it (a
       config.toml saved with it on and Developer mode turned off by hand) */
    rs->dumpTextures = (uint8_t)(o.dumpTextures != 0 && ico_opt_developer_mode());
    /* v0.4.1 (R1): the depth for an effects program (ReShade) */
    rs->effectsDepth = (uint8_t)(o.effectsDepth != 0);
    rs->modelPack = (uint8_t)(o.modelPack != 0);
    rs->dumpModels = (uint8_t)(o.dumpModels != 0 && ico_opt_developer_mode());
    /* R7b: rd presents between ticks, in both presets (F2) */
    {
        const char *e = getenv("ICO_RD_S2_LEGACY");

        s_pres.legacyAlpha = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    s_pres.framerate = ico_video_framerate();
    rs->interpolate = (uint8_t)(s_pres.framerate != ICO_FRAMERATE_ORIGINAL);
    /* P1: presenting between ticks with vsync on, the swapchain prefers the
       mailbox mode: no tearing, and a present never waits for the display,
       so it cannot hold the simulation back */
    s_pres.mailbox = rs->vsync && rs->interpolate;
    rhi_PreferMailbox(s_pres.mailbox != 0);
}

/* The window is fullscreen now (SDL's flag, which follows the window
   manager's answer) */
static int window_fullscreen(void)
{
    return s_window != NULL && (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

/* v0.4.3 (I17c): the window's mode now, from SDL's flags: fullscreen, a
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

/* v0.3.1 (P3): one line per change of the display options after startup,
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
   rd_SetSettings at the next frame (rd recreates the targets and the
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
    /* package AN-D: always fullscreen (immersive, the window's creation
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
        /* v0.4.2 N1: the output is the swapchain's image, which the
           backend makes at the surface's size; where that differs from the
           window's pixel size (Android, a resize not reported yet) the
           swapchain's size wins, so this call never takes back what the
           renderer followed (rd_OutputFollowed) */
        uint32_t sw = 0, sh = 0;
        if (rhi_SwapchainSize(&sw, &sh)) {
            w = (int)sw;
            h = (int)sh;
        }
    }
    const int mailbox = s_pres.mailbox;
    video_settings(&rs, w, h);
    rd_SetSettings(&rs);
    if (mailbox != s_pres.mailbox && w > 0 && h > 0) {
        /* P1: the present mode follows the frame rate option (the swapchain
           is recreated) */
        rd_ResizeOutput((uint32_t)w, (uint32_t)h);
    }
}

/* Package AN-G: the touch overlay's zones from the window's pixel size and
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

/* v0.4.2 N4: the window's pixel size the renderer was opened at or last
   given (the size event's path, window_pixel_size); read by
   window_size_recheck, which only Android needs */
#ifdef __ANDROID__
static int s_pixW, s_pixH;
#define WINDOW_PIX_NOTE(w, h) (s_pixW = (w), s_pixH = (h))
#else
#define WINDOW_PIX_NOTE(w, h) ((void)(w), (void)(h))
#endif

/* The size event's path: the renderer's output and swapchain, aspect
   "auto" and resolution "window" (R7a), the touch zones (AN-G) */
static void window_pixel_size(int w, int h)
{
    WINDOW_PIX_NOTE(w, h);
    rd_ResizeOutput((uint32_t)w, (uint32_t)h);
    ico_video_set_window(w, h);
    video_apply(1);
    touch_layout_update();
}

/* v0.4.2 N4, Android: the window's pixel size read again after the
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

/* The window's flags. Android (package AN-D): fullscreen always, which is
   immersive there (no status or navigation bar), and not resizable; the
   system decides the size. */
static SDL_WindowFlags window_flags(int mode)
{
    const SDL_WindowFlags vk = rhi_Backend() == RHI_BACKEND_VULKAN ? SDL_WINDOW_VULKAN : 0;

#ifdef __ANDROID__
    (void)mode;
    return vk | SDL_WINDOW_FULLSCREEN;
#else
    return vk | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
           (mode == ICO_WINDOW_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0) |
           (mode == ICO_WINDOW_BORDERLESS ? SDL_WINDOW_BORDERLESS : 0);
#endif
}

/* --- package AN-D: the app's lifecycle (window_lifecycle.h) ---------------
   The operations the tables run; each is called on the thread pumping the
   events, between two frames. */
static void lc_gpu_wait_idle(void *u)
{
    (void)u;
    rhi_WaitIdle();
}

static void lc_surface_release(void *u)
{
    (void)u;
    rhi_ReleaseSurface();
}

static int lc_surface_recreate(void *u)
{
    (void)u;
    return rhi_RecreateSurface(s_window) ? 0 : -1;
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
    texpack_LowMemory();
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

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    /* [video] backend (config.toml) or backend= (ico-pc.ini): "vulkan", the
       default, or "d3d12" where the build has it */
    const char *backend = ico_config_get_string("video.backend", "vulkan");
    if (!rhi_CreateBackend(backend)) {
        fprintf(stderr, "window: renderer backend \"%s\" is not in this build; using vulkan\n",
                backend);
        backend = "vulkan";
        rhi_CreateBackend(backend);
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "window: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    IcoVideoOptions start;

    ico_video_get(&start);
    s_videoSerial = ico_video_serial();
    /* v0.3.1 (P3): fullscreen from the config is asked for at creation, not
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
    rd_SetHostCall(ico_sched_call_on_host);
    /* the Vulkan pipeline cache in the per-user folder: a later start
       creates the renderer's pipelines from it (rhi_SetPipelineCachePath;
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
        rhi_SetPipelineCachePath(cache);
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
    WINDOW_PIX_NOTE(w, h); /* v0.4.2 N4: window_size_recheck compares against it */
    video_settings(&rs, w, h);
    if (!rd_Init(gsW, gsH, &rs, s_window)) {
        fprintf(stderr,
                "window: no usable %s device (rd_Init failed; the rhi lines above say "
                "why)\n",
                backend);
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return -1;
    }
    /* the whole reachable pipeline set before the first frame, so the
       game never waits on a pipeline compile (F2; the time is logged).  P1:
       after rd_Init, which makes the device: F2 called it before, where it
       created nothing, and every pipeline was compiled by the replay that
       first drew with it (tens of ms each on a GPU driver) */
    rd_PrecreatePipelines();
    {
        IcoVideoOptions o;
        char res[32], fr[16];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height, framerate %s), vsync %s, texture pack %s, "
                "dump textures %s, model pack %s, dump models %s, depth %s\n",
                w, h, s_fullscreen ? " fullscreen" : "",
                rhi_Backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan", rhi_AdapterName(),
                video_preset_label(&o), ico_video_resolution_name(&o, res, sizeof(res)),
                ico_video_aspect_name(o.aspect), ico_video_filter_name(o.filter),
                o.fullHeight ? "full" : "half",
                ico_video_framerate_name(ico_video_framerate(), fr, sizeof(fr)),
                o.vsync ? "on" : "off", o.texturePack ? "on" : "off",
                o.dumpTextures && ico_opt_developer_mode() ? "on" : "off",
                o.modelPack ? "on" : "off", o.dumpModels && ico_opt_developer_mode() ? "on" : "off",
                rhi_Limits()->depthStencilFormatName);
        fprintf(stderr, "window: present mode %s%s\n", rhi_PresentModeName(),
                rhi_PresentMailbox() ? " (vsync without waiting on the display)" : "");
        /* v0.4.1 (R0): a program hooking the graphics API, once */
        ico_diag_set_effects_program(rhi_InjectorName() != NULL);
        if (rhi_InjectorName() != NULL) {
            fprintf(stderr, "window: an effects program is loaded (%s); see docs/RESHADE.md\n",
                    rhi_InjectorName());
        }
        if (rhi_OverlayName() != NULL) {
            fprintf(stderr, "window: an overlay is loaded (%s)\n", rhi_OverlayName());
        }
        /* v0.3.1 (P3): later changes are logged against this */
        s_videoLast = o;
    }
    /* v0.4.2 N4: the size may have changed while the renderer opened */
    window_size_recheck("after the renderer opened");
    s_pres.cutSerial = ico_video_cut_serial();
    /* Q1: [dev] slow_step_ms (default 8; 0 off) */
    s_pres.slowMs = ico_config_get_float("dev.slow_step_ms", 8.0);
    s_deadline = SDL_GetTicksNS();
    s_open = 1;
    /* Phase 6 (6B): the port's runtime text and popups (port/ui) */
    ui_HostInit();
    /* v0.3.1 (P3): the Window mode row shows what the window is, not the
       option (the window manager can refuse or change it) */
    ui_SettingsSetWindowModeQuery(window_mode_now);
    /* v0.4.0: Display > Texture pack says "None installed" without one */
    ui_SettingsSetTexturePackCount(texpack_Count);
    {
        char dir[ICO_PATH_MAX], path[ICO_PATH_MAX];

        ico_host_pref_dir(dir, sizeof(dir));
        ico_path_join(path, sizeof(path), dir, "config.toml");
        ico_input_sdl_init(path);
    }
    /* package AN-G: the touch overlay's zones, its Settings rows (with a
       touch screen) and the copy its drawing reads (port/ui/touch_ui.h) */
    touch_layout_update();
    ui_SettingsSetTouchQuery(ico_input_sdl_touch_present);
    ui_TouchSetSource(ico_input_sdl_touch_overlay);
    /* package AN-D: SDL delivers the app's lifecycle only to a watch, as
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

/* I17a: mode is ICO_CAPTURE_* (mouse_look.h): STICK and DELTA hide and
   hold the pointer (relative mode), OFF frees it; the device layer routes
   the motion by mode */
static void set_capture(int mode)
{
#ifdef __ANDROID__
    /* package AN-D: no mouse to capture; touches and pads drive the
       camera */
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

/* I17a: the game's state for the capture rule, once a pump */
static int capture_mode(void)
{
    const IcoBindings *b = ico_input_live_bindings();
    IcoCaptureState c;

    memset(&c, 0, sizeof(c));
    c.focus = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    c.look = b->mouse_on && b->mouse_camera;
    c.photo = ico_photo_active();
    c.boy = boyGObj != NULL;
    c.stage = stage_no;
    c.layout = current_layout_id;
    c.paused = systemStatus[5] != 0;
    c.loading = data_loading != 0;
    c.movie = mpegPlay != 0;
    c.viewer = ico_mv_active != 0;
    c.credits = ico_credits_active();
    return ico_mouse_capture_rule(&c);
}

static void toggle_fullscreen(void)
{
    /* the window mode option flips between Windowed and the last other mode
       (in memory, not saved; the Settings menu sees it): video_apply asks
       SDL and the presenter boxes the picture (rd_ResizeOutput follows) */
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

/* B3: the renderer's device was removed, reset or hung (the driver
   crashed or was updated, the GPU was unplugged): nothing more can be drawn,
   so instead of a frozen window the player gets one message and the session
   ends through the normal quit path (atexit closes the window). The backend
   logged the reason. */
static int device_lost_quit(void)
{
    if (!rhi_DeviceLost()) {
        return 0;
    }
    fprintf(stderr, "window: the graphics device was lost; quitting\n");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC",
                             "The graphics card stopped responding (its driver was reset or "
                             "updated, or the card was removed).\n\nThe game has to close. "
                             "Your last save is kept; logs/ico-pc.log says why.",
                             s_window);
    return 1;
}

/* Q1: F12, the frame on the screen as an rd dump and a PNG in
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
    const int ok = rd_DumpOnDemand(dump, png);
    /* the recording up to this moment, for the same report */
    ico_input_record_flush();
    fprintf(stderr, "window: F12 at vsync %u, Main tick %u, frame %u: %s %s and %s\n",
            ico_host_vsync_count(), ico_host_main_ticks(), (unsigned)rd_FrameNumber(),
            ok ? "wrote" : "could not write all of", dump, png);
}

/* Package PHOTO: Cross in photo mode,
   the picture shown at the next present into
   <pref>/<[photo] png_dir>/ico-<time>.png (rd_CapturePresented); the
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
        snprintf(lastName, sizeof(lastName), "%s", name);
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
        ui_PhotoCaptureDone(0, name);
        return;
    }
    if (!rd_CapturePresented(path)) {
        fprintf(stderr, "photo: the capture could not be armed (%s)\n", path);
        ui_PhotoCaptureDone(0, name);
    }
}

/* Package PHOTO: Cross in photo mode arms a capture of the next present
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
        const int r = rd_CaptureResult(path, sizeof(path));

        if (r != 0) {
            const char *base = strrchr(path, '/');
#ifdef _WIN32
            const char *b2 = strrchr(path, '\\');

            base = b2 != NULL && (base == NULL || b2 > base) ? b2 : base;
#endif
            ui_PhotoCaptureDone(r > 0, base != NULL ? base + 1 : path);
        }
    }
}

/* Q1: F11, the stats lines every second for 30 s */
static void stats_fast_toggle(void)
{
    s_pres.fastUntil = ico_stats_fast_toggle(SDL_GetTicksNS(), s_pres.fastUntil);
    fprintf(stderr, "window: F11: the stats lines every %s\n",
            s_pres.fastUntil ? "second for 30 s" : "10 s again");
}

/* v0.3.1 (P3): the window entered or left fullscreen, at our request, at
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
    /* v0.4.3 (I17c): fullscreen entered makes the option Fullscreen; left
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
    int quit = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (device_lost_quit()) {
        return 0;
    }

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ico_hotkey_for(e.key.key, e.key.repeat) == ICO_HOTKEY_FRAME_DUMP) {
                frame_dump();
            } else if (ico_hotkey_for(e.key.key, e.key.repeat) == ICO_HOTKEY_STATS_FAST) {
                stats_fast_toggle();
            } else if (e.key.key == SDLK_F11 || e.key.key == SDLK_F12) {
                /* a held key's repeats: nothing */
#ifndef __ANDROID__
                /* Android (package AN-D): Back is the pause menu, where Quit
                   is; an Escape from a keyboard is an unbound key there */
            } else if (e.key.key == SDLK_ESCAPE) {
                quit = 1;
#endif
            } else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) &&
                       (e.key.mod & SDL_KMOD_ALT) != 0) {
                if (!e.key.repeat) {
                    toggle_fullscreen();
                }
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
            /* v0.4.3 (I17c): a frameless window follows its display */
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
            print = 1; /* v0.4.2 N4: every change (no drags there) */
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
                /* R7a: aspect "auto" and resolution "window" follow the
                   size; AN-G: the touch zones */
                window_pixel_size(e.window.data1, e.window.data2);
            } else {
                touch_layout_update(); /* AN-G: the size and the safe area */
            }
            break;
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            touch_layout_update(); /* AN-G: a notch or the system bars moved */
            break;
        default:
            ico_input_sdl_event(&e);
            break;
        }
    }
    first_pump_recheck(); /* v0.4.2 N4 */
    /* R7a: the Settings menu's changes; v0.4.2 N1: forced when the
       renderer's output followed a swapchain rebuilt at another size, so
       aspect "auto" and resolution "window" follow it as on a resize */
    video_apply(rd_OutputFollowed(NULL, NULL) ? 1 : 0);
    /* R7b: a hard camera cut or stage change the game signalled during the
       step that just ran: the frame it recorded is not blended from the
       one before (the step closes the previous frame before the game's
       threads run, so the open frame is the one the cut belongs to) */
    if (ico_video_cut_serial() != s_pres.cutSerial) {
        s_pres.cutSerial = ico_video_cut_serial();
        rd_CameraCut();
    }
    photo_pump();                /* package PHOTO */
    set_capture(capture_mode()); /* I17a: play and photo mode */
    ico_input_sdl_update();
    /* Phase 6 (6B): the popups' clock; the presenter draws them on its
       overlay at each present (package OV, port/ui/ui_host.c) */
    ui_HostVsync(ico_host_main_ticks());
    s_pres.pumpMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    return !quit;
}

/* --- package AN-C: the first start's progress, in the game's window ----- */

typedef struct ProgressView {
    const char *title;
    const char *phase;
    int pct;
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

    ui_BeginOverlay(ctx);
    if (v->title != NULL) {
        ui_DrawText(UI_GRID_CX, 196.0f, 30.0f, title, v->title,
                    UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    }
    if (v->phase != NULL) {
        if (v->pct >= 0) {
            snprintf(line, sizeof(line), "%s: %d%%", v->phase, v->pct > 100 ? 100 : v->pct);
        } else {
            snprintf(line, sizeof(line), "%s", v->phase);
        }
        ui_DrawText(UI_GRID_CX, 232.0f, 20.0f, body, line, UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    }
    if (v->pct >= 0) {
        const float x0 = 120.0f, x1 = 520.0f, y0 = 250.0f, y1 = 266.0f;
        const int pct = v->pct > 100 ? 100 : v->pct;

        ui_DrawRect(x0, y0, x1, y1, frame);
        ui_DrawRect(x0 + 2.0f, y0 + 2.0f, x1 - 2.0f, y1 - 2.0f, track);
        ui_DrawRect(x0 + 2.0f, y0 + 2.0f, x0 + 2.0f + (x1 - x0 - 4.0f) * (float)pct / 100.0f,
                    y1 - 2.0f, fill);
#ifdef __ANDROID__
        ui_DrawText(UI_GRID_CX, 300.0f, 16.0f, body,
                    "Press Back to stop (you can start again later)",
                    UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
#endif
    }
    ui_EndOverlay();
}

int ico_window_progress(const char *title, const char *phase, int pct)
{
    static int s_cancelLogged;
    SDL_Event e;
    int cancel = 0;
    void *prevUser = NULL;
    RdOverlayFn prev;
    ProgressView v;

    if (!s_open) {
        return 0;
    }
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            cancel = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            /* Android's Back (SDL_HINT_ANDROID_TRAP_BACK_BUTTON), Escape */
            if ((e.key.key == SDLK_AC_BACK || e.key.key == SDLK_ESCAPE) && !e.key.repeat) {
                cancel = 1;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (e.window.data1 > 0 && e.window.data2 > 0) {
                fprintf(stderr, "window: %dx%d pixels\n", e.window.data1, e.window.data2);
                window_pixel_size(e.window.data1, e.window.data2);
            }
            break;
        default:
            /* the pads plugged in at the start, and the rest: as the game's
               pump would pass them on */
            ico_input_sdl_event(&e);
            break;
        }
    }
    first_pump_recheck(); /* v0.4.2 N4 */
    if (device_lost_quit()) {
        return 1;
    }
    if (cancel && !s_cancelLogged) {
        s_cancelLogged = 1;
        fprintf(stderr, "window: stop asked during \"%s\"\n", phase != NULL ? phase : "");
    }
    v.title = title;
    v.phase = phase;
    v.pct = pct;
    prev = rd_GetPresentOverlay(&prevUser);
    rd_SetPresentOverlay(progress_overlay, &v);
    rd_PresentBlank();
    rd_SetPresentOverlay(prev, prevUser);
    return cancel;
}

/* P1: logs/ico-pc-perf.csv, opened on the first record when [dev] perf_log
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
    for (int l = 0; l < 13; l++) {
        fprintf(s_perf.csv, ",gpu_list%d_ms", l);
    }
    fprintf(s_perf.csv, ",gpu_present_ms,start_ms,alpha,first_of_tick\n");
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
    for (int l = 0; l < 13; l++) {
        fprintf(f, ",%.3f", r->gpuListMs[l]);
    }
    fprintf(f, ",%.3f,%.3f,%.4f,%u\n", r->gpuPresentMs, r->startMs, (double)r->alpha,
            r->firstOfTick);
}

/* P1: the finished records into the block's sums and the CSV */
/* v0.4.2 (N2): a presented replay's cost for resolution "auto": its GPU
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
    } else if (rhi_PresentMailbox()) {
        ms = r->totalMs - r->acquireMs - r->presentMs;
    } else {
        return;
    }
    if (s_autoRes.samples.count && now - s_autoRes.samples.lastNs > 500000000ull) {
        pace_SamplesReset(&s_autoRes.samples, now);
    }
    pace_SamplesAdd(&s_autoRes.samples, now, ms > 0.0 ? (Uint64)(ms * 1e6) : 0);
}

static void perf_drain(void)
{
    RdPerfRecord r;

    while (rd_PerfPop(&r)) {
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
            for (int l = 0; l < 13; l++) {
                s_perf.gpuList[l] += r.gpuListMs[l];
            }
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

/* P1: a new block's sums (the CSV stays open) */
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

/* P1: the block's second line: where a replay's time went, on average */
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
        /* v0.4.2: the graphics allocations against the device's limit (a
           phone GPU allows 4096) and the process's memory (a phone ends a
           process that holds too much without a word) */
        RhiStats st;
        long rss = -1, peak = -1;

        char gfx[160] = "graphics allocations not counted";

        rhi_GetStats(&st);
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

/* R7b: presents and frames per second, every 10 s of real time, in both
   presentation modes. F2: "game frames" counts the frames shown (closed
   frames the pace saw); "frame numbers" also counts the ones the game
   dropped unshown (rd_DiscardFrame: fbKeep screens, gsb_UpdateGSSystem(1)),
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
        s_pres.statFrameNo = rd_FrameNumber();
        s_pres.statMovie = rd_VideoPresents(&s_pres.statMovieFail);
        rd_ReplayTimeMax(1, &n);
        perf_drain();
        perf_reset();
        return;
    }
    if (now - s_pres.statAt < ico_stats_period_ns(now, s_pres.fastUntil)) {
        return;
    }
    const double sec = (double)(now - s_pres.statAt) / 1e9;
    uint32_t replays = 0;
    const double maxMs = rd_ReplayTimeMax(1, &replays);
    const uint32_t fn = rd_FrameNumber();
    uint32_t movieFail;
    const uint32_t movie = rd_VideoPresents(&movieFail);
    char movieFailed[32] = "";

    if (!rd_InterpolationActive()) {
        s_pres.statPresents = replays; /* one present per replay */
    }
    /* v0.3.1 (P3): what the presents went to: the frame rate option, the
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
            "%s, display %.1f Hz, window %dx%d%s\n",
            s_pres.statPresents, movie - s_pres.statMovie, movieFailed, s_pres.statFrames,
            fn - s_pres.statFrameNo, sec, s_pres.statPresents / sec, s_pres.statFrames / sec,
            s_pres.statVsyncs, s_pres.statVsyncs / sec, s_pres.statResyncs,
            (double)s_pres.statDropped / 1e6, maxMs, replays, s_pres.slowSteps, s_pres.slowMs,
            ico_video_framerate_name(s_pres.framerate, fr, sizeof(fr)), rhi_PresentModeName(),
            dm != NULL ? (double)dm->refresh_rate : 0.0, pw, ph,
            window_fullscreen() ? " fullscreen" : "");
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

/* The pacer more than RESYNC_NS behind: the lag is dropped (counted) */
static void resync(Uint64 now)
{
    s_pres.statResyncs++;
    s_pres.statDropped += now - s_deadline;
    s_deadline = now;
}

static void pace(int hz);

/* Q1: a step (ico_host_step to this pace) over [dev] slow_step_ms: what ran
   in it.  "other" is the host loop's work between the step and the pump:
   the trace line, the pad recording and the log flush. */
static void slow_step(double ms)
{
    IcoStepProfile p;
    const RdTexCacheStats *tc = rdtex_Stats();
    const RdStats *rs = rd_GetStats();

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

/* v0.4.2 (N2): every 2 s of samples, resolution "auto"'s step (one down at
   most, never up), applied as an option change would be */
static void auto_res_update(void)
{
    PaceSamples *s = &s_autoRes.samples;

    if (!s_autoRes.active || s->lastNs - s->firstNs < PACE_AUTO_WINDOW_NS) {
        return;
    }
    const int cur = ico_video_auto_scale();
    const int next = pace_AutoResolutionStep(s, cur, s_autoRes.windowScale);

    if (next != cur) {
        fprintf(stderr, "video: resolution auto -> %dx (presents took %.1f ms)\n", next,
                (double)pace_SamplesMedian(s) / 1e6);
        ico_video_set_auto_scale(next);
        s->lastStepNs = s->lastNs;
        video_apply(1);
    }
    pace_SamplesReset(s, s->lastNs);
}

void ico_window_pace(int hz)
{
    /* P1: the simulation step that ran since the last pace (the host loop
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
    perf_drain(); /* P1: every vsync, so the record queue never overflows */
    if (s_autoRes.tickBudget) {
        /* two fields a tick: 40 ms PAL, 33.4 ms NTSC (pace's periods) */
        s_autoRes.samples.budgetNs = 2u * (hz == 50 ? 20000000ull : 16683333ull);
    }
    auto_res_update();
    {
        const RdTexCacheStats *tc = rdtex_Stats();
        const RdStats *rs = rd_GetStats();

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

    s_deadline += period;
    s_pres.statVsyncs++;
    {
        const uint32_t fn = rd_FrameNumber();

        if (fn != s_pres.frame) {
            s_pres.frame = fn;
            s_pres.tickPrev = s_pres.tickAt;
            s_pres.tickAt = s_deadline - period;
            if (!s_pollRestarted) {
                /* N4: the first game frame (the opening scene follows it)
                   restarts the surface-size poll's every-present spell */
                s_pollRestarted = 1;
                rhi_SurfacePollRestart();
            }
            s_pres.statFrames++;
        }
    }
    if (!rd_InterpolationActive()) {
        /* framerate "original": rd_EndFrame presented the frame once; the
           picture is held until the next */
        now = SDL_GetTicksNS();
        if (now < s_deadline) {
            SDL_DelayPrecise(s_deadline - now);
        } else if (now - s_deadline > RESYNC_NS) {
            resync(now);
        }
        pace_log(SDL_GetTicksNS());
        return;
    }
    /* R7b: the simulation keeps its vsync cadence in simulated time; until
       this vsync's deadline the window presents as often as vsync (and the
       framerate cap) allow, each present at alpha = the real time since the
       last frame closed over the tick, between the last two frames (one
       tick of latency). A frame closes inside the step before this call:
       its time is the start of this vsync period. */
    /* F2: a present with vsync on blocks up to one display refresh; that
       wait is not the renderer being slow. The user's first Windows run
       (60 Hz display, 59.94 Hz simulation: refresh 16.67 ms against the
       16.68 ms period) sat on that edge and fell back to one present per
       game frame. Slow means a present costing more than a display refresh
       plus half a simulated period (pace_policy.h: the cost is smoothed, with
       hysteresis, and the limit is higher with an effects program loaded). */
    Uint64 refresh = period;
    Uint64 gap = s_pres.framerate > 0 ? 1000000000ull / (Uint64)s_pres.framerate : 0;
    {
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
        refresh = dm != NULL && dm->refresh_rate > 1.0f ? (Uint64)(1e9 / (double)dm->refresh_rate)
                                                        : period;

        /* P1: "uncapped" with vsync on.  In mailbox mode (rhi_PreferMailbox)
           a present never waits for the display; two presents a refresh keep
           every refresh supplied with a fresh picture without drawing many
           that are never shown.  Under FIFO (no mailbox) one a refresh: the
           display's own rate, so a present rarely finds the queue full and
           waits.  Without vsync "uncapped" is back to back. */
        if (s_pres.framerate == ICO_FRAMERATE_UNCAPPED && s_pres.mailbox) {
            gap = rhi_PresentMailbox() ? refresh / 2 : refresh - refresh / 16;
#ifdef __ANDROID__
            /* v0.4.2 (N2): one a refresh in mailbox mode too: two full
               replays a refresh on the thread that also runs the game left
               a phone too little time for the game and kept its GPU busy */
            if (rhi_PresentMailbox()) {
                gap = refresh;
            }
#endif
        }
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    for (;;) {
        now = SDL_GetTicksNS();
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        /* P1: nor a further present of a frame already shown that would
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
        /* S2: alpha from the present clock (the measured time smoothed
           to the presents' steady cadence) against the simulated time the
           frame closed; the raw measured time jittered with the step and
           the sleeps before the present */
        float a =
            rd_PresentClockAlpha(&s_pres.clock, (double)now / 1e6, (double)s_pres.tickAt / 1e6,
                                 (double)tick / 1e6, (double)gap / 1e6);

        if (s_pres.legacyAlpha) {
            /* ICO_RD_S2_LEGACY=1: the measured time, as before S2 */
            const double r =
                now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
            a = (float)(r > 0.999 ? 0.999 : r);
        }
        const Uint64 t0 = now;
        ico_diag_present_enter();
        const int ok = rd_Present(a);
        ico_diag_present_leave();
        now = SDL_GetTicksNS();
        s_pres.cost = now - t0;
        if (ok) {
            /* R2: the cost smoothed over the last presents, with hysteresis;
               a present is slower with an effects program loaded */
            s_pres.paceSlow = pace_SlowPresent(&s_pres.paceHist, s_pres.cost, refresh, period,
                                               rhi_InjectorName() != NULL);
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
#endif
    set_capture(0);
    ico_input_sdl_shutdown();
    ui_SettingsSetWindowModeQuery(NULL);
    ui_SettingsSetTexturePackCount(NULL);
    ui_SettingsSetTouchQuery(NULL);
    ui_TouchSetSource(NULL);
    ui_HostShutdown();
    rd_SetHostCall(NULL);
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
