/*
 * port/platform/host_config.h
 *
 * What ico_pc needs to run from a double-click: the folder the executable
 * is in, the ico-pc.ini file beside it, the log folder, the disc image's
 * SHA-1, the Windows file-open dialog and error box (package 1D; main_host.c
 * uses them).
 *
 * ico-pc.ini, next to the executable: `key=value` lines. A line whose first
 * non-blank character is `#` or `;` is a comment (whole lines only, so a
 * value may contain either). Key and value are trimmed of blanks; a value
 * in double quotes loses them (Explorer's "Copy as path" adds them). Paths
 * may contain spaces and backslashes; a relative path is taken from the
 * executable's folder. Keys:
 *
 *   iso=PATH         the disc image (written back after the file dialog)
 *   ticks=N          exit after N Main ticks; absent: run until closed
 *   pad_script=PATH  the pad script (default: pad-script.txt beside the
 *                    executable, if present)
 *   trace=0          no trace file; trace=PATH writes it there instead of
 *                    logs/trace-<yyyymmdd-hhmmss>.txt
 *   verify=0         skip the disc image's SHA-1 check
 *   dump_every=N     window build: write every Nth rendered frame as an rd
 *                    dump (rd_replay_tool) into dump_dir=PATH (default
 *                    dumps beside the ini), named rd-NNNNN.rddump by the
 *                    renderer's frame number (one per Main tick)
 *   audio_dump=PATH  write the mixed 48 kHz audio to PATH (beside the ini)
 *                    as a WAV; audio_dump=1 is logs/audio.wav
 *   audio=0          window build: no audio device (the driver still runs)
 *   saves=PATH       the memory card folder (port/save, docs/port/SAVES.md):
 *                    the game's files are PATH/BESCES-50760ico/; default
 *                    memcard beside the per-user folder
 *   saves2=PATH      a second card folder, the card in port 1; absent or
 *                    empty: port 1 has no card (the original behaviour)
 */
#ifndef ICO_PLATFORM_HOST_CONFIG_H
#define ICO_PLATFORM_HOST_CONFIG_H

#include <stddef.h>

#define ICO_PATH_MAX 1024
#define ICO_INI_MAX_KEYS 32

typedef struct IcoIni {
    int count;
    char key[ICO_INI_MAX_KEYS][64];
    char value[ICO_INI_MAX_KEYS][ICO_PATH_MAX];
} IcoIni;

/* The executable's folder, without a trailing separator. 0, or -1 (then
   out is "."). */
int ico_host_exe_dir(char *out, size_t size);
/* dir + separator + name. A name that is already absolute is copied. 0, or
   -1 when the result does not fit in size: out is then "" (never a cut-off
   path), so a file is not created under another name. Check it where a file
   is created or written. */
int ico_path_join(char *out, size_t size, const char *dir, const char *name);
int ico_path_is_absolute(const char *path);
int ico_file_exists(const char *path);
/* Creates a folder; 0 if it exists afterwards. */
int ico_make_dir(const char *path);
/* Parses ini text (NUL-terminated) into *ini (cleared first). */
void ico_ini_parse(IcoIni *ini, const char *text);
/* Loads a file; 0, or -1 if it cannot be read (*ini is then empty). For the
   executable's own ini this is ico_ini_load_layered and then ico_ini_export:
   called once, at start-up (main_host.c). */
int ico_ini_load(IcoIni *ini, const char *path);
/* The layered read alone (ini > config.toml), with no side effect: no
   environment variable set, no folder made. 0, or -1 as ico_ini_load. */
int ico_ini_load_layered(IcoIni *ini, const char *path);
/* The side effects of ico_ini_load for an ini read from path with result r:
   the environment exports (ICO_*) and the dump and log folders. */
void ico_ini_export(const IcoIni *ini, const char *path, int r);
/* The value of key, or NULL. */
const char *ico_ini_get(const IcoIni *ini, const char *key);
/* Sets key=value in the file at path, keeping its other lines (the first
   line with that key is replaced, else one is appended; the file is
   created if missing). 0, or -1. */
int ico_ini_store(const char *path, const char *key, const char *value);

/* The folder per-user settings live in (config.toml). The window build
   (ico_pc compiled with ICO_HOST_SDL_PREFPATH) uses SDL's
   SDL_GetPrefPath("ico-pc", "ico-pc"); the headless build and the tests,
   which have no SDL, the executable's folder, as does a failing
   SDL_GetPrefPath. No trailing separator. 0, or -1 (then out is "."). */
int ico_host_pref_dir(char *out, size_t size);
/* The ico-pc.ini beside the executable: the override layer. */
int ico_host_ini_path(char *out, size_t size);
/* ico_ini_load without the layering below: the file's own keys only. */
int ico_ini_load_file(IcoIni *ini, const char *path);
/* The ini key a config.toml path maps onto (the keys the ini has always had:
   "paths.iso" is iso=, "dev.ticks" is ticks=, "audio.enabled" is audio=), or
   NULL. A true/false toml value reads as 1/0 there. ico_ini_load of
   ico_host_ini_path() fills the ini's missing keys from these toml entries
   (ini beside the exe > config.toml > defaults) and exports the keys that
   other libraries read from the environment (ICO_AUDIO, ICO_AUDIO_DUMP,
   ICO_AUDIO_VOLUME, ICO_FIXED_CLOCK, ICO_RD_DUMP_*). */
const char *ico_config_ini_key(const char *toml_path);
/* Whether the clock should be fixed (ICO_FIXED_CLOCK): fixed_clock= when
   present, else true for the headless build, headless=1, or a trace written
   to an explicit path (trace= other than 0/none; the default trace of a
   user run does not count), else false. */
int ico_host_fixed_clock(const IcoIni *ini);
/* The memory card folder: saves= in ico-pc.ini (a relative path is taken
   from the executable's folder), else <pref dir>/memcard. Not created; a pure
   read (ico_ini_load_layered). 0, or -1 (then out is "memcard"). */
int ico_host_saves_dir(char *out, size_t size);
/* The port-1 card folder: saves2= (relative to the executable's folder).
   1 with out set, 0 when not configured (no card in port 1; out ""), -1 when
   the path does not fit (out ""). A pure read, like ico_host_saves_dir. */
int ico_host_saves2_dir(char *out, size_t size);

/* config.toml: a small TOML subset, enough for [sections] and key = value
   lines. Section headers are `[name]` or `[a.b]`; a key's path is
   "section.key" ("input.kb.cross"), so `[input] kb.cross = ..` and
   `[input.kb] cross = ..` are the same entry. Values: "basic" and 'literal'
   strings (quotes dropped), true/false, numbers, and one-line arrays, which
   are kept as written (brackets included) for the caller to split. A `#`
   outside quotes starts a comment; a repeated key keeps its last line.
   Not supported (ignored): multi-line values, inline tables, dates,
   [[arrays of tables]], escapes beyond \\ and \". */
typedef struct IcoToml IcoToml;
/* NULL only on out of memory (ico_toml_load: or an unreadable file). */
IcoToml *ico_toml_parse(const char *text);
IcoToml *ico_toml_load(const char *path);
void ico_toml_free(IcoToml *t);
/* The value text of path, or NULL (t may be NULL). */
const char *ico_toml_get(const IcoToml *t, const char *path);
/* A bool (true/false/1/0), or def if absent or malformed. */
int ico_toml_get_bool(const IcoToml *t, const char *path, int def);
double ico_toml_get_float(const IcoToml *t, const char *path, double def);
long long ico_toml_get_int(const IcoToml *t, const char *path, long long def);
/* Setters: a new key is added, an existing one changed (t may not be NULL).
   Strings are written quoted, the rest as written. 0, or -1 (out of memory). */
int ico_toml_set_string(IcoToml *t, const char *path, const char *value);
int ico_toml_set_bool(IcoToml *t, const char *path, int value);
int ico_toml_set_int(IcoToml *t, const char *path, long long value);
int ico_toml_set_float(IcoToml *t, const char *path, double value);
/* Whether path is in t. */
int ico_toml_has(const IcoToml *t, const char *path);
/* The file text with t's entries applied to `existing` (NULL: an empty
   file): a line whose key is in t and whose value differs is rewritten as
   `key = value`; everything else in the text (comments, unknown keys, array
   layout, the lines the reader ignores) is kept byte for byte; t's keys that
   are not in the text are added at the end of their section (a new [section]
   at the end of the file; top-level keys before the first section). Returns
   malloc'd text, or NULL on out of memory. */
char *ico_toml_render(const IcoToml *t, const char *existing);
/* Renders t over the file at path and writes it atomically (path.tmp, then
   rename over path). 0, or -1; the old file is intact on failure. */
int ico_toml_save(const IcoToml *t, const char *path);

/* SHA-1. */
typedef struct IcoSha1 {
    unsigned int h[5];
    unsigned long long length;
    unsigned char block[64];
    unsigned int used;
} IcoSha1;

void ico_sha1_init(IcoSha1 *s);
void ico_sha1_update(IcoSha1 *s, const void *data, size_t n);
void ico_sha1_final(IcoSha1 *s, unsigned char digest[20]);
/* The file's SHA-1 as 40 lowercase hex digits; *bytes gets its size. 0, or
   -1 if it cannot be read. */
int ico_sha1_file(const char *path, char hex[41], unsigned long long *bytes);
/* Sends stdout and stderr to log_path, created afresh: unbuffered on POSIX,
   fully buffered on Windows (package Q1: msvcrt writes an unbuffered stream
   one character per OS call), where the host loop calls ico_host_log_flush
   once per vsync. Fatal errors still reach the original stderr on POSIX. 0,
   or -1 (the streams are unchanged when the file cannot be created). */
int ico_host_redirect_output(const char *log_path);
/* Writes out what stdout and stderr hold (the host loop, once per vsync). */
void ico_host_log_flush(void);
/* Windows: the file-open dialog for the disc image; 0 and the path, or -1
   if cancelled. Elsewhere: -1. */
int ico_host_pick_iso(char *out, size_t size);

/* Windows GUI program (-mwindows): stdout and stderr go nowhere until a
   console is attached. Attaches the console of the program that started
   this one (a command prompt), if there is one, and points stdout and
   stderr at it unless they were already redirected; 1 when the streams
   reach a console or a file, 0 when they go nowhere (started from
   Explorer). Elsewhere: 1. */
int ico_host_attach_console(void);
/* A message box with a UTF-8 text (Windows; error != 0 for the error icon).
   Elsewhere: the text on stderr. */
void ico_host_message_box(const char *text, int error);

/* Logs the message (stderr, the log when redirected), shows it in a message
   box on Windows naming the log (log_path NULL: no log was opened), and
   exits 1. */
void ico_host_fatal(const char *log_path, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3), noreturn))
#endif
    ;

#endif /* ICO_PLATFORM_HOST_CONFIG_H */
