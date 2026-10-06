/*
 * port/ui/game_font.h
 *
 * The game's own lettering as the port's font (docs/port/UI.md, "The font"):
 * the letters of the PAL menu sheets (text/menu_PAL_{EG,FR,GR,IT,SP}/
 * menu_PAL_01..04.tm2, scei.tm2, title.tm2), segmented out of the word
 * rectangles the menu text table names (menu_text.c), measured, and packed
 * into one glyph atlas with a metrics table.  font.c draws it as the "game"
 * face; Arimo stands in for the characters the sheets never show.
 *
 * Three parts:
 *   - the builder (game_font_build.c, no game or disc code): sources in (a
 *     sheet's pixels, a word rectangle, its transcribed string, the line
 *     metrics), one blob out;
 *   - the disc side (game_font_disc.c): the sources from the disc's sheets
 *     and the game's texProperty / texFile tables, and the blob kept as a
 *     file in the per-user folder beside ico.o2r (UI_GF_FILE_FMT), extracted
 *     on the first start and again when the format version or the disc
 *     changes (both are in the file's name) or the file does not load;
 *   - the face (font.c ui_GameFaceLoad): the blob read back.
 *
 * Nothing of the disc is in the repository: the blob is made on the
 * player's machine from the player's disc.
 *
 * The blob (little-endian):
 *   0   char[4] "ICGF"
 *   4   u32     UI_GF_VERSION
 *   8   u16     atlas width, u16 atlas height (texels, R8 coverage)
 *   12  f32     em: the main size class's em, texels (13.5, the menu rows)
 *   16  f32     cap: the main class's capital height at half coverage, texels
 *   20  f32     space: the space's advance, texels
 *   24  u32     glyph count, u32 kerning pair count, u32 sheet count
 *   36  glyphs, 132 bytes each:
 *         u32 code point; f32 scale (main texels per cell texel);
 *         u16 ink x, y, w, h (the ink cell: the letter's light fill, a zero
 *             border round it);
 *         then three alpha / light pairs (the main cell, the left cap, the
 *         right cap), 16 bytes each: u16 alpha x, y, light x, y, w, h (with
 *         a one-texel apron: the quads cover the inner texels), f32 the
 *         first inner row from the baseline (main texels);
 *         f32 ink dx, dy (the ink cell's top-left from the pen on the
 *             baseline, main texels, y down);
 *         f32 advance (main texels: the main cell's inner width; the cells
 *             of a word tile; a cap is the cap width, a cell texel wide);
 *         u16 sheet (index into the sheet names); u8 language (0..4,
 *             EG FR GR IT SP); u8 instances (how many the letter had, 255 max);
 *         f32 the glow cells' dx, dy from the pen on the baseline; u16
 *             their w, h; u16 x, y of the four glow cells (the fitted glow,
 *             zero where the sheet's texels are kept: inside a word, at its
 *             start, at its end, a one-letter word);
 *         u16 x, y of the main cell's alpha and light in the same three
 *             other variants (at a word's start, end, alone: on an open
 *             side only the letter's own surroundings are the sheet's)
 *       kerning pairs, 12 bytes each: u32 left, u32 right, f32 adjustment
 *       (main texels, added to the left glyph's advance)
 *       sheet names, 48 bytes each (NUL-padded)
 *       the atlas, width x height bytes (0..255).  An alpha cell is the
 *       sheet's alpha round the letter, a light cell its premultiplied light
 *       (luminance x alpha), both the sheet's texels as cut within two of
 *       the letters' ink: drawn (after the glow cells, black, overlapping) as
 *       the alpha in black (blend 0x44) then the light added in the text's
 *       colour (0x48), they give the sprite's MODULATE blend exactly,
 *       dst (1 - A a) + col L A a.  The caps are drawn at a word's ends only.
 */
#ifndef PORT_UI_GAME_FONT_H
#define PORT_UI_GAME_FONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_GF_MAGIC "ICGF"
/* bump when the segmentation, the measurements or the format change: the
   file's name carries the version, so an older file is simply not read */
#define UI_GF_VERSION 1
/* the file in the per-user folder: printf'd with UI_GF_VERSION and the disc
   image's SHA-1 (40 hex digits, as the archive's meta.json records it) */
#define UI_GF_FILE_FMT "gamefont-%d-%s.bin"
/* a larger file is not read (the blob is about 1.6 MB) */
#define UI_GF_FILE_MAX (64L * 1024 * 1024)
#define UI_GF_HEADER 36
#define UI_GF_GLYPH 132
#define UI_GF_KERN 12
#define UI_GF_SHEET_NAME 48

/* One word rectangle of a sheet. */
typedef struct UiGfSource {
    const uint8_t *rgba; /* the whole sheet, RGBA8, alpha 0..255 */
    int sheetW, sheetH;
    int u, v, w, h;   /* the rectangle */
    int dark;         /* black letters (the white panel): the ink is the alpha */
    float em;         /* the table's em, texels: the size class */
    float capMid;     /* the first line's capital middle, texels from the top */
    float pitch;      /* texels from one line to the next */
    const char *text; /* the rectangle's words (UTF-8, '\n' between lines) */
    int sheet;        /* the caller's sheet index (ui_GfBuilderSheet) */
    int lang;         /* 0..4 */
} UiGfSource;

typedef struct UiGfStats {
    int sources;   /* rectangles given */
    int lines;     /* lines with ink and letters */
    int exact;     /* lines whose components were the letters one for one */
    int aligned;   /* lines aligned with touching pairs split */
    int splits;    /* components split into several letters */
    int unaligned; /* lines left out: no alignment */
    int instances; /* letters measured */
    int glyphs;    /* distinct characters kept */
    int kerns;     /* kerning pairs kept */
} UiGfStats;

typedef struct UiGfBuilder UiGfBuilder;

UiGfBuilder *ui_GfBuilderNew(void);
void ui_GfBuilderFree(UiGfBuilder *b);
/* names a sheet (reported per glyph); returns its index, or -1 when out of
   memory */
int ui_GfBuilderSheet(UiGfBuilder *b, const char *name);
/* adds a rectangle (its pixels are copied); 0, or -1 when out of memory or
   the rectangle is outside the sheet */
int ui_GfBuilderAdd(UiGfBuilder *b, const UiGfSource *src);
/* segments every rectangle, picks one bitmap per character, fits the
   spacing and packs the atlas.  mainEm: the size class the glyphs are kept
   at when they appear in it (13.5, the menu rows).  *blob is malloc'd. 0, or
   -1 with nothing written. */
int ui_GfBuilderFinish(UiGfBuilder *b, float mainEm, uint8_t **blob, size_t *size,
                       UiGfStats *stats);

/* ------------------------------------------------ the disc (game_font_disc.c) */

struct IcoVfs;
/* Builds the blob from the disc's sheets (the vfs's DATA.DF packs) for the
   rows of the menu text table, through the game's texProperty and texFile
   (loaded).  0, or -1 with the reason in why. */
int ui_GameFontBuild(struct IcoVfs *vfs, uint8_t **blob, size_t *size, UiGfStats *stats, char *why,
                     size_t whysize);
/* The start-up step (window build, after the tables are loaded): reads the
   blob from cachePath (the per-user folder's UI_GF_FILE_FMT) when it loads,
   else builds it from the mounted disc and writes it there (a temporary file
   moved over the old one, so an interrupted write leaves the old file or
   none); then hands it to the font (ui_GameFaceLoad).  cachePath NULL (the
   disc unidentified: use_iso with the SHA-1 check skipped): built in memory
   each start.  0, or -1 (logged; the port font is then Arimo alone). */
int ui_GameFontPrepare(const char *cachePath);
/* The write step: blob to path through path.tmp, flushed to disk and moved
   over path.  0, or -1 with the reason in why (path.tmp removed). */
int ui_GameFontWriteFile(const char *path, const void *blob, size_t size, char *why,
                         size_t whysize);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_GAME_FONT_H */
