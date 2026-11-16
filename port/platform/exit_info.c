/*
 * port/platform/exit_info.c
 *
 * How the previous run ended (exit_info.h): the record text's parse, the
 * reasons' words and the verdict (pure), then the start's part.
 */
#include "exit_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag_host.h"
#include "host_fs.h"
#include "report.h"

/* --- The reasons ---------------------------------------------------------------- */

/* ApplicationExitInfo's constants' names, by value */
static const char *const s_reason_name[EXIT_INFO_REASONS] = {
    "REASON_UNKNOWN",
    "REASON_EXIT_SELF",
    "REASON_SIGNALED",
    "REASON_LOW_MEMORY",
    "REASON_CRASH",
    "REASON_CRASH_NATIVE",
    "REASON_ANR",
    "REASON_INITIALIZATION_FAILURE",
    "REASON_PERMISSION_CHANGE",
    "REASON_EXCESSIVE_RESOURCE_USAGE",
    "REASON_USER_REQUESTED",
    "REASON_USER_STOPPED",
    "REASON_DEPENDENCY_DIED",
    "REASON_OTHER",
    "REASON_FREEZER",
    "REASON_PACKAGE_STATE_CHANGE",
    "REASON_PACKAGE_UPDATED",
};

/* the plain words, by value (SIGNALED and EXIT_SELF take their status) */
static const char *const s_reason_words[EXIT_INFO_REASONS] = {
    "Android does not know why it ended",
    "it closed itself",
    "it was killed",
    "Android ended it because the device was low on memory",
    "it crashed in Java code",
    "it crashed in native code",
    "it stopped responding (Android's \"not responding\")",
    "it failed to start (Android's initialization failure)",
    "Android ended it because a permission changed",
    "Android ended it for using too much of the device's resources",
    "it was closed from the recent apps list or with Force stop (or updated, before Android 14)",
    "it was stopped because the device's user it ran as was stopped",
    "Android ended it because a process it depends on went away",
    "Android ended it for a reason of the system's own (its description says which)",
    "Android ended it while it was frozen in the background",
    "it was ended because the app was turned off or changed",
    "it was ended because the app was updated",
};

const char *exit_info_reason_name(int reason)
{
    return reason >= 0 && reason < EXIT_INFO_REASONS ? s_reason_name[reason]
                                                     : "a reason not known to this program";
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case 6:
        return "SIGABRT";
    case 7:
        return "SIGBUS";
    case 9:
        return "SIGKILL";
    case 11:
        return "SIGSEGV";
    case 15:
        return "SIGTERM";
    default:
        return "";
    }
}

void exit_info_reason_words(int reason, int status, int lmk, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    if (reason == EXIT_INFO_REASON_SIGNALED) {
        const char *nm = signal_name(status);
        snprintf(out, size, "it was killed (signal %d%s%s)%s", status, nm[0] != '\0' ? ", " : "",
                 nm,
                 status == 9 && lmk == 0
                     ? "; this device does not report low-memory kills as such, so it may "
                       "have been one"
                     : "");
    } else if (reason == EXIT_INFO_REASON_EXIT_SELF) {
        snprintf(out, size, "it closed itself (exit code %d)", status);
    } else if (reason >= 0 && reason < EXIT_INFO_REASONS) {
        snprintf(out, size, "%s", s_reason_words[reason]);
    } else {
        snprintf(out, size, "Android ended it for reason %d (a reason not known to this program)",
                 reason);
    }
}

const char *exit_info_importance_words(int imp)
{
    /* ActivityManager.RunningAppProcessInfo's IMPORTANCE_* */
    switch (imp) {
    case 100:
        return "in front";
    case 125:
        return "a foreground service";
    case 130:
    case 230:
        return "noticed by the player, not in front";
    case 150:
    case 325:
        return "in front with the screen off";
    case 200:
        return "visible behind another window";
    case 300:
        return "a background service";
    case 350:
        return "in the background (heavy, state not saved)";
    case 400:
    case 500:
        return "in the background";
    case 1000:
        return "already gone";
    default:
        return imp < 200 ? "in front" : "in the background";
    }
}

/* --- The record text ------------------------------------------------------------- */

/* the next tab-separated field of a line (NUL-ended: the line copy's) */
static char *next_field(char **at)
{
    char *s = *at;
    char *t;
    if (s == NULL) {
        return NULL;
    }
    t = strchr(s, '\t');
    if (t != NULL) {
        *t = '\0';
        *at = t + 1;
    } else {
        *at = NULL;
    }
    return s;
}

/* a whole decimal (optionally negative) into v: 1, else 0 */
static int whole_number(const char *s, long long *v)
{
    char *end = NULL;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    *v = strtoll(s, &end, 10);
    return end != NULL && *end == '\0';
}

static void copy_text(char *out, size_t size, const char *s)
{
    size_t i = 0;
    for (; s != NULL && s[i] != '\0' && i + 1 < size; i++) {
        const char ch = s[i];
        out[i] = ch == '\r' || ch == '\n' || ch == '\t' ? ' ' : ch;
    }
    out[i] = '\0';
}

int exit_info_trace_name_ok(const char *name)
{
    static const char head[] = "exit-trace-";
    size_t i = sizeof head - 1;
    if (name == NULL || strncmp(name, head, sizeof head - 1) != 0) {
        return 0;
    }
    if (name[i] < '0' || name[i] > '9') {
        return 0;
    }
    while (name[i] >= '0' && name[i] <= '9') {
        i++;
    }
    return strcmp(name + i, ".txt") == 0 || strcmp(name + i, ".pb") == 0;
}

/* one "record" line's fields after its key: 1 when they are the format's */
static int record_parse(char *rest, ExitInfoRecord *r)
{
    char *f[10];
    long long v[7];
    for (int i = 0; i < 10; i++) {
        f[i] = next_field(&rest);
        if (f[i] == NULL) {
            return 0;
        }
    }
    if (rest != NULL) {
        return 0; /* more than the format's fields */
    }
    for (int i = 0; i < 7; i++) {
        if (!whole_number(f[i], &v[i])) {
            return 0;
        }
    }
    memset(r, 0, sizeof *r);
    r->pid = (long)v[0];
    r->reason = (int)v[1];
    r->status = (int)v[2];
    r->importance = (int)v[3];
    r->pssKb = v[4];
    r->rssKb = v[5];
    r->timeMs = v[6];
    if (strcmp(f[7], "-") == 0) {
        r->trace[0] = '\0';
    } else if (f[7][0] == '!' || exit_info_trace_name_ok(f[7])) {
        copy_text(r->trace, sizeof r->trace, f[7]);
    } else {
        return 0; /* a name the Java side does not write */
    }
    copy_text(r->description, sizeof r->description, f[8]);
    copy_text(r->summary, sizeof r->summary, f[9]);
    return 1;
}

/* the record kept in time order, newest first, at most the cap */
static void keep_record(ExitInfoList *out, const ExitInfoRecord *r)
{
    int at = out->n;
    if (out->n == EXIT_INFO_MAX_RECORDS) {
        out->beyond++;
        if (r->timeMs <= out->rec[out->n - 1].timeMs) {
            return; /* older than every one kept */
        }
        at = out->n - 1; /* the oldest goes */
    } else {
        out->n++;
    }
    while (at > 0 && out->rec[at - 1].timeMs < r->timeMs) {
        out->rec[at] = out->rec[at - 1];
        at--;
    }
    out->rec[at] = *r;
}

int exit_info_parse(const char *text, long long lastSeenMs, ExitInfoList *out)
{
    static char line[EXIT_INFO_TEXT_MAX];
    static ExitInfoRecord r;
    const char *p = text;
    memset(out, 0, sizeof *out);
    out->sdk = out->lmk = out->total = -1;
    out->newestMs = lastSeenMs;
    if (text == NULL) {
        out->system = EXIT_INFO_SYSTEM_NONE;
        return 0;
    }
    out->system = EXIT_INFO_SYSTEM_READ;
    while (*p != '\0') {
        const size_t len = strcspn(p, "\n");
        char *rest = line;
        char *key;
        long long v;
        const size_t n = len < sizeof line - 1 ? len : sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        if (n > 0 && line[n - 1] == '\r') {
            line[n - 1] = '\0';
        }
        p += len;
        if (*p == '\n') {
            p++;
        }
        if (line[0] == '\0') {
            continue;
        }
        key = next_field(&rest);
        if (strcmp(key, "record") == 0) {
            if (!record_parse(rest, &r)) {
                out->malformed++;
            } else if (r.timeMs <= lastSeenMs) {
                out->older++;
            } else {
                if (r.timeMs > out->newestMs) {
                    out->newestMs = r.timeMs;
                }
                keep_record(out, &r);
            }
        } else if (strcmp(key, "why") == 0 && rest != NULL) {
            copy_text(out->why, sizeof out->why, rest);
        } else if ((strcmp(key, "sdk") == 0 || strcmp(key, "lmk") == 0 ||
                    strcmp(key, "total") == 0) &&
                   rest != NULL && whole_number(rest, &v) && v >= 0 && v < 1000000) {
            if (key[0] == 's') {
                out->sdk = (int)v;
            } else if (key[0] == 'l') {
                out->lmk = v != 0;
            } else {
                out->total = (int)v;
            }
        } else {
            out->malformed++;
        }
    }
    if (out->sdk >= 0 && out->sdk < 30) {
        out->system = EXIT_INFO_SYSTEM_OLD;
    } else if (out->why[0] != '\0') {
        out->system = EXIT_INFO_SYSTEM_FAILED;
    }
    return out->n;
}

void exit_info_activity_parse(const char *text, ExitInfoActivity *out)
{
    /* diag_host.c's activity file: the version's line, the note's, then
       the careful set's ("set: careful"), the low-memory stop's ("set:
       stopped ..."), a loss's (act_loss_*: "loss dump: <zip>", "loss dump
       (the driver hung): <zip>", "graphics device lost, no loss dump",
       "graphics driver hung, no loss dump"), the batches' ("compiled: ")
       and the system's ("system: ") */
    const char *p;
    int line = 0;
    memset(out, 0, sizeof *out);
    if (text == NULL) {
        return;
    }
    out->found = 1;
    for (p = text; *p != '\0';) {
        const size_t len = strcspn(p, "\r\n");
        if (line == 1) {
            snprintf(out->note, sizeof out->note, "%.*s", (int)len, p);
        } else if (line >= 2 && out->loss[0] == '\0' &&
                   (strncmp(p, "loss dump", 9) == 0 ||
                    strncmp(p, "graphics device lost", 20) == 0 ||
                    strncmp(p, "graphics driver hung", 20) == 0)) {
            snprintf(out->loss, sizeof out->loss, "%.*s", (int)len, p);
        }
        p += len;
        if (*p == '\r') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        line++;
    }
}

int exit_info_find_pid(const ExitInfoList *list, long pid)
{
    if (list == NULL || pid < 0) {
        return -1;
    }
    for (int i = 0; i < list->n; i++) {
        if (list->rec[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

void exit_info_seen_parse(const char *text, long long *lastSeenMs, char *trace, size_t size)
{
    char name[EXIT_INFO_TRACE_NAME_MAX];
    long long v = 0;
    *lastSeenMs = 0;
    if (size > 0) {
        trace[0] = '\0';
    }
    if (text == NULL) {
        return;
    }
    name[0] = '\0';
    if (sscanf(text, "%lld %95s", &v, name) >= 1 && v > 0) {
        *lastSeenMs = v;
    }
    if (exit_info_trace_name_ok(name) && size > 0) {
        snprintf(trace, size, "%s", name);
    }
}

/* --- The verdict ------------------------------------------------------------------ */

/* the parts' room (their sum with the joins, 988 bytes, is under
   EXIT_INFO_VERDICT_MAX - 1: "the previous run ended: " 24, how 191, " ("
   2, Android's 319, "); " 3, where 223, "; " 2, resets 95, "; " 2, free
   127); each part's own longest text fits its room (the precisions below) */
#define HOW_MAX 192
#define ANDROID_MAX 320
#define WHERE_MAX 224
#define RESETS_MAX 96
#define FREE_MAX 128

static int last_found(const LastRunFacts *last)
{
    return last != NULL && last->found;
}

/* our own end mark: 1 with the words */
static int how_from_mark(const LastRunFacts *last, const ExitInfoActivity *act, char *out,
                         size_t size)
{
    if (last_found(last) && last->state[LAST_RUN_END] == LAST_RUN_SLOT_READ) {
        const char *v = last->value[LAST_RUN_END];
        switch (last->end) {
        case LAST_RUN_END_CRASH:
            snprintf(out, size, "it crashed%s%.160s", strncmp(v, "crash ", 6) == 0 ? " " : "",
                     strncmp(v, "crash ", 6) == 0 ? v + 6 : "");
            return 1;
        case LAST_RUN_END_LOSS:
            snprintf(out, size, "the graphics device was lost");
            return 1;
        case LAST_RUN_END_HANG:
            snprintf(out, size, "the graphics driver stopped responding (a GPU hang)");
            return 1;
        case LAST_RUN_END_WATCHDOG:
            snprintf(out, size, "the watchdog ended it%s%.160s",
                     strncmp(v, "the watchdog: ", 14) == 0 ? ": " : "",
                     strncmp(v, "the watchdog: ", 14) == 0 ? v + 14 : "");
            return 1;
        case LAST_RUN_END_CLEAN:
            snprintf(out, size, "it was closed normally%s%.160s",
                     strncmp(v, "clean, after: ", 14) == 0 ? ", after: " : "",
                     strncmp(v, "clean, after: ", 14) == 0 ? v + 14 : "");
            return 1;
        default:
            break;
        }
    }
    if (act != NULL && act->found && act->loss[0] != '\0') {
        if (strncmp(act->loss, "loss dump (the driver hung)", 27) == 0 ||
            strncmp(act->loss, "graphics driver hung", 20) == 0) {
            snprintf(out, size, "the graphics driver stopped responding (a GPU hang)");
        } else {
            snprintf(out, size, "the graphics device was lost");
        }
        return 1;
    }
    return 0;
}

static void android_part(const ExitInfoList *list, const LastRunFacts *last, int match, char *out,
                         size_t size)
{
    if (list == NULL || list->system == EXIT_INFO_SYSTEM_NONE) {
        snprintf(out, size, "the system keeps no exit record (Android 11 and later only)");
    } else if (list->system == EXIT_INFO_SYSTEM_OLD) {
        snprintf(out, size,
                 "Android API %d keeps no exit record (Android 11, API 30, and later only)",
                 list->sdk);
    } else if (list->system == EXIT_INFO_SYSTEM_FAILED) {
        snprintf(out, size, "Android's exit records not read: %.120s", list->why);
    } else if (match >= 0) {
        const ExitInfoRecord *r = &list->rec[match];
        char when[40];
        last_run_utc_text(r->timeMs / 1000, when, sizeof when);
        snprintf(out, size,
                 "Android's record: %s (%d), status %d, %s (importance %d), pss %lld MB, rss "
                 "%lld MB, at %s%s%.64s%s",
                 exit_info_reason_name(r->reason), r->reason, r->status,
                 exit_info_importance_words(r->importance), r->importance, r->pssKb / 1024,
                 r->rssKb / 1024, when, r->description[0] != '\0' ? ", \"" : "", r->description,
                 r->description[0] != '\0' ? "\"" : "");
    } else if (last_found(last) && last->pid >= 0) {
        snprintf(out, size, "no record from Android for pid %ld; %d other new record%s", last->pid,
                 list->n, list->n == 1 ? "" : "s");
    } else {
        snprintf(out, size, "no record from Android matched (no pid to match); %d new record%s",
                 list->n, list->n == 1 ? "" : "s");
    }
}

int exit_info_verdict(const LastRunFacts *last, const ExitInfoActivity *act,
                      const ExitInfoList *list, char *out, size_t size)
{
    char how[HOW_MAX], android[ANDROID_MAX], where[WHERE_MAX], resets[RESETS_MAX], free_[FREE_MAX];
    const int found = last_found(last);
    const int actFound = act != NULL && act->found;
    const int match = list != NULL && list->system == EXIT_INFO_SYSTEM_READ && found
                          ? exit_info_find_pid(list, last->pid)
                          : -1;
    int from;
    if (size == 0) {
        return EXIT_INFO_FROM_NOTHING;
    }
    if (how_from_mark(last, act, how, sizeof how)) {
        from = EXIT_INFO_FROM_MARK;
    } else if (match >= 0) {
        exit_info_reason_words(list->rec[match].reason, list->rec[match].status, list->lmk, how,
                               sizeof how);
        from = EXIT_INFO_FROM_RECORD;
    } else if (found) {
        snprintf(how, sizeof how,
                 "without a word: no crash, device loss, watchdog or clean end was written");
        from = EXIT_INFO_FROM_FILES;
    } else if (actFound) {
        snprintf(how, sizeof how,
                 "without a word inside the start-up (its activity file was left; no "
                 "last-run.txt)");
        from = EXIT_INFO_FROM_FILES;
    } else {
        snprintf(how, sizeof how,
                 "unknown: no last-run.txt and no start-up activity file (the first start with "
                 "them, or the files were deleted)");
        from = EXIT_INFO_FROM_NOTHING;
    }
    android_part(list, last, match, android, sizeof android);
    if (found && last->state[LAST_RUN_PHASE] == LAST_RUN_SLOT_READ) {
        if (last->seconds >= 0.0) {
            snprintf(where, sizeof where,
                     "it was at %.160s, %.1f s after its start at its last write",
                     last->value[LAST_RUN_PHASE], last->seconds);
        } else {
            snprintf(where, sizeof where, "it was at %.200s", last->value[LAST_RUN_PHASE]);
        }
    } else if (match >= 0 && list->rec[match].summary[0] != '\0') {
        snprintf(where, sizeof where, "it was at (its summary to Android) \"%.128s\"",
                 list->rec[match].summary);
    } else if (actFound && act->note[0] != '\0') {
        snprintf(where, sizeof where, "it was at (the start-up activity) %.180s", act->note);
    } else if (found) {
        snprintf(where, sizeof where, "where it was is not readable in last-run.txt");
    } else {
        snprintf(where, sizeof where, "where it was is not known");
    }
    if (found && last->resetsStart >= 0) {
        snprintf(resets, sizeof resets, "GPU resets %d -> %d", last->resetsStart,
                 last->resetsNow >= 0 ? last->resetsNow : last->resetsStart);
    } else if (found && last->state[LAST_RUN_RESETS] == LAST_RUN_SLOT_READ) {
        snprintf(resets, sizeof resets, "GPU resets %.80s", last->value[LAST_RUN_RESETS]);
    } else {
        snprintf(resets, sizeof resets, "GPU resets unknown");
    }
    if (found && last->freeMb >= 0) {
        char floor_[32] = "";
        if (last->floorMb > 0) {
            snprintf(floor_, sizeof floor_, ", floor %ld MB", last->floorMb);
        }
        snprintf(free_, sizeof free_, "free memory %ld MB at its last write (lowest %ld MB%s)",
                 last->freeMb, last->freeLowMb >= 0 ? last->freeLowMb : last->freeMb, floor_);
    } else {
        snprintf(free_, sizeof free_, "free memory unknown");
    }
    snprintf(out, size < EXIT_INFO_VERDICT_MAX ? size : EXIT_INFO_VERDICT_MAX,
             "the previous run ended: %s (%s); %s; %s; %s", how, android, where, resets, free_);
    return from;
}

void exit_info_record_line(const ExitInfoRecord *r, int lmk, char *out, size_t size)
{
    char words[HOW_MAX], when[40];
    exit_info_reason_words(r->reason, r->status, lmk, words, sizeof words);
    last_run_utc_text(r->timeMs / 1000, when, sizeof when);
    snprintf(out, size,
             "pid %ld, %s (%d): %s; status %d; importance %d (%s); pss %lld MB, rss %lld MB; at "
             "%s (%lld ms); trace %.95s; description \"%.160s\"; summary \"%.128s\"",
             r->pid, exit_info_reason_name(r->reason), r->reason, words, r->status, r->importance,
             exit_info_importance_words(r->importance), r->pssKb / 1024, r->rssKb / 1024, when,
             r->timeMs, r->trace[0] != '\0' ? r->trace : "none", r->description, r->summary);
}

/* --- The start's part ------------------------------------------------------------- */

static char s_verdict[EXIT_INFO_VERDICT_MAX];

const char *exit_info_last_verdict(void)
{
    return s_verdict;
}

#ifndef __ANDROID__
int exit_info_system_records(const char *logsDir, long long lastSeenMs, char *out, size_t size)
{
    /* the tests' records: the file ICO_TEST_EXIT_INFO names (TESTING.md) */
    const char *path = getenv("ICO_TEST_EXIT_INFO");
    FILE *f;
    size_t n;
    (void)logsDir;
    (void)lastSeenMs;
    if (size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    f = ico_fopen(path, "rb");
    if (f == NULL) {
        snprintf(out, size, "why\tICO_TEST_EXIT_INFO's file could not be opened\n");
        return -1;
    }
    n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = '\0';
    return 0;
}
#endif

/* dir + "/" + name: 0, or -1 when it does not fit (host_config.c's
   ico_path_join is not in this library) */
static int join(char *out, size_t size, const char *dir, const char *name)
{
    const size_t n = strlen(dir);
    const int slash = n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\';
    const int c = snprintf(out, size, "%s%s%s", dir, slash ? "/" : "", name);
    return c > 0 && (size_t)c < size ? 0 : -1;
}

/* a whole small file into buf (NUL-ended): its bytes, -1 none */
static long read_small(const char *path, char *buf, size_t size)
{
    FILE *f = ico_fopen(path, "rb");
    size_t n;
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

void exit_info_begin(const char *logsDir, const char *activityPath)
{
    static char text[EXIT_INFO_TEXT_MAX];
    static char buf[LAST_RUN_BYTES + 1];
    static char actText[8192];
    static char line[EXIT_INFO_LINE_MAX];
    static char trace[EXIT_INFO_TRACE_NAME_MAX];
    static char path[1100], cur[1100], prev[1100];
    static LastRunFacts last;
    static ExitInfoList list;
    static ExitInfoActivity act;
    long long seen = 0;
    long n;
    int sys, haveLast = 0, haveAct = 0;
    FILE *f;

    s_verdict[0] = '\0';
    if (logsDir == NULL || activityPath == NULL ||
        join(path, sizeof path, logsDir, "exit-info-seen.txt") != 0) {
        return;
    }
    /* the last seen record and the newest trace kept */
    if (read_small(path, actText, sizeof actText) >= 0) {
        exit_info_seen_parse(actText, &seen, trace, sizeof trace);
    } else {
        trace[0] = '\0';
    }
    sys = exit_info_system_records(logsDir, seen, text, sizeof text);
    (void)exit_info_parse(sys == 0 || text[0] != '\0' ? text : NULL, seen, &list);
    /* last-run.txt and the activity file, before this run's own */
    if (last_run_paths(activityPath, cur, prev, sizeof cur) == 0 &&
        (n = read_small(cur, buf, sizeof buf)) >= 0) {
        (void)last_run_parse(buf, (size_t)n, &last);
        haveLast = 1;
    }
    if (read_small(activityPath, actText, sizeof actText) >= 0) {
        exit_info_activity_parse(actText, &act);
        haveAct = 1;
    }
    (void)exit_info_verdict(haveLast ? &last : NULL, haveAct ? &act : NULL, &list, s_verdict,
                            sizeof s_verdict);
    ico_diag_log("ico_pc: %s", s_verdict);
    /* the newest trace copied this start, else the one kept before */
    for (int i = 0; i < list.n; i++) {
        if (exit_info_trace_name_ok(list.rec[i].trace)) {
            snprintf(trace, sizeof trace, "%s", list.rec[i].trace);
            break;
        }
    }
    if (list.system == EXIT_INFO_SYSTEM_READ) {
        ico_diag_log("ico_pc: Android's exit records: %d new since the last read (%d held in all, "
                     "%d older, %d past the %d kept, %d lines not read); low-memory kills "
                     "reported as such: %s",
                     list.n, list.total, list.older, list.beyond, EXIT_INFO_MAX_RECORDS,
                     list.malformed,
                     list.lmk == 1   ? "yes"
                     : list.lmk == 0 ? "no (they show as signal 9)"
                                     : "not said");
        for (int i = 0; i < list.n; i++) {
            exit_info_record_line(&list.rec[i], list.lmk, line, sizeof line);
            ico_diag_log("ico_pc: Android's exit record %d of %d: %s", i + 1, list.n, line);
        }
    }
    /* logs/exit-info.txt: this start's read */
    if (join(cur, sizeof cur, logsDir, "exit-info.txt") == 0 &&
        (f = ico_fopen(cur, "wb")) != NULL) {
        char now[40];
        last_run_utc_text((long long)time(NULL), now, sizeof now);
        fprintf(f, "ico-pc exit info: read at the start of %s\n%s\n", now, s_verdict);
        fprintf(f,
                "the system: %s; API %d; %d held, %d new, %d older, %d past the %d kept, %d "
                "lines not read; low-memory kills reported as such: %d (-1 not said)%s%s\n",
                list.system == EXIT_INFO_SYSTEM_READ     ? "records read"
                : list.system == EXIT_INFO_SYSTEM_OLD    ? "no exit records before Android 11"
                : list.system == EXIT_INFO_SYSTEM_FAILED ? "records not read"
                                                         : "no exit records on this system",
                list.sdk, list.total, list.n, list.older, list.beyond, EXIT_INFO_MAX_RECORDS,
                list.malformed, list.lmk, list.why[0] != '\0' ? "; why: " : "", list.why);
        for (int i = 0; i < list.n; i++) {
            exit_info_record_line(&list.rec[i], list.lmk, line, sizeof line);
            fprintf(f, "record %d of %d: %s\n", i + 1, list.n, line);
        }
        fprintf(f, "the newest trace kept: %s\n", trace[0] != '\0' ? trace : "none");
        fclose(f);
    }
    /* the newest record seen: the next start reads only newer ones */
    if (list.system == EXIT_INFO_SYSTEM_READ && list.n > 0 && (f = ico_fopen(path, "wb")) != NULL) {
        fprintf(f, "%lld %s\n", list.newestMs, trace[0] != '\0' ? trace : "-");
        fclose(f);
    }
    ico_report_set_exit_info(s_verdict, trace);
}
