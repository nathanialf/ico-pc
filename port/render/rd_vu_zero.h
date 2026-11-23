/* rd_vu_zero.h: the zero-area cull (issue 179; docs/port/PIPELINES.md,
 * "The zero-area cull").
 *
 * A triangle whose closed edges hold no sample point of the target it is
 * drawn on draws nothing: no fragment, no depth, no stencil.  The rule
 * stage's zero builds (port/shaders/vu_tri.hlsl with ICO_VU_ZERO=1:
 * vu_tri_rule_zero_*, the vertex shader id RD_VS_VU_TRI_RULE_ZERO) mark
 * such a triangle VU_TF_HIDDEN | VU_TF_ZERO in its record slot A, and every
 * reader of the slot culls it as a hidden triangle (vu_tri_verdict:
 * vu_main_vs's early return, the index compaction's filler).  Every module
 * the build had without the cull is the same module byte for byte; the
 * zero build exists only once the cull is tried.  The test is exact by
 * construction: the triangle is grown by ICO_VU_ZERO_GROW sixteenths of a
 * texel each way (more than a GPU's sub-pixel snap of at least 4 bits and
 * the exact viewport's float steps can move a corner), and the closed,
 * grown triangle holds every point the GPU's own triangle can cover under
 * any fill rule.  The sample points are the target's texel centres, which
 * the exact viewport (rd_replay.c rd__frame_group_ex) puts on the GS's 1/16
 * grid at a whole-number scale: the lattice word (shader_consts.h
 * ICO_VU_ZERO_ON, rd_vu_zero_word) says where; the rule stage's draw of an
 * entry with a word takes the zero build, every other the build without.
 *
 * An entry gets a word (rd_vu_zero_word, rd__vu_zero_entry_word,
 * rd__vu_zero_entry_keep) when: the exact viewport, texel centres on GS
 * integer coordinates, no jitter, the vertices on the GS's grid, a whole
 * number of texels per GS pixel (1 to 15), no MSAA setting and not WORK2
 * (the lamps' pass there may be multisampled), not skinned (the skin
 * check's twins), no velocity pass reading it, not a probe.  Its draws then
 * keep the mesh's original list (the shared vertices' list reuses a value
 * for other triangles: rd_vu_share.h); a draw of it in a multisampled pass
 * all the same turns the cull off for the run.
 *
 * The ways back (each logged, none written to the settings):
 *   - the install's mark (port/platform/vu_zero_mark.h, ICO_VU_ZERO_MARK,
 *     ICO_VU_ZERO_VERSION, ICO_VU_ZERO_BLOCK), each word with the running
 *     version: "trying" written and synced before the zero build is made
 *     (only once the settings give SCENE a lattice: a refusal by the
 *     settings writes nothing and makes nothing), "counting" when the
 *     self-test passed, "on" after RD_VU_ZERO_PROBATION pictures drawn with
 *     it, "failed" when the module could not be made or the self-test
 *     refused it; the next start keeps the cull off on that install while
 *     that version runs after any end but a clean close while trying, after
 *     a bad end (a crash, an ANR, a signal, a device loss, a GPU hang, the
 *     watchdog) while counting or on, or after "failed";
 *   - the start-up self-test (rd_vu_zero_selftest.c, run by
 *     rd_vuprepass_selftest after the pre-pass passed): triangles with
 *     CPU-computed answers drawn with the cull and without; the GPU's flag
 *     for every one must equal the CPU's, the pictures byte for byte; any
 *     difference, a module or pipeline missing or an answer not read back:
 *     off for the run, the module and its pipeline released.  Until it
 *     passes the cull is off and nothing of it exists on the device;
 *   - the loss ladder (port/rhi/rhi_tile_rule.h, ICO_LOSS_ROW: a start
 *     after one that lost the graphics device or whose driver hung): off
 *     for this start.
 * Once on, the cull is timed in play (rd_ab.h, rd_ab_run.h: a trial 2 s
 * into each stage alternates replays with it and without it, a verdict
 * kept for the stage): a replay at the judge's off arm gives no entry a
 * word (RD_VU_ZERO_TRIAL) and takes the build without it for every entry,
 * the path the device ran before the cull; such a picture is not one
 * "drawn with it" for the mark's count, and a verdict "off" writes nothing
 * to the mark.
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
    RD_VU_ZERO_OFF,      /* off for the run or this start (rd__vu_zero_state_text) */
    RD_VU_ZERO_MAPPING,  /* not the exact viewport, no half texel, or an offset off the grid */
    RD_VU_ZERO_SCALE,    /* texels per GS pixel not a whole number 1..15 */
    RD_VU_ZERO_JITTER,   /* a jittered frame (rd_jitter.h) */
    RD_VU_ZERO_GRID,     /* the vertices off the GS's grid (ICO_RD_VU_OFFGRID) */
    RD_VU_ZERO_MSAA,     /* a multisampled pass */
    RD_VU_ZERO_VELOCITY, /* the velocity pass reads the entry's slots (VELOCITY: not tested) */
    RD_VU_ZERO_ENTRY,    /* not through the record's main shader (a program's own entry, a
                           probe, a command the pre-pass did not record) */
    RD_VU_ZERO_TRIAL,    /* the in-play judge's off arm for this replay (rd_ab_run.h) */
    RD_VU_ZERO_REASONS
};

/* the pictures drawn with the cull after its self-test before its mark says
   on (10 s at 25 a second: past the first pictures of a 3D scene, where the
   earlier device losses on the Mali-G52 came) */
#define RD_VU_ZERO_PROBATION 250u

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

/* the reason's words ("velocity", "multisampled", ...) */
const char *rd_vu_zero_reason_name(int why);

/* ------------------------------------------------ the renderer's part */

struct RdStateBlock;
struct RdTargetRec;

/* rd__vu_prepass_init: the environment, the loss ladder (ICO_LOSS_ROW), the
   start state (off, or held until the self-test), its line when off; any
   read back buffer of a device before forgotten */
/* One checked picture's counts into the block's and the run's (its read
   back, and the tests: the 10 s clause's "checked" part) */
void rd__vu_zero_checked(uint64_t slots, uint64_t culled, uint64_t mismatches);
void rd__vu_zero_init(void);
/* rd__vu_prepass_shutdown, with the device: the self-test's read back
   buffers destroyed and forgotten (a renderer made again, as after a
   Graphics API switch, makes its own at its try) */
void rd__vu_zero_shutdown(void);
/* the read back buffers alive now; *made those made this process (the
   tests: made again at each try, none alive after the self-test) */
uint32_t rd__vu_zero_check_buffers(uint32_t *made);
/* the record buffer is made able to be copied from (the checks' copies):
   only while the self-test reads it back, or under the tests' =1 */
bool rd__vu_zero_copyable(void);
/* the cull tried: the mark "trying" (synced), then the zero build of the
   record's mode and batches made; false when it could not be (or is off) */
bool rd__vu_zero_try_begin(void);
/* the zero builds made this process (the tests: 0 without a try) */
uint32_t rd__vu_zero_variants_made(void);
/* the rule stage's vertex shader id: the zero build for every entry while
   the cull is on and its build made (an entry without a word skips the
   test), else (off, or the in-play judge's off arm for the replay) the
   build without the cull */
uint8_t rd__vu_zero_rule_vs(uint32_t word);
/* an entry's word kept, or 0 with *why: skinned, read by the velocity
   pass, a probe */
uint32_t rd__vu_zero_entry_keep(uint32_t word, bool skinned, bool velocity, bool probe, int *why);
/* a replay's pre-pass: culling (an entry had a word); the probation's count */
void rd__vu_zero_picture(bool culling);
/* a draw of a culling entry in a multisampled pass: off for the run */
void rd__vu_zero_fault_msaa(void);
/* a culling entry's draw gave up the shared vertices' list (counted) */
void rd__vu_zero_shared_given_up(void);
/* rd_replay_vu.c: the record buffers' usage changed (rd__vu_zero_copyable):
   every slot's made again at the next replay */
void rd__vu_rec_usage_changed(void);
/* rd_pipeline.c: every made pipeline of vertex shader vs destroyed (its
   entry left as a failed one: never asked for again); the count */
uint32_t rd__pipeline_drop_vs(uint8_t vs);
/* rd_pipeline.c: the made pipelines of vertex shader vs */
uint32_t rd__pipeline_count_vs(uint8_t vs);
/* the lattice word of a pre-pass entry drawn under st (its target, offset),
   0 when off or refused (*why; RD_VU_ZERO_TRIAL at the in-play judge's off
   arm, where the cull also says it had work: rd__ab_used) */
uint32_t rd__vu_zero_entry_word(const struct RdStateBlock *st, int *why);
/* the same whatever the cull's state: the settings' answer for st's target
   alone (the self-test asks it before it tries anything) */
uint32_t rd__vu_zero_target_word(const struct RdStateBlock *st, int *why);
/* the word the rule stage is given (word, but under the tests'
   ICO_RD_VU_ZERO_SABOTAGE=lattice half a texel off) */
uint32_t rd__vu_zero_gpu_word(uint32_t word);
/* the tests' ICO_RD_VU_ZERO_SABOTAGE=picture: the self-test's picture
   without the cull changed in one byte before the compare (its picture
   branch's way back) */
bool rd__vu_zero_sabotage_picture(void);
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
/* the start's line after the self-tests: "VU zero-area cull: on|held|off,
   N modules of it made (P pipelines)" (the tests read the count: 0 where it
   was never tried) */
void rd__vu_zero_start_line(void);
/* the cull may be put on trial in play (rd_ab_run.h): on, its build made,
   not the self-test's frames (a blocked, failed or held cull never is) */
bool rd__vu_zero_triable(void);
/* the cull is off for the rest of the run (off for a reason a renderer made
   again keeps: a failed self-test or check, the install's mark, the loss
   ladder, ICO_RD_VU_ZERO=0; not a self-test merely not run): the in-play
   judge's notice can leave (rd_ab.h rd_ab_gone) */
bool rd__vu_zero_gone(void);
/* whether the cull is on for the run now; its state's words */
bool rd__vu_zero_on(void);
const char *rd__vu_zero_state_text(void);

/* The 10 s VU line's clause (port/platform/window_host.c, after the shared
   vertices'), the block's n replays: "; zero-area cull: D draws a picture
   culling, T triangles; refused a picture: velocity a, ...; shared lists
   given up a picture: g", or "; zero-area cull off (why)" (the checks'
   clause only where a check ran: the tests) */
size_t rd_vu_zero_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("VU zero-area cull ..."): the run's counts, every
   check read first */
int rd__vu_zero_stats_text(char *out, size_t cap);

#endif
