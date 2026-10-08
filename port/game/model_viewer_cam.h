/* model_viewer_cam.h: the model viewer's camera step (a pure function of the
   held buttons and the sticks, so a CPU test can drive it). */
#ifndef ICO_PORT_GAME_MODEL_VIEWER_CAM_H
#define ICO_PORT_GAME_MODEL_VIEWER_CAM_H

/* the pad's trigger bits (keyInput.c's logical word) */
#define MV_PAD_L2 0x0001
#define MV_PAD_R2 0x0002

#define MV_STICK_DEAD 24
#define MV_YAW_RATE 0.12f
#define MV_PITCH_RATE 0.08f
#define MV_PITCH_MAX 1.35f
/* per tick while L2 or R2 is held: dist *= exp(+-ZOOM_RATE) */
#define MV_ZOOM_RATE 0.05f
/* per tick at full stick, as a share of dist */
#define MV_PAN_RATE 0.03f
/* the most the model is moved up or down, as a share of dist */
#define MV_PAN_MAX 0.6f

typedef struct MvCam {
    float dist, distMin, distMax;
    /* a vertical translation of the eye and the target in the game's world
       (y points down): positive moves the camera down, so the model rises on
       the screen */
    float panY;
    float yaw, pitch;
} MvCam;

/* a stick byte (128 centred) as -1..1 with a dead zone; negative is up/left */
float mv_CamStick(int v);

/* one tick: the right stick (ana[0] x, ana[1] y) turns, L2 zooms out, R2
   zooms in, the left stick's vertical axis (ana[3]) moves the model */
void mv_CamStep(MvCam *c, unsigned now, const unsigned char ana[4]);

#endif
