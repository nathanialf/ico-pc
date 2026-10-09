/*
 * port/ui/menu_text.c
 *
 * The table of the game's menu words (menu_text.h): each item names a
 * word rectangle of the sheets, its transcribed words and where the
 * lettering sits in it; the rows the layout draws from it are drawn as
 * text in the sheets' look (menu_font.c).
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
 * languages: the alignment is the edge that stays put.  The em, the
 * anchor, the capital middle, the width (wx, against the menus'
 * UI_SHEET_WIDTH) and the spacing (track) per language are then fitted,
 * each item on each sheet, to the sheet's lettering (ctest menu_look,
 * ICO_MENU_LOOK_GEOFIT: the smallest difference between the strip's fill
 * and the sheet's after a 3 x 3 blur), since the five sheets were lettered
 * apart; the digit tiles of one set share their em, capital middle, width
 * and spacing (the median of the set's), so a figure does not stand taller
 * than its neighbours; the black digits and prompts (no fill to compare on black) keep the measured em
 * and the width 1.  The halo of the light ink differs too: menu_PAL_02,
 * the title sheet, scei and the panels' prompts of 01 and 04 have none,
 * most English words of 03 and a few of 01 and 04 a faint one, the rest
 * the full one; rim is the halo's opacity on the sheet against the strip's
 * full one (ctest menu_look, ICO_MENU_LOOK_RIM=1): under 0.15 none, under
 * 0.55 faint, else full (the dark, plain and grey inks have none).  The
 * strings are the sheets' words, transcribed with their wording,
 * capitalisation and punctuation (strings_*.c, UI_STR_MT_*), line breaks
 * where the sheet breaks.
 *
 * The digit and letter tiles (one glyph a tile, the same on the five
 * sheets): the save screens' slot numbers (grey for an empty
 * file, black for a used one), the preview's play time and colons (white
 * without a rim; layout_action.c _la_set_preview_info picks a row per
 * figure), and the Options values (the film effect's 0..4, the hold type's
 * A / B, 1 / 2 players; light with the rim).  Their em, anchor and capital
 * middle were measured the same way, per ink (UiMenuTextInk).
 *
 * Rows not in the table (they keep their texels: no lettering, or
 * lettering in another style), and why:
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

#include <stddef.h>

#include "font.h"
#include "strings.h"

/* clang-format off */
/* {u, v, w, h, string, align, ink, {em}, {x}, pitch, {y}, {wx}, {track}, {rim}}, the
   arrays per language EN FR DE IT ES (rim: 0 none, 1 faint, 2 full,
   menu_font.h UI_RIM_*); the first row that draws it and the English text */
const UiMenuTextItem ui_menu_text_items[] = {
    {0, 25, 150, 20, UI_STR_MT_LANG_ENGLISH, UI_ALIGN_CENTER, 0, {16.76f, 16.76f, 16.76f, 16.76f, 16.76f}, {75.5f, 75.5f, 75.5f, 75.5f, 75.5f}, 18.9f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}, {0.963f, 0.963f, 0.963f, 0.963f, 0.963f}, {0.02f, 0.02f, 0.02f, 0.02f, 0.02f}, {0, 0, 0, 0, 0}}, /* 26: ENGLISH */
    {0, 45, 150, 20, UI_STR_MT_LANG_FRANCAIS, UI_ALIGN_CENTER, 0, {16.69f, 16.69f, 16.69f, 16.69f, 16.69f}, {74.75f, 74.75f, 74.75f, 74.75f, 74.75f}, 17.2f, {9.0f, 9.0f, 9.0f, 9.0f, 9.0f}, {0.958f, 0.958f, 0.958f, 0.958f, 0.958f}, {0.1f, 0.1f, 0.1f, 0.1f, 0.1f}, {0, 0, 0, 0, 0}}, /* 27: FRANÇAIS */
    {0, 65, 150, 20, UI_STR_MT_LANG_DEUTSCH, UI_ALIGN_CENTER, 0, {16.63f, 16.63f, 16.63f, 16.63f, 16.63f}, {75.5f, 75.5f, 75.5f, 75.5f, 75.5f}, 17.2f, {8.62f, 8.62f, 8.62f, 8.62f, 8.62f}, {0.964f, 0.964f, 0.964f, 0.964f, 0.964f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 28: DEUTSCH */
    {0, 85, 150, 20, UI_STR_MT_LANG_ITALIANO, UI_ALIGN_CENTER, 0, {16.71f, 16.71f, 16.71f, 16.71f, 16.71f}, {74.5f, 74.5f, 74.5f, 74.5f, 74.5f}, 17.2f, {9.25f, 9.25f, 9.25f, 9.25f, 9.25f}, {0.972f, 0.972f, 0.972f, 0.972f, 0.972f}, {-0.03f, -0.03f, -0.03f, -0.03f, -0.03f}, {0, 0, 0, 0, 0}}, /* 29: ITALIANO */
    {0, 105, 150, 20, UI_STR_MT_LANG_ESPANOL, UI_ALIGN_CENTER, 0, {16.61f, 16.61f, 16.61f, 16.61f, 16.61f}, {74.25f, 74.25f, 74.25f, 74.25f, 74.25f}, 22.2f, {9.37f, 9.37f, 9.37f, 9.37f, 9.37f}, {0.964f, 0.964f, 0.964f, 0.964f, 0.964f}, {0.16f, 0.16f, 0.16f, 0.16f, 0.16f}, {0, 0, 0, 0, 0}}, /* 30: ESPAÑOL */
    {150, 25, 80, 20, UI_STR_MT_HZ50, UI_ALIGN_CENTER, 0, {16.7f, 16.71f, 16.71f, 16.71f, 16.71f}, {40.25f, 40.26f, 40.26f, 40.26f, 40.26f}, 17.2f, {8.5f, 8.5f, 8.5f, 8.5f, 8.5f}, {0.948f, 0.936f, 0.936f, 0.936f, 0.936f}, {0.57f, 0.72f, 0.72f, 0.72f, 0.72f}, {0, 0, 0, 0, 0}}, /* 33: 50 Hz */
    {150, 45, 80, 20, UI_STR_MT_HZ60, UI_ALIGN_CENTER, 0, {16.78f, 16.78f, 16.78f, 16.78f, 16.78f}, {40.26f, 40.26f, 40.26f, 40.26f, 40.26f}, 17.2f, {8.51f, 8.51f, 8.51f, 8.51f, 8.51f}, {0.937f, 0.937f, 0.937f, 0.937f, 0.937f}, {0.65f, 0.65f, 0.65f, 0.65f, 0.65f}, {0, 0, 0, 0, 0}}, /* 34: 60 Hz */
    {0, 30, 384, 30, UI_STR_MT_NO_CARD, UI_ALIGN_CENTER, 0, {13.88f, 13.59f, 14.14f, 13.4f, 13.21f}, {191.88f, 192.0f, 191.5f, 192.0f, 192.38f}, 15.0f, {21.62f, 6.74f, 6.5f, 6.75f, 6.75f}, {0.97f, 0.986f, 0.942f, 0.998f, 1.053f}, {-0.06f, 0.0f, 0.06f, 0.03f, -0.45f}, {0, 0, 0, 0, 0}}, /* 38: No Memory Card (PS2) inserted */
    {0, 90, 384, 45, UI_STR_MT_NEED_START, UI_ALIGN_CENTER, 0, {14.08f, 13.61f, 13.46f, 13.54f, 13.75f}, {192.5f, 192.37f, 192.13f, 192.0f, 191.87f}, 15.0f, {6.63f, 6.75f, 6.74f, 6.75f, 6.75f}, {0.893f, 0.989f, 1.019f, 0.995f, 0.99f}, {0.64f, -0.05f, -0.25f, -0.06f, -0.17f}, {0, 0, 0, 0, 0}}, /* 39: At least 360KB is needed / to save this game data. / Do you want to start? */
    {0, 60, 384, 75, UI_STR_MT_NO_SPACE_START, UI_ALIGN_CENTER, 0, {13.59f, 13.61f, 13.39f, 13.54f, 13.53f}, {192.87f, 192.37f, 191.37f, 192.25f, 192.01f}, 15.0f, {6.75f, 6.75f, 6.75f, 6.74f, 6.75f}, {0.985f, 0.976f, 1.065f, 0.992f, 1.008f}, {0.0f, 0.09f, -0.7f, -0.03f, -0.2f}, {0, 0, 0, 0, 0}}, /* 40: Insufficient space / on the Memory Card (PS2). / At least 360KB is needed / to save this game data. / Do you want to start? */
    {390, 170, 60, 20, UI_STR_MT_YES, UI_ALIGN_CENTER, 0, {13.91f, 16.01f, 14.72f, 15.17f, 15.09f}, {30.63f, 29.25f, 30.0f, 30.87f, 28.88f}, 15.5f, {9.25f, 8.75f, 9.0f, 8.63f, 8.63f}, {0.996f, 0.89f, 1.023f, 0.97f, 0.971f}, {-0.41f, 0.0f, -0.78f, 0.06f, 0.33f}, {1, 2, 2, 2, 2}}, /* 41: Yes */
    {450, 170, 60, 20, UI_STR_MT_NO, UI_ALIGN_CENTER, 0, {15.14f, 15.71f, 15.26f, 15.79f, 15.78f}, {29.0f, 28.75f, 30.25f, 30.75f, 30.75f}, 15.5f, {8.75f, 8.88f, 8.88f, 8.88f, 8.87f}, {0.952f, 0.875f, 0.936f, 0.858f, 0.861f}, {-0.09f, 0.61f, -0.02f, 0.53f, 0.51f}, {1, 2, 2, 2, 2}}, /* 42: No */
    {0, 0, 128, 20, UI_STR_MT_VIBRATION, UI_ALIGN_CENTER, 0, {13.5f, 13.91f, 13.9f, 13.63f, 13.98f}, {64.99f, 64.13f, 65.0f, 64.75f, 64.25f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.983f, 0.966f, 0.993f, 0.975f, 0.96f}, {0.12f, -0.06f, -0.29f, 0.08f, -0.03f}, {2, 2, 2, 2, 2}}, /* 43: Vibration */
    {256, 160, 128, 20, UI_STR_MT_ACTIVATE, UI_ALIGN_CENTER, 0, {13.17f, 15.18f, 14.89f, 15.55f, 15.4f}, {65.12f, 63.26f, 64.0f, 62.5f, 64.38f}, 15.5f, {9.25f, 8.75f, 8.63f, 8.75f, 8.63f}, {1.01f, 0.97f, 0.96f, 0.93f, 0.943f}, {0.09f, -0.25f, 0.0f, -0.27f, 0.22f}, {2, 2, 2, 2, 2}}, /* 44: Activate */
    {384, 160, 128, 20, UI_STR_MT_DEACTIVATE, UI_ALIGN_CENTER, 0, {13.39f, 15.26f, 14.67f, 15.62f, 15.11f}, {64.0f, 62.88f, 64.12f, 63.88f, 64.25f}, 17.2f, {9.38f, 8.76f, 8.75f, 8.75f, 8.75f}, {0.994f, 0.943f, 0.994f, 0.924f, 0.951f}, {0.08f, -0.02f, -0.25f, -0.06f, 0.05f}, {2, 2, 2, 2, 2}}, /* 45: Deactivate */
    {384, 140, 128, 20, UI_STR_MT_CONTINUE, UI_ALIGN_CENTER, 0, {13.5f, 14.04f, 14.45f, 14.04f, 13.97f}, {63.88f, 64.38f, 64.12f, 63.75f, 63.76f}, 15.5f, {9.38f, 9.25f, 8.88f, 9.25f, 9.25f}, {1.009f, 0.985f, 1.033f, 0.98f, 0.979f}, {-0.14f, -0.36f, -0.57f, -0.24f, -0.2f}, {2, 2, 2, 2, 2}}, /* 49: Continue */
    {340, 0, 172, 20, UI_STR_MT_NEW_GAME, UI_ALIGN_CENTER, 0, {13.47f, 13.97f, 13.89f, 13.9f, 13.63f}, {88.87f, 84.5f, 85.63f, 86.25f, 85.87f}, 15.5f, {9.61f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.988f, 0.963f, 1.001f, 0.973f, 0.993f}, {0.11f, -0.03f, -0.42f, -0.09f, -0.11f}, {2, 2, 2, 2, 2}}, /* 50: New Game */
    {390, 30, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.38f, 10.38f, 14.38f, 10.38f, 10.38f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 52: 1 */
    {410, 30, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.25f, 10.37f, 10.38f, 10.38f, 10.38f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 53: 2 */
    {430, 30, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.25f, 10.25f, 10.25f, 10.13f, 10.13f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 54: 3 */
    {450, 30, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.0f, 10.0f, 10.0f, 9.99f, 10.0f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 55: 4 */
    {470, 30, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {11.12f, 10.62f, 10.5f, 10.5f, 10.62f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 56: 5 */
    {490, 30, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.49f, 10.0f, 10.12f, 10.0f, 10.12f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 57: 6 */
    {390, 45, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {9.75f, 9.75f, 9.75f, 9.75f, 9.75f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 58: 7 */
    {410, 45, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.25f, 10.38f, 10.25f, 10.25f, 10.38f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 59: 8 */
    {430, 45, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {10.38f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 60: 9 */
    {483, 15, 29, 15, UI_STR_MT_DIGIT_10, UI_ALIGN_CENTER, UI_INK_GREY, {16.73f, 16.1f, 16.43f, 16.19f, 16.11f}, {17.37f, 18.63f, 15.37f, 17.12f, 17.75f}, 15.5f, {8.52f, 8.4f, 8.4f, 8.4f, 8.4f}, {1.207f, 1.225f, 1.237f, 1.239f, 1.239f}, {0.44f, 0.34f, -0.12f, -0.19f, 0.68f}, {0, 0, 0, 0, 0}}, /* 61: 10 */
    {390, 0, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 62: 1 */
    {410, 0, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 63: 2 */
    {430, 0, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 64: 3 */
    {450, 0, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 65: 4 */
    {470, 0, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 66: 5 */
    {490, 0, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 67: 6 */
    {390, 15, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 68: 7 */
    {410, 15, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 69: 8 */
    {430, 15, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {10.5f, 10.5f, 10.5f, 10.5f, 10.5f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 70: 9 */
    {450, 15, 33, 15, UI_STR_MT_DIGIT_10, UI_ALIGN_CENTER, UI_INK_DARK, {18.1f, 18.1f, 18.1f, 18.1f, 18.1f}, {16.0f, 16.0f, 16.0f, 16.0f, 16.0f}, 15.5f, {7.9f, 7.9f, 7.9f, 7.9f, 7.9f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 71: 10 */
    {0, 180, 340, 45, UI_STR_MT_DO_NOT_REMOVE, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.0f, 170.0f, 170.0f, 170.0f, 170.0f}, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 72: Do not remove the / Memory Card (PS2) / or turn off the power */
    {0, 0, 256, 15, UI_STR_MT_ACCESSING, UI_ALIGN_CENTER, 0, {12.6f, 12.26f, 12.12f, 12.27f, 12.33f}, {128.62f, 125.5f, 128.62f, 129.0f, 129.0f}, 12.2f, {6.76f, 6.25f, 6.12f, 6.75f, 6.76f}, {0.948f, 0.974f, 0.988f, 0.994f, 0.975f}, {0.03f, 0.0f, 0.0f, -0.2f, -0.05f}, {0, 0, 0, 0, 0}}, /* 73: Accessing */
    {450, 75, 20, 15, UI_STR_MT_COLON, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.66f, 17.45f, 17.53f, 17.45f, 17.52f}, {10.5f, 10.13f, 10.0f, 10.12f, 10.13f}, 15.5f, {7.0f, 6.88f, 7.0f, 6.88f, 6.88f}, {1.384f, 1.048f, 1.03f, 1.045f, 0.98f}, {-0.62f, 1.32f, 0.0f, -0.06f, -0.12f}, {0, 0, 0, 0, 0}}, /* 74: : */
    {450, 45, 20, 15, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.49f, 14.37f, 14.37f, 6.37f, 10.25f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 76: 1 */
    {470, 45, 20, 15, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.26f, 10.26f, 10.26f, 10.38f, 10.13f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 77: 2 */
    {490, 45, 20, 15, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.26f, 10.26f, 10.25f, 10.26f, 10.63f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 78: 3 */
    {390, 60, 20, 15, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.0f, 10.13f, 10.13f, 10.25f, 10.13f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 79: 4 */
    {410, 60, 20, 15, UI_STR_MT_DIGIT_5, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.5f, 10.62f, 10.62f, 10.62f, 11.0f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 80: 5 */
    {430, 60, 20, 15, UI_STR_MT_DIGIT_6, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.38f, 10.12f, 10.0f, 10.0f, 10.12f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 81: 6 */
    {450, 60, 20, 15, UI_STR_MT_DIGIT_7, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {9.75f, 9.75f, 9.75f, 9.75f, 9.76f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 82: 7 */
    {390, 75, 20, 15, UI_STR_MT_DIGIT_8, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.38f, 10.38f, 10.38f, 10.38f, 10.38f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 83: 8 */
    {410, 75, 20, 15, UI_STR_MT_DIGIT_9, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.62f, 10.5f, 10.38f, 10.5f, 10.38f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 84: 9 */
    {430, 75, 20, 15, UI_STR_MT_DIGIT_0, UI_ALIGN_CENTER, UI_INK_PLAIN, {17.72f, 17.32f, 16.25f, 16.28f, 17.72f}, {10.37f, 10.38f, 10.37f, 10.38f, 10.38f}, 15.5f, {8.5f, 8.5f, 8.44f, 8.44f, 8.5f}, {1.095f, 1.172f, 1.22f, 1.233f, 1.068f}, {-0.205f, 0.0f, -0.05f, -0.135f, 0.105f}, {0, 0, 0, 0, 0}}, /* 85: 0 */
    {340, 0, 172, 15, UI_STR_MT_LOC_OLD_BRIDGE, UI_ALIGN_CENTER, 0, {15.24f, 15.04f, 15.33f, 15.12f, 15.23f}, {86.0f, 86.13f, 85.76f, 86.63f, 86.51f}, 15.5f, {5.76f, 5.88f, 5.76f, 5.87f, 5.76f}, {0.976f, 0.943f, 0.948f, 0.962f, 0.937f}, {-0.43f, 0.15f, -0.14f, -0.89f, -0.68f}, {0, 0, 0, 0, 0}}, /* 140: Old Bridge */
    {340, 30, 172, 15, UI_STR_MT_LOC_TROLLEY_A, UI_ALIGN_CENTER, 0, {14.98f, 15.18f, 15.19f, 15.09f, 13.09f}, {87.99f, 88.49f, 88.12f, 88.25f, 88.87f}, 15.5f, {5.88f, 5.88f, 5.87f, 5.88f, 6.0f}, {0.977f, 0.925f, 0.95f, 0.953f, 1.026f}, {-0.14f, 0.28f, -0.06f, 0.0f, -0.49f}, {0, 0, 0, 0, 0}}, /* 142: Trolley 2 */
    {300, 225, 210, 15, UI_STR_MT_LOC_MAIN_GATE, UI_ALIGN_CENTER, 0, {14.82f, 14.32f, 14.18f, 14.44f, 13.9f}, {106.0f, 107.36f, 106.12f, 104.74f, 107.38f}, 15.5f, {6.38f, 6.5f, 6.5f, 6.5f, 6.5f}, {0.969f, 1.022f, 1.011f, 1.012f, 1.031f}, {0.0f, -0.19f, 0.05f, -0.18f, 0.02f}, {0, 0, 0, 0, 0}}, /* 145: Main Gate */
    {340, 60, 172, 15, UI_STR_MT_LOC_GRAVEYARD, UI_ALIGN_CENTER, 0, {14.95f, 14.88f, 15.02f, 14.61f, 15.03f}, {86.0f, 86.51f, 86.12f, 86.62f, 86.38f}, 15.5f, {6.0f, 6.0f, 5.88f, 6.0f, 6.0f}, {0.955f, 0.974f, 0.963f, 0.998f, 0.963f}, {0.02f, -0.11f, -0.08f, -0.17f, -0.09f}, {0, 0, 0, 0, 0}}, /* 147: Graveyard */
    {320, 165, 192, 15, UI_STR_MT_LOC_WINDMILL, UI_ALIGN_CENTER, 0, {14.32f, 14.48f, 14.32f, 14.09f, 13.86f}, {100.13f, 96.25f, 99.49f, 96.62f, 95.25f}, 15.5f, {6.24f, 6.25f, 6.25f, 6.26f, 6.24f}, {0.99f, 0.983f, 0.99f, 1.039f, 1.08f}, {0.13f, 0.1f, 0.14f, -0.2f, -0.48f}, {0, 0, 0, 0, 0}}, /* 149: Windmill */
    {300, 240, 210, 15, UI_STR_MT_LOC_STONE_PILLAR, UI_ALIGN_CENTER, 0, {14.46f, 14.39f, 14.53f, 14.25f, 14.17f}, {105.88f, 105.5f, 105.62f, 106.24f, 106.25f}, 15.5f, {6.38f, 6.38f, 6.38f, 6.38f, 6.5f}, {0.995f, 1.025f, 0.995f, 1.022f, 1.013f}, {-0.06f, -0.31f, -0.11f, -0.16f, 0.0f}, {0, 0, 0, 0, 0}}, /* 151: Stone Pillar */
    {340, 75, 172, 15, UI_STR_MT_LOC_EAST_ARENA, UI_ALIGN_CENTER, 0, {15.26f, 15.18f, 14.82f, 14.82f, 15.02f}, {84.88f, 86.12f, 85.26f, 86.0f, 86.39f}, 15.5f, {5.88f, 5.88f, 6.0f, 6.0f, 6.0f}, {0.933f, 0.924f, 0.972f, 0.978f, 0.976f}, {0.25f, 0.3f, -0.06f, -0.09f, -0.25f}, {0, 0, 0, 0, 0}}, /* 153: East Arena */
    {340, 90, 172, 15, UI_STR_MT_LOC_EAST_REFLECTOR, UI_ALIGN_CENTER, 0, {15.19f, 15.26f, 15.12f, 14.88f, 14.95f}, {85.0f, 86.25f, 85.25f, 84.75f, 86.62f}, 15.5f, {5.88f, 5.88f, 6.0f, 6.0f, 6.0f}, {0.959f, 0.95f, 0.959f, 0.963f, 0.978f}, {-0.16f, -0.1f, -0.11f, 0.02f, -0.21f}, {0, 0, 0, 0, 0}}, /* 155: East Reflector */
    {340, 105, 172, 15, UI_STR_MT_LOC_WATERFALL, UI_ALIGN_CENTER, 0, {14.6f, 14.75f, 14.39f, 14.53f, 15.03f}, {87.0f, 86.5f, 86.88f, 86.75f, 86.5f}, 15.5f, {6.0f, 6.12f, 6.0f, 6.12f, 6.0f}, {0.987f, 1.008f, 1.001f, 1.004f, 0.976f}, {-0.07f, -0.44f, -0.06f, -0.14f, -0.25f}, {0, 0, 0, 0, 0}}, /* 156: Waterfall */
    {340, 120, 172, 15, UI_STR_MT_LOC_GONDOLA, UI_ALIGN_CENTER, 0, {14.89f, 14.79f, 14.89f, 14.46f, 14.74f}, {85.88f, 86.38f, 85.52f, 86.38f, 86.51f}, 15.5f, {6.12f, 6.13f, 6.13f, 6.13f, 6.12f}, {0.971f, 1.01f, 0.99f, 1.017f, 0.99f}, {-0.09f, -0.5f, -0.44f, -0.27f, -0.22f}, {0, 0, 0, 0, 0}}, /* 159: Gondola */
    {340, 135, 172, 15, UI_STR_MT_LOC_WATER_TOWER, UI_ALIGN_CENTER, 0, {14.66f, 14.46f, 15.37f, 14.38f, 14.68f}, {85.75f, 86.76f, 82.88f, 86.63f, 86.38f}, 15.5f, {6.12f, 6.25f, 6.0f, 6.25f, 6.26f}, {0.956f, 1.007f, 0.915f, 1.032f, 0.894f}, {0.32f, -0.19f, 0.2f, -1.11f, -0.3f}, {0, 0, 0, 0, 0}}, /* 160: Water Tower */
    {0, 60, 256, 15, UI_STR_MT_LOC_WEST_IDOL_STAIRS, UI_ALIGN_CENTER, 0, {14.22f, 14.29f, 13.9f, 14.2f, 14.3f}, {128.12f, 126.99f, 128.25f, 128.01f, 127.9f}, 13.9f, {6.62f, 6.38f, 6.38f, 6.62f, 6.62f}, {0.936f, 0.964f, 0.97f, 0.869f, 0.894f}, {0.06f, -0.61f, -0.06f, -0.2f, -0.95f}, {0, 0, 0, 0, 0}}, /* 163: West Idol Stairs */
    {340, 15, 172, 15, UI_STR_MT_LOC_TROLLEY_B, UI_ALIGN_CENTER, 0, {15.04f, 15.11f, 15.19f, 15.18f, 13.76f}, {88.0f, 88.5f, 88.12f, 88.62f, 87.12f}, 15.5f, {5.88f, 5.88f, 5.75f, 5.88f, 5.62f}, {0.973f, 0.973f, 0.954f, 0.963f, 0.956f}, {-0.12f, -0.25f, -0.12f, -0.19f, -0.25f}, {0, 0, 0, 0, 0}}, /* 173: Trolley 1 */
    {340, 45, 172, 15, UI_STR_MT_LOC_CRANE, UI_ALIGN_CENTER, 0, {15.26f, 15.39f, 14.51f, 14.8f, 15.4f}, {84.88f, 86.5f, 84.88f, 86.76f, 86.5f}, 15.5f, {5.88f, 5.88f, 5.75f, 6.0f, 5.88f}, {0.941f, 0.926f, 0.986f, 0.981f, 0.937f}, {0.0f, 0.0f, 0.02f, -0.1f, -0.22f}, {0, 0, 0, 0, 0}}, /* 174: Crane */
    {340, 150, 172, 15, UI_STR_MT_LOC_SANDY_BEACH, UI_ALIGN_CENTER, 0, {14.75f, 14.6f, 14.46f, 13.84f, 13.94f}, {85.87f, 86.5f, 85.75f, 86.37f, 86.5f}, 15.5f, {6.12f, 6.26f, 6.25f, 6.5f, 6.5f}, {0.966f, 1.016f, 0.996f, 0.868f, 0.973f}, {0.06f, -0.37f, -0.08f, -1.08f, -0.15f}, {0, 0, 0, 1, 0}}, /* 175: Sandy Beach */
    {0, 225, 300, 15, UI_STR_MT_SLOT_1, UI_ALIGN_CENTER, 1, {9.2f, 9.2f, 9.2f, 9.2f, 9.2f}, {149.0f, 149.0f, 149.0f, 149.0f, 149.0f}, 10.5f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 176: MEMORY CARD slot 1 */
    {0, 240, 300, 15, UI_STR_MT_SLOT_2, UI_ALIGN_CENTER, 1, {9.2f, 9.2f, 9.2f, 9.2f, 9.2f}, {150.5f, 150.5f, 150.5f, 150.5f, 150.5f}, 10.5f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 177: MEMORY CARD slot 2 */
    {390, 190, 100, 20, UI_STR_MT_OK, UI_ALIGN_LEFT, 0, {15.57f, 15.69f, 15.57f, 15.47f, 15.49f}, {6.0f, 6.87f, 6.0f, 6.0f, 6.87f}, 15.5f, {9.13f, 8.75f, 8.63f, 8.62f, 9.0f}, {0.953f, 0.94f, 0.945f, 0.943f, 0.934f}, {-0.44f, -0.12f, -0.43f, -0.09f, 0.0f}, {1, 2, 2, 2, 2}}, /* 181: OK */
    {384, 80, 128, 20, UI_STR_MT_BACK, UI_ALIGN_LEFT, 0, {13.77f, 13.84f, 14.39f, 13.63f, 13.63f}, {5.76f, 5.26f, 5.76f, 5.38f, 6.76f}, 15.5f, {9.38f, 9.13f, 9.12f, 9.25f, 9.13f}, {0.983f, 0.951f, 0.943f, 1.001f, 1.009f}, {-0.06f, 0.15f, -0.16f, -0.22f, -0.22f}, {1, 2, 2, 2, 2}}, /* 183: Back */
    {0, 120, 384, 30, UI_STR_MT_SELECT_SLOT, UI_ALIGN_CENTER, 0, {13.03f, 12.28f, 11.97f, 12.28f, 11.97f}, {192.0f, 192.0f, 192.13f, 192.0f, 192.0f}, 13.0f, {21.5f, 9.12f, 8.88f, 9.12f, 9.12f}, {0.92f, 0.957f, 1.001f, 0.975f, 0.999f}, {-0.03f, 0.15f, -0.02f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 185: Select MEMORY CARD slot. */
    {0, 165, 384, 15, UI_STR_MT_SELECT_LOAD, UI_ALIGN_CENTER, 0, {12.65f, 12.45f, 12.52f, 12.51f, 12.52f}, {191.88f, 192.0f, 192.12f, 192.0f, 191.88f}, 13.9f, {6.75f, 6.75f, 6.75f, 6.75f, 6.75f}, {0.951f, 0.955f, 0.952f, 0.947f, 0.95f}, {-0.05f, 0.05f, 0.03f, 0.08f, 0.05f}, {0, 0, 0, 0, 0}}, /* 195: Select a file to load. */
    {0, 0, 340, 30, UI_STR_MT_NO_CARD, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.5f, 170.5f, 170.5f, 170.5f, 170.5f}, 15.0f, {13.0f, 5.5f, 6.0f, 6.0f, 6.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 200: No Memory Card (PS2) inserted */
    {0, 20, 128, 20, UI_STR_MT_BACK, UI_ALIGN_CENTER, 0, {13.7f, 13.43f, 14.24f, 13.91f, 13.71f}, {63.38f, 64.5f, 63.88f, 64.12f, 64.12f}, 15.5f, {9.38f, 9.13f, 9.12f, 9.13f, 9.12f}, {1.01f, 1.003f, 0.993f, 0.996f, 0.975f}, {-0.42f, -0.03f, -0.74f, -0.39f, -0.03f}, {1, 2, 2, 2, 2}}, /* 201: Back */
    {0, 225, 340, 15, UI_STR_MT_NO_SAVE_DATA, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {171.0f, 171.0f, 171.0f, 171.0f, 171.0f}, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 205: There is no save data. */
    {0, 30, 256, 15, UI_STR_MT_LOADING, UI_ALIGN_CENTER, 0, {12.59f, 12.15f, 12.16f, 12.3f, 12.34f}, {126.88f, 124.01f, 126.76f, 129.01f, 128.88f}, 13.9f, {6.75f, 6.5f, 6.12f, 6.75f, 6.75f}, {0.957f, 0.964f, 0.999f, 0.977f, 0.952f}, {-0.06f, -0.03f, 0.0f, -0.3f, -0.05f}, {0, 0, 0, 0, 0}}, /* 211: Loading */
    {300, 180, 212, 25, UI_STR_MT_SAVE_Q, UI_ALIGN_CENTER, 0, {17.9f, 17.38f, 18.07f, 16.15f, 17.5f}, {103.88f, 106.87f, 104.62f, 102.99f, 105.36f}, 18.9f, {11.12f, 11.5f, 11.25f, 11.62f, 11.5f}, {0.957f, 0.977f, 0.975f, 1.054f, 0.971f}, {0.0f, 0.3f, -0.35f, -0.72f, 0.27f}, {2, 2, 2, 2, 2}}, /* 220: Save? */
    {0, 30, 340, 30, UI_STR_MT_NO_SPACE, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.5f, 170.5f, 170.5f, 170.5f, 170.5f}, 15.0f, {6.0f, 6.5f, 6.5f, 6.5f, 6.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 229: There is insufficient space / on the Memory Card (PS2). */
    {0, 240, 340, 15, UI_STR_MT_NEED_360, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.0f, 170.0f, 170.0f, 170.0f, 170.0f}, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 230: (Need 360KB or more) */
    {0, 150, 384, 15, UI_STR_MT_SELECT_SAVE, UI_ALIGN_CENTER, 0, {12.52f, 12.52f, 12.33f, 12.04f, 12.08f}, {191.0f, 191.88f, 191.76f, 191.88f, 191.88f}, 13.9f, {6.75f, 6.75f, 6.75f, 6.75f, 6.75f}, {0.955f, 0.955f, 0.963f, 1.0f, 0.986f}, {0.0f, 0.0f, 0.06f, -0.05f, 0.02f}, {0, 0, 0, 0, 0}}, /* 239: Select user file to save to. */
    {0, 120, 340, 45, UI_STR_MT_OVERWRITE, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.5f, 170.5f, 170.5f, 170.5f, 170.5f}, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 242: Saving will overwrite / the data on this file. / Is this okay? */
    {0, 90, 256, 45, UI_STR_MT_NOT_FORMATTED, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {129.2f, 129.2f, 129.2f, 129.2f, 129.2f}, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 244: The Memory Card (PS2) / is not formatted / Format? */
    {0, 45, 256, 15, UI_STR_MT_FORMATTING, UI_ALIGN_CENTER, 0, {12.45f, 12.46f, 11.86f, 12.77f, 12.22f}, {128.0f, 123.39f, 127.74f, 129.25f, 128.88f}, 13.9f, {6.75f, 6.5f, 6.25f, 6.63f, 6.75f}, {0.964f, 0.941f, 1.001f, 0.926f, 0.95f}, {-0.05f, -0.06f, 0.05f, -0.14f, 0.03f}, {0, 0, 0, 0, 0}}, /* 249: Formatting */
    {0, 15, 256, 15, UI_STR_MT_SAVING, UI_ALIGN_CENTER, 0, {12.57f, 12.1f, 12.04f, 12.52f, 12.34f}, {128.0f, 125.0f, 128.13f, 129.13f, 129.0f}, 13.9f, {6.74f, 6.38f, 6.25f, 6.75f, 6.75f}, {0.928f, 0.964f, 1.019f, 0.931f, 0.951f}, {0.25f, 0.0f, -0.28f, -0.03f, -0.05f}, {0, 0, 0, 0, 0}}, /* 254: Saving */
    {0, 75, 256, 15, UI_STR_MT_FILE_SAVED, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {129.2f, 129.2f, 129.2f, 129.2f, 129.2f}, 13.9f, {6.5f, 6.5f, 6.0f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 261: File saved. */
    {0, 135, 256, 20, UI_STR_MT_RESUME_GAME, UI_ALIGN_CENTER, 0, {13.69f, 13.77f, 14.25f, 13.57f, 13.63f}, {128.0f, 128.12f, 128.12f, 129.37f, 129.0f}, 15.5f, {9.62f, 9.38f, 9.0f, 9.13f, 9.25f}, {0.979f, 0.979f, 0.97f, 0.985f, 0.985f}, {0.0f, -0.08f, -0.3f, 0.0f, 0.0f}, {1, 2, 2, 2, 2}}, /* 264: Resume Game */
    {0, 155, 256, 20, UI_STR_MT_END_GAME, UI_ALIGN_CENTER, 0, {13.57f, 13.68f, 14.24f, 13.91f, 13.64f}, {127.99f, 127.63f, 128.25f, 129.75f, 129.01f}, 15.5f, {9.5f, 9.38f, 9.0f, 9.24f, 9.25f}, {0.99f, 0.98f, 0.954f, 0.965f, 0.994f}, {0.0f, -0.02f, -0.17f, 0.0f, -0.12f}, {1, 2, 2, 2, 2}}, /* 265: End Game */
    {0, 175, 256, 30, UI_STR_MT_GAME_WILL_END, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {129.0f, 129.0f, 129.0f, 129.0f, 129.0f}, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 269: The game will end. / Is this okay? */
    {0, 165, 320, 15, UI_STR_MT_SAVE_FAILED, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {161.0f, 161.0f, 161.0f, 161.0f, 161.0f}, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 275: Save failed. */
    {0, 90, 340, 15, UI_STR_MT_FORMAT_FAILED, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {171.5f, 171.5f, 171.5f, 171.5f, 171.5f}, 13.9f, {6.0f, 6.5f, 6.5f, 6.5f, 6.5f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 280: Format failed. */
    {0, 105, 340, 15, UI_STR_MT_LOAD_FAILED, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.5f, 170.5f, 170.5f, 170.5f, 170.5f}, 13.9f, {6.5f, 6.5f, 6.5f, 6.5f, 6.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 285: Load data failed */
    {0, 60, 340, 30, UI_STR_MT_NO_ACCESS, UI_ALIGN_CENTER, 1, {12.1f, 12.1f, 12.1f, 12.1f, 12.1f}, {170.0f, 170.0f, 170.0f, 170.0f, 170.0f}, 15.0f, {6.5f, 6.5f, 6.5f, 6.5f, 6.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0, 0}}, /* 290: Cannot access the / Memory Card (PS2) */
    {384, 120, 128, 20, UI_STR_MT_OPTIONS, UI_ALIGN_LEFT, 0, {13.64f, 13.76f, 14.46f, 13.91f, 13.98f}, {6.38f, 6.76f, 6.01f, 6.13f, 6.26f}, 15.5f, {9.37f, 9.24f, 9.12f, 9.25f, 9.25f}, {0.999f, 0.982f, 0.934f, 0.963f, 0.977f}, {-0.2f, -0.11f, -0.12f, -0.02f, -0.28f}, {1, 2, 2, 2, 2}}, /* 294: Options */
    {128, 20, 128, 20, UI_STR_MT_PAUSE_BACK, UI_ALIGN_LEFT, 0, {13.56f, 13.63f, 14.36f, 13.5f, 13.5f}, {5.75f, 6.12f, 7.0f, 6.0f, 6.0f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.12f, 9.12f}, {0.994f, 0.98f, 0.936f, 0.99f, 0.99f}, {0.0f, 0.03f, -0.05f, 0.0f, 0.0f}, {1, 2, 2, 2, 2}}, /* 295: Back */
    {128, 0, 212, 20, UI_STR_MT_END_GAME, UI_ALIGN_LEFT, 0, {13.56f, 13.57f, 13.84f, 13.7f, 13.57f}, {5.88f, 6.13f, 6.0f, 6.63f, 7.25f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.995f, 0.98f, 0.976f, 0.977f, 0.991f}, {-0.05f, 0.05f, -0.11f, 0.01f, -0.06f}, {1, 2, 2, 2, 2}}, /* 296: End Game */
    {384, 225, 128, 20, UI_STR_MT_OPTIONS, UI_ALIGN_CENTER, 0, {13.43f, 13.91f, 13.89f, 13.8f, 13.91f}, {64.0f, 64.38f, 64.5f, 62.0f, 64.51f}, 15.5f, {9.26f, 9.25f, 9.12f, 9.25f, 9.74f}, {1.006f, 0.977f, 1.0f, 0.983f, 0.977f}, {-0.11f, -0.17f, -0.43f, -0.17f, -0.22f}, {0, 2, 2, 2, 2}}, /* 299: Options */
    {0, 100, 192, 20, UI_STR_MT_FILM_EFFECT, UI_ALIGN_RIGHT, 0, {14.09f, 14.05f, 13.76f, 13.57f, 14.04f}, {184.12f, 185.12f, 185.38f, 186.12f, 186.12f}, 15.5f, {9.38f, 9.25f, 9.0f, 9.25f, 9.25f}, {0.966f, 0.957f, 0.988f, 1.009f, 0.979f}, {-0.15f, -0.02f, -0.14f, -0.19f, -0.25f}, {1, 2, 2, 2, 2}}, /* 300: Film Effect */
    {390, 90, 40, 20, UI_STR_MT_DIGIT_0, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.35f, 18.44f, 18.16f, 18.16f, 18.34f}, {20.5f, 20.5f, 20.5f, 20.5f, 20.5f}, 15.5f, {10.0f, 10.12f, 10.12f, 10.25f, 10.12f}, {1.111f, 1.117f, 1.159f, 1.139f, 1.137f}, {0.22f, 0.68f, -0.41f, 0.84f, 0.21f}, {2, 2, 2, 2, 2}}, /* 303: 0 */
    {430, 90, 40, 20, UI_STR_MT_DIGIT_1, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.35f, 18.44f, 18.16f, 18.16f, 18.34f}, {23.12f, 23.0f, 22.62f, 22.5f, 22.62f}, 15.5f, {10.0f, 10.12f, 10.12f, 10.25f, 10.12f}, {1.111f, 1.117f, 1.159f, 1.139f, 1.137f}, {0.22f, 0.68f, -0.41f, 0.84f, 0.21f}, {2, 2, 2, 2, 2}}, /* 304: 1 */
    {470, 90, 40, 20, UI_STR_MT_DIGIT_2, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.35f, 18.44f, 18.16f, 18.16f, 18.34f}, {20.38f, 20.25f, 20.38f, 20.14f, 20.38f}, 15.5f, {10.0f, 10.12f, 10.12f, 10.25f, 10.12f}, {1.111f, 1.117f, 1.159f, 1.139f, 1.137f}, {0.22f, 0.68f, -0.41f, 0.84f, 0.21f}, {2, 2, 2, 2, 2}}, /* 305: 2 */
    {390, 110, 40, 20, UI_STR_MT_DIGIT_3, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.35f, 18.44f, 18.16f, 18.16f, 18.34f}, {20.38f, 20.5f, 20.37f, 20.5f, 20.5f}, 15.5f, {10.0f, 10.12f, 10.12f, 10.25f, 10.12f}, {1.111f, 1.117f, 1.159f, 1.139f, 1.137f}, {0.22f, 0.68f, -0.41f, 0.84f, 0.21f}, {2, 2, 2, 2, 2}}, /* 306: 3 */
    {430, 110, 40, 20, UI_STR_MT_DIGIT_4, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.35f, 18.44f, 18.16f, 18.16f, 18.34f}, {20.12f, 20.12f, 20.12f, 20.12f, 20.12f}, 15.5f, {10.0f, 10.12f, 10.12f, 10.25f, 10.12f}, {1.111f, 1.117f, 1.159f, 1.139f, 1.137f}, {0.22f, 0.68f, -0.41f, 0.84f, 0.21f}, {2, 2, 2, 2, 2}}, /* 307: 4 */
    {426, 40, 86, 20, UI_STR_MT_SOUND, UI_ALIGN_RIGHT, 0, {13.69f, 15.05f, 13.96f, 15.04f, 15.11f}, {80.13f, 80.5f, 80.37f, 79.62f, 80.37f}, 17.2f, {9.62f, 8.62f, 9.12f, 8.62f, 8.62f}, {0.999f, 0.994f, 0.987f, 0.984f, 0.978f}, {-0.35f, -0.66f, -0.43f, -0.33f, -0.33f}, {1, 2, 2, 2, 2}}, /* 308: Sound */
    {128, 160, 128, 20, UI_STR_MT_STEREO, UI_ALIGN_CENTER, 0, {13.33f, 15.04f, 15.54f, 14.65f, 15.4f}, {63.88f, 63.63f, 64.26f, 63.75f, 65.5f}, 17.2f, {9.37f, 8.75f, 8.87f, 8.87f, 8.75f}, {1.066f, 0.98f, 0.972f, 1.039f, 0.96f}, {-0.69f, -0.39f, -0.66f, -0.74f, -0.36f}, {1, 2, 2, 2, 2}}, /* 311: Stereo */
    {340, 20, 86, 20, UI_STR_MT_MONO, UI_ALIGN_CENTER, 0, {13.9f, 14.74f, 15.53f, 14.74f, 14.9f}, {43.63f, 43.75f, 43.75f, 43.75f, 43.75f}, 15.5f, {9.37f, 8.87f, 8.75f, 8.88f, 8.87f}, {0.987f, 0.96f, 0.937f, 0.959f, 0.948f}, {-0.31f, 0.22f, -0.17f, 0.25f, 0.27f}, {1, 2, 2, 2, 2}}, /* 312: Mono */
    {384, 100, 128, 20, UI_STR_MT_VIBRATION, UI_ALIGN_RIGHT, 0, {13.5f, 14.04f, 13.9f, 13.77f, 14.05f}, {122.0f, 121.74f, 122.25f, 121.75f, 121.62f}, 15.5f, {9.62f, 9.24f, 9.12f, 9.25f, 9.25f}, {0.985f, 0.946f, 0.991f, 0.97f, 0.934f}, {0.06f, 0.05f, -0.29f, 0.0f, 0.17f}, {1, 2, 2, 2, 2}}, /* 313: Vibration */
    {192, 100, 192, 20, UI_STR_MT_HOLD_TYPE, UI_ALIGN_RIGHT, 0, {14.25f, 14.05f, 14.4f, 13.7f, 13.9f}, {185.38f, 184.99f, 185.01f, 185.87f, 185.63f}, 15.5f, {9.49f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.946f, 0.96f, 0.927f, 0.969f, 0.971f}, {-0.06f, -0.07f, 0.02f, 0.09f, -0.11f}, {1, 2, 2, 2, 2}}, /* 318: Hold Type */
    {470, 110, 40, 20, UI_STR_MT_VAL_A, UI_ALIGN_CENTER, UI_INK_LIGHT, {18.36f, 18.55f, 18.27f, 18.54f, 18.36f}, {20.0f, 20.0f, 20.0f, 20.0f, 20.0f}, 15.5f, {10.12f, 10.25f, 10.12f, 10.25f, 10.12f}, {1.147f, 1.154f, 1.144f, 1.156f, 1.147f}, {0.0f, -0.11f, 0.42f, 0.02f, 0.0f}, {2, 2, 2, 2, 2}}, /* 321: A */
    {470, 70, 40, 20, UI_STR_MT_VAL_B, UI_ALIGN_CENTER, UI_INK_LIGHT, {17.65f, 19.08f, 18.13f, 18.22f, 18.31f}, {20.75f, 20.5f, 20.75f, 20.62f, 20.62f}, 15.5f, {10.26f, 10.38f, 10.13f, 10.13f, 10.13f}, {1.297f, 1.167f, 1.218f, 1.241f, 1.231f}, {-0.34f, 0.02f, -0.32f, -0.52f, -0.48f}, {2, 2, 2, 2, 2}}, /* 322: B */
    {0, 120, 256, 20, UI_STR_MT_BUTTON_CONFIG, UI_ALIGN_RIGHT, 0, {13.49f, 14.18f, 14.31f, 13.62f, 14.05f}, {249.76f, 248.63f, 250.63f, 249.75f, 249.88f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.24f}, {1.043f, 0.934f, 0.961f, 1.03f, 0.946f}, {-0.53f, 0.1f, -0.28f, -0.49f, 0.08f}, {1, 2, 2, 2, 2}}, /* 323: Button Configuration */
    {256, 120, 128, 20, UI_STR_MT_BRIGHTNESS, UI_ALIGN_RIGHT, 0, {13.63f, 13.63f, 14.25f, 13.57f, 13.77f}, {121.12f, 121.5f, 121.0f, 121.62f, 121.88f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.13f}, {0.992f, 0.994f, 0.952f, 1.006f, 0.973f}, {-0.12f, -0.19f, -0.12f, -0.26f, -0.01f}, {1, 2, 2, 2, 2}}, /* 324: Brightness */
    {0, 140, 128, 20, UI_STR_MT_PLAYERS, UI_ALIGN_RIGHT, 0, {14.39f, 14.04f, 13.83f, 14.19f, 14.04f}, {121.24f, 120.74f, 120.62f, 121.86f, 121.62f}, 15.5f, {9.5f, 9.25f, 9.12f, 9.25f, 9.26f}, {0.932f, 0.956f, 1.033f, 0.983f, 0.955f}, {-0.02f, -0.06f, -0.67f, -0.45f, -0.05f}, {1, 2, 2, 2, 2}}, /* 325: Players */
    {0, 225, 256, 20, UI_STR_MT_BUTTON_CONFIG, UI_ALIGN_CENTER, 0, {13.84f, 13.96f, 14.45f, 13.83f, 14.04f}, {127.88f, 128.13f, 128.25f, 128.38f, 128.5f}, 15.5f, {9.38f, 9.75f, 9.12f, 9.25f, 9.75f}, {0.991f, 0.972f, 0.935f, 0.981f, 0.955f}, {-0.25f, -0.12f, -0.1f, -0.12f, 0.0f}, {0, 2, 2, 2, 2}}, /* 335: Button Configuration */
    {128, 140, 128, 20, UI_STR_MT_JUMP, UI_ALIGN_RIGHT, 0, {13.5f, 14.05f, 13.98f, 13.83f, 13.7f}, {121.0f, 121.12f, 121.75f, 121.88f, 121.0f}, 15.5f, {9.74f, 9.13f, 9.12f, 9.12f, 9.13f}, {0.99f, 0.971f, 0.963f, 0.983f, 0.978f}, {0.05f, -0.16f, -0.08f, -0.16f, -0.03f}, {1, 2, 2, 2, 2}}, /* 336: Jump */
    {256, 140, 128, 20, UI_STR_MT_ATTACK, UI_ALIGN_RIGHT, 0, {13.9f, 13.76f, 14.19f, 13.84f, 13.76f}, {120.88f, 120.0f, 122.25f, 122.38f, 121.0f}, 15.5f, {9.5f, 9.24f, 9.12f, 9.24f, 9.24f}, {0.97f, 0.977f, 0.959f, 0.999f, 0.977f}, {-0.08f, -0.03f, -0.18f, -0.29f, -0.03f}, {1, 2, 2, 2, 2}}, /* 337: Attack */
    {426, 20, 86, 20, UI_STR_MT_ACTION, UI_ALIGN_RIGHT, 0, {13.56f, 14.98f, 13.96f, 15.11f, 15.15f}, {80.01f, 81.02f, 80.01f, 79.63f, 80.5f}, 15.5f, {9.25f, 8.63f, 9.12f, 8.64f, 8.63f}, {1.001f, 0.969f, 0.986f, 0.953f, 0.955f}, {-0.12f, -0.13f, -0.25f, -0.08f, -0.06f}, {1, 2, 2, 2, 2}}, /* 338: Action */
    {0, 160, 128, 20, UI_STR_MT_RELEASE, UI_ALIGN_RIGHT, 0, {13.23f, 15.0f, 14.25f, 15.11f, 15.1f}, {122.38f, 119.99f, 122.25f, 121.62f, 121.0f}, 17.2f, {9.5f, 8.87f, 8.88f, 8.88f, 8.75f}, {1.038f, 0.994f, 1.078f, 0.942f, 0.96f}, {0.0f, -0.52f, -0.85f, 0.05f, -0.09f}, {1, 2, 2, 2, 2}}, /* 339: Release */
    {0, 60, 300, 20, UI_STR_MT_HOLD_HAND_CALL, UI_ALIGN_RIGHT, 0, {13.7f, 13.74f, 14.05f, 13.63f, 14.11f}, {293.63f, 291.87f, 293.24f, 294.12f, 292.01f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.24f}, {0.978f, 0.943f, 0.962f, 0.998f, 0.967f}, {-0.01f, 0.43f, -0.12f, -0.18f, -0.22f}, {1, 2, 2, 2, 2}}, /* 340: Hold hand / Call */
    {340, 40, 86, 20, UI_STR_MT_ZOOM, UI_ALIGN_RIGHT, 0, {13.68f, 14.68f, 15.13f, 14.74f, 15.02f}, {80.87f, 80.88f, 80.12f, 80.75f, 80.63f}, 17.2f, {9.38f, 8.75f, 8.88f, 8.75f, 8.74f}, {1.006f, 1.023f, 0.978f, 1.006f, 0.967f}, {-0.25f, -0.7f, -0.46f, -0.47f, -0.06f}, {1, 2, 2, 2, 2}}, /* 341: Zoom */
    {300, 60, 210, 20, UI_STR_MT_DEFAULT, UI_ALIGN_LEFT, 0, {13.84f, 13.5f, 13.77f, 13.69f, 13.57f}, {5.63f, 7.13f, 5.5f, 5.87f, 5.75f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.954f, 0.99f, 1.008f, 1.003f, 0.985f}, {0.17f, 0.0f, -0.4f, -0.26f, 0.0f}, {1, 2, 2, 2, 2}}, /* 391: Default */
    {256, 225, 128, 20, UI_STR_MT_BRIGHTNESS, UI_ALIGN_CENTER, 0, {13.36f, 13.77f, 13.91f, 13.63f, 13.5f}, {64.0f, 64.38f, 63.26f, 64.5f, 64.5f}, 15.5f, {9.25f, 8.25f, 9.12f, 9.25f, 9.75f}, {1.039f, 0.975f, 1.008f, 0.983f, 0.995f}, {-0.4f, -0.03f, -0.4f, 0.02f, 0.0f}, {0, 2, 2, 2, 2}}, /* 394: Brightness */
    {0, 80, 384, 20, UI_STR_MT_ADJUST_HINT, UI_ALIGN_CENTER, 0, {13.43f, 14.02f, 13.96f, 13.71f, 13.84f}, {193.0f, 191.63f, 191.38f, 192.37f, 192.37f}, 15.5f, {9.38f, 9.25f, 9.12f, 9.25f, 9.25f}, {0.964f, 0.955f, 0.982f, 0.959f, 0.979f}, {0.31f, 0.0f, -0.24f, 0.16f, -0.12f}, {1, 2, 2, 2, 2}}, /* 413: Adjust with directional buttons */
    {390, 150, 100, 20, UI_STR_MT_DARK, UI_ALIGN_CENTER, 0, {13.97f, 15.45f, 15.26f, 17.31f, 15.7f}, {48.75f, 49.12f, 50.13f, 50.5f, 49.75f}, 15.5f, {9.38f, 9.0f, 8.76f, 9.39f, 8.88f}, {1.046f, 0.954f, 0.949f, 1.189f, 0.924f}, {-0.25f, -0.4f, -0.11f, -0.09f, -0.17f}, {1, 2, 2, 2, 2}}, /* 414: Dark */
    {390, 130, 100, 20, UI_STR_MT_LIGHT, UI_ALIGN_CENTER, 0, {13.81f, 15.78f, 15.11f, 20.51f, 15.7f}, {48.88f, 50.25f, 50.01f, 50.13f, 50.63f}, 15.5f, {9.38f, 8.88f, 8.88f, 11.0f, 8.88f}, {1.063f, 0.918f, 0.964f, 1.121f, 0.923f}, {-0.23f, -0.06f, -0.18f, 0.0f, -0.14f}, {1, 2, 2, 2, 2}}, /* 415: Light */
    {0, 205, 256, 35, UI_STR_MT_GAME_WILL_END, UI_ALIGN_CENTER, 0, {13.5f, 13.77f, 14.32f, 14.04f, 13.9f}, {128.0f, 128.75f, 129.24f, 129.24f, 129.24f}, 15.0f, {9.62f, 9.62f, 9.12f, 9.25f, 9.25f}, {0.998f, 0.988f, 0.941f, 0.955f, 0.982f}, {-0.06f, -0.17f, -0.06f, 0.0f, -0.2f}, {1, 2, 2, 2, 2}}, /* 423: The game will end. / Is this okay? */
    {0, 180, 300, 25, UI_STR_MT_CONTINUE_Q, UI_ALIGN_CENTER, 0, {17.48f, 17.28f, 18.58f, 17.3f, 17.08f}, {149.14f, 156.62f, 149.5f, 150.0f, 149.88f}, 18.9f, {11.26f, 11.51f, 11.0f, 11.5f, 11.62f}, {0.984f, 0.989f, 0.937f, 0.964f, 0.978f}, {0.0f, 0.24f, -0.2f, 0.58f, 0.55f}, {2, 2, 2, 2, 2}}, /* 428: Continue ? */
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

/* the row -> item map, built on first use: -1 for a row not in the table */
#define MT_GAME_ROWS 436
static short s_itemOf[MT_GAME_ROWS];
static int s_mapBuilt;

static void buildMap(void)
{
    for (int i = 0; i < MT_GAME_ROWS; i++) {
        s_itemOf[i] = -1;
    }
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        const UiMenuTextRow *r = &ui_menu_text_rows[i];
        if (r->row >= 0 && r->row < MT_GAME_ROWS) {
            s_itemOf[r->row] = r->item;
        }
    }
    s_mapBuilt = 1;
}

const UiMenuTextItem *ui_MenuTextItemOf(const LtProperty *e)
{
    if (!e || e < texProperty || e >= texProperty + MT_GAME_ROWS) {
        return NULL;
    }
    if (!s_mapBuilt) {
        buildMap();
    }
    const int i = s_itemOf[e - texProperty];
    if (i < 0) {
        return NULL;
    }
    const UiMenuTextItem *it = &ui_menu_text_items[i];
    if (e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h) {
        return NULL;
    }
    return it;
}
