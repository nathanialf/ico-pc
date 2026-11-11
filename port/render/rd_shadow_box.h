/* rd_shadow_box.h: the shadow count and composites drawn only over the
 * shadow volumes' box (rd_shadow_box.c; docs/port/WORK_BUFFERS.md "The
 * shadows over the volumes' box").
 *
 * The geometry (pure, the CPU test's): boxes are inclusive GS pixels of a
 * target, after XYOFFSET.  The count's box is the volumes' 12.4 vertices'
 * extent, RD_SB_COUNT_MARGIN GS pixels wider each side; a sprite that samples a source whose
 * texels outside its box are (0, 0, 0, 0) takes, at a destination pixel, a
 * texel of that box only where its UV comes within RD_SB_TEX_MARGIN texels
 * of it (the bilinear taps, the B-spline's, the above-1x snap), so the
 * destination's box is the inverse of the sprite's UV map over that range,
 * RD_SB_PIX_MARGIN pixels wider; a clamped axis whose box reaches an edge
 * takes every UV past it, a repeated axis that reaches an edge (or whose UVs
 * leave the texture) is taken whole. */
#ifndef ICO_RD_SHADOW_BOX_H
#define ICO_RD_SHADOW_BOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "rhi.h"

struct RdStateBlock;
struct RdCmd;
struct RdFrame;
struct RdTargetRec;
struct RdPerfRecord;

#define RD_SB_TEX_MARGIN 3   /* source texels */
#define RD_SB_PIX_MARGIN 2   /* destination GS pixels */
#define RD_SB_COUNT_MARGIN 4 /* the count's GS pixels about the volumes' vertices */

/* inclusive GS pixels; empty: nothing */
typedef struct RdSbBox {
    int32_t x0, y0, x1, y1;
    uint8_t empty;
} RdSbBox;

/* the extent of vertices relative to the origin, in 1/16 GS pixels */
typedef struct RdSbExtent {
    int32_t minX, minY, maxX, maxY;
    uint8_t any;
} RdSbExtent;

/* a vertex (12.4, clamped to 0..0xFFFF as rd_replay.c convVtx clamps it)
 * relative to the origin ox16, oy16 (12.4) into e */
void rd_sb_extent_add(RdSbExtent *e, int32_t x16, int32_t y16, int32_t ox16, int32_t oy16);
/* the count's box of a w x h target: the extent's pixels, RD_SB_COUNT_MARGIN
 * wider each side, clipped to the target */
RdSbBox rd_sb_count_box(const RdSbExtent *e, uint32_t w, uint32_t h);
/* one axis of a sprite from xa to xb (destination GS pixels) with UVs ua to
 * ub (source texels) over a source of size texels whose box on the axis is
 * b0..b1: the destination pixels *d0..*d1 that may take a texel of it;
 * false when none */
bool rd_sb_map_axis(float xa, float xb, float ua, float ub, int32_t b0, int32_t b1, uint32_t size,
                    bool repeat, int32_t *d0, int32_t *d1);
/* a sprite's two vertices (12.4 positions, uvFixed 12.4 UVs) plus the UV
 * offset in texels, the origin, the source's box and size, its wrap: the
 * destination box (unclipped; empty when it takes none) */
RdSbBox rd_sb_map_sprite(const RdScreenVtx *a, const RdScreenVtx *b, float uOff, float vOff,
                         int32_t ox16, int32_t oy16, const RdSbBox *src, uint32_t sw, uint32_t sh,
                         bool repS, bool repT);
/* whether the sprite a..b covers every pixel of a w x h target (the GS's
 * rule: pixel p when ceil(x0) <= p < ceil(x1)) */
bool rd_sb_covers(const RdScreenVtx *a, const RdScreenVtx *b, int32_t ox16, int32_t oy16,
                  uint32_t w, uint32_t h);
RdSbBox rd_sb_union(RdSbBox a, RdSbBox b);
RdSbBox rd_sb_clip(RdSbBox a, uint32_t w, uint32_t h);

/* why a count or a composite was drawn whole (RdPerfRecord.shadowBoxWhy) */
enum {
    RD_SB_WHY_OFF = 0, /* ICO_RD_SHADOW_BOX=0 */
    RD_SB_WHY_SEVEN,   /* the count by the seven draws */
    RD_SB_WHY_MSAA,    /* the count in the MSAA epoch */
    RD_SB_WHY_WINDOW,  /* no reset this replay, or strips of another size or a scale under 1 */
    RD_SB_WHY_CHAIN,   /* the source holds no box (another write since, not drawn) */
    RD_SB_WHY_FORM,    /* the draw's form (alpha test, TCC, TFX, Z write, STQ, prim) */
    RD_SB_WHY_COUNT
};

_Static_assert(RD_SB_WHY_COUNT <= RD_SHADOW_BOX_WHYS, "rd.h RD_SHADOW_BOX_WHYS");

/* ------------------------------------------- the scan (rd_store_plan.c) */
bool rd__shadow_box_scan_begin(const struct RdFrame *f);
void rd__shadow_box_scan_cmd(const struct RdFrame *f, const struct RdCmd *c,
                             const struct RdStateBlock *st, bool state, uint32_t ord);
void rd__shadow_box_scan_end(void);
/* the role the scan gave ordinal ord (the tests): 0 none, 1 the count's
 * resolve, 2 a level, 3 a composite, 4 drawn whole; *src its source */
int rd__shadow_box_role(uint32_t ord, uint32_t *src);

/* --------------------------------------------- the replay (rd_replay.c) */
/* doShadowReset: a count begins on colour target color */
void rd__shadow_box_reset(uint32_t color);
/* doShadowStripIn: the strip's vertices before conversion (RD_SHADOW_TRIS:
 * n RdScreenVtx; the float strip: n XYZW floats), under the state st */
void rd__shadow_box_strip_v(const RdScreenVtx *v, uint32_t n, const struct RdStateBlock *st);
void rd__shadow_box_strip_f(const float (*fv)[4], uint32_t n, const struct RdStateBlock *st);
/* rd_shadow_one.c rd__shadow_one_draw, before its pass: true with *sc (the
 * count target's GS pixels; w 0 when the box is empty) when the count may
 * be drawn over its box with the colour cleared to 0 */
bool rd__shadow_box_count_area(const struct RdTargetRec *tc, RhiRect *sc);
/* shadowCountOne after the one draw (done) / doShadowResolve's seven draws
 * or MSAA (whole) for count target color */
void rd__shadow_box_count_done(uint32_t color);
void rd__shadow_box_count_whole(uint32_t color, bool msaa);
/* doScreen, after the DATE area: true with *rect (texels of the draw's
 * target; w 0: nothing to draw) for a composite over its box */
bool rd__shadow_box_screen(const struct RdStateBlock *st, const struct RdCmd *c,
                           const RdScreenVtx *v, uint32_t n, RhiRect *rect);
/* doScreen after bindDrawEx: the draw's scissor before the box, and after
 * it (NULL: not boxed); a level's box is kept here, once drawn */
void rd__shadow_box_drawn(const RhiRect *all, const RhiRect *boxed);

/* ------------------------------------------------------------ the lines */
void rd__shadow_box_pop(const struct RdPerfRecord *rec);
void rd__shadow_box_text(char *out, size_t size, size_t *at);
void rd__shadow_box_stats(const struct RdPerfRecord *rec, char *out, size_t size);

#endif
