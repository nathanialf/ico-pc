/* rd_vu_zero.h: the zero-area cull (issue 179; docs/port/PIPELINES.md,
 * "The zero-area cull").
 *
 * A triangle whose closed edges hold no sample point of the target it is
 * drawn on draws nothing: no fragment, no depth, no stencil.  The VU
 * pre-pass's rule stage (port/shaders/vu_tri.hlsl vu_tri_rule_pre_vs, its
 * vu_tri_zero) marks such a triangle VU_TF_ZERO in its record slot A, and
 * the draws that carry ICO_VU_ZERO_OK (rd_replay_vu.c vuDraw) cull it as a
 * dropped triangle is culled (vu_tri_verdict, vu_main_vs's early return).
 * The test is exact by construction: the triangle is grown by
 * ICO_VU_ZERO_GROW sixteenths of a texel each way (shader_consts.h: more
 * than the GPU's sub-pixel snap of at least 4 bits on every API and the
 * exact viewport's float steps can move a corner), and the closed, grown
 * triangle holds every point the GPU's own triangle can cover under its
 * fill rule.  The sample points are the target's texel centres, which the
 * exact viewport (rd_replay.c rd__frame_group_ex) puts on a lattice of the
 * GS's 1/16 grid at a whole-number scale: the lattice word
 * (shader_consts.h ICO_VU_ZERO_ON, rd_vu_zero_word) says where.
 *
 * When it is asked for (rd_vu_zero_word gives a word, else the reason):
 *   the exact viewport, GS integer coordinates on texel centres (not the
 *   MSAA epoch's expanded sprites), no jitter, the vertices on the GS's
 *   grid (not ICO_RD_VU_OFFGRID), a whole number of texels per GS pixel
 *   (1 to 15) on both axes, the Full pixel crop and the field's half line
 *   included, and the lattice's offset a whole sixteenth.
 * Where it is taken (rd__vu_zero_draw's reasons): a draw through the
 * record's main shader on its pre-pass entry's own target, offset and
 * picture (doVuDraw's frame key), single-sampled, on the mesh's original
 * list or the index compaction's (never the shared vertices' list, whose
 * vertices other triangles reuse: rd_vu_share.h); never the velocity pass
 * (its own VuCB, made before the flag), a probe or a program's own entry.
 *
 * The ways back (each logged, none written to the settings):
 *   - the start-up self-test (rd_vu_zero_selftest.c, run by
 *     rd_vuprepass_selftest after the pre-pass passed): triangles with
 *     CPU-computed answers drawn with the cull and without; the GPU's flag
 *     for every one must equal the CPU's, the pictures byte for byte; any
 *     difference, a pipeline missing or an answer not read back: off for
 *     the run.  Until it passes the cull is off;
 *   - the run-time check (rd__vu_zero_slot_begin): every
 *     RD_VU_ZERO_EVERY-th replay one entry's record (at most
 *     RD_VU_ZERO_KICKS triangles) is read back and every flag compared
 *     with the CPU's answer from the recorded corners (rd_vu_zero_check):
 *     a difference turns the cull off for the run;
 *   - the loss ladder (port/rhi/rhi_tile_rule.h, ICO_LOSS_ROW: a start
 *     after one that lost the graphics device or whose driver hung): off
 *     for this start (on a phone the VU pre-pass is off then too).
 * ICO_RD_VU_ZERO (tests): 0 off; 1 on without the self-test (the replay
 * tool runs none by default), every entry of every replay checked. */
#ifndef PORT_RENDER_RD_VU_ZERO_H
#define PORT_RENDER_RD_VU_ZERO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

/* why a draw does not cull (counted a draw, rd__vu_zero_draw; the first six
   also why an entry gets no word, rd_vu_zero_word) */
enum {
    RD_VU_ZERO_OFF,     /* off for the run or this start (rd__vu_zero_state_text) */
    RD_VU_ZERO_MAPPING, /* not the exact viewport, no half texel, or an offset off the grid */
    RD_VU_ZERO_SCALE,   /* texels per GS pixel not a whole number 1..15 */
    RD_VU_ZERO_JITTER,  /* a jittered frame (rd_jitter.h) */
    RD_VU_ZERO_GRID,    /* the vertices off the GS's grid (ICO_RD_VU_OFFGRID) */
    RD_VU_ZERO_MSAA,    /* a multisampled pass */
    RD_VU_ZERO_SHARED,  /* the shared vertices' list (rd_vu_share.h) */
    RD_VU_ZERO_ENTRY,   /* not through the record's main shader (a program's own entry, a
                           probe, a command the pre-pass did not record) */
    RD_VU_ZERO_REASONS
};

/* the run-time check: every RD_VU_ZERO_EVERY-th replay, one entry's first
   RD_VU_ZERO_KICKS triangles */
#define RD_VU_ZERO_EVERY 64u
#define RD_VU_ZERO_KICKS 1024u

/* ------------------------------------------------ the pure part (tests) */

/* A target as the exact viewport maps it (rd_replay.c rd__frame_group_ex,
   setViewport, vpExtent) */
typedef struct RdVuZeroMap {
    uint32_t w, h;          /* the target's GS size (FrameCB target.xy) */
    uint32_t tw, th;        /* its texture's size (RdTargetRec.tw, th) */
    float sx, sy;           /* its texels per GS pixel (RdTargetRec.sx, sy) */
    float cropOx;           /* Full pixel's left-out texels (RdTargetRec.ox), 0 none */
    float originX, originY; /* FrameCB origin.xy: the GS coordinate of its top-left */
    uint32_t extentMax;     /* the viewport's largest power of two (maxRenderTargetSize's) */
    bool exact;             /* the exact viewport (replayFrame's lists) */
    bool noHalf;            /* GS integer coordinates on texel edges (the MSAA epoch) */
    bool jitter;            /* a jittered frame group */
    bool offGrid;           /* the vertices off the 12.4 grid (FrameCB z.w) */
} RdVuZeroMap;

/* The lattice word (shader_consts.h ICO_VU_ZERO_ON) of target m, or 0 with
   *why (RD_VU_ZERO_MAPPING .. RD_VU_ZERO_GRID) when it cannot be proven */
uint32_t rd_vu_zero_word(const RdVuZeroMap *m, int *why);

/* vu_tri.hlsl vu_tri_zero, statement for statement: 1 when the triangle with
   GS corners (12.4, 0..65535) a, b, c covers no sample point of word's
   lattice, grown by ICO_VU_ZERO_GROW */
int rd_vu_zero_test(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy,
                    uint32_t word);

/* vu_common.hlsli vu_ftoi4 (saturating; NaN the minimum) */
int32_t rd_vu_zero_ftoi4(float x);

/* the record's answer against the CPU's: slots and triangles counted, the
   first difference in words */
typedef struct RdVuZeroCheck {
    uint64_t slots;      /* triangle slots read */
    uint64_t eligible;   /* of them tested (every corner at its GS position, the word) */
    uint64_t culled;     /* of them flagged VU_TF_ZERO by the GPU */
    uint64_t mismatches; /* the GPU's flag not the CPU's */
    char first[200];     /* the first mismatch, "" none */
} RdVuZeroCheck;

/* One entry's record read back: slot A of kicks triangles (a0: kicks uint4,
   lane x the flags), the vertex record's planes 1 and 2 of its kicks + 2
   vertices (r1, r2: uint4 each), its rule's clip mode (IcoVuCB.rec[3] &
   ICO_VU_CLIP_MASK) and the word it was given (0: none, so no flag may be
   set); added to *out */
void rd_vu_zero_check(const uint32_t *a0, const uint32_t *r1, const uint32_t *r2, uint32_t kicks,
                      uint32_t mode, uint32_t word, RdVuZeroCheck *out);

/* the reason's words ("shared list", "multisampled", ...) */
const char *rd_vu_zero_reason_name(int why);

/* ------------------------------------------------ the renderer's part */

struct RdStateBlock;
struct RdTargetRec;

/* rd__vu_prepass_init: the environment, the loss ladder (ICO_LOSS_ROW), the
   start state (off, or held until the self-test), its line when off */
void rd__vu_zero_init(void);
/* the record buffer is made able to be copied from (the checks' copies) */
bool rd__vu_zero_copyable(void);
/* the lattice word of a pre-pass entry drawn under st (its target, offset),
   0 when off or refused (*why) */
uint32_t rd__vu_zero_entry_word(const struct RdStateBlock *st, int *why);
/* the word the rule stage is given (word, but under the tests'
   ICO_RD_VU_ZERO_SABOTAGE=lattice half a texel off) */
uint32_t rd__vu_zero_gpu_word(uint32_t word);
/* a draw: culls (allowed) or why not, its triangles */
void rd__vu_zero_draw(bool allowed, int why, uint32_t tris);
/* the frame slot's checks, its frame done (rd__vu_prepass's start) */
void rd__vu_zero_slot_begin(uint32_t slot);
/* this replay's checks: whether entries are copied (all: every one, else
   one); then each entry's copy (rec the slot's record buffer, plane its
   plane's texels, base its first vertex's texel, count its vertices, slotA
   its kick first + 2's slot A texel), then the slot's end */
bool rd__vu_zero_check_now(bool *all);
void rd__vu_zero_copy(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t plane,
                      uint32_t base, uint32_t count, uint32_t slotA, uint32_t mode, uint32_t word);
/* every slot's checks read now (the GPU waited for): the self-test, the
   replay tool's stats */
void rd__vu_zero_collect_now(void);
/* the self-test's frames: the cull asked for (on, every entry checked) or
   not (off); rd__vu_zero_hold_end puts the run's state back */
void rd__vu_zero_hold(bool on);
void rd__vu_zero_hold_end(void);
/* the checks' totals since the last call (the self-test's) */
void rd__vu_zero_take(RdVuZeroCheck *out);
/* the self-test passed (on for the run) or failed (off, why) */
void rd__vu_zero_verdict(bool pass, const char *why);
/* the start-up self-test (rd_vu_zero_selftest.c) on SCENE t, after the
   VU pre-pass's passed; its line */
void rd__vu_zero_selftest(struct RdTargetRec *t);
/* no self-test (the pre-pass off or not tested): its line, the cull off */
void rd__vu_zero_selftest_skip(const char *why);
/* whether the cull is on for the run now; its state's words */
bool rd__vu_zero_on(void);
const char *rd__vu_zero_state_text(void);

/* The 10 s VU line's clause (port/platform/window_host.c, after the shared
   vertices'), the block's n replays: "; zero-area cull: D draws a picture
   culling, T triangles; refused a picture: shared list a, ...; checked P
   pictures: C of S triangle slots culled", or "; zero-area cull off (why)" */
size_t rd_vu_zero_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("VU zero-area cull ..."): the run's counts, every
   check read first */
int rd__vu_zero_stats_text(char *out, size_t cap);

#endif
