/*
 * port/config/config.c
 *
 * config.toml and ico-pc.ini as one settings store (config.h). The parsing
 * and writing are host_config.c's.
 */
#include "config.h"
#include "host_config.h"
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
        ico_path_join(toml_file, sizeof(toml_file), dir, "config.toml");
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
    if (!ico_toml_has(toml, "game.language")) {
        r |= ico_toml_set_string(toml, "game.language", "auto");
    }
    if (r != 0 || ico_toml_save(toml, toml_file) != 0) {
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    return 0;
}
