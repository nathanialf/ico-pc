/*
 * port/ui/menu_font.c
 *
 * The menus' text in the sheets' look (menu_font.h): the strip cache, the
 * inks and the draws.  font.c rasterises (ui__SheetRasterLine, the only
 * stb compile unit) and emits the sprites (ui__DrawTexQuads).
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
/* v0.4.2 (package F-G): the strips are rasterised at s strip texels a sheet
   texel (mfScale: the scene's scale, or the output's in overlay mode, 1 ..
   ICO_SHEET_SCALE_MAX), so the letters are as crisp as the picture; the
   gaps, the alignment and the first corner above are in sheet texels (s
   times as many strip texels).  A page holds strips of one scale: 1024 x
   1024 at 1x; above it 1024 s x 256 s of coverage, at most 4096 wide, and
   below it as many texels of rim (rd.h rd_SheetRim: the 1x rim of the
   sheet texels, one value over each sheet texel's texels, which the shader
   magnifies; v0.4.3 RIM), so the
   texture is 2048 x 1024 at 2x, 3072 x 1536 at 3x, 4096 x 2048 at 4x (2,
   4.5 and 8 MB, kept on the CPU and in the texture; at most MF_PAGES of a
   style, three styles: 96 MB of textures at 4x if every page of every style
   were in use, two pages, 16 MB, for a menu).  A page is 1009 sheet texels
   wide at every scale (the longest notes fit) and 243 tall (the 1x page
   1011: a menu's rows are 15 to 30 texels).
   Pages of another scale than a draw's are emptied once no frame can name
   them (MF_EVICT_FRAMES), so a resolution change re-rasterises the words
   it shows within a few frames and a popup on the output (a scale of its
   own) can share a style's pages with the scene's words. */
#define MF_PAGE_MAX 4096
#define MF_PAGES 4        /* pages per style */
#define MF_STRIPS 1024    /* strips cached at once */
#define MF_EVICT_FRAMES 4 /* rd frames a page must have gone undrawn before it is reset */
#define MF_LINES 16       /* lines of one text */
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

   Measured on the game's sheets (package F-C1: the PAL sheets menu_PAL_01..04,
   scei and title, the 79 light-ink items of the menu text table, texels as
   (grey, alpha) with the GS alpha 0x80 = 1; ctest menu_look testSurvey
   re-measures them and pins this table, RX, RY and LEVELS):
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
            matched the sheets better once the geometry was fitted
            (shader_consts.h).
   The first run of the comparison test on main kept them: its survey
   re-measures these numbers within 0.1.  (Its ink fit ran to the edges of
   its grid then, lowering the contrast because the strips' letters sat
   beside the sheets': Arimo was set 1.25 times too wide, see ui_internal.h
   UI_SHEET_WIDTH.) */
static const UiSheetInk kSheetInk[UI_LANG_COUNT] = {
    {1, 24, 255, 1, 0}, /* English */
    {1, 61, 255, 1, 0}, /* French */
    {1, 56, 255, 1, 0}, /* German */
    {1, 61, 255, 1, 0}, /* Italian */
    {1, 62, 255, 1, 0}, /* Spanish */
};
/* the dark, plain and grey inks and the light words without a halo: no
   rim, a white fill (the vertex colour makes them black, white or grey) */
static const UiSheetInk kPlainInk = {0, 0, 255, 1, 0};

typedef struct MfPage {
    uint8_t *cov; /* w x h coverage, NULL while unused; above 1x the rim
                     below it (w x h more) */
    int scale;    /* strip texels a sheet texel (F-G) */
    int w, h;     /* the coverage's texels (pageW, pageH); the texture is
                     twice as tall above 1x */
    uint32_t tex; /* the rd sheet texture, 0 until first drawn */
    int shelfX, shelfY, shelfH;
    uint32_t lastFrame; /* rd_FrameNumber at its last draw */
} MfPage;

typedef struct MfStrip {
    int used;
    uint64_t hash;
    char *text;          /* a port text's string; NULL for an item's */
    int item, lang;      /* an item's index and language; -1 for a port text */
    float em;            /* texel rows */
    unsigned layout;     /* a port text's alignment flags */
    int scale;           /* strip texels a sheet texel (F-G) */
    int cls, page, x, y; /* the page's texels */
    int w, h;            /* sheet texels (w * scale x h * scale on the page) */
    int ox, oy;          /* a port text's top-left from its snapped anchor: texels, rows */
} MfStrip;

static struct {
    MfPage pages[MF_CLASSES][MF_PAGES];
    MfStrip strips[MF_STRIPS];
    int lightLang; /* the language the light pages are styled for; -1 none */
    int warnedFull;
    int hasLast;
    UiMenuStrip last;
} s_mf = {.lightLang = -1};

const UiSheetInk *ui_MenuSheetInk(int lang)
{
    return &kSheetInk[lang >= 0 && lang < UI_LANG_COUNT ? lang : UI_LANG_EN];
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
    static UiSheetInk faint[UI_LANG_COUNT];
    if (cls == MF_PLAIN) {
        return &kPlainInk;
    }
    const UiSheetInk *k = ui_MenuSheetInk(lang);
    if (cls == MF_LIGHT) {
        return k;
    }
    const int l = (int)(k - kSheetInk);
    faint[l] = *k;
    faint[l].rimWeight = UI_MENU_FAINT_WEIGHT;
    return &faint[l];
}

const UiSheetInk *ui_MenuItemInk(int ink, int rim, int lang)
{
    return classInk(classOf(ink, rim), lang);
}

/* ------------------------------------------------------------- measuring */

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

float ui_MeasureMenuText(float size, const char *utf8)
{
    if (!utf8) {
        return 0.0f;
    }
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(utf8, start, len);
    float widest = 0.0f;
    for (int i = 0; i < n; i++) {
        const float w = ui__SheetLineWidth(size * 0.5f, 1.0f, 0.0f, start[i], len[i]);
        widest = w > widest ? w : widest;
    }
    return widest;
}

void ui_MenuFontMetrics(float size, float *ascent, float *descent, float *capHeight)
{
    float a = 0.0f, d = 0.0f, c = 0.0f;
    ui__SheetVMetrics(size * 0.5f, &a, &d, NULL, &c);
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

void ui__MenuSetBold(float bx, float by)
{
    s_boldX = bx;
    s_boldY = by;
}

/* the strip's letters made heavier (ui_internal.h UI_MENU_BOLD_X / Y): each texel gains half the strength of
   its two neighbours' coverage across (bx) and down (by), clamped; a stem
   or a bar grows by about the strength in texels, centred.  At scale s
   (F-G) the growth is the same in sheet texels, s times as many strip
   texels: half the strength times s a side, its whole part n as n
   neighbours a side added whole (the texels within n of a letter fill) and
   its fraction as at 1x from the neighbours n + 1 away, so the edge stays
   one strip texel wide (1x: n = 0, the sum above) */
static void embolden(uint8_t *cov, int w, int h, int scale)
{
    const float k[2] = {s_boldX * 0.5f * (float)scale, s_boldY * 0.5f * (float)scale};
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
   reference (ui__MenuStripRaster). */
static void rasterItem(const UiMenuTextItem *it, int lang, uint8_t *cov, int scale)
{
    const int w = it->w * scale, hgt = it->h * scale;
    const char *str = ui_StrIn((UiLang)lang, (UiStrId)it->str);
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(str, start, len);
    float em = it->em[lang];
    const float wx = it->wx[lang], ax = it->x[lang], track = it->track[lang];
    float widths[MF_LINES], widest = 0.0f;
    for (int i = 0; i < n; i++) {
        widths[i] = ui__SheetLineWidth(em, wx, track, start[i], len[i]);
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
    ui__SheetVMetrics(em, NULL, NULL, NULL, &cap);
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
        ui__SheetRasterLine(cov, w, hgt, w, em, wx, track, pen, base, start[i], len[i], scale);
    }
    embolden(cov, w, hgt, scale);
}

int ui__MenuStripRaster(const UiMenuTextItem *it, int lang, uint8_t *out, int w, int h)
{
    if (!it || !out || w != it->w || h != it->h || lang < 0 || lang >= UI_LANG_COUNT) {
        return -1;
    }
    memset(out, 0, (size_t)w * (size_t)h);
    rasterItem(it, lang, out, 1);
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

int ui_MenuFontLastStrip(UiMenuStrip *out)
{
    if (out && s_mf.hasLast) {
        *out = s_mf.last;
    }
    return s_mf.hasLast;
}

int ui_MenuFontIsPage(uint32_t tex)
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

const uint8_t *ui_MenuFontPage(int cls, int page, int *w, int *h)
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

int ui_MenuFontStripCount(void)
{
    int n = 0;
    for (int i = 0; i < MF_STRIPS; i++) {
        n += s_mf.strips[i].used;
    }
    return n;
}

/* tests: the strips' scale set (0: mfScale's own); only the drawn path reads it */
#ifdef ICO_RD
static int s_forceScale;
#endif

void ui__MenuForceScale(int scale)
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
   texels a GS line (rd_GetSceneScale: 1 at 1x and under the CRT filter),
   to whole strip texels a sheet texel: rounded up from a quarter past
   (2.11 for 1080 lines in the "window" resolution is 2, 2.41 is 3), 1 ..
   ICO_SHEET_SCALE_MAX (4K's 4.8 is 4).  The texels per GS pixel across are
   the same in the 4:3 picture the menus are drawn in, so one scale serves
   both axes. */
static int mfScale(void)
{
    if (s_forceScale > 0) {
        return s_forceScale > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX : s_forceScale;
    }
    float k = 1.0f;
    if (ui_OverlayActive()) {
        float y0 = 0.0f, y1 = 0.0f;
        ui_OverlayMap(0.0f, 0.0f, NULL, &y0);
        ui_OverlayMap(0.0f, 1.0f, NULL, &y1);
        k = (y1 - y0) / 16.0f;
    } else {
        rd_GetSceneScale(NULL, &k);
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
        rd_DestroyTexture((RdTex){p->tex});
        p->tex = 0;
    }
}

/* ui_FontShutdown: the pages, their textures and the strips go */
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

/* ui_FontForgetTextures: rd started again, the ids are stale; the
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
    const int sc = p->scale > 0 ? p->scale : 1;
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
    p->shelfX = p->shelfY = MF_ORIGIN * sc;
    p->shelfH = 0;
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
    p->shelfX = p->shelfY = MF_ORIGIN * sc;
    p->shelfH = 0;
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
   strip */
static int shelfFit(MfPage *p, int w, int h, int *x, int *y)
{
    const int sc = p->scale, gx = MF_GAP_X * sc, gy = MF_GAP_Y * sc;
    if (p->shelfX + w + gx > p->w) {
        p->shelfY = alignUp(p->shelfY + p->shelfH + gy, sc);
        p->shelfX = MF_ORIGIN * sc;
        p->shelfH = 0;
    }
    if (p->shelfY + h + gy > p->h) {
        return 0;
    }
    *x = p->shelfX;
    *y = p->shelfY;
    p->shelfX = alignUp(p->shelfX + w + gx, sc);
    if (h > p->shelfH) {
        p->shelfH = h;
    }
    return 1;
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

/* room for a w x h strip (strip texels) of scale sc in a style's pages: a
   shelf of a page of that scale in use, a new page (in a free slot or one
   of another scale that no frame can still name), or the least recently
   drawn page of the scale reset */
static int allocCell(int cls, int sc, int w, int h, int *page, int *x, int *y)
{
    if (w > pageW(sc) - (MF_ORIGIN + MF_GAP_X) * sc ||
        h > pageH(sc) - (MF_ORIGIN + MF_GAP_Y) * sc) {
        return 0;
    }
    const uint32_t now = rd_FrameNumber();
    for (int pg = 0; pg < MF_PAGES; pg++) {
        MfPage *p = &s_mf.pages[cls][pg];
        if (p->cov && p->scale == sc && shelfFit(p, w, h, x, y)) {
            *page = pg;
            return 1;
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
            if (shelfFit(p, w, h, x, y)) {
                *page = pg;
                return 1;
            }
        }
    }
    const int pg = evictOne(cls, sc, now);
    if (pg >= 0 && shelfFit(&s_mf.pages[cls][pg], w, h, x, y)) {
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

/* a free strip slot; when every slot is taken, a page that no frame can
   still name is reset (its strips with it) */
static MfStrip *newStrip(void)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < MF_STRIPS; i++) {
            if (!s_mf.strips[i].used) {
                return &s_mf.strips[i];
            }
        }
        const uint32_t now = rd_FrameNumber();
        int freed = 0;
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
        rd_UpdateTextureRect((RdTex){p->tex}, (uint32_t)s->x, (uint32_t)s->y, (uint32_t)w,
                             (uint32_t)h, cov);
    }
    if (p->scale > 1) {
        /* the rim below, over the strip and the sheet texel round it that
           the magnified rim's bilinear read takes (v0.4.3 RIM: one value a
           sheet texel; the gaps keep other strips' rims out) */
        uint8_t *rim = p->cov + (size_t)p->w * (size_t)p->h;
        const int sc = p->scale;
        const int rx = s->x - sc, ry = s->y - sc, rw = w + 2 * sc, rh = h + 2 * sc;
        rd_SheetRim(p->cov, (uint32_t)p->w, (uint32_t)p->h, (uint32_t)p->scale, rx, ry, rw, rh,
                    rim);
        if (p->tex) {
            uint8_t *px = malloc((size_t)rw * (size_t)rh);
            if (px) {
                for (int r = 0; r < rh; r++) {
                    memcpy(px + (size_t)r * (size_t)rw,
                           rim + (size_t)(ry + r) * (size_t)p->w + (size_t)rx, (size_t)rw);
                }
                rd_UpdateTextureRect((RdTex){p->tex}, (uint32_t)rx, (uint32_t)(p->h + ry),
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
        p->tex = rd_CreateTextureSheet((uint32_t)p->w, (uint32_t)(p->scale > 1 ? 2 * p->h : p->h),
                                       p->cov, &st, name)
                     .id;
        ui__SetMenuFontHooks(mfShutdown, mfForget);
    }
    p->lastFrame = rd_FrameNumber();
    return p->tex;
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
                rd_SetTextureSheetStyle((RdTex){s_mf.pages[c][p].tex}, &st);
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
            return s;
        }
    }
    const int w = it->w, hgt = it->h;
    MfStrip *s = newStrip();
    int page, x, y;
    if (!s || !allocCell(cls, sc, w * sc, hgt * sc, &page, &x, &y)) {
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
    s->w = w;
    s->h = hgt;
    placeCoverage(s, cov);
    free(cov);
    return s;
}

/* ---------------------------------------------------- a port text's strip */

static MfStrip *textStrip(const char *utf8, float em, unsigned layout, int cls, int sc)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = hashBytes(h, utf8, strlen(utf8));
    h = hashBytes(h, &em, sizeof(em));
    h = hashBytes(h, &layout, sizeof(layout));
    h = hashBytes(h, &cls, sizeof(cls));
    if (sc > 1) {
        h = hashBytes(h, &sc, sizeof(sc));
    }
    for (int i = 0; i < MF_STRIPS; i++) {
        MfStrip *s = &s_mf.strips[i];
        if (s->used && s->hash == h && s->text && s->cls == cls && s->scale == sc && s->em == em &&
            s->layout == layout && strcmp(s->text, utf8) == 0) {
            return s;
        }
    }
    const char *start[MF_LINES];
    size_t len[MF_LINES];
    const int n = splitLines(utf8, start, len);
    float asc = 0.0f, desc = 0.0f, step = 0.0f, cap = 0.0f;
    ui__SheetVMetrics(em, &asc, &desc, &step, &cap);
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
    for (int i = 0; i < n; i++) {
        const float w = ui__SheetLineWidth(em, 1.0f, 0.0f, start[i], len[i]);
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
    /* the glyphs' overhang past their advances (italic-free Arimo: a
       fraction of the em) and the rim */
    const float padX = ceilf(em * 2.0f * UI_X_PER_Y * 0.12f) + (float)MF_RIM_X;
    const float padY = ceilf(em * 0.12f) + (float)MF_RIM_Y;
    const int ox = (int)floorf(minX - padX), oy = (int)floorf(b0 - asc - padY);
    int w = (int)ceilf(maxX + padX) - ox;
    int hgt = (int)ceilf(b0 + (float)(n - 1) * step + desc + padY) - oy;
    w = w > MF_MAX_W ? MF_MAX_W : w;
    hgt = hgt > MF_MAX_H ? MF_MAX_H : hgt;
    if (w <= 0 || hgt <= 0) {
        return NULL;
    }
    MfStrip *s = newStrip();
    int page, x, y;
    if (!s || !allocCell(cls, sc, w * sc, hgt * sc, &page, &x, &y)) {
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
    for (int i = 0; i < n; i++) {
        ui__SheetRasterLine(cov, w * sc, hgt * sc, w * sc, em, 1.0f, 0.0f, pen[i] - (float)ox,
                            b0 + (float)i * step - (float)oy, start[i], len[i], sc);
    }
    embolden(cov, w * sc, hgt * sc, sc);
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->hash = h;
    s->text = copy;
    s->item = -1;
    s->lang = -1;
    s->em = em;
    s->layout = layout;
    s->scale = sc;
    s->cls = cls;
    s->page = page;
    s->x = x;
    s->y = y;
    s->w = w;
    s->h = hgt;
    s->ox = ox;
    s->oy = oy;
    placeCoverage(s, cov);
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

void ui_DrawMenuText(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                     unsigned flags, int ink, const UiXform *xf)
{
#ifdef ICO_RD
    int cls;
    uint8_t col[4];
    if (!utf8 || !*utf8 || !(size > 0.0f) || !ui_FontInit() ||
        !inkColour(ink, UI_RIM_FULL, rgba, (flags & UI_ADDITIVE) != 0, &cls, col)) {
        return;
    }
    UiLang lang = ui_GetLanguage();
    lang = (int)lang >= 0 && lang < UI_LANG_COUNT ? lang : UI_LANG_EN;
    if (cls != MF_PLAIN) {
        styleLight((int)lang);
    }
    const MfStrip *s =
        textStrip(utf8, size * 0.5f, flags & (UI_ALIGN_MASK | UI_VALIGN_MASK), cls, mfScale());
    if (!s) {
        /* every page of the style holds words a recorded frame still
           draws: the plain glyphs rather than nothing, so a screen whose
           strings change every tick never goes blank */
        ui_DrawTextXf(x, y, size, col, utf8, flags & ~(unsigned)UI_KEEP_STATE, xf);
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
    /* the whole strip, scale times the quad's texels (F-G) */
    q.u0 = (float)s->x;
    q.v0 = (float)s->y;
    q.u1 = (float)(s->x + s->w * s->scale);
    q.v1 = (float)(s->y + s->h * s->scale);
    noteLast(s, tex, ax, ay);
    ui__DrawTexQuads(tex, &q, 1, col, flags, xf, ui__TextKey(utf8, flags, s->page));
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
/* ui_MenuWordDraw with no room in the pages: the item's lines in the plain
   glyphs at the item's anchor, em and pitch, mapped through the sprite's
   box (the glow pass draws nothing) */
static void itemFallback(const UiMenuTextItem *it, int lang, const int box[4], const int uv[4],
                         const uint8_t col[4], int glow)
{
    const char *text = ui_StrIn((UiLang)lang, (UiStrId)it->str);
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
        ui_DrawText(x0 + it->x[lang] * sx, y0 + (it->y[lang] + (float)i * it->pitch) * sy,
                    it->em[lang] * sy, col, line, (unsigned)it->align | UI_VALIGN_MIDDLE);
    }
}
#endif

void ui_MenuWordDraw(const UiMenuTextItem *it, int lang, const int box[4], const int uv[4],
                     const unsigned char rgba[4], int glow)
{
#ifdef ICO_RD
    int cls;
    uint8_t col[4];
    if (!it || !box || !uv || uv[2] <= 0 || uv[3] <= 0 || !ui_FontInit() ||
        !inkColour(it->ink, it->rim[lang >= 0 && lang < UI_LANG_COUNT ? lang : UI_LANG_EN], rgba,
                   glow, &cls, col)) {
        return;
    }
    lang = lang >= 0 && lang < UI_LANG_COUNT ? lang : UI_LANG_EN;
    if (cls != MF_PLAIN) {
        styleLight(lang);
    }
    const int item = it >= ui_menu_text_items && it < ui_menu_text_items + ui_menu_text_item_count
                         ? (int)(it - ui_menu_text_items)
                         : -2;
    const MfStrip *s = itemStrip(it, item, lang, cls, mfScale());
    if (!s) {
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
        /* the strip's texels scale times the item's (F-G) */
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
    ui__DrawTexQuads(tex, &q, 1, col, flags, NULL,
                     ui__TextKey(ui_StrIn((UiLang)lang, (UiStrId)it->str), flags, s->page));
#else
    (void)it;
    (void)lang;
    (void)box;
    (void)uv;
    (void)rgba;
    (void)glow;
#endif
}
