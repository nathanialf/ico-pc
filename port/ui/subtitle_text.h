/*
 * port/ui/subtitle_text.h
 *
 * A community translation's subtitles drawn as text (strings.h
 * UI_LANG_COMMUNITY_FIRST, port/game/subtitles.h, docs/TRANSLATING.md).
 *
 * The game shows a subtitle as the two strips of one record's picture in a
 * band of the 4:3 picture (ico2/fumi/src/jimaku.c jimakuDisp: texProperty
 * 434 and 435 on the European disc, grid x 64 .. 576, field lines 144 ..
 * 192).  While a community translation is in force (ui_lang.h
 * ui_settings_text_language: the European disc, its game in English, [game]
 * text_language set), jimakuDisp asks ui_subtitle_words for the shown
 * record's words; when the translation has them it opens display_texture's
 * packet (list 11, ALPHA 0x44, no Z, the fade's colour) and calls
 * ui_subtitle_draw instead of drawing the two strips, under the same
 * conditions (the record shown, its picture loaded, subtitles on, photo mode
 * off), so the timing, the semaphores and the record reads stay the game's.
 * A disc language, another disc, a record the table leaves out and Yorda's
 * words on a first play keep the picture.
 */
#ifndef PORT_UI_SUBTITLE_TEXT_H
#define PORT_UI_SUBTITLE_TEXT_H

#include "subtitles.h" /* port/game: IcoSubtitle and the tables */

#ifdef __cplusplus
extern "C" {
#endif

/* The band the two strips cover, layout grid (font.h): x from the left, y in
   y units (two a field line, field line f at 2 f + 2). */
#define UI_SUBTITLE_X0 64.0f
#define UI_SUBTITLE_X1 576.0f
#define UI_SUBTITLE_Y0 290.0f
#define UI_SUBTITLE_Y1 386.0f
/* the words' em (the menus' rows', menu_font.h), the room a line is set
   smaller to fit (the band less a margin a side, x units) and the y units
   from the first line's capital middle to the second's */
#define UI_SUBTITLE_EM 27.0f
#define UI_SUBTITLE_ROOM 496.0f
#define UI_SUBTITLE_PITCH 44.0f

/* The subtitle table of community translation lang (a UiLang from
   UI_LANG_COMMUNITY_FIRST) and its entry count; NULL (count 0) for any other
   language. */
const IcoSubtitle *ui_subtitle_table(int lang, int *count);
/* The words of record block in table t (n entries): NULL for a record the
   table leaves out and for a second-play entry while gameClear is 0. */
const char *ui_subtitle_find(const IcoSubtitle *t, int n, int block, int gameClear);
/* jimakuDisp's question: the shown record's words in the translation in
   force, NULL to draw the record's picture (no translation in force, or
   ui_subtitle_find's NULL).  Logs the first record of each change (first 8,
   then every 100th). */
const char *ui_subtitle_words(int block, int gameClear);

/* Where ui_subtitle_draw puts words (tests): the lines (1 or 2), the em
   (UI_SUBTITLE_EM, or smaller so the widest line fits UI_SUBTITLE_ROOM), the
   lines' centre x and capital middles y, grid units. */
typedef struct UiSubtitleLayout {
    int lines;
    float size;
    float x;
    float y[2];
} UiSubtitleLayout;

int ui_subtitle_layout(const char *words, UiSubtitleLayout *out);

/* Draws words (ui_subtitle_words') centred in the band, each line in the
   menus' look (menu_font.h ui_draw_menu_text: light ink with the dark rim,
   the sheet's texel grid at 1x, rasterised at the scene's scale above it),
   colour rgba (GS, 0x80 = 1.0: the strips' sprite colour), in the open
   packet's state (UI_KEEP_STATE), keyed by block so the presenter blends a
   subtitle between ticks.  Without ICO_RD it only measures. */
void ui_subtitle_draw(const char *words, int block, const unsigned char rgba[4]);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SUBTITLE_TEXT_H */
