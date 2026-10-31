/*
 * port/platform/state_hash.c
 *
 * The masked hash of the game's state (state_hash.h).
 */
#include <stdlib.h>
#include <string.h>

#include "state_hash.h"
#include "xxh3.h"

void ico_state_hash_note_block(IcoStateHashBlocks *b, uint64_t p, uint64_t bytes)
{
    const uint64_t lo = p;
    const uint64_t hi = p + bytes;
    uint64_t best_gap = UINT64_MAX;
    int best = 0;
    int i;

    if (b == NULL || p == 0) {
        return;
    }
    for (i = 0; i < b->n; i++) {
        if (lo <= b->hi[i] && hi >= b->lo[i]) {
            /* overlapping or touching: widened */
            b->lo[i] = lo < b->lo[i] ? lo : b->lo[i];
            b->hi[i] = hi > b->hi[i] ? hi : b->hi[i];
            return;
        }
    }
    if (b->n < ICO_STATE_HASH_MAX_BLOCKS) {
        b->lo[b->n] = lo;
        b->hi[b->n] = hi;
        b->n++;
        return;
    }
    for (i = 0; i < b->n; i++) {
        const uint64_t gap = hi < b->lo[i] ? b->lo[i] - hi : lo - b->hi[i];

        if (gap < best_gap) {
            best_gap = gap;
            best = i;
        }
    }
    b->lo[best] = lo < b->lo[best] ? lo : b->lo[best];
    b->hi[best] = hi > b->hi[best] ? hi : b->hi[best];
}

static int in_block(const IcoStateHashBlocks *b, uint64_t w)
{
    int i;

    for (i = 0; i < b->n; i++) {
        if (w >= b->lo[i] && w <= b->hi[i]) {
            return 1;
        }
    }
    return 0;
}

int ico_state_hash_regions(const IcoStateHashRegion *rg, int nrg, const IcoStateHashField *f,
                           int nf, const IcoStateHashBlocks *b, IcoStateMaskFn fn, void *user,
                           IcoStateHashResult *out)
{
    int r;
    int i;

    memset(out, 0, sizeof *out);
    if (nrg < 0 || nrg > ICO_STATE_HASH_MAX_REGIONS) {
        return -1;
    }
    for (r = 0; r < nrg; r++) {
        const uintptr_t base = (uintptr_t)rg[r].p;
        const size_t n = rg[r].n;
        unsigned char *c = (unsigned char *)malloc(n ? n : 1);
        uintptr_t a;

        if (c == NULL) {
            memset(out, 0, sizeof *out);
            return -1;
        }
        if (n != 0) {
            memcpy(c, rg[r].p, n);
        }
        /* the typed fields, clipped to the region */
        for (i = 0; i < nf; i++) {
            const uintptr_t lo = (uintptr_t)f[i].p;
            const uintptr_t hi = lo + f[i].n;
            uintptr_t from, to;

            if (f[i].p == NULL || f[i].n == 0 || hi <= base || lo >= base + n) {
                continue;
            }
            from = lo > base ? lo : base;
            to = hi < base + n ? hi : base + n;
            memset(c + (from - base), 0, to - from);
            if (f[i].kind == ICO_STATE_MASK_ID) {
                out->host_ids++;
            } else {
                out->ptr_words++;
            }
            if (fn != NULL) {
                fn(r, from - base, to - from, f[i].kind, user);
            }
        }
        /* the catch-all: whole aligned words (by their address) inside the
           region, read from the copy so a typed field's zeros stay zero */
        if (b != NULL && b->n > 0) {
            for (a = (base + 7u) & ~(uintptr_t)7u; a >= base && a - base + 8u <= n; a += 8u) {
                uint64_t w;

                memcpy(&w, c + (a - base), sizeof w);
                if (w != 0 && in_block(b, w)) {
                    memset(c + (a - base), 0, sizeof w);
                    out->ptr_words++;
                    if (fn != NULL) {
                        fn(r, a - base, 8, ICO_STATE_MASK_BLOCK, user);
                    }
                }
            }
        }
        out->region_hash[r] = xxh3_64(c, n);
        free(c);
    }
    out->hash = xxh3_64(out->region_hash, (size_t)nrg * sizeof out->region_hash[0]);
    return 0;
}
