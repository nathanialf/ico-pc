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
#include "host_android.h" /* the activity's display reports and mode list */
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
#ifdef __ANDROID__
/* the activity's display reports (ico_android_display_hz's count) the 10 s
   poll and the pump have seen, and the last one the pump acted on */
static int s_lineSerial, s_pumpSerial;
static DisplayReportSeen s_seen;
/* the Frame rate row's value the pin was last called with (the 10 s poll
   asks again under Uncapped) */
static int s_pinFramerate;
/* the same for window_display_refresh_hz on any thread */
static SDL_AtomicInt s_rowFramerate;
#endif

static double run_seconds(void)
{
    return (double)(SDL_GetTicksNS() - s_startNs) / 1e9;
}

double window_display_refresh_hz(double sdlHz)
{
#ifdef __ANDROID__
    /* SDL's Android side learns the rate only in SDLSurface.surfaceChanged
       (mDisplay.getRefreshRate() with the size), and a refresh change
       does not resize the surface: the activity's display listener reports
       every change (IcoActivity.startDisplayWatch), and the game paces by
       it under Uncapped only (display_refresh_pick) */
    return display_refresh_pick(SDL_GetAtomicInt(&s_rowFramerate), sdlHz,
                                (double)ico_android_display_hz(NULL, NULL, NULL));
#else
    return sdlHz;
#endif
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
    return window_display_refresh_hz(hz);
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
    double hz;

    s_startNs = SDL_GetTicksNS();
#ifdef __ANDROID__
    /* the reports before the window are the starting rate, not changes */
    {
        int modes = 0;
        const float rep = ico_android_display_hz(&s_lineSerial, NULL, &modes);

        s_pumpSerial = s_lineSerial;
        s_seen = (DisplayReportSeen){0};
        (void)display_report_new(&s_seen, (double)rep, modes);
    }
#endif
    hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);
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

void window_display_rate_pump(SDL_Window *window)
{
#ifdef __ANDROID__
    int serial = 0, modes = 0;
    const float hz = ico_android_display_hz(&serial, NULL, &modes);

    if (serial == s_pumpSerial) {
        return; /* no report since the last pump: one atomic read */
    }
    s_pumpSerial = serial;
    /* a report already acted on does nothing; under a number or Original
       nothing at all (their rate is SDL's: display_refresh_pick) */
    if (display_report_new(&s_seen, (double)hz, modes) && s_pinFramerate < 0) {
        window_frame_rate_pin(window, s_pinFramerate);
    }
#else
    (void)window;
#endif
}

void window_display_rate_line(SDL_Window *window)
{
    char line[256];
    const double hz = read_mode(window != NULL ? SDL_GetDisplayForWindow(window) : 0);
    /* the events SDL posted since the last poll (the count taken, set to 0) */
    const int events = SDL_SetAtomicInt(&s_events, 0);
    unsigned changes = events > 0 ? (unsigned)events : 0u;
    double eventSeconds = (double)SDL_GetAtomicU32(&s_eventMs) / 1000.0;

#ifdef __ANDROID__
    {
        /* the activity's reports count as SDL's events do, at their time */
        int serial = 0;
        unsigned ms = 0;

        (void)ico_android_display_hz(&serial, &ms, NULL);
        if (serial != s_lineSerial) {
            const double at = (double)ms / 1000.0 - (double)s_startNs / 1e9;

            if (changes == 0 || at > eventSeconds) {
                eventSeconds = at > 0.0 ? at : 0.0;
            }
            changes += (unsigned)(serial - s_lineSerial);
            s_lineSerial = serial;
        }
    }
#endif
    /* a change SDL announced, at the event's time; one it did not report is
       still one */
    if (display_rate_poll(&s_watch, hz, changes, eventSeconds, s_w, s_h, line, sizeof line)) {
        fprintf(stderr, "window: %s\n", line);
    }
    display_rate_text(&s_watch, s_w, s_h, s_name, run_seconds(), line, sizeof line);
    fprintf(stderr, "window: %s\n", line);
#ifdef __ANDROID__
    /* Uncapped's ask again: a mode list, a focus or a size that changed
       without a call of the pin (nothing is called when nothing changed) */
    if (s_pinFramerate < 0) {
        window_frame_rate_pin(window, s_pinFramerate);
    }
#endif
}

#ifdef __ANDROID__
/* NDK: api 30 ANativeWindow_setFrameRate, api 31 the ...WithChangeStrategy
   form; looked up at run time so the APK keeps minSdk 29 */
typedef int32_t (*SetRateFn)(void *window, float rate, int8_t compat);
typedef int32_t (*SetRateStrategyFn)(void *window, float rate, int8_t compat, int8_t strategy);

/* android/native_window.h: ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT 0,
   _FIXED_SOURCE 1; ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS 0 */
enum { COMPAT_DEFAULT = 0, COMPAT_FIXED_SOURCE = 1, STRATEGY_ONLY_IF_SEAMLESS = 0 };

/* the display's modes read at most (the Java array beyond is not read) */
enum { PIN_MODES_MAX = 64 };

static DisplayPin s_pin;
static int s_pinResumed; /* window_frame_rate_pin_resumed's call */
/* Uncapped's ask: refused once this run (not made again), the refused
   call's return, and the reason last logged (one line per new reason) */
static int s_upRefused, s_upRet;
static DisplayTopSkip s_skipSaid = DISPLAY_TOP_NOT_UNCAPPED;

/* the two calls, looked up once a run (the library stays loaded: the
   system keeps libandroid.so for the process anyway); the pin runs on the
   main thread only */
static int s_pinLooked;
static SetRateStrategyFn s_withStrategy;
static SetRateFn s_plain;
/* Android's display timing on offer, looked up only (display_choreo_text) */
static int s_choreo[3];

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
        s_choreo[0] = dlsym(lib, "AChoreographer_getInstance") != NULL;
        s_choreo[1] = dlsym(lib, "AChoreographer_postFrameCallback64") != NULL;
        s_choreo[2] = dlsym(lib, "AChoreographer_postVsyncCallback") != NULL;
    }
}

/* rate 0 releases; seamlessOnly: only the ...WithChangeStrategy call
   (Uncapped's ask; display_pin_want never asks without it) */
static int pin_call(void *nw, float rate, int seamlessOnly, const char **call)
{
    const SetRateStrategyFn withStrategy = s_withStrategy;
    const SetRateFn plain = s_plain;
    const int8_t compat = rate > 0.0f ? COMPAT_FIXED_SOURCE : COMPAT_DEFAULT;

    /* only a seamless switch: a mode change that blinks the screen is not
       asked for */
    if (withStrategy != NULL) {
        *call = "ANativeWindow_setFrameRateWithChangeStrategy";
        return withStrategy(nw, rate, compat, STRATEGY_ONLY_IF_SEAMLESS);
    }
    if (plain != NULL && !seamlessOnly) {
        *call = "ANativeWindow_setFrameRate";
        return plain(nw, rate, compat);
    }
    *call = NULL;
    return -1000;
}

/* The display's modes from the activity (IcoActivity.displayModes: the
   current mode's physical width and height, the display's rate, then each
   supported mode's width, height and rate), the rates at the current
   mode's size chosen from them */
static void read_top(DisplayTop *t)
{
    float f[3 + 3 * PIN_MODES_MAX];
    DisplayModeRate m[PIN_MODES_MAX];
    const int got = ico_android_display_modes(f, (int)(sizeof f / sizeof f[0]));
    int n = 0;

    if (got < 3) {
        display_top_rate(NULL, -1, 0, 0, t);
        return;
    }
    for (int i = 3; i + 2 < got && n < PIN_MODES_MAX; i += 3) {
        m[n].w = (int)f[i];
        m[n].h = (int)f[i + 1];
        m[n].hz = (double)f[i + 2];
        n++;
    }
    display_top_rate(m, n, (int)f[0], (int)f[1], t);
}

void window_frame_rate_pin(SDL_Window *window, int framerate)
{
    static int told;
    const DisplayPinMode mode = display_pin_mode(getenv("ICO_FRAME_RATE_PIN"));
    void *nw = window != NULL ? SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                       SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL)
                              : NULL;
    const int focused =
        window != NULL && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const char *call = NULL;
    char line[400];
    DisplayTop top;
    DisplayTopSkip skip = DISPLAY_TOP_NOT_UNCAPPED;
    DisplayPinAct act;
    int want, offers, ret;

    s_pinFramerate = framerate;
    /* before the caller's vsync lock decision reads window_refresh_ns
       (video_settings: the pin, then the lock) */
    SDL_SetAtomicInt(&s_rowFramerate, framerate);
    /* does the device offer the call (api 30 and later)? */
    pin_lookup();
    offers = nw != NULL && (s_withStrategy != NULL || s_plain != NULL);
    /* the list is read only when Uncapped could ask */
    display_top_rate(NULL, -1, 0, 0, &top);
    if (framerate < 0 && mode != DISPLAY_PIN_OFF && s_withStrategy != NULL && !s_upRefused &&
        offers) {
        read_top(&top);
    }
    if (!told) {
        told = 1;
        fprintf(stderr,
                "window: frame rate pin: %s ([dev] frame_rate_pin = %s); the device %s the "
                "call%s\n",
                mode == DISPLAY_PIN_OFF ? "turned off" : "on",
                mode == DISPLAY_PIN_OFF ? "off" : "auto", offers ? "offers" : "does not offer",
                nw == NULL ? " (no native window from SDL)" : "");
        display_choreo_text(s_choreo[0], s_choreo[1], s_choreo[2], line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
    /* a new native window (the app came back: Android made the surface
       again) holds no vote: the ask is made again */
    (void)display_pin_surface(&s_pin, nw, s_pinResumed);
    s_pinResumed = 0;
    want = display_pin_want(framerate, mode, &top, s_withStrategy != NULL, s_upRefused, focused,
                            &s_pin, &skip);
    /* why Uncapped asks for nothing, once per new reason (the ask held
       while the focus is away is not one) */
    if (offers && skip != s_skipSaid && !(skip == DISPLAY_TOP_UNFOCUSED && want > 0)) {
        s_skipSaid = skip;
        if (display_pin_skip_text(skip, &top, s_upRet, line, sizeof line) > 0) {
            fprintf(stderr, "window: %s\n", line);
        }
    }
    act = display_pin_next_uncapped(
        &s_pin, want, skip == DISPLAY_TOP_ASK || skip == DISPLAY_TOP_UNFOCUSED, offers);
    if (act == DISPLAY_PIN_NOTHING) {
        return;
    }
    ret = pin_call(nw,
                   act == DISPLAY_PIN_RELEASE ? 0.0f
                   : skip == DISPLAY_TOP_ASK  ? (float)top.hz
                                              : (float)want,
                   skip == DISPLAY_TOP_ASK, &call);
    if (ret != 0) {
        display_pin_failed(&s_pin);
    }
    if (skip == DISPLAY_TOP_ASK) {
        display_pin_top_text(act, &top, call, ret, line, sizeof line);
    } else {
        display_pin_text(act, want, framerate, call, ret, line, sizeof line);
    }
    fprintf(stderr, "window: %s\n", line);
    if (skip == DISPLAY_TOP_ASK && ret != 0 && act != DISPLAY_PIN_RELEASE) {
        /* refused: Uncapped asks no more this run (today's behaviour) */
        s_upRefused = 1;
        s_upRet = ret;
        s_skipSaid = DISPLAY_TOP_REFUSED;
        display_pin_skip_text(DISPLAY_TOP_REFUSED, &top, ret, line, sizeof line);
        fprintf(stderr, "window: %s\n", line);
    }
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
}
