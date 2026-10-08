/* menu_look_test.c: the menus' Arimo text against the game's menu sheets
 * (v0.4.2, package F-C).
 *
 * Every menu word is Arimo rasterised on the sheets' texel grid and drawn in
 * the sheets' style (menu_font.h, rd.h rd_CreateTextureSheet).  This test
 * holds that look to the sheets it imitates: the sheets are read from the
 * player's disc (menu_sheets.c), the item strips are the ones the game draws
 * (ui__MenuStripRaster) run through the shader's CPU reference
 * (port/render/test/sheet_ref.c), and the two pictures are compared.
 *
 *   testSurvey        measures the sheets (the light-ink items of the menu
 *                     text table on the five languages' sheets) and pins
 *                     the constants that came from it: kSheetInk (menu_font.c),
 *                     ICO_SHEET_RX / RY, the rim's falloff ICO_SHEET_WX / WY
 *                     and ICO_SHEET_LEVELS (shader_consts.h).
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
 * prints the best, instead of the checks.  The constants it finds are then
 * written into shader_consts.h, sheet_text.hlsli and kSheetInk by hand.
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
#include "rd_internal.h" /* rd_WritePng */
#include "sheet_ref.h"
#include "shader_consts.h"
#include "strings.h"
#include "tables.h"
#include "ui_internal.h"
#include "vfs.h"
#include "host_fs.h"

/* ------------------------------------------------------------- limits */
/* The limits of testCompareItems, set from the first run on main (v0.4.2,
   the 100 items a language with ink on the sheet, 500 in all): each is the
   95th percentile over the five languages plus 25 %, or the worst item plus
   some room where that is further (named).  They hold the look as it is and
   catch a regression; they are not a likeness bound.  The strips differ
   from the sheets by the lettering's geometry, not by the ink: Arimo set at
   the sheets' capitals is wider than their lettering (the ink box's width,
   strip / sheet, median 1.12 on menu_PAL_01, 1.17 on 02, 1.22 on 03, 1.25
   on 04) and about one row shorter (height 0.83 .. 0.92), so the letters
   fall beside the sheet's and the differences below are that misregistration
   (taking the rim off the rimless sheets 02, title and scei moves their
   blur 37.5 to 36.7 only).  Measured, median / max per language EN FR DE IT
   ES, the larger of the two backgrounds: */
/* blur 27.8/55.1 31.9/56.2 31.2/56.0 29.8/56.2 31.5/56.2; p95 47.8; the worst
   ESPAÑOL (title.tm2, row 30) */
#define T_BLUR 60.0f
/* plain 35.7/70.9 41.3/73.5 41.2/73.2 37.7/73.5 40.0/73.6; p95 64.2 */
#define T_PLAIN 80.0f
/* the ink box's worst edge, texels: 10/44 8/41 9/50 6/47 7/43; p95 31.2;
   the long right-aligned Options rows of menu_PAL_03 run 40 to 50 texels
   further left (rows 323 Config. botones / 340 Hand halten/Rufen and their
   languages' words) */
#define T_EDGE 56.0f
/* the same for an item of several lines: 18/25 15/33 19/40 20/38 19/39; p95
   38.9; the worst row 185 (the slot prompt, two lines) */
#define T_EDGE_MULTI 49.0f
/* the first line's capital top and baseline, rows: 0.95/2.07 1.00/2.08
   0.92/2.03 0.94/2.08 1.00/2.33; p95 1.88 (the strip's capitals start about
   a row lower: the em is the sheets' capitals less 0.7 texel) */
#define T_LINE 2.5f
/* the ink amount, strip / sheet: median 0.84 0.77 0.76 0.79 0.78, min 0.28
   0.30 0.29 0.23 0.31, max 1.10 1.02 1.03 1.04 1.04; p5 0.57.  The low ones:
   row 61's "10" (the sheet's digits fill the tile, Arimo's two are set at 60
   % to fit) and row 414 in Italian (the sheet's dash is a thick outlined
   bar, Arimo's a thin rule) */
#define T_AMOUNT_LO 0.20f
#define T_AMOUNT_HI 1.25f
/* the rim / fill / edge shares, the worst of the three, absolute: median
   0.21 0.07 0.16 0.08 0.12, max 0.77 0.76 0.80 0.62 0.77; p95 0.62; the
   worst German menu_PAL_04 (rows 73 Accessing, 211 Loading, 249 Formatting:
   the strip's halo where that sheet has little) */
#define T_SHARE 0.90f
#define MIN_INK_AMOUNT 6.0f /* a sheet rectangle with less ink than this is not compared */

/* The survey's expectations (testSurvey): what the scratch survey measured on
   the PAL sheets (package F-C1), per language EN FR DE IT ES. */
static const float kExpFill[UI_LANG_COUNT] = {254.7f, 253.7f, 253.3f, 254.4f, 253.5f};
static const float kExpRim[UI_LANG_COUNT] = {23.9f, 61.1f, 55.7f, 60.8f, 61.7f};
/* the first six texels' alpha, summed: left and right averaged, above and below averaged */
static const float kExpMassX[UI_LANG_COUNT] = {3.10f, 3.73f, 3.99f, 3.78f, 3.64f};
static const float kExpMassY[UI_LANG_COUNT] = {2.15f, 2.83f, 2.99f, 2.90f, 2.74f};
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

static const char *const kLang[UI_LANG_COUNT] = {"en", "fr", "de", "it", "es"};

/* ------------------------------------------------------------- the rects */

/* One item on one language's sheet: its rectangle's texels. */
typedef struct Rect {
    int item, row, lang;
    int ink; /* UiMenuTextInk */
    int w, h;
    const char *sheet;     /* the sheet's name */
    uint8_t *grey, *alpha; /* w x h, alpha 0..255 */
} Rect;

static IcoVfs *s_vfs;
static Rect *s_rect[UI_LANG_COUNT];
static int s_nrect[UI_LANG_COUNT];

static int rowOfItem(int item)
{
    for (int r = 0; r < ui_menu_text_row_count; r++) {
        if (ui_menu_text_rows[r].item == item) {
            return ui_menu_text_rows[r].row;
        }
    }
    return -1;
}

static void collect(void)
{
    const int texFiles = ms_TexFileCount();
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        s_rect[lang] = calloc((size_t)ui_menu_text_item_count, sizeof(Rect));
        for (int i = 0; i < ui_menu_text_item_count; i++) {
            const UiMenuTextItem *it = &ui_menu_text_items[i];
            const int row = rowOfItem(i);
            if (row < 0) {
                continue;
            }
            const LtProperty *e = &texProperty[row];
            if (e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h ||
                e->texFileNo < 0 || e->texFileNo >= texFiles) {
                continue;
            }
            char name[64];
            ms_SheetName(texFile[e->texFileNo].path, lang, name, sizeof(name));
            const MsSheet *s = ms_SheetFor(s_vfs, name);
            if (!s || it->u + it->w > s->w || it->v + it->h > s->h) {
                continue;
            }
            Rect *r = &s_rect[lang][s_nrect[lang]++];
            r->item = i;
            r->row = row;
            r->lang = lang;
            r->ink = it->ink;
            r->w = it->w;
            r->h = it->h;
            r->sheet = s->name;
            r->grey = malloc((size_t)r->w * (size_t)r->h);
            r->alpha = malloc((size_t)r->w * (size_t)r->h);
            for (int y = 0; y < r->h; y++) {
                for (int x = 0; x < r->w; x++) {
                    const uint8_t *p =
                        s->rgba + ((size_t)(it->v + y) * (size_t)s->w + (size_t)(it->u + x)) * 4;
                    r->grey[y * r->w + x] = p[0];
                    r->alpha[y * r->w + x] = p[3];
                }
            }
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
    Survey sv[UI_LANG_COUNT];
    double meanX = 0, meanY = 0;
    int steps[UI_LANG_COUNT * 16], nsteps = 0;
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        survey(l, &sv[l]);
        const UiSheetInk *ink = ui_MenuSheetInk(l);
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
        meanX += sv[l].massX / UI_LANG_COUNT;
        meanY += sv[l].massY / UI_LANG_COUNT;
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
    CHECK(median + 2 == ICO_SHEET_LEVELS, "ICO_SHEET_LEVELS %d, the survey's %d", ICO_SHEET_LEVELS,
          median + 2);
    /* the strips' margin covers the rim's reach and the bilinear texel */
    CHECK(UI_MENU_RIM_X == ICO_SHEET_RX + 1 && UI_MENU_RIM_Y == ICO_SHEET_RY + 1,
          "menu_font.h's rim margin %d x %d for a rim of %d x %d", UI_MENU_RIM_X, UI_MENU_RIM_Y,
          ICO_SHEET_RX, ICO_SHEET_RY);
}

/* ------------------------------------------------------------ the strip */

/* the style and the sprite colour (GS, 0x80 = 1) an ink draws with */
static void inkOf(int ink, int lang, RdSheetStyle *st, int *col)
{
    if (ink == UI_INK_LIGHT) {
        const UiSheetInk *k = ui_MenuSheetInk(lang);
        st->rimOn = k->rimOn;
        st->rimLevel = k->rimLevel;
        st->fillLevel = k->fillLevel;
        st->dither = k->dither;
        *col = 128;
        return;
    }
    st->rimOn = 0;
    st->rimLevel = 0;
    st->fillLevel = 255;
    st->dither = 1;
    *col = ink == UI_INK_DARK ? 0 : ink == UI_INK_GREY ? (128 * 151 + 127) / 255 : 128;
}

/* the reference rectangle: grey and alpha 0..255 after the texture function
   (MODULATE with the sprite colour) */
static int refRect(const Rect *r, uint8_t *grey, uint8_t *alpha)
{
    uint8_t *cov = malloc((size_t)r->w * (size_t)r->h);
    if (!cov) {
        return -1;
    }
    if (ui__MenuStripRaster(&ui_menu_text_items[r->item], r->lang, cov, r->w, r->h) != 0) {
        free(cov);
        return -1;
    }
    RdSheetStyle st;
    int col;
    inkOf(r->ink, r->lang, &st, &col);
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            uint8_t g, a;
            sheetref_Texel(cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &g, &a);
            const int gm = (g * col + 64) / 128;
            grey[y * r->w + x] = (uint8_t)(gm > 255 ? 255 : gm);
            const int a255 = a * 255 / 128;
            alpha[y * r->w + x] = (uint8_t)(a255 > 255 ? 255 : a255);
        }
    }
    free(cov);
    return 0;
}

/* ------------------------------------------------------------- measures */

typedef struct Measures {
    float blur[2], plain[2];        /* per background (black, grey) */
    float ink;                      /* the ink amount, texels */
    float left, right, top, bottom; /* the ink box, texels (fractional) */
    float capTop, baseline;         /* the first line's, rows */
    int hasLine;                    /* a run of 4 rows or more (a colon's dots have none) */
    float shareRim, shareFill, shareEdge;
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
    float total = 0.0f;
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
                }
            }
        }
    }
    m->ink = total;
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

/* ------------------------------------------------------------ pictures */

static void writeDirs(const char *dir, int lang)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look", dir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/menu_look/%s", dir, kLang[lang]);
    ico_mkdir(path);
}

/* a w x h picture on grey as RGBA at scale s (nearest or bilinear) into dst
   (pitch dw): the picture's texels are at their centres */
static void blit(uint8_t *dst, int dw, int ox, int oy, const uint8_t *grey, const uint8_t *alpha,
                 int w, int h, int s, int bilinear)
{
    const float bg = 128.0f;
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
    blit(img, W, 0, 0, r->grey, r->alpha, r->w, r->h, s, 0);
    blit(img, W, r->w * s + gap, 0, rg, ra, r->w, r->h, s, 0);
    blit(img, W, 0, r->h * s + gap, r->grey, r->alpha, r->w, r->h, s, 1);
    blit(img, W, r->w * s + gap, r->h * s + gap, rg, ra, r->w, r->h, s, 1);
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look/%s/%d.png", dir, kLang[r->lang], r->row);
    rd_WritePng(path, img, (uint32_t)W, (uint32_t)H, (uint32_t)W * 4, 1);
    free(img);
}

/* ---------------------------------------------------------- the compare */

static void testCompareItems(const char *outDir)
{
    int total = 0, bad = 0, skipped = 0;
    double worstBlur = 0.0, worstPlain = 0.0;
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
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
                blit(contact, CW, 0, cy, r->grey, r->alpha, r->w, r->h, 1, 0);
                blit(contact, CW, contactW + 8, cy, rg, ra, r->w, r->h, 1, 0);
                cy += r->h + 2;
            }
            free(rg);
            free(ra);
            if (ms.ink < MIN_INK_AMOUNT) {
                skipped++;
                continue; /* nothing of the item's words on this sheet */
            }
            total++;
            langN++;
            const int multi = strchr(ui_StrIn((UiLang)lang, (UiStrId)it->str), '\n') != NULL;
            const float edge = multi ? T_EDGE_MULTI : T_EDGE;
            char why[512];
            why[0] = '\0';
            size_t n = 0;
#define NOTE(...) n += (size_t)snprintf(why + n, sizeof(why) - n, __VA_ARGS__)
            for (int b = 0; b < 2; b++) {
                if (mr.blur[b] > T_BLUR) {
                    NOTE(" blur[%d] %.1f", b, mr.blur[b]);
                }
                if (mr.plain[b] > T_PLAIN) {
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
            if (ratio < T_AMOUNT_LO || ratio > T_AMOUNT_HI) {
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
            rd_WritePng(path, contact, (uint32_t)CW, (uint32_t)(cy > 0 ? cy : 1), (uint32_t)CW * 4,
                        1);
            free(contact);
        }
        printf("menu_look: %s: %d items compared, %d outside the limits\n", kLang[lang], langN,
               langBad);
        CHECK(langN >= 60, "%s: only %d items compared", kLang[lang], langN);
    }
    printf("menu_look: %d items, %d outside the limits (blur <= %.1f, plain <= %.1f), %d without "
           "ink on the sheet; the worst blur %.1f, plain %.1f\n",
           total, bad, T_BLUR, T_PLAIN, skipped, worstBlur, worstPlain);
    CHECK(bad == 0, "%d items outside the limits", bad);
}

/* ------------------------------------------------------------------ fit */

/* font_sheet_ps's arithmetic (sheet_ref.c) with the constants as arguments */
static unsigned quantiseN(unsigned v, unsigned th32, unsigned levels)
{
    const unsigned n = levels - 1;
    unsigned q = (v * n * 32u + th32 * 255u) / (255u * 32u);
    q = q > n ? n : q;
    return (q * 255u + n / 2u) / n;
}

static const uint8_t kBayerT[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

typedef struct FitItem {
    const Rect *r;
    uint8_t *cov, *rim; /* the strip and its rim (the weighted dilation) */
} FitItem;

/* the rim: the coverage dilated with the falloff (shader_consts.h's), a copy
   of sheet_ref.c's arithmetic (testFitCopy holds the two together) */
static void maxFilter(const FitItem *f)
{
    static const unsigned kWx[ICO_SHEET_RX + 1] = {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2,
                                                   ICO_SHEET_WX_3, ICO_SHEET_WX_4, ICO_SHEET_WX_5,
                                                   ICO_SHEET_WX_6};
    static const unsigned kWy[ICO_SHEET_RY + 1] = {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2,
                                                   ICO_SHEET_WY_3, ICO_SHEET_WY_4};
    const int w = f->r->w, h = f->r->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned m = 0;
            for (int dy = -ICO_SHEET_RY; dy <= ICO_SHEET_RY; dy++) {
                for (int dx = -ICO_SHEET_RX; dx <= ICO_SHEET_RX; dx++) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
                        const unsigned c =
                            f->cov[yy * w + xx] * kWx[dx < 0 ? -dx : dx] * kWy[dy < 0 ? -dy : dy];
                        m = c > m ? c : m;
                    }
                }
            }
            f->rim[y * w + x] = (uint8_t)((m + 500000u) / 1000000u);
        }
    }
}

/* the blurred difference of one item for a style and level count, over both
   backgrounds */
static double fitScore(const FitItem *f, unsigned levels, unsigned rim, unsigned fill)
{
    const int w = f->r->w, h = f->r->h, n = w * h;
    uint8_t *g = malloc((size_t)n), *a = malloc((size_t)n);
    float *p = malloc((size_t)n * sizeof(float)), *q = malloc((size_t)n * sizeof(float)),
          *pb = malloc((size_t)n * sizeof(float));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned c = f->cov[y * w + x], r = f->rim[y * w + x];
            const unsigned av = c > r ? c : r;
            const unsigned t = av ? (c * 255u + av / 2u) / av : 0u;
            const unsigned th = 2u * kBayerT[y & 3][x & 3] + 1u;
            const unsigned aq = quantiseN(av, th, levels), tq = quantiseN(t, th, levels);
            g[y * w + x] = (uint8_t)((rim * (255u - tq) + fill * tq + 127u) / 255u);
            const unsigned a128 = (aq * 128u + 127u) / 255u;
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

    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        FitItem fi[MAXI];
        int n = 0;
        for (int k = 0; k < s_nrect[lang] && n < MAXI; k++) {
            const Rect *r = &s_rect[lang][k];
            if (r->ink != UI_INK_LIGHT) {
                continue;
            }
            uint8_t *cov = malloc((size_t)r->w * (size_t)r->h);
            if (!cov || ui__MenuStripRaster(&ui_menu_text_items[r->item], lang, cov, r->w, r->h)) {
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
        const UiSheetInk *cur = ui_MenuSheetInk(lang);
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

/* The reference of this file's own arithmetic against sheet_ref.c's, for the
   constants in force (fit mode's copy is only worth its numbers when it
   agrees). */
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
    CHECK(ui__MenuStripRaster(&ui_menu_text_items[r->item], UI_LANG_EN, f.cov, r->w, r->h) == 0,
          "the strip of the first item");
    maxFilter(&f);
    const UiSheetInk *k = ui_MenuSheetInk(UI_LANG_EN);
    RdSheetStyle st = {k->rimOn, k->rimLevel, k->fillLevel, k->dither};
    int diff = 0;
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            uint8_t g, a;
            sheetref_Texel(f.cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &g, &a);
            const unsigned c = f.cov[y * r->w + x], rm = f.rim[y * r->w + x];
            const unsigned av = c > rm ? c : rm;
            const unsigned t = av ? (c * 255u + av / 2u) / av : 0u;
            const unsigned th = 2u * kBayerT[y & 3][x & 3] + 1u;
            const unsigned aq = quantiseN(av, th, ICO_SHEET_LEVELS),
                           tq = quantiseN(t, th, ICO_SHEET_LEVELS);
            const unsigned gg =
                ((unsigned)k->rimLevel * (255u - tq) + (unsigned)k->fillLevel * tq + 127u) / 255u;
            diff += gg != g || (aq * 128u + 127u) / 255u != a;
        }
    }
    CHECK(diff == 0,
          "fit mode's copy of the sheet arithmetic differs from sheet_ref.c at %d texels", diff);
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
    const int rc = ico_tables_load_elf(elf, size, err, sizeof(err));
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
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        n += s_nrect[l];
    }
    if (n == 0) {
        printf("menu_look: no sheet could be read from the disc image: skipped\n");
        ico_vfs_unmount(s_vfs);
        return 77;
    }
    const char *fitEnv = getenv("ICO_MENU_LOOK_FIT");
    if (fitEnv && *fitEnv == '1') {
        fit();
    } else {
        testSurvey();
        testFitCopy();
        testCompareItems(argv[3]);
    }
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int k = 0; k < s_nrect[l]; k++) {
            free(s_rect[l][k].grey);
            free(s_rect[l][k].alpha);
        }
        free(s_rect[l]);
    }
    ms_SheetsFree();
    ico_vfs_unmount(s_vfs);
    printf("menu_look: %d failures\n", failures);
    return failures ? 1 : 0;
}
