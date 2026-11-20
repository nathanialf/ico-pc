/*
 * port/ui/pad_glyphs.h
 *
 * The button pictures of other controllers (issue 187): the contributed
 * SVG files (port/assets/buttons/<set>/<button>.svg, CREDIT.txt) embedded
 * at build time (pad_glyphs.cmake), parsed once by a small reader of exactly
 * what those files use, rasterised at run time at the size they are drawn
 * with stb_truetype's rasteriser (font.c ui__raster_polys), and cached as
 * one RGBA atlas of the eight pictures per set and height.  docs/port/UI.md
 * "Button pictures".
 *
 * Threading: every call on the thread that draws the menus (the one that
 * records rd lists, as font.c's), except the CPU-only entries at the end,
 * which touch no texture.  The first call of any of them reads the 24 files
 * and writes the start-up line.
 */
#ifndef PORT_UI_PAD_GLYPHS_H
#define PORT_UI_PAD_GLYPHS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The picture sets.  PlayStation is the disc's own pictures (nothing of
   this module draws it); the others are the contributed files. */
typedef enum IcoPadGlyphStyle {
    ICO_PAD_GLYPHS_PLAYSTATION = 0,
    ICO_PAD_GLYPHS_XBOX,
    ICO_PAD_GLYPHS_NINTENDO,
    ICO_PAD_GLYPHS_WII_CLASSIC,
    ICO_PAD_GLYPHS_COUNT
} IcoPadGlyphStyle;

/* The pictured buttons, by the PlayStation 2 button whose place each takes:
   the same numbers as layout_ext.h's LtExtGlyph LT_GLYPH_CROSS ..
   LT_GLYPH_R2 (0 .. 7; LT_GLYPH_LEFT / RIGHT, the menu arrows, have no
   picture here).  The art is mapped by position: nintendo/cross.svg is the
   B letter (the south button), xbox/cross.svg the A. */
#define PAD_GLYPHS_PIECES 8

/* Reads and checks the 24 files once and logs which sets are complete (also
   done by the first call of anything below). */
void pad_glyphs_init(void);

/* 1 when all eight pictures of the set parsed (PlayStation: always 1, the
   disc's own); 0 otherwise and for a style out of range.  A set that is not
   complete can never be the style in force (pad_glyphs_set_style). */
int pad_glyphs_set_complete(int style);

/* The style in force.  The caller (the Button pictures setting) resolves
   Auto and sets the result; a style out of range or a set that is not
   complete sets PlayStation instead (one log line per refused style).
   pad_glyphs_style returns the style in force; pad_glyphs_active is 1 when
   it is not PlayStation and its set is complete (checked again at each
   call: a set a test made incomplete stops drawing at once). */
void pad_glyphs_set_style(int style);
int pad_glyphs_style(void);
int pad_glyphs_active(void);

/* "PlayStation", "Xbox", "Nintendo", "Wii Classic Controller" (log lines);
   "?" out of range */
const char *pad_glyphs_style_name(int style);

/* The art's width over its height (its viewBox's: 1 for 24 x 24, 40/24 for
   the 40 x 24 shoulders, 24/32 for the Wii Classic Controller's face
   letters); 0 for PlayStation, an invalid piece or a glyph out of 0 .. 7. */
float pad_glyphs_aspect(int style, int glyph);

/* The atlas height a request of pixelH output pixels is drawn from: the
   first of 16, 24, 32, 48, 64, 96, 128, 192, 256 at or above it (256 above
   that, 16 below 1). */
int pad_glyphs_bucket(int pixelH);

/* The picture of glyph (0 .. 7, LtExtGlyph) in the style in force, for a
   draw about pixelH output pixels high: 1 with *tex the atlas's rd texture
   id (RdTex.id), uv the cell's texel rectangle (u0, v0, u1, v1: texels, the
   cell's outer edges, not half a texel in: the cell is the art's exact
   extent, rasterised at the bucket's height, with two transparent texels
   around it) and *aspect as pad_glyphs_aspect.  0 (and *tex 0) when no
   style is active, for the menu arrows or any other glyph, without ICO_RD,
   or when no atlas can be made.

   The texels: RGBA8, straight (not premultiplied) colour, the alpha at the
   GS's scale (0x80 opaque), made with rd_create_texture(..., RD_TEXA_80_80)
   for ui__draw_tex_quads (ui_internal.h: RD_TEXFN_MODULATE, RD_TCC_RGBA,
   RD_BLEND_LERP_AS, so a vertex colour of 0x80 0x80 0x80 0x80 draws the art
   as it is); a transparent texel carries the stroke's colour so bilinear
   filtering at the edge brings in no other colour.

   The atlas is made inside this call the first time a (style, bucket) is
   asked for (eight pictures rasterised, one texture upload; the menu font's
   pages are made at the same point of a draw), and lives until: the cache's
   six slots are full and it is the least recently drawn that no frame of
   the last four can still name; the style in force changes (then it goes
   at the first pad_glyphs_texture or pad_glyphs_set_style call once those
   frames have passed: calling pad_glyphs_set_style every frame with the
   same style is cheap and lets it go); ui_font_forget_textures (rd
   started again: the ids are stale) or ui_font_shutdown.  When every slot
   was drawn in the last four frames the nearest height of the same style
   stands in (its cell scaled).  A failed upload is logged once and not
   tried again for that (style, height) until the textures are forgotten. */
int pad_glyphs_texture(int glyph, int pixelH, unsigned *tex, float uv[4], float *aspect);

/* ---- CPU only: tests and tools (no texture, any thread that owns them) */

/* The size piece (style, glyph) rasterises to at pixelH (1 .. 1024) rows:
   *h = pixelH, *w = the art's width at that height, rounded, at least 1.
   0, or -1 for PlayStation, an invalid piece or a bad argument. */
int pad_glyphs_raster_size(int style, int glyph, int pixelH, int *w, int *h);
/* Rasterises piece (style, glyph) at pixelH rows into rgba (w x h texels of
   pad_glyphs_raster_size, rows stride bytes apart, stride >= 4 * w): the
   atlas cell's texels exactly (straight colour, GS alpha 0 .. 0x80).
   0, or -1 as pad_glyphs_raster_size or when the outline is over the
   reader's bounds. */
int pad_glyphs_raster(int style, int glyph, int pixelH, uint8_t *rgba, int stride);
/* 1 when piece (style, glyph) parsed; its refusal (a sentence for the log)
   or NULL */
int pad_glyphs_piece_valid(int style, int glyph);
const char *pad_glyphs_piece_error(int style, int glyph);
/* Tests: piece (style, glyph) read from svg (len bytes) instead of its
   embedded file, the set's completeness and the style in force updated
   (an incomplete set in force falls back to PlayStation), that style's
   atlases dropped; svg NULL reads the embedded file again.  Returns 1 when
   the piece parsed, 0 when it was refused, -1 for a bad style or glyph. */
int pad_glyphs_test_source(int style, int glyph, const char *svg, size_t len);
/* Tests: the atlases alive now (ICO_RD; 0 without) */
int pad_glyphs_atlas_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_PAD_GLYPHS_H */
