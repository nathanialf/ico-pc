/*
 * port/platform/android/host_android.h
 *
 * The Android side of the host layer (host_android.c): the app's folders
 * (the layout is android_paths.h's), free space, the log mirror and the
 * message box. Compiled into libmain.so only.
 */
#ifndef ICO_PLATFORM_HOST_ANDROID_H
#define ICO_PLATFORM_HOST_ANDROID_H

#include <stddef.h>
#include "android_paths.h"

/* The host program's main (main_host.c), run by SDL_main
   (main_android.c) on a thread with a large stack; the end-of-run steps
   (ico_host_shutdown, host_loop.h) have run when it returns. */
int ico_host_main(int argc, char **argv);

/* The layout from SDL's folders, worked out once (the files folder is made
   if missing): the external files folder when the shared storage is
   writable, else the internal one; the cache folder for the pipeline
   cache. NULL when neither folder is known or a path would not fit. */
const IcoAndroidPaths *ico_android_paths(void);
/* The files folder (where everything the port writes lives) and the cache
   folder; 0, or -1 with out emptied. */
int ico_android_files_dir(char *out, size_t size);
int ico_android_cache_dir(char *out, size_t size);
/* Bytes free to the app on the file system holding dir (statvfs), -1 when
   unknown. */
long long ico_android_free_bytes(const char *dir);

/* Sends stdout and stderr (fds 1 and 2) through a pipe whose reader, an SDL
   thread, appends every byte to log_path (opened O_APPEND) and every line to
   logcat (tag "ico-pc", ANDROID_LOG_INFO). 0, or -1 with the streams left
   as they were. */
int ico_android_log_mirror_start(const char *log_path);
/* Everything written to fds 1 and 2 before the call is in the file when it
   returns (before _exit). A no-op without the mirror. */
void ico_android_log_mirror_flush(void);
/* v0.4.2: the same for the end of a crash (diag_host.h ico_diag_set_fatal_ui):
   no stdio, and the mirror's lock only if it comes within half a second
   (the crashing thread may be the reader). */
void ico_android_log_mirror_try_flush(void);
/* v0.4.2: the error box for the end of a crash or the watchdog (diag_host.h
   ico_diag_set_fatal_ui): ico_android_message_box without stdio. */
void ico_android_fatal_box(const char *text);

/* A blocking message box (SDL_ShowSimpleMessageBox, which works before
   SDL_Init) titled ICO; the text also goes to logcat. */
void ico_android_message_box(const char *text, int error);

/* Vibrates the phone (IcoActivity.vibrate) at amplitude 1 to 255 for ms
   milliseconds; amplitude 0 stops it. Must be called on SDL's main thread
   (the host loop's), the thread SDL_GetAndroidJNIEnv answers for. Does
   nothing when the phone has no vibrator or the call is not available. */
void ico_host_vibrate(int amplitude, int ms);

/* Logs the APK's assets/VERSION.txt (a relative SDL_IOFromFile path reads
   the assets) to stderr. */
void ico_android_log_version(void);

#endif
