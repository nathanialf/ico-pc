#include "hand-camera.h"
#include "act-game.h"
#include "gv.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include <math.h>
#include <string.h>
#include <libvu0.h>
#include "main.h"

/* PC port: port/input/mouse_camera.c. The factor on the follow speed (exactly
   1.0f unless the mouse's stick is moving the camera) and whether the mouse
   may turn the camera past the area's angle limits. */
extern float ico_mouse_camera_speed(void);
extern int ico_mouse_camera_full_range(void);
/* PC port: the step toward the target for a speed other than 1 */
extern void ico_mouse_camera_step(float *da, float *db, float d, float spd, float k);

/* the correction rate scaled by the frame budget, and the correction mode
   HandCameraCorrect is called with */
static float handCameraRate; /* derived name */

static unsigned char handCameraMode; /* derived name */

/* the correction work area, two angles and the two limits
   SetLimitHandCameraCorrect writes */
static float handCameraWork[7]; /* derived name */

static void RotateAccordingToStick_PatternThree(float *pitch, float *yaw, float x, float y)
{
    float *p = handCameraWork;
    float len = FSqrt(x * x + y * y);
    float ang = atan2f(y, x);
    float v[4] = {len, 0.0f, 0.0f, 0.0f};
    float spd;
    float da;
    float db;
    float t;
    float d;
    float k;

    _ApplyRyGV(v, -ang);
    x = v[0];
    y = v[2];

    if (len < 0.1f) {
        if (handCameraMode == 0)
            spd = handCameraRate * 0.008726646f * _ACTGame_GetParamF(16);
        else
            spd = handCameraRate * 0.008726646f * _ACTGame_GetParamF(18);
    } else if (handCameraMode == 0)
        spd = handCameraRate * 0.008726646f * _ACTGame_GetParamF(15);
    else
        spd = handCameraRate * 0.008726646f * _ACTGame_GetParamF(17);

    /* PC port: the mouse camera speed, applied by ico_mouse_camera_step */
    k = ico_mouse_camera_speed();

    db = x * (p[5] * 3.1415927f / 180.0f) - *yaw;

    if (y > 0.0f) {
        t = p[2];
    } else {
        t = p[3];
        if (t < 0.0f)
            t = -t;
    }
    da = y * t - *pitch;

    d = FSqrt(da * da + db * db);

    if (k != 1.0f) {
        /* PC port: the same step for the mouse camera's other speeds */
        ico_mouse_camera_step(&da, &db, d, spd, k);
    } else if (d < spd * 10.0f) {
        da = da / 10.0f;
        db = db / 10.0f;
    } else if (spd < d) {
        da = da * spd / d;
        db = db * spd / d;
    }
    *pitch += da;
    *yaw += db;
}

static void SetCurrentInfo(void *eye, void *at)
{
    float *p = handCameraWork;
    float v[4];
    float w[4];
    float ang;
    float t;
    float mx;
    float mn;

    sceVu0SubVector(v, at, eye);
    w[0] = v[0];
    w[1] = 0.0f;
    w[2] = v[2];
    ang = _RotGVF(w, v);
    p[4] = ang;
    if (v[1] > 0.0f)
        p[4] = -ang;

    if (handCameraMode == 0) {
        t = p[6] * 3.1415927f / 180.0f;
        mx = (t < p[4]) ? p[4] : t;
        p[2] = mx - p[4];
        mn = (p[4] < -t) ? p[4] : -t;
        p[3] = mn - p[4];
    } else {
        mx = p[4] + p[6] * 3.1415927f / 180.0f;
        mx = (mx < -1.4835298f) ? -1.4835298f : ((1.4835298f < mx) ? 1.4835298f : mx);
        p[2] = mx - p[4];
        mn = p[4] - p[6] * 3.1415927f / 180.0f;
        mn = (mn < -1.4835298f) ? -1.4835298f : ((1.4835298f < mn) ? 1.4835298f : mn);
        p[3] = mn - p[4];
    }
}

static void HandyCamera_TargetMoveType(void *eye, void *at)
{
    float *p = handCameraWork;
    float q[4];
    float d[4];
    float m[16];
    float q2[4];
    float v0[4];
    float v1[4];
    float n[4];
    float q3[4];

    sceVu0SubVector(d, at, eye);

    SetIdentityQuaternion(q);

    SetIdentityQuaternion(q2);
    SetQuaternionByAxisRotate(q2, (short)(int)(-(p[1] * 32768.0f / 3.1415927f)), 0.0f, 1.0f, 0.0f);
    MultiQuaternion(q, q, q2);

    v0[0] = d[0];
    v0[1] = d[1];
    v0[2] = d[2];
    v1[0] = d[0];
    v1[1] = 0.0f;
    v1[2] = d[2];
    sceVu0OuterProduct(n, v0, v1);
    sceVu0Normalize(n, n);
    if (v0[1] < 0.0f)
        sceVu0ScaleVector(n, n, -1.0f);

    SetIdentityQuaternion(q3);
    SetQuaternionByAxisRotate(q3, (short)(int)(-(p[0] * 32768.0f / 3.1415927f)), n[0], n[1], n[2]);
    MultiQuaternion(q, q, q3);

    GetMatrixFromQuaternionPos(m, q, eye);

    d[3] = 0.0f;
    sceVu0ApplyMatrix(d, m, d);
    sceVu0AddVector(at, eye, d);
}

inline void ClearHandCameraCorrect(void)
{
    int a = systemStatus[0];
    int b = systemStatus[1];
    int t = a * 10;
    int diff = 60 - t;
    int q;
    *(int *)&handCameraWork[0] = 0;
    *(int *)&handCameraWork[1] = 0;
    q = diff / b;
    handCameraRate = 60.0f / (float)q;
}

inline void InitHandCameraCorrect(void)
{
    int a = systemStatus[0];
    int b = systemStatus[1];
    int t = a * 10;
    int diff = 60 - t;
    int q;
    *(int *)&handCameraWork[0] = 0;
    *(int *)&handCameraWork[1] = 0;
    q = diff / b;
    handCameraWork[5] = 120.0f;
    handCameraWork[6] = 80.0f;
    handCameraRate = 60.0f / (float)q;
}

inline void SetLimitHandCameraCorrect(float limitP, float limitV)
{
    handCameraWork[5] = limitP;
    handCameraWork[6] = limitV;
}

void HandCameraCorrect(void *eye, void *at, int mode, float stickX, float stickZ, float rate)
{
    float *p = handCameraWork;
    float savedYawLimit = p[5];
    float savedPitchLimit = p[6];

    handCameraRate = rate;
    handCameraMode = mode;

    /* PC port: the mouse camera's full range lets the mouse look all the way
       around and up to the pitch stop, never less than the area's own limits */
    if (ico_mouse_camera_full_range()) {
        if (p[5] < 180.0f)
            p[5] = 180.0f;
        if (p[6] < 85.0f)
            p[6] = 85.0f;
    }

    SetCurrentInfo(eye, at);

    RotateAccordingToStick_PatternThree(p, p + 1, stickX, -stickZ);

    HandyCamera_TargetMoveType(eye, at);

    /* PC port: the area's own limits again */
    p[5] = savedYawLimit;
    p[6] = savedPitchLimit;
}
