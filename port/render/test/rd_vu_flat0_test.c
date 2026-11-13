/* rd_vu_flat0_test.c: the main vertex shader's flat outputs at the
 * provoking corner, the pure parts, no device (port/render/rd_vu_flat0.c,
 * rd_vu_flat0.h; port/shaders/vu_tri.hlsl vu_main_vs, ICO_VU_FLAT0).
 *
 *   rd_vu_flat0_test
 *
 *   t1  the tests' switch: unset, empty and 1 are the shipped build, 0 the
 *       twins, any other word the shipped build and named as not 0 or 1
 *   t2  the start line: the shipped form starts with the words the A/B and
 *       a reporter's log are read by, the load counts as the header gives
 *       them; the twins' form names both twins; a twin that could not be
 *       made is named after the shipped form; a short buffer is cut, not
 *       overrun
 *   t3  the counts: corner 0 reads what every corner read before (the
 *       corner's vertex 3, slot A 1, the planes 5), the others the same
 *       less the planes' five */
#include <stdio.h>
#include <string.h>

#include "rd_vu_flat0.h"

static int s_fail;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void t1(void)
{
    bool bad = true;
    CHECK(rd_vu_flat0_word(NULL, &bad) && !bad, "unset: on");
    bad = true;
    CHECK(rd_vu_flat0_word("", &bad) && !bad, "empty: on");
    bad = true;
    CHECK(rd_vu_flat0_word("1", &bad) && !bad, "1: on");
    bad = true;
    CHECK(!rd_vu_flat0_word("0", &bad) && !bad, "0: off");
    bad = false;
    CHECK(rd_vu_flat0_word("off", &bad) && bad, "off: on, named");
    bad = false;
    CHECK(rd_vu_flat0_word("00", &bad) && bad, "00: on, named");
    CHECK(rd_vu_flat0_word("2", NULL), "2 without a flag: on");
}

static void t2(void)
{
    char buf[512];
    int n = rd_vu_flat0_line(buf, sizeof(buf), true, NULL);
    CHECK(n > 0 && (size_t)n < sizeof(buf), "on: %d bytes", n);
    const char *want =
        "VU flat outputs: provoking corner only (9 loads a corner-0 invocation, 4 at "
        "the others";
    CHECK(strncmp(buf, want, strlen(want)) == 0, "on: \"%s\"", buf);
    CHECK(strstr(buf, "could not be made") == NULL, "on: no twin named missing: \"%s\"", buf);
    n = rd_vu_flat0_line(buf, sizeof(buf), false, NULL);
    CHECK(n > 0 && (size_t)n < sizeof(buf), "off: %d bytes", n);
    const char *wantOff = "VU flat outputs: every corner (ICO_RD_VU_FLAT0=0";
    CHECK(strncmp(buf, wantOff, strlen(wantOff)) == 0, "off: \"%s\"", buf);
    CHECK(strstr(buf, "vu_all_main_vs") != NULL && strstr(buf, "vu_merge_all_main_vs") != NULL,
          "off: both twins named: \"%s\"", buf);
    CHECK(strstr(buf, "9 loads an invocation") != NULL, "off: the count: \"%s\"", buf);
    n = rd_vu_flat0_line(buf, sizeof(buf), true, "vu_merge_all_main_vs");
    CHECK(n > 0 && (size_t)n < sizeof(buf), "missing: %d bytes", n);
    CHECK(strncmp(buf, want, strlen(want)) == 0, "missing: the shipped form first: \"%s\"", buf);
    CHECK(strstr(buf, "ICO_RD_VU_FLAT0=0 asked, but vu_merge_all_main_vs could not be made") !=
              NULL,
          "missing: \"%s\"", buf);
    char small[16];
    memset(small, 'x', sizeof(small));
    n = rd_vu_flat0_line(small, 8, true, NULL);
    CHECK(n > 8 && small[7] == '\0' && small[8] == 'x', "short buffer: cut at 7, %d wanted", n);
}

static void t3(void)
{
    CHECK(RD_VU_FLAT0_LOADS_ALL == 3u + 1u + 5u, "every corner before: %u", RD_VU_FLAT0_LOADS_ALL);
    CHECK(RD_VU_FLAT0_LOADS_FIRST == RD_VU_FLAT0_LOADS_ALL, "corner 0: %u",
          RD_VU_FLAT0_LOADS_FIRST);
    CHECK(RD_VU_FLAT0_LOADS_OTHER == RD_VU_FLAT0_LOADS_ALL - 5u, "corners 1 and 2: %u",
          RD_VU_FLAT0_LOADS_OTHER);
}

int main(void)
{
    t1();
    t2();
    t3();
    if (s_fail) {
        printf("rd_vu_flat0_test: %d failures\n", s_fail);
        return 1;
    }
    printf("rd_vu_flat0_test: ok\n");
    return 0;
}
