/*
 * port/platform/test/last_run_skin_test.c
 *
 * The skin check's counts in words (last_run_skin.c, issue 217): the
 * pipelines slot's words and the summary's short form word for word, on a
 * phone and elsewhere; nothing with the check off or no counts; the largest
 * counts within LAST_RUN_SKIN_SHORT_MAX; a short buffer cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "last_run_skin.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
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

int main(void)
{
    char w[256], s[LAST_RUN_SKIN_SHORT_MAX];
    LastRunSkin k;

    /* issue 217's reporter on v0.11.10: a phone, 35 checks, nothing of its
       own drawn or made */
    memset(&k, 0, sizeof k);
    k.on = 1;
    k.phone = 1;
    k.checks = 35;
    last_run_skin_words(&k, w, sizeof w);
    CHECK(text_is(w, "the skin check: 35 checks, 0 box checks drawn, 0 twins drawn, 0 of its own "
                     "pipelines begun (a tile-based phone GPU: none compiled)"));
    last_run_skin_short(&k, s, sizeof s);
    CHECK(text_is(s, "skin box 0 twin 0 made 0"));

    /* a desktop Adreno: compiled under the memory rule, drawn */
    k.phone = 0;
    k.checks = 120;
    k.box = 118;
    k.twins = 2400;
    k.begun = 9;
    last_run_skin_words(&k, w, sizeof w);
    CHECK(text_is(w, "the skin check: 120 checks, 118 box checks drawn, 2400 twins drawn, 9 of its "
                     "own pipelines begun"));
    last_run_skin_short(&k, s, sizeof s);
    CHECK(text_is(s, "skin box 118 twin 2400 made 9"));

    /* the check off, or no counts: nothing */
    k.on = 0;
    memset(w, 'x', sizeof w);
    memset(s, 'x', sizeof s);
    last_run_skin_words(&k, w, sizeof w);
    last_run_skin_short(&k, s, sizeof s);
    CHECK(w[0] == '\0' && s[0] == '\0');
    last_run_skin_words(NULL, w, sizeof w);
    last_run_skin_short(NULL, s, sizeof s);
    CHECK(w[0] == '\0' && s[0] == '\0');

    /* the largest counts: the short form fits its bound */
    k.on = 1;
    k.box = k.twins = k.begun = ~0ull;
    last_run_skin_short(&k, s, sizeof s);
    CHECK(strlen(s) + 1u <= LAST_RUN_SKIN_SHORT_MAX);
    CHECK(text_is(s, "skin box 18446744073709551615 twin 18446744073709551615 made "
                     "18446744073709551615"));

    /* a short buffer: cut and terminated; none: untouched */
    char c[10];
    last_run_skin_short(&k, c, sizeof c);
    CHECK(text_is(c, "skin box "));
    c[0] = 'q';
    last_run_skin_words(&k, c, 0);
    last_run_skin_short(&k, c, 0);
    CHECK(c[0] == 'q');

    if (failures) {
        printf("last_run_skin_test: %d failures\n", failures);
        return 1;
    }
    printf("last_run_skin_test: ok\n");
    return 0;
}
