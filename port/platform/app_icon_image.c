/*
 * port/platform/app_icon_image.c
 *
 * The icon's image maths (app_icon_image.h): the two-frame difference
 * matte, the letter's box, the square crop and the resampling.  Pure C on
 * caller buffers; no allocation here.
 *
 * The matte.  The same tick is captured twice with the clear colour black
 * (A) and white (B).  A pixel of colour C and coverage a/255 over a
 * background K reads K + (C - K) * a/255, so
 *     A_c = C_c * a/255        B_c = C_c * a/255 + 255 - a
 *     B_c - A_c = 255 - a      (the same for every channel)
 * which gives a = 255 - (B - A) and C = A * 255 / a.  The maximum over the
 * channels is taken so a rounding difference in one channel cannot lower
 * the alpha.
 */
#include <stdlib.h>
#include <string.h>

#include "app_icon_image.h"

static int maxc3(const uint8_t *p)
{
    int m = p[0];
    if (p[1] > m) {
        m = p[1];
    }
    if (p[2] > m) {
        m = p[2];
    }
    return m;
}

static int minc3(const uint8_t *p)
{
    int m = p[0];
    if (p[1] < m) {
        m = p[1];
    }
    if (p[2] < m) {
        m = p[2];
    }
    return m;
}

static int validFrame(const uint8_t *p, int w, int h)
{
    return p != NULL && w > 0 && h > 0 && (size_t)w <= 65536 && (size_t)h <= 65536;
}

/* colour channel c of straight-alpha output from the premultiplied-by-255
 * value ca = A_c * 255 and the alpha a (> 0) */
static uint8_t unmultiply(int ac, int a)
{
    int v = (ac * 255 + a / 2) / a;
    return (uint8_t)(v > 255 ? 255 : v);
}

int ico_icon_matte(const uint8_t *a, const uint8_t *b, int w, int h, uint8_t *out)
{
    if (!validFrame(a, w, h) || b == NULL || out == NULL) {
        return -1;
    }
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *pa = a + i * 4;
        const uint8_t *pb = b + i * 4;
        uint8_t *o = out + i * 4;
        int d = 0;
        for (int c = 0; c < 3; c++) {
            const int e = (int)pb[c] - (int)pa[c];
            if (e > d) {
                d = e;
            }
        }
        const int al = 255 - d;
        if (al <= 0) {
            o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        o[0] = unmultiply(pa[0], al);
        o[1] = unmultiply(pa[1], al);
        o[2] = unmultiply(pa[2], al);
        o[3] = (uint8_t)al;
    }
    return 0;
}

int ico_icon_luma_key(const uint8_t *a, int w, int h, uint8_t *out)
{
    if (!validFrame(a, w, h) || out == NULL) {
        return -1;
    }
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *pa = a + i * 4;
        uint8_t *o = out + i * 4;
        const int al = maxc3(pa);
        if (al == 0) {
            o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        o[0] = unmultiply(pa[0], al);
        o[1] = unmultiply(pa[1], al);
        o[2] = unmultiply(pa[2], al);
        o[3] = (uint8_t)al;
    }
    return 0;
}

/* the box of pixels for which ink(pixel) is true; 0 found, -1 none */
static int inkBox(const uint8_t *p, int w, int h, int white, IcoIconBox *box)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = p + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            const int ink = white ? minc3(row + x * 4) <= 255 - ICO_ICON_MIN_ALPHA
                                  : maxc3(row + x * 4) >= ICO_ICON_MIN_ALPHA;
            if (!ink) {
                continue;
            }
            if (x < x0) {
                x0 = x;
            }
            if (x > x1) {
                x1 = x;
            }
            if (y < y0) {
                y0 = y;
            }
            y1 = y;
        }
    }
    if (x1 < 0) {
        return -1;
    }
    box->x0 = x0;
    box->y0 = y0;
    box->x1 = x1;
    box->y1 = y1;
    return 0;
}

int ico_icon_pair_ghost(const uint8_t *a, const uint8_t *b, int w, int h)
{
    if (!validFrame(a, w, h) || b == NULL) {
        return 1;
    }
    IcoIconBox ba, bb;
    const int ha = inkBox(a, w, h, 0, &ba);
    const int hb = inkBox(b, w, h, 1, &bb);
    if (ha != 0 || hb != 0) {
        return ha == hb ? 0 : 1;
    }
    const int t = ICO_ICON_GHOST_TOLERANCE;
    return abs(ba.x0 - bb.x0) > t || abs(ba.y0 - bb.y0) > t || abs(ba.x1 - bb.x1) > t ||
           abs(ba.y1 - bb.y1) > t;
}

int ico_icon_bbox(const uint8_t *rgba, int w, int h, int minAlpha, IcoIconBox *box)
{
    if (!validFrame(rgba, w, h) || box == NULL) {
        return -1;
    }
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            if (row[x * 4 + 3] < minAlpha) {
                continue;
            }
            if (x < x0) {
                x0 = x;
            }
            if (x > x1) {
                x1 = x;
            }
            if (y < y0) {
                y0 = y;
            }
            y1 = y;
        }
    }
    if (x1 < 0) {
        return -1;
    }
    box->x0 = x0;
    box->y0 = y0;
    box->x1 = x1;
    box->y1 = y1;
    return 0;
}

/* floor((2 * lo + len - side) / 2): the left edge that centres side on
 * [lo, lo + len) */
static int centreLeft(int lo, int len, int side)
{
    const int t = 2 * lo + len - side;
    return t >= 0 ? t / 2 : -((-t + 1) / 2);
}

int ico_icon_square(const IcoIconBox *box, int w, int h, IcoIconBox *sq)
{
    if (box == NULL || sq == NULL || w <= 0 || h <= 0 || box->x1 < box->x0 || box->y1 < box->y0) {
        return -1;
    }
    const int bw = box->x1 - box->x0 + 1;
    const int bh = box->y1 - box->y0 + 1;
    const int side = bw > bh ? bw : bh;
    int s = side + 2 * ((side * 10 + 99) / 100);
    const int lim = w < h ? w : h;
    if (s > lim) {
        s = lim;
    }
    int x = centreLeft(box->x0, bw, s);
    int y = centreLeft(box->y0, bh, s);
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x + s > w) {
        x = w - s;
    }
    if (y + s > h) {
        y = h - s;
    }
    sq->x0 = x;
    sq->y0 = y;
    sq->x1 = x + s - 1;
    sq->y1 = y + s - 1;
    return 0;
}

/* A source rectangle (rx, ry, rw, rh) of an image sw pixels wide to dw x dh.
 * Shrinking an axis: the mean over the source pixels it covers; growing:
 * bilinear.  Both weight colour by alpha so a transparent neighbour's
 * (zero) colour does not darken an edge. */
static void resizeRect(const uint8_t *src, int sw, int rx, int ry, int rw, int rh, uint8_t *dst,
                       int dw, int dh)
{
    const int shrink = rw >= dw && rh >= dh;
    for (int j = 0; j < dh; j++) {
        for (int i = 0; i < dw; i++) {
            uint8_t *o = dst + ((size_t)j * (size_t)dw + (size_t)i) * 4;
            double sum[3] = {0, 0, 0}, sa = 0, wsum = 0;
            if (shrink) {
                const int x0 = (int)((int64_t)i * rw / dw);
                int x1 = (int)(((int64_t)(i + 1) * rw + dw - 1) / dw);
                const int y0 = (int)((int64_t)j * rh / dh);
                int y1 = (int)(((int64_t)(j + 1) * rh + dh - 1) / dh);
                if (x1 <= x0) {
                    x1 = x0 + 1;
                }
                if (y1 <= y0) {
                    y1 = y0 + 1;
                }
                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        const uint8_t *p =
                            src + ((size_t)(ry + y) * (size_t)sw + (size_t)(rx + x)) * 4;
                        sum[0] += p[0] * (double)p[3];
                        sum[1] += p[1] * (double)p[3];
                        sum[2] += p[2] * (double)p[3];
                        sa += p[3];
                        wsum += 1;
                    }
                }
            } else {
                double fx = ((i + 0.5) * rw) / dw - 0.5;
                double fy = ((j + 0.5) * rh) / dh - 0.5;
                if (fx < 0) {
                    fx = 0;
                }
                if (fy < 0) {
                    fy = 0;
                }
                int x0 = (int)fx, y0 = (int)fy;
                if (x0 > rw - 1) {
                    x0 = rw - 1;
                }
                if (y0 > rh - 1) {
                    y0 = rh - 1;
                }
                const int x1 = x0 + 1 < rw ? x0 + 1 : x0;
                const int y1 = y0 + 1 < rh ? y0 + 1 : y0;
                const double tx = fx - x0 > 1 ? 1 : fx - x0;
                const double ty = fy - y0 > 1 ? 1 : fy - y0;
                const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
                const double wx[2] = {1 - tx, tx}, wy[2] = {1 - ty, ty};
                for (int q = 0; q < 2; q++) {
                    for (int r = 0; r < 2; r++) {
                        const uint8_t *p =
                            src + ((size_t)(ry + ys[q]) * (size_t)sw + (size_t)(rx + xs[r])) * 4;
                        const double wt = wy[q] * wx[r];
                        sum[0] += wt * p[0] * (double)p[3];
                        sum[1] += wt * p[1] * (double)p[3];
                        sum[2] += wt * p[2] * (double)p[3];
                        sa += wt * p[3];
                        wsum += wt;
                    }
                }
            }
            if (sa <= 0 || wsum <= 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; c++) {
                const double v = sum[c] / sa + 0.5;
                o[c] = (uint8_t)(v > 255 ? 255 : v);
            }
            const double av = sa / wsum + 0.5;
            o[3] = (uint8_t)(av > 255 ? 255 : av);
        }
    }
}

int ico_icon_resample(const uint8_t *src, int sw, int sh, const IcoIconBox *sq, uint8_t *dst,
                      int dw)
{
    if (!validFrame(src, sw, sh) || sq == NULL || dst == NULL || dw <= 0 || sq->x0 < 0 ||
        sq->y0 < 0 || sq->x1 >= sw || sq->y1 >= sh || sq->x1 < sq->x0 || sq->y1 < sq->y0) {
        return -1;
    }
    resizeRect(src, sw, sq->x0, sq->y0, sq->x1 - sq->x0 + 1, sq->y1 - sq->y0 + 1, dst, dw, dw);
    return 0;
}

int ico_icon_downscale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    if (!validFrame(src, sw, sh) || dst == NULL || dw <= 0 || dh <= 0 || dw > sw || dh > sh) {
        return -1;
    }
    resizeRect(src, sw, 0, 0, sw, sh, dst, dw, dh);
    return 0;
}

int ico_icon_make256(const uint8_t *matte, int w, int h, uint8_t *out256)
{
    IcoIconBox box, sq;
    if (out256 == NULL || ico_icon_bbox(matte, w, h, ICO_ICON_MIN_ALPHA, &box) != 0 ||
        ico_icon_square(&box, w, h, &sq) != 0) {
        return -1;
    }
    return ico_icon_resample(matte, w, h, &sq, out256, ICO_ICON_SIZE);
}
