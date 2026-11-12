/* rd_shadow_tile.h: the shadow composites culled outside a GPU tile mask
 * (rd_shadow_tile.c; docs/port/RENDER_POST.md "The shadow tiles").
 *
 * After the shadow levels are drawn, a small pass per level writes its tile
 * mask (shadow_tile_mask_ps: a texel per RD_ST_BLOCK x RD_ST_BLOCK level
 * texels, 1 where any level texel within the margin holds alpha above 0).
 * Each composite over a box (rd_shadow_box.c) on DATE's stencil path then
 * takes its DATE set-up as: bits 6 and 7 cleared over its area, bit 7 set
 * where its sampled UV's mask block is 0 (shadow_tile_mark_ps), bit 6 set
 * from the alpha's MSB where bit 7 is 0; the composite tests bit 6 where
 * bit 7 is 0.  Its geometry, interpolation and shader are the composite's
 * own; only the stencil culls it in the dead tiles, where its alpha test
 * would have failed at every fragment. */
#ifndef ICO_RD_SHADOW_TILE_H
#define ICO_RD_SHADOW_TILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

struct Replay;
struct DrawSetup;
struct RdPerfRecord;

#define RD_ST_BLOCK 2          /* level texels a mask texel, each axis */
#define RD_ST_MARGIN 3         /* level texels about a block: bilinear */
#define RD_ST_MARGIN_BSPLINE 5 /* with the B-spline composites above 1x */
#define RD_ST_MASKS 4          /* levels with a mask in one replay */

/* why a composite was drawn whole (RdPerfRecord.shadowTileWhy) */
enum {
    RD_ST_WHY_OFF = 0, /* the class, the self-test or ICO_RD_SHADOW_TILE=0 */
    RD_ST_WHY_PASS,    /* a pass on the composite's target was open: no mask could be drawn */
    RD_ST_WHY_MASK,    /* the mask could not be made (texture, pipeline, a level not at scale 1) */
    RD_ST_WHY_FORM,    /* not one sprite, a degenerate map, a wrap other than the mask's */
    RD_ST_WHY_SETUP,   /* the tiled set-up's pipelines or the readable pass missing */
    RD_ST_WHY_COUNT
};

_Static_assert(RD_ST_WHY_COUNT <= RD_SHADOW_TILE_WHYS, "rd.h RD_SHADOW_TILE_WHYS");

/* the decision: the composites may be drawn over the tile mask (the class,
 * the self-test, ICO_RD_SHADOW_TILE) */
bool rd__shadow_tile_on(void);
/* rd_pipeline.c: the start-up set holds the tiles' keys (a device where they
 * may be drawn: DATE's stencil bit, the modules, the class or the switch) */
bool rd__shadow_tile_keys_wanted(void);
RdPipeKeyInt rd__shadow_tile_mask_key(void);
/* TEST0 -> TEST0_T, TEST1 -> TEST1_T (the composite's passes over the mask) */
void rd__shadow_tile_test_key(RdPipeKeyInt *k);

/* rd_replay.c doScreen after prepareDrawEx: a boxed composite on DATE's
 * stencil path drawn over the tile mask (the masks drawn first, once a
 * replay, while no pass on its target is open): ds->shadowTile, r->tileNow */
void rd__shadow_tile_prepare(struct Replay *r, struct DrawSetup *ds, const RdScreenVtx *v,
                             uint32_t n);
/* rd_replay.c bindDrawEx: the tiled DATE set-up (false: nothing drawn) */
bool rd__shadow_tile_take(struct Replay *r, const struct DrawSetup *ds);

/* the masks this replay drew read back (the tests, rd_replay_tool --stats):
 * per mask its target, live texels and texels, in the order drawn; the
 * count returned */
unsigned rd__shadow_tile_count(uint32_t *target, uint32_t *live, uint32_t *total, unsigned max);

/* the start-up self-test (rd_fog_path.c rd_fog_selftest, once a renderer,
 * after the stencil's): where the class may take the tiles (or
 * ICO_RD_SHADOW_TILE=1); logs the start line either way.  variant: 0 a
 * shadow in a corner, 1 shadows at the frame's edges, 2 no shadow (the
 * tests run each; the start-up run is 0).  False when it ran and failed */
bool rd__shadow_tile_selftest(void);
bool rd__shadow_tile_selftest_variant(int variant, char *words, size_t size);
bool rd__shadow_tile_test_ok(void);

/* rd_core.c rd_shutdown, before the device goes: the masks' textures freed
 * and their slots forgotten (a mask of an earlier renderer is no texture of
 * the next one: rd_replay_tool's present renderer, a Graphics API swap, a
 * resume), the self-test's verdict with them (the next renderer runs its
 * own) */
void rd__shadow_tile_shutdown(void);

/* rd_profile.c: 1 the Imagination (PowerVR) class, 2 a desktop class
 * (desktop Vulkan, Direct3D 12, Direct3D 11, OpenGL), 0 any other (the
 * Adreno and Mali classes, Apple, another tile-based GPU) */
int rd__profile_tile_class(void);

/* the lines */
void rd__shadow_tile_pop(const struct RdPerfRecord *rec);
void rd__shadow_tile_text(char *out, size_t size, size_t *at);
void rd__shadow_tile_stats(const struct RdPerfRecord *rec, char *out, size_t size);

#endif
