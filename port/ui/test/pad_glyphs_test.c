/*
 * port/ui/test/pad_glyphs_test.c
 *
 * ctest pad_glyphs: the button pictures' core (pad_glyphs.c) over the
 * embedded files, CPU only (no texture).  Every sample point below is
 * worked out from the file's own path data (the comments give the
 * arithmetic), not from a reference picture.  A raster is the piece's ink
 * box (its outline, the stroke's half width and miter joins, clipped to the
 * viewBox) at a lettering height of `rows` pixels:
 *   - every shipped set: eight valid pieces, each ink box inside its
 *     viewBox, the aspect the box's, the capitals one lettering height
 *     tall and the Wii Classic Controller's a, x and y their sizes against
 *     its b, the fill colour and the black stroke present in each at 96
 *     rows, the alpha never past the GS's 0x80;
 *   - every piece at 24, 48 and 96 rows: its ink reaching all four sides of
 *     its raster within a pixel (no bearing left), every capital of a set
 *     the same height within a pixel, and the Wii Classic b as tall as the
 *     Nintendo B (one lettering size across the sets);
 *   - the Xbox B (xbox/circle.svg, the Nintendo cross's letter too) at 24
 *     and 96 rows: its ink box, the fill inside the stem, the stroke just
 *     left of it, the stem's square stroke corners in the raster's left
 *     corners and the right corners (the bowls') transparent, the upper
 *     hole's middle transparent at 96 rows (evenodd);
 *   - the Xbox A (xbox/cross.svg): the fill in its left leg, no fill in its
 *     hole (the stroke's black there, not the blue);
 *   - a synthetic evenodd square ring whose two contours turn the same way:
 *     its middle empty with fill-rule evenodd, filled without it;
 *   - the 40 x 24 Xbox LB and the 24 x 32 Wii Classic b: their ink boxes,
 *     their sizes at 24 and 96 rows and the fill in both letters of LB and
 *     in b's stem;
 *   - the absolute path forms (M L H V Q C Z, an implicit lineto repeat)
 *     drawing as the relative ones;
 *   - broken files through the test entry refused, their set incomplete
 *     and refused as the style in force, the embedded file back after.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pad_glyphs.h"

static int s_fail;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            s_fail++;                                                                              \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

enum { CROSS = 0, CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2 };

typedef struct Img {
    int w, h;
    float x0, y0, s; /* the ink box's corner (user units) and pixels a unit */
    uint8_t *p;
} Img;

static Img raster(int style, int glyph, int rows)
{
    Img im = {0, 0, 0.0f, 0.0f, 1.0f, NULL};
    float box[4];
    if (!pad_glyphs_ink_box(style, glyph, box) ||
        pad_glyphs_raster_size(style, glyph, rows, &im.w, &im.h) != 0) {
        CHECK(0, "raster_size(%d, %d, %d) refused", style, glyph, rows);
        return im;
    }
    /* a lettering height of rows pixels: the ink's height is
       pad_glyphs_height of it */
    im.x0 = box[0];
    im.y0 = box[1];
    im.s = (float)rows * pad_glyphs_height(style, glyph) / (box[3] - box[1]);
    im.p = malloc((size_t)im.w * (size_t)im.h * 4);
    if (!im.p || pad_glyphs_raster(style, glyph, rows, im.p, im.w * 4) != 0) {
        CHECK(0, "raster(%d, %d, %d) failed", style, glyph, rows);
        free(im.p);
        im.p = NULL;
    }
    return im;
}

/* the texel holding user point (ux, uy) */
static const uint8_t *at(const Img *im, float ux, float uy)
{
    static const uint8_t none[4] = {1, 2, 3, 4};
    if (!im->p) {
        return none;
    }
    int x = (int)floorf((ux - im->x0) * im->s), y = (int)floorf((uy - im->y0) * im->s);
    x = x < 0 ? 0 : x >= im->w ? im->w - 1 : x;
    y = y < 0 ? 0 : y >= im->h ? im->h - 1 : y;
    return im->p + ((size_t)y * (size_t)im->w + (size_t)x) * 4;
}

static const uint8_t *px(const Img *im, int x, int y)
{
    return im->p + ((size_t)y * (size_t)im->w + (size_t)x) * 4;
}

static int isColour(const uint8_t *t, unsigned rgb, int a)
{
    return t[0] == (rgb >> 16 & 0xff) && t[1] == (rgb >> 8 & 0xff) && t[2] == (rgb & 0xff) &&
           (a < 0 || t[3] == a);
}

#define OPAQUE 0x80

/* differences beyond rounding between two rasters of the same size */
static int differences(const Img *a, const Img *b)
{
    if (!a->p || !b->p || a->w != b->w || a->h != b->h) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < a->w * a->h; i++) {
        const uint8_t *s = a->p + 4 * i, *t = b->p + 4 * i;
        int bad = abs(s[3] - t[3]) > 1;
        for (int k = 0; k < 3 && s[3] > 8; k++) {
            bad |= abs(s[k] - t[k]) > 2;
        }
        n += bad;
    }
    return n;
}

static void freeImg(Img *im)
{
    free(im->p);
    im->p = NULL;
}

/* the files' fill colours (port/assets/buttons/<set>/<button>.svg, .s0 fill) */
static const unsigned kFill[3][8] = {
    {0x0078ff, 0xff0000, 0x00ff01, 0xffed00, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1},
    {0xffffff, 0xffffff, 0xffffff, 0xffffff, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1},
    {0xffffff, 0xffffff, 0xffffff, 0xffffff, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1, 0xe1e1e1}};
/* their viewBox sizes; the capitals and ascender letters (the Wii Classic
   Controller's a, x and y are not: their outlines start 7.63 and 7.98 units
   down, over a quarter of their heights 16.35, 15.68 and 22.02) */
static const float kVb[3][8][2] = {
    {{24, 24}, {24, 24}, {24, 24}, {24, 24}, {40, 24}, {40, 24}, {40, 24}, {40, 24}},
    {{24, 24}, {24, 24}, {24, 24}, {24, 24}, {24, 24}, {24, 24}, {40, 24}, {40, 24}},
    {{24, 32}, {24, 32}, {24, 32}, {24, 32}, {24, 24}, {24, 24}, {40, 24}, {40, 24}}};
static const int kTall[3][8] = {
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1}, {1, 0, 0, 0, 1, 1, 1, 1}};

static void testSets(void)
{
    CHECK(pad_glyphs_set_complete(ICO_PAD_GLYPHS_PLAYSTATION) == 1, "PlayStation is the disc's");
    CHECK(pad_glyphs_aspect(ICO_PAD_GLYPHS_PLAYSTATION, CROSS) == 0.0f &&
              pad_glyphs_height(ICO_PAD_GLYPHS_PLAYSTATION, CROSS) == 0.0f,
          "no PlayStation art");
    CHECK(pad_glyphs_set_complete(ICO_PAD_GLYPHS_COUNT) == 0, "a style out of range");
    for (int s = ICO_PAD_GLYPHS_XBOX; s < ICO_PAD_GLYPHS_COUNT; s++) {
        CHECK(pad_glyphs_set_complete(s) == 1, "%s complete", pad_glyphs_style_name(s));
        for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
            const float *vb = kVb[s - 1][g];
            CHECK(pad_glyphs_piece_valid(s, g), "%s piece %d valid (%s)", pad_glyphs_style_name(s),
                  g, pad_glyphs_piece_error(s, g) ? pad_glyphs_piece_error(s, g) : "");
            float box[4] = {0, 0, 0, 0};
            CHECK(pad_glyphs_ink_box(s, g, box) && box[0] >= 0.0f && box[1] >= 0.0f &&
                      box[2] <= vb[0] && box[3] <= vb[1] && box[2] > box[0] && box[3] > box[1],
                  "%s piece %d: the ink box %.2f,%.2f-%.2f,%.2f inside the %gx%g viewBox",
                  pad_glyphs_style_name(s), g, (double)box[0], (double)box[1], (double)box[2],
                  (double)box[3], (double)vb[0], (double)vb[1]);
            const float a = (box[2] - box[0]) / (box[3] - box[1]);
            CHECK(fabsf(pad_glyphs_aspect(s, g) - a) < 1e-5f, "%s piece %d aspect %g (%g)",
                  pad_glyphs_style_name(s), g, (double)pad_glyphs_aspect(s, g), (double)a);
            /* a capital's ink is one lettering height exactly */
            if (kTall[s - 1][g]) {
                CHECK(pad_glyphs_height(s, g) == 1.0f, "%s piece %d: a capital, height %g",
                      pad_glyphs_style_name(s), g, (double)pad_glyphs_height(s, g));
            }
            Img im = raster(s, g, 96);
            int fill = 0, stroke = 0, over = 0;
            for (int i = 0; im.p && i < im.w * im.h; i++) {
                const uint8_t *t = im.p + 4 * i;
                fill += isColour(t, kFill[s - 1][g], OPAQUE);
                stroke += isColour(t, 0x000000, OPAQUE);
                over += t[3] > OPAQUE;
            }
            CHECK(fill > 0 && stroke > 0 && over == 0,
                  "%s piece %d at 96 rows: %d fill texels, %d stroke texels, %d past 0x80",
                  pad_glyphs_style_name(s), g, fill, stroke, over);
            freeImg(&im);
        }
    }
    /* the Wii Classic Controller's short letters against its b (the ink
       0 .. 25.98: the stem's top 2 less the half stroke, the bowl's bottom
       23.98 plus it): the a's 5.63 .. 25.98 (its top curve 7.63), 20.35 of
       25.98 = 0.783; the x's 5.98 .. 25.66 (its top bars 7.98, its feet
       23.66), 0.758; the y's 5.98 .. 32 (its tail at 30 plus 2 passes the
       viewBox's 32), 26.02 of 25.98 = 1.0015 */
    const int W = ICO_PAD_GLYPHS_WII_CLASSIC;
    CHECK(fabsf(pad_glyphs_height(W, CIRCLE) - 0.783f) < 0.01f &&
              fabsf(pad_glyphs_height(W, TRIANGLE) - 0.758f) < 0.01f &&
              fabsf(pad_glyphs_height(W, SQUARE) - 1.0015f) < 0.01f,
          "the Wii Classic a, x, y: %.4f %.4f %.4f of the b", (double)pad_glyphs_height(W, CIRCLE),
          (double)pad_glyphs_height(W, TRIANGLE), (double)pad_glyphs_height(W, SQUARE));
    CHECK(pad_glyphs_bucket(0) == 16 && pad_glyphs_bucket(16) == 16 &&
              pad_glyphs_bucket(17) == 24 && pad_glyphs_bucket(100) == 128 &&
              pad_glyphs_bucket(256) == 256 && pad_glyphs_bucket(4000) == 256,
          "the atlas heights");
    unsigned tex = 7;
    float uv[4];
    CHECK(pad_glyphs_texture(CROSS, 24, &tex, uv, NULL) == 0 && tex == 0,
          "no texture without ICO_RD (or with no style in force)");
}

/* xbox/circle.svg, the B: d="m12.85 21.55h-8.77v-19.1h9.27c3.43 0 ..." so
   the stem's left edge is x 4.08 (12.85 - 8.77) from y 2.45 to 21.55; the
   upper hole "m-0.46-7.68h-5.45v5.27h5.45c..." starts at (12.87, 5.18) (the
   lower hole's start (13.33, 12.86) less 0.46, 7.68), so it spans x 7.42 ..
   12.87 and beyond along its curve to 15.68, y 5.18 .. 10.45; the outline
   reaches x 19.88 at most (the lower bowl's "c2.33 0.51 3.59 2.33 3.59
   4.92" ends at (19.88, 16.22) going straight down, and the next curve
   leaves it so).  The ink box: the stem's square corners' miters at (2.08,
   0.45) and (2.08, 23.55), the bowl's x 21.88: 19.8 x 23.1 units */
static void testB(int rows)
{
    float box[4] = {0, 0, 0, 0};
    CHECK(pad_glyphs_ink_box(ICO_PAD_GLYPHS_XBOX, CIRCLE, box) && fabsf(box[0] - 2.08f) < 0.02f &&
              fabsf(box[1] - 0.45f) < 0.02f && fabsf(box[2] - 21.88f) < 0.05f &&
              fabsf(box[3] - 23.55f) < 0.02f,
          "B: the ink box %.3f,%.3f-%.3f,%.3f", (double)box[0], (double)box[1], (double)box[2],
          (double)box[3]);
    Img im = raster(ICO_PAD_GLYPHS_XBOX, CIRCLE, rows);
    if (!im.p) {
        return;
    }
    /* pad_glyphs.c pgSize's own arithmetic: the ink's width at rows over
       its height, rounded up */
    const int wantW = (int)ceilf((box[2] - box[0]) * ((float)rows / (box[3] - box[1])) - 1e-3f);
    CHECK(im.w == wantW && im.h == rows, "B at %d rows: %d x %d (%d x %d)", rows, im.w, im.h, wantW,
          rows);
    /* the stem, x 4.08 .. 7.42: (5.75, 8) is 1.67 inside both edges */
    CHECK(isColour(at(&im, 5.75f, 8.0f), 0xff0000, OPAQUE), "B %d: the fill in the stem", rows);
    /* 1 unit left of the stem: inside the 2-unit half stroke, outside the fill */
    CHECK(isColour(at(&im, 3.08f, 12.0f), 0x000000, OPAQUE), "B %d: the stroke left of the stem",
          rows);
    /* the left corners are the stem's square stroke corners (x 2.08 ..
       4.08 by y 0.45 .. 2.45 and 21.55 .. 23.55: a texel, under 1 unit at
       24 rows, lies inside them); the right ones are the bowls' side, over
       3 units from the outline (the upper bowl near (18.5, 4), the lower
       near (19.3, 19.9)): transparent */
    CHECK(isColour(px(&im, 0, 0), 0x000000, OPAQUE) &&
              isColour(px(&im, 0, im.h - 1), 0x000000, OPAQUE),
          "B %d: the left corners the stroke's (a %d, %d)", rows, px(&im, 0, 0)[3],
          px(&im, 0, im.h - 1)[3]);
    CHECK(px(&im, im.w - 1, 0)[3] == 0 && px(&im, im.w - 1, im.h - 1)[3] == 0,
          "B %d: the right corners transparent (a %d, %d)", rows, px(&im, im.w - 1, 0)[3],
          px(&im, im.w - 1, im.h - 1)[3]);
    if (rows >= 96) {
        /* the upper hole's middle (10.5, 7.815): 2.64 from its top and
           bottom edges, 3.08 from its left: past the half stroke, and no
           fill (evenodd) */
        CHECK(at(&im, 10.5f, 7.815f)[3] == 0, "B %d: the upper hole's middle transparent (a %d)",
              rows, at(&im, 10.5f, 7.815f)[3]);
    }
    freeImg(&im);
}

/* xbox/cross.svg, the A: "m6.15 21.55h-3.44l7.32-19.1 ... h-7.62z" then the
   hole "m5.66-15.8h-0.08l-2.88 8.21h5.83z": from the first point (6.15,
   21.55) to (11.81, 5.75), (11.73, 5.75), (8.85, 13.96), (14.68, 13.96).  At
   y 11.5 the hole spans x 9.71 .. 13.82 (its legs), so the texel at (11.5,
   11.5) is inside it; its middle is about 2.07 from the three sides, so the
   stroke covers nearly all of it: black there, not the blue fill.  The left
   leg at y 19 spans x 3.69 (2.71 + 7.32 * 2.55 / 19.1) .. 7.06 (6.15 + 1.8 *
   2.55 / 5.06). */
static void testA(int rows)
{
    Img im = raster(ICO_PAD_GLYPHS_XBOX, CROSS, rows);
    if (!im.p) {
        return;
    }
    CHECK(isColour(at(&im, 5.37f, 19.0f), 0x0078ff, OPAQUE), "A %d: the fill in the left leg",
          rows);
    const uint8_t *h = at(&im, rows >= 96 ? 11.8f : 11.5f, rows >= 96 ? 11.8f : 11.5f);
    CHECK(isColour(h, 0x000000, -1) && h[3] > 0,
          "A %d: the hole holds the stroke, no fill "
          "(%02x %02x %02x %02x)",
          rows, h[0], h[1], h[2], h[3]);
    freeImg(&im);
}

#define HEAD24                                                                                     \
    "<?xml version=\"1.0\"?>\n<svg version=\"1.2\" xmlns=\"http://www.w3.org/2000/svg\" "          \
    "viewBox=\"0 0 24 24\" width=\"24\" height=\"24\">\n<!-- a test file -->\n"                    \
    "<style>.s0 { fill: #0078ff;stroke: #000000;paint-order:stroke fill markers;"                  \
    "stroke-miterlimit:100;stroke-width: 4 } </style>\n"
#define HEAD40                                                                                      \
    "<svg version=\"1.2\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 40 24\" width=\"40\" " \
    "height=\"24\">\n<style>\n.s0 { fill: #e1e1e1;stroke: #000000;paint-order:stroke fill "         \
    "markers;stroke-miterlimit:100;stroke-width: 4 }\n</style>\n"
#define A_D                                                                                        \
    "m6.15 21.55h-3.44l7.32-19.1h3.56l7.35 19.1h-3.58l-1.79-5.06h-7.62zm5.66-15.8h-0.08l-2.88 "    \
    "8.21h5.83z"

static int load(int style, int glyph, const char *svg)
{
    return pad_glyphs_test_source(style, glyph, svg, svg ? strlen(svg) : 0);
}

/* two squares turning the same way, x 2 .. 22 and 8 .. 16: with evenodd
   the inner one is a hole whose middle (12, 12) is 4 from its sides,
   past the half stroke; non-zero fills it.  Its ink box is the whole
   viewBox (the outer square's stroke and miters reach 0 and 24). */
static void testRing(void)
{
    static const char *kRule[2] = {" fill-rule=\"evenodd\"", ""};
    for (int r = 0; r < 2; r++) {
        char svg[600];
        snprintf(svg, sizeof(svg),
                 HEAD24 "<path class=\"s0\"%s d=\"M2 2H22V22H2ZM8 8H16V16H8Z\"/>"
                        "</svg>",
                 kRule[r]);
        CHECK(load(ICO_PAD_GLYPHS_XBOX, SQUARE, svg) == 1, "the ring (%s) read: %s",
              r ? "non-zero" : "evenodd", pad_glyphs_piece_error(ICO_PAD_GLYPHS_XBOX, SQUARE));
        Img im = raster(ICO_PAD_GLYPHS_XBOX, SQUARE, 96);
        if (im.p) {
            const uint8_t *m = at(&im, 12.0f, 12.0f), *band = at(&im, 4.5f, 12.0f);
            if (r == 0) {
                CHECK(m[3] == 0, "evenodd: the ring's middle empty (a %d)", m[3]);
            } else {
                CHECK(isColour(m, 0x0078ff, OPAQUE), "non-zero: the ring's middle filled");
            }
            /* x 4.5 is 2.5 inside the outer edge, 3.5 from the inner one */
            CHECK(isColour(band, 0x0078ff, OPAQUE), "the ring's band filled (%s)",
                  r ? "non-zero" : "evenodd");
            /* the outer square's corner miter: the stroke's corner at (0, 0)
               .. (2, 2), the texel at (0.1, 0.1) inside it */
            CHECK(isColour(at(&im, 0.1f, 0.1f), 0x000000, OPAQUE), "the corner's miter");
        }
        freeImg(&im);
    }
    CHECK(load(ICO_PAD_GLYPHS_XBOX, SQUARE, NULL) == 1, "xbox/square.svg back");
}

/* 40 x 24 (xbox/l1.svg, LB) and 24 x 32 (wii_classic/cross.svg, b): their
   ink boxes and sizes.  LB "m3.24 21.77v-19.54h3.23 ...": the L's stem from
   x 3.24 (its square corners' miters at x 1.24, y 0.23 and 23.77); the B's
   lower bowl "q3.69 1.09 3.69 4.76" ends at x 36.75 going straight down:
   1.24 .. 38.75 by 0.23 .. 23.77, 37.51 x 23.54.  b "m7.37 23.66h-3.13
   v-21.66h3.35 ...": the stem x 4.24 .. 7.59 from y 2 (its corners' miters
   at x 2.24, y 0); the bowl's lowest point 23.98, so the ink is 0 ..
   25.98 down */
static void testSizes(int rows)
{
    float lbBox[4] = {0, 0, 0, 0}, bBox[4] = {0, 0, 0, 0};
    CHECK(pad_glyphs_ink_box(ICO_PAD_GLYPHS_XBOX, L1, lbBox) && fabsf(lbBox[0] - 1.24f) < 0.02f &&
              fabsf(lbBox[1] - 0.23f) < 0.02f && fabsf(lbBox[2] - 38.75f) < 0.05f &&
              fabsf(lbBox[3] - 23.77f) < 0.02f,
          "LB: the ink box %.3f,%.3f-%.3f,%.3f", (double)lbBox[0], (double)lbBox[1],
          (double)lbBox[2], (double)lbBox[3]);
    CHECK(pad_glyphs_ink_box(ICO_PAD_GLYPHS_WII_CLASSIC, CROSS, bBox) &&
              fabsf(bBox[0] - 2.24f) < 0.02f && fabsf(bBox[1]) < 0.02f &&
              fabsf(bBox[3] - 25.98f) < 0.05f,
          "b: the ink box %.3f,%.3f-%.3f,%.3f", (double)bBox[0], (double)bBox[1], (double)bBox[2],
          (double)bBox[3]);
    Img lb = raster(ICO_PAD_GLYPHS_XBOX, L1, rows);
    const int lbW =
        (int)ceilf((lbBox[2] - lbBox[0]) * ((float)rows / (lbBox[3] - lbBox[1])) - 1e-3f);
    CHECK(lb.w == lbW && lb.h == rows, "LB at %d rows: %d x %d (%d x %d)", rows, lb.w, lb.h, lbW,
          rows);
    if (lb.p) {
        /* the L's stem x 3.24 .. 6.47 (its stroke from 1.24); the B's stem
           from x 21.24 ("zm18 0v-19.54h7.56..."), its hole from 24.46 */
        CHECK(isColour(at(&lb, 4.855f, 12.0f), 0xe1e1e1, OPAQUE), "LB %d: the L's fill", rows);
        CHECK(isColour(at(&lb, 22.85f, 12.0f), 0xe1e1e1, OPAQUE), "LB %d: the B's fill", rows);
        CHECK(isColour(at(&lb, 2.5f, 12.0f), 0x000000, OPAQUE), "LB %d: the stroke left of L",
              rows);
    }
    freeImg(&lb);
    Img b = raster(ICO_PAD_GLYPHS_WII_CLASSIC, CROSS, rows);
    const int bW = (int)ceilf((bBox[2] - bBox[0]) * ((float)rows / (bBox[3] - bBox[1])) - 1e-3f);
    CHECK(b.w == bW && b.h == rows, "b at %d rows: %d x %d (%d x %d)", rows, b.w, b.h, bW, rows);
    if (b.p) {
        /* the stem x 4.24 .. 7.59 from y 2 to 9.74 at least ("v7.74") */
        CHECK(isColour(at(&b, 5.9f, 6.0f), 0xffffff, OPAQUE), "b %d: the stem's fill", rows);
    }
    freeImg(&b);
}

/* Every piece of every set at a lettering height of rows pixels: the ink
   reaches all four sides of the raster within a pixel (the cell is the ink
   box, rounded up to whole texels: no bearing left); every capital of a set
   is the same height within a pixel (each fills the lettering height); the
   Wii Classic Controller's b is as tall as the Nintendo set's B. */
static void testInk(int rows)
{
    int bH[2] = {-1, -1};
    for (int s = ICO_PAD_GLYPHS_XBOX; s < ICO_PAD_GLYPHS_COUNT; s++) {
        int lo = 1 << 30, hi = -1;
        for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
            Img im = raster(s, g, rows);
            if (!im.p) {
                continue;
            }
            int x0 = im.w, y0 = im.h, x1 = -1, y1 = -1;
            for (int y = 0; y < im.h; y++) {
                for (int x = 0; x < im.w; x++) {
                    if (px(&im, x, y)[3] > 0) {
                        x0 = x < x0 ? x : x0;
                        x1 = x > x1 ? x : x1;
                        y0 = y < y0 ? y : y0;
                        y1 = y > y1 ? y : y1;
                    }
                }
            }
            CHECK(x1 >= 0 && x0 <= 1 && y0 <= 1 && x1 >= im.w - 2 && y1 >= im.h - 2,
                  "%s piece %d at %d rows: the ink %d,%d-%d,%d in the %dx%d cell",
                  pad_glyphs_style_name(s), g, rows, x0, y0, x1, y1, im.w, im.h);
            const int h = y1 - y0 + 1;
            if (kTall[s - 1][g]) {
                CHECK(abs(h - rows) <= 1, "%s piece %d at %d rows: a capital %d rows tall",
                      pad_glyphs_style_name(s), g, rows, h);
                lo = h < lo ? h : lo;
                hi = h > hi ? h : hi;
            }
            if (g == CROSS && s != ICO_PAD_GLYPHS_XBOX) {
                bH[s == ICO_PAD_GLYPHS_WII_CLASSIC] = h;
            }
            freeImg(&im);
        }
        CHECK(hi >= 0 && hi - lo <= 1, "%s at %d rows: the capitals %d .. %d rows tall",
              pad_glyphs_style_name(s), rows, lo, hi);
    }
    CHECK(bH[0] > 0 && abs(bH[0] - bH[1]) <= 1,
          "at %d rows: the Nintendo B %d rows tall, the Wii Classic b %d", rows, bH[0], bH[1]);
}

/* the absolute forms against the embedded relative files */
static void testAbsolute(void)
{
    /* xbox/l2.svg (LT) "m4.74 21.77v-19.54h3.23v16.78h9.74v2.76zm21.13 0
       v-16.78h-6.13v-2.76h15.51v2.76h-6.13v16.78z" point by point */
    static const char kLT[] = HEAD40 "<path id=\"LT\" class=\"s0\" d=\"M4.74 21.77V2.23H7.97V19.01"
                                     "H17.71V21.77ZM25.87 21.77V4.99H19.74V2.23H35.25V4.99H29.12"
                                     "V21.77Z\"/></svg>";
    /* nintendo/l2.svg (ZL) "m3.89 21.77v-2.94l10.15-12.61q0.44-0.59
       1.06-1.23h-10.06v-2.76h14.13v2.64l-10.82 13.41-0.62 0.7h11.83v2.79z
       m19.25 0v-19.54h3.22v16.78h9.74v2.76z" point by point, the second
       lineto by an implicit repeat */
    static const char kZL[] = HEAD40 "<path id=\"ZL\" class=\"s0\" d=\"M3.89 21.77V18.83L14.04 6.22"
                                     "Q14.48 5.63 15.1 4.99H5.04V2.23H19.17V4.87L8.35 18.28 7.73 "
                                     "18.98H19.56V21.77ZM23.14 21.77V2.23H26.36V19.01H36.1V21.77Z"
                                     "\"/></svg>";

    static const struct {
        int style, glyph;
        const char *svg;
    } kCase[2] = {{ICO_PAD_GLYPHS_XBOX, L2, kLT}, {ICO_PAD_GLYPHS_NINTENDO, L2, kZL}};

    for (int c = 0; c < 2; c++) {
        for (int rows = 24; rows <= 96; rows += 72) {
            Img rel = raster(kCase[c].style, kCase[c].glyph, rows);
            CHECK(load(kCase[c].style, kCase[c].glyph, kCase[c].svg) == 1, "absolute case %d: %s",
                  c, pad_glyphs_piece_error(kCase[c].style, kCase[c].glyph));
            Img abs_ = raster(kCase[c].style, kCase[c].glyph, rows);
            CHECK(differences(&rel, &abs_) == 0, "absolute case %d at %d rows: %d texels differ", c,
                  rows, differences(&rel, &abs_));
            CHECK(load(kCase[c].style, kCase[c].glyph, NULL) == 1, "case %d's file back", c);
            freeImg(&rel);
            freeImg(&abs_);
        }
    }
    /* C and c: "m4 6h12c2.2 0 4 1.8 4 4v4c0 2.2-1.8 4-4 4h-12z", absolute:
       (4, 6), (16, 6), the curve over (18.2, 6) (20, 7.8) to (20, 10),
       (20, 14), over (20, 16.2) (18.2, 18) to (16, 18), (4, 18) */
    static const char kRel[] = HEAD24 "<path class=\"s0\" d=\"m4 6h12c2.2 0 4 1.8 4 4v4c0 2.2-1.8 "
                                      "4-4 4h-12z\"/></svg>";
    static const char kAbs[] = HEAD24 "<path class=\"s0\" d=\"M4 6H16C18.2 6 20 7.8 20 10V14C20 "
                                      "16.2 18.2 18 16 18H4Z\"/></svg>";
    CHECK(load(ICO_PAD_GLYPHS_XBOX, TRIANGLE, kRel) == 1, "the relative curve read");
    Img a = raster(ICO_PAD_GLYPHS_XBOX, TRIANGLE, 96);
    CHECK(load(ICO_PAD_GLYPHS_XBOX, TRIANGLE, kAbs) == 1, "the absolute curve read");
    Img b = raster(ICO_PAD_GLYPHS_XBOX, TRIANGLE, 96);
    CHECK(differences(&a, &b) == 0, "C and c: %d texels differ", differences(&a, &b));
    /* the shape's middle (12, 12) is 6 from its long sides: the fill */
    CHECK(isColour(at(&b, 12.0f, 12.0f), 0x0078ff, OPAQUE), "the curve shape's fill");
    freeImg(&a);
    freeImg(&b);
    CHECK(load(ICO_PAD_GLYPHS_XBOX, TRIANGLE, NULL) == 1, "xbox/triangle.svg back");
}

/* a file that reads exactly as xbox/cross.svg draws exactly as it */
static void testSameFile(void)
{
    static const char kA[] =
        HEAD24 "\t<g id=\"(og) xbox\">\n\t\t<path id=\"A\" fill-rule=\"evenodd\" "
               "class=\"s0\" d=\"" A_D "\"/>\n\t</g>\n</svg>\n";
    Img e = raster(ICO_PAD_GLYPHS_XBOX, CROSS, 48);
    CHECK(load(ICO_PAD_GLYPHS_XBOX, CROSS, kA) == 1, "the A through the test entry: %s",
          pad_glyphs_piece_error(ICO_PAD_GLYPHS_XBOX, CROSS));
    Img t = raster(ICO_PAD_GLYPHS_XBOX, CROSS, 48);
    CHECK(e.p && t.p && memcmp(e.p, t.p, (size_t)e.w * (size_t)e.h * 4) == 0,
          "the same file, the same texels");
    freeImg(&e);
    freeImg(&t);
    CHECK(load(ICO_PAD_GLYPHS_XBOX, CROSS, NULL) == 1, "xbox/cross.svg back");
}

static void testBroken(void)
{
    static char big[9000];
    static char many[4200];
    memset(big, ' ', sizeof(big) - 1);
    memcpy(big, HEAD24 "<path class=\"s0\" d=\"" A_D "\"/>",
           strlen(HEAD24 "<path class=\"s0\" d=\"" A_D "\"/>"));
    memcpy(big + sizeof(big) - 8, "</svg>\n", 8);
    /* 600 path commands, past the reader's 512 */
    size_t n = (size_t)snprintf(many, sizeof(many), "%s", HEAD24 "<path class=\"s0\" d=\"m2 2");
    for (int i = 0; i < 600 && n + 8 < sizeof(many); i++) {
        n += (size_t)snprintf(many + n, sizeof(many) - n, "h0.01");
    }
    snprintf(many + n, sizeof(many) - n, "z\"/></svg>");
    const char *kBad[] = {
        /* an element that draws */
        HEAD24 "<circle cx=\"12\" cy=\"12\" r=\"4\"/><path class=\"s0\" d=\"" A_D "\"/></svg>",
        /* a transform */
        HEAD24 "<g transform=\"rotate(45)\"><path class=\"s0\" d=\"" A_D "\"/></g></svg>",
        /* an unknown command (an arc) */
        HEAD24 "<path class=\"s0\" d=\"m2 2h20v20a10 10 0 0 1-20 0z\"/></svg>",
        /* an open subpath */
        HEAD24 "<path class=\"s0\" d=\"m6.15 21.55h-3.44l7.32-19.1h3.56z"
               "m5.66-15.8h-0.08l-2.88 8.21h5.83\"/></svg>",
        /* no viewBox */
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\"><path d=\"" A_D
        "\"/></svg>",
        /* two paths */
        HEAD24 "<path class=\"s0\" d=\"" A_D "\"/><path class=\"s0\" d=\"" A_D "\"/></svg>",
        /* a style property the reader does not draw */
        "<svg viewBox=\"0 0 24 24\"><style>.s0 { fill: #fff; opacity: 0.5 }</style><path "
        "class=\"s0\" d=\"" A_D "\"/></svg>",
        /* a fill attribute on the path */
        HEAD24 "<path class=\"s0\" fill=\"#fff\" d=\"" A_D "\"/></svg>",
        /* not closed */
        HEAD24 "<path class=\"s0\" d=\"" A_D "\"/>",
        /* a viewBox too small to scale (its outline would leave the
           rasteriser's floats) */
        "<svg viewBox=\"0 0 1e-36 1e-36\"><style>.s0 { fill: #fff }</style><path class=\"s0\" "
        "d=\"" A_D "\"/></svg>",
        big,
        many,
    };
    const int nBad = (int)(sizeof(kBad) / sizeof(kBad[0]));
    for (int i = 0; i < nBad; i++) {
        CHECK(load(ICO_PAD_GLYPHS_XBOX, CROSS, kBad[i]) == 0, "broken file %d refused", i);
        const char *why = pad_glyphs_piece_error(ICO_PAD_GLYPHS_XBOX, CROSS);
        CHECK(why && why[0], "broken file %d: a reason", i);
        fprintf(stderr, "pad_glyphs_test: broken file %d: %s\n", i, why ? why : "(none)");
        CHECK(!pad_glyphs_piece_valid(ICO_PAD_GLYPHS_XBOX, CROSS), "broken file %d: invalid", i);
        CHECK(pad_glyphs_set_complete(ICO_PAD_GLYPHS_XBOX) == 0, "broken file %d: Xbox incomplete",
              i);
        CHECK(pad_glyphs_set_complete(ICO_PAD_GLYPHS_NINTENDO) == 1, "the other sets stay");
        int w = 0, h = 0;
        CHECK(pad_glyphs_raster_size(ICO_PAD_GLYPHS_XBOX, CROSS, 24, &w, &h) == -1,
              "an invalid piece is not drawn");
        pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
        CHECK(pad_glyphs_style() == ICO_PAD_GLYPHS_PLAYSTATION && !pad_glyphs_active(),
              "broken file %d: the incomplete set cannot be in force", i);
    }
    CHECK(big[0] == '<' && strlen(big) > 8192, "the big file is past the bound");
    /* the embedded file back: complete, and the style can be set */
    CHECK(load(ICO_PAD_GLYPHS_XBOX, CROSS, NULL) == 1, "xbox/cross.svg back");
    CHECK(pad_glyphs_set_complete(ICO_PAD_GLYPHS_XBOX) == 1, "Xbox complete again");
    pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
    CHECK(pad_glyphs_style() == ICO_PAD_GLYPHS_XBOX && pad_glyphs_active(), "Xbox in force");
    /* a set made incomplete while in force stops at once */
    CHECK(load(ICO_PAD_GLYPHS_XBOX, R2, kBad[0]) == 0, "R2 broken");
    CHECK(pad_glyphs_style() == ICO_PAD_GLYPHS_PLAYSTATION && !pad_glyphs_active(),
          "the style in force falls back to PlayStation");
    CHECK(load(ICO_PAD_GLYPHS_XBOX, R2, NULL) == 1, "xbox/r2.svg back");
    pad_glyphs_set_style(ICO_PAD_GLYPHS_NINTENDO);
    CHECK(pad_glyphs_style() == ICO_PAD_GLYPHS_NINTENDO && pad_glyphs_active(), "Nintendo");
    pad_glyphs_set_style(ICO_PAD_GLYPHS_COUNT);
    CHECK(pad_glyphs_style() == ICO_PAD_GLYPHS_PLAYSTATION, "a style out of range");
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    CHECK(!pad_glyphs_active(), "PlayStation draws no art of its own");
}

int main(void)
{
    pad_glyphs_init();
    testSets();
    testB(24);
    testB(96);
    testA(24);
    testA(96);
    testRing();
    testSizes(24);
    testSizes(96);
    testInk(24);
    testInk(48);
    testInk(96);
    testAbsolute();
    testSameFile();
    testBroken();
    for (int s = ICO_PAD_GLYPHS_XBOX; s < ICO_PAD_GLYPHS_COUNT; s++) {
        CHECK(pad_glyphs_set_complete(s) == 1, "%s complete at the end", pad_glyphs_style_name(s));
    }
    if (s_fail) {
        fprintf(stderr, "pad_glyphs_test: %d failures\n", s_fail);
        return 1;
    }
    printf("pad_glyphs_test: ok\n");
    return 0;
}
