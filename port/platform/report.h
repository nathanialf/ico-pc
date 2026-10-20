/*
 * port/platform/report.h
 *
 * One report file per abnormal end: on a crash, a watchdog stop, a lost
 * graphics device or graphics that could not start, the port writes
 * logs/report-<date>-<time>.zip (report-20261016-142301.zip), the one file
 * a player attaches to an issue. It holds
 *
 *   report.txt               what ended the run, the VERSION.txt lines,
 *                            the disc played (diag_host.h
 *                            ico_diag_set_disc), the summary line (diag_host.h
 *                            ico_diag_set_summary), the process's and the
 *                            device's memory (ico_diag_memory_text),
 *                            achievements.toml's
 *                            stages line (not the file), the latest
 *                            device, blend, index, fog and shader lines
 *                            of the log, the last 20 ico_pc:/window:/rd:
 *                            lines before the report and the report block
 *                            itself (the crash or watchdog report with its
 *                            symbolized stack), then the host's own part
 *                            (IcoReportInputs.extra: SDL's own log);
 *   ico-pc.log               the log so far;
 *   ico-pc-previous.log      the run before's;
 *   startup-activity.txt     when there is one (diag_host.h);
 *   config.toml, ico-pc.ini  the settings;
 *   loss-<stamp>.zip         the newest loss dump, when there is one
 *                            (stored as it is).
 *
 * Written from a crash handler: no allocation, no stdio, raw file calls
 * through static buffers, the zip's entries deflated by miniz's tdefl into
 * a static compressor (the loss dump's writer, dump_zip.c, takes miniz's
 * allocating zip writer, which a crash inside malloc would deadlock). One
 * report at a time. ico_report_prune (which lists the folder, and so
 * allocates) runs only on the normal paths.
 */
#ifndef ICO_PLATFORM_REPORT_H
#define ICO_PLATFORM_REPORT_H

#include <stddef.h>

/* At most this many report-*.zip files stay in the logs folder. */
#define ICO_REPORT_KEEP 5

/* The files a report takes (each copied; NULL or "" leaves one out):
   logs_dir the logs folder (the zip's folder; ico-pc-previous.log and
   startup-activity.txt are read there), exe_dir the program's folder
   (VERSION.txt is read there or in its parent; on Linux then in
   <the running program's folder>/../share/doc/ico-pc/, where the AppImage
   keeps it inside its mount while exe_dir is the AppImage file's folder),
   pref_dir the user folder
   (config.toml, achievements.toml), ini_path ico-pc.ini. Also notes the
   local time's offset from UTC, for the zip's name. */
void ico_report_set_paths(const char *logs_dir, const char *exe_dir, const char *pref_dir,
                          const char *ini_path);
/* Android: VERSION.txt's lines, read from the app's assets (copied, at
   most 255 bytes); report.txt takes them instead of a file. */
void ico_report_set_version_text(const char *text);

typedef struct IcoReportInputs {
    const char *what;     /* "a crash: SIGSEGV ...", "the watchdog", ... */
    const char *log_path; /* the current log (NULL: none) */
    const char *summary;  /* the summary line's text ("" none) */
    const char *disc;     /* the disc played ("SCPS-11003 (the Japanese disc)"; "" none) */
    /* the process's and the device's memory when the report is written
       (diag_host.h ico_diag_memory_text: "memory 191 MB (most 197 MB);
       device 320 MB available of 7632 MB"; NULL or "": unknown) */
    const char *memory;
    const char *loss_zip; /* the newest loss dump's path (NULL: none) */
    double uptime_s;      /* seconds into the run */
    /* text of the host's own for report.txt's end (the window: SDL's log
       ring, diag_host.h ico_diag_set_report_extra; NULL or "": none) */
    const char *extra;
} IcoReportInputs;

/* Writes the report. 0 with the zip's file name (no folder) in name, or -1
   (no logs folder, a write failed) with why in name. Async-signal-safe in
   practice: open/read/write/lseek and static buffers. */
int ico_report_write(const IcoReportInputs *in, char *name, size_t name_size);
/* Deletes the oldest report-*.zip files beyond keep (normal paths only);
   the number deleted, -1 when the folder cannot be read. */
int ico_report_prune(int keep);

/* The local time as the zip's name has it ("20261016-142301"; a second
   report in the same second is report-<stamp>-2.zip). */
void ico_report_stamp(char *out, size_t size);

#endif /* ICO_PLATFORM_REPORT_H */
