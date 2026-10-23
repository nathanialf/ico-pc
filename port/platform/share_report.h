/*
 * port/platform/share_report.h
 *
 * The files an rc build's Main row "Share a bug report" hands to the
 * phone's share menu (settings.h ui_settings_set_share_report_handler,
 * window_host.c share_report_arm, ICO_RC_TOOLS alone): pure C over
 * host_fs.h, so it is tested on the native builds (share_report_test).  A
 * release does not call it, so the linker leaves it out of the program.
 */
#ifndef ICO_PLATFORM_SHARE_REPORT_H
#define ICO_PLATFORM_SHARE_REPORT_H

#include <stddef.h>

#define ICO_SHARE_REPORT_MAX 5
#define ICO_SHARE_REPORT_PATH 1024

typedef struct IcoShareReport {
    int count;
    char path[ICO_SHARE_REPORT_MAX][ICO_SHARE_REPORT_PATH];
} IcoShareReport;

/* The report's files, in this order, each one left out when it is not
   there (or its path does not fit):
     logs_dir/ico-pc.log            the log
     logs_dir/ico-pc-previous.log   the run before's (main_host.c)
     dumps_dir's newest *.zip       F12's or the bug-report picture's zip
                                    (or a loss or sweep zip, if newer)
     dumps_dir's newest *-present.png
                                    the picture as presented, written one
                                    present after its zip was closed
     logs_dir's newest report-*.zip the crash or hang report (report.c)
   "newest": the latest modification time, then the greatest name.  NULL or
   "" folders are skipped.  The count (out->count). */
int ico_share_report_collect(const char *logs_dir, const char *dumps_dir, IcoShareReport *out);

/* The files' names (no folder) joined with ", " into out ("none" for no
   file), cut to size. */
void ico_share_report_names(const IcoShareReport *r, char *out, size_t size);

#endif /* ICO_PLATFORM_SHARE_REPORT_H */
