/*
 * port/platform/test/set_size_test.c
 *
 * The start-up set sized by the device's own memory fall (set_size.h, issue
 * 215).  Every expectation below is the code's arithmetic on the inputs
 * shown (a rest's cost = left x fall rounded to the nearest MB; the
 * headroom = free - floor - need; the median the lower middle):
 *   median    none, one, odd, even (the lower middle), a noisy set;
 *   decide    the handheld's 2D stop (free 6889 MB, floor 1915, need 32,
 *             418 left at 15 MB: 6270 against 4942), its VU stop with 2D
 *             stopped earlier (218 at 24 MB: 5232 against 4366), the
 *             tablet's VU stop with 2D's no fall kept (218 at 14 MB: 3052
 *             against 1512), no fall under the floor never stops, too few
 *             batches decide nothing, a fit kept, no floor or no figure off;
 *   handheld  a whole start of the issue 215 handheld from its 7249 MB
 *             (floor 1915 MB, 15 MB a 2D pipeline, 24 a VU one, batches of
 *             8): 2D stopped at pipeline 25, VU at 358, both lines word for
 *             word, the renderer's words, the phase words;
 *   tablet    the issue 123 tablet from its 3751 MB (floor 1947 MB, free
 *             memory rising 3 MB a 2D batch, 14 MB a VU pipeline): 2D's
 *             "no fall" line, VU stopped at 365, 2D going on after it;
 *   desktop   no floor: off, every ask 0 and no line; another driver's
 *             phone with a floor and no fall: never stopped;
 *   noisy     one batch moved by another process outvoted by the median;
 *   few       two batches of a large fall decide nothing;
 *   mixed     a batch of both families is not a sample; an ask whose memory
 *             was not read keeps the stops and its batches for the next;
 *   text      the start lines (on and off), the first picture's line, and
 *             the room: every text at its widest within its buffer.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "set_size.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the run is 32 KB of samples: static */
static SetSizeRun s_run;

static unsigned ask(long freeMb, unsigned made2d, unsigned madeVu, unsigned left2d, unsigned leftVu,
                    int next, unsigned place, unsigned total, char *line, char *why)
{
    SetSizeAsk a;
    memset(&a, 0, sizeof a);
    a.freeMb = freeMb;
    a.made[0] = made2d;
    a.made[1] = madeVu;
    a.left[0] = left2d;
    a.left[1] = leftVu;
    a.next = next;
    a.needMb = 32; /* the mesh arena's first chunk: the pre-pass is off on both devices */
    a.place = place;
    a.total = total;
    return set_size_ask(&s_run, &a, line, SET_SIZE_LINE_MAX, why, SET_SIZE_WHY_MAX);
}

static void testMedian(void)
{
    const float one[] = {5.0f}, odd[] = {3.0f, 1.0f, 2.0f}, even[] = {4.0f, 1.0f, 3.0f, 2.0f};
    const float noisy[] = {24.0f, -300.0f, 25.0f, 23.0f, 600.0f};
    CHECK(set_size_median(NULL, 0) == 0.0);
    CHECK(set_size_median(one, 1) == 5.0);
    CHECK(set_size_median(odd, 3) == 2.0);
    CHECK(set_size_median(even, 4) == 2.0);
    CHECK(set_size_median(noisy, 5) == 24.0);
}

static void testDecide(void)
{
    SetSizeIn in;
    SetSizeOut o;

    /* the handheld after three 2D batches */
    memset(&in, 0, sizeof in);
    in.freeMb = 6889;
    in.floorMb = 1915;
    in.needMb = 32;
    in.left[0] = 418;
    in.left[1] = 242;
    in.next = SET_SIZE_2D;
    in.batches[0] = 3;
    in.fallMb[0] = 15.0;
    set_size_decide(&in, &o);
    CHECK(o.active == 1 && o.headroomMb == 4942);
    CHECK(o.costMb[0] == 6270 && o.costMb[1] == -1);
    CHECK(o.stop == 1u && o.newly == 1u && o.keptMb == 0);

    /* ... after three VU batches, 2D stopped earlier */
    in.freeMb = 6313;
    in.left[0] = 108;
    in.left[1] = 218;
    in.next = SET_SIZE_VU;
    in.batches[1] = 3;
    in.fallMb[1] = 24.0;
    in.stopped = 1u;
    set_size_decide(&in, &o);
    CHECK(o.headroomMb == 4366 && o.costMb[0] == 1620 && o.costMb[1] == 5232);
    CHECK(o.stop == 3u && o.newly == 2u && o.keptMb == 0);

    /* the tablet: 2D no fall, VU 14 MB a pipeline */
    memset(&in, 0, sizeof in);
    in.freeMb = 3491;
    in.floorMb = 1947;
    in.needMb = 32;
    in.left[0] = 108;
    in.left[1] = 218;
    in.next = SET_SIZE_VU;
    in.batches[0] = 4;
    in.fallMb[0] = -0.375;
    in.batches[1] = 3;
    in.fallMb[1] = 14.0;
    set_size_decide(&in, &o);
    CHECK(o.headroomMb == 1512 && o.costMb[0] == 0 && o.costMb[1] == 3052);
    CHECK(o.stop == 2u && o.newly == 2u && o.keptMb == 0);

    /* no fall under the floor: never stopped by this rule (the floor's own
       stop is diag_host.c's) */
    memset(&in, 0, sizeof in);
    in.freeMb = 1000;
    in.floorMb = 1915;
    in.needMb = 32;
    in.left[0] = 400;
    in.left[1] = 200;
    in.batches[0] = in.batches[1] = 5;
    in.fallMb[0] = 0.0;
    in.fallMb[1] = -2.0;
    set_size_decide(&in, &o);
    CHECK(o.active == 1 && o.headroomMb == -947 && o.costMb[0] == 0 && o.costMb[1] == 0);
    CHECK(o.stop == 0u && o.newly == 0u);

    /* too few batches: nothing decided, however large the fall */
    in.freeMb = 3000;
    in.batches[0] = in.batches[1] = SET_SIZE_MIN_BATCHES - 1u;
    in.fallMb[0] = in.fallMb[1] = 500.0;
    set_size_decide(&in, &o);
    CHECK(o.costMb[0] == -1 && o.costMb[1] == -1 && o.stop == 0u);

    /* a fit: kept, the family goes on */
    memset(&in, 0, sizeof in);
    in.freeMb = 3751;
    in.floorMb = 1947;
    in.needMb = 32;
    in.left[0] = 316;
    in.left[1] = 242;
    in.batches[0] = 3;
    in.fallMb[0] = 0.5;
    set_size_decide(&in, &o);
    CHECK(o.headroomMb == 1772 && o.costMb[0] == 158 && o.keptMb == 158 && o.stop == 0u);

    /* both families fit alone, not together: the next family's kept first */
    in.left[1] = 100;
    in.batches[1] = 3;
    in.fallMb[1] = 16.5; /* 1650: 158 + 1650 = 1808 > 1772 */
    in.next = SET_SIZE_VU;
    set_size_decide(&in, &o);
    CHECK(o.costMb[1] == 1650 && o.stop == 1u && o.keptMb == 1650);
    in.next = SET_SIZE_2D;
    set_size_decide(&in, &o);
    CHECK(o.stop == 2u && o.keptMb == 158);

    /* off: no floor, the floor off, no free figure */
    in.floorMb = 0;
    set_size_decide(&in, &o);
    CHECK(o.active == 0 && o.stop == 0u && o.costMb[0] == -1);
    in.floorMb = 1947;
    in.freeMb = -1;
    in.stopped = 2u;
    set_size_decide(&in, &o);
    CHECK(o.active == 0 && o.stop == 2u && o.newly == 0u);
}

static void testHandheld(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX], words[SET_SIZE_WORDS_MAX];
    unsigned m;

    set_size_begin(&s_run, 7249, 1915);
    CHECK(s_run.active == 1);
    /* 2D batches of 8, 120 MB each: 15 MB a pipeline */
    CHECK(ask(7249, 0, 0, 442, 242, 0, 0, 684, line, why) == 0u && line[0] == '\0');
    CHECK(ask(7129, 8, 0, 434, 242, 0, 8, 684, line, why) == 0u && line[0] == '\0');
    CHECK(ask(7009, 16, 0, 426, 242, 0, 16, 684, line, why) == 0u && line[0] == '\0');
    CHECK(set_size_words(&s_run, words, sizeof words) == 0 && words[0] == '\0');
    m = ask(6889, 24, 0, 418, 242, 0, 24, 684, line, why);
    CHECK(m == 1u);
    CHECK(strcmp(line,
                 "ico_pc: pipelines: the start-up set's size at pipeline 25 of 684: free 6889 MB "
                 "less the floor 1915 MB less play's known need 32 MB leaves 4942 MB; 2D 15.0 MB "
                 "a pipeline (the median of 3 batches): its 418 left x 15.0 = 6270 MB, stops "
                 "now, the rest made on first use; VU not measured yet (0 of 3 batches), "
                 "measured when reached; the rests kept 0 MB") == 0);
    CHECK(strcmp(why, "2D 15.0 MB a pipeline, its rest 6270 MB, against 4942 MB above the floor") ==
          0);
    (void)set_size_words(&s_run, words, sizeof words);
    CHECK(strcmp(words, "start-up set sized by the memory fall: 2D left to first use (15.0 MB a "
                        "pipeline)") == 0);
    /* the renderer skips 2D from there: its next batch is VU's, 24 MB a
       pipeline (192 MB a batch of 8) */
    CHECK(ask(6697, 24, 8, 108, 234, 1, 342, 684, line, why) == 1u && line[0] == '\0');
    CHECK(ask(6505, 24, 16, 108, 226, 1, 350, 684, line, why) == 1u && line[0] == '\0');
    m = ask(6313, 24, 24, 108, 218, 1, 357, 684, line, why);
    CHECK(m == 3u);
    CHECK(strcmp(line,
                 "ico_pc: pipelines: the start-up set's size at pipeline 358 of 684: free 6313 MB "
                 "less the floor 1915 MB less play's known need 32 MB leaves 4366 MB; 2D 15.0 MB "
                 "a pipeline (the median of 3 batches): its 108 left x 15.0 = 1620 MB, stopped "
                 "earlier; VU 24.0 MB a pipeline (the median of 3 batches): its 218 left x 24.0 = "
                 "5232 MB, stops now, the rest made on first use; the rests kept 0 MB") == 0);
    CHECK(strcmp(why, "VU 24.0 MB a pipeline, its rest 5232 MB, against 4366 MB above the floor") ==
          0);
    (void)set_size_words(&s_run, words, sizeof words);
    CHECK(strcmp(words,
                 "start-up set sized by the memory fall: 2D, VU left to first use (15.0, 24.0 MB "
                 "a pipeline)") == 0);
    CHECK(s_run.leftAtStop[0] == 418 && s_run.leftAtStop[1] == 218);
    CHECK(s_run.placeAtStop[0] == 24 && s_run.placeAtStop[1] == 357);
    CHECK(s_run.lowMb == 6313 && s_run.n[0] == 3 && s_run.n[1] == 3 && s_run.mixed == 0);
}

static void testTablet(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX], words[SET_SIZE_WORDS_MAX];

    set_size_begin(&s_run, 3751, 1947);
    CHECK(ask(3751, 0, 0, 448, 242, 0, 0, 690, line, why) == 0u);
    /* free memory rises 3 MB a 2D batch of 8: -0.375 MB a pipeline */
    CHECK(ask(3754, 8, 0, 440, 242, 0, 8, 690, line, why) == 0u && line[0] == '\0');
    CHECK(ask(3757, 16, 0, 432, 242, 0, 16, 690, line, why) == 0u && line[0] == '\0');
    CHECK(ask(3760, 24, 0, 424, 242, 0, 24, 690, line, why) == 0u);
    CHECK(strcmp(line,
                 "ico_pc: pipelines: the start-up set's size at pipeline 25 of 690: free 3760 MB "
                 "less the floor 1947 MB less play's known need 32 MB leaves 1781 MB; 2D no fall "
                 "(-0.4 MB a pipeline, the median of 3 batches): its 424 left cost nothing, goes "
                 "on; VU not measured yet (0 of 3 batches), measured when reached; the rests kept "
                 "0 MB") == 0);
    CHECK(why[0] == '\0');
    (void)set_size_words(&s_run, words, sizeof words);
    CHECK(strcmp(words, "start-up set sized by the memory fall: every family fits") == 0);
    /* the rest of the 2D head in one step (3827 MB at its end), no line */
    CHECK(ask(3827, 340, 0, 108, 242, 1, 340, 690, line, why) == 0u && line[0] == '\0');
    /* VU, 14 MB a pipeline (112 MB a batch of 8) */
    CHECK(ask(3715, 340, 8, 108, 234, 1, 348, 690, line, why) == 0u);
    CHECK(ask(3603, 340, 16, 108, 226, 1, 356, 690, line, why) == 0u);
    CHECK(ask(3491, 340, 24, 108, 218, 1, 364, 690, line, why) == 2u);
    CHECK(strcmp(line,
                 "ico_pc: pipelines: the start-up set's size at pipeline 365 of 690: free 3491 MB "
                 "less the floor 1947 MB less play's known need 32 MB leaves 1512 MB; 2D no fall "
                 "(-0.4 MB a pipeline, the median of 4 batches): its 108 left cost nothing, goes "
                 "on; VU 14.0 MB a pipeline (the median of 3 batches): its 218 left x 14.0 = 3052 "
                 "MB, stops now, the rest made on first use; the rests kept 0 MB") == 0);
    CHECK(strcmp(why, "VU 14.0 MB a pipeline, its rest 3052 MB, against 1512 MB above the floor") ==
          0);
    /* the sprite planes builds after it (2D, one a batch): 2D goes on */
    CHECK(ask(3491, 341, 24, 107, 218, 0, 583, 690, line, why) == 2u && line[0] == '\0');
    CHECK(ask(3490, 342, 24, 106, 218, 0, 584, 690, line, why) == 2u && line[0] == '\0');
    (void)set_size_words(&s_run, words, sizeof words);
    CHECK(strcmp(words, "start-up set sized by the memory fall: VU left to first use (14.0 MB a "
                        "pipeline)") == 0);
}

static void testDesktop(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX], words[SET_SIZE_WORDS_MAX];
    unsigned i;

    /* no floor (every system but Android): off */
    set_size_begin(&s_run, 20000, 0);
    CHECK(s_run.active == 0);
    for (i = 0; i < 10; i++) {
        CHECK(ask(20000 - (long)i * 500, i * 8, 0, 400 - i * 8, 200, 0, i * 8, 600, line, why) ==
              0u);
        CHECK(line[0] == '\0' && why[0] == '\0');
    }
    CHECK(set_size_words(&s_run, words, sizeof words) == 0);
    /* another driver's phone with a floor, no fall in either family: one
       line each, never stopped, even at the floor */
    set_size_begin(&s_run, 2100, 512);
    for (i = 0; i <= 3; i++) {
        (void)ask(2100, i * 8, 0, 400 - i * 8, 200, 0, i * 8, 600, line, why);
    }
    CHECK(strstr(line, "2D no fall (0.0 MB a pipeline, the median of 3 batches)") != NULL);
    for (i = 1; i <= 3; i++) {
        CHECK(ask(500, 24, i * 8, 376, 200 - i * 8, 1, 24 + i * 8, 600, line, why) == 0u);
    }
    /* the first VU sample saw 2100 -> 500 MB: the median of three ignores it */
    CHECK(strstr(line, "VU no fall (0.0 MB a pipeline, the median of 3 batches)") != NULL);
    CHECK(s_run.stopped == 0u);
}

static void testNoisy(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX];

    set_size_begin(&s_run, 8000, 1915);
    (void)ask(8000, 0, 0, 0, 242, 1, 0, 242, line, why);
    (void)ask(7808, 0, 8, 0, 234, 1, 8, 242, line, why);           /* 24 */
    (void)ask(10208, 0, 16, 0, 226, 1, 16, 242, line, why);        /* -300: another process freed */
    CHECK(ask(10008, 0, 24, 0, 218, 1, 24, 242, line, why) == 0u); /* 25 */
    /* the median 24: 218 x 24 = 5232 within 10008 - 1915 - 32 = 8061 */
    CHECK(strstr(line, "VU 24.0 MB a pipeline (the median of 3 batches): its 218 left x 24.0 = "
                       "5232 MB, goes on") != NULL);
}

static void testFew(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX];

    set_size_begin(&s_run, 3000, 1915);
    (void)ask(3000, 0, 0, 0, 242, 1, 0, 242, line, why);
    CHECK(ask(2200, 0, 8, 0, 234, 1, 8, 242, line, why) == 0u && line[0] == '\0');
    CHECK(ask(1400, 0, 16, 0, 226, 1, 16, 242, line, why) == 0u && line[0] == '\0');
    CHECK(s_run.n[1] == 2 && s_run.stopped == 0u);
}

static void testMixed(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX];

    set_size_begin(&s_run, 7000, 1915);
    (void)ask(7000, 0, 0, 100, 100, 0, 0, 200, line, why);
    /* a batch of both families: no sample */
    (void)ask(6800, 4, 4, 96, 96, 0, 8, 200, line, why);
    CHECK(s_run.n[0] == 0 && s_run.n[1] == 0 && s_run.mixed == 1);
    /* the memory not read: nothing taken, the batch counted at the next */
    CHECK(ask(-1, 12, 4, 88, 96, 0, 16, 200, line, why) == 0u && line[0] == '\0');
    CHECK(s_run.n[0] == 0 && s_run.lastMade[0] == 4 && s_run.lastMb == 6800);
    (void)ask(6640, 20, 4, 80, 96, 0, 24, 200, line, why);
    CHECK(s_run.n[0] == 1 && s_run.fall[0][0] == 10.0f);
}

static void testText(void)
{
    char line[SET_SIZE_LINE_MAX], why[SET_SIZE_WHY_MAX], words[SET_SIZE_WORDS_MAX];
    size_t n;
    unsigned i;

    n = set_size_rule_text(line, sizeof line, 1, NULL, 684, 7249, 1915, 0, 32, 0, 0);
    CHECK(n == strlen(line));
    CHECK(strcmp(line,
                 "ico_pc: pipelines: the start-up set's size by the device's memory fall: on (684 "
                 "pipelines; free 7249 MB, the floor 1915 MB; play's known need not made yet 32 "
                 "MB: the VU pre-pass's record 0 MB and the mesh arena's first chunk 32 MB; made "
                 "already, before the free figure was read: the record 0 MB, the chunk 0 MB): "
                 "each family's fall of free memory a pipeline (2D: every pipeline without a VU "
                 "vertex shader; VU) is the median of its batches, trusted after 3; a family "
                 "whose pipelines not made x its fall do not fit in free memory less the floor "
                 "less that need stops at once and is made on first use; a family with no fall "
                 "never stops by this rule") == 0);
    /* a phone whose pre-pass self-test made the record (128 MB) and the
       arena's chunk (32 MB) before the set: nothing to make, both named as
       made */
    (void)set_size_rule_text(line, sizeof line, 1, NULL, 856, 1958, 512, 0, 0, 128, 32);
    CHECK(strstr(line, "; play's known need not made yet 0 MB: the VU pre-pass's record 0 MB and "
                       "the mesh arena's first chunk 0 MB; made already, before the free figure "
                       "was read: the record 128 MB, the chunk 32 MB): ") != NULL);
    (void)set_size_rule_text(line, sizeof line, 0,
                             "no free-memory floor on this system (Android's only)", 600, -1, -1, 0,
                             0, 0, 0);
    CHECK(strcmp(line, "ico_pc: pipelines: the start-up set's size by the device's memory fall: "
                       "off (no free-memory floor on this system (Android's only)): the set as "
                       "before") == 0);
    (void)set_size_first_picture_text(6213, 11265, 6150, 1915,
                                      "start-up set sized by the memory fall: 2D, VU left to first "
                                      "use (15.0, 24.0 MB a pipeline)",
                                      line, sizeof line);
    CHECK(strcmp(line,
                 "ico_pc: memory at the first picture: free 6213 MB of 11265 MB (the floor "
                 "1915 MB), the lowest since the start 6150 MB; the start-up set sized by "
                 "the memory fall: 2D, VU left to first use (15.0, 24.0 MB a pipeline)") == 0);
    (void)set_size_first_picture_text(3400, 11453, -1, 1947, "", line, sizeof line);
    CHECK(strcmp(line, "ico_pc: memory at the first picture: free 3400 MB of 11453 MB (the floor "
                       "1947 MB), the lowest since the start not known; the start-up set made as "
                       "each family fit") == 0);

    /* the room: the start line at its widest (692 bytes) and a decision line
       whose every figure is at the text's widest, within SET_SIZE_LINE_MAX
       and not cut (it ends with its last words) */
    n = set_size_rule_text(line, sizeof line, 1, NULL, UINT_MAX, -9999999L, -9999999L, -9999999L,
                           -9999999L, -9999999L, -9999999L);
    CHECK(n == 692 && n == strlen(line));
    set_size_begin(&s_run, 9999999L, 1);
    (void)ask(9999999L, 0, 0, 4096, 4096, 0, 0, 4096, line, why);
    for (i = 1; i <= 3; i++) {
        /* 2D falls 9999999 MB in one pipeline, then 0 back, then down again */
        (void)ask(i % 2 ? 0 : 9999999L, i, 0, 4096 - i, 4096, 0, i, 4096, line, why);
    }
    /* stopped at the third: the renderer's words at their widest */
    CHECK(s_run.stopped == 1u && strlen(why) < SET_SIZE_WHY_MAX - 1);
    CHECK(strcmp(why, "2D 999999.0 MB a pipeline, its rest 9999999 MB, against -33 MB above the "
                      "floor") == 0);
    for (i = 1; i <= 3; i++) {
        (void)ask(i % 2 ? 9999999L : 0, 3, i, 4093, 4096 - i, 1, 3 + i, 4096, line, why);
    }
    n = strlen(line);
    CHECK(n > 0 && n < SET_SIZE_LINE_MAX - 1);
    CHECK(n >= 9 && strcmp(line + n - 9, "kept 0 MB") == 0);
    n = set_size_words(&s_run, words, sizeof words);
    CHECK(n == strlen(words) && n < sizeof words - 1 && words[n - 1] == ')');
}

int main(void)
{
    testMedian();
    testDecide();
    testHandheld();
    testTablet();
    testDesktop();
    testNoisy();
    testFew();
    testMixed();
    testText();
    if (failures != 0) {
        fprintf(stderr, "set_size_test: %d failures\n", failures);
        return 1;
    }
    printf("set_size_test: ok\n");
    return 0;
}
