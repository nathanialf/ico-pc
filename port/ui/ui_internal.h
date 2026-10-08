/*
 * port/ui/ui_internal.h
 *
 * What port/ui's files share and the tests reach.
 */
#ifndef PORT_UI_INTERNAL_H
#define PORT_UI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef ICO_RD
#include "rd.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Called before every rd recording font.c makes.  The game build sets it to
   GifPacket.c's gif_HostFlush (ui_host.c): the register decoder emits what
   it still holds into the current list and forgets the state it emitted,
   so its next primitive re-sends PRIM, TEX0 and the rest after the text's
   own rd_Texture and rd_ABE (the pattern DisplayFont.c's host path uses). */
void ui__SetRecordHook(void (*fn)(void));
/* runs the record hook now (a caller about to switch rd lists flushes the
   decoder into the list it was recording first) */
void ui__RunRecordHook(void);
/* while > 0, font.c does not run the record hook (the caller ran it) */
void ui__SuppressRecordHook(int delta);
/* Called before text is laid out for the game: the game build syncs the GS
   frame (ScreenWidth, center_X...), the language and the scale from the game
   and rd (ui_host.c). */
void ui__SetSyncHook(void (*fn)(void));
void ui__Sync(void);

/* Tests: the pixel sizes of the Arimo size sets alive now (up to cap
   written), returns their count; whether a draw has had to reuse the
   nearest size (every slot drawn within the last frames); the pixel size
   of the Arimo fallback letters at a game-face size (0 without the game
   face). */
int ui__FontSizeSets(int *px, int cap);
int ui__FontReusedNearest(void);
int ui__FontFallbackPx(float size);

/* v0.4.2 (package F-B): sheet text, font.c's half of menu_font.c.  The
   menus' words are rasterised on the sheets' own texel grid: a texel
   column is an x unit, a texel row a field line (two y units), so an em of
   size y units is size / 2 rows and stb's horizontal scale is the
   vertical one times 2 * UI_X_PER_Y.  Coverage 0..255, stb's bitmap as it
   is (rd.h rd_CreateTextureSheet).
   ui__SheetVMetrics   ascent, descent (positive), line step and capital
                       height of an em of emRows rows, in rows
   ui__SheetLineWidth  the advance width of len bytes of utf8 (one line, no
                       '\n'), kerning included, in texel columns
   ui__SheetRasterLine those bytes rasterised into cov (w x h, rows stride
                       bytes apart), the pen at (penX, baseY) in texels,
                       fractional: each glyph at its fractional pen
                       (stbtt_MakeGlyphBitmapSubpixel), overlaps keeping
                       the larger coverage, clipped to the strip
   ui__TextKey         font.c's draw key of a string (font.h ui_SetDrawKey)
   ui__DrawTexQuads    n textured sprites (grid rectangles, texel UVs) of
                       texture tex, drawn as font.c draws text: into the
                       current rd list at the layout's Z with its state
                       (flags: blend, UI_KEEP_STATE), or in overlay mode on
                       the output, magnified and continuous
   ui__SetMenuFontHooks  what ui_FontShutdown and ui_FontForgetTextures
                       also run for menu_font.c's pages (menu_font.c sets
                       them when it makes its first page; font.c is built
                       without it in several tests and tools) */
void ui__SheetVMetrics(float emRows, float *ascent, float *descent, float *lineStep, float *cap);
float ui__SheetLineWidth(float emRows, const char *utf8, size_t len);
void ui__SheetRasterLine(uint8_t *cov, int w, int h, int stride, float emRows, float penX,
                         float baseY, const char *utf8, size_t len);
uint64_t ui__TextKey(const char *utf8, unsigned flags, int page);
void ui__SetMenuFontHooks(void (*shutdown)(void), void (*forget)(void));

#ifdef ICO_RD
/* Tests: in overlay mode (font.h ui_BeginOverlay) the prims go to fn
   instead of rd_OverlayPrims, so a test can see them outside a present.
   NULL: rd_OverlayPrims. */
typedef void (*UiOverlaySink)(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex,
                              RdBlend blend);
void ui__SetOverlaySink(UiOverlaySink fn);

typedef struct UiTexQuad {
    float x0, y0, x1, y1; /* the layout grid (font.h) */
    float u0, v0, u1, v1; /* texels */
} UiTexQuad;
struct UiXform;
void ui__DrawTexQuads(uint32_t tex, const UiTexQuad *q, int n, const uint8_t rgba[4],
                      unsigned flags, const struct UiXform *xf, uint64_t key);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_INTERNAL_H */
