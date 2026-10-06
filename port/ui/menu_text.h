/*
 * port/ui/menu_text.h
 *
 * The game's menu text rows drawn with the port font (package P3;
 * docs/port/UI.md, "Menu text").
 *
 * The PS2 game draws every menu word as a sprite from a pre-rendered sheet
 * (text/menu_PAL_{EG,FR,GR,IT,SP}/menu_PAL_0N.tm2, scei.tm2, title.tm2): a
 * texProperty row names the sheet and the texel rectangle, the layout code
 * (layout_texture.c, kanban.c) places it.  This module knows, for each row
 * whose rectangle holds text, the string (strings.h, UI_STR_MT_*, the five
 * sheets transcribed) and where the lettering sits in the rectangle (the
 * capitals' size, the anchor, the line pitch), measured from the sheets.
 * The draw hooks then lay the string out with Arimo in place of the sprite,
 * with the colour, fade, dimming and glow the game computed for the row.
 *
 * On by default; [game] classic_menu_text = true (Settings > Display, "Menu
 * text") draws the textures again (ui_MenuTextSetClassic).  The texture is
 * transferred either way (tex_TransTexture), so VRAM and the packets are
 * the texture path's; only the sprite is replaced.
 */
#ifndef PORT_UI_MENU_TEXT_H
#define PORT_UI_MENU_TEXT_H

#include "layout_texture.h" /* LtProperty, texProperty */

#ifdef __cplusplus
extern "C" {
#endif

/* The letters' fill on the sheet. */
typedef enum UiMenuTextInk {
    UI_INK_LIGHT = 0, /* light letters with the dark rim (the menu rows); drawn in the
                         sprite's colour with UI_HALO */
    UI_INK_DARK = 1,  /* black letters without a rim (the white panel's prompts, the
                         save screens' slot numbers of a used file); black, no glow */
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

/* A texProperty row drawn from an item. */
typedef struct UiMenuTextRow {
    short row;  /* the texProperty index */
    short item; /* into ui_menu_text_items */
} UiMenuTextRow;

extern const UiMenuTextItem ui_menu_text_items[];
extern const int ui_menu_text_item_count;
extern const UiMenuTextRow ui_menu_text_rows[];
extern const int ui_menu_text_row_count;

/* [game] classic_menu_text: 1 draws the textures (the PS2's look). */
void ui_MenuTextSetClassic(int on);
int ui_MenuTextClassic(void);

/* The item game row e is drawn from now: NULL in classic mode, for a row
   not in the table, and for a row whose texel rectangle is not the one the
   table was measured on (tables that are not the PAL ones). */
const UiMenuTextItem *ui_MenuTextItemOf(const LtProperty *e);

/* The draw hook: e's string where the texture sprite would be.  box is the
   sprite's rectangle (x, y, w, h: 1/16 pixel, 1/16 field line from the
   screen centre), uv its texel rectangle (1/16 texel: x, y, w, h) as the
   caller passes them to gif_SpriteSensitiveOffset, rgba the sprite colour
   (GS, 0x80 = 1.0).  glow != 0 is lt_glow_sprite's stretched copy (the
   packet's blend is additive): the text is mapped through the stretch from
   the row's last plain box. */
void ui_MenuTextDraw(const LtProperty *e, const int box[4], const int uv[4],
                     const unsigned char rgba[4], int glow);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_MENU_TEXT_H */
