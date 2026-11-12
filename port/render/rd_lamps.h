/* rd_lamps.h: the lamps' glow frames for less (port/render/rd_lamps.c;
 * docs/port/RENDER_POST.md, "The lamps' glow for less"; docs/port/WORK_BUFFERS.md,
 * "WORK2 under the lamps' area").
 *
 * In a frame whose glow light plan is LAMPS (rd_glow_light.c: WORK2's fill
 * drawn as (0, 0, 0, 0), the mask not drawn, the lamps' meshes drawn into
 * WORK2 under SCENE's depth), two parts, each its own bit of the switch:
 *   RD_LAMPS_AREA  the render pass that draws the lamps into WORK2 (2366 x
 *                  1080 on the PowerVR phone, SCENE's depth loaded for their
 *                  Z test) loads, clears and stores only the 32 x 32 tiles
 *                  of the lamps' screen box and of the region an earlier
 *                  lamps frame left lit, the rest of WORK2 holding (0, 0, 0,
 *                  0) already (the fill's colour);
 *   RD_LAMPS_PAD   WORK2_PAD's band (fillWork2's 256 x 64 sprite, read by
 *                  nothing) left a lazy clear as the DARK plan leaves it,
 *                  not a pass of its own.
 * Both the same bytes in every target.  AREA on by default on the PowerVR
 * class and a desktop class (rd__profile_tile_class 1 or 2, not a
 * tile-based phone) under Vulkan where the profile's pass area is on (the
 * tile-based GPUs': on the PowerVR phone), once its start-up self-test
 * passed (rd__lamps_selftest: a CLEAR over a render area leaves the rest of
 * the attachment as stored); off on the Arm, Qualcomm and Apple classes and
 * other tile-based phones (issues 123, 179, 211), on OpenGL, D3D11 and D3D12
 * (they ignore a render area).  PAD on every class and backend (the DARK
 * plan's lazy clears run there already).  ICO_RD_LAMPS=0, 1, or the words
 * area, pad, owed, wholepad, fold4 joined by ',' or '+' (the tests only,
 * docs/port/TESTING.md; no config key): off, every part, the parts named
 * (forced on any class; the self-tests still decide AREA and FOLD4).
 * ICO_RD_LAMPS_POISON=1 (the tests): before every planned lamps pass drawn
 * over the whole of WORK2 its colour is cleared to magenta first, so a pass
 * that relied on WORK2's earlier contents where it should not would show
 * the magenta, and before every pass of two blur pairs WORK0's swapped-in
 * colour, so a texel that pass does not write would (rd_lamps_ab). */
#ifndef PORT_RENDER_RD_LAMPS_H
#define PORT_RENDER_RD_LAMPS_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RD_LAMPS_AREA 1u
#define RD_LAMPS_PAD 2u
/* round 3 (docs/port/RENDER_POST.md, "The lamps' glow for less"):
 *   RD_LAMPS_OWED      under the DARK plan WORK0's alpha sprite (pasteWork0ToFB,
 *                      the clear (0, 0, 0, A) there) left a lazy clear where no
 *                      later command of the frame touches WORK0, not a pass
 *   RD_LAMPS_WHOLEPAD  WORK2_PAD's band a lazy clear under the WHOLE plan too
 *   RD_LAMPS_FOLD4     two staticBlur pairs (four passes) in one WORK0 pass
 *                      (fx_blur4_ps), behind its start-up self-test */
#define RD_LAMPS_OWED 4u
#define RD_LAMPS_WHOLEPAD 8u
#define RD_LAMPS_FOLD4 16u
/* Round 4 (owner's choice B: identical to the eye, not byte-identical,
 * DIVERGENCES.md VD-32): above 1x WORK2 at the GS frame's size, the glow's
 * fill, the sun's fans, the mask and the lamps drawn into it at the PS2's
 * own resolution, the lamps' and the fans' depth WORK2's own (SCENE's
 * depth taken down to that size, depth_down_ps).  Not in RD_LAMPS_ALL (not
 * a byte-identical item): only the word lowres asks for it, read once
 * (rd__lamps_lowres_on: WORK2's size is fixed when it is made). */
#define RD_LAMPS_LOWRES 32u
/* Under LOWRES above 1x the lamps' meshes write WORK2's own depth, never
 * SCENE's, so the late pass (rd_late_pass.c) may hold the crack fill past
 * them (the fill reads SCENE's depth and writes none): the same bytes as
 * the fill drawn where it was due, given LOWRES.  By its word alone (off by
 * default: rd__lamps_default_bits) */
#define RD_LAMPS_FILLHOLD 64u
/* LOWRES's reduction WORK2 -> WORK0: by default the PS2's own GS bilinear
 * (WORK2 at the GS size is the original's WORK2, and so is its reduction:
 * the original's glow); the word lowresbox (which implies lowres) the
 * above-1x box over each WORK0 texel's GS footprint instead (the scaled
 * WORK2's kernel), for the measurement */
#define RD_LAMPS_LOWRESBOX 128u
/* LOWRES's depth (depth_down_ps, rd_lamps.c lowresFill): where a GS pixel's
 * sample point is a SCENE texel's own centre (a whole-number scale and crop,
 * no jitter) that texel's depth, the original's own test; elsewhere by
 * default SCENE's depth interpolated bilinearly at the point.  For the
 * measurement, each implying lowres: lowresbias the slope bias there instead,
 * lowreshy bilinear where the texels around lie on one plane and the
 * nearest texel's elsewhere, lowres4 the farthest of the four texels around
 * the point everywhere, lowresp the nearest texel everywhere */
#define RD_LAMPS_LOWRESBIAS 256u
#define RD_LAMPS_LOWRES4 512u
#define RD_LAMPS_LOWRESP 1024u
#define RD_LAMPS_LOWRESHY 2048u
/* lowres2x (implies lowres and the box): WORK2 at twice the GS frame's size
 * (at most SCENE's scale), four samples a GS pixel by supersampling, the box
 * reducing them: the lamps' coverage anti-aliased as today's scaled raster
 * makes it (the original's 1x raster shimmers on thin lamps), at 4/(scale^2)
 * of today's WORK2 texels */
#define RD_LAMPS_LOWRES2X 4096u
/* lowresms (implies lowres and the box): WORK2 at the GS frame's size as the
 * resolve target of a 4-sample colour and depth pair that live inside the
 * lamps' pass only (RHI_TEX_TRANSIENT, RhiColorAttachment.resolve: resolved
 * in the pass, never stored): the fill, fans, mask and lamps drawn with 4
 * samples, the depth per sample (depth_down_ms_ps at the standard sample
 * locations, drawn first in the pass): the lamps' coverage and occlusion
 * anti-aliased as today's scaled raster makes them, at the GS size's
 * traffic.  Only where RhiLimits.resolveInPass, sampleRateShading,
 * standardSampleLocations, 4 samples, the PowerVR or a desktop class (never
 * the Arm, Qualcomm or Apple classes in this release: issues 123, 179, 182,
 * 211), the MSAA scene not blocked, and its start-up self-test passed;
 * elsewhere LOWRES is off (today's full-size path, never the aliased one) */
#define RD_LAMPS_LOWRESMS 8192u
/* lowresms8: the same with 8 samples (the standard 8-sample locations) */
#define RD_LAMPS_LOWRESMS8 16384u
/* fillpaste: under the lamps plan the crack fill's snapshot taken where it
 * lands, the glow's (or the aura's) paste pass (rd_crack_fill.c deferred):
 * by the swap there, the fill, the paste and list 11's joined draws one pass
 * on SCENE that loads nothing (today: the fill's own pass, then the paste's,
 * loading and storing SCENE).  Exact; it needs the hold to pass the lamps
 * (WORK2's own depth: LOWRES) and acts on the tile class under Vulkan */
#define RD_LAMPS_FILLPASTE 32768u
/* glowblur: the glow's blur pairs drawn as two passes of float filtering
 * (rd_glow_blur.c; identical to the eye, not byte-identical: DIVERGENCES.md
 * VD-33); not in RD_LAMPS_ALL.  By its word, or by the build's
 * RD_GLOW_BLUR_DEFAULT (rd_glow_blur.h) on every class and backend */
#define RD_LAMPS_GLOWBLUR 65536u
/* the tests that compare the glow with the game's exact chain (the CPU GS
 * model, the native-frame manifest, the light plan's A/B): the glow blur
 * off whatever RD_GLOW_BLUR_DEFAULT says, every other bit as before
 * (ICO_RD_LAMPS=noglowblur does the same for a tool run) */
void rd__lamps_glowblur_pin(bool off);
#ifndef RD_LAMPS_FILLPASTE_DEFAULT
#define RD_LAMPS_FILLPASTE_DEFAULT 1
#endif
/* lowresms where ICO_RD_LAMPS names no words of its own: 1 (v0.11.5) on
 * the PowerVR class under Vulkan alone, where the checks and the self-test
 * pass (rd__lamps_default_bits, rd__lamps_lowres_on; every other class keeps
 * today's full-size lamps: it never ran on desktop hardware), 0 nowhere */
#ifndef RD_LAMPS_MS_DEFAULT
#define RD_LAMPS_MS_DEFAULT 1
#endif
/* LOWRES's form where the build's default turns it on: 0 the GS size, 1
 * twice it (lowres2x) */
#ifndef RD_LAMPS_LOWRES_SS_DEFAULT
#define RD_LAMPS_LOWRES_SS_DEFAULT 0
#endif
#define RD_LAMPS_LOWRES_ANY                                                                        \
    (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESBOX | RD_LAMPS_LOWRESBIAS | RD_LAMPS_LOWRES4 |               \
     RD_LAMPS_LOWRESP | RD_LAMPS_LOWRESHY | RD_LAMPS_LOWRES2X | RD_LAMPS_LOWRESMS |                \
     RD_LAMPS_LOWRESMS8)
/* the slope bias's reach past the nearest texel's own distance from the
 * sample point, in GS pixels: the rasteriser's sub-pixel snap (the GS's
 * 1/16).  (Round 7's 0.25 covered a sample point taken (0.5 - 0.5 / scale)
 * GS pixels off, now corrected in depth_down_ps.) */
#define RD_LAMPS_BIAS_REACH 0.0625f
/* and a constant, in GS Z units (the depth format's rounding) */
#define RD_LAMPS_BIAS_UNITS 4.0f
/* the plain LOWRES (single-sampled, the GS-size raster: it shimmers) where
 * ICO_RD_LAMPS does not name it and the 4-sample form is not taken: 0 off
 * (v0.11.5), 1 on everywhere (with the GS bilinear).  A build define: a
 * variant build or a release flips it; ICO_RD_LAMPS=... with or without
 * lowres still decides for the tests */
#ifndef RD_LAMPS_LOWRES_DEFAULT
#define RD_LAMPS_LOWRES_DEFAULT 0
#endif
#define RD_LAMPS_ALL                                                                               \
    (RD_LAMPS_AREA | RD_LAMPS_PAD | RD_LAMPS_OWED | RD_LAMPS_WHOLEPAD | RD_LAMPS_FOLD4)

/* why a LAMPS frame's pass on WORK2 was drawn over the whole target */
enum {
    RD_LAMPS_WHY_NONE = 0,
    RD_LAMPS_WHY_OFF,    /* AREA switched off (ICO_RD_LAMPS) */
    RD_LAMPS_WHY_CLASS,  /* not the PowerVR or a desktop class, not Vulkan, no pass area */
    RD_LAMPS_WHY_TEST,   /* the self-test not run or not passed */
    RD_LAMPS_WHY_FIRST,  /* the WORK2 an earlier frame left not known */
    RD_LAMPS_WHY_BOX,    /* a lamp whose screen box is not known */
    RD_LAMPS_WHY_OTHER,  /* another command writes WORK2 in the frame */
    RD_LAMPS_WHY_LARGE,  /* the area holds WORK2 but a tile column and row */
    RD_LAMPS_WHY_EMPTY,  /* no lamp on WORK2 and nothing lit there */
    RD_LAMPS_WHY_FORM,   /* the pass not begun as planned (its load, colour, depth) */
    RD_LAMPS_WHY_WINDOW, /* the replay tool's pass window */
    RD_LAMPS_WHY_COUNT
};

/* why a lamp's screen box is not known (the BOX reason's parts) */
enum {
    RD_LAMPS_BOX_NONE = 0,
    RD_LAMPS_BOX_KIND,    /* not a mesh or a screen prim */
    RD_LAMPS_BOX_PAYLOAD, /* its payload short or outside the frame */
    RD_LAMPS_BOX_CLIP,    /* clip mode none (code 34) */
    RD_LAMPS_BOX_PROG,    /* not a mesh program */
    RD_LAMPS_BOX_MESH,    /* no mesh record or stream */
    RD_LAMPS_BOX_FINITE,  /* a position or a matrix lane not finite */
    RD_LAMPS_BOX_BEHIND,  /* a vertex at or behind the eye */
    RD_LAMPS_BOX_BUDGET,  /* past the vertex budget, the corners' box refused */
    RD_LAMPS_BOX_SCREEN,  /* a screen prim under DATE, the wrap or a full-frame sprite */
    RD_LAMPS_BOX_COUNT
};

/* The roles of the glow's chain commands the scan asks rd_glow_light.c for
 * (rd__glow_light_role): WORK2's fill, the mask (drawn or skipped), the
 * sun's fans (drawn or skipped), a lamp, the reduction, the band, any
 * other chain command; NONE for a command outside the chain */
enum {
    RD_GLOW_ROLE_NONE = 0,
    RD_GLOW_ROLE_PAD,
    RD_GLOW_ROLE_FILL,
    RD_GLOW_ROLE_FAN,
    RD_GLOW_ROLE_FAN_SKIPPED,
    RD_GLOW_ROLE_MASK,
    RD_GLOW_ROLE_MASK_SKIPPED,
    RD_GLOW_ROLE_LAMP,
    RD_GLOW_ROLE_REDUCTION,
    RD_GLOW_ROLE_OTHER
};

/* rd_glow_light.c: the role of the chain command walked at ordinal ord in
 * this replay's scan (after rd__glow_light_scan_end), and the ordinal of
 * the first command of a role (UINT32_MAX: none) */
int rd__glow_light_role(uint32_t ord);
uint32_t rd__glow_light_role_ord(int role);

/* ------------------------------------------------------- pure helpers
 * (rd_lamps_test.c checks them on the CPU) */

/* the bits ICO_RD_LAMPS asks for: 1 and *bits set when e names them ("0",
 * "1", "all", or "area" and "pad" joined by ',' or '+'), 0 when e is
 * unset or empty, -1 when it names something else (*bits untouched) */
int rd__lamps_parse(const char *e, uint32_t *bits);
/* the bits on by default: PAD, OWED and WHOLEPAD everywhere; AREA on tile
 * class 1 (PowerVR) or 2 (a desktop class) that is not a tile-based phone,
 * under Vulkan, where the profile's pass area is on; FOLD4 on tile class 1
 * under Vulkan where the profile's blur fold is on (a desktop class takes it
 * by ICO_RD_LAMPS only) */
uint32_t rd__lamps_default_bits(int tileClass, bool tilePhone, bool vulkan, bool passArea,
                                bool blurFold);
/* the union of a and b (an empty rect, w or h 0, adds nothing) */
RhiRect rd__lamps_union(RhiRect a, RhiRect b);
/* r aligned out to whole tiles of tile texels, clamped to w x h */
RhiRect rd__lamps_tile_align(RhiRect r, uint32_t tile, uint32_t w, uint32_t h);
/* The texels of a target of tw x th texels (sx, sy texels a GS pixel, ox
 * Full pixel's crop, GS size w x h) that a draw whose samples lie in GS
 * box x0..x1, y0..y1 (GS primitive coordinates: X / 16, the frame's origin
 * 2048 - gsW / 2, 2048 - gsH / 2) can touch, padded by pad GS pixels; false
 * when it holds none.  The arithmetic is rd_pass_area.c
 * rd__gs_rect_texels' (here on the record's fields). */
bool rd__lamps_box_texels(const RdTargetRec *tc, uint32_t gsW, uint32_t gsH, double x0, double y0,
                          double x1, double y1, int32_t pad, RhiRect *out);
/* The GS box (box[0..3]: x0, y0, x1, y1, GS primitive coordinates) that
 * holds every sample a VU mesh draw of the n vertices of stream (qpv qwords
 * each, the position qword 0) can touch through m (VU memory 16..19, VU
 * order: h = m[0] x + m[1] y + m[2] z + m[3] w; w = 1, under scissor code
 * 36 pos.w and 1): each vertex's h in double with the shader's float error
 * (RD_VIS_TOL of the sum of the absolute products) on every lane, its x / w
 * and y / w bounded over that interval; every vertex in front of the eye
 * (w less its error above 0), else RD_LAMPS_BOX_BEHIND with *behind the
 * count; the hull of the corners' points holds every point the GPU draws of
 * a triangle whose corners lie in front (its clipped points are convex
 * combinations with w > 0), widened by rd__vis_hiz_occludee's margins (the
 * divide, ftoi4's 1/16, the snap, the GS-to-clip mapping).
 * RD_LAMPS_BOX_NONE, or why not (MESH, FINITE, BEHIND). */
int rd__lamps_vertex_box(const float (*m)[4], const float (*stream)[4], uint32_t n, uint32_t qpv,
                         bool scissor, double box[4], uint32_t *behind);

/* The area of a LAMPS frame's pass on WORK2 (tw x th texels): the lamps'
 * box (texels) with the region an earlier frame left lit (lit, when
 * litKnown), aligned out to tiles; RD_LAMPS_WHY_NONE and *area set, or why
 * not (FIRST, EMPTY, LARGE) */
int rd__lamps_area_pick(bool litKnown, RhiRect lit, RhiRect box, uint32_t tw, uint32_t th,
                        RhiRect *area);

/* -------------------------------------------------------- the renderer */

/* the switch at rd_init (rd_pass_area.c rd__pass_area_init): the bits, the
 * tracker of WORK2's lit region forgotten, the 10 s counts kept */
void rd__lamps_init(void);
/* the bits in force (AREA only where the self-test passed) */
uint32_t rd__lamps_bits(void);
/* the tests' switch (-1: the environment's or the profile's) */
void rd__lamps_force(int bits);
/* "area+pad", "area", "pad" or "off" */
const char *rd__lamps_bits_text(uint32_t bits);
const char *rd__lamps_why_text(int why);
/* the words of a RD_LAMPS_BOX_* */
const char *rd__lamps_box_why_text(int why);
/* the self-test's state: 0 not run, 1 passed, 2 failed or not run for a
 * reason (its words) */
int rd__lamps_test_state(const char **words);
/* the start-up self-test (rd_crack_fill.c rd__crack_swap_selftest calls it,
 * so rd_fog_selftest's first run and rd_replay_tool after every rd_init
 * run it): true when it passed or was not needed */
bool rd__lamps_selftest(void);
/* the poison (ICO_RD_LAMPS_POISON=1) */
bool rd__lamps_poison(void);
/* WORK2_PAD's band left a lazy clear in a LAMPS frame (rd_glow_light.c
 * planActs) */
bool rd__lamps_pad_on(void);
/* a band left so (rd_glow_light.c rd__glow_light_sprite) */
void rd__lamps_pad_noted(void);
/* DARK's WORK0 alpha sprite left a lazy clear (RD_LAMPS_OWED; rd_glow_light.c
 * planActs), and one left so (rd__glow_light_sprite) */
bool rd__lamps_owed_on(void);
void rd__lamps_owed_noted(void);
/* WORK2_PAD's band left a lazy clear under WHOLE (RD_LAMPS_WHOLEPAD) */
bool rd__lamps_wholepad_on(void);

/* ------------------------------------------- the fold of two blur pairs
 * (RD_LAMPS_FOLD4; rd_replay_tiler.c, fx_sprite.hlsl fx_blur4_ps) */

/* why a pair that folds was drawn alone, not with the next one */
enum {
    RD_LAMPS_F4_NONE = 0,
    RD_LAMPS_F4_PAIR,   /* the next pair does not fold as a pair does */
    RD_LAMPS_F4_STRIP,  /* the glow's edge strip follows the next pair's pass 2 */
    RD_LAMPS_F4_LAST,   /* no pair after the next one holding its pass 1's texels */
    RD_LAMPS_F4_FORM,   /* the pairs differ in more than the shrink's six values */
    RD_LAMPS_F4_WINDOW, /* a window wider than fx_blur4_ps holds (rd__lamps_fold4_axis) */
    RD_LAMPS_F4_COUNT
};

const char *rd__lamps_f4_why_text(int why);

/* One axis of two folded pairs, as fx_blur4_ps computes it: o the origin
 * (12.4), c0 .. c1 pass 2B's covered pixels, pass 2B's rect and UVs on the
 * axis (b2x0, b2x1, b2u0, b2u1; pass 2A's rect is pass 2B's), pass 1B's
 * (b1*), pass 2A's UVs (a2u0, a2u1), pass 1A's rect (a1x0, a1x1; its UVs
 * pass 1B's), lsz the TEX0 size, w0 and w1 WORK0's and WORK1's sizes, loB
 * and loA passes 1B's and 1A's first drawn WORK1 column (0 on y). */
typedef struct RdFold4Axis {
    int32_t o, c0, c1;
    int32_t b2x0, b2x1, b2u0, b2u1;
    int32_t b1x0, b1x1, b1u0, b1u1;
    int32_t a2u0, a2u1, a1x0, a1x1;
    int32_t lsz, w0, w1, loB, loA;
} RdFold4Axis;

/* Every pixel c0 .. c1: pass 2B's taps j (CLAMP to lsz then w1, at least
 * loB), pass 1B's WORK0 taps there (G2: their range, clamped to lsz then
 * w0), pass 2A's WORK1 taps at G2's texels (G1: their range, at least loA),
 * the divides truncated (the GS's; fx_blur4_ps's gs_div_trunc and its
 * reciprocal form).  *w2 and *w1 raised to the widest G2 and G1 seen;
 * false when one is wider than fx_blur4_ps's FX4_W2 (4) or FX4_W1 (6), or
 * a divisor is 0. */
bool rd__lamps_fold4_axis(const RdFold4Axis *a, int32_t *w2, int32_t *w1);
/* the switch's bits before the self-tests (the fold's self-test reads it) */
uint32_t rd__lamps_wanted(void);
/* a pass of two pairs drawn; a pair drawn alone (why) */
void rd__lamps_fold4_noted(void);
void rd__lamps_fold4_refused(int why);
/* rd_replay_tiler.c: the fold of two pairs' start-up self-test (called with
 * rd__lamps_selftest) and its state (0 not run, 1 passed, 2 failed or not
 * run for a reason, its words) */
bool rd__blur_fold4_selftest(void);
int rd__blur_fold4_test_state(const char **words);

/* the scan (rd_store_plan.c's one walk; rd__lamps_scan_end after
 * rd__glow_light_scan_end) */
void rd__lamps_scan_begin(const RdFrame *f);
void rd__lamps_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                        uint32_t ord);
void rd__lamps_scan_end(void);

/* A render pass beginPass begins (rd_pass_area.c rd__pass_area_begin, before
 * the pass area's own rules): on WORK2 in a LAMPS frame the planned pass
 * gets the lamps' area (true: p->area set); any other pass on WORK2's
 * colour forgets the lit region.  The pass needs no end of its own: the
 * chain's grammar (rd_glow_light.c, PH_MASK) puts nothing but the lamps
 * (and the fans) into WORK2 between the fill and the reduction, which
 * draws into WORK0 and so begins the next pass.  ms, storage: as
 * rd__pass_area_begin's. */
bool rd__lamps_pass(RhiRenderPassDesc *p, uint32_t cid, bool ms, bool storage);

/* RD_LAMPS_LOWRES: asked for (ICO_RD_LAMPS with the word lowres), read
 * once and kept for the run (rd_core.c sceneClass and namedTargetDesc: WORK2
 * at the GS frame's size with its own depth at every scale) */
bool rd__lamps_lowres_on(void);
/* prepareDrawEx (rd_replay.c, after rd__late_pass_join): a draw into WORK2
 * on SCENE's depth where the two differ in size (above 1x under LOWRES)
 * takes WORK2's own depth, which holds SCENE's taken down to the GS size
 * (depth_down_ps, a depth-only pass of its own before the draw's: once a
 * replay, again after a draw that wrote SCENE's depth).  Where it cannot
 * (DATE's stencil, no pipeline, no copy of SCENE's depth) the draw goes
 * without a depth (counted, said once).  At 1x nothing changes. */
struct Replay;
struct DrawSetup;
void rd__lamps_lowres_depth(struct Replay *r, struct DrawSetup *ds);
/* rd_late_pass.c: true when a draw into color on depth is drawn on WORK2's
 * own depth (LOWRES and FILLHOLD on, color WORK2, depth another size), so it
 * writes no depth of SCENE (counted once a replay for the 10 s line) */
bool rd__lamps_fill_hold_ok(uint32_t color, uint32_t depth);
/* rd_store_plan.c: did is WORK2's own depth under LOWRES (the scan sees
 * the lamps' draws on SCENE's depth, so the plan keeps this one's loads and
 * stores as the pass asks) */
bool rd__lamps_own_depth(uint32_t did);
/* t is WORK2 at the GS size under LOWRES with SCENE scaled (rd_jitter.c:
 * not jittered: its lamps are drawn at the original's 1x sample points, and
 * depth_down_ps moves SCENE's sample point by the jitter instead) */
bool rd__lamps_lowres_gs(const RdTargetRec *t);
/* WORK2's own scale under LOWRES (rd_core.c rd__target_scale_of): 2 with
 * lowres2x, else 1 */
float rd__lamps_lowres_factor(void);
/* LOWRESMS in force (latched with LOWRES, its checks and self-test passed) */
bool rd__lamps_lowres_ms(void);
/* FILLPASTE in force (rd_crack_fill.c), and a deferred fill landed in a
 * paste pass (landed) or taken where it was flushed (why: RD_LATE_WHY_*) */
bool rd__lamps_fillpaste_on(void);
void rd__lamps_fillpaste_noted(bool landed, int why);
void rd__lamps_fillpaste_counts(uint64_t *landed, uint64_t *flushed);
/* the run's LOWRESMS counts (the tests): passes drawn with 4 samples and
 * resolved in the pass; passes on WORK2 drawn single-sampled (a pass that
 * loads WORK2, reads its destination, or is a storage pass) */
void rd__lamps_ms_counts(uint64_t *ms, uint64_t *single);
/* the run's LOWRES counts (the tests): draws on WORK2's own depth, depth
 * fills, draws without a depth */
void rd__lamps_lowres_counts(uint64_t *draws, uint64_t *fills, uint64_t *dropped);
/* the depth copy LOWRES samples SCENE's depth through (rd_shutdown) */
void rd__lamps_shutdown(void);

/* the counts since rd_init (the tests): passes with the lamps' area, drawn
 * whole, bands left lazy */
void rd__lamps_counts(uint64_t *areas, uint64_t *whole, uint64_t *pads);

#ifdef __cplusplus
}
#endif

#endif
