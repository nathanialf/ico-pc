/* tools/region_blocks.py --selftest input for `rename`, with the table's
 * first three rows (pal, jpn, kor): the two-row spelling becomes the
 * spelling of the rows sharing the Japanese arm, kor on jpn (`--base
 * kor=jpn`; rename_expected.c).
 * With the six rows and the three later ones on PAL (the default --base),
 * rename_expected.c becomes rename_pal_expected.c: LN gains PAL's value per
 * new row, no selector moves. Comments and strings keep PAL_JPN(a, b),
 * ICO_JPN and LN(a, b). */
#include "region.h"

static int items_pal[2], items_jpn[2];
#define items PAL_JPNKOR(items_pal, items_jpn)
#define ICO_IS_JPN() (ICO_JPNKOR)

extern void *alloc(int size, int line);
extern void draw(int x, int y);

int fx_rename(int w)
{
    const char *s = "PAL_JPN(1, 2) ICO_JPN LN(3, 4)";
    int n = PAL_JPNKOR(1, 2) + PAL_JPNKOR (3, PAL_JPNKOR(4, 5));

    if (ICO_JPNKOR && !ICO_JPNKOR) {
        n++;
    }
    alloc(64, LN(560, 557, 557, 560, 560, 560));
    alloc(64, LN(f(1, 2), PAL_JPNKOR(3, 4), PAL_JPNKOR(3, 4), f(1, 2), f(1, 2), f(1, 2)));
    alloc(LN(1, 2, 2, 1, 1, 1), LN(10,
                          11, 11, 10, 10, 10));
    draw(ICO_REGION_JPN, ICO_JPNKOR + PAL_JPNKOR(0, 1) + items[0] + (s != 0));
    return n + ICO_JPN_EXTRA; /* ICO_JPN_EXTRA is another name */
}
