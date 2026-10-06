/*
 * port/config/config.h
 *
 * The port's settings (docs/port/CONFIG.md): config.toml in the per-user
 * folder, with the ico-pc.ini beside the executable as an override layer.
 * Paths are "section.key" ("audio.volume", "game.language").
 *
 * Reading: ini key (for the keys the ini has always had, see
 * ico_config_ini_key) > config.toml > the default the caller gives.
 * Writing changes the config.toml copy in memory; ico_config_save writes it.
 * The files are read on first use.
 */
#ifndef ICO_CONFIG_CONFIG_H
#define ICO_CONFIG_CONFIG_H

#define ICO_CONFIG_VERSION 1

const char *ico_config_get_string(const char *path, const char *def);
int ico_config_get_bool(const char *path, int def);
long long ico_config_get_int(const char *path, long long def);
double ico_config_get_float(const char *path, double def);

/* 0, or -1 (out of memory). */
int ico_config_set_string(const char *path, const char *value);
int ico_config_set_bool(const char *path, int value);
int ico_config_set_int(const char *path, long long value);
int ico_config_set_float(const char *path, double value);

/* Writes config.toml atomically: the file's own lines, comments and keys this
   code does not know are kept; the keys with defaults (version, [video],
   [audio], [game] language, [paths] iso) are added when absent. 0, or -1. */
int ico_config_save(void);

/* The first run: when config.toml does not exist, writes it with the keys
   ico_config_save adds at their defaults and a comment per section. Never
   replaces an existing file. 0 written, 1 it exists already, -1 it cannot be
   written (the log says why). */
int ico_config_write_first_run(void);

/* The two files in use (after the first read). */
const char *ico_config_toml_path(void);
const char *ico_config_ini_path(void);

/* Tests: forget what was read and use these files (NULL: the defaults, the
   per-user folder's config.toml and the ico-pc.ini beside the executable);
   they are read on next use. */
void ico_config_reset(const char *toml_path, const char *ini_path);

#endif /* ICO_CONFIG_CONFIG_H */
