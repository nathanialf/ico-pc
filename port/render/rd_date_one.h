/* rd_date_one.h: one DATE set-up for the shadow composites (rd_date_one.c;
 * docs/port/RENDER_POST.md "One DATE set-up for the shadow composites").
 *
 * Shadow.c composites its three blur levels into SCENE with three sprites
 * under TEST 0x3400D (DATE on, DATM 0, ATST GREATER 0, AFAIL KEEP), FBA 0.
 * Each one tests the destination alpha's top bit, so the replay takes a DATE
 * set-up for each (the R8 snapshot, rd_date_cut.c rd__date_snapshot, or the
 * stencil bit, rd_replay_tiler.c rd__replay_date_take), because the one
 * before wrote alpha.  A composite C is top-bit-preserving when it draws
 * only where the bit is 0 (DATM 0, AFAIL KEEP: a failing fragment writes
 * nothing) and writes an alpha under 0x80 (FBA 0: the stored alpha is As;
 * the alpha never blends, on every blend path), or writes no alpha at all.
 * Then the bit is the same at every texel after C as before it, and the
 * next composite's set-up would hold exactly the bits of C's: it may use
 * C's.  As is at most the level's alpha times the vertex alpha, >> 7
 * (MODULATE, TCC 1) or the level's alpha (DECAL); a level's alpha is at most
 * its source's times its own vertex alpha >> 7 (the filters are convex: no
 * filtered texel is above its taps); the count the chain starts from holds
 * 0x80 or 0 (rd_shadow_one.c's table, the seven draws' A pass; the box
 * reduce averages them).  Every retail disc's stage data (shadowDepth 45 to
 * 80, shadowBlend up to 128) keeps the largest written alpha at 56 (0x38);
 * the stage-setting debug menu can reach 128 x 128, which writes 0x80: the
 * bound is checked per picture from the chain's own vertex alphas, never
 * assumed.
 *
 * Pure parts (the CPU test's): the bound and the group rule. */
#ifndef PORT_RENDER_RD_DATE_ONE_H
#define PORT_RENDER_RD_DATE_ONE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Replay;
struct RdCmd;
struct RdStateBlock;
struct RdFrame;
struct RdPerfRecord;

/* the largest alpha a sprite writes from a source whose alpha is at most
 * level, at vertex alpha at most vertex: (level x vertex) >> 7 under
 * MODULATE (rounded up here, so a shader that rounds to nearest is inside
 * it too), level under DECAL; at most 255 */
static inline uint32_t rd_date_one_alpha(uint32_t level, uint32_t vertex, bool decal)
{
    const uint32_t a = decal ? level : (level * vertex + 127u) >> 7;
    return a > 255u ? 255u : a;
}

/* a composite keeps the destination alpha's top bit: in the box scan's
 * composite form (rd_shadow_box.c compositeForm: an alpha test that fails
 * on As 0, AFAIL KEEP, no Z write, TCC 1, MODULATE or DECAL, a sprite),
 * DATE DATM 0 (date 1: RD_DATE_DEST_ALPHA_0), FBA 0, not COLCLAMP 0's wrap
 * draw, and no alpha written (colour mask without A) or at most amax under
 * 0x80; force (ICO_RD_DATE_ONE=force, the sabotage test) skips the bound */
static inline bool rd_date_one_keeps(bool compositeForm, uint8_t date, uint8_t fba, bool wrap,
                                     uint8_t colorMask, uint32_t amax, bool force)
{
    return compositeForm && date == 1u && fba == 0u && !wrap &&
           ((colorMask & 8u) == 0u || amax < 0x80u || force);
}

/* a command as the group rule reads it */
enum {
    RD_D1_STATE,     /* a state command, a NOP, the overlay's text: nothing drawn */
    RD_D1_COMPOSITE, /* a shadow composite (the box scan's role) */
    RD_D1_OTHER      /* anything else: it ends the group */
};

typedef struct RdDateOneCmd {
    uint8_t kind;
    uint32_t target; /* the colour target in force */
    bool date;       /* a composite: DATE on */
    bool keeps;      /* a composite: rd_date_one_keeps */
} RdDateOneCmd;

typedef struct RdDateOneScan {
    bool adj;        /* the last command that was not state was a DATE composite */
    bool open;       /* ... that kept the top bit, as every one before it in the group */
    uint32_t target; /* its target */
    uint32_t prev;   /* its ordinal */
} RdDateOneScan;

/* what a step found */
enum {
    RD_D1_NONE = 0,
    RD_D1_SHARED = 1,  /* the composite uses the set-up of the one at *prev */
    RD_D1_REFUSED = 2, /* it follows one on its target that could set the bit: its own */
};

/* command c (ordinal ord): a DATE composite right after another on the
 * same target (only state between) shares that one's set-up when every
 * composite of the group so far keeps the top bit; any other command that
 * draws ends the group */
static inline int rd_date_one_step(RdDateOneScan *s, const RdDateOneCmd *c, uint32_t ord,
                                   uint32_t *prev)
{
    if (c->kind == RD_D1_STATE) {
        return RD_D1_NONE;
    }
    if (c->kind != RD_D1_COMPOSITE || !c->date) {
        s->adj = s->open = false;
        return RD_D1_NONE;
    }
    int did = RD_D1_NONE;
    if (s->adj && s->target == c->target) {
        did = s->open ? RD_D1_SHARED : RD_D1_REFUSED;
        *prev = s->prev;
    }
    /* the set-up this one holds (its own or the group's) stays right after
     * it only when it keeps the bit */
    s->adj = true;
    s->open = c->keeps;
    s->target = c->target;
    s->prev = ord;
    return did;
}

/* ------------------------------------- the switch (ICO_RD_DATE_ONE, tests) */
/* 0 off, 1 on, 2 on ignoring the bound (force); the tests' override first
 * (rd__date_one_force: -1 the switch's own) */
int rd__date_one_mode(void);
void rd__date_one_force(int mode);

/* ------------------------------ the scan (rd_shadow_box.c's, per command) */
void rd__date_one_scan_begin(const struct RdFrame *f);
/* composite: the box scan named command ord a composite reading a source
 * whose alpha is at most level, its vertices' largest alpha vertex */
void rd__date_one_scan_cmd(const struct RdCmd *c, const struct RdStateBlock *st, bool state,
                           uint32_t ord, bool composite, uint32_t level, uint32_t vertex);
/* the DATE cut's scan (rd_date_cut.c): command ord shares the set-up before
 * it (no pass ends there) */
bool rd__date_one_shared_at(uint32_t ord);

/* ------------------------------------------------------------ the walk */
/* rd__date_snapshot / rd__replay_date_take, before rd__date_retake: the
 * write serial the held set-up (for heldFor at heldSerial) stands for; the
 * current serial when the draw at g_rdStoreOrd shares it (rd__date_retake
 * then reuses it if its area holds the draw's, as for any reuse) */
uint32_t rd__date_one_held(uint32_t heldFor, uint32_t heldSerial, uint32_t tcId, uint32_t serial,
                           bool stencil);
/* after the set-up of the DATE draw at g_rdStoreOrd: taken again or reused
 * (texels: the target's, for the counters) */
void rd__date_one_took(uint32_t tcId, uint32_t serial, bool stencil, bool reused, uint64_t texels);
/* rd_no_volume.c rd__no_volume_skip: the composite at g_rdStoreOrd is
 * skipped (its level is empty: it draws nothing), the write serial now
 * serial; the set-up the group holds still holds */
void rd__date_one_passed(uint32_t serial);
/* rd_shadow_box.c: a level or composite replayed with a vertex alpha above
 * the one the scan read: nothing is shared for the rest of the replay */
void rd__date_one_replay_bad(void);

/* ------------------------------------------------------------ the lines */
void rd__date_one_pop(const struct RdPerfRecord *rec);
/* "; S shared one DATE set-up, K kept their own" into the DATE clause */
void rd__date_one_text(char *out, size_t size, size_t *at);
/* the --stats clause */
void rd__date_one_stats(const struct RdPerfRecord *rec, char *out, size_t size);
/* the 10 s sums have anything to say (rd_date_cut.c prints its clause) */
bool rd__date_one_any(void);

#endif /* PORT_RENDER_RD_DATE_ONE_H */
