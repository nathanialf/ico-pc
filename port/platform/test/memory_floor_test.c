/*
 * port/platform/test/memory_floor_test.c
 *
 * The start-up set's free-memory floor (memory_floor.h, issue 161):
 *   floor    Qualcomm's own driver (Q, and a driver not known, U): 17 % of
 *            MemTotal, at most 2048 MB on a device of more than 8192 MB:
 *            696 MB on a 4 GB (4096 MB) phone, 522 MB on a 3 GB one, 870
 *            MB on 5120 MB, 1392 MB on 8 GB, 1445 MB on 8500 MB, 1915 MB
 *            on the Retroid Pocket 6's 11265 MB, 2048 MB on 12 GB and 16
 *            GB; any other driver (O): the same 17 % up to 8 GB, at most
 *            512 MB above (512 MB on 8500, 11265, 12288, 15552 and 16384
 *            MB); a setting above 0 is the floor for every class, 0 turns
 *            it off;
 *   driver   the class from the vendor id and the driver text: 0x5143
 *            with Qualcomm's own text Q, with "turnip Mesa driver" or
 *            "Mesa 25.1.0" (any case) O, PowerVR (0x1010) and Mali
 *            (0x13B5) O, vendor 0 U; the class's words;
 *   dxt48    the PowerVR DXT-48 phone (15552 MB, class O): a start's
 *            1823 MB (stopped under the 2048 MB cap: the rest made on first
 *            use, a 2.2 s freeze in play) and another start's 2554 MB
 *            continue under 512 MB, 511 MB stops; with [dev]
 *            pipeline_memory_floor_mb = 2048 1823 stops again;
 *   reporter the issue 161 start's figures on the 11265 MB device on
 *            Qualcomm's own driver: 3340 MB available continues and 1693
 *            MB (the stall) stops under its floor of 1915 MB; 1915 MB
 *            continues, 1914 MB stops; with [dev] pipeline_memory_floor_mb
 *            = 1600 1693 continues; the same figures on Turnip (class O)
 *            continue;
 *   small    a 4 GB phone at 1200 MB available continues, at 600 MB stops
 *            on every class (review finding 1: an absolute 1536 MB stopped
 *            it); a 3 GB phone at 700 MB continues; a 5 GB device at 1.5
 *            GB continues, at 800 MB stops;
 *   unknown  an unknown figure (below 0) never stops;
 *   rule     the log lines' words for each class, the setting and off;
 *   setting  "", "auto" unset; "0" off; "2048" 2048; "-1", "x", "12 MB"
 *            not read;
 *   retry    issue 123 (ico_memory_floor_retry): the next start's set again
 *            only after a stop at the floor, on Qualcomm's own driver, with a
 *            cache file that is not empty; the activity line told from the
 *            system's own signal by its floor figures; the reporter's
 *            1940 MB of 11453 MB against the floor of 1947 MB.
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

enum {
    U = ICO_MEMORY_FLOOR_DRIVER_UNKNOWN,
    Q = ICO_MEMORY_FLOOR_DRIVER_QUALCOMM,
    O = ICO_MEMORY_FLOOR_DRIVER_OTHER
};

static void testFloor(void)
{
    const int classes[2] = {Q, U};
    for (int c = 0; c < 2; c++) {
        const int d = classes[c];
        CHECK(ico_memory_floor_mb(4096, -1, d) == 696);
        CHECK(ico_memory_floor_mb(3072, -1, d) == 522);
        CHECK(ico_memory_floor_mb(5120, -1, d) == 870);
        CHECK(ico_memory_floor_mb(8192, -1, d) == 1392);
        CHECK(ico_memory_floor_mb(8500, -1, d) == 1445);
        CHECK(ico_memory_floor_mb(11265, -1, d) == 1915);
        CHECK(ico_memory_floor_mb(12288, -1, d) == 2048);
        CHECK(ico_memory_floor_mb(15552, -1, d) == 2048);
        CHECK(ico_memory_floor_mb(16384, -1, d) == 2048);
    }
    CHECK(ico_memory_floor_mb(4096, -1, O) == 696);
    CHECK(ico_memory_floor_mb(3072, -1, O) == 522);
    CHECK(ico_memory_floor_mb(5120, -1, O) == 870);
    CHECK(ico_memory_floor_mb(8192, -1, O) == 1392);
    CHECK(ico_memory_floor_mb(8500, -1, O) == 512);
    CHECK(ico_memory_floor_mb(11265, -1, O) == 512);
    CHECK(ico_memory_floor_mb(12288, -1, O) == 512);
    CHECK(ico_memory_floor_mb(15552, -1, O) == 512);
    CHECK(ico_memory_floor_mb(16384, -1, O) == 512);
    for (int d = U; d <= O; d++) {
        CHECK(ico_memory_floor_mb(11265, 0, d) == 0);
        CHECK(ico_memory_floor_mb(16384, 0, d) == 0);
        CHECK(ico_memory_floor_mb(11265, 1900, d) == 1900);
        CHECK(ico_memory_floor_mb(16384, 2643, d) == 2643);
        CHECK(ico_memory_floor_mb(4096, 1000, d) == 1000);
    }
}

static void testDriver(void)
{
    CHECK(ico_memory_floor_driver(0x5143u, "driver Qualcomm 512.676.53, version 512.676.53") == Q);
    CHECK(ico_memory_floor_driver(0x5143u, NULL) == Q);
    CHECK(ico_memory_floor_driver(0x5143u, "turnip Mesa driver") == O);
    CHECK(ico_memory_floor_driver(0x5143u, "Turnip Mesa 26.3.0") == O);
    CHECK(ico_memory_floor_driver(0x5143u, "Mesa 25.1.0") == O);
    CHECK(ico_memory_floor_driver(0x1010u, "PowerVR D-Series Vulkan Driver 26.1@7036548") == O);
    CHECK(ico_memory_floor_driver(0x13B5u, "Mali-G52") == O);
    CHECK(ico_memory_floor_driver(0u, "anything") == U);
    CHECK(strcmp(ico_memory_floor_driver_words(Q), "Qualcomm's own driver") == 0);
    CHECK(strcmp(ico_memory_floor_driver_words(O), "another driver than Qualcomm's own") == 0);
    CHECK(strcmp(ico_memory_floor_driver_words(U), "the driver not known yet") == 0);
}

static void testDxt48(void)
{
    CHECK(!ico_memory_floor_stops(1823, 15552, -1, O));
    CHECK(!ico_memory_floor_stops(2554, 15552, -1, O));
    CHECK(!ico_memory_floor_stops(512, 15552, -1, O));
    CHECK(ico_memory_floor_stops(511, 15552, -1, O));
    CHECK(ico_memory_floor_stops(1823, 15552, -1, Q)); /* that stop, under the 2048 MB cap */
    CHECK(ico_memory_floor_stops(1823, 15552, 2048, O));
    CHECK(!ico_memory_floor_stops(1823, 15552, 0, O));
}

static void testReporter(void)
{
    CHECK(!ico_memory_floor_stops(3340, 11265, -1, Q));
    CHECK(ico_memory_floor_stops(1693, 11265, -1, Q));
    CHECK(!ico_memory_floor_stops(1915, 11265, -1, Q));
    CHECK(ico_memory_floor_stops(1914, 11265, -1, Q));
    CHECK(ico_memory_floor_stops(1693, 11265, -1, U));
    CHECK(ico_memory_floor_stops(1693, 11265, 1900, Q));
    CHECK(!ico_memory_floor_stops(1693, 11265, 1600, Q));
    CHECK(!ico_memory_floor_stops(3340, 11265, 1900, Q));
    CHECK(!ico_memory_floor_stops(100, 11265, 0, Q));
    CHECK(!ico_memory_floor_stops(1693, 11265, -1, O)); /* Turnip on the same console */
}

static void testSmall(void)
{
    for (int d = U; d <= O; d++) {
        CHECK(!ico_memory_floor_stops(1200, 4096, -1, d));
        CHECK(ico_memory_floor_stops(600, 4096, -1, d));
        CHECK(!ico_memory_floor_stops(700, 3072, -1, d));
        CHECK(!ico_memory_floor_stops(1500, 5120, -1, d));
        CHECK(ico_memory_floor_stops(800, 5120, -1, d));
    }
}

static void testUnknown(void)
{
    CHECK(!ico_memory_floor_stops(-1, 11265, -1, Q));
    CHECK(!ico_memory_floor_stops(1000, -1, 2000, O));
}

static void testRule(void)
{
    CHECK(strcmp(ico_memory_floor_rule(-1, O),
                 "the set's need with a margin: 17 % of total, at most 512 MB above 8 GB; "
                 "Qualcomm's own driver keeps 17 % of total, at most 2048 MB above 8 GB") == 0);
    CHECK(strcmp(ico_memory_floor_rule(-1, Q),
                 "Qualcomm's own driver: 17 % of total, at most 2048 MB above 8 GB; other "
                 "drivers at most 512 MB") == 0);
    CHECK(strcmp(ico_memory_floor_rule(-1, U),
                 "the driver not known yet: 17 % of total, at most 2048 MB above 8 GB") == 0);
    CHECK(strcmp(ico_memory_floor_rule(2643, O), "[dev] pipeline_memory_floor_mb") == 0);
    CHECK(strcmp(ico_memory_floor_rule(0, Q), "off") == 0);
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

static void testRetry(void)
{
    char why[200];
    const char *floorLine = "set: stopped at the system's low-memory signal (1940 MB available "
                            "of 11453 MB, floor 1947 MB)";
    const char *sigLine = "set: stopped at the system's low-memory signal (running critical)";

    CHECK(ico_memory_floor_line_is_floor(floorLine));
    CHECK(!ico_memory_floor_line_is_floor(sigLine));
    CHECK(!ico_memory_floor_line_is_floor(""));
    CHECK(!ico_memory_floor_line_is_floor(NULL));
    /* the reporter: floor stop, Qualcomm's own, the 1.4 MB cache file */
    CHECK(ico_memory_floor_stops(1940, 11453, -1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM));
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, 1400000, why, sizeof why) ==
          1);
    CHECK(strstr(why, "on disk") != NULL);
    /* no file, an empty file */
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, -1, why, sizeof why) == 0);
    CHECK(strstr(why, "missing or empty") != NULL);
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, 0, why, sizeof why) == 0);
    /* the system's own signal, another driver, a driver not known */
    CHECK(ico_memory_floor_retry(0, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, 1400000, why, sizeof why) ==
          0);
    CHECK(strstr(why, "did not stop") != NULL);
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_OTHER, 1400000, why, sizeof why) == 0);
    CHECK(strstr(why, "not Qualcomm") != NULL);
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_UNKNOWN, 1400000, why, sizeof why) ==
          0);
    /* no words wanted; a small buffer cuts */
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, 5, NULL, 0) == 1);
    CHECK(ico_memory_floor_retry(1, ICO_MEMORY_FLOOR_DRIVER_QUALCOMM, 5, why, 8) == 1);
    CHECK(strlen(why) == 7);
}

int main(void)
{
    testFloor();
    testDriver();
    testDxt48();
    testReporter();
    testSmall();
    testUnknown();
    testRule();
    testSetting();
    testRetry();
    printf("memory_floor_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
