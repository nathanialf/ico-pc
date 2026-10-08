/*
 * port/ui/menu_font.h
 *
 * The menus' text in the look of the game's menu sheets (v0.4.2, package
 * F-B).  Every word a menu shows, the game's own (the rows of the menu text
 * table, menu_text.h) and the port's (its layout rows, popups and the photo
 * panel), is Arimo rasterised on the sheets' own texel grid and drawn with
 * rd's sheet shader (rd.h rd_CreateTextureSheet: a rim around the letters,
 * the antialiasing quantised to a few levels against a fixed Bayer
 * threshold), so it reads like the sheets did in every preset and scale.
 *
 * The recipe.  A sheet texel is one x unit wide and one field line (two y
 * units) tall (font.h's grid); the menu rows' em is 13.5 texels, 27 y units
 * (UI_MENU_TEXT_SIZE).  A text of em `size` y units is rasterised with
 * stb's vertical scale for an em of size / 2 texel rows and a horizontal
 * scale 2 * UI_X_PER_Y times that (font.c ui__SheetRasterLine), whatever the
 * output's resolution: the strip is the same 1x coverage in the scene list
 * (scaled by the replay as the sheets were) and on the presentation overlay
 * (magnified onto the output).
 *
 * Strips.  A drawn text is one coverage strip in a cache of 1024 x 1024
 * pages, shelf packed with 4 texels between strips and from the edges, a
 * page set per style (the light ink's rim, the plain inks without it); a
 * set whose pages are full drops its page drawn least recently once no
 * frame of the last EVICT_FRAMES can still name it.  A game row's strip is
 * its item's rectangle (it->w x it->h texels) with the item's words where
 * the sheet has its lettering (the anchor it->x and it->align, the first
 * line's capital middle it->y[lang], the line pitch); drawn with the
 * sprite's own box and texel rectangle relative to the item's, it lands
 * where the sheet's texels would have, insets and half-texel offsets
 * included.  A port text's strip is its measured lines plus a margin for
 * the rim, its anchor snapped to whole texels.
 *
 * Inks (menu_text.h UiMenuTextInk).  UI_INK_LIGHT: the rim on, its level
 * and the fill's per language (ui_MenuSheetInk, the table kSheetInk in
 * menu_font.c); a language change restyles the pages, nothing is
 * rasterised again.  UI_INK_DARK, PLAIN and GREY: no rim, a white fill,
 * the colour from the vertex colour (DARK black and never in the glow,
 * GREY 151 / 255 of the row colour).
 */
#ifndef PORT_UI_MENU_FONT_H
#define PORT_UI_MENU_FONT_H

#include <stdint.h>

#include "font.h"
#include "menu_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The margin round a port text's ink, texels: the rim's reach (rd's
   ICO_SHEET_RX and ICO_SHEET_RY) plus the bilinear read's texel. */
#define UI_MENU_RIM_X 7
#define UI_MENU_RIM_Y 5

/* A sheet style, rd.h RdSheetStyle's fields (rd.h is not included here:
   the headless build has no rd). */
typedef struct UiSheetInk {
    uint8_t rimOn, rimLevel, fillLevel, dither;
} UiSheetInk;

/* The light ink's style in language lang (UiLang; out of range: English):
   kSheetInk, menu_font.c, the one place the levels live. */
const UiSheetInk *ui_MenuSheetInk(int lang);

/* the widest line of utf8 at em size (y units), in x units; and the line
   metrics in y units (ascent and descent positive, the capitals' height),
   as the strips lay text out */
float ui_MeasureMenuText(float size, const char *utf8);
void ui_MenuFontMetrics(float size, float *ascent, float *descent, float *capHeight);

/* utf8 at (x, y) on the grid, em size (y units), in ink (UiMenuTextInk),
   colour rgba (GS, 0x80 = 1.0), the flags font.h's (alignment, vertical
   alignment, UI_ADDITIVE, UI_KEEP_STATE), the quad mapped through xf (NULL:
   identity).  Into the current rd list (the scene), keyed as font.c keys
   text (ui_SetDrawKey), or, between ui_BeginOverlay and ui_EndOverlay, on
   the output, the 1x strip magnified.  Without ICO_RD nothing is drawn. */
void ui_DrawMenuText(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                     unsigned flags, int ink, const UiXform *xf);

/* A game row's words where its sprite would be: it the row's item, lang
   its language (UiLang), box and uv the sprite's rectangle (x, y, w, h:
   1/16 pixel and 1/16 field line from the screen centre) and texel
   rectangle (1/16 texel) as the caller hands them to
   gif_SpriteSensitiveOffset, rgba the sprite colour.  glow != 0: the glow
   sprite (lt_glow_sprite: the stretched box, the packet's additive blend).
   The state is the packet's (UI_KEEP_STATE); the caller sets the draw key. */
void ui_MenuWordDraw(const UiMenuTextItem *it, int lang, const int box[4], const int uv[4],
                     const unsigned char rgba[4], int glow);

/* ------------------------------------------------------ introspection (tests) */
typedef struct UiMenuStrip {
    int cls;                /* 0 the light ink's pages, 1 the plain inks' */
    int page, x, y, w, h;   /* where the strip is, texels */
    uint32_t tex;           /* the page's rd texture (0 until drawn) */
    float anchorX, anchorY; /* a port text's anchor as drawn (snapped to whole
                               texels), a game row's sprite box corner; grid */
} UiMenuStrip;

/* the strip the last draw used; 0 when nothing was drawn yet */
int ui_MenuFontLastStrip(UiMenuStrip *out);
/* whether tex is one of the pages' textures */
int ui_MenuFontIsPage(uint32_t tex);
/* a page's coverage (w x h bytes), NULL when it does not exist */
const uint8_t *ui_MenuFontPage(int cls, int page, int *w, int *h);
/* the strips cached now */
int ui_MenuFontStripCount(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_MENU_FONT_H */
