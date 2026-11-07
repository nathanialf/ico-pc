/*
 * port/platform/app_icon_image.h
 *
 * The icon's image maths, pure C on caller buffers (app_icon_image.c).
 * Frames are RGBA8, rows w * 4 bytes apart.  A is the capture over a black
 * clear colour, B the same tick over white.
 */
#ifndef ICO_PLATFORM_APP_ICON_IMAGE_H
#define ICO_PLATFORM_APP_ICON_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICO_ICON_SIZE 256
/* the pixels of the matte that count as the letter */
#define ICO_ICON_MIN_ALPHA 8
/* the two frames' ink boxes may differ by this many pixels per edge */
#define ICO_ICON_GHOST_TOLERANCE 1

typedef struct IcoIconBox {
    int x0, y0, x1, y1; /* inclusive */
} IcoIconBox;

/* Difference matte: alpha = 255 - max over channels of (B - A), colour =
 * A * 255 / alpha (straight alpha); pixels of alpha 0 are all zero.  out
 * is w * h * 4 bytes.  0, or -1 on a bad argument. */
int ico_icon_matte(const uint8_t *a, const uint8_t *b, int w, int h, uint8_t *out);

/* Single-frame luma key: alpha = the brightest channel of A, colour =
 * A * 255 / alpha. */
int ico_icon_luma_key(const uint8_t *a, int w, int h, uint8_t *out);

/* 1 when the pair must be refused: the box of A's non-black pixels and the
 * box of B's non-white pixels (a channel off by 8 or more) differ by more
 * than ICO_ICON_GHOST_TOLERANCE at any edge, or one of them is empty and
 * the other is not.  0 when they agree (also when both are empty). */
int ico_icon_pair_ghost(const uint8_t *a, const uint8_t *b, int w, int h);

/* The box of pixels with alpha >= minAlpha.  0, or -1 when there is none. */
int ico_icon_bbox(const uint8_t *rgba, int w, int h, int minAlpha, IcoIconBox *box);

/* The square for a box: its longer side plus a 10 % margin on each side,
 * centred on the box, shifted inside the frame, and shrunk to the frame's
 * shorter side when it does not fit.  Written as x0, y0 and the side in
 * sq->x1 - sq->x0 + 1.  0, or -1 on a bad argument. */
int ico_icon_square(const IcoIconBox *box, int w, int h, IcoIconBox *sq);

/* The square sq of a straight-alpha RGBA image resampled to dw x dw: an
 * area average (weighted by alpha) when shrinking, bilinear when growing.
 * Straight alpha in and out.  0, or -1. */
int ico_icon_resample(const uint8_t *src, int sw, int sh, const IcoIconBox *sq, uint8_t *dst,
                      int dw);

/* An image box-downscaled to dw x dh (area average, weighted by alpha). */
int ico_icon_downscale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* The whole crop: bbox of alpha >= ICO_ICON_MIN_ALPHA, square, 256 x 256
 * into out256 (ICO_ICON_SIZE^2 * 4 bytes).  0, or -1 when the matte holds
 * no letter. */
int ico_icon_make256(const uint8_t *matte, int w, int h, uint8_t *out256);

#ifdef __cplusplus
}
#endif
#endif /* ICO_PLATFORM_APP_ICON_IMAGE_H */
