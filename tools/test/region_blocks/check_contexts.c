/* tools/region_blocks.py --selftest input for `check`'s constant-context
 * rules: copied beside the converted fixture.c, then `check` must name
 * exactly the lines that end in a "refused" comment (and no other line of
 * this file). Data twins are selectors by another name; the twins table's
 * functions (fx_twin, fx_twin_jpn) have no size and no place in a shift. */
#include "region.h"

typedef struct {
    int id;
} Item;

static Item items_pal[5] = {{1}, {2}, {3}, {4}, {5}};
static Item items_jpn[3] = {{1}, {2}, {3}};
#define items PAL_JPN(items_pal, items_jpn)

static int state_pal, state_jpn;
#define state (*PAL_JPN(&state_pal, &state_jpn))

int fx_twin(int x);
int fx_twin_jpn(int x);
int shift_word(int w);

int shift_word(int w)
{
    int i, n = 0;
    Item *p = &items[1];           /* an element's address */
    int *q = &state;               /* the deref form is an lvalue */
    int (*f)(int) = fx_twin;       /* the PAL body forwards */

    n += w >> PAL_JPN(16, 19);     /* refused */
    n += w >> (PAL_JPN(16, 19));   /* refused */
    n += w << (8 + PAL_JPN(8, 11)); /* refused */
    n += w >> shift_word(PAL_JPN(1, 2));
    n += PAL_JPN(1, 2) << 3;
    n += (int)sizeof(PAL_JPN(1, 2)); /* refused */
    for (i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++) { /* refused */
        n += items[i].id;
    }
    n += (int)sizeof items;        /* refused */
    n += w >> (items[0].id);       /* refused */
    p = (Item *)&items;            /* refused */
    n += (int)sizeof(fx_twin);     /* refused */
    f = &fx_twin_jpn;              /* refused */
    n += w >> fx_twin(1);          /* refused */
    n += w >> (state);             /* refused */
    /* the shared-arm selectors and predicates follow the same rules */
    n += w >> PAL_JPNKOR(16, 19);  /* refused */
    n += w << (2 + PAL_JPN_KOR(8, 11, 12)); /* refused */
    n += (int)sizeof(PAL_JPNKOR_US(1, 2, 3)); /* refused */
    n += w >> shift_word(PAL_JPNKOR(1, 2));
    n += PAL_KOR(1, 2) << 3;
    n += LN(10, 11, 11, 10);
    if (ICO_JPNKOR || ICO_KOR) {
        n++;
    }
#if ICO_JPNKOR /* refused */
    n++;
#endif
    n += fx_twin_jpn(w) + state + p->id + *q + f(1);
    return n;
}
