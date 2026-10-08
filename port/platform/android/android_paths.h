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
 *   <files>/ico.o2r                    the extracted game data
 *   <files>/memcard/                   the memory cards
 *   <files>/logs/ico-pc.log            the log (+ perf csv, pad recordings)
 *   <files>/dumps/
 *   <files>/textures/SCES-50760/replacements/
 *   <files>/Ico_PAL.<ext>              the picker's copy of the disc image
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
    char textures[ICO_ANDROID_PATH_MAX];       /* files/textures/SCES-50760/replacements */
    char pipeline_cache[ICO_ANDROID_PATH_MAX]; /* cache/pipelines.vkcache */
} IcoAndroidPaths;

/* Fills *out from the two folders (absolute; trailing slashes dropped).
   0, or -1 with *out zeroed when a folder is missing, relative or so long
   that a path in the layout would not fit, or when the cache folder is the
   files folder or inside it (the system may empty the cache). */
int ico_android_layout(const char *files, const char *cache, IcoAndroidPaths *out);

/* <files>/Ico_PAL.<ext> ("iso" or "chd"): where the picker's copy of the
   disc image goes. 0, or -1 (out emptied) when it does not fit. */
int ico_android_image_path(const IcoAndroidPaths *p, const char *ext, char *out, size_t size);

#endif
