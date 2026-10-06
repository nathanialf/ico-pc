/*
 * port/game/photo_mode.h
 *
 * Photo mode's state (package PHOTO; docs/port/DISPLAY.md "Photo mode"): a
 * free camera over the paused picture.  The pause menu's Options > "Photo
 * mode" row opens a port layout (port/ui/photo_ui.c) whose proc calls
 * ico_photo_enter, then ico_photo_update once a Main tick with the pad, and
 * ico_photo_exit on the way back.  The window (port/platform/window_host.c)
 * turns the state into the renderer's camera override every vsync
 * (ico_photo_camera, rd.h rd_SetPhotoCamera) and takes the captures.
 *
 * Nothing here is game state: the game stays in its pause (the simulation
 * does not run) and reads none of it, so a trace is the same with or
 * without photo mode.  No renderer dependency (RdCamera is rd.h's plain
 * struct): the headless build links it, and a headless run walks the same
 * menu and logs the same lines, without a picture.
 *
 *   [photo] stick_speed  1.0           the sticks' rates (orbit, dolly, pan) times this
 *   [photo] invert_y     false         the left stick's up and down swapped
 *   [photo] png_dir      "screenshots" the captures' folder, in the pref folder
 *   [photo] dof          false         depth of field (not implemented: logged once)
 *
 * The controls (the game's logical pad word, as layout procs read it):
 *   left stick      orbit: yaw about the world's vertical through the pivot,
 *                   pitch about the camera's right axis (the elevation kept
 *                   within 85 degrees of the horizontal)
 *   right stick     up and down dolly (the distance to the pivot), left and
 *                   right pan (the pivot slides along the camera's right)
 *   L1, R1          roll
 *   L2, R2, Up, Down the field of view: R2 or Up narrows (zooms in), L2 or
 *                   Down widens, between 10 and 100 degrees vertically
 *   Select          back to the game's camera
 *   Square          the HUD (the port's help lines) shown or hidden
 *   Cross           a capture (ico_photo_take_capture)
 *   Triangle, Circle, Start  leave (the proc goes back to Options)
 * The pivot is the game camera's look-at point: the point of its forward
 * axis nearest the object the camera follows (ico_photo_set_subject; the
 * photo screen gives camera-root.c's default_cameratarget_gobj's root), or
 * ICO_PHOTO_FOCUS in front of the eye without one (or when that point is
 * nearer than 50 or further than 5000).
 */
#ifndef ICO_PORT_GAME_PHOTO_MODE_H
#define ICO_PORT_GAME_PHOTO_MODE_H

#include "rd.h" /* RdCamera (the struct only; nothing of rd is called) */

#ifdef __cplusplus
extern "C" {
#endif

/* the pivot's distance in front of the game camera without a subject, the
   game's centimetres */
#define ICO_PHOTO_FOCUS 400.0f
/* the picture's half height in GS pixels (the PAL scene's 512 lines), for
   the field of view the HUD shows */
#define ICO_PHOTO_HALF_H 256.0f

/* the game's logical pad bits photo mode reads (keyInput.c's word) */
#define ICO_PHOTO_L2 0x0001u
#define ICO_PHOTO_R2 0x0002u
#define ICO_PHOTO_L1 0x0004u
#define ICO_PHOTO_R1 0x0008u
#define ICO_PHOTO_TRIANGLE 0x0010u
#define ICO_PHOTO_CIRCLE 0x0020u
#define ICO_PHOTO_CROSS 0x0040u
#define ICO_PHOTO_SQUARE 0x0080u
#define ICO_PHOTO_SELECT 0x0100u
#define ICO_PHOTO_START 0x0800u
#define ICO_PHOTO_UP 0x1000u
#define ICO_PHOTO_DOWN 0x4000u

/* The object the game camera follows, in the world (its root position), at
   ico_photo_enter; NULL: none. */
void ico_photo_set_subject(const float *pos);

/* One Main tick's pad: PadState's now, flags and ana (ana[0], ana[1] the
   right stick's x and y, ana[2], ana[3] the left's; 128 centred, 0 left
   or up). */
typedef struct IcoPhotoPad {
    unsigned int held;
    unsigned int pressed;
    unsigned char ana[4];
} IcoPhotoPad;

typedef struct IcoPhotoState {
    int active, hud;
    float yaw, pitch, roll; /* radians, from the game camera */
    float dolly;            /* the eye's distance to the pivot over the game camera's */
    float pan;              /* the pivot's slide along the camera's right, over that distance */
    float zoom;             /* magnification over the game's projection (1: the game's) */
    unsigned int captures;  /* Cross presses since ico_photo_enter */
} IcoPhotoState;

/* Opens the mode at the game's camera (logs "photo: enter"); ico_photo_exit
   closes it ("photo: exit"). */
void ico_photo_enter(void);
void ico_photo_exit(void);
int ico_photo_active(void);
/* One Main tick (1 / ico_photo_tick_hz s) of pad input; returns 1 when
   the pad asks to leave (Triangle, Circle, Start pressed), else 0.  Does
   nothing (0) while inactive. */
int ico_photo_update(const IcoPhotoPad *pad);
/* The ticks a second ico_photo_update assumes (25: PAL's frame step 2). */
void ico_photo_set_tick_hz(int hz);
/* The camera override for the game camera game: its view orbited and its
   proj43 narrowed or widened about the picture's centre (zoom also scaled);
   the other fields game's.  With nothing moved, *out is *game exactly.
   Returns 0 (out untouched) when game's view does not invert. */
int ico_photo_camera(RdCamera *out, const RdCamera *game);
/* The vertical field of view out shows, degrees, from its proj43. */
float ico_photo_fov_deg(const RdCamera *cam);
/* 1 once for each capture asked for (Cross) since the last call. */
int ico_photo_take_capture(void);
int ico_photo_hud(void);
void ico_photo_get(IcoPhotoState *out);
/* [photo]: stick_speed, invert_y, png_dir, dof (read at each enter) */
float ico_photo_stick_speed(void);
int ico_photo_invert_y(void);
const char *ico_photo_png_dir(void);
int ico_photo_dof(void);
/* The capture's file name for a local time (ico-YYYYMMDD-HHMMSS.png, then
   -2, -3 ... when seq > 1), into buf. */
void ico_photo_file_name(char *buf, unsigned size, int year, int mon, int day, int hour, int min,
                         int sec, int seq);
/* Tests: back to the state at start-up. */
void ico_photo_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_PHOTO_MODE_H */
