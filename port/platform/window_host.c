/*
 * port/platform/window_host.c
 *
 * The window, the renderer device on it and the real-time pacing
 * (window_host.h).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input_sdl.h"
#include "rd.h"
#include "rhi.h"
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

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "window: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    s_window =
        SDL_CreateWindow("ICO", WINDOW_W, WINDOW_H,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (s_window == NULL) {
        fprintf(stderr, "window: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    memset(&rs, 0, sizeof(rs));
    rs.preset = RD_PRESET_ORIGINAL;
    rs.outputWidth = (uint32_t)(w > 0 ? w : WINDOW_W);
    rs.outputHeight = (uint32_t)(h > 0 ? h : WINDOW_H);
    rs.aspect = 4.0f / 3.0f;
    rs.vsync = 1;
    if (!rd_Init(gsW, gsH, &rs, s_window)) {
        fprintf(stderr, "window: no usable Vulkan device (rd_Init failed; the rhi_vk lines "
                        "above say why)\n");
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return -1;
    }
    fprintf(stderr, "window: %dx%d pixels, Vulkan on %s, Original preset, vsync on\n", w, h,
            rhi_AdapterName());
    s_deadline = SDL_GetTicksNS();
    s_open = 1;
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
    /* no mode set: SDL's borderless fullscreen at the desktop resolution; the
       presenter letterboxes the 4:3 picture (rd_ResizeOutput follows) */
    SDL_SetWindowFullscreen(s_window, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) == 0);
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
            }
            break;
        default:
            ico_input_sdl_event(&e);
            break;
        }
    }
    set_capture((SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0 && boyGObj != NULL &&
                game_pause == 0 && data_loading == 0);
    ico_input_sdl_update();
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
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
