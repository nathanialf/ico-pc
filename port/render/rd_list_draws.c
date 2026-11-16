/*
 * port/render/rd_list_draws.c
 *
 * A list's draws by fragment entry and DATE mode: the table and its line
 * (rd_list_draws.h).  Pure.
 */
#include "rd_list_draws.h"

#include <stdio.h>
#include <string.h>

#include "rd.h" /* RD_LIST_COUNT, RD_STENCIL_DATE_* */

_Static_assert(RD_LIST_DRAWS_LISTS == RD_LIST_COUNT, "rd_list_draws: one row a game list");

unsigned rd_list_draws_date_of(uint8_t stencil)
{
    switch (stencil) {
    case RD_STENCIL_DATE_TEST0:
    case RD_STENCIL_DATE_TEST0_SET:
    case RD_STENCIL_DATE_TEST0_T:
        return RD_LIST_DRAWS_DATE_0;
    case RD_STENCIL_DATE_TEST1:
    case RD_STENCIL_DATE_TEST1_CLR:
    case RD_STENCIL_DATE_TEST1_T:
        return RD_LIST_DRAWS_DATE_1;
    case RD_STENCIL_DATE_CLEAR:
    case RD_STENCIL_DATE_SET:
    case RD_STENCIL_DATE_CLEAR2:
    case RD_STENCIL_DATE_SET_T:
        return RD_LIST_DRAWS_DATE_SETUP;
    default:
        return RD_LIST_DRAWS_DATE_NONE;
    }
}

void rd_list_draws_reset(RdListDraws *d)
{
    memset(d, 0, sizeof(*d));
}

void rd_list_draws_add(RdListDraws *d, int list, unsigned fs, unsigned date)
{
    if (list < 0 || (unsigned)list >= RD_LIST_DRAWS_LISTS || fs >= RD_LIST_DRAWS_FS ||
        date >= RD_LIST_DRAWS_DATES) {
        d->other++;
        return;
    }
    d->n[list][fs][date]++;
}

int rd_list_draws_pick(const double *listMs, unsigned count)
{
    int best = -1;

    for (unsigned l = 0; l < count; l++) {
        if (listMs[l] > 0.0 && (best < 0 || listMs[l] > listMs[best])) {
            best = (int)l;
        }
    }
    return best > 0 ? best : -1;
}

static const char *const k_date[RD_LIST_DRAWS_DATES] = {
    "no DATE in the key", "DATE 0 by the stencil", "DATE 1 by the stencil", "a DATE set-up"};

int rd_list_draws_text(const RdListDraws *d, int list, double listMs, RdListDrawsName name,
                       char *buf, size_t n)
{
    size_t at = 0;
    int total = 0;
#define PUT(...)                                                                                   \
    do {                                                                                           \
        const int w_ = snprintf(at < n ? buf + at : NULL, at < n ? n - at : 0, __VA_ARGS__);       \
        if (w_ > 0) {                                                                              \
            total += w_;                                                                           \
            at += (size_t)w_;                                                                      \
        }                                                                                          \
    } while (0)

    if (n > 0) {
        buf[0] = '\0';
    }
    if (list < 0 || (unsigned)list >= RD_LIST_DRAWS_LISTS) {
        return 0;
    }
    const double r = d->replays ? (double)d->replays : 1.0;
    PUT("list %d's draws by fragment entry and DATE mode (the block's most expensive list, %.1f "
        "ms a picture; pipeline look-ups a picture over %u replays, a look-up a draw or a merged "
        "run of draws):",
        list, listMs, d->replays);
    /* the top RD_LIST_DRAWS_SHOW by count (a selection: the table is small) */
    uint8_t shown[RD_LIST_DRAWS_FS][RD_LIST_DRAWS_DATES];
    memset(shown, 0, sizeof(shown));
    unsigned kinds = 0;
    uint64_t all = 0;
    for (unsigned f = 0; f < RD_LIST_DRAWS_FS; f++) {
        for (unsigned m = 0; m < RD_LIST_DRAWS_DATES; m++) {
            kinds += d->n[list][f][m] ? 1u : 0u;
            all += d->n[list][f][m];
        }
    }
    if (kinds == 0) {
        PUT(" none counted");
        return total;
    }
    uint64_t said = 0;
    unsigned saidKinds = 0;
    for (unsigned k = 0; k < RD_LIST_DRAWS_SHOW && k < kinds; k++) {
        unsigned bf = 0, bm = 0;
        uint32_t best = 0;
        for (unsigned f = 0; f < RD_LIST_DRAWS_FS; f++) {
            for (unsigned m = 0; m < RD_LIST_DRAWS_DATES; m++) {
                if (!shown[f][m] && d->n[list][f][m] > best) {
                    best = d->n[list][f][m];
                    bf = f;
                    bm = m;
                }
            }
        }
        shown[bf][bm] = 1;
        said += best;
        saidKinds++;
        const char *nm = name != NULL ? name((uint8_t)bf) : NULL;
        if (nm != NULL && nm[0] != '\0' && strcmp(nm, "?") != 0) {
            PUT("%s %s %s %.1f", k ? "," : "", nm, k_date[bm], (double)best / r);
        } else {
            PUT("%s entry %u %s %.1f", k ? "," : "", bf, k_date[bm], (double)best / r);
        }
    }
    if (saidKinds < kinds) {
        PUT("; %u more kinds %.1f", kinds - saidKinds, (double)(all - said) / r);
    }
#undef PUT
    return total;
}
