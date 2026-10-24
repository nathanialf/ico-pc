/*
 * port/ui/menu_font.c
 *
 * The menus' text in the sheets' look (menu_font.h): the strip cache, the
 * inks and the draws.  font.c rasterises (ui__sheet_raster_line, the only
 * stb compile unit) and emits the sprites (ui__draw_tex_quads).
 */
#include "menu_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif

#include "../shaders/shader_consts.h"
#include "strings.h"
#include "ui_internal.h"

#define MF_PAGE 1024 /* a 1x page's side, texels */
/* Texels between strips: font_sheet_ps builds a texel's rim from the page's
   coverage up to ICO_SHEET_RX across and ICO_SHEET_RY down and cannot see
   where a strip ends, and the bilinear read takes one texel past a strip's
   edge, so a neighbour's ink must sit at least the rim's reach plus one
   texel away or its rim smears onto this strip's edge.  Strip origins sit
   on multiples of MF_ALIGN (the dither's Bayer cell, font.hlsl
   sheet_threshold indexes it by page texel), so a word's grain is the same
   wherever it lands and the gaps come out at 8 either way. */
#define MF_GAP_X 7
#define MF_GAP_Y 5
#define MF_ALIGN 4
#define MF_ORIGIN 8 /* the first strip's corner: the gaps rounded up to MF_ALIGN */
_Static_assert(MF_GAP_X >= ICO_SHEET_RX + 1, "strips must be a rim's reach apart across");
_Static_assert(MF_GAP_Y >= ICO_SHEET_RY + 1, "strips must be a rim's reach apart down");
_Static_assert(MF_ORIGIN >= MF_GAP_X && MF_ORIGIN >= MF_GAP_Y && MF_ORIGIN % MF_ALIGN == 0,
               "the first strip's corner keeps the gaps and the alignment");
/* the largest strip a 1x page holds, sheet texels (a port text's is cut to
   it at every scale, so its quad is the same) */
#define MF_MAX_W (MF_PAGE - MF_ORIGIN - MF_GAP_X)
#define MF_MAX_H (MF_PAGE - MF_ORIGIN - MF_GAP_Y)
/* The strips are rasterised at s strip texels a sheet texel (mfScale: the
   scene's scale, or the output's in overlay mode, 1 .. ICO_SHEET_SCALE_MAX),
   so the letters are as crisp as the picture; the gaps, the alignment and the
   first corner above are in sheet texels (s times as many strip texels).  A
   page holds strips of one scale: 1024 x 1024 at 1x; above it 1024 s x 256 s
   of coverage, at most 4096 wide, and below it as many texels of rim (rd.h
   rd_sheet_rim: the 1x rim of the sheet texels, one value over each sheet
   texel's texels, which the shader magnifies), so the texture is 2048 x 1024
   at 2x, 3072 x 1536 at 3x, 4096 x 2048 at 4x (2, 4.5 and 8 MB, kept on the
   CPU and in the texture; at most MF_PAGES of a style, three styles: 96 MB of
   textures at 4x if every page of every style were in use, two pages, 16 MB,
   for a menu).  A page is 1009 sheet texels wide at every scale (the longest
   notes fit) and 243 tall (the 1x page 1011: a menu's rows are 15 to 30
   texels). Pages of another scale than a draw's are emptied once no frame can
   name them (MF_EVICT_FRAMES), so a resolution change re-rasterises the words
   it shows within a few frames and a popup on the output (a scale of its own)
   can share a style's pages with the scene's words.
   A page is filled in shelves, bands of rows packed left to right, each
   marked with the frame one of its strips was last drawn.  When a strip
   finds no room, the shelves no frame can still name (MF_EVICT_FRAMES
   frames undrawn) are emptied and their bands take new strips no taller
   than they are, before another page is opened: a screen with a string
   that changes every tick (the Models viewer's "Frame N / M") keeps its
   steady rows where they are and recycles the counter's bands, where a
   whole-page rule alone finds every page drawn each frame and draws new
   words with the plain font once the pages are full.  Only when every
   shelf of every page of the scale holds a strip drawn in the last
   MF_EVICT_FRAMES frames does a word fall back to the plain font (ui_menu_font_fallbacks counts those,
   ui_menu_font_shelf_reclaims the emptied shelves; both logged). */
#define MF_PAGE_MAX 4096
#define MF_PAGES 4        /* pages per style */
#define MF_STRIPS 1024    /* strips cached at once */
#define MF_EVICT_FRAMES 4 /* rd frames a page or a shelf must go undrawn before it is emptied */
#define MF_LINES 16       /* lines of one text */
#define MF_SHELVES 128    /* shelves of one page (a 1x page holds about 50 of a menu's rows) */
/* texels of margin round a port text's ink: the rim (rd's ICO_SHEET_RX and
   ICO_SHEET_RY, 6 and 4; ctest menu_look checks the pair) and the bilinear
   read one texel past it */
#define MF_RIM_X UI_MENU_RIM_X
#define MF_RIM_Y UI_MENU_RIM_Y

/* the page sets: the light ink with its full rim, with a faint one, and
   the plain style (no rim) */
enum { MF_LIGHT = 0, MF_FAINT = 1, MF_PLAIN = 2, MF_CLASSES };

/* The light ink per language (UiLang order: EN FR DE IT ES): rimOn,
   rimLevel, fillLevel, dither.  The one table to retune.

   Measured on the game's sheets (the PAL sheets menu_PAL_01..04, scei and
   title, the 79 light-ink items of the menu text table, texels as (grey,
   alpha) with the GS alpha 0x80 = 1; ctest menu_look testSurvey re-measures
   them and pins this table, RX, RY and LEVELS):
     fill   the letters are white: palette entries ffffff at alpha 0x80 on
            every sheet; the fill texels (grey >= 200, alpha >= 0.7) average
            251 (EN) and 249 (FR DE IT ES), so the fill level is 255.
     rim    the dark ink around the letters, alpha-weighted mean grey of the
            texels with alpha >= 0.5 and grey <= 100 two or more texels from the
            letters: EN 24 (black on sheet 01, graded 0..100 on 03 and 04),
            FR 61, DE 56, IT 61, ES 62.  German's rim is grey like the
            Romance languages', not black.
     width  not a hard edge: the alpha falls off over several texels (outside
            the letters, left and right, texel 1..6: EN .76 .61 .54 .50 .39 .33,
            FR .88 .78 .72 .59 .44 .37, DE .93 .84 .74 .65 .50 .41, above and
            below 1..4: EN .77 .44 .36 .27, FR .89 .67 .49 .30, DE .91 .70 .51
            .36).  The mass of the first six texels is 3.1 (EN) to 4.1 (DE) across
            and 2.0 (EN) to 3.1 (DE) down, 3.7 and 2.7 averaged over the five.
            The rim is the coverage dilated with the English falloff
            (shader_consts.h: ICO_SHEET_RX 6, ICO_SHEET_RY 4, ICO_SHEET_WX
            1 .76 .61 .54 .50 .39 .33, ICO_SHEET_WY 1 .77 .44 .36 .27), one
            shape for every language; the stronger French and German halo is
            not modelled (the style has a rim grey, not a rim strength).  A
            hard rim of the same mass (4 x 3) filled the gaps between the
            letters and drew each word in a dark box.
     steps  the white texels' alpha takes three steps between none and full
            (sheet 01 EN: 0x20 0x43 0x61 then 0x80; sheet 04 EN: 0x1D 0x3D
            0x5E then 0x7E): five levels; ICO_SHEET_LEVELS is 8, which
            matches the sheets better with the fitted geometry
            (shader_consts.h).
   ctest menu_look's survey re-measures these numbers within 0.1. */
static const UiSheetInk kSheetInk[UI_LANG_SHEET_COUNT] = {
    {1, 24, 255, 1, 0}, /* English */
    {1, 61, 255, 1, 0}, /* French */
    {1, 56, 255, 1, 0}, /* German */
    {1, 61, 255, 1, 0}, /* Italian */
    {1, 62, 255, 1, 0}, /* Spanish */
    /* the Japanese disc's sheets (UI_SHEET_JA), measured by ctest
       menu_look_jpn (menu_region_test.c) on Tex_menu01's option, mc04, title
       and scei sheets: fill 251.5 (the letters' palette white is fcfcfc,
       252), rim 34.3, one antialiasing step */
    {1, 34, 252, 1, 0},
    /* the Korean disc's sheets (UI_SHEET_KO), measured off line on SCPS-56001's
       Tex_menu01 title (새로 시작하기, 불러오기), mc04 (게임으로 돌아감, 게임을 종료함)
       and option (옵션, 게임으로) words with menu_region_test.c's rule (inkAdd:
       the fill texels grey >= 240 and alpha >= 0.9, the rim texels alpha
       >= 0.5, grey <= 100, not next to a fill texel), the same rule giving
       250.5 and 33.6 on the Japanese disc's same words where menu_look_jpn
       measures 251.5 and 34.3: fill 249.7 + 1.0, rim 42.2 + 0.7 (each
       sheet's own: title 254 / 45.6, mc04 245 / 40.2, option 252.7 / 41.1),
       one antialiasing step (alpha 232 on the title, 252 on mc04) as the
       Japanese sheets'.  ctest menu_look_kor (menu_region_test.c) measured the
       disc at fill 252.8, rim 41.8 (kInkExpect there): the values here. */
    {1, 42, 253, 1, 0},
};
/* the dark, plain and grey inks and the light words without a halo: no
   rim, a white fill (the vertex colour makes them black, white or grey) */
static const UiSheetInk kPlainInk = {0, 0, 255, 1, 0};

/* a band of a page's rows that strips fill left to right (shelfFit) */
typedef struct MfShelf {
    int y, h;           /* its rows: [y, y + h), strip texels */
    int x;              /* the next strip's column; the page's width once closed */
    int strips;         /* the strips on it */
    uint32_t lastFrame; /* rd_frame_number when one of them was last drawn or placed */
} MfShelf;

typedef struct MfPage {
    uint8_t *cov;                /* w x h coverage, NULL while unused; above 1x the rim
                     below it (w x h more) */
    int scale;                   /* strip texels a sheet texel */
    int w, h;                    /* the coverage's texels (pageW, pageH); the texture is
                     twice as tall above 1x */
    uint32_t tex;                /* the rd sheet texture, 0 until first drawn */
    MfShelf shelves[MF_SHELVES]; /* top to bottom; the last is the open one */
    int nShelves;
    uint32_t lastFrame; /* rd_frame_number at its last draw */
} MfPage;

/* a port text's geometry (sheetGeo, below) */
typedef struct MfGeo {
    float em, wx, gap, boldX, boldY;
} MfGeo;

typedef struct MfStrip {
    int used;
    uint64_t hash;
    char *text;          /* a port text's string; NULL for an item's */
    int item, lang;      /* an item's index and language; -1 for a port text */
    float em;            /* texel rows */
    MfGeo geo;           /* a port text's geometry */
    unsigned layout;     /* a port text's alignment flags */
    int scale;           /* strip texels a sheet texel */
    int cls, page, x, y; /* the page's texels */
    int shelf;           /* its shelf on the page */
    int w, h;            /* sheet texels (w * scale x h * scale on the page) */
    int ox, oy;          /* a port text's top-left from its snapped anchor: texels, rows */
} MfStrip;

static struct {
    MfPage pages[MF_CLASSES][MF_PAGES];
    MfStrip strips[MF_STRIPS];
    int lightLang; /* the language the light pages are styled for; -1 none */
    int warnedFull;
    int reclaims;  /* shelves emptied for new strips (reclaimShelf), this run */
    int fallbacks; /* words drawn with the plain font for want of room, this run */
    int hasLast;
    UiMenuStrip last;
} s_mf = {.lightLang = -1};

const UiSheetInk *ui_menu_sheet_ink(int lang)
{
    return &kSheetInk[lang >= 0 && lang < UI_LANG_SHEET_COUNT ? lang : UI_LANG_EN];
}

/* the class of an item's words: its ink and its rim on the sheet */
static int classOf(int ink, int rim)
{
    if (ink != UI_INK_LIGHT || rim == UI_RIM_NONE) {
        return MF_PLAIN;
    }
    return rim == UI_RIM_FAINT ? MF_FAINT : MF_LIGHT;
}

static const UiSheetInk *classInk(int cls, int lang)
{
    static UiSheetInk faint[UI_LANG_SHEET_COUNT];
    if (cls == MF_PLAIN) {
        return &kPlainInk;
    }
    const UiSheetInk *k = ui_menu_sheet_ink(lang);
    if (cls == MF_LIGHT) {
        return k;
    }
    const int l = (int)(k - kSheetInk);
    faint[l] = *k;
    faint[l].rimWeight = UI_MENU_FAINT_WEIGHT;
    return &faint[l];
}

const UiSheetInk *ui_menu_item_ink(int ink, int rim, int lang)
{
    return classInk(classOf(ink, rim), lang);
}

/* ------------------------------------------------------------- measuring */

static MfGeo sheetGeo(void);

/* the lines of utf8: starts and lengths; returns the count */
static int splitLines(const char *utf8, const char **start, size_t *len)
{
    int n = 0;
    const char *s = utf8;
    while (n < MF_LINES) {
        const char *nl = strchr(s, '\n');
        start[n] = s;
        len[n] = nl ? (size_t)(nl - s) : strlen(s);
        n++;
        if (!nl) {
            break;
        }
        s = nl + 1;
    }
    return n;
}

float ui_measure_menu_text(float size, const char *utf8)
{
    if (!utf8) {
        return 0.0f;
    }
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(utf8, start, len);
    const MfGeo g = sheetGeo();
    const float gap = ui__sheet_set_kana_gap(g.gap);
    float widest = 0.0f;
    for (int i = 0; i < n; i++) {
        const float w = ui__sheet_line_width(size * 0.5f * g.em, g.wx, 0.0f, start[i], len[i]);
        widest = w > widest ? w : widest;
    }
    ui__sheet_set_kana_gap(gap);
    return widest;
}

void ui_menu_font_metrics(float size, float *ascent, float *descent, float *capHeight)
{
    float a = 0.0f, d = 0.0f, c = 0.0f;
    ui__sheet_v_metrics(size * 0.5f * sheetGeo().em, &a, &d, NULL, &c);
    if (ascent) {
        *ascent = 2.0f * a;
    }
    if (descent) {
        *descent = 2.0f * d;
    }
    if (capHeight) {
        *capHeight = 2.0f * c;
    }
}

/* ------------------------------------------------------------ the weight */

static float s_boldX = UI_MENU_BOLD_X, s_boldY = UI_MENU_BOLD_Y;

void ui__menu_set_bold(float bx, float by)
{
    s_boldX = bx;
    s_boldY = by;
}

/* ------------------------------------------------------- the geometry */

/* A port text's geometry per sheet: the em over the one asked for, the
   width (wx, over UI_SHEET_WIDTH), the Japanese and Korean glyphs' gap (font.c
   ui__sheet_set_kana_gap: below 0 their own advances) and the weight
   across and down.  The European sheets' is the font's as it is (em 1,
   width 1, the weight UI_MENU_BOLD_X / Y, which ctest menu_look fitted;
   their words' own fit is per item, menu_text.c).  The Japanese disc's
   lettering is thinner than Noto Sans JP at the menus' em and set
   proportionally, and its sheets space their words apart: the title's
   (title.tm2: つづきから, はじめから) loosely, the pause menu's and the
   Options screens' (option.tm2, mc04.tm2: オプション, もどる, ゲーム終了,
   ズーム, キーコンフィグ) tightly.  So the Japanese sheets have two
   geometries: UI_SHEET_JA_TITLE for the port's rows on the title screen
   (layout_ext.c, the rows settings.c marks, ui_menu_text_set_title), and
   UI_SHEET_JA everywhere else (the pause menu, the Settings pages, the
   popups).  Each is fitted to its sheet's words by ctest menu_look_jpn
   (menu_region_test.c testGeometry, ICO_MENU_LOOK_JPN_FIT=1 for the search),
   which holds every word's ink to the disc's row: the height within a
   texel, the width within 10 % on the title sheet and 17 % on the others,
   whose words are spaced unevenly (もどる and ズーム tighter than
   オプション): their geometry keeps the disc's letter widths and close
   setting, and the tight words come out up to 15 % wide.
   The Korean disc's sheets (UI_SHEET_KO) letter Hangul thinner and
   narrower than Noto Sans KR at the menus' em, set proportionally with a
   wide gap, the title's words (title.tm2) as the other screens' (option.tm2,
   mc04.tm2): one geometry, no title variant.  Measured off line on the
   decoded sheets (the fill box of menu_region_test.c geoBoxOf, grey >= 200
   and alpha >= 0.7) against an emulation of this rasteriser, embolden and
   sheet_ref.c's 1x texel (which gives the Japanese geometry's words within
   the gate as menu_look_jpn does: オプション -11 %, もどる +14 %, ズーム
   +13 %, はじめから +3 %, つづきから -3 %), searched over em 1.04 .. 1.20,
   wx 0.50 .. 0.70, gap 5 .. 10, weight 0 / 0.15 with the score of
   testGeometry for the held words of both sheets together: em 1.12, wx
   0.62, gap 7.5, no extra weight.  The disc's boxes and the port's
   (width x height): 옵션 32 x 14 / 32 x 13, 게임으로 71 x 14 / 72 x 13,
   조작 설정 81 x 13 / 78 x 13, 줌 12 x 13 / 12 x 13, 게임을 종료함 106 x 12
   / 116 x 13 (+9.4 %, mc04's condensed lettering), 새로 시작하기 116 x 13 /
   118 x 14, 불러오기 72 x 14 / 71 x 14: every height within a texel, every
   width within 10 % (the title's gate on every sheet).  Syllable ink is
   12 to 14 texels high (a final consonant reaching 14), 11 to 15 wide, 5
   to 7 apart, a space 11 to 15. */
#define JA_GEO_EM 0.98f
#define JA_GEO_WX 0.86f
#define JA_GEO_GAP 1.5f
#define JA_GEO_BOLD 0.15f
#define JA_TITLE_GEO_EM 1.06f
#define JA_TITLE_GEO_WX 0.70f
#define JA_TITLE_GEO_GAP 6.0f
#define JA_TITLE_GEO_BOLD 0.0f
#define KO_GEO_EM 1.12f
#define KO_GEO_WX 0.62f
#define KO_GEO_GAP 7.5f
#define KO_GEO_BOLD 0.0f
static MfGeo s_cjkGeo[3] = {
    {JA_GEO_EM, JA_GEO_WX, JA_GEO_GAP, JA_GEO_BOLD, JA_GEO_BOLD},
    {JA_TITLE_GEO_EM, JA_TITLE_GEO_WX, JA_TITLE_GEO_GAP, JA_TITLE_GEO_BOLD, JA_TITLE_GEO_BOLD},
    {KO_GEO_EM, KO_GEO_WX, KO_GEO_GAP, KO_GEO_BOLD, KO_GEO_BOLD},
};
static int s_titleSheet; /* ui_menu_text_set_title */

/* UI_SHEET_JA_TITLE's, UI_SHEET_KO's, else UI_SHEET_JA's */
static MfGeo *jaGeo(int sheet)
{
    return &s_cjkGeo[sheet == UI_SHEET_JA_TITLE ? 1 : sheet == UI_SHEET_KO ? 2 : 0];
}

void ui__menu_set_ja_geometry(int sheet, float em, float wx, float gap, float bx, float by)
{
    MfGeo *g = jaGeo(sheet);
    g->em = em;
    g->wx = wx;
    g->gap = gap;
    g->boldX = bx;
    g->boldY = by;
}

void ui__menu_ja_geometry(int sheet, float *em, float *wx, float *gap, float *bx, float *by)
{
    const MfGeo *g = jaGeo(sheet);
    *em = g->em;
    *wx = g->wx;
    *gap = g->gap;
    *bx = g->boldX;
    *by = g->boldY;
}

int ui_menu_text_set_title(int on)
{
    const int old = s_titleSheet;
    s_titleSheet = on != 0;
    return old;
}

/* the current sheet's (ui_sheet_language, and on the Japanese disc the
   title's or the other screens'; the Korean disc's one) */
static MfGeo sheetGeo(void)
{
    if (ui_sheet_language() == UI_SHEET_JA) {
        return *jaGeo(s_titleSheet ? UI_SHEET_JA_TITLE : UI_SHEET_JA);
    }
    if (ui_sheet_language() == UI_SHEET_KO) {
        return *jaGeo(UI_SHEET_KO);
    }
    return (MfGeo){1.0f, 1.0f, -1.0f, s_boldX, s_boldY};
}

/* the strip's letters made heavier (ui_internal.h UI_MENU_BOLD_X / Y): each
   texel gains half the strength of its two neighbours' coverage across (bx)
   and down (by), clamped; a stem or a bar grows by about the strength in
   texels, centred.  At scale s the growth is the same in sheet texels, s times
   as many strip texels: half the strength times s a side, its whole part n as
   n neighbours a side added whole (the texels within n of a letter fill) and
   its fraction as at 1x from the neighbours n + 1 away, so the edge stays one
   strip texel wide (1x: n = 0, the sum above)
 */
static void embolden(uint8_t *cov, int w, int h, int scale, float bx, float by)
{
    const float k[2] = {bx * 0.5f * (float)scale, by * 0.5f * (float)scale};
    const int n[2] = {w, h};
    uint8_t *t = malloc((size_t)(w > h ? w : h));
    if (!t) {
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (!(k[pass] > 0.0f)) {
            continue;
        }
        const int lines = pass ? w : h, len = n[pass];
        const size_t step = pass ? (size_t)w : 1u;
        for (int l = 0; l < lines; l++) {
            uint8_t *base = pass ? cov + l : cov + (size_t)l * (size_t)w;
            for (int i = 0; i < len; i++) {
                t[i] = base[(size_t)i * step];
            }
            const int n = (int)k[pass];
            const float f = k[pass] - (float)n;
            for (int i = 0; i < len; i++) {
                const int a = i > n ? t[i - n - 1] : 0, b = i + n + 1 < len ? t[i + n + 1] : 0;
                int whole = 0;
                for (int d = 1; d <= n; d++) {
                    whole += (i >= d ? t[i - d] : 0) + (i + d < len ? t[i + d] : 0);
                }
                const float v = n ? (float)(t[i] + whole) + f * (float)(a + b)
                                  : (float)t[i] + f * (float)(a + b);
                base[(size_t)i * step] = (uint8_t)(v >= 255.0f ? 255 : (int)(v + 0.5f));
            }
        }
    }
    free(t);
}

/* ---------------------------------------------------------------- items */

/* An item's words rasterised into cov (it->w x it->h bytes, zeroed by the
   caller): the strip itemStrip caches, also the comparison test's
   reference (ui__menu_strip_raster). */
static void rasterItem(const UiMenuTextItem *it, int lang, uint8_t *cov, int scale)
{
    const int w = it->w * scale, hgt = it->h * scale;
    const char *str = ui_str_in((UiLang)lang, (UiStrId)it->str);
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(str, start, len);
    float em = it->em[lang];
    const float wx = it->wx[lang], ax = it->x[lang], track = it->track[lang];
    float widths[MF_LINES], widest = 0.0f;
    for (int i = 0; i < n; i++) {
        widths[i] = ui__sheet_line_width(em, wx, track, start[i], len[i]);
        widest = widths[i] > widest ? widths[i] : widest;
    }
    /* a line longer than the rectangle is set smaller to fit, down to 60 %,
       as the port's rows are; one that fits but would cross an edge from
       its anchor is moved inside (the sheets' long words fill their
       rectangles, the anchor is the fit's centre).  The advances' side
       bearings at the line's ends are not ink. */
    const float bearings = 0.2f * em, room = (float)it->w;
    if (widest - bearings > room) {
        float k = room / (widest - bearings);
        k = k < 0.6f ? 0.6f : k;
        em *= k;
        for (int i = 0; i < n; i++) {
            widths[i] *= k;
        }
    }
    float cap = 0.0f;
    ui__sheet_v_metrics(em, NULL, NULL, NULL, &cap);
    for (int i = 0; i < n; i++) {
        float pen = ax;
        if (it->align == UI_ALIGN_CENTER) {
            pen -= widths[i] * 0.5f;
        } else if (it->align == UI_ALIGN_RIGHT) {
            pen -= widths[i];
        }
        const float lo = -0.5f * bearings, hi = room - widths[i] + 0.5f * bearings;
        pen = pen > hi ? hi : pen;
        pen = pen < lo ? lo : pen;
        const float base = it->y[lang] + (float)i * it->pitch + cap * 0.5f;
        ui__sheet_raster_line(cov, w, hgt, w, em, wx, track, pen, base, start[i], len[i], scale);
    }
    embolden(cov, w, hgt, scale, s_boldX, s_boldY);
}

int ui__menu_strip_raster(const UiMenuTextItem *it, int lang, uint8_t *out, int w, int h)
{
    /* an item's geometry is per European sheet language (menu_text.h) */
    if (!it || !out || w != it->w || h != it->h || lang < 0 || lang >= UI_LANG_PAL_COUNT) {
        return -1;
    }
    memset(out, 0, (size_t)w * (size_t)h);
    rasterItem(it, lang, out, 1);
    return 0;
}

/* ---------------------------------------------- a port text's layout */

/* A port text's strip laid out (textStrip's, and the tests' through
   ui__menu_text_raster): its lines, their pens and the first baseline,
   the strip's corner and size, sheet texels. */
typedef struct MfText {
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    int n;
    float em, wx, gap, boldX, boldY, b0, step, pen[MF_LINES];
    int ox, oy, w, h;
} MfText;

/* 0 when the strip has no size; em is the one asked for, g the sheet
   language's geometry */
static int textLayout(const char *utf8, float em, MfGeo g, unsigned layout, MfText *t)
{
    em *= g.em;
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(utf8, start, len);
    float asc = 0.0f, desc = 0.0f, step = 0.0f, cap = 0.0f;
    ui__sheet_v_metrics(em, &asc, &desc, &step, &cap);
    float b0;
    switch (layout & UI_VALIGN_MASK) {
    case UI_VALIGN_MIDDLE:
        b0 = cap * 0.5f;
        break;
    case UI_VALIGN_BASELINE:
        b0 = 0.0f;
        break;
    default:
        b0 = asc;
        break;
    }
    float pen[MF_LINES], minX = 0.0f, maxX = 0.0f;
    const float gap = ui__sheet_set_kana_gap(g.gap);
    for (int i = 0; i < n; i++) {
        const float w = ui__sheet_line_width(em, g.wx, 0.0f, start[i], len[i]);
        switch (layout & UI_ALIGN_MASK) {
        case UI_ALIGN_CENTER:
            pen[i] = -w * 0.5f;
            break;
        case UI_ALIGN_RIGHT:
            pen[i] = -w;
            break;
        default:
            pen[i] = 0.0f;
            break;
        }
        if (i == 0 || pen[i] < minX) {
            minX = pen[i];
        }
        if (i == 0 || pen[i] + w > maxX) {
            maxX = pen[i] + w;
        }
    }
    ui__sheet_set_kana_gap(gap);
    /* the glyphs' overhang past their advances (italic-free Arimo: a
       fraction of the em) and the rim */
    const float padX = ceilf(em * 2.0f * UI_X_PER_Y * 0.12f) + (float)MF_RIM_X;
    const float padY = ceilf(em * 0.12f) + (float)MF_RIM_Y;
    const int ox = (int)floorf(minX - padX), oy = (int)floorf(b0 - asc - padY);
    int w = (int)ceilf(maxX + padX) - ox;
    int hgt = (int)ceilf(b0 + (float)(n - 1) * step + desc + padY) - oy;
    w = w > MF_MAX_W ? MF_MAX_W : w;
    hgt = hgt > MF_MAX_H ? MF_MAX_H : hgt;
    for (int i = 0; i < n; i++) {
        t->start[i] = start[i];
        t->len[i] = len[i];
        t->pen[i] = pen[i];
    }
    t->n = n;
    t->em = em;
    t->wx = g.wx;
    t->gap = g.gap;
    t->boldX = g.boldX;
    t->boldY = g.boldY;
    t->b0 = b0;
    t->step = step;
    t->ox = ox;
    t->oy = oy;
    t->w = w;
    t->h = hgt;
    return w > 0 && hgt > 0;
}

/* the laid-out text rasterised at sc strip texels a sheet texel into cov
   (t->w * sc x t->h * sc bytes, zeroed) and made heavier */
static void textRaster(const MfText *t, uint8_t *cov, int sc)
{
    const int n = t->n, w = t->w, hgt = t->h, ox = t->ox, oy = t->oy;
    const float em = t->em, b0 = t->b0, step = t->step;
    const char *const *start = t->start;
    const size_t *len = t->len;
    const float *pen = t->pen;
    const float gap = ui__sheet_set_kana_gap(t->gap);
    for (int i = 0; i < n; i++) {
        ui__sheet_raster_line(cov, w * sc, hgt * sc, w * sc, em, t->wx, 0.0f, pen[i] - (float)ox,
                              b0 + (float)i * step - (float)oy, start[i], len[i], sc);
    }
    ui__sheet_set_kana_gap(gap);
    embolden(cov, w * sc, hgt * sc, sc, t->boldX, t->boldY);
}

int ui__menu_text_raster(const char *utf8, float size, unsigned layout, uint8_t **out, int *w,
                         int *h)
{
    MfText t;
    if (!out || !w || !h || !utf8 || !*utf8 || !(size > 0.0f) ||
        !textLayout(utf8, size * 0.5f, sheetGeo(), layout & (UI_ALIGN_MASK | UI_VALIGN_MASK), &t)) {
        return -1;
    }
    uint8_t *cov = calloc((size_t)t.w * (size_t)t.h, 1);
    if (!cov) {
        return -1;
    }
    textRaster(&t, cov, 1);
    *out = cov;
    *w = t.w;
    *h = t.h;
    return 0;
}

/* ---------------------------------------------------------------- inks */

#ifdef ICO_RD
/* the class of an ink and the colour it draws with; 0 when it draws
   nothing (a dark ink's glow) */
static int inkColour(int ink, int rim, const uint8_t rgba[4], int glow, int *cls, uint8_t col[4])
{
    memcpy(col, rgba, 4);
    *cls = classOf(ink, rim);
    if (ink == UI_INK_DARK) {
        if (glow) {
            return 0; /* black letters add nothing to the additive glow */
        }
        col[0] = col[1] = col[2] = 0;
    } else if (ink == UI_INK_GREY) {
        for (int c = 0; c < 3; c++) {
            col[c] = (uint8_t)((col[c] * 151 + 127) / 255);
        }
    }
    return 1;
}
#endif

int ui_menu_font_last_strip(UiMenuStrip *out)
{
    if (out && s_mf.hasLast) {
        *out = s_mf.last;
    }
    return s_mf.hasLast;
}

int ui_menu_font_is_page(uint32_t tex)
{
    for (int c = 0; tex && c < MF_CLASSES; c++) {
        for (int p = 0; p < MF_PAGES; p++) {
            if (s_mf.pages[c][p].tex == tex) {
                return 1;
            }
        }
    }
    return 0;
}

const uint8_t *ui_menu_font_page(int cls, int page, int *w, int *h)
{
    if (cls < 0 || cls >= MF_CLASSES || page < 0 || page >= MF_PAGES ||
        !s_mf.pages[cls][page].cov) {
        return NULL;
    }
    if (w) {
        *w = s_mf.pages[cls][page].w;
    }
    if (h) {
        *h = s_mf.pages[cls][page].h;
    }
    return s_mf.pages[cls][page].cov;
}

int ui_menu_font_strip_count(void)
{
    int n = 0;
    for (int i = 0; i < MF_STRIPS; i++) {
        n += s_mf.strips[i].used;
    }
    return n;
}

int ui_menu_font_fallbacks(void)
{
    return s_mf.fallbacks;
}

int ui_menu_font_shelf_reclaims(void)
{
    return s_mf.reclaims;
}

/* the counts logged at 1, 10, 100, ... (a total, not a line a frame) */
#ifdef ICO_RD
static int logCount(int n)
{
    while (n >= 10 && n % 10 == 0) {
        n /= 10;
    }
    return n == 1;
}

/* a word drawn with the plain font: counted and logged */
static void noteFallback(void)
{
    s_mf.fallbacks++;
    if (logCount(s_mf.fallbacks)) {
        fprintf(stderr,
                "ui: menu text: %d words drawn with the plain font so far (%d strips cached, "
                "%d shelves reclaimed)\n",
                s_mf.fallbacks, ui_menu_font_strip_count(), s_mf.reclaims);
    }
}
#endif

/* tests: the strips' scale set (0: mfScale's own); only the drawn path reads it */
#ifdef ICO_RD
static int s_forceScale;
#endif

void ui__menu_force_scale(int scale)
{
#ifdef ICO_RD
    s_forceScale = scale;
#else
    (void)scale;
#endif
}

#ifdef ICO_RD

/* ------------------------------------------------------------ the pages */

/* a page's size at scale sc (MF_PAGE_MAX above) */
static int pageW(int sc)
{
    return sc <= 1 ? MF_PAGE : (MF_PAGE * sc > MF_PAGE_MAX ? MF_PAGE_MAX : MF_PAGE * sc);
}

static int pageH(int sc)
{
    return sc <= 1 ? MF_PAGE : MF_PAGE * sc / 4;
}

/* the page's bytes, and its texture's rows: the rim's below the coverage
   above 1x */
static size_t pageBytes(const MfPage *p)
{
    return (size_t)p->w * (size_t)p->h * (p->scale > 1 ? 2u : 1u);
}

/* the scale the strips drawn now are rasterised at: the output's pixels a
   y unit in overlay mode (the box's height / 448), else the scene's
   texels a GS line (rd_get_scene_scale: 1 at 1x and under the CRT filter),
   to whole strip texels a sheet texel: rounded up from a quarter past
   (2.11 for 1080 lines in the "window" resolution is 2, 2.41 is 3), 1 ..
   ICO_SHEET_SCALE_MAX (4K's 4.8 is 4).  The texels per GS pixel across are
   the same at every aspect (the GS frame is as wide as the aspect asks,
   rd_get_scene_scale) and equal to those down but under the Window
   resolution, whose box keeps the PS2's pixel shape, so the vertical scale
   serves both axes. */
static int mfScale(void)
{
    if (s_forceScale > 0) {
        return s_forceScale > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX : s_forceScale;
    }
    float k = 1.0f;
    if (ui_overlay_active()) {
        float y0 = 0.0f, y1 = 0.0f;
        ui_overlay_map(0.0f, 0.0f, NULL, &y0);
        ui_overlay_map(0.0f, 1.0f, NULL, &y1);
        k = (y1 - y0) / 16.0f;
    } else {
        rd_get_scene_scale(NULL, &k);
    }
    const int sc = (int)ceilf(k - 0.25f);
    return sc < 1 ? 1 : (sc > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX : sc);
}

static void freeStrip(MfStrip *s)
{
    free(s->text);
    memset(s, 0, sizeof(*s));
}

static void destroyTex(MfPage *p)
{
    if (p->tex) {
        rd_destroy_texture((RdTex){p->tex});
        p->tex = 0;
    }
}

/* ui_font_shutdown: the pages, their textures and the strips go */
static void mfShutdown(void)
{
    for (int c = 0; c < MF_CLASSES; c++) {
        for (int p = 0; p < MF_PAGES; p++) {
            destroyTex(&s_mf.pages[c][p]);
            free(s_mf.pages[c][p].cov);
        }
    }
    for (int i = 0; i < MF_STRIPS; i++) {
        freeStrip(&s_mf.strips[i]);
    }
    memset(s_mf.pages, 0, sizeof(s_mf.pages));
    s_mf.lightLang = -1;
    s_mf.warnedFull = 0;
    s_mf.hasLast = 0;
}

/* ui_font_forget_textures: rd started again, the ids are stale; the
   coverage stays and is uploaded whole at the next draw */
static void mfForget(void)
{
    for (int c = 0; c < MF_CLASSES; c++) {
        for (int p = 0; p < MF_PAGES; p++) {
            s_mf.pages[c][p].tex = 0;
        }
    }
    s_mf.lightLang = -1;
    s_mf.hasLast = 0;
}

/* a page no recorded or replayed frame can still name */
static int evictable(const MfPage *p, uint32_t now)
{
    return now - p->lastFrame >= MF_EVICT_FRAMES;
}

/* empties a page: its strips forgotten, its coverage cleared, its texture
   destroyed (made again, whole, at its next draw) */
static void resetPage(int cls, int page)
{
    MfPage *p = &s_mf.pages[cls][page];
    for (int i = 0; i < MF_STRIPS; i++) {
        MfStrip *s = &s_mf.strips[i];
        if (s->used && s->cls == cls && s->page == page) {
            freeStrip(s);
        }
    }
    destroyTex(p);
    if (p->cov) {
        memset(p->cov, 0, pageBytes(p));
    }
    p->nShelves = 0;
}

/* a page's coverage gone too: the slot free for a page of any scale */
static void dropPage(int cls, int page)
{
    MfPage *p = &s_mf.pages[cls][page];
    resetPage(cls, page);
    free(p->cov);
    memset(p, 0, sizeof(*p));
}

/* an empty page of scale sc in a free slot; 0 without the memory */
static int newPage(MfPage *p, int sc)
{
    p->cov = calloc((size_t)pageW(sc) * (size_t)pageH(sc) * (sc > 1 ? 2u : 1u), 1);
    if (!p->cov) {
        return 0;
    }
    p->scale = sc;
    p->w = pageW(sc);
    p->h = pageH(sc);
    p->nShelves = 0;
    p->tex = 0;
    return 1;
}

static int alignUp(int v, int sc)
{
    const int a = MF_ALIGN * sc;
    return (v + a - 1) / a * a;
}

/* a w x h cell (strip texels) on page p's shelves, its corner on MF_ALIGN
   sheet texels and MF_GAP_X / MF_GAP_Y sheet texels clear of every other
   strip; *shelf its shelf.  In order: the open shelf (the last, which grows
   down to the tallest strip on it), a band an emptied shelf left
   (reclaimShelf: a strip no taller than the band, beside the strips placed
   on it since), a new shelf below the last.  A strip too wide for the open
   shelf closes it, as the single shelf cursor did, so a page whose shelves
   are never emptied is packed as before. */
static int shelfFit(MfPage *p, int w, int h, int *x, int *y, int *shelf)
{
    const int sc = p->scale, gx = MF_GAP_X * sc, gy = MF_GAP_Y * sc;
    const int n = p->nShelves;
    MfShelf *last = n > 0 ? &p->shelves[n - 1] : NULL;
    int i = -1, across = 0;
    if (last) {
        across = last->x + w + gx > p->w;
        if (!across && last->y + h + gy <= p->h) {
            i = n - 1;
        }
    }
    for (int j = 0; i < 0 && j < n - 1; j++) {
        const MfShelf *b = &p->shelves[j];
        if (h <= b->h && b->x + w + gx <= p->w) {
            i = j;
        }
    }
    if (i < 0) {
        if (across) {
            last->x = p->w; /* closed: nothing more goes beside its strips */
        }
        const int ny = last ? alignUp(last->y + last->h + gy, sc) : MF_ORIGIN * sc;
        if (n >= MF_SHELVES || ny + h + gy > p->h) {
            return 0;
        }
        i = p->nShelves++;
        p->shelves[i] = (MfShelf){ny, 0, MF_ORIGIN * sc, 0, 0};
    }
    MfShelf *s = &p->shelves[i];
    *x = s->x;
    *y = s->y;
    *shelf = i;
    s->x = alignUp(s->x + w + gx, sc);
    if (h > s->h) {
        s->h = h; /* the last shelf only: a band's strips are no taller than it */
    }
    s->strips++;
    s->lastFrame = rd_frame_number();
    return 1;
}

/* Empties shelf i of a style's page: its strips forgotten, its rows of
   coverage cleared across the page and, above 1x, the rim's rows of the
   strips' sheet texels and the one round them that placeCoverage wrote
   ([y - scale, y + h + scale): the shelves' MF_GAP_Y of 5 sheet texels
   keeps those clear of a neighbour's, which reach a sheet texel past its
   strips), the same rows of the texture replaced (whole rows of the page
   are contiguous; rd uploads the union of the changed rows at the next
   replay).  The texture stays.  The band keeps its rows for strips no
   taller (shelfFit); empty shelves at the bottom are dropped, so the open
   shelf is the last one with strips. */
static void reclaimShelf(int cls, int page, int i)
{
    MfPage *p = &s_mf.pages[cls][page];
    MfShelf *b = &p->shelves[i];
    const int sc = p->scale;
    for (int k = 0; k < MF_STRIPS; k++) {
        MfStrip *s = &s_mf.strips[k];
        if (s->used && s->cls == cls && s->page == page && s->shelf == i) {
            freeStrip(s);
        }
    }
    if (b->h > 0) {
        uint8_t *rows = p->cov + (size_t)b->y * (size_t)p->w;
        memset(rows, 0, (size_t)b->h * (size_t)p->w);
        if (p->tex) {
            rd_update_texture_rect((RdTex){p->tex}, 0, (uint32_t)b->y, (uint32_t)p->w,
                                   (uint32_t)b->h, rows);
        }
        if (sc > 1) {
            const int ry = b->y - sc, rh = b->h + 2 * sc;
            uint8_t *rim = p->cov + (size_t)p->w * (size_t)p->h + (size_t)ry * (size_t)p->w;
            memset(rim, 0, (size_t)rh * (size_t)p->w);
            if (p->tex) {
                rd_update_texture_rect((RdTex){p->tex}, 0, (uint32_t)(p->h + ry), (uint32_t)p->w,
                                       (uint32_t)rh, rim);
            }
        }
    }
    b->x = MF_ORIGIN * sc;
    b->strips = 0;
    while (p->nShelves > 0 && p->shelves[p->nShelves - 1].strips == 0) {
        p->nShelves--;
    }
    s_mf.reclaims++;
    if (logCount(s_mf.reclaims)) {
        fprintf(stderr,
                "ui: menu text: %d shelves reclaimed so far (%d strips cached, %d words drawn "
                "with the plain font)\n",
                s_mf.reclaims, ui_menu_font_strip_count(), s_mf.fallbacks);
    }
}

/* Every shelf with strips that no recorded or replayed frame can still
   name (none drawn for MF_EVICT_FRAMES frames: evictable's rule at a
   shelf's grain) on a style's pages of scale sc (0 any) emptied; returns
   how many.  A page whose words change every tick (a counter beside the
   rows that stay) so keeps its live rows where they are and takes the new
   strips in the bands the old ones left. */
static int reclaimStale(int cls, int sc, uint32_t now)
{
    int n = 0;
    for (int pg = 0; pg < MF_PAGES; pg++) {
        MfPage *p = &s_mf.pages[cls][pg];
        if (!p->cov || (sc && p->scale != sc)) {
            continue;
        }
        for (int i = p->nShelves - 1; i >= 0; i--) {
            const MfShelf *b = &p->shelves[i];
            if (i < p->nShelves && b->strips > 0 && now - b->lastFrame >= MF_EVICT_FRAMES) {
                reclaimShelf(cls, pg, i);
                n++;
            }
        }
    }
    return n;
}

/* the page drawn least recently of a style (of scale sc; 0 any) that no
   frame can still name: reset and returned; -1 when every page is in use */
static int evictOne(int cls, int sc, uint32_t now)
{
    int best = -1;
    for (int pg = 0; pg < MF_PAGES; pg++) {
        const MfPage *p = &s_mf.pages[cls][pg];
        if (p->cov && (!sc || p->scale == sc) && evictable(p, now) &&
            (best < 0 || p->lastFrame < s_mf.pages[cls][best].lastFrame)) {
            best = pg;
        }
    }
    if (best >= 0) {
        resetPage(cls, best);
    }
    return best;
}

/* room for a w x h strip (strip texels) of scale sc in a style's pages, in
   order: a shelf of a page of that scale in use; the same after the stale
   shelves of those pages are emptied (reclaimStale); a new page (in a free
   slot or one of another scale that no frame can still name); the least
   recently drawn page of the scale reset.  Only when every shelf of every
   page holds a strip some frame still draws is nothing found */
static int allocCell(int cls, int sc, int w, int h, int *page, int *x, int *y, int *shelf)
{
    if (w > pageW(sc) - (MF_ORIGIN + MF_GAP_X) * sc ||
        h > pageH(sc) - (MF_ORIGIN + MF_GAP_Y) * sc) {
        return 0;
    }
    const uint32_t now = rd_frame_number();
    for (int pass = 0; pass < 2; pass++) {
        for (int pg = 0; pg < MF_PAGES; pg++) {
            MfPage *p = &s_mf.pages[cls][pg];
            if (p->cov && p->scale == sc && shelfFit(p, w, h, x, y, shelf)) {
                *page = pg;
                return 1;
            }
        }
        if (pass > 0 || reclaimStale(cls, sc, now) == 0) {
            break;
        }
    }
    for (int pg = 0; pg < MF_PAGES; pg++) {
        MfPage *p = &s_mf.pages[cls][pg];
        if (p->cov && p->scale != sc && evictable(p, now)) {
            dropPage(cls, pg);
        }
        if (!p->cov) {
            if (!newPage(p, sc)) {
                return 0;
            }
            if (shelfFit(p, w, h, x, y, shelf)) {
                *page = pg;
                return 1;
            }
        }
    }
    const int pg = evictOne(cls, sc, now);
    if (pg >= 0 && shelfFit(&s_mf.pages[cls][pg], w, h, x, y, shelf)) {
        *page = pg;
        return 1;
    }
    if (!s_mf.warnedFull) {
        s_mf.warnedFull = 1;
        fprintf(stderr, "ui: the menu text pages are full; words that do not fit are drawn "
                        "with the plain font until a page is free\n");
    }
    return 0;
}

/* a free strip slot; when every slot is taken, the shelves of every style
   and scale that no frame can still name are emptied (their strips with
   them), else a page that no frame can still name is reset */
static MfStrip *newStrip(void)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < MF_STRIPS; i++) {
            if (!s_mf.strips[i].used) {
                return &s_mf.strips[i];
            }
        }
        if (pass > 0) {
            break;
        }
        const uint32_t now = rd_frame_number();
        int freed = 0;
        for (int c = 0; c < MF_CLASSES; c++) {
            freed += reclaimStale(c, 0, now);
        }
        for (int c = 0; c < MF_CLASSES && !freed; c++) {
            freed = evictOne(c, 0, now) >= 0;
        }
        if (!freed) {
            break;
        }
    }
    if (!s_mf.warnedFull) {
        s_mf.warnedFull = 1;
        fprintf(stderr, "ui: the menu text strips are full; words that do not fit are drawn "
                        "with the plain font until a strip is free\n");
    }
    return NULL;
}

/* the strip's coverage (w x h bytes) into its page, and onto the page's
   texture when it has one */
static void placeCoverage(const MfStrip *s, const uint8_t *cov)
{
    MfPage *p = &s_mf.pages[s->cls][s->page];
    const int w = s->w * s->scale, h = s->h * s->scale;
    for (int r = 0; r < h; r++) {
        memcpy(p->cov + (size_t)(s->y + r) * (size_t)p->w + (size_t)s->x,
               cov + (size_t)r * (size_t)w, (size_t)w);
    }
    if (p->tex) {
        rd_update_texture_rect((RdTex){p->tex}, (uint32_t)s->x, (uint32_t)s->y, (uint32_t)w,
                               (uint32_t)h, cov);
    }
    if (p->scale > 1) {
        /* the rim below, over the strip and the sheet texel round it that
           the magnified rim's bilinear read takes (one value a
           sheet texel; the gaps keep other strips' rims out) */
        uint8_t *rim = p->cov + (size_t)p->w * (size_t)p->h;
        const int sc = p->scale;
        const int rx = s->x - sc, ry = s->y - sc, rw = w + 2 * sc, rh = h + 2 * sc;
        rd_sheet_rim(p->cov, (uint32_t)p->w, (uint32_t)p->h, (uint32_t)p->scale, rx, ry, rw, rh,
                     rim);
        if (p->tex) {
            uint8_t *px = malloc((size_t)rw * (size_t)rh);
            if (px) {
                for (int r = 0; r < rh; r++) {
                    memcpy(px + (size_t)r * (size_t)rw,
                           rim + (size_t)(ry + r) * (size_t)p->w + (size_t)rx, (size_t)rw);
                }
                rd_update_texture_rect((RdTex){p->tex}, (uint32_t)rx, (uint32_t)(p->h + ry),
                                       (uint32_t)rw, (uint32_t)rh, px);
                free(px);
            }
        }
    }
}

/* the page's texture, made whole from the coverage at its first draw, in
   its style's current ink */
static uint32_t pageTex(int cls, int page, int lang)
{
    MfPage *p = &s_mf.pages[cls][page];
    if (!p->tex && p->cov) {
        const UiSheetInk *k = classInk(cls, lang);
        const RdSheetStyle st = {k->rimOn,  k->rimLevel,  k->fillLevel,
                                 k->dither, k->rimWeight, (uint8_t)p->scale};
        static const char *const kName[MF_CLASSES] = {"light", "faint", "plain"};
        char name[32];
        if (p->scale > 1) {
            snprintf(name, sizeof(name), "ui menu text %s p%d %dx", kName[cls], page, p->scale);
        } else {
            snprintf(name, sizeof(name), "ui menu text %s p%d", kName[cls], page);
        }
        p->tex = rd_create_texture_sheet((uint32_t)p->w, (uint32_t)(p->scale > 1 ? 2 * p->h : p->h),
                                         p->cov, &st, name)
                     .id;
        ui__set_menu_font_hooks(mfShutdown, mfForget);
    }
    p->lastFrame = rd_frame_number();
    return p->tex;
}

/* a strip drawn now: its shelf too (reclaimShelf leaves a shelf that some
   frame still draws); pageTex marks the page */
static void touchStrip(const MfStrip *s)
{
    MfPage *p = &s_mf.pages[s->cls][s->page];
    if (s->shelf >= 0 && s->shelf < p->nShelves) {
        p->shelves[s->shelf].lastFrame = rd_frame_number();
    }
}

/* the light and faint pages restyled for lang (its rim and fill levels):
   read at the replay, so nothing is rasterised again */
static void styleLight(int lang)
{
    if (lang == s_mf.lightLang) {
        return;
    }
    for (int c = MF_LIGHT; c <= MF_FAINT; c++) {
        const UiSheetInk *k = classInk(c, lang);
        for (int p = 0; p < MF_PAGES; p++) {
            if (s_mf.pages[c][p].tex) {
                const RdSheetStyle st = {k->rimOn,  k->rimLevel,  k->fillLevel,
                                         k->dither, k->rimWeight, (uint8_t)s_mf.pages[c][p].scale};
                rd_set_texture_sheet_style((RdTex){s_mf.pages[c][p].tex}, &st);
            }
        }
    }
    s_mf.lightLang = lang;
}

static uint64_t hashBytes(uint64_t h, const void *data, size_t n)
{
    const uint8_t *b = data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    return h;
}

/* ------------------------------------------------------- an item's strip */

static MfStrip *itemStrip(const UiMenuTextItem *it, int item, int lang, int cls, int sc)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = hashBytes(h, &item, sizeof(item));
    h = hashBytes(h, &lang, sizeof(lang));
    h = hashBytes(h, &cls, sizeof(cls));
    if (sc > 1) {
        h = hashBytes(h, &sc, sizeof(sc));
    }
    for (int i = 0; i < MF_STRIPS; i++) {
        MfStrip *s = &s_mf.strips[i];
        if (s->used && s->hash == h && !s->text && s->item == item && s->lang == lang &&
            s->cls == cls && s->scale == sc) {
            touchStrip(s);
            return s;
        }
    }
    const int w = it->w, hgt = it->h;
    MfStrip *s = newStrip();
    int page, x, y, shelf;
    if (!s || !allocCell(cls, sc, w * sc, hgt * sc, &page, &x, &y, &shelf)) {
        return NULL;
    }
    uint8_t *cov = calloc((size_t)(w * sc) * (size_t)(hgt * sc), 1);
    if (!cov) {
        return NULL;
    }
    rasterItem(it, lang, cov, sc);
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->hash = h;
    s->item = item;
    s->lang = lang;
    s->em = it->em[lang];
    s->scale = sc;
    s->cls = cls;
    s->page = page;
    s->x = x;
    s->y = y;
    s->shelf = shelf;
    s->w = w;
    s->h = hgt;
    placeCoverage(s, cov);
    touchStrip(s);
    free(cov);
    return s;
}

/* ---------------------------------------------------- a port text's strip */

static MfStrip *textStrip(const char *utf8, float em, unsigned layout, int cls, int sc)
{
    /* the sheet language's geometry is part of the strip (a language
       change draws its words anew) */
    const MfGeo g = sheetGeo();
    uint64_t h = 0xCBF29CE484222325ull;
    h = hashBytes(h, utf8, strlen(utf8));
    h = hashBytes(h, &em, sizeof(em));
    h = hashBytes(h, &g, sizeof(g));
    h = hashBytes(h, &layout, sizeof(layout));
    h = hashBytes(h, &cls, sizeof(cls));
    if (sc > 1) {
        h = hashBytes(h, &sc, sizeof(sc));
    }
    for (int i = 0; i < MF_STRIPS; i++) {
        MfStrip *s = &s_mf.strips[i];
        if (s->used && s->hash == h && s->text && s->cls == cls && s->scale == sc && s->em == em &&
            memcmp(&s->geo, &g, sizeof(g)) == 0 && s->layout == layout &&
            strcmp(s->text, utf8) == 0) {
            touchStrip(s);
            return s;
        }
    }
    MfText t;
    if (!textLayout(utf8, em, g, layout, &t)) {
        return NULL;
    }
    const int w = t.w, hgt = t.h, ox = t.ox, oy = t.oy;
    MfStrip *s = newStrip();
    int page, x, y, shelf;
    if (!s || !allocCell(cls, sc, w * sc, hgt * sc, &page, &x, &y, &shelf)) {
        return NULL;
    }
    uint8_t *cov = calloc((size_t)(w * sc) * (size_t)(hgt * sc), 1);
    char *copy = malloc(strlen(utf8) + 1);
    if (!cov || !copy) {
        free(cov);
        free(copy);
        return NULL;
    }
    strcpy(copy, utf8);
    textRaster(&t, cov, sc);
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->hash = h;
    s->text = copy;
    s->item = -1;
    s->lang = -1;
    s->em = em;
    s->geo = g;
    s->layout = layout;
    s->scale = sc;
    s->cls = cls;
    s->page = page;
    s->x = x;
    s->y = y;
    s->shelf = shelf;
    s->w = w;
    s->h = hgt;
    s->ox = ox;
    s->oy = oy;
    placeCoverage(s, cov);
    touchStrip(s);
    free(cov);
    return s;
}

static void noteLast(const MfStrip *s, uint32_t tex, float ax, float ay)
{
    s_mf.last.anchorX = ax;
    s_mf.last.anchorY = ay;
    s_mf.last.cls = s->cls;
    s_mf.last.page = s->page;
    s_mf.last.x = s->x;
    s_mf.last.y = s->y;
    s_mf.last.w = s->w * s->scale;
    s_mf.last.h = s->h * s->scale;
    s_mf.last.scale = s->scale;
    s_mf.last.tex = tex;
    s_mf.hasLast = 1;
}

#endif /* ICO_RD */

/* ---------------------------------------------------------------- draws */

void ui_draw_menu_text(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                       unsigned flags, int ink, const UiXform *xf)
{
#ifdef ICO_RD
    int cls;
    uint8_t col[4];
    if (!utf8 || !*utf8 || !(size > 0.0f) || !ui_font_init() ||
        !inkColour(ink, UI_RIM_FULL, rgba, (flags & UI_ADDITIVE) != 0, &cls, col)) {
        return;
    }
    int lang = ui_sheet_language();
    lang = lang >= 0 && lang < UI_LANG_SHEET_COUNT ? lang : UI_LANG_EN;
    if (cls != MF_PLAIN) {
        styleLight((int)lang);
    }
    const MfStrip *s =
        textStrip(utf8, size * 0.5f, flags & (UI_ALIGN_MASK | UI_VALIGN_MASK), cls, mfScale());
    if (!s) {
        /* every shelf of every page of the style holds words a recorded
           frame still draws: the plain glyphs rather than nothing, so a
           screen whose strings change every tick never goes blank */
        noteFallback();
        ui_draw_text_xf(x, y, size, col, utf8, flags & ~(unsigned)UI_KEEP_STATE, xf);
        return;
    }
    const uint32_t tex = pageTex(s->cls, s->page, (int)lang);
    if (!tex) {
        return;
    }
    /* the anchor on whole texels: a column an x unit, a row two y units */
    const float ax = roundf(x), ay = 2.0f * roundf(y * 0.5f);
    UiTexQuad q;
    q.x0 = ax + (float)s->ox;
    q.y0 = ay + 2.0f * (float)s->oy;
    q.x1 = q.x0 + (float)s->w;
    q.y1 = q.y0 + 2.0f * (float)s->h;
    /* the whole strip, scale times the quad's texels */
    q.u0 = (float)s->x;
    q.v0 = (float)s->y;
    q.u1 = (float)(s->x + s->w * s->scale);
    q.v1 = (float)(s->y + s->h * s->scale);
    noteLast(s, tex, ax, ay);
    ui__draw_tex_quads(tex, &q, 1, col, flags, xf, ui__text_key(utf8, flags, s->page));
#else
    (void)x;
    (void)y;
    (void)size;
    (void)rgba;
    (void)utf8;
    (void)flags;
    (void)ink;
    (void)xf;
#endif
}

#ifdef ICO_RD
/* ui_menu_word_draw with no room in the pages: the item's lines in the plain
   glyphs at the item's anchor, em and pitch, mapped through the sprite's
   box (the glow pass draws nothing) */
static void itemFallback(const UiMenuTextItem *it, int lang, const int box[4], const int uv[4],
                         const uint8_t col[4], int glow)
{
    const char *text = ui_str_in((UiLang)lang, (UiStrId)it->str);
    if (glow || !text || !*text) {
        return;
    }
    /* grid units per item texel, and the item's corner on the grid */
    const float sx = ((float)box[2] / 16.0f) / ((float)uv[2] / 16.0f);
    const float sy = ((float)box[3] / 8.0f) / ((float)uv[3] / 16.0f);
    const float x0 =
        (float)box[0] / 16.0f + UI_GRID_CX - ((float)uv[0] / 16.0f - (float)it->u) * sx;
    const float y0 = (float)box[1] / 8.0f + UI_GRID_CY - ((float)uv[1] / 16.0f - (float)it->v) * sy;
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(text, start, len);
    for (int i = 0; i < n; i++) {
        char line[256];
        const size_t k = len[i] < sizeof(line) - 1 ? len[i] : sizeof(line) - 1;
        memcpy(line, start[i], k);
        line[k] = 0;
        ui_draw_text(x0 + it->x[lang] * sx, y0 + (it->y[lang] + (float)i * it->pitch) * sy,
                     it->em[lang] * sy, col, line, (unsigned)it->align | UI_VALIGN_MIDDLE);
    }
}
#endif

void ui_menu_word_draw(const UiMenuTextItem *it, int lang, const int box[4], const int uv[4],
                       const unsigned char rgba[4], int glow)
{
#ifdef ICO_RD
    int cls;
    uint8_t col[4];
    if (!it || !box || !uv || uv[2] <= 0 || uv[3] <= 0 || !ui_font_init() ||
        !inkColour(it->ink, it->rim[lang >= 0 && lang < UI_LANG_PAL_COUNT ? lang : UI_LANG_EN],
                   rgba, glow, &cls, col)) {
        return;
    }
    /* an item is a European sheet's word: its geometry per those languages */
    lang = lang >= 0 && lang < UI_LANG_PAL_COUNT ? lang : UI_LANG_EN;
    if (cls != MF_PLAIN) {
        styleLight(lang);
    }
    const int item = it >= ui_menu_text_items && it < ui_menu_text_items + ui_menu_text_item_count
                         ? (int)(it - ui_menu_text_items)
                         : -2;
    const MfStrip *s = itemStrip(it, item, lang, cls, mfScale());
    if (!s) {
        if (!glow) {
            noteFallback(); /* the glow pass of the same word draws nothing */
        }
        itemFallback(it, lang, box, uv, col, glow);
        return;
    }
    const uint32_t tex = pageTex(s->cls, s->page, lang);
    if (!tex) {
        return;
    }
    /* the sprite's box on the grid (x 1/16 pixel, y 1/16 field line from
       the centre: two y units a field line) and its texels moved from the
       item's rectangle on the sheet to the strip's on the page */
    UiTexQuad q;
    q.x0 = (float)box[0] / 16.0f + UI_GRID_CX;
    q.y0 = (float)box[1] / 8.0f + UI_GRID_CY;
    q.x1 = q.x0 + (float)box[2] / 16.0f;
    q.y1 = q.y0 + (float)box[3] / 8.0f;
    if (s->scale > 1) {
        /* the strip's texels scale times the item's */
        const float k = (float)s->scale;
        q.u0 = (float)s->x + ((float)uv[0] / 16.0f - (float)it->u) * k;
        q.v0 = (float)s->y + ((float)uv[1] / 16.0f - (float)it->v) * k;
        q.u1 = q.u0 + (float)uv[2] / 16.0f * k;
        q.v1 = q.v0 + (float)uv[3] / 16.0f * k;
    } else {
        q.u0 = (float)s->x + (float)uv[0] / 16.0f - (float)it->u;
        q.v0 = (float)s->y + (float)uv[1] / 16.0f - (float)it->v;
        q.u1 = q.u0 + (float)uv[2] / 16.0f;
        q.v1 = q.v0 + (float)uv[3] / 16.0f;
    }
    /* the packet's state (the row's blend, the glow's additive one) */
    const unsigned flags = UI_KEEP_STATE | (glow ? UI_ADDITIVE : 0u);
    noteLast(s, tex, q.x0, q.y0);
    ui__draw_tex_quads(tex, &q, 1, col, flags, NULL,
                       ui__text_key(ui_str_in((UiLang)lang, (UiStrId)it->str), flags, s->page));
#else
    (void)it;
    (void)lang;
    (void)box;
    (void)uv;
    (void)rgba;
    (void)glow;
#endif
}
