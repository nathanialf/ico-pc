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
 *            1, a missing coverage returns 0;
 *   scaled   a sheet texture of strip scale 2 and 4 (coverage in the top
 *            half, rd_sheet_rim's rim in the bottom one) with blocks,
 *            ramps, noise and ink on its four edges, baked whole (both
 *            halves and the apron) by rd__sheet_bake_scaled_rect: every
 *            texel is font.hlsl sheet_scaled's, transcribed here in floats
 *            with lerp as x (1 - a) + y a and as x + a (y - x) (the two
 *            forms a driver may compile: the same bits, as the blend
 *            weights are odd 4ths and 8ths); away from the edges also
 *            sheetref_texel's; past the apron the outermost texel's;
 *            sub-rectangles the whole bake's; a strip placed as
 *            menu_font.c placeCoverage places it (its coverage and rim
 *            rectangles) and baked over rd__sheet_bake_grow's rectangle
 *            gives a fresh bake's page; scale 3 (6ths, which round) and
 *            the device rule (rd__sheet_bake_scale: 1, and 2 on the tile-based profile) refused;
 *   looks    sheet_ref.h's SheetLook (the parametrised reference ctest
 *            menu_look's outline mode draws candidate looks with): the
 *            default look is the constants and draws sheetref_texel's and
 *            sheetref_sample's texels at 1x and 2x; sheetref_bayer8 is
 *            common.hlsli gs_bayer8 (row 0, every value once in a block,
 *            over 4 the 4x4 matrix); the tables are read only within the
 *            reach; 3 and 2 levels give only their alphas; texels past the
 *            reach have alpha 0 under every matrix and level count; a hard
 *            outline is full to its edge and none past it; a uniform field
 *            under the 8x8 and the 4x4 matrix lights exactly the texels
 *            whose threshold passes (2b + 1 in 128ths, 32nds), rounding with
 *            the dither off; sheetref_texel_cr over sheetref_rim_look is
 *            sheetref_texel_look.
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

/* corpus: each text line of the eight files, at em 11 and 14 */
static void testCorpus(const char *dir)
{
    static const char *const kLangs[] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"};
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

/* ---- scaled: strips at 2 and 4 texels a sheet texel ---- */

static uint32_t refQuantise(uint32_t v, uint32_t th)
{
    const uint32_t n = ICO_SHEET_LEVELS - 1u;
    const uint32_t q = (v * n * 32u + th * 255u) / (255u * 32u);
    return ((q < n ? q : n) * 255u + n / 2u) / n;
}

static uint32_t refThreshold(int x, int y, uint32_t dither)
{
    static const uint32_t kRows[4] = {ICO_SHEET_BAYER_ROW0, ICO_SHEET_BAYER_ROW1,
                                      ICO_SHEET_BAYER_ROW2, ICO_SHEET_BAYER_ROW3};
    if (!dither) {
        return ICO_SHEET_T_OFF;
    }
    return 2u * ((kRows[(uint32_t)y & 3u] >> (4u * ((uint32_t)x & 3u))) & 15u) + 1u;
}

/* sheet_text.hlsli sheet_solid_threshold */
static uint32_t refSolid(uint32_t a, uint32_t th)
{
    return a >= ICO_SHEET_SOLID_ABOVE ? ICO_SHEET_T_OFF : th;
}

static void shaderTexel(const uint8_t *tex, int w, int h, int si, const uint8_t st[4], int px,
                        int py, int form, uint8_t *grey, uint8_t *alpha)
{
    const int hh = h / 2;
    const uint32_t c = px >= 0 && px < w && py >= 0 && py < hh ? tex[(size_t)py * w + px] : 0u;
    /* the rim: the sheet texel the texel lies in, as blocks (font.hlsl
       sheet_scaled); form is kept for the caller (one form now) */
    (void)form;
    const int qx = (px + 4096 * si) / si - 4096, qy = (py + 4096 * si) / si - 4096;
    const int tx = qx * si, ty = qy * si;
    const int inside = tx >= 0 && tx < w && ty >= 0 && ty < hh;
    const uint32_t r = inside ? tex[(size_t)(hh + ty) * w + tx] : 0u;
    const uint32_t rw = st[0] != 0 ? (r * st[0] + 32u) / 64u : 0u;
    const uint32_t th0 = refThreshold(qx, qy, st[3]);
    const uint32_t rm = refQuantise(rw, refSolid(rw, th0));
    const float rav = (float)((rm * 128u + 127u) / 255u);
    const uint32_t a = c > rm ? c : rm;
    const uint32_t th = refSolid(a, th0);
    const uint32_t t = a != 0u ? (c * 255u + a / 2u) / a : 0u;
    const uint32_t cq = refQuantise(c, th), tq = refQuantise(t, th);
    *grey = (uint8_t)((st[1] * (255u - tq) + st[2] * tq + 127u) / 255u);
    const float af = (float)((cq * 128u + 127u) / 255u);
    *alpha = (uint8_t)floorf((rav > af ? rav : af) + 0.5f);
}

/* the scaled test page: S times SW x SH sheet texels of coverage (blocks,
   a ramp, noise at the strip's own texels; with edges, ink on the four
   edges too) over the rim rd_sheet_rim makes; 2 S SH rows in all */
enum { SW = 72, SH = 28 };

static uint8_t *scaledPage(int S, int edges)
{
    const int W = SW * S, HH = SH * S;
    uint8_t *tex = calloc((size_t)W * (size_t)HH * 2u, 1);
    if (!tex) {
        return NULL;
    }
    uint32_t seed = 777u + (uint32_t)S;
    for (int y = 0; y < HH; y++) {
        for (int x = 0; x < W; x++) {
            const int qx = x / S, qy = y / S;
            uint8_t v = 0;
            if (qx >= 8 && qx < 14 && qy >= 7 && qy < 18) {
                v = 255; /* a solid block */
            } else if (qx >= 18 && qx < 34 && qy >= 9 && qy < 13) {
                v = (uint8_t)(16 * (qx - 18) + x % S * 3); /* a ramp, finer than a sheet texel */
            } else if (qx >= 40 && qx < 58 && qy >= 7 && qy < 20) {
                seed = seed * 1664525u + 1013904223u;
                v = (seed >> 24) < 90u ? (uint8_t)(seed >> 16) : 0u; /* noise */
            } else if (edges && (x == 0 || y == 0 || x == W - 1 || y == HH - 1)) {
                v = (uint8_t)(60 + (x * 7 + y * 13) % 190);
            }
            tex[(size_t)y * W + x] = v;
        }
    }
    rd_sheet_rim(tex, (uint32_t)W, (uint32_t)HH, (uint32_t)S, 0, 0, W, HH,
                 tex + (size_t)W * (size_t)HH);
    return tex;
}

/* the whole device texture of a scaled page: (W + 2 AX) x (2 HH + 2 AY) */
static uint8_t *bakeScaledWhole(const uint8_t *tex, int W, int H, int S, const uint8_t st[4])
{
    const int dw = W + 2 * AX, dh = H + 2 * AY;
    uint8_t *dev = malloc((size_t)dw * (size_t)dh * 4);
    if (dev && !rd__sheet_bake_scaled_rect(tex, (uint32_t)W, (uint32_t)H, st, (uint32_t)S, -AX, -AY,
                                           dw, dh, dev, (size_t)dw * 4)) {
        free(dev);
        dev = NULL;
    }
    return dev;
}

static void testScaledAt(int S)
{
    const int W = SW * S, HH = SH * S, H = 2 * HH;
    const int dw = W + 2 * AX, dh = H + 2 * AY;
    long badShader = 0, badRef = 0, badFar = 0, refTexels = 0;
    for (int edges = 0; edges < 2; edges++) {
        uint8_t *tex = scaledPage(S, edges);
        if (!tex) {
            CHECK(0, "scaled %d: page memory", S);
            return;
        }
        for (int k = 0; k < STYLE_COUNT; k++) {
            for (int d = 0; d < 2; d++) {
                RdSheetStyle st = kStyles[k].st;
                st.dither = (uint8_t)d;
                st.scale = (uint8_t)S;
                uint8_t sb[4];
                styleBytes(&st, sb);
                uint8_t *dev = bakeScaledWhole(tex, W, H, S, sb);
                if (!dev) {
                    CHECK(0, "scaled %d: the whole bake", S);
                    continue;
                }
                for (int y = -AY; y < H + AY; y++) {
                    for (int x = -AX; x < W + AX; x++) {
                        const uint8_t *o = &dev[((size_t)(y + AY) * dw + (size_t)(x + AX)) * 4];
                        for (int form = 0; form < 2; form++) {
                            uint8_t g, a;
                            shaderTexel(tex, W, H, S, sb, x, y, form, &g, &a);
                            if (o[0] != g || o[1] != g || o[2] != g || o[3] != a) {
                                if (badShader < 8) {
                                    printf("  scaled %d (%s, dither %d, form %d): texel (%d, %d) "
                                           "baked %u %u, sheet_scaled %u %u\n",
                                           S, kStyles[k].name, d, form, x, y, o[0], o[3], g, a);
                                }
                                badShader++;
                            }
                        }
                        /* sheetref_texel makes the rims of the sheet texels
                           past the page from the coverage, the device holds
                           none: the two agree where no ink is near an edge
                           (English, faint and plain: the oracle's rim is a
                           full dilation a texel, slow at 4x) */
                        if (!edges && y < HH + AY && (k == 0 || k == 7 || k == 8)) {
                            uint8_t g, a;
                            sheetref_texel(tex, (uint32_t)W, (uint32_t)HH, x, y, &st, &g, &a);
                            badRef += o[0] != g || o[3] != a;
                            refTexels++;
                        }
                    }
                }
                /* past the apron, any distance: the clamped read's texel */
                for (int y = -AY - 30; y < H + AY + 30; y += 3) {
                    for (int x = -AX - 40; x < W + AX + 40; x += 5) {
                        const int cx = x < -AX ? -AX : x >= W + AX ? W + AX - 1 : x;
                        const int cy = y < -AY ? -AY : y >= H + AY ? H + AY - 1 : y;
                        uint8_t g, a;
                        shaderTexel(tex, W, H, S, sb, x, y, 0, &g, &a);
                        const uint8_t *o = &dev[((size_t)(cy + AY) * dw + (size_t)(cx + AX)) * 4];
                        badFar += o[0] != g || o[3] != a;
                    }
                }
                free(dev);
            }
        }
        free(tex);
    }
    printf("  scaled %dx: %dx%d texture, bake against sheet_scaled %ld off, against sheetref_texel "
           "%ld of %ld off, past the apron %ld off\n",
           S, W, H, badShader, badRef, refTexels, badFar);
    CHECK(badShader == 0, "scaled %d: %ld baked texels off font.hlsl sheet_scaled", S, badShader);
    CHECK(badRef == 0, "scaled %d: %ld baked texels off sheetref_texel", S, badRef);
    CHECK(badFar == 0, "scaled %d: %ld texels past the apron off the clamped read's", S, badFar);

    /* sub-rectangles against the whole bake */
    uint8_t *tex = scaledPage(S, 1);
    if (!tex) {
        return;
    }
    uint8_t sb[4];
    styleBytes(&kStyles[1].st, sb);
    uint8_t *whole = bakeScaledWhole(tex, W, H, S, sb);
    uint32_t seed = 4242u;
    long badRect = 0;
    for (int t = 0; whole && t < 120; t++) {
        seed = seed * 1664525u + 1013904223u;
        const int rx = (int)(seed >> 8) % dw;
        seed = seed * 1664525u + 1013904223u;
        const int ry = (int)(seed >> 8) % dh;
        seed = seed * 1664525u + 1013904223u;
        const int rw = 1 + (int)(seed >> 8) % (dw - rx);
        seed = seed * 1664525u + 1013904223u;
        const int rh = 1 + (int)(seed >> 8) % (dh - ry);
        const size_t pitch = (size_t)rw * 4 + 8;
        uint8_t *out = malloc(pitch * (size_t)rh);
        if (!out) {
            break;
        }
        CHECK(rd__sheet_bake_scaled_rect(tex, (uint32_t)W, (uint32_t)H, sb, (uint32_t)S, rx - AX,
                                         ry - AY, rw, rh, out, pitch),
              "scaled %d: a rectangle baked", S);
        for (int y = 0; y < rh; y++) {
            badRect += memcmp(out + (size_t)y * pitch,
                              whole + ((size_t)(ry + y) * (size_t)dw + (size_t)rx) * 4,
                              (size_t)rw * 4) != 0;
        }
        free(out);
    }
    CHECK(whole && badRect == 0, "scaled %d: %ld rows of sub-rectangles off the whole bake", S,
          badRect);

    /* a strip placed as menu_font.c placeCoverage places one: its coverage
       (and the sheet texel round it) and the rim below, two rectangles the
       texture's dirty rectangle joins; rd__sheet_bake_grow's device
       rectangle baked over the old device texture is a fresh bake */
    {
        const int sx = 20 * S, sy = 15 * S, sw = 9 * S, shh = 5 * S;
        for (int y = 0; y < shh; y++) {
            for (int x = 0; x < sw; x++) {
                tex[(size_t)(sy + y) * W + sx + x] = (uint8_t)((x * 37 + y * 11) % 256);
            }
        }
        const int rx = sx - S, ry = sy - S, rw = sw + 2 * S, rh = shh + 2 * S;
        rd_sheet_rim(tex, (uint32_t)W, (uint32_t)HH, (uint32_t)S, rx, ry, rw, rh,
                     tex + (size_t)W * (size_t)HH);
        RdTexRec rec;
        memset(&rec, 0, sizeof(rec));
        rec.w = (uint32_t)W;
        rec.h = (uint32_t)H;
        rec.sheetScale = (uint8_t)S;
        memcpy(rec.sheet, sb, 4);
        rec.pixels = tex;
        /* the dirty rectangle: the union of the two */
        uint32_t ux = (uint32_t)rx, uy = (uint32_t)ry, uw = (uint32_t)rw, uh = (uint32_t)(HH + rh);
        rd__sheet_bake_grow(&rec, (uint32_t)dw, (uint32_t)dh, &ux, &uy, &uw, &uh);
        uint8_t *dev = whole;
        if (dev && uw && uh) {
            CHECK(rd__sheet_bake_texels(&rec, (int32_t)ux - AX, (int32_t)uy - AY, (int32_t)uw,
                                        (int32_t)uh, dev + ((size_t)uy * dw + ux) * 4,
                                        (size_t)dw * 4),
                  "scaled %d: the grown rectangle baked", S);
        }
        uint8_t *fresh = bakeScaledWhole(tex, W, H, S, sb);
        CHECK(dev && fresh && memcmp(dev, fresh, (size_t)dw * (size_t)dh * 4) == 0,
              "scaled %d: a placed strip's grown rectangle (%u,%u %ux%u) bakes the fresh page", S,
              ux, uy, uw, uh);
        /* a rim row alone (the bottom half) reaches the texels 2 S round
           its sheet row in the top half */
        uint32_t gx = 10u * (uint32_t)S, gy = (uint32_t)(HH + 12 * S), gw = 1, gh = 1;
        rd__sheet_bake_grow(&rec, (uint32_t)dw, (uint32_t)dh, &gx, &gy, &gw, &gh);
        CHECK(gy <= (uint32_t)(12 * S - 2 * S + AY) && gy + gh >= (uint32_t)(12 * S + 3 * S + AY),
              "scaled %d: a rim texel's rectangle reaches its sheet row's texels (%u+%u)", S, gy,
              gh);
        free(fresh);
    }
    free(whole);
    free(tex);
}

static void testScaled(void)
{
    testScaledAt(2);
    testScaledAt(4);
    /* 3: the shader's blend by 6ths rounds; the bake refuses it, and the
       device bakes 1x and 2x only (4x pages are 34 MB baked) */
    uint8_t px[4] = {9, 9, 9, 9}, cov[16] = {0};
    const uint8_t sb[4] = {64, 0, 255, 1};
    CHECK(rd__sheet_bake_scaled_rect(cov, 2, 8, sb, 3, 0, 0, 1, 1, px, 4) == 0 && px[0] == 9,
          "scaled: scale 3 refused, nothing written");
    CHECK(rd__sheet_bake_scaled_rect(cov, 2, 8, sb, 1, 0, 0, 1, 1, px, 4) == 0,
          "scaled: scale 1 is the 1x bake's");
    /* the device rule: 1x always, 2x on the tile-based classes' profile only
       (RdProfile.sheetBake2x, issue 126), 3x and 4x never; the profile is
       zero here (no rd_init), the immediate-mode classes' */
    rd__set_sheet_bake_2x(false);
    CHECK(rd__sheet_bake_scale(0) && rd__sheet_bake_scale(1) && !rd__sheet_bake_scale(2) &&
              !rd__sheet_bake_scale(3) && !rd__sheet_bake_scale(4),
          "scaled: an immediate-mode profile bakes 1x only");
    rd__set_sheet_bake_2x(true);
    CHECK(rd__sheet_bake_scale(0) && rd__sheet_bake_scale(1) && rd__sheet_bake_scale(2) &&
              !rd__sheet_bake_scale(3) && !rd__sheet_bake_scale(4),
          "scaled: a tile-based profile bakes 1x and 2x");
    rd__set_sheet_bake_2x(false);
}

/* ------------------------------------------------------------- looks */

static const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* the texels of a look that differ from another look's, over the strip and
   a margin past the reach */
static long looksDiffer(const uint8_t *cov, int w, int h, const RdSheetStyle *st,
                        const SheetLook *a, const SheetLook *b)
{
    long bad = 0;
    for (int y = -ICO_SHEET_RY - 2; y < h + ICO_SHEET_RY + 2; y++) {
        for (int x = -ICO_SHEET_RX - 2; x < w + ICO_SHEET_RX + 2; x++) {
            uint8_t g0, a0, g1, a1;
            sheetref_texel_look(cov, (uint32_t)w, (uint32_t)h, x, y, st, a, &g0, &a0);
            sheetref_texel_look(cov, (uint32_t)w, (uint32_t)h, x, y, st, b, &g1, &a1);
            bad += g0 != g1 || a0 != a1;
        }
    }
    return bad;
}

static void testLooks(void)
{
    const SheetLook *d = sheetref_default_look();
    static const uint16_t kWx[ICO_SHEET_RX + 1] = {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2,
                                                   ICO_SHEET_WX_3, ICO_SHEET_WX_4, ICO_SHEET_WX_5,
                                                   ICO_SHEET_WX_6};
    static const uint16_t kWy[ICO_SHEET_RY + 1] = {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2,
                                                   ICO_SHEET_WY_3, ICO_SHEET_WY_4};
    CHECK(d->rx == ICO_SHEET_RX && d->ry == ICO_SHEET_RY && d->levels == ICO_SHEET_LEVELS &&
              d->bayer == 4 && memcmp(d->wx, kWx, sizeof(kWx)) == 0 &&
              memcmp(d->wy, kWy, sizeof(kWy)) == 0,
          "looks: the default look is the constants");
    /* gs_bayer8 */
    static const uint32_t kRow0[8] = {0, 32, 8, 40, 2, 34, 10, 42};
    int seen[64] = {0}, row0 = 0, coarse = 0, period = 0;
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            const uint32_t v = sheetref_bayer8(x, y);
            if (v < 64) {
                seen[v]++;
            }
            row0 += y == 0 && v != kRow0[x];
            coarse += v >> 2 != kBayer4[y & 3][x & 3];
            period += sheetref_bayer8(x + 8, y + 16) != v;
        }
    }
    int once = 0;
    for (int v = 0; v < 64; v++) {
        once += seen[v] == 1;
    }
    CHECK(row0 == 0 && once == 64 && coarse == 0 && period == 0,
          "looks: sheetref_bayer8 is gs_bayer8 (row 0 %d off, %d of 64 values once, %d over 4 "
          "off the 4x4, %d off the period)",
          row0, once, coarse, period);

    /* a strip: a block of ink, a ramp, a stroke */
    enum { W = 72, H = 28 };

    static uint8_t cov[W * H];
    memset(cov, 0, sizeof(cov));
    for (int y = 8; y < 16; y++) {
        for (int x = 10; x < 18; x++) {
            cov[y * W + x] = 255;
        }
        for (int x = 30; x < 62; x++) {
            cov[y * W + x] = (uint8_t)((x - 30) * 8);
        }
    }
    for (int y = 4; y < 22; y++) {
        cov[y * W + 66] = 200;
    }
    const RdSheetStyle st = {1, 24, 255, 1, 0, 1};
    long bad = 0;
    for (int sc = 1; sc <= 2; sc++) {
        RdSheetStyle ss = st;
        ss.scale = (uint8_t)sc;
        for (int y = -6; y < H + 6; y++) {
            for (int x = -8; x < W + 8; x++) {
                uint8_t g0, a0, g1, a1, p0[4], p1[4];
                sheetref_texel(cov, W, H, x, y, &ss, &g0, &a0);
                sheetref_texel_look(cov, W, H, x, y, &ss, NULL, &g1, &a1);
                const float u = ((float)x + 0.3f) / W, v = ((float)y + 0.7f) / H;
                sheetref_sample(cov, W, H, u, v, &ss, p0);
                sheetref_sample_look(cov, W, H, u, v, &ss, d, p1);
                bad += g0 != g1 || a0 != a1 || memcmp(p0, p1, 4) != 0;
            }
        }
    }
    CHECK(bad == 0, "looks: the default look draws sheetref_texel's texels (%ld off)", bad);
    /* the reach: entries past rx / ry are not read */
    const SheetLook shortA = {2, 1, {1000, 700, 300, 0, 0, 0, 0}, {1000, 500, 0, 0, 0}, 8, 4, 0};
    const SheetLook shortB = {
        2, 1, {1000, 700, 300, 900, 900, 900, 900}, {1000, 500, 900, 900, 900}, 8, 4, 0};
    CHECK(looksDiffer(cov, W, H, &st, &shortA, &shortB) == 0,
          "looks: the tables are read within the reach only");
    /* the level counts' alphas; nothing past the reach */
    static const int kLevels[] = {2, 3, 5, 8, 256};
    static const int kBayers[] = {0, 4, 8};
    long off = 0, far = 0;
    for (size_t li = 0; li < sizeof(kLevels) / sizeof(kLevels[0]); li++) {
        for (size_t bi = 0; bi < sizeof(kBayers) / sizeof(kBayers[0]); bi++) {
            SheetLook l = *d;
            l.levels = kLevels[li];
            l.bayer = kBayers[bi];
            for (int y = -8; y < H + 8; y++) {
                for (int x = -10; x < W + 10; x++) {
                    uint8_t g, a;
                    sheetref_texel_look(cov, W, H, x, y, &st, &l, &g, &a);
                    if (l.levels == 3) {
                        off += a != 0 && a != 64 && a != 128;
                    } else if (l.levels == 2) {
                        off += a != 0 && a != 128;
                    }
                    if (x < -ICO_SHEET_RX || y < -ICO_SHEET_RY || x >= W + ICO_SHEET_RX ||
                        y >= H + ICO_SHEET_RY) {
                        far += a != 0;
                    }
                }
            }
        }
    }
    CHECK(off == 0, "looks: %ld alphas off the 3 and 2 levels' (0 64 128, 0 128)", off);
    CHECK(far == 0, "looks: %ld texels past the reach with alpha", far);
    /* a hard outline: full two texels across and one down, none past */
    const SheetLook hard = {6, 4, {1000, 1000, 1000, 0, 0, 0, 0}, {1000, 1000, 0, 0, 0}, 8, 4, 0};
    const RdSheetStyle black = {1, 0, 255, 0, 0, 1};
    int hardBad = 0;
    for (int y = 8; y < 16; y++) {
        for (int dx = 1; dx <= 3; dx++) {
            uint8_t g, a;
            sheetref_texel_look(cov, W, H, 10 - dx, y, &black, &hard, &g, &a);
            hardBad += dx <= 2 ? a != 128 || g != 0 : a != 0;
        }
    }
    for (int x = 10; x < 18; x++) {
        for (int dy = 1; dy <= 2; dy++) {
            uint8_t g, a;
            sheetref_texel_look(cov, W, H, x, 16 - 1 + dy, &black, &hard, &g, &a);
            hardBad += dy == 1 ? a != 128 || g != 0 : a != 0;
        }
    }
    CHECK(hardBad == 0, "looks: the hard outline's edge (%d texels off)", hardBad);

    /* a uniform field (no rim) dithered to 2 levels: the texels whose
       threshold passes are full, the rest empty; rounding with the dither
       off */
    enum { F = 16 };

    static uint8_t field[F * F];
    memset(field, 100, sizeof(field));
    for (int m = 0; m < 3; m++) {
        const int n = m == 0 ? 0 : m == 1 ? 4 : 8, den = n == 8 ? 128 : 32;
        const SheetLook l = {6, 4, {1000, 0, 0, 0, 0, 0, 0}, {1000, 0, 0, 0, 0}, 2, n, 0};
        for (int dith = 0; dith < 2; dith++) {
            const RdSheetStyle plain = {0, 0, 255, (uint8_t)dith, 0, 1};
            int wrong = 0, lit = 0;
            for (int y = 0; y < F; y++) {
                for (int x = 0; x < F; x++) {
                    uint8_t g, a;
                    sheetref_texel_look(field, F, F, x, y, &plain, &l, &g, &a);
                    const int b = !dith || n == 0 ? -1
                                  : n == 8        ? (int)sheetref_bayer8((uint32_t)x, (uint32_t)y)
                                                  : kBayer4[y & 3][x & 3];
                    const int th = b < 0 ? den / 2 : 2 * b + 1;
                    const int on = 100 * den + th * 255 >= 255 * den;
                    wrong += a != (on ? 128 : 0);
                    lit += a == 128;
                }
            }
            CHECK(wrong == 0,
                  "looks: a uniform field under matrix %d, dither %d: %d texels off "
                  "the threshold's",
                  n, dith, wrong);
            if (n == 8 && dith) {
                /* 2b + 1 >= 78: b 39 .. 63, 25 of 64 */
                CHECK(lit == 25 * 4, "looks: the 8x8 field lights %d of 256, expected 100", lit);
            }
        }
    }
    /* the texel from coverage and rim is the 1x texel */
    const SheetLook c3 = {6, 4, {1000, 1000, 700, 400, 150, 0, 0}, {1000, 900, 450, 150, 0}, 8,
                          8, 0};
    long crBad = 0;
    for (int y = -6; y < H + 6; y++) {
        for (int x = -8; x < W + 8; x++) {
            const uint32_t c = x >= 0 && y >= 0 && x < W && y < H ? cov[y * W + x] : 0u;
            uint8_t g0, a0, g1, a1;
            sheetref_texel_look(cov, W, H, x, y, &st, &c3, &g0, &a0);
            sheetref_texel_cr(c, sheetref_rim_look(cov, W, H, x, y, &c3), x, y, &st, &c3, &g1, &a1);
            crBad += g0 != g1 || a0 != a1;
        }
    }
    CHECK(crBad == 0, "looks: sheetref_texel_cr over sheetref_rim_look differs at %ld texels",
          crBad);
    printf("  looks: the default look, gs_bayer8, the reach, %d level counts x 3 matrices, a hard "
           "outline, uniform fields, the coverage-and-rim texel\n",
           (int)(sizeof(kLevels) / sizeof(kLevels[0])));
}

int main(int argc, char **argv)
{
    const char *corpus = argc > 1 ? argv[1] : "port/ui/test/font_corpus";
    CHECK(ui_font_init(), "the embedded fonts parse");
    testLooks();
    testScaled();
    testRects();
    testEdges();
    testUpdates();
    testGlyphs();
    testCorpus(corpus);
    return ico_check_summary("rd_sheet_bake_test");
}
