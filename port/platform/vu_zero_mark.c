/*
 * port/platform/vu_zero_mark.c: the zero-area cull's mark on the install
 * (vu_zero_mark.h says what and why).
 *
 *   ico_pc: the previous run: zero-area cull: was being tried when it ended
 *     (a crash): off on this install until the version changes (blocked
 *     v0.11.14)
 *   ico_pc: the previous run: zero-area cull: on when the watchdog ended it:
 *     counted as a lost start (the loss ladder)
 */
#include "vu_zero_mark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "last_run.h"

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
        return "no end written: killed or cut off";
    default:
        return "its end not read";
    }
}

void vu_zero_mark_decide(const char *mark, int prevEnd, const char *version, char *out,
                         size_t outSize, char *line, size_t lineSize, int *block, int *lossRow)
{
    const char *v = version != NULL ? version : "?";
    *block = 0;
    *lossRow = 0;
    out[0] = '\0';
    if (mark == NULL || mark[0] == '\0') {
        snprintf(line, lineSize, "the previous run: zero-area cull: not tried");
        return;
    }
    if (strncmp(mark, "trying", 6) == 0) {
        if (prevEnd == LAST_RUN_END_CLEAN) {
            snprintf(line, lineSize,
                     "the previous run: zero-area cull: being tried when it was closed normally "
                     "(tried again)");
            return;
        }
        snprintf(out, outSize, "blocked %s", v);
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: was being tried when it ended (%s): off on "
                 "this install until the version changes (blocked %s)",
                 endWords(prevEnd), v);
        *block = 1;
        return;
    }
    if (strncmp(mark, "failed", 6) == 0) {
        snprintf(out, outSize, "blocked %s", v);
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: refused by its self-test or a check: off on "
                 "this install until the version changes (blocked %s)",
                 v);
        *block = 1;
        return;
    }
    if (strncmp(mark, "blocked ", 8) == 0) {
        char was[160];
        snprintf(was, sizeof was, "%.159s", mark + 8);
        was[strcspn(was, "\r\n")] = '\0';
        if (strcmp(was, v) == 0) {
            snprintf(out, outSize, "blocked %s", v);
            snprintf(line, lineSize,
                     "the previous run: zero-area cull: off on this install since a run of %s "
                     "ended while it was tried or it was refused (blocked %s)",
                     v, v);
            *block = 1;
        } else {
            snprintf(line, lineSize,
                     "the previous run: zero-area cull: blocked on %s, this is %s: tried again",
                     was, v);
        }
        return;
    }
    if (strncmp(mark, "on", 2) == 0) {
        if (prevEnd == LAST_RUN_END_WATCHDOG) {
            *lossRow = 1;
            snprintf(line, lineSize,
                     "the previous run: zero-area cull: on when the watchdog ended it: counted as "
                     "a lost start (the loss ladder)");
        } else {
            snprintf(line, lineSize, "the previous run: zero-area cull: on (it ended: %s)",
                     endWords(prevEnd));
        }
        return;
    }
    snprintf(line, lineSize, "the previous run: zero-area cull: its mark unread (\"%.40s\")", mark);
}

static void exportEnv(const char *name, const char *value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL && value[0] != '\0') {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

int vu_zero_mark_begin(const char *activityPath, const char *version, void (*log)(const char *line))
{
    char path[1024], cur[1024], prev[1024];
    size_t dir = activityPath != NULL ? strlen(activityPath) : 0;
    while (dir > 0 && activityPath[dir - 1] != '/' && activityPath[dir - 1] != '\\') {
        dir--;
    }
    if (activityPath == NULL || snprintf(path, sizeof path, "%.*svu-zero-mark.txt", (int)dir,
                                         activityPath) >= (int)sizeof path) {
        return 0;
    }
    char mark[192] = "";
    FILE *f = ico_fopen(path, "rb");
    if (f != NULL) {
        const size_t k = fread(mark, 1, sizeof mark - 1, f);
        mark[k] = '\0';
        fclose(f);
    }
    /* the previous run's end: its last-run.txt, kept by last_run_begin */
    int prevEnd = -1;
    if (last_run_paths(activityPath, cur, prev, sizeof cur) == 0) {
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
    char out[192], line[400];
    int block = 0, lossRow = 0;
    vu_zero_mark_decide(mark, prevEnd, version, out, sizeof out, line, sizeof line, &block,
                        &lossRow);
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
    if (mark[0] != '\0' && log != NULL) {
        log(line); /* a start that never tried it says nothing */
    }
    exportEnv("ICO_VU_ZERO_MARK", path);
    exportEnv("ICO_VU_ZERO_BLOCK",
              block ? line + strlen("the previous run: zero-area cull: ") : NULL);
    return lossRow;
}
