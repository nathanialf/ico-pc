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
 * changes its refresh rate with the load holds one; under Uncapped the game
 * asks for the highest rate the display's own mode list offers at the
 * current mode's size (Android 12 and later only, where the call takes the
 * seamless-only change strategy; read from Display.getSupportedModes at run
 * time, never a written rate); "Original" releases it, as does leaving the
 * game.  [dev] frame_rate_pin = auto (the default) or off (every ask off,
 * Uncapped's too).  One line each time the ask changes:
 *     window: frame rate pin: asked 60 Hz (fixed source; the Frame rate row
 *     is 60) with ANativeWindow_setFrameRateWithChangeStrategy: returned 0
 *     window: frame rate pin: asked 120 Hz (fixed source; the display's
 *     highest of 60, 120 at 1080x2364; the Frame rate row is Uncapped) with
 *     ANativeWindow_setFrameRateWithChangeStrategy: returned 0
 * and one line when Uncapped asks for nothing (display_pin_skip_text).
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
/* The rate to ask for the Frame rate row's value (ico_video_framerate: 0
   Original, -1 Uncapped, else the cap): the cap itself for a number, 0 (ask
   for none) for Original, Uncapped, a pin turned off or a rate above 1000 */
int display_pin_rate(int framerate, DisplayPinMode mode);

typedef struct DisplayPin {
    int asked;           /* the rate asked of the system now, 0 none */
    const void *surface; /* the native window the ask was made on */
    int again;           /* the ask was lost with its surface: the next SET says so */
    int uncapped;        /* the ask now is Uncapped's (the display's highest) */
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
/* The same with whether want is Uncapped's ask (display_pin_want): the
   same rate asked for the other reason (the row 120 and then Uncapped at a
   highest of 120) is a new ask, so the line says which.  display_pin_next
   is this with uncapped 0. */
DisplayPinAct display_pin_next_uncapped(DisplayPin *p, int want, int uncapped, int deviceOffers);
/* The system refused the ask (the call's return was not 0): the state keeps
   none, so the next change tries again */
void display_pin_failed(DisplayPin *p);
/* The pin's line without "window: "; call names the NDK function, ret its
   return; framerate the row's value */
int display_pin_text(DisplayPinAct act, int rate, int framerate, const char *call, int ret,
                     char *buf, size_t size);
/* Android's display timing on offer, read only, for the evenness line's
   next design (port/render/rd_even.h): whether libandroid.so has
   AChoreographer_getInstance, AChoreographer_postFrameCallback64 and
   AChoreographer_postVsyncCallback (looked up with dlsym, none called);
   the callbacks are reachable with the first and either of the others.
   The line without "window: " */
int display_choreo_text(int getInstance, int frame64, int vsync, char *buf, size_t size);

/* --- the pin under Uncapped: the display's highest rate ----------------- */

/* One mode of the display's list (Display.Mode: its physical width and
   height and its refresh rate in Hz) */
typedef struct DisplayModeRate {
    int w, h;
    double hz;
} DisplayModeRate;

enum { DISPLAY_TOP_RATES = 32 };

typedef struct DisplayTop {
    int listed;                      /* the modes read; -1: the list could not be read */
    int w, h;                        /* the current mode's size */
    int nRates;                      /* the distinct rates at w x h (at most DISPLAY_TOP_RATES) */
    double rates[DISPLAY_TOP_RATES]; /* those rates, lowest first */
    double hz;                       /* the highest of them, 0 none */
} DisplayTop;

/* The rates the display offers at the current mode's size curW x curH:
   modes NULL or n < 0 is a list that could not be read (listed -1).  A
   mode of another size, or with a rate of 0 or less or above 1000 Hz, is
   not counted; a rate within 0.05 Hz of one already counted is the same
   rate (the first one's value is kept), so 59.94 and 60.00 are two rates
   and 120.00 and 120.02 one.  More than DISPLAY_TOP_RATES distinct rates:
   the first DISPLAY_TOP_RATES met are kept and hz is the highest of those. */
void display_top_rate(const DisplayModeRate *modes, int n, int curW, int curH, DisplayTop *out);

/* Why the ask under Uncapped is or is not made */
typedef enum DisplayTopSkip {
    DISPLAY_TOP_ASK = 0,      /* asked: the display's highest rate */
    DISPLAY_TOP_NOT_UNCAPPED, /* the row is a number or Original: display_pin_rate's rule */
    DISPLAY_TOP_PIN_OFF,      /* [dev] frame_rate_pin = off: nothing asked */
    DISPLAY_TOP_NO_SEAMLESS,  /* no ANativeWindow_setFrameRateWithChangeStrategy */
    DISPLAY_TOP_NO_LIST,      /* the display's modes could not be read */
    DISPLAY_TOP_NO_SIZE,      /* no mode of the list has the current mode's size */
    DISPLAY_TOP_ONE_RATE,     /* one rate at the current size: nothing to choose */
    DISPLAY_TOP_REFUSED,      /* the system refused an Uncapped ask this run */
    DISPLAY_TOP_UNFOCUSED     /* the window has no input focus: no new ask */
} DisplayTopSkip;

/* The rate to ask for the Frame rate row's value (ico_video_framerate) with
   Uncapped's rule: a number or Original is display_pin_rate's answer (skip
   DISPLAY_TOP_NOT_UNCAPPED).  Uncapped (framerate < 0), checked in this
   order: 0 (ask for none, today's behaviour) with the reason in *skip when
   the pin is off, the device has no seamless-only call (seamless 0), an
   Uncapped ask was refused this run (refused), the list could not be read,
   no mode has the current size, or the current size has one rate; when the
   window has no input focus (focused 0) the ask Uncapped already holds is
   kept (p's) and no new one is made; else the highest rate rounded to whole
   Hz (the state's unit; the call is made with top->hz itself), skip
   DISPLAY_TOP_ASK.  p may be NULL (nothing held); skip may be NULL. */
int display_pin_want(int framerate, DisplayPinMode mode, const DisplayTop *top, int seamless,
                     int refused, int focused, const DisplayPin *p, DisplayTopSkip *skip);
/* The line of an Uncapped ask (act DISPLAY_PIN_SET or _SET_AGAIN) without
   "window: ": the rate asked, the rates it was chosen from at the size
   (the highest 8 when there are more), the call and its return
   ("(refused)" when not 0); "" for another act */
int display_pin_top_text(DisplayPinAct act, const DisplayTop *top, const char *call, int ret,
                         char *buf, size_t size);
/* The line when Uncapped asks for nothing (skip DISPLAY_TOP_NO_SEAMLESS to
   _UNFOCUSED) without "window: "; ret the refused call's return for
   DISPLAY_TOP_REFUSED; "" (returns 0) for DISPLAY_TOP_ASK, _NOT_UNCAPPED
   and _PIN_OFF (the pin's first line already says it is off) */
int display_pin_skip_text(DisplayTopSkip skip, const DisplayTop *top, int ret, char *buf,
                          size_t size);

/* --- the refresh the game paces by (Android) ----------------------------- */

/* SDL's Android side reads the display's rate only when the surface changes
   size, so after a panel switch SDL's mode keeps the old rate; the
   activity's display listener reports the rate itself.  The rate the game
   paces by: reportedHz (0 or less: none yet) only while the Frame rate row
   is Uncapped (framerate < 0) and the report is above 1 and at most 1000
   Hz; else sdlHz.  A number or Original keeps SDL's rate, so the vsync lock
   and the present mode are decided on the rate they were decided on before
   the listener existed (a phone's touch boost moving the panel cannot flip
   them); Uncapped never takes the lock (pace_cap_equals_refresh is false
   for a cap of 0 or less), so there only the period follows the panel. */
double display_refresh_pick(int framerate, double sdlHz, double reportedHz);

/* The activity's reports as the window last acted on them */
typedef struct DisplayReportSeen {
    int have;
    double hz;
    int modes; /* the mode list's hash (IcoActivity: Arrays.hashCode) */
} DisplayReportSeen;

/* 1 when a report (its rate and its mode list's hash) differs from the last
   one acted on (a rate within 0.05 Hz is the same rate) and is noted; 0
   for a report the window already acted on (a storm of the same report
   does nothing) or a rate of 0 or less */
int display_report_new(DisplayReportSeen *s, double hz, int modes);

#endif /* ICO_PLATFORM_DISPLAY_RATE_H */
