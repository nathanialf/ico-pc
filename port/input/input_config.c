/*
 * port/input/input_config.c
 *
 * config.toml's [input] and [gameplay] sections onto the bindings and the
 * pad host (input.h). Kept apart from bindings.c so that the headless build,
 * which has no config.toml, does not pull host_config.c's TOML reader in.
 */
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input.h"

static const char *const scalar_keys[] = {
    "keyboard",          "mouse",       "gamepad",       "rumble", "deadzone", "walk_scale",
    "mouse_sensitivity", "mouse_decay", "mouse_invert_y"};

int ico_input_apply_toml(IcoBindings *b, const struct IcoToml *t)
{
    static const char *const dev[3] = {"kb.", "mouse.", "pad."};
    char path[96], key[80];
    const char *v;
    size_t i;
    int d, k, bad = 0;

    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(scalar_keys) / sizeof(scalar_keys[0]); i++) {
        snprintf(path, sizeof(path), "input.%s", scalar_keys[i]);
        v = ico_toml_get(t, path);
        if (v != NULL && ico_bindings_set(b, scalar_keys[i], v) != 0) {
            bad = -1;
        }
    }
    for (d = 0; d < 3; d++) {
        for (k = -1; k < ICO_T_COUNT; k++) {
            if (k < 0 && d != 0) {
                continue;
            }
            snprintf(key, sizeof(key), "%s%s", dev[d], k < 0 ? "walk" : ico_target_name(k));
            snprintf(path, sizeof(path), "input.%s", key);
            v = ico_toml_get(t, path);
            if (v != NULL && ico_bindings_set(b, key, v) != 0) {
                bad = -1;
            }
        }
    }
    ico_input_set_stick_fix(ico_toml_get_bool(t, "gameplay.stick_fix", 0));
    return bad;
}
