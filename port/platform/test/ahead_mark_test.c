/*
 * port/platform/test/ahead_mark_test.c: the next picture submitted ahead,
 * its mark on the install (ahead_mark.c, issue 179).
 *   table   every mark against every previous end and Android's reason:
 *           "on" after a device loss, a GPU hang, the watchdog or a crash, or
 *           with no end written and Android's crash, native crash or no
 *           response: blocked for this version (of another version:
 *           cleared, tried again); "on" after a clean end, no end written
 *           with no record or an ordinary close by Android's record (closed
 *           from the recent apps or Force stop, low memory, signalled,
 *           ended for its resources, an update), or an end not read:
 *           cleared, not held off; "blocked" of this version kept, of
 *           another cleared; no mark: nothing, no line; other text: cleared
 *   begin   the start's step on files in the given folder: a run that lost
 *           the device with the order in effect: blocked v0.11.14, kept at
 *           the next start of v0.11.14 (a clean one between them changes
 *           nothing), cleared and the order on again at the first start of
 *           v0.11.15; a close from the recent apps with the order in effect
 *           (no end written, Android's REASON_USER_REQUESTED): not blocked;
 *           the in-effect write: "on <version>" once a process, its line
 *           once
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <string.h>

#include "ahead_mark.h"
#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"

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

static void table(void)
{
    char out[96], line[480];
    /* our own bad ends, whatever Android says */
    static const int kBad[] = {LAST_RUN_END_LOSS, LAST_RUN_END_HANG, LAST_RUN_END_WATCHDOG,
                               LAST_RUN_END_CRASH};
    static const int kReasons[] = {-1, EXIT_INFO_REASON_USER_REQUESTED, EXIT_INFO_REASON_CRASH};
    for (size_t i = 0; i < sizeof kBad / sizeof kBad[0]; i++) {
        for (size_t r = 0; r < sizeof kReasons / sizeof kReasons[0]; r++) {
            CHECK(ahead_mark_decide("on v0.11.14\n", kBad[i], kReasons[r], "v0.11.14", out,
                                    sizeof out, line, sizeof line) == 1);
            CHECK(strcmp(out, "blocked v0.11.14") == 0);
            CHECK(strstr(line, "the previous run: the next picture submitted ahead: in effect "
                               "when it ended (") == line);
            CHECK(strstr(line, "): off on this install until the version changes (blocked "
                               "v0.11.14)") != NULL);
            /* the same end in a run of another version: cleared */
            CHECK(ahead_mark_decide("on v0.11.13\n", kBad[i], kReasons[r], "v0.11.14", out,
                                    sizeof out, line, sizeof line) == 0);
            CHECK(out[0] == '\0' &&
                  strstr(line, "in effect in a run of v0.11.13 when it ended (") != NULL);
        }
    }
    CHECK(ahead_mark_decide("on v0.11.14", LAST_RUN_END_LOSS, -1, "v0.11.14", out, sizeof out, line,
                            sizeof line) == 1);
    CHECK(strcmp(line, "the previous run: the next picture submitted ahead: in effect when it "
                       "ended (a lost graphics device): off on this install until the version "
                       "changes (blocked v0.11.14)") == 0);
    /* no end written: Android's crash, native crash, no response block */
    static const int kAndroidBad[] = {EXIT_INFO_REASON_CRASH, EXIT_INFO_REASON_CRASH_NATIVE,
                                      EXIT_INFO_REASON_ANR};
    for (size_t r = 0; r < sizeof kAndroidBad / sizeof kAndroidBad[0]; r++) {
        CHECK(ahead_mark_decide("on v0.11.14", LAST_RUN_END_RUNNING, kAndroidBad[r], "v0.11.14",
                                out, sizeof out, line, sizeof line) == 1);
        CHECK(strcmp(out, "blocked v0.11.14") == 0 &&
              strstr(line, ": a crash or no response") != NULL);
    }
    /* ordinary ends: cleared, never blocked */
    static const int kOrdinary[] = {
        -1,
        EXIT_INFO_REASON_UNKNOWN,
        EXIT_INFO_REASON_EXIT_SELF,
        EXIT_INFO_REASON_SIGNALED,
        EXIT_INFO_REASON_LOW_MEMORY,
        EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE,
        EXIT_INFO_REASON_USER_REQUESTED,
        EXIT_INFO_REASON_USER_STOPPED,
        EXIT_INFO_REASON_OTHER,
        EXIT_INFO_REASON_FREEZER,
        EXIT_INFO_REASON_PACKAGE_STATE_CHANGE,
        EXIT_INFO_REASON_PACKAGE_UPDATED,
    };
    static const int kQuiet[] = {LAST_RUN_END_RUNNING, LAST_RUN_END_CLEAN, LAST_RUN_END_UNKNOWN,
                                 -1};
    for (size_t e = 0; e < sizeof kQuiet / sizeof kQuiet[0]; e++) {
        for (size_t r = 0; r < sizeof kOrdinary / sizeof kOrdinary[0]; r++) {
            CHECK(ahead_mark_decide("on v0.11.14\n", kQuiet[e], kOrdinary[r], "v0.11.14", out,
                                    sizeof out, line, sizeof line) == 0);
            CHECK(out[0] == '\0' && strstr(line, "): not held off (the mark cleared)") != NULL);
        }
    }
    /* a clean end with Android's crash record (a record of another process
       matched by chance): ours first */
    CHECK(ahead_mark_decide("on v0.11.14", LAST_RUN_END_CLEAN, EXIT_INFO_REASON_CRASH, "v0.11.14",
                            out, sizeof out, line, sizeof line) == 0);
    CHECK(strcmp(ahead_mark_decide("on v0.11.14", LAST_RUN_END_RUNNING,
                                   EXIT_INFO_REASON_USER_REQUESTED, "v0.11.14", out, sizeof out,
                                   line, sizeof line) == 0
                     ? line
                     : "",
                 "the previous run: the next picture submitted ahead: in effect when it ended (no "
                 "end written; Android's exit reason 10: an ordinary end (closed, stopped or ended "
                 "by the system)): not held off (the mark cleared)") == 0);
    /* blocked: kept on this version, whatever the end; cleared on another */
    CHECK(ahead_mark_decide("blocked v0.11.14\n", LAST_RUN_END_CLEAN, -1, "v0.11.14", out,
                            sizeof out, line, sizeof line) == 1);
    CHECK(strcmp(out, "blocked v0.11.14") == 0);
    CHECK(strcmp(line, "the previous run: the next picture submitted ahead: off on this install "
                       "since a run of v0.11.14 ended badly with it in effect (blocked "
                       "v0.11.14)") == 0);
    CHECK(ahead_mark_decide("blocked v0.11.14", LAST_RUN_END_LOSS, -1, "v0.11.15", out, sizeof out,
                            line, sizeof line) == 0);
    CHECK(out[0] == '\0');
    CHECK(strcmp(line, "the previous run: the next picture submitted ahead: blocked on v0.11.14, "
                       "this is v0.11.15: tried again (the mark cleared)") == 0);
    /* no mark: nothing, no line */
    CHECK(ahead_mark_decide(NULL, LAST_RUN_END_LOSS, -1, "v0.11.14", out, sizeof out, line,
                            sizeof line) == 0);
    CHECK(out[0] == '\0' && line[0] == '\0');
    CHECK(ahead_mark_decide("", LAST_RUN_END_CRASH, EXIT_INFO_REASON_CRASH, "v0.11.14", out,
                            sizeof out, line, sizeof line) == 0);
    CHECK(out[0] == '\0' && line[0] == '\0');
    /* other text: cleared */
    CHECK(ahead_mark_decide("trying\n", LAST_RUN_END_LOSS, -1, "v0.11.14", out, sizeof out, line,
                            sizeof line) == 0);
    CHECK(out[0] == '\0' &&
          strcmp(line, "the previous run: the next picture submitted ahead: its mark unread "
                       "(\"trying\"): cleared") == 0);
    CHECK(ahead_mark_bad_end(LAST_RUN_END_LOSS, -1) &&
          !ahead_mark_bad_end(LAST_RUN_END_RUNNING, -1) &&
          ahead_mark_bad_end(LAST_RUN_END_RUNNING, EXIT_INFO_REASON_CRASH_NATIVE) &&
          !ahead_mark_bad_end(LAST_RUN_END_RUNNING, EXIT_INFO_REASON_LOW_MEMORY));
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

/* the previous run's last-run-previous.txt with this end */
static void prevEnd(const char *dir, const char *end)
{
    char prev[1024];
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    const char *values[LAST_RUN_SLOTS] = {NULL};
    values[LAST_RUN_HEADER] = LAST_RUN_FORMAT;
    values[LAST_RUN_END] = end;
    static char text[LAST_RUN_BYTES + 1];
    last_run_text(values, text);
    text[LAST_RUN_BYTES] = '\0';
    writeText(prev, text);
}

/* a start of the given version on the files (ahead_mark_step: the
   process's own start is once a process, ahead_mark_begin), its block and
   the mark it left */
static int start(const char *dir, const char *version, int reason, char *mark, size_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, AHEAD_MARK_FILE);
    const int block = ahead_mark_step(dir, version, reason, logLine);
    readText(path, mark, size);
    return block;
}

static void runs(const char *dir)
{
    char path[1024], got[192];
    snprintf(path, sizeof path, "%s/%s", dir, AHEAD_MARK_FILE);
    (void)ico_remove(path);
    s_lines[0] = '\0';

    /* the process's own start (ahead_mark_begin) and the in-effect write:
       no mark yet, the order not held off; "on v0.11.14" written once */
    prevEnd(dir, "clean");
    CHECK(ahead_mark_begin(dir, "v0.11.14", -1, logLine) == NULL);
    CHECK(s_lines[0] == '\0'); /* no mark: no line */
    ahead_mark_in_effect();
    readText(path, got, sizeof got);
    CHECK(strcmp(got, "on v0.11.14\n") == 0);
    CHECK(strstr(s_lines, "the next picture submitted ahead: in effect on this install from "
                          "here (") != NULL &&
          strstr(s_lines, AHEAD_MARK_FILE ": on v0.11.14; ") != NULL);
    const size_t once = strlen(s_lines);
    (void)ico_remove(path);
    ahead_mark_in_effect(); /* once a process */
    readText(path, got, sizeof got);
    CHECK(got[0] == '\0' && strlen(s_lines) == once);
    /* a second begin returns the first's answer and reads nothing */
    CHECK(ahead_mark_begin(dir, "v0.11.14", EXIT_INFO_REASON_CRASH, logLine) == NULL);

    /* a loss with the order in effect: off for this version ... */
    writeText(path, "on v0.11.14\n");
    prevEnd(dir, "graphics device lost (the loss dump: loss-x.zip)");
    CHECK(start(dir, "v0.11.14", -1, got, sizeof got) == 1);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0);
    /* ... kept at every start of it: a clean start between changes nothing */
    prevEnd(dir, "clean");
    CHECK(start(dir, "v0.11.14", -1, got, sizeof got) == 1);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0);
    CHECK(start(dir, "v0.11.14", EXIT_INFO_REASON_USER_REQUESTED, got, sizeof got) == 1);
    /* ... and on again in the next version (the mark cleared) */
    CHECK(start(dir, "v0.11.15", -1, got, sizeof got) == 0);
    CHECK(got[0] == '\0');
    CHECK(start(dir, "v0.11.15", -1, got, sizeof got) == 0);

    /* a close from the recent apps with the order in effect (no end
       written, Android's REASON_USER_REQUESTED): not blocked */
    writeText(path, "on v0.11.15\n");
    prevEnd(dir, "running");
    CHECK(start(dir, "v0.11.15", EXIT_INFO_REASON_USER_REQUESTED, got, sizeof got) == 0);
    CHECK(got[0] == '\0');
    /* the same with no record from Android (an older Android, or none
       read), a low-memory kill and an update: not blocked */
    static const int kClose[] = {-1, EXIT_INFO_REASON_LOW_MEMORY, EXIT_INFO_REASON_PACKAGE_UPDATED,
                                 EXIT_INFO_REASON_SIGNALED};
    for (size_t i = 0; i < sizeof kClose / sizeof kClose[0]; i++) {
        writeText(path, "on v0.11.15\n");
        CHECK(start(dir, "v0.11.15", kClose[i], got, sizeof got) == 0);
        CHECK(got[0] == '\0');
    }
    /* a native crash Android recorded with no end of ours: blocked */
    writeText(path, "on v0.11.15\n");
    CHECK(start(dir, "v0.11.15", EXIT_INFO_REASON_CRASH_NATIVE, got, sizeof got) == 1);
    CHECK(strcmp(got, "blocked v0.11.15\n") == 0);
    (void)ico_remove(path);
    printf("%s", s_lines);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: ahead_mark_test <folder>\n");
        return 2;
    }
    table();
    runs(argv[1]);
    if (fails != 0) {
        printf("ahead_mark_test: %d failures\n", fails);
        return 1;
    }
    printf("ahead_mark_test: ok\n");
    return 0;
}
