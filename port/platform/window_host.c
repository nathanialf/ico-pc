/*
 * port/platform/window_host.c
 *
 * The window, the renderer device on it and the real-time pacing
 * (window_host.h).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "rd.h"
#include "rhi.h"
#include "window_host.h"

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull

static SDL_Window *s_window;
static Uint64 s_deadline;
static int s_open;

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
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
    return 0;
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
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (e.window.data1 > 0 && e.window.data2 > 0) {
                rd_ResizeOutput((uint32_t)e.window.data1, (uint32_t)e.window.data2);
            }
            break;
        default:
            break;
        }
    }
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
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
