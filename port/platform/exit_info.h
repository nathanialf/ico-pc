/*
 * port/platform/exit_info.h
 *
 * How the previous run ended, in one line at the start (issue 215: a
 * Retroid Pocket 6 whose runs end with no crash line, no device loss and no
 * watchdog end; the system's kill is the likely cause, but nothing the app
 * wrote could say who ended it).
 *
 * Android 11 (API 30) and later keep, per process of the app, the reason it
 * ended (ActivityManager.getHistoricalProcessExitReasons:
 * ApplicationExitInfo's reason, status, importance, PSS, RSS, time,
 * description, the summary the process set with setProcessStateSummary
 * (last_run.h) and for an ANR or a native crash a trace).  At the window's
 * start, before the start-up activity file is read
 * (window_host.c, exit_info_begin), the records newer than the newest one
 * reported before (logs/exit-info-seen.txt) are read through
 * IcoActivity.exitRecords (android/exit_info_android.c), at most
 * EXIT_INFO_MAX_RECORDS of them, each trace copied (at most
 * EXIT_INFO_TRACE_MAX_BYTES) to logs/exit-trace-<time>.<txt|pb> by the Java
 * side, which keeps the newest two such files.
 *
 * The verdict (exit_info_verdict, pure), in this order: our own end mark
 * (last-run.txt's end: crash, device loss, GPU hang, watchdog, clean; or
 * the start-up activity file's loss line); then Android's record whose pid
 * is last-run.txt's; then last-run.txt and the activity file alone ("no
 * record from Android"; below Android 11 and off Android "the system keeps
 * no exit record").  Logged as
 *
 *   ico_pc: the previous run ended: <how> (<Android's part>); it was at
 *   <phase>, <s> s after its start at its last write; GPU resets <a> -> <b>;
 *   free memory <n> MB at its last write (lowest <l> MB, floor <f> MB)
 *
 * one line for each record read, and the lot written to logs/exit-info.txt,
 * which a report zip carries with the newest trace (report.c), the verdict
 * in report.txt's head.  Nothing changes behaviour by it.
 *
 * The record text (IcoActivity.exitRecords' answer, or the file named by
 * ICO_TEST_EXIT_INFO off Android): lines of tab-separated fields,
 *   sdk     <API level>
 *   lmk     <1|0>  (ActivityManager.isLowMemoryKillReportSupported)
 *   total   <the records the system holds for the app>
 *   why     <text> (no records read: why)
 *   record  <pid> <reason> <status> <importance> <pss kB> <rss kB>
 *           <time ms since 1970> <trace: a file name, "-" none, "!<why>">
 *           <description> <summary>
 * (a record has exactly 11 fields; the Java side turns tabs and newlines in
 * the texts into blanks and cuts the description to EXIT_INFO_DESC_MAX - 1
 * characters and the summary to LAST_RUN_SUMMARY_MAX - 1 bytes).
 */
#ifndef ICO_PLATFORM_EXIT_INFO_H
#define ICO_PLATFORM_EXIT_INFO_H

#include <stddef.h>

#include "last_run.h"

/* the records read at a start (the newest) */
#define EXIT_INFO_MAX_RECORDS 8
/* the bytes of one trace copied */
#define EXIT_INFO_TRACE_MAX_BYTES (512 * 1024)
/* a record's description, with its NUL */
#define EXIT_INFO_DESC_MAX 161
/* a trace's field, with its NUL ("exit-trace-<20 digits>.txt", or "!<why>") */
#define EXIT_INFO_TRACE_NAME_MAX 96
/* the record text's bytes, with its NUL (8 records of at most about 850
   bytes each, and the header lines) */
#define EXIT_INFO_TEXT_MAX 8192
/* the verdict, with its NUL: under diag_host.c's log line (1024 bytes with
   "ico_pc: " and the newline) */
#define EXIT_INFO_VERDICT_MAX 1000
/* one record's line in exit-info.txt and the log, with its NUL */
#define EXIT_INFO_LINE_MAX 900

/* ApplicationExitInfo's reasons, as the public SDK's android.jar defines
   them (android-37.0: 0-16); any other number is printed as its number */
enum {
    EXIT_INFO_REASON_UNKNOWN = 0,
    EXIT_INFO_REASON_EXIT_SELF = 1,
    EXIT_INFO_REASON_SIGNALED = 2,
    EXIT_INFO_REASON_LOW_MEMORY = 3,
    EXIT_INFO_REASON_CRASH = 4,
    EXIT_INFO_REASON_CRASH_NATIVE = 5,
    EXIT_INFO_REASON_ANR = 6,
    EXIT_INFO_REASON_INITIALIZATION_FAILURE = 7,
    EXIT_INFO_REASON_PERMISSION_CHANGE = 8,
    EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE = 9,
    EXIT_INFO_REASON_USER_REQUESTED = 10,
    EXIT_INFO_REASON_USER_STOPPED = 11,
    EXIT_INFO_REASON_DEPENDENCY_DIED = 12,
    EXIT_INFO_REASON_OTHER = 13,
    EXIT_INFO_REASON_FREEZER = 14,
    EXIT_INFO_REASON_PACKAGE_STATE_CHANGE = 15,
    EXIT_INFO_REASON_PACKAGE_UPDATED = 16,
    EXIT_INFO_REASONS
};

/* where the verdict's <how> came from */
enum {
    EXIT_INFO_FROM_MARK,    /* our own end mark */
    EXIT_INFO_FROM_RECORD,  /* Android's record with last-run.txt's pid */
    EXIT_INFO_FROM_FILES,   /* last-run.txt and the activity file alone */
    EXIT_INFO_FROM_NOTHING, /* neither file */
};

/* what the system holds */
enum {
    EXIT_INFO_SYSTEM_NONE,   /* off Android: no exit record */
    EXIT_INFO_SYSTEM_OLD,    /* Android before 11 */
    EXIT_INFO_SYSTEM_READ,   /* read (the records may be none) */
    EXIT_INFO_SYSTEM_FAILED, /* Android 11+, not read: why */
};

typedef struct ExitInfoRecord {
    long pid;
    int reason; /* EXIT_INFO_REASON_*, or a newer one */
    int status; /* the exit code, or the signal */
    int importance;
    long long pssKb, rssKb;
    long long timeMs;
    char trace[EXIT_INFO_TRACE_NAME_MAX]; /* "" none; a file name, or "!<why>" */
    char description[EXIT_INFO_DESC_MAX];
    char summary[LAST_RUN_SUMMARY_MAX];
} ExitInfoRecord;

typedef struct ExitInfoList {
    int system; /* EXIT_INFO_SYSTEM_* */
    int sdk;    /* -1 not said */
    int lmk;    /* -1 not said */
    int total;  /* -1 not said */
    char why[160];
    int n;              /* the records kept, newest first */
    int malformed;      /* lines not in the format */
    int older;          /* records not newer than the last seen */
    int beyond;         /* newer records past EXIT_INFO_MAX_RECORDS */
    long long newestMs; /* the newest time read (the last seen when none) */
    ExitInfoRecord rec[EXIT_INFO_MAX_RECORDS];
} ExitInfoList;

/* The start-up activity file's facts (diag_host.h): present, its note
   (its second line) and its loss line ("" none) */
typedef struct ExitInfoActivity {
    int found;
    char note[LAST_RUN_VALUE_MAX];
    char loss[LAST_RUN_VALUE_MAX];
} ExitInfoActivity;

/* --- The pure part (tested on every platform) ------------------------------ */

/* "REASON_LOW_MEMORY", or "a reason not known to this program" */
const char *exit_info_reason_name(int reason);
/* the reason in plain words a player can read ("Android ended it because
   the device was low on memory"); status for a signal or an exit code,
   lmk the device's low-memory report (1 reported as such, 0 not, -1 not
   said) */
void exit_info_reason_words(int reason, int status, int lmk, char *out, size_t size);
/* "in front", "visible behind another window", "in the background", ... */
const char *exit_info_importance_words(int importance);
/* Parses the record text (NULL: no record kept, EXIT_INFO_SYSTEM_NONE):
   the records newer than lastSeenMs, the newest EXIT_INFO_MAX_RECORDS of
   them, newest first.  Returns the records kept. */
int exit_info_parse(const char *text, long long lastSeenMs, ExitInfoList *out);
/* The activity file's text (NULL: none) */
void exit_info_activity_parse(const char *text, ExitInfoActivity *out);
/* The record of pid (-1 none) */
int exit_info_find_pid(const ExitInfoList *list, long pid);
/* The verdict without "ico_pc: " ("the previous run ended: ..."); last NULL
   or not found: no last-run.txt; act NULL: no activity file; list NULL:
   off Android.  Returns EXIT_INFO_FROM_*. */
int exit_info_verdict(const LastRunFacts *last, const ExitInfoActivity *act,
                      const ExitInfoList *list, char *out, size_t size);
/* One record's line ("pid 4242, REASON_LOW_MEMORY (3): ...") */
void exit_info_record_line(const ExitInfoRecord *r, int lmk, char *out, size_t size);
/* The trace's field is a file the Java side writes ("exit-trace-<digits>.txt"
   or ".pb"): 1, else 0 (it is never taken for a path) */
int exit_info_trace_name_ok(const char *name);
/* The seen file's text ("<ms> <newest trace or ->\n") parsed: the last
   seen time (0 none) and the newest trace ("" none) */
void exit_info_seen_parse(const char *text, long long *lastSeenMs, char *trace, size_t size);

/* --- The run's part (exit_info.c) ------------------------------------------- */

/* The system's record text into out (EXIT_INFO_TEXT_MAX): Android
   (android/exit_info_android.c) through IcoActivity.exitRecords; elsewhere
   the file ICO_TEST_EXIT_INFO names (the tests).  0, or -1 when the system
   keeps none (out "") or the call failed (out a "why\t..." line). */
int exit_info_system_records(const char *logsDir, long long lastSeenMs, char *out, size_t size);
/* At the window's start, before ico_diag_startup_activity_begin: the
   verdict and the record lines logged, logs/exit-info.txt written, the
   seen file updated, the verdict and the newest trace handed to the
   report (report.h). */
void exit_info_begin(const char *logsDir, const char *activityPath);
/* the verdict of this start ("" before exit_info_begin) */
const char *exit_info_last_verdict(void);

#endif
