/*
 * port/platform/window_lifecycle.c
 *
 * The lifecycle tables (window_lifecycle.h).
 */
#include "window_lifecycle.h"

#include <stddef.h>
#include <stdio.h>

/* The order matters. Going to the background, the GPU finishes before the
   surface it presents to goes; the watchdog stops counting before SDL
   blocks the pump; the log is on the disc last, in case the system ends
   the process while it waits. Coming back, the surface is made before the
   watchdog counts again (it can take a moment), the sound resumes on an
   empty queue (no stale half second), and the pacer starts from now. */
static const IcoLifecycleStep k_background[] = {ICO_LC_LOG_LINE,
                                                ICO_LC_GPU_WAIT_IDLE,
                                                ICO_LC_SURFACE_RELEASE,
                                                ICO_LC_WATCHDOG_PAUSE,
                                                ICO_LC_AUDIO_PAUSE,
                                                ICO_LC_LOG_FLUSH,
                                                ICO_LC_END};

static const IcoLifecycleStep k_foreground[] = {ICO_LC_SURFACE_RECREATE, ICO_LC_WATCHDOG_RESUME,
                                                ICO_LC_AUDIO_RESUME,     ICO_LC_PACE_RESET,
                                                ICO_LC_LOG_LINE,         ICO_LC_END};

/* the recording's closing line goes into the log flush that follows */
static const IcoLifecycleStep k_terminating[] = {ICO_LC_LOG_LINE, ICO_LC_CONFIG_SAVE,
                                                 ICO_LC_RECORD_CLOSE, ICO_LC_LOG_FLUSH, ICO_LC_END};

static const IcoLifecycleStep k_low_memory[] = {ICO_LC_TEXTURE_LOW_MEMORY, ICO_LC_LOG_LINE,
                                                ICO_LC_LOG_FLUSH, ICO_LC_END};

static const struct {
    const char *name;
    const IcoLifecycleStep *steps;
} k_table[ICO_LIFECYCLE_COUNT] = {
    [ICO_LIFECYCLE_WILL_ENTER_BACKGROUND] = {"will enter background", k_background},
    [ICO_LIFECYCLE_DID_ENTER_FOREGROUND] = {"did enter foreground", k_foreground},
    [ICO_LIFECYCLE_TERMINATING] = {"terminating", k_terminating},
    [ICO_LIFECYCLE_LOW_MEMORY] = {"low memory", k_low_memory},
};

const IcoLifecycleStep *ico_lifecycle_steps(IcoLifecycleEvent event)
{
    if ((int)event <= (int)ICO_LIFECYCLE_NONE || (int)event >= (int)ICO_LIFECYCLE_COUNT) {
        return NULL;
    }
    return k_table[event].steps;
}

const char *ico_lifecycle_name(IcoLifecycleEvent event)
{
    if (ico_lifecycle_steps(event) == NULL) {
        return "none";
    }
    return k_table[event].name;
}

/* One step; 1 when an operation ran */
static int run_step(IcoLifecycleStep step, IcoLifecycleEvent event, const IcoLifecycleOps *o)
{
    void *u = o->user;

    switch (step) {
    case ICO_LC_GPU_WAIT_IDLE:
        if (o->gpu_wait_idle == NULL) {
            return 0;
        }
        o->gpu_wait_idle(u);
        return 1;
    case ICO_LC_SURFACE_RELEASE:
        if (o->surface_release == NULL) {
            return 0;
        }
        o->surface_release(u);
        return 1;
    case ICO_LC_SURFACE_RECREATE:
        if (o->surface_recreate == NULL) {
            return 0;
        }
        if (o->surface_recreate(u) != 0 && o->log != NULL) {
            o->log(u, "lifecycle: no surface yet; the next frame tries again");
        }
        return 1;
    case ICO_LC_WATCHDOG_PAUSE:
    case ICO_LC_WATCHDOG_RESUME:
        if (o->watchdog_pause == NULL) {
            return 0;
        }
        o->watchdog_pause(u, step == ICO_LC_WATCHDOG_PAUSE);
        return 1;
    case ICO_LC_AUDIO_PAUSE:
    case ICO_LC_AUDIO_RESUME:
        if (o->audio_pause == NULL) {
            return 0;
        }
        o->audio_pause(u, step == ICO_LC_AUDIO_PAUSE);
        return 1;
    case ICO_LC_PACE_RESET:
        if (o->pace_reset == NULL) {
            return 0;
        }
        o->pace_reset(u);
        return 1;
    case ICO_LC_CONFIG_SAVE:
        if (o->config_save == NULL || o->config_dirty == NULL || !o->config_dirty(u)) {
            return 0;
        }
        if (o->config_save(u) != 0 && o->log != NULL) {
            o->log(u, "lifecycle: the settings could not be saved");
        }
        return 1;
    case ICO_LC_RECORD_CLOSE:
        if (o->record_close == NULL) {
            return 0;
        }
        o->record_close(u);
        return 1;
    case ICO_LC_TEXTURE_LOW_MEMORY:
        if (o->texture_low_memory == NULL) {
            return 0;
        }
        o->texture_low_memory(u);
        return 1;
    case ICO_LC_LOG_LINE: {
        char line[96];

        if (o->log == NULL) {
            return 0;
        }
        snprintf(line, sizeof(line), "lifecycle: %s", ico_lifecycle_name(event));
        o->log(u, line);
        return 1;
    }
    case ICO_LC_LOG_FLUSH:
        if (o->log_flush == NULL) {
            return 0;
        }
        o->log_flush(u);
        return 1;
    case ICO_LC_END:
    case ICO_LC_STEP_COUNT:
        break;
    }
    return 0;
}

int ico_lifecycle_on(IcoLifecycleEvent event, const IcoLifecycleOps *ops)
{
    const IcoLifecycleStep *s = ico_lifecycle_steps(event);
    int n = 0;

    if (s == NULL || ops == NULL) {
        return -1;
    }
    for (; *s != ICO_LC_END; s++) {
        n += run_step(*s, event, ops);
    }
    return n;
}

IcoLifecycleEvent ico_lifecycle_from_sdl(uint32_t type)
{
    switch (type) {
    case ICO_SDL_EVENT_WILL_ENTER_BACKGROUND:
        return ICO_LIFECYCLE_WILL_ENTER_BACKGROUND;
    case ICO_SDL_EVENT_DID_ENTER_FOREGROUND:
        return ICO_LIFECYCLE_DID_ENTER_FOREGROUND;
    case ICO_SDL_EVENT_TERMINATING:
        return ICO_LIFECYCLE_TERMINATING;
    case ICO_SDL_EVENT_LOW_MEMORY:
        return ICO_LIFECYCLE_LOW_MEMORY;
    default:
        return ICO_LIFECYCLE_NONE;
    }
}
