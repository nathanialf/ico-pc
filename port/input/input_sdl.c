/*
 * port/input/input_sdl.c
 *
 * The device layer (input_sdl.h), the touch overlay's SDL side included
 * (touch.h's wiring notes): a direct touch screen's fingers into the
 * mapper, its pad merged with the bindings' once per vsync, the layout
 * from the window's size and safe area, and a copy of what the overlay
 * drawing reads.
 */
/* the real ico_input_sdl_set_safe_area is this file's (input_sdl.h) */
#define ICO_TOUCH_SAFE_AREA_IMPL 1
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input.h"
#include "input_sdl.h"
#include "touch.h"

#define MAX_PADS 8
#define RUMBLE_MS 250
#define RUMBLE_REFRESH 8 /* vsyncs between re-issues of a running rumble */

/* the live table (input_config.c), which the Settings menu's remap screen
   edits in place (Phase 6, 6C) */
#define s_bind (*ico_input_live_bindings())
static IcoInputRaw s_raw;
/* the last snapshot's gamepad sources, for the press edges the remap
   screen's capture reads (ico_input_note_press) */
static unsigned char s_gp_down[ICO_GP_COUNT];
static SDL_JoystickID s_pad_id[MAX_PADS];
static SDL_Gamepad *s_pad[MAX_PADS];
static unsigned char s_sdl_to_key[SDL_SCANCODE_COUNT];
static int s_capture;
static float s_acc_dx, s_acc_dy;
static unsigned short s_last_high, s_last_low;
static int s_rumble_age;
static int s_ready;

/* the touch overlay: the mapper's state and zones, whether a direct touch
   screen exists, the output and safe area the zones were built for, and the
   copy the presenter's overlay draws from (taken at the vsync step; the
   presenter runs on this thread, between pumps, but never reads the
   mapper while events write it) */
static IcoTouchState s_touch;
static IcoTouchLayout s_touchLayout;
static int s_touchDevice;
static int s_touchW, s_touchH;
static int s_safeX, s_safeY, s_safeW, s_safeH; /* s_safeW 0: the whole output */
static IcoTouchOverlay s_touchSnap;

static void open_pad(SDL_JoystickID id)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] != NULL && s_pad_id[i] == id) {
            return;
        }
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] == NULL) {
            s_pad[i] = SDL_OpenGamepad(id);
            if (s_pad[i] != NULL) {
                s_pad_id[i] = id;
                fprintf(stderr, "input: gamepad \"%s\"\n", SDL_GetGamepadName(s_pad[i]));
            }
            return;
        }
    }
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
    s_touchLayout = ico_touch_layout((uint32_t)s_touchW, (uint32_t)s_touchH, in, s_bind.touch_size);
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
            SDL_CloseGamepad(s_pad[i]);
            s_pad[i] = NULL;
            fprintf(stderr, "input: gamepad removed\n");
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

void ico_input_sdl_set_safe_area(int x, int y, int w, int h)
{
    s_safeX = x;
    s_safeY = y;
    s_safeW = w;
    s_safeH = h;
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
    ico_touch_event(&s_touch, (uint64_t)e->tfinger.fingerID, e->tfinger.x, e->tfinger.y, kind,
                    e->tfinger.timestamp);
}

static void touch_cancel(void)
{
    if (s_touchDevice) {
        ico_touch_event(&s_touch, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, SDL_GetTicksNS());
    }
}

static void clear_held(void)
{
    memset(s_raw.key, 0, sizeof(s_raw.key));
    memset(s_raw.mouse, 0, sizeof(s_raw.mouse));
    s_acc_dx = s_acc_dy = 0.0f;
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

        if (e->button.button >= 1 && e->button.button <= 5) {
            s_raw.mouse[map[e->button.button]] = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                ico_input_note_press(ICO_SRC_MOUSE, map[e->button.button]);
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (s_capture) {
            s_acc_dx += e->motion.xrel;
            s_acc_dy += e->motion.yrel;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        clear_held();
        touch_cancel();
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        /* nothing stays held while the app is away (Android) */
        touch_cancel();
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

void ico_input_sdl_set_capture(int on)
{
    s_capture = on != 0;
    if (!s_capture) {
        s_acc_dx = s_acc_dy = 0.0f;
    }
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
    s_raw.gamepads = 0;
    for (i = 0; i < MAX_PADS; i++) {
        SDL_Gamepad *g = s_pad[i];
        float v;

        if (g == NULL) {
            continue;
        }
        s_raw.gamepads++;
        for (j = 1; j < ICO_GP_BUTTONS; j++) {
            if (btn[j] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(g, btn[j])) {
                s_raw.gp[j] = 1.0f;
            }
        }
        v = axis_f(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        if (v > s_raw.gp[ICO_GP_LTRIGGER]) {
            s_raw.gp[ICO_GP_LTRIGGER] = v;
        }
        v = axis_f(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (v > s_raw.gp[ICO_GP_RTRIGGER]) {
            s_raw.gp[ICO_GP_RTRIGGER] = v;
        }
        for (j = 0; j < 4; j++) {
            v = axis_f(g, ax[j]);
            if (v * v > s_raw.axis[j] * s_raw.axis[j]) {
                s_raw.axis[j] = v;
            }
        }
    }
}

static void send_rumble(void)
{
    unsigned short high, low;
    int i;

    if (s_bind.rumble) {
        ico_input_rumble_get(&high, &low);
    } else {
        high = low = 0;
    }
    s_rumble_age++;
    if (high == s_last_high && low == s_last_low &&
        !((high != 0 || low != 0) && s_rumble_age >= RUMBLE_REFRESH)) {
        return;
    }
    s_rumble_age = 0;
    s_last_high = high;
    s_last_low = low;
    for (i = 0; i < MAX_PADS; i++) {
        if (s_pad[i] != NULL) {
            SDL_RumbleGamepad(s_pad[i], low, high, high != 0 || low != 0 ? RUMBLE_MS : 0);
        }
    }
}

/* Phase 6 (6C): the gamepad's new presses for the remap screen's capture:
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
    const int mode = s_bind.touch_mode;
    int pct = s_bind.touch_opacity;

    if (s_touchLayout.size != s_bind.touch_size) {
        touch_rebuild();
    }
    s_touch.look_decay = s_bind.mouse_decay;
    ico_touch_update(&s_touch, &s_touchLayout, mode, s_raw.gamepads, v, now);
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    s_touchSnap.layout = s_touchLayout;
    s_touchSnap.info = ico_touch_draw_info(&s_touch, &s_touchLayout);
    s_touchSnap.opacity =
        ico_touch_mode_opacity(&s_touch, mode, s_raw.gamepads, now) * (float)pct / 100.0f;
}

void ico_input_sdl_update(void)
{
    IcoVirtualPad v;

    if (!s_ready) {
        return;
    }
    sample_pads();
    note_pad_presses();
    s_raw.mouse_dx = s_acc_dx;
    s_raw.mouse_dy = s_acc_dy;
    s_acc_dx = s_acc_dy = 0.0f;
    ico_bindings_step(&s_bind, &s_raw, &v);
    if (s_touchDevice) {
        touch_step(&v);
    }
    ico_input_set_vpad(&v);
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
    s_touchSnap.opacity = 0.0f;
    s_touchDevice = 0;
    s_ready = 0;
}
