/*
 * port/ui/menu_text.c
 *
 * The game's menu text rows drawn with the port font (menu_text.h;
 * docs/port/UI.md, "Menu text").
 *
 * The table.  Each item is one texel rectangle of a sheet that holds text;
 * every texProperty row that draws that rectangle is drawn from it (several
 * rows share one: Yes / No on five prompts, Back on the save screens).  The
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
 * Rows left as textures (not in the table), and why:
 *   0..24      the stage's preload rows (layout 6, never drawn)
 *   25, 32     the LANGUAGE and TV headers: lettering inside the swash
 *              artwork
 *   31, 37     the ICO logo
 *   35, 36     the 50 / 60 Hz notes: text inside speech-bubble artwork
 *   46         "Sony Computer Entertainment Europe Presents": the publisher
 *              credit in the corporate lettering (artwork, as the copyright)
 *   47, 196, 240, 330, 412, 420 and the 1 x 1 rows: placeholders and ticks
 *   48         the copyright line (the corporate lettering, a legal notice)
 *   52..71     the slot numbers 1..10: digit tiles
 *   74..136    the preview's digits, colon and brackets: digit tiles placed
 *              one glyph at a time
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
 *   301..307, 309, 310, 314, 315, 319..322, 326..329
 *              the Options value arrows and the digit / letter tiles of
 *              the values (40 x 20 cells: 1 / 2 players, the hold types)
 *   346..349 and their copies 354..357 ... 386..389
 *              R1, R2, L2, L1: the pad's shoulder-button labels, outlined
 *              like the button glyphs
 *   395..409   the brightness markers; 410, 411 the ruler
 *   433        a stray corner of the speech-bubble artwork
 *   434, 435   the subtitle rows jimaku.c writes
 */
#include "menu_text.h"

#include <stdint.h>
#include <string.h>

#include "font.h"
#include "strings.h"
#include "ui_internal.h"

#define LT_GAME_ROWS 436

/* clang-format off */
/* {u, v, w, h, string, align, dark, em, x, pitch, {y EN, FR, DE, IT, ES}}, the
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
    {0, 180, 340, 45, UI_STR_MT_DO_NOT_REMOVE, UI_ALIGN_CENTER, 1, 12.1f, 170.0f, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}}, /* 72: Do not remove the / Memory Card (PS2) / or turn off the power */
    {0, 0, 256, 15, UI_STR_MT_ACCESSING, UI_ALIGN_CENTER, 0, 10.6f, 128.5f, 12.2f, {6.5f, 6.0f, 6.0f, 7.0f, 6.5f}}, /* 73: Accessing */
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
    {426, 40, 86, 20, UI_STR_MT_SOUND, UI_ALIGN_RIGHT, 0, 13.5f, 79.0f, 17.2f, {9.5f, 8.5f, 9.0f, 8.5f, 8.5f}}, /* 308: Sound */
    {128, 160, 128, 20, UI_STR_MT_STEREO, UI_ALIGN_CENTER, 0, 13.5f, 64.0f, 17.2f, {9.0f, 8.5f, 9.0f, 8.5f, 8.5f}}, /* 311: Stereo */
    {340, 20, 86, 20, UI_STR_MT_MONO, UI_ALIGN_CENTER, 0, 13.5f, 44.0f, 15.5f, {9.0f, 9.0f, 8.5f, 9.0f, 9.0f}}, /* 312: Mono */
    {384, 100, 128, 20, UI_STR_MT_VIBRATION, UI_ALIGN_RIGHT, 0, 13.5f, 121.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 313: Vibration */
    {192, 100, 192, 20, UI_STR_MT_HOLD_TYPE, UI_ALIGN_RIGHT, 0, 13.5f, 184.0f, 15.5f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}}, /* 318: Hold Type */
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
    {72, 17},
    {73, 18},
    {140, 19},
    {142, 20},
    {145, 21},
    {146, 21},
    {147, 22},
    {149, 23},
    {151, 24},
    {153, 25},
    {155, 26},
    {156, 27},
    {159, 28},
    {160, 29},
    {163, 30},
    {173, 31},
    {174, 32},
    {175, 33},
    {176, 34},
    {177, 35},
    {181, 36},
    {183, 37},
    {185, 38},
    {186, 34},
    {187, 35},
    {191, 36},
    {193, 37},
    {195, 39},
    {200, 40},
    {201, 41},
    {205, 42},
    {206, 41},
    {210, 17},
    {211, 43},
    {214, 10},
    {215, 11},
    {218, 10},
    {219, 11},
    {220, 44},
    {224, 40},
    {225, 41},
    {229, 45},
    {230, 46},
    {231, 41},
    {235, 36},
    {237, 37},
    {239, 47},
    {242, 48},
    {244, 49},
    {248, 17},
    {249, 50},
    {253, 17},
    {254, 51},
    {258, 17},
    {259, 18},
    {261, 52},
    {264, 53},
    {265, 54},
    {269, 55},
    {270, 10},
    {271, 11},
    {275, 56},
    {276, 41},
    {280, 57},
    {281, 41},
    {285, 58},
    {286, 41},
    {290, 59},
    {291, 41},
    {294, 60},
    {295, 61},
    {296, 62},
    {299, 63},
    {300, 64},
    {308, 65},
    {311, 66},
    {312, 67},
    {313, 68},
    {316, 13},
    {317, 14},
    {318, 69},
    {323, 70},
    {324, 71},
    {325, 72},
    {332, 37},
    {335, 73},
    {336, 74},
    {337, 75},
    {338, 76},
    {339, 77},
    {340, 78},
    {341, 79},
    {390, 36},
    {391, 80},
    {394, 81},
    {413, 82},
    {414, 83},
    {415, 84},
    {416, 36},
    {418, 80},
    {423, 85},
    {424, 10},
    {425, 11},
    {428, 86},
    {429, 10},
    {430, 11},
};
const int ui_menu_text_row_count = (int)(sizeof(ui_menu_text_rows) / sizeof(ui_menu_text_rows[0]));
/* clang-format on */

static int s_classic;

/* the row -> item map, built on first use: -1 for a row not in the table */
static short s_itemOf[LT_GAME_ROWS];
static int s_mapBuilt;

/* the last plain box of each row (the glow maps from it) */
static int s_base[LT_GAME_ROWS][4];
static unsigned char s_hasBase[LT_GAME_ROWS];

void ui_MenuTextSetClassic(int on)
{
    s_classic = on != 0;
}

int ui_MenuTextClassic(void)
{
    return s_classic;
}

static void buildMap(void)
{
    for (int i = 0; i < LT_GAME_ROWS; i++) {
        s_itemOf[i] = -1;
    }
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        const UiMenuTextRow *r = &ui_menu_text_rows[i];
        if (r->row >= 0 && r->row < LT_GAME_ROWS) {
            s_itemOf[r->row] = r->item;
        }
    }
    s_mapBuilt = 1;
}

static int rowIndex(const LtProperty *e)
{
    if (e < texProperty || e >= texProperty + LT_GAME_ROWS) {
        return -1;
    }
    return (int)(e - texProperty);
}

const UiMenuTextItem *ui_MenuTextItemOf(const LtProperty *e)
{
    if (s_classic || !e) {
        return NULL;
    }
    const int row = rowIndex(e);
    if (row < 0) {
        return NULL;
    }
    if (!s_mapBuilt) {
        buildMap();
    }
    const int i = s_itemOf[row];
    if (i < 0) {
        return NULL;
    }
    const UiMenuTextItem *it = &ui_menu_text_items[i];
    if (e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h) {
        return NULL;
    }
    return it;
}

#define MAX_LINES 6
#define LINE_BYTES 96

/* splits s at '\n' into lines; returns the count */
static int splitLines(const char *s, char lines[MAX_LINES][LINE_BYTES])
{
    int n = 0;
    while (n < MAX_LINES) {
        const char *nl = strchr(s, '\n');
        size_t len = nl ? (size_t)(nl - s) : strlen(s);
        if (len >= LINE_BYTES) {
            len = LINE_BYTES - 1;
        }
        memcpy(lines[n], s, len);
        lines[n][len] = '\0';
        n++;
        if (!nl) {
            break;
        }
        s = nl + 1;
    }
    return n;
}

void ui_MenuTextDraw(const LtProperty *e, const int box[4], const int uv[4],
                     const unsigned char rgba[4], int glow)
{
    const UiMenuTextItem *it = ui_MenuTextItemOf(e);
    if (!it || uv[2] <= 0 || uv[3] <= 0) {
        return;
    }
    const int row = rowIndex(e);
    ui__Sync();
    if (!glow || !s_hasBase[row]) {
        memcpy(s_base[row], box, sizeof(s_base[row]));
        s_hasBase[row] = 1;
    }
    const int *b = s_base[row];
    /* the sprite's box in the grid: x 1/16 px from the centre, y 1/16 field
       line from the centre (two y units a field line) */
    const float bx = (float)b[0] / 16.0f + UI_GRID_CX;
    const float by = (float)b[1] / 8.0f + UI_GRID_CY;
    const float bw = (float)b[2] / 16.0f;
    const float bh = (float)b[3] / 8.0f;
    /* grid units per texel, and the grid point of the rectangle's corner:
       the box spans the texels uv names (1/16 texel), which the caller's
       inset may start half a texel in */
    const float sx = bw * 16.0f / (float)uv[2];
    const float sy = bh * 16.0f / (float)uv[3];
    const float ox = bx + ((float)it->u * 16.0f - (float)uv[0]) / 16.0f * sx;
    const float oy = by + ((float)it->v * 16.0f - (float)uv[1]) / 16.0f * sy;

    char lines[MAX_LINES][LINE_BYTES];
    const int n = splitLines(ui_Str((UiStrId)it->str), lines);
    float size = it->em * sy;
    /* the room the anchor leaves in the rectangle; a longer line (Arimo is
       wider than the sheets' lettering at the same capitals) is set
       smaller to fit, down to 60 %, as the Settings rows are */
    float room;
    switch (it->align) {
    case UI_ALIGN_LEFT:
        room = ((float)it->w - it->x) * sx;
        break;
    case UI_ALIGN_RIGHT:
        room = it->x * sx;
        break;
    default:
        room = 2.0f * (it->x < (float)it->w - it->x ? it->x : (float)it->w - it->x) * sx;
        break;
    }
    float widest = 0.0f;
    for (int i = 0; i < n; i++) {
        float w = ui_MeasureText(size, lines[i]);
        widest = w > widest ? w : widest;
    }
    if (room > 0.0f && widest > room) {
        float k = room / widest;
        size *= k < 0.6f ? 0.6f : k;
    }
    /* whole y units: one atlas per size, and the atlases are bounded */
    size = (float)(int)(size + 0.5f);
    if (size < 1.0f) {
        size = 1.0f;
    }

    unsigned char col[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    if (it->dark) {
        if (glow) {
            return; /* black letters add nothing to the additive glow */
        }
        col[0] = col[1] = col[2] = 0;
    }
    UiLang lang = ui_GetLanguage();
    if ((int)lang < 0 || lang >= UI_LANG_COUNT) {
        lang = UI_LANG_EN;
    }
    const float x = ox + it->x * sx;
    unsigned flags = UI_KEEP_STATE | UI_VALIGN_MIDDLE | it->align;
    if (!glow && !it->dark) {
        flags |= UI_HALO;
    }
    /* the glow sprite stretches the row's box: the same map for the text */
    UiXform xf;
    if (glow) {
        xf.originX = bx;
        xf.originY = by;
        xf.scaleX = bw > 0.0f ? ((float)box[2] / 16.0f) / bw : 1.0f;
        xf.scaleY = bh > 0.0f ? ((float)box[3] / 8.0f) / bh : 1.0f;
        xf.offsetX = ((float)box[0] / 16.0f + UI_GRID_CX) - bx;
        xf.offsetY = ((float)box[1] / 8.0f + UI_GRID_CY) - by;
    }
    /* R7d: keyed by the row and the pass, as the port rows are */
    const uint64_t owner =
        ui_SetDrawKey(((uint64_t)(uintptr_t)e << 2) ^ (uint64_t)(glow ? 2u : 1u));
    for (int i = 0; i < n; i++) {
        const float y = oy + (it->y[lang] + (float)i * it->pitch) * sy;
        ui_DrawTextXf(x, y, size, col, lines[i], flags, glow ? &xf : NULL);
    }
    ui_SetDrawKey(owner);
}
