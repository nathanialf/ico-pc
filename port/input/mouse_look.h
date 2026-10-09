/*
 * port/input/mouse_look.h
 *
 * The mouse camera's capture rule and photo mode's look accumulator
 * (issue 17). Plain C, no SDL and no game symbols: the
 * window (port/platform/window_host.c) fills IcoCaptureState from the
 * game's state once a vsync, asks the rule, and hands the answer to the
 * device layer (input_sdl.c ico_input_sdl_set_capture).
 *
 *   ICO_CAPTURE_OFF    the pointer is free; the mouse's motion is dropped
 *   ICO_CAPTURE_STICK  play: the pointer is hidden and held (relative
 *                      mode), its motion is the right stick (bindings.c's
 *                      held look offset)
 *   ICO_CAPTURE_DELTA  photo mode: hidden and held, its motion goes to the
 *                      accumulator here, which photo mode's screen
 *                      (port/ui/photo_ui.c) takes once a Main tick and
 *                      turns into degrees of the photo camera
 *
 * The game runs on a fiber of the window's thread, so the accumulator needs
 * no lock.
 */
#ifndef ICO_PORT_INPUT_MOUSE_LOOK_H
#define ICO_PORT_INPUT_MOUSE_LOOK_H

#ifdef __cplusplus
extern "C" {
#endif

enum { ICO_CAPTURE_OFF = 0, ICO_CAPTURE_STICK = 1, ICO_CAPTURE_DELTA = 2 };

/* The game layout play runs on (layout_texture.c init_layout_texture), and
   the one of its scenes. */
#define ICO_CAPTURE_LAYOUT_PLAY 54
#define ICO_CAPTURE_LAYOUT_SCENE 55

typedef struct IcoCaptureState {
    int focus;   /* the window has the keyboard focus */
    int look;    /* [input] mouse and mouse_camera both on */
    int photo;   /* photo mode is open (photo_mode.h ico_photo_active) */
    int boy;     /* the boy exists (boyGObj) */
    int stage;   /* stage_no: 1 is the boot and the title */
    int layout;  /* current_layout_id */
    int paused;  /* systemStatus[5]: the pause menu or a title proc */
    int loading; /* data_loading */
    int movie;   /* mpegPlay: a movie plays */
    int viewer;  /* ico_mv_active: the model viewer's stage */
    int credits; /* ico_credits_active: the Extras credits */
} IcoCaptureState;

/* ICO_CAPTURE_DELTA in photo mode, ICO_CAPTURE_STICK in play (the boy in
   a stage past the title, on the play or scene layout, nothing paused,
   loading or playing a movie, no viewer and no credits), else
   ICO_CAPTURE_OFF; always OFF without the focus or with the mouse camera
   off. */
int ico_mouse_capture_rule(const IcoCaptureState *s);

/* Photo mode's accumulator: the device layer adds the captured motion
   (mouse counts, y down), the photo screen takes it all once a Main tick
   (returns 1 when anything had moved; *dx, *dy 0 otherwise); reset drops
   it (any capture mode change). */
void ico_mouse_look_add(float dx, float dy);
int ico_mouse_look_take(float *dx, float *dy);
void ico_mouse_look_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_INPUT_MOUSE_LOOK_H */
