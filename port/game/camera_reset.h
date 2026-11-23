/*
 * port/game/camera_reset.h
 *
 * Camera reset ([gameplay] camera_reset, options.h; GAME_FEATURES.md
 * "Camera reset").
 *
 * In play the right stick turns the camera away from the boy: each copy of
 * SetCameraMatrix_Ico2 (ico2/omori/src/camera-ico2.c, the European body and
 * the _ntsc, _aug6 and _jun26 twins) hands the stick to HandCameraCorrect
 * (ico2/omori/src/hand-camera.c), which keeps the look-around as two angles
 * (handCameraWork[0] pitch, [1] yaw) that step toward the stick's target
 * every tick, a tenth of the way near it, and ease back to 0 when the stick
 * is let go.  ClearHandCameraCorrect sets both to 0 at once: the game does
 * that itself whenever the boy's control is off, the game is paused or a
 * scripted shot holds the camera.  The game's own follow camera
 * (monitorMonitorCamera, CameraMove) is separate and is not touched.
 *
 * With the option on, a press of R3 (the right stick click, the game's pad
 * bit 0x400; Remap controls moves it to any key or button) while the
 * look-around is live makes the arm call ClearHandCameraCorrect in that
 * tick, so the picture is the one the game shows with the stick never
 * moved; the presenter is told it is a cut (no blend from the turned
 * picture), and a toggled zoom (zoom_toggle.h) ends too.  The stick is then
 * ignored until it is back in its dead zone (the game's own: both values 0),
 * so a stick still held does not swing the camera straight out again.  The
 * press is the level against the previous tick's, as Zoom type's.
 *
 * The state (the last level, the mute) is the port's: one FRESH save-state
 * block, "camera_reset.state".  Off, every call passes its input through,
 * tick for tick.
 */
#ifndef ICO_PORT_GAME_CAMERA_RESET_H
#define ICO_PORT_GAME_CAMERA_RESET_H

/* One look-around tick.  r3: the pad's R3 bit (pad[0].now & 0x400); live:
   the look-around runs this tick (the arm's own test: not paused, the boy
   under control, no scripted shot); sx, sz: the right stick as the arm
   hands it to HandCameraCorrect.  Off: returns 0 and leaves sx, sz as they
   are (records the level).  On: a press while live returns 1 (the arm then
   calls ClearHandCameraCorrect), ends a toggled zoom, marks a cut and
   mutes the stick; while muted sx and sz read 0 until both come in 0;
   !live ends the mute. */
int ico_cam_reset(int r3, int live, float *sx, float *sz);
/* Nothing recorded, no mute (tests). */
void ico_cam_reset_clear(void);
/* The save-state block (state_port.h): 0, or -1 when refused. */
int ico_cam_reset_state_register(void);
/* Resets made this run (the test and the log). */
unsigned long ico_cam_reset_count(void);

#endif /* ICO_PORT_GAME_CAMERA_RESET_H */
