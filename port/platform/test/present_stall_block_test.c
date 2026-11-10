/*
 * port/platform/test/present_stall_block_test.c
 *
 * The present stalls per 10 s block and per run (present_stall_block.h,
 * issue 123):
 *   quiet    blocks before the run's first stall write no line and still
 *            join the run's sums (time by display rate and by band);
 *   block    the issue 123 log's block (v0.11.2, replay 2021): 299 replays
 *            at 27.4 ms and one stalled 340 ms present with a GPU span of
 *            87.1 ms whose clock restarted: the line word for word (the
 *            stalled replay's span kept out of the band, "not measured
 *            (the GPU clock restarted)", the display's 120 Hz, the pin's
 *            ask, the clock not readable); the block after it with no
 *            stall still writes its line (the run has had one); a
 *            measured idle gap, an overlap, a GPU clock read;
 *   list     more stalls than the list holds: the count whole, the first
 *            listed;
 *   band     the band index for 27.4 ms (5: 25-30), 3 ms (0), 90 ms (the
 *            last), none;
 *   run      the run's line: the stalls a minute in all, by display rate
 *            (120 and 60) and by the block's band, word for word;
 *   short    a small buffer is cut and terminated.
 */
#include <stdio.h>
#include <string.h>
#include "present_stall_block.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* n presented replays timed at ms with a measured 5.8 ms idle gap */
static void steady(StallBlock *b, unsigned n, double ms)
{
    const StallBlockRec r = {1, 1.5, 1, ms, 1, 5.8, 0.0};

    for (unsigned i = 0; i < n; i++) {
        stall_block_add(b, &r);
    }
}

static void testQuietAndBlock(void)
{
    StallBlock b;
    char line[768];
    const StallBlockRec stall = {1, 340.4, 1, 87.1, 0, 0.0, 20699.4};
    const StallBlockRec untimed = {0, 0.0, 0, 0.0, 0, 0.0, 0.0};

    stall_block_init(&b);
    steady(&b, 300, 27.5);
    stall_block_add(&b, &untimed); /* not presented: not counted */
    CHECK(b.presents == 300);
    CHECK(stall_block_end(&b, 10.0, 120.0, 0, NULL, line, sizeof line) == 0);
    CHECK_STR(line, "");
    CHECK(b.runBlocks == 1 && b.runStalls == 0 && b.rateN == 1 && b.rateSeconds[0] == 10.0);
    CHECK(b.bandSeconds[5] == 10.0);

    /* the stalled block of the issue 123 log */
    steady(&b, 299, 27.4);
    stall_block_add(&b, &stall);
    CHECK(b.stalls == 1 && b.restarts == 1 && b.spanN == 299);
    CHECK(stall_block_end(&b, 10.0, 120.0, 59940, NULL, line, sizeof line) == 1);
    CHECK_STR(line, "present stalls: 1 over 250 ms in 10.0 s (340 ms; GPU idle before the "
                    "stalled replay: not measured (the GPU clock restarted)); GPU clock restarts "
                    "1; GPU span 27.4 ms a replay (27.4-27.4 ms, band 25-30 ms; stalled replays "
                    "87.1 ms); display 120.00 Hz, frame rate pin 59.94 Hz asked; GPU clock not "
                    "readable");

    /* the block after: no stall, still a line (the run has had one) */
    steady(&b, 300, 28.0);
    CHECK(stall_block_end(&b, 10.0, 120.0, 0, NULL, line, sizeof line) == 1);
    CHECK_STR(line, "present stalls: 0 over 250 ms in 10.0 s; GPU clock restarts 0; GPU span "
                    "28.0 ms a replay (28.0-28.0 ms, band 25-30 ms); display 120.00 Hz, no frame "
                    "rate asked; GPU clock not readable");

    /* a measured idle gap and an overlap; a GPU clock read; 60 Hz */
    {
        const StallBlockRec a = {1, 300.0, 1, 85.0, 1, 86.1, 0.0};
        const StallBlockRec o = {1, 260.0, 0, 0.0, 1, -3.5, 0.0};
        const StallBlockRec n = {1, 255.0, 1, 80.0, 0, 0.0, 0.0};
        const StallBlockClock clock = {40, 220, 680, 412.4};

        steady(&b, 10, 30.0);
        steady(&b, 10, 34.0);
        stall_block_add(&b, &a);
        stall_block_add(&b, &o);
        stall_block_add(&b, &n);
        CHECK(stall_block_end(&b, 10.0, 60.0, 59940, &clock, line, sizeof line) == 1);
        CHECK_STR(line, "present stalls: 3 over 250 ms in 10.0 s (300, 260, 255 ms; GPU idle "
                        "before the stalled replay: 86.1 ms, begun 3.5 ms before the one before "
                        "ended, not measured); GPU clock restarts 0; GPU span 32.0 ms a replay "
                        "(30.0-34.0 ms, band 30-35 ms; stalled replays 85.0, not timed, 80.0 ms); "
                        "display 60.00 Hz, frame rate pin 59.94 Hz asked; GPU clock 220-680 MHz "
                        "(mean 412, 40 samples)");
    }

    /* the run */
    CHECK(b.runBlocks == 4 && b.runStalls == 4);
    CHECK(stall_block_run_text(&b, line, sizeof line) > 0);
    CHECK_STR(line, "present stalls this run: 4 over 250 ms in 0.7 min (6.0 a minute); by "
                    "display rate: 120.00 Hz 1 in 0.5 min (2.0 a minute), 60.00 Hz 3 in 0.2 min "
                    "(18.0 a minute); by the block's GPU span: 25-30 ms 1 in 0.5 min (2.0 a "
                    "minute), 30-35 ms 3 in 0.2 min (18.0 a minute)");

    /* a short buffer: cut and terminated */
    {
        char small[24];

        memset(small, 'x', sizeof small);
        stall_block_run_text(&b, small, sizeof small);
        CHECK(strlen(small) == sizeof small - 1);
    }
}

static void testListAndBand(void)
{
    StallBlock b;
    char line[768];
    const StallBlockRec s = {1, 400.0, 0, 0.0, 0, 0.0, 0.0};

    stall_block_init(&b);
    CHECK(stall_block_band(&b) == -1);
    CHECK(stall_block_run_text(&b, line, sizeof line) == 0 && line[0] == '\0');
    for (int i = 0; i < STALL_BLOCK_LIST + 2; i++) {
        stall_block_add(&b, &s);
    }
    CHECK(b.stalls == STALL_BLOCK_LIST + 2);
    CHECK(stall_block_end(&b, 10.0, 0.0, 0, NULL, line, sizeof line) == 1);
    CHECK(strstr(line, "present stalls: 8 over 250 ms") == line);
    CHECK(strstr(line, "400, 400, 400, 400, 400, 400 ms, the first listed;") != NULL);
    CHECK(strstr(line, "GPU span not timed (stalled replays not timed") != NULL);
    CHECK(strstr(line, "display rate not known") != NULL);
    CHECK(stall_block_run_text(&b, line, sizeof line) > 0);
    CHECK(strstr(line, "by display rate: not known 8 in 0.2 min") != NULL);
    CHECK(strstr(line, "by the block's GPU span: not timed 8 in 0.2 min") != NULL);

    steady(&b, 4, 27.4);
    CHECK(stall_block_band(&b) == 5);
    stall_block_init(&b);
    steady(&b, 4, 3.0);
    CHECK(stall_block_band(&b) == 0);
    stall_block_init(&b);
    steady(&b, 4, 90.0);
    CHECK(stall_block_band(&b) == STALL_BLOCK_BANDS - 1);
}

int main(void)
{
    testQuietAndBlock();
    testListAndBand();
    printf("present_stall_block_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
