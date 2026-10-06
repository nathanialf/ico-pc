/*
 * port/ui/menu_text.c
 *
 * The table of the game's menu words (menu_text.h; docs/port/UI.md, "Menu
 * text" and "The font").  Nothing draws from it: the game's rows always
 * draw their textures.  It is the source the game face is cut from
 * (game_font_disc.c): each item names a word rectangle of the sheets, its
 * transcribed words and where the lettering sits in it.
 *
 * Each item is one texel rectangle of a sheet that holds text; the rows
 * list every texProperty row that draws that rectangle (several rows share
 * one: Yes / No on five prompts, Back on the save screens).  The
 * values were measured on the five PAL sheets (decoded from STGTTL.DF and
 * COMMON.DF on the user's disc; nothing of the disc is kept here): the
 * letters' fill (light grey with a dark rim, or black on the white panel),
 * each line's capital top and baseline (the em is the capitals' height in
 * texels less the 0.7 texel of antialiasing, over Arimo's 0.688; the
 * 20-texel menu rows give 13.5 texels, 27 y units, UI_MENU_TEXT_SIZE), the
 * line pitch, and the ink's left, centre and right edges over the five
 * languages: the alignment is the edge that stays put.  The strings are the
 * sheets' words, transcribed with their wording, capitalisation and
 * punctuation (strings_*.c, UI_STR_MT_*), line breaks where the sheet breaks.
 *
 * Package TXT added the digit and letter tiles (one glyph a tile, the same
 * on the five sheets): the save screens' slot numbers (grey for an empty
 * file, black for a used one), the preview's play time and colons (white
 * without a rim; layout_action.c _la_set_preview_info picks a row per
 * figure), and the Options values (the film effect's 0..4, the hold type's
 * A / B, 1 / 2 players; light with the rim).  Their em, anchor and capital
 * middle were measured the same way, per ink (UiMenuTextInk).
 *
 * Rows not in the table (no lettering to cut, or lettering in another
 * style), and why:
 *   0..24      the stage's preload rows (layout 6, never drawn)
 *   25, 32     the LANGUAGE and TV headers: lettering inside the swash
 *              artwork
 *   31, 37     the ICO logo
 *   35, 36     the 50 / 60 Hz notes: text inside speech-bubble artwork
 *   46         "Sony Computer Entertainment Europe Presents": the publisher
 *              credit in the corporate lettering (artwork, as the copyright)
 *   47, 196, 240, 330, 412, 420 and the 1 x 1 rows: placeholders and ticks
 *   48         the copyright line (the corporate lettering, a legal notice)
 *   136        the preview's mark of a cleared game (a symbol, not a letter)
 *   137..139, 141, 143, 144, 148, 150, 152, 154, 157, 158, 161, 162,
 *   164..172   preview location rows whose rectangle is blank on every sheet
 *   178..180, 188..190, 197..199, 202..204, 207..209, 212, 213, 216, 217,
 *   221..223, 226..228, 232..234, 241, 243, 245..247, 250..252, 255..257,
 *   260, 262, 263, 266..268, 272..274, 277..279, 282..284, 287..289, 292,
 *   293, 297, 298, 333, 334, 392, 393, 421, 422, 426, 427, 431, 432
 *              the panels, bars and backdrops
 *   182, 184, 192, 194, 236, 238, 331, 342..345, 350..353, 358..361,
 *   366..369, 374..377, 382..385, 417, 419
 *              the pad's button glyphs (buttons.tm2)
 *   301, 302, 309, 310, 314, 315, 319, 320, 326, 327
 *              the Options value arrows (bracket-shaped pointers)
 *   346..349 and their copies 354..357 ... 386..389
 *              R1, R2, L2, L1: the pad's shoulder-button labels, outlined
 *              like the button glyphs
 *   395..409   the brightness markers; 410, 411 the ruler
 *   433        a stray corner of the speech-bubble artwork
 *   434, 435   the subtitle rows jimaku.c writes
 */
#include "menu_text.h"

#include "font.h"
#include "strings.h"

/* clang-format off */
/* {u, v, w, h, string, align, ink, em, x, pitch, {y EN, FR, DE, IT, ES}}, the
   first row that draws it and the English text */
const UiMenuTextItem ui_menu_text_items[] = {
    {0, 25, 150, 20, UI_STR_MT_LANG_ENGLISH, UI_ALIGN_CENTER, 0, 15.0f, 75.0f, 18.9f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 26: ENGLISH */
    {0, 45, 150, 20, UI_STR_MT_LANG_FRANCAIS, UI_ALIGN_CENTER, 0, 15.0f, 75.0f, 17.2f, {9.5f, 9.5f, 9.5f, 9.5f, 9.5f}}, /* 27: FRANÇAIS */
    {0, 65, 150, 20, UI_STR_MT_LANG_DEUTSCH, UI_ALIGN_CENTER, 0, 15.0f, 75.0f, 17.2f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 28: DEUTSCH */
    {0, 85, 150, 20, UI_STR_MT_LANG_ITALIANO, UI_ALIGN_CENTER, 0, 15.0f, 75.0f, 17.2f, {9.5f, 9.5f, 9.5f, 9.5f, 9.5f}}, /* 29: ITALIANO */
    {0, 105, 150, 20, UI_STR_MT_LANG_ESPANOL, UI_ALIGN_CENTER, 0, 15.0f, 74.5f, 22.2f, {8.0f, 8.0f, 8.0f, 8.0f, 8.0f}}, /* 30: ESPAÑOL */
    {150, 25, 80, 20, UI_STR_MT_HZ50, UI_ALIGN_CENTER, 0, 15.0f, 40.5f, 17.2f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 33: 50 Hz */
    {150, 45, 80, 20, UI_STR_MT_HZ60, UI_ALIGN_CENTER, 0, 15.0f, 40.5f, 17.2f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 34: 60 Hz */
    {0, 30, 384, 30, UI_STR_MT_NO_CARD, UI_ALIGN_CENTER, 0, 12.1f, 192.0f, 15.0f, {21.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 38: No Memory Card (PS2) inserted */
    {0, 90, 384, 45, UI_STR_MT_NEED_START, UI_ALIGN_CENTER, 0, 12.1f, 192.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 39: At least 360KB is needed / to save this game data. / Do you want to start? */
    {0, 60, 384, 75, UI_STR_MT_NO_SPACE_START, UI_ALIGN_CENTER, 0, 12.1f, 192.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 40: Insufficient space / on the Memory Card (PS2). / At least 360KB is needed / to save this game data. / Do you want to start? */
    {390, 170, 60, 20, UI_STR_MT_YES, UI_ALIGN_CENTER, 0, 13.5f, 30.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 41: Yes */
    {450, 170, 60, 20, UI_STR_MT_NO, UI_ALIGN_CENTER, 0, 13.5f, 30.5f, 15.5f, {8.5f, 9.0f, 9.0f, 9.0f, 8.5f}}, /* 42: No */
    {0, 0, 128, 20, UI_STR_MT_VIBRATION, UI_ALIGN_CENTER, 0, 13.5f, 64.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 43: Vibration */
    {256, 160, 128, 20, UI_STR_MT_ACTIVATE, UI_ALIGN_CENTER, 0, 13.5f, 63.5f, 15.5f, {9.0f, 8.5f, 9.0f, 9.0f, 8.5f}}, /* 44: Activate */
    {384, 160, 128, 20, UI_STR_MT_DEACTIVATE, UI_ALIGN_CENTER, 0, 13.5f, 64.0f, 17.2f, {9.0f, 8.5f, 9.0f, 8.5f, 8.5f}}, /* 45: Deactivate */
    {384, 140, 128, 20, UI_STR_MT_CONTINUE, UI_ALIGN_CENTER, 0, 13.5f, 64.5f, 15.5f, {9.0f, 9.0f, 8.5f, 9.0f, 9.0f}}, /* 49: Continue */
    {340, 0, 172, 20, UI_STR_MT_NEW_GAME, UI_ALIGN_CENTER, 0, 13.5f, 86.0f, 15.5f, {9.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 50: New Game */
    {390, 30, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 52: 1 */
    {410, 30, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 53: 2 */
    {430, 30, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 54: 3 */
    {450, 30, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 55: 4 */
    {470, 30, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 56: 5 */
    {490, 30, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 57: 6 */
    {390, 45, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 58: 7 */
    {410, 45, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 59: 8 */
    {430, 45, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 10.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 60: 9 */
    {483, 15, 29, 15, UI_STR_MT_DIGIT_10, UI_ALIGN_CENTER, UI_INK_GREY, 16.6f, 14.5f, 15.5f, {8.4f, 8.4f, 8.4f, 8.4f, 8.4f}}, /* 61: 10 */
    {390, 0, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 62: 1 */
    {410, 0, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 63: 2 */
    {430, 0, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 64: 3 */
    {450, 0, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 65: 4 */
    {470, 0, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 66: 5 */
    {490, 0, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 67: 6 */
    {390, 15, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 68: 7 */
    {410, 15, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 69: 8 */
    {430, 15, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 10.5f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 70: 9 */
    {450, 15, 33, 15, UI_STR_MT_DIGIT_10, UI_ALIGN_CENTER, UI_INK_DARK, 18.1f, 16.0f, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}}, /* 71: 10 */
    {0, 180, 340, 45, UI_STR_MT_DO_NOT_REMOVE, UI_ALIGN_CENTER, 1, 12.1f, 170.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 72: Do not remove the / Memory Card (PS2) / or turn off the power */
    {0, 0, 256, 15, UI_STR_MT_ACCESSING, UI_ALIGN_CENTER, 0, 10.6f, 128.5f, 12.2f, {6.5f, 6.0f, 6.0f, 7.0f, 6.5f}}, /* 73: Accessing */
    {450, 75, 20, 15, UI_STR_MT_COLON, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 74: : */
    {450, 45, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 76: 1 */
    {470, 45, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 77: 2 */
    {490, 45, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 78: 3 */
    {390, 60, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 79: 4 */
    {410, 60, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 80: 5 */
    {430, 60, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 81: 6 */
    {450, 60, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 82: 7 */
    {390, 75, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 83: 8 */
    {410, 75, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 84: 9 */
    {430, 75, 20, 15, UI_STR_MT_DIGIT_0, UI_ALIGN_CENTER, UI_INK_PLAIN, 17.9f, 10.5f, 15.5f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 85: 0 */
    {340, 0, 172, 15, UI_STR_MT_LOC_OLD_BRIDGE, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 5.5f, 6.0f}}, /* 140: Old Bridge */
    {340, 30, 172, 15, UI_STR_MT_LOC_TROLLEY_A, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 142: Trolley 2 */
    {300, 225, 210, 15, UI_STR_MT_LOC_MAIN_GATE, UI_ALIGN_CENTER, 0, 13.5f, 107.0f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 145: Main Gate */
    {340, 60, 172, 15, UI_STR_MT_LOC_GRAVEYARD, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 147: Graveyard */
    {320, 165, 192, 15, UI_STR_MT_LOC_WINDMILL, UI_ALIGN_CENTER, 0, 13.5f, 97.0f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 149: Windmill */
    {300, 240, 210, 15, UI_STR_MT_LOC_STONE_PILLAR, UI_ALIGN_CENTER, 0, 13.5f, 106.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 151: Stone Pillar */
    {340, 75, 172, 15, UI_STR_MT_LOC_EAST_ARENA, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 153: East Arena */
    {340, 90, 172, 15, UI_STR_MT_LOC_EAST_REFLECTOR, UI_ALIGN_CENTER, 0, 13.5f, 86.0f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 155: East Reflector */
    {340, 105, 172, 15, UI_STR_MT_LOC_WATERFALL, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 156: Waterfall */
    {340, 120, 172, 15, UI_STR_MT_LOC_GONDOLA, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 159: Gondola */
    {340, 135, 172, 15, UI_STR_MT_LOC_WATER_TOWER, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 160: Water Tower */
    {0, 60, 256, 15, UI_STR_MT_LOC_WEST_IDOL_STAIRS, UI_ALIGN_CENTER, 0, 12.1f, 128.5f, 13.9f, {6.5f, 6.0f, 6.0f, 6.5f, 6.5f}}, /* 163: West Idol Stairs */
    {340, 15, 172, 15, UI_STR_MT_LOC_TROLLEY_B, UI_ALIGN_CENTER, 0, 13.5f, 88.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.0f}}, /* 173: Trolley 1 */
    {340, 45, 172, 15, UI_STR_MT_LOC_CRANE, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 5.5f, 6.0f}}, /* 174: Crane */
    {340, 150, 172, 15, UI_STR_MT_LOC_SANDY_BEACH, UI_ALIGN_CENTER, 0, 13.5f, 86.5f, 15.5f, {6.0f, 6.0f, 6.0f, 6.0f, 6.5f}}, /* 175: Sandy Beach */
    {0, 225, 300, 15, UI_STR_MT_SLOT_1, UI_ALIGN_CENTER, 1, 9.2f, 149.0f, 10.5f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 176: MEMORY CARD slot 1 */
    {0, 240, 300, 15, UI_STR_MT_SLOT_2, UI_ALIGN_CENTER, 1, 9.2f, 150.5f, 10.5f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 177: MEMORY CARD slot 2 */
    {390, 190, 100, 20, UI_STR_MT_OK, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 181: OK */
    {384, 80, 128, 20, UI_STR_MT_BACK, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 183: Back */
    {0, 120, 384, 30, UI_STR_MT_SELECT_SLOT, UI_ALIGN_CENTER, 0, 12.1f, 192.0f, 13.0f, {21.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 185: Select MEMORY CARD slot. */
    {0, 165, 384, 15, UI_STR_MT_SELECT_LOAD, UI_ALIGN_CENTER, 0, 12.1f, 191.5f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 195: Select a file to load. */
    {0, 0, 340, 30, UI_STR_MT_NO_CARD, UI_ALIGN_CENTER, 1, 12.1f, 170.5f, 15.0f, {13.0f, 5.5f, 6.0f, 6.0f, 6.0f}}, /* 200: No Memory Card (PS2) inserted */
    {0, 20, 128, 20, UI_STR_MT_BACK, UI_ALIGN_CENTER, 0, 13.5f, 64.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 201: Back */
    {0, 225, 340, 15, UI_STR_MT_NO_SAVE_DATA, UI_ALIGN_CENTER, 1, 12.1f, 171.0f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 205: There is no save data. */
    {0, 30, 256, 15, UI_STR_MT_LOADING, UI_ALIGN_CENTER, 0, 12.1f, 127.0f, 13.9f, {6.5f, 6.0f, 6.0f, 6.5f, 6.5f}}, /* 211: Loading */
    {300, 180, 212, 25, UI_STR_MT_SAVE_Q, UI_ALIGN_CENTER, 0, 16.4f, 105.5f, 18.9f, {11.0f, 11.0f, 11.0f, 11.0f, 11.0f}}, /* 220: Save? */
    {0, 30, 340, 30, UI_STR_MT_NO_SPACE, UI_ALIGN_CENTER, 1, 12.1f, 170.5f, 15.0f, {6.0f, 6.5f, 6.5f, 6.5f, 6.0f}}, /* 229: There is insufficient space / on the Memory Card (PS2). */
    {0, 240, 340, 15, UI_STR_MT_NEED_360, UI_ALIGN_CENTER, 1, 12.1f, 170.0f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 230: (Need 360KB or more) */
    {0, 150, 384, 15, UI_STR_MT_SELECT_SAVE, UI_ALIGN_CENTER, 0, 12.1f, 191.5f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 239: Select user file to save to. */
    {0, 120, 340, 45, UI_STR_MT_OVERWRITE, UI_ALIGN_CENTER, 1, 12.1f, 170.5f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 242: Saving will overwrite / the data on this file. / Is this okay? */
    {0, 90, 256, 45, UI_STR_MT_NOT_FORMATTED, UI_ALIGN_CENTER, 1, 12.1f, 129.2f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 244: The Memory Card (PS2) / is not formatted / Format? */
    {0, 45, 256, 15, UI_STR_MT_FORMATTING, UI_ALIGN_CENTER, 0, 12.1f, 128.0f, 13.9f, {6.5f, 6.0f, 6.0f, 6.5f, 6.5f}}, /* 249: Formatting */
    {0, 15, 256, 15, UI_STR_MT_SAVING, UI_ALIGN_CENTER, 0, 12.1f, 128.5f, 13.9f, {6.5f, 6.0f, 6.0f, 6.5f, 6.5f}}, /* 254: Saving */
    {0, 75, 256, 15, UI_STR_MT_FILE_SAVED, UI_ALIGN_CENTER, 1, 12.1f, 129.2f, 13.9f, {6.5f, 6.5f, 6.0f, 6.5f, 6.5f}}, /* 261: File saved. */
    {0, 135, 256, 20, UI_STR_MT_RESUME_GAME, UI_ALIGN_CENTER, 0, 13.5f, 129.0f, 15.5f, {9.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 264: Resume Game */
    {0, 155, 256, 20, UI_STR_MT_END_GAME, UI_ALIGN_CENTER, 0, 13.5f, 128.5f, 15.5f, {9.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 265: End Game */
    {0, 175, 256, 30, UI_STR_MT_GAME_WILL_END, UI_ALIGN_CENTER, 1, 12.1f, 129.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 269: The game will end. / Is this okay? */
    {0, 165, 320, 15, UI_STR_MT_SAVE_FAILED, UI_ALIGN_CENTER, 1, 12.1f, 161.0f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 275: Save failed. */
    {0, 90, 340, 15, UI_STR_MT_FORMAT_FAILED, UI_ALIGN_CENTER, 1, 12.1f, 171.5f, 13.9f, {6.0f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 280: Format failed. */
    {0, 105, 340, 15, UI_STR_MT_LOAD_FAILED, UI_ALIGN_CENTER, 1, 12.1f, 170.5f, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.0f}}, /* 285: Load data failed */
    {0, 60, 340, 30, UI_STR_MT_NO_ACCESS, UI_ALIGN_CENTER, 1, 12.1f, 170.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.0f}}, /* 290: Cannot access the / Memory Card (PS2) */
    {384, 120, 128, 20, UI_STR_MT_OPTIONS, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 294: Options */
    {128, 20, 128, 20, UI_STR_MT_PAUSE_BACK, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 295: Back */
    {128, 0, 212, 20, UI_STR_MT_END_GAME, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 296: End Game */
    {384, 225, 128, 20, UI_STR_MT_OPTIONS, UI_ALIGN_CENTER, 0, 13.5f, 64.5f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.5f}}, /* 299: Options */
    {0, 100, 192, 20, UI_STR_MT_FILM_EFFECT, UI_ALIGN_RIGHT, 0, 13.5f, 185.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 300: Film Effect */
    {390, 90, 40, 20, UI_STR_MT_DIGIT_0, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.5f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 303: 0 */
    {430, 90, 40, 20, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.0f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 304: 1 */
    {470, 90, 40, 20, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.5f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 305: 2 */
    {390, 110, 40, 20, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.5f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 306: 3 */
    {430, 110, 40, 20, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.0f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 307: 4 */
    {426, 40, 86, 20, UI_STR_MT_SOUND, UI_ALIGN_RIGHT, 0, 13.5f, 79.0f, 17.2f, {9.5f, 8.5f, 9.0f, 8.5f, 8.5f}}, /* 308: Sound */
    {128, 160, 128, 20, UI_STR_MT_STEREO, UI_ALIGN_CENTER, 0, 13.5f, 64.0f, 17.2f, {9.0f, 8.5f, 9.0f, 8.5f, 8.5f}}, /* 311: Stereo */
    {340, 20, 86, 20, UI_STR_MT_MONO, UI_ALIGN_CENTER, 0, 13.5f, 44.0f, 15.5f, {9.0f, 9.0f, 8.5f, 9.0f, 9.0f}}, /* 312: Mono */
    {384, 100, 128, 20, UI_STR_MT_VIBRATION, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 313: Vibration */
    {192, 100, 192, 20, UI_STR_MT_HOLD_TYPE, UI_ALIGN_RIGHT, 0, 13.5f, 184.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 318: Hold Type */
    {470, 110, 40, 20, UI_STR_MT_VAL_A, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 20.0f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 321: A */
    {470, 70, 40, 20, UI_STR_MT_VAL_B, UI_ALIGN_CENTER, UI_INK_LIGHT, 18.0f, 21.0f, 15.5f, {10.0f, 10.0f, 10.0f, 10.0f, 10.0f}}, /* 322: B */
    {0, 120, 256, 20, UI_STR_MT_BUTTON_CONFIG, UI_ALIGN_RIGHT, 0, 13.5f, 248.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 323: Button Configuration */
    {256, 120, 128, 20, UI_STR_MT_BRIGHTNESS, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 324: Brightness */
    {0, 140, 128, 20, UI_STR_MT_PLAYERS, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 325: Players */
    {0, 225, 256, 20, UI_STR_MT_BUTTON_CONFIG, UI_ALIGN_CENTER, 0, 13.5f, 128.0f, 15.5f, {9.0f, 9.5f, 9.0f, 9.0f, 9.5f}}, /* 335: Button Configuration */
    {128, 140, 128, 20, UI_STR_MT_JUMP, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 336: Jump */
    {256, 140, 128, 20, UI_STR_MT_ATTACK, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.5f, 9.5f, 9.0f, 9.0f, 9.0f}}, /* 337: Attack */
    {426, 20, 86, 20, UI_STR_MT_ACTION, UI_ALIGN_RIGHT, 0, 13.5f, 79.0f, 15.5f, {9.0f, 9.0f, 9.0f, 8.5f, 8.5f}}, /* 338: Action */
    {0, 160, 128, 20, UI_STR_MT_RELEASE, UI_ALIGN_RIGHT, 0, 13.5f, 120.0f, 17.2f, {9.0f, 8.5f, 8.5f, 8.5f, 9.0f}}, /* 339: Release */
    {0, 60, 300, 20, UI_STR_MT_HOLD_HAND_CALL, UI_ALIGN_RIGHT, 0, 13.5f, 292.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 340: Hold hand / Call */
    {340, 40, 86, 20, UI_STR_MT_ZOOM, UI_ALIGN_RIGHT, 0, 13.5f, 79.0f, 17.2f, {9.0f, 8.5f, 8.5f, 8.5f, 8.5f}}, /* 341: Zoom */
    {300, 60, 210, 20, UI_STR_MT_DEFAULT, UI_ALIGN_LEFT, 0, 13.5f, 7.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 391: Default */
    {256, 225, 128, 20, UI_STR_MT_BRIGHTNESS, UI_ALIGN_CENTER, 0, 13.5f, 65.0f, 15.5f, {9.0f, 8.0f, 9.0f, 9.0f, 9.5f}}, /* 394: Brightness */
    {0, 80, 384, 20, UI_STR_MT_ADJUST_HINT, UI_ALIGN_CENTER, 0, 13.5f, 192.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 413: Adjust with directional buttons */
    {390, 150, 100, 20, UI_STR_MT_DARK, UI_ALIGN_CENTER, 0, 13.5f, 49.5f, 15.5f, {9.0f, 9.0f, 8.5f, 9.0f, 8.5f}}, /* 414: Dark */
    {390, 130, 100, 20, UI_STR_MT_LIGHT, UI_ALIGN_CENTER, 0, 13.5f, 50.5f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 8.5f}}, /* 415: Light */
    {0, 205, 256, 35, UI_STR_MT_GAME_WILL_END, UI_ALIGN_CENTER, 0, 13.5f, 129.0f, 15.0f, {9.5f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 423: The game will end. / Is this okay? */
    {0, 180, 300, 25, UI_STR_MT_CONTINUE_Q, UI_ALIGN_CENTER, 0, 16.4f, 149.5f, 18.9f, {11.0f, 11.0f, 11.0f, 11.0f, 11.0f}}, /* 428: Continue ? */
};
const int ui_menu_text_item_count = (int)(sizeof(ui_menu_text_items) / sizeof(ui_menu_text_items[0]));

/* {texProperty row, item} */
const UiMenuTextRow ui_menu_text_rows[] = {
    {26, 0},
    {27, 1},
    {28, 2},
    {29, 3},
    {30, 4},
    {33, 5},
    {34, 6},
    {38, 7},
    {39, 8},
    {40, 9},
    {41, 10},
    {42, 11},
    {43, 12},
    {44, 13},
    {45, 14},
    {49, 15},
    {50, 16},
    {51, 16},
    {52, 17},
    {53, 18},
    {54, 19},
    {55, 20},
    {56, 21},
    {57, 22},
    {58, 23},
    {59, 24},
    {60, 25},
    {61, 26},
    {62, 27},
    {63, 28},
    {64, 29},
    {65, 30},
    {66, 31},
    {67, 32},
    {68, 33},
    {69, 34},
    {70, 35},
    {71, 36},
    {72, 37},
    {73, 38},
    {74, 39},
    {75, 39},
    {76, 40},
    {77, 41},
    {78, 42},
    {79, 43},
    {80, 44},
    {81, 45},
    {82, 46},
    {83, 47},
    {84, 48},
    {85, 49},
    {86, 40},
    {87, 41},
    {88, 42},
    {89, 43},
    {90, 44},
    {91, 45},
    {92, 46},
    {93, 47},
    {94, 48},
    {95, 49},
    {96, 40},
    {97, 41},
    {98, 42},
    {99, 43},
    {100, 44},
    {101, 45},
    {102, 46},
    {103, 47},
    {104, 48},
    {105, 49},
    {106, 40},
    {107, 41},
    {108, 42},
    {109, 43},
    {110, 44},
    {111, 45},
    {112, 46},
    {113, 47},
    {114, 48},
    {115, 49},
    {116, 40},
    {117, 41},
    {118, 42},
    {119, 43},
    {120, 44},
    {121, 45},
    {122, 46},
    {123, 47},
    {124, 48},
    {125, 49},
    {126, 40},
    {127, 41},
    {128, 42},
    {129, 43},
    {130, 44},
    {131, 45},
    {132, 46},
    {133, 47},
    {134, 48},
    {135, 49},
    {140, 50},
    {142, 51},
    {145, 52},
    {146, 52},
    {147, 53},
    {149, 54},
    {151, 55},
    {153, 56},
    {155, 57},
    {156, 58},
    {159, 59},
    {160, 60},
    {163, 61},
    {173, 62},
    {174, 63},
    {175, 64},
    {176, 65},
    {177, 66},
    {181, 67},
    {183, 68},
    {185, 69},
    {186, 65},
    {187, 66},
    {191, 67},
    {193, 68},
    {195, 70},
    {200, 71},
    {201, 72},
    {205, 73},
    {206, 72},
    {210, 37},
    {211, 74},
    {214, 10},
    {215, 11},
    {218, 10},
    {219, 11},
    {220, 75},
    {224, 71},
    {225, 72},
    {229, 76},
    {230, 77},
    {231, 72},
    {235, 67},
    {237, 68},
    {239, 78},
    {242, 79},
    {244, 80},
    {248, 37},
    {249, 81},
    {253, 37},
    {254, 82},
    {258, 37},
    {259, 38},
    {261, 83},
    {264, 84},
    {265, 85},
    {269, 86},
    {270, 10},
    {271, 11},
    {275, 87},
    {276, 72},
    {280, 88},
    {281, 72},
    {285, 89},
    {286, 72},
    {290, 90},
    {291, 72},
    {294, 91},
    {295, 92},
    {296, 93},
    {299, 94},
    {300, 95},
    {303, 96},
    {304, 97},
    {305, 98},
    {306, 99},
    {307, 100},
    {308, 101},
    {311, 102},
    {312, 103},
    {313, 104},
    {316, 13},
    {317, 14},
    {318, 105},
    {321, 106},
    {322, 107},
    {323, 108},
    {324, 109},
    {325, 110},
    {328, 97},
    {329, 98},
    {332, 68},
    {335, 111},
    {336, 112},
    {337, 113},
    {338, 114},
    {339, 115},
    {340, 116},
    {341, 117},
    {390, 67},
    {391, 118},
    {394, 119},
    {413, 120},
    {414, 121},
    {415, 122},
    {416, 67},
    {418, 118},
    {423, 123},
    {424, 10},
    {425, 11},
    {428, 124},
    {429, 10},
    {430, 11},
};
const int ui_menu_text_row_count = (int)(sizeof(ui_menu_text_rows) / sizeof(ui_menu_text_rows[0]));
/* clang-format on */
