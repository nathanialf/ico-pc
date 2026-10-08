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
   nearest size (every slot drawn within the last frames). */
int ui__FontSizeSets(int *px, int cap);
int ui__FontReusedNearest(void);

/* v0.4.2 (package F-B): sheet text, font.c's half of menu_font.c.  The
   menus' words are rasterised on the sheets' own texel grid: a texel
   column is an x unit, a texel row a field line (two y units), so an em of
   size y units is size / 2 rows and stb's horizontal scale is the
   vertical one times 2 * UI_X_PER_Y (the typeface's own proportions on the
   4:3 screen) times UI_SHEET_WIDTH (the sheets' lettering, below) times
   the caller's wx.  Coverage 0..255, stb's bitmap as it is (rd.h
   rd_CreateTextureSheet).
   ui__SheetVMetrics   ascent, descent (positive), line step and capital
                       height of an em of emRows rows, in rows
   ui__SheetLineWidth  the advance width of len bytes of utf8 (one line, no
                       '\n'), kerning included, in texel columns, the
                       horizontal scale times wx (1: the menus' width)
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
float ui__SheetLineWidth(float emRows, float wx, const char *utf8, size_t len);
void ui__SheetRasterLine(uint8_t *cov, int w, int h, int stride, float emRows, float wx, float penX,
                         float baseY, const char *utf8, size_t len);
/* The sheets' lettering is 0.8 as wide as the typeface's proportions on the
   screen.  The units are right: display_texture draws a menu row's box
   texW x units wide (dispW is 0 on the menu rows, or texW) and 2 * texH y
   units tall (dispH = 2 * texH), so a texel is an x unit by a field line
   on the 4:3 screen as the scales above have it.  But the sheets were
   lettered for a texel a pixel of the GS's 512-wide frame (1.25 x units):
   gif_SpriteSensitiveOffset maps the 640-unit grid onto 512 pixels and so
   shows them at 512 / 640 of their width.  ctest menu_look's fit
   (ICO_MENU_LOOK_GEOFIT) found the lettering's width at 0.79 .. 0.87 of
   Arimo's per sheet (menu_PAL_03 / 04 0.79, 02 and the title 0.83, 01
   and scei 0.87), the rest per item (UiMenuTextItem.wx). */
#define UI_SHEET_WIDTH 0.8f
/* Tests: the lettering's width factor (UI_SHEET_WIDTH) set; returns the
   previous one */
float ui__SheetSetWidth(float k);
uint64_t ui__TextKey(const char *utf8, unsigned flags, int page);
/* Tests (menu_look): the coverage strip of a menu text item in language lang
   (UiLang), the one ui_MenuWordDraw caches, into out (w x h bytes, the
   item's w and h; no ICO_RD needed).  0, or -1 for a size that is not the
   item's. */
struct UiMenuTextItem;
int ui__MenuStripRaster(const struct UiMenuTextItem *it, int lang, uint8_t *out, int w, int h);
/* The letters' extra weight, texels across and down (menu_font.c embolden):
   the sheets' strokes are heavier than Arimo Regular's at the same size
   (the fill's amount 0.8 of the sheets' without it); ctest menu_look's fit
   over 0 .. 1.2 across and 0 .. 0.6 down put the smallest difference at
   0.3 / 0.3 (0.45 / 0.15 and 0.6 / 0 within 1 %). */
#define UI_MENU_BOLD_X 0.3f
#define UI_MENU_BOLD_Y 0.3f
/* Tests: the letters' extra weight across and down (UI_MENU_BOLD_X / Y) set */
void ui__MenuSetBold(float bx, float by);
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
