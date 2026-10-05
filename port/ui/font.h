/*
 * port/ui/font.h
 *
 * The port's own text (Phase 6, package 6B; docs/port/UI.md): Arimo
 * Regular (SIL OFL 1.1, port/assets/fonts/; 6C replaced EB Garamond), embedded in the program,
 * rasterised at run time by stb_truetype into glyph atlases and drawn
 * through rd as GS sprites in UI space.
 *
 * Coordinates: the layout grid.  layout_texture.c places its rows on a
 * 640-pixel-wide, 226-field-line-high screen centred on (320, 113)
 * ((dispX - 320) * 16, (dispY - 113) * 16), and gif_SpriteSensitiveOffset
 * maps 640 pixels to ScreenWidth and 224 field lines to ScreenHeight.  The
 * text API works on that grid with half field lines vertically, so both
 * axes count 4:3 "pixels" of a 640 x 448 frame:
 *   x  0 .. 640, left to right (dispX)
 *   y  0 .. 452, top to bottom (2 * dispY), centre (320, 226)
 * Sizes (the em, the font's pixel size) are in y units.  A y unit is
 * 15/14 of an x unit on screen (640 x 448 shown at 4:3), which the glyph
 * placement corrects, so glyphs keep the typeface's proportions.
 *
 * Atlases: one set per rasterised pixel size, built on demand.  The pixel
 * size is round(size * scale): scale 1 in the Original preset (one atlas
 * pixel per y unit), output height / 448 in Enhanced (ui_ScaleFor).  Each
 * page is 512 x 512, kept as R8 coverage on the CPU and uploaded as an
 * RGBA8 texture (white, alpha = coverage in GS units, 0x80 = 1.0) so the
 * game's sprite path draws it: rd has no R8 texture entry point and no way
 * to select font.hlsl (docs/port/UI.md, "Requested rd API").
 *
 * Drawing: ui_DrawText records GS sprites with uvFixed texels into the
 * current rd list (the caller selects it), RD_SPACE_UI, MODULATE with TCC
 * RGBA, the standard sprite blend ALPHA 0x44 ((Cs - Cd) * As + Cd).  The
 * colour is a GS colour: 0x80 is 1.0 (the atlas is white), as for any
 * textured layout sprite.  Built without ICO_RD (the headless build) the
 * draw calls only measure.
 */
#ifndef PORT_UI_FONT_H
#define PORT_UI_FONT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the layout grid (above) */
#define UI_GRID_W 640.0f
#define UI_GRID_H 452.0f
#define UI_GRID_CX 320.0f
#define UI_GRID_CY 226.0f
/* x units per y unit: 640 x 448 shown at 4:3 */
#define UI_X_PER_Y (15.0f / 14.0f)

/* The default size of a Settings row, in y units: the em that puts Arimo's
   capitals (1409 / 2048 = 0.688 em) at the height of the menu lettering of
   the Options and vibration screens (texFile 21, 20-texel rows shown 20
   field lines tall): about 18.7 y units, measured on the title frame of a
   run (docs/port/UI.md, "Coordinates and metrics"). 27 gives 18.6. */
#define UI_MENU_TEXT_SIZE 27.0f

/* the Z the layout draws its rows at (layout_texture.c) */
#define UI_LAYOUT_Z 0xFFFFFF9Bu

enum {
    UI_ALIGN_LEFT = 0,
    UI_ALIGN_CENTER = 1,
    UI_ALIGN_RIGHT = 2,
    UI_ALIGN_MASK = 3,
    UI_VALIGN_TOP = 0,      /* y is the top of the line (ascent above the baseline) */
    UI_VALIGN_MIDDLE = 4,   /* y is the middle of the capitals */
    UI_VALIGN_BASELINE = 8, /* y is the baseline */
    UI_VALIGN_MASK = 12,
    /* blend Cs * As + Cd (ALPHA 0x48), the layout's glow, instead of 0x44 */
    UI_ADDITIVE = 16,
    /* record only the texture, the sampler and the sprites: the blend, the
       tests and Z write stay as the caller left them (the layout hook, whose
       packet already holds the game's state) */
    UI_KEEP_STATE = 32,
    /* first a soft dark halo (eight offset copies, black, a quarter of the
       alpha), as the game's menu textures carry one around their letters */
    UI_HALO = 64
};

/* Where the GS window is: the game's ScreenWidth, ScreenHeight, center_X,
   center_Y (GsBase.c) and the Z of the sprites.  Defaults: 512, 512, 2048,
   2048, UI_LAYOUT_Z. */
typedef struct UiGsFrame {
    int screenW, screenH;
    float centerX, centerY;
    uint32_t z;
} UiGsFrame;

/* An affine map of the layout grid applied to the glyph quads after layout:
   p' = (p - origin) * scale + origin + offset.  The layout's glow stretches
   a row's box this way. */
typedef struct UiXform {
    float originX, originY;
    float scaleX, scaleY;
    float offsetX, offsetY;
} UiXform;

/* Parses the embedded font; false if it is unusable (logged).  Idempotent. */
bool ui_FontInit(void);
/* Drops the atlases (their rd textures too, when rd is up) and the font. */
void ui_FontShutdown(void);
/* rd was shut down and started again without ui_FontShutdown (tests): the
   texture ids are stale, forget them; the CPU atlases are re-uploaded. */
void ui_FontForgetTextures(void);

void ui_SetGsFrame(const UiGsFrame *f);
const UiGsFrame *ui_GetGsFrame(void);
/* atlas pixels per y unit; 1 by default */
void ui_SetScale(float scale);
float ui_GetScale(void);
/* the scale a preset wants: 1 for Original (preset 0), else outputHeight /
   448, at least 1 */
float ui_ScaleFor(int preset, uint32_t outputHeight);

/* line metrics of size, in y units: ascent above the baseline (positive),
   descent below it (positive), the capitals' height */
void ui_FontMetrics(float size, float *ascent, float *descent, float *capHeight);
/* the advance width of utf8 at size, in x units, with kerning */
float ui_MeasureText(float size, const char *utf8);
/* draws utf8 with its reference point at (x, y): the flags pick the
   horizontal and vertical alignment and the blend */
void ui_DrawText(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                 unsigned flags);
/* the same, the quads mapped through xf (NULL: identity) */
void ui_DrawTextXf(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                   unsigned flags, const UiXform *xf);
/* one untextured sprite (the popup panel), x0, y0, x1, y1 in the grid; the
   blend is 0x44 with the state set as ui_DrawText sets it */
void ui_DrawRect(float x0, float y0, float x1, float y1, const uint8_t rgba[4]);
/* Renderer R7d (docs/port/RENDER_API.md "Frame rate and interpolation", "Keys"): the owner the
   next draws are keyed by, so the presenter blends them between two ticks.
   A string's draw is keyed by a hash of the string, its alignment flags,
   its atlas page and the owner (0: the string alone); the n-th draw of one
   key matches the n-th of the frame before, and the glyphs within a draw
   match in order.  A rect is keyed only under an owner (owner and the rect
   ordinal; 0: unkeyed, as before).  Returns the previous owner. */
uint64_t ui_SetDrawKey(uint64_t owner);

/* The next code point of a UTF-8 string, advancing *s; U+FFFD for a
   malformed or overlong sequence or a surrogate (one byte consumed), 0 at
   the terminator (not advanced). */
uint32_t ui_Utf8Next(const char **s);

/* ------------------------------------------------------ introspection (tests) */
typedef struct UiGlyph {
    int page;         /* atlas page */
    int x, y, w, h;   /* the bitmap in the page, texels (0 x 0 for a blank glyph) */
    float xoff, yoff; /* bitmap origin from the pen, atlas pixels (y down) */
    float advance;    /* atlas pixels */
} UiGlyph;

/* the glyph of cp at the integer pixel size px, rasterised if new */
bool ui_FontGlyph(uint32_t cp, int px, UiGlyph *out);
/* kerning between two code points at px, atlas pixels */
float ui_FontKern(uint32_t a, uint32_t b, int px);
/* the coverage of a page (R8, w x h), NULL when it does not exist */
const uint8_t *ui_FontPage(int px, int page, int *w, int *h);
/* the rd texture id of a page, 0 when none was created */
uint32_t ui_FontPageTex(int px, int page);
/* whether the font maps cp to a glyph (not .notdef) */
bool ui_FontHasGlyph(uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_FONT_H */
