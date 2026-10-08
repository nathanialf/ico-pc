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

/*   ico_window_video_mode(w, mode, pxW, pxH)      (v0.4.3, I17c)
 *       puts the window in mode (these values equal video_options.h's
 *       ICO_WINDOW_*): FULLSCREEN is SDL's desktop fullscreen; BORDERLESS a
 *       frameless window over its display (on Wayland, which does not let a
 *       window place itself, a frameless maximised one); WINDOWED the framed
 *       window at the rectangle it had before it left the windowed state.
 *       Waits for the window system, logs, returns the real mode and the
 *       pixel size as ico_window_video_fullscreen does.
 */
enum {
    ICO_WINDOWVIDEO_WINDOWED = 0,
    ICO_WINDOWVIDEO_BORDERLESS = 1,
    ICO_WINDOWVIDEO_FULLSCREEN = 2
};

int ico_window_video_fullscreen(SDL_Window *w, int want, int *pxW, int *pxH);
int ico_window_video_mode(SDL_Window *w, int mode, int *pxW, int *pxH);
/* A borderless window moved to another display (or whose display changed
   size): fits it to the display again.  1 when it did. */
int ico_window_video_refit(SDL_Window *w);

#endif /* ICO_PLATFORM_WINDOW_VIDEO_H */
