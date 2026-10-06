/*
 * port/ui/test/subtitles.h
 *
 * The game's subtitles as text (package TXT; docs/port/UI.md,
 * "Subtitles"): test data, never drawn.  The game draws its subtitles as
 * the pictures they are; the transcriptions are part of the font coverage
 * corpus (font_coverage_test.c) and are checked by menu_text_test.c.
 *
 * The PS2 game streams its subtitles as pictures: DATA.DF's
 * data_<LL><SS>.jim (jimaku.c's jimakuFileName[]: five languages, SS 01 for
 * the first run, 02 once the game is cleared) is a run of 0x8800-byte
 * blocks, one TIM2 a block, and jimakuDisp draws the current block's picture
 * as two sprites (texProperty 434 and 435: the left and right halves of a
 * 512 x 48 texel strip, shown at x 64..576 from dispY 144, a field line a
 * texel).  This module holds, per language and set, the words of every
 * block that shows Latin text, transcribed from the decoded pictures
 * (tools/tm2_sheets.py; nothing of the disc is kept), with each line's
 * centre measured on the strip.  Blocks in Yorda's script, the Japanese
 * placeholders the PAL files carry where the English one is blank, and
 * empty blocks have no entry.
 *
 * A text has one or two lines ('\n'): a single line sits in the strip's
 * lower slot, two lines fill both.  The slots' capital middles and the em
 * are per language (the sheets use three faces: a hand-lettered one in
 * English, another in German, a bold italic sans in French, Italian and
 * Spanish).
 */
#ifndef PORT_UI_SUBTITLES_H
#define PORT_UI_SUBTITLES_H

#include "strings.h" /* UiLang */

#ifdef __cplusplus
extern "C" {
#endif

/* the blocks in a subtitle file: 4,038,656 bytes / 0x8800 */
#define UI_SUB_BLOCKS 116
/* the strip the two rows show, texels */
#define UI_SUB_STRIP_W 512
#define UI_SUB_STRIP_H 48

/* One block's text. */
typedef struct UiSubtitle {
    short block;      /* the block's index in its file */
    float x[2];       /* each line's centre on the strip, texels (0 .. 512) */
    const char *text; /* UTF-8, the sheet's words, lines split at '\n' */
} UiSubtitle;

/* The lettering of one language's files, measured on the strip. */
typedef struct UiSubtitleFace {
    float em;   /* texels (vertical): the capitals' height less 0.7 over 0.688 */
    float y[2]; /* the capital middle of the upper and the lower slot, texels */
} UiSubtitleFace;

/* set: 0 the first run (data_<LL>01.jim), 1 after the game is cleared
   (data_<LL>02.jim) */
const UiSubtitle *ui_SubtitleFind(UiLang lang, int set, int block);
const UiSubtitleFace *ui_SubtitleFace(UiLang lang);
/* the table of (lang, set): *count entries sorted by block */
const UiSubtitle *ui_SubtitleTable(UiLang lang, int set, int *count);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SUBTITLES_H */
