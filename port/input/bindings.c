/*
 * port/input/bindings.c
 *
 * Raw device state to the virtual pad (input.h): the
 * binding tables, their defaults and config syntax, and the per-vsync step.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "touch.h"

/* The mouse camera (input.h IcoBindings.mouse_camera): counts for the
   whole look offset at sensitivity 1, the stick's start just past the
   game's dead zone of 48 (pad.c) and its full deflection, both over
   127.5, the relax time constant in seconds, and the offset under which
   a relaxing offset is centre */
#define MOUSE_COUNTS 400.0f
#define MOUSE_STICK_MIN (48.5f / 127.5f)
#define MOUSE_STICK_MAX (120.0f / 127.5f)
#define MOUSE_RELAX 0.25f
#define MOUSE_SNAP 0.01f
#define BUTTON_ON 0.5f

static const char *const target_names[ICO_T_COUNT] = {
    "l2",          "r2",           "l1",        "r1",          "triangle",    "circle",
    "cross",       "square",       "select",    "l3",          "r3",          "start",
    "up",          "right",        "down",      "left",        "lstick_up",   "lstick_down",
    "lstick_left", "lstick_right", "rstick_up", "rstick_down", "rstick_left", "rstick_right"};

static const char *const gp_names[ICO_GP_COUNT] = {
    "none",      "south",       "east",         "west",          "north",  "back",   "start",
    "leftstick", "rightstick",  "leftshoulder", "rightshoulder", "dpup",   "dpdown", "dpleft",
    "dpright",   "lefttrigger", "righttrigger", "leftx-",        "leftx+", "lefty-", "lefty+",
    "rightx-",   "rightx+",     "righty-",      "righty+"};

/* shared with input_config.c (input.h) */
const char *const ico_mouse_names[ICO_MOUSE_BUTTONS] = {"none",   "left", "right",
                                                        "middle", "x1",   "x2"};

static const struct {
    const char *name;
    const char *id;
} key_table[] = {

#define KEY(id, name, sdl) {name, #id},
#include "keys.def"
#undef KEY
};

/* name -> key id aliases beyond the table's names */
static const struct {
    const char *alias;
    const char *name;
} key_alias[] = {
    {"return", "enter"},          {"grave", "backquote"},  {"lshift", "leftshift"},
    {"rshift", "rightshift"},     {"lctrl", "leftctrl"},   {"rctrl", "rightctrl"},
    {"lalt", "leftalt"},          {"ralt", "rightalt"},    {"lbracket", "leftbracket"},
    {"rbracket", "rightbracket"}, {"apostrophe", "quote"}, {"pgup", "pageup"},
    {"pgdn", "pagedown"},         {"del", "delete"},       {"ins", "insert"},
};

/* --- names ---------------------------------------------------------------- */

/* case-insensitive compare ignoring blanks and underscores */
static int name_eq(const char *a, const char *b)
{
    for (;;) {
        while (*a == ' ' || *a == '_' || *a == '\t') {
            a++;
        }
        while (*b == ' ' || *b == '_' || *b == '\t') {
            b++;
        }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        if (*a == '\0') {
            return 1;
        }
        a++;
        b++;
    }
}

const char *ico_target_name(int t)
{
    return t >= 0 && t < ICO_T_COUNT ? target_names[t] : "?";
}

int ico_target_from_name(const char *name)
{
    int i;

    for (i = 0; i < ICO_T_COUNT; i++) {
        if (name_eq(name, target_names[i])) {
            return i;
        }
    }
    return -1;
}

int ico_key_from_name(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(key_alias) / sizeof(key_alias[0]); i++) {
        if (name_eq(name, key_alias[i].alias)) {
            name = key_alias[i].name;
            break;
        }
    }
    for (i = 0; i < sizeof(key_table) / sizeof(key_table[0]); i++) {
        if (name_eq(name, key_table[i].name)) {
            return (int)i + 1;
        }
    }
    return ICO_KEY_NONE;
}

const char *ico_key_name(int key)
{
    return key > 0 && key < ICO_KEY_COUNT ? key_table[key - 1].name : "none";
}

int ico_gp_from_name(const char *name)
{
    int i;

    for (i = 1; i < ICO_GP_COUNT; i++) {
        if (name_eq(name, gp_names[i])) {
            return i;
        }
    }
    return ICO_GP_NONE;
}

const char *ico_gp_name(int src)
{
    return src > 0 && src < ICO_GP_COUNT ? gp_names[src] : "none";
}

static int mouse_from_name(const char *name)
{
    int i;

    for (i = 1; i < ICO_MOUSE_BUTTONS; i++) {
        if (name_eq(name, ico_mouse_names[i])) {
            return i;
        }
    }
    return 0;
}

/* --- defaults ------------------------------------------------------------- */

static const char default_text[] = "[input]\n"
                                   "keyboard = true\n"
                                   "mouse = true\n"
                                   "gamepad = true\n"
                                   "deadzone = 0.12\n"
                                   "walk_scale = 0.5\n"
                                   "mouse_sensitivity = 1.0\n"
                                   "mouse_decay = 0.80\n"
                                   "mouse_invert_y = false\n"
                                   "mouse_camera = true\n"
                                   "mouse_hold = 0.75\n"
                                   "mouse_camera_speed = 1.0\n"
                                   "mouse_full_range = false\n"
                                   "mouse_return = true\n"
                                   "rumble = true\n"
                                   "touch_mode = \"auto\"\n"
                                   "touch_size = \"medium\"\n"
                                   "touch_opacity = 75\n"
                                   "\n"
                                   "[input.kb]\n"
                                   "walk = \"LeftShift\"\n"
                                   "lstick_up = \"W\"\n"
                                   "lstick_down = \"S\"\n"
                                   "lstick_left = \"A\"\n"
                                   "lstick_right = \"D\"\n"
                                   "rstick_up = \"I\"\n"
                                   "rstick_down = \"K\"\n"
                                   "rstick_left = \"J\"\n"
                                   "rstick_right = \"L\"\n"
                                   "up = \"Up\"\n"
                                   "down = \"Down\"\n"
                                   "left = \"Left\"\n"
                                   "right = \"Right\"\n"
                                   "cross = \"Space\"\n"
                                   "circle = \"E\"\n"
                                   "square = \"Q\"\n"
                                   "triangle = \"R\"\n"
                                   "l1 = [\"Tab\", \"Backquote\"]\n"
                                   "r1 = \"F\"\n"
                                   "l2 = \"Z\"\n"
                                   "r2 = \"X\"\n"
                                   "l3 = \"V\"\n"
                                   "r3 = \"B\"\n"
                                   "start = \"Enter\"\n"
                                   "select = \"Backspace\"\n"
                                   "\n"
                                   "[input.mouse]\n"
                                   "cross = \"left\"\n"
                                   "circle = \"right\"\n"
                                   "r1 = \"middle\"\n"
                                   "\n"
                                   "[input.pad]\n"
                                   "cross = \"south\"\n"
                                   "circle = \"east\"\n"
                                   "square = \"west\"\n"
                                   "triangle = \"north\"\n"
                                   "l1 = \"leftshoulder\"\n"
                                   "r1 = \"rightshoulder\"\n"
                                   "l2 = \"lefttrigger\"\n"
                                   "r2 = \"righttrigger\"\n"
                                   "l3 = \"leftstick\"\n"
                                   "r3 = \"rightstick\"\n"
                                   "start = \"start\"\n"
                                   "select = \"back\"\n"
                                   "up = \"dpup\"\n"
                                   "down = \"dpdown\"\n"
                                   "left = \"dpleft\"\n"
                                   "right = \"dpright\"\n";

const char *ico_bindings_default_text(void)
{
    return default_text;
}

static void set_defaults_from_text(IcoBindings *b);

void ico_bindings_defaults(IcoBindings *b)
{
    memset(b, 0, sizeof(*b));
    b->keyboard = b->mouse_on = b->gamepad = b->rumble = 1;
    b->deadzone = 0.12f;
    b->walk_scale = 0.5f;
    b->mouse_sens = 1.0f;
    b->mouse_decay = 0.80f;
    b->mouse_camera = 1;
    b->mouse_hold = 0.75f;
    b->mouse_camera_speed = 1.0f;
    b->mouse_return = 1;
    b->touch_mode = ICO_TOUCH_MODE_AUTO;
    b->touch_size = ICO_TOUCH_MEDIUM;
    b->touch_opacity = 75;
    set_defaults_from_text(b);
}

/* --- config values -------------------------------------------------------- */

/* Split a value (`A`, `"A", "B"`, `["A", "B"]`) into trimmed, unquoted
   items; returns the count (all of them, even past max). */
static int split_list(const char *value, char items[][40], int max)
{
    int n = 0;
    const char *p = value;

    while (*p != '\0') {
        char buf[40];
        size_t o = 0;

        while (*p != '\0' && *p != ',') {
            if (*p != '[' && *p != ']' && *p != '"' && *p != '\'' &&
                !(o == 0 && (*p == ' ' || *p == '\t')) && o < sizeof(buf) - 1) {
                buf[o++] = *p;
            }
            p++;
        }
        while (o > 0 && (buf[o - 1] == ' ' || buf[o - 1] == '\t' || buf[o - 1] == '\r')) {
            o--;
        }
        buf[o] = '\0';
        if (*p == ',') {
            p++;
        }
        if (o == 0 || name_eq(buf, "none")) {
            continue;
        }
        if (n < max) {
            memcpy(items[n], buf, o + 1);
        }
        n++;
    }
    return n;
}

static int parse_bool(const char *v, int *out)
{
    if (name_eq(v, "true") || name_eq(v, "1") || name_eq(v, "on") || name_eq(v, "yes")) {
        *out = 1;
        return 0;
    }
    if (name_eq(v, "false") || name_eq(v, "0") || name_eq(v, "off") || name_eq(v, "no")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int parse_float(const char *v, float lo, float hi, float *out)
{
    char *end;
    double d = strtod(v, &end);

    if (end == v || *end != '\0' || !(d >= lo && d <= hi)) { /* also NaN */
        return -1;
    }
    *out = (float)d;
    return 0;
}

/* The touch overlay's settings: a name of names[] (case and blanks as
   name_eq, quotes around it allowed) -> its index, -1 none. */
static int parse_choice(const char *v, const char *const *names, int n)
{
    char buf[24];
    size_t o = 0;
    int i;

    for (; *v != '\0' && o < sizeof(buf) - 1; v++) {
        if (*v != '"' && *v != '\'') {
            buf[o++] = *v;
        }
    }
    buf[o] = '\0';
    for (i = 0; i < n; i++) {
        if (name_eq(buf, names[i])) {
            return i;
        }
    }
    return -1;
}

const char *const ico_touch_mode_names[3] = {"off", "auto", "always"};
const char *const ico_touch_size_names[3] = {"small", "medium", "large"};

/* Bind `value` to a row of a table; type 0 keys, 1 mouse, 2 gamepad. */
static int bind_list(unsigned char *row, int type, const char *what, const char *value)
{
    char items[ICO_BIND_MAX][40];
    int n = split_list(value, items, ICO_BIND_MAX);
    int i, bad = 0, o = 0;

    memset(row, 0, ICO_BIND_MAX);
    if (n > ICO_BIND_MAX) {
        fprintf(stderr, "input: %s: only the first %d sources are used\n", what, ICO_BIND_MAX);
        n = ICO_BIND_MAX;
    }
    for (i = 0; i < n; i++) {
        int id = type == 0   ? ico_key_from_name(items[i])
                 : type == 1 ? mouse_from_name(items[i])
                             : ico_gp_from_name(items[i]);

        if (id == 0) {
            fprintf(stderr, "input: %s: unknown %s \"%s\"\n", what,
                    type == 0   ? "key"
                    : type == 1 ? "mouse button"
                                : "gamepad source",
                    items[i]);
            bad = -1;
        } else {
            row[o++] = (unsigned char)id;
        }
    }
    return bad;
}

int ico_bindings_set(IcoBindings *b, const char *key, const char *value)
{
    int t;
    int type;
    const char *name;
    char what[96];

    if (strncmp(key, "kb.", 3) == 0) {
        type = 0;
        name = key + 3;
    } else if (strncmp(key, "mouse.", 6) == 0) {
        type = 1;
        name = key + 6;
    } else if (strncmp(key, "pad.", 4) == 0) {
        type = 2;
        name = key + 4;
    } else {
        float f;
        int on;

        if (strcmp(key, "keyboard") == 0 && parse_bool(value, &on) == 0) {
            b->keyboard = on;
        } else if (strcmp(key, "mouse") == 0 && parse_bool(value, &on) == 0) {
            b->mouse_on = on;
        } else if (strcmp(key, "gamepad") == 0 && parse_bool(value, &on) == 0) {
            b->gamepad = on;
        } else if (strcmp(key, "rumble") == 0 && parse_bool(value, &on) == 0) {
            b->rumble = on;
        } else if (strcmp(key, "mouse_invert_y") == 0 && parse_bool(value, &on) == 0) {
            b->mouse_invert_y = on;
        } else if (strcmp(key, "mouse_camera") == 0 && parse_bool(value, &on) == 0) {
            b->mouse_camera = on;
        } else if (strcmp(key, "mouse_hold") == 0 && parse_float(value, 0.0f, 10.0f, &f) == 0) {
            b->mouse_hold = f;
        } else if (strcmp(key, "mouse_camera_speed") == 0 &&
                   parse_float(value, 0.5f, 10.0f, &f) == 0) {
            b->mouse_camera_speed = f;
        } else if (strcmp(key, "mouse_full_range") == 0 && parse_bool(value, &on) == 0) {
            b->mouse_full_range = on;
        } else if (strcmp(key, "mouse_return") == 0 && parse_bool(value, &on) == 0) {
            b->mouse_return = on;
        } else if (strcmp(key, "deadzone") == 0 && parse_float(value, 0.0f, 0.9f, &f) == 0) {
            b->deadzone = f;
        } else if (strcmp(key, "walk_scale") == 0 && parse_float(value, 0.05f, 1.0f, &f) == 0) {
            b->walk_scale = f;
        } else if (strcmp(key, "mouse_sensitivity") == 0 &&
                   parse_float(value, 0.01f, 100.0f, &f) == 0) {
            b->mouse_sens = f;
        } else if (strcmp(key, "mouse_decay") == 0 && parse_float(value, 0.0f, 0.99f, &f) == 0) {
            b->mouse_decay = f;
        } else if (strcmp(key, "touch_mode") == 0 &&
                   (on = parse_choice(value, ico_touch_mode_names, 3)) >= 0) {
            b->touch_mode = on;
        } else if (strcmp(key, "touch_size") == 0 &&
                   (on = parse_choice(value, ico_touch_size_names, 3)) >= 0) {
            b->touch_size = on;
        } else if (strcmp(key, "touch_opacity") == 0 &&
                   parse_float(value, 10.0f, 100.0f, &f) == 0) {
            b->touch_opacity = (int)(f + 0.5f);
        } else {
            fprintf(stderr, "input: ignoring \"%s = %s\" (unknown key or bad value)\n", key, value);
            return -1;
        }
        return 0;
    }
    snprintf(what, sizeof(what), "%s", key);
    if (type == 0 && name_eq(name, "walk")) {
        return bind_list(b->walk, 0, what, value);
    }
    t = ico_target_from_name(name);
    if (t < 0) {
        fprintf(stderr, "input: ignoring \"%s\" (no such target)\n", key);
        return -1;
    }
    return bind_list(type == 0 ? b->kb[t] : type == 1 ? b->mouse[t] : b->gp[t], type, what, value);
}

/* the defaults go through the same parser as a user's file: a mini TOML
   walk over default_text (sections input, input.kb, input.mouse, input.pad) */
static void set_defaults_from_text(IcoBindings *b)
{
    const char *p = default_text;
    char prefix[16] = "";

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        char line[96];
        size_t n = (size_t)(eol - p);

        memcpy(line, p, n);
        line[n] = '\0';
        p = eol + 1;
        if (line[0] == '[') {
            if (strcmp(line, "[input.kb]") == 0) {
                strcpy(prefix, "kb.");
            } else if (strcmp(line, "[input.mouse]") == 0) {
                strcpy(prefix, "mouse.");
            } else if (strcmp(line, "[input.pad]") == 0) {
                strcpy(prefix, "pad.");
            } else {
                prefix[0] = '\0';
            }
        } else if (line[0] != '\0') {
            char *eq = strstr(line, " = ");
            char key[128];

            *eq = '\0';
            snprintf(key, sizeof(key), "%s%s", prefix, line);
            ico_bindings_set(b, key, eq + 3);
        }
    }
}

/* --- the step ------------------------------------------------------------- */

static int key_down(const IcoInputRaw *raw, const unsigned char *row)
{
    int i;

    for (i = 0; i < ICO_BIND_MAX; i++) {
        if (row[i] != 0 && raw->key[row[i]]) {
            return 1;
        }
    }
    return 0;
}

static int mouse_down(const IcoInputRaw *raw, const unsigned char *row)
{
    int i;

    for (i = 0; i < ICO_BIND_MAX; i++) {
        if (row[i] != 0 && raw->mouse[row[i]]) {
            return 1;
        }
    }
    return 0;
}

static float gp_value(const IcoInputRaw *raw, int src)
{
    float v;

    if (src < ICO_GP_BUTTONS) {
        return raw->gp[src];
    }
    v = raw->axis[(src - ICO_GP_LX_NEG) >> 1];
    return ((src - ICO_GP_LX_NEG) & 1) ? (v > 0.0f ? v : 0.0f) : (v < 0.0f ? -v : 0.0f);
}

/* The target each gamepad source has in the shipped defaults (the [input.pad]
   rows above), by ICO_GP_*; 0 for a source with none (the stick half-axes).
   The menus go by these for the face buttons and d-pad (gp_menu_word). */
static const unsigned int gp_default_target[ICO_GP_COUNT] = {
    [ICO_GP_SOUTH] = ICO_PAD_CROSS,  [ICO_GP_EAST] = ICO_PAD_CIRCLE,
    [ICO_GP_WEST] = ICO_PAD_SQUARE,  [ICO_GP_NORTH] = ICO_PAD_TRIANGLE,
    [ICO_GP_BACK] = ICO_PAD_SELECT,  [ICO_GP_START] = ICO_PAD_START,
    [ICO_GP_LSTICK] = ICO_PAD_L3,    [ICO_GP_RSTICK] = ICO_PAD_R3,
    [ICO_GP_LSHOULDER] = ICO_PAD_L1, [ICO_GP_RSHOULDER] = ICO_PAD_R1,
    [ICO_GP_DPUP] = ICO_PAD_UP,      [ICO_GP_DPDOWN] = ICO_PAD_DOWN,
    [ICO_GP_DPLEFT] = ICO_PAD_LEFT,  [ICO_GP_DPRIGHT] = ICO_PAD_RIGHT,
    [ICO_GP_LTRIGGER] = ICO_PAD_L2,  [ICO_GP_RTRIGGER] = ICO_PAD_R2,
};

unsigned int ico_bindings_gp_default_target(int src)
{
    return src > 0 && src < ICO_GP_COUNT ? gp_default_target[src] : 0;
}

/* the face buttons and the d-pad: what the menus keep by position */
#define POSITIONAL_BITS                                                                            \
    (ICO_PAD_CROSS | ICO_PAD_CIRCLE | ICO_PAD_SQUARE | ICO_PAD_TRIANGLE | ICO_PAD_UP |             \
     ICO_PAD_DOWN | ICO_PAD_LEFT | ICO_PAD_RIGHT)

/* The gamepad's buttons as the menus see them. Per source that is down: the
   eight face and d-pad sources always mean their default target, whatever
   they are bound to. Any other source bound to a face or d-pad target (a
   shoulder swapped onto Cross, say) means its own default target, so a
   shoulder still works in the menus and one press never makes two bits. A
   source bound to anything else means that target, as in play. */
static unsigned int gp_menu_word(const IcoInputRaw *raw, const IcoBindings *b)
{
    unsigned int menu = 0;
    int s, t, i;

    for (s = 1; s < ICO_GP_COUNT; s++) {
        const unsigned int def = gp_default_target[s];

        if (!(gp_value(raw, s) >= BUTTON_ON)) {
            continue;
        }
        if (def & POSITIONAL_BITS) {
            menu |= def;
            continue;
        }
        for (t = 0; t < ICO_T_BUTTONS; t++) {
            for (i = 0; i < ICO_BIND_MAX; i++) {
                if (b->gp[t][i] == s) {
                    menu |= ((1u << t) & POSITIONAL_BITS) ? def : (1u << t);
                }
            }
        }
    }
    return menu;
}

static int gp_down(const IcoInputRaw *raw, const unsigned char *row)
{
    int i;

    for (i = 0; i < ICO_BIND_MAX; i++) {
        if (row[i] != 0 && gp_value(raw, row[i]) >= BUTTON_ON) {
            return 1;
        }
    }
    return 0;
}

static void unit_clamp(float *x, float *y)
{
    float len2 = *x * *x + *y * *y;

    if (len2 > 1.0f) {
        float s = 1.0f, len = len2;
        int i;

        for (i = 0; i < 20; i++) { /* len = sqrt(len2) */
            len = 0.5f * (len + len2 / len);
        }
        s = 1.0f / len;
        *x *= s;
        *y *= s;
    }
}

/* sqrt(v) for v >= 0 by Newton's method (no libm here, as unit_clamp) */
static float sqrt_pos(float v)
{
    float r = v > 1.0f ? v : 1.0f;
    int i;

    if (!(v > 0.0f)) {
        return 0.0f;
    }
    for (i = 0; i < 40; i++) {
        r = 0.5f * (r + v / r);
    }
    return r;
}

/* e^-x for x >= 0: halved to under 1/8, a Taylor series, squared back */
static float exp_neg(float x)
{
    float r;
    int n = 0;

    if (!(x > 0.0f)) {
        return 1.0f;
    }
    while (x > 0.125f && n < 40) {
        x *= 0.5f;
        n++;
    }
    r = 1.0f - x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f)));
    while (n-- > 0) {
        r *= r;
    }
    return r;
}

void ico_bindings_mouse_reset(IcoBindings *b)
{
    b->look_x = b->look_y = b->look_idle = 0.0f;
    b->mouse_x = b->mouse_y = 0.0f;
    b->mouse_drives = 0;
}

/* The mouse camera's stick from this snapshot's motion (input.h):
   motion moves the held look offset; still for mouse_hold seconds, the
   offset relaxes to centre; the stick is the offset past the dead zone */
static void mouse_look(IcoBindings *b, const IcoInputRaw *raw)
{
    const float dt = raw->dt > 0.0f ? (raw->dt < 0.1f ? raw->dt : 0.1f) : 1.0f / 60.0f;
    const float dx = raw->mouse_dx;
    const float dy = b->mouse_invert_y ? -raw->mouse_dy : raw->mouse_dy;
    float len;

    if (dx != 0.0f || dy != 0.0f) {
        b->look_x += dx * b->mouse_sens / MOUSE_COUNTS;
        b->look_y += dy * b->mouse_sens / MOUSE_COUNTS;
        unit_clamp(&b->look_x, &b->look_y);
        if (b->look_x * b->look_x + b->look_y * b->look_y < 1e-10f) {
            /* back where it started (a flick and the same move back): the
               rounding is centre, not a nudge past the dead zone */
            b->look_x = b->look_y = 0.0f;
        }
        b->look_idle = 0.0f;
    } else if (b->mouse_return) {
        /* with the swing back off, the offset stays until the next motion */
        const float before = b->look_idle;

        b->look_idle += dt;
        if (b->look_idle > b->mouse_hold) {
            /* the part of this step past the hold */
            const float t = before >= b->mouse_hold ? dt : b->look_idle - b->mouse_hold;
            const float k = exp_neg(t / MOUSE_RELAX);

            b->look_x *= k;
            b->look_y *= k;
            if (b->look_x * b->look_x + b->look_y * b->look_y < MOUSE_SNAP * MOUSE_SNAP) {
                b->look_x = b->look_y = 0.0f;
            }
        }
    }
    len = sqrt_pos(b->look_x * b->look_x + b->look_y * b->look_y);
    if (len > 0.0f) {
        const float s = (MOUSE_STICK_MIN + (MOUSE_STICK_MAX - MOUSE_STICK_MIN) * len) / len;

        b->mouse_x = b->look_x * s;
        b->mouse_y = b->look_y * s;
    } else {
        b->mouse_x = b->mouse_y = 0.0f;
    }
}

/* four digital directions to a vector no longer than 1 */
static void dirs(int up, int down, int left, int right, float *x, float *y)
{
    *x = (float)(right - left);
    *y = (float)(down - up);
    unit_clamp(x, y);
}

static void merge_stick(float *dx, float *dy, float sx, float sy)
{
    if (sx * sx + sy * sy > *dx * *dx + *dy * *dy) {
        *dx = sx;
        *dy = sy;
    }
}

void ico_bindings_step(IcoBindings *b, const IcoInputRaw *raw, IcoVirtualPad *out)
{
    IcoVirtualPad gp, kb;
    int t;
    float x, y;

    memset(&gp, 0, sizeof(gp));
    memset(&kb, 0, sizeof(kb));
    memset(out, 0, sizeof(*out));

    /* the mouse camera's stick */
    if (b->mouse_on && b->mouse_camera) {
        mouse_look(b, raw);
    } else {
        ico_bindings_mouse_reset(b);
    }

    /* gamepad: buttons, analog sticks with the dead zone, digital stick targets */
    if (b->gamepad) {
        int d[8];

        for (t = 0; t < ICO_T_BUTTONS; t++) {
            if (gp_down(raw, b->gp[t])) {
                gp.buttons |= 1u << t;
            }
        }
        for (t = 0; t < 8; t++) {
            d[t] = gp_down(raw, b->gp[ICO_T_LSTICK_UP + t]);
        }
        x = raw->axis[0];
        y = raw->axis[1];
        ico_input_deadzone(&x, &y, b->deadzone);
        gp.lx = x;
        gp.ly = y;
        x = raw->axis[2];
        y = raw->axis[3];
        ico_input_deadzone(&x, &y, b->deadzone);
        gp.rx = x;
        gp.ry = y;
        dirs(d[0], d[1], d[2], d[3], &x, &y);
        merge_stick(&gp.lx, &gp.ly, x, y);
        dirs(d[4], d[5], d[6], d[7], &x, &y);
        merge_stick(&gp.rx, &gp.ry, x, y);
    }

    /* keyboard and mouse buttons */
    {
        int d[8];

        for (t = 0; t < ICO_T_COUNT; t++) {
            int on = (b->keyboard && key_down(raw, b->kb[t])) ||
                     (b->mouse_on && mouse_down(raw, b->mouse[t]));

            if (t < ICO_T_BUTTONS) {
                if (on) {
                    kb.buttons |= 1u << t;
                }
            } else {
                d[t - ICO_T_LSTICK_UP] = on;
            }
        }
        dirs(d[0], d[1], d[2], d[3], &kb.lx, &kb.ly);
        if (b->keyboard && key_down(raw, b->walk)) {
            kb.lx *= b->walk_scale;
            kb.ly *= b->walk_scale;
        }
        dirs(d[4], d[5], d[6], d[7], &kb.rx, &kb.ry);
        merge_stick(&kb.rx, &kb.ry, b->mouse_x, b->mouse_y);
        /* the mouse's stick is what moves the camera when it is not centred
           and is strictly longer than both the keys' and the gamepad's (a tie
           goes to them, as in ico_vpad_merge) */
        {
            const float m = b->mouse_x * b->mouse_x + b->mouse_y * b->mouse_y;

            b->mouse_drives = m > 0.0f && kb.rx == b->mouse_x && kb.ry == b->mouse_y &&
                              m > gp.rx * gp.rx + gp.ry * gp.ry;
        }
    }

    kb.menu_buttons = kb.buttons;
    if (b->gamepad) {
        gp.menu_buttons = gp_menu_word(raw, b);
    }
    *out = gp;
    ico_vpad_merge(out, &kb);
}

void ico_bindings_mouse_merged(IcoBindings *b, const IcoVirtualPad *v)
{
    if (b->mouse_drives && (v->rx != b->mouse_x || v->ry != b->mouse_y)) {
        b->mouse_drives = 0;
    }
}
