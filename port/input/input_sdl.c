/*
 * port/input/input_sdl.c
 *
 * The device layer (input_sdl.h), the touch overlay's SDL side included
 * (touch.h's wiring notes): a direct touch screen's fingers into the
 * mapper, its pad merged with the bindings' once per vsync, the layout
 * from the window's size and safe area, and a copy of what the overlay
 * drawing reads.
 *
 * Gamepads sit in slots (open_pad, pad_slot): slot 0 is the first player's
 * pad and feeds libpad port 0 with the keyboard, the mouse and the touch
 * overlay; slot 1 is the second player's and feeds port 1 alone (Yorda,
 * when the game's two-player control is on); slots 2..7 drive no port,
 * only the remap screen's capture. A pad keeps its slot across a
 * reconnection by serial, path or (when unique) GUID; a different pad
 * plugged in while a player's slot stands empty takes that slot.
 */
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input.h"
#include "input_sdl.h"
#include "mouse_look.h"
#include "pointer.h"
#include "touch.h"
#include "touch_edit.h"
#ifdef __ANDROID__
#include "android/host_android.h" /* port/platform/android: the phone vibrator */
#else
/* no phone vibrator here: a controller is the only rumble */
static void ico_host_vibrate(int amplitude, int ms)
{
    (void)amplitude;
    (void)ms;
}
#endif

/* the mouse rows of Cross and Circle exchanged (ico_pointer_swap_face) */
static void swap_mouse_face(IcoBindings *b)
{
    unsigned char t[ICO_BIND_MAX];

    memcpy(t, b->mouse[ICO_T_CROSS], sizeof(t));
    memcpy(b->mouse[ICO_T_CROSS], b->mouse[ICO_T_CIRCLE], sizeof(t));
    memcpy(b->mouse[ICO_T_CIRCLE], t, sizeof(t));
}

#define MAX_PADS 8
#define PAD_ID_TEXT 256
#define RUMBLE_MS 250
#define RUMBLE_REFRESH 8 /* vsyncs between re-issues of a running rumble */

/* the live table (input_config.c), which the Settings menu's remap screen
   edits in place */
#define s_bind (*ico_input_live_bindings())
static IcoInputRaw s_raw;
/* SDL's open-pad slots are assigned once per connection and stay attached to
    their joystick IDs until removal; slot 0 is Ico, slot 1 is Yorda. */
static IcoInputRaw s_pad_raw[MAX_PADS];
/* the last snapshot's gamepad sources, for the press edges the remap
   screen's capture reads (ico_input_note_press) */
static unsigned char s_gp_down[ICO_GP_COUNT];
static SDL_JoystickID s_pad_id[MAX_PADS];
static SDL_Gamepad *s_pad[MAX_PADS];

typedef struct PadIdentity {
    SDL_GUID guid;
    char serial[PAD_ID_TEXT];
    char path[PAD_ID_TEXT];
    int valid;
} PadIdentity;

static PadIdentity s_pad_identity[MAX_PADS];
static unsigned char s_sdl_to_key[SDL_SCANCODE_COUNT];
static int s_capture; /* ICO_CAPTURE_* (mouse_look.h) */
/* Escape or Android's Back, and the button it holds (ico_input_escape) */
static IcoEscapeLatch s_escape;
static float s_acc_dx, s_acc_dy;
static unsigned short s_last_high[MAX_PADS], s_last_low[MAX_PADS];
static int s_rumble_age[MAX_PADS];
static unsigned char s_rumble_took[MAX_PADS];
static int s_phone_vibrating; /* the phone, not a controller, was last sent a rumble */
static void phone_vibrate_stop(void);
static int s_ready;
static Uint64 s_last_update; /* the snapshot's time, for its dt */
/* the left button went down over a menu the pointer uses, so it stays
   off Cross until it is released (a click there is the pointer's) */
static int s_left_menu;
/* when the menus' pointer was last freed: SDL puts it back where it was
   held as relative mode ends, a motion the player did not make, so the
   motion of the first moment after is not a hover */
static Uint64 s_free_since;
#define POINTER_SETTLE_NS 100000000ull

/* the touch overlay: the mapper's state and zones, whether a direct touch
   screen exists, the output, safe area, density and fold the zones were
   built for, and the copy the presenter's overlay draws from (taken at the
   vsync step; the presenter runs on this thread, between pumps, but never
   reads the mapper while events write it) */
static IcoTouchState s_touch;
static IcoTouchLayout s_touchLayout;
static int s_touchDevice;
static int s_touchW, s_touchH;
static int s_safeX, s_safeY, s_safeW, s_safeH; /* s_safeW 0: the whole output */
static IcoTouchEnv s_touchEnv;                 /* zero: unknown density, no fold */
static IcoTouchOverlay s_touchSnap;
/* the layout editor (touch_edit.h) while the Edit layout page is open: the
   fingers go to it, not to s_touch.  Everything here runs on the window
   thread: the events (ico_window_pump -> ico_input_sdl_event), the vsync
   step (ico_input_sdl_update, the same pump) and the page's tick (the
   game's Main step, between pumps), so the editor's queue needs no lock */
static IcoTouchEditor s_touchEdit;

static void pad_identity_read(PadIdentity *identity, SDL_JoystickID id, SDL_Gamepad *pad)
{
    const char *serial = SDL_GetGamepadSerial(pad);
    const char *path = SDL_GetGamepadPath(pad);

    memset(identity, 0, sizeof(*identity));
    identity->guid = SDL_GetGamepadGUIDForID(id);
    if (serial != NULL) {
        SDL_strlcpy(identity->serial, serial, sizeof(identity->serial));
    }
    if (path != NULL) {
        SDL_strlcpy(identity->path, path, sizeof(identity->path));
    }
    identity->valid = 1;
}

static int identity_slot(const PadIdentity *identity, int first, int end)
{
    int guid_slot = -1;
    int guid_matches = 0;
    int i;

    for (i = first; i < end; i++) {
        const PadIdentity *known = &s_pad_identity[i];

        if (s_pad[i] != NULL || !known->valid) {
            continue;
        }
        if (identity->serial[0] != '\0' && known->serial[0] != '\0') {
            if (strcmp(identity->serial, known->serial) == 0) {
                return i;
            }
            continue;
        }
        if (identity->path[0] != '\0' && known->path[0] != '\0') {
            if (strcmp(identity->path, known->path) == 0) {
                return i;
            }
            continue;
        }
        if (memcmp(&identity->guid, &known->guid, sizeof(identity->guid)) == 0) {
            guid_slot = i;
            guid_matches++;
        }
    }
    return guid_matches == 1 ? guid_slot : -1;
}

static int pad_slot(const PadIdentity *identity)
{
    int slot, i;

    slot = identity_slot(identity, 0, ICO_INPUT_PLAYERS);
    if (slot >= 0) {
        return slot;
    }
    for (i = 0; i < ICO_INPUT_PLAYERS; i++) {
        if (s_pad[i] == NULL && !s_pad_identity[i].valid) {
            return i;
        }
    }
    /* a new pad while a player's slot stands empty (its pad unplugged, a
       different one plugged in): that slot, so the swap keeps a player
       in control; the slot's old identity goes */
    for (i = 0; i < ICO_INPUT_PLAYERS; i++) {
        if (s_pad[i] == NULL) {
            return i;
        }
    }
    slot = identity_slot(identity, ICO_INPUT_PLAYERS, MAX_PADS);
    if (slot >= 0) {
        return slot;
    }
    for (i = ICO_INPUT_PLAYERS; i < MAX_PADS; i++) {
        if (s_pad[i] == NULL && !s_pad_identity[i].valid) {
            return i;
        }
    }
    for (i = ICO_INPUT_PLAYERS; i < MAX_PADS; i++) {
        if (s_pad[i] == NULL) {
            return i;
        }
    }
    return -1;
}

static void open_pad(SDL_JoystickID id)
{
    SDL_Gamepad *pad;
    PadIdentity identity;
    int slot, i;

    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] != NULL && s_pad_id[i] == id) {
            return;
        }
    }
    pad = SDL_OpenGamepad(id);
    if (pad == NULL) {
        return;
    }
    pad_identity_read(&identity, id, pad);
    slot = pad_slot(&identity);
    if (slot < 0) {
        SDL_CloseGamepad(pad);
        return;
    }
    s_pad[slot] = pad;
    s_pad_id[slot] = id;
    s_pad_identity[slot] = identity;
    s_last_high[slot] = s_last_low[slot] = 0;
    s_rumble_age[slot] = 0;
    s_rumble_took[slot] = 0;
    fprintf(stderr, "input: gamepad slot %d \"%s\"\n", slot, SDL_GetGamepadName(s_pad[slot]));
}

static int open_pads(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_PADS; i++) {
        n += s_pad[i] != NULL;
    }
    return n;
}

static void touch_rebuild(void)
{
    IcoTouchInsets in = {0.0f, 0.0f, 0.0f, 0.0f};

    if (s_touchW <= 0 || s_touchH <= 0) {
        return;
    }
    if (s_safeW > 0 && s_safeH > 0) {
        in.left = (float)s_safeX;
        in.top = (float)s_safeY;
        in.right = (float)(s_touchW - s_safeX - s_safeW);
        in.bottom = (float)(s_touchH - s_safeY - s_safeH);
        in.left = in.left > 0.0f ? in.left : 0.0f;
        in.top = in.top > 0.0f ? in.top : 0.0f;
        in.right = in.right > 0.0f ? in.right : 0.0f;
        in.bottom = in.bottom > 0.0f ? in.bottom : 0.0f;
    }
    s_touchLayout = ico_touch_layout_edits(
        (uint32_t)s_touchW, (uint32_t)s_touchH, in, s_bind.touch_size, &s_touchEnv,
        s_touchEdit.active ? &s_touchEdit.work : ico_input_live_touch_edits());
}

/* the edits the layout should carry now: the editor's working copy while
   it is open, else the live ones */
static const IcoTouchEdits *touch_edits_now(void)
{
    return s_touchEdit.active ? &s_touchEdit.work : ico_input_live_touch_edits();
}

/* start-up: one line when the file had edits (none for an untouched config);
   how many of them lie past the safe area (a negative offset: clamped to the
   window, touch_edit.h ico_touch_edit_outside), the rest clamped inside it as
   v0.8.4 clamped every edit */
static void touch_edits_log(void)
{
    const IcoTouchEdits *e = ico_input_live_touch_edits();
    char list[64] = "";
    int total = 0, outside = 0, r, c;

    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        int n = 0;

        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            n += e->ctl[r][c].set != 0;
            outside += ico_touch_edit_outside(&e->ctl[r][c]);
        }
        if (n > 0) {
            const size_t used = strlen(list);

            snprintf(list + used, sizeof(list) - used, "%s%s", used > 0 ? ", " : "",
                     ico_touch_posture_name(r));
            total += n;
        }
    }
    if (total > 0) {
        fprintf(stderr,
                "touch: layout edits: %d controls in %s (%d past the safe area, the rest kept "
                "inside it)\n",
                total, list, outside);
    }
}

/* a direct touch screen is there: the overlay shows for 5 s from now */
static void touch_found(const char *how)
{
    if (s_touchDevice) {
        return;
    }
    s_touchDevice = 1;
    ico_touch_reset(&s_touch, SDL_GetTicksNS());
    s_touch.look_decay = s_bind.mouse_decay;
    ico_touch_set_gamepads(&s_touch, open_pads(), SDL_GetTicksNS());
    fprintf(stderr, "input: touch screen (%s); touch controls %s\n", how,
            ico_touch_mode_names[s_bind.touch_mode >= 0 && s_bind.touch_mode <= 2
                                     ? s_bind.touch_mode
                                     : ICO_TOUCH_MODE_AUTO]);
}

static void close_pad(SDL_JoystickID id)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] != NULL && s_pad_id[i] == id) {
            fprintf(stderr, "input: gamepad slot %d \"%s\" removed\n", i,
                    SDL_GetGamepadName(s_pad[i]));
            SDL_CloseGamepad(s_pad[i]);
            s_pad[i] = NULL;
            s_last_high[i] = s_last_low[i] = 0;
            s_rumble_age[i] = 0;
            s_rumble_took[i] = 0;
        }
    }
}

void ico_input_sdl_init(const char *config_path)
{
    IcoToml *t;
    int n = 0, i;
    SDL_JoystickID *ids;

    memset(s_sdl_to_key, 0, sizeof(s_sdl_to_key));
#define KEY(id, name, sdl) s_sdl_to_key[sdl] = ICO_KEY_##id;
#include "keys.def"
#undef KEY
    ico_bindings_defaults(&s_bind);
    if (config_path != NULL) {
        t = ico_toml_load(config_path);
        if (t != NULL) {
            if (ico_input_apply_toml(&s_bind, t) != 0) {
                fprintf(stderr, "input: %s: some settings were ignored\n", config_path);
            }
            /* the layout edits ([touch]); before touch_rebuild below, so the
               first layout carries them */
            ico_input_apply_touch_toml(ico_input_live_touch_edits(), t);
            touch_edits_log();
            ico_toml_free(t);
        } else {
            fprintf(stderr, "input: no %s, default bindings\n", config_path);
        }
    }
    ids = SDL_GetGamepads(&n);
    for (i = 0; ids != NULL && i < n; i++) {
        open_pad(ids[i]);
    }
    SDL_free(ids);
    memset(&s_raw, 0, sizeof(s_raw));
    /* the touch overlay: a direct touch screen known at start shows it */
    memset(&s_touch, 0, sizeof(s_touch));
    memset(&s_touchSnap, 0, sizeof(s_touchSnap));
    s_touchDevice = 0;
    ico_touch_set_gamepads(&s_touch, open_pads(), SDL_GetTicksNS());
    {
        SDL_TouchID *touch = SDL_GetTouchDevices(&n);

        for (i = 0; touch != NULL && i < n; i++) {
            if (SDL_GetTouchDeviceType(touch[i]) == SDL_TOUCH_DEVICE_DIRECT) {
                touch_found("at start");
                break;
            }
        }
        SDL_free(touch);
    }
    touch_rebuild();
    ico_input_set_live(1);
    s_ready = 1;
}

void ico_input_sdl_set_touch_layout(int w, int h, int sx, int sy, int sw, int sh)
{
    s_touchW = w;
    s_touchH = h;
    s_safeX = sx;
    s_safeY = sy;
    s_safeW = sw;
    s_safeH = sh;
    touch_rebuild();
}

void ico_input_sdl_set_touch_env(const IcoTouchEnv *env)
{
    if (env != NULL) {
        s_touchEnv = *env;
    } else {
        memset(&s_touchEnv, 0, sizeof(s_touchEnv));
    }
    touch_rebuild();
}

int ico_input_sdl_touch_present(void)
{
    return s_touchDevice;
}

int ico_input_sdl_touch_overlay(IcoTouchOverlay *out)
{
    if (out == NULL || !s_touchDevice || !(s_touchSnap.opacity > 0.0f)) {
        return 0;
    }
    *out = s_touchSnap;
    return 1;
}

static void touch_finger(const SDL_Event *e, int kind)
{
    if (SDL_GetTouchDeviceType(e->tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) {
        return; /* a trackpad's fingers (macOS) are not the overlay's */
    }
    touch_found("first touch");
    if (s_touchEdit.active) {
        ico_touch_edit_event(&s_touchEdit, (uint64_t)e->tfinger.fingerID, e->tfinger.x,
                             e->tfinger.y, kind, e->tfinger.timestamp);
        return;
    }
    ico_touch_event(&s_touch, (uint64_t)e->tfinger.fingerID, e->tfinger.x, e->tfinger.y, kind,
                    e->tfinger.timestamp);
}

static void touch_cancel(void)
{
    if (s_touchEdit.active) {
        /* focus lost: the editor drops a drag and keeps its edits */
        ico_touch_edit_event(&s_touchEdit, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, SDL_GetTicksNS());
    }
    if (s_touchDevice) {
        ico_touch_event(&s_touch, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, SDL_GetTicksNS());
    }
}

static void clear_held(void)
{
    memset(s_raw.key, 0, sizeof(s_raw.key));
    memset(s_raw.mouse, 0, sizeof(s_raw.mouse));
    s_acc_dx = s_acc_dy = 0.0f;
    s_left_menu = 0;
    ico_escape_latch(&s_escape, 0, NULL);
}

/* whether the pointer is the menus' (free, not the camera's; with
   [input] mouse = false the mouse does nothing, in the menus too) */
static int pointer_free(void)
{
    return s_capture == ICO_CAPTURE_OFF && s_bind.mouse_on;
}

/* the pointer's place from a window position (points), as 0..1 of
   the window's size */
static void pointer_at(SDL_WindowID id, float x, float y)
{
    SDL_Window *w = SDL_GetWindowFromID(id);
    int ww = 0, wh = 0;

    if (w == NULL || !SDL_GetWindowSize(w, &ww, &wh) || ww <= 0 || wh <= 0) {
        return;
    }
    ico_pointer_move(x / (float)ww, y / (float)wh);
}

/* SDL synthesizes mouse events from touches (SDL_TOUCH_MOUSEID); with the
   overlay active the fingers are already handled as touches, so those
   mouse events are dropped (else a tap is a click and a drag a look too) */
static int touch_synth_mouse(SDL_MouseID which)
{
    return which == SDL_TOUCH_MOUSEID && s_touchDevice && s_bind.touch_mode != ICO_TOUCH_MODE_OFF;
}

void ico_input_sdl_event(const SDL_Event *e)
{
    if (!s_ready) {
        return;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if ((unsigned)e->key.scancode < SDL_SCANCODE_COUNT && s_sdl_to_key[e->key.scancode] != 0) {
            s_raw.key[s_sdl_to_key[e->key.scancode]] = e->type == SDL_EVENT_KEY_DOWN;
            if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
                ico_input_note_press(ICO_SRC_KEY, s_sdl_to_key[e->key.scancode]);
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        /* SDL: 1 left, 2 middle, 3 right, 4 x1, 5 x2; ours: 1 left, 2 right, 3 middle */
        static const unsigned char map[6] = {0, 1, 3, 2, 4, 5};

        if (touch_synth_mouse(e->button.which)) {
            break;
        }
        if (e->button.button >= 1 && e->button.button <= 5) {
            const int down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;

            s_raw.mouse[map[e->button.button]] = (unsigned char)down;
            if (down) {
                ico_input_note_press(ICO_SRC_MOUSE, map[e->button.button]);
            }
            /* the menus' pointer, while it is free: the left button
               clicks (and, pressed over a menu, is not Cross until it is
               released); the others only show the pointer is in use */
            if (e->button.button == SDL_BUTTON_LEFT) {
                if (down) {
                    s_left_menu = pointer_free() && ico_pointer_menu();
                } else {
                    s_left_menu = 0;
                }
            }
            if (pointer_free()) {
                if (e->button.button == SDL_BUTTON_LEFT) {
                    if (down) {
                        pointer_at(e->button.windowID, e->button.x, e->button.y);
                    }
                    ico_pointer_button(down);
                } else if (down) {
                    ico_pointer_other_button();
                }
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (touch_synth_mouse(e->motion.which)) {
            break;
        }
        if (s_capture == ICO_CAPTURE_STICK) {
            /* play: the right stick (bindings.c's held look offset) */
            s_acc_dx += e->motion.xrel;
            s_acc_dy += e->motion.yrel;
        } else if (s_capture == ICO_CAPTURE_DELTA) {
            /* photo mode: its screen takes the motion each Main tick */
            ico_mouse_look_add(e->motion.xrel, e->motion.yrel);
        } else if (pointer_free() && e->motion.timestamp >= s_free_since + POINTER_SETTLE_NS) {
            /* free, the menus' pointer */
            pointer_at(e->motion.windowID, e->motion.x, e->motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        /* the menus' wheel, up (away from the player) positive; a
           flipped direction (natural scrolling) is turned back */
        if (touch_synth_mouse(e->wheel.which) || !pointer_free()) {
            break;
        }
        ico_pointer_wheel(e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        ico_pointer_leave();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        clear_held();
        touch_cancel();
        phone_vibrate_stop();
        ico_pointer_leave();
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        /* nothing stays held while the app is away (Android) */
        touch_cancel();
        phone_vibrate_stop();
        break;
    case SDL_EVENT_FINGER_DOWN:
        touch_finger(e, ICO_TOUCH_DOWN);
        break;
    case SDL_EVENT_FINGER_MOTION:
        touch_finger(e, ICO_TOUCH_MOVE);
        break;
    case SDL_EVENT_FINGER_UP:
        touch_finger(e, ICO_TOUCH_UP);
        break;
    case SDL_EVENT_FINGER_CANCELED:
        touch_finger(e, ICO_TOUCH_CANCEL);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(e->gdevice.which);
        ico_touch_set_gamepads(&s_touch, open_pads(), SDL_GetTicksNS());
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        close_pad(e->gdevice.which);
        ico_touch_set_gamepads(&s_touch, open_pads(), SDL_GetTicksNS());
        break;
    default:
        break;
    }
}

void ico_input_sdl_set_capture(int mode)
{
    if (mode == s_capture) {
        return;
    }
    /* any change starts the look from centre, so play does not keep
       a turn from before a menu and photo mode not a move from play */
    s_capture = mode;
    s_acc_dx = s_acc_dy = 0.0f;
    /* a held pointer has no place in the menus; the next move after
       it is freed gives it one */
    ico_pointer_leave();
    s_free_since = mode == ICO_CAPTURE_OFF ? SDL_GetTicksNS() : 0;
    ico_bindings_mouse_reset(&s_bind);
    ico_mouse_look_reset();
}

static float axis_f(SDL_Gamepad *g, SDL_GamepadAxis a)
{
    float v = (float)SDL_GetGamepadAxis(g, a) / 32767.0f;

    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}

static void sample_pads(void)
{
    static const SDL_GamepadButton btn[ICO_GP_BUTTONS] = {
        SDL_GAMEPAD_BUTTON_INVALID,        SDL_GAMEPAD_BUTTON_SOUTH,
        SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH,          SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
        SDL_GAMEPAD_BUTTON_RIGHT_STICK,    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
        SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     SDL_GAMEPAD_BUTTON_INVALID,
        SDL_GAMEPAD_BUTTON_INVALID};
    static const SDL_GamepadAxis ax[4] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY,
                                          SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY};
    int i, j;

    memset(s_raw.gp, 0, sizeof(s_raw.gp));
    memset(s_raw.axis, 0, sizeof(s_raw.axis));
    memset(s_pad_raw, 0, sizeof(s_pad_raw));
    s_raw.gamepads = 0;
    for (i = 0; i < MAX_PADS; i++) {
        SDL_Gamepad *g = s_pad[i];
        IcoInputRaw *raw = &s_pad_raw[i];
        float v;

        if (g == NULL) {
            continue;
        }
        s_raw.gamepads++;
        raw->gamepads = 1;
        for (j = 1; j < ICO_GP_BUTTONS; j++) {
            if (btn[j] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(g, btn[j])) {
                raw->gp[j] = 1.0f;
                s_raw.gp[j] = 1.0f;
            }
        }
        v = axis_f(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        raw->gp[ICO_GP_LTRIGGER] = v;
        if (v > s_raw.gp[ICO_GP_LTRIGGER]) {
            s_raw.gp[ICO_GP_LTRIGGER] = v;
        }
        v = axis_f(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        raw->gp[ICO_GP_RTRIGGER] = v;
        if (v > s_raw.gp[ICO_GP_RTRIGGER]) {
            s_raw.gp[ICO_GP_RTRIGGER] = v;
        }
        for (j = 0; j < 4; j++) {
            v = axis_f(g, ax[j]);
            raw->axis[j] = v;
            if (v * v > s_raw.axis[j] * s_raw.axis[j]) {
                s_raw.axis[j] = v;
            }
        }
    }
}

/* stops the phone's vibration if it was started */
static void phone_vibrate_stop(void)
{
    if (s_phone_vibrating) {
        s_phone_vibrating = 0;
        ico_host_vibrate(0, 0);
    }
}

static void send_rumble(void)
{
    unsigned short phone_high = 0, phone_low = 0;
    int i, phone_taken = 0;

    for (i = 0; i < MAX_PADS; i++) {
        const int port = i < ICO_INPUT_PLAYERS ? i : 0;
        unsigned short high, low;
        int changed;

        if (s_bind.rumble) {
            ico_input_rumble_get_for_port(port, &high, &low);
        } else {
            high = low = 0;
        }
        s_rumble_age[i]++;
        changed = high != s_last_high[i] || low != s_last_low[i] ||
                  ((high != 0 || low != 0) && s_rumble_age[i] >= RUMBLE_REFRESH);
        if (changed) {
            s_rumble_age[i] = 0;
            s_last_high[i] = high;
            s_last_low[i] = low;
            s_rumble_took[i] =
                s_pad[i] != NULL &&
                SDL_RumbleGamepad(s_pad[i], low, high, high != 0 || low != 0 ? RUMBLE_MS : 0);
        } else if (s_pad[i] == NULL) {
            s_rumble_took[i] = 0;
        }
        if (port == 0 && s_rumble_took[i]) {
            phone_taken = 1;
        }
        if (i == 0) {
            phone_high = high;
            phone_low = low;
        }
    }
    /* no controller took it: the phone's own vibrator does (the same values
       the game's vibration setting and [input] rumble already produced, so
       Off in either gives zero here). Called on SDL's main thread, which
       is where the host loop runs this. */
    if ((phone_high != 0 || phone_low != 0) && !phone_taken) {
        /* one vibrator for two motors: the stronger of the two, the small
           motor (on or off, pad_host.c) counting as full strength and the
           large one by its level; at least 1 so a weak large motor alone
           still vibrates */
        const int small_amp = phone_high != 0 ? 255 : 0;
        const int large_amp = phone_low / 257;
        int amp = small_amp > large_amp ? small_amp : large_amp;

        if (amp < 1) {
            amp = 1;
        }

        ico_host_vibrate(amp, RUMBLE_MS);
        s_phone_vibrating = 1;
    } else {
        phone_vibrate_stop();
    }
}

/* The gamepad's new presses for the remap screen's capture:
   a button, a trigger past half, a stick axis past half (as a direction
   source); release below a quarter re-arms it. */
static void note_pad_presses(void)
{
    int j;

    for (j = 1; j < ICO_GP_COUNT; j++) {
        float v;
        int down;

        if (j < ICO_GP_BUTTONS) {
            v = s_raw.gp[j];
        } else {
            int a = (j - ICO_GP_LX_NEG) / 2; /* lx ly rx ry */
            float x = s_raw.axis[a];

            v = (j - ICO_GP_LX_NEG) % 2 == 0 ? -x : x;
        }
        down = s_gp_down[j] ? v > 0.25f : v >= 0.5f;
        if (down && !s_gp_down[j]) {
            ico_input_note_press(ICO_SRC_PAD, j);
        }
        s_gp_down[j] = (unsigned char)down;
    }
}

/* the touch overlay's vsync: the Touch size row's change, the look pad's
   decay from the bindings (a reload may change it), the step and the merge
   (dropped with a gamepad in Auto: ico_touch_update), then the copy the
   presenter draws */
static void touch_step(IcoVirtualPad *v)
{
    const uint64_t now = SDL_GetTicksNS();
#if defined(__ANDROID__) || defined(ICO_IOS)
    const int phone = 1;
#else
    const int phone = 0;
#endif
    const int mode = ico_touch_effective_mode(s_bind.touch_mode, s_raw.gamepads, phone);
    static int s_offKept; /* the rail's line, once per run */
    int pct = s_bind.touch_opacity;

    if (mode != s_bind.touch_mode && !s_offKept) {
        s_offKept = 1;
        fprintf(stderr, "touch: Touch controls is Off but no gamepad is connected: the "
                        "on-screen buttons stay (Auto), nothing else could turn them on\n");
    }

    const int editing = s_touchEdit.active;

    if (s_touchLayout.size != s_bind.touch_size ||
        s_touchLayout.editGen != touch_edits_now()->gen) {
        touch_rebuild();
    }
    s_touch.look_decay = s_bind.mouse_decay;
    /* editing: the mapper steps (a tap made earlier is not replayed) but
       merges nothing, so a drag never presses a button */
    ico_touch_update(&s_touch, &s_touchLayout, editing ? ICO_TOUCH_MODE_OFF : mode, s_raw.gamepads,
                     v, now);
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    s_touchSnap.layout = s_touchLayout;
    if (editing) {
        /* every control at full strength, nothing pressed or lit */
        memset(&s_touchSnap.info, 0, sizeof(s_touchSnap.info));
        s_touchSnap.opacity = 1.0f;
        ico_touch_edit_view(&s_touchEdit, &s_touchLayout, &s_touchSnap.edit);
        return;
    }
    memset(&s_touchSnap.edit, 0, sizeof(s_touchSnap.edit));
    s_touchSnap.info = ico_touch_draw_info(&s_touch, &s_touchLayout);
    s_touchSnap.opacity =
        ico_touch_mode_opacity(&s_touch, mode, s_raw.gamepads, now) * (float)pct / 100.0f;
}

IcoTouchEditor *ico_input_sdl_touch_editor(void)
{
    return &s_touchEdit;
}

void ico_input_sdl_touch_edit_begin(void)
{
    /* the mapper lets go of every finger first: none stays pressed under
       the editor */
    ico_touch_event(&s_touch, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, SDL_GetTicksNS());
    ico_touch_edit_begin(&s_touchEdit, ico_input_live_touch_edits(), s_touchLayout.rule);
    touch_rebuild();
}

void ico_input_sdl_touch_edit_end(int keep)
{
    ico_touch_edit_end(&s_touchEdit, keep);
    /* a finger still down (the one that tapped Done) is not the mapper's */
    ico_touch_event(&s_touch, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, SDL_GetTicksNS());
    touch_rebuild();
}

int ico_input_sdl_touch_rule(void)
{
    return s_touchLayout.rule;
}

const IcoTouchLayout *ico_input_sdl_touch_layout(void)
{
    return &s_touchLayout;
}

void ico_input_escape(int down, const IcoCaptureState *s)
{
    if (!s_ready) {
        return;
    }
    ico_escape_latch(&s_escape, down, s);
    /* the layout editor: the Japanese and Korean discs' back is Cross, the
       editor's Opacity + (touch_edit.c handle_pad); Back leaves it as Done
       does, so it presses Triangle there (touch_edit_ui.c) */
    if (down && s_touchEdit.active && s_escape.target == ICO_T_CROSS) {
        s_escape.target = ICO_T_TRIANGLE;
    }
}

void ico_input_sdl_update(void)
{
    IcoVirtualPad v, v2;
    IcoInputRaw player_raw, player2_raw;
    IcoBindings player2_bindings;
    unsigned int start_src = 0;

    if (!s_ready) {
        return;
    }
    sample_pads();
    ico_input_set_gamepad_present(1, s_pad[1] != NULL);
    note_pad_presses();
    s_raw.mouse_dx = s_acc_dx;
    s_raw.mouse_dy = s_acc_dy;
    s_acc_dx = s_acc_dy = 0.0f;
    {
        /* the time the mouse camera's hold and relax run on */
        const Uint64 now = SDL_GetTicksNS();

        s_raw.dt = s_last_update != 0 ? (float)((double)(now - s_last_update) / 1e9) : 0.0f;
        s_last_update = now;
    }
    {
        /* over a menu the pointer uses, the left button is the
           pointer's click (port/ui/ui_mouse.c), not Cross, so a click off
           every row never confirms the row the cursor is on; right (Circle)
           and middle (R1) stay, and the remap screen's capture still saw
           the press (ico_input_note_press, at the event) */
        const unsigned char left = s_raw.mouse[1];

        if (s_left_menu || (pointer_free() && ico_pointer_menu())) {
            s_raw.mouse[1] = 0;
        }
        player_raw = s_raw;
        memcpy(player_raw.gp, s_pad_raw[0].gp, sizeof(player_raw.gp));
        memcpy(player_raw.axis, s_pad_raw[0].axis, sizeof(player_raw.axis));
        /* the Japanese disc's menus and photo mode: the mouse's Cross and
           Circle trade places for the step (pointer.h), so the right
           button goes back there as on the European disc */
        const int swap = ico_pointer_swap_face();
        if (swap) {
            swap_mouse_face(&s_bind);
        }
        ico_bindings_step(&s_bind, &player_raw, &v);
        if (swap) {
            swap_mouse_face(&s_bind);
        }
        start_src = ico_bindings_start_sources(&s_bind, &s_raw);
        s_raw.mouse[1] = left;
    }
    memset(&player2_raw, 0, sizeof(player2_raw));
    memcpy(player2_raw.gp, s_pad_raw[1].gp, sizeof(player2_raw.gp));
    memcpy(player2_raw.axis, s_pad_raw[1].axis, sizeof(player2_raw.axis));
    player2_raw.gamepads = s_pad_raw[1].gamepads;
    player2_raw.dt = s_raw.dt;
    player2_bindings = s_bind;
    player2_bindings.keyboard = 0;
    player2_bindings.mouse_on = 0;
    ico_bindings_step(&player2_bindings, &player2_raw, &v2);
    /* Escape (Back on Android): Start or Triangle, outside the bindings */
    {
        const unsigned int esc = ico_escape_take(&s_escape);

        v.buttons |= esc;
        v.menu_buttons |= esc;
        start_src |= (esc & ICO_PAD_START) != 0 ? ICO_START_ESCAPE : 0;
    }
    if (s_touchDevice) {
        touch_step(&v);
        /* a touch look pad stick that won the merge is not the mouse's */
        ico_bindings_mouse_merged(&s_bind, &v);
        start_src |= (v.menu_buttons & ICO_PAD_START) != 0 && start_src == 0 ? ICO_START_TOUCH : 0;
    }
    ico_input_set_start_sources(start_src);
    ico_input_set_vpad(&v);
    ico_input_set_vpad_for_port(1, &v2);
    send_rumble();
}

void ico_input_sdl_shutdown(void)
{
    int i;

    if (!s_ready) {
        return;
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] != NULL) {
            SDL_RumbleGamepad(s_pad[i], 0, 0, 0);
            SDL_CloseGamepad(s_pad[i]);
            s_pad[i] = NULL;
        }
    }
    phone_vibrate_stop();
    s_touchEdit.active = 0;
    s_touchSnap.opacity = 0.0f;
    s_touchDevice = 0;
    s_ready = 0;
}
