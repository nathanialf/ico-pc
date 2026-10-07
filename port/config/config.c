/*
 * port/config/config.c
 *
 * config.toml and ico-pc.ini as one settings store (config.h). The parsing
 * and writing are host_config.c's.
 */
#include "config.h"
#include "host_config.h"
#include "host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int loaded;
static char toml_file[ICO_PATH_MAX];
static char ini_file[ICO_PATH_MAX];
static char want_toml[ICO_PATH_MAX];
static char want_ini[ICO_PATH_MAX];
static IcoToml *toml;
static IcoIni ini;

void ico_config_reset(const char *toml_path, const char *ini_path)
{
    ico_toml_free(toml);
    toml = NULL;
    loaded = 0;
    snprintf(want_toml, sizeof(want_toml), "%s", toml_path != NULL ? toml_path : "");
    snprintf(want_ini, sizeof(want_ini), "%s", ini_path != NULL ? ini_path : "");
}

/* Keys a former version read and this one does not: kept in the file (as
   any unknown key is) and logged once a load.  [game] classic_menu_text and
   [game] port_font chose how the game's own words were drawn; they always
   keep their texels now. */
static const char *const retired_keys[] = {"game.classic_menu_text", "game.port_font"};

static void log_retired(void)
{
    for (size_t i = 0; i < sizeof(retired_keys) / sizeof(retired_keys[0]); i++) {
        if (ico_toml_has(toml, retired_keys[i])) {
            fprintf(stderr, "config: %s is no longer used; ignored\n", retired_keys[i]);
        }
    }
}

static void ensure(void)
{
    char dir[ICO_PATH_MAX];

    if (loaded) {
        return;
    }
    loaded = 1;
    if (want_toml[0] != '\0') {
        snprintf(toml_file, sizeof(toml_file), "%s", want_toml);
    } else {
        ico_host_pref_dir(dir, sizeof(dir));
        if (ico_path_join(toml_file, sizeof(toml_file), dir, "config.toml") != 0) {
            fprintf(stderr, "config: the per-user folder's path is too long for config.toml\n");
        }
    }
    if (want_ini[0] != '\0') {
        snprintf(ini_file, sizeof(ini_file), "%s", want_ini);
    } else {
        ico_host_ini_path(ini_file, sizeof(ini_file));
    }
    ico_ini_load_file(&ini, ini_file);
    toml = ico_toml_load(toml_file);
    if (toml == NULL) {
        toml = ico_toml_parse("");
        fprintf(stderr, "config: no %s; defaults\n", toml_file);
    } else {
        long long v = ico_toml_get_int(toml, "version", ICO_CONFIG_VERSION);

        fprintf(stderr, "config: %s (version %lld)\n", toml_file, v);
        if (v > ICO_CONFIG_VERSION) {
            fprintf(stderr, "config: written by a newer version; unknown keys are kept\n");
        }
        log_retired();
    }
}

const char *ico_config_toml_path(void)
{
    ensure();
    return toml_file;
}

const char *ico_config_ini_path(void)
{
    ensure();
    return ini_file;
}

/* the ini's value for a toml path, or NULL */
static const char *ini_value(const char *path)
{
    const char *key = ico_config_ini_key(path);
    const char *v = key != NULL ? ico_ini_get(&ini, key) : NULL;

    return v != NULL && v[0] != '\0' ? v : NULL;
}

const char *ico_config_get_string(const char *path, const char *def)
{
    const char *v;

    ensure();
    v = ini_value(path);
    if (v == NULL) {
        v = ico_toml_get(toml, path);
    }
    return v != NULL ? v : def;
}

int ico_config_get_bool(const char *path, int def)
{
    const char *v;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0) {
            return 1;
        }
        if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0) {
            return 0;
        }
    }
    return ico_toml_get_bool(toml, path, def);
}

long long ico_config_get_int(const char *path, long long def)
{
    const char *v;
    char *end;
    long long n;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        n = strtoll(v, &end, 10);
        if (*end == '\0') {
            return n;
        }
    }
    return ico_toml_get_int(toml, path, def);
}

double ico_config_get_float(const char *path, double def)
{
    const char *v;
    char *end;
    double d;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        d = strtod(v, &end);
        if (*end == '\0') {
            return d;
        }
    }
    return ico_toml_get_float(toml, path, def);
}

int ico_config_set_string(const char *path, const char *value)
{
    ensure();
    return ico_toml_set_string(toml, path, value);
}

int ico_config_set_bool(const char *path, int value)
{
    ensure();
    return ico_toml_set_bool(toml, path, value);
}

int ico_config_set_int(const char *path, long long value)
{
    ensure();
    return ico_toml_set_int(toml, path, value);
}

int ico_config_set_float(const char *path, double value)
{
    ensure();
    return ico_toml_set_float(toml, path, value);
}

int ico_config_save(void)
{
    int r = 0;

    ensure();
    if (!ico_toml_has(toml, "version")) {
        r |= ico_toml_set_int(toml, "version", ICO_CONFIG_VERSION);
    }
    if (!ico_toml_has(toml, "paths.iso")) {
        r |= ico_toml_set_string(toml, "paths.iso", "");
    }
    if (!ico_toml_has(toml, "video.preset")) {
        r |= ico_toml_set_string(toml, "video.preset", "original");
    }
    if (!ico_toml_has(toml, "video.vsync")) {
        r |= ico_toml_set_bool(toml, "video.vsync", 1);
    }
    if (!ico_toml_has(toml, "video.fullscreen")) {
        r |= ico_toml_set_bool(toml, "video.fullscreen", 0);
    }
    if (!ico_toml_has(toml, "audio.enabled")) {
        r |= ico_toml_set_bool(toml, "audio.enabled", 1);
    }
    if (!ico_toml_has(toml, "audio.volume")) {
        r |= ico_toml_set_float(toml, "audio.volume", 1.0);
    }
    if (!ico_toml_has(toml, "audio.music")) {
        r |= ico_toml_set_float(toml, "audio.music", 1.0);
    }
    if (!ico_toml_has(toml, "audio.effects")) {
        r |= ico_toml_set_float(toml, "audio.effects", 1.0);
    }
    if (!ico_toml_has(toml, "audio.output")) {
        r |= ico_toml_set_string(toml, "audio.output", "auto");
    }
    if (!ico_toml_has(toml, "audio.device")) {
        r |= ico_toml_set_string(toml, "audio.device", "");
    }
    if (!ico_toml_has(toml, "game.language")) {
        r |= ico_toml_set_string(toml, "game.language", "auto");
    }
    if (r != 0 || ico_toml_save(toml, toml_file) != 0) {
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    return 0;
}

/* the file a first run leaves: every key ico_config_save adds, at its
   default, and a line per section */
static const char first_run_text[] =
    "# ico-pc settings. Every key below is at its default; edit and restart.\n"
    "# The Options menu rewrites only the lines it changes, so comments and\n"
    "# keys of your own stay. Keys not listed here keep their defaults.\n"
    "\n"
    "version = 1\n"
    "\n"
    "[paths]\n"
    "# The disc image, a .iso or a .chd; empty asks on the first start.\n"
    "iso = \"\"\n"
    "\n"
    "[video]\n"
    "# \"original\" is the PS2 picture; \"enhanced\" takes resolution,\n"
    "# aspect, texture_filter and full_height as written.\n"
    "preset = \"original\"\n"
    "vsync = true\n"
    "fullscreen = false\n"
    "\n"
    "[audio]\n"
    "# volume, music and effects are 0.0 to 1.0; output is \"auto\" (the memory\n"
    "# card's choice), \"stereo\" or \"mono\"; device is a device name, empty for\n"
    "# the system's default.\n"
    "enabled = true\n"
    "volume = 1.0\n"
    "music = 1.0\n"
    "effects = 1.0\n"
    "output = \"auto\"\n"
    "device = \"\"\n"
    "\n"
    "[game]\n"
    "# \"auto\" follows the system; or en, fr, de, it, es.\n"
    "language = \"auto\"\n";

int ico_config_write_first_run(void)
{
    char tmp[ICO_PATH_MAX + 8];
    FILE *f;
    size_t n = sizeof(first_run_text) - 1;
    int ok;

    ensure();
    if (toml_file[0] == '\0') {
        return -1;
    }
    if (ico_file_exists(toml_file)) {
        return 1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", toml_file);
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    ok = fwrite(first_run_text, 1, n, f) == n;
    ok = ico_fsync(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    /* a file that appeared meanwhile is the player's: not replaced */
    if (ok && ico_file_exists(toml_file)) {
        ico_remove(tmp);
        return 1;
    }
    if (!ok || ico_rename_replace(tmp, toml_file) != 0) {
        ico_remove(tmp);
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    fprintf(stderr, "config: wrote %s (first run)\n", toml_file);
    return 0;
}
