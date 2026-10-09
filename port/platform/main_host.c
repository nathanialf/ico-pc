/*
 * port/platform/main_host.c
 *
 * The host program's entry point. The game's own main (common/src/main.c)
 * is compiled as ico_game_main (CMakeLists.txt renames it with a definition
 * on that one source) and runs on the boot fiber (host_loop.c).
 *
 * The window build (ICO_HEADLESS=OFF) opens a window titled "ICO"
 * (window_host.h): the game draws through rd into it, one simulated vsync
 * per real 20 ms (PAL; 16.7 ms at 60 Hz), presented at the frame rate and
 * with the vsync the display options choose, and closing the window (or
 * the title's quit row) ends the run. The headless build (CMake ICO_HEADLESS=ON: the trace and test
 * runs) has no window and simulates vsyncs as fast as it can.
 *
 * Run with no arguments (a double-click), everything comes from the
 * executable's folder (host_config.h describes ico-pc.ini):
 *
 *   logs/ico-pc.log          stdout and stderr, rewritten each run (the
 *                            run before's kept as logs/ico-pc-previous.log)
 *   logs/trace-<time>.txt    the trace (trace_host.h): by default in the
 *                            headless build only; ini trace=1 or PATH turns
 *                            it on in the window build, trace=0 off
 *   game data                the extracted archive ico.o2r (port/data/
 *                            archive.h) in the per-user folder, else beside
 *                            the executable. Without one (the first run) the
 *                            disc image is found as below, verified and
 *                            extracted into the per-user folder once
 *                            (port/data/extract.h), with progress in the log
 *                            and, in the window build, a small progress
 *                            window. use_iso=1 ([dev] use_iso) mounts the
 *                            image directly instead: the default of the
 *                            headless build, so trace runs and tests read
 *                            the ISO as before
 *   disc image               Ico_PAL.iso or Ico_PAL.chd beside the
 *                            executable, else ini iso=, else $ICO_ISO, else
 *                            baserom/Ico_PAL.iso or .chd under the working
 *                            folder, else a (Windows: native; Linux window build: SDL3)
 *                            file-open dialog whose answer is saved as iso=
 *                            in ico-pc.ini. A .chd is read as the ISO it
 *                            holds (port/data/iso9660.c). With use_iso its
 *                            SHA-1 is checked against the SCES-50760
 *                            image's (ini
 *                            verify=0 skips it); the extractor always
 *                            verifies
 *   pad script               ini pad_script= ([dev] pad_script); the
 *                            headless build also takes pad-script.txt beside
 *                            the executable if present; else the live pad
 *   logs/input-<time>.txt    the pad recording (port/input/input_record.h):
 *                            what the game read from the pad, as a pad
 *                            script the headless build replays; on by
 *                            default in the window build, off headless; ini
 *                            input_record= ([dev] input_record) false, true
 *                            or a path
 *   ticks                    ini ticks=N exits after N Main ticks
 *   watchdog                 ini watchdog=S (default 30): the run is stopped
 *                            and reported when no Main tick came S seconds
 *                            after boot started, or no new one for 2*S
 *                            seconds; 0 turns it off (diag_host.h)
 *
 * Developer overrides, which win over all of the above:
 *
 *   ico_pc [--iso PATH] [--pad-script FILE] [--trace FILE|none] [--ticks N]
 *          [--no-verify] [--console] [--help]
 *
 * --console keeps stdout and stderr on the console instead of the log.
 * Options also take the --opt=VALUE spelling. An unknown option or a bad
 * value prints the usage on stderr and exits 2. An error that stops the run
 * (no disc image, a wrong one, a bad pad script) is logged, shown in a
 * message box on Windows, and exits 1.
 *
 * Android: SDL_main (port/platform/android/main_android.c) runs this as
 * ico_host_main on a thread of its own; "the executable's folder" is the
 * app's files folder (port/platform/android/android_paths.h lists what is
 * in it), the log is mirrored to logcat, a fatal error shows SDL's message
 * box and ends the process with _exit, and the end-of-run steps run from
 * ico_host_shutdown before ico_host_main returns instead of atexit.
 * The window opens before the game data is mounted (SDL has one window
 * there), and the disc image is Ico_PAL.iso or .chd in the files folder,
 * else ini iso= when that file exists, else the one chosen in the system's
 * file picker, copied into the files folder first
 * (port/platform/android/iso_import.h); the copy's and the extraction's
 * progress are drawn in the game's window (window_host.h
 * ico_window_progress), Back stops them and the run ends; once the data is
 * mounted, or when Back stops the extraction, the picker's copy is deleted
 * unless ini keep_image=1.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "archive.h"
#include "cdvd_host.h"
#include "config.h"
#include "diag_host.h"
#include "extract.h"
#include "fpenv.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "ico_build_commit.h" /* ICO_BUILD_COMMIT, generated (port/platform/CMakeLists.txt) */
#include "input_record.h"
#include "pad_script.h"
#include "tables.h"
#include "trace_host.h"
#include "options.h" /* the recording's effective stick_fix */
#ifdef __ANDROID__
#include "host_android.h" /* port/platform/android: the version line */
#include "iso_import.h"   /* the first start's copy of the chosen image */
#endif

#ifndef ICO_HEADLESS

#include <SDL3/SDL.h>
#include "rhi.h"
#include "modelpack.h" /* model packs */
#include "settings.h"  /* port/ui: Display > Model pack's "None installed" */
#include "texpack.h"   /* PCSX2 texture packs */
#include "video_options.h"
#include "window_host.h"

#endif
/* Android's window build: the window opens before the game
   data is mounted, the first start chooses the image in the system's
   picker and copies it, and its progress is drawn in the game's window. */
#if defined(__ANDROID__) && !defined(ICO_HEADLESS)
#define ICO_ANDROID_UI 1
#endif
/* Vsyncs without a Main tick after which the loop warns once (the game's
   tick hook may be missing, and then ticks= never ends). */
#define NO_TICK_WARN_VSYNCS 3000u
/* watchdog= when the ini has none: seconds to the first Main tick */
#define WATCHDOG_DEFAULT_S 30u

/* The game's thread functions with external names, for the thread lines
   (main.c names its static idle and scheduler itself). */
void Main(void);
void StageManager(void);
void iosCdvdManager(void);
void iosMcManager(void);
void jimakuManager(void);
void sndManager(void);
void InitIcoMisc(int *arg); /* common/src/icoMisc.c */

typedef struct Args {
    const char *iso;
    const char *pad_script;
    const char *trace;
    const char *ticks;
    int no_verify;
    int console;
} Args;

static char log_path[ICO_PATH_MAX];

/* The NULL-or-path log_path for ico_host_fatal and ico_diag_init: NULL when
   no log file was opened (--console, or logs/ not writable) */
static const char *log_file(void)
{
    return log_path[0] != '\0' ? log_path : NULL;
}

/* parse_args' output. A Windows GUI program started from Explorer has no
   console (ico_host_attach_console returns 0): the text is collected and
   shown in a message box instead of going nowhere. */
static int have_console = 1;

static char args_text[2048];

static void say(FILE *out, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void say(FILE *out, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (have_console) {
        vfprintf(out, fmt, ap);
    } else {
        size_t n = strlen(args_text);

        vsnprintf(args_text + n, sizeof(args_text) - n, fmt, ap);
    }
    va_end(ap);
}

static void usage(FILE *out, const char *prog)
{
    say(out,
        "usage: %s [options]   (none needed: see ico-pc.ini)\n"
        "  --iso PATH         the SCES-50760 disc image\n"
        "  --pad-script FILE  plug a scripted DualShock into controller port 1;\n"
        "                     lines: <tick> <buttons-hex> [lx ly rx ry]\n"
        "  --trace FILE|none  the per-Main-tick trace (logs/trace-<time>.txt by\n"
        "                     default in the headless build; FILE turns it on)\n"
        "  --ticks N          exit after N Main ticks\n"
        "  --no-verify        skip the disc image's SHA-1 check\n"
        "  --console          log to the console, not logs/ico-pc.log\n"
        "  --help             this text\n",
        prog);
}

static int parse_count(const char *s, unsigned long *out)
{
    char *end;

    if (s[0] < '0' || s[0] > '9') {
        return -1;
    }
    errno = 0;
    *out = strtoul(s, &end, 10);
    return errno != 0 || *end != '\0' || *out > 0xFFFFFFFFul ? -1 : 0;
}

static const char *exit_reason = "exit() from the game or the C library";

/* The end-of-run steps: each start-up step registers its own end (the pad
   recording, the summary, the window, the texture and model packs, and
   through ico_host_at_shutdown the achievements' flush and the audio), and
   they run last registered first.
   Desktop: atexit, so an exit() from the game runs them too.
   Android: SDL_main's return finishes the activity while the process may
   live on, so ico_host_main runs them itself (ico_host_shutdown) when it
   returns. */
#define SHUTDOWN_MAX 12
static void (*shutdown_fn[SHUTDOWN_MAX])(void);
static int shutdown_n;

static void at_shutdown(void (*fn)(void))
{
#ifdef __ANDROID__
    if (shutdown_n < SHUTDOWN_MAX) {
        shutdown_fn[shutdown_n++] = fn;
    } else {
        ico_diag_log("ico_pc: no room for another end-of-run step (SHUTDOWN_MAX %d)", SHUTDOWN_MAX);
    }
#else
    atexit(fn);
#endif
}

void ico_host_at_shutdown(void (*fn)(void))
{
    at_shutdown(fn);
}

void ico_host_shutdown(void)
{
    while (shutdown_n > 0) {
        shutdown_fn[--shutdown_n]();
    }
}

/* Every normal end goes through here (at_shutdown). */
static void summary(void)
{
    ico_trace_close();
    fflush(stdout);
    fflush(stderr);
    ico_diag_log("ico_pc: exit: %s", exit_reason);
    ico_diag_log("ico_pc: %u Main ticks, %u vsyncs, stage_no %d", ico_host_main_ticks(),
                 ico_host_vsync_count(), ico_host_stage_no());
}

/* The run before's log as logs/ico-pc-previous.log, so the log of
   a run that ended by itself is still there when the player starts the
   game again before sending it (on a phone the game just went away) */
static void keep_previous_log(const char *logs_dir, const char *log_path)
{
    char prev[ICO_PATH_MAX];

    if (ico_path_kind(log_path, NULL, NULL) == 0 &&
        ico_path_join(prev, sizeof(prev), logs_dir, "ico-pc-previous.log") == 0) {
        ico_rename_replace(log_path, prev);
    }
}

/* A crash or the watchdog (diag_host.h): the same summary without stdio,
   whose locks the stopped thread may hold. */
static void fatal_summary(const char *reason)
{
    ico_diag_log("ico_pc: %u Main ticks, %u vsyncs, stage_no %d (%s)", ico_host_main_ticks(),
                 ico_host_vsync_count(), ico_host_stage_no(), reason);
}

/* "--name VALUE" or "--name=VALUE": 1 and *value when argv[*i] is the
   option (advancing *i past a separate value), 0 when it is not, -1 when
   the value is missing. */
static int option(int argc, char **argv, int *i, const char *name, const char **value)
{
    const char *a = argv[*i];
    size_t n = strlen(name);

    if (strncmp(a, name, n) != 0) {
        return 0;
    }
    if (a[n] == '=') {
        *value = a + n + 1;
        return 1;
    }
    if (a[n] != '\0') {
        return 0;
    }
    if (*i + 1 >= argc) {
        return -1;
    }
    *value = argv[++*i];
    return 1;
}

/* 0, 1 to exit 0 (--help), 2 on a usage error. */
static int parse_args(int argc, char **argv, Args *a)
{
    /* Android passes one argument, SDL's name for the program */
    const char *prog = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "ico_pc";
    int i;

    memset(a, 0, sizeof(*a));
    for (i = 1; i < argc; i++) {
        const char *v = NULL;
        const char *name = argv[i];
        unsigned long n;
        int r = 1;

        if (strcmp(name, "--help") == 0 || strcmp(name, "-h") == 0) {
            usage(stdout, prog);
            return 1;
        } else if (strcmp(name, "--no-verify") == 0) {
            a->no_verify = 1;
        } else if (strcmp(name, "--console") == 0) {
            a->console = 1;
        } else if ((r = option(argc, argv, &i, "--iso", &v)) != 0) {
            a->iso = v;
        } else if ((r = option(argc, argv, &i, "--pad-script", &v)) != 0) {
            a->pad_script = v;
        } else if ((r = option(argc, argv, &i, "--trace", &v)) != 0) {
            a->trace = v;
        } else if ((r = option(argc, argv, &i, "--ticks", &v)) != 0) {
            if (r > 0 && parse_count(v, &n) != 0) {
                say(stderr, "%s: --ticks wants a count, not '%s'\n", prog, v);
                usage(stderr, prog);
                return 2;
            }
            a->ticks = v;
        } else {
            say(stderr, "%s: unknown option '%s'\n", prog, name);
            usage(stderr, prog);
            return 2;
        }
        if (r < 0) {
            say(stderr, "%s: %s needs a value\n", prog, name);
            usage(stderr, prog);
            return 2;
        }
    }
    return 0;
}

static void timestamp(char *out, size_t size, const char *fmt)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    if (tm == NULL || strftime(out, size, fmt, tm) == 0) {
        snprintf(out, size, "unknown");
    }
}

/* --- the pad recording (port/input/input_record.h) ---------------------- */

/* The config values the simulation depends on, written into the recording's
   header and compared when a recording is replayed as the pad script. */
static const char *const record_keys[] = {
    "video.video_mode",    "game.language",           "gameplay.mirror", "gameplay.stick_fix",
    "gameplay.yorda_safe", "gameplay.developer_mode", "dev.start_stage",
};

#define RECORD_MAGIC "# ico-pc input recording"

static const char *fixed_clock_now(void)
{
    const char *v = getenv("ICO_FIXED_CLOCK");

    return v != NULL && v[0] != '\0' ? v : "1";
}

static void record_header(char *out, size_t size, const char *stamp)
{
    size_t n = 0;
    size_t i;

#define PUT(...)                                                                                   \
    do {                                                                                           \
        int w_ = snprintf(out + n, size - n, __VA_ARGS__);                                         \
        if (w_ > 0) {                                                                              \
            n += (size_t)w_ < size - n ? (size_t)w_ : size - n - 1;                                \
        }                                                                                          \
    } while (0)
    PUT("%s: a pad script (port/input/pad_script.h); replay it with pad_script=<this file>\n",
        RECORD_MAGIC);
    PUT("# build %s\n", ICO_BUILD_COMMIT);
    PUT("# started %s\n", stamp);
    for (i = 0; i < sizeof(record_keys) / sizeof(record_keys[0]); i++) {
        PUT("# config %s = %s\n", record_keys[i], ico_config_get_string(record_keys[i], "(unset)"));
    }
    PUT("# effective fixed_clock = %s\n", fixed_clock_now());
    /* gameplay.stick_fix's default differs by platform (on under Android):
       the value the run used, for a phone's recording played on a computer */
    PUT("# effective stick_fix = %d\n", ico_opt_stick_fix());
    PUT("# the game's memory card folder at the start also decides the run (a Continue loads "
        "from it)\n");
    PUT("# <tick> <buttons-hex> lx ly rx ry: one line for each Main tick whose read differs "
        "from the one before\n");
#undef PUT
}

/* ini input_record= / [dev] input_record: off ("0", "false", "none"), on
   ("1", "true": logs/input-<time>.txt) or a path (the exe's folder's when
   relative). Default: on in the window build, off headless. */
static void record_open(const IcoIni *ini, const char *exe_dir, const char *logs_dir)
{
    const char *v = ico_ini_get(ini, "input_record");
    char path[ICO_PATH_MAX];
    char stamp[32];
    char header[2048];
#ifdef ICO_HEADLESS
    int on = 0;
#else
    int on = 1;
#endif

    if (v != NULL && v[0] != '\0') {
        on = !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "none") == 0);
    }
    if (!on) {
        return;
    }
    if (v != NULL && v[0] != '\0' && strcmp(v, "1") != 0 && strcmp(v, "true") != 0) {
        if (ico_path_join(path, sizeof(path), exe_dir, v) != 0) {
            fprintf(stderr, "ico_pc: input_record: the path is too long; no recording\n");
            return;
        }
    } else {
        char name[64];

        timestamp(stamp, sizeof(stamp), "%Y%m%d-%H%M%S");
        snprintf(name, sizeof(name), "input-%s.txt", stamp);
        if (ico_path_join(path, sizeof(path), logs_dir, name) != 0) {
            fprintf(stderr, "ico_pc: input_record: the logs folder's path is too long\n");
            return;
        }
    }
    timestamp(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S");
    record_header(header, sizeof(header), stamp);
    if (ico_input_record_open(path, header) == 0) {
        fprintf(stderr, "ico_pc: pad recording %s\n", path);
        at_shutdown(ico_input_record_close);
    }
}

/* A pad script that is a recording: its header against this run's
   settings, each difference logged (the replay then may not follow the
   recorded session). */
static void record_check(const char *path)
{
    FILE *f = ico_fopen(path, "rb");
    char line[512];
    int first = 1;
    int recording = 0;
    int diffs = 0;

    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL && line[0] == '#') {
        char key[128], value[256], fixbuf[8];
        const char *now = NULL;

        line[strcspn(line, "\r\n")] = '\0';
        if (first) {
            first = 0;
            if (strncmp(line, RECORD_MAGIC, strlen(RECORD_MAGIC)) != 0) {
                break;
            }
            recording = 1;
            continue;
        }
        if (sscanf(line, "# config %127s = %255[^\n]", key, value) == 2) {
            now = ico_config_get_string(key, "(unset)");
        } else if (sscanf(line, "# effective %127s = %255[^\n]", key, value) == 2 &&
                   strcmp(key, "fixed_clock") == 0) {
            now = fixed_clock_now();
        } else if (sscanf(line, "# effective %127s = %255[^\n]", key, value) == 2 &&
                   strcmp(key, "stick_fix") == 0) {
            snprintf(fixbuf, sizeof(fixbuf), "%d", ico_opt_stick_fix());
            now = fixbuf;
        } else if (sscanf(line, "# build %255s", value) == 1) {
            strcpy(key, "build");
            now = ICO_BUILD_COMMIT;
        }
        if (now != NULL && strcmp(now, value) != 0) {
            fprintf(stderr, "ico_pc: pad script recorded with %s = %s; this run has %s\n", key,
                    value, now);
            diffs++;
        }
    }
    fclose(f);
    if (recording && diffs == 0) {
        fprintf(stderr, "ico_pc: pad script is a recording made with this build and settings\n");
    }
}

#if !defined(ICO_HEADLESS) && !defined(_WIN32)

/* Linux window build: SDL3's file dialog (xdg-desktop-portal, else zenity or
   kdialog), since host_config.c's native dialog is Windows only. Returns 0
   with the path in out, -1 when cancelled or unavailable. Android: the
   system's file picker in front of the game's window, which is already
   open (SDL's video stays as it is); out is a content:// address
   (iso_import.h copies it). */
typedef struct PickState {
    char path[ICO_PATH_MAX];
    SDL_AtomicInt done; /* the answer may come on another thread (Android's UI thread) */
    int ok;
} PickState;

/* static, not on the waiting function's stack: the answer can come after
   that function gave up (Back, the window closed). Each dialog has its
   own number (user); an answer for one given up on is ignored. */
static PickState s_pick;
static SDL_AtomicInt s_pick_gen;

static void SDLCALL pick_done(void *user, const char *const *files, int filter)
{
    (void)filter;
    if ((int)(intptr_t)user != SDL_GetAtomicInt(&s_pick_gen)) {
        return;
    }
    if (files != NULL && files[0] != NULL) {
        snprintf(s_pick.path, sizeof(s_pick.path), "%s", files[0]);
        s_pick.ok = 1;
    }
    SDL_SetAtomicInt(&s_pick.done, 1);
}

/* a new dialog's number, for SDL_ShowOpenFileDialog's user */
static void *pick_begin(void)
{
    const int gen = SDL_AddAtomicInt(&s_pick_gen, 1) + 1;

    memset(s_pick.path, 0, sizeof(s_pick.path));
    s_pick.ok = 0;
    SDL_SetAtomicInt(&s_pick.done, 0);
    return (void *)(intptr_t)gen;
}

/* the dialog given up on: a later answer is ignored */
static void pick_abandon(void)
{
    SDL_AddAtomicInt(&s_pick_gen, 1);
}

#ifdef __ANDROID__

/* no filters: Android's picker matches MIME types, and a disc image has
   none it knows (the extractor checks the file) */
static int pick_iso_sdl(char *out, size_t size)
{
    SDL_ShowOpenFileDialog(pick_done, pick_begin(), NULL, NULL, 0, NULL, false);
    while (SDL_GetAtomicInt(&s_pick.done) == 0) {
        /* the window keeps drawing behind the picker; Back while it shows
           (the picker did not come up) gives up */
        if (ico_window_progress("Setting up ICO (first start only)",
                                "Choose your ICO disc image (.iso or .chd)", -1)) {
            pick_abandon();
            fprintf(stderr, "ico_pc: stopped while waiting for the file picker\n");
            return -1;
        }
        SDL_Delay(20);
    }
    if (!s_pick.ok) {
        fprintf(stderr, "ico_pc: no file chosen in the picker (%s)\n", SDL_GetError());
        return -1;
    }
    snprintf(out, size, "%s", s_pick.path);
    return 0;
}

#else

static int pick_iso_sdl(char *out, size_t size)
{
    static const SDL_DialogFileFilter filters[] = {
        {"ICO disc image (*.iso, *.chd)", "iso;chd"},
        {"All files", "*"},
    };
    SDL_Event ev;
    int quit = 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ico_pc: no file dialog: %s\n", SDL_GetError());
        return -1;
    }
    SDL_ShowOpenFileDialog(pick_done, pick_begin(), NULL, filters, 2, NULL, false);
    while (!quit && SDL_GetAtomicInt(&s_pick.done) == 0) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                quit = 1;
            }
        }
        SDL_Delay(20);
    }
    if (quit) {
        pick_abandon();
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (quit || !s_pick.ok) {
        return -1;
    }
    snprintf(out, size, "%s", s_pick.path);
    return 0;
}

#endif /* __ANDROID__ */

#endif

#ifdef ICO_ANDROID_UI
static int find_iso_android(const IcoIni *ini, const char *exe_dir, char *iso, int *picked);
#endif

/* The disc image's names beside the program (Android: in the app's files
   folder), the plain image first: of two copies, the one read without
   decoding. */
static const char *const image_names[2] = {"Ico_PAL.iso", "Ico_PAL.chd"};

/* iso= into ico-pc.ini when the image was picked (the dialog, the Android
   picker), so the next start finds it */
static void save_picked_iso(const char *ini_path, const char *iso)
{
    if (ico_ini_store(ini_path, "iso", iso) == 0) {
        fprintf(stderr, "ico_pc: saved iso=%s in %s\n", iso, ini_path);
    } else {
        fprintf(stderr, "ico_pc: cannot save the image path in %s\n", ini_path);
    }
}

/* Finds the disc image; fatal when there is none. *picked is set when it
   came from the dialog (to be saved once verified). 0, or 1 when the player
   stopped the Android first start's copy (the run ends without a box). */
static int find_iso(const Args *a, const IcoIni *ini, const char *exe_dir, char *iso, int *picked)
{
    const char *v;
    int i;

    *picked = 0;
    if (a->iso != NULL) {
        snprintf(iso, ICO_PATH_MAX, "%s", a->iso);
        fprintf(stderr, "ico_pc: disc image from --iso: %s\n", iso);
        return 0;
    }
#ifdef ICO_ANDROID_UI
    (void)v;
    (void)i;
    return find_iso_android(ini, exe_dir, iso, picked);
#else
    for (i = 0; i < 2; i++) {
        ico_path_join(iso, ICO_PATH_MAX, exe_dir, image_names[i]);
        if (ico_file_exists(iso)) {
            fprintf(stderr, "ico_pc: disc image beside the executable: %s\n", iso);
            return 0;
        }
    }
    v = ico_ini_get(ini, "iso");
    if (v != NULL && v[0] != '\0') {
        if (ico_path_join(iso, ICO_PATH_MAX, exe_dir, v) != 0) {
            fprintf(stderr, "ico_pc: iso= in ico-pc.ini: the path is too long\n");
        }
        fprintf(stderr, "ico_pc: disc image from ico-pc.ini: %s\n", iso);
        return 0;
    }
    v = getenv("ICO_ISO");
    if (v != NULL && v[0] != '\0') {
        snprintf(iso, ICO_PATH_MAX, "%s", v);
        fprintf(stderr, "ico_pc: disc image from ICO_ISO: %s\n", iso);
        return 0;
    }
    for (i = 0; i < 2; i++) {
        snprintf(iso, ICO_PATH_MAX, "baserom/%s", image_names[i]);
        if (ico_file_exists(iso)) {
            fprintf(stderr, "ico_pc: disc image from the working folder: %s\n", iso);
            return 0;
        }
    }
#if !defined(ICO_HEADLESS) && !defined(_WIN32)
    if (pick_iso_sdl(iso, ICO_PATH_MAX) == 0) {
#else
    if (ico_host_pick_iso(iso, ICO_PATH_MAX) == 0) {
#endif
        fprintf(stderr, "ico_pc: disc image chosen in the dialog: %s\n", iso);
        *picked = 1;
        return 0;
    }
    ico_host_fatal(log_file(), "No ICO disc image was found or chosen. Put Ico_PAL.iso or "
                               "Ico_PAL.chd next to the program, or set iso=<path> in ico-pc.ini.");
    return 0;
#endif
}

static void verify_iso(const char *iso)
{
    char hex[41];
    char why[256];
    uint64_t bytes = 0;
    clock_t start = clock();
    double secs;

    /* the ISO's bytes, also for a .chd (extract.h) */
    fprintf(stderr, "ico_pc: checking the disc image's SHA-1...\n");
    if (ico_extract_image_sha1(iso, hex, &bytes, why, sizeof(why)) != 0) {
        fprintf(stderr, "ico_pc: %s\n", why);
        ico_host_fatal(log_file(),
                       "Cannot read the disc image %s.\n"
                       "Check that the file is complete and is a .iso or .chd copy of the PAL "
                       "disc. The log says why.",
                       iso);
    }
    secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "ico_pc: SHA-1 %s, %llu bytes, %.1f s\n", hex, (unsigned long long)bytes, secs);
    if (strcmp(hex, ICO_DISC_ISO_SHA1) != 0) {
        /* the checksums are for the log; the box says what to use instead */
        fprintf(stderr, "ico_pc: the image's SHA-1 is %s, expected %s\n", hex, ICO_DISC_ISO_SHA1);
        ico_host_fatal(log_file(),
                       "%s is not the disc image this port needs.\n"
                       "Use a complete, unmodified image of the PAL release of ICO "
                       "(SCES-50760). Other regions and editions do not work.",
                       iso);
    }
    fprintf(stderr, "ico_pc: disc image verified (SCES-50760)\n");
}

/* --- the game data: the archive, or the image in dev mode ----------------- */

#ifndef ICO_HEADLESS
/* ico_dir_walk's callback: deletes the game-lettering files an earlier
   version left in the per-user folder (gamefont-*.bin, and a .tmp of one
   an interrupted write left); *user counts them */
static int remove_gamefont(const char *path, const char *name, void *user)
{
    const size_t n = strlen(name);

    if (strncmp(name, "gamefont-", 9) == 0 &&
        ((n > 4 && strcmp(name + n - 4, ".bin") == 0) ||
         (n > 8 && strcmp(name + n - 8, ".bin.tmp") == 0)) &&
        ico_remove(path) == 0) {
        (*(int *)user)++;
    }
    return 0;
}
#endif

/* use_iso= / [dev] use_iso: 1 reads the image directly. Default: the
   headless build reads the image (trace runs and tests stay as they were),
   the window build the archive. */
static int use_iso_mode(const IcoIni *ini)
{
    const char *v = ico_ini_get(ini, "use_iso");

    if (v != NULL && v[0] != '\0') {
        return strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
    }
#ifdef ICO_HEADLESS
    return 1;
#else
    return 0;
#endif
}

#ifdef ICO_ANDROID_UI

/* Android: one window only (SDL), the game's, already open: the progress is
   drawn in it (window_host.h ico_window_progress), Back stops */
static void progress_ui_open(void) {}

/* 1 when the player asked to stop.  pct < 0: a step with no byte count,
   shown as its words alone with no bar. */
static int progress_ui_update(const char *phase, int pct)
{
    const char *words = strcmp(phase, "copy") == 0     ? "Copying the disc image into the app"
                        : strcmp(phase, "save") == 0   ? "Saving the copy"
                        : strcmp(phase, "open") == 0   ? "Opening the disc image"
                        : strcmp(phase, "hash") == 0   ? "Checking the disc image"
                        : strcmp(phase, "finish") == 0 ? "Finishing the game's data"
                                                       : "Preparing the game's data";

    return ico_window_progress("Setting up ICO (first start only)", words, pct);
}

static void progress_ui_close(void) {}

#elif !defined(ICO_HEADLESS)

/* The window build's progress window during the first-run extraction: SDL's
   2D renderer, a title and a bar, closed before the game's window opens. */
static SDL_Window *progressWin;
static SDL_Renderer *progressRen;
static int progressClosed;

static void progress_ui_open(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ico_pc: no progress window: %s\n", SDL_GetError());
        return;
    }
    progressWin = SDL_CreateWindow("ICO: preparing the game data", 480, 72, 0);
    if (progressWin == NULL) {
        fprintf(stderr, "ico_pc: no progress window: %s\n", SDL_GetError());
        return;
    }
    progressRen = SDL_CreateRenderer(progressWin, NULL);
}

/* 1 when the user closed the window (cancel). */
static int progress_ui_update(const char *phase, int pct)
{
    SDL_Event ev;
    char title[128];

    if (progressWin == NULL) {
        return 0;
    }
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            progressClosed = 1;
        }
    }
    if (pct < 0) {
        /* a step with no byte count: its words, the bar where it was */
        snprintf(title, sizeof(title), "ICO: preparing the game data (first run): %s",
                 strcmp(phase, "open") == 0 ? "opening the disc image" : "finishing");
        pct = 0;
    } else {
        snprintf(title, sizeof(title), "ICO: preparing the game data (first run): %s %d%%",
                 strcmp(phase, "hash") == 0 ? "checking the disc image" : "copying", pct);
    }
    SDL_SetWindowTitle(progressWin, title);
    if (progressRen != NULL) {
        SDL_FRect frame = {16.0f, 24.0f, 448.0f, 24.0f};
        SDL_FRect bar = {18.0f, 26.0f, 444.0f * (float)pct / 100.0f, 20.0f};

        SDL_SetRenderDrawColor(progressRen, 24, 24, 24, 255);
        SDL_RenderClear(progressRen);
        SDL_SetRenderDrawColor(progressRen, 160, 160, 160, 255);
        SDL_RenderRect(progressRen, &frame);
        SDL_SetRenderDrawColor(progressRen, 200, 180, 120, 255);
        SDL_RenderFillRect(progressRen, &bar);
        SDL_RenderPresent(progressRen);
    }
    return progressClosed;
}

static void progress_ui_close(void)
{
    if (progressRen != NULL) {
        SDL_DestroyRenderer(progressRen);
        progressRen = NULL;
    }
    if (progressWin != NULL) {
        SDL_DestroyWindow(progressWin);
        progressWin = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

#endif

typedef struct Progress {
    int logged; /* the last tenth logged, per phase */
    const char *phase;
    double ui_at;
} Progress;

static int extract_progress(void *ctx, const char *phase, uint64_t done, uint64_t total)
{
    Progress *p = ctx;
    /* -1: a step with no byte count (open, save, finish): no percentage, no bar */
    int pct = total > 0 ? (int)(done * 100u / total) : -1;

    if (p->phase == NULL || strcmp(p->phase, phase) != 0) {
        p->phase = phase;
        p->logged = -1;
        fprintf(stderr, "ico_pc: first run: %s\n",
                strcmp(phase, "hash") == 0     ? "checking the disc image's SHA-1"
                : strcmp(phase, "copy") == 0   ? "copying the chosen disc image into the app"
                : strcmp(phase, "save") == 0   ? "saving the copy to the storage"
                : strcmp(phase, "open") == 0   ? "opening the disc image"
                : strcmp(phase, "finish") == 0 ? "writing the archive's directory"
                                               : "copying the game's files into the archive");
    }
    if (pct >= 0 && pct / 10 != p->logged) {
        p->logged = pct / 10;
        fprintf(stderr, "ico_pc: first run: %3d%% (%llu of %llu MB)\n", pct,
                (unsigned long long)(done >> 20), (unsigned long long)(total >> 20));
    }
#ifndef ICO_HEADLESS
    {
        double now = ico_diag_uptime();

        if (now - p->ui_at >= 0.05 || done == total) {
            p->ui_at = now;
            return progress_ui_update(phase, pct);
        }
    }
#endif
    return 0;
}

#ifdef ICO_ANDROID_UI

/* Android's find_iso: Ico_PAL.iso or .chd in the app's files folder (a
   copy kept with keep_image=1, or one put there with a cable or the Files
   app), then ini iso= when that file is there, else the system's picker
   and a copy of the chosen file into the files folder (iso_import.h). */
static int find_iso_android(const IcoIni *ini, const char *exe_dir, char *iso, int *picked)
{
    char uri[ICO_PATH_MAX];
    char why[1024];
    const char *v;
    Progress prog;
    int i, r;

    for (i = 0; i < 2; i++) {
        if (ico_path_join(iso, ICO_PATH_MAX, exe_dir, image_names[i]) == 0 &&
            ico_file_exists(iso)) {
            fprintf(stderr, "ico_pc: disc image in the app's folder: %s\n", iso);
            return 0;
        }
    }
    v = ico_ini_get(ini, "iso");
    if (v != NULL && v[0] != '\0') {
        if (ico_path_join(iso, ICO_PATH_MAX, exe_dir, v) == 0 && ico_file_exists(iso)) {
            fprintf(stderr, "ico_pc: disc image from ico-pc.ini: %s\n", iso);
            return 0;
        }
        fprintf(stderr, "ico_pc: iso=%s in ico-pc.ini is not there; asking for the image\n", v);
    }
    if (pick_iso_sdl(uri, sizeof(uri)) != 0) {
        ico_host_fatal(log_file(),
                       "No disc image was chosen.\n"
                       "Start the game again and choose your ICO disc image (a .iso or .chd "
                       "copy of the PAL disc), or copy it as Ico_PAL.iso into %s with a USB "
                       "cable or the Files app.",
                       exe_dir);
    }
    fprintf(stderr, "ico_pc: disc image chosen in the file picker: %s\n", uri);
    memset(&prog, 0, sizeof(prog));
    r = ico_iso_import(uri, iso, ICO_PATH_MAX, extract_progress, &prog, why, sizeof(why));
    if (r == ICO_ISO_IMPORT_CANCELLED) {
        return 1;
    }
    if (r != ICO_ISO_IMPORT_OK) {
        ico_host_fatal(log_file(), "%s", why);
    }
    *picked = 1;
    return 0;
}

/* whether iso is the app's own Ico_PAL.iso or .chd in the files folder */
static int android_own_image(const char *exe_dir, const char *iso)
{
    char own[ICO_PATH_MAX];
    int i;

    for (i = 0; i < 2; i++) {
        if (ico_path_join(own, sizeof(own), exe_dir, image_names[i]) == 0 &&
            strcmp(own, iso) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The extractor refused the app's own image (unreadable, or not the PAL
   disc): it goes, so the next start asks for the image again instead of
   finding this one. */
static void android_drop_refused_image(const char *exe_dir, const char *iso)
{
    if (android_own_image(exe_dir, iso)) {
        fprintf(stderr, "ico_pc: deleting the refused image %s %s\n", iso,
                ico_remove(iso) == 0 ? "(the next start asks for one)" : "failed");
    }
}

static int android_keep_image(const IcoIni *ini)
{
    const char *v = ico_ini_get(ini, "keep_image");

    return v != NULL && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0);
}

/* Back during the extraction: the copy the picker made this run goes, as
   after an extraction (unless ini keep_image=1), so the next start asks
   for the image again instead of using it unasked; an image the player
   put in the files folder by hand stays. */
static void android_drop_cancelled_copy(const IcoIni *ini, const char *exe_dir, const char *iso,
                                        int picked)
{
    if (!picked || !android_own_image(exe_dir, iso)) {
        return;
    }
    if (android_keep_image(ini)) {
        fprintf(stderr, "ico_pc: keep_image=1: the copy of the disc image %s stays\n", iso);
        return;
    }
    fprintf(stderr, "ico_pc: deleting the copy of the disc image %s %s\n", iso,
            ico_remove(iso) == 0 ? "(the next start asks for one)" : "failed");
}

/* After the game's data is mounted: the app's own copy of the image
   (Ico_PAL.iso or .chd in the files folder) is deleted, which gives back
   its space, unless ini keep_image=1; iso= is saved only for an image that
   stays. */
static void android_image_done(const IcoIni *ini, const char *ini_path, const char *exe_dir,
                               const char *iso, int picked)
{
    const int keep = android_keep_image(ini);
    const int mine = android_own_image(exe_dir, iso);
    SDL_PathInfo info;

    if (mine && !keep) {
        const long long bytes = SDL_GetPathInfo(iso, &info) ? (long long)info.size : -1;

        if (ico_remove(iso) == 0) {
            fprintf(stderr,
                    "ico_pc: deleted the copy of the disc image %s: %.2f GB given back "
                    "(keep_image=1 in ico-pc.ini keeps it)\n",
                    iso, bytes > 0 ? (double)bytes / 1e9 : 0.0);
        } else {
            fprintf(stderr, "ico_pc: cannot delete the copy of the disc image %s\n", iso);
        }
        return;
    }
    if (mine) {
        fprintf(stderr, "ico_pc: keep_image=1: the copy of the disc image %s stays\n", iso);
    }
    if (picked) {
        save_picked_iso(ini_path, iso);
    }
}

#endif

/* Checks an archive (meta.json, this disc, the boot ELF's hash read back
   through it) and makes it the disc. 0, or -1 with the reason. */
static int mount_archive(const char *path, char *why, size_t whysize)
{
    static unsigned char chunk[1 << 16];
    IcoArchiveInfo info;
    IcoVfs *vfs;
    IcoVfsFile f;
    IcoSha1 s;
    unsigned char d[20];
    char hex[41];
    uint64_t off;
    int i;

    if (ico_archive_read_info(path, &info, why, whysize) != 0 ||
        !ico_archive_info_acceptable(&info, why, whysize)) {
        return -1;
    }
    vfs = ico_vfs_mount_archive(path);
    if (vfs == NULL) {
        snprintf(why, whysize, "it cannot be mounted");
        return -1;
    }
    if (ico_vfs_open(vfs, ICO_TABLES_BOOT_ELF, &f) != 0) {
        ico_vfs_unmount(vfs);
        snprintf(why, whysize, "it holds no %s", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    ico_sha1_init(&s);
    for (off = 0; off < f.entry.size;) {
        int64_t n = ico_vfs_read(&f, off, chunk, sizeof(chunk));

        if (n <= 0) {
            break;
        }
        ico_sha1_update(&s, chunk, (size_t)n);
        off += (uint64_t)n;
    }
    ico_sha1_final(&s, d);
    for (i = 0; i < 20; i++) {
        snprintf(hex + 2 * i, 3, "%02x", d[i]);
    }
    if (off != f.entry.size || strcmp(hex, ICO_DISC_ELF_SHA1) != 0) {
        ico_vfs_unmount(vfs);
        snprintf(why, whysize, "its %s reads back with SHA-1 %s", ICO_TABLES_BOOT_ELF, hex);
        return -1;
    }
    ico_vfs_set_disc(vfs);
    fprintf(stderr,
            "ico_pc: game data %s (%s, %u files, %llu bytes; from an image with SHA-1 %s, "
            "accepted by %s)\n",
            path, info.disc_id, (unsigned)info.stored, (unsigned long long)info.file_bytes,
            info.iso_sha1, info.accepted_by);
    return 0;
}

static void copy_path(char *out, size_t size, const char *s)
{
    size_t n = strlen(s);

    if (n >= size) {
        n = size - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

/* The archive mode: mount ico.o2r, extracting it on the first run. 0, or 1
   when the player stopped the Android first start (the run ends). */
static int mount_game_data(const Args *a, const IcoIni *ini, const char *exe_dir,
                           const char *ini_path, char *source, size_t source_size)
{
    char pref[ICO_PATH_MAX];
    char cand[2][ICO_PATH_MAX];
    char iso[ICO_PATH_MAX];
    char why[1024];
    IcoExtractResult res;
    Progress prog;
    int picked, i, r;

    ico_host_pref_dir(pref, sizeof(pref));
    /* cand[0] is also where the first run writes */
    if (ico_path_join(cand[0], sizeof(cand[0]), pref, ICO_ARCHIVE_NAME) != 0) {
        ico_host_fatal(log_file(), "The path of the folder %s is too long to hold the game's data.",
                       pref);
    }
    if (ico_path_join(cand[1], sizeof(cand[1]), exe_dir, ICO_ARCHIVE_NAME) != 0) {
        cand[1][0] = '\0'; /* read only: not a candidate */
    }
    for (i = 0; i < 2; i++) {
        if ((i == 1 && strcmp(cand[0], cand[1]) == 0) || !ico_file_exists(cand[i])) {
            continue;
        }
        if (mount_archive(cand[i], why, sizeof(why)) == 0) {
            copy_path(source, source_size, cand[i]);
            return 0;
        }
        fprintf(stderr, "ico_pc: %s is not usable: %s\n", cand[i], why);
    }

    /* the first run: the image, verified and extracted once */
    fprintf(stderr, "ico_pc: no usable %s in %s: the first run extracts it from the disc image\n",
            ICO_ARCHIVE_NAME, pref);
    if (find_iso(a, ini, exe_dir, iso, &picked) != 0) {
        return 1;
    }
    if (ico_make_dir(pref) != 0) {
        ico_host_fatal(log_file(),
                       "Cannot create the folder %s for the game's data.\n"
                       "Check that you are allowed to write there.",
                       pref);
    }
    memset(&prog, 0, sizeof(prog));
#ifndef ICO_HEADLESS
    progress_ui_open();
#endif
    r = ico_extract_archive(iso, cand[0], 0, extract_progress, &prog, &res, why, sizeof(why));
#ifndef ICO_HEADLESS
    progress_ui_close();
#endif
    if (r != 0 && res.cancelled) {
        fprintf(stderr, "ico_pc: first run: cancelled; nothing was written\n");
        fflush(stderr);
#ifdef ICO_ANDROID_UI
        android_drop_cancelled_copy(ini, exe_dir, iso, picked);
        /* ico_host_main's end-of-run steps, then SDL_main returns */
        return 1;
#else
        exit(0);
#endif
    }
    if (r != 0) {
        fprintf(stderr, "ico_pc: first run: %s\n", why);
#ifdef ICO_ANDROID_UI
        if (res.wrong_disc || res.unreadable) {
            android_drop_refused_image(exe_dir, iso);
        }
#endif
        /* the box says what to do about it, in verify_iso's words */
        if (res.wrong_disc) {
            ico_host_fatal(log_file(),
                           "%s is not the disc image this port needs.\n"
                           "Use a complete, unmodified image of the PAL release of ICO "
                           "(SCES-50760). Other regions and editions do not work.",
                           iso);
        }
        if (res.unreadable) {
            ico_host_fatal(log_file(),
                           "Cannot read the disc image %s.\n"
                           "Check that the file is complete and is a .iso or .chd copy of the PAL "
                           "disc. The log says why.",
                           iso);
        }
        ico_host_fatal(log_file(),
                       "Could not extract the game's data from %s into %s.\n"
                       "Check that there is about 1 GB of free space and that the disc image "
                       "is complete. The log says why.",
                       iso, cand[0]);
    }
    fprintf(stderr,
            "ico_pc: first run: disc image SHA-1 %s, %llu bytes; accepted by %s%s\n"
            "ico_pc: first run: %u files, %llu bytes, into %s (%llu bytes) in %.1f s "
            "(SHA-1 %.1f s, copy %.1f s)\n",
            res.iso_sha1, (unsigned long long)res.iso_size, res.rule,
            strcmp(res.rule, ICO_RULE_ISO_SHA1) == 0
                ? " (the image's SHA-1)"
                : " (SCES_507.60's SHA-1 and DATA.DF's manifest: a re-dump of the PAL disc)",
            (unsigned)res.files, (unsigned long long)res.bytes, cand[0],
            (unsigned long long)res.archive_bytes, res.hash_seconds + res.extract_seconds,
            res.hash_seconds, res.extract_seconds);
    if (!res.datadf_ok) {
        fprintf(stderr, "ico_pc: first run: note: DATA.DF did not match the manifest (%s)\n",
                res.datadf_why);
    }
#ifndef ICO_ANDROID_UI
    if (picked) {
        save_picked_iso(ini_path, iso);
    }
#endif
    if (mount_archive(cand[0], why, sizeof(why)) != 0) {
        fprintf(stderr, "ico_pc: %s cannot be mounted: %s\n", cand[0], why);
        ico_host_fatal(log_file(),
                       "The game's data just written to %s cannot be read back.\n"
                       "Delete that file and start the game again to extract it once more.",
                       cand[0]);
    }
#ifdef ICO_ANDROID_UI
    android_image_done(ini, ini_path, exe_dir, iso, picked);
#endif
    copy_path(source, source_size, cand[0]);
    return 0;
}

#ifndef ICO_HEADLESS

/* The window and the renderer before boot: gsb_InitGSSystem's first frame
   already records into rd.  512 x 512 is the PAL frame; gsb_Init resizes
   the scene targets if the game switches to 60 Hz.  On Android before the
   game data is mounted (ico_window_open needs only the config, rd and
   port/ui's own font). */
static void open_window(void)
{
    if (ico_window_open(512, 512) != 0) {
#ifdef ICO_ANDROID_UI
        ico_host_fatal(log_file(), "Could not start the game's graphics.\n"
                                   "ICO needs a device whose graphics support Vulkan 1.2 (most "
                                   "phones and tablets from 2022 on). Install the latest system "
                                   "update and try again; the log says why.");
#else
        ico_host_fatal(log_file(), "Could not open the game window.\n"
                                   "The game needs a graphics driver with Vulkan 1.2 or later. "
                                   "Update your graphics driver and try again; the log says "
                                   "why.");
#endif
    }
    at_shutdown(ico_window_close);
}

#endif

/* the data tables, from the disc's boot ELF, before anything reads one
   (port/data/tables.h) */
static void load_tables(const char *source)
{
    char why[512];

    if (ico_tables_load_vfs(ico_vfs_disc(), why, sizeof(why)) != 0) {
        fprintf(stderr, "ico_pc: the data tables: %s\n", why);
        ico_host_fatal(log_file(), "Cannot read the game's data from %s.\nThe log says why.",
                       source);
    }
    fprintf(stderr, "ico_pc: %u data table rows (%u records) loaded from %s\n",
            (unsigned)ico_tables_loaded_rows(), (unsigned)ico_tables_loaded_records(),
            ICO_TABLES_BOOT_ELF);
}

#ifndef ICO_HEADLESS
/* The menus do not use the game's lettering, so the files an earlier
   version cut from the disc's menu sheets on its first
   start (gamefont-<version>-<disc SHA-1>.bin in the per-user folder)
   are of no use: removed, once (nothing is left to find after that) */
static void remove_old_lettering(void)
{
    char pref[ICO_PATH_MAX];
    int removed = 0;

    if (ico_host_pref_dir(pref, sizeof(pref)) == 0) {
        ico_dir_walk(pref, 0, remove_gamefont, &removed);
    }
    if (removed > 0) {
        fprintf(stderr,
                "ico_pc: removed %d file(s) of the game's lettering from the user "
                "folder (the menus no longer use them)\n",
                removed);
    }
}
#endif

/* the pad: a script from --pad-script or [dev] pad_script */
static void setup_pad(const Args *a, const IcoIni *ini, const char *exe_dir)
{
    const char *v;
    char path[ICO_PATH_MAX];

    /* command-line paths are the working folder's, ini paths the
       executable's */
    v = ico_ini_get(ini, "pad_script");
    if (a->pad_script != NULL) {
        snprintf(path, sizeof(path), "%s", a->pad_script);
    } else if (v != NULL && v[0] != '\0') {
        if (ico_path_join(path, sizeof(path), exe_dir, v) != 0) {
            fprintf(stderr, "ico_pc: pad_script: the path is too long\n");
        }
    } else {
        path[0] = '\0';
#ifdef ICO_HEADLESS
        /* the headless build's default; a player's window build takes a
           script only when [dev] pad_script names one, so a stray file
           beside the exe never replaces the live controller */
        ico_path_join(path, sizeof(path), exe_dir, "pad-script.txt");
        if (!ico_file_exists(path)) {
            path[0] = '\0';
        }
#endif
    }
    if (path[0] != '\0') {
        if (ico_pad_script_load(path) != 0) {
            ico_host_fatal(log_file(), "Cannot use the pad script %s (details in the log).", path);
        }
        fprintf(stderr, "ico_pc: pad script %s, %d entries: a DualShock in port 1\n", path,
                ico_pad_script_count());
        record_check(path);
    } else {
        fprintf(stderr, "ico_pc: no pad script: no controller\n");
    }
}

/* the trace */
static void setup_trace(const Args *a, const IcoIni *ini, const char *exe_dir, const char *logs_dir)
{
    const char *v;
    char path[ICO_PATH_MAX];
    char stamp[32];

    /* on by default in the headless build; the window build writes one only
       when asked ([dev] trace = true or a path, or --trace), so a player's
       logs folder does not fill with a trace per run */
    v = a->trace != NULL ? a->trace : ico_ini_get(ini, "trace");
#ifdef ICO_HEADLESS
    const int trace_on =
        !(v != NULL && (strcmp(v, "0") == 0 || strcmp(v, "none") == 0 || strcmp(v, "false") == 0));
#else
    const int trace_on = v != NULL && v[0] != '\0' && strcmp(v, "0") != 0 &&
                         strcmp(v, "none") != 0 && strcmp(v, "false") != 0;
#endif
    if (!trace_on) {
        fprintf(stderr, "ico_pc: no trace\n");
    } else {
        if (a->trace != NULL) {
            snprintf(path, sizeof(path), "%s", a->trace);
        } else if (v != NULL && strcmp(v, "1") != 0 && strcmp(v, "true") != 0) {
            if (ico_path_join(path, sizeof(path), exe_dir, v) != 0) {
                path[0] = '\0';
            }
        } else {
            char name[64];

            timestamp(stamp, sizeof(stamp), "%Y%m%d-%H%M%S");
            snprintf(name, sizeof(name), "trace-%s.txt", stamp);
            if (ico_path_join(path, sizeof(path), logs_dir, name) != 0) {
                path[0] = '\0';
            }
        }
        if (path[0] != '\0' && ico_trace_open(path) == 0) {
            fprintf(stderr, "ico_pc: trace %s\n", path);
        } else {
            fprintf(stderr, "ico_pc: running without a trace\n");
        }
    }
}

/* the game's thread functions, by name in the crash report */
static void name_threads(void)
{
    ico_diag_name_func((void *)Main, "Main");
    ico_diag_name_func((void *)StageManager, "StageManager");
    ico_diag_name_func((void *)iosCdvdManager, "iosCdvdManager");
    ico_diag_name_func((void *)iosMcManager, "iosMcManager");
    ico_diag_name_func((void *)jimakuManager, "jimakuManager");
    ico_diag_name_func((void *)sndManager, "sndManager");
    ico_diag_name_func((void *)InitIcoMisc, "InitIcoMisc");
}

#ifndef ICO_HEADLESS
/* A PCSX2 texture pack in the user folder or beside the
   program, indexed now (the game data is mounted, the device knows its
   formats); its loader thread stops before the window closes */
static void open_texture_pack(const char *exe_dir)
{
    IcoVideoOptions vo;
    TexpackConfig tc;
    char user[ICO_PATH_MAX];

    ico_video_get(&vo);
    ico_host_pref_dir(user, sizeof(user));
    memset(&tc, 0, sizeof(tc));
    tc.userDir = user;
    tc.programDir = exe_dir;
    tc.serial = ICO_DISC_ID;
    tc.budgetMb = (uint32_t)vo.texturePackBudgetMb;
    tc.cacheMb = (uint32_t)vo.texturePackCacheMb;
    /* read ahead only while the pack is in use */
    tc.precache = vo.texturePackPrecache && vo.texturePack;
    tc.bcSupported = rhi_limits() != NULL && rhi_limits()->bcTextures;
    tc.maxTextureSize = rhi_limits() != NULL ? rhi_limits()->maxTextureSize : 0;
    tc.developer = ico_opt_developer_mode();
    texpack_init(&tc);
    at_shutdown(texpack_shutdown);
}

/* A model pack from the same folders, read and converted
   now (before the game loads a model: the meshes made at load look
   their parts up); the dump is a Developer mode row */
static void open_model_pack(const char *exe_dir)
{
    ModelpackConfig mc;
    char user[ICO_PATH_MAX];
    const int developer = ico_opt_developer_mode();

    ico_host_pref_dir(user, sizeof(user));
    memset(&mc, 0, sizeof(mc));
    mc.userDir = user;
    mc.programDir = exe_dir;
    mc.serial = ICO_DISC_ID;
    mc.developer = developer;
    mc.dumpEnabled = developer && ico_video_dump_models();
    modelpack_init(&mc);
    modelpack_set_enabled(ico_video_model_pack() != 0);
    fprintf(stderr, "models: %d replacements, model pack %s, dump models %s\n", modelpack_count(),
            ico_video_model_pack() ? "on" : "off", mc.dumpEnabled ? "on" : "off");
    ui_settings_set_model_pack_count(modelpack_count);
    at_shutdown(modelpack_shutdown);
}
#endif

/* the watchdog, from the limit in seconds (0 off) */
static void start_watchdog(unsigned long watchdog)
{
    /* An effects program (ReShade) compiles its shaders on the first
       frames; the first limit is doubled for it (rhi_injector_name, port/rhi/
       rhi.h, is only in the window build). */
    const char *injector = NULL;
#ifndef ICO_HEADLESS
    injector = rhi_injector_name();
#endif
    if (injector != NULL && watchdog != 0) {
        ico_diag_log("ico_pc: an effects program (%s) is loaded, so the start-up time limit "
                     "is doubled to %lu seconds (it prepares its effects on the first frames)",
                     injector, watchdog * 2);
        ico_diag_start((unsigned int)(watchdog * 2), (unsigned int)(watchdog * 2));
    } else {
        ico_diag_start((unsigned int)watchdog, (unsigned int)(watchdog * 2));
    }
}

static int host_main(int argc, char **argv)
{
    Args a;
    IcoIni ini;
    char exe_dir[ICO_PATH_MAX];
    char logs_dir[ICO_PATH_MAX];
    char ini_path[ICO_PATH_MAX];
    char source[ICO_PATH_MAX]; /* the disc image (use_iso) or the archive */
    char stamp[32];
    const char *v;
    unsigned long ticks = 0;
    unsigned long watchdog;
    int have_ticks = 0;
    int picked;
    int warned = 0;
    int r;

    have_console = ico_host_attach_console();
    r = parse_args(argc, argv, &a);
    if (r != 0) {
        if (args_text[0] != '\0') {
            ico_host_message_box(args_text, r != 1);
        }
        return r == 1 ? 0 : 2;
    }

    /* the log first, so everything after it is recorded */
    ico_host_exe_dir(exe_dir, sizeof(exe_dir));
    if (ico_path_join(logs_dir, sizeof(logs_dir), exe_dir, "logs") != 0 ||
        ico_path_join(log_path, sizeof(log_path), logs_dir, "ico-pc.log") != 0) {
        logs_dir[0] = '\0';
        log_path[0] = '\0';
    }
    if (a.console && !have_console) {
        ico_host_message_box("--console: there is no console to log to (start ico_pc from a "
                             "command prompt); the log goes to logs\\ico-pc.log instead",
                             0);
        a.console = 0;
    }
    if (a.console) {
        log_path[0] = '\0';
    } else if (log_path[0] == '\0' || ico_make_dir(logs_dir) != 0 ||
               (keep_previous_log(logs_dir, log_path), ico_host_redirect_output(log_path) != 0)) {
        fprintf(stderr, "ico_pc: cannot write %s; logging to the console\n", log_path);
        log_path[0] = '\0';
    }
    /* crash handlers and the unbuffered diagnostics writer, first thing;
       no log file when none was opened */
    ico_diag_init(log_file());
#ifdef __ANDROID__
    /* A crash or the watchdog ends the run with a box and the C
       library's last words in the log, not by the game just going */
    ico_diag_set_fatal_ui(ico_android_fatal_box, ico_android_log_mirror_try_flush);
#endif
    timestamp(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S");
    fprintf(stderr, "ico_pc: started %s in %s\n", stamp, exe_dir);
#ifdef __ANDROID__
    ico_android_log_version();
#endif
    for (r = 1; r < argc; r++) {
        fprintf(stderr, "ico_pc: argument %s\n", argv[r]);
    }

    if (ico_path_join(ini_path, sizeof(ini_path), exe_dir, "ico-pc.ini") != 0) {
        ico_host_fatal(log_file(),
                       "The path of the folder %s is too long. Move the game to a folder with "
                       "a shorter path.",
                       exe_dir);
    }
    if (ico_ini_load(&ini, ini_path) == 0) {
        fprintf(stderr, "ico_pc: settings from %s (%d keys)\n", ini_path, ini.count);
    } else {
        fprintf(stderr, "ico_pc: no %s; defaults\n", ini_path);
    }

    {
        char user[ICO_PATH_MAX];

        ico_host_pref_dir(user, sizeof(user));
        fprintf(stderr, "ico_pc: user folder %s (%s)\n", user,
                ico_host_pref_is_portable() ? "portable" : "user profile");
    }

    /* config.toml on the first run, so there is a file to edit; the window
       build by default, the headless one (whose per-user folder is the
       build's) only with write_config=1 */
    {
        const char *wc = ico_ini_get(&ini, "write_config");
#ifdef ICO_HEADLESS
        int write_config = 0;
#else
        int write_config = 1;
#endif

        if (wc != NULL && wc[0] != '\0') {
            write_config = strcmp(wc, "0") != 0 && strcmp(wc, "false") != 0;
        }
        if (write_config) {
            ico_config_write_first_run();
        }
    }

#ifdef ICO_ANDROID_UI
    /* Android: the window first, the first start's picker, copy and
       progress are shown in it (SDL has one window there) */
    open_window();
#endif
    /* the disc goes in before boot, so a missing image fails here rather
       than leaving the game at file_Init's disc wait */
    if (use_iso_mode(&ini)) {
        fprintf(stderr, "ico_pc: use_iso: the disc image is read directly\n");
        if (find_iso(&a, &ini, exe_dir, source, &picked) != 0) {
            exit_reason = "the first start was stopped";
            fprintf(stderr, "ico_pc: the first start was stopped\n");
            return 0;
        }
        v = ico_ini_get(&ini, "verify");
        if (a.no_verify || (v != NULL && strcmp(v, "0") == 0)) {
            fprintf(stderr, "ico_pc: disc image SHA-1 check skipped\n");
        } else {
            verify_iso(source);
        }
        if (picked) {
            save_picked_iso(ini_path, source);
        }
        if (ico_cdvd_host_mount_iso(source) != 0) {
            ico_host_fatal(log_file(), "Cannot open the disc image %s.", source);
        }
    } else if (mount_game_data(&a, &ini, exe_dir, ini_path, source, sizeof(source)) != 0) {
        exit_reason = "the first start was stopped";
        fprintf(stderr, "ico_pc: the first start was stopped\n");
        return 0;
    }
#ifdef ICO_ANDROID_UI
    /* every start, not only the first: the tables, the game's own start-up
       and the first frame follow with nothing else on the screen; this
       stays up until the game draws.  Back, a quit or a close polled here
       ends the run, as on the first start's screens. */
    if (ico_window_progress("ICO", "Starting the game", -1)) {
        exit_reason = "the start was stopped";
        fprintf(stderr, "ico_pc: the start was stopped\n");
        return 0;
    }
#endif
    load_tables(source);
#ifndef ICO_HEADLESS
    remove_old_lettering();
#endif

    setup_pad(&a, &ini, exe_dir);

    setup_trace(&a, &ini, exe_dir, logs_dir);

    /* the pad recording */
    record_open(&ini, exe_dir, logs_dir);

    /* when to stop */
    v = a.ticks != NULL ? a.ticks : ico_ini_get(&ini, "ticks");
    if (v != NULL && v[0] != '\0') {
        if (parse_count(v, &ticks) != 0) {
            ico_host_fatal(log_file(), "ticks=%s in %s is not a count.", v, ini_path);
        }
        have_ticks = 1;
        fprintf(stderr, "ico_pc: exit after %lu Main ticks\n", ticks);
    } else {
        fprintf(stderr, "ico_pc: no tick limit\n");
    }
    v = ico_ini_get(&ini, "watchdog");
    watchdog = WATCHDOG_DEFAULT_S;
    if (v != NULL && v[0] != '\0' && parse_count(v, &watchdog) != 0) {
        ico_host_fatal(log_file(), "watchdog=%s in %s is not a number of seconds.", v, ini_path);
    }
    at_shutdown(summary);

    if (have_ticks && ticks == 0) {
        exit_reason = "ticks=0";
        return 0;
    }
    ico_diag_set_sources(ico_host_status, ico_host_main_ticks, ico_host_vsync_count);
    ico_diag_set_exit_hook(fatal_summary);
    name_threads();
#ifdef _WIN32
    ico_diag_arm_vectored();
#endif
#ifndef ICO_HEADLESS
#ifndef ICO_ANDROID_UI
    open_window();
#endif
    open_texture_pack(exe_dir);
    open_model_pack(exe_dir);
#endif
    start_watchdog(watchdog);
    ico_diag_milestone("boot starts (ico_host_init)");
    ico_host_init();
    ico_diag_milestone("boot ran until every thread waits");
    for (;;) {
        ico_host_step();
        if (ico_host_vsync_count() == 1) {
            ico_diag_milestone("first vsync done");
        }
        ico_trace_poll();
        /* the pad recording's lines, then the log: on Windows
           stdout and stderr are fully buffered (host_config.h), so this is
           one write a vsync at most */
        ico_input_record_poll(ico_host_main_ticks());
        ico_host_log_flush();
#ifndef ICO_HEADLESS
        /* the window, SDL and the renderer in the host FP mode; the next
           ico_host_step puts the simulation's back */
        ico_fpenv_host_enter();
        if (!ico_window_pump()) {
            exit_reason = "the window was closed";
            /* the close button can end the run on a
               Settings page that was never left (Characters in the model
               viewer, the pause menu's pages): its changes are written now,
               not lost */
            ui_settings_save_on_quit();
            return 0;
        }
        ico_window_pace(ico_host_vsync_hz());
#endif
        if (have_ticks && ico_host_main_ticks() >= ticks) {
            exit_reason = "ticks= reached";
            return 0;
        }
        if (!warned && ico_host_main_ticks() == 0 &&
            ico_host_vsync_count() >= NO_TICK_WARN_VSYNCS) {
            fprintf(stderr,
                    "ico_pc: no Main tick after %u vsyncs (is ico_host_main_tick hooked?)\n",
                    ico_host_vsync_count());
            warned = 1;
        }
    }
}

#ifdef __ANDROID__

/* Android: SDL_main (port/platform/android/main_android.c) calls this on
   its own thread; the end-of-run steps run before it returns. */
int ico_host_main(int argc, char **argv)
{
    int r = host_main(argc, argv);

    ico_host_shutdown();
    /* the process may stay cached after this thread ends: the watchdog
       must neither count the time nor signal a thread that is gone */
    ico_diag_watchdog_pause(1);
    ico_diag_main_thread_end();
    ico_android_log_mirror_flush();
    return r;
}

#else

int main(int argc, char **argv)
{
    return host_main(argc, argv);
}

#endif
