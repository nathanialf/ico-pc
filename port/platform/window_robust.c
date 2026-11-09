/*
 * port/platform/window_robust.c
 *
 * The window's part of the port's own [dev] gpu_robust turning itself off
 * (window_robust.h, robust_persist.h).
 */
#include "window_robust.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "diag_host.h"
#include "host_config.h"
#include "ico_version.h"
#include "robust_persist.h"

#define KEY_ON "dev.gpu_robust"
#define KEY_BY "dev.gpu_robust_by"

static const char *game_version(void)
{
    return ICO_VERSION_LABEL[0] != '\0' ? ICO_VERSION_LABEL : "dev";
}

static void set_env_empty(void)
{
#ifdef _WIN32
    _putenv_s("ICO_GPU_ROBUST", "");
#else
    setenv("ICO_GPU_ROBUST", "", 1);
#endif
}

/* the mark of config.toml's [dev] gpu_robust_by when robust access is on
   by it; 0 when there is one */
static int mark_read(RobustMark *mark)
{
    return ico_config_get_bool(KEY_ON, 0)
               ? robust_mark_parse(ico_config_get_string(KEY_BY, NULL), mark)
               : -1;
}

static int mark_write(const RobustMark *mark)
{
    char value[96];

    return robust_mark_format(value, sizeof(value), mark) == 0 &&
                   ico_config_set_string(KEY_BY, value) == 0 && ico_config_save() == 0
               ? 0
               : -1;
}

/* both keys removed from the file, the copy in memory read again */
static int keys_remove(void)
{
    const char *path = ico_config_toml_path();
    char *text, *stripped;
    int r;

    if (path == NULL || (text = ico_toml_read_text(path)) == NULL) {
        return -1;
    }
    stripped = robust_persist_strip(text);
    free(text);
    if (stripped == NULL) {
        return -1;
    }
    r = ico_toml_write_text(stripped, path);
    free(stripped);
    if (r == 0) {
        ico_config_reset(path, ico_config_ini_path());
    }
    return r;
}

void window_robust_start(const char *cfgName)
{
    RobustMark old;
    RobustStep step;
    char line[512];
    const int lost = ico_diag_startup_end_count(ICO_DIAG_END_LOSS) > 0;
    /* an end while preparing the graphics, or a watchdog end (the game
       stopped responding; a GPU hang is a loss above): not clean */
    const int ended = ico_diag_startup_end_count(ICO_DIAG_END_PIPELINES) > 0 ||
                      ico_diag_startup_end_count(ICO_DIAG_END_VU_PRE) > 0 ||
                      ico_diag_startup_prev_watchdog();

    if (mark_read(&old) != 0) {
        /* robust access on with no mark of the game's: set by hand, or by a
           game that wrote no mark: never touched, said every start */
        if (ico_config_get_bool(KEY_ON, 0) &&
            robust_unmarked_line(line, sizeof(line), cfgName) == 0) {
            ico_diag_log("%s", line);
        }
        return;
    }
    step = robust_persist_step(&old, game_version(), lost, ended);
    if (step.action == ROBUST_STAYS) {
        if (mark_write(&step.mark) != 0) {
            ico_diag_log("ico_pc: the count of clean starts could not be written to %s",
                         cfgName != NULL ? cfgName : "config.toml");
        }
    } else if (step.action != ROBUST_NONE) {
        if (keys_remove() != 0) {
            ico_diag_log("ico_pc: robust access could not be turned off in %s; it stays on",
                         cfgName != NULL ? cfgName : "config.toml");
            return;
        }
        set_env_empty();
    }
    if (robust_persist_line(line, sizeof(line), &step, &old, game_version(), cfgName) == 0) {
        ico_diag_log("%s", line);
    }
}

int window_robust_persist(void)
{
    RobustMark mark = {0};

    snprintf(mark.version, sizeof(mark.version), "%s", game_version());
    return ico_config_set_bool(KEY_ON, 1) == 0 && mark_write(&mark) == 0 ? 0 : -1;
}

void window_robust_reached(void)
{
    static bool s_done;
    RobustMark mark;

    if (s_done || mark_read(&mark) != 0) {
        return;
    }
    s_done = true;
    if (!mark.running) {
        mark.running = 1;
        (void)mark_write(&mark);
    }
}
