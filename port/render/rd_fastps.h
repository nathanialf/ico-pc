/* rd_fastps.h: the fast pixel build (issue 179; docs/port/PIPELINES.md,
 * "The fast pixel build").
 *
 * Why.  On a device without dual-source blending every VU pass draws
 * through vu_ps_nodual, one program for every DrawCB the VU draws give it:
 * the texture function, TEXA, the GS levels, the nearest filter, DATE, the
 * clip plane and the alpha test are tested on DrawCB words at every pixel.
 * The boat scene's captures (v0.11.14, stage 42) give 98.1 % and 99.6 % of
 * their lists 0-2 shaded fragments, and 98.5-100 % of list 5's, to one form
 * (handbacks SHADERCOST: textured, MODULATE with TCC, an RGBA32 texel, no GS
 * levels, no nearest, no DATE, no clip plane, no alpha test, no Full pixel
 * crop); vu_ps_nodual built with those words as constants executes 95 SPIR-V
 * instructions a fragment of that form against 142 (28 branches to 13; the
 * same float arithmetic).  What that buys on the issue 179 reporter's
 * Mali-G52 is not known (no GPU compiler's figures here): the build is timed
 * in play (rd_ab_run.h RD_AB_FASTPS) and kept only where it is faster.
 *
 * What.  vu_ps_nodual_fast is vu_ps built with ICO_NO_DUAL=1 ICO_VU_FAST=1
 * (port/shaders/CMakeLists.txt; common.hlsli "ICO_VU_FAST"): g_mode.x reads
 * TEXTURED and TCC set and DECAL, DATE, DATM, DATE_DST, PLANE, GS_LOD,
 * NEAREST and GS_BILINEAR_GRID clear (RD_FPS_FIXED_MASK, RD_FPS_FIXED) and
 * every other bit as the DrawCB has it (FBA, PABE, FIX, PREMUL, C1_DST and
 * the two-pass bits among them); g_mode.y, z and w read 0 and g_scale.z 0.
 * A pass takes it (key.gs.nodual's bit RD_NODUAL_FAST, with RD_NODUAL_ON)
 * only when its DrawCB holds exactly those values (rd_fps_eligible):
 *   - mode.x & RD_FPS_FIXED_MASK == RD_FPS_FIXED;
 *   - mode.y >> 8 == 0 (the texel RGBA32; TEXA's mode in the low byte is
 *     then never read, vu_common.hlsli vu_texa_expand_f);
 *   - mode.z bit 8 clear (the alpha test off: gs_alpha_discard returns
 *     false whatever the rest of mode.z and AREF hold, their one reader);
 *   - scale.z == 0 (no Full pixel crop: vu_uv's one read is "!= 0.0").
 * There every value the build reads is the DrawCB's own, so it writes what
 * vu_ps_nodual writes, pixel for pixel: the same statements on the same
 * values, the branches it never takes folded away by the compiler.  Proven
 * three ways: by construction (above), on the SPIR-V (an interpreter of
 * both builds over random fragments of the set, every output bit; the
 * hand-back), and on the device at the start (the self-test below).
 * Not discard-free: the side band's discard stays (its flag is an
 * interpolated input, vu_pixel_tests), so the fast build discards exactly
 * where vu_ps_nodual does, a NaN flag included, and no clip mode, program,
 * band or clipper condition is needed for it.  That discard is what keeps
 * its depth update late on a Mali (Arm's compiler: "Modifies coverage",
 * "Uses late ZS update").  It cannot be dropped for every pass of the set:
 * the flag is 0 at every corner of a draw without a side band, but a
 * clipper may make it NaN at a corner it adds behind the eye
 * (rd_nodiscard.h "Why clip mode none"), where the *_nodual build leaves a
 * hole and a discard-free build would draw.  So the discard-free form,
 * vu_ps_nodual_fast_nd (ICO_VU_FAST=1 ICO_VU_NODISCARD=1), is given only to
 * a pass the discard-free twins' own rule already gave vu_ps_nodual_nd
 * (RD_NODUAL_NODISCARD in its key: clip mode none, or the camera rule's
 * checked forms on a device whose clipper check measured clean, with no
 * plane, DATE, band or alpha test) and whose DrawCB is in the set: that
 * rule's proof is untouched, the fast form changes nothing it relies on
 * (the same statements as vu_ps_nodual_nd on the same values).  Every other
 * pass of the set takes vu_ps_nodual_fast.  vu_texa_ps's passes, the
 * planes' twins at a fractional scale (vu_np_ps), the exact blend's
 * destination reads and every device with dual-source blending keep their
 * builds (a dual-source fast build would need a key bit RdPipelineKey has
 * not: docs).
 * Its pipelines are made in the background between pictures (the twins'
 * queue, rd_pipeline.c rd__pipeline_twin): a pass whose fast pipeline is not
 * made yet draws through vu_ps_nodual, never waits for one.
 *
 * The ways back (each logged; none written to the settings):
 *   - the install's mark (port/platform/fastps_mark.h, logs/fastps-mark.txt:
 *     ICO_FASTPS_MARK, ICO_FASTPS_VERSION, ICO_FASTPS_BLOCK), the zero-area
 *     cull's stages: "trying" written and synced before the module is made,
 *     "counting" when the self-test passed, "on" after RD_FPS_PROBATION
 *     pictures drawn with it, "failed" when the module could not be made or
 *     the self-test refused it; a bad end while trying, counting or on keeps
 *     it off until the version changes;
 *   - the start-up self-test (rd_fastps_selftest.c, after the discard-free
 *     twins', on the VU pre-pass that passed): eleven code 34 meshes, the
 *     set's forms (opaque, the two-pass lerp's two passes, the additive and
 *     FIX premultiplied forms, FBA) and passes the CPU must refuse (DECAL,
 *     untextured twice, nearest, a kept alpha test, a clip plane), then a
 *     depth probe, drawn with the build and without it, with the twins held
 *     off (vu_ps_nodual_fast) and, where they are on for the run, held on
 *     (vu_ps_nodual_fast_nd): every pair byte for byte (a depth one wrote and
 *     the other not shown through the probe), at least six passes through
 *     each module tested, every refusal refused, no pipeline missing, the
 *     frame without its opaque case different; else off for the run, the
 *     modules and pipelines released, "failed"; a module whose pair was not
 *     drawn (the twins off) is not used in play ("module not tested");
 *   - the loss ladder (ICO_LOSS_ROW): off for this start;
 *   - a fast pipeline the background could not make: off for the run, the
 *     mark unchanged;
 *   - the judge times only replays whose every eligible pass had its fast
 *     pipeline (rd_fps_use_counts);
 *   - the in-play judge (rd_ab_run.h RD_AB_FASTPS): with its verdict "off"
 *     every pass draws through vu_ps_nodual.
 * Until the self-test passes nothing of it exists on the device.
 *
 * The tests' switches (docs/port/TESTING.md; no setting): ICO_RD_VU_FASTPS=0
 * off, =1 on without the self-test (the replay tool runs none), its
 * pipelines made at the next replay's start; ICO_RD_VU_FASTPS_SABOTAGE=decal
 * (the CPU's rule skips the texture function: a DECAL pass draws through the
 * build, which reads MODULATE) or =module (the module not made). */
#ifndef PORT_RENDER_RD_FASTPS_H
#define PORT_RENDER_RD_FASTPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

/* key.gs.nodual's bit for the fast build (with RD_NODUAL_ON, rd_nodiscard.h;
   never with RD_NODUAL_NODISCARD) */
#define RD_NODUAL_FAST 4u

/* the modules' names: the fast build, and its discard-free form */
#define RD_FPS_MODULE_NAME "vu_ps_nodual_fast"
#define RD_FPS_MODULE_NAME_ND "vu_ps_nodual_fast_nd"

/* the DrawCB mode.x bits the build holds as constants, and their values
   (ICO_DF_*: a file expanding them includes shader_consts.h)
   (common.hlsli VU_FAST_FIXED_MASK, VU_FAST_FIXED: rd_fastps_test reads the
   shader's own lines and compares) */
#define RD_FPS_FIXED_MASK                                                                          \
    ((uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_DECAL | (uint32_t)ICO_DF_TCC_RGBA |              \
     (uint32_t)ICO_DF_DATE | (uint32_t)ICO_DF_DATM | (uint32_t)ICO_DF_PLANE |                      \
     (uint32_t)ICO_DF_GS_LOD | (uint32_t)ICO_DF_NEAREST | (uint32_t)ICO_DF_GS_BILINEAR_GRID |      \
     (uint32_t)ICO_DF_DATE_DST)
#define RD_FPS_FIXED ((uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA)

/* the pictures drawn with it after the self-test before the mark says on */
#define RD_FPS_PROBATION 250u

/* a pass's verdict (rd_fps_eligible): the fast build, or why not */
enum {
    RD_FPS_FAST = 0, /* through the fast build */
    RD_FPS_TEXTURE,  /* untextured, DECAL or TCC off (mode.x's texture bits) */
    RD_FPS_TEXEL,    /* not an RGBA32 texel (mode.y >> 8) */
    RD_FPS_LEVELS,   /* GS levels, the GS levels' grid or the nearest filter */
    RD_FPS_DATE,     /* DATE */
    RD_FPS_PLANE,    /* a clip plane */
    RD_FPS_ALPHA,    /* the alpha test in the pass */
    RD_FPS_CROP,     /* a Full pixel crop (scale.z) */
    RD_FPS_UNMADE,   /* its pipeline not made yet (asked for in the background) */
    RD_FPS_TRIAL,    /* the in-play judge's off arm */
    RD_FPS_UNTESTED, /* its module drew nothing in the start-up self-test */
    RD_FPS_REASONS
};

/* ------------------------------------------------ the pure part (tests) */

/* a pass of a VU draw at its draw: its key's words and its DrawCB */
typedef struct RdFpsPass {
    uint8_t fs;     /* the key's fragment entry (RdFsId) */
    uint8_t nodual; /* key.gs.nodual */
    uint8_t dst;    /* key.dst (RhiDstRead) */
    uint32_t mode[4];
    float scaleZ;   /* DrawCB scale[2] */
    bool skipDecal; /* the tests' sabotage: the texture function not looked at */
} RdFpsPass;

/* -1: not a pass the build exists for (not vu_ps's *_nodual build alone: a
   discard-free twin, vu_texa_ps, vu_np_ps, a destination read, dual-source
   blending); else RD_FPS_FAST or the first reason that applies, in the
   enum's order */
int rd_fps_eligible(const RdFpsPass *p);
/* a reason's words ("texture", "texel", "levels", "DATE", "plane",
   "alpha", "crop", "not made yet", "timed without it", "module not tested") */
const char *rd_fps_reason_name(int why);

/* the in-play judge's use of a replay, as the twins' (rd_nodiscard.h
   RdNdUse): in use once a pass drew through it (or would have at the off
   arm), never once a pass found its pipeline not made yet */
typedef struct RdFpsUse {
    bool fast, unmade;
} RdFpsUse;

void rd_fps_use_note(RdFpsUse *u, int verdict);
bool rd_fps_use_counts(const RdFpsUse *u);

/* ------------------------------------------------ the renderer's part */

struct RdPipeKeyInt;
struct RdTargetRec;
struct IcoDrawCB;

/* rd__vu_prepass_init: the environment, the loss ladder, the install's
   mark, the start state (off, or held until the self-test) */
void rd__fps_init(void);
/* rd__vu_prepass_shutdown, with the device: the module destroyed */
void rd__fps_shutdown(void);
/* the module of a fast key (rd_pipeline.c pipelineDesc): nd its
   discard-free form; id 0 when not made */
RhiShader rd__fps_module(bool nd);
/* a replay's start (rd__vu_prepass): the last replay's pictures counted
   for the probation; under ICO_RD_VU_FASTPS=1 the pipelines asked for in
   the replays before made now */
void rd__fps_replay_begin(void);
/* vuDraw, a pass at its draw (its DrawCB filled; tris the draw's
   triangles): the pass takes the fast build's pipeline where it may and
   that pipeline is made (key and pipe changed), else nothing changes */
void rd__fps_pass(struct RdPipeKeyInt *key, RhiPipeline *pipe, const struct IcoDrawCB *cb,
                  uint32_t tris);
/* rd_pipeline.c: a fast pipeline the background could not make (off for
   the run) */
void rd__fps_fault(const char *what);
/* the self-test's frames: the build asked for (on) or not (off);
   rd__fps_hold_end puts the run's state back */
void rd__fps_hold(bool on);
void rd__fps_hold_end(void);
/* the build tried: the mark "trying" (synced), then the module made */
bool rd__fps_try_begin(void);
/* the self-test passed (on for the run) or failed (off, why) */
void rd__fps_verdict(bool pass, const char *why);
/* the start-up self-test (rd_fastps_selftest.c) on SCENE t, after the
   discard-free twins'; its line, then the start line */
void rd__fps_selftest(struct RdTargetRec *t);
/* no self-test (the pre-pass off or not tested): its line and the start
   line, the build off */
void rd__fps_selftest_skip(const char *why);
/* "fast pixel build: on|held|off, N modules of it made, P of its pipelines
   alive" */
void rd__fps_start_line(void);
/* the build may be put on trial in play: on, the module made, not the
   self-test's frames */
bool rd__fps_triable(void);
bool rd__fps_on(void);
const char *rd__fps_state_text(void);
/* the "not applicable here: ..." words where the device has dual-source
   blending (NULL where it applies, or without a device) */
const char *rd__fps_not_applicable_text(void);
/* the self-test's counts since the last call: draws through the build,
   the refusals by reason; the draws through each module (0 the fast
   build, 1 its discard-free form) */
void rd__fps_take(uint64_t *fast, uint64_t refused[RD_FPS_REASONS]);
void rd__fps_take_entries(uint64_t out[2]);
/* a module tested by the self-test (only a tested module is used in play) */
void rd__fps_entry_tested(int entry, bool tested);
/* rd_pipeline.c: every made pipeline of a fast key destroyed (the twins'
   queue emptied, the GPU idle), the count; the made pipelines of fast keys */
uint32_t rd__pipeline_drop_fast(void);
uint32_t rd__pipeline_count_fast(void);
/* the tests' sabotage =decal */
bool rd__fps_sabotage_decal(void);

/* The 10 s VU line's clause (port/platform/window_host.c, after the twins'),
   the block's n replays: "; fast pixel build: P passes a picture through
   it, T triangles (R refused a picture: texture t, ...)", "" while held or
   off from the
   start; the block's counts back to zero */
size_t rd_fps_text(char *out, size_t cap, unsigned n);
/* rd_replay_tool --stats ("fast pixel build ..."): the run's counts */
int rd__fps_stats_text(char *out, size_t cap);

#endif
