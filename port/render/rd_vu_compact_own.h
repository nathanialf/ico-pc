/* rd_vu_compact_own.h: between rd_vu_compact.c and the Mali-G52's form's
 * own start-up test (rd_vu_compact_selftest.c; rd_vu_compact.h says what
 * and why).  Renderer-internal: include after rd_internal.h. */
#ifndef PORT_RENDER_RD_VU_COMPACT_OWN_H
#define PORT_RENDER_RD_VU_COMPACT_OWN_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_vu_compact.h"

/* the draws a replay of the test plans (its four mesh commands) */
#define RD_VU_CMP_OWN_DRAWS 4u
/* the test's replays with the compaction (and as many without) */
#define RD_VU_CMP_OWN_REPLAYS 6

/* one replay's copy: the lists from value from (a whole texel) and the
   counts after them, read back after the replays; the plan's calls and
   their sources as planned (the sources the meshes' own, alive while the
   test runs) */
typedef struct RdVuCmpOwnCopy {
    RhiBuffer rb;
    uint64_t bytes;
    uint32_t from;   /* the first value copied, from the fourth plane's start */
    uint32_t counts; /* the counts' first texel, from the fourth plane's start */
    uint32_t n;      /* the calls planned (above RD_VU_CMP_OWN_DRAWS: more than the test's) */
    RdVuCmpCall calls[RD_VU_CMP_OWN_DRAWS];
    const uint32_t *src[RD_VU_CMP_OWN_DRAWS];
    bool copied; /* pass K ran and every planned command was dispatched */
} RdVuCmpOwnCopy;

/* the test's replays: begin (the record's test is not this one), the
   compaction on or held off in them, the lists' first value moved by shift
   values past the plane's middle */
void rd__vu_cmp_own_begin(void);
void rd__vu_cmp_own_end(void);
void rd__vu_cmp_own_hold(bool on);
void rd__vu_cmp_own_shift(uint32_t shift);
/* the next replay's lists copied as replay k's (k < RD_VU_CMP_OWN_REPLAYS;
   -1: none) */
void rd__vu_cmp_own_arm(int k);
const RdVuCmpOwnCopy *rd__vu_cmp_own_copy(int k);
/* the copies' buffers destroyed (after the read) */
void rd__vu_cmp_own_free(void);
/* the tests' ICO_RD_VU_COMPACT_SABOTAGE=readback: one filler read back as
   its corner 0 (a filler write missing), so the check must refuse */
bool rd__vu_cmp_own_sabotage_readback(void);

/* the mark (port/platform/compact_mark.h): "trying" (synced) before the
   test's first replay with it; false when it could not be written (the
   test then does not run) */
bool rd__vu_cmp_own_trying(void);
/* the verdict: 1 passed (the mark "counting"), 0 refused (the mark
   "failed"), 2 not checked (the mark removed: tried again at the next
   start); why its words; the row and the line follow at the record's
   rd__vu_compact_selftest_done */
void rd__vu_cmp_own_verdict(int result, const char *why);
/* the class has the form, the compaction is to be tried and not kept off */
bool rd__vu_cmp_own_wanted(void);

#endif /* PORT_RENDER_RD_VU_COMPACT_OWN_H */
