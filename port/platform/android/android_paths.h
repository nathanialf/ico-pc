/*
 * port/platform/android/android_paths.h
 *
 * Where the Android build keeps its files. Everything the port writes lives
 * in the app's files folder (Android/data/<package>/files on the shared
 * storage when it is writable, which the Files app and a USB cable reach,
 * else the app's internal folder); only the Vulkan pipeline cache, which the
 * system may delete when space runs low, lives in the cache folder:
 *
 *   <files>/ico-pc.ini
 *   <files>/config.toml
 *   <files>/achievements.toml          the achievements
 *   <files>/ico.o2r                    the extracted game data (European disc)
 *   <files>/ico-jpn.o2r                the same of the Japanese disc
 *   <files>/memcard/                   the memory cards
 *   <files>/memcard-preview/           the August 2001 preview disc's own card
 *   <files>/logs/ico-pc.log            the log (+ perf csv, pad recordings)
 *   <files>/dumps/
 *   <files>/drivers/                   driver zips dropped by hand (Add from the
 *                                      drivers folder; the game deletes one once added)
 *   <files>/textures/<serial>/replacements/   (SCES-50760, SCPS-11003 or SCPS-56001)
 *   <files>/Ico_PAL.<ext>              the picker's copy of the disc image
 *                                      (Ico_JPN.<ext> or Ico_KOR.<ext>
 *                                      once read as that disc)
 *   <cache>/pipelines.vkcache
 *
 * Pure C with no Android call, so the layout is unit-tested on every
 * platform (test/android_paths_test.c); host_android.c feeds it SDL's
 * folders.
 */
#ifndef ICO_PLATFORM_ANDROID_PATHS_H
#define ICO_PLATFORM_ANDROID_PATHS_H

#include <stddef.h>

/* host_config.h's ICO_PATH_MAX: every path here fits the port's buffers */
#define ICO_ANDROID_PATH_MAX 1024

typedef struct IcoAndroidPaths {
    char files[ICO_ANDROID_PATH_MAX];          /* the files folder, no trailing slash */
    char cache[ICO_ANDROID_PATH_MAX];          /* the cache folder, no trailing slash */
    char ini[ICO_ANDROID_PATH_MAX];            /* files/ico-pc.ini */
    char config[ICO_ANDROID_PATH_MAX];         /* files/config.toml */
    char archive[ICO_ANDROID_PATH_MAX];        /* files/ico.o2r */
    char memcard[ICO_ANDROID_PATH_MAX];        /* files/memcard */
    char logs[ICO_ANDROID_PATH_MAX];           /* files/logs */
    char log[ICO_ANDROID_PATH_MAX];            /* files/logs/ico-pc.log */
    char dumps[ICO_ANDROID_PATH_MAX];          /* files/dumps */
    char drivers[ICO_ANDROID_PATH_MAX];        /* files/drivers */
    char textures[ICO_ANDROID_PATH_MAX];       /* files/textures/<serial>/replacements */
    char pipeline_cache[ICO_ANDROID_PATH_MAX]; /* cache/pipelines.vkcache */
} IcoAndroidPaths;

/* Fills *out from the two folders (absolute; trailing slashes dropped) and
   the disc's serial for the texture pack folder ("SCES-50760",
   "SCPS-11003", "SCPS-56001"; NULL is SCES-50760, before the disc is known).
   0, or -1 with *out zeroed when a folder is missing, relative or so long
   that a path in the layout would not fit, or when the cache folder is the
   files folder or inside it (the system may empty the cache). */
int ico_android_layout(const char *files, const char *cache, const char *serial,
                       IcoAndroidPaths *out);

/* <files>/<base>.<ext> (base "Ico_PAL", "Ico_JPN" or "Ico_KOR", NULL for Ico_PAL; ext
   "iso", "chd" or "bin"): where the picker's copy of the disc image goes.
   0, or -1 (out emptied) when it does not fit. */
int ico_android_image_path(const IcoAndroidPaths *p, const char *base, const char *ext, char *out,
                           size_t size);

#endif
