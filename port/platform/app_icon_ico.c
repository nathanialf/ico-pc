/*
 * port/platform/app_icon_ico.c
 *
 * The .ico writer (app_icon.h, ico_icon_write) and the atomic file write
 * the icon files share.  Layout (Microsoft's ICO format, as read by
 * Explorer, the shell and Steam):
 *
 *   ICONDIR      reserved 0, type 1, count 4                      6 bytes
 *   4 x entry    width, height (0 = 256), colours 0, reserved 0,
 *                planes 1, bits 32, bytes in resource, offset    16 bytes
 *   images       16, 32 and 48: BITMAPINFOHEADER (biHeight = 2 * h: the
 *                XOR bitmap and the AND mask), 32-bit BGRA rows bottom
 *                up, a 1-bit AND mask (all zero: the alpha channel
 *                decides) with rows padded to 4 bytes; 256: a PNG file
 *
 * The small sizes are BMP entries because some readers (Explorer's small
 * views, Steam's list) choose a small size and some refuse PNG-only files.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h"
#include "host_fs.h"

static void le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* data to path as <path>.tmp moved over it; the .tmp is removed on a
 * failure.  0, or -1. */
int ico_app_icon_write_atomic(const char *path, const uint8_t *data, size_t n)
{
    char tmp[1024];
    if (path == NULL || data == NULL ||
        snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    FILE *f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    int ok = fwrite(data, 1, n, f) == n;
    ok = ico_fsync(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || ico_rename_replace(tmp, path) != 0) {
        (void)ico_remove(tmp);
        return -1;
    }
    return 0;
}

#define ICO_ENTRIES 4

int ico_icon_write(const char *path, const uint8_t *png256, size_t n, const uint8_t *rgba256)
{
    static const int sizes[ICO_ENTRIES - 1] = {16, 32, 48};
    if (path == NULL || png256 == NULL || n == 0 || n > 0x7FFFFFFF || rgba256 == NULL) {
        return -1;
    }
    const size_t head = 6 + 16 * ICO_ENTRIES;
    size_t imageBytes[ICO_ENTRIES];
    size_t total = head;
    for (int i = 0; i < ICO_ENTRIES - 1; i++) {
        const size_t s = (size_t)sizes[i];
        const size_t mask = ((s + 31) / 32) * 4 * s;
        imageBytes[i] = 40 + s * s * 4 + mask;
        total += imageBytes[i];
    }
    imageBytes[ICO_ENTRIES - 1] = n;
    total += n;
    uint8_t *file = calloc(1, total);
    uint8_t *small = malloc(48 * 48 * 4);
    if (file == NULL || small == NULL) {
        free(file);
        free(small);
        return -1;
    }
    le16(file, 0);
    le16(file + 2, 1);
    le16(file + 4, ICO_ENTRIES);
    size_t at = head;
    int ok = 1;
    for (int i = 0; i < ICO_ENTRIES; i++) {
        uint8_t *e = file + 6 + 16 * (size_t)i;
        const int big = i == ICO_ENTRIES - 1;
        const int s = big ? 256 : sizes[i];
        e[0] = e[1] = (uint8_t)(big ? 0 : s);
        e[2] = 0;
        e[3] = 0;
        le16(e + 4, 1);
        le16(e + 6, 32);
        le32(e + 8, (uint32_t)imageBytes[i]);
        le32(e + 12, (uint32_t)at);
        if (big) {
            memcpy(file + at, png256, n);
        } else {
            ok = ok && ico_icon_downscale(rgba256, ICO_ICON_SIZE, ICO_ICON_SIZE, small, s, s) == 0;
            uint8_t *b = file + at;
            le32(b, 40);
            le32(b + 4, (uint32_t)s);
            le32(b + 8, (uint32_t)(2 * s));
            le16(b + 12, 1);
            le16(b + 14, 32);
            le32(b + 16, 0);                     /* BI_RGB */
            le32(b + 20, (uint32_t)(s * s * 4)); /* the XOR bitmap */
            uint8_t *d = b + 40;
            for (int y = 0; y < s; y++) { /* bottom-up */
                const uint8_t *row = small + (size_t)(s - 1 - y) * (size_t)s * 4;
                for (int x = 0; x < s; x++) {
                    d[0] = row[x * 4 + 2];
                    d[1] = row[x * 4 + 1];
                    d[2] = row[x * 4];
                    d[3] = row[x * 4 + 3];
                    d += 4;
                }
            }
            /* the AND mask follows, zero (calloc) */
        }
        at += imageBytes[i];
    }
    free(small);
    if (ok) {
        ok = ico_app_icon_write_atomic(path, file, total) == 0;
    }
    free(file);
    return ok ? 0 : -1;
}
