/* rd_nodiscard.h: the discard-free twins (issue 179; docs/port/PIPELINES.md,
 * "The discard-free twins").
 *
 * Why.  On a device without dual-source blending every VU pass draws
 * through vu_ps_nodual or vu_texa_ps_nodual (rd_pipeline.c
 * rd__expand_no_dual), and both hold discard: the clip plane's, the side
 * bands', DATE's and the alpha test's (port/shaders/vu_common.hlsli
 * vu_pixel_tests, vu_pixel, vu_pixel_texa).  Arm's published guidance for
 * its GPUs says a fragment shader that may discard updates the depth late
 * and loses the hidden-pixel kill; that it costs anything on the issue 179
 * reporter's Mali-G52 is not known (nothing here could measure it), so the
 * twins are timed in play (rd_ab_run.h) and kept only where they are
 * faster.  Under the rule below, the boat scene's captures (v0.11.14,
 * stage 42) have 2 % and 6 % of their lists 0-2 triangles in passes a twin
 * may serve (clip mode none alone; the camera rule's code 32, 36 and 24
 * draws, which put corners behind the eye, are refused).
 *
 * What.  The twins vu_ps_nodual_nd and vu_texa_ps_nodual_nd are the same
 * entries built with ICO_VU_NODISCARD=1: no discard at all, every other
 * statement the same (port/shaders/CMakeLists.txt).  A pass takes its
 * twin (key.gs.nodual's bit RD_NODUAL_NODISCARD, rd_state.h) only when the
 * CPU proves at the draw's key that no discard of the *_nodual build can
 * fire there (rd_nd_eligible, refusing when unsure):
 *   - no clip plane (the draw's chain plane, ICO_DF_PLANE);
 *   - no DATE (the snapshot's ICO_DF_DATE, nor the stencil bit's);
 *   - no side band: the draw carries ICO_VU_NO_BESIDE (the camera rule
 *     gives every mesh, skinned and grid draw the flag, rd_replay_vu.c
 *     vuSource; under [dev] region_rule = ps2 on a frame wider than 4:3
 *     almost no draw has it, and those are refused), is clip mode none
 *     (code 34: no region test, so no band) or particles (never the rule);
 *   - the alpha test off in the pass (DrawCB mode.z bit 8 clear: the test
 *     off in the state, ATST ALWAYS, the AFAIL merge's one draw, or the
 *     alpha plan's proof for the draw's As range, rd_alpha_plan.c);
 *   - clip mode none (code 34), on every device, not particles; another
 *     clip mode only where this device's clipper check (below) came out
 *     clean.
 * Why clip mode none (issue 179 round 2).  The *_nodual build discards a
 * pixel in the picture's columns whose interpolated band flag is not below
 * 0.5, a NaN included (vu_common.hlsli vu_pixel_tests; commit e65012742,
 * for Mesa's clipper).  The vertex stage writes the flag as the literal 0.0
 * or 1.0 at every corner (vu_tri.hlsl vu_main_vs "o.plane.y = beside ...",
 * vu_common.hlsli vu_mesh_out, vu_out_init), so it is finite where it is
 * written: a non-finite flag at a pixel can only come from the clipper's
 * arithmetic at a corner it adds.  Mesa's (llvmpipe, lavapipe) takes a
 * noperspective varying's factor from the corners' divided x or y, not
 * finite where those coincide, which a corner behind the eye makes
 * possible; what another GPU's clipper does across w = 0 is not known here
 * and is not assumed.  A clip mode none corner is always a GS position:
 * w = 1, a depth in 0..1 (vu_tri.hlsl vu_tri_verdict: at 1, or 3 for the
 * wrap; vu_common.hlsli vu_corner_position, vu_vtx_position,
 * vu_gs_position; gs_math.hlsli gs_z_to_depth clamps), so no clip against
 * the depth range and every corner equal in w: a clipper's factor for an
 * edge crossing a side of the frame is a finite fraction whatever form it
 * takes, and a flag 0 at every corner stays 0.  Every other clip mode can
 * hand the GPU homogeneous corners at or behind the eye (the camera rule's
 * clipped drops, code 36's flagged corners): refused as "clipped" unless
 * the device's clipper check came out clean.  So clip mode none holds on
 * every device; the tests' runs without the self-test (rd_nodiscard_ab: no
 * clipper check) take the path of a phone whose check was not clean,
 * rd_nodiscard_clean the path of one whose check was clean.
 * The clipper check (issue 179 round 3: measure, never assume).  After the
 * code 34 cases pass, the self-test draws 13 camera rule cases whose
 * triangles cross the eye plane (rd_nodiscard_selftest.c kClip: an edge
 * nearly on the centre view ray, on view rays with the divided x alike and
 * the divided y alike, nearly on an off-centre one, a corner at w = 0, just
 * behind (w = -0.001) and far behind (w = -1290, the dock's) the eye, two
 * corners behind, every w above 0 clipped at GS Z 0 with and without an
 * edge nearly on a view ray, a corner saturated in X, a sliver, a large
 * triangle across the frame), each at code 32 and code 36, untextured and
 * textured, both windings, each a frame of its own with a depth probe after
 * it, through the twins and without them, the pictures byte for byte.  The
 * corners are sized to the frame at run time (a corner by its screen
 * direction as a fraction of the frame's half-size and its depth,
 * caseCorners).  A triangle with an edge on a view ray has its plane
 * through the eye, so it draws nothing on any device; one with an edge
 * near one draws a sliver as wide as the offset, a quarter of the
 * half-size here.  Every frame runs, whatever an earlier one found, and
 * each case logs one line: the texels it should cover (expectedTexels: the
 * vertex stage's clip at GS Z 0 and projection, an estimate), the texels
 * each of its four frames drew through the ordinary entry against the
 * probe alone, the texels code 32 drew without the probe against the clear
 * (whether the probe hid it), and the texels each frame differs by with
 * the twins.  A frame that drew nothing tests nothing: "not clean (drew
 * nothing (not tested): case N at 32, 32t, ...)", every such frame named;
 * only a frame whose case drew counts its passes, and each twin entry must
 * have drawn a case's own pass (the depth probe, code 34, does not count).
 * All drawn and the same: "clean", and the camera rule's passes take a
 * twin too (counted apart: "of them by the clipper check").  Any
 * difference: "not clean", the first differing frame named, clip mode none
 * alone; never "failed" in the mark (a property of the device), the verdict
 * kept on the mark's second line with the version and logged at the next
 * start.  Evidence, not a proof: it covers the clipper's branches (the w and
 * near-depth planes, the far plane, the side planes through saturation, one
 * and two corners out, the factor's 0 / 0) for these corners only; geometry
 * the cases miss, other states (MSAA, other targets), a driver's size
 * thresholds and anything time-dependent are not covered.  What is known
 * (2026-11-22): lavapipe is not clean.  Its clipper makes the band flag
 * NaN on triangles crossing the eye plane without any side band: cases 2
 * (an edge on a view ray, x / w alike) and 5 (a corner at w = 0) lose
 * texels through the ordinary entry (case 5 all of them) that the twins
 * draw.  At such a texel the twin's other noperspective varyings (col,
 * stq) come from the same factor and are NaN too: a triangle with a
 * corner behind the eye has no attribute planes (vu_pixel_attrs takes the
 * interpolated ones), so the twin writes a colour made from NaN (0 under
 * Vulkan's NaN-to-UNORM conversion, or what a clamp makes of it) at the
 * triangle's depth (SV_Position is clipped by the plane distances, not that
 * factor): garbage, not the triangle's picture, where the ordinary entry
 * leaves a hole.  rd_nodiscard_clean's four fixtures hold no such triangle
 * (0 targets differing with the verdict taken as clean).  History: the
 * check measured "clean" until the second review's run 3 because no case
 * had drawn a triangle crossing the eye plane (see below).
 * ICO_RD_VU_NODISCARD_CLIPPER=clean takes the verdict as clean for the
 * tests (rd_vuprepass_selftest_nodiscard_clean, rd_nodiscard_clean); ICO_RD_VU_NODISCARD_SABOTAGE=clipper changes the check's first
 * case's picture through the twins in one byte, =empty moves its first
 * case off the frame (its positive controls: the check must say not
 * clean).  The "clean" lavapipe measured on 2026-11-22 was an earlier case
 * set in model units, five of whose cases covered no pixel; the next set's
 * first case, an edge 0.25 model units off the centre view ray, drew
 * nothing on lavapipe at 912 x 512 where the CPU expected about 47 texels.
 * The cause was the check's set-up: its prelit meshes had no facing table,
 * so their draws kept the PS2's drops (rd_replay_vu.c vuDropMode) and the
 * region rule dropped every triangle with a corner behind the eye that
 * reaches the picture (vu_region_rule_cut) before any clipper; its code 36
 * frames ran the region test.  Now each mesh has a table and code 36 the
 * scissor; ICO_RD_VU_NODISCARD_TRACE=1 logs each case's stages.  A frame
 * is "not tested" only when neither entry drew it (the ordinary entry can
 * lose a whole case to the NaN).  Code 32's expectation is uncut where
 * every corner passes the region test (it does not test Z: EXACT, as the
 * PS2 draws it), code 36's cut at GS Z 0 (the scissor flags it).
 * The twins' pipelines are made in the background between pictures
 * (rd_pipeline.c rd__pipeline_twin, rd__pipeline_twin_tick: a batch on the
 * workers at low priority, as the start-up set's background finisher, or
 * between pictures on a backend without background creation); a pass whose
 * twin's pipeline is not made yet draws through the *_nodual build, never
 * waits for one.
 *
 * The ways back (each logged; none written to the settings):
 *   - the install's mark (port/platform/nodiscard_mark.h,
 *     logs/nodiscard-mark.txt: ICO_NODISCARD_MARK, ICO_NODISCARD_VERSION,
 *     ICO_NODISCARD_BLOCK), the zero-area cull's stages: "trying" written
 *     and synced before the two modules are made, "counting" when the
 *     self-test's floor cases passed (before the clipper check, which never
 *     writes "failed"), "on" after RD_ND_PROBATION pictures drawn with a
 *     twin, "failed" when a module could not be made or the self-test
 *     refused them (a background pipeline that failed in play turns them
 *     off for the run only: no write on the render thread); a bad end while trying, counting or on (a
 *     crash, an ANR, a signal other than 9, a device loss, a GPU hang, the
 *     watchdog; and while trying any end but a clean close) keeps them off
 *     until the version changes;
 *   - the start-up self-test (rd_nodiscard_selftest.c, after the zero-area
 *     cull's, on the VU pre-pass that passed): a frame with passes the CPU
 *     must give the twins and passes it must refuse (a failing alpha test,
 *     a clip plane; a side band checked on the rule, the camera rule giving
 *     no draw one), drawn with the twins and without them: the pictures
 *     byte for byte, the twins' draws counted, every refusal case refused,
 *     no twin pipeline missing; else off for the run, the modules and
 *     pipelines released;
 *   - the loss ladder (ICO_LOSS_ROW: a start after one that lost the
 *     device or hung its driver): off for this start;
 *   - a twin pipeline the background could not make: off for the run,
 *     the mark unchanged;
 *   - per entry: a twin entry whose start-up case drew nothing through it
 *     is not used in play ("entry not tested");
 *   - the judge times only replays whose every eligible pass had its twin
 *     (rd_nd_use_counts: one found "not made yet" takes the replay out), so
 *     no verdict rests on a partial share;
 *   - the in-play judge (rd_ab_run.h RD_AB_NODISCARD): a stage judged
 *     without them draws through the *_nodual builds.
 * Until the self-test passes nothing of them exists on the device: with
 * them off every shader and pipeline is the build's without them.
 *
 * The tests' switches (docs/port/TESTING.md; no setting):
 * ICO_RD_VU_NODISCARD=0 off, =1 on without the self-test (the replay tool
 * runs none), the twins' pipelines taken at the next replay's start;
 * ICO_RD_VU_NODISCARD_SABOTAGE=alpha (the CPU's proof skips the alpha test:
 * a twin draws where the build discards) or =module (the modules not
 * made). */
#ifndef PORT_RENDER_RD_NODISCARD_H
#define PORT_RENDER_RD_NODISCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

/* key.gs.nodual's bits (rd_state.h RdPipelineKey.nodual): the *_nodual
   entry, and its discard-free twin (with the first) */
#define RD_NODUAL_ON 1u
#define RD_NODUAL_NODISCARD 2u

/* the pictures drawn with a twin after the self-test before the mark says
   on (the zero-area cull's 250: 10 s at 25 a second) */
#define RD_ND_PROBATION 250u

/* a pass's verdict (rd_nd_eligible): the twin, or why not (counted a pass,
   the 10 s line) */
enum {
    RD_ND_TWIN = 0, /* through the twin */
    RD_ND_PLANE,    /* a clip plane */
    RD_ND_DATE,     /* DATE (the snapshot or the stencil bit) */
    RD_ND_BAND,     /* a side band possible */
    RD_ND_ALPHA,    /* the alpha test in the pass */
    RD_ND_CLIPPED,  /* not clip mode none, or particles: a corner may be behind the eye */
    RD_ND_UNMADE,   /* its twin's pipeline not made yet (asked for in the background) */
    RD_ND_TRIAL,    /* the in-play judge's off arm */
    RD_ND_UNTESTED, /* its entry's twin drew nothing in the start-up self-test */
    RD_ND_REASONS
};

/* ------------------------------------------------ the pure part (tests) */

/* a planned pass of a VU draw, as rd_replay_vu.c vuDraw has it */
typedef struct RdNdPass {
    uint8_t fs;         /* the pass's fragment entry (RdFsId) */
    uint8_t nodual;     /* key.gs.nodual */
    uint8_t dst;        /* key.dst (RhiDstRead) */
    uint32_t flags;     /* RdDrawPass.flags (ICO_DF_*) */
    uint32_t modeZ;     /* RdDrawPass.modeZ (atst | ate << 8 | split << 16) */
    uint32_t drawFlags; /* IcoVuCB draw[2] (the clip mode, ICO_VU_NO_BESIDE) */
    bool plane;         /* IcoVuCB batch[2] != 0: a chain plane */
    bool particle;      /* a particles command */
    bool dateStencil;   /* DATE through the stencil bit for this draw (Replay.dateStNow) */
    bool skipAlpha;     /* the tests' sabotage: the alpha test not looked at */
    bool clipperClean;  /* the device's clipper check passed (RD_ND_CLIP_CLEAN), or its frames */
} RdNdPass;

/* the device's clipper check (rd_nodiscard_selftest.c, "The clipper check") */
enum { RD_ND_CLIP_UNKNOWN = 0, RD_ND_CLIP_CLEAN, RD_ND_CLIP_DIRTY };

/* -1: not a pass a twin exists for (not vu_ps / vu_texa_ps, no *_nodual
   build, a destination read); else RD_ND_TWIN or the first reason that
   applies, in the enum's order */
int rd_nd_eligible(const RdNdPass *p);
/* a reason's words ("plane", "DATE", "band", "alpha", "clipped", "not made
   yet", "timed without them") */
const char *rd_nd_reason_name(int why);
/* the twin's module name of fragment entry fs ("vu_ps_nodual_nd"), NULL
   for an entry without one */
const char *rd_nd_module_name(uint8_t fs);

/* ------------------------------------------------ the renderer's part */

struct RdPipeKeyInt;
struct RdDrawPass;
struct RdTargetRec;

/* rd__vu_prepass_init: the environment, the loss ladder, the install's
   mark, the start state (off, or held until the self-test) */
void rd__nd_init(void);
/* rd__vu_prepass_shutdown, with the device: the modules destroyed */
void rd__nd_shutdown(void);
/* the twin module of fragment entry fs (rd_pipeline.c pipelineDesc): id 0
   when not made */
RhiShader rd__nd_module(uint8_t fs);
/* a replay's start (rd__vu_prepass): the last replay's pictures counted
   for the probation; under ICO_RD_VU_NODISCARD=1 the twins' pipelines asked
   for in the replays before made now */
void rd__nd_replay_begin(void);
/* vuDraw: a planned pass of a VU draw (key and pipe as the draw has them)
   takes its twin where it may and the twin's pipeline is made */
void rd__nd_pass(struct RdPipeKeyInt *key, RhiPipeline *pipe, const struct RdDrawPass *p,
                 uint32_t drawFlags, bool plane, bool particle, bool dateStencil);
/* rd_pipeline.c: a twin pipeline the background could not make (off for
   the run) */
void rd__nd_fault(const char *what);
/* rd_pipeline.c, the twins' pipelines: a twin key's pipeline when made
   (the cache's), else {0}, the key asked for (gathered while the self-test
   gathers, else queued for the background: never made here) */
RhiPipeline rd__pipeline_twin(const struct RdPipeKeyInt *k);
/* the queue's next step (between pictures: rd__pipe_finish_tick; a
   replay's start under the tests' =1 with wait): a batch in flight taken
   once its compiles ended (wait: at once), else the next one begun on the
   workers at low priority (a backend without background creation: made
   here) */
void rd__pipeline_twin_tick(bool wait);
/* every made pipeline of a twin destroyed and the queue emptied (the
   GPU idle); the count */
uint32_t rd__pipeline_drop_twins(void);
/* the made pipelines of the twins; those made in the background so far */
uint32_t rd__pipeline_count_twins(void);
uint32_t rd__pipeline_twins_background(void);
/* the twins' background set since the last call (the 10 s line): pipelines
   made and failed, batches, their wall time from begin to taken */
void rd__pipeline_twins_take(uint32_t *made, uint32_t *failed, uint32_t *batches, double *ms);
/* the cache's entries now (a mark), and every entry made after a mark
   destroyed and forgotten, as if never asked for (the GPU idle; the self-
   test's own pipelines, so the run's set is the one it had before it); the
   count */
uint32_t rd__pipeline_mark(void);
uint32_t rd__pipeline_forget_since(uint32_t mark);
/* the self-test's frames: the twins asked for (on) or not (off);
   rd__nd_hold_end puts the run's state back */
void rd__nd_hold(bool on);
void rd__nd_hold_end(void);
/* the twins tried: the mark "trying" (synced), then both modules made;
   false when they could not be (or are off) */
bool rd__nd_try_begin(void);
/* the self-test passed (on for the run) or failed (off, why) */
void rd__nd_verdict(bool pass, const char *why);
/* the start-up self-test (rd_nodiscard_selftest.c) on SCENE t, after the
   zero-area cull's; its line, then the start line */
void rd__nd_selftest(struct RdTargetRec *t);
/* no self-test (the pre-pass off or not tested): its line and the start
   line, the twins off */
void rd__nd_selftest_skip(const char *why);
/* "discard-free twins: on|held|off, N modules of them made, P of their
   pipelines alive" (the tests read the counts) */
void rd__nd_start_line(void);
/* the twins may be put on trial in play (rd_ab_run.h): on, both modules
   made, not the self-test's frames */
bool rd__nd_triable(void);
/* on for the run now; the state's words; why (off, held) */
bool rd__nd_on(void);
const char *rd__nd_state_text(void);
/* the self-test's counts since the last call: draws through a twin, and
   the refusals by reason */
void rd__nd_take(uint64_t *twin, uint64_t refused[RD_ND_REASONS]);
/* the self-test's draws through each twin entry since the last call (0
   vu_ps_nodual_nd, 1 vu_texa_ps_nodual_nd), and an entry marked tested
   (only a tested entry's twin is used in play) */
void rd__nd_take_entries(uint64_t out[2]);
/* the twins apply on this device: no dual-source blending by its own
   features (not merely the two-pass fallback the blend's self-test chose);
   else the "not applicable here: ..." words (NULL where they apply, or
   without a device) */
bool rd__nd_applicable(void);

/* The clipper check's trace (its tests' ICO_RD_VU_NODISCARD_TRACE=1,
   rd_replay_vu.c): the first VU draw of a replay as the replay set it up:
   the draw word (ICO_VU_CAMERA, ICO_VU_NO_BESIDE, ICO_VU_MIRROR, ...), the
   drops' mode (0 kept, the PS2's drop; 1 the side bands; 2 the camera
   rule; 3 the camera rule two-sided), why the mesh has no facing (0 it
   has, else RD_VU_NOFACE_* + 1), the code, the clip mode and the mesh's
   facingFrom */
typedef struct RdVuTraceDraw {
    uint32_t word;
    int drops, noFacing, code, clip, facingFrom;
} RdVuTraceDraw;

void rd__vu_trace(bool on);
bool rd__vu_trace_take(RdVuTraceDraw *out);
const char *rd__nd_not_applicable_text(void);
/* the same for the draws of a clip mode other than none only (the clipper
   check's class: its depth probe, a code 34 pass, not among them) */
void rd__nd_take_entries_clip(uint64_t out[2]);
void rd__nd_entry_tested(int entry, bool tested);
/* the floor's cases passed: the mark says "counting" from here (the
   clipper check after it is a device property, like play) */
void rd__nd_floor_passed(void);

/* the in-play judge's use of a replay (pure): in use once a pass drew
   through a twin, never once a pass of it found its twin not made yet (so
   a trial times only replays with the stage's whole twin share) */
typedef struct RdNdUse {
    bool twin, unmade;
} RdNdUse;

void rd_nd_use_note(RdNdUse *u, int verdict);
bool rd_nd_use_counts(const RdNdUse *u);
/* the tests' sabotage =alpha, =clipper (the clipper check's first case's
   picture through the twins changed in one byte: its positive control),
   =empty (the clipper check's first case moved off the frame: a case that
   draws nothing must make the verdict not clean) */
bool rd__nd_sabotage_alpha(void);
bool rd__nd_sabotage_clipper(void);
bool rd__nd_sabotage_empty(void);
/* the self-test's draws through a twin of a clip mode other than none since
   the last call */
uint64_t rd__nd_take_clip(void);
/* the clipper check's frames running (their camera rule passes taken as a
   clean clipper's would be), and its verdict (clean, or the first differing
   case's words; the tests' ICO_RD_VU_NODISCARD_CLIPPER=clean replaces
   it, saying what was measured); the verdict (RD_ND_CLIP_*) and its words */
void rd__nd_clipper_check(bool on);
void rd__nd_clipper_verdict(bool clean, const char *why);
int rd__nd_clipper(void);
const char *rd__nd_clipper_text(void);

/* The 10 s VU line's clause (port/platform/window_host.c, after the
   zero-area cull's), the block's n replays: "; discard-free twins: D draws
   a picture through them (R refused: plane p, DATE d, band b, alpha a,
   clipped c, not made yet u, timed without them t)", "" while held or off
   (the start lines say why); the block's counts back to zero */
size_t rd_nd_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("discard-free twins ..."): the run's counts */
int rd__nd_stats_text(char *out, size_t cap);

#endif
