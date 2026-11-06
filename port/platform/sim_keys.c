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
    "gameplay.yorda_safe",
    "gameplay.pit_time",
    "gameplay.shadow_speed",
    "gameplay.developer_mode",
    "dev.start_stage",
    "gameplay.door_fix",
    "gameplay.yorda_trips",
    "video.anti_aliasing",
    "gameplay.hold_type",
    "gameplay.hold_type_chosen", /* an earlier "hold" without it is the disc's own */
    "gameplay.skip_opening",
    /* the game variants (port/game/variants.h): each changes the simulation */
    "variants.shade_toughness",
    "variants.one_hit",
    "variants.never_dodge",
    "variants.aggression",
    "variants.no_hints",
    "variants.barrier_hits",
    "variants.no_cutscene_look",
    "variants.ignore_cutscene_cameras",
    "gameplay.shadows_together", /* the Shadows' turns (enemy_act.c) */
    /* Gameplay > Combat (port/game/variants.h): on is the game as shipped */
    "variants.auto_guard",
    "variants.attack_turn",
    "variants.shadows_wait_down",
    /* Gameplay > Combat's weapon rows: the weapon objects' kinds (weapon.c) */
    "variants.sword_colour",
    "variants.weapon",
    NULL,
};

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
    "false",    /* gameplay.yorda_safe */
    "original", /* gameplay.pit_time */
    "original", /* gameplay.shadow_speed */
    "false",    /* gameplay.developer_mode */
    NULL,       /* dev.start_stage */
    "false",    /* gameplay.door_fix */
    "false",    /* gameplay.yorda_trips */
    "off",      /* video.anti_aliasing */
    "disc",     /* gameplay.hold_type */
    "false",    /* gameplay.hold_type_chosen */
    "false",    /* gameplay.skip_opening */
    "original", /* variants.shade_toughness */
    "false",    /* variants.one_hit */
    "false",    /* variants.never_dodge */
    "own",      /* variants.aggression */
    "false",    /* variants.no_hints */
    NULL,       /* variants.barrier_hits: unset is the disc's own number */
    "false",    /* variants.no_cutscene_look */
    "false",    /* variants.ignore_cutscene_cameras */
    "false",    /* gameplay.shadows_together */
    "true",     /* variants.auto_guard */
    "true",     /* variants.attack_turn */
    "true",     /* variants.shadows_wait_down */
    "original", /* variants.sword_colour */
    "original", /* variants.weapon */
    NULL,
};

_Static_assert(sizeof(kDefaults) / sizeof(kDefaults[0]) ==
                   sizeof(ICO_SIM_KEYS) / sizeof(ICO_SIM_KEYS[0]),
               "every simulation key has its default (NULL: none)");

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
            if (out != NULL && size > 0) {
                snprintf(out, size, "%s=%.*s (now %s)", k, (int)vn, v, cur);
            }
            return k;
        }
    }
    return NULL;
}
