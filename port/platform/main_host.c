/*
 * port/platform/main_host.c
 *
 * The host program's entry point. The game's own main (common/src/main.c)
 * is compiled as ico_game_main (CMakeLists.txt renames it with a definition
 * on that one source) and runs on the boot fiber (host_loop.c).
 *
 * The default build opens a window titled "ICO" (window_host.h): the game
 * draws through rd into it, one simulated vsync per real 20 ms (PAL; 16.7 ms
 * at 60 Hz), presented with vsync on, and Escape or closing the window ends
 * the run. The headless build (CMake ICO_HEADLESS=ON: the trace and test
 * runs) has no window and simulates vsyncs as fast as it can.
 *
 * Run with no arguments (a double-click), everything comes from the
 * executable's folder (host_config.h describes ico-pc.ini):
 *
 *   logs/ico-pc.log          stdout and stderr, rewritten each run
 *   logs/trace-<time>.txt    the trace (trace_host.h); ini trace=0 or PATH
 *   disc image               Ico_PAL.iso beside the executable, else ini
 *                            iso=, else $ICO_ISO, else baserom/Ico_PAL.iso
 *                            under the working folder, else (Windows) a
 *                            file-open dialog whose answer is saved as iso=
 *                            in ico-pc.ini; then its SHA-1 is checked
 *                            against the SCES-50760 image's (ini verify=0
 *                            skips it)
 *   pad script               ini pad_script=, else pad-script.txt beside the
 *                            executable if present, else no controller
 *   ticks                    ini ticks=N exits after N Main ticks
 *   watchdog                 ini watchdog=S (default 30): the run is stopped
 *                            and reported when no Main tick came S seconds
 *                            after boot started, or no new one for 2*S
 *                            seconds; 0 turns it off (diag_host.h)
 *
 * Developer overrides, which win over all of the above:
 *
 *   ico_pc [--iso PATH] [--pad-script FILE] [--trace FILE|none] [--ticks N]
 *          [--vsync-rate X] [--no-verify] [--console] [--help]
 *
 * --console keeps stdout and stderr on the console instead of the log.
 * Options also take the --opt=VALUE spelling. An unknown option or a bad
 * value prints the usage on stderr and exits 2. An error that stops the run
 * (no disc image, a wrong one, a bad pad script) is logged, shown in a
 * message box on Windows, and exits 1.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cdvd_host.h"
#include "diag_host.h"
#include "host_config.h"
#include "host_loop.h"
#include "pad_script.h"
#include "tables.h"
#include "trace_host.h"
#ifndef ICO_HEADLESS
#include "window_host.h"
#endif

/* The PAL disc image's SHA-1 (docs/port/DATA.md, "Facts about the PAL disc
   relied on"). */
#define ICO_ISO_SHA1 "1017b53f6e80f41f823369b0be1d8c69f7e16dc6"
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
void InitIcoMisc(void);

#ifdef _WIN32

void ico_diag_arm_vectored(void);

#endif

typedef struct Args {
    const char *iso;
    const char *pad_script;
    const char *trace;
    const char *ticks;
    int no_verify;
    int console;
} Args;

static char log_path[ICO_PATH_MAX];

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "usage: %s [options]   (none needed: see ico-pc.ini)\n"
            "  --iso PATH         the SCES-50760 disc image\n"
            "  --pad-script FILE  plug a scripted DualShock into controller port 1;\n"
            "                     lines: <tick> <buttons-hex> [lx ly rx ry]\n"
            "  --trace FILE|none  the per-Main-tick trace (default logs/trace-<time>.txt)\n"
            "  --ticks N          exit after N Main ticks\n"
            "  --vsync-rate X     accepted and ignored (pacing comes later)\n"
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

static int parse_rate(const char *s)
{
    char *end;
    double v;

    errno = 0;
    v = strtod(s, &end);
    return errno != 0 || end == s || *end != '\0' || !(v > 0.0) ? -1 : 0;
}

static const char *exit_reason = "exit() from the game or the C library";

/* Every normal end goes through here (atexit). */
static void summary(void)
{
    ico_trace_close();
    fflush(stdout);
    fflush(stderr);
    ico_diag_log("ico_pc: exit: %s", exit_reason);
    ico_diag_log("ico_pc: %u Main ticks, %u vsyncs, stage_no %d", ico_host_main_ticks(),
                 ico_host_vsync_count(), ico_host_stage_no());
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
    const char *prog = argc > 0 ? argv[0] : "ico_pc";
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
                fprintf(stderr, "%s: --ticks wants a count, not '%s'\n", prog, v);
                usage(stderr, prog);
                return 2;
            }
            a->ticks = v;
        } else if ((r = option(argc, argv, &i, "--vsync-rate", &v)) != 0) {
            if (r > 0 && parse_rate(v) != 0) {
                fprintf(stderr, "%s: --vsync-rate wants a positive number, not '%s'\n", prog, v);
                usage(stderr, prog);
                return 2;
            }
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", prog, name);
            usage(stderr, prog);
            return 2;
        }
        if (r < 0) {
            fprintf(stderr, "%s: %s needs a value\n", prog, name);
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

/* Finds the disc image; fatal when there is none. *picked is set when it
   came from the dialog (to be saved once verified). */
static void find_iso(const Args *a, const IcoIni *ini, const char *exe_dir, char *iso, int *picked)
{
    const char *v;

    *picked = 0;
    if (a->iso != NULL) {
        snprintf(iso, ICO_PATH_MAX, "%s", a->iso);
        fprintf(stderr, "ico_pc: disc image from --iso: %s\n", iso);
        return;
    }
    ico_path_join(iso, ICO_PATH_MAX, exe_dir, "Ico_PAL.iso");
    if (ico_file_exists(iso)) {
        fprintf(stderr, "ico_pc: disc image beside the executable: %s\n", iso);
        return;
    }
    v = ico_ini_get(ini, "iso");
    if (v != NULL && v[0] != '\0') {
        ico_path_join(iso, ICO_PATH_MAX, exe_dir, v);
        fprintf(stderr, "ico_pc: disc image from ico-pc.ini: %s\n", iso);
        return;
    }
    v = getenv("ICO_ISO");
    if (v != NULL && v[0] != '\0') {
        snprintf(iso, ICO_PATH_MAX, "%s", v);
        fprintf(stderr, "ico_pc: disc image from ICO_ISO: %s\n", iso);
        return;
    }
    if (ico_file_exists("baserom/Ico_PAL.iso")) {
        snprintf(iso, ICO_PATH_MAX, "baserom/Ico_PAL.iso");
        fprintf(stderr, "ico_pc: disc image from the working folder: %s\n", iso);
        return;
    }
    if (ico_host_pick_iso(iso, ICO_PATH_MAX) == 0) {
        fprintf(stderr, "ico_pc: disc image chosen in the dialog: %s\n", iso);
        *picked = 1;
        return;
    }
    ico_host_fatal(log_path, "No ICO disc image was found or chosen. Put Ico_PAL.iso next to "
                             "ico_pc, or set iso=<path> in ico-pc.ini.");
}

static void verify_iso(const char *iso)
{
    char hex[41];
    unsigned long long bytes = 0;
    clock_t start = clock();
    double secs;

    fprintf(stderr, "ico_pc: checking the disc image's SHA-1...\n");
    if (ico_sha1_file(iso, hex, &bytes) != 0) {
        ico_host_fatal(log_path, "Cannot read the disc image %s.", iso);
    }
    secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "ico_pc: SHA-1 %s, %llu bytes, %.1f s\n", hex, bytes, secs);
    if (strcmp(hex, ICO_ISO_SHA1) != 0) {
        ico_host_fatal(log_path,
                       "%s is not the expected ICO disc image (PAL, SCES-50760).\n"
                       "SHA-1 %s, expected %s.",
                       iso, hex, ICO_ISO_SHA1);
    }
    fprintf(stderr, "ico_pc: disc image verified (SCES-50760)\n");
}

int main(int argc, char **argv)
{
    Args a;
    IcoIni ini;
    char exe_dir[ICO_PATH_MAX];
    char logs_dir[ICO_PATH_MAX];
    char ini_path[ICO_PATH_MAX];
    char iso[ICO_PATH_MAX];
    char path[ICO_PATH_MAX];
    char stamp[32];
    const char *v;
    unsigned long ticks = 0;
    unsigned long watchdog;
    int have_ticks = 0;
    int picked;
    int warned = 0;
    int r;

    r = parse_args(argc, argv, &a);
    if (r != 0) {
        return r == 1 ? 0 : 2;
    }

    /* the log first, so everything after it is recorded */
    ico_host_exe_dir(exe_dir, sizeof(exe_dir));
    ico_path_join(logs_dir, sizeof(logs_dir), exe_dir, "logs");
    ico_path_join(log_path, sizeof(log_path), logs_dir, "ico-pc.log");
    if (a.console) {
        snprintf(log_path, sizeof(log_path), "(the console)");
    } else if (ico_make_dir(logs_dir) != 0 || ico_host_redirect_output(log_path) != 0) {
        fprintf(stderr, "ico_pc: cannot write %s; logging to the console\n", log_path);
        snprintf(log_path, sizeof(log_path), "(none: the logs folder is not writable)");
    }
    /* crash handlers and the unbuffered diagnostics writer, first thing */
    ico_diag_init(a.console ? NULL : log_path);
    timestamp(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S");
    fprintf(stderr, "ico_pc: started %s in %s\n", stamp, exe_dir);
    for (r = 1; r < argc; r++) {
        fprintf(stderr, "ico_pc: argument %s\n", argv[r]);
    }

    ico_path_join(ini_path, sizeof(ini_path), exe_dir, "ico-pc.ini");
    if (ico_ini_load(&ini, ini_path) == 0) {
        fprintf(stderr, "ico_pc: settings from %s (%d keys)\n", ini_path, ini.count);
    } else {
        fprintf(stderr, "ico_pc: no %s; defaults\n", ini_path);
    }

    /* the disc goes in before boot, so a missing image fails here rather
       than leaving the game at file_Init's disc wait (docs/port/DATA.md) */
    find_iso(&a, &ini, exe_dir, iso, &picked);
    v = ico_ini_get(&ini, "verify");
    if (a.no_verify || (v != NULL && strcmp(v, "0") == 0)) {
        fprintf(stderr, "ico_pc: disc image SHA-1 check skipped\n");
    } else {
        verify_iso(iso);
    }
    if (picked) {
        if (ico_ini_store(ini_path, "iso", iso) == 0) {
            fprintf(stderr, "ico_pc: saved iso=%s in %s\n", iso, ini_path);
        } else {
            fprintf(stderr, "ico_pc: cannot save the image path in %s\n", ini_path);
        }
    }
    if (ico_cdvd_host_mount_iso(iso) != 0) {
        ico_host_fatal(log_path, "Cannot open the disc image %s.", iso);
    }
    /* the data tables, from the disc's boot ELF, before anything reads one
       (port/data/tables.h) */
    {
        char why[512];

        if (ico_tables_load_vfs(ico_vfs_disc(), why, sizeof(why)) != 0) {
            ico_host_fatal(log_path, "Cannot load the game's data tables from %s.\n%s", iso, why);
        }
        fprintf(stderr, "ico_pc: %u data table rows (%u records) loaded from %s\n",
                (unsigned)ico_tables_loaded_rows(), (unsigned)ico_tables_loaded_records(),
                ICO_TABLES_BOOT_ELF);
    }

    /* the pad */
    /* command-line paths are the working folder's, ini paths the
       executable's */
    v = ico_ini_get(&ini, "pad_script");
    if (a.pad_script != NULL) {
        snprintf(path, sizeof(path), "%s", a.pad_script);
    } else if (v != NULL) {
        ico_path_join(path, sizeof(path), exe_dir, v);
    } else {
        ico_path_join(path, sizeof(path), exe_dir, "pad-script.txt");
        if (!ico_file_exists(path)) {
            path[0] = '\0';
        }
    }
    if (path[0] != '\0') {
        if (ico_pad_script_load(path) != 0) {
            ico_host_fatal(log_path, "Cannot use the pad script %s (details in the log).", path);
        }
        fprintf(stderr, "ico_pc: pad script %s, %d entries: a DualShock in port 1\n", path,
                ico_pad_script_count());
    } else {
        fprintf(stderr, "ico_pc: no pad script: no controller\n");
    }

    /* the trace */
    v = a.trace != NULL ? a.trace : ico_ini_get(&ini, "trace");
    if (v != NULL && (strcmp(v, "0") == 0 || strcmp(v, "none") == 0)) {
        fprintf(stderr, "ico_pc: no trace\n");
    } else {
        if (a.trace != NULL) {
            snprintf(path, sizeof(path), "%s", a.trace);
        } else if (v != NULL && strcmp(v, "1") != 0) {
            ico_path_join(path, sizeof(path), exe_dir, v);
        } else {
            char name[64];

            timestamp(stamp, sizeof(stamp), "%Y%m%d-%H%M%S");
            snprintf(name, sizeof(name), "trace-%s.txt", stamp);
            ico_path_join(path, sizeof(path), logs_dir, name);
        }
        if (ico_trace_open(path) == 0) {
            fprintf(stderr, "ico_pc: trace %s\n", path);
        } else {
            fprintf(stderr, "ico_pc: running without a trace\n");
        }
    }

    /* when to stop */
    v = a.ticks != NULL ? a.ticks : ico_ini_get(&ini, "ticks");
    if (v != NULL && v[0] != '\0') {
        if (parse_count(v, &ticks) != 0) {
            ico_host_fatal(log_path, "ticks=%s in %s is not a count.", v, ini_path);
        }
        have_ticks = 1;
        fprintf(stderr, "ico_pc: exit after %lu Main ticks\n", ticks);
    } else {
        fprintf(stderr, "ico_pc: no tick limit\n");
    }
    v = ico_ini_get(&ini, "watchdog");
    watchdog = WATCHDOG_DEFAULT_S;
    if (v != NULL && v[0] != '\0' && parse_count(v, &watchdog) != 0) {
        ico_host_fatal(log_path, "watchdog=%s in %s is not a number of seconds.", v, ini_path);
    }
    atexit(summary);

    if (have_ticks && ticks == 0) {
        exit_reason = "ticks=0";
        return 0;
    }
    ico_diag_set_sources(ico_host_status, ico_host_main_ticks, ico_host_vsync_count);
    ico_diag_set_exit_hook(fatal_summary);
    ico_diag_name_func((void *)Main, "Main");
    ico_diag_name_func((void *)StageManager, "StageManager");
    ico_diag_name_func((void *)iosCdvdManager, "iosCdvdManager");
    ico_diag_name_func((void *)iosMcManager, "iosMcManager");
    ico_diag_name_func((void *)jimakuManager, "jimakuManager");
    ico_diag_name_func((void *)sndManager, "sndManager");
    ico_diag_name_func((void *)InitIcoMisc, "InitIcoMisc");
#ifdef _WIN32
    ico_diag_arm_vectored();
#endif
#ifndef ICO_HEADLESS
    /* the window and the renderer before boot: gsb_InitGSSystem's first
       frame already records into rd.  512 x 512 is the PAL frame; gsb_Init
       resizes the scene targets if the game switches to 60 Hz. */
    if (ico_window_open(512, 512) != 0) {
        ico_host_fatal(log_path, "Could not open the game window or start Vulkan.\n"
                                 "The log names the reason; a Vulkan 1.2 driver is needed.");
    }
    atexit(ico_window_close);
#endif
    ico_diag_start((unsigned int)watchdog, (unsigned int)(watchdog * 2));
    ico_diag_milestone("boot starts (ico_host_init)");
    ico_host_init();
    ico_diag_milestone("boot ran until every thread waits");
    for (;;) {
        ico_host_step();
        if (ico_host_vsync_count() == 1) {
            ico_diag_milestone("first vsync done");
        }
        ico_trace_poll();
#ifndef ICO_HEADLESS
        if (!ico_window_pump()) {
            exit_reason = "the window was closed";
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
