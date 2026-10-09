/*
 * port/fmv/field_match.c
 *
 * field_match.h.
 */
#include "field_match.h"
#include <stddef.h>

#define COMB_STEP 10

uint32_t ico_field_comb(const uint8_t *keep, const uint8_t *other, uint32_t pitch, uint32_t w,
                        uint32_t h, int keep_odd)
{
    uint32_t n = 0, y, x;

    for (y = 1; y + 1 < h; y++) {
        const int mine = (int)(y & 1) == (keep_odd != 0);
        /* row y and its neighbours (the other parity) */
        const uint8_t *mid = (mine ? keep : other) + (size_t)y * pitch;
        const uint8_t *up = (mine ? other : keep) + (size_t)(y - 1) * pitch;
        const uint8_t *dn = (mine ? other : keep) + (size_t)(y + 1) * pitch;

        for (x = 0; x < w; x += 2) {
            const int e1 = (int)mid[x] - (int)up[x], e2 = (int)mid[x] - (int)dn[x];
            if ((e1 > COMB_STEP && e2 > COMB_STEP) || (e1 < -COMB_STEP && e2 < -COMB_STEP)) {
                n++;
            }
        }
    }
    return n;
}

int ico_field_match(const uint8_t *prev, const uint8_t *cur, const uint8_t *next, uint32_t pitch,
                    uint32_t w, uint32_t h, int keep_odd)
{
    const uint32_t samples = ((w + 1) / 2) * (h > 2 ? h - 2 : 0);
    const uint32_t margin = samples / 1000;
    uint32_t best = ico_field_comb(cur, cur, pitch, w, h, keep_odd);
    int pick = ICO_FIELD_MATCH_CUR;

    if (prev != NULL) {
        const uint32_t c = ico_field_comb(cur, prev, pitch, w, h, keep_odd);
        if (c * 2 < best && c + margin < best) {
            best = c;
            pick = ICO_FIELD_MATCH_PREV;
        }
    }
    if (next != NULL) {
        const uint32_t c = ico_field_comb(cur, next, pitch, w, h, keep_odd);
        if (c * 2 < best && c + margin < best) {
            pick = ICO_FIELD_MATCH_NEXT;
        }
    }
    return pick;
}
