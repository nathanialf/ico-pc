/*
 * port/platform/test/compact_mark_test.c: the index compaction's mark on the
 * install, for its Mali-G52 form (compact_mark.c, issue 179; its stages and
 * table are the zero-area cull's, vu_zero_mark_test.c tests those over every
 * end).
 *   decide  every stage at a crash (trying, counting, on, failed, blocked:
 *           blocked for the version in the mark, an update clears it), a
 *           device loss while trying (blocked), a clean close while
 *           counting (not blocked), no mark (nothing), an unread text
 *           (taken as trying), an end while another attempt's mark said
 *           trying (counting or on tried again, trying still blocks), the
 *           lines word for word
 *   step    the start's step on files in the given folder, first as
 *           diag_host.c runs it: its own file (compact-mark.txt); no mark:
 *           nothing said, the environment exported; a close from the recent
 *           apps list while counting: not blocked, the mark removed; a
 *           device loss while trying: "blocked v0.11.16", kept at the next
 *           start, removed at v0.11.17's; ICO_COMPACT_MARK,
 *           ICO_COMPACT_VERSION and ICO_COMPACT_BLOCK exported; a crash
 *           while it was on and the twins' mark (as the previous run left
 *           it, read before their step) said trying: not blocked, the line
 *           names the twins; the same with the one-pass blend's mark and
 *           the rate card's file (a leanvs-mark.txt is not read); the cull's
 *           own step after it then blocks nothing of the compaction's; a
 *           lost device while its own test was trying with the four others
 *           counting or on: the five steps in diag_host.c's order, it
 *           blocked, none of them (each line names it)
 * argv[1]: a folder the test may write in (a folder of its own).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact_mark.h"
#include "fastps_mark.h"
#include "nodiscard_mark.h"
#include "onepass_mark.h"
#include "exit_info.h"
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

static char s_lines[8192];

static void logLine(const char *line)
{
    strncat(s_lines, line, sizeof s_lines - strlen(s_lines) - 2);
    strcat(s_lines, "\n");
}

static void same(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL: line\n  got:  %s\n  want: %s\n", got, want);
        fails++;
    }
}

static void decide(void)
{
    char out[192], line[512];
    int block = 0;

    static const struct {
        const char *mark;
        const char *what;
    } kStage[] = {
        {"trying v0.11.16", "was being tried when it ended (a crash)"},
        {"counting v0.11.16", "was on, its first pictures being counted, when it ended (a crash)"},
        {"on v0.11.16", "was on when it ended (a crash)"},
        {"failed v0.11.16", "was refused by its own start-up test (the lists read back, the "
                            "pictures or the stamps)"},
        {"blocked v0.11.16", "kept off by its mark"},
    };

    for (size_t i = 0; i < sizeof kStage / sizeof kStage[0]; i++) {
        compact_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.16",
                            VU_ZERO_STAGE_NONE, NULL, out, sizeof out, line, sizeof line, &block);
        char want[512];
        snprintf(want, sizeof want,
                 "the previous run: index compaction: %s: off on this install until the version "
                 "changes (blocked v0.11.16)",
                 kStage[i].what);
        CHECK(strcmp(out, "blocked v0.11.16") == 0 && block == 1);
        same(line, want);
        compact_mark_decide(kStage[i].mark, VU_ZERO_END_CRASH, "a crash", "v0.11.17",
                            VU_ZERO_STAGE_NONE, NULL, out, sizeof out, line, sizeof line, &block);
        CHECK(out[0] == '\0' && block == 0);
        CHECK(strstr(line, "in v0.11.16, this is v0.11.17: tried again") != NULL);
    }
    /* a device loss while trying: v0.11.6's black run would have ended so */
    compact_mark_decide("trying v0.11.16", VU_ZERO_END_LOSS, "a lost graphics device", "v0.11.16",
                        VU_ZERO_STAGE_NONE, NULL, out, sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.16") == 0 && block == 1);
    same(line, "the previous run: index compaction: was being tried when it ended (a lost "
               "graphics device): off on this install until the version changes (blocked "
               "v0.11.16)");
    compact_mark_decide("counting v0.11.16", VU_ZERO_END_CLEAN, "closed normally", "v0.11.16",
                        VU_ZERO_STAGE_NONE, NULL, out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    same(line, "the previous run: index compaction: was on, its first pictures being counted, "
               "when it ended (closed normally): no sign of a bad end: tried again");
    compact_mark_decide(NULL, VU_ZERO_END_CRASH, "a crash", "v0.11.16", VU_ZERO_STAGE_NONE, NULL,
                        out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    same(line, "the previous run: index compaction: not tried");
    compact_mark_decide("onward", VU_ZERO_END_NONE, "none", "v0.11.16", VU_ZERO_STAGE_NONE, NULL,
                        out, sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.16") == 0 && block == 1);
    CHECK(strstr(line, "its mark unread (\"onward\")") != NULL);
    /* another attempt trying at that end: counting or on tried again,
       trying still blocks */
    compact_mark_decide("counting v0.11.16", VU_ZERO_END_CRASH, "a crash", "v0.11.16",
                        VU_ZERO_STAGE_TRYING, "the discard-free twins were", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    same(line, "the previous run: index compaction: was on, its first pictures being counted, "
               "when it ended (a crash): the discard-free twins were being tried then (their "
               "mark): tried again");
    compact_mark_decide("on v0.11.16", VU_ZERO_END_CRASH, "a crash", "v0.11.16",
                        VU_ZERO_STAGE_TRYING, "the zero-area cull was", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    same(line, "the previous run: index compaction: was on when it ended (a crash): the zero-area "
               "cull was being tried then (its mark): tried again");
    compact_mark_decide("trying v0.11.16", VU_ZERO_END_CRASH, "a crash", "v0.11.16",
                        VU_ZERO_STAGE_TRYING, "the zero-area cull was", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.16") == 0 && block == 1);
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
    const char *b = getenv("ICO_COMPACT_BLOCK");
    return b != NULL && b[0] != '\0';
}

static void step(const char *dir)
{
    char act[1024], prev[1024], mark[1024], zero[1024], nd[1024], lv[1024], rc[1024], got[256];
    snprintf(act, sizeof act, "%s/compact_mark_activity.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    snprintf(mark, sizeof mark, "%s/compact-mark.txt", dir);
    snprintf(zero, sizeof zero, "%s/vu-zero-mark.txt", dir);
    snprintf(nd, sizeof nd, "%s/nodiscard-mark.txt", dir);
    snprintf(lv, sizeof lv, "%s/leanvs-mark.txt", dir);
    snprintf(rc, sizeof rc, "%s/ratecard.txt", dir);
    (void)ico_remove(zero);
    (void)ico_remove(nd);
    (void)ico_remove(lv);
    (void)ico_remove(rc);

    /* no mark: nothing said, the environment exported */
    (void)ico_remove(mark);
    prevRun(prev, "crash (signal 11)");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(s_lines[0] == '\0');
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());
    CHECK(getenv("ICO_COMPACT_MARK") != NULL && strcmp(getenv("ICO_COMPACT_MARK"), mark) == 0);
    CHECK(getenv("ICO_COMPACT_VERSION") != NULL &&
          strcmp(getenv("ICO_COMPACT_VERSION"), "v0.11.16") == 0);

    /* closed from the recent apps list while counting: tried again */
    prevRun(prev, "running");
    writeText(mark, "counting v0.11.16\n");
    CHECK(compact_mark_step(act, "v0.11.16", EXIT_INFO_REASON_USER_REQUESTED, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    /* a device loss while trying: blocked for the version, kept, cleared
       by an update */
    prevRun(prev, "graphics device lost");
    writeText(mark, "trying v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.16\n") == 0);
    CHECK(blockSet() && strcmp(getenv("ICO_COMPACT_BLOCK"),
                               "was being tried when it ended (a lost graphics device): off on "
                               "this install until the version changes (blocked v0.11.16)") == 0);
    CHECK(strstr(s_lines, "the previous run: index compaction: was being tried when it ended (a "
                          "lost graphics device)") != NULL);
    prevRun(prev, "clean");
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 1);
    CHECK(compact_mark_step(act, "v0.11.17", -1, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    /* a crash while it was on and the twins' mark (as the previous run left
       it) said trying: theirs, it is tried again; then the cull's own step
       blocks nothing of it */
    prevRun(prev, "crash (signal 11)");
    writeText(nd, "trying v0.11.16\n");
    writeText(mark, "on v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(!blockSet());
    CHECK(strstr(s_lines, "index compaction: was on when it ended (a crash): the discard-free "
                          "twins were being tried then (their mark): tried again") != NULL);
    (void)vu_zero_mark_step(act, "v0.11.16", -1, 0, logLine);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    (void)ico_remove(nd);

    /* a leanvs-mark.txt (a package not in this build, a file a test build
       left) is no attempt's: not read, the crash is its own */
    writeText(lv, "trying v0.11.16\n");
    writeText(mark, "counting v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 1);
    CHECK(strstr(s_lines, "lean vertex") == NULL);
    (void)ico_remove(lv);
    (void)ico_remove(mark);
    /* the one-pass blend's (its check runs after this one's test) */
    char op[1024];
    snprintf(op, sizeof op, "%s/onepass-mark.txt", dir);
    writeText(op, "trying v0.11.16\n");
    writeText(mark, "on v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(strstr(s_lines, "the one-pass blend was being tried then (its mark): tried again") !=
          NULL);
    (void)ico_remove(op);
    writeText(rc, "measuring 2026-11-24 10:00\nkey Mali-G52 | 688x512 | v0.11.16\n");
    writeText(mark, "on v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(strstr(s_lines, "the rate card was being tried then (its mark): tried again") != NULL);
    (void)ico_remove(rc);
    /* the same crash with nothing else trying: blocked */
    writeText(mark, "on v0.11.16\n");
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 1);
    (void)ico_remove(mark);

    /* a lost device while its own test was trying (run again after a
       graphics API switch) and the four others counting or on: the five
       steps in diag_host.c's order; it blocks, they do not (each reads its
       stage, compact_mark_stage_read), each line names it */
    char fp[1024];
    snprintf(fp, sizeof fp, "%s/fastps-mark.txt", dir);
    prevRun(prev, "graphics device lost");
    writeText(mark, "trying v0.11.16\n");
    writeText(zero, "counting v0.11.16\n");
    writeText(nd, "on v0.11.16\n");
    writeText(fp, "counting v0.11.16\n");
    writeText(op, "on v0.11.16\n");
    s_lines[0] = '\0';
    CHECK(compact_mark_step(act, "v0.11.16", -1, 0, logLine) == 1);
    CHECK(compact_mark_stage_read() == VU_ZERO_STAGE_TRYING);
    CHECK(vu_zero_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(nodiscard_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(fastps_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(onepass_mark_step(act, "v0.11.16", -1, 0, logLine) == 0);
    CHECK(strstr(s_lines, "zero-area cull: was on, its first pictures being counted, when it ended "
                          "(a lost graphics device): the index compaction was being tried then "
                          "(its mark): tried again") != NULL);
    CHECK(strstr(s_lines, "discard-free twins: were on when it ended (a lost graphics device): the "
                          "index compaction was being tried then (its mark): tried again") != NULL);
    CHECK(strstr(s_lines, "fast pixel build: was on, its first pictures being counted, when it "
                          "ended (a lost graphics device): the index compaction was being tried "
                          "then (its mark): tried again") != NULL);
    CHECK(strstr(s_lines,
                 "one-pass blend: was on when it ended (a lost graphics device): the index "
                 "compaction was being tried then (its mark): tried again") != NULL);
    if (fails) {
        printf("%s", s_lines);
    }
    char got2[64];
    readText(zero, got2, sizeof got2);
    CHECK(strncmp(got2, "blocked", 7) != 0);
    readText(mark, got2, sizeof got2);
    CHECK(strcmp(got2, "blocked v0.11.16\n") == 0);
    (void)ico_remove(mark);
    (void)ico_remove(zero);
    (void)ico_remove(nd);
    (void)ico_remove(fp);
    (void)ico_remove(op);
}

int main(int argc, char **argv)
{
    decide();
    if (argc >= 2) {
        step(argv[1]);
    } else {
        printf("compact_mark_test: no folder given: the step's checks not run\n");
        fails++;
    }
    printf("compact_mark_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
