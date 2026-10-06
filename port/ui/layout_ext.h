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
/* Changes a port label's em (a line of button prompts set smaller to fit);
   -1 if index is not a port row. */
int lt_ext_SetSize(int index, float size);
/* Greys a port row (its colour at half, whatever the cursor does): the
   "locked" style of Settings > Extras.  Cleared by lt_ext_Reset. */
int lt_ext_SetDim(int index, int dim);
int lt_ext_RowDim(int index);
/* the label as it would be drawn now */
const char *lt_ext_RowText(int index);
/* the label's size before any shrink to fit (UI_MENU_TEXT_SIZE for 0), 0
   if index is not a port row */
float lt_ext_RowSize(int index);

/* The game's button glyphs on a port row (docs/port/UI.md, "Button
   glyphs").  A glyph row draws one of the game's own sprites, the texture
   path of display_texture unchanged: the texture is the one a game row of
   the PAL tables draws the glyph from (its texNo, which the stage's texture
   set-up fills for every stage, since every stage's layout range covers
   the menus), the texel rectangle that row's.  The face buttons are
   text/buttons.tm2's four (the save prompts' OK and Back, the key config
   screen's columns), L1 and R1 the key config screen's labels
   (menu_PAL_02), Left and Right the Options values' arrows (menu_PAL_01).
   When the loaded tables are not the PAL ones (the row's rectangle
   differs) the glyph draws nothing. */
typedef enum LtExtGlyph {
    LT_GLYPH_CROSS = 0,
    LT_GLYPH_CIRCLE,
    LT_GLYPH_SQUARE,
    LT_GLYPH_TRIANGLE,
    LT_GLYPH_L1,
    LT_GLYPH_R1,
    LT_GLYPH_LEFT,
    LT_GLYPH_RIGHT,
    LT_GLYPH_COUNT
} LtExtGlyph;

/* Appends a glyph row at (x, y) (dispX, dispY: its box's left and top) at
   the size it has beside a label of em `size` (the game's own pairs, a
   27-unit label: 32 x 30 for a face button); -1 when full. */
int lt_ext_AddGlyph(int glyph, int x, int y, float size);
/* The box (dispW pixels, dispH y units) of a glyph beside a label of em
   size, without adding it (layout). */
void lt_ext_GlyphBox(int glyph, float size, int *w, int *h);
/* whether e is a glyph row; the texture it draws (texProperty's texNo of
   its game row), -1 when the tables are not the PAL ones */
int lt_ext_IsGlyphRow(const LtProperty *e);
int lt_ext_GlyphTexNo(const LtProperty *e);
/* The PAL texProperty row a glyph is drawn from and its texel rectangle
   (u, v, w, h), for tests that build fake tables. */
int lt_ext_GlyphSource(int glyph, int uvwh[4]);

/* A filled rectangle on a port row (the music gallery's progress bar): the
   row's box (dispX, dispY, dispW, dispH), the left `fill` of it (0..1,
   lt_ext_SetFill; 1 when added), in rgba (GS, 0x80 = 1.0) times the row's
   colour (its fade), drawn where a label would be (no glow); -1 when
   full. */
int lt_ext_AddRect(int x, int y, int w, int h, const unsigned char rgba[4]);
int lt_ext_SetFill(int index, float fill);
float lt_ext_RowFill(int index);

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

/* Whether display_texture draws e as text: a port row (a glyph row only
   when it has no texture: it then draws nothing).  The game's own rows are
   never text: they draw their textures, the PS2's lettering
   (docs/port/UI.md, "Menu text"). */
int lt_ext_IsTextRow(const LtProperty *e);

/* Q2 (docs/port/SETTINGS.md, "Circle goes back"): the pad bits that take
   the game menus' back action, where the game checks Triangle for it
   (default_item_select's left link in layout_texture.c, the la_* procs'
   cancels in layout_action.c, both under ICO_HOST).  Triangle (0x10), plus
   Circle (0x20) while the alias is on ([game] circle_back, default on; the
   Settings module sets it from port/game/options.h at install and on a
   change).  Off, it is 0x10 alone: the PS2's checks exactly. */
#define LT_PAD_TRIANGLE 0x0010
#define LT_PAD_CIRCLE 0x0020
int lt_ext_BackButtons(void);
void lt_ext_SetCircleBack(int on);
int lt_ext_CircleBack(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_LAYOUT_EXT_H */
