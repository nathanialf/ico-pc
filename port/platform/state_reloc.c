/*
 * port/platform/state_reloc.c
 *
 * Moving saved host addresses (state_reloc.h).  The word functions are
 * the whole file for now; the scan over a state's regions (A3) builds on
 * them.
 */
#include <stddef.h>

#include "state_reloc.h"

static int in_range(const IcoStateRange *g, uint64_t w)
{
    return g->lo < g->hi && w >= g->lo && w <= g->hi;
}

int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w)
{
    int i;
    for (i = 0; i < ICO_RELOC_RANGES; i++) {
        const IcoStateRange *g = &r->range[i];
        if (in_range(g, *w)) {
            if (g->delta == 0) {
                return 0;
            }
            *w += (uint64_t)g->delta;
            return 1;
        }
    }
    return 0;
}

int ico_state_reloc_typed(const IcoStateReloc *r, int which, uint64_t *w)
{
    const IcoStateRange *g;
    if (which < 0 || which >= ICO_RELOC_RANGES) {
        return -1;
    }
    g = &r->range[which];
    if (*w == 0) {
        return 0;
    }
    if (g->lo < g->hi && !in_range(g, *w)) {
        return -1;
    }
    *w += (uint64_t)g->delta;
    return 0;
}
