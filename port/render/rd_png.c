/* rd_png.c: a minimal PNG writer (8-bit RGBA or RGB, no compression).
 *
 * Written for the port from the PNG specification (ISO/IEC 15948, W3C PNG
 * 2nd edition) and RFC 1950/1951: the zlib stream holds stored (type 0)
 * deflate blocks, each scanline has filter type 0.  Files are large but the
 * writer is about a hundred lines and has no dependency; the replay tool and
 * the tests only need lossless images any viewer opens.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "rd_internal.h"
#include "rd_png.h"

static uint32_t s_crcTable[256];

static void crcInit(void)
{
    if (s_crcTable[1]) {
        return;
    }
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        s_crcTable[n] = c;
    }
}

void rd_png_prepare(void)
{
    crcInit();
}

static uint32_t crcUpdate(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc = s_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* One chunk appended at dst (12 + len bytes); returns the bytes written. */
static size_t chunk(uint8_t *dst, const char *type, const uint8_t *data, uint32_t len)
{
    be32(dst, len);
    memcpy(dst + 4, type, 4);
    uint32_t crc = crcUpdate(0xFFFFFFFFu, dst + 4, 4);
    crc = crcUpdate(crc, data, len) ^ 0xFFFFFFFFu;
    if (len) {
        memcpy(dst + 8, data, len);
    }
    be32(dst + 8 + len, crc);
    return (size_t)len + 12;
}

bool rd_write_png_mem(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch, int withAlpha,
                      uint8_t **out, size_t *outN)
{
    if (out) {
        *out = NULL;
    }
    if (outN) {
        *outN = 0;
    }
    /* PNG caps the image's sides and a chunk's length at 2^31 - 1 */
    if (!out || !outN || !rgba || !w || !h || w > 0x7FFFFFFFu || h > 0x7FFFFFFFu ||
        pitch < (uint64_t)w * 4) {
        return false;
    }
    crcInit();
    const uint32_t bpp = withAlpha ? 4 : 3;
    const size_t rowLen = (size_t)w * bpp + 1;
    const size_t raw = rowLen * h;
    const size_t blocks = (raw + 65534) / 65535;
    const size_t zlen = 2 + blocks * 5 + raw + 4;
    if (zlen > 0x7FFFFFFFu) {
        return false;
    }
    uint8_t *scan = malloc(raw);
    uint8_t *z = malloc(zlen);
    if (!scan || !z) {
        free(scan);
        free(z);
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *d = scan + (size_t)y * rowLen;
        const uint8_t *s = rgba + (size_t)y * pitch;
        *d++ = 0; /* filter: none */
        for (uint32_t x = 0; x < w; x++) {
            memcpy(d, s + (size_t)x * 4, bpp);
            d += bpp;
        }
    }
    /* zlib: CMF 0x78 (deflate, 32K window), FLG 0x01 (check bits) */
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw;) {
        size_t n = raw - at > 65535 ? 65535 : raw - at;
        z[o++] = (uint8_t)(at + n == raw ? 1 : 0); /* BFINAL, BTYPE 00 */
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, scan + at, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + scan[at + i]) % 65521u;
            b = (b + a) % 65521u;
        }
        o += n;
        at += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    free(scan);

    uint8_t ihdr[13];
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;                        /* bit depth */
    ihdr[9] = withAlpha ? 6 : 2;        /* colour type: RGBA or RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0; /* deflate, adaptive filtering, no interlace */
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t *blob = malloc(8 + 25 + 12 + o + 12);
    if (!blob) {
        free(z);
        return false;
    }
    memcpy(blob, sig, 8);
    size_t n = 8;
    n += chunk(blob + n, "IHDR", ihdr, 13);
    n += chunk(blob + n, "IDAT", z, (uint32_t)o);
    n += chunk(blob + n, "IEND", NULL, 0);
    free(z);
    *out = blob;
    *outN = n;
    return true;
}

bool rd_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                  int withAlpha)
{
    if (!path) {
        return false;
    }
    uint8_t *blob;
    size_t n;
    if (!rd_write_png_mem(rgba, w, h, pitch, withAlpha, &blob, &n)) {
        return false;
    }
    FILE *fp = ico_fopen(path, "wb"); /* a UTF-8 path on Windows too (texture dumps) */
    bool ok = fp != NULL && fwrite(blob, 1, n, fp) == n;
    if (fp) {
        ok = fclose(fp) == 0 && ok;
    }
    free(blob);
    return ok;
}
