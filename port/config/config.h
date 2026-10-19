/*
 * port/config/config.h
 *
 * The port's settings: config.toml in the per-user folder, with ico-pc.ini
 * as an override layer (beside the executable; in the per-user folder on
 * macOS: host_config.c ico_host_ini_path).
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
/* 1 when a setter changed the copy in memory since it was read or last
   saved (the Android lifecycle saves on termination only then), else 0. */
int ico_config_dirty(void);

/* The first run: when config.toml does not exist, writes the commented
   template first_run_text (config.c; a superset of the keys
   ico_config_save adds, at their defaults). Never
   replaces an existing file. 0 written, 1 it exists already, -1 it cannot be
   written (the log says why). */
int ico_config_write_first_run(void);

/* The two files in use (after the first read). */
const char *ico_config_toml_path(void);
const char *ico_config_ini_path(void);

/* Tests: forget what was read and use these files (NULL: the defaults, the
   per-user folder's config.toml and ico_host_ini_path's ico-pc.ini);
   they are read on next use. */
void ico_config_reset(const char *toml_path, const char *ini_path);

/* [game] disc: the row of config/regions.txt it pins ("pal" 0, "jpn" 1;
   port/core region.h), or -1 for "auto", the default, which lets the disc
   image given and the game data there decide (main_host.c). Any other
   value is auto, logged once a load. */
int ico_config_disc_pin(void);
/* [game] last_disc, written by the program (not in the template): the row
   of the disc a start last played, or -1 (absent, or not a row's slug). */
int ico_config_last_disc(void);
/* Sets [game] last_disc to slug in memory (ico_config_save writes it); 0,
   or -1 for a slug that is not a row. */
int ico_config_set_last_disc(const char *slug);

#endif /* ICO_CONFIG_CONFIG_H */
