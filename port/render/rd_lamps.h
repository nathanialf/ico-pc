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
 * plan's lazy clears run there already).  ICO_RD_LAMPS=0|1|area|pad|area,pad
 * (the tests only, docs/port/TESTING.md; no config key): off, both, one
 * (forced on any class; the self-test still decides AREA).
 * ICO_RD_LAMPS_POISON=1 (the tests): before every planned lamps pass drawn
 * over the whole of WORK2 its colour is cleared to magenta first, so a pass
 * that relied on WORK2's earlier contents where it should not would show
 * the magenta (rd_lamps_ab). */
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
#define RD_LAMPS_ALL (RD_LAMPS_AREA | RD_LAMPS_PAD)

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
/* the bits on by default: PAD everywhere, AREA on tile class 1 (PowerVR)
 * or 2 (a desktop class) that is not a tile-based phone, under Vulkan,
 * where the profile's pass area is on */
uint32_t rd__lamps_default_bits(int tileClass, bool tilePhone, bool vulkan, bool passArea);
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

/* the counts since rd_init (the tests): passes with the lamps' area, drawn
 * whole, bands left lazy */
void rd__lamps_counts(uint64_t *areas, uint64_t *whole, uint64_t *pads);

#ifdef __cplusplus
}
#endif

#endif
