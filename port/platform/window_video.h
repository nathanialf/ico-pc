/*
 * port/platform/window_video.h
 *
 * The window's fullscreen request and what came of it (package P3, v0.3.1).
 * SDL only, no game state, so port/platform/test/window_video_test.c drives
 * it on SDL's offscreen video driver; window_host.c is the caller.
 *
 *   ico_window_video_fullscreen(w, want, pxW, pxH)
 *       asks SDL for borderless fullscreen at the desktop resolution (want
 *       1) or the window back (want 0), waits for the window system's
 *       answer (SDL_SyncWindow: the request is asynchronous and can be
 *       denied), logs what the window became and its size in pixels, and
 *       returns the real state: 1 fullscreen, 0 windowed.  pxW and pxH (may
 *       be NULL) receive the pixel size read back.  A refused request is
 *       logged with SDL's reason.
 */
#ifndef ICO_PLATFORM_WINDOW_VIDEO_H
#define ICO_PLATFORM_WINDOW_VIDEO_H

#include <SDL3/SDL.h>

int ico_window_video_fullscreen(SDL_Window *w, int want, int *pxW, int *pxH);

#endif /* ICO_PLATFORM_WINDOW_VIDEO_H */
