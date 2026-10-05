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
/* dir + separator + name. A name that is already absolute is copied. */
void ico_path_join(char *out, size_t size, const char *dir, const char *name);
int ico_path_is_absolute(const char *path);
int ico_file_exists(const char *path);
/* Creates a folder; 0 if it exists afterwards. */
int ico_make_dir(const char *path);
/* Parses ini text (NUL-terminated) into *ini (cleared first). */
void ico_ini_parse(IcoIni *ini, const char *text);
/* Loads a file; 0, or -1 if it cannot be read (*ini is then empty). */
int ico_ini_load(IcoIni *ini, const char *path);
/* The value of key, or NULL. */
const char *ico_ini_get(const IcoIni *ini, const char *key);
/* Sets key=value in the file at path, keeping its other lines (the first
   line with that key is replaced, else one is appended; the file is
   created if missing). 0, or -1. */
int ico_ini_store(const char *path, const char *key, const char *value);

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
/* Sends stdout and stderr (unbuffered) to log_path, created afresh. Fatal
   errors still reach the original stderr on POSIX. 0, or -1 (the streams
   are unchanged when the file cannot be created). */
int ico_host_redirect_output(const char *log_path);
/* Windows: the file-open dialog for the disc image; 0 and the path, or -1
   if cancelled. Elsewhere: -1. */
int ico_host_pick_iso(char *out, size_t size);

/* Logs the message (stderr, the log when redirected), shows it in a message
   box on Windows naming the log, and exits 1. */
void ico_host_fatal(const char *log_path, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3), noreturn))
#endif
    ;

#endif /* ICO_PLATFORM_HOST_CONFIG_H */
