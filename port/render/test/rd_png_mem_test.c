/*
 * port/render/test/rd_png_mem_test.c
 *
 * rd_write_png_mem (rd_png.c):
 *   golden    a 2 x 2 image (RGBA and RGB) encodes to the exact bytes the
 *             file writer produced before it was split (captured from
 *             that code), so the split changed no output
 *   file      rd_write_png's file holds the same bytes as the memory blob
 *             (a multi-block image: more than 65535 raw bytes)
 *   round     the blob decodes through texpack_load_png to the source
 *             exactly, with a padded pitch and a ragged size; the RGB
 *             form comes back with the loader's alpha 0x80 (a PNG without alpha)
 *   refusals  no output pointers, no pixels, zero size, a short pitch; the
 *             output is NULL / 0 then
 * argv[1]: a folder the test may write in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "rd_png.h"
#include "texpack.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static const uint8_t k_px[16] = {10, 20, 30, 255, 200, 100, 50, 128, 0, 0, 0, 0, 255, 255, 255, 7};

static const char *const k_goldenRgba =
    "89504E470D0A1A0A0000000D494844520000000200000002080600000072B60D"
    "240000001D494441547801011200EDFF000A141EFFC86432800000000000FFFF"
    "FF0730AC061EFD085F240000000049454E44AE426082";

static const char *const k_goldenRgb =
    "89504E470D0A1A0A0000000D4948445200000002000000020802000000FDD49A"
    "7300000019494441547801010E00F1FF000A141EC8643200000000FFFFFF15A8"
    "04986528749C0000000049454E44AE426082";

static void pattern(uint8_t *p, uint32_t w, uint32_t h, uint32_t pitch)
{
    memset(p, 0xEE, (size_t)pitch * h); /* the padding must not reach the PNG */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *q = p + (size_t)y * pitch + (size_t)x * 4;
            q[0] = (uint8_t)(x * 7 + y);
            q[1] = (uint8_t)(y * 13 + (x >> 2));
            q[2] = (uint8_t)(x ^ y);
            q[3] = (uint8_t)(x * 3 + y * 5);
        }
    }
}

/* hex text to bytes; returns the count */
static size_t unhex(const char *hex, uint8_t *out)
{
    size_t n = 0;
    for (; hex[0] && hex[1]; hex += 2) {
        char t[3] = {hex[0], hex[1], 0};
        out[n++] = (uint8_t)strtoul(t, NULL, 16);
    }
    return n;
}

static void golden(void)
{
    uint8_t want[128];
    uint8_t *blob = NULL;
    size_t n = 0;
    CHECK(rd_write_png_mem(k_px, 2, 2, 8, 1, &blob, &n));
    size_t wn = unhex(k_goldenRgba, want);
    CHECK(n == wn && blob && memcmp(blob, want, n) == 0);
    free(blob);
    CHECK(rd_write_png_mem(k_px, 2, 2, 8, 0, &blob, &n));
    wn = unhex(k_goldenRgb, want);
    CHECK(n == wn && blob && memcmp(blob, want, n) == 0);
    free(blob);
}

static void roundTrip(uint32_t w, uint32_t h, uint32_t pitch, int withAlpha)
{
    uint8_t *src = malloc((size_t)pitch * h);
    pattern(src, w, h, pitch);
    uint8_t *blob = NULL;
    size_t n = 0;
    CHECK(rd_write_png_mem(src, w, h, pitch, withAlpha, &blob, &n));
    TexpackImage img;
    CHECK(blob && texpack_load_png(blob, n, "rd_png_mem_test", &img) == 0);
    if (blob && img.blob) {
        CHECK(img.w == w && img.h == h);
        int bad = 0;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t *a = src + (size_t)y * pitch + (size_t)x * 4;
                const uint8_t *b = img.lv[0].data + (size_t)y * img.lv[0].pitch + (size_t)x * 4;
                bad += a[0] != b[0] || a[1] != b[1] || a[2] != b[2] ||
                       b[3] != (withAlpha ? a[3] : 0x80);
            }
        }
        CHECK(bad == 0);
    }
    texpack_free_image(&img);
    free(blob);
    free(src);
}

static void fileEqual(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_png_mem_test.png", dir);
    const uint32_t w = 301, h = 117, pitch = w * 4 + 16; /* 140 KB raw: three stored blocks */
    uint8_t *src = malloc((size_t)pitch * h);
    pattern(src, w, h, pitch);
    for (int withAlpha = 0; withAlpha < 2; withAlpha++) {
        uint8_t *blob = NULL;
        size_t n = 0;
        CHECK(rd_write_png_mem(src, w, h, pitch, withAlpha, &blob, &n));
        CHECK(rd_write_png(path, src, w, h, pitch, withAlpha));
        FILE *f = ico_fopen(path, "rb");
        CHECK(f != NULL);
        if (f) {
            uint8_t *back = malloc(n + 1);
            const size_t got = fread(back, 1, n + 1, f);
            fclose(f);
            CHECK(got == n && memcmp(back, blob, n) == 0);
            free(back);
        }
        free(blob);
    }
    (void)ico_remove(path);
    free(src);
}

static void refusals(void)
{
    uint8_t *blob = (uint8_t *)1;
    size_t n = 5;
    CHECK(!rd_write_png_mem(NULL, 2, 2, 8, 1, &blob, &n) && blob == NULL && n == 0);
    CHECK(!rd_write_png_mem(k_px, 0, 2, 8, 1, &blob, &n) && blob == NULL && n == 0);
    CHECK(!rd_write_png_mem(k_px, 2, 0, 8, 1, &blob, &n) && blob == NULL && n == 0);
    CHECK(!rd_write_png_mem(k_px, 2, 2, 7, 1, &blob, &n) && blob == NULL && n == 0);
    CHECK(!rd_write_png_mem(k_px, 2, 2, 8, 1, NULL, &n));
    CHECK(!rd_write_png_mem(k_px, 2, 2, 8, 1, &blob, NULL));
    CHECK(!rd_write_png(NULL, k_px, 2, 2, 8, 1));
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: rd_png_mem_test <folder>\n");
        return 2;
    }
    golden();
    roundTrip(2, 2, 8, 1);
    roundTrip(37, 23, 37 * 4 + 12, 1);
    roundTrip(37, 23, 37 * 4 + 12, 0);
    roundTrip(256, 256, 1024, 1);
    fileEqual(argv[1]);
    refusals();
    if (fails) {
        printf("rd_png_mem_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("rd_png_mem_test: ok\n");
    return 0;
}
