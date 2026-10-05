/*
 * port/ui/layout_ext.h
 *
 * Port-owned rows in the game's layout system (Phase 6, 6B;
 * docs/port/UI.md, "Layout extension").
 *
 * The game's tables are runtime-loaded arrays of fixed size
 * (port/data/gen/table_defs.c): texLayout[80] and texProperty[436].  Every
 * texProperty row is used (434 and 435 are the subtitle rows jimaku.c
 * writes), and texLayout rows 66..79 are empty in the data
 * ({434, 434, ...}) but sit inside every stage's layout range (stageData
 * layoutFirst..layoutLast is 1 or 6 .. 80), so kanban.c and
 * layout_texture.c initialise the textures of their property ranges at
 * each stage load: a port row there would be looked up as a texture file.
 * So both tables are extended past their ends instead:
 *
 *   layouts    80 .. 80 + LT_EXT_MAX_LAYOUTS - 1
 *   properties 436 .. 436 + LT_EXT_MAX_PROPERTIES - 1
 *
 * held here.  layout_texture.c reads both tables through LT_LAYOUT(i) and
 * LT_PROP(i) under ICO_HOST, which are lt_ext_Layout(i) and lt_ext_Prop(i):
 * the game's row below the game's count, the port's row above it.  No
 * stage range reaches an extension index, so no texture is ever looked up
 * for a port row.  A port row draws its label with the port font where the
 * texture path would draw the texture (lt_ext_DrawRow, called from
 * display_texture), with the colour, fade, dimming, cursor sparkle and glow
 * the game computes for any row.
 *
 * Link fields (up/down/left/right, the item links, link) may name game or
 * port indices freely: they are only ever used as table indices.
 */
#ifndef PORT_UI_LAYOUT_EXT_H
#define PORT_UI_LAYOUT_EXT_H

#include "layout_texture.h" /* LtProp, LtProperty, texLayout, texProperty */

#ifdef __cplusplus
extern "C" {
#endif

#define LT_GAME_LAYOUT_COUNT 80
#define LT_GAME_PROPERTY_COUNT 436
#define LT_EXT_MAX_LAYOUTS 32
#define LT_EXT_MAX_PROPERTIES 256

/* The label of a port row. */
typedef struct LtExtText {
    int strId;        /* a UiStrId (strings.h), used when text is NULL */
    const char *text; /* a literal UTF-8 label (copied), or NULL */
    float size;       /* the em in y units (font.h); 0: UI_MENU_TEXT_SIZE */
    int align;        /* UI_ALIGN_LEFT/CENTER/RIGHT in the row's box */
} LtExtText;

/* Appends a layout; returns its index (>= LT_GAME_LAYOUT_COUNT), or -1 when
   the extension is full.  first/last name port property indices. */
int lt_ext_AddLayout(const LtProp *layout);
/* Appends a property row with its label; returns its index
   (>= LT_GAME_PROPERTY_COUNT), or -1.  A row with neither dispW nor texW
   gets dispW 400, one with neither dispH nor texH dispH 40 (the Options
   screen's row height: 20 field lines). */
int lt_ext_AddProperty(const LtProperty *row, const LtExtText *text);
/* Changes a port row's label (a value that changes, a language switch);
   -1 if index is not a port row. */
int lt_ext_SetText(int index, const char *utf8);
int lt_ext_SetStr(int index, int strId);
/* the label as it would be drawn now */
const char *lt_ext_RowText(int index);

/* The table lookups layout_texture.c makes through LT_LAYOUT/LT_PROP: the
   game's row for an index below its count (or any index outside both
   ranges, as the plain array access would), else the port's. */
LtProp *lt_ext_Layout(int index);
LtProperty *lt_ext_Prop(int index);
/* whether e is a port row, and its index */
int lt_ext_IsPortProp(const LtProperty *e);
int lt_ext_PropIndex(const LtProperty *e);
int lt_ext_LayoutCount(void);
int lt_ext_PropCount(void);
/* drops every port row and layout (tests, a Settings rebuild) */
void lt_ext_Reset(void);

/* display_texture's hook: draws port row e's label into the open packet's
   list with the state the game set (UI_KEEP_STATE).  box is the SprRect the
   texture sprite would have had (x, y, w, h in 1/16 pixel / 1/16 field
   line from the screen centre, after display_texture's inset), rgba the
   sprite colour (GS, 0x80 = 1.0).  glow != 0 is lt_glow_sprite's stretched
   copy: box is the stretched rectangle, mapped from the row's last plain
   box. */
void lt_ext_DrawRow(const LtProperty *e, const int box[4], const unsigned char rgba[4], int glow);

/* P3 (docs/port/UI.md, "Menu text"): whether display_texture draws e as
   text: a port row, or a game row the menu text table holds while classic
   menu text is off (menu_text.h ui_MenuTextItemOf). */
int lt_ext_IsTextRow(const LtProperty *e);
/* The text hook of display_texture, lt_glow_sprite and kanban.c's
   display_texture: a port row's label (lt_ext_DrawRow) or a game row's menu
   text (ui_MenuTextDraw); uv is the sprite's texel rectangle (1/16 texel). */
void lt_ext_DrawTextRow(const LtProperty *e, const int box[4], const int uv[4],
                        const unsigned char rgba[4], int glow);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_LAYOUT_EXT_H */
