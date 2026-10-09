/*
 * port/input/mouse_look.c
 *
 * The mouse camera's capture rule and photo mode's look accumulator
 * (mouse_look.h; package I17a).
 */
#include "mouse_look.h"

static float s_dx, s_dy;

int ico_mouse_capture_rule(const IcoCaptureState *s)
{
    if (s == 0 || !s->focus || !s->look) {
        return ICO_CAPTURE_OFF;
    }
    if (s->photo) {
        return ICO_CAPTURE_DELTA;
    }
    if (s->boy && s->stage != 1 &&
        (s->layout == ICO_CAPTURE_LAYOUT_PLAY || s->layout == ICO_CAPTURE_LAYOUT_SCENE) &&
        !s->paused && !s->loading && !s->movie && !s->viewer && !s->credits) {
        return ICO_CAPTURE_STICK;
    }
    return ICO_CAPTURE_OFF;
}

void ico_mouse_look_add(float dx, float dy)
{
    s_dx += dx;
    s_dy += dy;
}

int ico_mouse_look_take(float *dx, float *dy)
{
    const int moved = s_dx != 0.0f || s_dy != 0.0f;

    if (dx) {
        *dx = s_dx;
    }
    if (dy) {
        *dy = s_dy;
    }
    s_dx = s_dy = 0.0f;
    return moved;
}

void ico_mouse_look_reset(void)
{
    s_dx = s_dy = 0.0f;
}
