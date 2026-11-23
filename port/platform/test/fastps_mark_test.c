/*
 * port/platform/test/fastps_mark_test.c: the fast pixel build's mark on the
 * install (fastps_mark.c, issue 179; its stages and table are the zero-area
 * cull's, vu_zero_mark_test.c tests those over every end).
 *   decide  every stage at a crash (trying, counting, on, failed, blocked:
 *           blocked for the version in the mark, an update clears it), a
 *           clean close while counting (not blocked), no mark (nothing),
 *           an unread text (taken as trying), an end while another
 *           attempt's mark said trying (counting or on tried again), the
 *           lines word for word
 *   step    the start's step on files in the given folder, after the
 *           cull's and the twins' as diag_host.c runs them: its own file
 *           (fastps-mark.txt); a close from the recent apps list while
 *           counting: not blocked, the mark removed; a crash while on:
 *           "blocked v0.11.15", kept at the next start, removed at
 *           v0.11.16's; while trying an end without evidence: blocked;
 *           ICO_FASTPS_MARK, ICO_FASTPS_VERSION and ICO_FASTPS_BLOCK
 *           exported; a crash while it was trying and the cull and the
 *           twins counting: it blocked, they not (theirs judged beside its
 *           mark); a crash while the twins were trying: the twins blocked,
 *           it not; no line without a mark
 * argv[1]: a folder the test may write in (a folder of its own).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exit_info.h"
#include "fastps_mark.h"
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
        {"trying v0.11.15", "was being tried when it ended (a crash)"},
        {"counting v0.11.15", "was on, its first pictures being counted, when it ended (a crash)"},
        {"on v0.11.15", "was on when it ended (a crash)"},
        {"failed v0.11.15", "was refused by its start-up self-test (a module or one of its "
                            "pipelines not made, or its pictures differed)"},
        {"blocked v0.11.15", "kept off by its mark"},
    };

    for (size_t i = 0; i < sizeof kStage / sizeof kStage[0]; i++) {
        fastps_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.15", out,
                           sizeof out, line, sizeof line, &block);
        char want[512];
        snprintf(want, sizeof want,
                 "the previous run: fast pixel build: %s: off on this install until the version "
                 "changes (blocked v0.11.15)",
                 kStage[i].what);
        CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
        CHECK(strcmp(line, want) == 0);
        if (strcmp(line, want) != 0) {
            printf("  got:  %s\n  want: %s\n", line, want);
        }
        fastps_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.16", out,
                           sizeof out, line, sizeof line, &block);
        CHECK(out[0] == '\0' && block == 0);
        CHECK(strstr(line, "in v0.11.15, this is v0.11.16: tried again") != NULL);
    }
    fastps_mark_decide("counting v0.11.15", VU_ZERO_END_CLEAN, "closed normally", "v0.11.15", out,
                       sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: fast pixel build: was on, its first pictures being "
                       "counted, when it ended (closed normally): no sign of a bad end: tried "
                       "again") == 0);
    fastps_mark_decide(NULL, VU_ZERO_END_CRASH, "a crash", "v0.11.15", out, sizeof out, line,
                       sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0 &&
          strcmp(line, "the previous run: fast pixel build: not tried") == 0);
    fastps_mark_decide("onward", VU_ZERO_END_NONE, "none", "v0.11.15", out, sizeof out, line,
                       sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    CHECK(strstr(line, "its mark unread (\"onward\")") != NULL);
    /* an end while another attempt's mark said trying: counting or on tried
       again, trying still blocks */
    fastps_mark_decide_beside("counting v0.11.15", VU_ZERO_END_CRASH, "a crash", "v0.11.15",
                              VU_ZERO_STAGE_TRYING, out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: fast pixel build: was on, its first pictures being "
                       "counted, when it ended (a crash): the zero-area cull was being tried "
                       "then (its mark): tried again") == 0);
    fastps_mark_decide_beside("trying v0.11.15", VU_ZERO_END_CRASH, "a crash", "v0.11.15",
                              VU_ZERO_STAGE_TRYING, out, sizeof out, line, sizeof line, &block);
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
    const char *b = getenv("ICO_FASTPS_BLOCK");
    return b != NULL && b[0] != '\0';
}

/* the three steps in the start's order (diag_host.c) */
static void steps(const char *act, int *zero, int *twins, int *fast)
{
    *zero = vu_zero_mark_step(act, "v0.11.15", -1, 0, logLine);
    *twins = nodiscard_mark_step(act, "v0.11.15", -1, 0, logLine);
    *fast = fastps_mark_step(act, "v0.11.15", -1, 0, logLine);
}

static void step(const char *dir)
{
    char act[1024], prev[1024], mark[1024], zero[1024], nd[1024], got[256];
    snprintf(act, sizeof act, "%s/fastps_mark_activity.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    snprintf(mark, sizeof mark, "%s/fastps-mark.txt", dir);
    snprintf(zero, sizeof zero, "%s/vu-zero-mark.txt", dir);
    snprintf(nd, sizeof nd, "%s/nodiscard-mark.txt", dir);
    (void)ico_remove(zero);
    (void)ico_remove(nd);

    (void)ico_remove(mark);
    prevRun(prev, "crash (signal 11)");
    s_lines[0] = '\0';
    CHECK(fastps_mark_step(act, "v0.11.15", -1, 0, logLine) == 0);
    CHECK(s_lines[0] == '\0');
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());
    CHECK(getenv("ICO_FASTPS_MARK") != NULL && strcmp(getenv("ICO_FASTPS_MARK"), mark) == 0);
    CHECK(getenv("ICO_FASTPS_VERSION") != NULL &&
          strcmp(getenv("ICO_FASTPS_VERSION"), "v0.11.15") == 0);

    prevRun(prev, "running");
    writeText(mark, "counting v0.11.15\n");
    CHECK(fastps_mark_step(act, "v0.11.15", EXIT_INFO_REASON_USER_REQUESTED, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    prevRun(prev, "crash (signal 11)");
    writeText(mark, "on v0.11.15\n");
    CHECK(fastps_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.15\n") == 0);
    CHECK(blockSet() && strcmp(getenv("ICO_FASTPS_BLOCK"),
                               "was on when it ended (a crash): off on this install until the "
                               "version changes (blocked v0.11.15)") == 0);
    prevRun(prev, "clean");
    CHECK(fastps_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);
    CHECK(fastps_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    prevRun(prev, NULL);
    writeText(mark, "trying v0.11.15\n");
    CHECK(fastps_mark_step(act, "v0.11.15", -1, 0, logLine) == 1);

    /* a crash while the build was being tried, the cull and the twins
       counting: it is blocked, they are not (its mark read beside theirs) */
    int z = 0, t = 0, f = 0;
    prevRun(prev, "crash (signal 11)");
    writeText(zero, "counting v0.11.15\n");
    writeText(nd, "counting v0.11.15\n");
    writeText(mark, "trying v0.11.15\n");
    s_lines[0] = '\0';
    steps(act, &z, &t, &f);
    CHECK(z == 0 && t == 0 && f == 1);
    CHECK(strstr(s_lines, "zero-area cull: was on, its first pictures being counted, when it "
                          "ended (a crash): the fast pixel build was being tried then (its "
                          "mark): tried again") != NULL);
    CHECK(strstr(s_lines, "discard-free twins: were on, their first pictures being counted, when "
                          "it ended (a crash): the fast pixel build was being tried then (its "
                          "mark): tried again") != NULL);
    /* a crash while the twins were being tried, the build on: the twins
       blocked, it not */
    (void)ico_remove(zero);
    writeText(nd, "trying v0.11.15\n");
    writeText(mark, "on v0.11.15\n");
    s_lines[0] = '\0';
    steps(act, &z, &t, &f);
    CHECK(t == 1 && f == 0);
    CHECK(strstr(s_lines, "fast pixel build: was on when it ended (a crash): the discard-free "
                          "twins were being tried then (their mark): tried again") != NULL);
    (void)ico_remove(zero);
    (void)ico_remove(nd);
    (void)ico_remove(mark);
    (void)ico_remove(prev);
    printf("%s", s_lines);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: fastps_mark_test <folder>\n");
        return 2;
    }
    decide();
    step(argv[1]);
    if (fails != 0) {
        printf("fastps_mark_test: %d failures\n", fails);
        return 1;
    }
    printf("fastps_mark_test: ok\n");
    return 0;
}
