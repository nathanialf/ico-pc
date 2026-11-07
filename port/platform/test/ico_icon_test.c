/*
 * port/platform/test/ico_icon_test.c
 *
 * ico_icon_write (app_icon_ico.c) read back: the ICONDIR (count 4), the
 * four entries (16, 32, 48 as BMP, 256 as a PNG with width and height byte
 * 0), each BMP's resource size (40 + w*h*4 + the AND mask), the
 * BITMAPINFOHEADER (height 2*h, 32 bits), the bottom-up BGRA row order
 * (the source's first row is the last stored), the transparent column kept
 * clear, the zero mask, the 256 entry holding the given PNG byte for byte,
 * the offsets contiguous and ending at the file's end, no .tmp left, and
 * the refusals.  argv[1]: a folder the test may write in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h"
#include "host_fs.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t *buf = malloc(1 << 20);
    *n = fread(buf, 1, 1 << 20, f);
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: ico_icon_test <folder>\n");
        return 2;
    }
    char path[1024], tmp[1100];
    snprintf(path, sizeof(path), "%s/ico_icon_test.ico", argv[1]);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    /* red top half, blue bottom half, the last 64 columns clear */
    uint8_t *img = calloc(256 * 256, 4);
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 192; x++) {
            uint8_t *p = img + ((size_t)y * 256 + x) * 4;
            p[0] = y < 128 ? 255 : 0;
            p[2] = y < 128 ? 0 : 255;
            p[3] = 255;
        }
    }
    uint8_t png[300];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(png, sig, 8);
    for (int i = 8; i < 300; i++) {
        png[i] = (uint8_t)(i * 7);
    }

    CHECK(ico_icon_write(path, png, sizeof(png), img) == 0);
    CHECK(ico_path_kind(tmp, NULL, NULL) == -1);
    size_t n = 0;
    uint8_t *f = slurp(path, &n);
    CHECK(f != NULL);
    if (f == NULL) {
        return 1;
    }
    CHECK(n > 70);
    CHECK(rd16(f) == 0 && rd16(f + 2) == 1 && rd16(f + 4) == 4);
    static const unsigned want[4] = {16, 32, 48, 256};
    size_t next = 6 + 16 * 4;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = f + 6 + 16 * i;
        const unsigned s = want[i];
        CHECK(e[0] == (s == 256 ? 0 : s) && e[1] == (s == 256 ? 0 : s));
        CHECK(e[2] == 0 && e[3] == 0);
        CHECK(rd16(e + 4) == 1 && rd16(e + 6) == 32);
        const uint32_t bytes = rd32(e + 8), off = rd32(e + 12);
        CHECK(off == next);
        CHECK((size_t)off + bytes <= n);
        next = (size_t)off + bytes;
        if (s == 256) {
            CHECK(bytes == sizeof(png));
            CHECK(off + 8 <= n && memcmp(f + off, sig, 8) == 0);
            CHECK(memcmp(f + off, png, sizeof(png)) == 0);
            continue;
        }
        const size_t maskRow = ((s + 31) / 32) * 4;
        CHECK(bytes == 40 + s * s * 4 + maskRow * s);
        const uint8_t *b = f + off;
        CHECK(rd32(b) == 40 && rd32(b + 4) == s && rd32(b + 8) == 2 * s);
        CHECK(rd16(b + 12) == 1 && rd16(b + 14) == 32 && rd32(b + 16) == 0);
        const uint8_t *px = b + 40;
        const uint8_t *top = px + (size_t)(s - 1) * s * 4; /* the last stored row = row 0 */
        const uint8_t *bottom = px;                        /* the first stored row = row s-1 */
        /* BGRA: row 0 is red, the last row blue */
        CHECK(top[0] == 0 && top[1] == 0 && top[2] == 255 && top[3] == 255);
        CHECK(bottom[0] == 255 && bottom[1] == 0 && bottom[2] == 0 && bottom[3] == 255);
        /* the clear quarter stays clear with no colour */
        const uint8_t *clr = px + ((size_t)(s / 2) * s + s - 1) * 4;
        CHECK(clr[0] == 0 && clr[1] == 0 && clr[2] == 0 && clr[3] == 0);
        /* the AND mask is all zero */
        const uint8_t *mask = px + (size_t)s * s * 4;
        int nz = 0;
        for (size_t k = 0; k < maskRow * s; k++) {
            nz |= mask[k];
        }
        CHECK(nz == 0);
    }
    CHECK(next == n);
    free(f);

    /* the refusals, and a rewrite over the old file */
    CHECK(ico_icon_write(NULL, png, sizeof(png), img) == -1);
    CHECK(ico_icon_write(path, NULL, sizeof(png), img) == -1);
    CHECK(ico_icon_write(path, png, 0, img) == -1);
    CHECK(ico_icon_write(path, png, sizeof(png), NULL) == -1);
    CHECK(ico_icon_write(path, png, 100, img) == 0);
    f = slurp(path, &n);
    CHECK(f != NULL && rd32(f + 6 + 16 * 3 + 8) == 100);
    free(f);
    CHECK(ico_path_kind(tmp, NULL, NULL) == -1);
    (void)ico_remove(path);
    free(img);
    if (fails) {
        printf("ico_icon_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("ico_icon_test: ok\n");
    return 0;
}
