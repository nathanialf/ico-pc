/*
 * port/render/test/rd_list_draws_test.c
 *
 * A list's draws by fragment entry and DATE mode (rd_list_draws.h, issue
 * 179's list 3 in the deja scene): the stencil modes' DATE words, the
 * table (out of range counted apart), the list picked (the most expensive,
 * not list 0, none above 0), the line word for word with its top kinds,
 * the rest summed and a short buffer cut (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "rd.h"
#include "rd_list_draws.h"

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

static const char *names(uint8_t fs)
{
    static const char *const k[4] = {"sprite_ps", "blit_ps", "vu_ps", "?"};
    return fs < 4u ? k[fs] : NULL;
}

int main(void)
{
    /* the DATE words of the stencil modes */
    CHECK(rd_list_draws_date_of(RD_STENCIL_OFF) == RD_LIST_DRAWS_DATE_NONE);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST0) == RD_LIST_DRAWS_DATE_0);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST0_SET) == RD_LIST_DRAWS_DATE_0);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST0_T) == RD_LIST_DRAWS_DATE_0);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST1) == RD_LIST_DRAWS_DATE_1);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST1_CLR) == RD_LIST_DRAWS_DATE_1);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_TEST1_T) == RD_LIST_DRAWS_DATE_1);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_SET) == RD_LIST_DRAWS_DATE_SETUP);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_CLEAR) == RD_LIST_DRAWS_DATE_SETUP);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_CLEAR2) == RD_LIST_DRAWS_DATE_SETUP);
    CHECK(rd_list_draws_date_of(RD_STENCIL_DATE_SET_T) == RD_LIST_DRAWS_DATE_SETUP);
    CHECK(rd_list_draws_date_of(RD_STENCIL_TILE_MARK) == RD_LIST_DRAWS_DATE_NONE);

    /* the pick: the largest list, -1 for list 0 or none */
    {
        double ms[RD_LIST_COUNT];
        memset(ms, 0, sizeof ms);
        CHECK(rd_list_draws_pick(ms, RD_LIST_COUNT) == -1);
        ms[0] = 20.7;
        ms[3] = 23.1;
        ms[8] = 5.3;
        CHECK(rd_list_draws_pick(ms, RD_LIST_COUNT) == 3);
        ms[0] = 23.2;
        CHECK(rd_list_draws_pick(ms, RD_LIST_COUNT) == -1);
        ms[3] = 23.2; /* equal: the first */
        CHECK(rd_list_draws_pick(ms, RD_LIST_COUNT) == -1);
    }

    /* list 3 over 4 replays: vu_ps 248 without DATE in the key, sprite_ps 84
       DATE 0 by the stencil, 12 set-ups, blit_ps 4, entry 9 (no name) 2,
       entry 3 ("?") 1; list 0's and out of range apart */
    static RdListDraws d;
    rd_list_draws_reset(&d);
    d.replays = 4;
    for (int i = 0; i < 248; i++) {
        rd_list_draws_add(&d, 3, 2, RD_LIST_DRAWS_DATE_NONE);
    }
    for (int i = 0; i < 84; i++) {
        rd_list_draws_add(&d, 3, 0, RD_LIST_DRAWS_DATE_0);
    }
    for (int i = 0; i < 12; i++) {
        rd_list_draws_add(&d, 3, 0, RD_LIST_DRAWS_DATE_SETUP);
    }
    for (int i = 0; i < 4; i++) {
        rd_list_draws_add(&d, 3, 1, RD_LIST_DRAWS_DATE_NONE);
    }
    rd_list_draws_add(&d, 3, 9, RD_LIST_DRAWS_DATE_1);
    rd_list_draws_add(&d, 3, 9, RD_LIST_DRAWS_DATE_1);
    rd_list_draws_add(&d, 3, 3, RD_LIST_DRAWS_DATE_NONE);
    rd_list_draws_add(&d, 0, 2, RD_LIST_DRAWS_DATE_NONE);
    rd_list_draws_add(&d, -1, 2, RD_LIST_DRAWS_DATE_NONE);
    rd_list_draws_add(&d, RD_LIST_COUNT, 2, RD_LIST_DRAWS_DATE_NONE);
    rd_list_draws_add(&d, 3, RD_LIST_DRAWS_FS, RD_LIST_DRAWS_DATE_NONE);
    rd_list_draws_add(&d, 3, 2, RD_LIST_DRAWS_DATES);
    CHECK(d.other == 4u && d.n[3][2][0] == 248u && d.n[0][2][0] == 1u);

    char line[1024];
    (void)rd_list_draws_text(&d, 3, 23.1, names, line, sizeof line);
    printf("%s\n", line);
    CHECK_STR(line, "list 3's draws by fragment entry and DATE mode (the block's most expensive "
                    "list, 23.1 ms a picture; pipeline look-ups a picture over 4 replays, a "
                    "look-up a draw or a merged run of draws): vu_ps no DATE in the key 62.0, "
                    "sprite_ps DATE 0 by the stencil 21.0, sprite_ps a DATE set-up 3.0, blit_ps "
                    "no DATE in the key 1.0, entry 9 DATE 1 by the stencil 0.5, entry 3 no DATE "
                    "in the key 0.2");
    /* more kinds than the line names: the rest summed */
    for (unsigned f = 10; f < 14; f++) {
        rd_list_draws_add(&d, 3, f, RD_LIST_DRAWS_DATE_NONE);
    }
    (void)rd_list_draws_text(&d, 3, 23.1, names, line, sizeof line);
    CHECK(strstr(line, "; 2 more kinds 0.5") != NULL);
    /* a list with nothing counted; a list out of range: nothing */
    (void)rd_list_draws_text(&d, 5, 1.0, names, line, sizeof line);
    CHECK(strstr(line, "): none counted") != NULL);
    CHECK(rd_list_draws_text(&d, -1, 1.0, names, line, sizeof line) == 0 && line[0] == '\0');
    /* a short buffer: cut and ended, the full length returned */
    {
        char s[24];
        const int n = rd_list_draws_text(&d, 3, 23.1, NULL, s, sizeof s);
        CHECK(n > (int)sizeof s && strlen(s) == sizeof s - 1u);
    }
    if (failures) {
        printf("rd_list_draws_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rd_list_draws_test: ok\n");
    return 0;
}
