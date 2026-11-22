/*
 * port/platform/nodiscard_mark.c: the discard-free twins' mark on the
 * install (nodiscard_mark.h says what and why; the stages and the table
 * are vu_zero_mark.c's).
 *
 *   ico_pc: the previous run: discard-free twins: were being tried when it
 *     ended (a crash): off on this install until the version changes
 *     (blocked v0.11.15)
 *   ico_pc: the previous run: discard-free twins: were on, their first
 *     pictures being counted, when it ended (Android: closed from the recent
 *     apps list or with Force stop (REASON_USER_REQUESTED)): no sign of a
 *     bad end: tried again
 */
#include "nodiscard_mark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"
#include "vu_zero_mark.h"

static const char kHead[] = "the previous run: discard-free twins: ";

/* the renderer's second line, "clipper clean|not clean <v> (...)" (its
   start-up clipper check, port/render/rd_nodiscard.h), as "; their clipper
   check: ..." for the line (said, never a reason to block) */
static void clipperWords(const char *mark, char *out, size_t size)
{
    const char *c = mark != NULL ? strstr(mark, "\nclipper ") : NULL;
    out[0] = '\0';
    if (c != NULL) {
        c += 9;
        const int n = (int)strcspn(c, "\r\n");
        snprintf(out, size, "; their clipper check: %.*s", n > 200 ? 200 : n, c);
    }
}

void nodiscard_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                           char *out, size_t outSize, char *line, size_t lineSize, int *block)
{
    const char *v = version != NULL && version[0] != '\0' ? version : "?";
    char ver[64], what[200];
    const int stage = vu_zero_mark_parse(mark, ver, sizeof ver);
    const char *mv = ver[0] != '\0' ? ver : v;
    const char *ends = endWords != NULL ? endWords : "?";
    *block = 0;
    out[0] = '\0';
    switch (stage) {
    case VU_ZERO_STAGE_NONE:
        snprintf(line, lineSize, "%snot tried", kHead);
        return;
    case VU_ZERO_STAGE_TRYING:
        snprintf(what, sizeof what, "were being tried when it ended (%s)", ends);
        break;
    case VU_ZERO_STAGE_COUNTING:
        snprintf(what, sizeof what,
                 "were on, their first pictures being counted, when it ended (%s)", ends);
        break;
    case VU_ZERO_STAGE_ON:
        snprintf(what, sizeof what, "were on when it ended (%s)", ends);
        break;
    case VU_ZERO_STAGE_FAILED:
        snprintf(what, sizeof what, "were refused by their self-test or a pipeline not made");
        break;
    case VU_ZERO_STAGE_BLOCKED:
        snprintf(what, sizeof what, "kept off by their mark");
        break;
    default:
        snprintf(what, sizeof what, "their mark unread (\"%.*s\") when it ended (%s)",
                 (int)strcspn(mark, "\r\n") > 40 ? 40 : (int)strcspn(mark, "\r\n"), mark, ends);
        break;
    }
    const int bad = vu_zero_mark_blocks(stage, endKind);
    char clip[256];
    clipperWords(mark, clip, sizeof clip);
    if (bad && strcmp(mv, v) == 0) {
        snprintf(out, outSize, "blocked %s", v);
        snprintf(line, lineSize,
                 "%s%s: off on this install until the version changes (blocked %s)%s", kHead, what,
                 v, clip);
        *block = 1;
    } else if (bad) {
        /* an update since: the block was the version that ran then */
        snprintf(line, lineSize, "%s%s in %s, this is %s: tried again%s", kHead, what, mv, v, clip);
    } else {
        snprintf(line, lineSize, "%s%s: no sign of a bad end: tried again%s", kHead, what, clip);
    }
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

int nodiscard_mark_step(const char *activityPath, const char *version, int exitReason,
                        int exitStatus, void (*log)(const char *line))
{
    char path[1024], cur[1024], prev[1024];
    size_t dir = activityPath != NULL ? strlen(activityPath) : 0;
    while (dir > 0 && activityPath[dir - 1] != '/' && activityPath[dir - 1] != '\\') {
        dir--;
    }
    if (activityPath == NULL || snprintf(path, sizeof path, "%.*snodiscard-mark.txt", (int)dir,
                                         activityPath) >= (int)sizeof path) {
        return 0;
    }
    char mark[512] = "";
    FILE *f = ico_fopen(path, "rb");
    if (f != NULL) {
        const size_t k = fread(mark, 1, sizeof mark - 1, f);
        mark[k] = '\0';
        fclose(f);
    }
    /* the previous run's end: its last-run.txt, kept by last_run_begin */
    int prevEnd = -1;
    static char endText[LAST_RUN_VALUE_MAX];
    endText[0] = '\0';
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
                snprintf(endText, sizeof endText, "%s", facts.value[LAST_RUN_END]);
            }
        }
    }
    const int kind = vu_zero_mark_end_kind(prevEnd, endText, exitReason, exitStatus);
    char words[160], out[192], line[768];
    int block = 0;
    vu_zero_mark_end_words(kind, exitReason, exitStatus, words, sizeof words);
    nodiscard_mark_decide(mark, kind, words, version, out, sizeof out, line, sizeof line, &block);
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
        log(line); /* a start that never tried them says nothing */
    }
    exportEnv("ICO_NODISCARD_MARK", path);
    exportEnv("ICO_NODISCARD_VERSION", version != NULL && version[0] != '\0' ? version : NULL);
    exportEnv("ICO_NODISCARD_BLOCK", block ? line + strlen(kHead) : NULL);
    return block;
}

int nodiscard_mark_begin(const char *activityPath, const char *version,
                         void (*log)(const char *line))
{
    int status = 0;
    const int reason = exit_info_last_reason(&status);
    return nodiscard_mark_step(activityPath, version, reason, status, log);
}
