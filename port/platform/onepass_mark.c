/*
 * port/platform/onepass_mark.c: the one-pass blend's mark on the
 * install (onepass_mark.h says what and why; the stages and the table
 * are vu_zero_mark.c's).
 *
 *   ico_pc: the previous run: one-pass blend: was being tried when it
 *     ended (a crash): off on this install until the version changes
 *     (blocked v0.11.16)
 *   ico_pc: the previous run: one-pass blend: was on, its first
 *     pictures being counted, when it ended (Android: closed from the recent
 *     apps list or with Force stop (REASON_USER_REQUESTED)): no sign of a
 *     bad end: tried again
 */
#include "onepass_mark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exit_info.h"
#include "fastps_mark.h"
#include "host_fs.h"
#include "last_run.h"
#include "nodiscard_mark.h"
#include "compact_mark.h" /* compact_mark_stage_read */
#include "vu_zero_mark.h"

static const char kHead[] = "the previous run: one-pass blend: ";

void onepass_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                         char *out, size_t outSize, char *line, size_t lineSize, int *block)
{
    onepass_mark_decide_beside(mark, endKind, endWords, version, VU_ZERO_STAGE_NONE, out, outSize,
                               line, lineSize, block);
}

void onepass_mark_decide_beside(const char *mark, int endKind, const char *endWords,
                                const char *version, int cullStage, char *out, size_t outSize,
                                char *line, size_t lineSize, int *block)
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
        snprintf(what, sizeof what, "was being tried when it ended (%s)", ends);
        break;
    case VU_ZERO_STAGE_COUNTING:
        snprintf(what, sizeof what, "was on, its first pictures being counted, when it ended (%s)",
                 ends);
        break;
    case VU_ZERO_STAGE_ON:
        snprintf(what, sizeof what, "was on when it ended (%s)", ends);
        break;
    case VU_ZERO_STAGE_FAILED:
        snprintf(what, sizeof what,
                 "was refused by its device check (nothing in use: an arm's pictures differed, "
                 "nothing checked, or the device failed)");
        break;
    case VU_ZERO_STAGE_BLOCKED:
        snprintf(what, sizeof what, "kept off by its mark");
        break;
    default:
        snprintf(what, sizeof what, "its mark unread (\"%.*s\") when it ended (%s)",
                 (int)strcspn(mark, "\r\n") > 40 ? 40 : (int)strcspn(mark, "\r\n"), mark, ends);
        break;
    }
    const int bad = vu_zero_mark_blocks_beside(stage, endKind, cullStage);
    const char *clip = "";
    if (!bad && vu_zero_mark_blocks(stage, endKind)) {
        snprintf(line, lineSize, "%s%s: %s being tried then (%s): tried again%s", kHead, what,
                 vu_zero_mark_beside_who("the zero-area cull was"),
                 strstr(vu_zero_mark_beside_who(""), "twins") != NULL ? "their mark" : "its mark",
                 clip);
    } else if (bad && strcmp(mv, v) == 0) {
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

int onepass_mark_step(const char *activityPath, const char *version, int exitReason, int exitStatus,
                      void (*log)(const char *line))
{
    char path[1024], cur[1024], prev[1024];
    size_t dir = activityPath != NULL ? strlen(activityPath) : 0;
    while (dir > 0 && activityPath[dir - 1] != '/' && activityPath[dir - 1] != '\\') {
        dir--;
    }
    if (activityPath == NULL || snprintf(path, sizeof path, "%.*sonepass-mark.txt", (int)dir,
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
    /* the cull's, the twins' and the fast pixel build's marks at that end
       as their steps read them (diag_host.c runs them first): an end while
       one of them was trying is theirs */
    int beside = vu_zero_mark_stage_read();
    vu_zero_mark_beside_words(NULL);
    if (beside != VU_ZERO_STAGE_TRYING && nodiscard_mark_stage_read() == VU_ZERO_STAGE_TRYING) {
        beside = VU_ZERO_STAGE_TRYING;
        vu_zero_mark_beside_words("the discard-free twins were");
    } else if (beside != VU_ZERO_STAGE_TRYING && fastps_mark_stage_read() == VU_ZERO_STAGE_TRYING) {
        beside = VU_ZERO_STAGE_TRYING; /* the fast pixel build's (its step ran before) */
        vu_zero_mark_beside_words("the fast pixel build was");
    } else if (beside != VU_ZERO_STAGE_TRYING &&
               compact_mark_stage_read() == VU_ZERO_STAGE_TRYING) {
        beside = VU_ZERO_STAGE_TRYING; /* the index compaction's (its step ran first) */
        vu_zero_mark_beside_words("the index compaction was");
    } else if (beside != VU_ZERO_STAGE_TRYING && vu_zero_mark_ratecard_measuring(activityPath)) {
        beside = VU_ZERO_STAGE_TRYING; /* the rate card's end (REV4R N3) */
        vu_zero_mark_beside_words("the rate card was");
    }
    onepass_mark_decide_beside(mark, kind, words, version, beside, out, sizeof out, line,
                               sizeof line, &block);
    vu_zero_mark_beside_words(NULL);
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
    exportEnv("ICO_ONEPASS_MARK", path);
    exportEnv("ICO_ONEPASS_VERSION", version != NULL && version[0] != '\0' ? version : NULL);
    exportEnv("ICO_ONEPASS_BLOCK", block ? line + strlen(kHead) : NULL);
    return block;
}

int onepass_mark_begin(const char *activityPath, const char *version, void (*log)(const char *line))
{
    int status = 0;
    const int reason = exit_info_last_reason(&status);
    return onepass_mark_step(activityPath, version, reason, status, log);
}
