/*
 * port/input/input.h
 *
 * The input layers (docs/port/INPUT.md):
 *
 *   device      SDL3 events: keyboard, mouse, gamepads (input_sdl.c)
 *   bindings    raw device state -> a virtual PS2 pad (bindings.c)
 *   virtual pad floats: sixteen buttons and two sticks (pad_host.c)
 *   libpad      the DualShock 2 read buffer the game reads (pad_host.c)
 *   key config  the game's own PadConf on top (fumi/ios/pad.c, untouched)
 *
 * Everything except input_sdl.c is plain C with no SDL, so the headless build
 * and the tests link it.
 */
#ifndef ICO_PORT_INPUT_INPUT_H
#define ICO_PORT_INPUT_INPUT_H

#include "pad_script.h" /* ICO_PAD_* button bits, IcoPadFrame */

/* --- the virtual pad ------------------------------------------------------ */

/* Buttons in the game's logical word (ICO_PAD_*, active high). Sticks run -1
   to +1: x grows to the right, y downwards (a stick pushed up is -1, as on
   the pad's byte 0 = up). */
typedef struct IcoVirtualPad {
    unsigned int buttons;
    float lx, ly, rx, ry;
} IcoVirtualPad;

/* OR the buttons; per stick, the vector with the larger magnitude. */
void ico_vpad_merge(IcoVirtualPad *dst, const IcoVirtualPad *src);

/* A stick value (-1..+1) to the pad's byte: 0 -> 128 (the DualShock's
   idle), -1 -> 0, +1 -> 255; out of range clamps. */
unsigned char ico_input_quantise(float v);
/* A radial dead zone with rescaling: a vector shorter than dz becomes zero,
   the rest is stretched so the edge of the zone is the new zero. */
void ico_input_deadzone(float *x, float *y, float dz);
/* The stick fix (docs/port/INPUT.md): scale the vector by
   1 + 0.2 * d / 45, d the angle in degrees to the nearest axis (0..45), so
   iosPadNormalizeStick's divisor (fumi/ios/pad.c) cancels. Never shrinks;
   capped so neither component leaves -1..+1 (the direction is kept). */
void ico_input_stick_fix(float *x, float *y);
/* The scale ico_input_stick_fix applies to a vector of this direction (before
   the cap), for tables and tests. */
float ico_input_stick_fix_scale(float x, float y);
/* A virtual pad to the byte frame: optional stick fix and mirror (negated
   stick X), then ico_input_quantise. */
void ico_input_vpad_to_frame(const IcoVirtualPad *v, int stick_fix, int mirror, IcoPadFrame *out);

/* --- the pad as the game sees it (pad_host.c) ----------------------------- */

/* Live sources (keyboard, mouse, gamepads) plug a DualShock 2 into port 0.
   Off by default so the headless build keeps the scripted / empty pad; the
   window build turns it on. A script, when loaded, wins over live sources. */
void ico_input_set_live(int on);
int ico_input_live(void);
/* The binding layer's output for this vsync. */
void ico_input_set_vpad(const IcoVirtualPad *v);
/* [gameplay] stick_fix (default off) and the mirror mode hook (Phase 6):
   negate stick X, both sticks, of the live sources (a script is bytes and is
   left as written). */
void ico_input_set_stick_fix(int on);
int ico_input_stick_fix_enabled(void);
void ico_input_set_mirror(int on);
int ico_input_mirror(void);
/* The frame scePadRead hands out now: the script's if one is loaded, else
   the live pad, else a centred, released frame. */
void ico_input_frame(IcoPadFrame *out);

/* Rumble. act = the 6 bytes scePadSetActDirect gets, align = the bytes
   scePadSetActAlign set (align[i] = the actuator data byte i drives: 0 the
   small motor, on/off; 1 the large motor, 0..255; 0xFF none). high_freq is
   the small motor and low_freq the large one, as SDL names them, 0..65535. */
void ico_pad_rumble_map(const unsigned char align[6], const unsigned char act[6],
                        unsigned short *high_freq, unsigned short *low_freq);
/* The motors the game asked for last (0 when no live pad is plugged). */
void ico_input_rumble_get(unsigned short *high_freq, unsigned short *low_freq);

/* --- bindings (bindings.c) ------------------------------------------------ */

/* Targets: the sixteen buttons in ICO_PAD_* bit order, then the eight stick
   directions. */
enum {
    ICO_T_L2,
    ICO_T_R2,
    ICO_T_L1,
    ICO_T_R1,
    ICO_T_TRIANGLE,
    ICO_T_CIRCLE,
    ICO_T_CROSS,
    ICO_T_SQUARE,
    ICO_T_SELECT,
    ICO_T_L3,
    ICO_T_R3,
    ICO_T_START,
    ICO_T_UP,
    ICO_T_RIGHT,
    ICO_T_DOWN,
    ICO_T_LEFT,
    ICO_T_LSTICK_UP,
    ICO_T_LSTICK_DOWN,
    ICO_T_LSTICK_LEFT,
    ICO_T_LSTICK_RIGHT,
    ICO_T_RSTICK_UP,
    ICO_T_RSTICK_DOWN,
    ICO_T_RSTICK_LEFT,
    ICO_T_RSTICK_RIGHT,
    ICO_T_COUNT
};

#define ICO_T_BUTTONS 16

/* Keyboard keys (keys.def); 0 is none. */
enum {
    ICO_KEY_NONE = 0,

#define KEY(id, name, sdl) ICO_KEY_##id,
#include "keys.def"
#undef KEY
    ICO_KEY_COUNT
};

/* Gamepad sources by position (SDL's gamepad layout names). The first
   ICO_GP_BUTTONS carry a value 0..1 (triggers are analog); the stick
   directions are derived from the axes at half deflection. */
enum {
    ICO_GP_NONE = 0,
    ICO_GP_SOUTH,
    ICO_GP_EAST,
    ICO_GP_WEST,
    ICO_GP_NORTH,
    ICO_GP_BACK,
    ICO_GP_START,
    ICO_GP_LSTICK,
    ICO_GP_RSTICK,
    ICO_GP_LSHOULDER,
    ICO_GP_RSHOULDER,
    ICO_GP_DPUP,
    ICO_GP_DPDOWN,
    ICO_GP_DPLEFT,
    ICO_GP_DPRIGHT,
    ICO_GP_LTRIGGER,
    ICO_GP_RTRIGGER,
    ICO_GP_BUTTONS, /* count of the value sources, with NONE */
    ICO_GP_LX_NEG = ICO_GP_BUTTONS,
    ICO_GP_LX_POS,
    ICO_GP_LY_NEG,
    ICO_GP_LY_POS,
    ICO_GP_RX_NEG,
    ICO_GP_RX_POS,
    ICO_GP_RY_NEG,
    ICO_GP_RY_POS,
    ICO_GP_COUNT
};

/* Mouse buttons: 1 left, 2 right, 3 middle, 4 x1, 5 x2 (0 none). */
#define ICO_MOUSE_BUTTONS 6

/* One snapshot of the devices (input_sdl.c fills it once per vsync). */
typedef struct IcoInputRaw {
    unsigned char key[ICO_KEY_COUNT];       /* 1 down */
    unsigned char mouse[ICO_MOUSE_BUTTONS]; /* index 1..5 */
    float mouse_dx, mouse_dy;               /* relative motion since the last snapshot */
    float gp[ICO_GP_BUTTONS];               /* 0..1; all gamepads merged (max) */
    float axis[4]; /* lx ly rx ry, -1..1, y down; merged (largest magnitude) */
    int gamepads;  /* how many are connected */
} IcoInputRaw;

#define ICO_BIND_MAX 4

typedef struct IcoBindings {
    unsigned char kb[ICO_T_COUNT][ICO_BIND_MAX];
    unsigned char mouse[ICO_T_COUNT][ICO_BIND_MAX];
    unsigned char gp[ICO_T_COUNT][ICO_BIND_MAX];
    unsigned char walk[ICO_BIND_MAX]; /* keys that scale the keyboard's left stick */
    int keyboard, mouse_on, gamepad;
    float deadzone;    /* gamepad radial dead zone, 0..0.9 */
    float walk_scale;  /* left stick magnitude while a walk key is down */
    float mouse_sens;  /* multiplier on the built-in per-count gain */
    float mouse_decay; /* per vsync, 0..0.99: how much of the stick stays */
    int mouse_invert_y;
    int rumble;
    float mouse_x, mouse_y; /* the mouse's stick, state between steps */
} IcoBindings;

/* The defaults (docs/port/INPUT.md). */
void ico_bindings_defaults(IcoBindings *b);
/* The default bindings as config text, for documentation and tests. */
const char *ico_bindings_default_text(void);
/* One [input] setting: key is relative to "input." ("kb.cross", "deadzone",
   ...). 0 applied; -1 unknown key or a name that did not resolve (logged to
   stderr; the rest of the value is still applied). */
int ico_bindings_set(IcoBindings *b, const char *key, const char *value);
/* Names. */
const char *ico_target_name(int target);
int ico_target_from_name(const char *name);
int ico_key_from_name(const char *name);
const char *ico_key_name(int key);
int ico_gp_from_name(const char *name);
const char *ico_gp_name(int src);
/* One snapshot to the virtual pad: advances the mouse stick by one vsync. */
void ico_bindings_step(IcoBindings *b, const IcoInputRaw *raw, IcoVirtualPad *out);

/* config.toml's [input] and [gameplay] onto *b and the pad host, through
   host_config's TOML subset (input_config.c). Missing file: defaults. */
struct IcoToml;
int ico_input_apply_toml(IcoBindings *b, const struct IcoToml *t);

/* --- the remap screen (Phase 6, 6C; input_config.c) ------------------------ */

/* The bindings the live sources use: input_sdl.c steps this table, the
   Settings menu's remap screen edits it in place, so a change applies on the
   next vsync. Zeroed until ico_input_sdl_init (or a test) fills it. */
IcoBindings *ico_input_live_bindings(void);

/* Source kinds of one binding. */
enum { ICO_SRC_NONE = 0, ICO_SRC_KEY = 1, ICO_SRC_MOUSE = 2, ICO_SRC_PAD = 3 };

/* The device layer reports every new press here (input_sdl.c: a key down, a
   mouse button down, a gamepad button or trigger crossing half, a stick
   axis crossing half: ICO_GP_*_NEG/POS); code is an ICO_KEY_*, a mouse
   button 1..5 or an ICO_GP_*. */
void ico_input_note_press(int kind, int code);
/* The last press: returns a sequence number that changes with every press
   (0 before the first), kind and code when not NULL. The remap screen's
   capture compares the sequence with the one it saw when it started. */
unsigned int ico_input_last_press(int *kind, int *code);

/* Bind target to one source of its kind: that device's row for target is
   replaced by the source alone, and the source is taken off every other
   target of the same device (one key, one action). The other devices' rows
   are kept. 0, or -1 for a bad target, kind or code. */
int ico_bindings_assign(IcoBindings *b, int target, int kind, int code);
/* Clear every device's sources of target. */
void ico_bindings_clear(IcoBindings *b, int target);
/* A device's row of target as config text: "Space", "Tab, Backquote",
   "none". kind ICO_SRC_KEY/MOUSE/PAD. */
const char *ico_bindings_row_text(const IcoBindings *b, int kind, int target, char *buf,
                                  unsigned size);
/* A mouse button's config name ("left", "right", "middle", "x1", "x2"). */
const char *ico_mouse_name(int button);

/* The binding tables into config.toml through port/config (ico_config_set_*;
   the caller saves with ico_config_save): [input.kb], [input.mouse] and
   [input.pad] for each target whose row differs from the default or is
   already in the file, as a string ("Space", "Tab, Backquote", "none";
   bindings.c splits a comma list like an array), and mouse_sensitivity.
   Returns the number of keys set, or -1. */
int ico_input_write_bindings(const IcoBindings *b);
/* Rebuild *b from the defaults and the config's [input] and [gameplay]
   keys as port/config reads them (ico_config_get_string): the reload after
   a write, or ico_input_apply_toml over the config in use. */
void ico_input_reload_bindings(IcoBindings *b);

#endif /* ICO_PORT_INPUT_INPUT_H */
