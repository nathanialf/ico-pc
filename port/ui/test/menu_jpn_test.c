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
 *   menu_jpn_test text BASE_ELF_JPN
 *   menu_jpn_test look BASE_ELF_JPN DISC_IMAGE_JPN
 *
 * Exit 0, 1 on a failure, 77 without the ELF (or, for look, the disc).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charFileManager.h" /* texFile */
#include "../../core/region.h"
#include "layout_texture.h" /* texProperty, texLayout */
#include "menu_font.h"
#include "menu_sheets.h"
#include "menu_text.h"
#include "tables.h"
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

int main(int argc, char **argv)
{
    if (argc < 3 || (strcmp(argv[1], "text") != 0 && strcmp(argv[1], "look") != 0)) {
        fprintf(stderr, "usage: menu_jpn_test text|look BASE_ELF_JPN [DISC_IMAGE_JPN]\n");
        return 2;
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
