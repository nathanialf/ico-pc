/*
 * port/platform/test/app_icon_image_test.c
 *
 * The icon's image maths (app_icon_image.c) on synthetic frames: a
 * soft-edged coloured ellipse composed over black (A) and over white (B).
 *
 *   matte      alpha within +-2 of the true coverage everywhere, colour
 *              within +-2 where alpha >= 192 (below it one
 *              step of rounding in A or B weighs more), nothing where it is 0
 *   bbox       the box of alpha >= 8 against the true coverage's
 *   square     the longer side plus 10 % each side, centred, inside the
 *              frame, shrunk and shifted at the frame's edge
 *   256        the crop's output size, shrinking (a large frame) and
 *              growing (a small one), the centre opaque, the corners clear
 *   downscale  colour kept at transparent edges (alpha weighting)
 *   ghost      the frames shifted 3 px refuse, 0 and 1 px accept
 *   luma       the single-frame key: alpha = brightest channel
 * Exit status 0 on success.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon_image.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

typedef struct Shape {
    double cx, cy, rx, ry;
    int col[3];
} Shape;

static double coverage(const Shape *s, int x, int y)
{
    const double dx = (x + 0.5 - s->cx) / s->rx, dy = (y + 0.5 - s->cy) / s->ry;
    const double r = sqrt(dx * dx + dy * dy);
    double c = (1.0 - r) * s->rx / 2.0 + 0.5; /* about 2 px of soft edge */
    return c < 0 ? 0 : c > 1 ? 1 : c;
}

/* A over black and B over white, with the shape moved by shift px in x in B */
static void compose(const Shape *s, int w, int h, int shift, uint8_t *a, uint8_t *b)
{
    Shape sb = *s;
    sb.cx += shift;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const double ca = coverage(s, x, y), cb = coverage(&sb, x, y);
            uint8_t *pa = a + ((size_t)y * w + x) * 4, *pb = b + ((size_t)y * w + x) * 4;
            for (int c = 0; c < 3; c++) {
                pa[c] = (uint8_t)lround(s->col[c] * ca);
                pb[c] = (uint8_t)lround(s->col[c] * cb + 255.0 * (1 - cb));
            }
            pa[3] = pb[3] = 255;
        }
    }
}

static int near(int a, int b, int tol)
{
    return abs(a - b) <= tol;
}

static void testMatte(void)
{
    const int w = 200, h = 160;
    const Shape s = {100, 80, 60, 40, {200, 120, 60}};
    uint8_t *a = malloc((size_t)w * h * 4), *b = malloc((size_t)w * h * 4),
            *m = malloc((size_t)w * h * 4);
    compose(&s, w, h, 0, a, b);
    CHECK(ico_icon_matte(a, b, w, h, m) == 0);
    int badA = 0, badC = 0, badZero = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = m + ((size_t)y * w + x) * 4;
            const int ta = (int)lround(255 * coverage(&s, x, y));
            if (!near(p[3], ta, 2)) {
                badA++;
            }
            if (p[3] >= 192) {
                for (int c = 0; c < 3; c++) {
                    if (!near(p[c], s.col[c], 2)) {
                        badC++;
                    }
                }
            }
            if (p[3] == 0 && (p[0] || p[1] || p[2])) {
                badZero++;
            }
        }
    }
    CHECK(badA == 0);
    CHECK(badC == 0);
    CHECK(badZero == 0);
    CHECK(ico_icon_matte(NULL, b, w, h, m) == -1);
    CHECK(ico_icon_matte(a, b, 0, h, m) == -1);

    /* the box against the true coverage's (alpha >= 8) */
    IcoIconBox box, tb = {w, h, -1, -1};
    CHECK(ico_icon_bbox(m, w, h, ICO_ICON_MIN_ALPHA, &box) == 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (lround(255 * coverage(&s, x, y)) >= ICO_ICON_MIN_ALPHA) {
                tb.x0 = x < tb.x0 ? x : tb.x0;
                tb.y0 = y < tb.y0 ? y : tb.y0;
                tb.x1 = x > tb.x1 ? x : tb.x1;
                tb.y1 = y > tb.y1 ? y : tb.y1;
            }
        }
    }
    CHECK(near(box.x0, tb.x0, 1) && near(box.x1, tb.x1, 1));
    CHECK(near(box.y0, tb.y0, 1) && near(box.y1, tb.y1, 1));
    CHECK(box.x1 - box.x0 > 110 && box.y1 - box.y0 > 70);
    uint8_t *none = calloc((size_t)w * h, 4);
    CHECK(ico_icon_bbox(none, w, h, ICO_ICON_MIN_ALPHA, &box) == -1);
    free(none);

    /* ghost: shifted 3 px refuses; 0 and 1 accept */
    CHECK(ico_icon_pair_ghost(a, b, w, h) == 0);
    compose(&s, w, h, 1, a, b);
    CHECK(ico_icon_pair_ghost(a, b, w, h) == 0);
    compose(&s, w, h, 3, a, b);
    CHECK(ico_icon_pair_ghost(a, b, w, h) == 1);
    compose(&s, w, h, -3, a, b);
    CHECK(ico_icon_pair_ghost(a, b, w, h) == 1);
    /* both frames empty agree; one empty refuses */
    uint8_t *blk = calloc((size_t)w * h, 4), *wht = malloc((size_t)w * h * 4);
    memset(wht, 255, (size_t)w * h * 4);
    CHECK(ico_icon_pair_ghost(blk, wht, w, h) == 0);
    compose(&s, w, h, 0, a, b);
    CHECK(ico_icon_pair_ghost(a, wht, w, h) == 1);
    free(blk);
    free(wht);
    free(a);
    free(b);
    free(m);
}

static void testSquare(void)
{
    IcoIconBox sq;
    /* a 121 x 81 box in a roomy frame: side 121, margin 13 each, S = 147 */
    const IcoIconBox box = {40, 50, 160, 130};
    CHECK(ico_icon_square(&box, 400, 300, &sq) == 0);
    const int s = sq.x1 - sq.x0 + 1;
    CHECK(s == 121 + 2 * 13);
    CHECK(sq.y1 - sq.y0 + 1 == s);
    CHECK(sq.x0 == 40 - 13);
    CHECK(near(sq.x0 + s / 2, (40 + 160) / 2, 1));
    CHECK(near(sq.y0 + s / 2, (50 + 130) / 2, 1));
    CHECK(sq.x0 <= box.x0 - 13 && sq.x1 >= box.x1 + 13);
    CHECK(sq.y0 >= 0 && sq.y1 < 300 && sq.x0 >= 0 && sq.x1 < 400);
    /* at the frame's corner the square shifts in, and shrinks to the frame */
    const IcoIconBox corner = {0, 0, 99, 99};
    CHECK(ico_icon_square(&corner, 100, 100, &sq) == 0);
    CHECK(sq.x0 == 0 && sq.y0 == 0 && sq.x1 == 99 && sq.y1 == 99);
    const IcoIconBox edge = {180, 10, 195, 25}; /* 16 px, near the right edge of a 200 frame */
    CHECK(ico_icon_square(&edge, 200, 100, &sq) == 0);
    CHECK(sq.x1 <= 199 && sq.x0 >= 0 && sq.x1 - sq.x0 == sq.y1 - sq.y0);
    CHECK(sq.x0 <= edge.x0 && sq.x1 >= edge.x1);
    const IcoIconBox bad = {5, 5, 2, 2};
    CHECK(ico_icon_square(&bad, 100, 100, &sq) == -1);
}

static void check256(const char *what, int w, int h, Shape s)
{
    uint8_t *a = malloc((size_t)w * h * 4), *b = malloc((size_t)w * h * 4),
            *m = malloc((size_t)w * h * 4);
    uint8_t *out = malloc((size_t)ICO_ICON_SIZE * ICO_ICON_SIZE * 4);
    compose(&s, w, h, 0, a, b);
    CHECK(ico_icon_matte(a, b, w, h, m) == 0);
    CHECK(ico_icon_make256(m, w, h, out) == 0);
    const uint8_t *c = out + ((size_t)128 * 256 + 128) * 4;
    if (!(c[3] >= 253 && near(c[0], s.col[0], 3) && near(c[1], s.col[1], 3) &&
          near(c[2], s.col[2], 3))) {
        printf("%s: centre %d %d %d %d\n", what, c[0], c[1], c[2], c[3]);
        fails++;
    }
    CHECK(out[3] == 0);
    CHECK(out[((size_t)255 * 256 + 255) * 4 + 3] == 0);
    /* the letter fills about 1/1.2 of the width: columns 21..234 opaque on the middle row */
    const uint8_t *row = out + (size_t)128 * 256 * 4;
    CHECK(row[30 * 4 + 3] >= 250 && row[225 * 4 + 3] >= 250);
    CHECK(row[10 * 4 + 3] <= 8 && row[245 * 4 + 3] <= 8);
    free(a);
    free(b);
    free(m);
    free(out);
}

static void testCrop(void)
{
    /* shrinking: the square is 1200 * 1.2 px, down to 256 */
    check256("shrink", 1600, 1200, (Shape){800, 600, 500, 500, {200, 120, 60}});
    /* growing: a 100 px letter */
    check256("grow", 300, 200, (Shape){150, 100, 50, 50, {30, 160, 220}});
    /* an empty matte has no letter */
    uint8_t *none = calloc(64 * 64, 4), *out = malloc(256 * 256 * 4);
    CHECK(ico_icon_make256(none, 64, 64, out) == -1);
    free(none);
    free(out);
}

static void testDownscale(void)
{
    /* the left half opaque colour (200, 100, 50), the right half clear (0): the
       small copies keep the colour at the edge column and the clear half clear */
    uint8_t *src = calloc(256 * 256, 4), *dst = malloc(48 * 48 * 4);
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 128; x++) {
            uint8_t *p = src + ((size_t)y * 256 + x) * 4;
            p[0] = 200;
            p[1] = 100;
            p[2] = 50;
            p[3] = 255;
        }
    }
    const int sizes[3] = {48, 32, 16};
    for (int i = 0; i < 3; i++) {
        const int d = sizes[i];
        CHECK(ico_icon_downscale(src, 256, 256, dst, d, d) == 0);
        for (int y = 0; y < d; y++) {
            const uint8_t *l = dst + ((size_t)y * d) * 4, *r = dst + ((size_t)y * d + d - 1) * 4;
            CHECK(l[3] == 255 && l[0] == 200 && l[1] == 100 && l[2] == 50);
            CHECK(r[3] == 0);
            for (int x = 0; x < d; x++) {
                const uint8_t *p = dst + ((size_t)y * d + x) * 4;
                if (p[3] > 0) {
                    CHECK(near(p[0], 200, 1) && near(p[1], 100, 1) && near(p[2], 50, 1));
                }
            }
        }
    }
    CHECK(ico_icon_downscale(src, 256, 256, dst, 300, 300) == -1);
    free(src);
    free(dst);
}

static void testLuma(void)
{
    const int w = 120, h = 100;
    const Shape s = {60, 50, 30, 30, {255, 128, 64}}; /* a channel at 255: alpha = coverage */
    uint8_t *a = malloc((size_t)w * h * 4), *b = malloc((size_t)w * h * 4),
            *m = malloc((size_t)w * h * 4);
    compose(&s, w, h, 0, a, b);
    CHECK(ico_icon_luma_key(a, w, h, m) == 0);
    int bad = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = m + ((size_t)y * w + x) * 4;
            if (!near(p[3], (int)lround(255 * coverage(&s, x, y)), 2)) {
                bad++;
            }
            if (p[3] >= 128 && !(near(p[0], 255, 2) && near(p[1], 128, 2) && near(p[2], 64, 2))) {
                bad++;
            }
        }
    }
    CHECK(bad == 0);
    memset(a, 0, (size_t)w * h * 4);
    CHECK(ico_icon_luma_key(a, w, h, m) == 0);
    uint8_t *out = malloc(256 * 256 * 4);
    CHECK(ico_icon_make256(m, w, h, out) == -1);
    free(out);
    free(a);
    free(b);
    free(m);
}

int main(void)
{
    testMatte();
    testSquare();
    testCrop();
    testDownscale();
    testLuma();
    if (fails) {
        printf("app_icon_image_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("app_icon_image_test: ok\n");
    return 0;
}
