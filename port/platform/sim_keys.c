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
    NULL,
};

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

        /* a key the saved text lacks was unset there (an older build is
           refused by its build id first) */
        (void)saved_value(saved_text != NULL ? saved_text : "", k, &v, &vn);
        if (vn != strlen(cur) || memcmp(v, cur, vn) != 0) {
            if (out != NULL && size > 0) {
                snprintf(out, size, "%s=%.*s (now %s)", k, (int)vn, v, cur);
            }
            return k;
        }
    }
    return NULL;
}
