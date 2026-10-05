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
        char res[32];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height), vsync %s\n",
                w, h, o.fullscreen ? " fullscreen" : "",
                rhi_Backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan", rhi_AdapterName(),
                o.preset == ICO_VIDEO_ENHANCED ? "Enhanced" : "Original",
                ico_video_resolution_name(&o, res, sizeof(res)), ico_video_aspect_name(o.aspect),
                ico_video_filter_name(o.filter), o.fullHeight ? "full" : "half",
                o.vsync ? "on" : "off");
    }
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

int ico_window_pump(void)
{
    SDL_Event e;
    int quit = 0;

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

void ico_window_pace(int hz)
{
    /* PAL 50 Hz: 20 ms; NTSC 59.94 Hz: 16.683 ms (host_loop.c's simulated
       periods) */
    const Uint64 period = hz == 50 ? 20000000ull : 16683333ull;
    Uint64 now;

    s_deadline += period;
    now = SDL_GetTicksNS();
    if (now < s_deadline) {
        SDL_DelayPrecise(s_deadline - now);
    } else if (now - s_deadline > RESYNC_NS) {
        s_deadline = now;
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
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
