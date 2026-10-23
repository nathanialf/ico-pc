/*
 * port/platform/android/host_android.h
 *
 * The Android side of the host layer (host_android.c): the app's folders
 * (the layout is android_paths.h's), free space, the log mirror and the
 * message box, the screen's density and fold, the file picker. Compiled
 * into libmain.so only.
 */
#ifndef ICO_PLATFORM_HOST_ANDROID_H
#define ICO_PLATFORM_HOST_ANDROID_H

#include <stddef.h>
#include "android_paths.h"

/* The logcat tag of every line the program writes there. */
#define ICO_ANDROID_LOG_TAG "ico-pc"

/* The host program's main (main_host.c), run by SDL_main
   (main_android.c) on a thread with a large stack; the end-of-run steps
   (ico_host_shutdown, host_loop.h) have run when it returns. */
int ico_host_main(int argc, char **argv);

/* The layout from SDL's folders, worked out once (the files folder is made
   if missing): the external files folder when the shared storage is
   writable, else the internal one; the cache folder for the pipeline
   cache. NULL when neither folder is known or a path would not fit. */
const IcoAndroidPaths *ico_android_paths(void);

/* The disc is known (main_host.c): the layout's texture pack folder
   becomes textures/<serial>/replacements, made if missing. 0, or -1. */
int ico_android_select_serial(const char *serial);
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
/* The same for the end of a crash (diag_host.h ico_diag_set_fatal_ui):
   no stdio, and the mirror's lock only if it comes within half a second
   (the crashing thread may be the reader). */
void ico_android_log_mirror_try_flush(void);
/* The error box for the end of a crash or the watchdog (diag_host.h
   ico_diag_set_fatal_ui): ico_android_message_box without stdio. */
void ico_android_fatal_box(const char *text);

/* A blocking message box (SDL_ShowSimpleMessageBox, which works before
   SDL_Init) titled "ICO PC"; the text also goes to logcat. */
void ico_android_message_box(const char *text, int error);
/* The same with buttons (SDL_ShowMessageBox, error style, over the SDL
   window given, or none): labels[0..count-1] (at most 4) left to right;
   enter is the one Enter chooses, quit the one Escape chooses.  Blocks
   until one is picked and returns its index; quit when the box cannot be
   shown. */
int ico_android_message_box_buttons(void *window, const char *text, const char *const *labels,
                                    int count, int enter, int quit);

/* Vibrates the phone (IcoActivity.vibrate) at amplitude 1 to 255 for ms
   milliseconds; amplitude 0 stops it. Must be called on SDL's main thread
   (the host loop's), the thread SDL_GetAndroidJNIEnv answers for. Does
   nothing when the phone has no vibrator or the call is not available. */
void ico_host_vibrate(int amplitude, int ms);

/* The screen as the activity last reported it (IcoActivity.sendScreen,
   from the system's window-layout callbacks and every configuration
   change): densityDpi is the screen's density in dots per inch; fold is
   the first hinge or fold the system lists, 0 none, 1 vertical (a
   left/right split), 2 horizontal (top/bottom); halfOpened is 1 when it is
   bent part way, separating 1 when the system says it splits the screen in
   two (a hinge with a gap, or bent part way); l, t, r, b its bounds in the
   window's pixels as the activity sees them, and winW, winH that window's
   size, so the bounds can be scaled to SDL's pixel size. */
typedef struct IcoAndroidScreen {
    int densityDpi, fold, halfOpened, separating, l, t, r, b, winW, winH;
} IcoAndroidScreen;

/* Copies the last report into out (when out is not NULL) and returns how
   many reports came in, 0 when none did yet (out is then left as it is).
   A change of the count tells the window loop to lay the touch controls
   out again: folding or unfolding does not always resize the window. Any
   thread. */
int ico_android_screen(IcoAndroidScreen *out);

/* Logs the APK's assets/VERSION.txt (a relative SDL_IOFromFile path reads
   the assets) to stderr. */
void ico_android_log_version(void);
/* Opens the APK's assets/libmain.syms.gz for the crash report's symbol
   lines (diag_host.h ico_diag_set_symbols_fd); not read until a report. */
void ico_android_open_symbols(void);

/* The app's own file picker (IcoActivity.pickFile, issue 135), not SDL's,
   whose one pending-dialog slot the app cannot clear. The answer comes on
   the activity's thread (Android's main thread, not the game's): uri the
   chosen file's content URI, or NULL; why NULL, or why the picker gave no
   usable answer; both NULL when the picker was closed without a file. The
   strings live for the call only. */
typedef void (*IcoAndroidPickFn)(void *user, int gen, const char *uri, const char *why);

/* Starts the system's document picker (ACTION_OPEN_DOCUMENT, else
   ACTION_GET_CONTENT) for pick number gen, from the game's thread (the JNI
   environment is SDL's for the calling thread). fn(user, gen, ...) is
   called once with its answer, unless a later call replaced the pick
   first: then that answer is logged as late and dropped. The pending pick
   is cleared before fn runs, so fn may start the next one. 0 when the
   picker was started; else -1 with the reason in why and *noApp 1 when no
   app on the device takes either intent (0 when the start failed some
   other way). */
int ico_android_pick_file(int gen, IcoAndroidPickFn fn, void *user, int *noApp, char *why,
                          size_t n);

/* ICO_RC_TOOLS: the release candidates' tooling (port/ui/settings.h); 0 in
   every other build, which has none of what follows */
#ifndef ICO_RC_TOOLS
#define ICO_RC_TOOLS 0
#endif
#if ICO_RC_TOOLS
/* An rc build's Share a bug report (window_host.c share_report_pump): the
   n files at paths (UTF-8, under the app's files folder) to the system's
   share menu, titled title, through IcoActivity.shareReport (an
   ACTION_SEND_MULTIPLE chooser of FileProvider content URIs, the provider
   enabled in an rc build's manifest alone).  On SDL's main thread.  0 when
   the menu was asked for, else -1 with the reason in why. */
int ico_android_share_files(const char *title, const char *const *paths, int n, char *why,
                            size_t why_size);
#endif

#endif
