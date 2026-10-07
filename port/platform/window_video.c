/*
 * port/platform/window_video.c
 *
 * The fullscreen request and its readback (window_video.h).
 */
#include "window_video.h"
#include <stdio.h>

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
