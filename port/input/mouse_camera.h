/*
 * port/input/mouse_camera.h
 *
 * What the game's hand camera asks of the mouse camera's options: how fast
 * to follow the stick, whether to ignore the area's angle limits, and the
 * step arithmetic for a speed other than 1. The speed and the range apply
 * only while the mouse's stick is what moves the camera (input.h
 * IcoBindings.mouse_drives); with the gamepad or the keys in charge, the
 * speed is exactly 1 and the range is the area's own, so the camera
 * behaves as it always did. ico2/omori/src/hand-camera.c declares the two
 * accessors itself, as the game's other calls into the port do.
 */
#ifndef ICO_PORT_INPUT_MOUSE_CAMERA_H
#define ICO_PORT_INPUT_MOUSE_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* The factor on the camera's follow speed: the mouse camera speed row while
   the mouse drives the camera, else exactly 1.0f. */
float ico_mouse_camera_speed(void);

/* Nonzero while the mouse drives the camera and the range row is Full. */
int ico_mouse_camera_full_range(void);

/* One step of the camera toward its target for a speed k other than 1. da and
   db are the remaining pitch and yaw to the target, d their length, spd the
   per step limit already multiplied by k. Close to the target (d < spd * 10)
   the camera covers min(1, k / 10) of the remainder (k == 1 is the plain
   tenth the game uses; k of 10 and more arrive at once); farther away it
   moves spd along the line to the target. */
void ico_mouse_camera_step(float *da, float *db, float d, float spd, float k);

#ifdef __cplusplus
}
#endif
#endif
