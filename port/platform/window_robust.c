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
#include "attempt_ledger.h"
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

/* config.toml's own [dev] gpu_robust (ico-pc.ini's gpu_robust left out:
   ico_config reads the ini over the file) and gpu_robust_by into *by
   (NULL none); 1 when the file says true.  t is the caller's to free */
static int file_on(IcoToml **t, const char **by)
{
    const char *path = ico_config_toml_path();

    *t = path != NULL ? ico_toml_load(path) : NULL;
    *by = *t != NULL ? ico_toml_get(*t, KEY_BY) : NULL;
    return *t != NULL && ico_toml_get_bool(*t, KEY_ON, 0);
}

/* the attempt ledger's row (attempt_ledger.h ICO_ATT_ROBUST_LINE): the
   first start of the window says, and a later one (the window made again)
   only when it found a key without the mark, so the row that took one off
   is not turned to "not applicable" by the start after it */
static void ledger_row(int found, int removed)
{
    static bool s_said;

    if (!s_said || found) {
        s_said = true;
        ico_attempt_decide_robust_line(found, removed);
    }
}

void window_robust_start(const char *cfgName)
{
    RobustMark old = {0};
    RobustStep step;
    IcoToml *t;
    const char *by;
    char line[1024];
    const char *cfg = cfgName != NULL ? cfgName : "config.toml";
    const int lost = ico_diag_startup_end_count(ICO_DIAG_END_LOSS) > 0;
    /* an end while preparing the graphics, or a watchdog end (the game
       stopped responding; a GPU hang is a loss above): not clean */
    const int ended = ico_diag_startup_end_count(ICO_DIAG_END_PIPELINES) > 0 ||
                      ico_diag_startup_end_count(ICO_DIAG_END_VU_PRE) > 0 ||
                      ico_diag_startup_prev_watchdog();
    const int on = file_on(&t, &by);
    const int marked = on && robust_mark_parse(by, &old) == 0;

    /* a key without the game's mark (issue 179: written by v0.7.10 to
       v0.10.1 after two lost starts, or by hand) is another version's: off
       at once by the same removal as a marked key's */
    step = robust_start_step(on, by, game_version(), lost, ended);
    ico_toml_free(t);
    if (step.action == ROBUST_STAYS) {
        if (mark_write(&step.mark) != 0) {
            ico_diag_log("ico_pc: the count of clean starts could not be written to %s", cfg);
        }
    } else if (step.action != ROBUST_NONE) {
        if (keys_remove() != 0) {
            ico_diag_log("ico_pc: robust access could not be turned off in %s; it stays on", cfg);
            ledger_row(step.action == ROBUST_OFF_UNMARKED, 0);
            return;
        }
        /* robust access off for this start too, unless ico-pc.ini's line
           asks for it (the copy in memory read again: the ini alone) */
        if (!ico_config_get_bool(KEY_ON, 0)) {
            set_env_empty();
        }
    }
    ledger_row(step.action == ROBUST_OFF_UNMARKED, 1);
    if (step.action != ROBUST_NONE &&
        robust_persist_line(line, sizeof(line), &step, marked ? &old : NULL, game_version(), cfg) ==
            0) {
        ico_diag_log("%s", line);
    }
    /* on with no mark of the game's in config.toml: ico-pc.ini's line (a
       bug report's), never touched, said every start */
    if (step.action != ROBUST_STAYS && ico_config_get_bool(KEY_ON, 0) &&
        robust_ini_line(line, sizeof(line), ico_config_ini_path()) == 0) {
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
