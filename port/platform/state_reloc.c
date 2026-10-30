/*
 * port/platform/state_reloc.c
 *
 * Moving saved host addresses (state_reloc.h): the word functions and the
 * conservative scan over a state's regions.  The image range and the
 * self-checks are state_check.c's (they name the scheduler and the kernel,
 * which this file, linked by every fiber user, must not pull in).
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

/* --- the scan -------------------------------------------------------------- */

static int log_on;

void ico_state_reloc_set_log(int on)
{
    log_on = on != 0;
}

size_t ico_state_reloc_scan(const IcoStateReloc *r, void *region, size_t n, uint64_t *rewritten)
{
    const uintptr_t base = (uintptr_t)region;
    uintptr_t a = (base + 7u) & ~(uintptr_t)7u;
    size_t moved = 0;

    if (region == NULL) {
        return 0;
    }
    for (; a >= base && a - base + 8u <= n; a += 8u) {
        uint64_t w;

        memcpy(&w, (const void *)a, sizeof w);
        {
            const uint64_t old = w;

            if (ico_state_reloc_word(r, &w)) {
                memcpy((void *)a, &w, sizeof w);
                moved++;
                if (log_on) {
                    fprintf(stderr, "state: moved the word at %p: %#llx -> %#llx\n", (void *)a,
                            (unsigned long long)old, (unsigned long long)w);
                }
            }
        }
    }
    if (rewritten != NULL) {
        *rewritten += moved;
    }
    return moved;
}
