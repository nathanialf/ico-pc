/*
 * port/platform/window_video.c
 *
 * The fullscreen request and its readback (window_video.h).
 */
#include "window_video.h"
#include <stdio.h>
#include <string.h>

int ico_window_video_fullscreen(SDL_Window *w, int want, int *pxW, int *pxH)
{
    const char *what = want ? "on" : "off";
    int pw = 0, ph = 0;

    /* no mode set: SDL's borderless fullscreen at the desktop resolution
       (never an exclusive mode) */
    if (!SDL_SetWindowFullscreen(w, want != 0)) {
        fprintf(stderr, "window: fullscreen %s refused: %s\n", what, SDL_GetError());
    }
    /* the window manager answers later, and may say no: wait for it, then
       read what the window is rather than what was asked */
    SDL_SyncWindow(w);
    const int real = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    fprintf(stderr, "window: fullscreen %s (window %dx%d pixels)%s\n", real ? "on" : "off", pw, ph,
            real != (want != 0) ? "; the window manager did not follow the request" : "");
    if (pxW != NULL) {
        *pxW = pw;
    }
    if (pxH != NULL) {
        *pxH = ph;
    }
    return real;
}

/* v0.4.3 (I17c): the window's mode.  The windowed rectangle is remembered
   when the window leaves the windowed state, and comes back with it. */
static int s_haveRect;
static int s_rectX, s_rectY, s_rectW, s_rectH;
static int s_maximizedByUs; /* Wayland's borderless is a maximised window */

#define DEFAULT_W 960
#define DEFAULT_H 720

static int real_mode(SDL_Window *w)
{
    const SDL_WindowFlags f = SDL_GetWindowFlags(w);

    if (f & SDL_WINDOW_FULLSCREEN) {
        return ICO_WINDOWVIDEO_FULLSCREEN;
    }
    return (f & SDL_WINDOW_BORDERLESS) ? ICO_WINDOWVIDEO_BORDERLESS : ICO_WINDOWVIDEO_WINDOWED;
}

static void leave_fullscreen(SDL_Window *w)
{
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) {
        if (!SDL_SetWindowFullscreen(w, false)) {
            fprintf(stderr, "window: leaving fullscreen refused: %s\n", SDL_GetError());
        }
        SDL_SyncWindow(w);
    }
}

static void remember_rect(SDL_Window *w)
{
    if (real_mode(w) != ICO_WINDOWVIDEO_WINDOWED || s_maximizedByUs) {
        return; /* not the windowed rectangle */
    }
    SDL_GetWindowPosition(w, &s_rectX, &s_rectY);
    SDL_GetWindowSize(w, &s_rectW, &s_rectH);
    s_haveRect = s_rectW > 0 && s_rectH > 0;
}

static int on_wayland(void)
{
    const char *d = SDL_GetCurrentVideoDriver();

    return d != NULL && strcmp(d, "wayland") == 0;
}

int ico_window_video_mode(SDL_Window *w, int mode, int *pxW, int *pxH)
{
    int pw = 0, ph = 0;

    if (mode < ICO_WINDOWVIDEO_WINDOWED || mode > ICO_WINDOWVIDEO_FULLSCREEN) {
        mode = ICO_WINDOWVIDEO_WINDOWED;
    }
    if (mode == ICO_WINDOWVIDEO_FULLSCREEN) {
        remember_rect(w);
        if (s_maximizedByUs) {
            SDL_RestoreWindow(w);
            s_maximizedByUs = 0;
        }
        SDL_SetWindowBordered(w, true);
        /* no mode set: SDL's borderless fullscreen at the desktop resolution
           (never an exclusive mode) */
        if (!SDL_SetWindowFullscreen(w, true)) {
            fprintf(stderr, "window: fullscreen refused: %s\n", SDL_GetError());
        }
    } else if (mode == ICO_WINDOWVIDEO_BORDERLESS) {
        remember_rect(w); /* before leaving fullscreen: that is not the windowed rectangle */
        leave_fullscreen(w);
        SDL_SetWindowBordered(w, false);
        if (on_wayland()) {
            /* a client cannot place its window there: maximised instead */
            fprintf(stderr, "window: borderless on Wayland: maximised without a frame\n");
            SDL_MaximizeWindow(w);
            s_maximizedByUs = 1;
        } else {
            SDL_Rect b;

            if (s_maximizedByUs) {
                SDL_RestoreWindow(w);
                s_maximizedByUs = 0;
            }
            if (SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &b) && b.w > 0 && b.h > 0) {
                SDL_SetWindowPosition(w, b.x, b.y);
                SDL_SetWindowSize(w, b.w, b.h);
            } else {
                fprintf(stderr, "window: the display's bounds are unknown: %s\n", SDL_GetError());
            }
        }
    } else {
        leave_fullscreen(w);
        if (s_maximizedByUs) {
            SDL_RestoreWindow(w);
            s_maximizedByUs = 0;
        }
        SDL_SetWindowBordered(w, true);
        if (s_haveRect) {
            SDL_SetWindowSize(w, s_rectW, s_rectH);
            SDL_SetWindowPosition(w, s_rectX, s_rectY);
        } else {
            SDL_SetWindowSize(w, DEFAULT_W, DEFAULT_H);
            SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    /* the window manager answers later, and may say no: wait, then read what
       the window is rather than what was asked */
    SDL_SyncWindow(w);
    const int real = real_mode(w);
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    fprintf(stderr, "window: mode %s (window %dx%d pixels)%s\n",
            real == ICO_WINDOWVIDEO_FULLSCREEN   ? "fullscreen"
            : real == ICO_WINDOWVIDEO_BORDERLESS ? "borderless"
                                                 : "windowed",
            pw, ph, real != mode ? "; the window manager did not follow the request" : "");
    if (pxW != NULL) {
        *pxW = pw;
    }
    if (pxH != NULL) {
        *pxH = ph;
    }
    return real;
}

int ico_window_video_refit(SDL_Window *w)
{
    SDL_Rect b;
    int x = 0, y = 0, pw = 0, ph = 0;

    if (real_mode(w) != ICO_WINDOWVIDEO_BORDERLESS || on_wayland()) {
        return 0; /* Wayland's maximised window follows its display itself */
    }
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &b) || b.w <= 0 || b.h <= 0) {
        return 0;
    }
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &pw, &ph);
    if (x == b.x && y == b.y && pw == b.w && ph == b.h) {
        return 0;
    }
    fprintf(stderr, "window: borderless refitted to its display (%dx%d at %d,%d)\n", b.w, b.h, b.x,
            b.y);
    SDL_SetWindowPosition(w, b.x, b.y);
    SDL_SetWindowSize(w, b.w, b.h);
    return 1;
}
