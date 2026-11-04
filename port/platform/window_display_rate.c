/*
 * port/platform/window_display_rate.c
 *
 * The screen's refresh rate in the log and the Android frame rate pin
 * (display_rate.h, window_display_rate.h).
 */
#include "window_display_rate.h"

#include <stdio.h>
#include <stdlib.h>
#include "display_rate.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

static DisplayRateWatch s_watch;
static SDL_Mutex *s_lock;
static int s_w, s_h;
static char s_name[96];
static Uint64 s_startNs;
static int s_watching;

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

/* SDL's display mode change, on whatever thread SDL posts it from */
static bool SDLCALL display_watch(void *userdata, SDL_Event *e)
{
    (void)userdata;
    if (e->type != SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED) {
        return true;
    }
    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
    }
    {
        const double old = s_watch.hz;
        const double hz = read_mode(e->display.displayID);
        char line[256];

        if (display_rate_note(&s_watch, hz)) {
            display_rate_change_text(old, hz, s_w, s_h, run_seconds(), s_watch.changes, line,
                                     sizeof line);
            fprintf(stderr, "window: %s\n", line);
        }
    }
    if (s_lock != NULL) {
        SDL_UnlockMutex(s_lock);
    }
    return true;
}

void window_display_rate_start(SDL_Window *window)
{
    char line[256];
    const double hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);

    s_startNs = SDL_GetTicksNS();
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
    }
    s_watch = (DisplayRateWatch){0};
    (void)display_rate_note(&s_watch, hz);
    display_rate_text(&s_watch, s_w, s_h, s_name, 0.0, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
    if (!s_watching && SDL_AddEventWatch(display_watch, NULL)) {
        s_watching = 1;
    }
}

void window_display_rate_line(SDL_Window *window)
{
    char line[256];
    double hz;

    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
    }
    hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);
    /* a change SDL did not report is still one */
    if (display_rate_note(&s_watch, hz)) {
        fprintf(stderr, "window: display refresh changed without an event from SDL: %.2f Hz\n", hz);
    }
    display_rate_text(&s_watch, s_w, s_h, s_name, run_seconds(), line, sizeof line);
    if (s_lock != NULL) {
        SDL_UnlockMutex(s_lock);
    }
    fprintf(stderr, "window: %s\n", line);
}

#ifdef __ANDROID__
/* NDK: api 30 ANativeWindow_setFrameRate, api 31 the ...WithChangeStrategy
   form; looked up at run time so the APK keeps minSdk 29 */
typedef int32_t (*SetRateFn)(void *window, float rate, int8_t compat);
typedef int32_t (*SetRateStrategyFn)(void *window, float rate, int8_t compat, int8_t strategy);

enum { COMPAT_DEFAULT = 0, COMPAT_FIXED_SOURCE = 1, STRATEGY_ONLY_IF_SEAMLESS = 0 };

static DisplayPin s_pin;
static int s_pinResumed; /* window_frame_rate_pin_resumed's call */

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

static int pin_call(void *nw, int rate, const char **call)
{
    const SetRateStrategyFn withStrategy = s_withStrategy;
    const SetRateFn plain = s_plain;
    const int8_t compat = rate > 0 ? COMPAT_FIXED_SOURCE : COMPAT_DEFAULT;

    /* only a seamless switch: a mode change that blinks the screen is not
       asked for */
    if (withStrategy != NULL) {
        *call = "ANativeWindow_setFrameRateWithChangeStrategy";
        return withStrategy(nw, (float)rate, compat, STRATEGY_ONLY_IF_SEAMLESS);
    }
    if (plain != NULL) {
        *call = "ANativeWindow_setFrameRate";
        return plain(nw, (float)rate, compat);
    }
    *call = NULL;
    return -1000;
}

void window_frame_rate_pin(SDL_Window *window, int framerate)
{
    static int told;
    const DisplayPinMode mode = display_pin_mode(getenv("ICO_FRAME_RATE_PIN"));
    void *nw = window != NULL ? SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                       SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL)
                              : NULL;
    const int want = display_pin_rate(framerate, mode);
    const char *call = NULL;
    char line[320];
    DisplayPinAct act;
    int ret;

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
        return;
    }
    ret = pin_call(nw, act == DISPLAY_PIN_RELEASE ? 0 : want, &call);
    if (ret != 0) {
        display_pin_failed(&s_pin);
    }
    display_pin_text(act, want, framerate, call, ret, line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
}

void window_frame_rate_pin_resumed(SDL_Window *window, int framerate)
{
    s_pinResumed = 1;
    window_frame_rate_pin(window, framerate);
}
#else
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
#endif

void window_display_rate_stop(SDL_Window *window)
{
#ifdef __ANDROID__
    /* the ask released while the window still exists (Original wants none) */
    window_frame_rate_pin(window, 0);
#else
    (void)window;
#endif
    if (s_watching) {
        SDL_RemoveEventWatch(display_watch, NULL);
        s_watching = 0;
    }
    if (s_lock != NULL) {
        SDL_DestroyMutex(s_lock);
        s_lock = NULL;
    }
}
