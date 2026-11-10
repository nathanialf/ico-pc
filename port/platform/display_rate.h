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
 * The pin (Android only, through the NDK's
 * ANativeWindow_setFrameRateWithChangeStrategy, api 31, or
 * ANativeWindow_setFrameRate, api 30; the window glue asks and logs): the
 * game asks the system for the rate its pictures come at, so a panel that
 * offers several refresh rates can run one the pictures divide.  The Frame
 * rate row's number asks that number; Original asks the game's own field
 * rate (59.94 Hz, or 50 Hz on a European disc in its 50 Hz mode: the 30 or
 * 25 pictures a second the game shows divide it, as do its 60 or 50 a
 * second screens); Uncapped asks none (the system's choice), as does
 * leaving the game.  The compatibility is DEFAULT, the value Android's NDK
 * reference gives for game content (ANativeWindow_FrameRateCompatibility:
 * "This value should be used when displaying game content"; FIXED_SOURCE
 * "should be used for video content", and the frame rate guide: "For
 * non-video usage, use FRAME_RATE_COMPATIBILITY_DEFAULT"), and the strategy
 * ONLY_IF_SEAMLESS (no blink).  The rate is passed as the exact rate
 * ("it's best to pass in the exact frame rate rather than rounding to an
 * integer", developer.android.com/media/optimize/performance/frame-rate).
 * The system may still keep another rate, a multiple of the ask among them
 * ("the platform may switch to a multiple of the frame rate the app
 * specified"), so the window says, at the 10 s poll after each ask and at
 * each change while one holds, the rate the display took against the ask
 * (display_pin_took_text).  [dev] frame_rate_pin = auto (the default) or
 * off.  The lines:
 *     window: frame rate pin: asked 59.94 Hz (the game's own field rate;
 *     the Frame rate row is Original) with
 *     ANativeWindow_setFrameRateWithChangeStrategy: returned 0
 *     window: frame rate pin: the display runs 60.00 Hz with 59.94 Hz
 *     asked: the ask taken (run time 10.2 s)
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
/* The 10 s poll on the main thread (SDL's display calls are main-thread
   calls, and Android posts the display event from its Java thread, so the
   event watch only counts the events and keeps the last one's run time):
   hz the rate read now, events the mode-change events SDL posted since the
   last poll, eventSeconds the run time of the last of them.  Notes hz;
   returns 1 with a line in buf (without "window: ") when it changed: the
   change's line at the event's run time when SDL announced one, else
   "display refresh changed without an event from SDL: B Hz"; 0 (buf "")
   otherwise. */
int display_rate_poll(DisplayRateWatch *w, double hz, unsigned events, double eventSeconds,
                      int width, int height, char *buf, size_t size);

/* --- the pin ------------------------------------------------------------ */

typedef enum DisplayPinMode { DISPLAY_PIN_AUTO = 0, DISPLAY_PIN_OFF } DisplayPinMode;

/* [dev] frame_rate_pin: "auto" (also NULL, "" and a value not read) or
   "off" (also false, 0 and no, any case) */
DisplayPinMode display_pin_mode(const char *value);
/* The field rates of Frame rate Original in mHz: NTSC's 60000/1001 Hz
   (59.94) and PAL's 50 Hz (the simulation's own, host_loop.h
   ico_host_vsync_hz) */
#define DISPLAY_PIN_NTSC_MHZ 59940
#define DISPLAY_PIN_PAL_MHZ 50000
/* The rate to ask for, in mHz (0 asks none), for the Frame rate row's value
   (ico_video_framerate: 0 Original, -1 Uncapped, else the cap) and the
   simulation's field rate vsyncHz (50 or 60): the cap itself for a number
   (30 asks 30: the system picks a rate, a multiple of it or not, and the
   app keeps asking its own rate); Original the field rate (50 for vsyncHz
   50, else 59.94); 0 for Uncapped, a pin turned off or a rate above 1000 */
int display_pin_rate(int framerate, DisplayPinMode mode, int vsyncHz);

typedef struct DisplayPin {
    int asked;           /* the rate asked of the system now in mHz, 0 none */
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

/* What to do when the ask should be `want` (mHz, display_pin_rate) and whether
   the device offers the call; the state is updated as if the call was made
   (display_pin_failed for a call that failed).  A device without
   the call is never asked.  A SET after display_pin_surface lost the ask is
   DISPLAY_PIN_SET_AGAIN. */
DisplayPinAct display_pin_next(DisplayPin *p, int want, int deviceOffers);
/* The system refused the ask (the call's return was not 0): the state keeps
   none, so the next change tries again */
void display_pin_failed(DisplayPin *p);
/* The pin's line without "window: "; rate the ask in mHz, call names the
   NDK function, ret its return; framerate the row's value */
int display_pin_text(DisplayPinAct act, int rate, int framerate, const char *call, int ret,
                     char *buf, size_t size);
/* The line once a run when the first decision asks none (Uncapped, or the
   pin off): "frame rate pin: nothing asked (the Frame rate row is
   Uncapped: the display's own choice)" */
int display_pin_none_text(int framerate, DisplayPinMode mode, char *buf, size_t size);

/* The rate the display took against the ask (askedMhz > 0, displayHz the
   display's current rate; 0 or less not known): 0 the ask taken (within
   0.5 Hz), n >= 2 a whole multiple of it (within 0.5 %: 120 on 59.94 is 2,
   120 on 30 is 4), -1 neither (60 on 25), -2 the rate not known */
int display_pin_took(int askedMhz, double displayHz);
/* "frame rate pin: the display runs 120.00 Hz with 59.94 Hz asked: 2 x the
   ask (the system kept a multiple; the pictures still divide it) (run time
   10.2 s)"; "...: the ask taken ..."; "...: not a multiple of the ask (the
   pictures do not divide it: uneven spacing) ..."; "the display's rate not
   known" */
int display_pin_took_text(int askedMhz, double displayHz, double runSeconds, char *buf,
                          size_t size);

#endif /* ICO_PLATFORM_DISPLAY_RATE_H */
