/*
 * port/platform/sim_keys.c
 *
 * The config keys that change the simulation (sim_keys.h): the pad
 * recording's header lists them and a save state's header hashes them.
 * Compiled into ico_config (port/config/CMakeLists.txt), whose getters it
 * reads.
 */
#include "sim_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config/config.h"
#include "xxh3.h"

const char *const ICO_SIM_KEYS[] = {
    "video.video_mode",
    "game.language",
    "gameplay.mirror",
    "gameplay.stick_fix",
    "gameplay.developer_mode",
    "dev.start_stage",
    "gameplay.door_fix",
    "video.anti_aliasing",
    "gameplay.hold_type",
    "gameplay.hold_type_chosen", /* an earlier "hold" without it is the disc's own */
    "gameplay.skip_opening",
    /* the game variants that stay in config (port/game/variants.h) */
    "variants.no_cutscene_look",
    "variants.ignore_cutscene_cameras",
    /* Gameplay > Combat's weapon rows: the weapon objects' kinds (weapon.c) */
    "variants.sword_colour",
    "variants.weapon",
    "developer.free_movement", /* Developer > Free movement (ico_rows.h) */
    /* Gameplay > Chains' chain rows: the swing's pace (chain.c, debug values 1..10) */
    "variants.chain_swing_speed",
    "variants.chain_swing_damping",
    "gameplay.zoom_type",    /* Controls > Zoom type: the camera's zoom latch (zoom_toggle.h) */
    "gameplay.camera_reset", /* Controls > Camera reset: R3 rests the look-around (camera_reset.h) */
    NULL,
};

/* The keys that left ICO_SIM_KEYS in v0.11.3: rows of each save's difficulty
   record (port/game/difficulty.h), which a state carries as its own block
   ("difficulty.record").  A state saved before lists them in its keys text;
   state_file.c's key check passes over them (a state is never refused for
   one), and difficulty.c builds an older state's record from them. */
const char *const ICO_SIM_KEYS_RETIRED[] = {
    "gameplay.yorda_safe",
    "gameplay.pit_time",
    "gameplay.shadow_speed",
    "gameplay.yorda_trips",
    "gameplay.shadows_together", /* the Shadows' turns (enemy_act.c) */
    "variants.shade_toughness",
    "variants.one_hit",
    "variants.never_dodge",
    "variants.aggression",
    "variants.no_hints",
    "variants.barrier_hits",
    "variants.auto_guard",
    "variants.attack_turn",
    "variants.shadows_wait_down",
    "variants.peaceful_shadows", /* no brain thread for a Shadow */
    NULL,
};

int ico_sim_key_retired(const char *key, size_t key_n)
{
    for (int i = 0; key != NULL && ICO_SIM_KEYS_RETIRED[i] != NULL; i++) {
        const char *k = ICO_SIM_KEYS_RETIRED[i];

        if (strlen(k) == key_n && memcmp(k, key, key_n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Each key's default as the config text gives it (the value the game takes
   when the key is unset), NULL where none is one fixed text (unset is the
   disc's own or the device's choice).  A key absent from a saved state's
   keys text (a key a later version added) reads as its default.  A key
   added later gives its default here, in the same place as in the list. */
static const char *const kDefaults[] = {
    NULL,       /* video.video_mode: unset asks the memory card / the disc */
    "auto",     /* game.language */
    "false",    /* gameplay.mirror */
    "false",    /* gameplay.stick_fix (Off on every platform, options.c) */
    "false",    /* gameplay.developer_mode */
    NULL,       /* dev.start_stage */
    "false",    /* gameplay.door_fix */
    "off",      /* video.anti_aliasing */
    "disc",     /* gameplay.hold_type */
    "false",    /* gameplay.hold_type_chosen */
    "false",    /* gameplay.skip_opening */
    "false",    /* variants.no_cutscene_look */
    "false",    /* variants.ignore_cutscene_cameras */
    "original", /* variants.sword_colour */
    "original", /* variants.weapon */
    "false",    /* developer.free_movement */
    "4",        /* variants.chain_swing_speed */
    "4",        /* variants.chain_swing_damping */
    "hold",     /* gameplay.zoom_type */
    "off",      /* gameplay.camera_reset */
    NULL,
};

_Static_assert(sizeof(kDefaults) / sizeof(kDefaults[0]) ==
                   sizeof(ICO_SIM_KEYS) / sizeof(ICO_SIM_KEYS[0]),
               "every simulation key has its default (NULL: none)");

/* What a load does about each key when the state's value is not this run's
   (sim_keys.h IcoSimKeyCarry), in the list's order.  A key added later
   declares its carry here, in the same place as in the list. */
static const IcoSimKeyCarry kCarry[] = {
    /* video.video_mode: the restore puts the game's mode word back
       (systemStatus[0]), which the host's clock follows; the setting is
       read at the start (main.c ico_boot_video_mode) */
    ICO_SIM_CARRY_KEPT,
    /* game.language: the restore puts the game's language back
       (NonLinearCameraMove): this run's is set again as the Language row
       does (settings.c) */
    ICO_SIM_CARRY_FIXUP,
    /* gameplay.mirror: options.c's flag is not in the state; it is the
       running journey's (its save slot's [mirror] keys) */
    ICO_SIM_CARRY_HARMLESS,
    /* gameplay.stick_fix: read on every pad read (pad_host.c) */
    ICO_SIM_CARRY_HARMLESS,
    /* gameplay.developer_mode: read live; the Debug menu a state left open
       is closed when developer mode is off now (settings.c) */
    ICO_SIM_CARRY_FIXUP,
    /* dev.start_stage: read at the start only */
    ICO_SIM_CARRY_HARMLESS,
    /* gameplay.door_fix: a grace the state carries (door_grace.c's blocks)
       ends when the fix is off now */
    ICO_SIM_CARRY_FIXUP,
    /* video.anti_aliasing: the renderer's, not in the state */
    ICO_SIM_CARRY_HARMLESS,
    /* gameplay.hold_type and hold_type_chosen: the restore puts the game's
       hold type back (optionControlType): this run's is set again */
    ICO_SIM_CARRY_FIXUP,
    ICO_SIM_CARRY_FIXUP,
    /* gameplay.skip_opening: read as the opening runs (op.c, kanbanBoot.c),
       as a change on the menu would be */
    ICO_SIM_CARRY_HARMLESS,
    /* variants.no_cutscene_look, variants.ignore_cutscene_cameras: the
       restore puts the game's debug switches back: this run's values are
       set again (variants.c ico_variants_state_loaded) */
    ICO_SIM_CARRY_FIXUP,
    ICO_SIM_CARRY_FIXUP,
    /* variants.sword_colour, variants.weapon: read as a weapon is made
       (variants.c ico_variant_weapon_kind), as a change on the menu would */
    ICO_SIM_CARRY_HARMLESS,
    ICO_SIM_CARRY_HARMLESS,
    /* developer.free_movement: read live (ico_rows.c) */
    ICO_SIM_CARRY_HARMLESS,
    /* variants.chain_swing_speed, variants.chain_swing_damping: the game's
       debug switches, as the cutscene rows */
    ICO_SIM_CARRY_FIXUP,
    ICO_SIM_CARRY_FIXUP,
    /* gameplay.zoom_type: the latch's block clears itself on a tick of Hold
       (zoom_toggle.c ico_zoom_held) and starts off in a state of Hold */
    ICO_SIM_CARRY_HARMLESS,
    /* gameplay.camera_reset: the mute clears on the first tick after the
       load (camera_reset.c: a call that does not follow the last by one
       tick) and when the row is off */
    ICO_SIM_CARRY_HARMLESS,
};

_Static_assert(sizeof(kCarry) / sizeof(kCarry[0]) + 1 ==
                   sizeof(ICO_SIM_KEYS) / sizeof(ICO_SIM_KEYS[0]),
               "every simulation key declares what a load does with it");

int ico_sim_key_carry(const char *key)
{
    int i;

    for (i = 0; key != NULL && ICO_SIM_KEYS[i] != NULL; i++) {
        if (strcmp(ICO_SIM_KEYS[i], key) == 0) {
            return (int)kCarry[i];
        }
    }
    return -1;
}

const char *ico_sim_key_carry_name(int carry)
{
    switch (carry) {
    case ICO_SIM_CARRY_HARMLESS:
        return "harmless";
    case ICO_SIM_CARRY_FIXUP:
        return "fix-up";
    case ICO_SIM_CARRY_KEPT:
        return "kept";
    }
    return "?";
}

const char *ico_sim_key_default(const char *key)
{
    int i;

    for (i = 0; key != NULL && ICO_SIM_KEYS[i] != NULL; i++) {
        if (strcmp(ICO_SIM_KEYS[i], key) == 0) {
            return kDefaults[i];
        }
    }
    return NULL;
}

int ico_sim_key_is_default(const char *key, size_t key_n, const char *value, size_t value_n)
{
    int i;

    for (i = 0; key != NULL && ICO_SIM_KEYS[i] != NULL; i++) {
        const char *k = ICO_SIM_KEYS[i];

        if (strlen(k) == key_n && memcmp(k, key, key_n) == 0) {
            /* unset, or the default's text */
            return value_n == 0 || (kDefaults[i] != NULL && strlen(kDefaults[i]) == value_n &&
                                    memcmp(kDefaults[i], value, value_n) == 0);
        }
    }
    return 0;
}

/* "key=value\n" for every key, the value as the config gives it ("" when
   unset), into a malloc'd text; its length in *len.  NULL out of memory. */
static char *keys_text(size_t *len)
{
    size_t need = 1;
    size_t at = 0;
    char *t;
    int i;

    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        need += strlen(ICO_SIM_KEYS[i]) + 2 + strlen(ico_config_get_string(ICO_SIM_KEYS[i], ""));
    }
    t = (char *)malloc(need);
    if (t == NULL) {
        return NULL;
    }
    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        const char *k = ICO_SIM_KEYS[i];
        const char *v = ico_config_get_string(k, "");
        const size_t kn = strlen(k);
        const size_t vn = strlen(v);

        if (at + kn + vn + 2 >= need) {
            break; /* a value changed between the two passes */
        }
        memcpy(t + at, k, kn);
        t[at + kn] = '=';
        memcpy(t + at + kn + 1, v, vn);
        t[at + kn + 1 + vn] = '\n';
        at += kn + vn + 2;
    }
    t[at] = '\0';
    *len = at;
    return t;
}

uint64_t ico_sim_keys_hash(char *text, size_t size)
{
    size_t len = 0;
    char *t = keys_text(&len);
    uint64_t h;

    if (t == NULL) {
        if (text != NULL && size > 0) {
            text[0] = '\0';
        }
        return 0;
    }
    h = xxh3_64(t, len);
    if (text != NULL && size > 0) {
        const size_t n = len < size - 1 ? len : size - 1;

        memcpy(text, t, n);
        text[n] = '\0';
    }
    free(t);
    return h;
}

/* the value of key in a "key=value\n" text: 1 with its start and length */
static int saved_value(const char *text, const char *key, const char **v, size_t *n)
{
    const size_t kn = strlen(key);
    const char *line = text;

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        const size_t len = end != NULL ? (size_t)(end - line) : strlen(line);

        if (len > kn && memcmp(line, key, kn) == 0 && line[kn] == '=') {
            *v = line + kn + 1;
            *n = len - kn - 1;
            return 1;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return 0;
}

int ico_sim_keys_value(const char *text, const char *key, char *out, size_t size)
{
    const char *v = "";
    size_t n = 0;
    const int found = key != NULL && saved_value(text != NULL ? text : "", key, &v, &n);

    if (out != NULL && size > 0) {
        const size_t m = n < size - 1 ? n : size - 1;

        memcpy(out, v, m);
        out[m] = '\0';
    }
    return found;
}

const char *ico_sim_keys_first_diff(const char *saved_text, char *out, size_t size)
{
    int i;

    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        const char *k = ICO_SIM_KEYS[i];
        const char *cur = ico_config_get_string(k, "");
        const char *v = "";
        size_t vn = 0;

        /* a key the saved text lacks is one a later version added: the
           state was made at its default, so it is equal while the key is
           unset or at its default now */
        if (!saved_value(saved_text != NULL ? saved_text : "", k, &v, &vn)) {
            if (ico_sim_key_is_default(k, strlen(k), cur, strlen(cur))) {
                continue;
            }
            if (out != NULL && size > 0) {
                snprintf(out, size, "%s=(absent) (now %s)", k, cur);
            }
            return k;
        }
        if (vn != strlen(cur) || memcmp(v, cur, vn) != 0) {
            /* unset on one side and the default's text on the other: the
               same setting (issue 218: a menu writes the key's default once
               it is opened) */
            if (ico_sim_key_is_default(k, strlen(k), v, vn) &&
                ico_sim_key_is_default(k, strlen(k), cur, strlen(cur))) {
                continue;
            }
            if (out != NULL && size > 0) {
                snprintf(out, size, "%s=%.*s (now %s)", k, (int)vn, v, cur);
            }
            return k;
        }
    }
    return NULL;
}
