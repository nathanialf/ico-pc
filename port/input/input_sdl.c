/*
 * port/input/input_sdl.c
 *
 * The device layer (input_sdl.h).
 */
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input.h"
#include "input_sdl.h"

#define MAX_PADS 8
#define RUMBLE_MS 250
#define RUMBLE_REFRESH 8 /* vsyncs between re-issues of a running rumble */

static IcoBindings s_bind;
static IcoInputRaw s_raw;
static SDL_JoystickID s_pad_id[MAX_PADS];
static SDL_Gamepad *s_pad[MAX_PADS];
static unsigned char s_sdl_to_key[SDL_SCANCODE_COUNT];
static int s_capture;
static float s_acc_dx, s_acc_dy;
static unsigned short s_last_high, s_last_low;
static int s_rumble_age;
static int s_ready;

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
    ico_input_set_live(1);
    s_ready = 1;
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
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        /* SDL: 1 left, 2 middle, 3 right, 4 x1, 5 x2; ours: 1 left, 2 right, 3 middle */
        static const unsigned char map[6] = {0, 1, 3, 2, 4, 5};

        if (e->button.button >= 1 && e->button.button <= 5) {
            s_raw.mouse[map[e->button.button]] = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
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
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        close_pad(e->gdevice.which);
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

void ico_input_sdl_update(void)
{
    IcoVirtualPad v;

    if (!s_ready) {
        return;
    }
    sample_pads();
    s_raw.mouse_dx = s_acc_dx;
    s_raw.mouse_dy = s_acc_dy;
    s_acc_dx = s_acc_dy = 0.0f;
    ico_bindings_step(&s_bind, &s_raw, &v);
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
    s_ready = 0;
}
