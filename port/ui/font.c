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
    s_font.inited = 0;
    s_font.failed = 0;
    s_font.warnedSizes = s_font.warnedPages = 0;
}

void ui_FontForgetTextures(void)
{
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
        *capHeight = (float)s_font.capHeight * sc;
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

void ui_DrawTextXf(float x, float y, float size, const uint8_t rgba[4], const char *utf8,
                   unsigned flags, const UiXform *xf)
{
    if (!utf8 || !*utf8 || !ui_FontInit()) {
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
                float ax = q->x0, ay = q->y0, bx = q->x1, by = q->y1;
                mapXf(xf, &ax, &ay);
                mapXf(xf, &bx, &by);
                float ax16, ay16, bx16, by16;
                ui_OverlayMap(ax, ay, &ax16, &ay16);
                ui_OverlayMap(bx, by, &bx16, &by16);
                RdScreenVtx *a = &v[n++], *b = &v[n++];
                memset(a, 0, sizeof(*a));
                memset(b, 0, sizeof(*b));
                a->x = ovPix(ax16);
                a->y = ovPix(ay16);
                b->x = a->x + ovPix(bx16 - ax16);
                b->y = a->y + ovPix(by16 - ay16);
                a->s = (float)(q->u0 * 16);
                a->t = (float)(q->v0 * 16);
                b->s = (float)(q->u1 * 16);
                b->t = (float)(q->v1 * 16);
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
