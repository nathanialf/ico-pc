/* menu_jpn_test.c: the Japanese disc's menus as the port reads them.
 *
 * The port names the game's menus by number (a layout, a row, a button
 * glyph's rectangle); on the Japanese disc they are its own numbers, the
 * region table's (port/core/region_jpn.c).  Over the Japanese boot ELF's
 * tables (the loader's JPN copy, port/data/gen/jpn/):
 *
 *   text  every layout the region names runs the procedure it is named
 *         for; the title's, pause menu's and memory card screen's rows are
 *         where the Settings menu and the mouse expect them; the glyph rows'
 *         texel rectangles and heights (IcoGlyphRow) are the table's; the
 *         tables' sizes are the region's counts; and no row of the Japanese
 *         tables is drawn from the menu text table (the European sheets'
 *         words): the Japanese menu words keep their own texels.
 *   look  with the disc image: the sheets decoded through the disc's packs
 *         (menu_sheets.c, as menu_look_test does on the European disc):
 *         each glyph's rectangle (its ink and the box of it), the light
 *         words of the pause menu and the title (their fill and their
 *         rim), and over them the Japanese sheets' ink, the port's own rows
 *         over these sheets are drawn with (menu_font.c
 *         kSheetInk[UI_SHEET_JA]), printed and held to that table within
 *         menu_look's tolerances.
 *
 *   dump  with the disc image, pictures to compare the port's Japanese
 *         words with the disc's lettering (not a ctest; no checks):
 *         OUT_DIR/sheets/<file>.png every sheet the rows name, whole;
 *         OUT_DIR/rows/<row>.png every row with ink, x4 nearest, its number
 *         above, and rows_contact.png of them all at x2;
 *         OUT_DIR/port/<id>.png every UI_STR_MT_* string and the port's
 *         title, pause and Settings page title words in Japanese, drawn
 *         as ui_draw_menu_text strips them (ui__menu_text_raster) in the
 *         Japanese sheets' light ink, x4, and port_contact.png at x2;
 *         OUT_DIR/compare.png each UI_STR_MT_* string beside the rows it
 *         transcribes (the region table's title and pause rows, and with
 *         the European ELF the European layouts' rows of menu_text.c),
 *         x4; OUT_DIR/index.txt the sheets, the rows (sheet, rectangle,
 *         word), the strings and what has no partner.
 *
 *   menu_jpn_test text BASE_ELF_JPN
 *   menu_jpn_test look BASE_ELF_JPN DISC_IMAGE_JPN
 *   menu_jpn_test dump BASE_ELF_JPN DISC_IMAGE_JPN OUT_DIR [BASE_ELF_PAL]
 *
 * Exit 0, 1 on a failure, 77 without the ELF (or, for look and dump, the
 * disc).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charFileManager.h" /* texFile */
#include "../../core/region.h"
#include "host_fs.h"
#include "layout_texture.h" /* texProperty, texLayout */
#include "menu_font.h"
#include "menu_sheets.h"
#include "menu_text.h"
#include "rd_internal.h" /* rd_write_png */
#include "sheet_ref.h"
#include "strings.h"
#include "tables.h"
#include "ui_internal.h"
#include "vfs.h"
#include "gen/table_desc.h"

/* the procedures the region's layouts are named for (the loader's stubs:
   port/data/gen/jpn/ee_symbols.c with ICO_EE_SYMBOLS_STUBS) */
void la_game_pause(void);
void la_title_continue_or_new(void);
void la_title_new_game_only(void);
void la_game_option(void);
void la_key_config(void);
void la_vibe_select(void);
void la_game_loop(void);
void la_game_demo(void);
void la_mc_file_select(void);
void la_load_processing(void);
void la_save_confirm_complete(void);

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint8_t *readAll(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return b;
}

/* a table's row count as the loader describes it (the Japanese set) */
static int rowsOf(const void *host)
{
    for (uint32_t i = 0; i < ico_table_set_jpn.row_count; i++) {
        const IcoTableRow *r = &ico_table_set_jpn.rows[i];
        if (r->host == host && r->record) {
            return (int)r->count;
        }
    }
    return -1;
}

static void checkLayout(int id, void (*proc)(void), const char *name)
{
    const IcoRegionInfo *r = ico_region_cur();
    CHECK(id >= 0 && id < r->layout_count, "%s: layout %d", name, id);
    if (id >= 0 && id < r->layout_count) {
        CHECK((void *)texLayout[id].proc == (void *)proc, "texLayout[%d] is not %s", id, name);
    }
}

static void testText(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    CHECK(rowsOf(texProperty) == r->property_count, "texProperty: %d rows, the region %d",
          rowsOf(texProperty), r->property_count);
    CHECK(rowsOf(texLayout) == r->layout_count, "texLayout: %d rows, the region %d",
          rowsOf(texLayout), r->layout_count);

    checkLayout(r->layout_pause, la_game_pause, "la_game_pause");
    checkLayout(r->layout_title_continue, la_title_continue_or_new, "la_title_continue_or_new");
    checkLayout(r->layout_title_new, la_title_new_game_only, "la_title_new_game_only");
    checkLayout(r->layout_options, la_game_option, "la_game_option");
    checkLayout(r->layout_key_config, la_key_config, "la_key_config");
    checkLayout(r->layout_vibe, la_vibe_select, "la_vibe_select");
    checkLayout(r->layout_play, la_game_loop, "la_game_loop");
    checkLayout(r->layout_scene, la_game_demo, "la_game_demo");
    checkLayout(r->layout_mc_files, la_mc_file_select, "la_mc_file_select");
    checkLayout(r->layout_load, la_load_processing, "la_load_processing");
    checkLayout(r->layout_save_complete, la_save_confirm_complete, "la_save_confirm_complete");

    /* the pause menu: its range, Options -> Back -> End Game, Options
       opening the game's Options screen */
    const LtProp *pause = &texLayout[r->layout_pause];
    CHECK(pause->first == r->pause_first && pause->last == r->pause_last,
          "la_game_pause's rows %d .. %d, the region %d .. %d", pause->first, pause->last,
          r->pause_first, r->pause_last);
    CHECK(texProperty[r->row_pause_options].right == r->layout_options &&
              texProperty[r->row_pause_options].downItem == r->row_pause_back &&
              texProperty[r->row_pause_back].upItem == r->row_pause_options &&
              texProperty[r->row_pause_back].downItem == r->row_pause_end &&
              texProperty[r->row_pause_end].upItem == r->row_pause_back,
          "the pause rows %d, %d, %d", r->row_pause_options, r->row_pause_back, r->row_pause_end);
    CHECK(pause->defaultItem == r->row_pause_back, "la_game_pause's default item %d",
          pause->defaultItem);
    CHECK(texProperty[r->row_pause_back].dispY == texProperty[r->row_pause_options].dispY + 20,
          "Back one pitch (20) under Options");

    /* the title: Continue and New Game, New Game alone, the copyright line
       at the place the Settings menu checks (135, 165, 195) */
    CHECK(texLayout[r->layout_title_continue].first == r->row_title_continue &&
              texLayout[r->layout_title_new].first == r->row_title_new_only,
          "the title layouts' first rows");
    CHECK(texProperty[r->row_title_continue].downItem == r->row_title_new &&
              texProperty[r->row_title_new].upItem == r->row_title_continue,
          "Continue -> New Game");
    CHECK(texProperty[r->row_title_continue].dispY == 135 &&
              texProperty[r->row_title_new].dispY == 165 &&
              texProperty[r->row_title_new_only].dispY == 165 &&
              texProperty[r->row_title_copyright].dispY == 195 &&
              (r->row_title_copyright2 < 0 || texProperty[r->row_title_copyright2].dispY == 195),
          "the title rows' places");

    /* the memory card files: file i's picture and item rows */
    const LtProp *mc = &texLayout[r->layout_mc_files];
    CHECK(mc->first == r->row_mc_picture && mc->defaultItem == r->row_mc_item,
          "la_mc_file_select: rows from %d, default %d", mc->first, mc->defaultItem);
    for (int i = 0; i < 10; i++) {
        CHECK(texProperty[r->row_mc_item + i].ownerItem < 0 &&
                  texProperty[r->row_mc_picture + i].texW > 0,
              "memory card file %d's rows", i);
    }

    /* the glyphs: each row's rectangle and its own height */
    for (int g = 0; g < ICO_GLYPH_COUNT; g++) {
        const IcoGlyphRow *k = &r->glyph[g];
        const LtProperty *e = &texProperty[k->row];
        CHECK(e->texU == k->u && e->texV == k->v && e->texW == k->w && e->texH == k->h,
              "glyph %d: row %d draws %d,%d %dx%d, the region %d,%d %dx%d", g, k->row, e->texU,
              e->texV, e->texW, e->texH, k->u, k->v, k->w, k->h);
        CHECK(e->dispH == k->disp_h, "glyph %d: row %d's dispH %d, the region %d", g, k->row,
              e->dispH, k->disp_h);
    }

    /* the menu text table is the European sheets': no Japanese row */
    CHECK(!ui_menu_text_applies(), "the menu text table applies to the Japanese disc");
    int drawn = 0;
    for (int i = 0; i < r->property_count; i++) {
        drawn += ui_menu_text_item_of(&texProperty[i]) != NULL;
    }
    CHECK(drawn == 0, "%d Japanese rows would be drawn from the European words", drawn);
    printf("menu_jpn: %d layouts, %d rows; the region's layouts, rows and glyphs checked\n",
           r->layout_count, r->property_count);
}

/* The measures over one rectangle of a sheet, as menu_look_test's survey
   defines them on the European sheets (there in grey, here per channel):
   fill  alpha-weighted mean colour of the texels whose grey (the channels'
         mean) is >= 240 and alpha >= 0.9;
   rim   the same over the texels with alpha >= 0.5 and grey <= 100 that
         have no fill texel (grey >= 200, alpha >= 0.7) within a texel;
   the white texels' alphas between none and full (grey >= 240, alpha 13 ..
   254), for the antialiasing steps. */
typedef struct Ink {
    double fill[3], fillW, rim[3], rimW;
    long hist[256];
} Ink;

static void inkAdd(const MsSheet *s, int u0, int v0, int w, int h, Ink *k)
{
    uint8_t *fill = calloc((size_t)w * (size_t)h, 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int u = u0 + x, v = v0 + y;
            if (u >= s->w || v >= s->h) {
                continue;
            }
            const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
            const int g = (p[0] + p[1] + p[2]) / 3, a = p[3];
            if (g >= 240 && a * 10 >= 9 * 255) {
                for (int c = 0; c < 3; c++) {
                    k->fill[c] += (double)p[c] * a;
                }
                k->fillW += a;
            }
            if (g >= 200 && a * 10 >= 7 * 255) {
                fill[y * w + x] = 1;
            }
            if (g >= 240 && a >= 13 && a < 255) {
                k->hist[a]++;
            }
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int u = u0 + x, v = v0 + y;
            if (u >= s->w || v >= s->h) {
                continue;
            }
            const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
            const int g = (p[0] + p[1] + p[2]) / 3, a = p[3];
            if (a * 2 < 255 || g > 100) {
                continue;
            }
            int near = 0;
            for (int dy = -1; dy <= 1 && !near; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h && fill[yy * w + xx]) {
                        near = 1;
                        break;
                    }
                }
            }
            if (!near) {
                for (int c = 0; c < 3; c++) {
                    k->rim[c] += (double)p[c] * a;
                }
                k->rimW += a;
            }
        }
    }
    free(fill);
}

static void inkMerge(Ink *to, const Ink *k)
{
    for (int c = 0; c < 3; c++) {
        to->fill[c] += k->fill[c];
        to->rim[c] += k->rim[c];
    }
    to->fillW += k->fillW;
    to->rimW += k->rimW;
    for (int a = 0; a < 256; a++) {
        to->hist[a] += k->hist[a];
    }
}

static double inkFill(const Ink *k, int c)
{
    return k->fillW > 0 ? k->fill[c] / k->fillW : 0.0;
}

static double inkRim(const Ink *k, int c)
{
    return k->rimW > 0 ? k->rim[c] / k->rimW : 0.0;
}

/* the white's antialiasing steps (menu_look_test survey's rule): the
   alphas taken by 1 % of the white texels or more, merged when within 16,
   the clusters below 0.93 counted; their alphas into lv (up to 8) */
static int inkSteps(const Ink *k, int *lv, int nlv)
{
    long tot = 0;
    for (int a = 0; a < 256; a++) {
        tot += k->hist[a];
    }
    int last = -1000, steps = 0, clusterMax = 0, open = 0;
    for (int a = 0; a < 256; a++) {
        if (k->hist[a] > 0 && k->hist[a] * 100 >= tot) {
            if (open && a - last <= 16) {
                clusterMax = a;
            } else {
                if (open && clusterMax * 100 < 93 * 255) {
                    if (steps < nlv) {
                        lv[steps] = clusterMax;
                    }
                    steps++;
                }
                open = 1;
                clusterMax = a;
            }
            last = a;
        }
    }
    if (open && clusterMax * 100 < 93 * 255) {
        if (steps < nlv) {
            lv[steps] = clusterMax;
        }
        steps++;
    }
    return steps;
}

static const MsSheet *sheetOfRow(IcoVfs *vfs, int row)
{
    const LtProperty *e = &texProperty[row];
    if (e->texFileNo < 0 || e->texFileNo >= ms_tex_file_count()) {
        return NULL;
    }
    return ms_sheet_for(vfs, texFile[e->texFileNo].path);
}

/* one light word's row: its measures printed and added to all */
static void wordRow(IcoVfs *vfs, const char *what, int row, Ink *all, int *rows)
{
    const LtProperty *e = &texProperty[row];
    const MsSheet *s = sheetOfRow(vfs, row);
    if (!s || e->texW < 8 || e->texH < 8) {
        /* la_game_pause's first two rows are 1 x 1 texels of mc01.tm2 (the
           cursor's and the panel's colour), no word */
        printf("menu_look_jpn: %s row %d: no word (%dx%d)\n", what, row, e->texW, e->texH);
        return;
    }
    Ink k;
    memset(&k, 0, sizeof(k));
    inkAdd(s, e->texU, e->texV, e->texW, e->texH, &k);
    printf("menu_look_jpn: %s row %d fill %.0f %.0f %.0f rim %.0f %.0f %.0f (%s %d,%d %dx%d)\n",
           what, row, inkFill(&k, 0), inkFill(&k, 1), inkFill(&k, 2), inkRim(&k, 0), inkRim(&k, 1),
           inkRim(&k, 2), s->name, e->texU, e->texV, e->texW, e->texH);
    if (k.fillW > 0) {
        inkMerge(all, &k);
        (*rows)++;
    }
}

/* The Japanese sheets' light ink, measured over the pause menu's and the
   title's words (the rows the port's own rows sit among: the pause menu's
   Photo mode row, the title's Settings and Quit rows), in kSheetInk's terms
   (menu_font.h UiSheetInk: rimOn, rimLevel, fillLevel, dither, rimWeight).
   The values the table holds; the test fails when the sheets measure
   otherwise (menu_look's tolerances: the rim within 1 level of the
   measure, the fill within 3). Measured on SCPS-11003 (menu_jpn_test look,
   8 words: pause 271..273, title 18..22): fill 252 252 252 (the letters'
   palette white is fcfcfc, not the European sheets' ffffff; 251.5 with the
   antialiased texels), rim 34 34 34 (34.3: black-grey, as the English
   sheets' 24 rather than the other European languages' 56..62), one
   antialiasing step (alpha 231) where the European sheets have three. */
#define EXP_FILL 251.5
#define EXP_RIM 34.3
#define TOL_LEVEL 0.6 /* menu_look_test's: grey levels */

static void testLook(IcoVfs *vfs)
{
    const IcoRegionInfo *r = ico_region_cur();
    static const char *const kName[ICO_GLYPH_COUNT] = {
        "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Left", "Right"};
    for (int g = 0; g < ICO_GLYPH_COUNT; g++) {
        const IcoGlyphRow *k = &r->glyph[g];
        const MsSheet *s = sheetOfRow(vfs, k->row);
        CHECK(s != NULL, "glyph %s: row %d's sheet", kName[g], k->row);
        if (!s) {
            continue;
        }
        int x0 = k->w, y0 = k->h, x1 = -1, y1 = -1, ink = 0;
        for (int y = 0; y < k->h; y++) {
            for (int x = 0; x < k->w; x++) {
                const int u = k->u + x, v = k->v + y;
                if (u >= s->w || v >= s->h) {
                    continue;
                }
                const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
                if (p[3] >= 32 && (p[0] >= 32 || p[1] >= 32 || p[2] >= 32)) {
                    ink++;
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }
        printf("menu_look_jpn: glyph %s row %d rect %d,%d,%d,%d ink %.1f%% bbox %d,%d,%d,%d (%s)\n",
               kName[g], k->row, k->u, k->v, k->w, k->h, 100.0 * ink / (k->w * k->h),
               x1 >= 0 ? x0 : 0, x1 >= 0 ? y0 : 0, x1 >= 0 ? x1 - x0 + 1 : 0,
               x1 >= 0 ? y1 - y0 + 1 : 0, s->name);
        /* a picture, not an empty corner of the sheet: 5 % of the rectangle
           at least (the shoulder labels are a word at the left of a 64 x 20
           box: 7 .. 13 %), and its box half the rectangle's height */
        CHECK(ink >= 8 && ink * 20 >= k->w * k->h && y1 - y0 + 1 >= k->h / 2,
              "glyph %s: rectangle %d,%d %dx%d holds no picture (%d texels)", kName[g], k->u, k->v,
              k->w, k->h, ink);
    }

    Ink all;
    memset(&all, 0, sizeof(all));
    int rows = 0;
    for (int row = r->pause_first; row < r->pause_last; row++) {
        wordRow(vfs, "pause", row, &all, &rows);
    }
    const int title[5] = {r->row_title_continue, r->row_title_new, r->row_title_new_only,
                          r->row_title_copyright, r->row_title_copyright2};
    for (int i = 0; i < 5; i++) {
        if (title[i] >= 0) {
            wordRow(vfs, "title", title[i], &all, &rows);
        }
    }
    int lv[8] = {0};
    const int steps = inkSteps(&all, lv, 8);
    const double fill = (inkFill(&all, 0) + inkFill(&all, 1) + inkFill(&all, 2)) / 3.0;
    const double rim = (inkRim(&all, 0) + inkRim(&all, 1) + inkRim(&all, 2)) / 3.0;
    const int rimOn = all.rimW > 0;
    printf("menu_look_jpn: sheet ink jpn fill %.0f %.0f %.0f rim %.0f %.0f %.0f levels",
           inkFill(&all, 0), inkFill(&all, 1), inkFill(&all, 2), inkRim(&all, 0), inkRim(&all, 1),
           inkRim(&all, 2));
    for (int i = 0; i < steps && i < 8; i++) {
        printf(" %d", lv[i]);
    }
    printf(" (%d steps, %d rows; kSheetInk[UI_SHEET_JA] = {%d, %d, %d, 1, 0})\n", steps, rows,
           rimOn, rimOn ? (int)(rim + 0.5) : 0, (int)(fill + 0.5));

    /* the table holds the measure (menu_font.c kSheetInk[UI_SHEET_JA]) */
    const UiSheetInk *ink = ui_menu_sheet_ink(UI_SHEET_JA);
    CHECK(rows >= 4, "only %d light words measured", rows);
    CHECK(fabs(fill - EXP_FILL) <= TOL_LEVEL, "fill %.1f, expected %.1f", fill, EXP_FILL);
    CHECK(fabs(rim - EXP_RIM) <= TOL_LEVEL, "rim %.1f, expected %.1f", rim, EXP_RIM);
    CHECK(ink->rimOn == rimOn, "kSheetInk[UI_SHEET_JA] rimOn %d, the sheets %d", ink->rimOn, rimOn);
    CHECK(!rimOn || fabs(rim - (double)ink->rimLevel) <= 1.0,
          "kSheetInk[UI_SHEET_JA] rim %d, measured %.1f", ink->rimLevel, rim);
    CHECK(fabs(fill - (double)ink->fillLevel) <= 3.0,
          "kSheetInk[UI_SHEET_JA] fill %d, measured %.1f", ink->fillLevel, fill);
    CHECK(ink->dither == 1, "kSheetInk[UI_SHEET_JA] dithers as the European rows do");
    ms_sheets_free();
}

/* ---------------------------------------------------------------- dump */

/* The pictures for comparing the port's Japanese words with the disc's
   (dump mode; no checks, no ctest): the sheets, every row with ink on
   them at x4, the port's strings in the sheets' look at x4, and the pairs
   of a game word's transcription (UI_STR_MT_*) and the row it transcribes.
   A row's word is found through the region table's rows (the title's and
   the pause menu's) and, with the European ELF, through the European
   layout of the same procedure: a layout with as many rows on both discs
   takes the European row's word (menu_text.c's table) for the Japanese row
   at the same place. */

#define DUMP_BG 128 /* the pictures' backdrop grey (menu_look's) */
#define DUMP_CANVAS 40
#define DUMP_X 4 /* the rows' and the strings' magnification */

typedef struct Img {
    int w, h;
    uint8_t *p;
} Img;

static int imgNew(Img *m, int w, int h)
{
    m->w = w > 0 ? w : 1;
    m->h = h > 0 ? h : 1;
    m->p = malloc((size_t)m->w * (size_t)m->h * 4);
    if (!m->p) {
        return 0;
    }
    for (size_t i = 0; i < (size_t)m->w * (size_t)m->h; i++) {
        m->p[i * 4] = m->p[i * 4 + 1] = m->p[i * 4 + 2] = DUMP_CANVAS;
        m->p[i * 4 + 3] = 255;
    }
    return 1;
}

static void imgPut(Img *m, int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) {
        return;
    }
    uint8_t *q = &m->p[((size_t)y * (size_t)m->w + (size_t)x) * 4];
    q[0] = (uint8_t)r;
    q[1] = (uint8_t)g;
    q[2] = (uint8_t)b;
    q[3] = 255;
}

static void imgWrite(const Img *m, const char *path)
{
    if (!rd_write_png(path, m->p, (uint32_t)m->w, (uint32_t)m->h, (uint32_t)m->w * 4, 0)) {
        printf("menu_jpn dump: cannot write %s\n", path);
    }
}

/* a sheet's rectangle (RGBA, alpha 0..255) on the backdrop at scale s,
   nearest; texels past the sheet are the backdrop */
static void blitSheet(Img *m, int ox, int oy, const MsSheet *s, int u0, int v0, int w, int h,
                      int sc)
{
    for (int y = 0; y < h * sc; y++) {
        for (int x = 0; x < w * sc; x++) {
            const int u = u0 + x / sc, v = v0 + y / sc;
            int c[3] = {DUMP_BG, DUMP_BG, DUMP_BG};
            if (u >= 0 && v >= 0 && u < s->w && v < s->h) {
                const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
                for (int k = 0; k < 3; k++) {
                    c[k] = (p[k] * p[3] + DUMP_BG * (255 - p[3]) + 127) / 255;
                }
            }
            imgPut(m, ox + x, oy + y, c[0], c[1], c[2]);
        }
    }
}

/* a grey and alpha picture on the backdrop at scale s, nearest */
static void blitGrey(Img *m, int ox, int oy, const uint8_t *grey, const uint8_t *alpha, int w,
                     int h, int sc)
{
    for (int y = 0; y < h * sc; y++) {
        for (int x = 0; x < w * sc; x++) {
            const int i = (y / sc) * w + x / sc;
            const int v = (grey[i] * alpha[i] + DUMP_BG * (255 - alpha[i]) + 127) / 255;
            imgPut(m, ox + x, oy + y, v, v, v);
        }
    }
}

/* a 3 x 5 face for the labels: 0-9, A-Z and a few marks */
static const char kFaceChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_-:./()+=,";
/* 15 bits a glyph, the top row first, each row left to right */
static const unsigned short kFace[] = {
    0x7B6F, 0x2C97, 0x73E7, 0x72CF, 0x5BC9, 0x79CF, 0x79EF, 0x7252, 0x7BEF, 0x7BCF, 0x2BED, 0x6BAE,
    0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED, 0x6B6D,
    0x2B6A, 0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
    0x0007, 0x01C0, 0x0410, 0x0002, 0x12A4, 0x2922, 0x224A, 0x05D0, 0x0E38, 0x0014,
};

static void drawLabel(Img *m, int x, int y, const char *t, int sc)
{
    for (; *t; t++, x += 4 * sc) {
        int ch = (unsigned char)*t;
        ch = ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
        const char *at = ch ? strchr(kFaceChars, ch) : NULL;
        if (!at) {
            continue;
        }
        const unsigned g = kFace[at - kFaceChars];
        for (int k = 0; k < 15; k++) {
            if (g & (1u << (14 - k))) {
                for (int dy = 0; dy < sc; dy++) {
                    for (int dx = 0; dx < sc; dx++) {
                        imgPut(m, x + (k % 3) * sc + dx, y + (k / 3) * sc + dy, 255, 220, 64);
                    }
                }
            }
        }
    }
}

#define LABEL_SC 2
#define LABEL_H (6 * LABEL_SC)

/* a port string: its id, its name for the files, the em (y units, 0 for
   the menu rows') and the box it is fitted to (x units, 0 for none), as
   settings.c lays it out */
typedef struct PortStr {
    int id;
    const char *name;
    float size;
    int box;
} PortStr;

#define MT(x) {UI_STR_MT_##x, "MT_" #x, 0.0f, 0}
static const PortStr kPortStr[] = {
    MT(LANG_ENGLISH),
    MT(LANG_FRANCAIS),
    MT(LANG_DEUTSCH),
    MT(LANG_ITALIANO),
    MT(LANG_ESPANOL),
    MT(HZ50),
    MT(HZ60),
    MT(NO_CARD),
    MT(NEED_START),
    MT(NO_SPACE_START),
    MT(YES),
    MT(NO),
    MT(VIBRATION),
    MT(ACTIVATE),
    MT(DEACTIVATE),
    MT(CONTINUE),
    MT(NEW_GAME),
    MT(DO_NOT_REMOVE),
    MT(ACCESSING),
    MT(LOC_OLD_BRIDGE),
    MT(LOC_TROLLEY_A),
    MT(LOC_MAIN_GATE),
    MT(LOC_GRAVEYARD),
    MT(LOC_WINDMILL),
    MT(LOC_STONE_PILLAR),
    MT(LOC_EAST_ARENA),
    MT(LOC_EAST_REFLECTOR),
    MT(LOC_WATERFALL),
    MT(LOC_GONDOLA),
    MT(LOC_WATER_TOWER),
    MT(LOC_WEST_IDOL_STAIRS),
    MT(LOC_TROLLEY_B),
    MT(LOC_CRANE),
    MT(LOC_SANDY_BEACH),
    MT(SLOT_1),
    MT(SLOT_2),
    MT(OK),
    MT(BACK),
    MT(SELECT_SLOT),
    MT(SELECT_LOAD),
    MT(NO_SAVE_DATA),
    MT(LOADING),
    MT(SAVE_Q),
    MT(NO_SPACE),
    MT(NEED_360),
    MT(SELECT_SAVE),
    MT(OVERWRITE),
    MT(NOT_FORMATTED),
    MT(FORMATTING),
    MT(SAVING),
    MT(FILE_SAVED),
    MT(RESUME_GAME),
    MT(END_GAME),
    MT(GAME_WILL_END),
    MT(SAVE_FAILED),
    MT(FORMAT_FAILED),
    MT(LOAD_FAILED),
    MT(NO_ACCESS),
    MT(OPTIONS),
    MT(PAUSE_BACK),
    MT(FILM_EFFECT),
    MT(SOUND),
    MT(STEREO),
    MT(MONO),
    MT(HOLD_TYPE),
    MT(BUTTON_CONFIG),
    MT(BRIGHTNESS),
    MT(PLAYERS),
    MT(JUMP),
    MT(ATTACK),
    MT(ACTION),
    MT(RELEASE),
    MT(HOLD_HAND_CALL),
    MT(ZOOM),
    MT(DEFAULT),
    MT(ADJUST_HINT),
    MT(DARK),
    MT(LIGHT),
    MT(CONTINUE_Q),
    MT(DIGIT_0),
    MT(DIGIT_1),
    MT(DIGIT_2),
    MT(DIGIT_3),
    MT(DIGIT_4),
    MT(DIGIT_5),
    MT(DIGIT_6),
    MT(DIGIT_7),
    MT(DIGIT_8),
    MT(DIGIT_9),
    MT(DIGIT_10),
    MT(COLON),
    MT(VAL_A),
    MT(VAL_B),
    /* the port's words on the title and the pause menu (settings.c
       buildEntries, the quit confirmation) and the Settings pages' titles
       (addHeader: HEADER_SIZE 30 in a 600 box) */
    {UI_STR_PHOTO_MODE, "PHOTO_MODE", 0.0f, 250},
    {UI_STR_QUIT_DESKTOP, "QUIT_DESKTOP", 0.0f, 400},
    {UI_STR_QUIT_GAME, "QUIT_GAME", 0.0f, 400},
    {UI_STR_QUIT_CONFIRM, "QUIT_CONFIRM", 30.0f, 600},
    {UI_STR_QUIT_GAME_CONFIRM, "QUIT_GAME_CONFIRM", 30.0f, 600},
    {UI_STR_SETTINGS, "SETTINGS", 30.0f, 600},
    {UI_STR_SECTION_DISPLAY, "SECTION_DISPLAY", 30.0f, 600},
    {UI_STR_SECTION_EFFECTS, "SECTION_EFFECTS", 30.0f, 600},
    {UI_STR_SECTION_GPU_DRIVER, "SECTION_GPU_DRIVER", 30.0f, 600},
    {UI_STR_SECTION_AUDIO, "SECTION_AUDIO", 30.0f, 600},
    {UI_STR_SECTION_CONTROLS, "SECTION_CONTROLS", 30.0f, 600},
    {UI_STR_SECTION_GAMEPLAY, "SECTION_GAMEPLAY", 30.0f, 600},
    {UI_STR_SECTION_ACHIEVEMENTS, "SECTION_ACHIEVEMENTS", 30.0f, 600},
    {UI_STR_OPT_REMAP, "OPT_REMAP", 30.0f, 600},
    {UI_STR_EXTRAS, "EXTRAS", 30.0f, 600},
    {UI_STR_EXTRAS_MUSIC, "EXTRAS_MUSIC", 30.0f, 600},
    {UI_STR_SECTION_CHARACTERS, "SECTION_CHARACTERS", 30.0f, 600},
};
#undef MT
#define PORT_STR_COUNT ((int)(sizeof(kPortStr) / sizeof(kPortStr[0])))
#define MT_COUNT (UI_STR_MT_VAL_B - UI_STR_MT_LANG_ENGLISH + 1)
_Static_assert(MT_COUNT == 93, "kPortStr starts with every UI_STR_MT_* id, in order");

/* a port string rendered as ui_draw_menu_text draws it in the Japanese
   sheets' light ink (kSheetInk[UI_SHEET_JA], the full rim), unlit sprite
   colour 0x80: grey and alpha 0..255, malloc'd */
typedef struct Rendered {
    uint8_t *grey, *alpha;
    int w, h;
    float size;
} Rendered;

static int renderPort(const PortStr *ps, Rendered *out)
{
    memset(out, 0, sizeof(*out));
    const char *text = ui_str_in(UI_LANG_JA, (UiStrId)ps->id);
    float size = ps->size > 0.0f ? ps->size : UI_MENU_TEXT_SIZE;
    if (ps->box > 0) {
        /* layout_ext.c: a label wider than its box is set smaller, to 60 % */
        const float w = ui_measure_menu_text(size, text);
        if (w > (float)ps->box) {
            const float k = (float)ps->box / w;
            size *= k < 0.6f ? 0.6f : k;
        }
    }
    uint8_t *cov = NULL;
    int w = 0, h = 0;
    if (ui__menu_text_raster(text, size, UI_ALIGN_LEFT | UI_VALIGN_MIDDLE, &cov, &w, &h) != 0) {
        return 0;
    }
    const UiSheetInk *k = ui_menu_item_ink(UI_INK_LIGHT, UI_RIM_FULL, UI_SHEET_JA);
    const RdSheetStyle st = {k->rimOn, k->rimLevel, k->fillLevel, k->dither, k->rimWeight, 1};
    out->grey = malloc((size_t)w * (size_t)h);
    out->alpha = malloc((size_t)w * (size_t)h);
    if (!out->grey || !out->alpha) {
        free(cov);
        free(out->grey);
        free(out->alpha);
        return 0;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t g, a;
            sheetref_texel(cov, (uint32_t)w, (uint32_t)h, x, y, &st, &g, &a);
            const int a255 = a * 255 / 128;
            out->grey[y * w + x] = g;
            out->alpha[y * w + x] = (uint8_t)(a255 > 255 ? 255 : a255);
        }
    }
    free(cov);
    out->w = w;
    out->h = h;
    out->size = size;
    return 1;
}

/* the sheets' file names: Tex_menu01/option.tm2 -> Tex_menu01_option */
static void sheetFileName(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", path);
    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }
    for (char *c = out; *c; c++) {
        if (*c == '/' || *c == '\\' || *c == ':' || *c == ' ') {
            *c = '_';
        }
    }
}

/* a row's rectangle with ink: texels of alpha >= 32, and not one flat
   colour on the backdrop (a panel or a bar) */
static int rowHasInk(const MsSheet *s, const LtProperty *e)
{
    if (e->texW < 4 || e->texH < 4) {
        return 0;
    }
    long ink = 0;
    double sum = 0.0, sum2 = 0.0;
    long n = 0;
    for (int y = 0; y < e->texH; y++) {
        for (int x = 0; x < e->texW; x++) {
            const int u = e->texU + x, v = e->texV + y;
            if (u >= s->w || v >= s->h) {
                continue;
            }
            const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
            /* on the backdrop, so black letters differ from no texel */
            const double g = ((p[0] + p[1] + p[2]) / 3.0 * p[3] + DUMP_BG * (255.0 - p[3])) / 255.0;
            sum += g;
            sum2 += g * g;
            n++;
            ink += p[3] >= 32;
        }
    }
    if (n == 0 || ink < 8) {
        return 0;
    }
    const double mean = sum / (double)n;
    return sum2 / (double)n - mean * mean >= 64.0;
}

/* the European layouts kept before the Japanese tables are loaded */
static LtProperty *s_palProp;
static LtProp *s_palLayout;
static int s_palPropN, s_palLayoutN;

static int rowsIn(const IcoTableSet *set, const void *host)
{
    for (uint32_t i = 0; i < set->row_count; i++) {
        const IcoTableRow *r = &set->rows[i];
        if (r->host == host && r->record) {
            return (int)r->count;
        }
    }
    return -1;
}

static int loadSet(const IcoTableSet *set, const char *path)
{
    size_t size;
    uint8_t *elf = readAll(path, &size);
    if (!elf) {
        return 0;
    }
    char err[512];
    const int rc = ico_tables_load_elf(set, elf, size, err, sizeof(err));
    free(elf);
    if (rc != 0) {
        printf("menu_jpn dump: %s: %s\n", path, err);
    }
    return rc == 0;
}

/* the European rows' words, through the layouts of the same procedure
   (the k-th layout of a procedure on one disc is the k-th on the other)
   with as many rows; word[row] the UiStrId or -1, how[row] what found it */
static void mapByLayout(int *word, char (*how)[48])
{
    if (!s_palLayout) {
        return;
    }
    const IcoRegionInfo *r = ico_region_cur();
    int matched = 0, unequal = 0;
    for (int j = 0; j < r->layout_count; j++) {
        void *proc = (void *)texLayout[j].proc;
        if (!proc) {
            continue;
        }
        int k = 0;
        for (int i = 0; i < j; i++) {
            k += (void *)texLayout[i].proc == proc;
        }
        int p = -1;
        for (int i = 0, seen = 0; i < s_palLayoutN; i++) {
            if ((void *)s_palLayout[i].proc == proc && seen++ == k) {
                p = i;
                break;
            }
        }
        if (p < 0) {
            continue;
        }
        const int n = texLayout[j].last - texLayout[j].first;
        if (n != s_palLayout[p].last - s_palLayout[p].first) {
            unequal++;
            printf(
                "menu_jpn dump: layout %d (%d rows) and the European %d (%d rows): not aligned\n",
                j, n, p, s_palLayout[p].last - s_palLayout[p].first);
            continue;
        }
        matched++;
        for (int i = 0; i < n; i++) {
            const int jr = texLayout[j].first + i, pr = s_palLayout[p].first + i;
            if (jr < 0 || jr >= r->property_count || pr < 0 || pr >= s_palPropN || word[jr] >= 0) {
                continue;
            }
            for (int t = 0; t < ui_menu_text_row_count; t++) {
                if (ui_menu_text_rows[t].row == pr) {
                    word[jr] = ui_menu_text_items[ui_menu_text_rows[t].item].str;
                    snprintf(how[jr], sizeof(how[jr]), "layout %d = PAL layout %d row %d", j, p,
                             pr);
                    break;
                }
            }
        }
    }
    printf("menu_jpn dump: %d layouts aligned with the European ones, %d of unequal rows\n",
           matched, unequal);
}

static const PortStr *portOf(int id)
{
    for (int i = 0; i < PORT_STR_COUNT; i++) {
        if (kPortStr[i].id == id) {
            return &kPortStr[i];
        }
    }
    return NULL;
}

/* lay cells (w x h each) out left to right within maxW; their corners
   into xy, the canvas size returned */
static void flow(const int *cw, const int *ch, int n, int maxW, int *xy, int *W, int *H)
{
    int x = 4, y = 4, lineH = 0, wide = 0;
    for (int i = 0; i < n; i++) {
        if (x > 4 && x + cw[i] > maxW) {
            x = 4;
            y += lineH + 8;
            lineH = 0;
        }
        xy[2 * i] = x;
        xy[2 * i + 1] = y;
        x += cw[i] + 8;
        lineH = ch[i] > lineH ? ch[i] : lineH;
        wide = x > wide ? x : wide;
    }
    *W = wide + 4;
    *H = y + lineH + 8;
}

static int dump(IcoVfs *vfs, const char *outDir)
{
    const IcoRegionInfo *r = ico_region_cur();
    const int nRows = r->property_count;
    char path[1200];
    ico_mkdir(outDir);
    snprintf(path, sizeof(path), "%s/sheets", outDir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/rows", outDir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/port", outDir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/index.txt", outDir);
    FILE *ix = fopen(path, "w");
    if (!ix) {
        printf("menu_jpn dump: cannot write %s\n", path);
        return 1;
    }

    /* (1) the sheets the rows draw from */
    const int nFiles = ms_tex_file_count();
    int sheets = 0;
    fprintf(ix, "# sheets (texFile rows the texProperty rows name)\n");
    for (int f = 0; f < nFiles; f++) {
        int used = 0;
        for (int i = 0; i < nRows && !used; i++) {
            used = texProperty[i].texFileNo == f;
        }
        const MsSheet *s = used ? ms_sheet_for(vfs, texFile[f].path) : NULL;
        if (!s) {
            if (used) {
                fprintf(ix, "texFile %d %s: not decoded\n", f, texFile[f].path);
            }
            continue;
        }
        char name[128];
        sheetFileName(texFile[f].path, name, sizeof(name));
        Img m;
        if (imgNew(&m, s->w, s->h)) {
            blitSheet(&m, 0, 0, s, 0, 0, s->w, s->h, 1);
            snprintf(path, sizeof(path), "%s/sheets/%s.png", outDir, name);
            imgWrite(&m, path);
            free(m.p);
            sheets++;
        }
        fprintf(ix, "texFile %d %s: %dx%d -> sheets/%s.png\n", f, texFile[f].path, s->w, s->h,
                name);
    }

    /* the words of the rows: the region's rows first, then the layouts */
    int *word = malloc(sizeof(int) * (size_t)nRows);
    char (*how)[48] = calloc((size_t)nRows, sizeof(*how));
    if (!word || !how) {
        fclose(ix);
        free(word);
        free(how);
        return 1;
    }
    for (int i = 0; i < nRows; i++) {
        word[i] = -1;
    }

    const struct {
        int row, id;
    } kRegion[] = {
        {r->row_title_continue, UI_STR_MT_CONTINUE}, {r->row_title_new, UI_STR_MT_NEW_GAME},
        {r->row_title_new_only, UI_STR_MT_NEW_GAME}, {r->row_pause_options, UI_STR_MT_OPTIONS},
        {r->row_pause_back, UI_STR_MT_PAUSE_BACK},   {r->row_pause_end, UI_STR_MT_END_GAME},
    };

    for (size_t i = 0; i < sizeof(kRegion) / sizeof(kRegion[0]); i++) {
        if (kRegion[i].row >= 0 && kRegion[i].row < nRows) {
            word[kRegion[i].row] = kRegion[i].id;
            snprintf(how[kRegion[i].row], sizeof(how[0]), "region table");
        }
    }
    mapByLayout(word, how);

    /* (2) the rows with ink */
    int *inked = malloc(sizeof(int) * (size_t)nRows);
    int nInked = 0;
    fprintf(ix,
            "\n# rows (texProperty rows with ink on a decoded sheet): row sheet u,v wxh word\n");
    for (int i = 0; inked && i < nRows; i++) {
        const LtProperty *e = &texProperty[i];
        const MsSheet *s = sheetOfRow(vfs, i);
        if (!s || !rowHasInk(s, e)) {
            continue;
        }
        inked[nInked++] = i;
        Img m;
        char label[32];
        snprintf(label, sizeof(label), "%d", i);
        const int lw = (int)strlen(label) * 4 * LABEL_SC + 8;
        const int W = e->texW * DUMP_X > lw ? e->texW * DUMP_X : lw;
        if (imgNew(&m, W, e->texH * DUMP_X + LABEL_H + 4)) {
            drawLabel(&m, 2, 2, label, LABEL_SC);
            blitSheet(&m, 0, LABEL_H + 4, s, e->texU, e->texV, e->texW, e->texH, DUMP_X);
            snprintf(path, sizeof(path), "%s/rows/%d.png", outDir, i);
            imgWrite(&m, path);
            free(m.p);
        }
        const PortStr *ps = word[i] >= 0 ? portOf(word[i]) : NULL;
        fprintf(ix, "%d %s %d,%d %dx%d %s%s%s%s\n", i, s->name, e->texU, e->texV, e->texW, e->texH,
                ps ? ps->name : "-", ps ? " \"" : "",
                ps ? ui_str_in(UI_LANG_JA, (UiStrId)word[i]) : "", ps ? "\"" : "");
        if (ps) {
            fprintf(ix, "    (%s)\n", how[i]);
        }
    }
    {
        int *cw = malloc(sizeof(int) * (size_t)(nInked + 1));
        int *chh = malloc(sizeof(int) * (size_t)(nInked + 1));
        int *xy = malloc(sizeof(int) * 2 * (size_t)(nInked + 1));
        int W = 0, H = 0;
        if (cw && chh && xy) {
            for (int k = 0; k < nInked; k++) {
                const LtProperty *e = &texProperty[inked[k]];
                cw[k] = e->texW * 2 > 40 ? e->texW * 2 : 40;
                chh[k] = e->texH * 2 + LABEL_H + 2;
            }
            flow(cw, chh, nInked, 2048, xy, &W, &H);
            Img m;
            if (imgNew(&m, W, H)) {
                for (int k = 0; k < nInked; k++) {
                    const LtProperty *e = &texProperty[inked[k]];
                    char label[16];
                    snprintf(label, sizeof(label), "%d", inked[k]);
                    drawLabel(&m, xy[2 * k], xy[2 * k + 1], label, LABEL_SC);
                    blitSheet(&m, xy[2 * k], xy[2 * k + 1] + LABEL_H + 2, sheetOfRow(vfs, inked[k]),
                              e->texU, e->texV, e->texW, e->texH, 2);
                }
                snprintf(path, sizeof(path), "%s/rows_contact.png", outDir);
                imgWrite(&m, path);
                free(m.p);
            }
        }
        free(cw);
        free(chh);
        free(xy);
    }

    /* (3) the port's strings */
    Rendered *rend = calloc((size_t)PORT_STR_COUNT, sizeof(Rendered));
    int nPort = 0;
    fprintf(ix, "\n# port strings (ui_str_in(UI_LANG_JA), the Japanese sheets' light ink): name "
                "em box text rows\n");
    for (int i = 0; rend && i < PORT_STR_COUNT; i++) {
        const PortStr *ps = &kPortStr[i];
        if (!renderPort(ps, &rend[i])) {
            fprintf(ix, "%s: not rendered\n", ps->name);
            continue;
        }
        nPort++;
        Img m;
        if (imgNew(&m, rend[i].w * DUMP_X, rend[i].h * DUMP_X)) {
            blitGrey(&m, 0, 0, rend[i].grey, rend[i].alpha, rend[i].w, rend[i].h, DUMP_X);
            snprintf(path, sizeof(path), "%s/port/%s.png", outDir, ps->name);
            imgWrite(&m, path);
            free(m.p);
        }
        char rows[256] = "";
        size_t at = 0;
        for (int k = 0; k < nRows && at + 8 < sizeof(rows); k++) {
            if (word[k] == ps->id) {
                at += (size_t)snprintf(rows + at, sizeof(rows) - at, "%s%d", at ? "," : "", k);
            }
        }
        fprintf(ix, "%s %.1f %d \"%s\"%s rows %s\n", ps->name, rend[i].size, ps->box,
                ui_str_in(UI_LANG_JA, (UiStrId)ps->id), ui_strings_ja[ps->id] ? "" : " (English)",
                at ? rows : "-");
    }
    {
        int cw[PORT_STR_COUNT], chh[PORT_STR_COUNT], xy[2 * PORT_STR_COUNT], W = 0, H = 0;
        for (int i = 0; i < PORT_STR_COUNT; i++) {
            const int lw = (int)strlen(kPortStr[i].name) * 4 * LABEL_SC;
            const int pw = rend ? rend[i].w * 2 : 0;
            cw[i] = pw > lw ? pw : lw;
            chh[i] = (rend ? rend[i].h * 2 : 0) + LABEL_H + 2;
        }
        flow(cw, chh, PORT_STR_COUNT, 2048, xy, &W, &H);
        Img m;
        if (rend && imgNew(&m, W, H)) {
            for (int i = 0; i < PORT_STR_COUNT; i++) {
                drawLabel(&m, xy[2 * i], xy[2 * i + 1], kPortStr[i].name, LABEL_SC);
                if (rend[i].grey) {
                    blitGrey(&m, xy[2 * i], xy[2 * i + 1] + LABEL_H + 2, rend[i].grey,
                             rend[i].alpha, rend[i].w, rend[i].h, 2);
                }
            }
            snprintf(path, sizeof(path), "%s/port_contact.png", outDir);
            imgWrite(&m, path);
            free(m.p);
        }
    }

    /* (4) a game word's transcription beside the rows it transcribes (one
       a sheet rectangle) */
    int pairs = 0, W = 0, H = 4;
    for (int pass = 0; pass < 2; pass++) {
        Img m = {0, 0, NULL};
        if (pass == 1 && !imgNew(&m, W, H)) {
            break;
        }
        int y = 4;
        for (int i = 0; rend && i < MT_COUNT; i++) {
            const PortStr *ps = &kPortStr[i];
            for (int k = 0; k < nRows; k++) {
                if (word[k] != ps->id || !rend[i].grey) {
                    continue;
                }
                const LtProperty *e = &texProperty[k];
                int dup = 0;
                for (int q = 0; q < k && !dup; q++) {
                    const LtProperty *o = &texProperty[q];
                    dup = word[q] == ps->id && o->texFileNo == e->texFileNo && o->texU == e->texU &&
                          o->texV == e->texV && o->texW == e->texW && o->texH == e->texH;
                }
                const MsSheet *s = sheetOfRow(vfs, k);
                if (dup || !s) {
                    continue;
                }
                char label[96];
                snprintf(label, sizeof(label), "%d %s", k, ps->name);
                const int sw = e->texW * DUMP_X, sh = e->texH * DUMP_X;
                const int pw = rend[i].w * DUMP_X, ph = rend[i].h * DUMP_X;
                const int lineH = LABEL_H + 2 + (sh > ph ? sh : ph);
                if (pass == 0) {
                    const int w = 4 + sw + 16 + pw + 4;
                    W = w > W ? w : W;
                    const int lw = 4 + (int)strlen(label) * 4 * LABEL_SC;
                    W = lw > W ? lw : W;
                    pairs++;
                } else {
                    drawLabel(&m, 4, y, label, LABEL_SC);
                    blitSheet(&m, 4, y + LABEL_H + 2, s, e->texU, e->texV, e->texW, e->texH,
                              DUMP_X);
                    blitGrey(&m, 4 + sw + 16, y + LABEL_H + 2, rend[i].grey, rend[i].alpha,
                             rend[i].w, rend[i].h, DUMP_X);
                }
                y += lineH + 10;
            }
        }
        H = y + 4;
        if (pass == 1) {
            snprintf(path, sizeof(path), "%s/compare.png", outDir);
            imgWrite(&m, path);
            free(m.p);
        }
    }

    /* what has no partner */
    fprintf(ix, "\n# rows with ink and no port id:");
    int noId = 0, noRow = 0;
    for (int k = 0; k < nInked; k++) {
        if (word[inked[k]] < 0) {
            fprintf(ix, " %d", inked[k]);
            noId++;
        }
    }
    fprintf(ix, "\n# UI_STR_MT_* ids with no row:");
    for (int i = 0; i < MT_COUNT; i++) {
        int found = 0;
        for (int k = 0; k < nRows && !found; k++) {
            found = word[k] == kPortStr[i].id;
        }
        if (!found) {
            fprintf(ix, " %s", kPortStr[i].name);
            noRow++;
        }
    }
    fprintf(ix, "\n");
    fclose(ix);
    printf("menu_jpn dump: %d sheets, %d rows with ink (%d without a port id), %d port strings, "
           "%d pairs (%d MT ids without a row) in %s\n",
           sheets, nInked, noId, nPort, pairs, noRow, outDir);
    for (int i = 0; rend && i < PORT_STR_COUNT; i++) {
        free(rend[i].grey);
        free(rend[i].alpha);
    }
    free(rend);
    free(inked);
    free(word);
    free(how);
    ms_sheets_free();
    return 0;
}

int main(int argc, char **argv)
{
    const int isDump = argc > 1 && strcmp(argv[1], "dump") == 0;
    if (argc < 3 || (strcmp(argv[1], "text") != 0 && strcmp(argv[1], "look") != 0 && !isDump) ||
        (isDump && argc < 5)) {
        fprintf(stderr, "usage: menu_jpn_test text|look BASE_ELF_JPN [DISC_IMAGE_JPN]\n"
                        "       menu_jpn_test dump BASE_ELF_JPN DISC_IMAGE_JPN OUT_DIR "
                        "[BASE_ELF_PAL]\n");
        return 2;
    }
    if (isDump && argc > 5) {
        /* the European layouts, kept for the row alignment, before the
           Japanese tables replace them */
        if (loadSet(&ico_table_set_pal, argv[5])) {
            s_palPropN = rowsIn(&ico_table_set_pal, texProperty);
            s_palLayoutN = rowsIn(&ico_table_set_pal, texLayout);
            s_palProp = malloc(sizeof(LtProperty) * (size_t)s_palPropN);
            s_palLayout = malloc(sizeof(LtProp) * (size_t)s_palLayoutN);
            if (s_palProp && s_palLayout) {
                memcpy(s_palProp, texProperty, sizeof(LtProperty) * (size_t)s_palPropN);
                memcpy(s_palLayout, texLayout, sizeof(LtProp) * (size_t)s_palLayoutN);
            }
        } else {
            printf("menu_jpn dump: no European ELF (%s): the region table's rows only\n", argv[5]);
        }
    }
    size_t size;
    uint8_t *elf = readAll(argv[2], &size);
    if (!elf) {
        printf("menu_jpn: no Japanese base ELF (%s): skipped\n", argv[2]);
        return 77;
    }
    char err[512];
    const int rc = ico_tables_load_elf(&ico_table_set_jpn, elf, size, err, sizeof(err));
    free(elf);
    if (rc != 0) {
        printf("menu_jpn: the ELF's tables do not load: %s\n", err);
        return 1;
    }
    if (ico_region_select("jpn") != 0) {
        printf("menu_jpn: no Japanese region\n");
        return 1;
    }
    if (strcmp(argv[1], "text") == 0) {
        testText();
    } else if (isDump) {
        IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[3]);
        if (!vfs) {
            printf("menu_jpn: no Japanese disc image (%s): skipped\n", argv[3]);
            return 77;
        }
        const int rc = dump(vfs, argv[4]);
        ico_vfs_unmount(vfs);
        free(s_palProp);
        free(s_palLayout);
        return rc;
    } else {
        IcoVfs *vfs = argc > 3 ? ico_vfs_mount(&ico_vfs_iso9660, argv[3]) : NULL;
        if (!vfs) {
            printf("menu_jpn: no Japanese disc image (%s): skipped\n", argc > 3 ? argv[3] : "");
            return 77;
        }
        testLook(vfs);
        ico_vfs_unmount(vfs);
    }
    printf("menu_jpn: %d failures\n", failures);
    return failures ? 1 : 0;
}
