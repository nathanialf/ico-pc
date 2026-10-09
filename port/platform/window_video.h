/*
 * port/platform/window_video.h
 *
 * The window's mode requests and what came of them. SDL only, no game
 * state, so port/platform/test/window_video_test.c drives them on SDL's
 * offscreen video driver; window_host.c is the caller.
 */
#ifndef ICO_PLATFORM_WINDOW_VIDEO_H
#define ICO_PLATFORM_WINDOW_VIDEO_H

#include <SDL3/SDL.h>

/* The window modes; the values equal video_options.h's ICO_WINDOW_*. */
enum {
    ICO_WINDOWVIDEO_WINDOWED = 0,
    ICO_WINDOWVIDEO_BORDERLESS = 1,
    ICO_WINDOWVIDEO_FULLSCREEN = 2
};

/* (tests): production calls ico_window_video_mode.  Asks SDL for borderless
   fullscreen at the desktop resolution (want 1) or the window back (want
   0), waits for the window system's answer (SDL_SyncWindow: the request is
   asynchronous and can be denied), logs what the window became and its
   size in pixels, and returns the real state: 1 fullscreen, 0 windowed.
   pxW and pxH (may be NULL) receive the pixel size read back. A refused
   request is logged with SDL's reason. */
int ico_window_video_fullscreen(SDL_Window *w, int want, int *pxW, int *pxH);
/* Puts the window in mode: FULLSCREEN is SDL's desktop fullscreen;
   BORDERLESS a frameless window over its display (on Wayland, which does
   not let a window place itself, a frameless maximised one); WINDOWED the
   framed window at the rectangle it had before it left the windowed state.
   Waits for the window system, logs, and returns the real mode and the
   pixel size as ico_window_video_fullscreen does. */
int ico_window_video_mode(SDL_Window *w, int mode, int *pxW, int *pxH);
/* A borderless window moved to another display (or whose display changed
   size): fits it to the display again.  1 when it did. */
int ico_window_video_refit(SDL_Window *w);

#endif /* ICO_PLATFORM_WINDOW_VIDEO_H */
