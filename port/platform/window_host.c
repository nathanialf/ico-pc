/*
 * port/platform/window_host.c
 *
 * The window, the renderer device on it and the real-time pacing
 * (window_host.h).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "host_config.h"
#include "input_sdl.h"
#include "rd.h"
#include "rhi.h"
#include "sched.h"
#include "trace_host.h"
#include "ui_host.h"
#include "video_options.h"
#include "window_host.h"

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull

/* The game's state the mouse capture follows (common/include/main.h): the
   boy exists in a stage, and the game is neither paused nor loading. */
extern void *boyGObj;
extern int game_pause;
extern int data_loading;

static SDL_Window *s_window;

static int s_captured;

static Uint64 s_deadline;

static int s_open;

/* renderer wave 7 (R7a): the display options last applied
   (port/game/video_options.h, docs/port/DISPLAY.md) */
static unsigned s_videoSerial;

static int s_fullscreen;

/* renderer wave 7 (R7b): the presentation loop (ico_window_pace) */
static struct {
    int framerate;           /* ico_video_framerate() as last applied */
    unsigned cutSerial;      /* ico_video_cut_serial() last passed on */
    uint32_t frame;          /* rd_FrameNumber() last seen */
    uint32_t presentedFrame; /* the frame of the last present */
    Uint64 tickAt, tickPrev; /* real time the last two frames closed */
    Uint64 lastPresent;
    Uint64 cost; /* how long the last rd_Present took */
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
    /* F2: simulated vsyncs paced, and the lag the pacer dropped (each time
       it was more than RESYNC_NS behind: the simulation ran slower than
       real time by that much) */
    unsigned statVsyncs, statResyncs;
    Uint64 statDropped;
    uint32_t statFrameNo; /* rd_FrameNumber() at the block's start */
} s_pres;

/* The renderer's settings from the display options and the window's pixel
   size. */
static void video_settings(RdSettings *rs, int w, int h)
{
    IcoVideoOptions o;

    ico_video_get(&o);
    ico_video_set_window(w, h);
    memset(rs, 0, sizeof(*rs));
    rs->preset = o.preset == ICO_VIDEO_ENHANCED ? RD_PRESET_ENHANCED : RD_PRESET_ORIGINAL;
    rs->outputWidth = (uint32_t)(w > 0 ? w : WINDOW_W);
    rs->outputHeight = (uint32_t)(h > 0 ? h : WINDOW_H);
    rs->aspect = ico_video_aspect();
    rs->vsync = (uint8_t)(o.vsync != 0);
    rs->filterUpgrade = (uint8_t)o.filter;
    rs->fullHeightScene = (uint8_t)(o.fullHeight != 0);
    rs->sceneWidth = (uint32_t)o.resW;
    rs->sceneHeight = (uint32_t)o.resH;
    rs->sceneScale = (float)o.resScale;
    /* R7b: rd presents between ticks, in both presets (F2) */
    s_pres.framerate = ico_video_framerate();
    rs->interpolate = (uint8_t)(s_pres.framerate != ICO_FRAMERATE_ORIGINAL);
}

/* Applies the options changed since the last call (the Settings menu's
   ico_video_set, Alt+Enter; force: a resize, for aspect "auto" and
   resolution "window"): fullscreen through SDL, the rest through
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
    if (o.fullscreen != s_fullscreen) {
        /* no mode set: SDL's borderless fullscreen at the desktop
           resolution; the resize event follows */
        SDL_SetWindowFullscreen(s_window, o.fullscreen != 0);
        s_fullscreen = o.fullscreen;
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    video_settings(&rs, w, h);
    rd_SetSettings(&rs);
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
    s_window = SDL_CreateWindow("ICO", WINDOW_W, WINDOW_H,
                                (rhi_Backend() == RHI_BACKEND_VULKAN ? SDL_WINDOW_VULKAN : 0) |
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (s_window == NULL) {
        fprintf(stderr, "window: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    /* the replay, the present and the FMV picture run on the host stack in
       the host FP mode, not on the game's 256 KB fiber stacks
       (docs/port/PLATFORM.md "Fiber stacks and host calls") */
    rd_SetHostCall(ico_sched_call_on_host);
    /* the whole reachable pipeline set before the first frame, so the
       game never waits on a pipeline compile (F2; the time is logged) */
    rd_PrecreatePipelines();
    {
        IcoVideoOptions o;

        ico_video_get(&o);
        s_videoSerial = ico_video_serial();
        if (o.fullscreen) {
            SDL_SetWindowFullscreen(s_window, true);
            SDL_SyncWindow(s_window);
            s_fullscreen = 1;
        }
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
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
    {
        IcoVideoOptions o;
        char res[32], fr[16];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height, framerate %s), vsync %s\n",
                w, h, o.fullscreen ? " fullscreen" : "",
                rhi_Backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan", rhi_AdapterName(),
                o.preset == ICO_VIDEO_ENHANCED ? "Enhanced" : "Original",
                ico_video_resolution_name(&o, res, sizeof(res)), ico_video_aspect_name(o.aspect),
                ico_video_filter_name(o.filter), o.fullHeight ? "full" : "half",
                ico_video_framerate_name(ico_video_framerate(), fr, sizeof(fr)),
                o.vsync ? "on" : "off");
    }
    s_pres.cutSerial = ico_video_cut_serial();
    s_deadline = SDL_GetTicksNS();
    s_open = 1;
    /* Phase 6 (6B): the port's runtime text and popups (port/ui) */
    ui_HostInit();
    {
        char dir[ICO_PATH_MAX], path[ICO_PATH_MAX];

        ico_host_pref_dir(dir, sizeof(dir));
        ico_path_join(path, sizeof(path), dir, "config.toml");
        ico_input_sdl_init(path);
    }
    return 0;
}

static void set_capture(int want)
{
    if (want == s_captured) {
        return;
    }
    s_captured = want;
    SDL_SetWindowRelativeMouseMode(s_window, want != 0);
    ico_input_sdl_set_capture(want);
}

static void toggle_fullscreen(void)
{
    /* the fullscreen option flips (in memory, not saved; the Settings menu
       sees it): video_apply sets SDL's borderless fullscreen at the desktop
       resolution and the presenter boxes the picture (rd_ResizeOutput
       follows) */
    IcoVideoOptions o;

    ico_video_get(&o);
    o.fullscreen = !s_fullscreen;
    ico_video_set(&o);
    video_apply(0);
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
                             "The graphics device stopped responding (the driver was reset or "
                             "the GPU was removed).\n\nThe game has to close. Your last save on "
                             "the memory card is kept; logs/ico-pc.log names the reason.",
                             s_window);
    return 1;
}

int ico_window_pump(void)
{
    SDL_Event e;
    int quit = 0;

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
            if (e.key.key == SDLK_ESCAPE) {
                quit = 1;
            } else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) &&
                       (e.key.mod & SDL_KMOD_ALT) != 0) {
                if (!e.key.repeat) {
                    toggle_fullscreen();
                }
            } else {
                ico_input_sdl_event(&e);
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (e.window.data1 > 0 && e.window.data2 > 0) {
                rd_ResizeOutput((uint32_t)e.window.data1, (uint32_t)e.window.data2);
                /* R7a: aspect "auto" and resolution "window" follow the size */
                ico_video_set_window(e.window.data1, e.window.data2);
                video_apply(1);
            }
            break;
        default:
            ico_input_sdl_event(&e);
            break;
        }
    }
    video_apply(0); /* R7a: the Settings menu's changes */
    /* R7b: a hard camera cut or stage change the game signalled during the
       step that just ran: the frame it recorded is not blended from the
       one before (the step closes the previous frame before the game's
       threads run, so the open frame is the one the cut belongs to) */
    if (ico_video_cut_serial() != s_pres.cutSerial) {
        s_pres.cutSerial = ico_video_cut_serial();
        rd_CameraCut();
    }
    set_capture((SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0 && boyGObj != NULL &&
                game_pause == 0 && data_loading == 0);
    ico_input_sdl_update();
    /* Phase 6 (6B): the popup overlay's clock, and the popup recorded into
       the open frame once per game frame.  rd presents inside rd_EndFrame
       and has no post-present overlay hook yet, so the popup is drawn into
       the frame's list 12 (port/ui/popup.h, docs/port/UI.md) */
    ui_HostVsync(ico_host_main_ticks());
    return !quit;
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
        rd_ReplayTimeMax(1, &n);
        return;
    }
    if (now - s_pres.statAt < 10000000000ull) {
        return;
    }
    const double sec = (double)(now - s_pres.statAt) / 1e9;
    uint32_t replays = 0;
    const double maxMs = rd_ReplayTimeMax(1, &replays);
    const uint32_t fn = rd_FrameNumber();

    if (!rd_InterpolationActive()) {
        s_pres.statPresents = replays; /* one present per replay */
    }
    fprintf(stderr,
            "window: %u presents and %u game frames (%u frame numbers) in %.1f s: %.1f "
            "presented fps, %.1f game fps; %u vsyncs (%.1f Hz simulated), %u resyncs dropping "
            "%.0f ms; longest replay %.1f ms of %u\n",
            s_pres.statPresents, s_pres.statFrames, fn - s_pres.statFrameNo, sec,
            s_pres.statPresents / sec, s_pres.statFrames / sec, s_pres.statVsyncs,
            s_pres.statVsyncs / sec, s_pres.statResyncs, (double)s_pres.statDropped / 1e6, maxMs,
            replays);
    s_pres.statAt = now;
    s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
    s_pres.statDropped = 0;
    s_pres.statFrameNo = fn;
}

/* The pacer more than RESYNC_NS behind: the lag is dropped (counted) */
static void resync(Uint64 now)
{
    s_pres.statResyncs++;
    s_pres.statDropped += now - s_deadline;
    s_deadline = now;
}

void ico_window_pace(int hz)
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
       plus half a simulated period. */
    Uint64 slow = period;
    {
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
        const Uint64 refresh = dm != NULL && dm->refresh_rate > 1.0f
                                   ? (Uint64)(1e9 / (double)dm->refresh_rate)
                                   : period;

        slow = (refresh > period ? refresh : period) + period / 2;
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    const Uint64 gap = s_pres.framerate > 0 ? 1000000000ull / (Uint64)s_pres.framerate : 0;
    for (;;) {
        now = SDL_GetTicksNS();
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        if (s_pres.presentedFrame == s_pres.frame && (now >= s_deadline || s_pres.cost > slow)) {
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
        double a = now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
        a = a > 0.999 ? 0.999 : a;
        const Uint64 t0 = now;
        const int ok = rd_Present((float)a);
        now = SDL_GetTicksNS();
        s_pres.cost = now - t0;
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
    set_capture(0);
    ico_input_sdl_shutdown();
    ui_HostShutdown();
    rd_SetHostCall(NULL);
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
