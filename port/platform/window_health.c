/*
 * port/platform/window_health.c
 *
 * The window's part of the picture-health guard (window_health.h,
 * port/render/rd_health.h).
 */
#include "window_health.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "ico_version.h"
#include "rd_health.h"

#define KEY_V "dev.picture_health"
#define KEY_BY "dev.picture_health_by"

static uint32_t s_seq;
static RdHealthMark s_mark;

static const char *game_version(void)
{
    return ICO_VERSION_LABEL[0] != '\0' ? ICO_VERSION_LABEL : "dev";
}

static const char *name_of(const char *cfgName)
{
    return cfgName != NULL && cfgName[0] != '\0' ? cfgName : "config.toml";
}

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void window_health_start(const char *cfgName)
{
    static int s_done; /* once a process: a renderer made again keeps the run's guard */
    char was[16], wasBy[288];

    if (s_done) {
        return;
    }
    s_done = 1;
    const char *v = ico_config_get_string(KEY_V, "");
    const char *by = ico_config_get_string(KEY_BY, "");

    snprintf(was, sizeof was, "%s", v != NULL ? v : "");
    snprintf(wasBy, sizeof wasBy, "%s", by != NULL ? by : "");
    const int k = rd_health_mark_start(was, wasBy, game_version(), &s_mark);
    if (k == RD_HEALTH_START_NONE) {
        rd_health_set_start(NULL, game_version());
        return;
    }
    if (k != RD_HEALTH_START_KEEP) {
        const int removed = ico_config_set_string(KEY_V, "") == 0 &&
                            ico_config_set_string(KEY_BY, "") == 0 && ico_config_save() == 0;
        fprintf(stderr,
                "window: picture health: [dev] picture_health = \"%s\" (picture_health_by = "
                "\"%s\") %s %s: %s; this start begins with nothing turned off\n",
                was, wasBy, removed ? "removed from" : "could not be removed from",
                name_of(cfgName),
                k == RD_HEALTH_START_DROP_VERSION ? "written by another version of the game (an "
                                                    "update starts the guard afresh)"
                                                  : "not in the form the game writes");
        rd_health_set_start(NULL, game_version());
        return;
    }
    rd_health_set_start(&s_mark, game_version());
    char parts[512];
    const uint32_t off = rd_health_parts_of(s_mark.groups);
    rd_health_parts_text(parts, sizeof parts, off);
    fprintf(stderr,
            "window: picture health: [dev] picture_health = \"%s\" (picture_health_by = \"%s\") "
            "in %s: this start begins with these turned off: %s\n",
            was, wasBy, name_of(cfgName), parts);
    if ((off >> RD_HEALTH_P_ROBUST) & 1u) {
        /* group (e): robust access on for this start, as the reporter's
           v0.11.5 ran it; the player's gpu_robust (ico-pc.ini or a [dev]
           line, exported before this) rules where set; after the loss
           steps' own (window_host.c, before this call) nothing to add */
        const char *said = getenv("ICO_GPU_ROBUST");
        const char *loss = getenv("ICO_GPU_ROBUST_AFTER_LOSS");
        if (loss != NULL && loss[0] == '1') {
            fprintf(stderr, "window: picture health: robust access on for this start already "
                            "(the steps after a lost start)\n");
        } else if (said != NULL && said[0] != '\0') {
            fprintf(stderr,
                    "window: picture health: robust access as gpu_robust sets it (%s): the "
                    "player's setting rules\n",
                    said);
        } else {
            set_env("ICO_GPU_ROBUST", "1");
            set_env("ICO_GPU_ROBUST_BY_GUARD", "1");
            fprintf(stderr, "window: picture health: robust access on for this start (group "
                            "(e)); nothing is written to the settings for it\n");
        }
    }
}

void window_health_pump(void)
{
    char value[16], by[288];

    if (!rd_health_persist_take(&s_seq, value, sizeof value, by, sizeof by)) {
        return;
    }
    const char *cfg = ico_config_toml_path() != NULL ? ico_config_toml_path() : "config.toml";
    if (ico_config_set_string(KEY_V, value) == 0 && ico_config_set_string(KEY_BY, by) == 0 &&
        ico_config_save() == 0) {
        fprintf(stderr,
                "window: picture health: [dev] picture_health = \"%s\" and picture_health_by = "
                "\"%s\" written to %s: every start of the same version begins there\n",
                value, by, cfg);
    } else {
        fprintf(stderr,
                "window: picture health: [dev] picture_health = \"%s\" could not be written to "
                "%s: the next start begins without it\n",
                value, cfg);
    }
}
