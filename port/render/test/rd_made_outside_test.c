/* rd_made_outside_test.c: the made-in-play line's words and its limiter
 * (rd_made_outside.h, rd_made_outside_text.c), pure.
 *
 *   1. the limiter: the 1st to 8th of a name, then the 100th, 200th; not
 *      the 0th, 9th, 99th, 101st;
 *   2. a pipeline's line word for word (the wrap accumulate's name, replay
 *      1, list 12, its 1st; 3 pipelines of 2 names);
 *   3. a buffer's line with its size in KB rounded up and the 8th's tail
 *      ("this name's lines from here every 100th"), the 2nd, 3rd, 11th,
 *      12th, 13th, 21st, 102nd, 111th ordinals;
 *   4. an unnamed buffer, a NULL where, and a small output cut without
 *      overrunning it. */
#include <stdio.h>
#include <string.h>

#include "rd_made_outside.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void same(const char *got, const char *want)
{
    CHECK(strcmp(got, want) == 0, "\n  got  %s\n  want %s", got, want);
}

int main(void)
{
    /* 1. */
    static const uint64_t yes[] = {1, 2, 7, 8, 100, 200, 1000};
    static const uint64_t no[] = {0, 9, 50, 99, 101, 199};
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) {
        CHECK(rd_made_outside_due(yes[i]), "the %llu-th not due", (unsigned long long)yes[i]);
    }
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
        CHECK(!rd_made_outside_due(no[i]), "the %llu-th due", (unsigned long long)no[i]);
    }
    char line[384];
    /* 2. */
    (void)rd_made_outside_text(line, sizeof(line), 1, "rd wrap accumulate", 0, 1, "list 12", 1, 3,
                               2);
    printf("  2. %s\n", line);
    same(line, "made in play outside the key cache in replay 1 (list 12): pipeline \"rd wrap "
               "accumulate\" (its 1st; pipelines so far 3, of 2 names)");
    /* 3. */
    (void)rd_made_outside_text(line, sizeof(line), 2, "rd ring", 8u << 20, 146,
                               "no draw list: the present, a check or the game thread", 8, 9, 3);
    printf("  3. %s\n", line);
    same(line, "made in play outside the key cache in replay 146 (no draw list: the present, a "
               "check or the game thread): buffer \"rd ring\" of 8192 KB (its 8th; buffers so far "
               "9, of 3 names; this name's lines from here every 100th)");
    (void)rd_made_outside_text(line, sizeof(line), 2, "x", 1, 5, "list 0", 2, 2, 1);
    same(line, "made in play outside the key cache in replay 5 (list 0): buffer \"x\" of 1 KB (its "
               "2nd; buffers so far 2, of 1 names)");

    static const struct {
        uint64_t n;
        const char *word;
    } ord[] = {{3, "3rd"},   {11, "11th"},   {12, "12th"},  {13, "13th"},
               {21, "21st"}, {102, "102nd"}, {111, "111th"}};

    for (size_t i = 0; i < sizeof(ord) / sizeof(ord[0]); i++) {
        char want[64];
        (void)rd_made_outside_text(line, sizeof(line), 1, "p", 0, 1, "list 1", ord[i].n, 1, 1);
        snprintf(want, sizeof(want), "(its %s;", ord[i].word);
        CHECK(strstr(line, want) != NULL, "%s not in %s", want, line);
    }
    /* 4. */
    (void)rd_made_outside_text(line, sizeof(line), 2, NULL, 2048, 7, NULL, 1, 1, 1);
    same(line,
         "made in play outside the key cache in replay 7 (?): buffer \"unnamed\" of 2 KB (its "
         "1st; buffers so far 1, of 1 names)");
    char small[16];
    memset(small, 'z', sizeof(small));
    const size_t n = rd_made_outside_text(small, 12, 1, "p", 0, 1, "list 1", 1, 1, 1);
    CHECK(n == 11 && small[11] == '\0' && small[12] == 'z', "cut to %zu, the byte after %d", n,
          small[12]);
    if (failures) {
        printf("rd_made_outside_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_made_outside_test: ok\n");
    return 0;
}
