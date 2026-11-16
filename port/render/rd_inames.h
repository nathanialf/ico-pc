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
 * And the stair-steppers: a matched mesh draw placed in the world that the
 * interpolation would draw still this tick (its placement where it was the
 * tick before) while it moved at the tick before against the one before
 * that.  "Moved" is the interpolation's own still bound (rd_interp.c
 * stillRows, RD_INTERP_STILL_REL 2^-17): the tick before's model rows
 * (VU mem 16..19, the model through the frame's screen-view) carried to
 * this tick's view (the tick before's view inverted, this tick's applied)
 * against this tick's, X, Y and W apart by more than 2^-17 of the larger
 * magnitude in their row.  v0.11.7-rc1 compared the model's origin in the
 * world (through the view's inverse) at a millionth of its size: the
 * screen-view's Z lane (about 1e9) puts the float rounding of a still
 * object's origin at 1.4e-7 to 2.0e-6 of its size in the world (seven
 * stage 4 dump pairs of the owner's phone, the camera moving: taimatsu1,
 * chain, cage, sekizo01, all still), so a still torch moved and stopped at
 * random and counted as a stepper; carried in the rows the same objects
 * are 0.1-1.1 % of the 2^-17 bound and Ico's moving parts 90 to 24000
 * times over it.
 *
 * Each name in the list carries its cadence over the window: the ticks its
 * draws moved in of the ticks judged, and of them the ticks in a run of
 * every other tick (moved, still, moved, still: the last four judged ticks
 * alternating), "chain4 40 [moved 120 of 245, alternating 118]": an
 * object the game moves every other tick shows about half its ticks moved
 * and nearly all of them alternating; one that pauses now and then, few.
 *
 * The line (the render thread's, every 250 frames, after the interpolation's
 * first line; counted at each frame's first present, as that line's keyed
 * draws are):
 *   rd: interp names: draws left at their tick's position over 250 frames
 *   (348 presents; counted at each frame's first present): unmatched 9
 *   (cage 5, ico part 2 3, a screen prim 1); mismatched 0; jumped 2 (cage
 *   2); turn snapped 0; unkeyed 812 (a screen prim 700, ...); stair-steppers
 *   (matched, still this tick after moving the tick before) 30 (cage part 2
 *   15 [moved 15 of 30, alternating 12], ...); names past the table 0
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
    /* of n[RD_INAME_UNMATCHED], the draws whose VU block scrolls their
       texture (qw 2's x or y not 0: an unmatched draw keeps its tick's
       scroll, rd_interp.c camCurDraw; issue 123's right wall) */
    uint32_t scroll;
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
/* of name's unmatched draws, n whose texture scrolls (after their
   rd_inames_add; a name not in the table: nothing) */
void rd_inames_scroll(RdInameTally *t, const char *name, uint32_t n);
struct RdInameStairs;
/* "draws left at their tick's position over F frames (P presents; counted
   at each frame's first present): unmatched N (name n, ...); ...; names
   past the table L", each stair-stepper's name with its cadence from cad
   ("[moved M of J, alternating A]"; cad NULL: none), each unmatched name
   whose draws scrolled their texture with "[scroll S]" (S of its n) */
int rd_inames_text(const RdInameTally *t, uint32_t frames, uint32_t presents,
                   const struct RdInameStairs *cad, char *out, size_t cap);

/* --- the stair-steppers --------------------------------------------------- */

#define RD_INAMES_KEYS 4096 /* a power of two */
/* the interpolation's still bound (rd_interp.c RD_INTERP_STILL_REL) */
#define RD_INAMES_STILL_REL (1.0 / 131072.0)

typedef struct RdInameKey {
    uint64_t key;
    uint32_t tick;     /* the tick it was last seen at (0: empty) */
    uint32_t nameHash; /* its name's (rd_inames_name_hash) */
    uint16_t judged;   /* in the window: ticks judged against the tick before */
    uint16_t moved;    /* of them, moved */
    uint16_t alt;      /* of them, the last four judged alternating */
    uint8_t hist;      /* moved bits of the judged ticks, the latest bit 0 */
    uint8_t run;       /* judged ticks in a row (at most 8) */
} RdInameKey;

typedef struct RdInameStairs {
    RdInameKey k[RD_INAMES_KEYS];
    uint32_t used;
} RdInameStairs;

enum {
    RD_INAMES_V_JUDGED = 1, /* paired with the tick before's: judged */
    RD_INAMES_V_MOVED = 2,  /* moved against it */
    RD_INAMES_V_STEP = 4,   /* still now, moved at the tick before against the one before */
    RD_INAMES_V_ALT = 8     /* the last four judged ticks alternate (moved, still, ...) */
};

void rd_inames_stairs_reset(RdInameStairs *s);
/* A matched draw of key (its name's hash nameHash) seen at tick (the
   frame's number), prevTick the frame it was paired with, moved its place
   against that frame's: RD_INAMES_V_* bits (0: not judged: the first sight,
   a second draw of the key in the tick, a tick skipped).  The table
   emptied when three quarters full (stale keys) */
unsigned rd_inames_stair(RdInameStairs *s, uint64_t key, uint32_t nameHash, uint32_t tick,
                         uint32_t prevTick, bool moved);
/* the window's cadence of the keys of nameHash (summed), then every key's
   zeroed by rd_inames_stairs_window */
void rd_inames_cadence(const RdInameStairs *s, uint32_t nameHash, uint32_t *judged, uint32_t *moved,
                       uint32_t *alt);
void rd_inames_stairs_window(RdInameStairs *s);
/* a name's hash as the tally keys it (cut to RD_INAMES_NAME - 1 bytes;
   NULL or "": "a draw") */
uint32_t rd_inames_name_hash(const char *name);

/* The tick before's view carried to this tick's: t = Vp^-1 Vc (the
   frames' screen-views, VU mem 4..7 of each tick's block), kept while both
   views stay (one frame's draws share them) */
typedef struct RdInameCarry {
    float vp[16], vc[16];
    double t[16];
    bool have, ok;
} RdInameCarry;

/* t for the blocks mp (the tick before's) and mc (this tick's); false when
   the tick before's view does not invert */
bool rd_inames_carry(RdInameCarry *k, const float (*mp)[4], const float (*mc)[4]);
/* the model moved: mp's rows 16..19 through t against mc's, lanes X, Y and W,
   apart by more than RD_INAMES_STILL_REL of the larger magnitude of the
   row's lanes (the interpolation's still test, rd_interp.c stillRows) */
bool rd_inames_moved(const double t[16], const float (*mp)[4], const float (*mc)[4]);

/* --- rd_interp.c's side (rd_inames_live.c; the render thread's) --------- */

struct RdCmd;
struct RdFrame;

/* the present's start: the notes below count at a frame's first present
   only (firstOfTick), as the interpolation's own line counts its draws;
   out the frame being blended, whose payload the notes' commands address
   (an unmatched draw's texture scroll read from its VU block) */
void rd__inames_first(int firstOfTick, const struct RdFrame *out);
/* a draw left for why (RD_INAME_*: unmatched, mismatched, jumped, turn
   snapped, unkeyed), named by its mesh or its kind */
void rd__inames_note(const struct RdCmd *c, int why);
/* a matched draw before its blend: pc the tick before's (in prev), op c's
   payload as recorded (s_out's, not yet blended); the stair test at the
   tick's first present (firstOfTick), mesh draws placed in the world */
void rd__inames_step(const struct RdFrame *prev, const struct RdCmd *pc, const uint8_t *op,
                     const struct RdCmd *c, uint32_t curNumber, int firstOfTick);
/* the line after the interpolation's first (rd_interp.c presentLog), the
   tally and the window's cadence then emptied */
void rd__inames_log(uint32_t frames, uint32_t presents);

#endif /* ICO_RD_INAMES_H */
