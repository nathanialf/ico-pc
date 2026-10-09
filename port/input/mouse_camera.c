/*
 * port/input/mouse_camera.c
 *
 * The mouse camera's speed and range for the game's hand camera
 * (mouse_camera.h).
 */
#include "input.h"
#include "mouse_camera.h"

float ico_mouse_camera_speed(void)
{
    const IcoBindings *b = ico_input_live_bindings();

    if (b->mouse_drives && b->mouse_camera_speed > 0.0f) {
        return b->mouse_camera_speed;
    }
    return 1.0f;
}

int ico_mouse_camera_full_range(void)
{
    const IcoBindings *b = ico_input_live_bindings();

    return b->mouse_drives && b->mouse_full_range;
}

void ico_mouse_camera_step(float *da, float *db, float d, float spd, float k)
{
    /* the farther move: spd * k (spd * 1.0f is spd exactly) */
    const float lim = spd * k;

    /* the close zone is the game's own (spd * 10), so both zones move by
       lim where they meet: k * spd / 10 of the remainder at d = spd * 10 */
    if (d < spd * 10.0f) {
        if (k == 1.0f) {
            *da = *da / 10.0f;
            *db = *db / 10.0f;
        } else {
            const float frac = 0.1f * k < 1.0f ? 0.1f * k : 1.0f;

            *da = *da * frac;
            *db = *db * frac;
        }
    } else if (lim < d) {
        *da = *da * lim / d;
        *db = *db * lim / d;
    }
}
