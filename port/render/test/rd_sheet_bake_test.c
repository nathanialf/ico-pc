/* rd_sheet_bake_test.c: the 1x sheet texels the renderer bakes on the CPU
 * (rd_tex.c rd__sheet_bake_rect; rd_internal.h rd__sheet_baked) against
 * font_sheet_ps's CPU reference (sheet_ref.c sheetref_texel), CPU only:
 *
 *   glyphs   every glyph the fonts serve in the Latin, punctuation, symbol
 *            and arrow ranges, rasterised at 1x through font.c
 *            (ui__sheet_raster_line) as menu strips with menu_font.c's
 *            margins, packed on 1024-wide pages with its gaps (MF_GAP_X 7,
 *            MF_GAP_Y 5) on its 4-texel grid from its first corner (8):
 *            the whole page and its apron (ICO_SHEET_BAKE_AX/AY) baked in one
 *            rectangle is sheetref_texel's at every texel;
 *   corpus   the words of port/ui/test/font_corpus/<lang>.txt (argv[1]),
 *            each line a strip at the menus' two em sizes, packed the same
 *            way: the same, every texel;
 *   styles   each of the eight light inks' rim and fill levels (menu_font.c
 *            kSheetInk: English, French and Italian, German, Spanish,
 *            Japanese, Korean, Chinese), the faint rim (16 64ths) and the
 *            plain style (no rim), each with the dither on and off;
 *   updates  a strip placed and a strip's band cleared on a baked page:
 *            the changed rectangle grown by the apron (rd_upload.c's rule:
 *            the device rectangle [x, x + w + 2 AX) x [y, y + h + 2 AY)),
 *            baked into the page through a row pitch, leaves the page equal
 *            to a fresh bake of the new coverage;
 *   edges    ink touching the four edges of a small page: the apron is
 *            sheetref_texel's, and every texel past it (any distance) is
 *            the apron's outermost one's, the texel font_sheet_baked_ps's
 *            clamped read returns;
 *   rects    rectangles of any size and position bake the same texels as
 *            the whole page; an empty rectangle bakes nothing and returns
 *            1, a missing coverage returns 0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "rd_internal.h"
#include "shader_consts.h"
#include "sheet_ref.h"
#include "ui_internal.h"
#include "../../test/ico_check.h"

#define PAGE_W 1024
#define PAGE_H 1024
#define GAP_X 7  /* port/ui/menu_font.c MF_GAP_X */
#define GAP_Y 5  /* MF_GAP_Y */
#define ALIGN 4  /* MF_ALIGN */
#define ORIGIN 8 /* MF_ORIGIN */
#define MAX_W (PAGE_W - ORIGIN - GAP_X)
#define AX ICO_SHEET_BAKE_AX
#define AY ICO_SHEET_BAKE_AY
/* port/ui/menu_font.h's UI_MENU_RIM_X, UI_MENU_RIM_Y and
   UI_MENU_FAINT_WEIGHT (the header needs the game's layout headers) */
#define RIM_X 7
#define RIM_Y 5
#define FAINT_WEIGHT 16

_Static_assert(GAP_X >= ICO_SHEET_RX + 1 && GAP_Y >= ICO_SHEET_RY + 1,
               "the pages' gaps are menu_font.c's");

/* a page filled in shelves as menu_font.c shelfFit fills one (no reclaim) */
typedef struct Page {
    uint8_t *cov;
    int w, h;
    int x, y, shelfH;
    int strips;
} Page;

static int alignUp(int v)
{
    return (v + ALIGN - 1) / ALIGN * ALIGN;
}

static int pageInit(Page *p, int w, int h)
{
    memset(p, 0, sizeof(*p));
    p->cov = calloc((size_t)w * (size_t)h, 1);
    p->w = w;
    p->h = h;
    p->x = ORIGIN;
    p->y = ORIGIN;
    return p->cov != NULL;
}

/* the strip (sw x sh) on the page at the next place; 0 when it is full */
static int pagePlace(Page *p, const uint8_t *strip, int sw, int sh, int *ox, int *oy)
{
    if (p->x + sw + GAP_X > p->w) {
        p->y = alignUp(p->y + p->shelfH + GAP_Y);
        p->x = ORIGIN;
        p->shelfH = 0;
    }
    if (p->y + sh + GAP_Y > p->h || sw + ORIGIN + GAP_X > p->w) {
        return 0;
    }
    for (int r = 0; r < sh; r++) {
        memcpy(p->cov + (size_t)(p->y + r) * (size_t)p->w + (size_t)p->x,
               strip + (size_t)r * (size_t)sw, (size_t)sw);
    }
    if (ox) {
        *ox = p->x;
    }
    if (oy) {
        *oy = p->y;
    }
    p->x = alignUp(p->x + sw + GAP_X);
    p->shelfH = sh > p->shelfH ? sh : p->shelfH;
    p->strips++;
    return 1;
}

/* utf8[0, len) as a menu strip at em rows: menu_font.c's margins round the
   ink (the rim's reach and the bilinear read's texel, plus the letters'
   overhang); *w, *h its size; NULL without the font or the memory */
static uint8_t *rasterStrip(const char *utf8, size_t len, float em, int *w, int *h)
{
    float asc = 0.0f, desc = 0.0f, step = 0.0f, cap = 0.0f;
    ui__sheet_v_metrics(em, &asc, &desc, &step, &cap);
    const float padX = ceilf(em * 2.0f * UI_X_PER_Y * 0.12f) + (float)RIM_X;
    const float padY = ceilf(em * 0.12f) + (float)RIM_Y;
    const float lw = ui__sheet_line_width(em, 1.0f, 0.0f, utf8, len);
    int sw = (int)ceilf(lw + 2.0f * padX), sh = (int)ceilf(asc + desc + 2.0f * padY);
    sw = sw > MAX_W ? MAX_W : sw < 1 ? 1 : sw;
    sh = sh < 1 ? 1 : sh;
    uint8_t *cov = calloc((size_t)sw * (size_t)sh, 1);
    if (!cov) {
        return NULL;
    }
    ui__sheet_raster_line(cov, sw, sh, sw, em, 1.0f, 0.0f, padX, padY + asc, utf8, len, 1);
    *w = sw;
    *h = sh;
    return cov;
}

/* RdTexRec.sheet of a style, as rd_tex.c sheetStyleSet makes it */
static void styleBytes(const RdSheetStyle *s, uint8_t out[4])
{
    out[0] = s->rimOn ? (s->rimWeight && s->rimWeight < 64 ? s->rimWeight : 64) : 0;
    out[1] = s->rimLevel;
    out[2] = s->fillLevel;
    out[3] = s->dither ? 1 : 0;
}

typedef struct Style {
    const char *name;
    RdSheetStyle st;
} Style;

/* menu_font.c kSheetInk's rim and fill levels, the faint class
   (FAINT_WEIGHT) and the plain style (kPlainInk) */
static const Style kStyles[] = {
    {"English", {1, 24, 255, 1, 0, 1}},  {"French, Italian", {1, 61, 255, 1, 0, 1}},
    {"German", {1, 56, 255, 1, 0, 1}},   {"Spanish", {1, 62, 255, 1, 0, 1}},
    {"Japanese", {1, 34, 252, 1, 0, 1}}, {"Korean", {1, 42, 253, 1, 0, 1}},
    {"Chinese", {1, 39, 251, 1, 0, 1}},  {"faint", {1, 24, 255, 1, FAINT_WEIGHT, 1}},
    {"plain", {0, 0, 255, 1, 0, 1}},
};
#define STYLE_COUNT (int)(sizeof(kStyles) / sizeof(kStyles[0]))

/* the page and its apron baked in one rectangle, (w + 2 AX) x (h + 2 AY)
   RGBA8; NULL without the memory or when the bake refuses */
static uint8_t *bakeWhole(const Page *p, const uint8_t style[4])
{
    const int dw = p->w + 2 * AX, dh = p->h + 2 * AY;
    uint8_t *dev = malloc((size_t)dw * (size_t)dh * 4);
    if (dev && !rd__sheet_bake_rect(p->cov, (uint32_t)p->w, (uint32_t)p->h, style, -AX, -AY, dw, dh,
                                    dev, (size_t)dw * 4)) {
        free(dev);
        dev = NULL;
    }
    return dev;
}

/* the texels of dev (a whole bake) that differ from sheetref_texel's */
static long pageBad(const Page *p, const uint8_t *dev, const RdSheetStyle *st, const char *what)
{
    const int dw = p->w + 2 * AX, dh = p->h + 2 * AY;
    long bad = 0;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint8_t g, a;
            sheetref_texel(p->cov, (uint32_t)p->w, (uint32_t)p->h, x - AX, y - AY, st, &g, &a);
            const uint8_t *d = &dev[((size_t)y * (size_t)dw + (size_t)x) * 4];
            if (d[0] != g || d[1] != g || d[2] != g || d[3] != a) {
                if (bad < 8) {
                    printf("  %s: texel (%d, %d) baked %u %u %u %u, sheetref %u %u\n", what, x - AX,
                           y - AY, d[0], d[1], d[2], d[3], g, a);
                }
                bad++;
            }
        }
    }
    return bad;
}

/* every style, dither on and off, over the page */
static void checkStyles(const Page *p, const char *what)
{
    for (int k = 0; k < STYLE_COUNT; k++) {
        for (int d = 0; d < 2; d++) {
            RdSheetStyle st = kStyles[k].st;
            st.dither = (uint8_t)d;
            uint8_t sb[4];
            styleBytes(&st, sb);
            uint8_t *dev = bakeWhole(p, sb);
            CHECK(dev != NULL, "%s: the page baked (%s)", what, kStyles[k].name);
            if (!dev) {
                continue;
            }
            const long bad = pageBad(p, dev, &st, what);
            CHECK(bad == 0, "%s, %s, dither %d: %ld texels off sheetref_texel", what,
                  kStyles[k].name, d, bad);
            free(dev);
        }
    }
    printf("  %s: %d strips on a %dx%d page, %d styles with the dither on and off\n", what,
           p->strips, p->w, p->h, STYLE_COUNT);
}

/* the code point cp as UTF-8 into out (4 bytes); its length */
static size_t utf8Put(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

/* glyphs: every code point of the ranges the fonts serve, one strip each */
static void testGlyphs(void)
{
    static const uint32_t kRanges[][2] = {
        {0x20, 0x7F},     {0xA0, 0x250},    {0x2000, 0x2070}, {0x20A0, 0x20C0},
        {0x2100, 0x2190}, {0x2190, 0x2200}, {0x25A0, 0x2600},
    };
    Page p;
    if (!pageInit(&p, PAGE_W, PAGE_H)) {
        CHECK(0, "glyph page memory");
        return;
    }
    int glyphs = 0, pages = 0;
    for (size_t r = 0; r < sizeof(kRanges) / sizeof(kRanges[0]); r++) {
        for (uint32_t cp = kRanges[r][0]; cp < kRanges[r][1]; cp++) {
            if (!ui_font_has_glyph(cp)) {
                continue;
            }
            char u[4];
            const size_t n = utf8Put(cp, u);
            int sw = 0, sh = 0;
            uint8_t *strip = rasterStrip(u, n, 11.0f, &sw, &sh);
            if (!strip) {
                CHECK(0, "glyph U+%04X rasterised", (unsigned)cp);
                continue;
            }
            if (!pagePlace(&p, strip, sw, sh, NULL, NULL)) {
                /* full: this page checked, the glyph on a new one */
                checkStyles(&p, "glyphs");
                pages++;
                free(p.cov);
                if (!pageInit(&p, PAGE_W, PAGE_H)) {
                    free(strip);
                    CHECK(0, "glyph page memory");
                    return;
                }
                CHECK(pagePlace(&p, strip, sw, sh, NULL, NULL), "glyph U+%04X placed",
                      (unsigned)cp);
            }
            glyphs++;
            free(strip);
        }
    }
    if (p.strips) {
        checkStyles(&p, "glyphs");
        pages++;
    }
    free(p.cov);
    printf("  glyphs: %d through ui__sheet_raster_line at 1x, %d pages\n", glyphs, pages);
    CHECK(glyphs > 300, "glyphs: %d found in the fonts (the Latin ranges alone have more)", glyphs);
}

/* corpus: each text line of the nine files, at em 11 and 14 */
static void testCorpus(const char *dir)
{
    static const char *const kLangs[] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh", "ru"};
    Page p;
    if (!pageInit(&p, PAGE_W, PAGE_H)) {
        CHECK(0, "corpus page memory");
        return;
    }
    int lines = 0;
    for (size_t l = 0; l < sizeof(kLangs) / sizeof(kLangs[0]); l++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.txt", dir, kLangs[l]);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL, "the corpus file %s", path);
        if (!f) {
            continue;
        }
        char line[2048];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if (n == 0 || line[0] == '#') {
                continue;
            }
            for (int e = 0; e < 2; e++) {
                int sw = 0, sh = 0;
                uint8_t *strip = rasterStrip(line, n, e ? 14.0f : 11.0f, &sw, &sh);
                if (!strip) {
                    CHECK(0, "corpus line rasterised");
                    continue;
                }
                if (!pagePlace(&p, strip, sw, sh, NULL, NULL)) {
                    checkStyles(&p, "corpus");
                    free(p.cov);
                    if (!pageInit(&p, PAGE_W, PAGE_H)) {
                        free(strip);
                        fclose(f);
                        CHECK(0, "corpus page memory");
                        return;
                    }
                    pagePlace(&p, strip, sw, sh, NULL, NULL);
                }
                lines++;
                free(strip);
            }
        }
        fclose(f);
    }
    if (p.strips) {
        checkStyles(&p, "corpus");
    }
    free(p.cov);
    printf("  corpus: %d lines as strips\n", lines);
    CHECK(lines >= 8 * 2, "corpus: %d lines", lines);
}

/* rd_upload.c's rectangle for a changed page rectangle (x, y, w, h): the
   device texels [x, x + w + 2 AX) x [y, y + h + 2 AY), clipped */
static void bakeChanged(const Page *p, const uint8_t style[4], uint8_t *dev, int x, int y, int w,
                        int h)
{
    const int dw = p->w + 2 * AX, dh = p->h + 2 * AY;
    int rx = x, ry = y, rw = w + 2 * AX, rh = h + 2 * AY;
    rw = rx + rw > dw ? dw - rx : rw;
    rh = ry + rh > dh ? dh - ry : rh;
    CHECK(rd__sheet_bake_rect(p->cov, (uint32_t)p->w, (uint32_t)p->h, style, rx - AX, ry - AY, rw,
                              rh, dev + ((size_t)ry * (size_t)dw + (size_t)rx) * 4, (size_t)dw * 4),
          "the changed rectangle baked");
}

/* updates: strips placed and a band cleared on a baked page */
static void testUpdates(void)
{
    Page p;
    if (!pageInit(&p, 256, 128)) {
        CHECK(0, "update page memory");
        return;
    }
    static const char *const kWords[] = {"Options", "Language", "Wide 21:9", "Ölçü",
                                         "Résumé",  "Größe",    "1234567890"};
    const RdSheetStyle st = kStyles[0].st;
    uint8_t sb[4];
    styleBytes(&st, sb);
    uint8_t *dev = bakeWhole(&p, sb);
    CHECK(dev != NULL, "the empty page baked");
    int placed = 0;
    for (size_t i = 0; dev && i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        int sw = 0, sh = 0, ox = 0, oy = 0;
        uint8_t *strip = rasterStrip(kWords[i], strlen(kWords[i]), 11.0f, &sw, &sh);
        if (!strip || !pagePlace(&p, strip, sw, sh, &ox, &oy)) {
            free(strip);
            break;
        }
        free(strip);
        placed++;
        bakeChanged(&p, sb, dev, ox, oy, sw, sh);
        uint8_t *fresh = bakeWhole(&p, sb);
        const size_t bytes = (size_t)(p.w + 2 * AX) * (size_t)(p.h + 2 * AY) * 4;
        CHECK(fresh && memcmp(fresh, dev, bytes) == 0,
              "updates: \"%s\" placed, the grown rectangle's bake is the fresh page's", kWords[i]);
        free(fresh);
    }
    /* a band cleared across the page (menu_font.c reclaimShelf at 1x: whole
       rows) */
    if (dev && placed) {
        const int y0 = ORIGIN, rows = 20;
        memset(p.cov + (size_t)y0 * (size_t)p.w, 0, (size_t)rows * (size_t)p.w);
        bakeChanged(&p, sb, dev, 0, y0, p.w, rows);
        uint8_t *fresh = bakeWhole(&p, sb);
        const size_t bytes = (size_t)(p.w + 2 * AX) * (size_t)(p.h + 2 * AY) * 4;
        CHECK(fresh && memcmp(fresh, dev, bytes) == 0,
              "updates: a band cleared, the grown rectangle's bake is the fresh page's");
        CHECK(fresh && pageBad(&p, fresh, &st, "updates") == 0,
              "updates: the page after the clear is sheetref_texel's");
        free(fresh);
    }
    printf("  updates: %d strips placed one by one, then a band cleared\n", placed);
    CHECK(placed >= 4, "updates: %d strips placed", placed);
    free(dev);
    free(p.cov);
}

/* edges: ink on the page's four edges; the apron and past it */
static void testEdges(void)
{
    enum { W = 40, H = 24 };

    Page p;
    if (!pageInit(&p, W, H)) {
        CHECK(0, "edge page memory");
        return;
    }
    for (int y = 0; y < H; y++) {
        p.cov[(size_t)y * W] = 255;                           /* the left column */
        p.cov[(size_t)y * W + W - 1] = (uint8_t)(40 + 9 * y); /* the right one */
    }
    for (int x = 0; x < W; x += 3) {
        p.cov[x] = 200;                      /* the top row */
        p.cov[(size_t)(H - 1) * W + x] = 90; /* the bottom row */
    }
    p.cov[(size_t)(H / 2) * W + W / 2] = 255;
    p.strips = 1;
    checkStyles(&p, "edges");
    /* past the apron: the outermost apron texel's, at any distance */
    const int dw = W + 2 * AX, dh = H + 2 * AY;
    long bad = 0;
    for (int k = 0; k < STYLE_COUNT; k++) {
        const RdSheetStyle *st = &kStyles[k].st;
        for (int y = -AY - 40; y < H + AY + 40; y += 3) {
            for (int x = -AX - 60; x < W + AX + 60; x += 5) {
                const int cx = x < -AX ? -AX : x >= W + AX ? W + AX - 1 : x;
                const int cy = y < -AY ? -AY : y >= H + AY ? H + AY - 1 : y;
                uint8_t g, a, cg, ca;
                sheetref_texel(p.cov, W, H, x, y, st, &g, &a);
                sheetref_texel(p.cov, W, H, cx, cy, st, &cg, &ca);
                bad += g != cg || a != ca;
            }
        }
    }
    CHECK(bad == 0, "edges: %ld texels past the apron differ from the clamped one's", bad);
    printf("  edges: a %dx%d page's apron (%dx%d) and the texels past it\n", W, H, dw, dh);
    free(p.cov);
}

/* rects: sub-rectangles against the whole bake; the empty and missing cases */
static void testRects(void)
{
    Page p;
    if (!pageInit(&p, 128, 64)) {
        CHECK(0, "rect page memory");
        return;
    }
    int sw = 0, sh = 0;
    uint8_t *strip = rasterStrip("Brightness", 10, 14.0f, &sw, &sh);
    if (strip) {
        pagePlace(&p, strip, sw, sh, NULL, NULL);
    }
    free(strip);
    const RdSheetStyle st = kStyles[1].st;
    uint8_t sb[4];
    styleBytes(&st, sb);
    uint8_t *whole = bakeWhole(&p, sb);
    const int dw = p.w + 2 * AX, dh = p.h + 2 * AY;
    uint32_t seed = 12345u;
    long bad = 0;
    for (int t = 0; whole && t < 200; t++) {
        seed = seed * 1664525u + 1013904223u;
        const int rx = (int)(seed >> 8) % dw;
        seed = seed * 1664525u + 1013904223u;
        const int ry = (int)(seed >> 8) % dh;
        seed = seed * 1664525u + 1013904223u;
        const int rw = 1 + (int)(seed >> 8) % (dw - rx);
        seed = seed * 1664525u + 1013904223u;
        const int rh = 1 + (int)(seed >> 8) % (dh - ry);
        /* a pitch wider than the rectangle: the bytes past it stay */
        const size_t pitch = (size_t)rw * 4 + 12;
        uint8_t *out = malloc(pitch * (size_t)rh);
        if (!out) {
            break;
        }
        memset(out, 0xEE, pitch * (size_t)rh);
        CHECK(rd__sheet_bake_rect(p.cov, (uint32_t)p.w, (uint32_t)p.h, sb, rx - AX, ry - AY, rw, rh,
                                  out, pitch),
              "rects: the rectangle baked");
        for (int y = 0; y < rh; y++) {
            bad += memcmp(out + (size_t)y * pitch,
                          whole + ((size_t)(ry + y) * (size_t)dw + (size_t)rx) * 4,
                          (size_t)rw * 4) != 0;
            for (size_t b = (size_t)rw * 4; b < pitch; b++) {
                bad += out[(size_t)y * pitch + b] != 0xEE;
            }
        }
        free(out);
    }
    CHECK(bad == 0, "rects: %ld rows of sub-rectangles differ from the whole page's", bad);
    uint8_t one[4] = {1, 2, 3, 4};
    CHECK(rd__sheet_bake_rect(p.cov, (uint32_t)p.w, (uint32_t)p.h, sb, 0, 0, 0, 5, one, 4) == 1 &&
              one[0] == 1 && one[3] == 4,
          "rects: an empty rectangle bakes nothing");
    CHECK(rd__sheet_bake_rect(NULL, (uint32_t)p.w, (uint32_t)p.h, sb, 0, 0, 1, 1, one, 4) == 0,
          "rects: no coverage, no bake");
    printf("  rects: 200 sub-rectangles of a %dx%d bake\n", dw, dh);
    free(whole);
    free(p.cov);
}

int main(int argc, char **argv)
{
    const char *corpus = argc > 1 ? argv[1] : "port/ui/test/font_corpus";
    CHECK(ui_font_init(), "the embedded fonts parse");
    testRects();
    testEdges();
    testUpdates();
    testGlyphs();
    testCorpus(corpus);
    return ico_check_summary("rd_sheet_bake_test");
}
