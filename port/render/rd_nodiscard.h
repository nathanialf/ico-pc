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
 * faster.  The boat scene's capture (v0.11.14, stage 42) has about 90 % of
 * its lists 0-2 triangles in passes none of those discards can fire for
 * (build-host's step-0 count).
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
 *   - on Mesa's software rasteriser (llvmpipe, lavapipe: the tests' and
 *     the replay tool's device) clip mode none alone: its clipper can make
 *     every varying NaN at the corners it adds to a triangle behind the
 *     eye, and the *_nodual build takes a NaN side band flag as a band
 *     (vu_common.hlsli vu_pixel_tests), discarding those pixels in the
 *     picture where the twin would draw them; a clip mode none corner is
 *     always at its GS position (w 1), so no corner is ever added behind
 *     the eye there.  A GPU whose interpolation keeps a constant has no
 *     such NaN.
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
 *     self-test passed, "on" after RD_ND_PROBATION pictures drawn with a
 *     twin, "failed" when a module or a pipeline could not be made or the
 *     self-test refused them; a bad end while trying, counting or on (a
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
 *   - a twin pipeline the background could not make: off for the run;
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
    RD_ND_CLIPPED,  /* Mesa's software rasteriser: not clip mode none */
    RD_ND_UNMADE,   /* its twin's pipeline not made yet (asked for in the background) */
    RD_ND_TRIAL,    /* the in-play judge's off arm */
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
    bool softClip;      /* Mesa's software rasteriser */
    bool skipAlpha;     /* the tests' sabotage: the alpha test not looked at */
} RdNdPass;

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
/* Mesa's software rasteriser by the device's own words: Vulkan driver id 13
   (VK_DRIVER_ID_MESA_LLVMPIPE) or "llvmpipe" in the adapter's name (OpenGL's
   GL_RENDERER, lavapipe's device name) */
bool rd_nd_soft_raster(uint32_t driverId, const char *adapter);

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
/* the tests' sabotage =alpha */
bool rd__nd_sabotage_alpha(void);

/* The 10 s VU line's clause (port/platform/window_host.c, after the
   zero-area cull's), the block's n replays: "; discard-free twins: D draws
   a picture through them (R refused: plane p, DATE d, band b, alpha a,
   clipped c, not made yet u, timed without them t)", "" while held or off
   (the start lines say why); the block's counts back to zero */
size_t rd_nd_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("discard-free twins ..."): the run's counts */
int rd__nd_stats_text(char *out, size_t cap);

#endif
