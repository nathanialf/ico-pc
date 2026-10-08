/* model_viewer_cam.c: the model viewer's camera step, see the header. */
#include <math.h>

#include "model_viewer_cam.h"

float mv_CamStick(int v)
{
    int d = v - 128;
    if (d > -MV_STICK_DEAD && d < MV_STICK_DEAD) {
        return 0.0f;
    }
    return (float)(d > 0 ? d - MV_STICK_DEAD : d + MV_STICK_DEAD) / (float)(128 - MV_STICK_DEAD);
}

void mv_CamStep(MvCam *c, unsigned now, const unsigned char ana[4])
{
    /* the signs are as they were when the sticks were the other way round */
    c->yaw -= mv_CamStick(ana[0]) * MV_YAW_RATE;
    c->pitch += mv_CamStick(ana[1]) * MV_PITCH_RATE;
    c->pitch = c->pitch > MV_PITCH_MAX    ? MV_PITCH_MAX
               : c->pitch < -MV_PITCH_MAX ? -MV_PITCH_MAX
                                          : c->pitch;

    /* L2 out, R2 in; both held cancel */
    float z = (now & MV_PAD_L2 ? 1.0f : 0.0f) - (now & MV_PAD_R2 ? 1.0f : 0.0f);
    c->dist *= expf(z * MV_ZOOM_RATE);
    c->dist = c->dist < c->distMin ? c->distMin : c->dist > c->distMax ? c->distMax : c->dist;

    /* stick() is negative for up and the game's y points down: up must add
       to panY (the camera goes down, the model rises) */
    c->panY -= mv_CamStick(ana[3]) * MV_PAN_RATE * c->dist;
    /* stick right: the model moves right on the screen */
    c->panX += mv_CamStick(ana[2]) * MV_PAN_RATE * c->dist;
    float lim = MV_PAN_MAX * c->dist;
    c->panY = c->panY > lim ? lim : c->panY < -lim ? -lim : c->panY;
    c->panX = c->panX > lim ? lim : c->panX < -lim ? -lim : c->panX;
}
