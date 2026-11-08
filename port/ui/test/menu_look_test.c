/* menu_look_test.c: the menus' Arimo text against the game's menu sheets.
 *
 * Every menu word is Arimo rasterised on the sheets' texel grid and drawn in
 * the sheets' style (menu_font.h, rd.h rd_create_texture_sheet).  This test
 * holds that look to the sheets it imitates: the sheets are read from the
 * player's disc (menu_sheets.c), the item strips are the ones the game draws
 * (ui__menu_strip_raster) run through the shader's CPU reference
 * (port/render/test/sheet_ref.c), and the two pictures are compared.
 *
 *   testSurvey        measures the sheets (the light-ink items of the menu
 *                     text table on the five languages' sheets) and pins
 *                     the constants that came from it: kSheetInk (menu_font.c),
 *                     ICO_SHEET_RX / RY, the rim's falloff ICO_SHEET_WX / WY
 *                     and the floor of ICO_SHEET_LEVELS (shader_consts.h).
 *   testFitCopy       the fit's texels and the outline mode's control look
 *                     against sheetref_texel.
 *   testCompareItems  every item of ui_menu_text_items in the five languages:
 *                     the reference strip against the sheet's rectangle,
 *                     both composited on black and on mid-grey with MODULATE
 *                     0x80 (the sprite colour of an unlit row), measured with
 *                     the limits below; writes the pictures.
 *
 *   menu_look_test BASE_ELF DISC_IMAGE OUT_DIR
 *
 * Pictures: OUT_DIR/menu_look/<lang>/<row>.png (the item's first texProperty
 * row), the sheet ("before", left) and the strip ("after", right), on grey,
 * the top row at x4 nearest, the bottom at x4 bilinear; and
 * OUT_DIR/menu_look/<lang>/contact.png, every item of the language at x1.
 *
 * ICO_MENU_LOOK_FIT=1 (development only): searches the levels and each
 * language's rim and fill grey (the rim's reach and falloff are the
 * survey's) for the smallest blurred difference over the light-ink items and
 * prints the best, instead of the checks (sheet_ref.c's arithmetic, each
 * item's rim computed once; testFitCopy holds it to sheetref_texel).  The constants it finds are then
 * written into shader_consts.h, sheet_text.hlsli and kSheetInk by hand.
 *
 * ICO_MENU_LOOK_REF46=1 (development only): measures the publisher's plate
 * at the foot of the title screen ("Sony Computer Entertainment Europe
 * Presents", texProperty row 46) and the copyright line (row 48) on the
 * five languages' sheets, each sheet once (title.tm2 is one for all), and
 * prints, instead of the checks: the grey/alpha histogram; per distance
 * from the letters (0 the ink, 1 .. 8 texels across in the row and down in
 * the column) the mean alpha, the alpha-weighted darkness, the count, the
 * variance, the alpha histogram and the agreement with the 4x4 and 8x8
 * Bayer orders at their best phase; the outline's alpha steps and grey; the
 * falloff normalised to the ink (WX' 0 .. 6, WY' 0 .. 4) and the dither it
 * shows (refProfile).  ICO_MENU_LOOK_INK=<grey> lowers the ink rule (grey
 * 200).  Pictures OUT_DIR/menu_look/ref46/<row>-<lang>.png: the crop on
 * black, on each ICO_MENU_LOOK_BG=g1,g2 grey (the title's backdrop under
 * the plate, at most four) and on 128, x1 above, x4 nearest below.
 *
 * ICO_MENU_LOOK_OUTLINE=1 (development only): the menus' words drawn in
 * candidate looks (sheet_ref.h SheetLook: the rim's falloff, the levels,
 * the dither matrix; s_cand below, C4 the plate's measured profile, C5 the
 * shader's constants) beside the plate, instead of the checks: per
 * candidate OUT_DIR/menu_look/outline/<name>.png (rows: the words x {full,
 * faint rim} x {rim black, the language's rim grey}; columns: black, the
 * ICO_MENU_LOOK_BG greys, 128; the first column the row-46 crop; x1 above,
 * x4 nearest below), contact.png (every candidate's x1 block, the crop once
 * at the top) and contact.txt (the legend: the order, each block's y and
 * tables).  No shader has these looks: they are pictures to choose from.
 *
 * ICO_MENU_LOOK_GEOFIT=1 (development only): fits each item's em, width,
 * anchor and capital middle per language to its sheet (geoFit) and prints
 * them, instead of the checks; =2 keeps the width.  The menu text table's
 * values came from it.  ICO_MENU_LOOK_BOLD=x,y and ICO_MENU_LOOK_WIDTH=k
 * set the letters' weight and the menus' width for a run (the fit's
 * grids).  ICO_MENU_LOOK_GEOM=1 prints every item's ink box and capitals
 * against the sheet's; ICO_MENU_LOOK_RIM=1 prints each item's halo opacity
 * (the sheet's, the strip's with the full halo and as drawn: what the
 * table's rim values were set from).  The medians per sheet and the limits'
 * measures per language are always printed.
 *
 * Exit 0, 1 on a failure, 77 without the ELF or the disc image (so it runs
 * where the disc is: main's release validation, never in a worktree).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charFileManager.h" /* texFile */
#include "df_pack.h"
#include "layout_texture.h" /* texProperty */
#include "menu_font.h"
#include "menu_sheets.h"
#include "menu_text.h"
#include "miniz.h"       /* the outline mode's compressed pictures */
#include "rd_internal.h" /* rd_write_png */
#include "sheet_ref.h"
#include "shader_consts.h"
#include "strings.h"
#include "tables.h"
#include "ui_internal.h"
#include "vfs.h"
#include "host_fs.h"

/* ------------------------------------------------------------- limits */
/* The limits of testCompareItems, set from the run on main after the
   geometry fit (the sheets' width UI_SHEET_WIDTH, the letters'
   weight, the table's em, anchor, capital middle, width and spacing per
   language fitted to each sheet, the halo per item, 8 levels; 100 items a
   language with ink on the sheet, 500 in all): the largest measure over the
   five languages plus 25 %.  Measured, median / max per language EN FR DE
   IT ES, the larger of the two backgrounds, the figure 1 left out (below);
   the first run on main, before the fit, in brackets.  The ink box: width
   strip / sheet median 1.000 on every language (1.12 .. 1.25), height 1.000
   (0.93 on German; 0.83 .. 0.92), the first line's capital height 0.98 ..
   1.00 (0.82 .. 0.95), its top +0.00 .. +0.10 rows (+0.5 .. +1.0). */
/* blur 7.9/21.2 8.4/27.3 8.3/15.8 8.1/23.5 8.7/29.3 (27.8 .. 31.9 / 56.2);
   the worst Spanish rows 173 and 142 (menu_PAL_02) */
#define T_BLUR 37.0f
/* plain 11.2/26.9 11.9/34.8 11.7/22.8 11.3/30.7 12.2/44.3 (35.7 .. 41.3 /
   73.6) */
#define T_PLAIN 56.0f
/* the ink box's worst edge, texels: 1/3 1/3 1/3 1/11 1/4 (6 .. 10 / 50); the
   worst Italian row 220 (Vuoi salvare?: the sheet's words are set looser,
   the strip's ink ends 11 texels short) */
#define T_EDGE 14.0f
/* the same for an item of several lines: 1/1 everywhere (15 .. 20 / 40); a
   texel's step */
#define T_EDGE_MULTI 1.5f
/* the first line's capital top and baseline, rows: 0.28/1.29 0.26/1.30
   0.16/1.60 0.16/1.15 0.31/2.33 (0.92 .. 1.00 / 2.33); the worst Spanish
   rows 142 and 173 (menu_PAL_02: the baseline under a descender-heavy
   word) */
#define T_LINE 2.9f
/* the ink amount, strip / sheet: median 1.01 1.04 0.94 1.03 1.02, min 0.82,
   max 1.21 (0.76 .. 0.84, 0.23 .. 1.10) */
#define T_AMOUNT_LO 0.65f
#define T_AMOUNT_HI 1.5f
/* the rim / fill / edge shares, the worst of the three, absolute: median
   0.13 0.07 0.08 0.08 0.07, max 0.53 (0.07 .. 0.21, 0.80): the rimless
   sheets' antialiasing is grey at full opacity where the strips' is white
   at partial opacity, so their softest edge texels count as rim */
#define T_SHARE 0.66f
/* The figure 1 (rows 52, 61 "10", 76, 304; the IT dash of row 414 is now
   inside the common limits): Arimo's 1 has a foot and a long flag the
   sheets' 1 has not, so its ink and box differ whatever the size; worst
   blur 52.5 and plain 62.0 (Italian row 76), edge 7 (German row 52), ink
   0.64 .. 1.37 */
#define T_ONE_BLUR 66.0f
#define T_ONE_PLAIN 78.0f
#define T_ONE_EDGE 9.0f
#define T_ONE_AMOUNT_LO 0.5f
#define T_ONE_AMOUNT_HI 1.7f
#define MIN_INK_AMOUNT 6.0f /* a sheet rectangle with less ink than this is not compared */

/* The survey's expectations (testSurvey): what the scratch survey measured on
   the PAL sheets, per language EN FR DE IT ES. */
static const float kExpFill[UI_LANG_PAL_COUNT] = {254.7f, 253.7f, 253.3f, 254.4f, 253.5f};
static const float kExpRim[UI_LANG_PAL_COUNT] = {23.9f, 61.1f, 55.7f, 60.8f, 61.7f};
/* the first six texels' alpha, summed: left and right averaged, above and below averaged */
static const float kExpMassX[UI_LANG_PAL_COUNT] = {3.10f, 3.73f, 3.99f, 3.78f, 3.64f};
static const float kExpMassY[UI_LANG_PAL_COUNT] = {2.15f, 2.83f, 2.99f, 2.90f, 2.74f};
#define TOL_LEVEL 0.6f /* grey levels */
#define TOL_MASS 0.08f
/* the rim's falloff (ICO_SHEET_WX / WY, per mille) against the English
   sheets' mean rim alpha at each distance, per entry */
#define TOL_FALLOFF 0.08f
#define EXP_STEPS 3 /* the median number of antialiasing steps of a sheet's white */

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

static const char *const kLang[UI_LANG_PAL_COUNT] = {"en", "fr", "de", "it", "es"};

/* ------------------------------------------------------------- the rects */

/* One item on one language's sheet: its rectangle's texels. */
typedef struct Rect {
    int item, row, lang;
    int ink;  /* UiMenuTextInk */
    int u, v; /* the rectangle's corner on the sheet, texels */
    int w, h;
    const char *sheet;     /* the sheet's name */
    uint8_t *grey, *alpha; /* w x h, alpha 0..255 */
} Rect;

static IcoVfs *s_vfs;
static Rect *s_rect[UI_LANG_PAL_COUNT];
static int s_nrect[UI_LANG_PAL_COUNT];

static int rowOfItem(int item)
{
    for (int r = 0; r < ui_menu_text_row_count; r++) {
        if (ui_menu_text_rows[r].item == item) {
            return ui_menu_text_rows[r].row;
        }
    }
    return -1;
}

/* The rectangle of texProperty row `row` (0 .. 411, the rows every disc
   has) on language lang's sheet: its texels into out (grey the red channel,
   as the sheets are grey; alpha 0..255, malloc'd; item -1, ink 0).  The
   sheet is resolved as the game resolves it: the row's texFile path, its
   language folder replaced (ms_sheet_name; title.tm2 is one sheet for every
   language).  0, or -1 when the row names no sheet on the disc or lies
   outside it. */
static int rectOfRow(int row, int lang, Rect *out)
{
    memset(out, 0, sizeof(*out));
    out->item = -1;
    if (row < 0 || row >= 412) {
        return -1;
    }
    const LtProperty *e = &texProperty[row];
    if (e->texFileNo < 0 || e->texFileNo >= ms_tex_file_count() || e->texW <= 0 || e->texH <= 0 ||
        e->texU < 0 || e->texV < 0) {
        return -1;
    }
    char name[64];
    ms_sheet_name(texFile[e->texFileNo].path, lang, name, sizeof(name));
    const MsSheet *s = ms_sheet_for(s_vfs, name);
    if (!s || e->texU + e->texW > s->w || e->texV + e->texH > s->h) {
        return -1;
    }
    out->row = row;
    out->lang = lang;
    out->u = e->texU;
    out->v = e->texV;
    out->w = e->texW;
    out->h = e->texH;
    out->sheet = s->name;
    out->grey = malloc((size_t)out->w * (size_t)out->h);
    out->alpha = malloc((size_t)out->w * (size_t)out->h);
    if (!out->grey || !out->alpha) {
        free(out->grey);
        free(out->alpha);
        out->grey = out->alpha = NULL;
        return -1;
    }
    for (int y = 0; y < out->h; y++) {
        for (int x = 0; x < out->w; x++) {
            const uint8_t *p =
                s->rgba + ((size_t)(out->v + y) * (size_t)s->w + (size_t)(out->u + x)) * 4;
            out->grey[y * out->w + x] = p[0];
            out->alpha[y * out->w + x] = p[3];
        }
    }
    return 0;
}

static void collect(void)
{
    for (int lang = 0; lang < UI_LANG_PAL_COUNT; lang++) {
        s_rect[lang] = calloc((size_t)ui_menu_text_item_count, sizeof(Rect));
        for (int i = 0; i < ui_menu_text_item_count; i++) {
            const UiMenuTextItem *it = &ui_menu_text_items[i];
            const int row = rowOfItem(i);
            if (row < 0) {
                continue;
            }
            const LtProperty *e = &texProperty[row];
            if (e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h) {
                continue;
            }
            Rect r;
            if (rectOfRow(row, lang, &r) != 0) {
                continue;
            }
            /* the same texels once (the player count's items are the film
               effect's 1 and 2 tiles under their own words) */
            int seen = 0;
            for (int k = 0; k < s_nrect[lang] && !seen; k++) {
                const UiMenuTextItem *o = &ui_menu_text_items[s_rect[lang][k].item];
                seen = s_rect[lang][k].sheet == r.sheet && o->u == it->u && o->v == it->v &&
                       o->w == it->w && o->h == it->h;
            }
            if (seen) {
                free(r.grey);
                free(r.alpha);
                continue;
            }
            r.item = i;
            r.ink = it->ink;
            s_rect[lang][s_nrect[lang]++] = r;
        }
    }
}

/* -------------------------------------------------------------- survey */

typedef struct Survey {
    double fill, rim, massX, massY;
    double profX[7], profY[7]; /* the mean alpha (0..1) at distance 1..6, [0] unused */
    int sheets;
    int steps[16]; /* the antialiasing steps of each sheet's white */
} Survey;

static int cmpInt(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* The measures of the survey, over the light-ink rectangles of a language
   (the definitions are the scratch survey's):
   fill   alpha-weighted mean grey of the texels with grey >= 240 and alpha >= 0.9
   rim    the same over the texels with alpha >= 0.5 and grey <= 100 that have no
          fill texel (grey >= 200, alpha >= 0.7) within a texel
   mass   outside the letters of a short word (w <= 60, h <= 25), the mean alpha
          at distance 1..6 from the first / last fill texel of each row (X) and the
          topmost / lowest of each column (Y), summed over the six distances
   steps  per sheet, the distinct alphas of its white texels (grey >= 240, alpha
          between 13 and 254) taken by at least 1 % of them, merged when within 16:
          the clusters that are not full (< 0.93) */
static void survey(int lang, Survey *out)
{
    memset(out, 0, sizeof(*out));
    double fN = 0, fD = 0, rN = 0, rD = 0;
    double pm[4][7][2];
    memset(pm, 0, sizeof(pm));
    const char *names[16];
    static long hist[16][256];
    memset(hist, 0, sizeof(hist));
    for (int k = 0; k < s_nrect[lang]; k++) {
        const Rect *r = &s_rect[lang][k];
        if (r->ink != UI_INK_LIGHT) {
            continue;
        }
        const int w = r->w, h = r->h;
        uint8_t *fill = calloc((size_t)w * (size_t)h, 1);
        int nfill = 0;
        for (int i = 0; i < w * h; i++) {
            const int g = r->grey[i], a = r->alpha[i];
            if (g >= 240 && a * 10 >= 9 * 255) {
                fN += (double)g * a;
                fD += a;
            }
            if (g >= 200 && a * 10 >= 7 * 255) {
                fill[i] = 1;
                nfill++;
            }
        }
        if (nfill == 0) {
            free(fill);
            continue;
        }
        int si = 0;
        while (si < out->sheets && names[si] != r->sheet) {
            si++;
        }
        if (si == out->sheets && si < 16) {
            names[out->sheets++] = r->sheet;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int i = y * w + x, g = r->grey[i], a = r->alpha[i];
                if (a * 2 >= 255 && g <= 100) {
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
                        rN += (double)g * a;
                        rD += a;
                    }
                }
                if (si < 16 && g >= 240 && a >= 13 && a < 255) {
                    hist[si][a]++;
                }
            }
        }
        if (r->w <= 60 && r->h <= 25 && nfill >= 5) {
            for (int y = 0; y < h; y++) {
                int x0 = -1, x1 = -1;
                for (int x = 0; x < w; x++) {
                    if (fill[y * w + x]) {
                        x0 = x0 < 0 ? x : x0;
                        x1 = x;
                    }
                }
                if (x0 < 0) {
                    continue;
                }
                for (int d = 1; d <= 6; d++) {
                    if (x0 - d >= 0) {
                        pm[0][d][0] += r->alpha[y * w + x0 - d];
                        pm[0][d][1] += 1;
                    }
                    if (x1 + d < w) {
                        pm[1][d][0] += r->alpha[y * w + x1 + d];
                        pm[1][d][1] += 1;
                    }
                }
            }
            for (int x = 0; x < w; x++) {
                int y0 = -1, y1 = -1;
                for (int y = 0; y < h; y++) {
                    if (fill[y * w + x]) {
                        y0 = y0 < 0 ? y : y0;
                        y1 = y;
                    }
                }
                if (y0 < 0) {
                    continue;
                }
                for (int d = 1; d <= 6; d++) {
                    if (y0 - d >= 0) {
                        pm[2][d][0] += r->alpha[(y0 - d) * w + x];
                        pm[2][d][1] += 1;
                    }
                    if (y1 + d < h) {
                        pm[3][d][0] += r->alpha[(y1 + d) * w + x];
                        pm[3][d][1] += 1;
                    }
                }
            }
        }
        free(fill);
    }
    out->fill = fD > 0 ? fN / fD : 0;
    out->rim = rD > 0 ? rN / rD : 0;
    double m[4] = {0, 0, 0, 0};
    for (int s = 0; s < 4; s++) {
        for (int d = 1; d <= 6; d++) {
            m[s] += pm[s][d][1] > 0 ? pm[s][d][0] / pm[s][d][1] / 255.0 : 0;
        }
    }
    out->massX = (m[0] + m[1]) * 0.5;
    out->massY = (m[2] + m[3]) * 0.5;
    for (int d = 1; d <= 6; d++) {
        double px[2], py[2];
        for (int k = 0; k < 2; k++) {
            px[k] = pm[k][d][1] > 0 ? pm[k][d][0] / pm[k][d][1] / 255.0 : 0;
            py[k] = pm[2 + k][d][1] > 0 ? pm[2 + k][d][0] / pm[2 + k][d][1] / 255.0 : 0;
        }
        out->profX[d] = (px[0] + px[1]) * 0.5;
        out->profY[d] = (py[0] + py[1]) * 0.5;
    }
    for (int si = 0; si < out->sheets; si++) {
        long tot = 0;
        for (int a = 0; a < 256; a++) {
            tot += hist[si][a];
        }
        int last = -1000, steps = 0, clusterMax = 0, open = 0;
        for (int a = 0; a < 256; a++) {
            if (hist[si][a] > 0 && hist[si][a] * 100 >= tot) {
                if (open && a - last <= 16) {
                    clusterMax = a;
                } else {
                    if (open && clusterMax * 100 < 93 * 255) {
                        steps++;
                    }
                    open = 1;
                    clusterMax = a;
                }
                last = a;
            }
        }
        if (open && clusterMax * 100 < 93 * 255) {
            steps++;
        }
        out->steps[si] = steps;
    }
}

static void testSurvey(void)
{
    Survey sv[UI_LANG_PAL_COUNT];
    double meanX = 0, meanY = 0;
    int steps[UI_LANG_PAL_COUNT * 16], nsteps = 0;
    for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
        survey(l, &sv[l]);
        const UiSheetInk *ink = ui_menu_sheet_ink(l);
        printf("menu_look: survey %s: fill %.1f rim %.1f massX %.2f massY %.2f, %d sheets, steps",
               kLang[l], sv[l].fill, sv[l].rim, sv[l].massX, sv[l].massY, sv[l].sheets);
        for (int s = 0; s < sv[l].sheets; s++) {
            printf(" %d", sv[l].steps[s]);
            steps[nsteps++] = sv[l].steps[s];
        }
        printf("\n");
        printf("menu_look: survey %s: falloff across", kLang[l]);
        for (int d = 1; d <= 6; d++) {
            printf(" %.2f", sv[l].profX[d]);
        }
        printf(", down");
        for (int d = 1; d <= 6; d++) {
            printf(" %.2f", sv[l].profY[d]);
        }
        printf("\n");
        CHECK(sv[l].sheets >= 4, "%s: only %d sheets measured", kLang[l], sv[l].sheets);
        CHECK(fabs(sv[l].fill - kExpFill[l]) <= TOL_LEVEL, "%s: fill %.1f, expected %.1f", kLang[l],
              sv[l].fill, kExpFill[l]);
        CHECK(fabs(sv[l].rim - kExpRim[l]) <= TOL_LEVEL, "%s: rim %.1f, expected %.1f", kLang[l],
              sv[l].rim, kExpRim[l]);
        CHECK(fabs(sv[l].massX - kExpMassX[l]) <= TOL_MASS, "%s: massX %.2f, expected %.2f",
              kLang[l], sv[l].massX, kExpMassX[l]);
        CHECK(fabs(sv[l].massY - kExpMassY[l]) <= TOL_MASS, "%s: massY %.2f, expected %.2f",
              kLang[l], sv[l].massY, kExpMassY[l]);
        /* kSheetInk is the measurement, rounded; the fill is the palette's white */
        CHECK(ink->rimOn == 1, "%s: the light ink has its rim", kLang[l]);
        CHECK(fabs(sv[l].rim - (double)ink->rimLevel) <= 1.0, "%s: kSheetInk rim %d, measured %.1f",
              kLang[l], ink->rimLevel, sv[l].rim);
        CHECK(fabs(sv[l].fill - (double)ink->fillLevel) <= 3.0,
              "%s: kSheetInk fill %d, measured %.1f", kLang[l], ink->fillLevel, sv[l].fill);
        meanX += sv[l].massX / UI_LANG_PAL_COUNT;
        meanY += sv[l].massY / UI_LANG_PAL_COUNT;
    }
    /* German's rim is grey like the Romance languages', not the English black */
    CHECK(sv[UI_LANG_DE].rim > 40.0 && sv[UI_LANG_EN].rim < 40.0,
          "the German rim is grey (%.1f) and the English one dark (%.1f)", sv[UI_LANG_DE].rim,
          sv[UI_LANG_EN].rim);
    qsort(steps, (size_t)nsteps, sizeof(int), cmpInt);
    const int median = nsteps ? steps[nsteps / 2] : 0;
    printf("menu_look: survey: rim mass %.2f x %.2f, median %d steps (rim %d x %d, %d levels)\n",
           meanX, meanY, median, ICO_SHEET_RX, ICO_SHEET_RY, ICO_SHEET_LEVELS);
    /* the rim's reach and falloff: the English sheets' rim alpha at each
       distance (six across, four down; the survey measures six, the
       falloff past four field lines is in the noise of the rows below) */
    static const int kWx[7] = {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2, ICO_SHEET_WX_3,
                               ICO_SHEET_WX_4, ICO_SHEET_WX_5, ICO_SHEET_WX_6};
    static const int kWy[5] = {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2, ICO_SHEET_WY_3,
                               ICO_SHEET_WY_4};
    CHECK(ICO_SHEET_RX == 6 && ICO_SHEET_RY == 4, "the rim's reach %d x %d, the survey's 6 x 4",
          ICO_SHEET_RX, ICO_SHEET_RY);
    CHECK(kWx[0] == 1000 && kWy[0] == 1000, "the falloff is 1 at the texel itself");
    for (int d = 1; d <= 6; d++) {
        CHECK(fabs(sv[UI_LANG_EN].profX[d] - kWx[d] / 1000.0) <= TOL_FALLOFF,
              "ICO_SHEET_WX_%d %d, the English sheets' %.2f", d, kWx[d], sv[UI_LANG_EN].profX[d]);
    }
    for (int d = 1; d <= 4; d++) {
        CHECK(fabs(sv[UI_LANG_EN].profY[d] - kWy[d] / 1000.0) <= TOL_FALLOFF,
              "ICO_SHEET_WY_%d %d, the English sheets' %.2f", d, kWy[d], sv[UI_LANG_EN].profY[d]);
    }
    CHECK(median == EXP_STEPS, "the sheets' white has %d antialiasing steps, expected %d", median,
          EXP_STEPS);
    /* the levels are not the survey's five (shader_consts.h: eight matched the
       sheets better in testCompareItems) but never fewer */
    CHECK(ICO_SHEET_LEVELS >= median + 2, "ICO_SHEET_LEVELS %d, fewer than the survey's %d",
          ICO_SHEET_LEVELS, median + 2);
    /* the strips' margin covers the rim's reach and the bilinear texel */
    CHECK(UI_MENU_RIM_X == ICO_SHEET_RX + 1 && UI_MENU_RIM_Y == ICO_SHEET_RY + 1,
          "menu_font.h's rim margin %d x %d for a rim of %d x %d", UI_MENU_RIM_X, UI_MENU_RIM_Y,
          ICO_SHEET_RX, ICO_SHEET_RY);
}

/* ------------------------------------------------------------ the strip */

/* the style and the sprite colour (GS, 0x80 = 1) an ink draws with */
static void inkOf(int ink, int rim, int lang, RdSheetStyle *st, int *col)
{
    const UiSheetInk *k = ui_menu_item_ink(ink, rim, lang);
    st->rimOn = k->rimOn;
    st->rimLevel = k->rimLevel;
    st->fillLevel = k->fillLevel;
    st->dither = k->dither;
    st->rimWeight = k->rimWeight;
    st->scale = 1;
    *col = ink == UI_INK_DARK ? 0 : ink == UI_INK_GREY ? (128 * 151 + 127) / 255 : 128;
}

/* a sheet texel (grey, alpha in GS units) after the texture function
   (MODULATE with the sprite colour col, GS units): grey and alpha 0..255 */
static void modulate(uint8_t g, uint8_t a, int col, uint8_t *outGrey, uint8_t *outAlpha)
{
    const int gm = (g * col + 64) / 128;
    *outGrey = (uint8_t)(gm > 255 ? 255 : gm);
    const int a255 = a * 255 / 128;
    *outAlpha = (uint8_t)(a255 > 255 ? 255 : a255);
}

/* the reference rectangle: grey and alpha 0..255 after the texture function
   (MODULATE with the sprite colour) */
static int refRectOf(const Rect *r, const UiMenuTextItem *it, uint8_t *grey, uint8_t *alpha)
{
    uint8_t *cov = malloc((size_t)r->w * (size_t)r->h);
    if (!cov) {
        return -1;
    }
    if (ui__menu_strip_raster(it, r->lang, cov, r->w, r->h) != 0) {
        free(cov);
        return -1;
    }
    RdSheetStyle st;
    int col;
    inkOf(r->ink, it->rim[r->lang], r->lang, &st, &col);
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            uint8_t g, a;
            sheetref_texel(cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &g, &a);
            modulate(g, a, col, &grey[y * r->w + x], &alpha[y * r->w + x]);
        }
    }
    free(cov);
    return 0;
}

static int refRect(const Rect *r, uint8_t *grey, uint8_t *alpha)
{
    return refRectOf(r, &ui_menu_text_items[r->item], grey, alpha);
}

/* ------------------------------------------------------------- measures */

typedef struct Measures {
    float blur[2], plain[2];        /* per background (black, grey) */
    float ink;                      /* the ink amount, texels */
    float left, right, top, bottom; /* the ink box, texels (fractional) */
    float capTop, baseline;         /* the first line's, rows */
    int hasLine;                    /* a run of 4 rows or more (a colon's dots have none) */
    float shareRim, shareFill, shareEdge;
    float mx, my, sdx, sdy; /* the fill's centre and spread (standard deviation), texels */
    float rimMass;          /* the dark rim's opacity (texels of the rim class) per fill amount */
    int ok;
} Measures;

static const float kBg[2] = {0.0f, 128.0f};

/* the picture on a flat background, 0..255 */
static void composite(const uint8_t *grey, const uint8_t *alpha, int n, float bg, float *out)
{
    for (int i = 0; i < n; i++) {
        const float a = (float)alpha[i] / 255.0f;
        out[i] = (float)grey[i] * a + bg * (1.0f - a);
    }
}

static void blur3(const float *in, int w, int h, float *out)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float s = 0.0f;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
                    const int yy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
                    s += in[yy * w + xx];
                }
            }
            out[y * w + x] = s / 9.0f;
        }
    }
}

/* how much of a texel is letter, 0..1, from its picture on black: the fill is
   white, the rim and the halo dark grey */
static float fillness(float onBlack)
{
    const float f = (onBlack - 80.0f) / (200.0f - 80.0f);
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

/* the ink's measures of a picture (grey, alpha): ink amount, box, the first
   line's capital top and baseline (rows crossing a fraction of the line's
   peak row amount, linearly between rows), and the class shares */
static void inkMeasures(const uint8_t *grey, const uint8_t *alpha, int w, int h, Measures *m)
{
    float *amt = calloc((size_t)h, sizeof(float));
    float *col = calloc((size_t)w, sizeof(float));
    int rim = 0, fill = 0, edge = 0, any = 0;
    float total = 0.0f, rimA = 0.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            const float onBlack = (float)grey[i] * (float)alpha[i] / 255.0f;
            const float f = fillness(onBlack);
            amt[y] += f;
            col[x] += f;
            total += f;
            if (alpha[i] >= 13) {
                any++;
                if (onBlack >= 200.0f) {
                    fill++;
                } else if (onBlack > 80.0f) {
                    edge++;
                } else {
                    rim++;
                    rimA += (float)alpha[i] / 255.0f;
                }
            }
        }
    }
    m->ink = total;
    m->rimMass = total > 0.0f ? rimA / total : 0.0f;
    {
        double sx = 0, sxx = 0, sy = 0, syy = 0;
        for (int x = 0; x < w; x++) {
            sx += col[x] * (x + 0.5);
            sxx += col[x] * (x + 0.5) * (x + 0.5);
        }
        for (int y = 0; y < h; y++) {
            sy += amt[y] * (y + 0.5);
            syy += amt[y] * (y + 0.5) * (y + 0.5);
        }
        const double t = total > 0.0f ? total : 1.0;
        m->mx = (float)(sx / t);
        m->my = (float)(sy / t);
        m->sdx = (float)sqrt(fmax(sxx / t - (sx / t) * (sx / t), 0.0));
        m->sdy = (float)sqrt(fmax(syy / t - (sy / t) * (sy / t), 0.0));
    }
    m->shareRim = any ? (float)rim / (float)any : 0.0f;
    m->shareFill = any ? (float)fill / (float)any : 0.0f;
    m->shareEdge = any ? (float)edge / (float)any : 0.0f;
    /* the box: the first and last row (column) whose amount reaches half a
       texel, edges between texels */
    m->top = m->bottom = m->left = m->right = 0.0f;
    for (int pass = 0; pass < 2; pass++) {
        const float *v = pass ? col : amt;
        const int n = pass ? w : h;
        int a = -1, b = -1;
        for (int i = 0; i < n; i++) {
            if (v[i] >= 0.5f) {
                a = a < 0 ? i : a;
                b = i;
            }
        }
        const float lo = a >= 0 ? (float)a : 0.0f, hi = a >= 0 ? (float)(b + 1) : 0.0f;
        if (pass) {
            m->left = lo;
            m->right = hi;
        } else {
            m->top = lo;
            m->bottom = hi;
        }
    }
    /* the first line: from the first row with ink to the next empty row; a
       run of fewer than 4 rows before it (an accent standing apart, ESPAÑOL's
       tilde) is not the line, unless no run is longer (a colon's dots) */
    int y0 = -1, y1 = -1, f0 = -1, f1 = -1;
    for (int y = 0; y < h; y++) {
        if (amt[y] >= 0.5f) {
            if (y0 < 0) {
                y0 = y;
            }
            y1 = y;
        } else if (y0 >= 0) {
            if (y1 - y0 + 1 >= 4) {
                break;
            }
            if (f0 < 0) {
                f0 = y0;
                f1 = y1;
            }
            y0 = y1 = -1;
        }
    }
    m->hasLine = y0 >= 0;
    if (y0 < 0) {
        y0 = f0;
        y1 = f1;
    }
    m->capTop = m->baseline = 0.0f;
    if (y0 >= 0) {
        float peak = 0.0f;
        for (int y = y0; y <= y1; y++) {
            peak = amt[y] > peak ? amt[y] : peak;
        }
        /* the top: the row amount crosses 15 % of the peak (capitals and
           ascenders alone); the baseline: it falls under 35 % (descenders alone) */
        const float tTop = 0.15f * peak, tBase = 0.35f * peak;
        m->capTop = (float)y0;
        for (int y = y0; y <= y1; y++) {
            if (amt[y] >= tTop) {
                m->capTop = (float)y;
                if (y > y0 && amt[y] > amt[y - 1]) {
                    m->capTop = (float)y - (amt[y] - tTop) / (amt[y] - amt[y - 1]);
                }
                break;
            }
        }
        int yb = y0;
        for (int y = y0; y <= y1; y++) {
            if (amt[y] >= tBase) {
                yb = y;
            }
        }
        m->baseline = (float)yb + 1.0f;
        if (yb < y1 && amt[yb] > amt[yb + 1]) {
            m->baseline = (float)yb + 1.0f - 1.0f + (amt[yb] - tBase) / (amt[yb] - amt[yb + 1]);
        }
    }
    free(amt);
    free(col);
}

static void measure(const Rect *r, const uint8_t *rg, const uint8_t *ra, Measures *sheet,
                    Measures *ref)
{
    const int n = r->w * r->h;
    float *a = malloc((size_t)n * sizeof(float)), *b = malloc((size_t)n * sizeof(float));
    float *ab = malloc((size_t)n * sizeof(float)), *bb = malloc((size_t)n * sizeof(float));
    for (int k = 0; k < 2; k++) {
        composite(r->grey, r->alpha, n, kBg[k], a);
        composite(rg, ra, n, kBg[k], b);
        double d = 0.0, db;
        for (int i = 0; i < n; i++) {
            d += fabsf(a[i] - b[i]);
        }
        ref->plain[k] = (float)(d / n);
        blur3(a, r->w, r->h, ab);
        blur3(b, r->w, r->h, bb);
        db = 0.0;
        for (int i = 0; i < n; i++) {
            db += fabsf(ab[i] - bb[i]);
        }
        ref->blur[k] = (float)(db / n);
    }
    free(a);
    free(b);
    free(ab);
    free(bb);
    inkMeasures(r->grey, r->alpha, r->w, r->h, sheet);
    inkMeasures(rg, ra, r->w, r->h, ref);
}

/* ------------------------------------------------------------ geometry */

/* The ink box and the first line of the strips against the sheets', per
   language and sheet: medians of strip / sheet (the box's width and height,
   the first line's capital height: capital top to baseline), the capital
   top's offset (rows, strip less sheet) and the ink amount.  Single-line
   items with a line on both. ICO_MENU_LOOK_GEOM=1 prints every item. */
#define GEO_SHEETS 16
#define GEO_ITEMS 256

enum { G_W, G_H, G_CAP, G_TOP, G_INK, G_SX, G_SY, G_DX, G_DY, G_N };

typedef struct Geo {
    const char *sheet;
    int n;
    float v[G_N][GEO_ITEMS];
} Geo;

static Geo s_geo[UI_LANG_PAL_COUNT][GEO_SHEETS + 1]; /* [GEO_SHEETS]: the language's all */
static float s_geoMed[UI_LANG_PAL_COUNT][G_N];       /* the language's medians */

static int cmpFloat(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static float median(const float *v, int n)
{
    float t[GEO_ITEMS];
    if (n <= 0) {
        return 0.0f;
    }
    memcpy(t, v, sizeof(float) * (size_t)n);
    qsort(t, (size_t)n, sizeof(float), cmpFloat);
    return n & 1 ? t[n / 2] : 0.5f * (t[n / 2 - 1] + t[n / 2]);
}

static void geoAdd(const Rect *r, const Measures *ms, const Measures *mr)
{
    const float sw = ms->right - ms->left, sh = ms->bottom - ms->top;
    const float sc = ms->baseline - ms->capTop, rc = mr->baseline - mr->capTop;
    if (!ms->hasLine || !mr->hasLine || sw < 2.0f || sh < 2.0f || sc < 2.0f) {
        return;
    }
    const float v[G_N] = {(mr->right - mr->left) / sw,
                          (mr->bottom - mr->top) / sh,
                          rc / sc,
                          mr->capTop - ms->capTop,
                          mr->ink / ms->ink,
                          mr->sdx / ms->sdx,
                          mr->sdy / ms->sdy,
                          mr->mx - ms->mx,
                          mr->my - ms->my};
    const char *env = getenv("ICO_MENU_LOOK_GEOM");
    if (env && *env == '1') {
        const LtProperty *e = &texProperty[r->row];
        printf(
            "menu_look: geom %s row %d item %d %s tex %dx%d disp %dx%d em %.1f: cap %.2f / "
            "%.2f (x%.3f) width x%.3f height x%.3f top %+.2f ink x%.2f spread x%.3f y%.3f centre %+.2f %+.2f\n",
            kLang[r->lang], r->row, r->item, r->sheet, e->texW, e->texH, e->dispW, e->dispH,
            ui_menu_text_items[r->item].em[r->lang], rc, sc, v[G_CAP], v[G_W], v[G_H], v[G_TOP],
            v[G_INK], v[G_SX], v[G_SY], v[G_DX], v[G_DY]);
    }
    int si = 0;
    while (si < GEO_SHEETS && s_geo[r->lang][si].sheet && s_geo[r->lang][si].sheet != r->sheet) {
        si++;
    }
    Geo *gs[2] = {si < GEO_SHEETS ? &s_geo[r->lang][si] : NULL, &s_geo[r->lang][GEO_SHEETS]};
    for (int k = 0; k < 2; k++) {
        Geo *g = gs[k];
        if (!g || g->n >= GEO_ITEMS) {
            continue;
        }
        if (k == 0) {
            g->sheet = r->sheet;
        }
        for (int m = 0; m < G_N; m++) {
            g->v[m][g->n] = v[m];
        }
        g->n++;
    }
}

static void geoPrint(int lang)
{
    for (int si = 0; si <= GEO_SHEETS; si++) {
        const Geo *g = &s_geo[lang][si];
        if (g->n == 0) {
            continue;
        }
        const char *name = si == GEO_SHEETS ? "all" : g->sheet;
        const char *slash = strrchr(name, '/');
        float med[G_N];
        for (int m = 0; m < G_N; m++) {
            med[m] = median(g->v[m], g->n);
            if (si == GEO_SHEETS) {
                s_geoMed[lang][m] = med[m];
            }
        }
        printf("menu_look: geometry %s %-16s %3d items, strip / sheet: width %.3f height %.3f cap "
               "%.3f top %+.2f ink %.2f spread %.3f %.3f centre %+.2f %+.2f\n",
               kLang[lang], slash ? slash + 1 : name, g->n, med[G_W], med[G_H], med[G_CAP],
               med[G_TOP], med[G_INK], med[G_SX], med[G_SY], med[G_DX], med[G_DY]);
    }
}

/* the measures of every compared item, per language, for the limits'
   comments: blur and plain (the larger background), the worst box edge
   (single and several lines), the worse of capital top and baseline, the
   ink ratio, the worst share */
enum { S_BLUR, S_PLAIN, S_EDGE, S_LINE, S_INK, S_SHARE, S_N };

static float s_stat[UI_LANG_PAL_COUNT][S_N][GEO_ITEMS];
static int s_statMulti[UI_LANG_PAL_COUNT][GEO_ITEMS];

static void statPrint(int lang, int n)
{
    static const char *const kName[S_N] = {"blur", "plain", "edge", "line", "ink", "share"};
    printf("menu_look: limits %s:", kLang[lang]);
    for (int q = 0; q <= S_N; q++) {
        /* q == S_N: the edge of the items of several lines */
        const int k = q == S_N ? S_EDGE : q;
        float v[GEO_ITEMS], lo = 1e9f, hi = -1e9f;
        int m = 0;
        for (int i = 0; i < n; i++) {
            if (s_statMulti[lang][i] == 2 ||
                (k == S_EDGE && (s_statMulti[lang][i] == 1) != (q == S_N))) {
                continue; /* the figure 1 has its own limits */
            }
            v[m++] = s_stat[lang][k][i];
            lo = s_stat[lang][k][i] < lo ? s_stat[lang][k][i] : lo;
            hi = s_stat[lang][k][i] > hi ? s_stat[lang][k][i] : hi;
        }
        printf(" %s %.2f/%.2f", q == S_N ? "edge(multi)" : kName[q], median(v, m), m ? hi : 0.0f);
        if (k == S_INK) {
            printf(" (min %.2f)", lo);
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------ pictures */

static void writeDirs(const char *dir, int lang)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look", dir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/menu_look/%s", dir, kLang[lang]);
    ico_mkdir(path);
}

/* a w x h picture on the flat grey bg (0..255) as RGBA at scale s (nearest
   or bilinear) into dst (pitch dw): the picture's texels are at their
   centres */
static void blit(uint8_t *dst, int dw, int ox, int oy, const uint8_t *grey, const uint8_t *alpha,
                 int w, int h, int s, int bilinear, float bg)
{
    for (int y = 0; y < h * s; y++) {
        for (int x = 0; x < w * s; x++) {
            float v;
            if (!bilinear) {
                const int i = (y / s) * w + (x / s);
                const float a = (float)alpha[i] / 255.0f;
                v = (float)grey[i] * a + bg * (1.0f - a);
            } else {
                const float fx = ((float)x + 0.5f) / (float)s - 0.5f;
                const float fy = ((float)y + 0.5f) / (float)s - 0.5f;
                const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
                const float tx = fx - (float)x0, ty = fy - (float)y0;
                float g = 0.0f, a = 0.0f;
                for (int k = 0; k < 4; k++) {
                    const int xx = x0 + (k & 1), yy = y0 + (k >> 1);
                    const float wgt = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? ty : 1.0f - ty);
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
                        const float al = (float)alpha[yy * w + xx] / 255.0f;
                        g += wgt * (float)grey[yy * w + xx] * al;
                        a += wgt * al;
                    }
                }
                v = g + bg * (1.0f - a);
            }
            uint8_t *p = dst + ((size_t)(oy + y) * (size_t)dw + (size_t)(ox + x)) * 4;
            p[0] = p[1] = p[2] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            p[3] = 255;
        }
    }
}

static void writePair(const char *dir, const Rect *r, const uint8_t *rg, const uint8_t *ra)
{
    const int s = 4, gap = 8;
    const int W = 2 * r->w * s + gap, H = 2 * r->h * s + gap;
    uint8_t *img = malloc((size_t)W * (size_t)H * 4);
    if (!img) {
        return;
    }
    for (size_t i = 0; i < (size_t)W * (size_t)H; i++) {
        img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = 40;
        img[i * 4 + 3] = 255;
    }
    blit(img, W, 0, 0, r->grey, r->alpha, r->w, r->h, s, 0, 128.0f);
    blit(img, W, r->w * s + gap, 0, rg, ra, r->w, r->h, s, 0, 128.0f);
    blit(img, W, 0, r->h * s + gap, r->grey, r->alpha, r->w, r->h, s, 1, 128.0f);
    blit(img, W, r->w * s + gap, r->h * s + gap, rg, ra, r->w, r->h, s, 1, 128.0f);
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look/%s/%d.png", dir, kLang[r->lang], r->row);
    rd_write_png(path, img, (uint32_t)W, (uint32_t)H, (uint32_t)W * 4, 1);
    free(img);
}

/* ---------------------------------------------------------- the compare */

static void testCompareItems(const char *outDir)
{
    int total = 0, bad = 0, skipped = 0;
    double worstBlur = 0.0, worstPlain = 0.0;
    for (int lang = 0; lang < UI_LANG_PAL_COUNT; lang++) {
        writeDirs(outDir, lang);
        int langBad = 0, langN = 0, contactH = 0, contactW = 0;
        for (int k = 0; k < s_nrect[lang]; k++) {
            contactH += s_rect[lang][k].h + 2;
            contactW = s_rect[lang][k].w > contactW ? s_rect[lang][k].w : contactW;
        }
        const int CW = 2 * contactW + 8;
        uint8_t *contact = calloc((size_t)CW * (size_t)(contactH > 0 ? contactH : 1), 4);
        int cy = 0;
        for (int k = 0; k < s_nrect[lang]; k++) {
            const Rect *r = &s_rect[lang][k];
            const UiMenuTextItem *it = &ui_menu_text_items[r->item];
            uint8_t *rg = malloc((size_t)r->w * (size_t)r->h);
            uint8_t *ra = malloc((size_t)r->w * (size_t)r->h);
            Measures ms, mr;
            if (refRect(r, rg, ra) != 0) {
                CHECK(0, "%s item %d: the strip cannot be rasterised", kLang[lang], r->item);
                free(rg);
                free(ra);
                continue;
            }
            measure(r, rg, ra, &ms, &mr);
            writePair(outDir, r, rg, ra);
            if (contact) {
                blit(contact, CW, 0, cy, r->grey, r->alpha, r->w, r->h, 1, 0, 128.0f);
                blit(contact, CW, contactW + 8, cy, rg, ra, r->w, r->h, 1, 0, 128.0f);
                cy += r->h + 2;
            }
            free(rg);
            free(ra);
            if (ms.ink < MIN_INK_AMOUNT) {
                skipped++;
                continue; /* nothing of the item's words on this sheet */
            }
            total++;
            if (!strchr(ui_str_in((UiLang)lang, (UiStrId)it->str), '\n')) {
                geoAdd(r, &ms, &mr);
            }
            const char *rimEnv = getenv("ICO_MENU_LOOK_RIM");
            if (rimEnv && *rimEnv == '1') {
                /* the halo's opacity per fill: the sheet's, the strip's
                   with the full halo (what the table's rim was set from)
                   and as drawn */
                UiMenuTextItem full = *it;
                full.rim[lang] = UI_RIM_FULL;
                uint8_t *fg = malloc((size_t)r->w * (size_t)r->h);
                uint8_t *fa = malloc((size_t)r->w * (size_t)r->h);
                Measures mf;
                memset(&mf, 0, sizeof(mf));
                if (fg && fa && refRectOf(r, &full, fg, fa) == 0) {
                    inkMeasures(fg, fa, r->w, r->h, &mf);
                }
                free(fg);
                free(fa);
                printf("menu_look: rim %s row %d item %d %s ink %d: sheet %.3f full %.3f (%.2f) "
                       "drawn %.3f (rim %d)\n",
                       kLang[lang], r->row, r->item, r->sheet, r->ink, ms.rimMass, mf.rimMass,
                       mf.rimMass > 0.0f ? ms.rimMass / mf.rimMass : 0.0f, mr.rimMass,
                       it->rim[lang]);
            }
            langN++;
            const int multi = strchr(ui_str_in((UiLang)lang, (UiStrId)it->str), '\n') != NULL;
            /* the figure 1 (and 10): Arimo's 1 has a foot the sheets' has not */
            const int one = it->str == UI_STR_MT_DIGIT_1 || it->str == UI_STR_MT_DIGIT_10 ||
                            it->str == UI_STR_MT_PLAYERS_1;
            if (langN <= GEO_ITEMS) {
                const float b[4] = {fabsf(mr.left - ms.left), fabsf(mr.right - ms.right),
                                    fabsf(mr.top - ms.top), fabsf(mr.bottom - ms.bottom)};
                const float sh[3] = {fabsf(mr.shareRim - ms.shareRim),
                                     fabsf(mr.shareFill - ms.shareFill),
                                     fabsf(mr.shareEdge - ms.shareEdge)};
                const float line =
                    mr.hasLine && ms.hasLine
                        ? fmaxf(fabsf(mr.capTop - ms.capTop), fabsf(mr.baseline - ms.baseline))
                        : 0.0f;
                const float st[S_N] = {fmaxf(mr.blur[0], mr.blur[1]),
                                       fmaxf(mr.plain[0], mr.plain[1]),
                                       fmaxf(fmaxf(b[0], b[1]), fmaxf(b[2], b[3])),
                                       line,
                                       mr.ink / ms.ink,
                                       fmaxf(sh[0], fmaxf(sh[1], sh[2]))};
                for (int q = 0; q < S_N; q++) {
                    s_stat[lang][q][langN - 1] = st[q];
                }
                s_statMulti[lang][langN - 1] = multi ? 1 : one ? 2 : 0;
            }
            const float edge = one ? T_ONE_EDGE : multi ? T_EDGE_MULTI : T_EDGE;
            const float tBlur = one ? T_ONE_BLUR : T_BLUR, tPlain = one ? T_ONE_PLAIN : T_PLAIN;
            const float tLo = one ? T_ONE_AMOUNT_LO : T_AMOUNT_LO,
                        tHi = one ? T_ONE_AMOUNT_HI : T_AMOUNT_HI;
            char why[512];
            why[0] = '\0';
            size_t n = 0;
#define NOTE(...) n += (size_t)snprintf(why + n, sizeof(why) - n, __VA_ARGS__)
            for (int b = 0; b < 2; b++) {
                if (mr.blur[b] > tBlur) {
                    NOTE(" blur[%d] %.1f", b, mr.blur[b]);
                }
                if (mr.plain[b] > tPlain) {
                    NOTE(" plain[%d] %.1f", b, mr.plain[b]);
                }
                worstBlur = mr.blur[b] > worstBlur ? mr.blur[b] : worstBlur;
                worstPlain = mr.plain[b] > worstPlain ? mr.plain[b] : worstPlain;
            }
            const float lr = fabsf(mr.left - ms.left), rr = fabsf(mr.right - ms.right),
                        tr = fabsf(mr.top - ms.top), br = fabsf(mr.bottom - ms.bottom);
            if (lr > edge || rr > edge || tr > edge || br > edge) {
                NOTE(" box dL %.1f dR %.1f dT %.1f dB %.1f", mr.left - ms.left, mr.right - ms.right,
                     mr.top - ms.top, mr.bottom - ms.bottom);
            }
            if (mr.hasLine && ms.hasLine &&
                (fabsf(mr.capTop - ms.capTop) > T_LINE ||
                 fabsf(mr.baseline - ms.baseline) > T_LINE)) {
                NOTE(" cap %.1f base %.1f", mr.capTop - ms.capTop, mr.baseline - ms.baseline);
            }
            const float ratio = mr.ink / ms.ink;
            if (ratio < tLo || ratio > tHi) {
                NOTE(" ink x%.2f", ratio);
            }
            if (fabsf(mr.shareRim - ms.shareRim) > T_SHARE ||
                fabsf(mr.shareFill - ms.shareFill) > T_SHARE ||
                fabsf(mr.shareEdge - ms.shareEdge) > T_SHARE) {
                NOTE(" shares rim %.2f/%.2f fill %.2f/%.2f edge %.2f/%.2f", mr.shareRim,
                     ms.shareRim, mr.shareFill, ms.shareFill, mr.shareEdge, ms.shareEdge);
            }
#undef NOTE
            if (why[0]) {
                bad++;
                langBad++;
                printf("menu_look: %s row %d (item %d, %s):%s\n", kLang[lang], r->row, r->item,
                       r->sheet, why);
            }
        }
        if (contact) {
            char path[1100];
            snprintf(path, sizeof(path), "%s/menu_look/%s/contact.png", outDir, kLang[lang]);
            rd_write_png(path, contact, (uint32_t)CW, (uint32_t)(cy > 0 ? cy : 1), (uint32_t)CW * 4,
                         1);
            free(contact);
        }
        geoPrint(lang);
        statPrint(lang, langN < GEO_ITEMS ? langN : GEO_ITEMS);
        printf("menu_look: %s: %d items compared, %d outside the limits\n", kLang[lang], langN,
               langBad);
        CHECK(langN >= 60, "%s: only %d items compared", kLang[lang], langN);
    }
    printf("menu_look: %d items, %d outside the limits (blur <= %.1f, plain <= %.1f), %d without "
           "ink on the sheet; the worst blur %.1f, plain %.1f\n",
           total, bad, T_BLUR, T_PLAIN, skipped, worstBlur, worstPlain);
    CHECK(bad == 0, "%d items outside the limits", bad);
}

/* -------------------------------------------------------------- geofit */

/* ICO_MENU_LOOK_GEOFIT=1 (development only): per item and language, the
   item's width factor (UiMenuTextItem.wx), the em's factor and the
   anchor's x and y offsets that bring the strip's fill closest to the
   sheet's (the mean difference of the two fill maps after the 3 x 3 blur),
   by coordinate descent; =2 keeps the width.
   Prints each item and the medians per sheet. */

/* the fill map (fillness of the picture on black) blurred */
static void fillMap(const uint8_t *grey, const uint8_t *alpha, int w, int h, float *tmp, float *out)
{
    for (int i = 0; i < w * h; i++) {
        tmp[i] = fillness((float)grey[i] * (float)alpha[i] / 255.0f);
    }
    blur3(tmp, w, h, out);
}

typedef struct GeoFitCtx {
    const Rect *r;
    const float *sheet; /* the sheet's blurred fill map */
    uint8_t *g, *a;
    float *tmp, *map;
} GeoFitCtx;

static double geoErr(GeoFitCtx *c, const float *p)
{
    const Rect *r = c->r;
    UiMenuTextItem t = ui_menu_text_items[r->item];
    t.em[r->lang] *= p[1];
    t.x[r->lang] += p[2];
    t.y[r->lang] += p[3];
    t.track[r->lang] += p[4];
    t.wx[r->lang] *= p[0];
    if (refRectOf(r, &t, c->g, c->a) != 0) {
        return 1e9;
    }
    fillMap(c->g, c->a, r->w, r->h, c->tmp, c->map);
    double d = 0.0;
    for (int i = 0; i < r->w * r->h; i++) {
        d += fabsf(c->map[i] - c->sheet[i]);
    }
    return d / (r->w * r->h) * 255.0;
}

static int cmpD(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double medianD(double *v, int n)
{
    if (n <= 0) {
        return 0.0;
    }
    qsort(v, (size_t)n, sizeof(double), cmpD);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static void geoFit(int fixedWidth)
{
    const float w0 = ui__sheet_set_width(UI_SHEET_WIDTH);
    ui__sheet_set_width(w0);
    double tot0 = 0.0, tot = 0.0;
    int totN = 0;

    enum { NP = 5 };

    static double res[UI_LANG_PAL_COUNT][512][NP + 2];
    static int has[UI_LANG_PAL_COUNT][512];
    memset(has, 0, sizeof(has));
    for (int lang = 0; lang < UI_LANG_PAL_COUNT; lang++) {
        for (int k = 0; k < s_nrect[lang]; k++) {
            const Rect *r = &s_rect[lang][k];
            const int n = r->w * r->h;
            GeoFitCtx c;
            float *sheet = malloc(sizeof(float) * (size_t)n);
            c.r = r;
            c.sheet = sheet;
            c.g = malloc((size_t)n);
            c.a = malloc((size_t)n);
            c.tmp = malloc(sizeof(float) * (size_t)n);
            c.map = malloc(sizeof(float) * (size_t)n);
            fillMap(r->grey, r->alpha, r->w, r->h, c.tmp, sheet);
            double ink = 0.0;
            for (int i = 0; i < n; i++) {
                ink += c.tmp[i];
            }
            if (ink >= MIN_INK_AMOUNT) {
                /* several starts (the width, the spacing and the anchor):
                   the descent alone stops in a valley on a long word */
                static const float kStart[6][3] = {{1.0f, 0.0f, 0.0f},  {1.1f, 0.0f, 0.0f},
                                                   {1.0f, 0.75f, 0.0f}, {0.95f, 1.5f, 0.0f},
                                                   {1.0f, 0.0f, -4.0f}, {1.0f, 0.0f, 4.0f}};
                float best[NP];
                double bestE = 1e30;
                const float p0[NP] = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                const double e0 = geoErr(&c, p0);
                for (int s0 = 0; s0 < 6; s0++) {
                    float p[NP] = {kStart[s0][0], 1.0f, kStart[s0][2], 0.0f, kStart[s0][1]};
                    float st[NP] = {0.04f, 0.04f, 1.0f, 1.0f, 0.25f};
                    const float minSt[NP] = {0.005f, 0.005f, 0.125f, 0.125f, 0.03f};
                    double e = geoErr(&c, p);
                    for (int it = 0; it < 200; it++) {
                        int better = 0;
                        for (int q = fixedWidth ? 1 : 0; q < NP; q++) {
                            for (int sgn = -1; sgn <= 1; sgn += 2) {
                                float t[NP];
                                memcpy(t, p, sizeof(t));
                                t[q] += (float)sgn * st[q];
                                const double et = geoErr(&c, t);
                                if (et < e - 1e-6) {
                                    e = et;
                                    memcpy(p, t, sizeof(t));
                                    better = 1;
                                }
                            }
                        }
                        /* the em or the spacing up and the width down
                           together (and back): the glyphs keep their width */
                        for (int k = 0; k < 4; k++) {
                            const int sgn = k & 1 ? 1 : -1, q = k < 2 ? 1 : 4;
                            float t[NP];
                            memcpy(t, p, sizeof(t));
                            t[q] += (float)sgn * st[q];
                            t[0] -= (float)sgn * (q == 1 ? st[1] : st[4] * 0.1f);
                            if (fixedWidth) {
                                break;
                            }
                            const double et = geoErr(&c, t);
                            if (et < e - 1e-6) {
                                e = et;
                                memcpy(p, t, sizeof(t));
                                better = 1;
                            }
                        }
                        if (!better) {
                            int more = 0;
                            for (int q = 0; q < NP; q++) {
                                if (st[q] > minSt[q]) {
                                    st[q] *= 0.5f;
                                    more = 1;
                                }
                            }
                            if (!more) {
                                break;
                            }
                        }
                    }
                    if (e < bestE) {
                        bestE = e;
                        memcpy(best, p, sizeof(best));
                    }
                }
                float p[NP];
                memcpy(p, best, sizeof(p));
                const double e = bestE;
                printf("menu_look: geofit %s row %d item %d %s: width %.3f em x%.3f dx %+.2f dy "
                       "%+.2f track %+.2f err %.2f -> %.2f\n",
                       kLang[lang], r->row, r->item, r->sheet, p[0], p[1], p[2], p[3], p[4], e0, e);
                for (int q = 0; q < NP; q++) {
                    res[lang][r->item][q] = p[q];
                }
                tot0 += e0;
                tot += e;
                totN++;
                res[lang][r->item][NP] = e0;
                res[lang][r->item][NP + 1] = e;
                has[lang][r->item] = 1;
            }
            free(sheet);
            free(c.g);
            free(c.a);
            free(c.tmp);
            free(c.map);
        }
    }
    /* per item over the languages: the medians (the table's em and x are
       per item, y per language) */
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        double v[NP + 2][UI_LANG_PAL_COUNT];
        int m = 0;
        for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
            if (has[l][i]) {
                for (int q = 0; q < NP + 2; q++) {
                    v[q][m] = res[l][i][q];
                }
                m++;
            }
        }
        if (!m) {
            continue;
        }
        printf("menu_look: geofit item %d row %d: width %.3f em x%.3f (%.2f) dx %+.2f err %.2f -> "
               "%.2f, dy",
               i, rowOfItem(i), medianD(v[0], m), medianD(v[1], m),
               ui_menu_text_items[i].em[0] * medianD(v[1], m), medianD(v[2], m), medianD(v[NP], m),
               medianD(v[NP + 1], m));
        for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
            if (has[l][i]) {
                printf(" %s %+.2f", kLang[l], res[l][i][3]);
            }
        }
        printf("\n");
    }
    /* per sheet: the medians over its items and languages */
    for (int pass = 0; pass < 16; pass++) {
        const char *name = NULL;
        double v[NP + 2][UI_LANG_PAL_COUNT * 512];
        int m = 0;
        for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
            for (int k = 0; k < s_nrect[l]; k++) {
                const Rect *r = &s_rect[l][k];
                const char *base = strrchr(r->sheet, '/');
                base = base ? base + 1 : r->sheet;
                static const char *seen[16];
                int si = 0;
                while (si < 16 && seen[si] && strcmp(seen[si], base) != 0) {
                    si++;
                }
                if (si < 16 && !seen[si]) {
                    seen[si] = base;
                }
                if (si != pass || !has[l][r->item]) {
                    continue;
                }
                name = base;
                for (int q = 0; q < NP + 2; q++) {
                    v[q][m] = res[l][r->item][q];
                }
                m++;
            }
        }
        if (name) {
            printf("menu_look: geofit sheet %-16s %3d: width %.3f em x%.3f dx %+.2f dy %+.2f err "
                   "%.2f -> %.2f\n",
                   name, m, medianD(v[0], m), medianD(v[1], m), medianD(v[2], m), medianD(v[3], m),
                   medianD(v[NP], m), medianD(v[NP + 1], m));
        }
    }
    printf("menu_look: geofit total %d: err %.3f -> %.3f\n", totN, totN ? tot0 / totN : 0.0,
           totN ? tot / totN : 0.0);
    ui__sheet_set_width(w0);
}

/* ------------------------------------------------------------------ fit */

typedef struct FitItem {
    const Rect *r;
    uint8_t *cov, *rim; /* the strip and its rim (the weighted dilation) */
} FitItem;

/* the rim of every texel, once per item (sheet_ref.c's, the default look:
   the rim's reach and falloff are the survey's) */
static void maxFilter(const FitItem *f)
{
    const int w = f->r->w, h = f->r->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            f->rim[y * w + x] =
                (uint8_t)sheetref_rim_look(f->cov, (uint32_t)w, (uint32_t)h, x, y, NULL);
        }
    }
}

/* the fit's texel (x, y) of an item: the light ink's full rim at rim and
   fill grey, dithered, look's levels, from the rim maxFilter computed
   (sheet_ref.c sheetref_texel_cr): grey and alpha in GS units */
static void fitTexel(const FitItem *f, int x, int y, const SheetLook *look, unsigned rim,
                     unsigned fill, uint8_t *g, uint8_t *a)
{
    const RdSheetStyle st = {1, (uint8_t)rim, (uint8_t)fill, 1, 0, 1};
    const int i = y * f->r->w + x;
    sheetref_texel_cr(f->cov[i], f->rim[i], x, y, &st, look, g, a);
}

/* the blurred difference of one item for a style and level count, over both
   backgrounds */
static double fitScore(const FitItem *f, unsigned levels, unsigned rim, unsigned fill)
{
    const int w = f->r->w, h = f->r->h, n = w * h;
    uint8_t *g = malloc((size_t)n), *a = malloc((size_t)n);
    float *p = malloc((size_t)n * sizeof(float)), *q = malloc((size_t)n * sizeof(float)),
          *pb = malloc((size_t)n * sizeof(float));
    SheetLook look = *sheetref_default_look();
    look.levels = (int)levels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t a128;
            fitTexel(f, x, y, &look, rim, fill, &g[y * w + x], &a128);
            a[y * w + x] = (uint8_t)(a128 * 255u / 128u > 255u ? 255u : a128 * 255u / 128u);
        }
    }
    double sum = 0.0;
    for (int k = 0; k < 2; k++) {
        float *ref = malloc((size_t)n * sizeof(float));
        composite(f->r->grey, f->r->alpha, n, kBg[k], ref);
        composite(g, a, n, kBg[k], p);
        blur3(ref, w, h, q);
        blur3(p, w, h, pb);
        for (int i = 0; i < n; i++) {
            sum += fabsf(q[i] - pb[i]);
        }
        free(ref);
    }
    free(g);
    free(a);
    free(p);
    free(q);
    free(pb);
    return sum / (2.0 * n);
}

static void fit(void)
{
    /* the grid holds every language's current rim (24 56 61 62) and room
       past the best the first run found (rim 90, fill 235, both its edges) */
    static const unsigned kRims[] = {0, 16, 24, 32, 48, 56, 61, 62, 72, 90, 110, 130, 160};
    static const unsigned kFills[] = {205, 215, 225, 235, 245, 255};

    enum { MAXI = 160 };

    for (int lang = 0; lang < UI_LANG_PAL_COUNT; lang++) {
        FitItem fi[MAXI];
        int n = 0;
        for (int k = 0; k < s_nrect[lang] && n < MAXI; k++) {
            const Rect *r = &s_rect[lang][k];
            if (r->ink != UI_INK_LIGHT) {
                continue;
            }
            uint8_t *cov = malloc((size_t)r->w * (size_t)r->h);
            if (!cov ||
                ui__menu_strip_raster(&ui_menu_text_items[r->item], lang, cov, r->w, r->h)) {
                free(cov);
                continue;
            }
            fi[n].r = r;
            fi[n].cov = cov;
            fi[n].rim = malloc((size_t)r->w * (size_t)r->h);
            n++;
        }
        double best = 1e9, atCurrent = 1e9;
        unsigned bl = 0, br = 0, bf = 0;
        const UiSheetInk *cur = ui_menu_sheet_ink(lang);
        for (int i = 0; i < n; i++) {
            maxFilter(&fi[i]);
        }
        for (unsigned lv = 3; lv <= 9; lv++) {
            for (size_t ri = 0; ri < sizeof(kRims) / sizeof(kRims[0]); ri++) {
                for (size_t fl = 0; fl < sizeof(kFills) / sizeof(kFills[0]); fl++) {
                    double s = 0.0;
                    for (int i = 0; i < n; i++) {
                        s += fitScore(&fi[i], lv, kRims[ri], kFills[fl]);
                    }
                    s /= n ? n : 1;
                    if (s < best) {
                        best = s;
                        bl = lv;
                        br = kRims[ri];
                        bf = kFills[fl];
                    }
                    if (lv == (unsigned)ICO_SHEET_LEVELS && kRims[ri] == cur->rimLevel &&
                        kFills[fl] == cur->fillLevel) {
                        atCurrent = s;
                    }
                }
            }
        }
        printf("menu_look: fit %s (%d items): best blur %.2f at LEVELS %u rim %u fill %u;"
               " the constants (LEVELS %d rim %d fill %d): %.2f\n",
               kLang[lang], n, best, bl, br, bf, ICO_SHEET_LEVELS, cur->rimLevel, cur->fillLevel,
               atCurrent);
        for (int i = 0; i < n; i++) {
            free(fi[i].cov);
            free(fi[i].rim);
        }
    }
}

/* -------------------------------------------- the row-46 lettering, looks */

/* The publisher's plate at the foot of the title screen ("Sony Computer
   Entertainment Europe Presents", texProperty row 46; row 48 the copyright
   line) is the look the menus' outline is judged against: a thick dark
   outline that tapers out quickly.  ICO_MENU_LOOK_REF46=1 measures it
   (refProfile), ICO_MENU_LOOK_OUTLINE=1 draws the menus' words in candidate
   looks (sheet_ref.h SheetLook) beside it.  Development only: the pictures
   are for choosing the menu look, nothing is checked but that the rows
   resolve. */

#define REF_ROW_PLATE 46
#define REF_ROW_COPYRIGHT 48
#define PROF_D 8      /* the distance bins: 0 the ink, 1 .. 8 outside it */
#define PROF_AHIST 16 /* the alpha histograms' bins */
#define MAX_BG 4      /* the ICO_MENU_LOOK_BG greys */

static const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* the agreement of a bin's texels with a Bayer matrix's order: over the
   pairs of texels of the bin inside one matrix cell whose alphas differ,
   the share where the larger matrix entry has the larger alpha (the
   quantiser rounds up at a larger threshold); an ordered dither of one
   value agrees at every pair, a gradient or noise about half.  The 8x8
   matrix's entry over 4 is the 4x4 one's (common.hlsli gs_bayer8), so a
   4x4 dither agrees with the 8x8 order too: the fine pairs (8x8 entries
   that differ, the same 4x4 entry) tell them apart: under a 4x4 dither
   their alphas are equal or differ at random (noise near a level: about
   half agree), under an 8x8 one they agree (0.7 .. 1.0 on dithered
   near-constant fields in the scratch trial of this measure). */
typedef struct Agree {
    double best;      /* the best phase's agreement, 0 without pairs */
    int px, py;       /* its phase: the matrix entry at sheet texel (x, y) is the
                         matrix's at ((x + px) % N, (y + py) % N) */
    long pairs;       /* its pairs (alphas that differ) */
    double fine;      /* 8x8: the fine pairs' agreement, 0 without */
    double fineShare; /* 8x8: the share of the fine pairs whose alphas differ */
    long fineN;       /* 8x8: the fine pairs whose alphas differ */
} Agree;

typedef struct Cluster {
    int lo, hi;   /* alpha 0..255 */
    double mean;  /* count-weighted */
    double share; /* of the outline's partial texels */
} Cluster;

typedef struct Profile {
    int ok;
    int inkGrey; /* the fill rule's grey */
    long ink;    /* ink texels */
    /* [axis 0 across (the row), 1 down (the column)][distance] */
    double alpha[2][PROF_D + 1]; /* mean alpha 0..1 */
    double dark[2][PROF_D + 1];  /* alpha-weighted mean darkness, 255 - grey */
    double var[2][PROF_D + 1];   /* the alpha's variance (0..1 units) */
    long count[2][PROF_D + 1];
    long ahist[2][PROF_D + 1][PROF_AHIST];
    Agree a4[2][PROF_D + 1], a8[2][PROF_D + 1];
    Agree pool4, pool8; /* over the bins 1 .. 6 across and 1 .. 4 down */
    Cluster steps[16];
    int nclusters, nsteps; /* the clusters, those not full (< 0.93) */
    double outlineGrey;    /* alpha-weighted mean grey of the outline within 2 texels */
    double spread;         /* the largest |r - g| or |g - b| of the rectangle (a grey sheet: 0) */
    int wx[ICO_SHEET_RX + 1], wy[ICO_SHEET_RY + 1]; /* the normalised falloff, per mille */
    int matrix;                                     /* 0 none, 4, 8: the dither it shows */
} Profile;

/* the distance from texel (x, y) to the nearest ink texel of its row (axis
   0) or column (axis 1), the gaps between letters counted; PROF_D + 1 past
   the bins or without ink in the line */
static int inkDistance(const uint8_t *ink, int w, int h, int x, int y, int axis)
{
    for (int d = 0; d <= PROF_D; d++) {
        for (int s = -1; s <= 1; s += 2) {
            const int xx = axis == 0 ? x + s * d : x, yy = axis == 1 ? y + s * d : y;
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && ink[yy * w + xx]) {
                return d;
            }
        }
    }
    return PROF_D + 1;
}

/* the Bayer agreement of the texels labelled bin0 .. bin1 (label[i], -1
   none; a pair within one label) at matrix size n (4, 8) and phase (px,
   py), in the sheet's coordinates (the rectangle's corner r->u, r->v):
   pairs with different alphas and entries, and of them the agreeing; the
   fine pairs (n 8: the same 4x4 entry), those whose alphas differ, and of
   them the agreeing */
typedef struct AgreeCount {
    long agree, pairs, fine, fineDiff, fineAgree;
} AgreeCount;

static void agreeAt(const Rect *r, const int *label, int bin0, int bin1, int n, int px, int py,
                    AgreeCount *o)
{
    const int w = r->w, h = r->h;
    memset(o, 0, sizeof(*o));
    int ix[64];
    /* the cells of the phase: sheet texel (sx, sy) is in cell
       ((sx + px) / n, (sy + py) / n) */
    for (int cy = (r->v + py) / n; cy <= (r->v + h - 1 + py) / n; cy++) {
        for (int cx = (r->u + px) / n; cx <= (r->u + w - 1 + px) / n; cx++) {
            int k = 0;
            for (int j = 0; j < n; j++) {
                for (int i = 0; i < n; i++) {
                    const int x = cx * n + i - px - r->u, y = cy * n + j - py - r->v;
                    if (x < 0 || y < 0 || x >= w || y >= h) {
                        continue;
                    }
                    const int l = label[y * w + x];
                    if (l >= bin0 && l <= bin1) {
                        ix[k++] = y * w + x;
                    }
                }
            }
            for (int a = 0; a < k; a++) {
                for (int b = a + 1; b < k; b++) {
                    const int ia = ix[a], ib = ix[b];
                    if (label[ia] != label[ib]) {
                        continue;
                    }
                    const unsigned xa = (unsigned)(r->u + ia % w + px),
                                   ya = (unsigned)(r->v + ia / w + py);
                    const unsigned xb = (unsigned)(r->u + ib % w + px),
                                   yb = (unsigned)(r->v + ib / w + py);
                    const unsigned ma = n == 4 ? kBayer4[ya & 3][xa & 3] : sheetref_bayer8(xa, ya);
                    const unsigned mb = n == 4 ? kBayer4[yb & 3][xb & 3] : sheetref_bayer8(xb, yb);
                    if (ma == mb) {
                        continue;
                    }
                    const int differ = r->alpha[ia] != r->alpha[ib];
                    const int agree = (ma > mb) == (r->alpha[ia] > r->alpha[ib]);
                    if (n == 8 && ma >> 2 == mb >> 2) {
                        o->fine++;
                        o->fineDiff += differ;
                        o->fineAgree += differ && agree;
                    }
                    if (differ) {
                        o->pairs++;
                        o->agree += agree;
                    }
                }
            }
        }
    }
}

/* the best phase of matrix size n over the bins bin0 .. bin1 of label: the
   largest agreement plus (8x8) the fine pairs' */
static Agree agreeBest(const Rect *r, const int *label, int bin0, int bin1, int n)
{
    Agree best = {0.0, 0, 0, 0, 0.0, 0.0, 0};
    double bestScore = -1.0;
    for (int py = 0; py < n; py++) {
        for (int px = 0; px < n; px++) {
            AgreeCount c;
            agreeAt(r, label, bin0, bin1, n, px, py, &c);
            if (!c.pairs) {
                continue;
            }
            const double a = (double)c.agree / (double)c.pairs;
            const double f = c.fineDiff ? (double)c.fineAgree / (double)c.fineDiff : 0.0;
            if (a + f > bestScore) {
                bestScore = a + f;
                best.best = a;
                best.px = px;
                best.py = py;
                best.pairs = c.pairs;
                best.fine = f;
                best.fineShare = c.fine ? (double)c.fineDiff / (double)c.fine : 0.0;
                best.fineN = c.fineDiff;
            }
        }
    }
    return best;
}

/* the sheet's colour spread over the rectangle (the measures read the red
   channel, rectOfRow) */
static double colourSpread(const Rect *r)
{
    char name[64];
    const LtProperty *e = &texProperty[r->row];
    ms_sheet_name(texFile[e->texFileNo].path, r->lang, name, sizeof(name));
    const MsSheet *s = ms_sheet_for(s_vfs, name);
    int m = 0;
    for (int y = 0; s && y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            const uint8_t *p =
                s->rgba + ((size_t)(r->v + y) * (size_t)s->w + (size_t)(r->u + x)) * 4;
            if (p[3] == 0) {
                continue;
            }
            const int d0 = abs((int)p[0] - (int)p[1]), d1 = abs((int)p[1] - (int)p[2]);
            m = d0 > m ? d0 : m;
            m = d1 > m ? d1 : m;
        }
    }
    return m;
}

/* The profile of a rectangle's outline (the definitions are survey()'s
   where they overlap): ink = the fill texels (grey >= inkGrey, alpha >=
   0.7); every other texel binned by its distance to the nearest ink of its
   row (across) and of its column (down), 1 .. PROF_D; bin 0 the ink.  Per
   bin the mean alpha, the alpha-weighted mean darkness, the count, the
   alpha histogram, the variance and the Bayer agreement (4x4 at 16 phases,
   8x8 at 64, the best); the outline's alpha steps (its texels within the
   bins with alpha 13 .. 254: the values taken by at least 1 % of them,
   merged when within 16; the steps the clusters below 0.93); the outline's
   grey; the falloff normalised to the ink's alpha (WX' d 0 .. 6, WY' 0 ..
   4, per mille) and the dither it shows (> 0.75 pooled agreement). */
static void refProfile(const Rect *r, int inkGrey, Profile *p)
{
    memset(p, 0, sizeof(*p));
    p->inkGrey = inkGrey;
    const int w = r->w, h = r->h, n = w * h;
    uint8_t *ink = calloc((size_t)n, 1);
    int *lab[2] = {malloc((size_t)n * sizeof(int)), malloc((size_t)n * sizeof(int))};
    if (!ink || !lab[0] || !lab[1]) {
        free(ink);
        free(lab[0]);
        free(lab[1]);
        return;
    }
    for (int i = 0; i < n; i++) {
        ink[i] = r->grey[i] >= inkGrey && r->alpha[i] * 10 >= 7 * 255;
        p->ink += ink[i];
    }
    double sum[2][PROF_D + 1], sum2[2][PROF_D + 1], dN[2][PROF_D + 1], dD[2][PROF_D + 1];
    memset(sum, 0, sizeof(sum));
    memset(sum2, 0, sizeof(sum2));
    memset(dN, 0, sizeof(dN));
    memset(dD, 0, sizeof(dD));
    static long hist[256];
    memset(hist, 0, sizeof(hist));
    long tot = 0;
    double gN = 0.0, gD = 0.0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            int dist[2];
            for (int ax = 0; ax < 2; ax++) {
                dist[ax] = ink[i] ? 0 : inkDistance(ink, w, h, x, y, ax);
                lab[ax][i] = dist[ax] <= PROF_D ? dist[ax] : -1;
                if (dist[ax] > PROF_D) {
                    continue;
                }
                const double a = r->alpha[i] / 255.0;
                const int d = dist[ax];
                sum[ax][d] += a;
                sum2[ax][d] += a * a;
                dN[ax][d] += a * (255.0 - r->grey[i]);
                dD[ax][d] += a;
                p->count[ax][d]++;
                p->ahist[ax][d][r->alpha[i] * PROF_AHIST / 256]++;
            }
            if (ink[i] || (dist[0] > PROF_D && dist[1] > PROF_D)) {
                continue;
            }
            if (r->alpha[i] >= 13 && r->alpha[i] < 255) {
                hist[r->alpha[i]]++;
                tot++;
            }
            if ((dist[0] <= 2 || dist[1] <= 2) && r->alpha[i] >= 128) {
                gN += (double)r->grey[i] * r->alpha[i];
                gD += r->alpha[i];
            }
        }
    }
    for (int ax = 0; ax < 2; ax++) {
        for (int d = 0; d <= PROF_D; d++) {
            const double c = (double)p->count[ax][d];
            p->alpha[ax][d] = c > 0 ? sum[ax][d] / c : 0.0;
            p->var[ax][d] = c > 0 ? sum2[ax][d] / c - p->alpha[ax][d] * p->alpha[ax][d] : 0.0;
            p->dark[ax][d] = dD[ax][d] > 0 ? dN[ax][d] / dD[ax][d] : 0.0;
            if (d > 0) {
                p->a4[ax][d] = agreeBest(r, lab[ax], d, d, 4);
                p->a8[ax][d] = agreeBest(r, lab[ax], d, d, 8);
            }
        }
    }
    /* pooled: the falloff tables' reach, across then down (each its own
       best phase), weighted by the pairs */
    for (int m = 0; m < 2; m++) {
        const int size = m ? 8 : 4;
        const Agree ax = agreeBest(r, lab[0], 1, ICO_SHEET_RX, size);
        const Agree ay = agreeBest(r, lab[1], 1, ICO_SHEET_RY, size);
        Agree *o = m ? &p->pool8 : &p->pool4;
        const double px = (double)ax.pairs, py = (double)ay.pairs;
        *o = ax.pairs >= ay.pairs ? ax : ay;
        o->pairs = ax.pairs + ay.pairs;
        o->best = o->pairs ? (ax.best * px + ay.best * py) / (px + py) : 0.0;
        o->fine = o->pairs ? (ax.fine * px + ay.fine * py) / (px + py) : 0.0;
        o->fineShare = o->pairs ? (ax.fineShare * px + ay.fineShare * py) / (px + py) : 0.0;
        o->fineN = ax.fineN + ay.fineN;
    }
    /* dithered: the order agrees (> 0.75); 8x8 when its fine pairs agree
       too (> 0.65 over 50 pairs or more), else 4x4 */
    if (p->pool8.best > 0.75 && p->pool8.fine > 0.65 && p->pool8.fineN >= 50) {
        p->matrix = 8;
    } else if (p->pool4.best > 0.75) {
        p->matrix = 4;
    } else {
        p->matrix = 0;
    }
    /* the steps: survey()'s clustering */
    {
        int open = 0, last = -1000;
        long cnt = 0;
        double wsum = 0.0;
        Cluster cur = {0, 0, 0.0, 0.0};
        for (int a = 0; a <= 256; a++) {
            const int take = a < 256 && hist[a] > 0 && hist[a] * 100 >= tot;
            if (take && open && a - last <= 16) {
                cur.hi = a;
                cnt += hist[a];
                wsum += (double)a * hist[a];
                last = a;
                continue;
            }
            if ((take || a == 256) && open) {
                cur.mean = cnt ? wsum / cnt : 0.0;
                cur.share = tot ? (double)cnt / tot : 0.0;
                if (p->nclusters < 16) {
                    p->steps[p->nclusters++] = cur;
                }
                p->nsteps += cur.hi * 100 < 93 * 255;
                open = 0;
            }
            if (take) {
                open = 1;
                cur.lo = cur.hi = a;
                cnt = hist[a];
                wsum = (double)a * hist[a];
                last = a;
            }
        }
    }
    p->outlineGrey = gD > 0 ? gN / gD : 0.0;
    p->spread = colourSpread(r);
    const double a0x = p->alpha[0][0] > 0 ? p->alpha[0][0] : 1.0;
    const double a0y = p->alpha[1][0] > 0 ? p->alpha[1][0] : 1.0;
    for (int d = 0; d <= ICO_SHEET_RX; d++) {
        const double v = d == 0 ? 1000.0 : p->alpha[0][d] / a0x * 1000.0;
        p->wx[d] = (int)(v < 0 ? 0 : v > 1000 ? 1000 : v + 0.5);
    }
    for (int d = 0; d <= ICO_SHEET_RY; d++) {
        const double v = d == 0 ? 1000.0 : p->alpha[1][d] / a0y * 1000.0;
        p->wy[d] = (int)(v < 0 ? 0 : v > 1000 ? 1000 : v + 0.5);
    }
    p->ok = p->ink > 0;
    free(ink);
    free(lab[0]);
    free(lab[1]);
}

/* the sheet's base name, for the printouts */
static const char *baseName(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* agreement@phase/pairs, the 8x8 one with its fine pairs' agreement, count
   and share (f) */
static void agreePrint(const Agree *a4, const Agree *a8)
{
    printf(" %.2f@%d,%d/%ld %.2f@%d,%d/%ld f%.2f/%ld/%.2f", a4->best, a4->px, a4->py, a4->pairs,
           a8->best, a8->px, a8->py, a8->pairs, a8->fine, a8->fineN, a8->fineShare);
}

static void profilePrint(const Rect *r, const Profile *p)
{
    const char *sheet = baseName(r->sheet);
    printf("menu_look: ref46 row %d %s %s: ink rule grey >= %d alpha >= 0.7: %ld ink texels of "
           "%d; colour spread %.0f%s\n",
           r->row, kLang[r->lang], sheet, p->inkGrey, p->ink, r->w * r->h, p->spread,
           p->spread > 8 ? " (a coloured sheet: the measures read the red channel)" : "");
    printf("menu_look: ref46 row %d %s %s: row lang sheet d: alphaX darkX nX varX agree4X agree8X "
           "| alphaY darkY nY varY agree4Y agree8Y (agreement@phase/pairs, f the 8x8 fine "
           "pairs' agreement / count / share)\n",
           r->row, kLang[r->lang], sheet);
    for (int d = 0; d <= PROF_D; d++) {
        printf("menu_look: ref46 %d %s %s %d: %.3f %5.1f %5ld %.4f", r->row, kLang[r->lang], sheet,
               d, p->alpha[0][d], p->dark[0][d], p->count[0][d], p->var[0][d]);
        if (d > 0) {
            agreePrint(&p->a4[0][d], &p->a8[0][d]);
        }
        printf(" | %.3f %5.1f %5ld %.4f", p->alpha[1][d], p->dark[1][d], p->count[1][d],
               p->var[1][d]);
        if (d > 0) {
            agreePrint(&p->a4[1][d], &p->a8[1][d]);
        }
        printf("\n");
    }
    for (int ax = 0; ax < 2; ax++) {
        for (int d = 0; d <= PROF_D; d++) {
            printf("menu_look: ref46 %d %s %s alpha histogram %s d %d (16ths):", r->row,
                   kLang[r->lang], sheet, ax ? "down" : "across", d);
            for (int b = 0; b < PROF_AHIST; b++) {
                printf(" %ld", p->ahist[ax][d][b]);
            }
            printf("\n");
        }
    }
    printf("menu_look: ref46 %d %s %s: %d alpha steps (%d clusters):", r->row, kLang[r->lang],
           sheet, p->nsteps, p->nclusters);
    for (int c = 0; c < p->nclusters; c++) {
        printf(" %d..%d mean %.1f share %.2f;", p->steps[c].lo, p->steps[c].hi, p->steps[c].mean,
               p->steps[c].share);
    }
    printf("\n");
    printf("menu_look: ref46 %d %s %s: outline grey %.1f; pooled agreement 4x4 / 8x8 (fine "
           "pairs' agreement / count / share):",
           r->row, kLang[r->lang], sheet, p->outlineGrey);
    agreePrint(&p->pool4, &p->pool8);
    printf(": %s\n", p->matrix == 8   ? "dithered, 8x8"
                     : p->matrix == 4 ? "dithered, 4x4"
                                      : "a gradient");
    printf("menu_look: ref46 %d %s %s: WX'", r->row, kLang[r->lang], sheet);
    for (int d = 0; d <= ICO_SHEET_RX; d++) {
        printf(" %d", p->wx[d]);
    }
    printf("; WY'");
    for (int d = 0; d <= ICO_SHEET_RY; d++) {
        printf(" %d", p->wy[d]);
    }
    printf("; levels %d\n", p->nsteps + 2);
}

/* the grey/alpha histogram of a rectangle: grey in 8 bins (rows), alpha in 8
   (columns), the texels with alpha > 0 */
static void histPrint(const Rect *r)
{
    long t[8][8];
    memset(t, 0, sizeof(t));
    for (int i = 0; i < r->w * r->h; i++) {
        if (r->alpha[i]) {
            t[r->grey[i] / 32][r->alpha[i] / 32]++;
        }
    }
    printf("menu_look: ref46 row %d %s %s: grey (rows, 32s) x alpha (columns, 32s), alpha > 0:\n",
           r->row, kLang[r->lang], baseName(r->sheet));
    for (int g = 0; g < 8; g++) {
        printf("menu_look: ref46   grey %3d..%3d:", g * 32, g * 32 + 31);
        for (int a = 0; a < 8; a++) {
            printf(" %6ld", t[g][a]);
        }
        printf("\n");
    }
}

/* ICO_MENU_LOOK_BG=g1,g2..: the backdrop greys (the title's castle under
   the plate), at most MAX_BG; the count */
static int parseBg(float *bg)
{
    const char *e = getenv("ICO_MENU_LOOK_BG");
    int n = 0;
    while (e && *e && n < MAX_BG) {
        char *end;
        const long v = strtol(e, &end, 10);
        if (end == e) {
            break;
        }
        bg[n++] = (float)(v < 0 ? 0 : v > 255 ? 255 : v);
        e = *end == ',' ? end + 1 : end;
        if (*end != ',') {
            break;
        }
    }
    return n;
}

/* ICO_MENU_LOOK_INK=<grey>: the fill rule's grey (200, survey()'s) */
static int inkGreyOf(void)
{
    const char *e = getenv("ICO_MENU_LOOK_INK");
    const int g = e ? atoi(e) : 200;
    return g < 1 ? 1 : g > 255 ? 255 : g;
}

/* a picture's backgrounds: black, the backdrop greys, 128 */
static int backgrounds(float *out)
{
    float bg[MAX_BG];
    const int n = parseBg(bg);
    out[0] = 0.0f;
    for (int i = 0; i < n; i++) {
        out[1 + i] = bg[i];
    }
    out[1 + n] = 128.0f;
    return n + 2;
}

/* an RGB picture as a compressed PNG (the pictures here are large and
   mostly flat; rd_write_png stores) */
static void writePngZ(const char *path, const uint8_t *rgba, int w, int h)
{
    uint8_t *rgb = malloc((size_t)w * (size_t)h * 3);
    if (!rgb) {
        return;
    }
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
        memcpy(rgb + i * 3, rgba + i * 4, 3);
    }
    size_t len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(rgb, w, h, 3, &len, 6, 0);
    free(rgb);
    FILE *f = png ? fopen(path, "wb") : NULL;
    if (f) {
        fwrite(png, 1, len, f);
        fclose(f);
        printf("menu_look: wrote %s (%dx%d)\n", path, w, h);
    } else {
        printf("menu_look: %s not written\n", path);
    }
    mz_free(png);
}

/* a w x h RGBA canvas filled with the frame colour (dark blue grey: apart
   from every background) */
static uint8_t *canvas(int w, int h)
{
    uint8_t *img = malloc((size_t)w * (size_t)h * 4);
    for (size_t i = 0; img && i < (size_t)w * (size_t)h; i++) {
        img[i * 4] = 24;
        img[i * 4 + 1] = 32;
        img[i * 4 + 2] = 56;
        img[i * 4 + 3] = 255;
    }
    return img;
}

static void ref46Dirs(const char *dir, const char *sub)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look", dir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/menu_look/%s", dir, sub);
    ico_mkdir(path);
}

/* ref46/<row>-<lang>.png: the crop on each background (columns), x1 above,
   x4 nearest below */
static void ref46Picture(const char *dir, const Rect *r)
{
    float bg[MAX_BG + 2];
    const int nb = backgrounds(bg), gap = 8;
    const int W = gap + nb * (r->w * 4 + gap), H = gap + r->h + gap + r->h * 4 + gap;
    uint8_t *img = canvas(W, H);
    if (!img) {
        return;
    }
    for (int b = 0; b < nb; b++) {
        const int ox = gap + b * (r->w * 4 + gap);
        blit(img, W, ox, gap, r->grey, r->alpha, r->w, r->h, 1, 0, bg[b]);
        blit(img, W, ox, gap + r->h + gap, r->grey, r->alpha, r->w, r->h, 4, 0, bg[b]);
    }
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look/ref46/%d-%s.png", dir, r->row, kLang[r->lang]);
    writePngZ(path, img, W, H);
    free(img);
}

/* ICO_MENU_LOOK_REF46=1: rows 46 and 48 on the five languages' sheets, each
   sheet once */
static void ref46(const char *dir)
{
    const int inkGrey = inkGreyOf();
    ref46Dirs(dir, "ref46");
    static const int kRows[2] = {REF_ROW_PLATE, REF_ROW_COPYRIGHT};
    for (int k = 0; k < 2; k++) {
        const char *seen[UI_LANG_PAL_COUNT];
        int nseen = 0;
        for (int lang = 0; lang < UI_LANG_PAL_COUNT; lang++) {
            Rect r;
            if (rectOfRow(kRows[k], lang, &r) != 0) {
                CHECK(0, "row %d %s: no sheet on the disc", kRows[k], kLang[lang]);
                continue;
            }
            int dup = -1;
            for (int s = 0; s < nseen; s++) {
                dup = seen[s] == r.sheet ? s : dup;
            }
            printf("menu_look: ref46 row %d %s: %s -> %s, texels %d,%d %dx%d%s\n", kRows[k],
                   kLang[lang], texFile[texProperty[kRows[k]].texFileNo].path, r.sheet, r.u, r.v,
                   r.w, r.h, dup >= 0 ? " (measured above)" : "");
            if (dup < 0) {
                seen[nseen++] = r.sheet;
                histPrint(&r);
                Profile p;
                refProfile(&r, inkGrey, &p);
                CHECK(p.ok,
                      "row %d %s: no ink texel (grey >= %d, alpha >= 0.7): lower "
                      "ICO_MENU_LOOK_INK",
                      kRows[k], kLang[lang], inkGrey);
                profilePrint(&r, &p);
                ref46Picture(dir, &r);
            }
            free(r.grey);
            free(r.alpha);
        }
    }
}

/* ------------------------------------------------- the outline candidates */

typedef struct Cand {
    const char *name; /* the picture's name */
    const char *what;
    SheetLook look;
} Cand;

/* The candidates (per mille; a sheet texel is 1 pixel wide and 2 tall at
   1x, so the tables down fall about twice as fast per texel).  Every one
   keeps the reach 6 x 4 as its tables' lengths, zeros past the taper: the
   strips' margins, the pages' gaps and the shader's unrolled loops derive
   from ICO_SHEET_RX / RY.  C4 is the measured plate, filled in the run. */
static Cand s_cand[] = {
    {"C1-L8",
     "plateau 1, fall 2",
     {6, 4, {1000, 1000, 500, 150, 0, 0, 0}, {1000, 1000, 300, 0, 0}, 8, 4, 0}},
    {"C1-L3",
     "plateau 1, fall 2, 3 levels",
     {6, 4, {1000, 1000, 500, 150, 0, 0, 0}, {1000, 1000, 300, 0, 0}, 3, 4, 0}},
    {"C2-L8",
     "plateau 2, fall 2",
     {6, 4, {1000, 1000, 1000, 500, 150, 0, 0}, {1000, 1000, 600, 200, 0}, 8, 4, 0}},
    {"C2-L3",
     "plateau 2, fall 2, 3 levels",
     {6, 4, {1000, 1000, 1000, 500, 150, 0, 0}, {1000, 1000, 600, 200, 0}, 3, 4, 0}},
    {"C3-L8",
     "plateau 1, fall 3, the 8x8 matrix",
     {6, 4, {1000, 1000, 700, 400, 150, 0, 0}, {1000, 900, 450, 150, 0}, 8, 8, 0}},
    {"C4",
     "the measured row-46 profile (WX', WY', its steps + 2 levels, its matrix)",
     {6, 4, {1000, 0, 0, 0, 0, 0, 0}, {1000, 0, 0, 0, 0}, 0, 0, 0}},
    {"C5",
     "today's constants (the control)",
     {ICO_SHEET_RX,
      ICO_SHEET_RY,
      {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2, ICO_SHEET_WX_3, ICO_SHEET_WX_4,
       ICO_SHEET_WX_5, ICO_SHEET_WX_6},
      {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2, ICO_SHEET_WY_3, ICO_SHEET_WY_4},
      ICO_SHEET_LEVELS,
      4}},
    {"C6",
     "a hard outline",
     {6, 4, {1000, 1000, 1000, 0, 0, 0, 0}, {1000, 1000, 0, 0, 0}, 8, 4, 0}},
};
#define CAND_COUNT (int)(sizeof(s_cand) / sizeof(s_cand[0]))
#define CAND_C4 5

/* candidate `name`'s look into out; 0, or -1 for no such candidate */
static int outlineLook(SheetLook *out, const char *name)
{
    for (int c = 0; c < CAND_COUNT; c++) {
        if (strcmp(s_cand[c].name, name) == 0) {
            *out = s_cand[c].look;
            return 0;
        }
    }
    return -1;
}

/* the words: the game's items (their strips, ui__menu_strip_raster) and
   the port's (ui__menu_text_raster), trimmed to the ink plus the rim's
   reach after the drawing (the dither keeps the strip's phase) */
typedef struct Word {
    char label[48];
    int lang;
    uint8_t *cov;
    int w, h;           /* the strip */
    int x0, y0, tw, th; /* the trimmed rectangle */
} Word;

#define WORD_MAX 12

/* the trimmed rectangle: the coverage's box grown by the margin
   (UI_MENU_RIM_X / Y), within the strip */
static void wordTrim(Word *wd)
{
    int x0 = wd->w, y0 = wd->h, x1 = -1, y1 = -1;
    for (int y = 0; y < wd->h; y++) {
        for (int x = 0; x < wd->w; x++) {
            if (wd->cov[y * wd->w + x]) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 < 0) {
        x0 = y0 = 0;
        x1 = wd->w - 1;
        y1 = wd->h - 1;
    }
    x0 = x0 - UI_MENU_RIM_X < 0 ? 0 : x0 - UI_MENU_RIM_X;
    y0 = y0 - UI_MENU_RIM_Y < 0 ? 0 : y0 - UI_MENU_RIM_Y;
    x1 = x1 + UI_MENU_RIM_X >= wd->w ? wd->w - 1 : x1 + UI_MENU_RIM_X;
    y1 = y1 + UI_MENU_RIM_Y >= wd->h ? wd->h - 1 : y1 + UI_MENU_RIM_Y;
    wd->x0 = x0;
    wd->y0 = y0;
    wd->tw = x1 - x0 + 1;
    wd->th = y1 - y0 + 1;
}

static int itemOfStr(int str)
{
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        if (ui_menu_text_items[i].str == str) {
            return i;
        }
    }
    return -1;
}

/* settings.c's NOTE_SIZE: the explanations under the rows */
#define OUTLINE_NOTE_SIZE 19.0f

static int words(Word *wd)
{
    static const struct {
        int str, lang;
    } kItems[] = {
        {UI_STR_MT_CONTINUE, UI_LANG_EN},      {UI_STR_MT_NEW_GAME, UI_LANG_EN},
        {UI_STR_MT_OPTIONS, UI_LANG_EN},       {UI_STR_MT_SELECT_SAVE, UI_LANG_EN},
        {UI_STR_MT_DO_NOT_REMOVE, UI_LANG_EN}, {UI_STR_MT_SELECT_SAVE, UI_LANG_FR},
    };

    static const struct {
        int str;
        float size;
        const char *what;
    } kPort[] = {
        {UI_STR_OPT_MIRROR, UI_MENU_TEXT_SIZE, "row"},
        {UI_STR_SECTION_PICTURE, UI_MENU_TEXT_SIZE, "section"},
        {UI_STR_LANGUAGE_NOTE, OUTLINE_NOTE_SIZE, "note"},
    };

    int n = 0;
    for (size_t k = 0; k < sizeof(kItems) / sizeof(kItems[0]) && n < WORD_MAX; k++) {
        const int i = itemOfStr(kItems[k].str);
        if (i < 0) {
            CHECK(0, "no menu text item of string %d", kItems[k].str);
            continue;
        }
        const UiMenuTextItem *it = &ui_menu_text_items[i];
        Word *w = &wd[n];
        w->lang = kItems[k].lang;
        w->w = it->w;
        w->h = it->h;
        w->cov = malloc((size_t)w->w * (size_t)w->h);
        if (!w->cov || ui__menu_strip_raster(it, w->lang, w->cov, w->w, w->h) != 0) {
            CHECK(0, "the strip of item %d (%s)", i, kLang[w->lang]);
            free(w->cov);
            continue;
        }
        snprintf(w->label, sizeof(w->label), "item %d %s", i, kLang[w->lang]);
        wordTrim(w);
        n++;
    }
    for (size_t k = 0; k < sizeof(kPort) / sizeof(kPort[0]) && n < WORD_MAX; k++) {
        Word *w = &wd[n];
        w->lang = UI_LANG_EN;
        if (ui__menu_text_raster(ui_str_in(UI_LANG_EN, (UiStrId)kPort[k].str), kPort[k].size,
                                 UI_ALIGN_LEFT | UI_VALIGN_TOP, &w->cov, &w->w, &w->h) != 0) {
            CHECK(0, "the port's text %d", kPort[k].str);
            continue;
        }
        snprintf(w->label, sizeof(w->label), "port %s %.0f", kPort[k].what, kPort[k].size);
        wordTrim(w);
        n++;
    }
    return n;
}

/* the four styles of a word: {full, faint} x {rim black, the language's rim
   grey}; the light ink otherwise (kSheetInk's fill and dither) */
#define STYLE_ROWS 4

static void styleOf(int k, int lang, RdSheetStyle *st, char *what, size_t n)
{
    const UiSheetInk *ink = ui_menu_sheet_ink(lang);
    const int faint = k & 1, langGrey = k >> 1;
    st->rimOn = 1;
    st->rimLevel = langGrey ? ink->rimLevel : 0;
    st->fillLevel = ink->fillLevel;
    st->dither = ink->dither;
    st->rimWeight = faint ? UI_MENU_FAINT_WEIGHT : 0;
    st->scale = 1;
    snprintf(what, n, "%s rim, grey %d", faint ? "faint" : "full", st->rimLevel);
}

/* the trimmed picture of a word under a look and style, after MODULATE 128
   (refRectOf's step): grey and alpha 0..255, tw x th each */
static void wordPicture(const Word *wd, const RdSheetStyle *st, const SheetLook *look,
                        uint8_t *grey, uint8_t *alpha)
{
    for (int y = 0; y < wd->th; y++) {
        for (int x = 0; x < wd->tw; x++) {
            uint8_t g, a;
            sheetref_texel_look(wd->cov, (uint32_t)wd->w, (uint32_t)wd->h, wd->x0 + x, wd->y0 + y,
                                st, look, &g, &a);
            modulate(g, a, 128, &grey[y * wd->tw + x], &alpha[y * wd->tw + x]);
        }
    }
}

/* the layout of a block at scale s: column 0 the crop (each background
   stacked) when crop, then a column a background; a row per word and style */
typedef struct Block {
    int s, gap, colW, cropW, rowsH, cropH, w, h;
} Block;

static Block blockOf(const Word *wd, int nw, const Rect *crop, int nb, int s)
{
    Block b;
    b.s = s;
    b.gap = 3 * s;
    b.colW = 0;
    b.rowsH = 0;
    for (int i = 0; i < nw; i++) {
        b.colW = wd[i].tw > b.colW ? wd[i].tw : b.colW;
        b.rowsH += STYLE_ROWS * (wd[i].th * s + b.gap);
    }
    b.colW *= s;
    b.cropW = crop ? crop->w * s + b.gap : 0;
    b.cropH = crop ? nb * (crop->h * s + b.gap) : 0;
    b.w = b.gap + b.cropW + nb * (b.colW + b.gap);
    b.h = b.gap + (b.rowsH > b.cropH ? b.rowsH : b.cropH);
    return b;
}

static void blockDraw(uint8_t *img, int W, int oy, const Block *b, const Word *wd, int nw,
                      const Rect *crop, const float *bg, int nb, const SheetLook *look)
{
    if (crop) {
        for (int k = 0; k < nb; k++) {
            blit(img, W, b->gap, oy + b->gap + k * (crop->h * b->s + b->gap), crop->grey,
                 crop->alpha, crop->w, crop->h, b->s, 0, bg[k]);
        }
    }
    int y = oy + b->gap;
    for (int i = 0; i < nw; i++) {
        uint8_t *g = malloc((size_t)wd[i].tw * (size_t)wd[i].th);
        uint8_t *a = malloc((size_t)wd[i].tw * (size_t)wd[i].th);
        for (int k = 0; k < STYLE_ROWS && g && a; k++) {
            RdSheetStyle st;
            char what[48];
            styleOf(k, wd[i].lang, &st, what, sizeof(what));
            wordPicture(&wd[i], &st, look, g, a);
            for (int c = 0; c < nb; c++) {
                blit(img, W, b->gap + b->cropW + c * (b->colW + b->gap), y, g, a, wd[i].tw,
                     wd[i].th, b->s, 0, bg[c]);
            }
            y += wd[i].th * b->s + b->gap;
        }
        free(g);
        free(a);
    }
}

static void lookText(FILE *f, const Cand *c)
{
    fprintf(f, "%s: %s; WX", c->name, c->what);
    for (int d = 0; d <= ICO_SHEET_RX; d++) {
        fprintf(f, " %d", c->look.wx[d]);
    }
    fprintf(f, "; WY");
    for (int d = 0; d <= ICO_SHEET_RY; d++) {
        fprintf(f, " %d", c->look.wy[d]);
    }
    fprintf(f, "; levels %d; dither %s\n", c->look.levels,
            c->look.bayer == 8   ? "8x8"
            : c->look.bayer == 4 ? "4x4"
                                 : "none");
}

/* ICO_MENU_LOOK_OUTLINE=1: outline/<candidate>.png (x1 block above, x4
   nearest below; the row-46 crop the first column), contact.png (every
   candidate's x1 block, the crop once at the top) and contact.txt (the
   legend) */
static void outline(const char *dir)
{
    ref46Dirs(dir, "outline");
    float bg[MAX_BG + 2];
    const int nb = backgrounds(bg);
    Rect crop;
    const int haveCrop = rectOfRow(REF_ROW_PLATE, UI_LANG_EN, &crop) == 0;
    CHECK(haveCrop, "row %d: no sheet on the disc", REF_ROW_PLATE);
    if (haveCrop) {
        Profile p;
        refProfile(&crop, inkGreyOf(), &p);
        profilePrint(&crop, &p);
        if (p.ok) {
            SheetLook *l = &s_cand[CAND_C4].look;
            for (int d = 0; d <= ICO_SHEET_RX; d++) {
                l->wx[d] = (uint16_t)p.wx[d];
            }
            for (int d = 0; d <= ICO_SHEET_RY; d++) {
                l->wy[d] = (uint16_t)p.wy[d];
            }
            l->levels = p.nsteps + 2;
            l->bayer = p.matrix;
        }
    }
    if (s_cand[CAND_C4].look.levels == 0) {
        printf("menu_look: outline: C4 left out (no row-46 measurement)\n");
    }
    Word wd[WORD_MAX];
    const int nw = words(wd);
    const Rect *cr = haveCrop ? &crop : NULL;
    const Block b1 = blockOf(wd, nw, cr, nb, 1), b4 = blockOf(wd, nw, cr, nb, 4);
    const Block bc = blockOf(wd, nw, NULL, nb, 1);
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look/outline/contact.txt", dir);
    FILE *txt = fopen(path, "w");
    if (txt) {
        fprintf(txt, "columns: the backgrounds");
        for (int k = 0; k < nb; k++) {
            fprintf(txt, " %.0f", bg[k]);
        }
        fprintf(txt,
                "\nrows, per word: full rim grey 0, faint rim grey 0, full rim the language's "
                "kSheetInk grey, faint rim the language's grey (faint = %d 64ths)\nwords:",
                UI_MENU_FAINT_WEIGHT);
        for (int i = 0; i < nw; i++) {
            fprintf(txt, " %s%s", i ? "; " : "", wd[i].label);
        }
        fprintf(txt,
                "\n<candidate>.png: the x1 block on top, the x4 nearest below at y %d; the "
                "first column the row-46 crop on each background\n",
                b1.h);
        fprintf(txt, "contact.png: the row-46 crop on each background at the top, then each "
                     "candidate's x1 block (no crop column):\n");
    }
    const int cropBand = haveCrop ? crop.h + 6 : 0;
    const int cropRowW = haveCrop ? 3 + nb * (crop.w + 3) : 0;
    const int CW = bc.w > cropRowW ? bc.w : cropRowW;
    int live = 0;
    for (int c = 0; c < CAND_COUNT; c++) {
        live += s_cand[c].look.levels != 0;
    }
    const int CH = cropBand + live * bc.h;
    uint8_t *contact = canvas(CW, CH);
    if (contact && haveCrop) {
        for (int k = 0; k < nb; k++) {
            blit(contact, CW, 3 + k * (crop.w + 3), 3, crop.grey, crop.alpha, crop.w, crop.h, 1, 0,
                 bg[k]);
        }
    }
    int cy = cropBand;
    for (int c = 0; c < CAND_COUNT; c++) {
        const Cand *cd = &s_cand[c];
        if (cd->look.levels == 0) {
            continue;
        }
        const int W = b4.w, H = b1.h + b4.h;
        uint8_t *img = canvas(W, H);
        if (img) {
            blockDraw(img, W, 0, &b1, wd, nw, cr, bg, nb, &cd->look);
            blockDraw(img, W, b1.h, &b4, wd, nw, cr, bg, nb, &cd->look);
            snprintf(path, sizeof(path), "%s/menu_look/outline/%s.png", dir, cd->name);
            writePngZ(path, img, W, H);
            free(img);
        }
        if (contact) {
            blockDraw(contact, CW, cy, &bc, wd, nw, NULL, bg, nb, &cd->look);
        }
        if (txt) {
            fprintf(txt, "y %d: ", cy);
            lookText(txt, cd);
        }
        printf("menu_look: outline ");
        lookText(stdout, cd);
        cy += bc.h;
    }
    if (contact) {
        snprintf(path, sizeof(path), "%s/menu_look/outline/contact.png", dir);
        writePngZ(path, contact, CW, CH);
        free(contact);
    }
    if (txt) {
        fclose(txt);
        printf("menu_look: wrote %s/menu_look/outline/contact.txt\n", dir);
    }
    for (int i = 0; i < nw; i++) {
        free(wd[i].cov);
    }
    if (haveCrop) {
        free(crop.grey);
        free(crop.alpha);
    }
}

/* Fit mode's texels (each rim computed once, maxFilter, then the texel from
   coverage and rim, sheetref_texel_cr) against sheetref_texel's for the
   constants in force, and the outline mode's control look (C5) against the
   constants: both modes' pictures are only worth their numbers when they
   agree.  The arithmetic is sheet_ref.c's alone (the shader and it are the
   two implementations). */
static void testFitCopy(void)
{
    const Rect *r = NULL;
    for (int k = 0; k < s_nrect[UI_LANG_EN] && !r; k++) {
        if (s_rect[UI_LANG_EN][k].ink == UI_INK_LIGHT) {
            r = &s_rect[UI_LANG_EN][k];
        }
    }
    if (!r) {
        return;
    }
    FitItem f;
    f.r = r;
    f.cov = malloc((size_t)r->w * (size_t)r->h);
    f.rim = malloc((size_t)r->w * (size_t)r->h);
    CHECK(ui__menu_strip_raster(&ui_menu_text_items[r->item], UI_LANG_EN, f.cov, r->w, r->h) == 0,
          "the strip of the first item");
    maxFilter(&f);
    const UiSheetInk *k = ui_menu_sheet_ink(UI_LANG_EN);
    RdSheetStyle st = {k->rimOn, k->rimLevel, k->fillLevel, k->dither, k->rimWeight, 1};
    /* the outline mode's control (C5) is the constants' look */
    SheetLook c5;
    CHECK(outlineLook(&c5, "C5") == 0, "the outline mode's control look C5");
    int diff = 0, diffC5 = 0;
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            uint8_t g, a, gf, af, g5, a5;
            sheetref_texel(f.cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &g, &a);
            fitTexel(&f, x, y, sheetref_default_look(), k->rimLevel, k->fillLevel, &gf, &af);
            sheetref_texel_look(f.cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &c5, &g5, &a5);
            diff += gf != g || af != a;
            diffC5 += g5 != g || a5 != a;
        }
    }
    CHECK(diffC5 == 0, "the outline mode's control look differs from the constants at %d texels",
          diffC5);
    CHECK(diff == 0,
          "fit mode's texels (the rim once, then sheetref_texel_cr) differ from sheetref_texel at "
          "%d texels",
          diff);
    free(f.cov);
    free(f.rim);
}

/* ----------------------------------------------------------------- main */

static uint8_t *readAll(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: menu_look_test BASE_ELF DISC_IMAGE OUT_DIR\n");
        return 2;
    }
    size_t size;
    uint8_t *elf = readAll(argv[1], &size);
    if (!elf) {
        printf("menu_look: no base ELF (%s): skipped\n", argv[1]);
        return 77;
    }
    char err[512];
    const int rc = ico_tables_load_elf(&ico_table_set_pal, elf, size, err, sizeof(err));
    free(elf);
    if (rc != 0) {
        printf("menu_look: the ELF's tables do not load: %s\n", err);
        return 1;
    }
    s_vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[2]);
    if (!s_vfs) {
        printf("menu_look: no disc image (%s): skipped\n", argv[2]);
        return 77;
    }
    collect();
    int n = 0;
    for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
        n += s_nrect[l];
    }
    if (n == 0) {
        printf("menu_look: no sheet could be read from the disc image: skipped\n");
        ico_vfs_unmount(s_vfs);
        return 77;
    }
    const char *fitEnv = getenv("ICO_MENU_LOOK_FIT");
    const char *geoEnv = getenv("ICO_MENU_LOOK_GEOFIT");
    const char *ref46Env = getenv("ICO_MENU_LOOK_REF46");
    const char *outlineEnv = getenv("ICO_MENU_LOOK_OUTLINE");
    const char *boldEnv = getenv("ICO_MENU_LOOK_BOLD");
    if (boldEnv) {
        float bx = 0.0f, by = 0.0f;
        if (sscanf(boldEnv, "%f,%f", &bx, &by) == 2) {
            ui__menu_set_bold(bx, by);
            printf("menu_look: bold %.2f %.2f\n", bx, by);
        }
    }
    const char *widthEnv = getenv("ICO_MENU_LOOK_WIDTH");
    if (widthEnv) {
        ui__sheet_set_width((float)atof(widthEnv));
    }
    if (geoEnv && (*geoEnv == '1' || *geoEnv == '2')) {
        geoFit(*geoEnv == '2');
    } else if (fitEnv && *fitEnv == '1') {
        fit();
    } else if (ref46Env && *ref46Env == '1') {
        ref46(argv[3]);
    } else if (outlineEnv && *outlineEnv == '1') {
        outline(argv[3]);
    } else {
        testSurvey();
        testFitCopy();
        testCompareItems(argv[3]);
    }
    for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
        for (int k = 0; k < s_nrect[l]; k++) {
            free(s_rect[l][k].grey);
            free(s_rect[l][k].alpha);
        }
        free(s_rect[l]);
    }
    ms_sheets_free();
    ico_vfs_unmount(s_vfs);
    printf("menu_look: %d failures\n", failures);
    return failures ? 1 : 0;
}
