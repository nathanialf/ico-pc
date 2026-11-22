/*
 * port/platform/vu_zero_mark.c: the zero-area cull's mark on the install
 * (vu_zero_mark.h says what and why).
 *
 *   ico_pc: the previous run: zero-area cull: was being tried when it ended
 *     (no end written and no record from Android): off on this install
 *     until the version changes (blocked v0.11.14)
 *   ico_pc: the previous run: zero-area cull: was on, its first pictures
 *     being counted, when it ended (Android: closed from the recent apps
 *     list or with Force stop (REASON_USER_REQUESTED)): no sign of a bad end:
 *     tried again
 *   ico_pc: the previous run: zero-area cull: was on when it ended (a
 *     crash) in v0.11.14, this is v0.11.15: tried again
 */
#include "vu_zero_mark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"

/* last-run.txt's end words (last_run.c s_end_words) after "clean, after: " */
static int endAfterClean(const char *text)
{
    static const char kAfter[] = "clean, after: ";
    if (text == NULL || strncmp(text, kAfter, sizeof kAfter - 1) != 0) {
        return VU_ZERO_END_CLEAN;
    }
    text += sizeof kAfter - 1;
    if (strncmp(text, "crash", 5) == 0) {
        return VU_ZERO_END_CRASH;
    }
    if (strncmp(text, "graphics device lost", 20) == 0) {
        return VU_ZERO_END_LOSS;
    }
    if (strncmp(text, "the graphics driver stopped responding", 38) == 0) {
        return VU_ZERO_END_HANG;
    }
    if (strncmp(text, "the watchdog", 12) == 0) {
        return VU_ZERO_END_WATCHDOG;
    }
    return VU_ZERO_END_CLEAN;
}

int vu_zero_mark_end_kind(int lastEnd, const char *lastEndText, int exitReason, int exitStatus)
{
    /* our own end mark first (exit_info.h's verdict's order) */
    switch (lastEnd) {
    case LAST_RUN_END_CLEAN:
        return endAfterClean(lastEndText);
    case LAST_RUN_END_CRASH:
        return VU_ZERO_END_CRASH;
    case LAST_RUN_END_LOSS:
        return VU_ZERO_END_LOSS;
    case LAST_RUN_END_HANG:
        return VU_ZERO_END_HANG;
    case LAST_RUN_END_WATCHDOG:
        return VU_ZERO_END_WATCHDOG;
    default:
        break; /* running (no end written), not read */
    }
    switch (exitReason) {
    case EXIT_INFO_REASON_CRASH:
    case EXIT_INFO_REASON_CRASH_NATIVE:
        return VU_ZERO_END_A_CRASH;
    case EXIT_INFO_REASON_ANR:
        return VU_ZERO_END_A_ANR;
    case EXIT_INFO_REASON_SIGNALED:
        /* SIGKILL is never raised by a fault: the system's kill (a low-memory
           one where the device does not report those as such) or a user's */
        return exitStatus == 9 ? VU_ZERO_END_A_KILLED : VU_ZERO_END_A_SIGNAL;
    case EXIT_INFO_REASON_USER_REQUESTED:
    case EXIT_INFO_REASON_USER_STOPPED:
        return VU_ZERO_END_A_USER;
    case EXIT_INFO_REASON_LOW_MEMORY:
    case EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE:
    case EXIT_INFO_REASON_FREEZER:
    case EXIT_INFO_REASON_PERMISSION_CHANGE:
    case EXIT_INFO_REASON_DEPENDENCY_DIED:
    case EXIT_INFO_REASON_INITIALIZATION_FAILURE:
    case EXIT_INFO_REASON_OTHER:
        return VU_ZERO_END_A_SYSTEM;
    case EXIT_INFO_REASON_PACKAGE_UPDATED:
    case EXIT_INFO_REASON_PACKAGE_STATE_CHANGE:
        return VU_ZERO_END_A_UPDATE;
    case EXIT_INFO_REASON_EXIT_SELF:
        return VU_ZERO_END_A_SELF;
    default:
        return VU_ZERO_END_NONE; /* no record, REASON_UNKNOWN, a newer reason */
    }
}

int vu_zero_mark_blocks(int stage, int endKind)
{
    switch (stage) {
    case VU_ZERO_STAGE_TRYING:
    case VU_ZERO_STAGE_UNREAD: /* taken as trying: the safe direction */
        return endKind != VU_ZERO_END_CLEAN;
    case VU_ZERO_STAGE_COUNTING:
    case VU_ZERO_STAGE_ON:
        return endKind == VU_ZERO_END_CRASH || endKind == VU_ZERO_END_LOSS ||
               endKind == VU_ZERO_END_HANG || endKind == VU_ZERO_END_WATCHDOG ||
               endKind == VU_ZERO_END_A_CRASH || endKind == VU_ZERO_END_A_ANR ||
               endKind == VU_ZERO_END_A_SIGNAL;
    case VU_ZERO_STAGE_FAILED:
    case VU_ZERO_STAGE_BLOCKED:
        return 1;
    default:
        return 0;
    }
}

int vu_zero_mark_blocks_beside(int stage, int endKind, int otherStage)
{
    if ((stage == VU_ZERO_STAGE_COUNTING || stage == VU_ZERO_STAGE_ON) &&
        otherStage == VU_ZERO_STAGE_TRYING) {
        return 0; /* the other attempt was trying at that end: its end */
    }
    return vu_zero_mark_blocks(stage, endKind);
}

void vu_zero_mark_end_words(int endKind, int exitReason, int exitStatus, char *out, size_t size)
{
    const char *name = exit_info_reason_name(exitReason);
    switch (endKind) {
    case VU_ZERO_END_CLEAN:
        snprintf(out, size, "closed normally");
        break;
    case VU_ZERO_END_CRASH:
        snprintf(out, size, "a crash");
        break;
    case VU_ZERO_END_LOSS:
        snprintf(out, size, "a lost graphics device");
        break;
    case VU_ZERO_END_HANG:
        snprintf(out, size, "a GPU hang");
        break;
    case VU_ZERO_END_WATCHDOG:
        snprintf(out, size, "the watchdog");
        break;
    case VU_ZERO_END_A_CRASH:
        snprintf(out, size, "Android: a crash (%s)", name);
        break;
    case VU_ZERO_END_A_ANR:
        snprintf(out, size, "Android: not responding (%s)", name);
        break;
    case VU_ZERO_END_A_SIGNAL:
        snprintf(out, size, "Android: signal %d (%s)", exitStatus, name);
        break;
    case VU_ZERO_END_A_KILLED:
        snprintf(out, size, "Android: killed by the system or a user (signal 9, %s)", name);
        break;
    case VU_ZERO_END_A_USER:
        snprintf(out, size, "Android: closed from the recent apps list or with Force stop (%s)",
                 name);
        break;
    case VU_ZERO_END_A_SYSTEM:
        snprintf(out, size, "Android: ended by the system (%s)", name);
        break;
    case VU_ZERO_END_A_UPDATE:
        snprintf(out, size, "Android: the app was updated (%s)", name);
        break;
    case VU_ZERO_END_A_SELF:
        snprintf(out, size, "Android: the app ended itself (%s)", name);
        break;
    default:
        snprintf(out, size, "no end written and no record from Android");
        break;
    }
}

int vu_zero_mark_parse(const char *mark, char *ver, size_t verSize)
{
    static const struct {
        const char *word;
        int stage;
    } kStage[] = {{"trying", VU_ZERO_STAGE_TRYING},
                  {"counting", VU_ZERO_STAGE_COUNTING},
                  {"on", VU_ZERO_STAGE_ON},
                  {"failed", VU_ZERO_STAGE_FAILED},
                  {"blocked", VU_ZERO_STAGE_BLOCKED}};

    ver[0] = '\0';
    if (mark == NULL || mark[0] == '\0' || mark[0] == '\r' || mark[0] == '\n') {
        return VU_ZERO_STAGE_NONE;
    }
    for (size_t i = 0; i < sizeof kStage / sizeof kStage[0]; i++) {
        const size_t n = strlen(kStage[i].word);
        if (strncmp(mark, kStage[i].word, n) != 0) {
            continue;
        }
        const char e = mark[n];
        if (e == ' ') {
            snprintf(ver, verSize, "%.*s", (int)strcspn(mark + n + 1, " \r\n"), mark + n + 1);
            return kStage[i].stage;
        }
        if (e == '\0' || e == '\r' || e == '\n') {
            return kStage[i].stage; /* no version: the running one's */
        }
    }
    return VU_ZERO_STAGE_UNREAD;
}

void vu_zero_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                         char *out, size_t outSize, char *line, size_t lineSize, int *block)
{
    vu_zero_mark_decide_beside(mark, endKind, endWords, version, VU_ZERO_STAGE_NONE, out, outSize,
                               line, lineSize, block);
}

void vu_zero_mark_decide_beside(const char *mark, int endKind, const char *endWords,
                                const char *version, int otherStage, char *out, size_t outSize,
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
        snprintf(line, lineSize, "the previous run: zero-area cull: not tried");
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
        snprintf(what, sizeof what, "was refused by its self-test or a check");
        break;
    case VU_ZERO_STAGE_BLOCKED:
        snprintf(what, sizeof what, "kept off by its mark");
        break;
    default:
        snprintf(what, sizeof what, "its mark unread (\"%.*s\") when it ended (%s)",
                 (int)strcspn(mark, "\r\n") > 40 ? 40 : (int)strcspn(mark, "\r\n"), mark, ends);
        break;
    }
    const int bad = vu_zero_mark_blocks_beside(stage, endKind, otherStage);
    if (!bad && vu_zero_mark_blocks(stage, endKind)) {
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: %s: the discard-free twins were being tried "
                 "then (their mark): tried again",
                 what);
    } else if (bad && strcmp(mv, v) == 0) {
        snprintf(out, outSize, "blocked %s", v);
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: %s: off on this install until the version "
                 "changes (blocked %s)",
                 what, v);
        *block = 1;
    } else if (bad) {
        /* an update since: the block was the version that ran then */
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: %s in %s, this is %s: tried again", what, mv,
                 v);
    } else {
        snprintf(line, lineSize,
                 "the previous run: zero-area cull: %s: no sign of a bad end: tried again", what);
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

static int s_stageRead = VU_ZERO_STAGE_NONE;

int vu_zero_mark_stage_read(void)
{
    return s_stageRead;
}

int vu_zero_mark_step(const char *activityPath, const char *version, int exitReason, int exitStatus,
                      void (*log)(const char *line))
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
    char words[160], out[192], line[512];
    int block = 0;
    /* the twins' mark at that end, before their step rewrites it, and this
       mark's own stage for their step (each blocks only its own subject) */
    char twins[512] = "", tver[64];
    char tpath[1024];
    if (snprintf(tpath, sizeof tpath, "%.*snodiscard-mark.txt", (int)dir, activityPath) <
        (int)sizeof tpath) {
        FILE *t = ico_fopen(tpath, "rb");
        if (t != NULL) {
            const size_t k = fread(twins, 1, sizeof twins - 1, t);
            twins[k] = '\0';
            fclose(t);
        }
    }
    const int twinStage =
        twins[0] != '\0' ? vu_zero_mark_parse(twins, tver, sizeof tver) : VU_ZERO_STAGE_NONE;
    s_stageRead =
        mark[0] != '\0' ? vu_zero_mark_parse(mark, tver, sizeof tver) : VU_ZERO_STAGE_NONE;
    vu_zero_mark_end_words(kind, exitReason, exitStatus, words, sizeof words);
    vu_zero_mark_decide_beside(mark, kind, words, version, twinStage, out, sizeof out, line,
                               sizeof line, &block);
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
    exportEnv("ICO_VU_ZERO_VERSION", version != NULL && version[0] != '\0' ? version : NULL);
    exportEnv("ICO_VU_ZERO_BLOCK",
              block ? line + strlen("the previous run: zero-area cull: ") : NULL);
    return block;
}

int vu_zero_mark_begin(const char *activityPath, const char *version, void (*log)(const char *line))
{
    int status = 0;
    const int reason = exit_info_last_reason(&status);
    return vu_zero_mark_step(activityPath, version, reason, status, log);
}
