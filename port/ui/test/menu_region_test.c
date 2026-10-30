/* menu_region_test.c: the Japanese, Korean and Asian discs' menus as the port reads them.
 *
 * The port names the game's menus by number (a layout, a row, a button
 * glyph's rectangle); on the Japanese, Korean and Asian discs they are the
 * disc's own numbers, the region table's (port/core/region_jpn.c,
 * region_kor.c, region_cn.c; the Korean build is the Japanese program with
 * its own tex-property table, the Asian one the Korean program on the
 * Japanese tex-property table).  Over the disc's boot ELF's tables (the loader's copy of that
 * disc, port/data/gen/<slug>/):
 *
 *   text  every layout the region names runs the procedure it is named
 *         for (the Continue screen's file select linking the files'
 *         layout); the title's, pause menu's and memory card screen's rows are
 *         where the Settings menu and the mouse expect them; the glyph rows'
 *         texel rectangles and heights (IcoGlyphRow) are the table's; the
 *         tables' sizes are the region's counts; and no row of the disc's
 *         tables is drawn from the menu text table (the European sheets'
 *         words): the disc's menu words keep their own texels.
 *   look  with the disc image: the sheets decoded through the disc's packs
 *         (menu_sheets.c, as menu_look_test does on the European disc):
 *         each glyph's rectangle (its ink and the box of it), the light
 *         words of the pause menu and the title (their fill and their
 *         rim), and over them the disc's sheet ink, the port's own rows
 *         over these sheets are drawn with (menu_font.c kSheetInk[the
 *         row's menu_sheet]: UI_SHEET_JA, UI_SHEET_KO, UI_SHEET_ZH), printed and held to
 *         that table within menu_look's tolerances; and the port's geometry
 *         for the sheet language (menu_font.c sheetGeo): the title's and
 *         pause menu's words drawn by the port against the disc's rows of
 *         the same words, the ink's height within a texel and its width
 *         within 10 % (17 % on the Japanese sheets other than the title's
 *         and on the Korean pause menu's End Game, the condensed mc04 word;
 *         ICO_MENU_LOOK_JPN_FIT=1 searches the numbers).
 *
 *   dump  with the disc image, pictures to compare the port's words
 *         with the disc's lettering (not a ctest; no checks):
 *         OUT_DIR/sheets/<file>.png every sheet the rows name, whole;
 *         OUT_DIR/rows/<row>.png every row with ink, x4 nearest, its number
 *         above, and rows_contact.png of them all at x2;
 *         OUT_DIR/port/<id>.png every UI_STR_MT_* string and the port's
 *         title, pause and Settings page title words in the disc's
 *         language, drawn as ui_draw_menu_text strips them
 *         (ui__menu_text_raster) in the sheets' light ink, x4, and
 *         port_contact.png at x2;
 *         OUT_DIR/compare.png each UI_STR_MT_* string beside the rows it
 *         transcribes (the region table's title and pause rows, and with
 *         the European ELF the European layouts' rows of menu_text.c),
 *         x4; OUT_DIR/index.txt the sheets, the rows (sheet, rectangle,
 *         word), the strings and what has no partner.
 *
 *   menu_region_test text SLUG BASE_ELF
 *   menu_region_test look SLUG BASE_ELF DISC_IMAGE
 *   menu_region_test dump SLUG BASE_ELF DISC_IMAGE OUT_DIR [BASE_ELF_PAL]
 *
 * SLUG is "jpn", "kor" or "cn" (ctests menu_text_jpn, menu_look_jpn,
 * menu_text_kor, menu_look_kor, menu_text_cn, menu_look_cn).  Exit 0, 1 on a failure, 77 without the ELF (or, for look
 * and dump, the disc).
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
   port/data/gen/<slug>/ee_symbols.c with ICO_EE_SYMBOLS_STUBS) */
void la_game_pause(void);
void la_title_continue_or_new(void);
void la_title_new_game_only(void);
void la_game_option(void);
void la_key_config(void);
void la_vibe_select(void);
void la_game_loop(void);
void la_game_demo(void);
void la_mc_file_select(void);
void la_mc_load_file_select(void);
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

/* the descriptor set of the disc under test (main) */
static const IcoTableSet *s_set;

/* a table's row count as the loader describes it (the disc's set) */
static int rowsOf(const void *host)
{
    for (uint32_t i = 0; i < s_set->row_count; i++) {
        const IcoTableRow *r = &s_set->rows[i];
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
    checkLayout(r->layout_load_files, la_mc_load_file_select, "la_mc_load_file_select");
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

    /* the Continue screen's file select: no cursor of its own, the files'
       layout linked after it (the Save states prompt goes between them) */
    const LtProp *lf = &texLayout[r->layout_load_files];
    CHECK(lf->link == r->layout_mc_files && lf->defaultItem == -1,
          "la_mc_load_file_select links %d (want %d), default %d", lf->link, r->layout_mc_files,
          lf->defaultItem);

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

    /* the menu text table is the European sheets': no row of this disc */
    CHECK(!ui_menu_text_applies(), "the menu text table applies to the %s disc", r->slug);
    int drawn = 0;
    for (int i = 0; i < r->property_count; i++) {
        drawn += ui_menu_text_item_of(&texProperty[i]) != NULL;
    }
    CHECK(drawn == 0, "%d rows of the disc would be drawn from the European words", drawn);
    printf("menu_text_%s: %d layouts, %d rows; the region's layouts, rows and glyphs checked\n",
           r->slug, r->layout_count, r->property_count);
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
        printf("menu_look_%s: %s row %d: no word (%dx%d)\n", ico_region_cur()->slug, what, row,
               e->texW, e->texH);
        return;
    }
    Ink k;
    memset(&k, 0, sizeof(k));
    inkAdd(s, e->texU, e->texV, e->texW, e->texH, &k);
    printf("menu_look_%s: %s row %d fill %.0f %.0f %.0f rim %.0f %.0f %.0f (%s %d,%d %dx%d)\n",
           ico_region_cur()->slug, what, row, inkFill(&k, 0), inkFill(&k, 1), inkFill(&k, 2),
           inkRim(&k, 0), inkRim(&k, 1), inkRim(&k, 2), s->name, e->texU, e->texV, e->texW,
           e->texH);
    if (k.fillW > 0) {
        inkMerge(all, &k);
        (*rows)++;
    }
}

/* The disc's sheets' light ink, measured over the pause menu's and the
   title's words (the rows the port's own rows sit among: the pause menu's
   Photo mode row, the title's Settings and Quit rows), in kSheetInk's terms
   (menu_font.h UiSheetInk: rimOn, rimLevel, fillLevel, dither, rimWeight).
   The values the table holds; the test fails when the sheets measure
   otherwise (menu_look's tolerances: the rim within 1 level of the
   measure, the fill within 3). Japanese, measured on SCPS-11003 (menu_look_jpn,
   8 words: pause 271..273, title 18..22): fill 252 252 252 (the letters'
   palette white is fcfcfc, not the European sheets' ffffff; 251.5 with the
   antialiased texels), rim 34 34 34 (34.3: black-grey, as the English
   sheets' 24 rather than the other European languages' 56..62), one
   antialiasing step (alpha 231) where the European sheets have three.
   Korean, measured on SCPS-56001 (menu_look_kor, the title, mc04 and
   option words): fill 252.8, rim 41.8, kSheetInk[UI_SHEET_KO] =
   {1, 42, 253, 1, 0}. Before that run the expectation was an offline
   estimate (the decoded SCPS-56001 sheets, UI.md "The Japanese and Korean
   sheets' geometry"): this survey's rule on the decoded Korean words,
   249.7 / 42.2, plus the 1.0 / 0.7 by which the same rule falls short of
   this test's own result on the Japanese words (250.5 / 33.6 against
   251.5 / 34.3), gave fill 250.7 and rim 42.9. */
typedef struct InkExpect {
    const char *slug;
    double fill, rim;
} InkExpect;

/* The Asian disc's (Traditional Chinese sheets) is the Japanese disc's
   numbers until menu_look_cn has measured SCPS-55001: this test prints
   "sheet ink fill .. rim .." and fails against them, so the printed values
   replace these two and kSheetInk[UI_SHEET_ZH] (menu_font.c). */
static const InkExpect kInkExpect[] = {
    {"jpn", 251.5, 34.3}, {"kor", 252.8, 41.8}, {"cn", 250.7, 39.4}};
#define TOL_LEVEL 0.6 /* menu_look_test's: grey levels */

static void testLook(IcoVfs *vfs)
{
    const IcoRegionInfo *r = ico_region_cur();
    double expFill = -1.0, expRim = -1.0;
    for (size_t i = 0; i < sizeof(kInkExpect) / sizeof(kInkExpect[0]); i++) {
        if (strcmp(kInkExpect[i].slug, r->slug) == 0) {
            expFill = kInkExpect[i].fill;
            expRim = kInkExpect[i].rim;
        }
    }
    CHECK(expFill > 0.0, "no ink expectation for the region %s", r->slug);
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
        printf("menu_look_%s: glyph %s row %d rect %d,%d,%d,%d ink %.1f%% bbox %d,%d,%d,%d (%s)\n",
               r->slug, kName[g], k->row, k->u, k->v, k->w, k->h, 100.0 * ink / (k->w * k->h),
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
    printf("menu_look_%s: sheet ink fill %.0f %.0f %.0f rim %.0f %.0f %.0f levels", r->slug,
           inkFill(&all, 0), inkFill(&all, 1), inkFill(&all, 2), inkRim(&all, 0), inkRim(&all, 1),
           inkRim(&all, 2));
    for (int i = 0; i < steps && i < 8; i++) {
        printf(" %d", lv[i]);
    }
    printf(" (%d steps, %d rows; kSheetInk[%d] = {%d, %d, %d, 1, 0})\n", steps, rows, r->menu_sheet,
           rimOn, rimOn ? (int)(rim + 0.5) : 0, (int)(fill + 0.5));

    /* the table holds the measure (menu_font.c kSheetInk[the row's menu_sheet]) */
    const UiSheetInk *ink = ui_menu_sheet_ink(r->menu_sheet);
    CHECK(rows >= 4, "only %d light words measured", rows);
    CHECK(fabs(fill - expFill) <= TOL_LEVEL, "fill %.1f, expected %.1f", fill, expFill);
    CHECK(fabs(rim - expRim) <= TOL_LEVEL, "rim %.1f, expected %.1f", rim, expRim);
    CHECK(ink->rimOn == rimOn, "kSheetInk[%d] rimOn %d, the sheets %d", r->menu_sheet, ink->rimOn,
          rimOn);
    CHECK(!rimOn || fabs(rim - (double)ink->rimLevel) <= 1.0, "kSheetInk[%d] rim %d, measured %.1f",
          r->menu_sheet, ink->rimLevel, rim);
    CHECK(fabs(fill - (double)ink->fillLevel) <= 3.0, "kSheetInk[%d] fill %d, measured %.1f",
          r->menu_sheet, ink->fillLevel, fill);
    CHECK(ink->dither == 1, "kSheetInk[%d] dithers as the European rows do", r->menu_sheet);
    ms_sheets_free();
}

/* ---------------------------------------------------------------- dump */

/* The pictures for comparing the port's words with the disc's
   (dump mode; no checks, no ctest): the sheets, every row with ink on
   them at x4, the port's strings in the sheets' look at x4, and the pairs
   of a game word's transcription (UI_STR_MT_*) and the row it transcribes.
   A row's word is found through the region table's rows (the title's and
   the pause menu's) and, with the European ELF, through the European
   layout of the same procedure: a layout with as many rows on both discs
   takes the European row's word (menu_text.c's table) for the disc's row
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
        printf("menu_region dump: cannot write %s\n", path);
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
    MT(PLAYERS_1),
    MT(PLAYERS_2),
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
#define MT_COUNT (UI_STR_MT_PLAYERS_2 - UI_STR_MT_LANG_ENGLISH + 1)
_Static_assert(MT_COUNT == 95, "kPortStr starts with every UI_STR_MT_* id, in order");

/* a port string rendered as ui_draw_menu_text draws it in the disc's
   sheets' light ink (kSheetInk[menu_sheet], the full rim), unlit sprite
   colour 0x80: grey and alpha 0..255, malloc'd */
typedef struct Rendered {
    uint8_t *grey, *alpha;
    int w, h;
    float size;
} Rendered;

static int renderPort(const PortStr *ps, Rendered *out)
{
    memset(out, 0, sizeof(*out));
    const char *text = ui_str_in((UiLang)ico_region_cur()->ui_lang_default, (UiStrId)ps->id);
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
    const UiSheetInk *k = ui_menu_item_ink(UI_INK_LIGHT, UI_RIM_FULL, ico_region_cur()->menu_sheet);
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

/* ------------------------------------------------------------ geometry */

/* The port's words against the disc's: the ink box of a word's
   letters (the fill texels, grey >= 200 and alpha >= 0.7, as the survey
   counts them; the rim and its halo are not the letters), and its columns
   with ink (the letters' width without the gaps between them), on the
   disc's row and on the port's strip of the same word (ui__menu_text_raster at
   UI_MENU_TEXT_SIZE in kSheetInk[menu_sheet], renderPort), with the
   fill's texel count, in the geometry of the word's sheet: the title's
   (Japanese: UI_SHEET_JA_TITLE: つづきから, はじめから) or the other
   screens' (UI_SHEET_JA: the pause menu's オプション, もどる, ゲーム終了 and
   Options' ズーム, キーコンフィグ).  Korean has one geometry, UI_SHEET_KO,
   for every sheet (no title variant: 새로 시작하기, 불러오기 on the title,
   옵션, 게임으로, 게임을 종료함 on the pause menu, 줌, 조작 설정 on
   Options).  Those are held to the disc: the ink height within GEO_TOL_H
   texels, the width within GEO_TOL_W of the disc's on the title sheet
   and GEO_TOL_W_OTHER on the others (their words are spaced unevenly:
   もどる and ズーム tighter than オプション, so one geometry with the
   disc's letter widths sets those wide), on the Korean disc GEO_TOL_W
   except GEO_TOL_W_OTHER for the condensed mc04 word, End Game
   (the offline fit has it +9.4 %, the rest within 4 %); more of Options' words are
   printed beside them. */
typedef struct GeoWord {
    int id;
    const char *name;
    int row;   /* the disc's table's row */
    int sheet; /* the geometry's sheet: UI_SHEET_JA_TITLE or UI_SHEET_JA, UI_SHEET_KO, UI_SHEET_ZH */
    int held;
    int title;   /* the title screen's word (ui_menu_text_set_title) */
    double tolW; /* the width gate, a fraction of the disc's */
} GeoWord;

typedef struct GeoBox {
    int w, h, fill;
    int cols; /* the columns with a fill texel: the letters' own width, the gaps not */
} GeoBox;

#define GEO_TOL_H 1
#define GEO_TOL_W 0.10
#define GEO_TOL_W_OTHER 0.17

static int geoFill(int grey, int alpha)
{
    return grey >= 200 && alpha * 10 >= 7 * 255;
}

static GeoBox geoBoxOf(const uint8_t *grey, const uint8_t *alpha, int w, int h, int stride)
{
    GeoBox b = {0, 0, 0, 0};
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (geoFill(grey[y * stride + x], alpha[y * stride + x])) {
                b.fill++;
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 >= 0) {
        b.w = x1 - x0 + 1;
        b.h = y1 - y0 + 1;
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (geoFill(grey[y * stride + x], alpha[y * stride + x])) {
                b.cols++;
                break;
            }
        }
    }
    return b;
}

static int geoRowBox(IcoVfs *vfs, int row, GeoBox *out)
{
    const LtProperty *e = &texProperty[row];
    const MsSheet *s = sheetOfRow(vfs, row);
    if (!s || e->texW < 8 || e->texH < 8) {
        return 0;
    }
    const int w = e->texW, h = e->texH;
    uint8_t *g = calloc((size_t)w * (size_t)h, 1), *a = calloc((size_t)w * (size_t)h, 1);
    if (!g || !a) {
        free(g);
        free(a);
        return 0;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int u = e->texU + x, v = e->texV + y;
            if (u >= s->w || v >= s->h) {
                continue;
            }
            const uint8_t *p = &s->rgba[((size_t)v * (size_t)s->w + (size_t)u) * 4];
            g[y * w + x] = (uint8_t)((p[0] + p[1] + p[2]) / 3);
            a[y * w + x] = p[3];
        }
    }
    *out = geoBoxOf(g, a, w, h, w);
    free(g);
    free(a);
    return out->w > 0;
}

static int geoPortBox(int id, GeoBox *out)
{
    const PortStr ps = {id, "", 0.0f, 0};
    Rendered r;
    if (!renderPort(&ps, &r)) {
        return 0;
    }
    *out = geoBoxOf(r.grey, r.alpha, r.w, r.h, r.w);
    free(r.grey);
    free(r.alpha);
    return out->w > 0;
}

/* the words' boxes on the disc and drawn by the port, printed when verbose;
   the sum of the squared misses (height in texels, width in tenths, the
   fill's amount in halves) over the held words, and their failures */
/* the words of one sheet (sheet; -1 every word): their boxes on the disc
   and drawn by the port, printed when verbose; the sum of the squared
   misses (height in texels, width in tenths, the fill's amount in halves)
   over the held words, and their failures in *bad */
static double geoScore(const GeoWord *wd, int n, const GeoBox *disc, int sheet, int verbose,
                       int *bad)
{
    double score = 0.0;
    *bad = 0;
    for (int i = 0; i < n; i++) {
        if (disc[i].w <= 0 || (sheet >= 0 && wd[i].sheet != sheet) || (!verbose && !wd[i].held)) {
            continue;
        }
        const int was = ui_menu_text_set_title(wd[i].title);
        GeoBox p;
        const int ok0 = geoPortBox(wd[i].id, &p);
        ui_menu_text_set_title(was);
        if (!ok0) {
            continue;
        }
        const double dh = (double)(p.h - disc[i].h);
        const double dw = (double)(p.w - disc[i].w) / (double)disc[i].w;
        const double da = disc[i].fill > 0 ? (double)p.fill / (double)disc[i].fill - 1.0 : 0.0;
        const double dc = (double)(p.cols - disc[i].cols) / (double)disc[i].w;
        const int ok = abs(p.h - disc[i].h) <= GEO_TOL_H && fabs(dw) <= wd[i].tolW;
        if (wd[i].held) {
            score += dh * dh + 100.0 * dw * dw + 100.0 * dc * dc + 4.0 * da * da;
            *bad += !ok;
        }
        if (verbose) {
            printf("menu_look_%s: geometry %-5s %-13s row %3d disc %3d x %2d (%3d inked) fill %3d, "
                   "port %3d x %2d (%3d inked) fill %3d: height %+d, width %+.1f %%, fill %+.0f "
                   "%%%s\n",
                   ico_region_cur()->slug, wd[i].title ? "title" : "other", wd[i].name, wd[i].row,
                   disc[i].w, disc[i].h, disc[i].cols, disc[i].fill, p.w, p.h, p.cols, p.fill,
                   p.h - disc[i].h, 100.0 * dw, 100.0 * da,
                   wd[i].held ? (ok ? "" : " (off)") : " (printed)");
        }
    }
    return score;
}

static void testGeometry(IcoVfs *vfs)
{
    const IcoRegionInfo *r = ico_region_cur();
    /* the sheets' geometry: the Japanese disc has the title's own (T) and
       the other screens' (O); the Korean and Asian discs' one (UI_SHEET_KO, UI_SHEET_ZH) */
    const int kor = r->menu_sheet == UI_SHEET_KO || r->menu_sheet == UI_SHEET_ZH;
    const int T = kor ? r->menu_sheet : UI_SHEET_JA_TITLE, O = r->menu_sheet;
    const double tolT = GEO_TOL_W, tolO = kor ? GEO_TOL_W : GEO_TOL_W_OTHER;
    /* Options' words: the Japanese disc's rows (zoom 318, button config 300, sound 285 ...)
       moved by the distance of the pause menu's Options row from the Japanese disc's 271.
       Evidence for the Korean disc: its tex-property table is 406 rows, every row from the
       title's copyright line (JPN 18) on 6 lower (the glyph rows 321 -> 315, 155 -> 149, the
       pause rows 271 -> 265: region_kor.c, region_select_test's Korean values) */
    const int d = r->row_pause_options - 271;
    const GeoWord wd[] = {
        {UI_STR_MT_CONTINUE, "CONTINUE", r->row_title_continue, T, 1, 1, tolT},
        {UI_STR_MT_NEW_GAME, "NEW_GAME", r->row_title_new, T, 1, 1, tolT},
        {UI_STR_MT_OPTIONS, "OPTIONS", r->row_pause_options, O, 1, 0, tolO},
        {UI_STR_MT_PAUSE_BACK, "PAUSE_BACK", r->row_pause_back, O, 1, 0, tolO},
        /* the condensed mc04 word: the offline Korean fit has it +9.4 % wide */
        {UI_STR_MT_END_GAME, "END_GAME", r->row_pause_end, O, 1, 0, GEO_TOL_W_OTHER},
        /* Options' words (their halo is the Options page's dark glow) */
        {UI_STR_MT_ZOOM, "ZOOM", 318 + d, O, 1, 0, tolO},
        {UI_STR_MT_BUTTON_CONFIG, "BUTTON_CONFIG", 300 + d, O, 1, 0, tolO},
        {UI_STR_MT_SOUND, "SOUND", 285 + d, O, 0, 0, tolO},
        {UI_STR_MT_STEREO, "STEREO", 288 + d, O, 0, 0, tolO},
        {UI_STR_MT_MONO, "MONO", 289 + d, O, 0, 0, tolO},
        {UI_STR_MT_VIBRATION, "VIBRATION", 290 + d, O, 0, 0, tolO},
        {UI_STR_MT_BRIGHTNESS, "BRIGHTNESS", 301 + d, O, 0, 0, tolO},
        {UI_STR_MT_JUMP, "JUMP", 313 + d, O, 0, 0, tolO},
        {UI_STR_MT_ACTION, "ACTION", 315 + d, O, 0, 0, tolO},
    };
    const int n = (int)(sizeof(wd) / sizeof(wd[0]));
    GeoBox disc[sizeof(wd) / sizeof(wd[0])];
    for (int i = 0; i < n; i++) {
        memset(&disc[i], 0, sizeof(disc[i]));
        const int ok =
            wd[i].row >= 0 && wd[i].row < r->property_count && geoRowBox(vfs, wd[i].row, &disc[i]);
        CHECK(ok || !wd[i].held, "geometry: no word on row %d (%s)", wd[i].row, wd[i].name);
    }
    ui_set_sheet_language(r->menu_sheet);
    const int sheets[2] = {T, O};
    const char *fit = getenv("ICO_MENU_LOOK_JPN_FIT");
    if (fit == NULL) {
        fit = getenv("ICO_MENU_LOOK_FIT");
    }
    for (int k = 0; k < (T == O ? 1 : 2); k++) {
        const int sh = sheets[k];
        const char *name = T == O ? "all" : sh == T ? "title" : "other";
        float em, wx, gap, bx, by;
        ui__menu_ja_geometry(sh, &em, &wx, &gap, &bx, &by);
        if (fit && *fit == '1') {
            /* the search the table's numbers came from: within the gate
               first, then the smallest misses */
            double best = 1e30;
            float be = em, bw = wx, bg = gap, bb = bx;
            for (float e = 0.90f; e <= 1.2001f; e += 0.02f) {
                for (float x = 0.50f; x <= 1.1001f; x += 0.02f) {
                    for (float q = 0.0f; q <= 10.0001f; q += 0.5f) {
                        for (float b = 0.0f; b <= 0.3001f; b += 0.15f) {
                            ui__menu_set_ja_geometry(sh, e, x, q, b, b);
                            int bad;
                            const double sc = geoScore(wd, n, disc, sh, 0, &bad) + 1000.0 * bad;
                            if (sc < best) {
                                best = sc;
                                be = e;
                                bw = x;
                                bg = q;
                                bb = b;
                            }
                        }
                    }
                }
            }
            printf("menu_look_%s: geometry fit %s em %.2f wx %.2f gap %.2f bold %.2f (score "
                   "%.2f)\n",
                   r->slug, name, be, bw, bg, bb, best);
            ui__menu_set_ja_geometry(sh, be, bw, bg, bb, bb);
            int bad;
            (void)geoScore(wd, n, disc, sh, 1, &bad);
            ui__menu_set_ja_geometry(sh, em, wx, gap, bx, by);
        }
        printf("menu_look_%s: geometry %s: em %.2f wx %.2f gap %.2f bold %.2f %.2f\n", r->slug,
               name, em, wx, gap, bx, by);
        int bad;
        (void)geoScore(wd, n, disc, sh, 1, &bad);
        CHECK(bad == 0,
              "geometry %s: %d words off the disc's (height within %d texel, width within %.0f %%)",
              name, bad, GEO_TOL_H, 100.0 * (sh == T ? tolT : tolO));
    }
    ms_sheets_free();
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

/* the European layouts kept before the disc's tables are loaded */
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
        printf("menu_region dump: %s: %s\n", path, err);
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
                "menu_region dump: layout %d (%d rows) and the European %d (%d rows): not aligned\n",
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
    printf("menu_region dump: %d layouts aligned with the European ones, %d of unequal rows\n",
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
    /* the port's words as drawn over the disc's sheets (menu_font.c sheetGeo) */
    const IcoRegionInfo *r = ico_region_cur();
    const UiLang lang = (UiLang)r->ui_lang_default;
    ui_set_sheet_language(r->menu_sheet);
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
        printf("menu_region dump: cannot write %s\n", path);
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
                ps ? ps->name : "-", ps ? " \"" : "", ps ? ui_str_in(lang, (UiStrId)word[i]) : "",
                ps ? "\"" : "");
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
    fprintf(ix, "\n# port strings (ui_str_in of the disc's language, the sheets' light ink): name "
                "em box text rows\n");
    for (int i = 0; rend && i < PORT_STR_COUNT; i++) {
        const PortStr *ps = &kPortStr[i];
        /* the title screen's words in the title sheet's geometry (the
           port's title rows are marked so: settings.c placeTitle) */
        const int was =
            ui_menu_text_set_title(ps->id == UI_STR_MT_CONTINUE || ps->id == UI_STR_MT_NEW_GAME ||
                                   ps->id == UI_STR_QUIT_DESKTOP);
        const int ok = renderPort(ps, &rend[i]);
        ui_menu_text_set_title(was);
        if (!ok) {
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
                ui_str_in(lang, (UiStrId)ps->id),
                (lang == UI_LANG_ZH   ? ui_strings_zh
                 : lang == UI_LANG_KO ? ui_strings_ko
                                      : ui_strings_ja)[ps->id]
                    ? ""
                    : " (English)",
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
    printf("menu_region dump: %d sheets, %d rows with ink (%d without a port id), %d port strings, "
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
    /* menu_region_test MODE SLUG ELF [DISC [OUT_DIR [PAL_ELF]]] */
    const int isDump = argc > 1 && strcmp(argv[1], "dump") == 0;
    if (argc < 4 || (strcmp(argv[1], "text") != 0 && strcmp(argv[1], "look") != 0 && !isDump) ||
        (isDump && argc < 6)) {
        fprintf(stderr, "usage: menu_region_test text|look SLUG BASE_ELF [DISC_IMAGE]\n"
                        "       menu_region_test dump SLUG BASE_ELF DISC_IMAGE OUT_DIR "
                        "[BASE_ELF_PAL]\n");
        return 2;
    }
    const char *slug = argv[2];
    const char *elfPath = argv[3];
    const char *discPath = argc > 4 ? argv[4] : NULL;
    for (int i = 0; i < ico_region_count(); i++) {
        if (strcmp(ico_region_at(i)->slug, slug) == 0) {
            s_set = ico_table_sets[i];
        }
    }
    if (s_set == NULL || strcmp(slug, "pal") == 0) {
        fprintf(stderr, "menu_region_test: no region %s (jpn, kor or cn)\n", slug);
        return 2;
    }
    if (isDump && argc > 6) {
        /* the European layouts, kept for the row alignment, before the
           disc's tables replace them */
        if (loadSet(&ico_table_set_pal, argv[6])) {
            s_palPropN = rowsIn(&ico_table_set_pal, texProperty);
            s_palLayoutN = rowsIn(&ico_table_set_pal, texLayout);
            s_palProp = malloc(sizeof(LtProperty) * (size_t)s_palPropN);
            s_palLayout = malloc(sizeof(LtProp) * (size_t)s_palLayoutN);
            if (s_palProp && s_palLayout) {
                memcpy(s_palProp, texProperty, sizeof(LtProperty) * (size_t)s_palPropN);
                memcpy(s_palLayout, texLayout, sizeof(LtProp) * (size_t)s_palLayoutN);
            }
        } else {
            printf("menu_region dump: no European ELF (%s): the region table's rows only\n",
                   argv[6]);
        }
    }
    size_t size;
    uint8_t *elf = readAll(elfPath, &size);
    if (!elf) {
        printf("menu_region_test %s: no base ELF (%s): skipped\n", slug, elfPath);
        return 77;
    }
    char err[512];
    const int rc = ico_tables_load_elf(s_set, elf, size, err, sizeof(err));
    free(elf);
    if (rc != 0) {
        printf("menu_region_test %s: the ELF's tables do not load: %s\n", slug, err);
        return 1;
    }
    if (ico_region_select(slug) != 0) {
        printf("menu_region_test: no region %s\n", slug);
        return 1;
    }
    /* the port's words in the disc's language, drawn from the font that
       language's ideographs come from (font.c ui__font_prefer_chinese) */
    ui_set_language((UiLang)ico_region_cur()->ui_lang_default);
    ui__font_prefer_chinese(ico_region_cur()->ui_lang_default == UI_LANG_ZH);
    if (strcmp(argv[1], "text") == 0) {
        testText();
    } else if (isDump) {
        IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, discPath);
        if (!vfs) {
            printf("menu_region_test %s: no disc image (%s): skipped\n", slug, discPath);
            return 77;
        }
        const int drc = dump(vfs, argv[5]);
        ico_vfs_unmount(vfs);
        free(s_palProp);
        free(s_palLayout);
        return drc;
    } else {
        IcoVfs *vfs = discPath ? ico_vfs_mount(&ico_vfs_iso9660, discPath) : NULL;
        if (!vfs) {
            printf("menu_region_test %s: no disc image (%s): skipped\n", slug,
                   discPath ? discPath : "");
            return 77;
        }
        testLook(vfs);
        testGeometry(vfs);
        ico_vfs_unmount(vfs);
    }
    printf("menu_region_test %s: %d failures\n", slug, failures);
    return failures ? 1 : 0;
}
