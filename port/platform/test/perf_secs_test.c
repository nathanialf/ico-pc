/*
 * port/platform/test/perf_secs_test.c
 *
 * Pictures a second by stage (perf_secs.h): a 10 s block at 25 pictures a
 * second with a 0.4 s hitch (its second reads 15), a stage change inside a
 * second (the second keeps the stage of its first step) and the block's
 * cut asked from then until the line is taken, the part second at the end,
 * pictures before any step ("?"), a quiet end (zeros to the end), seconds
 * past the 64 held counted apart, no second begun (no line) and a short
 * buffer cut, the lines word for word.
 */
#include <stdio.h>
#include <string.h>

#include "perf_secs.h"

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

/* steps every 20 ms in stage from t0 to t1 (ms, t1 left out); a picture
   at every multiple of 40 ms, none in [gap0, gap1) */
static void play(PerfSecs *s, int stage, int t0, int t1, int gap0, int gap1)
{
    for (int t = t0; t < t1; t += 20) {
        perf_secs_step(s, stage, (double)t);
        if (t % 40 == 0 && !(t >= gap0 && t < gap1)) {
            perf_secs_picture(s, (double)t);
        }
    }
}

int main(void)
{
    PerfSecs s;
    char b[512];

    /* nothing begun: no line, and the next block starts there */
    perf_secs_reset(&s);
    CHECK(perf_secs_block_text(&s, 500.0, b, sizeof(b)) == 0 && b[0] == '\0');
    CHECK(s.t0Ms == 500.0 && !perf_secs_cut_due(&s));

    /* 25 a second, a hitch of 0.4 s in the fourth second */
    perf_secs_reset(&s);
    play(&s, 42, 0, 10000, 3000, 3400);
    CHECK(!perf_secs_cut_due(&s));
    CHECK(perf_secs_block_text(&s, 10000.0, b, sizeof(b)) == strlen(b));
    CHECK(text_is(b, "pictures a second: 42: 25 25 25 15 25 25 25 25 25 25"));

    /* a stage change at 4.5 s: the fifth second keeps 42 (its first step);
       the cut asked; the part second's pictures */
    perf_secs_reset(&s);
    play(&s, 42, 0, 4500, -1, -1);
    CHECK(!perf_secs_cut_due(&s));
    play(&s, 40, 4500, 6500, -1, -1);
    CHECK(perf_secs_cut_due(&s));
    (void)perf_secs_block_text(&s, 6500.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 42: 25 25 25 25 25 | 40: 25 (and 13 in the block's last "
                     "0.5 s)"));
    /* the next block: from 6.5 s in the stage now, no cut asked */
    CHECK(!perf_secs_cut_due(&s) && s.stage == 40 && s.t0Ms == 6500.0);
    play(&s, 40, 6500, 8500, 7500, 8500);
    (void)perf_secs_block_text(&s, 8500.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 40: 25 0"));

    /* pictures before any step; a quiet end: the seconds to the end */
    perf_secs_reset(&s);
    perf_secs_picture(&s, 0.0);
    perf_secs_picture(&s, 500.0);
    perf_secs_step(&s, 3, 1200.0);
    perf_secs_picture(&s, 1300.0);
    (void)perf_secs_block_text(&s, 4200.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: ?: 2 | 3: 1 0 0 (and 0 in the block's last 0.2 s)"));

    /* a block under 1 s */
    perf_secs_reset(&s);
    play(&s, 7, 0, 600, -1, -1);
    (void)perf_secs_block_text(&s, 600.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 7: no whole second (and 15 in the block's last 0.6 s)"));

    /* seconds past the 64 held: counted apart */
    perf_secs_reset(&s);
    perf_secs_step(&s, 1, 0.0);
    for (int i = 0; i < 70; i++) {
        perf_secs_picture(&s, 1000.0 * i + 10.0);
    }
    (void)perf_secs_block_text(&s, 70000.0, b, sizeof(b));
    CHECK(strstr(b, "pictures a second: 1: 1 1 1") == b);
    CHECK(strstr(b, " 1 1; 6 seconds past the 64 held, 6 pictures in them") != NULL);

    /* a short buffer: cut and ended */
    {
        char t[16];
        perf_secs_reset(&s);
        play(&s, 42, 0, 5000, -1, -1);
        const size_t n = perf_secs_block_text(&s, 5000.0, t, sizeof(t));
        CHECK(n == sizeof(t) - 1u && strlen(t) == n);
    }

    if (fails) {
        printf("perf_secs_test: %d failures\n", fails);
        return 1;
    }
    printf("perf_secs_test: ok\n");
    return 0;
}
