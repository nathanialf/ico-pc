/*
 * port/platform/window_display_rate.c
 *
 * The screen's refresh rate in the log and the Android frame rate pin
 * (display_rate.h, window_display_rate.h).
 */
#include "window_display_rate.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "display_rate.h"
#include "host_loop.h" /* ico_host_vsync_hz: Original's field rate */

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

/* the main thread's (start, the 10 s poll, stop) */
static DisplayRateWatch s_watch;
static int s_w, s_h;
static char s_name[96];
static Uint64 s_startNs;
static int s_watching;
/* the event watch's (SDL's posting thread: Android's Java thread): the
   mode-change events since the last poll and the last one's run time in ms */
static SDL_AtomicInt s_events;
static SDL_AtomicU32 s_eventMs;

static double run_seconds(void)
{
    return (double)(SDL_GetTicksNS() - s_startNs) / 1e9;
}

/* the display's current mode into s_w, s_h, s_name and the rate (0 when SDL
   gives none) */
static double read_mode(SDL_DisplayID display)
{
    const SDL_DisplayMode *m = display != 0 ? SDL_GetCurrentDisplayMode(display) : NULL;
    const char *name = display != 0 ? SDL_GetDisplayName(display) : NULL;
    double hz = 0.0;

    s_w = s_h = 0;
    snprintf(s_name, sizeof s_name, "%s", name != NULL ? name : "");
    if (m != NULL) {
        s_w = m->w;
        s_h = m->h;
        hz = m->refresh_rate_denominator > 0
                 ? (double)m->refresh_rate_numerator / (double)m->refresh_rate_denominator
                 : (double)m->refresh_rate;
        if (!(hz > 0.0)) {
            hz = (double)m->refresh_rate;
        }
    }
    return hz;
}

/* SDL's display mode change, on whatever thread SDL posts it from: only
   counted with its time (SDL_GetCurrentDisplayMode and SDL_GetDisplayName
   are main-thread calls); the 10 s poll reads the mode and writes the
   change's line at the event's run time (display_rate_poll) */
static bool SDLCALL display_watch(void *userdata, SDL_Event *e)
{
    (void)userdata;
    if (e->type != SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED) {
        return true;
    }
    SDL_SetAtomicU32(&s_eventMs, (Uint32)((SDL_GetTicksNS() - s_startNs) / 1000000u));
    (void)SDL_AddAtomicInt(&s_events, 1);
    return true;
}

void window_display_rate_start(SDL_Window *window)
{
    char line[256];
    const double hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);

    s_startNs = SDL_GetTicksNS();
    SDL_SetAtomicInt(&s_events, 0);
    SDL_SetAtomicU32(&s_eventMs, 0);
    s_watch = (DisplayRateWatch){0};
    (void)display_rate_note(&s_watch, hz);
    display_rate_text(&s_watch, s_w, s_h, s_name, 0.0, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    if (!s_watching && SDL_AddEventWatch(display_watch, NULL)) {
        s_watching = 1;
    }
}

static void pin_poll(SDL_Window *window);

void window_display_rate_line(SDL_Window *window)
{
    char line[256];
    const double hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);
    /* the events SDL posted since the last poll (the count taken, set to 0) */
    const int events = SDL_SetAtomicInt(&s_events, 0);
    const double eventSeconds = (double)SDL_GetAtomicU32(&s_eventMs) / 1000.0;

    /* a change SDL announced, at the event's time; one it did not report is
       still one */
    if (display_rate_poll(&s_watch, hz, events > 0 ? (unsigned)events : 0u, eventSeconds, s_w, s_h,
                          line, sizeof line)) {
        fprintf(stderr, "window: %s\n", line);
    }
    display_rate_text(&s_watch, s_w, s_h, s_name, run_seconds(), line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    pin_poll(window);
}

double window_display_rate_hz(void)
{
    return s_watch.have ? s_watch.hz : 0.0;
}

#ifdef __ANDROID__
/* NDK: api 30 ANativeWindow_setFrameRate, api 31 the ...WithChangeStrategy
   form; looked up at run time so the APK keeps minSdk 29 */
typedef int32_t (*SetRateFn)(void *window, float rate, int8_t compat);
typedef int32_t (*SetRateStrategyFn)(void *window, float rate, int8_t compat, int8_t strategy);

/* ANativeWindow_FrameRateCompatibility and _ChangeFrameRateStrategy
   (android/native_window.h).  DEFAULT, not FIXED_SOURCE: the NDK reference
   says DEFAULT "should be used when displaying game content" and
   FIXED_SOURCE "should be used for video content" (the window "will need
   to do pull down"); the frame rate guide: "FRAME_RATE_COMPATIBILITY_
   FIXED_SOURCE is intended only for video apps. For non-video usage, use
   FRAME_RATE_COMPATIBILITY_DEFAULT" (developer.android.com/media/optimize/
   performance/frame-rate, "Usage of FRAME_RATE_COMPATIBILITY_FIXED_SOURCE").
   v0.11.2 and earlier asked FIXED_SOURCE. */
enum { COMPAT_DEFAULT = 0, STRATEGY_ONLY_IF_SEAMLESS = 0 };

static DisplayPin s_pin;
static int s_pinResumed;   /* window_frame_rate_pin_resumed's call */
static int s_pinFramerate; /* the row's value at the last call */
static int s_pinHave;      /* a call was made (the 10 s poll asks again only after one) */
static int s_pinTookDue;   /* an ask made: the took line at the next poll */
static double s_pinTookHz; /* the display's rate at the last took line */
static int s_pinStopping;  /* window_display_rate_stop's release: no "nothing asked" line */

/* the two calls, looked up once a run (the library stays loaded: the
   system keeps libandroid.so for the process anyway); the pin runs on the
   main thread only */
static int s_pinLooked;
static SetRateStrategyFn s_withStrategy;
static SetRateFn s_plain;

static void pin_lookup(void)
{
    void *lib;

    if (s_pinLooked) {
        return;
    }
    s_pinLooked = 1;
    lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib != NULL) {
        s_withStrategy =
            (SetRateStrategyFn)dlsym(lib, "ANativeWindow_setFrameRateWithChangeStrategy");
        s_plain = (SetRateFn)dlsym(lib, "ANativeWindow_setFrameRate");
    }
}

/* rateMhz 0 releases ("0 is a special value that indicates the app will
   accept the system's choice", the NDK reference); the rate is passed
   exact (59.94, not 60: "pass in the exact frame rate rather than rounding
   to an integer", the frame rate guide) */
static int pin_call(void *nw, int rateMhz, const char **call)
{
    const SetRateStrategyFn withStrategy = s_withStrategy;
    const SetRateFn plain = s_plain;
    const float rate = (float)rateMhz / 1000.0f;

    /* only a seamless switch: a mode change that blinks the screen is not
       asked for (ANativeWindow_setFrameRate is the same call with
       ONLY_IF_SEAMLESS, the NDK reference) */
    if (withStrategy != NULL) {
        *call = "ANativeWindow_setFrameRateWithChangeStrategy";
        return withStrategy(nw, rate, COMPAT_DEFAULT, STRATEGY_ONLY_IF_SEAMLESS);
    }
    if (plain != NULL) {
        *call = "ANativeWindow_setFrameRate";
        return plain(nw, rate, COMPAT_DEFAULT);
    }
    *call = NULL;
    return -1000;
}

void window_frame_rate_pin(SDL_Window *window, int framerate)
{
    static int told, noneTold;
    const DisplayPinMode mode = display_pin_mode(getenv("ICO_FRAME_RATE_PIN"));
    void *nw = window != NULL ? SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                       SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL)
                              : NULL;
    const int want = display_pin_rate(framerate, mode, ico_host_vsync_hz());
    const char *call = NULL;
    char line[320];
    DisplayPinAct act;
    int ret;

    s_pinFramerate = framerate;
    s_pinHave = 1;
    /* does the device offer the call (api 30 and later)? */
    pin_lookup();
    {
        const int offers = nw != NULL && (s_withStrategy != NULL || s_plain != NULL);

        if (!told) {
            told = 1;
            fprintf(stderr,
                    "window: frame rate pin: %s ([dev] frame_rate_pin = %s); the device %s the "
                    "call%s\n",
                    mode == DISPLAY_PIN_OFF ? "turned off" : "on",
                    mode == DISPLAY_PIN_OFF ? "off" : "auto", offers ? "offers" : "does not offer",
                    nw == NULL ? " (no native window from SDL)" : "");
        }
        /* a new native window (the app came back: Android made the surface
           again) holds no vote: the ask is made again */
        (void)display_pin_surface(&s_pin, nw, s_pinResumed);
        s_pinResumed = 0;
        act = display_pin_next(&s_pin, want, offers);
    }
    if (act == DISPLAY_PIN_NOTHING) {
        if (want == 0 && !noneTold && window != NULL && !s_pinStopping) {
            /* once: what the first decision was when it asked none (the
               v0.11.2 log's "on" with no ask after it was Original asking
               none) */
            noneTold = 1;
            display_pin_none_text(framerate, mode, line, sizeof line);
            fprintf(stderr, "window: %s\n", line);
        }
        return;
    }
    noneTold = 1;
    ret = pin_call(nw, act == DISPLAY_PIN_RELEASE ? 0 : want, &call);
    if (ret != 0) {
        display_pin_failed(&s_pin);
    }
    s_pinTookDue = ret == 0 && act != DISPLAY_PIN_RELEASE;
    display_pin_text(act, want, framerate, call, ret, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
}

void window_frame_rate_pin_resumed(SDL_Window *window, int framerate)
{
    s_pinResumed = 1;
    window_frame_rate_pin(window, framerate);
}

int window_frame_rate_pin_asked_mhz(void)
{
    return s_pin.asked;
}

/* the 10 s poll: the field rate may have changed (Original's ask follows
   it), and the rate the display took after an ask, or at a change while
   one holds */
static void pin_poll(SDL_Window *window)
{
    char line[256];
    const double hz = window_display_rate_hz();

    if (s_pinHave && window != NULL) {
        window_frame_rate_pin(window, s_pinFramerate); /* nothing when unchanged */
    }
    if (s_pin.asked > 0 && (s_pinTookDue || fabs(hz - s_pinTookHz) > 0.05)) {
        s_pinTookDue = 0;
        s_pinTookHz = hz;
        display_pin_took_text(s_pin.asked, hz, run_seconds(), line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
}
#else
static void pin_poll(SDL_Window *window)
{
    (void)window;
}

void window_frame_rate_pin(SDL_Window *window, int framerate)
{
    (void)window;
    (void)framerate;
}

void window_frame_rate_pin_resumed(SDL_Window *window, int framerate)
{
    (void)window;
    (void)framerate;
}

int window_frame_rate_pin_asked_mhz(void)
{
    return 0;
}
#endif

void window_display_rate_stop(SDL_Window *window)
{
#ifdef __ANDROID__
    /* the ask released while the window still exists (Uncapped's value asks
       none; Original's 0 would ask the field rate); the 10 s poll stops
       asking */
    s_pinStopping = 1;
    window_frame_rate_pin(window, -1);
    s_pinHave = 0;
    s_pinStopping = 0;
#else
    (void)window;
#endif
    if (s_watching) {
        SDL_RemoveEventWatch(display_watch, NULL);
        s_watching = 0;
    }
}
