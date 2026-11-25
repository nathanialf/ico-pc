/* rd_onepass.h: the one-pass blend (issue 179; docs/port/PIPELINES.md,
 * "The one-pass blend").
 *
 * Why.  Without dual-source blending (a Vulkan device whose features lack
 * dualSrcBlend: the issue 179 reporter's Mali-G52) a (Cs - Cd) As + Cd draw
 * whose mask has R, G or B and A is two passes (rd_pipeline.c
 * rd__expand_no_dual): a colour pass whose c0.a is the factor As/128 under
 * SRC_ALPHA / ONE_MINUS_SRC_ALPHA, then an alpha pass writing As/255 with
 * blending off.  The v0.11.15 log has 31k of the cage scene's 46k triangles
 * drawn twice that way; v0.11.10's (the last with a one-pass form) had every
 * pair of that phone a LERP with the As factor.
 *
 * What.  The colour pass also writes A (key.gs.nodual's bit
 * RD_NODUAL_ONEPASS, the mask with A), its alpha blended as c0.a times the
 * blend constant's alpha 128/255 and nothing of Ad (rd_pipeline.c
 * blendState; rd__op_bind sets the constant): (As/128) (128/255) = As/255,
 * the alpha pass's byte, for every As 0..128 under round-to-nearest at any
 * blender precision of 8 bits or more (rd_op_alpha_model; the unit test's
 * table; Vulkan does not pin the blender's rounding, so the device's own
 * start-up check decides).  The pair's alpha pass is then not drawn.  A
 * pair is taken only where the CPU proves the bytes (rd_op_why): a LERP
 * with the As factor (not FIX), no PABE, no FBA, the alpha plan's largest
 * As at most 128, not Z GREATER with Z write, an RGBA8 UNORM target, no
 * destination read, single-sampled.  Only the taken pair's colour pass
 * changes its key; every other pass, and every pair while nothing is in
 * use, keeps today's key, pipeline and bytes.
 *
 * The classes.  A code 36 command (the scissor) is two draws, its cut
 * fans under the state with ABE on and its kicks under the state; with the
 * state's ABE off they are fused (rd_replay_vu.c vuFuseVerdict): the kick
 * draws under the cut's pipeline, its pixels unblended (the factor 1.0 in
 * c0.a).  A fused pair cannot go to one pass: the kick's pixel carries the
 * factor 1.0 in the one output the blender reads, and its stored alpha
 * As/255 would need a second value (so no fused one-pass form exists: the
 * brief's arm D).  v0.11.8-v0.11.10 unfused such a command where its cut's
 * pair went to one pass, and the reporter's phone drew stretched triangles
 * (issue 179, v0.11.10's report; v01116 handbacks/TRI-ONEPASS.md: the
 * unfused code 36 path is the leading suspect, never drawn by that
 * release's self-test).  So two classes, each measured on its own arm of
 * the device check (only the plain class is ever put in use: below):
 *   plain  a pair of a draw that is not code 36's (no cut, kick or fused
 *          bit in its VuCB);
 *   cut    a pair of a code 36 cut or kick drawn apart (the cut of a
 *          command unfused for it: rd__op_unfuse), never a fused one's.
 *
 * The device check (rd_onepass_selftest.c, after the fast pixel build's,
 * on SCENE at its start size with the VU record at play's size; the
 * discard-free twins and the fast pixel build held in their run states, so
 * the keys drawn are the keys play draws): the discard-free twins' clipper
 * check's 13 cases (triangles across and near the eye plane) as frames of
 * their own, each at code 32 (ABE on), 34 (clip mode none, ABE on), 36 with
 * the state's ABE off (the fused pair: the reporter's pattern) untextured
 * and textured (RGBA32, MODULATE, TCC: the fast pixel build's form), and 36
 * with ABE on (cut and kick drawn apart) untextured, every corner's alpha
 * 0..127, then a depth probe (a code 34 quad at GS Z 0, Z write off: a
 * depth written by one form and not the other shows in the colour).  Each
 * frame drawn and SCENE read back under:
 *   REF  today: two passes, code 36 fused where it fuses;
 *   A    one pass on the plain class only (on code 36 frames A is REF);
 *   B    code 36 unfused where C unfuses it, two passes kept (the suspect
 *        alone, no blend state changed);
 *   C    A + B + one pass on the cut class (v0.11.8's whole form; on the
 *        frames without code 36 the same draws as A, A's replay counted);
 *   S    C with the constant's alpha 127/255: the check's positive control,
 *        which must differ where a pair went to one pass.
 * Every arm compared byte for byte with REF on every frame that drew.  The
 * pipelines of every frame and arm gathered in ONE gathering and made on
 * the workers first; a replay that found a pipeline not made (its own or
 * the twins' or the fast build's, asked for in the draw) is drawn again
 * once the background queue is made; still without one the frame compares
 * nothing for that arm ("not checked"), never a verdict.  A frame whose
 * case drew nothing (REF against the probe alone) tests nothing.  An arm
 * passes only with every frame the same, at least one pair of its class
 * in one pass (B: at least one command unfused, or nothing fused on this
 * device at all) and its control seen to differ.  In use: the plain class
 * where A passed, and only for draws whose every corner reaches the
 * rasteriser at w = 1 (rd_op_safe_draw: clip mode none, a kick drawn
 * apart, a prelit mesh proven wholly in front of the eye).  The cut class
 * is never used: where a corner reaches the GPU's clipper behind the eye
 * the noperspective colour extrapolates past its corners, so As can pass
 * 128 where the alpha plan says not, and the carry stores 128 where the
 * alpha pass stores As (measured on lavapipe: arm C at case 7, As 129 from
 * corners of at most 127).  B and C are logged measurements.  Logged per
 * case and per arm.
 *
 * The ways back (each logged, none written to the settings):
 *   - the check (above): an arm that does not pass keeps its class in two
 *     passes; nothing passed: off for the run;
 *   - the install's mark (port/platform/onepass_mark.h,
 *     logs/onepass-mark.txt, ICO_ONEPASS_MARK, ICO_ONEPASS_VERSION,
 *     ICO_ONEPASS_BLOCK): "trying" written and synced before the check,
 *     "counting" when a class is in use, "on" after RD_OP_PROBATION
 *     pictures drawn with it, "failed" when the check put nothing in use;
 *     a bad end while trying, counting or on keeps it off until the version
 *     changes;
 *   - the loss ladder (ICO_LOSS_ROW): off for this start;
 *   - a pipeline of it the background could not make: off for the run;
 *   - the in-play judge (rd_ab_run.h RD_AB_ONEPASS): with its verdict
 *     "off" every pair is drawn in two passes, nothing unfused.
 * A wrong picture in play is not detected: what the check did not draw is
 * not known to be right on the device.
 *
 * Its pipelines are made in the background between pictures (the
 * discard-free twins' queue, rd_pipeline.c rd__pipeline_twin): a pair whose
 * one-pass pipeline is not made yet is drawn in two passes, and a code 36
 * command is unfused only once the cut's one-pass pipeline and the kick's
 * own are made.  With the device's dynamic blend state the one-pass key
 * folds into the colour pass's own pipeline (rd__key_dynamic_normalise),
 * its blend state set at each bind.
 *
 * The tests' switches (docs/port/TESTING.md; no setting): ICO_RD_ONEPASS=0
 * off; =plain or =full on without the check (the plain class, or both),
 * its pipelines made in the draw (the replay tool's A/B);
 * ICO_RD_ONEPASS_SABOTAGE=constant (the constant 127/255 in arms A and C:
 * both fail), =cut (127/255 on the code 36 frames' C alone: C fails, A
 * stays), =unmade (every one-pass pipeline taken as not made in the check:
 * "not checked"), =sliver (arm C's picture given a texel on a code 36 frame
 * where today's two passes drew nothing: C must differ), =read (the first
 * frame's reference read taken as failed without a device loss: not
 * checked, no verdict). */
#ifndef PORT_RENDER_RD_ONEPASS_H
#define PORT_RENDER_RD_ONEPASS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

/* key.gs.nodual's bit for the colour pass that also writes A (with
   RD_NODUAL_ON; composes with the twins' RD_NODUAL_NODISCARD and the fast
   build's RD_NODUAL_FAST) */
#define RD_NODUAL_ONEPASS 8u

/* the largest As the proof takes (the factor As/128 at most 1.0) */
#define RD_OP_ALPHA_MAX 128u

/* the pictures drawn with it after the check before the mark says on */
#define RD_OP_PROBATION 250u

/* the classes (a bit each in the use mask) */
#define RD_OP_PLAIN 1u
#define RD_OP_CUT 2u

/* the check's arms */
enum { RD_OP_ARM_REF = 0, RD_OP_ARM_A, RD_OP_ARM_B, RD_OP_ARM_C, RD_OP_ARMS };

/* an arm's verdict */
enum {
    RD_OP_V_NONE = 0,  /* not run */
    RD_OP_V_PASS,      /* every frame the same bytes, its class drawn, its control differed */
    RD_OP_V_DIFFERS,   /* a frame differs (measured) */
    RD_OP_V_UNCHECKED, /* no difference, but frames without a pipeline after a retry */
    RD_OP_V_UNTESTED,  /* no pair of its class (or no command unfused) drawn */
    RD_OP_V_NOTHING,   /* B: nothing fused on this device, nothing to unfuse */
    RD_OP_V_BLIND,     /* its control (the constant 127/255) did not differ */
    RD_OP_V_DEVICE,    /* the device failed in its replays */
    RD_OP_VERDICTS
};

/* a pair's verdict at its draw: in one pass, or why not (counted a pair) */
enum {
    RD_OP_ONE = 0, /* drawn in one pass */
    RD_OP_ALPHA,   /* As may pass 128 (or the range is not known) */
    RD_OP_FBA,
    RD_OP_PABE,
    RD_OP_FIX,    /* the factor is FIX (LERP FIX), not As */
    RD_OP_OTHER,  /* Cd*FIX + Cs, Z GREATER with Z write, the target, a read, MSAA */
    RD_OP_EYE,    /* a corner may reach the rasteriser behind the eye (rd_op_safe_draw) */
    RD_OP_FUSED,  /* a fused code 36 pair (its kick, or a fused command's cut) */
    RD_OP_CLASS,  /* its class not in use on this device */
    RD_OP_UNMADE, /* its pipeline not made yet (asked for in the background) */
    RD_OP_TRIAL,  /* the in-play judge's off arm */
    RD_OP_REASONS
};

/* ------------------------------------------------ the pure part (tests) */

struct RdDrawPass;

/* the proof for the pair colour (ICO_DF_NODUAL_FACTOR, mask without A) and
   alpha (ICO_DF_NODUAL_ALPHA_PASS, mask A), class aside: RD_OP_ONE when one
   pass stores the same bytes; hi the alpha plan's largest As of the draw,
   samples the pass's sample count */
int rd_op_why(const struct RdDrawPass *colour, const struct RdDrawPass *alpha, uint8_t hi,
              uint32_t samples);
/* the class of a draw by its VuCB draw[2] bits: RD_OP_PLAIN, RD_OP_CUT
   (a cut or kick drawn apart), 0 (a fused kick, or the cut of a command
   that fused: cmdFused) */
uint32_t rd_op_class_of(uint32_t vuBits, bool cmdFused);
/* the classes in use from the arms' verdicts (RD_OP_V_*): the plain class
   where A passed, never the cut class (its one-pass form is inexact by
   construction, rd_op_safe_draw: B and C are logged measurements only) */
uint32_t rd_op_classes(int a, int b, int c);
/* Whether every corner of the draw reaches the rasteriser at w = 1 (its GS
   position), so the noperspective colour, and with it As, interpolates
   within its corners' values and the alpha plan's hi bounds every pixel:
   clip mode none (vu_gs_position, w 1), a code 36 kick drawn apart (its
   triangles have no clip flag: their GS positions), or a mesh the CPU
   proved wholly in front of the eye (proven: rd_vis.c
   rd__vis_mesh_gs_proof); never particles.  A corner at w <= 0 goes to
   the GPU's clipper, whose new vertices' noperspective values are taken
   in window coordinates, where a corner behind the eye projects through
   it: the colour extrapolates past its corners (the check measured As 129
   from corners of at most 127 at case 7, code 36, on lavapipe), and the
   carry clamps As above 128 to 128 where the alpha pass stores As */
bool rd_op_safe_draw(uint32_t vuBits, bool particle, bool proven);
/* the stored alpha of the carry and of the alpha pass under a blender
   model (rd_onepass_test's table): each value quantised to an unsigned
   normalised fixed point of bits (8..24) or to a float of 16 or 32 bits
   (116, 132), rounding 0 nearest, 1 down, 2 up; the source's precision bs,
   the constant's bk, the product's bp; store the conversion to 8 bits.
   *two the alpha pass's byte for As, the return the carry's */
int rd_op_alpha_model(int as, int bs, int bk, int bp, int round, int store, int *two);
/* the words of a reason ("drawn in one pass", "alpha above 128", ...), of a
   verdict ("the same bytes", "differs", ...), of the classes ("the pairs
   outside code 36 and code 36's", "none") */
const char *rd_op_reason_name(int why);
const char *rd_op_verdict_name(int v);
const char *rd_op_classes_name(uint32_t classes);

/* the in-play judge's use of a replay (as the fast build's RdFpsUse) */
typedef struct RdOpUse {
    bool one, unmade;
} RdOpUse;

void rd_op_use_note(RdOpUse *u, int why);
bool rd_op_use_counts(const RdOpUse *u);

/* ------------------------------------------------ the renderer's part */

struct RdPipeKeyInt;
struct RdTargetRec;

/* rd__vu_prepass_init: the environment, the loss ladder, the install's
   mark, the start state (off, or held until the check) */
void rd__op_init(void);
/* a replay's start (rd__vu_prepass): the last replay's pictures counted
   for the probation, the judge's note cleared */
void rd__op_replay_begin(void);
/* rd_replay_vu.c vuDraw, after the plan: each pair of dp[0 .. *np)
   classified and counted; a pair in one pass where its class is in use (or
   the check's arm asks for it) and its pipeline is made: its colour pass's
   key takes RD_NODUAL_ONEPASS and A, its alpha pass is dropped (the passes
   after it moved down, *np lowered).  hi the alpha plan's largest As,
   vuBits the VuCB's draw[2], count the draw's index count */
void rd__op_take(struct RdDrawPass *dp, int *np, uint8_t hi, uint32_t vuBits, uint32_t count,
                 bool particle, bool proven);
/* rd_replay_vu.c vuFuseVerdict: a code 36 command whose cut pair (cut[0],
   cut[1]) would go to one pass is not fused with its kick (kick: the kick's
   one planned pass), the pipelines of both made (or asked for); the check's
   arm B unfuses it with its two passes kept */
bool rd__op_unfuse(const struct RdDrawPass *cut, int nc, const struct RdDrawPass *kick, int nk,
                   uint8_t hi);
/* rd_replay_vu.c doVuDraw: the code 36 command's verdict (fused or not),
   for its cut draws' class */
void rd__op_cut_fused(bool fused);
/* rd_pipeline.c rd__set_pipeline: the blend constant (alpha 128/255) for a
   one-pass key; nothing for any other key (NULL included) */
void rd__op_bind(RhiCommandList cl, const struct RdPipeKeyInt *k);
/* rd_pipeline.c: a pipeline of it the background could not make (off for
   the run) */
void rd__op_fault(const char *what);
/* on: a class in use (or the check drawing) */
bool rd__op_on(void);
/* the in-play judge may time it: on, not the check's frames */
bool rd__op_triable(void);
const char *rd__op_state_text(void);
/* the check (rd_onepass_selftest.c) on SCENE t, after the fast pixel
   build's; its lines, then the start line */
void rd__op_selftest(struct RdTargetRec *t);
/* no check (the pre-pass's self-test not run, no SCENE): its line and the
   start line, off for the run */
void rd__op_selftest_skip(const char *why);
/* "one-pass blend: on (classes ...)|held|off (why), P of its pipelines
   alive" */
void rd__op_start_line(void);

/* the check's state (rd_onepass_selftest.c): its arm drawn (RD_OP_ARM_*),
   -1 none; sabotage the constant 127/255 in its taken pairs.  The rate
   card (rd_ratecard_run.c) holds RD_OP_ARM_REF through its frames: its
   blended layer is the two-pass blend's */
void rd__op_check_arm(int arm, bool sabotage);
/* the check's counts since the last call: pairs in one pass by class
   ([0] plain, [1] cut), commands unfused, pairs whose pipeline was not made */
void rd__op_check_take(uint64_t one[2], uint64_t *unfused, uint64_t *unmade);
/* the check's verdicts: the classes in use (0: off for the run), its
   words; measured false with nothing in use: no verdict (every arm that
   did not pass was not checked: the mark removed, nothing written as
   failed; the check runs again at the next start) */
void rd__op_verdict(uint32_t classes, bool measured, const char *why);
/* the check's mark "trying" (synced; false: not written, not tried) */
bool rd__op_try_begin(void);
/* the tests' sabotage words: 0 none, 1 constant, 2 cut, 3 unmade, 4 sliver
   (arm C's picture changed on a code 36 frame today's drew nothing on),
   5 read (the first frame's reference read taken as failed, no loss) */
int rd__op_sabotage(void);
/* the tests' forced state (ICO_RD_ONEPASS=plain|full): no check */
bool rd__op_forced(void);
/* "not applicable here: ..." where the device has dual-source blending or
   the backend is not Vulkan (NULL where it applies) */
const char *rd__op_not_applicable_text(void);

/* rd_pipeline.c: the made pipelines of one-pass keys (none under the
   device's dynamic blend state: folded into the colour pass's own) */
uint32_t rd__pipeline_count_onepass(void);

/* rd_nodiscard_selftest.c: the clipper check's cases (its geometry, reused
   by the one-pass blend's check): their count, case k's corners on a
   frame w x h GS pixels (model space), its name */
int rd__nd_clip_case_count(void);
const char *rd__nd_clip_case(int k, uint32_t w, uint32_t h, float out[3][3]);
/* the texel centres case k should cover on SCENE t (the clipper check's
   estimate; code32 the whole triangle, else the one cut at GS Z 0) */
uint64_t rd__nd_clip_case_expected(int k, const struct RdTargetRec *t, bool code32);

/* The 10 s VU line's clause (port/platform/window_host.c, after the fast
   pixel build's), the block's n replays: "; one-pass blend: P pairs a
   picture in one pass (Q of code 36), T triangles (R refused a picture:
   ...)", "" while held or off from the start; the block's counts back to
   zero */
size_t rd_op_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("one-pass blend ..."): the run's counts */
int rd__op_stats_text(char *out, size_t cap);

#endif
