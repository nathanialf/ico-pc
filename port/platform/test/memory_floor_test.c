/*
 * port/platform/test/memory_floor_test.c
 *
 * The start-up set's free-memory floor (memory_floor.h, issue 161):
 *   floor    the default is 17 % of MemTotal, at most 1536 MB on a device
 *            of more than 8192 MB: 696 MB on a 4 GB (4096 MB) phone, 522 MB
 *            on a 3 GB one, 870 MB on 5120 MB, 1392 MB on 8 GB (8192 MB),
 *            1445 MB on 8500 MB (17 % under the cap), 1536 MB on 12 GB
 *            (12288 MB), 16 GB (16384 MB) and the Retroid Pocket 6's
 *            11265 MB; a setting above 0 is the floor (above the cap too),
 *            0 turns it off;
 *   dxt48    the v0.9.8-rc1 start on the PowerVR DXT-48 phone (15552 MB):
 *            2554 MB available (where the plain 17 %, 2643 MB, stopped the
 *            set after 33 of 1193) and 2647 MB (in play) continue under the
 *            1536 MB floor, 1535 MB stops; with [dev]
 *            pipeline_memory_floor_mb = 2643 the old stop comes back;
 *   reporter the v0.9.5 start's figures on the 11265 MB device: 3340 MB
 *            available (147 made) and 1693 MB (211 made, the stall)
 *            continue under the default floor (the system's low-memory
 *            signal, at 1900 MB on that console, the careful budget and the
 *            creation limit stop it there); 1536 MB continues, 1535 MB
 *            stops; with [dev] pipeline_memory_floor_mb = 1915 1693 stops;
 *   small    a 4 GB phone at 1200 MB available continues, at 600 MB stops
 *            (review finding 1: an absolute 1536 MB stopped it); a 3 GB
 *            phone at 700 MB continues; a 5 GB device at 1.5 GB continues,
 *            at 800 MB stops;
 *   unknown  an unknown figure (below 0) never stops;
 *   rule     the log lines' words for the default, the setting and off;
 *   setting  "", "auto" unset; "0" off; "2048" 2048; "-1", "x", "12 MB"
 *            not read.
 */
#include <stdio.h>
#include <string.h>
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
    CHECK(ico_memory_floor_mb(4096, -1) == 696);
    CHECK(ico_memory_floor_mb(3072, -1) == 522);
    CHECK(ico_memory_floor_mb(5120, -1) == 870);
    CHECK(ico_memory_floor_mb(8192, -1) == 1392);
    CHECK(ico_memory_floor_mb(8500, -1) == 1445);
    CHECK(ico_memory_floor_mb(11265, -1) == 1536);
    CHECK(ico_memory_floor_mb(12288, -1) == 1536);
    CHECK(ico_memory_floor_mb(15552, -1) == 1536);
    CHECK(ico_memory_floor_mb(16384, -1) == 1536);
    CHECK(ico_memory_floor_mb(11265, 0) == 0);
    CHECK(ico_memory_floor_mb(16384, 0) == 0);
    CHECK(ico_memory_floor_mb(11265, 1900) == 1900);
    CHECK(ico_memory_floor_mb(16384, 2643) == 2643);
    CHECK(ico_memory_floor_mb(4096, 1000) == 1000);
}

static void testDxt48(void)
{
    CHECK(!ico_memory_floor_stops(2554, 15552, -1));
    CHECK(!ico_memory_floor_stops(2647, 15552, -1));
    CHECK(!ico_memory_floor_stops(1536, 15552, -1));
    CHECK(ico_memory_floor_stops(1535, 15552, -1));
    CHECK(ico_memory_floor_stops(2554, 15552, 2643));
    CHECK(!ico_memory_floor_stops(2554, 15552, 0));
}

static void testReporter(void)
{
    CHECK(!ico_memory_floor_stops(3340, 11265, -1));
    CHECK(!ico_memory_floor_stops(1693, 11265, -1));
    CHECK(!ico_memory_floor_stops(1536, 11265, -1));
    CHECK(ico_memory_floor_stops(1535, 11265, -1));
    CHECK(ico_memory_floor_stops(1693, 11265, 1915));
    CHECK(ico_memory_floor_stops(1693, 11265, 1900));
    CHECK(!ico_memory_floor_stops(1693, 11265, 1600));
    CHECK(!ico_memory_floor_stops(3340, 11265, 1900));
    CHECK(!ico_memory_floor_stops(100, 11265, 0));
}

static void testSmall(void)
{
    CHECK(!ico_memory_floor_stops(1200, 4096, -1));
    CHECK(ico_memory_floor_stops(600, 4096, -1));
    CHECK(!ico_memory_floor_stops(700, 3072, -1));
    CHECK(!ico_memory_floor_stops(1500, 5120, -1));
    CHECK(ico_memory_floor_stops(800, 5120, -1));
}

static void testUnknown(void)
{
    CHECK(!ico_memory_floor_stops(-1, 11265, -1));
    CHECK(!ico_memory_floor_stops(1000, -1, 2000));
}

static void testRule(void)
{
    CHECK(strcmp(ico_memory_floor_rule(-1), "17 % of total, at most 1536 MB above 8 GB") == 0);
    CHECK(strcmp(ico_memory_floor_rule(2643), "[dev] pipeline_memory_floor_mb") == 0);
    CHECK(strcmp(ico_memory_floor_rule(0), "off") == 0);
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
    testDxt48();
    testReporter();
    testSmall();
    testUnknown();
    testRule();
    testSetting();
    printf("memory_floor_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
