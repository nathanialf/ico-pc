/*
 * port/game/camera_reset.c
 *
 * Camera reset's press and stick mute (camera_reset.h).
 */
#include "camera_reset.h"

#include <stdio.h>

#include "options.h"
#include "video_options.h"
#include "zoom_toggle.h"
#include "../platform/state_port.h"
#include "../platform/state_registry.h"

/* prev: R3's level the last tick saw; mute: the stick is ignored until it
   is back in its dead zone */
typedef struct CamResetState {
    int prev;
    int mute;
} CamResetState;

static CamResetState s_cr;
static unsigned long s_count;

/* the Main tick clock (camera_reset.h ico_cam_reset_set_clock) and the tick
   of the last call: a call that does not follow the last one by one tick is
   the first of a stretch of control */
static unsigned (*s_clock)(void);
static unsigned s_last;
static int s_haveLast;

void ico_cam_reset_set_clock(unsigned (*clock)(void))
{
    s_clock = clock;
    s_haveLast = 0;
}

/* FRESH: a state saved before the block existed loads with no mute */
int ico_cam_reset_state_register(void)
{
    return ico_state_register("camera_reset.state", &s_cr, sizeof s_cr, ICO_STATE_FRESH) != 0 ? -1
                                                                                              : 0;
}

int ico_cam_reset(int r3, int live, float *sx, float *sz)
{
    const int level = r3 != 0;
    int press;
    int fired = 0;

    if (s_clock != NULL) {
        const unsigned now = s_clock();

        if (!s_haveLast || (now != s_last + 1u && now != s_last)) {
            /* the first tick after a cutscene, a pause, photo mode or a
               load: R3 already down is held, not pressed */
            s_cr.prev = level;
            s_cr.mute = 0;
        }
        s_last = now;
        s_haveLast = 1;
    }
    press = level && !s_cr.prev;
    s_cr.prev = level;
    if (!ico_opt_camera_reset()) {
        s_cr.mute = 0;
        return 0;
    }
    if (!live) {
        s_cr.mute = 0;
        return 0;
    }
    if (press) {
        fired = 1;
        s_cr.mute = 1;
        s_count++;
        ico_zoom_drop();
        ico_video_camera_cut();
        if (s_count <= 8 || s_count % 100 == 0) {
            fprintf(stderr, "camera: reset to the boy (%lu this run)\n", s_count);
        }
    }
    if (s_cr.mute) {
        if (*sx == 0.0f && *sz == 0.0f) {
            s_cr.mute = 0;
        } else {
            *sx = 0.0f;
            *sz = 0.0f;
        }
    }
    return fired;
}

void ico_cam_reset_clear(void)
{
    s_cr.prev = 0;
    s_cr.mute = 0;
    s_haveLast = 0;
}

unsigned long ico_cam_reset_count(void)
{
    return s_count;
}
