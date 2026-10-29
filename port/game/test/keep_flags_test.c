/*
 * port/game/test/keep_flags_test.c
 *
 * The twenty words a load's keep/restore walk reads on each disc
 * (ico2/common/src/layout_action.c gflagKeepState/gflagRestoreState through
 * la_host_keep_words, the file linked whole with keep_flags_stubs.c): the
 * disc's own five game-flag ids, then what follows them in that disc's EE
 * .data, the alignment's zero padding included, as the decomp's matching
 * objects have it (readelf -sW and -x .data on
 * build/<disc>/ico2/common/src/layout_action.o: keepFlagNo at .data+0,
 * keyConfigCode at +0x18 and keyConfigSlot at +0x38, barLine at +0x20 on
 * the June 2001 demo; the gaps are zero).  Issue 160: one European list on
 * every disc left the North American disc's new-game flag 349 raised after
 * Continue.
 *
 *   keep_flags_test [pal|jpn|kor|cn|ntsc|aug6|jun26]   (default pal)
 *
 * Checks, for the selected disc: the twenty words equal the expected ones
 * exactly, with the key-config slots as at start and again as a Button
 * configuration visit leaves them (keyConfigSlot[k] = k: word 19 is 5 and
 * no word is 6, since slot 6 is past the walk); the fifth is the disc's new-game flag (region table
 * gflag_new_game); every word is below the disc's flag count (gflag_count),
 * since the walk passes each to gflagChk/gflagOn/gflagOff.
 */
#include <stdio.h>
#include <string.h>

#include "../../core/region.h"

void la_host_keep_words(int out[20]);

/* the European, Japanese, Korean and Asian discs: keepFlagNo, one zero pad
   word, keyConfigCode[8] and keyConfigSlot[0..5] */
static const int words_pal[20] = {388, 384, 383, 385, 382, 0, 16, 128, 32, 64,
                                  8,   2,   1,   4,   1,   2, 3,  4,   5,  0};
/* the North American disc */
static const int words_ntsc[20] = {355, 351, 350, 352, 349, 0, 16, 128, 32, 64,
                                   8,   2,   1,   4,   1,   2, 3,  4,   5,  0};
/* the August 2001 preview */
static const int words_aug6[20] = {354, 350, 349, 351, 348, 0, 16, 128, 32, 64,
                                   8,   2,   1,   4,   1,   2, 3,  4,   5,  0};
/* the June 2001 demo: keepFlagNo, three zero pad words, then barLine's
   first twelve words ({0,3,0,0},{7,3,0,0},{0,0,0,0}) */
static const int words_jun26[20] = {339, 335, 334, 336, 333, 0, 0, 0, 0, 3,
                                    0,   0,   7,   3,   0,   0, 0, 0, 0, 0};

/* the key-config slots after a Button configuration visit */
static const int slots_identity[8] = {0, 1, 2, 3, 4, 5, 6, 7};

void la_host_set_key_config_slots(const int slots[8]);

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
    /* a Button configuration visit: keyConfigSlot[k] = k; the walk ends at
       slot 5 (the June 2001 demo's walk reads no slot) */
    la_host_set_key_config_slots(slots_identity);
    memset(got, 0, sizeof got);
    la_host_keep_words(got);
    for (i = 0; i < 20; i++) {
        const int w = ICO_JUN26 || i < 14 ? want[i] : i - 14;

        CHECK(got[i] == w, "%s, slots k = k: word %d is %d, the disc's .data has %d", slug, i,
              got[i], w);
        CHECK(ICO_JUN26 || i < 5 || got[i] != 6, "%s, slots k = k: word %d keeps flag 6", slug, i);
    }
    CHECK(ICO_JUN26 || got[19] == 5, "%s, slots k = k: word 19 is %d, not slot 5's 5", slug,
          got[19]);
    if (failures == 0) {
        printf("keep_flags %s: ok\n", slug);
    }
    return failures != 0;
}
