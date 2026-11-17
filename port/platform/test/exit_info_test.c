/*
 * port/platform/test/exit_info_test.c
 *
 * How the previous run ended (exit_info.h, issue 215):
 *   reasons  every ApplicationExitInfo reason's constant name and words, a
 *            signal with and without the device's low-memory report, an
 *            exit code, a number outside the SDK's 0-16, the importances;
 *   parse    the record text: the header lines, a trace, ten new records
 *            (the newest eight kept, newest first), records not newer than
 *            the last seen, malformed lines, Android before 11, a failed
 *            read, no text; the trace names; the seen file;
 *   activity the start-up activity file's note and loss line;
 *   verdict  the precedence table (our own end marks, the activity file's
 *            loss, the record of last-run.txt's pid, the files alone, no
 *            file at all, a torn end), the two issue 215 runs word for
 *            word, the longest inputs within EXIT_INFO_VERDICT_MAX with
 *            every part whole;
 *   run      exit_info_begin through ICO_TEST_EXIT_INFO in a folder under
 *            the build folder: the verdict, exit-info.txt, the seen file,
 *            the report's verdict line and its exit-info.txt and trace
 *            entries; a second start that finds no new record.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exit_info.h"
#include "host_fs.h"
#include "last_run.h"
#include "miniz.h"
#include "report.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

static int starts_with(const char *got, const char *want)
{
    if (strncmp(got, want, strlen(want)) != 0) {
        printf("got:        %s\nstart want: %s\n", got, want);
        return 0;
    }
    return 1;
}

static int has(const char *got, const char *want)
{
    if (strstr(got, want) == NULL) {
        printf("got:       %s\nmissing:   %s\n", got, want);
        return 0;
    }
    return 1;
}

static int put(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    const int ok = f != NULL && fwrite(data, 1, n, f) == n;
    if (f != NULL) {
        fclose(f);
    }
    return ok ? 0 : -1;
}

/* a whole file (NUL-ended) into buf, its bytes or -1 */
static long get(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

/* the issue 215 runs' last-run.txt as LASTWORDS' file would have held them
   (LASTWORDS hand-back: the earlier run died in play at Main tick 4, its
   last heartbeat at 14.8 s; the later in the fog self-test) */
static const char *const kRunA[LAST_RUN_SLOTS] = {
    LAST_RUN_FORMAT " (rewritten in place while the game runs)",
    "v0.11.8 (commit v0.11.7-40-g1234567890)",
    "23456",
    "2026-11-16 20:50:00 UTC",
    "14.8",
    "play: stage 1, Main tick 2 (since 14.2 s)",
    "in front since 0.0 s",
    "level 15 (running critical) at 1.5 s, 1 in all",
    "47 at start, 47 now (+0)",
    "1945 MB now of 11265 MB, lowest 1804 MB at 4.0 s, floor 1915 MB",
    "140 MB now, peak 191 MB",
    "kernel 2 MB",
    "start-up set 210 of 684; 27 made as the frames needed them; the last pipeline note: none",
    "running (nothing written at an end: a run that stops here ended without a word)",
};

static const char *const kRunB[LAST_RUN_SLOTS] = {
    LAST_RUN_FORMAT " (rewritten in place while the game runs)",
    "v0.11.8 (commit v0.11.7-40-g1234567890)",
    "34567",
    "2026-11-16 20:56:01 UTC",
    "4.4",
    "a self-test: the fog self-test (since 4.4 s)",
    "in front since 0.0 s",
    "none",
    "66 at start, 66 now (+0)",
    "2226 MB now of 11265 MB, lowest 2226 MB at 4.0 s, floor 1915 MB",
    "257 MB now, peak 257 MB",
    "kernel 2 MB",
    "start-up set 290 of 684; 0 made as the frames needed them; the last pipeline note: none",
    "running (nothing written at an end: a run that stops here ended without a word)",
};

static char s_file[LAST_RUN_BYTES];

static void facts_of(const char *const v[LAST_RUN_SLOTS], LastRunFacts *f)
{
    last_run_text(v, s_file);
    (void)last_run_parse(s_file, sizeof s_file, f);
}

/* kRunA with one slot's value replaced */
static void facts_with(int slot, const char *value, LastRunFacts *f)
{
    const char *v[LAST_RUN_SLOTS];
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        v[s] = s == slot ? value : kRunA[s];
    }
    facts_of(v, f);
}

static void reasons(void)
{
    static const char *const want[EXIT_INFO_REASONS] = {
        "REASON_UNKNOWN",
        "REASON_EXIT_SELF",
        "REASON_SIGNALED",
        "REASON_LOW_MEMORY",
        "REASON_CRASH",
        "REASON_CRASH_NATIVE",
        "REASON_ANR",
        "REASON_INITIALIZATION_FAILURE",
        "REASON_PERMISSION_CHANGE",
        "REASON_EXCESSIVE_RESOURCE_USAGE",
        "REASON_USER_REQUESTED",
        "REASON_USER_STOPPED",
        "REASON_DEPENDENCY_DIED",
        "REASON_OTHER",
        "REASON_FREEZER",
        "REASON_PACKAGE_STATE_CHANGE",
        "REASON_PACKAGE_UPDATED",
    };
    char w[256];
    for (int r = 0; r < EXIT_INFO_REASONS; r++) {
        CHECK(text_is(exit_info_reason_name(r), want[r]));
        w[0] = '\0';
        exit_info_reason_words(r, 0, 1, w, sizeof w);
        CHECK(w[0] != '\0');
    }
    CHECK(text_is(exit_info_reason_name(EXIT_INFO_REASONS), "a reason not known to this program"));
    CHECK(text_is(exit_info_reason_name(-1), "a reason not known to this program"));
    exit_info_reason_words(EXIT_INFO_REASON_LOW_MEMORY, 0, 1, w, sizeof w);
    CHECK(text_is(w, "Android ended it because the device was low on memory"));
    exit_info_reason_words(EXIT_INFO_REASON_CRASH_NATIVE, 0, 1, w, sizeof w);
    CHECK(text_is(w, "it crashed in native code"));
    exit_info_reason_words(EXIT_INFO_REASON_ANR, 0, 1, w, sizeof w);
    CHECK(text_is(w, "it stopped responding (Android's \"not responding\")"));
    exit_info_reason_words(EXIT_INFO_REASON_USER_REQUESTED, 0, 1, w, sizeof w);
    CHECK(text_is(w, "it was closed from the recent apps list or with Force stop (or updated, "
                     "before Android 14)"));
    exit_info_reason_words(EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE, 0, 1, w, sizeof w);
    CHECK(text_is(w, "Android ended it for using too much of the device's resources"));
    exit_info_reason_words(EXIT_INFO_REASON_SIGNALED, 9, 0, w, sizeof w);
    CHECK(text_is(w, "it was killed (signal 9, SIGKILL); this device does not report low-memory "
                     "kills as such, so it may have been one"));
    exit_info_reason_words(EXIT_INFO_REASON_SIGNALED, 9, 1, w, sizeof w);
    CHECK(text_is(w, "it was killed (signal 9, SIGKILL)"));
    exit_info_reason_words(EXIT_INFO_REASON_SIGNALED, 3, -1, w, sizeof w);
    CHECK(text_is(w, "it was killed (signal 3)"));
    exit_info_reason_words(EXIT_INFO_REASON_EXIT_SELF, 3, 1, w, sizeof w);
    CHECK(text_is(w, "it closed itself (exit code 3)"));
    exit_info_reason_words(17, 0, 1, w, sizeof w);
    CHECK(text_is(w, "Android ended it for reason 17 (a reason not known to this program)"));
    exit_info_reason_words(42, 0, 1, w, sizeof w);
    CHECK(text_is(w, "Android ended it for reason 42 (a reason not known to this program)"));
    CHECK(text_is(exit_info_importance_words(100), "in front"));
    CHECK(text_is(exit_info_importance_words(200), "visible behind another window"));
    CHECK(text_is(exit_info_importance_words(325), "in front with the screen off"));
    CHECK(text_is(exit_info_importance_words(400), "in the background"));
    CHECK(text_is(exit_info_importance_words(1000), "already gone"));
}

/* one record line into the text at *at */
static void rec_line(char *text, size_t size, long pid, int reason, int status, int imp,
                     long long pss, long long rss, long long ms, const char *trace,
                     const char *desc, const char *summary)
{
    const size_t n = strlen(text);
    snprintf(text + n, size - n, "record\t%ld\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%s\t%s\t%s\n", pid,
             reason, status, imp, pss, rss, ms, trace, desc, summary);
}

static void parse(void)
{
    static char text[EXIT_INFO_TEXT_MAX];
    static ExitInfoList l;
    const long long seen = 1794862000000LL;
    text[0] = '\0';
    snprintf(text, sizeof text, "sdk\t33\nlmk\t0\ntotal\t17\n");
    /* not newer than the last seen: equal and older */
    rec_line(text, sizeof text, 100, 3, 0, 100, 1, 1, seen, "-", "", "");
    rec_line(text, sizeof text, 101, 3, 0, 100, 1, 1, seen - 1000, "-", "", "");
    /* ten new ones, in rising time: 1..10 s after the last seen */
    for (int i = 1; i <= 10; i++) {
        char tr[EXIT_INFO_TRACE_NAME_MAX];
        if (i == 10) {
            snprintf(tr, sizeof tr, "exit-trace-%lld.txt", seen + 10000);
        } else if (i == 9) {
            snprintf(tr, sizeof tr, "!java.io.IOException: no space");
        } else {
            snprintf(tr, sizeof tr, "-");
        }
        rec_line(text, sizeof text, 200 + i, i, i == 2 ? 9 : 0, 400, 1024L * i, 2048L * i,
                 seen + 1000LL * i, tr, i == 10 ? "a description" : "",
                 i == 10 ? "ico-pc play: stage 1 | resets 47->47 | free 1945 MB, lowest 1804" : "");
    }
    /* malformed: too few fields, a pid that is no number, a trace name the
       Java side never writes, a field too many, an unknown key */
    strcat(text, "record\t1\t2\n");
    strcat(text, "record\tx\t3\t0\t100\t0\t0\t1794862500000\t-\td\ts\n");
    strcat(text, "record\t7\t3\t0\t100\t0\t0\t1794862500000\t../etc/passwd\td\ts\n");
    strcat(text, "record\t7\t3\t0\t100\t0\t0\t1794862500000\t-\td\ts\textra\n");
    strcat(text, "bogus\tline\n");
    CHECK(exit_info_parse(text, seen, &l) == EXIT_INFO_MAX_RECORDS);
    CHECK(l.system == EXIT_INFO_SYSTEM_READ);
    CHECK(l.sdk == 33 && l.lmk == 0 && l.total == 17);
    CHECK(l.n == EXIT_INFO_MAX_RECORDS);
    CHECK(l.older == 2);
    CHECK(l.beyond == 10 - EXIT_INFO_MAX_RECORDS);
    CHECK(l.malformed == 5);
    CHECK(l.newestMs == seen + 10000);
    /* newest first: records 10 .. 3 */
    for (int i = 0; i < l.n; i++) {
        CHECK(l.rec[i].pid == 210 - i);
        CHECK(l.rec[i].timeMs == seen + 1000LL * (10 - i));
        CHECK(l.rec[i].reason == 10 - i);
        CHECK(l.rec[i].importance == 400);
        CHECK(l.rec[i].pssKb == 1024LL * (10 - i));
        CHECK(l.rec[i].rssKb == 2048LL * (10 - i));
    }
    {
        char want[EXIT_INFO_TRACE_NAME_MAX];
        snprintf(want, sizeof want, "exit-trace-%lld.txt", seen + 10000);
        CHECK(text_is(l.rec[0].trace, want));
    }
    CHECK(text_is(l.rec[1].trace, "!java.io.IOException: no space"));
    CHECK(l.rec[2].trace[0] == '\0');
    CHECK(text_is(l.rec[0].description, "a description"));
    CHECK(text_is(l.rec[0].summary,
                  "ico-pc play: stage 1 | resets 47->47 | free 1945 MB, lowest 1804"));
    CHECK(exit_info_find_pid(&l, 207) == 3);
    CHECK(exit_info_find_pid(&l, 201) == -1); /* the oldest new ones are past the cap */
    CHECK(exit_info_find_pid(&l, 100) == -1); /* not newer than the last seen */
    CHECK(exit_info_find_pid(&l, -1) == -1);
    CHECK(exit_info_find_pid(NULL, 207) == -1);

    /* the caps: a description and a summary longer than their room are cut */
    {
        static char longDesc[400], longSum[300];
        memset(longDesc, 'd', sizeof longDesc - 1);
        longDesc[sizeof longDesc - 1] = '\0';
        memset(longSum, 's', sizeof longSum - 1);
        longSum[sizeof longSum - 1] = '\0';
        text[0] = '\0';
        rec_line(text, sizeof text, 5, 3, 0, 100, 0, 0, 5, "-", longDesc, longSum);
        CHECK(exit_info_parse(text, 0, &l) == 1);
        CHECK(strlen(l.rec[0].description) == EXIT_INFO_DESC_MAX - 1);
        CHECK(strlen(l.rec[0].summary) == LAST_RUN_SUMMARY_MAX - 1);
        CHECK(l.sdk == -1 && l.lmk == -1 && l.total == -1);
    }

    /* Android before 11; a failed read; no text; an empty text */
    CHECK(exit_info_parse("sdk\t29\n", 0, &l) == 0);
    CHECK(l.system == EXIT_INFO_SYSTEM_OLD && l.sdk == 29);
    CHECK(exit_info_parse("sdk\t33\nwhy\tgetHistoricalProcessExitReasons: java.lang.Security"
                          "Exception\n",
                          0, &l) == 0);
    CHECK(l.system == EXIT_INFO_SYSTEM_FAILED);
    CHECK(text_is(l.why, "getHistoricalProcessExitReasons: java.lang.SecurityException"));
    CHECK(exit_info_parse(NULL, 7, &l) == 0);
    CHECK(l.system == EXIT_INFO_SYSTEM_NONE && l.newestMs == 7);
    CHECK(exit_info_parse("", 0, &l) == 0);
    CHECK(l.system == EXIT_INFO_SYSTEM_READ && l.n == 0 && l.malformed == 0);

    /* the trace names */
    CHECK(exit_info_trace_name_ok("exit-trace-1.pb"));
    CHECK(exit_info_trace_name_ok("exit-trace-1794862215000.txt"));
    CHECK(!exit_info_trace_name_ok("exit-trace-.txt"));
    CHECK(!exit_info_trace_name_ok("exit-trace-12.txt.x"));
    CHECK(!exit_info_trace_name_ok("exit-trace-12a.txt"));
    CHECK(!exit_info_trace_name_ok("../exit-trace-1.txt"));
    CHECK(!exit_info_trace_name_ok(NULL));

    /* the seen file */
    {
        long long s = -1;
        char tr[EXIT_INFO_TRACE_NAME_MAX];
        exit_info_seen_parse("1794862215000 exit-trace-1794862215000.txt\n", &s, tr, sizeof tr);
        CHECK(s == 1794862215000LL);
        CHECK(text_is(tr, "exit-trace-1794862215000.txt"));
        exit_info_seen_parse("1794862215000 -\n", &s, tr, sizeof tr);
        CHECK(s == 1794862215000LL && tr[0] == '\0');
        exit_info_seen_parse("123 ../x\n", &s, tr, sizeof tr);
        CHECK(s == 123 && tr[0] == '\0');
        exit_info_seen_parse("garbage\n", &s, tr, sizeof tr);
        CHECK(s == 0 && tr[0] == '\0');
        exit_info_seen_parse(NULL, &s, tr, sizeof tr);
        CHECK(s == 0 && tr[0] == '\0');
    }
}

static const char kActivity[] =
    "v0.11.8 (commit v0.11.7-40-g1234567890)\n"
    "creating pipeline program 9 blend 12 vs 1 fs 0 fmt 1/8 msaa 0 dst 0 (sprite_world_vs 1243 "
    "words + sprite_ps 9942 words)\n"
    "set: careful\n"
    "set: stopped at the system's low-memory signal (1911 MB available of 11265 MB, floor 1915 "
    "MB); resume at pipeline 502 of 684, batches of 4, stop 2, 2026-11-16 20:56\n"
    "compiled: VU batch 198-201 of 684 (compiler E031.41.03.47): 4 pipelines\n";

static void activity(void)
{
    static ExitInfoActivity a;
    static char text[1024];
    exit_info_activity_parse(kActivity, &a);
    CHECK(a.found);
    CHECK(text_is(a.note, "creating pipeline program 9 blend 12 vs 1 fs 0 fmt 1/8 msaa 0 dst 0 "
                          "(sprite_world_vs 1243 words + sprite_ps 9942 words)"));
    CHECK(a.loss[0] == '\0');
    snprintf(text, sizeof text,
             "v0.11.8\r\nthe game runs\r\nset: careful\r\ngraphics driver hung, "
             "no loss dump\r\ncompiled: x\r\n");
    exit_info_activity_parse(text, &a);
    CHECK(text_is(a.note, "the game runs"));
    CHECK(text_is(a.loss, "graphics driver hung, no loss dump"));
    exit_info_activity_parse("v0.11.8\nnote\nloss dump: loss-20261116-205600.zip\n", &a);
    CHECK(text_is(a.loss, "loss dump: loss-20261116-205600.zip"));
    exit_info_activity_parse(NULL, &a);
    CHECK(!a.found && a.note[0] == '\0');
}

/* a list read from Android 13 holding one record of pid */
static void list_one(ExitInfoList *l, long pid, int reason, int status)
{
    char text[1024];
    snprintf(text, sizeof text, "sdk\t33\nlmk\t1\ntotal\t3\n");
    rec_line(text, sizeof text, pid, reason, status, 100, 215040, 266240, 1794862215000LL, "-", "",
             "ico-pc play: stage 1, Main tick 2 (since 14.2 s) | resets 47->47 | free 1945 MB, "
             "lowest 1804");
    CHECK(exit_info_parse(text, 1794862000000LL, l) == 1);
}

static void verdict(void)
{
    static LastRunFacts f;
    static ExitInfoList match, other, old;
    static ExitInfoActivity actLoss, actHang, actNote;
    char v[EXIT_INFO_VERDICT_MAX];
    static const char tail[] =
        "; it was at play: stage 1, Main tick 2 (since 14.2 s), 14.8 s after its start at its "
        "last write; GPU resets 47 -> 47; free memory 1945 MB at its last write (lowest 1804 MB, "
        "floor 1915 MB)";
    static const char rec[] =
        "(Android's record: REASON_LOW_MEMORY (3), status 9, in front "
        "(importance 100), pss 210 MB, rss 260 MB, at 2026-11-16 20:50:15 UTC)";

    list_one(&match, 23456, EXIT_INFO_REASON_LOW_MEMORY, 9);
    list_one(&other, 11111, EXIT_INFO_REASON_LOW_MEMORY, 9);
    CHECK(exit_info_parse("sdk\t29\n", 0, &old) == 0);
    exit_info_activity_parse("v0.11.8\nnote\nloss dump: loss-20261116-205600.zip\n", &actLoss);
    exit_info_activity_parse("v0.11.8\nnote\ngraphics driver hung, no loss dump\n", &actHang);
    exit_info_activity_parse(kActivity, &actNote);

    /* 1. our own marks come first, whatever Android says */
    facts_with(LAST_RUN_END, "crash (signal 11)", &f);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: it crashed (signal 11) "));
    CHECK(has(v, rec));
    CHECK(has(v, tail));
    facts_with(LAST_RUN_END, "graphics device lost", &f);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: the graphics device was lost ("));
    facts_with(LAST_RUN_END, "the graphics driver stopped responding (a GPU hang)", &f);
    CHECK(exit_info_verdict(&f, NULL, &other, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: the graphics driver stopped responding (a GPU "
                         "hang) (no record from Android for pid 23456; 1 other new record); "));
    facts_with(LAST_RUN_END, "the watchdog: no new Main tick for 10.0 s", &f);
    CHECK(exit_info_verdict(&f, NULL, NULL, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: the watchdog ended it: no new Main tick for "
                         "10.0 s (the system keeps no exit record (Android 11 and later only)); "));
    facts_with(LAST_RUN_END, "clean", &f);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: it was closed normally (Android's record: "));
    facts_with(LAST_RUN_END, "clean, after: graphics device lost", &f);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: it was closed normally, after: graphics device "
                         "lost ("));
    /* the activity file's loss line, last-run.txt without an end */
    facts_of(kRunA, &f);
    CHECK(exit_info_verdict(&f, &actLoss, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: the graphics device was lost ("));
    CHECK(exit_info_verdict(&f, &actHang, &match, v, sizeof v) == EXIT_INFO_FROM_MARK);
    CHECK(starts_with(v, "the previous run ended: the graphics driver stopped responding (a GPU "
                         "hang) ("));
    CHECK(exit_info_verdict(NULL, &actLoss, NULL, v, sizeof v) == EXIT_INFO_FROM_MARK);

    /* 2. Android's record of last-run.txt's pid */
    CHECK(exit_info_verdict(&f, &actNote, &match, v, sizeof v) == EXIT_INFO_FROM_RECORD);
    {
        char want[EXIT_INFO_VERDICT_MAX];
        snprintf(want, sizeof want,
                 "the previous run ended: Android ended it because the device was low on memory "
                 "%s%s",
                 rec, tail);
        CHECK(text_is(v, want));
    }
    /* a torn end is no mark: the record decides */
    facts_of(kRunA, &f);
    s_file[last_run_slot_offset(LAST_RUN_END) + 10] ^= 1;
    (void)last_run_parse(s_file, sizeof s_file, &f);
    CHECK(f.state[LAST_RUN_END] == LAST_RUN_SLOT_TORN);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_RECORD);
    /* a torn end and no record: the files alone */
    CHECK(exit_info_verdict(&f, NULL, &other, v, sizeof v) == EXIT_INFO_FROM_FILES);

    /* 3. the files alone */
    facts_of(kRunA, &f);
    CHECK(exit_info_verdict(&f, &actNote, &other, v, sizeof v) == EXIT_INFO_FROM_FILES);
    CHECK(starts_with(v, "the previous run ended: without a word: no crash, device loss, "
                         "watchdog or clean end was written (no record from Android for pid "
                         "23456; 1 other new record); it was at play: "));
    CHECK(exit_info_verdict(&f, NULL, NULL, v, sizeof v) == EXIT_INFO_FROM_FILES);
    CHECK(has(v, "(the system keeps no exit record (Android 11 and later only))"));
    CHECK(exit_info_verdict(&f, NULL, &old, v, sizeof v) == EXIT_INFO_FROM_FILES);
    CHECK(has(v, "(Android API 29 keeps no exit record (Android 11, API 30, and later only))"));
    {
        static ExitInfoList failed;
        CHECK(exit_info_parse("sdk\t33\nwhy\tno activity\n", 0, &failed) == 0);
        CHECK(exit_info_verdict(&f, NULL, &failed, v, sizeof v) == EXIT_INFO_FROM_FILES);
        CHECK(has(v, "(Android's exit records not read: no activity)"));
    }
    /* no last-run.txt: the activity file's note; its record cannot be matched */
    CHECK(exit_info_verdict(NULL, &actNote, &match, v, sizeof v) == EXIT_INFO_FROM_FILES);
    CHECK(text_is(v, "the previous run ended: without a word inside the start-up (its activity "
                     "file was left; no last-run.txt) (no record from Android matched (no pid to "
                     "match); 1 new record); it was at (the start-up activity) creating pipeline "
                     "program 9 blend 12 vs 1 fs 0 fmt 1/8 msaa 0 dst 0 (sprite_world_vs 1243 "
                     "words + sprite_ps 9942 words); GPU resets unknown; free memory unknown"));
    /* neither file */
    CHECK(exit_info_verdict(NULL, NULL, NULL, v, sizeof v) == EXIT_INFO_FROM_NOTHING);
    CHECK(text_is(v, "the previous run ended: unknown: no last-run.txt and no start-up activity "
                     "file (the first start with them, or the files were deleted) (the system "
                     "keeps no exit record (Android 11 and later only)); where it was is not "
                     "known; GPU resets unknown; free memory unknown"));
    /* last-run.txt in another format: as none */
    {
        static LastRunFacts none;
        (void)last_run_parse("not a last-run file", 19, &none);
        CHECK(!none.found);
        CHECK(exit_info_verdict(&none, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_NOTHING);
    }

    /* the issue 215 runs as their next start would have said it. The
       earlier run (died in play), with a low-memory record Android might
       have kept for it (hypothetical: the logs cannot say), then the later
       run (died in the fog self-test) with no record of its pid */
    facts_of(kRunA, &f);
    CHECK(exit_info_verdict(&f, NULL, &match, v, sizeof v) == EXIT_INFO_FROM_RECORD);
    CHECK(text_is(v, "the previous run ended: Android ended it because the device was low on "
                     "memory (Android's record: REASON_LOW_MEMORY (3), status 9, in front "
                     "(importance 100), pss 210 MB, rss 260 MB, at 2026-11-16 20:50:15 UTC); it "
                     "was at play: stage 1, Main tick 2 (since 14.2 s), 14.8 s after its start at "
                     "its last write; GPU resets 47 -> 47; free memory 1945 MB at its last write "
                     "(lowest 1804 MB, floor 1915 MB)"));
    facts_of(kRunB, &f);
    CHECK(exit_info_verdict(&f, NULL, &other, v, sizeof v) == EXIT_INFO_FROM_FILES);
    CHECK(text_is(v, "the previous run ended: without a word: no crash, device loss, watchdog or "
                     "clean end was written (no record from Android for pid 34567; 1 other new "
                     "record); it was at a self-test: the fog self-test (since 4.4 s), 4.4 s after "
                     "its start at its last write; GPU resets 66 -> 66; free memory 2226 MB at "
                     "its last write (lowest 2226 MB, floor 1915 MB)"));
}

/* the longest inputs: every part within its room, the line within
   EXIT_INFO_VERDICT_MAX and whole to its end */
static void room(void)
{
    static char endV[LAST_RUN_VALUE_MAX], phaseV[LAST_RUN_VALUE_MAX], resetsV[LAST_RUN_VALUE_MAX];
    static char text[EXIT_INFO_TEXT_MAX], desc[EXIT_INFO_DESC_MAX], want[400];
    static LastRunFacts f;
    static ExitInfoList l;
    const char *v[LAST_RUN_SLOTS];
    char out[EXIT_INFO_VERDICT_MAX];
    /* each value as long as its slot holds (the slot's width less its key,
       ": " and the check's tail) */
    const size_t endRoom = last_run_slot_width(LAST_RUN_END) - strlen("end: ") - LAST_RUN_TAIL;
    const size_t phaseRoom =
        last_run_slot_width(LAST_RUN_PHASE) - strlen("phase: ") - LAST_RUN_TAIL;
    const size_t resetsRoom =
        last_run_slot_width(LAST_RUN_RESETS) - strlen("GPU resets: ") - LAST_RUN_TAIL;
    snprintf(endV, sizeof endV, "clean, after: ");
    memset(endV + strlen(endV), 'a', endRoom - strlen(endV));
    endV[endRoom] = '\0';
    memset(phaseV, 'p', phaseRoom);
    phaseV[phaseRoom] = '\0';
    snprintf(resetsV, sizeof resetsV, "not readable (");
    memset(resetsV + strlen(resetsV), 'r', resetsRoom - strlen(resetsV));
    resetsV[resetsRoom] = '\0';
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        v[s] = kRunA[s];
    }
    v[LAST_RUN_PID] = "2147483647";
    v[LAST_RUN_SECONDS] = "99999.9";
    v[LAST_RUN_PHASE] = phaseV;
    v[LAST_RUN_RESETS] = resetsV;
    v[LAST_RUN_FREE] = "2147483647 MB now of 1 MB, lowest 2147483647 MB at 1.0 s, floor "
                       "2147483647 MB";
    v[LAST_RUN_END] = endV;
    facts_of(v, &f);
    CHECK(f.found && f.end == LAST_RUN_END_CLEAN && f.pid == 2147483647L);
    memset(desc, 'd', sizeof desc - 1);
    desc[sizeof desc - 1] = '\0';
    text[0] = '\0';
    rec_line(text, sizeof text, 2147483647L, EXIT_INFO_REASON_EXCESSIVE_RESOURCE_USAGE,
             -2147483647 - 1, 350, -9223372036854775807LL - 1, -9223372036854775807LL - 1,
             9223372036854775807LL, "-", desc, "");
    CHECK(exit_info_parse(text, 0, &l) == 1);
    CHECK(exit_info_verdict(&f, NULL, &l, out, sizeof out) == EXIT_INFO_FROM_MARK);
    CHECK(strlen(out) < EXIT_INFO_VERDICT_MAX - 1);
    /* the how part whole: 160 of the end's letters after "after: " */
    snprintf(want, sizeof want, "it was closed normally, after: %.160s (Android's record: ",
             endV + strlen("clean, after: "));
    CHECK(has(out, want));
    CHECK(has(out, "(importance 350), pss -9007199254740992 MB, rss -9007199254740992 MB, at "));
    CHECK(has(out, ", 99999.9 s after its start at its last write; GPU resets not readable ("));
    CHECK(has(out, "; free memory 2147483647 MB at its last write (lowest 2147483647 MB, floor "
                   "2147483647 MB)"));
    CHECK(out[strlen(out) - 1] == ')');
    /* a buffer smaller than the line: cut, NUL-ended */
    {
        char small[16];
        (void)exit_info_verdict(&f, NULL, &l, small, sizeof small);
        CHECK(strlen(small) == sizeof small - 1);
    }
}

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void run(const char *base)
{
    /* p holds "<dir>/<name>": sized from both (gcc's -O1 fortify format-truncation) */
    static char dir[600], p[600 + 1 + 160], act[700], recs[700], buf[16384];
    static char text[EXIT_INFO_TEXT_MAX];
    static char lastRun[LAST_RUN_BYTES];
    static const char kTrace[] = "----- pid 23456 at 2026-11-16 20:50:15 -----\n\"main\" ...\n";
    static const char kVerdict[] =
        "the previous run ended: it was killed (signal 9, SIGKILL); this device does not "
        "report low-memory kills as such, so it may have been one (Android's record: "
        "REASON_SIGNALED (2), status 9, a foreground service (importance 125), pss 210 MB, rss "
        "260 MB, at 2026-11-16 20:50:15 UTC, \"too much memory\"); it was at play: stage 1, Main "
        "tick 2 (since 14.2 s), 14.8 s after its start at its last write; GPU resets 47 -> 47; "
        "free memory 1945 MB at its last write (lowest 1804 MB, floor 1915 MB)";
    char name[160];
    mz_zip_archive z;

    snprintf(dir, sizeof dir, "%s/exit_info_test_logs", base);
    (void)ico_remove_tree(dir);
    CHECK(ico_mkdir(dir) == 0);
    /* the previous run's files */
    last_run_text(kRunA, lastRun);
    snprintf(p, sizeof p, "%s/last-run.txt", dir);
    CHECK(put(p, lastRun, sizeof lastRun) == 0);
    snprintf(act, sizeof act, "%s/startup-activity.txt", dir);
    CHECK(put(act, kActivity, sizeof kActivity - 1) == 0);
    /* the last start's read */
    snprintf(p, sizeof p, "%s/exit-info-seen.txt", dir);
    CHECK(put(p, "1794862000000 -\n", 16) == 0);
    /* Android's answer: the run's record with its trace (the Java side
       copies it), and one already seen */
    snprintf(text, sizeof text, "sdk\t33\nlmk\t0\ntotal\t2\n");
    rec_line(text, sizeof text, 23456, EXIT_INFO_REASON_SIGNALED, 9, 125, 215040, 266240,
             1794862215000LL, "exit-trace-1794862215000.txt", "too much memory",
             "ico-pc play: stage 1, Main tick 2 (since 14.2 s) | resets 47->47 | free 1945 MB, "
             "lowest 1804");
    rec_line(text, sizeof text, 11111, EXIT_INFO_REASON_LOW_MEMORY, 0, 400, 0, 0, 1794861000000LL,
             "-", "", "");
    snprintf(recs, sizeof recs, "%s/records.txt", dir);
    CHECK(put(recs, text, strlen(text)) == 0);
    snprintf(p, sizeof p, "%s/exit-trace-1794862215000.txt", dir);
    CHECK(put(p, kTrace, sizeof kTrace - 1) == 0);
    set_env("ICO_TEST_EXIT_INFO", recs);

    exit_info_begin(dir, act);
    CHECK(text_is(exit_info_last_verdict(), kVerdict));
    /* exit-info.txt */
    snprintf(p, sizeof p, "%s/exit-info.txt", dir);
    CHECK(get(p, buf, sizeof buf) > 0);
    CHECK(starts_with(buf, "ico-pc exit info: read at the start of "));
    CHECK(has(buf, kVerdict));
    CHECK(has(buf, "\nthe system: records read; API 33; 2 held, 1 new, 1 older, 0 past the 8 "
                   "kept, 0 lines not read; low-memory kills reported as such: 0 (-1 not said)\n"));
    CHECK(has(buf, "\nrecord 1 of 1: pid 23456, REASON_SIGNALED (2): it was killed (signal 9, "
                   "SIGKILL); this device does not report low-memory kills as such, so it may "
                   "have been one; status 9; importance 125 (a foreground service); pss 210 MB, "
                   "rss 260 MB; at 2026-11-16 20:50:15 UTC (1794862215000 ms); trace "
                   "exit-trace-1794862215000.txt; description \"too much memory\"; summary "
                   "\"ico-pc play: stage 1, Main tick 2 (since 14.2 s) | resets 47->47 | free "
                   "1945 MB, lowest 1804\"\n"));
    CHECK(has(buf, "\nthe newest trace kept: exit-trace-1794862215000.txt\n"));
    /* the seen file: the newest record and its trace */
    snprintf(p, sizeof p, "%s/exit-info-seen.txt", dir);
    CHECK(get(p, buf, sizeof buf) > 0);
    CHECK(text_is(buf, "1794862215000 exit-trace-1794862215000.txt\n"));

    /* the report: the verdict in report.txt, exit-info.txt and the trace */
    ico_report_set_paths(dir, dir, dir, NULL);
    {
        IcoReportInputs in;
        memset(&in, 0, sizeof in);
        in.what = "the test";
        name[0] = '\0';
        CHECK(ico_report_write(&in, name, sizeof name) == 0);
    }
    snprintf(p, sizeof p, "%s/%s", dir, name);
    memset(&z, 0, sizeof z);
    CHECK(mz_zip_reader_init_file(&z, p, 0));
    {
        size_t got = 0;
        char *t = mz_zip_reader_extract_file_to_heap(&z, "report.txt", &got, 0);
        CHECK(t != NULL && got < sizeof buf);
        if (t != NULL && got < sizeof buf) {
            memcpy(buf, t, got);
            buf[got] = '\0';
            CHECK(has(buf, kVerdict));
        }
        mz_free(t);
        t = mz_zip_reader_extract_file_to_heap(&z, "exit-trace-1794862215000.txt", &got, 0);
        CHECK(t != NULL && got == sizeof kTrace - 1 && memcmp(t, kTrace, got) == 0);
        mz_free(t);
        CHECK(mz_zip_reader_locate_file(&z, "exit-info.txt", NULL, 0) >= 0);
    }
    mz_zip_reader_end(&z);
    (void)ico_remove(p);

    /* the next start with the same answer: nothing newer than the seen
       record; the seen file and its trace stay */
    exit_info_begin(dir, act);
    CHECK(starts_with(exit_info_last_verdict(),
                      "the previous run ended: without a word: no crash, device loss, watchdog "
                      "or clean end was written (no record from Android for pid 23456; 0 other "
                      "new records); "));
    snprintf(p, sizeof p, "%s/exit-info-seen.txt", dir);
    CHECK(get(p, buf, sizeof buf) > 0);
    CHECK(text_is(buf, "1794862215000 exit-trace-1794862215000.txt\n"));
    snprintf(p, sizeof p, "%s/exit-info.txt", dir);
    CHECK(get(p, buf, sizeof buf) > 0);
    CHECK(has(buf, "; 2 held, 0 new, 2 older, "));
    CHECK(has(buf, "\nthe newest trace kept: exit-trace-1794862215000.txt\n"));

    /* off Android without the fake: the system keeps no exit record */
    set_env("ICO_TEST_EXIT_INFO", "");
    exit_info_begin(dir, act);
    CHECK(has(exit_info_last_verdict(), "(the system keeps no exit record (Android 11 and later "
                                        "only))"));
    snprintf(p, sizeof p, "%s/exit-info.txt", dir);
    CHECK(get(p, buf, sizeof buf) > 0);
    CHECK(has(buf, "\nthe system: no exit records on this system; "));
    (void)ico_remove_tree(dir);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: exit_info_test <folder>\n");
        return 2;
    }
    reasons();
    parse();
    activity();
    verdict();
    room();
    run(argv[1]);
    if (fails != 0) {
        printf("exit_info_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("exit_info_test: ok\n");
    return 0;
}
