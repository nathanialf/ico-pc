/*
 * port/ui/menu_text.h
 *
 * The table of the game's menu words (package P3; drawn again since
 * v0.4.2, package F-B).
 *
 * The PS2 game draws every menu word as a sprite from a pre-rendered sheet
 * (text/menu_PAL_{EG,FR,GR,IT,SP}/menu_PAL_0N.tm2, scei.tm2, title.tm2): a
 * texProperty row names the sheet and the texel rectangle, the layout code
 * (layout_texture.c, kanban.c) places it.  This table knows, for each row
 * whose rectangle holds text, the string (strings.h, UI_STR_MT_*, the five
 * sheets transcribed) and where the lettering sits in the rectangle (the
 * capitals' size, the anchor, the line pitch), measured from the sheets.
 * The draw hooks (layout_ext.h lt_ext_DrawTextRow) draw such a row's words
 * with Arimo in the sheets' look in place of the sprite (menu_font.h
 * ui_MenuWordDraw), with the colour, fade, dimming and glow the game
 * computed for the row; the texture is still transferred, so VRAM and the
 * packets are the texture path's.  Rows the table leaves out (artwork,
 * logos, the copyright, button glyphs, arrows, L1..R2, the subtitles) keep
 * their texels.
 */
#ifndef PORT_UI_MENU_TEXT_H
#define PORT_UI_MENU_TEXT_H

#include "layout_texture.h" /* LtProperty, texProperty */

#ifdef __cplusplus
extern "C" {
#endif

/* The letters' fill on the sheet. */
typedef enum UiMenuTextInk {
    UI_INK_LIGHT = 0, /* light letters with the dark rim (the menu rows): menu_font.c's
                         rim and the language's levels (ui_MenuSheetInk) */
    UI_INK_DARK = 1,  /* black letters without a rim (the white panel's prompts, the
                         save screens' slot numbers of a used file); no glow */
    UI_INK_PLAIN = 2, /* white letters without a rim (the save preview's play time) */
    UI_INK_GREY = 3   /* grey letters without a rim, 151 / 255 of white (the slot
                         numbers of an empty file) */
} UiMenuTextInk;

/* One text rectangle of the sheets.  Texel coordinates are measured from
   the rectangle's top-left corner, at texel edges (0 .. w, 0 .. h). */
typedef struct UiMenuTextItem {
    unsigned short u, v, w, h; /* the texel rectangle, as the PAL rows have it */
    int str;                   /* UiStrId: the text, lines split at '\n' */
    unsigned char align;       /* UI_ALIGN_LEFT / CENTER / RIGHT about x */
    unsigned char ink;         /* UiMenuTextInk: how the sheet colours the letters */
    float em[5];               /* the em, texels (vertical), per language (UiLang) */
    float x[5];                /* the anchor: the lettering's left edge, centre or right
                                  edge, texels, per language */
    float pitch;               /* texels from one line's capitals to the next's */
    float y[5];                /* the first line's capital middle, texels, per language
                                  (UiLang: EN FR DE IT ES; the sheets differ in lines) */
    float wx[5];               /* the lettering's width against the menus' (UI_SHEET_WIDTH),
                                  per language: the five sheets were lettered apart and
                                  their faces differ a little in size and width */
    unsigned char rim[5];      /* the halo the light ink has on the sheet, per language:
                                  UI_RIM_NONE, UI_RIM_FAINT, UI_RIM_FULL (menu_font.h) */
} UiMenuTextItem;

/* A texProperty row that draws an item's rectangle. */
typedef struct UiMenuTextRow {
    short row;  /* the texProperty index */
    short item; /* into ui_menu_text_items */
} UiMenuTextRow;

extern const UiMenuTextItem ui_menu_text_items[];
extern const int ui_menu_text_item_count;
extern const UiMenuTextRow ui_menu_text_rows[];
extern const int ui_menu_text_row_count;

/* The item game row e is drawn from: NULL for a row not in the table (or
   not a game row) and for a row whose texel rectangle is not the one the
   table was measured on (tables that are not the PAL ones), which then
   draws its texture. */
const UiMenuTextItem *ui_MenuTextItemOf(const LtProperty *e);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_MENU_TEXT_H */
