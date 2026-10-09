/*
 * port/input/touch.h
 *
 * The touch overlay's input mapper (Android, and any touch screen): fingers
 * on the output become a virtual pad, the same IcoVirtualPad the bindings
 * produce, merged with it by ico_vpad_merge. Plain C, no SDL: the device
 * layer feeds it events, the overlay drawing (port/ui) reads its zones and
 * its state.
 *
 * The layout, in output pixels inside the safe area (u = the safe area's
 * height times the size setting's scale: 0.85 small, 1.0 medium, 1.2 large):
 *
 *   L1  [Select][Start]  R1     shoulders in the top corners, L2/R2 under
 *   L2   up               R2    them; Start and Select at the top centre
 *     left  right   . . . . .   the D-pad cluster right of L1/L2, above
 *        down       look pad    the stick; the look pad covers the right
 *   . . . . . . .   . . . . .   60 % x top 45 % (buttons on it win)
 *   :  left stick :      /\     the left stick's area: the left 40 % x
 *   :  (centre    :    []  ()   bottom 70 %; the face buttons as a diamond
 *   :  floats)    :      X      in the bottom right corner
 *
 * Left stick: the first finger down in its area (not on a button) is the
 * stick's centre for as long as it stays down; its offset from there is the
 * deflection, magnitude clamp(distance / R), R = 12 % of the safe height at
 * medium. Under ICO_TOUCH_STICK_DEADZONE * R it is centred (finger jitter);
 * past that the vector is not rescaled, so the game's own reading decides
 * walk and run like a real stick (fumi/ios/pad.c iosPadNormalizeStick: a
 * centred dead zone of 48 of 127.5, full at 120 after the off-axis divisor;
 * enemy_act.c:445 runs at a normalised magnitude of 0.99, which is a
 * deflection of about 0.94 along an axis, and needs the stick fix between
 * the eight directions, as on a gamepad). The run ring is drawn at
 * ICO_TOUCH_RUN_RING * R, 0.94: where a push along an axis starts the run.
 *
 * Look pad: a finger's offset from where it went down, over R, is the right
 * stick (clamped to the unit circle); after it lifts the stick decays to
 * centre by look_decay per step, the mouse look's mouse_decay (bindings.c,
 * default 0.80 per vsync).
 *
 * Buttons: a finger down on a button holds it; sliding onto another button
 * holds that one instead, sliding off every button releases it (the finger
 * stays a button finger until it lifts). A tap that goes down and up
 * between two steps is held for one step. Any number of fingers at once
 * (ICO_TOUCH_FINGERS), so the stick, the look pad and buttons combine.
 *
 * Visibility: hidden while a gamepad is connected, and 5 s after the last
 * finger event with no finger down; a finger down shows it again. Opacity
 * fades over 300 ms both ways; draw while ico_touch_opacity() > 0. The
 * mapper keeps mapping while hidden (a finger down shows it anyway, unless
 * a gamepad is connected: then touches still act, unseen).
 *
 * The wiring is input_sdl.c's (input_sdl.h): it feeds the finger events of
 * a direct touch screen, cancels every finger when the window loses focus
 * or the app goes to the background, steps the mapper once per vsync
 * (ico_touch_update) and merges its pad before the stick fix, the mirror
 * and the quantisation, so touch is treated like every other source;
 * window_host.c hands it the output's size and safe area, and the overlay
 * (port/ui/touch_ui.c) draws from the copy input_sdl.c takes at the step.
 */
#ifndef ICO_PORT_INPUT_TOUCH_H
#define ICO_PORT_INPUT_TOUCH_H

#include <stdint.h>
#include "input.h"

/* event kinds */
enum { ICO_TOUCH_DOWN = 0, ICO_TOUCH_MOVE = 1, ICO_TOUCH_UP = 2, ICO_TOUCH_CANCEL = 3 };

/* the size setting */
enum { ICO_TOUCH_SMALL = 0, ICO_TOUCH_MEDIUM = 1, ICO_TOUCH_LARGE = 2 };

/* the Touch controls setting ([input] touch_mode): Off never maps or
   draws; Auto draws with the fades and drops the touches while a gamepad
   is connected; Always draws at full opacity and maps with a gamepad too */
enum { ICO_TOUCH_MODE_OFF = 0, ICO_TOUCH_MODE_AUTO = 1, ICO_TOUCH_MODE_ALWAYS = 2 };

/* the button zones; IcoTouchLayout.button[] and the
   pressed mask (bit 1 << zone) use these */
enum {
    ICO_TOUCH_B_UP,
    ICO_TOUCH_B_DOWN,
    ICO_TOUCH_B_LEFT,
    ICO_TOUCH_B_RIGHT,
    ICO_TOUCH_B_CROSS,
    ICO_TOUCH_B_CIRCLE,
    ICO_TOUCH_B_SQUARE,
    ICO_TOUCH_B_TRIANGLE,
    ICO_TOUCH_B_L1,
    ICO_TOUCH_B_R1,
    ICO_TOUCH_B_L2,
    ICO_TOUCH_B_R2,
    ICO_TOUCH_B_SELECT,
    ICO_TOUCH_B_START,
    ICO_TOUCH_BUTTONS
};

#define ICO_TOUCH_FINGERS 10
#define ICO_TOUCH_STICK_RADIUS 0.12f   /* of the safe height, at medium */
#define ICO_TOUCH_RUN_RING 0.94f       /* of the stick radius, drawn: where the game runs */
#define ICO_TOUCH_STICK_DEADZONE 0.05f /* of the stick radius */
#define ICO_TOUCH_LOOK_DECAY 0.80f     /* bindings.c's default mouse_decay */
#define ICO_TOUCH_HIDE_NS 5000000000ull
#define ICO_TOUCH_FADE_NS 300000000ull

/* the safe area's insets from each edge of the output, pixels */
typedef struct IcoTouchInsets {
    float left, top, right, bottom;
} IcoTouchInsets;

/* x, y the top-left corner; output pixels */
typedef struct IcoTouchRect {
    float x, y, w, h;
} IcoTouchRect;

typedef struct IcoTouchButton {
    IcoTouchRect rect; /* the hit box; a round button's bounding square */
    int round;         /* 1: a disc of radius rect.w / 2 (the face buttons) */
    unsigned int pad;  /* its ICO_PAD_* bit */
} IcoTouchButton;

typedef struct IcoTouchLayout {
    uint32_t outW, outH;
    IcoTouchInsets safe;
    int size;               /* ICO_TOUCH_SMALL..LARGE */
    float unit;             /* u: the safe height times the size's scale */
    IcoTouchRect safeRect;  /* the output less the insets */
    IcoTouchRect stickArea; /* where a finger down starts the stick */
    float stickR;           /* R: full deflection */
    float runR;             /* the run ring, ICO_TOUCH_RUN_RING * R */
    float stickHomeX;       /* the idle stick's centre, for drawing */
    float stickHomeY;
    IcoTouchRect lookArea; /* the right stick's pad */
    float lookR;           /* full right-stick deflection, pixels */
    float faceX, faceY;    /* the face-button diamond's centre */
    float dpadX, dpadY;    /* the D-pad cluster's centre */
    IcoTouchButton button[ICO_TOUCH_BUTTONS];
} IcoTouchLayout;

/* one finger slot (internal; the drawing reads IcoTouchDrawInfo) */
typedef struct IcoTouchFinger {
    uint64_t id;
    float x0, y0;       /* where it went down, normalised 0..1 */
    float x, y;         /* where it is now, normalised */
    unsigned char used; /* the slot holds a finger */
    unsigned char role; /* touch.c's ROLE_* */
    unsigned char up;   /* lifted before a step classified it: one step */
    signed char zone;   /* ICO_TOUCH_B_* under a button finger, -1 none */
} IcoTouchFinger;

/* Zero-initialised is a valid state (hidden until the first finger, the
   look pad centring at once on release); ico_touch_reset shows it from now
   and sets look_decay to ICO_TOUCH_LOOK_DECAY. */
typedef struct IcoTouchState {
    IcoTouchFinger finger[ICO_TOUCH_FINGERS];
    float look_decay;   /* 0..0.99: how much of the right stick stays per step */
    float lookX, lookY; /* the right stick, kept between steps for the decay */
    /* the last step's result, for the drawing */
    unsigned int pressed; /* 1 << ICO_TOUCH_B_* */
    int stickActive;
    float stickCX, stickCY; /* the stick's centre, output pixels */
    float stickFX, stickFY; /* the knob (the finger, clamped to R), pixels */
    float lx, ly;           /* the left stick it gave */
    int lookActive;
    float lookCX, lookCY, lookFX, lookFY; /* the look finger's anchor and position */
    /* visibility */
    int seen;            /* a finger (or a reset) has shown it once */
    uint64_t lastNs;     /* the last finger event */
    uint64_t shownNs;    /* the fade-in's start */
    int gamepads;        /* as of ico_touch_set_gamepads */
    uint64_t gamepadsNs; /* when that count last changed; 0 never */
} IcoTouchState;

/* What the overlay drawing needs from the last step, in output pixels. */
typedef struct IcoTouchDrawInfo {
    unsigned int pressed; /* 1 << ICO_TOUCH_B_* held */
    int stickActive;      /* a finger holds the stick: centre and knob below;
                             else both are the layout's home */
    float stickCX, stickCY, stickR, runR;
    float knobX, knobY;   /* the knob, at most R from the centre */
    float stickMag;       /* 0..1, the left stick's magnitude */
    int lookActive;       /* a finger on the look pad */
    float lookCX, lookCY; /* where it went down */
    float lookFX, lookFY; /* where it is */
    float rx, ry;         /* the right stick (decaying after release) */
} IcoTouchDrawInfo;

/* What the overlay drawing reads, copied once per vsync by the device layer
   (input_sdl.c) after its step: the zones, the step's state, and the
   opacity to draw at (the fades and the mode times the opacity setting). */
typedef struct IcoTouchOverlay {
    IcoTouchLayout layout;
    IcoTouchDrawInfo info;
    float opacity; /* 0..1; nothing is drawn at 0 */
} IcoTouchOverlay;

/* The zones for an output of outW x outH pixels with the safe area's insets
   and a size (ICO_TOUCH_SMALL..LARGE; out of range is medium). Insets that
   leave nothing are ignored. */
IcoTouchLayout ico_touch_layout(uint32_t outW, uint32_t outH, IcoTouchInsets safe, int size);

/* Clear every finger and output, look_decay to ICO_TOUCH_LOOK_DECAY (set
   the bindings' mouse_decay after), and show the overlay from nowNs with
   its fade in. */
void ico_touch_reset(IcoTouchState *t, uint64_t nowNs);

/* One finger event: finger an id unique while it is down, nx/ny 0..1 over
   the output, kind ICO_TOUCH_DOWN/MOVE/UP/CANCEL (CANCEL releases every
   finger and centres both sticks at once; the finger is ignored). */
void ico_touch_event(IcoTouchState *t, uint64_t finger, float nx, float ny, int kind,
                     uint64_t nowNs);

/* Once per vsync: classify new fingers against the layout, then the pad
   they hold now into *out (zeroed first: buttons, lx/ly, rx/ry), and the
   drawing's state into *t. */
void ico_touch_step(IcoTouchState *t, const IcoTouchLayout *l, IcoVirtualPad *out, uint64_t nowNs);

/* The connected gamepads' count, for the fade when it changes (the device
   layer calls it on hotplug). */
void ico_touch_set_gamepads(IcoTouchState *t, int gamepads, uint64_t nowNs);

/* 1 while the overlay shows: no gamepad, and a finger down or one lifted
   less than ICO_TOUCH_HIDE_NS ago. */
int ico_touch_visible(const IcoTouchState *t, int gamepads, uint64_t nowNs);

/* 0..1: the overlay's opacity with its ICO_TOUCH_FADE_NS fades in and out
   (before the user's opacity setting). 0 when hidden and faded. */
float ico_touch_opacity(const IcoTouchState *t, int gamepads, uint64_t nowNs);

/* The last step's state for drawing. */
IcoTouchDrawInfo ico_touch_draw_info(const IcoTouchState *t, const IcoTouchLayout *l);

/* Whether the touches act: the mode is not Off, and in Auto no gamepad is
   connected (the overlay is hidden then, so a touch would press unseen). */
int ico_touch_accepts(int mode, int gamepads);

/* The device layer's vsync: ico_touch_step every time (so a tap made while
   the touches are dropped is not replayed later), its pad merged into *v
   (ico_vpad_merge) only while ico_touch_accepts. Returns 1 when merged. */
int ico_touch_update(IcoTouchState *t, const IcoTouchLayout *l, int mode, int gamepads,
                     IcoVirtualPad *v, uint64_t nowNs);

/* The overlay's opacity for a mode, before the user's opacity setting: 0
   for Off, 1 for Always, ico_touch_opacity for Auto. */
float ico_touch_mode_opacity(const IcoTouchState *t, int mode, int gamepads, uint64_t nowNs);

#endif /* ICO_PORT_INPUT_TOUCH_H */
