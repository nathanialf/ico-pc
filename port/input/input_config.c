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
    int loser = -1;
    unsigned char prev[ICO_BIND_MAX];
    int limit = kind == ICO_SRC_KEY     ? ICO_KEY_COUNT
                : kind == ICO_SRC_MOUSE ? ICO_MOUSE_BUTTONS
                : kind == ICO_SRC_PAD   ? ICO_GP_COUNT
                                        : 0;

    if (target < 0 || target >= ICO_T_COUNT || code <= 0 || code >= limit) {
        return -1;
    }
    memcpy(prev, row_of(b, kind, target), ICO_BIND_MAX);
    /* one source, one action: take it off the device's other targets. The
       first target that held it is swapped in below, as the game's own
       Button configuration does, so no action is left without a button */
    for (t = 0; t < ICO_T_COUNT; t++) {
        unsigned char *row = row_of(b, kind, t);

        for (i = o = 0; i < ICO_BIND_MAX; i++) {
            if (row[i] != code) {
                row[o++] = row[i];
            } else if (t != target && loser < 0) {
                loser = t;
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
    if (loser >= 0) {
        unsigned char *row = row_of(b, kind, loser);

        for (o = 0; o < ICO_BIND_MAX && row[o] != 0; o++) {}
        for (i = 0; i < ICO_BIND_MAX && o < ICO_BIND_MAX; i++) {
            int have = 0, k;

            for (k = 0; k < o; k++) {
                have |= row[k] == prev[i];
            }
            if (prev[i] != 0 && prev[i] != code && !have) {
                row[o++] = prev[i];
            }
        }
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

/* --- the touch layout edits ([touch] tables, touch_edit.h) ---------------- */
#include <stdlib.h>
#include "touch_edit.h"

static IcoTouchEdits s_touchEdits;
static int s_touchEditsInit;

IcoTouchEdits *ico_input_live_touch_edits(void)
{
    if (!s_touchEditsInit) {
        ico_touch_edits_defaults(&s_touchEdits);
        s_touchEditsInit = 1;
    }
    return &s_touchEdits;
}

/* A bad line is logged once per key and run, until it reads well again (the
   settings file is read again after every save). */
static unsigned char s_touchLogged[ICO_TOUCH_POSTURES][ICO_TOUCH_CTL_COUNT];
static unsigned char s_touchLoggedSnap, s_touchLoggedGrid, s_touchLoggedFormat;

/* why a control's line did not parse, for the log: the cases a hand edit
   most likely makes, else the general one */
static const char *touch_why(int ctl, const char *text)
{
    char tok[32];
    const char *p = text;
    int words = 0, haveO = 0, n = 0, badS = 0;

    (void)ctl;
    while (*p != '\0') {
        size_t k = 0;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t' && k + 1 < sizeof(tok)) {
            tok[k++] = *p++;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++; /* a token longer than any valid one */
        }
        tok[k] = '\0';
        if (k == 0) {
            continue;
        }
        n++;
        if (strcmp(tok, "default") == 0 || strcmp(tok, "hidden") == 0 || strcmp(tok, "off") == 0) {
            words++;
        } else if (tok[0] == 'o') {
            haveO = 1;
        } else if (tok[0] == 's') {
            char *end = NULL;
            const double v = strtod(tok + 1, &end);

            badS |= end == tok + 1 || v < ICO_TOUCH_EDIT_SCALE_MIN - 0.005 ||
                    v > ICO_TOUCH_EDIT_SCALE_MAX + 0.005;
        }
    }
    if (words > 1 || (words == 1 && strstr(text, "default") != NULL && n > 1)) {
        return "default stands alone; hidden or off once, alone or after the place";
    }
    if (words == 1 && haveO) {
        return "hidden and off take no o";
    }
    if (badS) {
        return "the size s is 0.7 to 3.0";
    }
    return "not a valid layout";
}

/* The [touch] keys through get(src, path) onto *e: what the source names is
   applied (a control's "default" or absence leaves it unset), what it does
   not name is left as it is. Returns the keys applied. */
static int apply_touch(IcoTouchEdits *e, const char *(*get)(const void *, const char *),
                       const void *src)
{
    char path[64];
    const char *v;
    int r, c, n = 0, format, migrated = 0;

    v = get(src, "touch.snap");
    if (v != NULL && (strcmp(v, "true") == 0 || strcmp(v, "1") == 0)) {
        e->snap = 1;
        n++;
        s_touchLoggedSnap = 0;
    } else if (v != NULL && (strcmp(v, "false") == 0 || strcmp(v, "0") == 0)) {
        e->snap = 0;
        n++;
        s_touchLoggedSnap = 0;
    } else if (v != NULL) {
        if (!s_touchLoggedSnap) {
            fprintf(stderr, "touch: ignoring snap = \"%s\" (use true or false)\n", v);
            s_touchLoggedSnap = 1;
        }
    } else {
        s_touchLoggedSnap = 0;
    }

    v = get(src, "touch.grid");
    if (v != NULL) {
        char *end = NULL;
        double g = strtod(v, &end);
        int i, ok = 0;

        for (i = 0; end != v && *end == '\0' && i < 3; i++) {
            if (g == (double)ico_touch_edit_grids[i]) {
                e->grid = ico_touch_edit_grids[i];
                n++;
                ok = 1;
            }
        }
        if (ok) {
            s_touchLoggedGrid = 0;
        } else if (!s_touchLoggedGrid) {
            fprintf(stderr, "touch: ignoring grid = \"%s\" (use 2, 4 or 8)\n", v);
            s_touchLoggedGrid = 1;
        }
    } else {
        s_touchLoggedGrid = 0;
    }

    /* the lines' format: 2 written since v0.8.6; none (v0.8.4, v0.8.5) is
       1, whose "hidden" and "o0" read as Off (touch_edit.h) */
    v = get(src, "touch.format");
    format = 1;
    if (v != NULL) {
        char *end = NULL;
        const long f = strtol(v, &end, 10);

        if (end != v && *end == '\0' && f >= 1) {
            format = f >= ICO_TOUCH_EDIT_FORMAT ? ICO_TOUCH_EDIT_FORMAT : (int)f;
            s_touchLoggedFormat = 0;
        } else if (!s_touchLoggedFormat) {
            fprintf(stderr, "touch: ignoring format = \"%s\" (use %d)\n", v, ICO_TOUCH_EDIT_FORMAT);
            s_touchLoggedFormat = 1;
        }
    } else {
        s_touchLoggedFormat = 0;
    }

    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            IcoTouchCtlEdit *ed = &e->ctl[r][c];

            snprintf(path, sizeof(path), "touch.%s.%s", ico_touch_posture_name(r),
                     ico_touch_ctl_name(c));
            v = get(src, path);
            memset(ed, 0, sizeof(*ed));
            if (v == NULL || strcmp(v, "default") == 0) {
                s_touchLogged[r][c] = 0;
                continue;
            }
            if (ico_touch_edit_parse_format(ed, c, v, format) != 0) {
                memset(ed, 0, sizeof(*ed));
                if (!s_touchLogged[r][c]) {
                    fprintf(stderr, "touch: ignoring %s.%s = \"%s\" (%s)\n",
                            ico_touch_posture_name(r), ico_touch_ctl_name(c), v, touch_why(c, v));
                    s_touchLogged[r][c] = 1;
                }
                continue;
            }
            ed->set = 1;
            ico_touch_edit_sanitize(ed, c);
            s_touchLogged[r][c] = 0;
            if (format < ICO_TOUCH_EDIT_FORMAT && !ed->active) {
                /* only a line the current format reads otherwise ("hidden",
                   o0: Hidden there) was migrated; an "off" means Off in both */
                IcoTouchCtlEdit now;

                memset(&now, 0, sizeof(now));
                migrated += ico_touch_edit_parse(&now, c, v) == 0 && now.active;
            }
            n++;
        }
    }
    if (migrated > 0) {
        fprintf(stderr,
                "touch: [touch] without format 2: \"hidden\" and o0 read as off (%d controls)\n",
                migrated);
    }
    return n;
}

int ico_input_apply_touch_toml(IcoTouchEdits *e, const IcoToml *t)
{
    int n;

    if (e == NULL || t == NULL) {
        return 0;
    }
    n = apply_touch(e, toml_get, t);
    if (n > 0) {
        e->gen++;
    }
    return n;
}

void ico_input_reload_touch_edits(IcoTouchEdits *e)
{
    IcoTouchEdits was;

    if (e == NULL) {
        return;
    }
    was = *e;
    ico_touch_edits_defaults(e);
    apply_touch(e, config_get, NULL);
    /* the layout is rebuilt only when the file says something else than what
       was live (a save writes what is live) */
    e->gen = was.gen;
    if (memcmp(e->ctl, was.ctl, sizeof(e->ctl)) != 0 || e->snap != was.snap ||
        e->grid != was.grid) {
        e->gen++;
    }
}

/* a [touch] line will be written: snap or grid off their defaults, a set
   control, or a [touch] key already in the file */
static int touch_lines(const IcoTouchEdits *e, const IcoTouchEdits *def)
{
    char path[64];
    int r, c;

    if (e->snap != def->snap || e->grid != def->grid || ico_touch_edits_any(e, -1) ||
        ico_config_get_string("touch.snap", NULL) != NULL ||
        ico_config_get_string("touch.grid", NULL) != NULL ||
        ico_config_get_string("touch.format", NULL) != NULL) {
        return 1;
    }
    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            snprintf(path, sizeof(path), "touch.%s.%s", ico_touch_posture_name(r),
                     ico_touch_ctl_name(c));
            if (ico_config_get_string(path, NULL) != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

int ico_input_write_touch_edits(const IcoTouchEdits *e)
{
    IcoTouchEdits def;
    char path[64], text[96];
    const char *old;
    int r, c, n = 0, format;

    ico_touch_edits_defaults(&def);
    /* the format goes with any line: the lines below are written in its
       grammar ("hidden" Hidden, "off" Off), so an older file's "o0" and
       "hidden" are rewritten as what they were read as.  It is set last,
       once every line is: a save after a failed line (the settings save
       still writes the file) keeps the old format over what it has, where
       a new "hidden" reads as Off, never format 2 over an old "hidden"
       (Off) that would then read as Hidden.  Asked before the lines, as it
       looks at what the file has. */
    format = touch_lines(e, &def);
    if (e->snap != def.snap || ico_config_get_string("touch.snap", NULL) != NULL) {
        if (ico_config_set_bool("touch.snap", e->snap != 0) != 0) {
            return -1;
        }
        n++;
    }
    if (e->grid != def.grid || ico_config_get_string("touch.grid", NULL) != NULL) {
        if (ico_config_set_int("touch.grid", (long long)(e->grid + 0.5f)) != 0) {
            return -1;
        }
        n++;
    }
    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            const IcoTouchCtlEdit *ed = &e->ctl[r][c];

            snprintf(path, sizeof(path), "touch.%s.%s", ico_touch_posture_name(r),
                     ico_touch_ctl_name(c));
            old = ico_config_get_string(path, NULL);
            if (ed->set) {
                ico_touch_edit_text(ed, text, sizeof(text));
            } else if (old != NULL && strcmp(old, "default") != 0) {
                snprintf(text, sizeof(text), "default"); /* reset: the line stays, as default */
            } else {
                continue; /* unset, and the file has nothing (or default) there */
            }
            if (ico_config_set_string(path, text) != 0) {
                return -1;
            }
            n++;
        }
    }
    if (format) {
        if (ico_config_set_int("touch.format", ICO_TOUCH_EDIT_FORMAT) != 0) {
            return -1;
        }
        n++;
    }
    return n;
}
