/*
 * port/platform/test/present_hist_test.c
 *
 * The 10 s block's histogram of the driver's present call (present_hist.h,
 * issue 123):
 *   rows     a call is counted in every row it is over, the limits
 *            themselves are not over (20 ms is not over 20 ms);
 *   reporter the v0.10.0 log's block: 750 presents of 8.8 ms, 247 of them
 *            9-15 ms, five of 344-365 ms: over 20/50/100/300 is 5 each;
 *   text     the line's words, "" for an empty block, a small buffer cut;
 *   bad      negative and NaN times are not counted;
 *   reset    a reset block is empty.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "present_hist.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    PresentHist h;
    char line[256];

    present_hist_reset(&h);
    CHECK(present_hist_text(&h, line, sizeof line) == 0 && line[0] == '\0');
    present_hist_add(&h, 20.0);
    CHECK(h.n == 1 && h.over[0] == 0);
    present_hist_add(&h, 20.5);
    CHECK(h.over[0] == 1 && h.over[1] == 0);
    present_hist_add(&h, 50.5);
    CHECK(h.over[0] == 2 && h.over[1] == 1 && h.over[2] == 0);
    present_hist_add(&h, 120.0);
    CHECK(h.over[2] == 1 && h.over[3] == 0);
    present_hist_add(&h, 300.5);
    CHECK(h.over[0] == 4 && h.over[1] == 3 && h.over[2] == 2 && h.over[3] == 1);
    CHECK(h.maxMs == 300.5);
    CHECK(present_hist_limit(0) == 20.0 && present_hist_limit(3) == 300.0);
    CHECK(present_hist_limit(-1) == 0.0 && present_hist_limit(4) == 0.0);

    /* bad times */
    present_hist_add(&h, -1.0);
    present_hist_add(&h, NAN);
    CHECK(h.n == 5);

    /* the reporter's block */
    present_hist_reset(&h);
    CHECK(h.n == 0 && h.maxMs == 0.0 && h.totalMs == 0.0);
    for (int i = 0; i < 503; i++) {
        present_hist_add(&h, 8.8);
    }
    for (int i = 0; i < 247; i++) {
        present_hist_add(&h, 9.0 + (double)(i % 7));
    }
    {
        const double big[5] = {344.0, 351.0, 360.0, 362.0, 365.0};
        for (int i = 0; i < 5; i++) {
            present_hist_add(&h, big[i]);
        }
    }
    CHECK(h.n == 755);
    CHECK(h.over[0] == 5 && h.over[1] == 5 && h.over[2] == 5 && h.over[3] == 5);
    CHECK(h.maxMs == 365.0);
    present_hist_text(&h, line, sizeof line);
    CHECK(strncmp(line,
                  "present calls: 755 timed (over 20 ms 5, over 50 ms 5, over 100 ms 5, "
                  "over 300 ms 5), longest 365 ms, total ",
                  strlen("present calls: 755 timed (over 20 ms 5, over 50 ms 5, over 100 ms 5, "
                         "over 300 ms 5), longest 365 ms, total ")) == 0);
    /* a small buffer is cut and terminated */
    {
        char small[12];
        present_hist_text(&h, small, sizeof small);
        CHECK(strlen(small) == 11 && strncmp(small, "present cal", 11) == 0);
    }
    printf("present_hist_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
