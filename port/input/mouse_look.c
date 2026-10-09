/*
 * port/input/mouse_look.c
 *
 * The mouse camera's capture rule, Escape's button and photo mode's look
 * accumulator (mouse_look.h).
 */
#include "mouse_look.h"
#include "input.h"

static float s_dx, s_dy;

/* play: the boy in a stage past the title, on the play or scene layout,
   nothing paused, loading or playing a movie, no viewer and no credits */
static int in_play(const IcoCaptureState *s)
{
    return s->boy && s->stage != 1 &&
           (s->layout == ICO_CAPTURE_LAYOUT_PLAY || s->layout == ICO_CAPTURE_LAYOUT_SCENE) &&
           !s->paused && !s->loading && !s->movie && !s->viewer && !s->credits;
}

int ico_mouse_capture_rule(const IcoCaptureState *s)
{
    if (s == 0 || !s->focus || !s->look) {
        return ICO_CAPTURE_OFF;
    }
    if (s->photo) {
        return ICO_CAPTURE_DELTA;
    }
    if (in_play(s)) {
        return ICO_CAPTURE_STICK;
    }
    return ICO_CAPTURE_OFF;
}

int ico_escape_target(const IcoCaptureState *s)
{
    if (s == 0 || s->photo || !in_play(s)) {
        return ICO_T_TRIANGLE;
    }
    return ICO_T_START;
}

void ico_escape_latch(IcoEscapeLatch *l, int down, const IcoCaptureState *s)
{
    if (!down) {
        l->held = 0;
        return;
    }
    if (!l->held) {
        /* a tap still on the pad keeps its button: choosing again could
           cut that tap to one step, which the game may not sample */
        if (!l->tapped) {
            l->target = ico_escape_target(s);
        }
        l->held = 1;
        l->tapped = ICO_ESCAPE_TAP_STEPS;
    }
}

unsigned ico_escape_take(IcoEscapeLatch *l)
{
    const int on = l->held || l->tapped > 0;

    if (l->tapped > 0) {
        l->tapped--;
    }
    return on ? 1u << l->target : 0u;
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
