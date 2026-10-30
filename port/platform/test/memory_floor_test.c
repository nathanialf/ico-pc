/*
 * port/platform/test/memory_floor_test.c
 *
 * The start-up set's free-memory floor (memory_floor.h, issue 161):
 *   floor    the default is the larger of 1536 MB and 17 % of MemTotal:
 *            1915 MB on the Retroid Pocket 6's 11265 MB, 1536 MB on a
 *            5120 MB or smaller device; a setting above 0 is the floor, 0
 *            turns it off;
 *   reporter the v0.9.5 start's figures on the 11265 MB device: 3340 MB
 *            available (147 made) continues; 1693 MB (211 made, the stall)
 *            is under the default floor and stops, as is 1900 MB (the level
 *            v0.9.4 got Android's "running critical" signal at); 1915 MB
 *            continues; with [dev] pipeline_memory_floor_mb = 1600, 1693
 *            continues;
 *   small    a 5 GB device at 2 GB available continues, at 1.5 GB stops;
 *   unknown  an unknown figure (below 0) never stops;
 *   setting  "", "auto" unset; "0" off; "2048" 2048; "-1", "x", "12 MB"
 *            not read.
 */
#include <stdio.h>
#include "memory_floor.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testFloor(void)
{
    CHECK(ico_memory_floor_mb(11265, -1) == 1915);
    CHECK(ico_memory_floor_mb(5120, -1) == 1536);
    CHECK(ico_memory_floor_mb(3072, -1) == 1536);
    CHECK(ico_memory_floor_mb(16384, -1) == 2785);
    CHECK(ico_memory_floor_mb(11265, 0) == 0);
    CHECK(ico_memory_floor_mb(11265, 1900) == 1900);
}

static void testReporter(void)
{
    CHECK(!ico_memory_floor_stops(3340, 11265, -1));
    CHECK(ico_memory_floor_stops(1693, 11265, -1));
    CHECK(ico_memory_floor_stops(1900, 11265, -1));
    CHECK(!ico_memory_floor_stops(1915, 11265, -1));
    CHECK(ico_memory_floor_stops(1914, 11265, -1));
    CHECK(!ico_memory_floor_stops(1693, 11265, 1600));
    CHECK(ico_memory_floor_stops(1693, 11265, 1900));
    CHECK(!ico_memory_floor_stops(3340, 11265, 1900));
    CHECK(!ico_memory_floor_stops(100, 11265, 0));
}

static void testSmall(void)
{
    CHECK(!ico_memory_floor_stops(2048, 5120, -1));
    CHECK(ico_memory_floor_stops(1500, 5120, -1));
}

static void testUnknown(void)
{
    CHECK(!ico_memory_floor_stops(-1, 11265, -1));
    CHECK(!ico_memory_floor_stops(1000, -1, 2000));
}

static void testSetting(void)
{
    CHECK(ico_memory_floor_setting(NULL) == -1);
    CHECK(ico_memory_floor_setting("") == -1);
    CHECK(ico_memory_floor_setting("auto") == -1);
    CHECK(ico_memory_floor_setting("0") == 0);
    CHECK(ico_memory_floor_setting("2048") == 2048);
    CHECK(ico_memory_floor_setting("-1") == -2);
    CHECK(ico_memory_floor_setting("x") == -2);
    CHECK(ico_memory_floor_setting("12 MB") == -2);
}

int main(void)
{
    testFloor();
    testReporter();
    testSmall();
    testUnknown();
    testSetting();
    printf("memory_floor_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
