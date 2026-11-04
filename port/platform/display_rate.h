/*
 * port/platform/display_rate.h
 *
 * Issue 123 (Xiaomi Pad 6S Pro, Adreno 740 on Qualcomm's own driver): the
 * screen's refresh rate in the log, and the frame rate the game asks the
 * system to hold.  The pure parts of window_display_rate.c, so the lines'
 * words and the pin's decisions are unit tested on every host
 * (test/display_rate_test.c).
 *
 * The refresh rate: the window writes the display's current mode (SDL's
 * SDL_GetCurrentDisplayMode) in every 10 s block and a line at each change
 * SDL reports (SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED), so a log shows
 * whether the panel switched modes around a present that blocked for 360 ms:
 *     window: display refresh: 144.00 Hz, 3048x2032 "Built-in display"
 *     (run time 85.2 s, 2 changes this run)
 *     window: display refresh changed: 144.00 Hz -> 60.00 Hz, 3048x2032
 *     (run time 85.2 s, change 1 of this run)
 *
 * The pin (Android only, through the NDK's ANativeWindow_setFrameRate; the
 * window glue asks and logs): while the Frame rate row is a number the
 * game asks the system for that rate as a fixed source, so a panel that
 * changes its refresh rate with the load holds one; Uncapped and "Original"
 * release it, as does leaving the game.  [dev] frame_rate_pin = auto (the
 * default) or off.  One line each time the ask changes:
 *     window: frame rate pin: asked 60 Hz (fixed source; the Frame rate row
 *     is 60) with ANativeWindow_setFrameRateWithChangeStrategy: returned 0
 */
#ifndef ICO_PLATFORM_DISPLAY_RATE_H
#define ICO_PLATFORM_DISPLAY_RATE_H

#include <stddef.h>

/* --- the refresh rate's log -------------------------------------------- */

typedef struct DisplayRateWatch {
    int have;         /* a rate was noted */
    double hz;        /* the last rate noted */
    unsigned changes; /* the changes noted this run */
} DisplayRateWatch;

/* hz is a refresh rate in Hz as SDL reports it (0 or less: not known).
   Returns 0 for the first rate noted, a known one equal to the last (within
   0.05 Hz) or an unknown one, and 1 for a change (counted in changes). */
int display_rate_note(DisplayRateWatch *w, double hz);
/* the 10 s block's line without "window: "; name may be NULL or "" */
int display_rate_text(const DisplayRateWatch *w, int width, int height, const char *name,
                      double runSeconds, char *buf, size_t size);
/* the change's line without "window: " (oldHz and newHz as noted) */
int display_rate_change_text(double oldHz, double newHz, int width, int height, double runSeconds,
                             unsigned change, char *buf, size_t size);

/* --- the pin ------------------------------------------------------------ */

typedef enum DisplayPinMode { DISPLAY_PIN_AUTO = 0, DISPLAY_PIN_OFF } DisplayPinMode;

/* [dev] frame_rate_pin: "auto" (also NULL, "" and a value not read) or
   "off" (also false, 0 and no, any case) */
DisplayPinMode display_pin_mode(const char *value);
/* The rate to ask for the Frame rate row's value (ico_video_framerate: 0
   Original, -1 Uncapped, else the cap): the cap itself for a number, 0 (ask
   for none) for Original, Uncapped, a pin turned off or a rate above 1000 */
int display_pin_rate(int framerate, DisplayPinMode mode);

typedef struct DisplayPin {
    int asked;           /* the rate asked of the system now, 0 none */
    const void *surface; /* the native window the ask was made on */
    int again;           /* the ask was lost with its surface: the next SET says so */
} DisplayPin;

typedef enum DisplayPinAct {
    DISPLAY_PIN_NOTHING = 0, /* the system already has this ask */
    DISPLAY_PIN_SET,         /* ask for want Hz */
    DISPLAY_PIN_RELEASE,     /* ask for none (the rate was pinned) */
    DISPLAY_PIN_SET_AGAIN    /* ask for want Hz again: the surface that held it is gone */
} DisplayPinAct;

/* The ask is a vote on the Surface: Android destroys the SurfaceView's
   surface when the app goes to the background and the foreground brings a
   new native window with no vote.  Before display_pin_next: surface is the
   native window now (SDL's), resumed true when the app has just come back
   (the pointer may be reused); a changed surface or a resume forgets the
   ask (the next display_pin_next asks again, DISPLAY_PIN_SET_AGAIN when a
   rate had been asked).  Returns 1 when an ask was lost. */
int display_pin_surface(DisplayPin *p, const void *surface, int resumed);

/* What to do when the ask should be `want` (display_pin_rate) and whether
   the device offers the call; the state is updated as if the call was made
   (display_pin_failed for a call that failed).  A device without
   the call is never asked.  A SET after display_pin_surface lost the ask is
   DISPLAY_PIN_SET_AGAIN. */
DisplayPinAct display_pin_next(DisplayPin *p, int want, int deviceOffers);
/* The system refused the ask (the call's return was not 0): the state keeps
   none, so the next change tries again */
void display_pin_failed(DisplayPin *p);
/* The pin's line without "window: "; call names the NDK function, ret its
   return; framerate the row's value */
int display_pin_text(DisplayPinAct act, int rate, int framerate, const char *call, int ret,
                     char *buf, size_t size);

#endif /* ICO_PLATFORM_DISPLAY_RATE_H */
