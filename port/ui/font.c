/*
 * port/ui/font.c
 *
 * Runtime text on rd (font.h; docs/port/UI.md): the embedded Arimo Regular
 * through stb_truetype into per-size R8 atlases, drawn as GS sprites.
 */
#include "font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif

/* stb_truetype (port/third_party/stb, MIT), compiled here and nowhere else */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "game_font.h"
#include "ui_internal.h"

/* the font file, embedded at build time (port/ui/embed_font.cmake) */
extern const unsigned char ui_font_ttf[];
extern const unsigned int ui_font_ttf_size;

/* T1: a size's pages are PAGE_MIN to PAGE_MAX texels wide, wider for big
   pixel sizes (an Enhanced 4K output rasterises the menu's 27 units at
   about 130 px, which four 512-texel pages could not hold), and PAGE_TRIM
   texels less high: not a power of two, so the Enhanced texture filter
   never gives a page a mip chain (rd_replay.c texLevels), whose 2 x 2 box
   levels would average neighbouring glyphs across the gutter and scale
   their alpha (rdtex_KeepAlphaCoverage). Glyph cells are GUTTER texels
   apart and from the page's edges: bilinear sampling inside a quad reaches
   at most one texel past the glyph's box, which is then zero coverage. */
#define PAGE_MIN 512
#define PAGE_MAX 2048
#define PAGE_TRIM 8
#define GUTTER 2
#define MAX_PAGES 4
#define MAX_SIZES 16
#define GLYPH_SLOTS 1024 /* per size, open addressing; a power of two */
#define MAX_QUADS 512    /* glyphs per draw call */

typedef struct Page {
    uint8_t *cov; /* pageW x pageH coverage (SizeSet) */
    uint32_t tex; /* R8 RdTex id (rd_CreateTextureR8), 0 until the page is first drawn */
    int shelfX, shelfY, shelfH;
} Page;

typedef struct GlyphSlot {
    uint32_t cp;
    int used;
    UiGlyph g;
} GlyphSlot;

typedef struct SizeSet {
    int px;      /* 0: free */
    float scale; /* stb scale for px */
    int pageW, pageH;
    int pageCount;
    Page pages[MAX_PAGES];
    GlyphSlot *slots;
} SizeSet;

static struct {
    int inited, failed;
    stbtt_fontinfo info;
    int ascent, descent, lineGap, capHeight; /* font units */
    UiGsFrame frame;
    float scale;
    SizeSet sizes[MAX_SIZES];
    void (*recordHook)(void);
    void (*syncHook)(void);
    int suppress;
    int warnedSizes, warnedPages;
} s_font = {.frame = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z}, .scale = 1.0f};

/* the game face (package GFONT; below) */
/* a glyph's alpha / light pairs after its ink (game_font.h): the main
   cell, the left cap, the right cap */
enum { GG_MAIN, GG_LEFT, GG_RIGHT, GG_PAIRS };

typedef struct GPair {
    int ax, ay, px, py; /* the alpha and the light cells in the atlas */
    int w, h;           /* with a texel of apron all round */
    float top;          /* the first inner row from the baseline, main texels */
} GPair;

typedef struct GGlyph {
    uint32_t cp;
    float scale;        /* main texels per cell texel */
    int ix, iy, iw, ih; /* the ink cell (zero border) */
    GPair c[GG_PAIRS];
    float idx, idy, adv; /* main texels */
    int sheet, lang, count;
    int gx[4], gy[4], gw, gh; /* the fitted glow cells (zero border): inside a
                                 word, at its start, its end, alone */
    float gdx, gdy;           /* their top-left from the pen on the baseline, main texels */
    int mv[3][4]; /* the main cell's alpha x, y and light x, y at a word's start, end, alone */
} GGlyph;

typedef struct GKern {
    uint32_t a, b;
    float adj;
} GKern;

static struct {
    int loaded;
    int w, h;
    float em, cap, space;
    GGlyph *g;
    int ng;
    GKern *k;
    int nk;
    char (*sheets)[UI_GF_SHEET_NAME];
    int nsheets;
    uint8_t *cov;
    uint32_t tex;
} s_game;

static int gameActive(void);
static float gameMeasure(float size, const char *utf8);
static float gameCapY(float size);

/* --------------------------------------------------------------- UTF-8 */

uint32_t ui_Utf8Next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    uint32_t c = s[0], min;
    int n;

    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        *sp += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        c &= 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        c &= 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        c &= 0x07;
        min = 0x10000;
    } else {
        *sp += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *sp += 1;
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        *sp += 1;
        return 0xFFFD;
    }
    *sp += n + 1;
    return c;
}

/* ----------------------------------------------------------- lifecycle */

bool ui_FontInit(void)
{
    if (s_font.inited) {
        return true;
    }
    if (s_font.failed) {
        return false;
    }
    const unsigned char *data = ui_font_ttf;
    /* the size first: stbtt_GetFontOffsetForIndex reads the header */
    int off = ui_font_ttf_size < 12 ? -1 : stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&s_font.info, data, off)) {
        fprintf(stderr, "ui: the embedded font does not parse; no runtime text\n");
        s_font.failed = 1;
        return false;
    }
    stbtt_GetFontVMetrics(&s_font.info, &s_font.ascent, &s_font.descent, &s_font.lineGap);
    {
        int x0, y0, x1, y1;
        if (stbtt_GetCodepointBox(&s_font.info, 'H', &x0, &y0, &x1, &y1)) {
            s_font.capHeight = y1;
        } else {
            s_font.capHeight = s_font.ascent * 2 / 3;
        }
    }
    s_font.inited = 1;
    return true;
}

static void freeSize(SizeSet *z, int destroyTex)
{
    for (int p = 0; p < z->pageCount; p++) {
#ifdef ICO_RD
        if (destroyTex && z->pages[p].tex) {
            rd_DestroyTexture((RdTex){z->pages[p].tex});
        }
#else
        (void)destroyTex;
#endif
        free(z->pages[p].cov);
    }
    free(z->slots);
    memset(z, 0, sizeof(*z));
}

void ui_FontShutdown(void)
{
    for (int i = 0; i < MAX_SIZES; i++) {
        if (s_font.sizes[i].px) {
            freeSize(&s_font.sizes[i], 1);
        }
    }
#ifdef ICO_RD
    if (s_game.tex) {
        rd_DestroyTexture((RdTex){s_game.tex});
    }
#endif
    s_game.tex = 0;
    s_font.inited = 0;
    s_font.failed = 0;
    s_font.warnedSizes = s_font.warnedPages = 0;
}

void ui_FontForgetTextures(void)
{
    s_game.tex = 0;
    for (int i = 0; i < MAX_SIZES; i++) {
        SizeSet *z = &s_font.sizes[i];
        for (int p = 0; p < z->pageCount; p++) {
            z->pages[p].tex = 0;
        }
    }
}

void ui__SetRecordHook(void (*fn)(void))
{
    s_font.recordHook = fn;
}

void ui__RunRecordHook(void)
{
    if (s_font.recordHook) {
        s_font.recordHook();
    }
}

void ui__SuppressRecordHook(int delta)
{
    s_font.suppress += delta;
}

void ui__SetSyncHook(void (*fn)(void))
{
    s_font.syncHook = fn;
}

void ui__Sync(void)
{
    if (s_font.syncHook) {
        s_font.syncHook();
    }
}

void ui_SetGsFrame(const UiGsFrame *f)
{
    if (f) {
        s_font.frame = *f;
    }
}

const UiGsFrame *ui_GetGsFrame(void)
{
    return &s_font.frame;
}

/* package OV: overlay mode (font.h ui_BeginOverlay) */
static struct {
    int active;
    float left, top;  /* the 4:3 picture's top-left corner, output pixels */
    float sx, sy;     /* output pixels per grid unit */
    float savedScale; /* the scale ui_EndOverlay restores */
} s_ov;

void ui_SetScale(float scale)
{
    const float s = scale > 0.25f ? scale : 0.25f;
    if (s_ov.active) {
        s_ov.savedScale = s;
        return;
    }
    s_font.scale = s;
}

float ui_GetScale(void)
{
    return s_font.scale;
}

float ui_ScaleFor(int preset, uint32_t outputHeight)
{
    if (preset == 0 || outputHeight == 0) {
        return 1.0f;
    }
    float s = (float)outputHeight / 448.0f;
    return s < 1.0f ? 1.0f : s;
}

/* -------------------------------------------------------------- atlas */

static SizeSet *sizeSet(int px)
{
    SizeSet *freeSlot = NULL, *nearest = NULL;
    int bestDist = 1 << 30;

    for (int i = 0; i < MAX_SIZES; i++) {
        SizeSet *z = &s_font.sizes[i];
        if (z->px == px) {
            return z;
        }
        if (!z->px && !freeSlot) {
            freeSlot = z;
        }
        if (z->px) {
            int d = abs(z->px - px);
            if (d < bestDist) {
                bestDist = d;
                nearest = z;
            }
        }
    }
    if (!freeSlot) {
        /* every slot in use: no eviction (draws already recorded this frame
           may name the textures); the nearest size stands in */
        if (!s_font.warnedSizes) {
            fprintf(stderr, "ui: more than %d text sizes; reusing the nearest\n", MAX_SIZES);
            s_font.warnedSizes = 1;
        }
        return nearest;
    }
    freeSlot->slots = calloc(GLYPH_SLOTS, sizeof(GlyphSlot));
    if (!freeSlot->slots) {
        return NULL;
    }
    freeSlot->px = px;
    freeSlot->scale = stbtt_ScaleForMappingEmToPixels(&s_font.info, (float)px);
    /* T1: about 32 cells of a px-high glyph a page at least */
    freeSlot->pageW = PAGE_MIN;
    while (freeSlot->pageW < PAGE_MAX && freeSlot->pageW < px * 6) {
        freeSlot->pageW *= 2;
    }
    freeSlot->pageH = freeSlot->pageW - PAGE_TRIM;
    freeSlot->pageCount = 0;
    return freeSlot;
}

static Page *newPage(SizeSet *z)
{
    if (z->pageCount == MAX_PAGES) {
        return NULL;
    }
    Page *p = &z->pages[z->pageCount];
    memset(p, 0, sizeof(*p));
    p->cov = calloc((size_t)z->pageW * (size_t)z->pageH, 1);
    if (!p->cov) {
        return NULL;
    }
    p->shelfX = GUTTER;
    p->shelfY = GUTTER;
    p->shelfH = 0;
    z->pageCount++;
    return p;
}

/* a w x h cell GUTTER texels from its neighbours and the page's edges,
   shelf packing */
static int allocCell(SizeSet *z, int w, int h, int *page, int *x, int *y)
{
    if (w + 2 * GUTTER > z->pageW || h + 2 * GUTTER > z->pageH) {
        return 0;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        Page *p = z->pageCount ? &z->pages[z->pageCount - 1] : newPage(z);
        if (!p) {
            return 0;
        }
        if (p->shelfX + w + GUTTER > z->pageW) {
            p->shelfY += p->shelfH + GUTTER;
            p->shelfX = GUTTER;
            p->shelfH = 0;
        }
        if (p->shelfY + h + GUTTER <= z->pageH) {
            *page = z->pageCount - 1;
            *x = p->shelfX;
            *y = p->shelfY;
            p->shelfX += w + GUTTER;
            if (h > p->shelfH) {
                p->shelfH = h;
            }
            return 1;
        }
        if (!newPage(z)) {
            if (!s_font.warnedPages) {
                fprintf(stderr, "ui: the %d px atlas is full\n", z->px);
                s_font.warnedPages = 1;
            }
            return 0;
        }
    }
    return 0;
}

#ifdef ICO_RD
/* the texels rd gets: coverage in GS alpha units (255 -> 0x80, rounded) */
static void gsCoverage(uint8_t *dst, const uint8_t *cov, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)((cov[i] * 128 + 127) / 255);
    }
}
#endif

/* The code points drawn as '?' so far (the font has no glyph for them): each
   is logged once.  A fixed set; past it the log stops, the drawing does not. */
#define MISSING_MAX 64
static uint32_t s_missing[MISSING_MAX];
static int s_missingN;

static void noteMissing(uint32_t cp)
{
    for (int i = 0; i < s_missingN; i++) {
        if (s_missing[i] == cp) {
            return;
        }
    }
    if (s_missingN < MISSING_MAX) {
        s_missing[s_missingN++] = cp;
        fprintf(stderr, "ui: no glyph for U+%04X; drawn as '?'\n", (unsigned)cp);
    }
}

static int glyphIndex(uint32_t cp)
{
    int g = stbtt_FindGlyphIndex(&s_font.info, (int)cp);
    if (g == 0 && cp != '?') {
        noteMissing(cp);
        g = stbtt_FindGlyphIndex(&s_font.info, '?');
    }
    return g;
}

static const UiGlyph *glyphIn(SizeSet *z, uint32_t cp)
{
    uint32_t h = (cp * 2654435761u) & (GLYPH_SLOTS - 1);
    GlyphSlot *slot = NULL;

    for (int i = 0; i < GLYPH_SLOTS; i++) {
        GlyphSlot *s = &z->slots[(h + (uint32_t)i) & (GLYPH_SLOTS - 1)];
        if (s->used && s->cp == cp) {
            return &s->g;
        }
        if (!s->used) {
            slot = s;
            break;
        }
    }
    if (!slot) {
        return NULL;
    }
    int g = glyphIndex(cp);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&s_font.info, g, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&s_font.info, g, z->scale, z->scale, &x0, &y0, &x1, &y1);
    UiGlyph out;
    memset(&out, 0, sizeof(out));
    out.advance = (float)adv * z->scale;
    out.xoff = (float)x0;
    out.yoff = (float)y0;
    int w = x1 - x0, hgt = y1 - y0;
    if (w > 0 && hgt > 0) {
        int page, x, y;
        if (!allocCell(z, w, hgt, &page, &x, &y)) {
            return NULL;
        }
        Page *p = &z->pages[page];
        stbtt_MakeGlyphBitmap(&s_font.info, p->cov + (size_t)y * (size_t)z->pageW + (size_t)x, w,
                              hgt, z->pageW, z->scale, z->scale, g);
#ifdef ICO_RD
        /* R8: a page already on the GPU gets the glyph's cell alone; a page
           not yet drawn is created whole when it is (pageTexture) */
        if (p->tex) {
            uint8_t *cell = malloc((size_t)w * (size_t)hgt);
            if (cell) {
                for (int r = 0; r < hgt; r++) {
                    gsCoverage(cell + (size_t)r * (size_t)w,
                               p->cov + (size_t)(y + r) * (size_t)z->pageW + (size_t)x, (size_t)w);
                }
                rd_UpdateTextureRect((RdTex){p->tex}, (uint32_t)x, (uint32_t)y, (uint32_t)w,
                                     (uint32_t)hgt, cell);
                free(cell);
            }
        }
#endif
        out.page = page;
        out.x = x;
        out.y = y;
        out.w = w;
        out.h = hgt;
    }
    slot->used = 1;
    slot->cp = cp;
    slot->g = out;
    return &slot->g;
}

bool ui_FontGlyph(uint32_t cp, int px, UiGlyph *out)
{
    if (!ui_FontInit() || px < 1) {
        return false;
    }
    SizeSet *z = sizeSet(px);
    const UiGlyph *g = z ? glyphIn(z, cp) : NULL;
    if (!g) {
        return false;
    }
    *out = *g;
    return true;
}

float ui_FontKern(uint32_t a, uint32_t b, int px)
{
    if (!ui_FontInit() || px < 1) {
        return 0.0f;
    }
    float sc = stbtt_ScaleForMappingEmToPixels(&s_font.info, (float)px);
    return (float)stbtt_GetGlyphKernAdvance(&s_font.info, glyphIndex(a), glyphIndex(b)) * sc;
}

int ui_FontMissingSeen(uint32_t *cps, int cap)
{
    for (int i = 0; cps && i < s_missingN && i < cap; i++) {
        cps[i] = s_missing[i];
    }
    return s_missingN;
}

bool ui_FontHasGlyph(uint32_t cp)
{
    return ui_FontInit() && stbtt_FindGlyphIndex(&s_font.info, (int)cp) != 0;
}

const uint8_t *ui_FontPage(int px, int page, int *w, int *h)
{
    for (int i = 0; i < MAX_SIZES; i++) {
        SizeSet *z = &s_font.sizes[i];
        if (z->px == px && page >= 0 && page < z->pageCount) {
            *w = z->pageW;
            *h = z->pageH;
            return z->pages[page].cov;
        }
    }
    return NULL;
}

uint32_t ui_FontPageTex(int px, int page)
{
    for (int i = 0; i < MAX_SIZES; i++) {
        SizeSet *z = &s_font.sizes[i];
        if (z->px == px && page >= 0 && page < z->pageCount) {
            return z->pages[page].tex;
        }
    }
    return 0;
}

#ifdef ICO_RD
/* the page as rd sees it: R8, the coverage in GS alpha units (255 ->
   0x80, rounded; rd_CreateTextureR8), drawn by font_ps as a white texel
   with that alpha; created whole the first time the page is drawn, after
   that each new glyph's cell is uploaded alone (glyphIn) */
static uint32_t pageTexture(SizeSet *z, int page)
{
    Page *p = &z->pages[page];
    if (!p->tex) {
        const size_t texels = (size_t)z->pageW * (size_t)z->pageH;
        uint8_t *gs = malloc(texels);
        if (!gs) {
            return 0;
        }
        gsCoverage(gs, p->cov, texels);
        char name[32];
        snprintf(name, sizeof(name), "ui font %dpx p%d", z->px, page);
        p->tex = rd_CreateTextureR8((uint32_t)z->pageW, (uint32_t)z->pageH, gs, name).id;
        free(gs);
    }
    return p->tex;
}
#endif

/* ---------------------------------------------------------------- layout */

typedef struct Quad {
    float x0, y0, x1, y1; /* grid */
    int page;
    int u0, v0, u1, v1; /* texels */
} Quad;

static int pxFor(float size)
{
    int px = (int)lrintf(size * s_font.scale);
    return px < 1 ? 1 : px;
}

void ui_FontMetrics(float size, float *ascent, float *descent, float *capHeight)
{
    if (!ui_FontInit()) {
        if (ascent) {
            *ascent = 0.0f;
        }
        if (descent) {
            *descent = 0.0f;
        }
        if (capHeight) {
            *capHeight = 0.0f;
        }
        return;
    }
    const int px = pxFor(size);
    const float sc = stbtt_ScaleForMappingEmToPixels(&s_font.info, (float)px) / s_font.scale;
    if (ascent) {
        *ascent = (float)s_font.ascent * sc;
    }
    if (descent) {
        *descent = (float)-s_font.descent * sc;
    }
    if (capHeight) {
        *capHeight = gameActive() ? gameCapY(size) : (float)s_font.capHeight * sc;
    }
}

/* Lays utf8 out with the pen at (0, 0) on the first baseline, in atlas
   pixels; calls back per glyph with a bitmap; returns the widest line's
   advance in atlas pixels and the line count. */
typedef void (*GlyphFn)(void *user, const UiGlyph *g, float penX, float penY);

static float layoutRun(SizeSet *z, const char *utf8, GlyphFn fn, void *user, float *lineWidths,
                       int maxLines, int *lines)
{
    const float lineStep = (float)(s_font.ascent - s_font.descent + s_font.lineGap) * z->scale;
    float penX = 0.0f, penY = 0.0f, widest = 0.0f;
    uint32_t prev = 0;
    int line = 0;
    const char *s = utf8;
    uint32_t cp;

    while ((cp = ui_Utf8Next(&s)) != 0) {
        if (cp == '\n') {
            if (lineWidths && line < maxLines) {
                lineWidths[line] = penX;
            }
            if (penX > widest) {
                widest = penX;
            }
            line++;
            penX = 0.0f;
            penY += lineStep;
            prev = 0;
            continue;
        }
        if (prev) {
            penX +=
                (float)stbtt_GetGlyphKernAdvance(&s_font.info, glyphIndex(prev), glyphIndex(cp)) *
                z->scale;
        }
        const UiGlyph *g = glyphIn(z, cp);
        if (g) {
            if (fn && g->w > 0) {
                fn(user, g, penX, penY);
            }
            penX += g->advance;
        }
        prev = cp;
    }
    if (lineWidths && line < maxLines) {
        lineWidths[line] = penX;
    }
    if (penX > widest) {
        widest = penX;
    }
    if (lines) {
        *lines = line + 1;
    }
    return widest;
}

float ui_MeasureText(float size, const char *utf8)
{
    if (!utf8 || !ui_FontInit()) {
        return 0.0f;
    }
    if (gameActive()) {
        return gameMeasure(size, utf8);
    }
    SizeSet *z = sizeSet(pxFor(size));
    if (!z) {
        return 0.0f;
    }
    const float xs = UI_X_PER_Y / s_font.scale * ((float)pxFor(size) / (float)z->px);
    return layoutRun(z, utf8, NULL, NULL, NULL, 0, NULL) * xs;
}

#define MAX_LINES 16

typedef struct Collect {
    Quad *q;
    int n, max;
    int line;
    float lineStep;
    float originX[MAX_LINES]; /* per-line start in atlas pixels (alignment) */
} Collect;

static void collectGlyph(void *user, const UiGlyph *g, float penX, float penY)
{
    Collect *c = user;
    if (c->n == c->max) {
        return;
    }
    int line = c->lineStep > 0.0f ? (int)lrintf(penY / c->lineStep) : 0;
    if (line < 0) {
        line = 0;
    }
    if (line >= MAX_LINES) {
        line = MAX_LINES - 1;
    }
    Quad *q = &c->q[c->n++];
    q->x0 = c->originX[line] + penX + g->xoff;
    q->y0 = penY + g->yoff;
    q->x1 = q->x0 + (float)g->w;
    q->y1 = q->y0 + (float)g->h;
    q->page = g->page;
    q->u0 = g->x;
    q->v0 = g->y;
    q->u1 = g->x + g->w;
    q->v1 = g->y + g->h;
}

#ifdef ICO_RD
static int32_t gsX(float gx)
{
    const UiGsFrame *f = &s_font.frame;
    return (int32_t)lrintf(f->centerX * 16.0f +
                           (gx - UI_GRID_CX) * 16.0f * (float)f->screenW / 640.0f);
}

static int32_t gsY(float gy)
{
    const UiGsFrame *f = &s_font.frame;
    return (int32_t)lrintf(f->centerY * 16.0f +
                           (gy - UI_GRID_CY) * 8.0f * (float)f->screenH / 224.0f);
}

static void mapXf(const UiXform *xf, float *x, float *y)
{
    if (xf) {
        *x = (*x - xf->originX) * xf->scaleX + xf->originX + xf->offsetX;
        *y = (*y - xf->originY) * xf->scaleY + xf->originY + xf->offsetY;
    }
}

static void setState(unsigned flags)
{
    if (s_font.recordHook && s_font.suppress <= 0) {
        s_font.recordHook();
    }
    if (!(flags & UI_KEEP_STATE)) {
        rd_Blend((flags & UI_ADDITIVE) ? RD_BLEND_CS_AS_ADD_CD : RD_BLEND_LERP_AS, 0, 1);
        rd_TestGs(0x30000); /* Z test on, ALWAYS; no alpha test */
        rd_ZWrite(0);
        rd_FBA(0);
        rd_PABE(0);
    }
    rd_ABE(1);
}
#endif

/* ------------------------------------------------ overlay (package OV) */

/* the frame's 448 lines are grid y 2 .. 450 (centre 226) */
#define OV_GRID_TOP (UI_GRID_CY - 224.0f)

int ui_OverlayActive(void)
{
    return s_ov.active;
}

void ui_OverlayMap(float gx, float gy, float *x16, float *y16)
{
    if (x16) {
        *x16 = (s_ov.left + gx * s_ov.sx) * 16.0f;
    }
    if (y16) {
        *y16 = (s_ov.top + (gy - OV_GRID_TOP) * s_ov.sy) * 16.0f;
    }
}

#ifdef ICO_RD
static UiOverlaySink s_ovSink;

void ui__SetOverlaySink(UiOverlaySink fn)
{
    s_ovSink = fn;
}

void ui_BeginOverlay(const struct RdOverlayCtx *ctx)
{
    if (!ctx || s_ov.active || !ctx->box.w || !ctx->box.h) {
        return;
    }
    const float bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float w = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    s_ov.left = (float)ctx->box.x + (bw - w) * 0.5f;
    s_ov.top = (float)ctx->box.y;
    s_ov.sx = w / UI_GRID_W;
    s_ov.sy = bh / 448.0f;
    s_ov.savedScale = s_font.scale;
    s_font.scale = ctx->boxScale > 0.25f ? ctx->boxScale : 0.25f;
    s_ov.active = 1;
}

void ui_EndOverlay(void)
{
    if (s_ov.active) {
        s_ov.active = 0;
        s_font.scale = s_ov.savedScale;
    }
}

static void ovEmit(const RdScreenVtx *v, uint32_t n, uint32_t tex, unsigned flags)
{
    const RdBlend blend = (flags & UI_ADDITIVE) ? RD_BLEND_CS_AS_ADD_CD : RD_BLEND_LERP_AS;
    if (s_ovSink) {
        s_ovSink(RD_PRIM_SPRITES, v, n, (RdTex){tex}, blend);
    } else {
        rd_OverlayPrims(RD_PRIM_SPRITES, v, n, (RdTex){tex}, blend);
    }
}

/* whole output pixels, in 12.4 */
static int32_t ovPix(float p16)
{
    return (int32_t)lrintf(p16 / 16.0f) * 16;
}
#else
void ui_BeginOverlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

void ui_EndOverlay(void) {}
#endif

static void drawHalo(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                     unsigned flags, const UiXform *xf);

/* R7d: the draws' keys (font.h ui_SetDrawKey) */
static uint64_t s_keyOwner;

uint64_t ui_SetDrawKey(uint64_t owner)
{
    const uint64_t prev = s_keyOwner;
    s_keyOwner = owner;
    return prev;
}

#ifdef ICO_RD
static uint64_t keyMix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

static uint64_t textKey(const char *utf8, unsigned flags, int page)
{
    uint64_t h = 0xCBF29CE484222325ull; /* FNV-1a */
    for (const char *s = utf8; *s; s++) {
        h = (h ^ (uint8_t)*s) * 0x100000001B3ull;
    }
    h = keyMix(h, flags & (UI_ALIGN_MASK | UI_VALIGN_MASK));
    h = keyMix(h, (uint64_t)page);
    h = keyMix(h, s_keyOwner);
    return h ? h : 1;
}

static uint64_t rectKey(void)
{
    if (!s_keyOwner) {
        return 0;
    }
    const uint64_t h = keyMix(s_keyOwner, 0x52454354u); /* "RECT" */
    return h ? h : 1;
}
#endif

/* ------------------------------------------- the game face (package GFONT) */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int rd16(const uint8_t *p)
{
    return (int)p[0] | ((int)p[1] << 8);
}

static float rdF(const uint8_t *p)
{
    const uint32_t v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void ui_GameFaceUnload(void)
{
#ifdef ICO_RD
    if (s_game.tex) {
        rd_DestroyTexture((RdTex){s_game.tex});
    }
#endif
    free(s_game.g);
    free(s_game.k);
    free(s_game.sheets);
    free(s_game.cov);
    memset(&s_game, 0, sizeof(s_game));
}

bool ui_GameFaceLoad(const void *blob, size_t size)
{
    const uint8_t *p = blob;
    ui_GameFaceUnload();
    if (!p || size < UI_GF_HEADER || memcmp(p, "ICGF", 4) != 0 || rd32(p + 4) != UI_GF_VERSION) {
        fprintf(stderr, "ui: the game font item does not parse (version %u, %zu bytes)\n",
                p && size >= 8 ? (unsigned)rd32(p + 4) : 0u, size);
        return false;
    }
    const int w = rd16(p + 8), h = rd16(p + 10);
    const uint32_t ng = rd32(p + 24), nk = rd32(p + 28), ns = rd32(p + 32);
    const uint64_t need = (uint64_t)UI_GF_HEADER + (uint64_t)ng * UI_GF_GLYPH +
                          (uint64_t)nk * UI_GF_KERN + (uint64_t)ns * UI_GF_SHEET_NAME +
                          (uint64_t)w * (uint64_t)h;
    const float em = rdF(p + 12), cap = rdF(p + 16), space = rdF(p + 20);
    if (w <= 0 || h <= 0 || ng == 0 || ng > 4096 || nk > 65536 || ns > 4096 || need != size ||
        !(em > 1.0f && em < 100.0f) || !(cap > 1.0f && cap < 100.0f) ||
        !(space >= 0.0f && space < 100.0f)) {
        fprintf(stderr, "ui: the game font item is malformed\n");
        return false;
    }
    s_game.g = calloc(ng, sizeof(GGlyph));
    s_game.k = calloc(nk ? nk : 1, sizeof(GKern));
    s_game.sheets = calloc(ns ? ns : 1, UI_GF_SHEET_NAME);
    s_game.cov = malloc((size_t)w * (size_t)h);
    if (!s_game.g || !s_game.k || !s_game.sheets || !s_game.cov) {
        ui_GameFaceUnload();
        return false;
    }
    const uint8_t *q = p + UI_GF_HEADER;
    for (uint32_t i = 0; i < ng; i++, q += UI_GF_GLYPH) {
        GGlyph *g = &s_game.g[i];
        g->cp = rd32(q);
        g->scale = rdF(q + 4);
        g->ix = rd16(q + 8);
        g->iy = rd16(q + 10);
        g->iw = rd16(q + 12);
        g->ih = rd16(q + 14);
        int bad = 0;
        for (int k = 0; k < GG_PAIRS; k++) {
            const uint8_t *r = q + 16 + k * 16;
            GPair *c = &g->c[k];
            c->ax = rd16(r);
            c->ay = rd16(r + 2);
            c->px = rd16(r + 4);
            c->py = rd16(r + 6);
            c->w = rd16(r + 8);
            c->h = rd16(r + 10);
            c->top = rdF(r + 12);
            bad |= c->w < 3 || c->h < 3 || c->ax + c->w > w || c->px + c->w > w ||
                   c->ay + c->h > h || c->py + c->h > h || !(fabsf(c->top) < 1000.0f);
        }
        g->idx = rdF(q + 64);
        g->idy = rdF(q + 68);
        g->adv = rdF(q + 72);
        g->sheet = rd16(q + 76);
        g->lang = q[78];
        g->count = q[79];
        g->gdx = rdF(q + 80);
        g->gdy = rdF(q + 84);
        g->gw = rd16(q + 88);
        g->gh = rd16(q + 90);
        for (int v = 0; v < 4; v++) {
            g->gx[v] = rd16(q + 92 + v * 4);
            g->gy[v] = rd16(q + 94 + v * 4);
            bad |= g->gx[v] + g->gw > w || g->gy[v] + g->gh > h;
        }
        for (int v = 0; v < 3; v++) {
            for (int j = 0; j < 4; j++) {
                g->mv[v][j] = rd16(q + 108 + v * 8 + j * 2);
            }
            bad |= g->mv[v][0] + g->c[GG_MAIN].w > w || g->mv[v][2] + g->c[GG_MAIN].w > w ||
                   g->mv[v][1] + g->c[GG_MAIN].h > h || g->mv[v][3] + g->c[GG_MAIN].h > h;
        }
        bad |= g->ix + g->iw > w || g->iy + g->ih > h || !(g->scale > 0.1f && g->scale < 10.0f) ||
               (i > 0 && g->cp <= s_game.g[i - 1].cp) || g->sheet >= (int)(ns ? ns : 1) ||
               !(g->adv > 0.0f && g->adv < 1000.0f);
        if (bad) {
            fprintf(stderr, "ui: the game font item's glyph %u is malformed\n", (unsigned)i);
            ui_GameFaceUnload();
            return false;
        }
    }
    for (uint32_t i = 0; i < nk; i++, q += UI_GF_KERN) {
        s_game.k[i].a = rd32(q);
        s_game.k[i].b = rd32(q + 4);
        s_game.k[i].adj = rdF(q + 8);
    }
    memcpy(s_game.sheets, q, (size_t)ns * UI_GF_SHEET_NAME);
    for (uint32_t i = 0; i < ns; i++) {
        s_game.sheets[i][UI_GF_SHEET_NAME - 1] = '\0';
    }
    q += (size_t)ns * UI_GF_SHEET_NAME;
    memcpy(s_game.cov, q, (size_t)w * (size_t)h);
    s_game.w = w;
    s_game.h = h;
    s_game.em = em;
    s_game.cap = cap;
    s_game.space = space;
    s_game.ng = (int)ng;
    s_game.nk = (int)nk;
    s_game.nsheets = (int)ns;
    s_game.loaded = 1;
    return true;
}

bool ui_GameFaceLoaded(void)
{
    return s_game.loaded != 0;
}

static int gameActive(void)
{
    return s_game.loaded;
}

static const GGlyph *gameGlyph(uint32_t cp)
{
    int lo = 0, hi = s_game.ng - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (s_game.g[mid].cp == cp) {
            return &s_game.g[mid];
        }
        if (s_game.g[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static float gameKern(uint32_t a, uint32_t b)
{
    for (int i = 0; i < s_game.nk; i++) {
        if (s_game.k[i].a == a && s_game.k[i].b == b) {
            return s_game.k[i].adj;
        }
    }
    return 0.0f;
}

static int isSpace(uint32_t cp)
{
    return cp == ' ' || cp == 0xA0;
}

int ui_FontFaceOf(uint32_t cp)
{
    if (gameActive() && (gameGlyph(cp) || isSpace(cp))) {
        return UI_FACE_GAME;
    }
    return ui_FontHasGlyph(cp) ? UI_FACE_ARIMO : -1;
}

bool ui_GameFaceSource(uint32_t cp, const char **sheet, int *lang, int *count)
{
    const GGlyph *g = s_game.loaded ? gameGlyph(cp) : NULL;
    if (!g) {
        return false;
    }
    if (sheet) {
        *sheet = g->sheet < s_game.nsheets ? s_game.sheets[g->sheet] : "";
    }
    if (lang) {
        *lang = g->lang;
    }
    if (count) {
        *count = g->count;
    }
    return true;
}

int ui_GameFaceChars(uint32_t *cps, int cap)
{
    for (int i = 0; cps && i < s_game.ng && i < cap; i++) {
        cps[i] = s_game.g[i].cp;
    }
    return s_game.loaded ? s_game.ng : 0;
}

/* the code points the game face lacks, drawn with Arimo: each logged once */
static uint32_t s_fallback[MISSING_MAX];
static int s_fallbackN;

static void noteFallback(uint32_t cp)
{
    for (int i = 0; i < s_fallbackN; i++) {
        if (s_fallback[i] == cp) {
            return;
        }
    }
    if (s_fallbackN < MISSING_MAX) {
        s_fallback[s_fallbackN++] = cp;
        fprintf(stderr, "ui: the game's lettering has no U+%04X; drawn with Arimo\n", (unsigned)cp);
    }
}

int ui_FontFallbackSeen(uint32_t *cps, int cap)
{
    for (int i = 0; cps && i < s_fallbackN && i < cap; i++) {
        cps[i] = s_fallback[i];
    }
    return s_fallbackN;
}

/* grid units of the game face at size: x units and y units per main texel
   (a texel of the sheets is a pixel wide and a field line, two y units,
   tall; the main em, 13.5 texels, is 27 y units) */
static void gameUnits(float size, float *mx, float *my)
{
    const float m = size / (2.0f * s_game.em);
    *mx = m;
    *my = 2.0f * m;
}

/* the capitals' height of the game face at size, y units */
static float gameCapY(float size)
{
    float mx, my;
    gameUnits(size, &mx, &my);
    return s_game.cap * my;
}

/* the size Arimo stands in at for a game-face size: its capitals the game
   glyphs' height */
static float arimoSizeFor(float size)
{
    const float upem = 1.0f / stbtt_ScaleForMappingEmToPixels(&s_font.info, 1.0f);
    const float arimoCap = (float)s_font.capHeight / upem; /* per em */
    return gameCapY(size) / arimoCap;
}

/* The game face's passes (UI.md "The font"): the ink (text drawn without
   the rim: its light fill, blend 0x44); the sheets' alpha in black (0x44)
   and then their light added in the text's colour (0x48), which together
   are the sprite's MODULATE blend of the sheet's texels; Arimo's halo
   copies for the fallback letters */
/* GP_ARIMO: Arimo's letters alone (after the game letters' alpha and light) */
enum { GP_INK = 0, GP_ALPHA = 1, GP_LIGHT = 2, GP_ARIMO_HALO = 3, GP_ARIMO = 4, GP_GLOW = 5 };

#define MQ_GAME 0x100 /* the game atlas, as a quad's texture */

typedef struct MQuad {
    float x0, y0, x1, y1; /* grid units from the pen origin (x) and the first baseline (y) */
    int tex;              /* an Arimo page, or MQ_GAME */
    int arimo;
    int line;
    int u0, v0, u1, v1;
} MQuad;

typedef struct MPlaced {
    const GGlyph *g;   /* the game face's glyph, or */
    const UiGlyph *ag; /* Arimo's */
    float penX, penY;  /* grid units */
    int line;
    int first, last; /* the first or last game letter of a word */
} MPlaced;

typedef struct MLayout {
    MPlaced *p;
    int n, max;
    float lineW[MAX_LINES];
    int lines;
    float widest;
    float mx, my, axs, ays;
    SizeSet *z; /* Arimo's set for the fallback */
} MLayout;

/* Lays utf8 out in both faces, in grid units: x from the line's start, y
   from the first baseline (y down).  The glyphs are kept when L->p is set.
   The game face's advances, kerning and space are whole texels, so at its
   own size a word's cells tile as the sheet's columns did. */
static void gameLayout(float size, const char *utf8, MLayout *L)
{
    gameUnits(size, &L->mx, &L->my);
    const float mx = L->mx;
    const float sa = arimoSizeFor(size);
    const int apx = pxFor(sa);
    SizeSet *z = sizeSet(apx);
    L->ays = z ? (float)apx / (float)z->px / s_font.scale : 0.0f;
    L->axs = L->ays * UI_X_PER_Y;
    /* the line pitch as Arimo's at the requested size */
    const int lpx = pxFor(size);
    const float lineStep = (float)(s_font.ascent - s_font.descent + s_font.lineGap) *
                           stbtt_ScaleForMappingEmToPixels(&s_font.info, (float)lpx) / s_font.scale;
    float penX = 0.0f, penY = 0.0f;
    uint32_t prev = 0;
    int prevGame = 0, line = 0, inWord = 0, lastGame = -1;
    const char *s = utf8;
    uint32_t cp;
    L->z = z;
    L->widest = 0.0f;
    L->n = 0;
    while ((cp = ui_Utf8Next(&s)) != 0) {
        const GGlyph *g = (cp == '\n' || isSpace(cp)) ? NULL : gameGlyph(cp);
        if (!g && inWord) {
            if (L->p && lastGame >= 0) {
                L->p[lastGame].last = 1;
            }
            inWord = 0;
        }
        if (cp == '\n') {
            if (line < MAX_LINES) {
                L->lineW[line] = penX;
            }
            L->widest = penX > L->widest ? penX : L->widest;
            line++;
            penX = 0.0f;
            penY += lineStep;
            prev = 0;
            continue;
        }
        if (isSpace(cp)) {
            penX += s_game.space * mx;
            prev = cp;
            prevGame = 1;
            continue;
        }
        if (g) {
            if (prev && prevGame) {
                penX += gameKern(prev, cp) * mx;
            }
            if (L->p && L->n < L->max) {
                MPlaced *pl = &L->p[L->n];
                memset(pl, 0, sizeof(*pl));
                pl->g = g;
                pl->penX = penX;
                pl->penY = penY;
                pl->line = line;
                pl->first = !inWord;
                lastGame = L->n++;
            }
            inWord = 1;
            penX += g->adv * mx;
            prev = cp;
            prevGame = 1;
            continue;
        }
        noteFallback(cp);
        if (!z) {
            continue;
        }
        if (prev && !prevGame) {
            penX +=
                (float)stbtt_GetGlyphKernAdvance(&s_font.info, glyphIndex(prev), glyphIndex(cp)) *
                z->scale * L->axs;
        }
        const UiGlyph *ag = glyphIn(z, cp);
        if (ag) {
            if (L->p && ag->w > 0 && L->n < L->max) {
                MPlaced *pl = &L->p[L->n++];
                memset(pl, 0, sizeof(*pl));
                pl->ag = ag;
                pl->penX = penX;
                pl->penY = penY;
                pl->line = line;
            }
            penX += ag->advance * L->axs;
        }
        prev = cp;
        prevGame = 0;
    }
    if (inWord && L->p && lastGame >= 0) {
        L->p[lastGame].last = 1;
    }
    if (line < MAX_LINES) {
        L->lineW[line] = penX;
    }
    L->widest = penX > L->widest ? penX : L->widest;
    L->lines = line + 1;
}

/* one alpha or light cell of a game glyph as a quad: its inner texels
   (inside the apron), from the pen at grid (px, py) */
static void gameCellQuad(const MLayout *L, const GGlyph *g, int k, int light, int variant, float px,
                         float py, MQuad *q)
{
    const GPair *c = &g->c[k];
    const float sx = g->scale * L->mx, sy = g->scale * L->my;
    const int inner = c->w - 2;
    const float x0 = k == GG_MAIN   ? px
                     : k == GG_LEFT ? px - (float)inner * sx
                                    : px + g->adv * L->mx;
    q->x0 = x0;
    /* the main cell ends exactly where the next letter's starts (the same
       sum), so a word's tiles leave no hairline between them */
    q->x1 = k == GG_MAIN ? px + g->adv * L->mx : k == GG_LEFT ? px : x0 + (float)inner * sx;
    q->y0 = py + c->top * L->my;
    q->y1 = q->y0 + (float)(c->h - 2) * sy;
    q->u0 = (light ? c->px : c->ax) + 1;
    q->v0 = (light ? c->py : c->ay) + 1;
    if (k == GG_MAIN && variant > 0) {
        /* the main cell at a word's start, end, or alone */
        q->u0 = g->mv[variant - 1][light ? 2 : 0] + 1;
        q->v0 = g->mv[variant - 1][light ? 3 : 1] + 1;
    }
    q->u1 = q->u0 + inner;
    q->v1 = q->v0 + c->h - 2;
    q->tex = MQ_GAME;
    q->arimo = 0;
}

/* the quads of one pass */
static int gameQuads(const MLayout *L, int pass, MQuad *out, int max)
{
    int n = 0;
    for (int i = 0; i < L->n && n + 3 <= max; i++) {
        const MPlaced *pl = &L->p[i];
        if (pl->ag) {
            if (pass == GP_ALPHA || pass == GP_LIGHT || pass == GP_GLOW) {
                continue;
            }
            const UiGlyph *ag = pl->ag;
            MQuad *q = &out[n++];
            q->x0 = pl->penX + ag->xoff * L->axs;
            q->y0 = pl->penY + ag->yoff * L->ays;
            q->x1 = q->x0 + (float)ag->w * L->axs;
            q->y1 = q->y0 + (float)ag->h * L->ays;
            q->tex = ag->page;
            q->arimo = 1;
            q->line = pl->line;
            q->u0 = ag->x;
            q->v0 = ag->y;
            q->u1 = ag->x + ag->w;
            q->v1 = ag->y + ag->h;
            continue;
        }
        if (pass == GP_ARIMO_HALO || pass == GP_ARIMO) {
            continue;
        }
        const GGlyph *g = pl->g;
        if (pass == GP_INK) {
            MQuad *q = &out[n++];
            q->x0 = pl->penX + g->idx * L->mx;
            q->y0 = pl->penY + g->idy * L->my;
            q->x1 = q->x0 + (float)g->iw * g->scale * L->mx;
            q->y1 = q->y0 + (float)g->ih * g->scale * L->my;
            q->tex = MQ_GAME;
            q->arimo = 0;
            q->line = pl->line;
            q->u0 = g->ix;
            q->v0 = g->iy;
            q->u1 = g->ix + g->iw;
            q->v1 = g->iy + g->ih;
            continue;
        }
        if (pass == GP_GLOW) {
            MQuad *q = &out[n++];
            q->x0 = pl->penX + g->gdx * L->mx;
            q->y0 = pl->penY + g->gdy * L->my;
            q->x1 = q->x0 + (float)g->gw * g->scale * L->mx;
            q->y1 = q->y0 + (float)g->gh * g->scale * L->my;
            q->tex = MQ_GAME;
            q->arimo = 0;
            q->line = pl->line;
            const int v = (pl->first ? 1 : 0) | (pl->last ? 2 : 0);
            q->u0 = g->gx[v];
            q->v0 = g->gy[v];
            q->u1 = g->gx[v] + g->gw;
            q->v1 = g->gy[v] + g->gh;
            continue;
        }
        const int light = pass == GP_LIGHT;
        const int variant = (pl->first ? 1 : 0) | (pl->last ? 2 : 0);
        gameCellQuad(L, g, GG_MAIN, light, variant, pl->penX, pl->penY, &out[n]);
        out[n++].line = pl->line;
        if (pl->first) {
            gameCellQuad(L, g, GG_LEFT, light, 0, pl->penX, pl->penY, &out[n]);
            out[n++].line = pl->line;
        }
        if (pl->last) {
            gameCellQuad(L, g, GG_RIGHT, light, 0, pl->penX, pl->penY, &out[n]);
            out[n++].line = pl->line;
        }
    }
    return n;
}

#ifdef ICO_RD
static uint32_t gameTexture(void)
{
    if (!s_game.tex && s_game.loaded) {
        const size_t texels = (size_t)s_game.w * (size_t)s_game.h;
        uint8_t *gs = malloc(texels);
        if (!gs) {
            return 0;
        }
        gsCoverage(gs, s_game.cov, texels);
        s_game.tex =
            rd_CreateTextureR8((uint32_t)s_game.w, (uint32_t)s_game.h, gs, "ui game font").id;
        free(gs);
    }
    return s_game.tex;
}
#endif

static void gameDrawPass(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                         unsigned flags, const UiXform *xf, int pass)
{
    MLayout L;
    memset(&L, 0, sizeof(L));
    L.max = MAX_QUADS;
    L.p = malloc(sizeof(MPlaced) * (size_t)L.max);
    MQuad *qs = malloc(sizeof(MQuad) * (size_t)L.max * 3);
    if (!L.p || !qs) {
        free(L.p);
        free(qs);
        return;
    }
    gameLayout(size, utf8, &L);
    const int nq = gameQuads(&L, pass, qs, L.max * 3);
    float base;
    switch (flags & UI_VALIGN_MASK) {
    case UI_VALIGN_MIDDLE:
        base = y + gameCapY(size) * 0.5f;
        break;
    case UI_VALIGN_BASELINE:
        base = y;
        break;
    default: {
        float asc = 0.0f;
        ui_FontMetrics(size, &asc, NULL, NULL);
        base = y + asc;
        break;
    }
    }
    /* the game face on whole texels: the baseline and each line's start on
       the sheets' texel grid at this size (at the sheets' size the cells sit
       texel for texel where a sprite's texels would) */
    base = floorf(base / L.my + 0.5f) * L.my;
    /* the light pass adds (0x48) */
    const unsigned passFlags = pass == GP_LIGHT ? flags | UI_ADDITIVE : flags;
#ifdef ICO_RD
    if (nq > 0) {
        RdScreenVtx *v = malloc(sizeof(RdScreenVtx) * 2 * (size_t)nq);
        if (!v) {
            free(L.p);
            free(qs);
            return;
        }
        int texIds[MAX_PAGES + 1], ntex = 0;
        for (int i = 0; i < nq; i++) {
            int t = qs[i].tex, seen = 0;
            for (int k = 0; k < ntex; k++) {
                seen |= texIds[k] == t;
            }
            if (!seen && ntex < MAX_PAGES + 1) {
                texIds[ntex++] = t;
            }
        }
        /* a pass that adds where the caller's packet holds the plain blend
           (UI_KEEP_STATE): set it, and the plain one again after */
        const int setAdd = pass == GP_LIGHT && (flags & UI_KEEP_STATE) && !(flags & UI_ADDITIVE);
        if (!s_ov.active) {
            setState(passFlags);
            if (setAdd) {
                rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0, 1);
            }
            rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        }
        for (int k = 0; k < ntex; k++) {
            const int t = texIds[k];
            uint32_t nv = 0;
            for (int i = 0; i < nq; i++) {
                const MQuad *q = &qs[i];
                if (q->tex != t) {
                    continue;
                }
                const int line = q->line < MAX_LINES ? q->line : MAX_LINES - 1;
                const float w = L.lineW[line];
                float ox = (flags & UI_ALIGN_MASK) == UI_ALIGN_CENTER  ? -w * 0.5f
                           : (flags & UI_ALIGN_MASK) == UI_ALIGN_RIGHT ? -w
                                                                       : 0.0f;
                ox = floorf((x + ox) / L.mx + 0.5f) * L.mx - x;
                float ax = x + ox + q->x0, ay = base + q->y0, bx = x + ox + q->x1,
                      by = base + q->y1;
                mapXf(xf, &ax, &ay);
                mapXf(xf, &bx, &by);
                RdScreenVtx *a = &v[nv++], *b = &v[nv++];
                memset(a, 0, sizeof(*a));
                memset(b, 0, sizeof(*b));
                if (s_ov.active) {
                    float ax16, ay16, bx16, by16;
                    ui_OverlayMap(ax, ay, &ax16, &ay16);
                    ui_OverlayMap(bx, by, &bx16, &by16);
                    if (q->arimo) {
                        /* Arimo: a texel a pixel, as ui_DrawTextXf */
                        a->x = ovPix(ax16);
                        a->y = ovPix(ay16);
                        b->x = a->x + ovPix(bx16 - ax16);
                        b->y = a->y + ovPix(by16 - ay16);
                    } else {
                        /* the game face: the sheet's texels scaled, continuous */
                        a->x = (int32_t)lrintf(ax16);
                        a->y = (int32_t)lrintf(ay16);
                        b->x = (int32_t)lrintf(bx16);
                        b->y = (int32_t)lrintf(by16);
                    }
                } else {
                    a->x = gsX(ax);
                    a->y = gsY(ay);
                    b->x = gsX(bx);
                    b->y = gsY(by);
                    a->z = b->z = s_font.frame.z;
                }
                a->s = (float)(q->u0 * 16);
                a->t = (float)(q->v0 * 16);
                b->s = (float)(q->u1 * 16);
                b->t = (float)(q->v1 * 16);
                a->q = b->q = 1.0f;
                memcpy(a->rgba, rgba, 4);
                memcpy(b->rgba, rgba, 4);
            }
            if (nv == 0) {
                continue;
            }
            const uint32_t tex = t == MQ_GAME                  ? gameTexture()
                                 : (L.z && t < L.z->pageCount) ? pageTexture(L.z, t)
                                                               : 0;
            if (!tex) {
                continue;
            }
            if (s_ov.active) {
                ovEmit(v, nv, tex, passFlags);
            } else {
                rd_Texture((RdTex){tex}, RD_TEXFN_MODULATE, RD_TCC_RGBA);
                rd_ScreenPrims(RD_PRIM_SPRITES, v, nv, RD_SPACE_UI, RD_UV_FIXED_CONTINUOUS,
                               textKey(utf8, flags, t == MQ_GAME ? MQ_GAME + pass : t));
            }
        }
        if (!s_ov.active && setAdd) {
            rd_Blend(RD_BLEND_LERP_AS, 0, 1);
        }
        free(v);
    }
#else
    (void)x;
    (void)rgba;
    (void)xf;
    (void)base;
    (void)passFlags;
    (void)nq;
#endif
    free(L.p);
    free(qs);
}

/* the game face's draw (UI.md "The font"):
   - with UI_HALO: Arimo's fallback letters' eight dark copies; the game
     letters' fitted glow in black (each letter's whole reach, overlapping),
     their sheet alpha in black (the text's alpha) and their sheet light
     added in the text's colour (with the alpha, the sprite's blend of the
     sheet's texels near the letters); then Arimo's letters;
   - UI_ADDITIVE (the glow, an additive sprite of the sheet: col L A): the
     light added, and Arimo's letters added;
   - else (black, white or grey letters without a rim): the letters' ink */
static void gameDraw(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                     unsigned flags, const UiXform *xf)
{
    if (flags & UI_HALO) {
        flags &= ~(unsigned)UI_HALO;
        static const float dirs[8][2] = {{-1, 0},  {1, 0},  {0, -1}, {0, 1},
                                         {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        const uint8_t dark[4] = {0, 0, 0, (uint8_t)(rgba[3] / 4)};
        const float r = 1.5f * size / UI_MENU_TEXT_SIZE;
        int anyArimo = 0, anyGame = 0;
        for (const char *s = utf8; *s;) {
            const uint32_t cp = ui_Utf8Next(&s);
            if (cp != '\n' && !isSpace(cp)) {
                anyArimo |= !gameGlyph(cp);
                anyGame |= gameGlyph(cp) != NULL;
            }
        }
        if (anyArimo) {
            for (int i = 0; i < 8; i++) {
                const float k = (dirs[i][0] != 0.0f && dirs[i][1] != 0.0f) ? 0.7071f : 1.0f;
                gameDrawPass(x + dirs[i][0] * r * k * UI_X_PER_Y, y + dirs[i][1] * r * k, size,
                             dark, utf8, flags, xf, GP_ARIMO_HALO);
            }
        }
        if (anyGame && !(flags & UI_ADDITIVE)) {
            const uint8_t black[4] = {0, 0, 0, rgba[3]};
            gameDrawPass(x, y, size, black, utf8, flags, xf, GP_GLOW);
            gameDrawPass(x, y, size, black, utf8, flags, xf, GP_ALPHA);
            gameDrawPass(x, y, size, rgba, utf8, flags, xf, GP_LIGHT);
        }
        if (anyArimo) {
            gameDrawPass(x, y, size, rgba, utf8, flags, xf, GP_ARIMO);
        }
        return;
    }
    if (flags & UI_ADDITIVE) {
        gameDrawPass(x, y, size, rgba, utf8, flags, xf, GP_LIGHT);
        gameDrawPass(x, y, size, rgba, utf8, flags, xf, GP_ARIMO);
        return;
    }
    gameDrawPass(x, y, size, rgba, utf8, flags, xf, GP_INK);
}

static float gameMeasure(float size, const char *utf8)
{
    MLayout L;
    memset(&L, 0, sizeof(L));
    gameLayout(size, utf8, &L);
    return L.widest;
}

static void drawTextXf(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                       unsigned flags, const UiXform *xf, int soft);

void ui_DrawTextXf(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                   unsigned flags, const UiXform *xf)
{
    if (s_ov.active && (flags & UI_ADDITIVE)) {
        /* package GHOST: the glow on the output is rasterised at the menu
           sheets' texel density and magnified (font.h UI_GLOW_SCALE) */
        const float keep = s_font.scale;
        s_font.scale = UI_GLOW_SCALE;
        drawTextXf(x, y, size, rgba, utf8, flags, xf, 1);
        s_font.scale = keep;
        return;
    }
    drawTextXf(x, y, size, rgba, utf8, flags, xf, 0);
}

static void drawTextXf(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                       unsigned flags, const UiXform *xf, int soft)
{
    if (!utf8 || !*utf8 || !ui_FontInit()) {
        return;
    }
    if (gameActive()) {
        gameDraw(x, y, size, rgba, utf8, flags, xf);
        return;
    }
    if (flags & UI_HALO) {
        drawHalo(x, y, size, rgba, utf8, flags, xf);
        flags &= ~(unsigned)UI_HALO;
    }
    const int px = pxFor(size);
    SizeSet *z = sizeSet(px);
    if (!z) {
        return;
    }
    /* y units per atlas pixel (a stand-in size scales too) */
    const float ys = (float)px / (float)z->px / s_font.scale;
    const float xs = ys * UI_X_PER_Y;
    float widths[MAX_LINES];
    int lines = 1;
    memset(widths, 0, sizeof(widths));
    layoutRun(z, utf8, NULL, NULL, widths, MAX_LINES, &lines);

    Collect c;
    memset(&c, 0, sizeof(c));
    c.q = malloc(sizeof(Quad) * MAX_QUADS);
    if (!c.q) {
        return;
    }
    c.max = MAX_QUADS;
    c.lineStep = (float)(s_font.ascent - s_font.descent + s_font.lineGap) * z->scale;
    for (int i = 0; i < MAX_LINES; i++) {
        float w = i < lines ? widths[i] : 0.0f;
        switch (flags & UI_ALIGN_MASK) {
        case UI_ALIGN_CENTER:
            c.originX[i] = -w * 0.5f;
            break;
        case UI_ALIGN_RIGHT:
            c.originX[i] = -w;
            break;
        default:
            c.originX[i] = 0.0f;
            break;
        }
    }
    layoutRun(z, utf8, collectGlyph, &c, NULL, 0, NULL);

    /* the first baseline, in grid units */
    float base;
    switch (flags & UI_VALIGN_MASK) {
    case UI_VALIGN_MIDDLE:
        base = y + (float)s_font.capHeight * z->scale * ys * 0.5f;
        break;
    case UI_VALIGN_BASELINE:
        base = y;
        break;
    default:
        base = y + (float)s_font.ascent * z->scale * ys;
        break;
    }
    for (int i = 0; i < c.n; i++) {
        Quad *q = &c.q[i];
        q->x0 = x + q->x0 * xs;
        q->x1 = x + q->x1 * xs;
        q->y0 = base + q->y0 * ys;
        q->y1 = base + q->y1 * ys;
    }
#ifdef ICO_RD
    if (c.n > 0 && s_ov.active) {
        /* package OV: on the output, each quad's corner on a whole pixel,
           its size kept (a texel a pixel) */
        RdScreenVtx *v = malloc(sizeof(RdScreenVtx) * 2 * (size_t)c.n);
        for (int page = 0; v && page < z->pageCount; page++) {
            uint32_t n = 0;
            for (int i = 0; i < c.n; i++) {
                const Quad *q = &c.q[i];
                if (q->page != page) {
                    continue;
                }
                /* a magnified glyph takes one gutter texel (zero) on each
                   side, so its edge fades out instead of being cut */
                const int m = soft ? 1 : 0;
                float ax = q->x0 - (float)m * xs, ay = q->y0 - (float)m * ys;
                float bx = q->x1 + (float)m * xs, by = q->y1 + (float)m * ys;
                mapXf(xf, &ax, &ay);
                mapXf(xf, &bx, &by);
                float ax16, ay16, bx16, by16;
                ui_OverlayMap(ax, ay, &ax16, &ay16);
                ui_OverlayMap(bx, by, &bx16, &by16);
                RdScreenVtx *a = &v[n++], *b = &v[n++];
                memset(a, 0, sizeof(*a));
                memset(b, 0, sizeof(*b));
                if (soft) {
                    /* magnified: continuous, sampled linearly */
                    a->x = (int32_t)lrintf(ax16);
                    a->y = (int32_t)lrintf(ay16);
                    b->x = (int32_t)lrintf(bx16);
                    b->y = (int32_t)lrintf(by16);
                } else {
                    a->x = ovPix(ax16);
                    a->y = ovPix(ay16);
                    b->x = a->x + ovPix(bx16 - ax16);
                    b->y = a->y + ovPix(by16 - ay16);
                }
                a->s = (float)((q->u0 - m) * 16);
                a->t = (float)((q->v0 - m) * 16);
                b->s = (float)((q->u1 + m) * 16);
                b->t = (float)((q->v1 + m) * 16);
                a->q = b->q = 1.0f;
                memcpy(a->rgba, rgba, 4);
                memcpy(b->rgba, rgba, 4);
            }
            const uint32_t tex = n ? pageTexture(z, page) : 0;
            if (tex) {
                ovEmit(v, n, tex, flags);
            }
        }
        free(v);
    } else if (c.n > 0) {
        RdScreenVtx *v = malloc(sizeof(RdScreenVtx) * 2 * (size_t)c.n);
        if (v) {
            setState(flags);
            rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
            for (int page = 0; page < z->pageCount; page++) {
                uint32_t n = 0;
                for (int i = 0; i < c.n; i++) {
                    const Quad *q = &c.q[i];
                    if (q->page != page) {
                        continue;
                    }
                    float ax = q->x0, ay = q->y0, bx = q->x1, by = q->y1;
                    mapXf(xf, &ax, &ay);
                    mapXf(xf, &bx, &by);
                    RdScreenVtx *a = &v[n++], *b = &v[n++];
                    memset(a, 0, sizeof(*a));
                    memset(b, 0, sizeof(*b));
                    a->x = gsX(ax);
                    a->y = gsY(ay);
                    b->x = gsX(bx);
                    b->y = gsY(by);
                    a->z = b->z = s_font.frame.z;
                    a->s = (float)(q->u0 * 16);
                    a->t = (float)(q->v0 * 16);
                    b->s = (float)(q->u1 * 16);
                    b->t = (float)(q->v1 * 16);
                    a->q = b->q = 1.0f;
                    memcpy(a->rgba, rgba, 4);
                    memcpy(b->rgba, rgba, 4);
                }
                if (n == 0) {
                    continue;
                }
                uint32_t tex = pageTexture(z, page);
                if (!tex) {
                    continue;
                }
                rd_Texture((RdTex){tex}, RD_TEXFN_MODULATE, RD_TCC_RGBA);
                /* T1: continuous quads (no GS-pixel snap on a scaled target) */
                rd_ScreenPrims(RD_PRIM_SPRITES, v, n, RD_SPACE_UI, RD_UV_FIXED_CONTINUOUS,
                               textKey(utf8, flags, page));
            }
            free(v);
        }
    }
#else
    (void)rgba;
    (void)xf;
    (void)soft;
#endif
    free(c.q);
}

void ui_DrawText(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                 unsigned flags)
{
    ui_DrawTextXf(x, y, size, rgba, utf8, flags, NULL);
}

/* ------------------------------------------- deferred text (package DEF) */

#ifdef ICO_RD
/* the key of an item: the quads' (textKey) with a page no atlas has */
#define DEFER_KEY_PAGE 0x7FFF

void ui_DrawTextDeferred(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                         unsigned flags, const UiXform *xf)
{
    if (!utf8 || !*utf8 || !ui_FontInit()) {
        return;
    }
    if (s_ov.active) {
        ui_DrawTextXf(x, y, size, rgba, utf8, flags, xf);
        return;
    }
    /* what the decoder still holds belongs before the item, as before the
       quads (setState) */
    if (s_font.recordHook && s_font.suppress <= 0) {
        s_font.recordHook();
    }
    RdTextItem it;
    memset(&it, 0, sizeof(it));
    size_t n = strlen(utf8);
    if (n > RD_TEXT_BYTES - 1) {
        n = RD_TEXT_BYTES - 1;
        while (n > 0 && ((unsigned char)utf8[n] & 0xC0) == 0x80) {
            n--; /* not inside a code point */
        }
    }
    memcpy(it.utf8, utf8, n);
    it.x = x;
    it.y = y;
    it.size = size;
    it.flags = flags & ~(unsigned)UI_KEEP_STATE;
    memcpy(it.rgba, rgba, 4);
    it.additive = (flags & UI_ADDITIVE) ? 1 : 0;
    if (xf) {
        it.hasXf = 1;
        it.xf[0] = xf->originX;
        it.xf[1] = xf->originY;
        it.xf[2] = xf->scaleX;
        it.xf[3] = xf->scaleY;
        it.xf[4] = xf->offsetX;
        it.xf[5] = xf->offsetY;
    }
    rd_DeferredText(&it, textKey(it.utf8, flags, DEFER_KEY_PAGE));
    rd_DeferredTextQuads(1);
    ui_DrawTextXf(x, y, size, rgba, utf8, flags, xf);
    rd_DeferredTextQuads(0);
}

/* the renderer rd calls at a present, once per item and region: the item
   through overlay mode, which rasterises it at the box's scale and puts each
   glyph's corner on a whole output pixel */
static void deferredDraw(const RdOverlayCtx *ctx, const RdTextItem *item, void *user)
{
    (void)user;
    ui_BeginOverlay(ctx);
    if (!s_ov.active) {
        return;
    }
    UiXform xf;
    if (item->hasXf) {
        xf.originX = item->xf[0];
        xf.originY = item->xf[1];
        xf.scaleX = item->xf[2];
        xf.scaleY = item->xf[3];
        xf.offsetX = item->xf[4];
        xf.offsetY = item->xf[5];
    }
    ui_DrawTextXf(item->x, item->y, item->size, item->rgba, item->utf8, item->flags,
                  item->hasXf ? &xf : NULL);
    ui_EndOverlay();
}

void ui_InstallDeferredText(int on)
{
    rd_SetDeferredTextFn(on ? deferredDraw : NULL, NULL);
}
#else
void ui_DrawTextDeferred(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                         unsigned flags, const UiXform *xf)
{
    ui_DrawTextXf(x, y, size, rgba, utf8, flags, xf);
}

void ui_InstallDeferredText(int on)
{
    (void)on;
}
#endif

static void drawHalo(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                     unsigned flags, const UiXform *xf)
{
    /* eight copies around the text, 1.5 y units out (an x unit is 14/15 of
       a y unit), black at a quarter of the text's alpha: about the darkened
       rim the menu textures have */
    static const float dirs[8][2] = {{-1, 0},  {1, 0},  {0, -1}, {0, 1},
                                     {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    const uint8_t dark[4] = {0, 0, 0, (uint8_t)(rgba[3] / 4)};
    const float r = 1.5f * size / UI_MENU_TEXT_SIZE;
    for (int i = 0; i < 8; i++) {
        const float k = (dirs[i][0] != 0.0f && dirs[i][1] != 0.0f) ? 0.7071f : 1.0f;
        ui_DrawTextXf(x + dirs[i][0] * r * k * UI_X_PER_Y, y + dirs[i][1] * r * k, size, dark, utf8,
                      flags & ~(unsigned)UI_HALO, xf);
    }
}

void ui_DrawRect(float x0, float y0, float x1, float y1, const uint8_t rgba[4])
{
#ifdef ICO_RD
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    if (s_ov.active) {
        /* package OV: both corners on whole output pixels */
        float ax, ay, bx, by;
        ui_OverlayMap(x0, y0, &ax, &ay);
        ui_OverlayMap(x1, y1, &bx, &by);
        v[0].x = ovPix(ax);
        v[0].y = ovPix(ay);
        v[1].x = ovPix(bx);
        v[1].y = ovPix(by);
        v[0].q = v[1].q = 1.0f;
        memcpy(v[0].rgba, rgba, 4);
        memcpy(v[1].rgba, rgba, 4);
        ovEmit(v, 2, 0, 0);
        return;
    }
    v[0].x = gsX(x0);
    v[0].y = gsY(y0);
    v[1].x = gsX(x1);
    v[1].y = gsY(y1);
    v[0].z = v[1].z = s_font.frame.z;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, rgba, 4);
    memcpy(v[1].rgba, rgba, 4);
    setState(0);
    rd_TextureOff();
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, rectKey());
#else
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)rgba;
#endif
}
