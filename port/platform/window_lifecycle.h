/*
 * port/platform/window_lifecycle.h
 *
 * What the window build does when the system moves the app (package AN-D,
 * Android): one table of steps per lifecycle event, run in order through
 * a set of operations the caller supplies, so a unit test checks the order
 * with fake operations (port/platform/test/lifecycle_test.c) and
 * window_host.c plugs in the real ones. Plain C, no SDL.
 *
 *   will enter background  the GPU idle, the window's surface released
 *                          (the device kept), the watchdog paused, the
 *                          sound paused, the log flushed: SDL then blocks
 *                          the event pump until the app comes back
 *   did enter foreground   the surface and swapchain made again, the
 *                          watchdog running, the sound resumed with its
 *                          queue emptied, the pacer's deadline reset to
 *                          now (the time away is not caught up), a line
 *   terminating            the settings saved when changed, the pad
 *                          recording closed, the log flushed
 *   low memory             the texture pack's read-ahead held at what it
 *                          has, a line, the log flushed
 *
 * SDL3 delivers these four only to an event watch (SDL_AddEventWatch), on
 * the thread that pumps the events, as they happen; window_host.c installs
 * one on Android and maps SDL's event types with ico_lifecycle_from_sdl.
 */
#ifndef ICO_PLATFORM_WINDOW_LIFECYCLE_H
#define ICO_PLATFORM_WINDOW_LIFECYCLE_H

#include <stdint.h>

typedef enum IcoLifecycleEvent {
    ICO_LIFECYCLE_NONE = 0,
    ICO_LIFECYCLE_WILL_ENTER_BACKGROUND,
    ICO_LIFECYCLE_DID_ENTER_FOREGROUND,
    ICO_LIFECYCLE_TERMINATING,
    ICO_LIFECYCLE_LOW_MEMORY,
    ICO_LIFECYCLE_COUNT
} IcoLifecycleEvent;

/* The steps the tables are made of, one per operation (a pause and its
   resume are two steps of one operation). */
typedef enum IcoLifecycleStep {
    ICO_LC_END = 0,            /* the end of a table */
    ICO_LC_GPU_WAIT_IDLE,      /* ops.gpu_wait_idle */
    ICO_LC_SURFACE_RELEASE,    /* ops.surface_release */
    ICO_LC_SURFACE_RECREATE,   /* ops.surface_recreate */
    ICO_LC_WATCHDOG_PAUSE,     /* ops.watchdog_pause(1) */
    ICO_LC_WATCHDOG_RESUME,    /* ops.watchdog_pause(0) */
    ICO_LC_AUDIO_PAUSE,        /* ops.audio_pause(1) */
    ICO_LC_AUDIO_RESUME,       /* ops.audio_pause(0): resumed, the queue emptied */
    ICO_LC_PACE_RESET,         /* ops.pace_reset */
    ICO_LC_CONFIG_SAVE,        /* ops.config_save, only when ops.config_dirty says so */
    ICO_LC_RECORD_CLOSE,       /* ops.record_close */
    ICO_LC_TEXTURE_LOW_MEMORY, /* ops.texture_low_memory */
    ICO_LC_LOG_LINE,           /* ops.log with the event's line */
    ICO_LC_LOG_FLUSH,          /* ops.log_flush */
    ICO_LC_STEP_COUNT
} IcoLifecycleStep;

/* The operations. Any may be NULL (that step is skipped); user is passed
   to each. */
typedef struct IcoLifecycleOps {
    void *user;
    void (*gpu_wait_idle)(void *user);
    void (*surface_release)(void *user);
    /* 0 made, -1 not (logged; the renderer tries again at the next frame) */
    int (*surface_recreate)(void *user);
    void (*watchdog_pause)(void *user, int paused);
    void (*audio_pause)(void *user, int paused);
    void (*pace_reset)(void *user);
    int (*config_dirty)(void *user); /* nonzero: there are unsaved settings */
    int (*config_save)(void *user);  /* 0, or -1 (logged) */
    void (*record_close)(void *user);
    void (*texture_low_memory)(void *user);
    void (*log)(void *user, const char *line);
    void (*log_flush)(void *user);
} IcoLifecycleOps;

/* Runs the event's table in order. Returns the number of steps run (a
   NULL operation or a clean config is not counted), or -1 for an event
   outside the table (ICO_LIFECYCLE_NONE and anything past it). */
int ico_lifecycle_on(IcoLifecycleEvent event, const IcoLifecycleOps *ops);

/* The event's table, ending with ICO_LC_END; NULL outside the table. */
const IcoLifecycleStep *ico_lifecycle_steps(IcoLifecycleEvent event);

/* The event's name for the log ("will enter background", ...); "none"
   outside the table. */
const char *ico_lifecycle_name(IcoLifecycleEvent event);

/* SDL3's event types (SDL_events.h, spelled out so this file needs no SDL;
   window_host.c checks them against SDL's at compile time). */
#define ICO_SDL_EVENT_TERMINATING 0x101u
#define ICO_SDL_EVENT_LOW_MEMORY 0x102u
#define ICO_SDL_EVENT_WILL_ENTER_BACKGROUND 0x103u
#define ICO_SDL_EVENT_DID_ENTER_FOREGROUND 0x106u

/* An SDL event type to the lifecycle event it is, ICO_LIFECYCLE_NONE for
   every other type (DID_ENTER_BACKGROUND and WILL_ENTER_FOREGROUND need
   nothing: their partners do the work). */
IcoLifecycleEvent ico_lifecycle_from_sdl(uint32_t type);

#endif /* ICO_PLATFORM_WINDOW_LIFECYCLE_H */
