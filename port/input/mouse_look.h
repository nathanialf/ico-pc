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
 * The same state decides what Escape (and Android's Back) presses: Start
 * in play, so the pause menu opens, nothing on the game's Button
 * configuration screen, Triangle (back) everywhere else.
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
/* The game's Button configuration screen (la_key_config): any single
   button there is assigned to the highlighted action, and the screen is
   left through its OK row. */
#define ICO_CAPTURE_LAYOUT_KEY_CONFIG 59

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

/* The steps (ico_escape_take calls, one a vsync) a press stays on the pad
   at least: the game reads the pad once a Main tick, every second vsync,
   so a press and release inside one pump (Android's back gesture) must
   last two steps to be seen. */
#define ICO_ESCAPE_TAP_STEPS 2

/* No button: ico_escape_target's answer where Escape presses nothing. */
#define ICO_ESCAPE_NONE (-1)

/* The pad button Escape and Android's Back press: ICO_ESCAPE_NONE on the
   Button configuration screen (ICO_CAPTURE_LAYOUT_KEY_CONFIG, where
   Triangle would be assigned to the highlighted action), ICO_T_START in
   play (the same test as ICO_CAPTURE_STICK, without the focus and mouse
   camera conditions), ICO_T_TRIANGLE anywhere else (the pause menu, the
   title, the port's pages, photo mode). The quit is the title's row or the
   window's close button, never Escape. */
int ico_escape_target(const IcoCaptureState *s);

/* The key held: the target is chosen at the press and kept until the
   release, so a held Escape that opens the pause menu does not turn into
   Triangle on the next vsync (now paused) and close it again. A press
   stays on the pad for ICO_ESCAPE_TAP_STEPS steps even when it was let go
   sooner (tapped counts them down); a new press inside those steps keeps
   the button and starts the count again. */
typedef struct IcoEscapeLatch {
    int held;
    int tapped;
    int target;
} IcoEscapeLatch;

/* A press (down 1) latches ico_escape_target(s) unless already held or
   still on the pad; a release (down 0) lets go. */
void ico_escape_latch(IcoEscapeLatch *l, int down, const IcoCaptureState *s);
/* Once a step: the latched target as a pad button bit (1u << target) while
   held or within ICO_ESCAPE_TAP_STEPS steps of the press, else 0 (always
   0 for ICO_ESCAPE_NONE). */
unsigned ico_escape_take(IcoEscapeLatch *l);

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
