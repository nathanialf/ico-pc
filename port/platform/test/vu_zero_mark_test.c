/*
 * port/platform/test/vu_zero_mark_test.c: the zero-area cull's mark on the
 * install (vu_zero_mark.c, issue 179).
 *   decide  every mark against every previous end: trying after a crash, a
 *           device loss, a GPU hang, the watchdog, no end written or no file:
 *           blocked for this version; trying after a clean end: removed,
 *           tried again; failed: blocked; blocked on this version: kept, off;
 *           on another: removed, tried again; on after the watchdog: a lost
 *           start, else nothing; no mark: nothing; each line word for word
 *           where it matters
 *   begin   the start's step on files in the given folder: a previous run's
 *           last-run-previous.txt ending in a crash and a "trying" mark: the
 *           mark rewritten "blocked v0.11.14", ICO_VU_ZERO_BLOCK and
 *           ICO_VU_ZERO_MARK exported, one line; the next start keeps it; a
 *           start of another version removes it and exports no block; the
 *           watchdog with "on" returns the lost start
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "last_run.h"
#include "vu_zero_mark.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static char s_lines[4096];

static void logLine(const char *line)
{
    strncat(s_lines, line, sizeof s_lines - strlen(s_lines) - 2);
    strcat(s_lines, "\n");
}

static void decide(void)
{
    static const int kBad[] = {LAST_RUN_END_CRASH,    LAST_RUN_END_LOSS,    LAST_RUN_END_HANG,
                               LAST_RUN_END_WATCHDOG, LAST_RUN_END_RUNNING, -1};
    char out[96], line[320];
    int block, loss;
    for (size_t i = 0; i < sizeof kBad / sizeof kBad[0]; i++) {
        vu_zero_mark_decide("trying\n", kBad[i], "v0.11.14", out, sizeof out, line, sizeof line,
                            &block, &loss);
        CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1 && loss == 0);
        CHECK(strstr(line, "the previous run: zero-area cull: was being tried when it ended (") ==
              line);
        CHECK(strstr(line, "): off on this install until the version changes (blocked "
                           "v0.11.14)") != NULL);
    }
    vu_zero_mark_decide("trying", LAST_RUN_END_CLEAN, "v0.11.14", out, sizeof out, line,
                        sizeof line, &block, &loss);
    CHECK(out[0] == '\0' && block == 0 && loss == 0);
    CHECK(strcmp(line, "the previous run: zero-area cull: being tried when it was closed normally "
                       "(tried again)") == 0);
    vu_zero_mark_decide("failed", LAST_RUN_END_CLEAN, "v0.11.14", out, sizeof out, line,
                        sizeof line, &block, &loss);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    vu_zero_mark_decide("blocked v0.11.14\n", LAST_RUN_END_CLEAN, "v0.11.14", out, sizeof out, line,
                        sizeof line, &block, &loss);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    vu_zero_mark_decide("blocked v0.11.14", LAST_RUN_END_CLEAN, "v0.11.15", out, sizeof out, line,
                        sizeof line, &block, &loss);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: zero-area cull: blocked on v0.11.14, this is v0.11.15: "
                       "tried again") == 0);
    vu_zero_mark_decide("on", LAST_RUN_END_WATCHDOG, "v0.11.14", out, sizeof out, line, sizeof line,
                        &block, &loss);
    CHECK(out[0] == '\0' && block == 0 && loss == 1);
    CHECK(strcmp(line, "the previous run: zero-area cull: on when the watchdog ended it: counted "
                       "as a lost start (the loss ladder)") == 0);
    vu_zero_mark_decide("on", LAST_RUN_END_CRASH, "v0.11.14", out, sizeof out, line, sizeof line,
                        &block, &loss);
    CHECK(block == 0 && loss == 0);
    vu_zero_mark_decide(NULL, LAST_RUN_END_CRASH, "v0.11.14", out, sizeof out, line, sizeof line,
                        &block, &loss);
    CHECK(out[0] == '\0' && block == 0 && loss == 0);
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

static void begin(const char *dir)
{
    char act[1024], prev[1024], mark[1024], got[256];
    snprintf(act, sizeof act, "%s/vu_zero_mark_activity.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    snprintf(mark, sizeof mark, "%s/vu-zero-mark.txt", dir);
    /* a previous run that crashed while trying */
    const char *values[LAST_RUN_SLOTS] = {NULL};
    values[LAST_RUN_HEADER] = LAST_RUN_FORMAT;
    values[LAST_RUN_END] = "crash (signal 11)";
    static char text[LAST_RUN_BYTES + 1];
    last_run_text(values, text);
    text[LAST_RUN_BYTES] = '\0';
    writeText(prev, text);
    writeText(mark, "trying\n");
    s_lines[0] = '\0';
    CHECK(vu_zero_mark_begin(act, "v0.11.14", logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0);
    CHECK(getenv("ICO_VU_ZERO_BLOCK") != NULL &&
          strstr(getenv("ICO_VU_ZERO_BLOCK"), "was being tried when it ended (a crash)") != NULL);
    CHECK(getenv("ICO_VU_ZERO_MARK") != NULL && strcmp(getenv("ICO_VU_ZERO_MARK"), mark) == 0);
    CHECK(strstr(s_lines, "the previous run: zero-area cull: was being tried when it ended (a "
                          "crash)") != NULL);
    /* the next start of the same version: still off */
    CHECK(vu_zero_mark_begin(act, "v0.11.14", logLine) == 0);
    CHECK(getenv("ICO_VU_ZERO_BLOCK") != NULL);
    /* a start of another version: removed, tried again */
    CHECK(vu_zero_mark_begin(act, "v0.11.15", logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    CHECK(getenv("ICO_VU_ZERO_BLOCK") == NULL || getenv("ICO_VU_ZERO_BLOCK")[0] == '\0');
    /* the watchdog with the cull on: a lost start */
    values[LAST_RUN_END] = "the watchdog: the game thread stopped for 10.0 s";
    last_run_text(values, text);
    writeText(prev, text);
    writeText(mark, "on\n");
    CHECK(vu_zero_mark_begin(act, "v0.11.15", logLine) == 1);
    printf("%s", s_lines);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: vu_zero_mark_test <folder>\n");
        return 2;
    }
    decide();
    begin(argv[1]);
    if (fails != 0) {
        printf("vu_zero_mark_test: %d failures\n", fails);
        return 1;
    }
    printf("vu_zero_mark_test: ok\n");
    return 0;
}
