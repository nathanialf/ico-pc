/*
 * port/platform/window_display_rate.h
 *
 * The window's side of display_rate.h (window_host.c is at its file size
 * gate): the screen's refresh rate in the log on every platform, and the
 * Android frame rate pin.
 */
#ifndef ICO_PLATFORM_WINDOW_DISPLAY_RATE_H
#define ICO_PLATFORM_WINDOW_DISPLAY_RATE_H

#include <SDL3/SDL.h>

/* After the window exists: notes the display's rate, writes the first
   line, and watches SDL's display mode changes (counted on SDL's thread;
   the 10 s line's poll writes the change's line at the event's time) */
void window_display_rate_start(SDL_Window *window);
/* The 10 s block's refresh line (the display's mode now) */
void window_display_rate_line(SDL_Window *window);
/* The Frame rate row's value (ico_video_framerate) whenever the options are
   applied: Android asks the system for that rate, for the display's
   highest under Uncapped (Android 12 and later), or releases the ask
   ([dev] frame_rate_pin, display_rate.h); every other platform nothing */
void window_frame_rate_pin(SDL_Window *window, int framerate);
/* The same after the app came back from the background (the pump, not
   SDL's event thread): the surface is new, so the ask is made again
   ("frame rate pin: asked again for a new surface") */
void window_frame_rate_pin_resumed(SDL_Window *window, int framerate);
/* The display's refresh rate in Hz from SDL's (sdlHz, 0 or less unknown):
   on Android the rate the activity's display listener last reported
   (IcoActivity.watchDisplay: SDL's Android side reads the rate only when
   the surface changes size, so a panel that switched rates after the
   frame rate pin's ask kept the old rate in SDL's mode), else sdlHz.  Any
   thread (window_host.c window_refresh_ns). */
double window_display_refresh_hz(double sdlHz);
/* Android: 1 once after each display report from the activity since the
   last call (the main thread's pump then applies the video settings again
   as for SDL's display mode event: the refresh, the vsync lock, the pin);
   every other platform 0 */
int window_display_rate_pushed(void);
/* At exit: the ask released, the watch removed */
void window_display_rate_stop(SDL_Window *window);

#endif
