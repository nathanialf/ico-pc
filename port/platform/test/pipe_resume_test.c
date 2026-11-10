/*
 * port/platform/test/pipe_resume_test.c
 *
 * The start-up set resumed after a low-memory stop (pipe_resume.h, issue
 * 123):
 *   part     the stop line's part word for word, an empty date written as
 *            "an unknown date", a cut at the buffer's end;
 *   parse    the part read back from a whole stop line (the floor's
 *            figures and the retry mark before it), an older version's
 *            line without it (0), numbers out of range (at past the set,
 *            total 0, batch 0, stops 0) refused, the unknown date read as
 *            "";
 *   batch    the halving across starts that stop again: the default set's
 *            32 -> 16 -> 8 -> 4 -> 4, the careful set's 8 -> 4, 5 -> 4,
 *            0 (not known) 0;
 *   at       inside the set kept, at or past a set that shrank 0;
 *   stops    a start's own stop counts 1, a resumed start's stop the
 *            previous count + 1;
 *   careful  a resumed start keeps the player's [dev] pipeline_set =
 *            "careful" ("careful" 1; "full", "lazy", "", unset 0);
 *   size     the longest line the activity file holds (diag_host.c
 *            act_low_memory, 256 bytes) with the floor's words, the retry
 *            mark and the largest numbers fits.
 */
#include <stdio.h>
#include <string.h>
#include "memory_floor.h"
#include "pipe_resume.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testPart(void)
{
    char b[160];
    IcoPipeResume r = {312, 1196, 8, 1, "2026-11-09 20:45"};

    CHECK(ico_pipe_resume_part(b, sizeof b, &r) ==
          strlen("; resume at pipeline 312 of 1196, batches of 8, stop 1, 2026-11-09 20:45"));
    CHECK(strcmp(b, "; resume at pipeline 312 of 1196, batches of 8, stop 1, 2026-11-09 20:45") ==
          0);
    r.date[0] = '\0';
    (void)ico_pipe_resume_part(b, sizeof b, &r);
    CHECK(strcmp(b, "; resume at pipeline 312 of 1196, batches of 8, stop 1, an unknown date") ==
          0);
    CHECK(ico_pipe_resume_part(b, 10, &r) == 9);
    CHECK(strcmp(b, "; resume ") == 0);
    CHECK(ico_pipe_resume_part(b, 0, &r) == 0);
}

static void testParse(void)
{
    IcoPipeResume r;

    CHECK(ico_pipe_resume_parse("set: stopped at the system's low-memory signal (1940 MB "
                                "available of 11453 MB, floor 1947 MB) after a retry; resume at "
                                "pipeline 312 of 1196, batches of 8, stop 2, 2026-11-09 20:45",
                                &r) == 1);
    CHECK(r.at == 312 && r.total == 1196 && r.batch == 8 && r.stops == 2);
    CHECK(strcmp(r.date, "2026-11-09 20:45") == 0);
    CHECK(ico_pipe_resume_parse("set: stopped at the system's low-memory signal (running "
                                "critical); resume at pipeline 0 of 527, batches of 32, stop 1, "
                                "an unknown date",
                                &r) == 1);
    CHECK(r.at == 0 && r.total == 527 && r.batch == 32 && r.stops == 1 && r.date[0] == '\0');
    /* an older version's line: no part */
    r.at = 7;
    CHECK(ico_pipe_resume_parse("set: stopped at the system's low-memory signal (1940 MB "
                                "available of 11453 MB, floor 1947 MB)",
                                &r) == 0);
    CHECK(r.at == 0 && r.total == 0 && r.batch == 0);
    CHECK(ico_pipe_resume_parse(NULL, &r) == 0);
    /* numbers out of range */
    CHECK(ico_pipe_resume_parse("x; resume at pipeline 527 of 527, batches of 8, stop 1, d", &r) ==
          0);
    CHECK(ico_pipe_resume_parse("x; resume at pipeline 0 of 0, batches of 8, stop 1, d", &r) == 0);
    CHECK(ico_pipe_resume_parse("x; resume at pipeline 3 of 9, batches of 0, stop 1, d", &r) == 0);
    CHECK(ico_pipe_resume_parse("x; resume at pipeline 3 of 9, batches of 8, stop 0, d", &r) == 0);
    CHECK(ico_pipe_resume_parse("x; resume at pipeline 3 of nine, batches of 8, stop 1, d", &r) ==
          0);
    /* a round trip */
    {
        char b[160];
        const IcoPipeResume w = {1195, 1196, 4, 7, "2026-12-31 23:59"};

        (void)ico_pipe_resume_part(b, sizeof b, &w);
        CHECK(ico_pipe_resume_parse(b, &r) == 1);
        CHECK(memcmp(&r, &w, sizeof r) == 0);
    }
}

static void testBatch(void)
{
    /* the default set's batch (rd_pipeline.c RD_PIPE_BATCH 32) across starts
       that stop again */
    CHECK(ico_pipe_resume_batch(32) == 16);
    CHECK(ico_pipe_resume_batch(16) == 8);
    CHECK(ico_pipe_resume_batch(8) == 4);
    CHECK(ico_pipe_resume_batch(4) == 4);
    /* the careful set's 8 (rd_pipeline_policy_careful) */
    CHECK(ico_pipe_resume_batch(ico_pipe_resume_batch(8)) == 4);
    CHECK(ico_pipe_resume_batch(5) == 4);
    CHECK(ico_pipe_resume_batch(1) == 4);
    CHECK(ico_pipe_resume_batch(0) == 0);
}

static void testAt(void)
{
    const IcoPipeResume r = {312, 1196, 8, 1, ""};

    CHECK(ico_pipe_resume_at(&r, 1196) == 312);
    CHECK(ico_pipe_resume_at(&r, 1300) == 312);
    CHECK(ico_pipe_resume_at(&r, 313) == 312);
    CHECK(ico_pipe_resume_at(&r, 312) == 0);
    CHECK(ico_pipe_resume_at(&r, 100) == 0);
}

static void testCareful(void)
{
    CHECK(ico_pipe_resume_careful("careful") == 1);
    CHECK(ico_pipe_resume_careful("full") == 0);
    CHECK(ico_pipe_resume_careful("lazy") == 0);
    CHECK(ico_pipe_resume_careful("") == 0);
    CHECK(ico_pipe_resume_careful("Careful ") == 0);
    CHECK(ico_pipe_resume_careful(NULL) == 0);
}

static void testStops(void)
{
    const IcoPipeResume r = {312, 1196, 8, 3, ""};

    CHECK(ico_pipe_resume_stops(0, &r) == 1);
    CHECK(ico_pipe_resume_stops(1, &r) == 4);
    CHECK(ico_pipe_resume_stops(1, NULL) == 1);
}

static void testSize(void)
{
    char part[160], line[512];
    const IcoPipeResume r = {4294967294u, 4294967295u, 4294967295u, 4294967295u,
                             "2026-12-31 23:59"};

    (void)ico_pipe_resume_part(part, sizeof part, &r);
    snprintf(line, sizeof line,
             "set: stopped at the system's low-memory signal (1048576 MB available of 1048576 MB, "
             "floor 1048576 MB)" ICO_MEMORY_FLOOR_RETRIED_MARK "%s",
             part);
    CHECK(strlen(line) < 256);
}

int main(void)
{
    testPart();
    testParse();
    testBatch();
    testAt();
    testStops();
    testCareful();
    testSize();
    printf("pipe_resume_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
