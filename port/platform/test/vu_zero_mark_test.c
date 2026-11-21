/*
 * port/platform/test/vu_zero_mark_test.c: the zero-area cull's mark on the
 * install (vu_zero_mark.c, issue 179).
 *   ends    the end kind from last-run.txt's end and Android's record: our
 *           own end first ("clean, after: <end>" is that end), then the
 *           record (REASON_SIGNALED with SIGKILL apart from other signals),
 *           no evidence; the words of every kind
 *   table   every stage against every end kind (the stage table of
 *           vu_zero_mark.h, printed): trying blocks every end but a clean
 *           close; counting and on only a crash, an ANR, a signal, a device
 *           loss, a GPU hang or the watchdog; failed and blocked always
 *   decide  the marks with versions: a bad end blocks for the version in
 *           the mark, never the next one (an update clears it); a mark
 *           without a version is the running one's; failed, blocked; no
 *           mark; a text this program does not know (taken as trying); the
 *           lines word for word where they matter
 *   step    the start's step on files in the given folder: closed from the
 *           recent apps list while counting: not blocked, the mark removed,
 *           no block exported (the self-test runs again); a crash while
 *           counting: "blocked v0.11.14", kept at the next start of that
 *           version, removed at v0.11.15's; any end without evidence while
 *           trying (killed, no last-run file, no record): blocked; the same
 *           while counting: not blocked; the watchdog while on: blocked
 *           (the cull, never a lost start for the pre-pass); a device loss
 *           closed cleanly after ("clean, after: graphics device lost")
 *           while on: blocked; a SIGKILL while on: not blocked;
 *           ICO_VU_ZERO_MARK and ICO_VU_ZERO_VERSION exported
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void ends(void)
{
    /* our own end mark first, whatever Android's record says */
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN, "clean", EXIT_INFO_REASON_CRASH, 0) ==
          VU_ZERO_END_CLEAN);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CRASH, "crash (signal 11)",
                                EXIT_INFO_REASON_USER_REQUESTED, 0) == VU_ZERO_END_CRASH);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_LOSS, "graphics device lost", -1, 0) ==
          VU_ZERO_END_LOSS);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_HANG, "the graphics driver stopped responding", -1,
                                0) == VU_ZERO_END_HANG);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_WATCHDOG, "the watchdog: x", -1, 0) ==
          VU_ZERO_END_WATCHDOG);
    /* a clean close after a bad end (last_run_end_clean: "clean, after: ") */
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN, "clean, after: graphics device lost", -1, 0) ==
          VU_ZERO_END_LOSS);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN,
                                "clean, after: the graphics driver stopped responding (a GPU "
                                "hang)",
                                -1, 0) == VU_ZERO_END_HANG);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN, "clean, after: the watchdog: the game thread",
                                -1, 0) == VU_ZERO_END_WATCHDOG);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN, "clean, after: crash (signal 6)", -1, 0) ==
          VU_ZERO_END_CRASH);
    CHECK(vu_zero_mark_end_kind(LAST_RUN_END_CLEAN, NULL, -1, 0) == VU_ZERO_END_CLEAN);

    /* no end written: Android's record */
    static const struct {
        int reason, status, kind;
    } kRec[] = {
        {EXIT_INFO_REASON_CRASH, 0, VU_ZERO_END_A_CRASH},
        {EXIT_INFO_REASON_CRASH_NATIVE, 11, VU_ZERO_END_A_CRASH},
        {EXIT_INFO_REASON_ANR, 0, VU_ZERO_END_A_ANR},
        {EXIT_INFO_REASON_SIGNALED, 11, VU_ZERO_END_A_SIGNAL},
        {EXIT_INFO_REASON_SIGNALED, 6, VU_ZERO_END_A_SIGNAL},
        {EXIT_INFO_REASON_SIGNALED, 9, VU_ZERO_END_A_KILLED},
        {EXIT_INFO_REASON_USER_REQUESTED, 0, VU_ZERO_END_A_USER},
        {EXIT_INFO_REASON_USER_STOPPED, 0, VU_ZERO_END_A_USER},
        {EXIT_INFO_REASON_LOW_MEMORY, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_FREEZER, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_PERMISSION_CHANGE, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_DEPENDENCY_DIED, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_INITIALIZATION_FAILURE, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_OTHER, 0, VU_ZERO_END_A_SYSTEM},
        {EXIT_INFO_REASON_PACKAGE_UPDATED, 0, VU_ZERO_END_A_UPDATE},
        {EXIT_INFO_REASON_PACKAGE_STATE_CHANGE, 0, VU_ZERO_END_A_UPDATE},
        {EXIT_INFO_REASON_EXIT_SELF, 0, VU_ZERO_END_A_SELF},
        {EXIT_INFO_REASON_UNKNOWN, 0, VU_ZERO_END_NONE},
        {99, 0, VU_ZERO_END_NONE},
        {-1, 0, VU_ZERO_END_NONE},
    };

    for (size_t i = 0; i < sizeof kRec / sizeof kRec[0]; i++) {
        CHECK(vu_zero_mark_end_kind(LAST_RUN_END_RUNNING, "running", kRec[i].reason,
                                    kRec[i].status) == kRec[i].kind);
        CHECK(vu_zero_mark_end_kind(-1, NULL, kRec[i].reason, kRec[i].status) == kRec[i].kind);
        CHECK(vu_zero_mark_end_kind(LAST_RUN_END_UNKNOWN, "?", kRec[i].reason, kRec[i].status) ==
              kRec[i].kind);
    }
    char w[160];
    vu_zero_mark_end_words(VU_ZERO_END_A_USER, EXIT_INFO_REASON_USER_REQUESTED, 0, w, sizeof w);
    CHECK(strcmp(w, "Android: closed from the recent apps list or with Force stop "
                    "(REASON_USER_REQUESTED)") == 0);
    vu_zero_mark_end_words(VU_ZERO_END_A_SIGNAL, EXIT_INFO_REASON_SIGNALED, 11, w, sizeof w);
    CHECK(strcmp(w, "Android: signal 11 (REASON_SIGNALED)") == 0);
    vu_zero_mark_end_words(VU_ZERO_END_NONE, -1, 0, w, sizeof w);
    CHECK(strcmp(w, "no end written and no record from Android") == 0);
    for (int k = 0; k < VU_ZERO_ENDS; k++) {
        vu_zero_mark_end_words(k, EXIT_INFO_REASON_OTHER, 0, w, sizeof w);
        CHECK(w[0] != '\0');
    }
}

/* the stage table (vu_zero_mark.h), as the brief states it */
static void table(void)
{
    static const char *const kStage[VU_ZERO_STAGES] = {"none",   "trying",  "counting", "on",
                                                       "failed", "blocked", "unread"};
    static const char *const kEnd[VU_ZERO_ENDS] = {
        "clean",          "crash",          "loss",           "hang",         "watchdog",
        "Android-crash",  "Android-ANR",    "Android-signal", "Android-kill", "Android-user",
        "Android-system", "Android-update", "Android-self",   "no-evidence"};
    /* the ends the evidence calls bad */
    static const int kBad[VU_ZERO_ENDS] = {0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0};
    printf("stage table (1: the cull blocked for the version):\n");
    for (int st = 0; st < VU_ZERO_STAGES; st++) {
        printf("  %-8s", kStage[st]);
        for (int e = 0; e < VU_ZERO_ENDS; e++) {
            int want;
            switch (st) {
            case VU_ZERO_STAGE_NONE:
                want = 0;
                break;
            case VU_ZERO_STAGE_TRYING:
            case VU_ZERO_STAGE_UNREAD:
                want = e != VU_ZERO_END_CLEAN;
                break;
            case VU_ZERO_STAGE_COUNTING:
            case VU_ZERO_STAGE_ON:
                want = kBad[e];
                break;
            default:
                want = 1;
                break;
            }
            const int got = vu_zero_mark_blocks(st, e);
            printf(" %s=%d", kEnd[e], got);
            CHECK(got == want);
        }
        printf("\n");
    }
}

static void decide(void)
{
    char out[96], line[512];
    int block;
    /* trying: every end but a clean close blocks, for the mark's version */
    for (int e = 0; e < VU_ZERO_ENDS; e++) {
        vu_zero_mark_decide("trying v0.11.14\n", e, "x", "v0.11.14", out, sizeof out, line,
                            sizeof line, &block);
        if (e == VU_ZERO_END_CLEAN) {
            CHECK(out[0] == '\0' && block == 0);
            CHECK(strcmp(line, "the previous run: zero-area cull: was being tried when it ended "
                               "(x): no sign of a bad end: tried again") == 0);
        } else {
            CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
            CHECK(strcmp(line, "the previous run: zero-area cull: was being tried when it ended "
                               "(x): off on this install until the version changes (blocked "
                               "v0.11.14)") == 0);
        }
    }
    /* counting: closed from the recent apps list: not blocked */
    vu_zero_mark_decide("counting v0.11.14", VU_ZERO_END_A_USER, "Android: recents", "v0.11.14",
                        out, sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line,
                 "the previous run: zero-area cull: was on, its first pictures being "
                 "counted, when it ended (Android: recents): no sign of a bad end: tried again") ==
          0);
    /* counting: a crash blocks this version, not the next */
    vu_zero_mark_decide("counting v0.11.14", VU_ZERO_END_CRASH, "a crash", "v0.11.14", out,
                        sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    vu_zero_mark_decide("counting v0.11.14", VU_ZERO_END_CRASH, "a crash", "v0.11.15", out,
                        sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: zero-area cull: was on, its first pictures being "
                       "counted, when it ended (a crash) in v0.11.14, this is v0.11.15: tried "
                       "again") == 0);
    /* on: no evidence does not block; the watchdog does (the cull alone) */
    vu_zero_mark_decide("on v0.11.14", VU_ZERO_END_NONE, "none", "v0.11.14", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    vu_zero_mark_decide("on v0.11.14", VU_ZERO_END_WATCHDOG, "the watchdog", "v0.11.14", out,
                        sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    CHECK(strcmp(line, "the previous run: zero-area cull: was on when it ended (the watchdog): off "
                       "on this install until the version changes (blocked v0.11.14)") == 0);
    /* a mark without a version: the running one's */
    vu_zero_mark_decide("trying", VU_ZERO_END_NONE, "none", "v0.11.15", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.15") == 0 && block == 1);
    /* failed and blocked: for their version */
    vu_zero_mark_decide("failed v0.11.14", VU_ZERO_END_CLEAN, "closed normally", "v0.11.14", out,
                        sizeof out, line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    vu_zero_mark_decide("failed v0.11.14", VU_ZERO_END_CLEAN, "closed normally", "v0.11.15", out,
                        sizeof out, line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    vu_zero_mark_decide("blocked v0.11.14\n", VU_ZERO_END_CLEAN, "x", "v0.11.14", out, sizeof out,
                        line, sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    CHECK(strcmp(line,
                 "the previous run: zero-area cull: kept off by its mark: off on this install "
                 "until the version changes (blocked v0.11.14)") == 0);
    vu_zero_mark_decide("blocked v0.11.14", VU_ZERO_END_CLEAN, "x", "v0.11.15", out, sizeof out,
                        line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    CHECK(strcmp(line, "the previous run: zero-area cull: kept off by its mark in v0.11.14, this "
                       "is v0.11.15: tried again") == 0);
    /* no mark; a text not known (taken as trying) */
    vu_zero_mark_decide(NULL, VU_ZERO_END_CRASH, "a crash", "v0.11.14", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    vu_zero_mark_decide("", VU_ZERO_END_CRASH, "a crash", "v0.11.14", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    vu_zero_mark_decide("onward", VU_ZERO_END_NONE, "none", "v0.11.14", out, sizeof out, line,
                        sizeof line, &block);
    CHECK(strcmp(out, "blocked v0.11.14") == 0 && block == 1);
    CHECK(strstr(line, "its mark unread (\"onward\")") != NULL);
    vu_zero_mark_decide("onward", VU_ZERO_END_CLEAN, "closed normally", "v0.11.14", out, sizeof out,
                        line, sizeof line, &block);
    CHECK(out[0] == '\0' && block == 0);
    /* the parse */
    char ver[64];
    CHECK(vu_zero_mark_parse("counting v0.11.14\r\n", ver, sizeof ver) == VU_ZERO_STAGE_COUNTING &&
          strcmp(ver, "v0.11.14") == 0);
    CHECK(vu_zero_mark_parse("on\n", ver, sizeof ver) == VU_ZERO_STAGE_ON && ver[0] == '\0');
    CHECK(vu_zero_mark_parse("oncoming", ver, sizeof ver) == VU_ZERO_STAGE_UNREAD);
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
    const char *b = getenv("ICO_VU_ZERO_BLOCK");
    return b != NULL && b[0] != '\0';
}

static void step(const char *dir)
{
    char act[1024], prev[1024], mark[1024], got[256];
    snprintf(act, sizeof act, "%s/vu_zero_mark_activity.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);
    snprintf(mark, sizeof mark, "%s/vu-zero-mark.txt", dir);

    /* closed from the recent apps list while counting: not blocked, the
       self-test runs again (no block, the mark removed) */
    prevRun(prev, "running");
    writeText(mark, "counting v0.11.14\n");
    s_lines[0] = '\0';
    CHECK(vu_zero_mark_step(act, "v0.11.14", EXIT_INFO_REASON_USER_REQUESTED, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    CHECK(!blockSet());
    CHECK(strstr(s_lines, "the previous run: zero-area cull: was on, its first pictures being "
                          "counted, when it ended (Android: closed from the recent apps list or "
                          "with Force stop (REASON_USER_REQUESTED)): no sign of a bad end: tried "
                          "again") != NULL);
    CHECK(getenv("ICO_VU_ZERO_MARK") != NULL && strcmp(getenv("ICO_VU_ZERO_MARK"), mark) == 0);
    CHECK(getenv("ICO_VU_ZERO_VERSION") != NULL &&
          strcmp(getenv("ICO_VU_ZERO_VERSION"), "v0.11.14") == 0);

    /* a crash while counting: blocked for this version, not the next */
    prevRun(prev, "crash (signal 11)");
    writeText(mark, "counting v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", -1, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0);
    CHECK(blockSet() && strstr(getenv("ICO_VU_ZERO_BLOCK"),
                               "was on, its first pictures being counted, when it ended (a "
                               "crash): off on this install until the version changes (blocked "
                               "v0.11.14)") == getenv("ICO_VU_ZERO_BLOCK"));
    prevRun(prev, "clean");
    CHECK(vu_zero_mark_step(act, "v0.11.14", -1, 0, logLine) == 1); /* the next start */
    CHECK(blockSet());
    CHECK(vu_zero_mark_step(act, "v0.11.15", -1, 0, logLine) == 0); /* an update */
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());

    /* trying: a close from the recent apps list blocks (a hang in the
       driver's compiler the player swiped away looks the same) */
    prevRun(prev, "running");
    writeText(mark, "trying v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", EXIT_INFO_REASON_USER_REQUESTED, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0 && blockSet());
    /* trying, no evidence at all (no last-run file, no record): blocked */
    prevRun(prev, NULL);
    writeText(mark, "trying v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", -1, 0, logLine) == 1);
    CHECK(strstr(s_lines, "was being tried when it ended (no end written and no record from "
                          "Android): off on this install") != NULL);
    /* counting, no evidence at all: not blocked */
    writeText(mark, "counting v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", -1, 0, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0' && !blockSet());
    /* trying, closed cleanly: not blocked */
    prevRun(prev, "clean");
    writeText(mark, "trying v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", -1, 0, logLine) == 0);
    CHECK(!blockSet());

    /* on: the watchdog blocks the cull (the pre-pass's loss count is not
       touched: diag_host.c no longer reads anything from the mark) */
    prevRun(prev, "the watchdog: the game thread stopped for 10.0 s");
    writeText(mark, "on v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", EXIT_INFO_REASON_EXIT_SELF, 0, logLine) == 1);
    readText(mark, got, sizeof got);
    CHECK(strcmp(got, "blocked v0.11.14\n") == 0);
    /* on: a device loss the window then closed cleanly after */
    prevRun(prev, "clean, after: graphics device lost");
    writeText(mark, "on v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", EXIT_INFO_REASON_EXIT_SELF, 0, logLine) == 1);
    CHECK(strstr(s_lines, "was on when it ended (a lost graphics device): off on this install") !=
          NULL);
    /* on: killed by the system in the background (SIGKILL): not blocked */
    prevRun(prev, "running");
    writeText(mark, "on v0.11.14\n");
    CHECK(vu_zero_mark_step(act, "v0.11.14", EXIT_INFO_REASON_SIGNALED, 9, logLine) == 0);
    readText(mark, got, sizeof got);
    CHECK(got[0] == '\0');
    /* the start's own step reads Android's record from exit_info (none
       read in this process: -1), so trying with no end written blocks */
    writeText(mark, "trying v0.11.14\n");
    CHECK(vu_zero_mark_begin(act, "v0.11.14", logLine) == 1);
    (void)ico_remove(mark);
    (void)ico_remove(prev);
    printf("%s", s_lines);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: vu_zero_mark_test <folder>\n");
        return 2;
    }
    ends();
    table();
    decide();
    step(argv[1]);
    if (fails != 0) {
        printf("vu_zero_mark_test: %d failures\n", fails);
        return 1;
    }
    printf("vu_zero_mark_test: ok\n");
    return 0;
}
