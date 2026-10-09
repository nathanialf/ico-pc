/* window_video_test.c: ico_window_video_fullscreen and ico_window_video_mode
 * (window_video.c) on SDL's offscreen video driver.
 *
 * This exercises their readback and logging: what they return is the
 * window's state after the request, and the size they report is the
 * window's pixel size then.  It does not exercise X11, KWin or gamescope (the
 * offscreen driver has no window manager to refuse anything); the Deck is
 * where that is checked, through the log lines these calls write.
 *
 * Exit 77 (skipped) when SDL has no offscreen video driver. */
#include "../window_video.h"
#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL " __VA_ARGS__);                                                           \
            printf("\n");                                                                          \
            s_failures++;                                                                          \
        }                                                                                          \
    } while (0)

/* Drains the queue; returns whether a fullscreen or pixel size event for w
   was in it */
static int drain_fullscreen_events(SDL_Window *w, int *entered, int *left, int *sized)
{
    SDL_Event e;
    const SDL_WindowID id = SDL_GetWindowID(w);

    *entered = *left = *sized = 0;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e)) {
        if (e.window.windowID != id) {
            continue;
        }
        *entered |= e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        *left |= e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN;
        *sized |= e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    }
    return *entered || *left || *sized;
}

int main(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SKIP window_video_test: SDL_Init(offscreen): %s\n", SDL_GetError());
        return 77;
    }
    SDL_Window *w = SDL_CreateWindow("window_video_test", 960, 720, SDL_WINDOW_RESIZABLE);
    if (w == NULL) {
        printf("SKIP window_video_test: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(w));
    if (desk == NULL) {
        printf("SKIP window_video_test: no desktop mode: %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        SDL_Quit();
        return 77;
    }
    printf("offscreen desktop mode %dx%d\n", desk->w, desk->h);
    int entered, left, sized;
    drain_fullscreen_events(w, &entered, &left, &sized);

    /* on: the flag, the desktop's size, and the events window_host.c's pump
       acts on */
    int pw = -1, ph = -1;
    int r = ico_window_video_fullscreen(w, 1, &pw, &ph);
    CHECK(r == 1, "fullscreen on returned %d", r);
    CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0, "fullscreen flag not set");
    CHECK(pw == desk->w && ph == desk->h, "fullscreen size %dx%d, desktop %dx%d", pw, ph, desk->w,
          desk->h);
    {
        int sw = 0, sh = 0;
        SDL_GetWindowSizeInPixels(w, &sw, &sh);
        CHECK(sw == pw && sh == ph, "reported %dx%d, the window is %dx%d", pw, ph, sw, sh);
    }
    drain_fullscreen_events(w, &entered, &left, &sized);
    printf("fullscreen on: events enter %d, leave %d, pixel size %d\n", entered, left, sized);
    CHECK(entered || sized, "no ENTER_FULLSCREEN or PIXEL_SIZE_CHANGED event after fullscreen on");
    CHECK(!left, "LEAVE_FULLSCREEN after fullscreen on");

    /* on again: nothing to do, the same answer */
    r = ico_window_video_fullscreen(w, 1, NULL, NULL);
    CHECK(r == 1, "fullscreen on again returned %d", r);

    /* off: the flag clear and the window's own size back */
    pw = ph = -1;
    r = ico_window_video_fullscreen(w, 0, &pw, &ph);
    CHECK(r == 0, "fullscreen off returned %d", r);
    CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) == 0, "fullscreen flag still set");
    CHECK(pw == 960 && ph == 720, "windowed size %dx%d, want 960x720", pw, ph);
    drain_fullscreen_events(w, &entered, &left, &sized);
    printf("fullscreen off: events enter %d, leave %d, pixel size %d\n", entered, left, sized);
    CHECK(left || sized, "no LEAVE_FULLSCREEN or PIXEL_SIZE_CHANGED event after fullscreen off");

    /* the window mode.  Borderless: the frameless flag, no
       fullscreen state, the display's size (informational: the offscreen
       driver may ignore a size request). */
    {
        SDL_Rect b = {0, 0, 0, 0};
        SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &b);
        pw = ph = -1;
        r = ico_window_video_mode(w, ICO_WINDOWVIDEO_BORDERLESS, &pw, &ph);
        CHECK(r == ICO_WINDOWVIDEO_BORDERLESS, "borderless returned %d", r);
        if (strcmp(SDL_GetCurrentVideoDriver(), "offscreen") != 0) {
            CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS) != 0, "borderless flag not set");
        } else {
            printf("borderless: the offscreen driver has no borders, flag %s\n",
                   (SDL_GetWindowFlags(w) & SDL_WINDOW_BORDERLESS) ? "set" : "not set");
        }
        CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) == 0, "borderless is fullscreen");
        printf("borderless: window %dx%d, display %dx%d\n", pw, ph, b.w, b.h);

        /* windowed: framed, the rectangle from before */
        pw = ph = -1;
        r = ico_window_video_mode(w, ICO_WINDOWVIDEO_WINDOWED, &pw, &ph);
        CHECK(r == ICO_WINDOWVIDEO_WINDOWED, "windowed returned %d", r);
        CHECK((SDL_GetWindowFlags(w) & (SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN)) == 0,
              "windowed still frameless or fullscreen");
        CHECK(pw == 960 && ph == 720, "windowed size %dx%d, want 960x720", pw, ph);

        /* fullscreen as before, then fullscreen -> borderless leaves it */
        r = ico_window_video_mode(w, ICO_WINDOWVIDEO_FULLSCREEN, NULL, NULL);
        CHECK(r == ICO_WINDOWVIDEO_FULLSCREEN, "mode fullscreen returned %d", r);
        CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0, "mode fullscreen: no flag");
        r = ico_window_video_mode(w, ICO_WINDOWVIDEO_BORDERLESS, NULL, NULL);
        CHECK(r == ICO_WINDOWVIDEO_BORDERLESS, "fullscreen -> borderless returned %d", r);
        CHECK((SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) == 0, "still fullscreen");
        r = ico_window_video_mode(w, ICO_WINDOWVIDEO_WINDOWED, &pw, &ph);
        CHECK(r == ICO_WINDOWVIDEO_WINDOWED && pw == 960 && ph == 720,
              "back to windowed: %d, %dx%d", r, pw, ph);
    }

    SDL_DestroyWindow(w);
    SDL_Quit();
    if (s_failures) {
        printf("window_video_test: %d failures\n", s_failures);
        return 1;
    }
    printf("window_video_test: ok\n");
    return 0;
}
