/*
 * port/platform/test/log_cost_test.c
 *
 * The logging's own cost (log_cost.h): from a made-up block on a phone with
 * the log thread: the window thread's print, the log thread's writes with
 * one slow write during a present that blocked and one during a late step,
 * the pipe never full, a capture whose worker overlapped a late step: the
 * line word for word; the overlaps (touching spans do not overlap); a
 * desktop block with no log thread and the bytes not known; a full pipe;
 * spans past LOG_COST_SPANS counted, not kept; a short buffer.
 */
#include <stdio.h>
#include <string.h>

#include "log_cost.h"

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

static void testOverlaps(void)
{
    const LogCostSpan a[3] = {{0, 10}, {20, 30}, {40, 50}};
    const LogCostSpan b[2] = {{10, 20}, {45, 46}}; /* touching a[0] and a[1]: no overlap */
    CHECK(log_cost_overlaps(a, 3, b, 2) == 1);
    CHECK(log_cost_overlaps(b, 2, a, 3) == 1);
    CHECK(log_cost_overlaps(a, 3, b, 0) == 0);
}

static void testPhone(void)
{
    LogCost c;
    char line[1024];
    log_cost_reset(&c);
    CHECK(c.bytes == -1);
    log_cost_present(&c, 1000.0, 1357.0); /* issue 123's stall */
    log_cost_late(&c, 5000.0, 5020.0);
    log_cost_writes(&c, 40, 5.5, 3.0, "window: perf classes", 1.0);
    log_cost_writes(&c, 2, 9.0, 9.0, "rd: GPU clock", 0.5);
    log_cost_slow_write(&c, 1100.0, 1109.0); /* inside the stall */
    log_cost_slow_write(&c, 5010.0, 5019.0); /* inside the late step */
    log_cost_pipe(&c, 2, 64);
    log_cost_pipe(&c, 1, 0);
    log_cost_job(&c, 30.8, 4800.0, 5350.0);
    c.bytes = 8192;
    c.printValid = 1;
    c.printMs = 1.25;
    c.print = (LogCostSpan){9990.0, 9991.25};
    log_cost_text(&c, line, sizeof line);
    CHECK_STR(line, "logging: 8.0 KB written to the log in the block (every thread's); the window "
                    "thread's 10 s lines 1.25 ms (formatting and writing; 0 during a present that "
                    "blocked, 0 during a late game step); the log thread's file writes 42 in 14.5 "
                    "ms, the longest 9.0 ms (from \"rd: GPU clock\"), 2 of 8 ms or more (1 during "
                    "a present that blocked, 1 during a late game step); logcat 1.5 ms; the pipe "
                    "at most 2 of 64 KB waiting (never full: no writer waited); captures 1: 30.8 "
                    "ms on the game thread (the longest 30.8 ms), 550 ms on the worker (0 during a "
                    "present that blocked, 1 during a late game step); 1 presents blocked, 1 late "
                    "game steps in the block");
    log_cost_pipe(&c, 64, 64);
    log_cost_text(&c, line, sizeof line);
    CHECK(strstr(line, "the pipe at most 64 of 64 KB waiting (full: a writer waited for the log "
                       "thread)") != NULL);
}

static void testDesktop(void)
{
    LogCost c;
    char line[512];
    log_cost_reset(&c);
    c.printValid = 1;
    c.printMs = 0.5;
    c.print = (LogCostSpan){10.0, 10.5};
    log_cost_present(&c, 10.2, 70.0); /* the print during a blocked present */
    log_cost_text(&c, line, sizeof line);
    CHECK_STR(line, "logging: the log's bytes not known; the window thread's 10 s lines 0.50 ms "
                    "(formatting and writing; 1 during a present that blocked, 0 during a late "
                    "game step); no log thread: each line written on its own thread; 1 presents "
                    "blocked, 0 late game steps in the block");
    /* spans past the table: counted, not kept */
    log_cost_reset(&c);
    for (unsigned i = 0; i < LOG_COST_SPANS + 5u; i++) {
        log_cost_late(&c, (double)i, (double)i + 0.5);
        log_cost_slow_write(&c, (double)i, (double)i + 0.5);
    }
    CHECK(c.lateAll == LOG_COST_SPANS + 5u && c.lateN == LOG_COST_SPANS);
    CHECK(c.slow == LOG_COST_SPANS + 5u && c.slowN == LOG_COST_SPANS);
    log_cost_writes(&c, 1, 1.0, 1.0, NULL, 0.0);
    log_cost_text(&c, line, sizeof line);
    CHECK(strstr(line, "; the first 32 compared)") != NULL);
    char small[20];
    const int w = log_cost_text(&c, small, sizeof small);
    CHECK(w > (int)sizeof small && strlen(small) == sizeof small - 1);
}

int main(void)
{
    testOverlaps();
    testPhone();
    testDesktop();
    printf("log_cost_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
