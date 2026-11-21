/*
 * port/platform/ahead_mark.c: the next picture submitted ahead, its mark
 * on the install (ahead_mark.h says what and why).
 *
 *   ico_pc: the previous run: the next picture submitted ahead: in effect
 *     when it ended (a lost graphics device): off on this install until the
 *     version changes (blocked v0.11.14)
 *   ico_pc: the next picture submitted ahead: in effect on this install
 *     from here (logs/ahead-mark.txt: on v0.11.14)
 */
#include "ahead_mark.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"

#define HEAD "the previous run: the next picture submitted ahead: "

static const char *endWords(int end)
{
    switch (end) {
    case LAST_RUN_END_CLEAN:
        return "closed normally";
    case LAST_RUN_END_CRASH:
        return "a crash";
    case LAST_RUN_END_LOSS:
        return "a lost graphics device";
    case LAST_RUN_END_HANG:
        return "a GPU hang";
    case LAST_RUN_END_WATCHDOG:
        return "the watchdog";
    case LAST_RUN_END_RUNNING:
        return "no end written";
    default:
        return "its end not read";
    }
}

/* Android's reasons that say the process crashed or stopped responding */
static int androidBad(int reason)
{
    return reason == EXIT_INFO_REASON_CRASH || reason == EXIT_INFO_REASON_CRASH_NATIVE ||
           reason == EXIT_INFO_REASON_ANR;
}

int ahead_mark_bad_end(int prevEnd, int androidReason)
{
    if (prevEnd == LAST_RUN_END_LOSS || prevEnd == LAST_RUN_END_HANG ||
        prevEnd == LAST_RUN_END_WATCHDOG || prevEnd == LAST_RUN_END_CRASH) {
        return 1;
    }
    /* no end of ours: Android's record alone (a clean end is ours) */
    return prevEnd != LAST_RUN_END_CLEAN && androidBad(androidReason);
}

/* the end's words with Android's reason where it has one */
static void howText(int prevEnd, int androidReason, char *out, size_t size)
{
    if (androidReason >= 0) {
        snprintf(out, size, "%s; Android's exit reason %d%s", endWords(prevEnd), androidReason,
                 androidBad(androidReason) ? ": a crash or no response"
                                           : ": an ordinary end (closed, stopped or ended by "
                                             "the system)");
    } else {
        snprintf(out, size, "%s", endWords(prevEnd));
    }
}

/* the version a mark names after its word ("on v0.11.14\n": "v0.11.14") */
static void markVersion(const char *mark, size_t skip, char *out, size_t size)
{
    snprintf(out, size, "%.159s", mark + skip);
    out[strcspn(out, "\r\n")] = '\0';
}

int ahead_mark_decide(const char *mark, int prevEnd, int androidReason, const char *version,
                      char *out, size_t outSize, char *line, size_t lineSize)
{
    const char *v = version != NULL && version[0] != '\0' ? version : "?";
    char was[160], how[192];
    out[0] = '\0';
    line[0] = '\0';
    if (mark == NULL || mark[0] == '\0') {
        return 0; /* never in effect on this install, or cleared: nothing to say */
    }
    howText(prevEnd, androidReason, how, sizeof how);
    if (strncmp(mark, "on ", 3) == 0) {
        markVersion(mark, 3, was, sizeof was);
        if (!ahead_mark_bad_end(prevEnd, androidReason)) {
            snprintf(line, lineSize,
                     HEAD "in effect when it ended (%s): not held off (the mark cleared)", how);
            return 0;
        }
        if (strcmp(was, v) != 0) {
            snprintf(line, lineSize,
                     HEAD "in effect in a run of %s when it ended (%s); this is %s: tried again "
                          "(the mark cleared)",
                     was, how, v);
            return 0;
        }
        snprintf(out, outSize, "blocked %s", v);
        snprintf(line, lineSize,
                 HEAD "in effect when it ended (%s): off on this install until the version "
                      "changes (blocked %s)",
                 how, v);
        return 1;
    }
    if (strncmp(mark, "blocked ", 8) == 0) {
        markVersion(mark, 8, was, sizeof was);
        if (strcmp(was, v) == 0) {
            snprintf(out, outSize, "blocked %s", v);
            snprintf(line, lineSize,
                     HEAD "off on this install since a run of %s ended badly with it in effect "
                          "(blocked %s)",
                     v, v);
            return 1;
        }
        snprintf(line, lineSize, HEAD "blocked on %s, this is %s: tried again (the mark cleared)",
                 was, v);
        return 0;
    }
    markVersion(mark, 0, was, sizeof was);
    snprintf(line, lineSize, HEAD "its mark unread (\"%.40s\"): cleared", was);
    return 0;
}

/* --- The run's part ---------------------------------------------------------- */

static char s_path[1100];
static char s_version[128];
static char s_why[480];
static int s_begun, s_blocked, s_written;
static void (*s_log)(const char *line);

static int join(char *out, size_t size, const char *dir, const char *name)
{
    const size_t n = strlen(dir);
    const char *sep = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "/";
    const int w = snprintf(out, size, "%s%s%s", dir, sep, name);
    return w < 0 || (size_t)w >= size ? -1 : 0;
}

int ahead_mark_step(const char *logsDir, const char *version, int androidReason,
                    void (*log)(const char *line))
{
    char path[1100], act[1100], cur[1100], prev[1100];
    if (logsDir == NULL || join(path, sizeof path, logsDir, AHEAD_MARK_FILE) != 0 ||
        join(act, sizeof act, logsDir, "startup-activity.txt") != 0) {
        return -1;
    }
    char mark[192] = "";
    FILE *f = ico_fopen(path, "rb");
    if (f != NULL) {
        const size_t k = fread(mark, 1, sizeof mark - 1, f);
        mark[k] = '\0';
        fclose(f);
    }
    /* the previous run's end: its last-run.txt, kept by last_run_begin as
       last-run-previous.txt (the window's start, before the renderer) */
    int prevEnd = -1;
    if (last_run_paths(act, cur, prev, sizeof cur) == 0) {
        static char buf[LAST_RUN_BYTES + 1];
        static LastRunFacts facts;
        FILE *p = ico_fopen(prev, "rb");
        if (p != NULL) {
            const size_t n = fread(buf, 1, LAST_RUN_BYTES, p);
            fclose(p);
            memset(&facts, 0, sizeof facts);
            if (last_run_parse(buf, n, &facts) > 0 && facts.found) {
                prevEnd = facts.end;
            }
        }
    }
    char out[192], line[480];
    const int blocked = ahead_mark_decide(mark, prevEnd, androidReason, version, out, sizeof out,
                                          line, sizeof line);
    mark[strcspn(mark, "\r\n")] = '\0';
    if (strcmp(mark, out) != 0) {
        if (out[0] == '\0') {
            (void)ico_remove(path);
        } else {
            FILE *w = ico_fopen(path, "wb");
            if (w != NULL) {
                (void)fprintf(w, "%s\n", out);
                (void)ico_fsync(w);
                fclose(w);
            }
        }
    }
    if (line[0] != '\0' && log != NULL) {
        log(line);
    }
    return blocked;
}

const char *ahead_mark_begin(const char *logsDir, const char *version, int androidReason,
                             void (*log)(const char *line))
{
    if (__atomic_load_n(&s_begun, __ATOMIC_ACQUIRE) != 0) {
        return s_blocked ? s_why : NULL;
    }
    snprintf(s_version, sizeof s_version, "%s", version != NULL ? version : "");
    const int step = ahead_mark_step(logsDir, s_version, androidReason, log);
    s_blocked = step == 1;
    if (step < 0 || join(s_path, sizeof s_path, logsDir, AHEAD_MARK_FILE) != 0) {
        s_path[0] = '\0';
    }
    s_log = log;
    __atomic_store_n(&s_begun, 1, __ATOMIC_RELEASE); /* the path and version set */
    if (s_blocked) {
        snprintf(s_why, sizeof s_why,
                 "the install's mark, %s: blocked %s: a run of %s ended badly with it in "
                 "effect, so it stays off until the version changes",
                 AHEAD_MARK_FILE, s_version, s_version);
        return s_why;
    }
    return NULL;
}

void ahead_mark_in_effect(void)
{
    if (__atomic_load_n(&s_begun, __ATOMIC_ACQUIRE) == 0 || s_blocked || s_path[0] == '\0') {
        return;
    }
    int no = 0;
    if (!__atomic_compare_exchange_n(&s_written, &no, 1, false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return; /* once a process */
    }
    FILE *w = ico_fopen(s_path, "wb");
    int ok = 0;
    if (w != NULL) {
        ok = fprintf(w, "on %s\n", s_version) > 0;
        ok = fflush(w) == 0 && ok;
        ok = fclose(w) == 0 && ok;
    }
    if (s_log != NULL) {
        char line[1300];
        snprintf(line, sizeof line,
                 ok ? "the next picture submitted ahead: in effect on this install from here "
                      "(%s: on %s; a device loss, GPU hang, watchdog end or crash of this run "
                      "keeps it off until the version changes)"
                    : "the next picture submitted ahead: in effect, but its mark could not be "
                      "written (%s; on %s): a bad end of this run holds it off for the next "
                      "start only",
                 s_path, s_version);
        s_log(line);
    }
}
