/*
 * port/input/input_config.c
 *
 * config.toml's [input] and [gameplay] sections onto the bindings and the
 * pad host (input.h). Kept apart from bindings.c so that the headless build,
 * which has no config.toml, does not pull host_config.c's TOML reader in.
 *
 * Here: the live binding table, the last-press record the remap
 * screen's capture reads, and the writer that puts the tables back into
 * config.toml through port/config.
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "host_config.h"
#include "input.h"
#include "options.h"

static const char *const scalar_keys[] = {"keyboard",
                                          "mouse",
                                          "gamepad",
                                          "rumble",
                                          "deadzone",
                                          "walk_scale",
                                          "mouse_sensitivity",
                                          "mouse_decay",
                                          "mouse_invert_y",
                                          "mouse_camera",
                                          "mouse_hold",
                                          "touch_mode",
                                          "touch_size",
                                          "touch_opacity",
                                          "mouse_camera_speed",
                                          "mouse_full_range",
                                          "mouse_return"};

static const char *const dev_prefix[3] = {"kb.", "mouse.", "pad."};

/* Every [input] key through get(path): the scalars, then each device's
   targets (and the keyboard's walk keys). */
static int apply_keys(IcoBindings *b, const char *(*get)(const void *, const char *),
                      const void *src)
{
    char path[96], key[80];
    const char *v;
    size_t i;
    int d, k, bad = 0;

    for (i = 0; i < sizeof(scalar_keys) / sizeof(scalar_keys[0]); i++) {
        snprintf(path, sizeof(path), "input.%s", scalar_keys[i]);
        v = get(src, path);
        if (v != NULL && ico_bindings_set(b, scalar_keys[i], v) != 0) {
            bad = -1;
        }
    }
    for (d = 0; d < 3; d++) {
        for (k = -1; k < ICO_T_COUNT; k++) {
            if (k < 0 && d != 0) {
                continue;
            }
            snprintf(key, sizeof(key), "%s%s", dev_prefix[d], k < 0 ? "walk" : ico_target_name(k));
            snprintf(path, sizeof(path), "input.%s", key);
            v = get(src, path);
            if (v != NULL && ico_bindings_set(b, key, v) != 0) {
                bad = -1;
            }
        }
    }
    return bad;
}

static const char *toml_get(const void *t, const char *path)
{
    return ico_toml_get((const IcoToml *)t, path);
}

int ico_input_apply_toml(IcoBindings *b, const struct IcoToml *t)
{
    int bad;

    if (t == NULL) {
        return 0;
    }
    bad = apply_keys(b, toml_get, t);
    ico_input_set_stick_fix(
        ico_toml_get_bool(t, "gameplay.stick_fix", ico_opt_stick_fix_default()));
    return bad;
}

/* --- the remap screen's side ---------------------------------------------- */

static IcoBindings s_live;

IcoBindings *ico_input_live_bindings(void)
{
    return &s_live;
}

static unsigned int s_press_seq;
static int s_press_kind, s_press_code;

void ico_input_note_press(int kind, int code)
{
    s_press_kind = kind;
    s_press_code = code;
    s_press_seq++;
    if (s_press_seq == 0) {
        s_press_seq = 1;
    }
}

unsigned int ico_input_last_press(int *kind, int *code)
{
    if (kind != NULL) {
        *kind = s_press_kind;
    }
    if (code != NULL) {
        *code = s_press_code;
    }
    return s_press_seq;
}

const char *ico_mouse_name(int button)
{
    return button > 0 && button < ICO_MOUSE_BUTTONS ? ico_mouse_names[button] : "none";
}

static unsigned char *row_of(IcoBindings *b, int kind, int target)
{
    switch (kind) {
    case ICO_SRC_KEY:
        return b->kb[target];
    case ICO_SRC_MOUSE:
        return b->mouse[target];
    case ICO_SRC_PAD:
        return b->gp[target];
    default:
        return NULL;
    }
}

int ico_bindings_assign(IcoBindings *b, int target, int kind, int code)
{
    int t, i, o;
    int limit = kind == ICO_SRC_KEY     ? ICO_KEY_COUNT
                : kind == ICO_SRC_MOUSE ? ICO_MOUSE_BUTTONS
                : kind == ICO_SRC_PAD   ? ICO_GP_COUNT
                                        : 0;

    if (target < 0 || target >= ICO_T_COUNT || code <= 0 || code >= limit) {
        return -1;
    }
    /* one source, one action: take it off the device's other targets */
    for (t = 0; t < ICO_T_COUNT; t++) {
        unsigned char *row = row_of(b, kind, t);

        for (i = o = 0; i < ICO_BIND_MAX; i++) {
            if (row[i] != code) {
                row[o++] = row[i];
            }
        }
        while (o < ICO_BIND_MAX) {
            row[o++] = 0;
        }
    }
    {
        unsigned char *row = row_of(b, kind, target);

        memset(row, 0, ICO_BIND_MAX);
        row[0] = (unsigned char)code;
    }
    return 0;
}

void ico_bindings_clear(IcoBindings *b, int target)
{
    if (target < 0 || target >= ICO_T_COUNT) {
        return;
    }
    memset(b->kb[target], 0, ICO_BIND_MAX);
    memset(b->mouse[target], 0, ICO_BIND_MAX);
    memset(b->gp[target], 0, ICO_BIND_MAX);
}

const char *ico_bindings_row_text(const IcoBindings *b, int kind, int target, char *buf,
                                  unsigned size)
{
    const unsigned char *row;
    size_t n = 0;
    int i;

    if (size == 0) {
        return buf;
    }
    buf[0] = '\0';
    if (target < 0 || target >= ICO_T_COUNT) {
        return buf;
    }
    row = row_of((IcoBindings *)b, kind, target);
    if (row == NULL) {
        return buf;
    }
    for (i = 0; i < ICO_BIND_MAX; i++) {
        const char *name;
        int w;

        if (row[i] == 0) {
            continue;
        }
        name = kind == ICO_SRC_KEY     ? ico_key_name(row[i])
               : kind == ICO_SRC_MOUSE ? ico_mouse_name(row[i])
                                       : ico_gp_name(row[i]);
        w = snprintf(buf + n, size - n, "%s%s", n > 0 ? ", " : "", name);
        if (w < 0 || (size_t)w >= size - n) {
            break;
        }
        n += (size_t)w;
    }
    if (n == 0) {
        snprintf(buf, size, "none");
    }
    return buf;
}

int ico_input_write_bindings(const IcoBindings *b)
{
    static const int kinds[3] = {ICO_SRC_KEY, ICO_SRC_MOUSE, ICO_SRC_PAD};
    IcoBindings def;
    char path[96], now[160], was[160];
    int d, t, n = 0;

    ico_bindings_defaults(&def);
    for (d = 0; d < 3; d++) {
        for (t = 0; t < ICO_T_COUNT; t++) {
            snprintf(path, sizeof(path), "input.%s%s", dev_prefix[d], ico_target_name(t));
            ico_bindings_row_text(b, kinds[d], t, now, sizeof(now));
            ico_bindings_row_text(&def, kinds[d], t, was, sizeof(was));
            if (strcmp(now, was) == 0 && ico_config_get_string(path, NULL) == NULL) {
                continue; /* the default, and the file does not name it */
            }
            if (ico_config_set_string(path, now) != 0) {
                return -1;
            }
            n++;
        }
    }
    if (b->mouse_sens != def.mouse_sens ||
        ico_config_get_string("input.mouse_sensitivity", NULL) != NULL) {
        if (ico_config_set_float("input.mouse_sensitivity", b->mouse_sens) != 0) {
            return -1;
        }
        n++;
    }
    /* the Mouse camera and Invert mouse up/down rows, and the hold
       (config only) */
    if (b->mouse_camera != def.mouse_camera ||
        ico_config_get_string("input.mouse_camera", NULL) != NULL) {
        if (ico_config_set_bool("input.mouse_camera", b->mouse_camera != 0) != 0) {
            return -1;
        }
        n++;
    }
    if (b->mouse_invert_y != def.mouse_invert_y ||
        ico_config_get_string("input.mouse_invert_y", NULL) != NULL) {
        if (ico_config_set_bool("input.mouse_invert_y", b->mouse_invert_y != 0) != 0) {
            return -1;
        }
        n++;
    }
    if (b->mouse_hold != def.mouse_hold ||
        ico_config_get_string("input.mouse_hold", NULL) != NULL) {
        if (ico_config_set_float("input.mouse_hold", b->mouse_hold) != 0) {
            return -1;
        }
        n++;
    }
    /* the mouse camera's speed, range and swing back rows */
    if (b->mouse_camera_speed != def.mouse_camera_speed ||
        ico_config_get_string("input.mouse_camera_speed", NULL) != NULL) {
        if (ico_config_set_float("input.mouse_camera_speed", b->mouse_camera_speed) != 0) {
            return -1;
        }
        n++;
    }
    if (b->mouse_full_range != def.mouse_full_range ||
        ico_config_get_string("input.mouse_full_range", NULL) != NULL) {
        if (ico_config_set_bool("input.mouse_full_range", b->mouse_full_range != 0) != 0) {
            return -1;
        }
        n++;
    }
    if (b->mouse_return != def.mouse_return ||
        ico_config_get_string("input.mouse_return", NULL) != NULL) {
        if (ico_config_set_bool("input.mouse_return", b->mouse_return != 0) != 0) {
            return -1;
        }
        n++;
    }
    /* the touch overlay's rows (Settings > Controls) */
    if (b->touch_mode != def.touch_mode ||
        ico_config_get_string("input.touch_mode", NULL) != NULL) {
        if (b->touch_mode < 0 || b->touch_mode > 2 ||
            ico_config_set_string("input.touch_mode", ico_touch_mode_names[b->touch_mode]) != 0) {
            return -1;
        }
        n++;
    }
    if (b->touch_size != def.touch_size ||
        ico_config_get_string("input.touch_size", NULL) != NULL) {
        if (b->touch_size < 0 || b->touch_size > 2 ||
            ico_config_set_string("input.touch_size", ico_touch_size_names[b->touch_size]) != 0) {
            return -1;
        }
        n++;
    }
    if (b->touch_opacity != def.touch_opacity ||
        ico_config_get_string("input.touch_opacity", NULL) != NULL) {
        if (ico_config_set_int("input.touch_opacity", b->touch_opacity) != 0) {
            return -1;
        }
        n++;
    }
    return n;
}

static const char *config_get(const void *unused, const char *path)
{
    (void)unused;
    return ico_config_get_string(path, NULL);
}

void ico_input_reload_bindings(IcoBindings *b)
{
    ico_bindings_defaults(b);
    apply_keys(b, config_get, NULL);
}
