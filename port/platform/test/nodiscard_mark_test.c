/*
 * port/platform/test/nodiscard_mark_test.c: the discard-free twins' mark on
 * the install (nodiscard_mark.c, issue 179; its stages and table are the
 * zero-area cull's, vu_zero_mark_test.c tests those over every end).
 *   decide  every stage at a crash (trying, counting, on, failed, blocked:
 *           blocked for the version in the mark, an update clears it), a
 *           clean close while counting (not blocked), no mark (nothing),
 *           an unread text (taken as trying), the lines word for word with
 *           the twins' own words
 *   step    the start's step on files in the given folder: its own file
 *           (nodiscard-mark.txt, never the cull's vu-zero-mark.txt); a close
 *           from the recent apps list while counting: not blocked, the mark
 *           removed; a crash while on: "blocked v0.11.15", kept at the next
 *           start of that version, removed at v0.11.16's; while trying an
 *           end without evidence: blocked, a clean close: not; a SIGKILL
 *           while on: not blocked; ICO_NODISCARD_MARK, ICO_NODISCARD_VERSION
 *           and ICO_NODISCARD_BLOCK exported; the cull's mark untouched and
 *           its environment not set; no line without a mark
 * argv[1]: a folder the test may write in (a folder of its own under the
 * build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"
#include "nodiscard_mark.h"
#include "vu_zero_mark.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static char s_lines[8192];

static void logLine(const char *line)
{
    strncat(s_lines, line, sizeof s_lines - strlen(s_lines) - 2);
    strcat(s_lines, "\n");
}

static void decide(void)
{
    char out[192], line[512];
    int block = 0;

    static const struct {
        const char *mark;
        const char *what;
    } kStage[] = {
        {"trying v0.11.15", "were being tried when it ended (a crash)"},
        {"counting v0.11.15", "were on, their first pictures being counted, when it ended (a "
                              "crash)"},
        {"on v0.11.15", "were on when it ended (a crash)"},
        {"failed v0.11.15", "were refused by their start-up self-test (a module or one of its "
                            "pipelines not made, or its pictures differed)"},
        {"blocked v0.11.15", "kept off by their mark"},
    };

    for (size_t i = 0; i < sizeof kStage / sizeof kStage[0]; i++) {
        nodiscard_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.15", out,
                              sizeof out, line, sizeof line, &block);
        char want[512];
        snprintf(want, sizeof want,
                 "the previous run: discard-free twins: %s: off on this install until the "
                 "version changes (blocked v0.11.15)",
                 kStage[i].what);
        CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
        CHECK(strcmp(line, want) == 0);
        if (strcmp(line, want) != 0) {
            printf("  got:  %s\n  want: %s\n", line, want);
        }
        /* the same mark read by the next version: tried again */
        nodiscard_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.16", out,
                              sizeof out, line, sizeof line, &block);
        CHECK(out[0] == '\0' && block == 0);
        CHECK(strstr(line, "in v0.11.15, this is v0.11.16: tried again") != NULL);
    }
    /* counting, closed cleanly: tried again */
    nodiscard_mark_decide("counting v0.11.15", VU_ZERO_END_CLEAN, "closed normally", "v0.11.15",
                          out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: discard-free twins: were on, their first pictures "
                       "being counted, when it ended (closed normally): no sign of a bad end: "
                       "tried again") == 0);
    /* no mark */
    nodiscard_mark_decide(NULL, VU_ZERO_END_CRASH, "a crash", "v0.11.15", out, sizeof out, line,
                          sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0 &&
          strcmp(line, "the previous run: discard-free twins: not tried") == 0);
    /* a text this program does not know: taken as trying */
    nodiscard_mark_decide("onward", VU_ZERO_END_NONE, "none", "v0.11.15", out, sizeof out, line,
                          sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    CHECK(strstr(line, "their mark unread (\"onward\")") != NULL);
    /* the renderer's clipper check on the second line: said, never a reason
       to block (a clean close while counting: tried again) */
    nodiscard_mark_decide("counting v0.11.15\nclipper not clean v0.11.15 (an edge on the centre "
                          "view ray at code 32 differs, the first at texel 340,256)\n",
                          VU_ZERO_END_CLEAN, "closed normally", "v0.11.15", out, sizeof out, line,
                          sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: discard-free twins: were on, their first pictures "
                       "being counted, when it ended (closed normally): no sign of a bad end: "
                       "tried again; their clipper check: not clean v0.11.15 (an edge on the "
                       "centre view ray at code 32 differs, the first at texel 340,256)") == 0);
    nodiscard_mark_decide("on v0.11.15\nclipper clean v0.11.15\n", VU_ZERO_END_CRASH, "a crash",
                          "v0.11.15", out, sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    CHECK(strstr(line, "(blocked v0.11.15); their clipper check: clean v0.11.15") != NULL);
    /* each mark blocks only its own subject: a crash while the cull's mark
       said trying is the cull's, so counting or on is tried again; trying
       still blocks, and the cull counting or on changes nothing */
    nodiscard_mark_decide_beside("counting v0.11.15", VU_ZERO_END_CRASH, "a crash", "v0.11.15",
                                 VU_ZERO_STAGE_TRYING, out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: discard-free twins: were on, their first pictures "
                       "being counted, when it ended (a crash): the zero-area cull was being "
                       "tried then (its mark): tried again") == 0);
    nodiscard_mark_decide_beside("on v0.11.15", VU_ZERO_END_LOSS, "a lost graphics device",
                                 "v0.11.15", VU_ZERO_STAGE_TRYING, out, sizeof out, line,
                                 sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    nodiscard_mark_decide_beside("trying v0.11.15", VU_ZERO_END_CRASH, "a crash", "v0.11.15",
                                 VU_ZERO_STAGE_TRYING, out, sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    nodiscard_mark_decide_beside("counting v0.11.15", VU_ZERO_END_CRASH, "a crash", "v0.11.15",
                                 VU_ZERO_STAGE_COUNTING, out, sizeof out, line, sizeof line,
                                 &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    for (int st = VU_ZERO_STAGE_NONE; st <= VU_ZERO_STAGE_UNREAD; st++) {
        for (int e = VU_ZERO_END_CLEAN; e <= VU_ZERO_END_NONE; e++) {
            const int want = (st == VU_ZERO_STAGE_COUNTING || st == VU_ZERO_STAGE_ON)
                                 ? 0
                                 : vu_zero_mark_blocks(st, e);
            CHECK(vu_zero_mark_blocks_beside(st, e, VU_ZERO_STAGE_TRYING) == want);
            CHECK(vu_zero_mark_blocks_beside(st, e, VU_ZERO_STAGE_COUNTING) ==
                  vu_zero_mark_blocks(st, e));
            CHECK(vu_zero_mark_blocks_beside(st, e, VU_ZERO_STAGE_NONE) ==
                  vu_zero_mark_blocks(st, e));
        }
    }
    /* a mark without a version: the running one's */
    nodiscard_mark_decide("on", VU_ZERO_END_LOSS, "a lost graphics device", "v0.11.15", out,
                          sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
}

static void writeText(const char *path, const char *text)
{
    FILE *f = ico_fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void readText(const char *path, char *out, size_t size)
{
    out[0] = '\0';
    FILE *f = ico_fopen(path, "rb");
    if (f != NULL) {
        const size_t n = fread(out, 1, size - 1, f);
        out[n] = '\0';
        fclose(f);
    }
}

/* the previous run's last-run-previous.txt ending in end (NULL: none) */
static void prevRun(const char *path, const char *end)
{
    if (end == NULL) {
        (void)ico_remove(path);
        return;
    }
    const char *values[LAST_RUN_SLOTS] = {NULL};
    values[LAST_RUN_HEADER] = LAST_RUN_FORMAT;
    values[LAST_RUN_END] = end;
    static char text[LAST_RUN_BYTES + 1];
    last_run_text(values, text);
    text[LAST_RUN_BYTES] = '\0';
    writeText(path, text);
}

static int blockSet(void)
{
    const char *b = getenv("ICO_NODISCARD_BLOCK");
    return b != NULL && b[0] != '\0';
}

static void step(const char *dir)
{
    char act[1024], prev[1024], mark[1024], zero[1024], got[256];
    snprintf(act, sizeof act, "%s/nodiscard_mark_activity.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    snprintf(mark, sizeof mark, "%s/nodiscard-mark.txt", dir);
    snprintf(zero, sizeof zero, "%s/vu-zero-mark.txt", dir);
    (void)ico_remove(zero);

    /* no mark: nothing said, nothing written, the path and the version
       exported, no block */
    (void)ico_remove(mark);
    prevRun(prev, "crash (signal 11)");
    s_lines[0] = '\0';
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 0);
    CHECK(s_lines[0] == '\0');
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());
    CHECK(getenv("ICO_NODISCARD_MARK") != NULL && strcmp(getenv("ICO_NODISCARD_MARK"), mark) == 0);
    CHECK(getenv("ICO_NODISCARD_VERSION") != NULL &&
          strcmp(getenv("ICO_NODISCARD_VERSION"), "v0.11.15") == 0);

    /* closed from the recent apps list while counting: not blocked, the
       mark removed (the self-test runs again) */
    prevRun(prev, "running");
    writeText(mark, "counting v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", EXIT_INFO_REASON_USER_REQUESTED, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());
    CHECK(strstr(s_lines, "the previous run: discard-free twins: were on, their first pictures "
                          "being counted, when it ended (Android: closed from the recent apps "
                          "list or with Force stop (REASON_USER_REQUESTED)): no sign of a bad "
                          "end: tried again") != NULL);

    /* a crash while on: blocked for this version, at the next start too,
       not in the next version */
    prevRun(prev, "crash (signal 11)");
    writeText(mark, "on v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.15\n") == 0);
    CHECK(blockSet() && strcmp(getenv("ICO_NODISCARD_BLOCK"),
                               "were on when it ended (a crash): off on this install until the "
                               "version changes (blocked v0.11.15)") == 0);
    prevRun(prev, "clean");
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(blockSet());
    CHECK(nodiscard_mark_step(act, "v0.11.16", -1, 0, logLine) == 0); /* an update */
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    /* trying, no evidence at all: blocked; closed cleanly: not */
    prevRun(prev, NULL);
    writeText(mark, "trying v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(strstr(s_lines, "were being tried when it ended (no end written and no record from "
                          "Android): off on this install") != NULL);
    prevRun(prev, "clean");
    writeText(mark, "trying v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 0);
    CHECK(!blockSet());

    /* on: killed by the system in the background (SIGKILL): not blocked;
       a signal other than 9: blocked */
    prevRun(prev, "running");
    writeText(mark, "on v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", EXIT_INFO_REASON_SIGNALED, 9, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    writeText(mark, "on v0.11.15\n");
    CHECK(nodiscard_mark_step(act, "v0.11.15", EXIT_INFO_REASON_SIGNALED, 6, logLine) == 1);

    /* the cull's mark is never touched, its environment never set here */
    readText(zero, got, sizeof got);
    CHECK(got[0] == '\0');
    CHECK(getenv("ICO_VU_ZERO_BLOCK") == NULL);

    /* the two steps in the start's order (diag_host.c: the cull's, then
       the twins'), each mark blocking only its own subject: a crash in the
       twins' start-up check (theirs trying, the cull's counting) blocks the
       twins, not the cull; a crash in the cull's (the cull trying, the
       twins on from before) blocks the cull, not the twins */
    prevRun(prev, "crash (signal 11)");
    writeText(zero, "counting v0.11.15\n");
    writeText(mark, "trying v0.11.15\n");
    s_lines[0] = '\0';
    CHECK(vu_zero_mark_step(act, "v0.11.15", -1, 0, logLine) == 0);
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(strstr(s_lines, "zero-area cull: was on, its first pictures being counted, when it "
                          "ended (a crash): the discard-free twins were being tried then (their "
                          "mark): tried again") != NULL);
    readText(zero, got, sizeof got);
    CHECK(got[0] == '\0');
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.15\n") == 0);
    writeText(zero, "trying v0.11.15\n");
    writeText(mark, "on v0.11.15\n");
    CHECK(vu_zero_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 0);
    CHECK(strstr(s_lines, "discard-free twins: were on when it ended (a crash): the zero-area "
                          "cull was being tried then (its mark): tried again") != NULL);
    readText(zero, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.15\n") == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    /* both counting: the end is not attributed, both blocked */
    writeText(zero, "counting v0.11.15\n");
    writeText(mark, "counting v0.11.15\n");
    CHECK(vu_zero_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    (void)ico_remove(zero);

    /* the start's own step reads Android's record from exit_info (none
       read in this process: -1), so trying with no end written blocks */
    prevRun(prev, NULL);
    writeText(mark, "trying v0.11.15\n");
    CHECK(nodiscard_mark_begin(act, "v0.11.15", logLine) == 1);
    (void)ico_remove(mark);
    (void)ico_remove(prev);
    printf("%s", s_lines);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: nodiscard_mark_test <folder>\n");
        return 2;
    }
    decide();
    step(argv[1]);
    if (fails != 0) {
        printf("nodiscard_mark_test: %d failures\n", fails);
        return 1;
    }
    printf("nodiscard_mark_test: ok\n");
    return 0;
}
