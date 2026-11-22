/*
 * port/game/zoom_toggle.c
 *
 * Zoom type's latch (zoom_toggle.h).
 */
#include "zoom_toggle.h"

#include "options.h"
#include "../platform/state_port.h"
#include "../platform/state_registry.h"

/* latch: a toggled zoom is in; prev: the level the last tick saw; source and
   mode: the last tick's (-1: none yet, so the first tick only records) */
typedef struct ZoomToggleState {
    int latch;
    int prev;
    int source;
    int mode;
} ZoomToggleState;

static ZoomToggleState s_zoom = {0, 0, -1, -1};

/* FRESH: a state saved before the block existed loads, keeping this run's
   words (the latch then clears at the next new source or mode, or stays as
   the player left it) */
int ico_zoom_state_register(void)
{
    return ico_state_register("zoom_toggle.state", &s_zoom, sizeof s_zoom, ICO_STATE_FRESH) != 0
               ? -1
               : 0;
}

int ico_zoom_held(int held, int override, int live, int source, int mode)
{
    const int level = held != 0;

    if (ico_opt_zoom_type() != ICO_ZOOM_TOGGLE) {
        s_zoom.latch = 0;
        s_zoom.prev = level;
        s_zoom.source = source;
        s_zoom.mode = mode;
        return held;
    }
    if (override != 0 || live == 0 || source != s_zoom.source || mode != s_zoom.mode) {
        s_zoom.latch = 0;
        s_zoom.prev = level;
        s_zoom.source = source;
        s_zoom.mode = mode;
        return 0;
    }
    if (level != 0 && s_zoom.prev == 0) {
        s_zoom.latch = !s_zoom.latch;
    }
    s_zoom.prev = level;
    return s_zoom.latch;
}

int ico_zoom_wanted(int held)
{
    return ico_opt_zoom_type() == ICO_ZOOM_TOGGLE ? s_zoom.latch : held;
}

void ico_zoom_reset(void)
{
    s_zoom.latch = 0;
    s_zoom.prev = 0;
    s_zoom.source = -1;
    s_zoom.mode = -1;
}
