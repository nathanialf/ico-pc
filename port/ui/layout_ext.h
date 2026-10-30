/*
 * port/ui/layout_ext.h
 *
 * Port-owned rows in the game's layout system.
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
 * LT_PROP(i) under ICO_HOST, which are lt_ext_layout(i) and lt_ext_prop(i):
 * the game's row below the game's count, the port's row above it.  No
 * stage range reaches an extension index, so no texture is ever looked up
 * for a port row.  A port row draws its label in the menus' look where the
 * texture path would draw the texture (lt_ext_draw_text_row, called from
 * display_texture), with the colour, fade, dimming, cursor sparkle and glow
 * the game computes for any row; so does a game row of the menu text
 * table (menu_text.h), its texture still transferred.
 *
 * Link fields (up/down/left/right, the item links, link) may name game or
 * port indices freely: they are only ever used as table indices.
 */
#ifndef PORT_UI_LAYOUT_EXT_H
#define PORT_UI_LAYOUT_EXT_H

#include <stdint.h>

#include "layout_texture.h" /* LtProp, LtProperty, texLayout, texProperty */

#ifdef __cplusplus
extern "C" {
#endif

/* The game's table sizes, the running disc's (port/core/region.h:
   texLayout[80] and texProperty[436] on the European disc, 76 and 412 on
   the Japanese one); the _MAX values are the larger, for arrays sized at
   build time. */
#define LT_GAME_LAYOUT_COUNT (lt_ext_game_layout_count())
#define LT_GAME_PROPERTY_COUNT (lt_ext_game_property_count())
#define LT_GAME_LAYOUT_COUNT_MAX 80
#define LT_GAME_PROPERTY_COUNT_MAX 436
int lt_ext_game_layout_count(void);
int lt_ext_game_property_count(void);
#define LT_EXT_MAX_LAYOUTS 32
#define LT_EXT_MAX_PROPERTIES                                                                      \
    768 /* room for every page's rows, the mouse rows and the Android driver page among them */

/* The label of a port row. */
typedef struct LtExtText {
    int strId;        /* a UiStrId (strings.h), used when text is NULL */
    const char *text; /* a literal UTF-8 label (copied), or NULL */
    float size;       /* the em in y units (font.h); 0: UI_MENU_TEXT_SIZE */
    int align;        /* UI_ALIGN_LEFT/CENTER/RIGHT in the row's box */
} LtExtText;

/* Appends a layout; returns its index (>= LT_GAME_LAYOUT_COUNT), or -1 when
   the extension is full.  first/last name port property indices. */
int lt_ext_add_layout(const LtProp *layout);
/* Appends a property row with its label; returns its index
   (>= LT_GAME_PROPERTY_COUNT), or -1 (full: "ui: layout extension full",
   logged once).  A row with neither dispW nor texW
   gets dispW 400, one with neither dispH nor texH dispH 40 (the Options
   screen's row height: 20 field lines). */
int lt_ext_add_property(const LtProperty *row, const LtExtText *text);
/* Changes a port row's label (a value that changes, a language switch);
   -1 if index is not a port row. */
int lt_ext_set_text(int index, const char *utf8);
int lt_ext_set_str(int index, int strId);
/* Changes a port label's em (a line of button prompts set smaller to fit);
   -1 if index is not a port row. */
int lt_ext_set_size(int index, float size);
/* Greys a port row (its colour at half, whatever the cursor does): the
   "locked" style of Settings > Extras.  Cleared by lt_ext_reset. */
int lt_ext_set_dim(int index, int dim);
/* whether a port row is greyed (tests) */
int lt_ext_row_dim(int index);
/* Marks a port row as one of the title screen's (settings.c: Options and
   Quit under the title's words): its label is measured and drawn in the
   title sheet's geometry (menu_font.h ui_menu_text_set_title; the Japanese
   disc's title sheet spaces its words apart more than the others).
   Cleared by lt_ext_reset; -1 if index is not a port row. */
int lt_ext_set_title_sheet(int index, int on);
/* the label as it would be drawn now (tests) */
const char *lt_ext_row_text(int index);
/* the label's size before any shrink to fit (UI_MENU_TEXT_SIZE for 0), 0
   if index is not a port row (tests) */
float lt_ext_row_size(int index);
/* the label's alignment in its box (UI_ALIGN_*), -1 if index is not a port
   row (tests) */
int lt_ext_row_align(int index);

/* The game's button glyphs on a port row.  A glyph row draws one of the
   game's own sprites, the texture path of display_texture unchanged: the
   texture is the one a game row of the PAL tables draws the glyph from (its
   texNo, which the stage's texture set-up fills for every stage, since
   every stage's layout range covers the menus), the texel rectangle that
   row's.  The face buttons are text/buttons.tm2's four (the save prompts'
   OK and Back, the key config screen's columns), L1, R1, L2 and R2 the key
   config screen's labels (menu_PAL_02), Left and Right the Options values'
   arrows (menu_PAL_01).  When the loaded tables are not the PAL ones (the
   row's rectangle differs) the glyph draws nothing.  Words are text rows
   (the title's Options word among them); glyph rows are icons. */
typedef enum LtExtGlyph {
    LT_GLYPH_CROSS = 0,
    LT_GLYPH_CIRCLE,
    LT_GLYPH_SQUARE,
    LT_GLYPH_TRIANGLE,
    LT_GLYPH_L1,
    LT_GLYPH_R1,
    LT_GLYPH_L2,
    LT_GLYPH_R2,
    LT_GLYPH_LEFT,
    LT_GLYPH_RIGHT,
    LT_GLYPH_COUNT
} LtExtGlyph;

/* Appends a glyph row at (x, y) (dispX, dispY: its box's left and top) at
   the size it has beside a label of em `size` (the game's own pairs, a
   27-unit label: 32 x 30 for a face button); -1 when full. */
int lt_ext_add_glyph(int glyph, int x, int y, float size);
/* The box (dispW pixels, dispH y units) of a glyph beside a label of em
   size, without adding it (layout). */
void lt_ext_glyph_box(int glyph, float size, int *w, int *h);
/* whether e is a glyph row (tests; layout_ext.c uses it too); the texture
   it draws (texProperty's texNo of its game row), -1 when the tables are
   not the PAL ones */
int lt_ext_is_glyph_row(const LtProperty *e);
int lt_ext_glyph_tex_no(const LtProperty *e);
/* the same for a glyph by itself (the photo panel draws the glyphs on the
   presentation overlay from that texture) */
int lt_ext_glyph_texture(int glyph);
/* The PAL texProperty row a glyph is drawn from and its texel rectangle
   (u, v, w, h): the photo panel draws from it, and the tests build fake
   tables with it. */
int lt_ext_glyph_source(int glyph, int uvwh[4]);

/* A filled rectangle on a port row (the music gallery's progress bar): the
   row's box (dispX, dispY, dispW, dispH), the left `fill` of it (0..1,
   lt_ext_set_fill; 1 when added), in rgba (GS, 0x80 = 1.0) times the row's
   colour (its fade), drawn where a label would be (no glow); -1 when
   full. */
int lt_ext_add_rect(int x, int y, int w, int h, const unsigned char rgba[4]);
int lt_ext_set_fill(int index, float fill);
/* the fill a rect row has (tests) */
float lt_ext_row_fill(int index);

/* The table lookups layout_texture.c makes through LT_LAYOUT/LT_PROP: the
   game's row for an index below its count, the port's above it; an index in
   neither (-1, the tables' "none", or past the port's last row) a zeroed
   scratch row, never memory outside the tables (logged once unless -1). */
LtProp *lt_ext_layout(int index);
LtProperty *lt_ext_prop(int index);
/* whether e is a port row, and its index (tests) */
int lt_ext_is_port_prop(const LtProperty *e);
int lt_ext_prop_index(const LtProperty *e);
int lt_ext_layout_count(void);
int lt_ext_prop_count(void);
/* drops every port row and layout (tests, a Settings rebuild) */
void lt_ext_reset(void);

/* Port row e's label into the open packet's list with the state the game
   set (UI_KEEP_STATE), in the menus' look (menu_font.h ui_draw_menu_text,
   light ink, into the scene list at 1x).  box is the SprRect the texture
   sprite would have had (x, y, w, h in 1/16 pixel / 1/16 field line from
   the screen centre, after display_texture's inset), rgba the sprite
   colour (GS, 0x80 = 1.0).  glow != 0 is lt_glow_sprite's stretched copy:
   box is the stretched rectangle, mapped from the row's last plain box. */
void lt_ext_draw_row(const LtProperty *e, const int box[4], const unsigned char rgba[4], int glow);

/* Whether display_texture (and kanban.c's) draws e as text: a port row (a
   glyph row only when it has no texture: it then draws nothing), or a game
   row of the menu text table (menu_text.h ui_menu_text_item_of: its rectangle
   the PAL one).  Every other game row draws its texture. */
int lt_ext_is_text_row(const LtProperty *e);
/* The text hook of display_texture, lt_glow_sprite and kanban.c's
   display_texture, for a row lt_ext_is_text_row says is text: a port row's
   label (lt_ext_draw_row) or a game row's words (menu_font.h
   ui_menu_word_draw, in the language the game shows), keyed by the row and
   the pass ((e << 2) ^ (glow ? 2 : 1)).  box and uv are the sprite's
   rectangle and texel rectangle (1/16 texel) as the caller would hand them
   to gif_SpriteSensitiveOffset, rgba its colour, glow the glow sprite's
   pass. */
void lt_ext_draw_text_row(const LtProperty *e, const int box[4], const int uv[4],
                          const unsigned char rgba[4], int glow);

/* The pad bits that take the game menus' back action, where the game
   checks its cancel button for it (default_item_select's left link in
   layout_texture.c, the la_* procs' cancels in layout_action.c).  On the
   European disc Triangle (0x10), plus Circle (0x20) while the alias is on
   ([game] circle_back, default on; the Settings module sets it from
   port/game/options.h at install and on a change); off, it is 0x10 alone:
   the PS2's checks exactly.  On the Japanese disc Cross (0x40), the disc's
   own cancel (region.h pad_back; Circle confirms there, so there is no
   alias and lt_ext_has_circle_back is 0).  lt_ext_circle_back reads the
   alias back (tests).

   lt_ext_confirm_button is the disc's confirm (region.h pad_confirm: Cross
   0x40, Circle 0x20 on the Japanese disc), the button the port's own
   screens confirm with and a mouse click on an item gives;
   lt_ext_confirm_glyph its picture (LT_GLYPH_CROSS or LT_GLYPH_CIRCLE);
   lt_ext_port_back_buttons the port's own screens' back buttons: Triangle
   and the one of Cross and Circle that does not confirm, whatever [game]
   circle_back says (that switch is for the game's own menus). */
#define LT_PAD_TRIANGLE 0x0010
#define LT_PAD_CIRCLE 0x0020
/* the other trigger bits of the pad word (keyInput.c's logical word) the
   port's screens read */
#define LT_PAD_L1 0x0004
#define LT_PAD_R1 0x0008
#define LT_PAD_CROSS 0x0040
#define LT_PAD_SQUARE 0x0080
#define LT_PAD_START 0x0800
#define LT_PAD_UP 0x1000
#define LT_PAD_RIGHT 0x2000
#define LT_PAD_DOWN 0x4000
#define LT_PAD_LEFT 0x8000
int lt_ext_back_buttons(void);
int lt_ext_confirm_button(void);
int lt_ext_confirm_glyph(void);
int lt_ext_port_back_buttons(void);
int lt_ext_has_circle_back(void);
void lt_ext_set_circle_back(int on);
int lt_ext_circle_back(void);

/* An image row (the save-state slots' pictures): the box (x, y, w, h) as
   lt_ext_add_rect takes it, drawn with the texture `tex` (an RdTex id) over
   its texels (0, 0)..(texW, texH), modulated by the row's colour (its fade),
   where a label would be (no glow); -1 when full.  lt_ext_set_image changes
   the texture (0, or a zero size: nothing is drawn, an empty slot or the
   headless build); -1 for an index that is not an image row.  The texture is
   the PAGE's, not the row's: the page creates it with rd_create_texture and
   destroys it with rd_destroy_texture (port/render/rd.h, the entry
   ui/font.c and ui/menu_font.c release their atlas pages with), after
   setting the row's texture to 0 or before the next draw of the row; the
   row never frees it.  lt_ext_reset drops every row.  lt_ext_row_image reads
   the texture and size back (tests; 0 for a row that is not an image). */
int lt_ext_add_image(int x, int y, int w, int h);
int lt_ext_set_image(int index, uint32_t tex, int texW, int texH);
uint32_t lt_ext_row_image(int index, int *texW, int *texH);

/* A rect row's colour changed after it was added (Settings >
   Extras > Characters' swatches): rgba as lt_ext_add_rect takes it (GS,
   0x80 = 1.0, times the row's colour when drawn).  0, or -1 for an index
   that is not a rect row.  lt_ext_rect_color reads it back (tests). */
int lt_ext_set_rect_color(int index, const unsigned char rgba[4]);
int lt_ext_rect_color(int index, unsigned char rgba[4]);

/* What a click of the mouse pointer on a port row does
   (port/ui/ui_mouse.h).  AUTO (every row to begin with, and every game
   row): an item (a row with item links, one the pad can reach, or one lit
   with an owner item) is pointed at and a click is Cross on it, on its
   owner item when it has no item links of its own; any other row is not
   hit.  NONE: never hit (a list's heading or empty slot).  LEFT / RIGHT: a value's
   arrows, a click is Left / Right on the owner item (their boxes padded so
   the small arrows are easy to hit).  STEP: a stepped value, a click is
   Right on the owner item (Cross does nothing on a stepped row).  UP / DOWN: a scroll
   mark, a click is Up / Down on the page's cursor, which does not move (the
   hit's item is -1).  SQUARE: a button prompt (the Continue screen's Save
   states line), a click is Square and the cursor does not move (item -1); hit
   in a layout with no cursor of its own whatever the current layout has.
   Cleared by lt_ext_reset; -1 for an index that is not a port row. */
enum {
    LT_POINTER_AUTO = 0,
    LT_POINTER_NONE,
    LT_POINTER_LEFT,
    LT_POINTER_RIGHT,
    LT_POINTER_STEP,
    LT_POINTER_UP,
    LT_POINTER_DOWN,
    LT_POINTER_SQUARE
};

int lt_ext_set_pointer_role(int index, int role);
/* LT_POINTER_AUTO for a game row or an index outside the tables */
int lt_ext_pointer_role(int index);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_LAYOUT_EXT_H */
