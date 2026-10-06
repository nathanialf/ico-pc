/*
 * port/ui/menu_text.h
 *
 * The table of the game's menu words (package P3, kept for package GFONT).
 *
 * The PS2 game draws every menu word as a sprite from a pre-rendered sheet
 * (text/menu_PAL_{EG,FR,GR,IT,SP}/menu_PAL_0N.tm2, scei.tm2, title.tm2): a
 * texProperty row names the sheet and the texel rectangle, the layout code
 * (layout_texture.c, kanban.c) places it.  The port draws those sprites as
 * the game does, always.  This table knows, for each row whose rectangle
 * holds text, the string (strings.h, UI_STR_MT_*, the five sheets
 * transcribed) and where the lettering sits in the rectangle (the capitals'
 * size, the anchor, the line pitch), measured from the sheets: the game
 * face's builder (game_font_disc.c, game_font_build.c) cuts the letters of
 * the port's own text from those rectangles by matching them to the
 * strings.
 */
#ifndef PORT_UI_MENU_TEXT_H
#define PORT_UI_MENU_TEXT_H

#include "layout_texture.h" /* LtProperty, texProperty */

#ifdef __cplusplus
extern "C" {
#endif

/* The letters' fill on the sheet. */
typedef enum UiMenuTextInk {
    UI_INK_LIGHT = 0, /* light letters with the dark rim (the menu rows) */
    UI_INK_DARK = 1,  /* black letters without a rim (the white panel's prompts, the
                         save screens' slot numbers of a used file) */
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
    float em;                  /* the em, texels (vertical) */
    float x;                   /* the anchor: the lettering's left edge, centre or right
                                  edge, texels */
    float pitch;               /* texels from one line's capitals to the next's */
    float y[5];                /* the first line's capital middle, texels, per language
                                  (UiLang: EN FR DE IT ES; the sheets differ in lines) */
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

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_MENU_TEXT_H */
