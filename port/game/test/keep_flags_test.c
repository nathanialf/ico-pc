/*
 * port/game/test/keep_flags_test.c
 *
 * The twenty words a load's keep/restore walk reads on each disc
 * (ico2/common/src/layout_action.c gflagKeepState/gflagRestoreState through
 * la_host_keep_words, the file linked whole with keep_flags_stubs.c): the
 * disc's own five game-flag ids, then what follows them in that disc's EE
 * .data, as upstream's sources have it (ico2/common/src/layout_action.c's
 * VERSION_ blocks of keepFlagNo, then barLine on VERSION_JUN26, keyConfigCode
 * and keyConfigSlot).  Issue 160: one European list on every disc left the
 * North American disc's new-game flag 349 raised after Continue.
 *
 *   keep_flags_test [pal|jpn|kor|cn|ntsc|aug6|jun26]   (default pal)
 *
 * Checks, for the selected disc: the twenty words equal the expected ones
 * exactly; the fifth is the disc's new-game flag (region table
 * gflag_new_game); every word is below the disc's flag count (gflag_count),
 * since the walk passes each to gflagChk/gflagOn/gflagOff.
 */
#include <stdio.h>
#include <string.h>

#include "../../core/region.h"

void la_host_keep_words(int out[20]);

/* the European, Japanese, Korean and Asian discs: keepFlagNo, then
   keyConfigCode[8] and keyConfigSlot[0..6] */
static const int words_pal[20] = {388, 384, 383, 385, 382, 16, 128, 32, 64, 8,
                                  2,   1,   4,   1,   2,   3,  4,   5,  0,  0};
/* the North American disc */
static const int words_ntsc[20] = {355, 351, 350, 352, 349, 16, 128, 32, 64, 8,
                                   2,   1,   4,   1,   2,   3,  4,   5,  0,  0};
/* the August 2001 preview */
static const int words_aug6[20] = {354, 350, 349, 351, 348, 16, 128, 32, 64, 8,
                                   2,   1,   4,   1,   2,   3,  4,   5,  0,  0};
/* the June 2001 demo: keepFlagNo, then barLine's first fifteen words
   ({0,3,0,0},{7,3,0,0},{0,0,0,0},{7,7,0,...}) */
static const int words_jun26[20] = {339, 335, 334, 336, 333, 0, 3, 0, 0, 7,
                                    3,   0,   0,   0,   0,   0, 0, 7, 7, 0};

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    const char *slug = argc > 1 ? argv[1] : "pal";
    const IcoRegionInfo *r;
    const int *want;
    int got[20];
    int i;

    if (ico_region_select(slug) != 0) {
        printf("FAIL: no disc \"%s\"\n", slug);
        return 1;
    }
    r = ico_region_cur();
    want = ICO_JUN26 ? words_jun26 : ICO_AUG6 ? words_aug6 : ICO_NTSC ? words_ntsc : words_pal;
    memset(got, 0, sizeof got);
    la_host_keep_words(got);
    for (i = 0; i < 20; i++) {
        CHECK(got[i] == want[i], "%s: word %d is %d, the disc's .data has %d", slug, i, got[i],
              want[i]);
        CHECK(got[i] >= 0 && got[i] < r->gflag_count, "%s: word %d (%d) past the disc's %d flags",
              slug, i, got[i], r->gflag_count);
    }
    CHECK(got[4] == r->gflag_new_game, "%s: the fifth kept id %d is not the new-game flag %d", slug,
          got[4], r->gflag_new_game);
    if (failures == 0) {
        printf("keep_flags %s: ok\n", slug);
    }
    return failures != 0;
}
