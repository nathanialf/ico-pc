/* rd_inames.h: the draws left at their tick's position, by name (the
 * owner's "slow down on movement of objects like the cage", v0.11.6-rc13 on
 * his Pixel; docs/port/RENDER.md's interpolation diagnostics).
 *
 * The interpolation's 250-frame lines (rd_interp.c presentLog) count the
 * keyed draws it could not blend (unmatched, mismatched, jumped, a turn
 * snapped) and the draws it never tries (unkeyed), but not which objects
 * they were: the owner's log could not say whether the cage was among them
 * (0.1-0.5 % of keyed draws, the same as stage 3).  This names them, every
 * 250 frames, the top RD_INAMES_TOP names of each reason with their
 * counts, by the mesh's name (the model's name the game gave the packet:
 * ico2/seki/src/Packet.c pac_hostBuild) or the draw's kind where it has no
 * mesh.
 *
 * And the stair-steppers: a matched mesh draw whose model is where it was
 * the tick before while the tick before it moved (its place in the world,
 * the model's origin through the inverse of the frame's camera, as
 * rd_interp.c vuWorldOrigin reads it).  An object the game moves every
 * other tick is blended between ticks but steps on alternate ones: next to
 * the blended world it looks slow and jerky.  A draw that only stopped
 * counts once; one that steps counts every other tick, so the top names
 * over 250 frames are the steppers.
 *
 * The line (the render thread's, every 250 frames, after the interpolation's
 * first line):
 *   rd: interp names: draws left at their tick's position over 250 frames
 *   (348 presents): unmatched 9 (cage 5, ico part 2 3, a screen prim 1);
 *   mismatched 0; jumped 2 (cage 2); turn snapped 0; unkeyed 812 (a screen
 *   prim 700, ...); stair-steppers (matched, still this tick after moving
 *   the tick before) 30 (cage part 2 15, ...); names past the table 0
 *
 * Nothing here changes a draw.  The tally and the stair test are pure
 * (port/render/test/rd_inames_test.c); rd_inames_live.c is rd_interp.c's
 * side. */
#ifndef ICO_RD_INAMES_H
#define ICO_RD_INAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RD_INAME_UNMATCHED = 0, /* keyed, no draw of the tick before paired with it */
    RD_INAME_MISMATCHED,    /* paired, not blendable (rd_interp.c R_MISMATCH) */
    RD_INAME_JUMPED,        /* paired, jumped (R_JUMP: drawn as the tick's) */
    RD_INAME_TURN,          /* blended, its turn snapped (over the turn limit) */
    RD_INAME_UNKEYED,       /* no key: drawn as the tick's through the blended camera */
    RD_INAME_STAIR,         /* matched, still this tick after moving the tick before */
    RD_INAME_COUNT
};

#define RD_INAMES_NAME 24 /* RdMeshRec.name's size */
#define RD_INAMES_SLOTS 128
#define RD_INAMES_TOP 5

typedef struct RdInameSlot {
    char name[RD_INAMES_NAME];
    uint32_t n[RD_INAME_COUNT];
    bool used;
} RdInameSlot;

typedef struct RdInameTally {
    RdInameSlot s[RD_INAMES_SLOTS];
    uint64_t total[RD_INAME_COUNT];
    uint64_t lost; /* notes whose name found no slot (the table full) */
} RdInameTally;

void rd_inames_reset(RdInameTally *t);
/* n draws of name (NULL or "": "a draw") left for why */
void rd_inames_add(RdInameTally *t, const char *name, int why, uint32_t n);
/* "draws left at their tick's position over F frames (P presents):
   unmatched N (name n, ...); ...; names past the table L" */
int rd_inames_text(const RdInameTally *t, uint32_t frames, uint32_t presents, char *out,
                   size_t cap);

/* --- the stair-steppers --------------------------------------------------- */

#define RD_INAMES_KEYS 4096 /* a power of two */

typedef struct RdInameKey {
    uint64_t key;
    uint32_t tick; /* the tick it was last seen at (0: empty) */
    bool moved;    /* it moved at that tick against the one before */
} RdInameKey;

typedef struct RdInameStairs {
    RdInameKey k[RD_INAMES_KEYS];
    uint32_t used;
} RdInameStairs;

void rd_inames_stairs_reset(RdInameStairs *s);
/* A matched draw of key seen at tick (the frame's number), prevTick the
   frame it was paired with, moved its place against that frame's: true
   when it is a stair step (still now, moved at prevTick against the one
   before).  The table emptied when three quarters full (stale keys) */
bool rd_inames_stair(RdInameStairs *s, uint64_t key, uint32_t tick, uint32_t prevTick, bool moved);

/* the model's origin in the world from a VU block (mem 0..35: the frame's
   screen-view at 4..7, the model through it at 16..19), rd_interp.c
   vuWorldOrigin's arithmetic; false when the view does not invert */
bool rd_inames_origin(const float (*m)[4], float out[3]);

/* The same with the view's inverse kept (one frame's draws share its
   screen-view: inverted once, compared to the bit) */
typedef struct RdInameView {
    float v[16];
    double inv[16];
    bool have, ok;
} RdInameView;

bool rd_inames_origin_kept(RdInameView *k, const float (*m)[4], float out[3]);
/* two origins apart by more than a millionth of their size (1 at least) */
bool rd_inames_moved(const float a[3], const float b[3]);

/* --- rd_interp.c's side (rd_inames_live.c; the render thread's) --------- */

struct RdCmd;
struct RdFrame;

/* a draw left for why (RD_INAME_*: unmatched, mismatched, jumped, turn
   snapped, unkeyed), named by its mesh or its kind */
void rd__inames_note(const struct RdCmd *c, int why);
/* a matched draw before its blend: pc the tick before's (in prev), op c's
   payload as recorded (s_out's, not yet blended); the stair test at the
   tick's first present (firstOfTick), mesh draws placed in the world */
void rd__inames_step(const struct RdFrame *prev, const struct RdCmd *pc, const uint8_t *op,
                     const struct RdCmd *c, uint32_t curNumber, int firstOfTick);
/* the line after the interpolation's first (rd_interp.c presentLog), the
   tally then emptied */
void rd__inames_log(uint32_t frames, uint32_t presents);

#endif /* ICO_RD_INAMES_H */
