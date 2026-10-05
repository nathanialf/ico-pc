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

#define PAGE_W 512
#define PAGE_H 512
#define MAX_PAGES 4
#define MAX_SIZES 16
#define GLYPH_SLOTS 1024 /* per size, open addressing; a power of two */
#define MAX_QUADS 512    /* glyphs per draw call */

typedef struct Page {
    uint8_t *cov; /* PAGE_W x PAGE_H coverage */
    uint32_t tex; /* RdTex id, 0 before the first upload */
    int dirty;
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
    uint8_t *rgba; /* upload scratch, PAGE_W x PAGE_H x 4 */
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
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (ui_font_ttf_size < 12 || off < 0 || !stbtt_InitFont(&s_font.info, data, off)) {
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
    free(s_font.rgba);
    s_font.rgba = NULL;
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
            z->pages[p].dirty = 1;
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

void ui_SetScale(float scale)
{
    s_font.scale = scale > 0.25f ? scale : 0.25f;
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
    p->cov = calloc(PAGE_W * PAGE_H, 1);
    if (!p->cov) {
        return NULL;
    }
    p->shelfX = 1;
    p->shelfY = 1;
    p->shelfH = 0;
    p->dirty = 1;
    z->pageCount++;
    return p;
}

/* a w x h cell with a one-texel gutter, shelf packing */
static int allocCell(SizeSet *z, int w, int h, int *page, int *x, int *y)
{
    if (w + 2 > PAGE_W || h + 2 > PAGE_H) {
        return 0;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        Page *p = z->pageCount ? &z->pages[z->pageCount - 1] : newPage(z);
        if (!p) {
            return 0;
        }
        if (p->shelfX + w + 1 > PAGE_W) {
            p->shelfY += p->shelfH + 1;
            p->shelfX = 1;
            p->shelfH = 0;
        }
        if (p->shelfY + h + 1 <= PAGE_H) {
            *page = z->pageCount - 1;
            *x = p->shelfX;
            *y = p->shelfY;
            p->shelfX += w + 1;
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

static int glyphIndex(uint32_t cp)
{
    int g = stbtt_FindGlyphIndex(&s_font.info, (int)cp);
    if (g == 0 && cp != '?') {
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
        stbtt_MakeGlyphBitmap(&s_font.info, p->cov + y * PAGE_W + x, w, hgt, PAGE_W, z->scale,
                              z->scale, g);
        p->dirty = 1;
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

bool ui_FontHasGlyph(uint32_t cp)
{
    return ui_FontInit() && stbtt_FindGlyphIndex(&s_font.info, (int)cp) != 0;
}

const uint8_t *ui_FontPage(int px, int page, int *w, int *h)
{
    for (int i = 0; i < MAX_SIZES; i++) {
        SizeSet *z = &s_font.sizes[i];
        if (z->px == px && page >= 0 && page < z->pageCount) {
            *w = PAGE_W;
            *h = PAGE_H;
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
/* the page as rd sees it: RGBA8, white, alpha the coverage in GS units
   (255 -> 0x80, rounded) */
static uint32_t pageTexture(SizeSet *z, int page)
{
    Page *p = &z->pages[page];
    if (p->tex && !p->dirty) {
        return p->tex;
    }
    if (!s_font.rgba) {
        s_font.rgba = malloc((size_t)PAGE_W * PAGE_H * 4);
        if (!s_font.rgba) {
            return 0;
        }
    }
    for (int i = 0; i < PAGE_W * PAGE_H; i++) {
        uint8_t *o = &s_font.rgba[i * 4];
        o[0] = o[1] = o[2] = 0xFF;
        o[3] = (uint8_t)((p->cov[i] * 128 + 127) / 255);
    }
    if (!p->tex) {
        char name[32];
        snprintf(name, sizeof(name), "ui font %dpx p%d", z->px, page);
        p->tex = rd_CreateTexture(PAGE_W, PAGE_H, s_font.rgba, RD_TEXA_80_80, name).id;
    } else {
        rd_UpdateTexture((RdTex){p->tex}, s_font.rgba);
    }
    p->dirty = 0;
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
    if (c.n > 0) {
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
                rd_ScreenPrims(RD_PRIM_SPRITES, v, n, RD_SPACE_UI, 1, textKey(utf8, flags, page));
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
