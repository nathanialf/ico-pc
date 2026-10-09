/* texpack_load_test.c: the texture-pack file loaders (texpack_dds.c,
 * texpack_png.c), on files built here in memory (no pack data):
 *
 *   dds   the five uncompressed layouts with PCSX2's conversions and alpha
 *         quirks (A8R8G8B8 swapped with its alpha, X8R8G8B8 and R8G8B8
 *         alpha 0xFF, X8B8G8R8 alpha 0x80, A8B8G8R8 as stored), a header
 *         row pitch with padding, BC1/BC2/BC3 by fourcc (DXT1..DXT5) and
 *         BC1/BC2/BC3/BC7 by DX10 format with a full chain down to 1x1,
 *         a mip count of 0 (the full chain), a file without the mip flag
 *         that holds a second level anyway (PCSX2 reads it), a mip count
 *         past the end of the file (levels while the file holds them),
 *         and refusals: BC 6x8 (not a multiple of 4), BC without device
 *         support, cut short, a DX10 array, a volume, an unknown fourcc
 *         and layout, a damaged header, width 32768;
 *   png   colour types 0, 2, 3, 4, 6 at every legal depth, the five
 *         filters (one per row in turn), Adam7 at odd sizes and at sizes
 *         that leave passes empty, tRNS (palette alpha, grey and RGB keys),
 *         16-bit samples to their high byte, alpha 0x80 without alpha,
 *         and refusals: cut short, a bad checksum, not a PNG.
 *
 * With "--pack <dir>": a real pack's files (walked recursively; a few BC7,
 * BC3 and other DDS files picked from their headers, and a few PNGs) load,
 * with sane sizes and formats, and every level reads.  77 when the folder
 * does not exist.  Exit 0, or 1 on a failure.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "miniz.h"
#include "rd_internal.h"
#include "texpack.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* ------------------------------------------------------------------ DDS */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 256;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void put32(Buf *b, uint32_t v)
{
    const uint8_t x[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, x, 4);
}

static void putBE32(Buf *b, uint32_t v)
{
    const uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    put(b, x, 4);
}

#define FCC(a, b, c, d)                                                                            \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) |     \
     ((uint32_t)(uint8_t)(d) << 24))

typedef struct DdsSpec {
    uint32_t w, h, flags, pitch, mipCount;
    uint32_t pfFlags, fourcc, bits, r, g, b, a;
    int dx10;
    uint32_t dxgi, dim, arraySize;
} DdsSpec;

/* magic, header (and DX10 header) */
static void ddsHeader(Buf *b, const DdsSpec *s)
{
    put32(b, 0x20534444u);
    put32(b, 124);
    put32(b, s->flags);
    put32(b, s->h);
    put32(b, s->w);
    put32(b, s->pitch);
    put32(b, 0);
    put32(b, s->mipCount);
    for (int i = 0; i < 11; i++) {
        put32(b, 0);
    }
    put32(b, 32);
    put32(b, s->pfFlags);
    put32(b, s->fourcc);
    put32(b, s->bits);
    put32(b, s->r);
    put32(b, s->g);
    put32(b, s->b);
    put32(b, s->a);
    for (int i = 0; i < 5; i++) {
        put32(b, 0);
    }
    if (s->dx10) {
        put32(b, s->dxgi);
        put32(b, s->dim);
        put32(b, 0);
        put32(b, s->arraySize);
        put32(b, 0);
    }
}

static uint8_t texelByte(uint32_t level, uint32_t x, uint32_t y, uint32_t c)
{
    return (uint8_t)hash(level * 7919u + y * 131u + x * 4u + c);
}

enum { L_A8R8G8B8, L_X8R8G8B8, L_X8B8G8R8, L_R8G8B8, L_A8B8G8R8, L_COUNT };

static void layoutSpec(int layout, DdsSpec *s)
{
    s->pfFlags = layout == L_A8R8G8B8 || layout == L_A8B8G8R8 ? 0x41u : 0x40u;
    s->bits = layout == L_R8G8B8 ? 24u : 32u;
    const int bgr = layout == L_A8R8G8B8 || layout == L_X8R8G8B8 || layout == L_R8G8B8;
    s->r = bgr ? 0x00ff0000u : 0x000000ffu;
    s->g = 0x0000ff00u;
    s->b = bgr ? 0x000000ffu : 0x00ff0000u;
    s->a = layout == L_A8R8G8B8 || layout == L_A8B8G8R8 ? 0xff000000u : 0u;
}

/* the RGBA8 texel PCSX2 makes of the stored bytes s */
static void layoutRef(int layout, const uint8_t *s, uint8_t *o)
{
    switch (layout) {
    case L_A8R8G8B8:
        o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = s[3];
        break;
    case L_X8R8G8B8:
        o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = 0xFF;
        break;
    case L_X8B8G8R8:
        o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = 0x80;
        break;
    case L_R8G8B8:
        o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = 0xFF;
        break;
    default:
        memcpy(o, s, 4);
        break;
    }
}

static void ddsUncompressed(int layout, int padded)
{
    const uint32_t W = 5, H = 3, bpp = layout == L_R8G8B8 ? 3u : 4u;
    DdsSpec s;
    memset(&s, 0, sizeof(s));
    s.w = W;
    s.h = H;
    s.flags = 0x1007u | 0x20000u | (padded ? 0x8u | 0x80000u : 0u);
    s.mipCount = 3; /* 5x3, 2x1, 1x1 */
    s.pitch = padded ? W * bpp + 7 : 0;
    layoutSpec(layout, &s);
    Buf b = {0};
    ddsHeader(&b, &s);
    for (uint32_t l = 0; l < 3; l++) {
        const uint32_t w = W >> l ? W >> l : 1, h = H >> l ? H >> l : 1;
        const uint32_t pitch = l == 0 && padded ? s.pitch : w * bpp;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < pitch; x++) {
                const uint8_t v = x < w * bpp ? texelByte(l, x / bpp, y, x % bpp) : 0xEE;
                put(&b, &v, 1);
            }
        }
    }
    TexpackImage img;
    const int rc = texpack_load_dds(b.p, b.n, 1, NULL, &img);
    CHECK(rc == 0 && img.fmt == RD_TEXEL_RGBA8 && img.w == W && img.h == H && img.levels == 3,
          "layout %d%s: rc %d fmt %u %ux%u levels %u", layout, padded ? " padded" : "", rc, img.fmt,
          img.w, img.h, img.levels);
    for (uint32_t l = 0; rc == 0 && l < img.levels; l++) {
        const TexpackImageLevel *lv = &img.lv[l];
        const uint32_t w = W >> l ? W >> l : 1, h = H >> l ? H >> l : 1;
        CHECK(lv->w == w && lv->h == h && lv->pitch == w * 4 && lv->size == (size_t)w * h * 4,
              "layout %d level %u shape %ux%u pitch %u", layout, l, lv->w, lv->h, lv->pitch);
        int bad = 0;
        for (uint32_t y = 0; y < h && lv->w == w; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t src[4] = {texelByte(l, x, y, 0), texelByte(l, x, y, 1),
                                  texelByte(l, x, y, 2), bpp == 4 ? texelByte(l, x, y, 3) : 0};
                uint8_t want[4];
                layoutRef(layout, src, want);
                bad += memcmp(lv->data + (size_t)y * lv->pitch + x * 4, want, 4) != 0;
            }
        }
        CHECK(bad == 0, "layout %d level %u: %d texels differ", layout, l, bad);
    }
    texpack_free_image(&img);
    CHECK(img.blob == NULL && img.levels == 0, "texpack_free_image empties the image");
    free(b.p);
}

/* A BC file: w x h, the given levels (0: write the full chain), each level
   filled with distinct bytes; mipFlag sets DDSD_MIPMAPCOUNT with
   mipCount. */
static Buf ddsBc(uint32_t w, uint32_t h, uint32_t fourcc, int dx10, uint32_t dxgi, int mipFlag,
                 uint32_t mipCount, uint32_t levels, uint32_t blockBytes)
{
    DdsSpec s;
    memset(&s, 0, sizeof(s));
    s.w = w;
    s.h = h;
    s.flags = 0x1007u | (mipFlag ? 0x20000u : 0u);
    s.mipCount = mipCount;
    s.pfFlags = 0x4u;
    s.fourcc = dx10 ? FCC('D', 'X', '1', '0') : fourcc;
    s.dx10 = dx10;
    s.dxgi = dxgi;
    s.dim = 3;
    s.arraySize = 1;
    Buf b = {0};
    ddsHeader(&b, &s);
    for (uint32_t l = 0; l < levels; l++) {
        const uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
        const uint32_t n = ((lw + 3) / 4) * ((lh + 3) / 4) * blockBytes;
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t v = texelByte(l + 100, i, 0, 0);
            put(&b, &v, 1);
        }
    }
    return b;
}

static void checkBcLevels(const char *what, const TexpackImage *img, uint32_t w, uint32_t h,
                          uint32_t levels, uint32_t blockBytes)
{
    CHECK(img->w == w && img->h == h && img->levels == levels, "%s: %ux%u with %u levels", what,
          img->w, img->h, img->levels);
    for (uint32_t l = 0; l < img->levels && l < levels; l++) {
        const TexpackImageLevel *lv = &img->lv[l];
        const uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
        const uint32_t bw = (lw + 3) / 4, bh = (lh + 3) / 4;
        CHECK(lv->w == lw && lv->h == lh && lv->pitch == bw * blockBytes &&
                  lv->size == (size_t)bw * bh * blockBytes,
              "%s level %u: %ux%u pitch %u size %zu", what, l, lv->w, lv->h, lv->pitch, lv->size);
        int bad = 0;
        for (uint32_t i = 0; i < lv->size; i++) {
            bad += lv->data[i] != texelByte(l + 100, i, 0, 0);
        }
        CHECK(bad == 0, "%s level %u: %d bytes differ", what, l, bad);
    }
}

static void ddsChecks(void)
{
    for (int layout = 0; layout < L_COUNT; layout++) {
        ddsUncompressed(layout, 0);
    }
    ddsUncompressed(L_A8B8G8R8, 1);
    ddsUncompressed(L_R8G8B8, 1);

    /* BC by fourcc and by DX10 format: 8x8, full chain 8, 4, 2, 1 */
    static const struct {
        const char *name;
        uint32_t fourcc;
        int dx10;
        uint32_t dxgi;
        uint8_t fmt;
        uint32_t bb;
    } bc[] = {
        {"DXT1", FCC('D', 'X', 'T', '1'), 0, 0, RD_TEXEL_BC1, 8},
        {"DXT2", FCC('D', 'X', 'T', '2'), 0, 0, RD_TEXEL_BC2, 16},
        {"DXT3", FCC('D', 'X', 'T', '3'), 0, 0, RD_TEXEL_BC2, 16},
        {"DXT4", FCC('D', 'X', 'T', '4'), 0, 0, RD_TEXEL_BC3, 16},
        {"DXT5", FCC('D', 'X', 'T', '5'), 0, 0, RD_TEXEL_BC3, 16},
        {"DX10 71", 0, 1, 71, RD_TEXEL_BC1, 8},
        {"DX10 74", 0, 1, 74, RD_TEXEL_BC2, 16},
        {"DX10 77", 0, 1, 77, RD_TEXEL_BC3, 16},
        {"DX10 98", 0, 1, 98, RD_TEXEL_BC7, 16},
    };

    for (size_t i = 0; i < sizeof(bc) / sizeof(bc[0]); i++) {
        Buf b = ddsBc(8, 8, bc[i].fourcc, bc[i].dx10, bc[i].dxgi, 1, 4, 4, bc[i].bb);
        TexpackImage img;
        int rc = texpack_load_dds(b.p, b.n, 1, NULL, &img);
        CHECK(rc == 0 && img.fmt == bc[i].fmt, "%s: rc %d fmt %u", bc[i].name, rc, img.fmt);
        if (rc == 0) {
            checkBcLevels(bc[i].name, &img, 8, 8, 4, bc[i].bb);
        }
        texpack_free_image(&img);
        /* the device has no BC: refused */
        rc = texpack_load_dds(b.p, b.n, 0, NULL, &img);
        CHECK(rc == -1 && img.blob == NULL, "%s without BC support refused", bc[i].name);
        free(b.p);
    }

    TexpackImage img;
    /* mip count 0 with the flag: the full chain (16x4: 16x4 8x2 4x1 2x1 1x1) */
    Buf b = ddsBc(16, 4, FCC('D', 'X', 'T', '5'), 0, 0, 1, 0, 5, 16);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == 0, "mip count 0 loads");
    checkBcLevels("mip count 0", &img, 16, 4, 5, 16);
    texpack_free_image(&img);
    free(b.p);
    /* no mip flag, two levels in the file: PCSX2 reads the second */
    b = ddsBc(8, 8, FCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 2, 8);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == 0, "no mip flag loads");
    checkBcLevels("no mip flag, 2 levels stored", &img, 8, 8, 2, 8);
    texpack_free_image(&img);
    free(b.p);
    /* no mip flag, one level: one */
    b = ddsBc(8, 8, FCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 1, 8);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == 0, "single level loads");
    checkBcLevels("single level", &img, 8, 8, 1, 8);
    texpack_free_image(&img);
    /* larger than the graphics card takes: refused at the header */
    texpack_set_max_side(4);
    CHECK(texpack_load_dds(b.p, b.n, 1, "big.dds", &img) == -1 && img.blob == NULL,
          "a DDS wider than the card's largest texture refused");
    texpack_set_max_side(8);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == 0, "a DDS at the card's largest loads");
    texpack_free_image(&img);
    texpack_set_max_side(0);
    free(b.p);
    /* mip count 9, three stored, the third cut short: two */
    b = ddsBc(16, 16, FCC('D', 'X', 'T', '5'), 0, 0, 1, 9, 3, 16);
    b.n -= 5;
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == 0, "levels until the file ends");
    checkBcLevels("mip count past the file", &img, 16, 16, 2, 16);
    texpack_free_image(&img);
    free(b.p);
    /* refusals */
    b = ddsBc(6, 8, FCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 1, 8);
    CHECK(texpack_load_dds(b.p, b.n, 1, "six.dds", &img) == -1 && img.blob == NULL,
          "BC 6x8 refused (with a log line)");
    free(b.p);
    b = ddsBc(8, 8, FCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 1, 8);
    CHECK(texpack_load_dds(b.p, b.n - 1, 1, NULL, &img) == -1, "BC level 0 cut short refused");
    CHECK(texpack_load_dds(b.p, 100, 1, NULL, &img) == -1, "a cut header refused");
    CHECK(texpack_load_dds(b.p, 128, 1, NULL, &img) == -1, "a header without image refused");
    b.p[4] = 100; /* dwSize */
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == -1, "dwSize < 124 refused");
    b.p[4] = 124;
    b.p[0] = 'X';
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == -1, "bad magic refused");
    free(b.p);
    b = ddsBc(8, 8, FCC('A', 'T', 'I', '2'), 0, 0, 0, 0, 1, 16);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == -1, "an unknown fourcc refused");
    free(b.p);
    b = ddsBc(8, 8, 0, 1, 99, 0, 0, 1, 16);
    CHECK(texpack_load_dds(b.p, b.n, 1, NULL, &img) == -1, "an unknown DXGI format refused");
    free(b.p);
    {
        DdsSpec s;
        memset(&s, 0, sizeof(s));
        s.w = s.h = 8;
        s.flags = 0x1007u;
        s.pfFlags = 0x4u;
        s.fourcc = FCC('D', 'X', '1', '0');
        s.dx10 = 1;
        s.dxgi = 98;
        s.dim = 3;
        s.arraySize = 2;
        Buf a = {0};
        ddsHeader(&a, &s);
        uint8_t blocks[64];
        memset(blocks, 1, sizeof(blocks));
        put(&a, blocks, sizeof(blocks));
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == -1, "a DX10 array refused");
        a.p[128 + 12] = 1; /* the DX10 header (at 4 + 124): arraySize 1 */
        a.p[128 + 4] = 4;  /* 3D */
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == -1, "a DX10 3D texture refused");
        a.p[128 + 4] = 3;
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == 0, "the same file as 2D loads");
        texpack_free_image(&img);
        a.p[4 + 4 + 2] |= 0x80; /* DDSD_DEPTH (0x800000): a volume */
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == -1, "a volume refused");
        a.p[4 + 4 + 2] &= 0x7F;
        a.p[4 + 12] = 0; /* width 32768 */
        a.p[4 + 13] = 0x80;
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == -1, "width 32768 refused");
        free(a.p);
    }
    {
        /* A8R8G8B8 masks with a bit count of 16: no layout */
        DdsSpec s;
        memset(&s, 0, sizeof(s));
        s.w = s.h = 2;
        s.flags = 0x1007u;
        layoutSpec(L_A8R8G8B8, &s);
        s.bits = 16;
        Buf a = {0};
        ddsHeader(&a, &s);
        uint8_t px[16] = {0};
        put(&a, px, sizeof(px));
        CHECK(texpack_load_dds(a.p, a.n, 1, NULL, &img) == -1, "an unknown layout refused");
        free(a.p);
    }
}

/* ------------------------------------------------------------------ PNG */

typedef struct PngSpec {
    uint32_t w, h;
    uint8_t colour, depth, interlace;
    uint32_t palCount;
    int trnsCount; /* palette alpha entries; -1 none */
    int key;       /* grey / RGB tRNS key */
    uint16_t keyv[3];
} PngSpec;

static uint32_t channelsOf(uint8_t colour)
{
    return colour == 0 || colour == 3 ? 1u : colour == 2 ? 3u : colour == 4 ? 2u : 4u;
}

/* the sample (c) of pixel (x, y) at the spec's depth; keys and palette
   indices kept in range */
static uint16_t sampleAt(const PngSpec *s, uint32_t x, uint32_t y, uint32_t c)
{
    const uint32_t max = s->depth == 16 ? 0xFFFFu : (1u << s->depth) - 1u;
    uint32_t v = hash(y * 977u + x * 7u + c * 131u + s->colour * 17u + s->depth) & max;
    if (s->colour == 3) {
        v %= s->palCount;
    }
    /* some texels on the key, so tRNS has something to clear */
    if (s->key && ((x + y) % 3) == 0 && c < 3) {
        v = s->keyv[c];
    }
    return (uint16_t)v;
}

static uint8_t pal(uint32_t i, uint32_t c)
{
    return (uint8_t)hash(i * 3u + c + 5000u);
}

static uint8_t palAlpha(uint32_t i)
{
    return (uint8_t)hash(i + 9000u);
}

static uint8_t scale8(const PngSpec *s, uint32_t v)
{
    switch (s->depth) {
    case 16:
        return (uint8_t)(v >> 8);
    case 8:
        return (uint8_t)v;
    case 4:
        return (uint8_t)(v * 17);
    case 2:
        return (uint8_t)(v * 85);
    default:
        return v ? 255 : 0;
    }
}

/* the texel the loader must give */
static void pngRef(const PngSpec *s, uint32_t x, uint32_t y, uint8_t *o)
{
    uint32_t v[4];
    for (uint32_t c = 0; c < channelsOf(s->colour); c++) {
        v[c] = sampleAt(s, x, y, c);
    }
    switch (s->colour) {
    case 0:
        o[0] = o[1] = o[2] = scale8(s, v[0]);
        o[3] = s->key && v[0] == s->keyv[0] ? 0 : 0x80;
        break;
    case 2:
        o[0] = scale8(s, v[0]);
        o[1] = scale8(s, v[1]);
        o[2] = scale8(s, v[2]);
        o[3] = s->key && v[0] == s->keyv[0] && v[1] == s->keyv[1] && v[2] == s->keyv[2] ? 0 : 0x80;
        break;
    case 3:
        o[0] = pal(v[0], 0);
        o[1] = pal(v[0], 1);
        o[2] = pal(v[0], 2);
        o[3] = (int)v[0] < s->trnsCount ? palAlpha(v[0]) : 0x80;
        break;
    case 4:
        o[0] = o[1] = o[2] = scale8(s, v[0]);
        o[3] = scale8(s, v[1]);
        break;
    default:
        o[0] = scale8(s, v[0]);
        o[1] = scale8(s, v[1]);
        o[2] = scale8(s, v[2]);
        o[3] = scale8(s, v[3]);
        break;
    }
}

static uint8_t paethRef(int a, int b, int c)
{
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

static void chunk(Buf *b, const char *type, const uint8_t *d, uint32_t n)
{
    putBE32(b, n);
    const size_t at = b->n;
    put(b, type, 4);
    if (n) {
        put(b, d, n);
    }
    putBE32(b, (uint32_t)mz_crc32(MZ_CRC32_INIT, b->p + at, (size_t)n + 4));
}

static const uint8_t s_ax0[7] = {0, 4, 0, 2, 0, 1, 0}, s_ay0[7] = {0, 0, 4, 0, 2, 0, 1};
static const uint8_t s_adx[7] = {8, 8, 4, 4, 2, 2, 1}, s_ady[7] = {8, 8, 8, 4, 4, 2, 2};

static Buf makePng(const PngSpec *s)
{
    const uint32_t ch = channelsOf(s->colour);
    const uint32_t bpp = (ch * s->depth + 7) / 8 ? (ch * s->depth + 7) / 8 : 1;
    Buf raw = {0}, png = {0};
    int filter = 0;
    for (int p = 0; p < (s->interlace ? 7 : 1); p++) {
        const uint32_t x0 = s->interlace ? s_ax0[p] : 0, y0 = s->interlace ? s_ay0[p] : 0;
        const uint32_t dx = s->interlace ? s_adx[p] : 1, dy = s->interlace ? s_ady[p] : 1;
        const uint32_t pw = s->w > x0 ? (s->w - x0 + dx - 1) / dx : 0;
        const uint32_t ph = s->h > y0 ? (s->h - y0 + dy - 1) / dy : 0;
        if (!pw || !ph) {
            continue;
        }
        const size_t n = ((size_t)pw * ch * s->depth + 7) / 8;
        uint8_t *prev = calloc(n, 1), *cur = calloc(n, 1), *f = malloc(n + 1);
        for (uint32_t y = 0; y < ph; y++) {
            memset(cur, 0, n);
            for (uint32_t x = 0; x < pw; x++) {
                for (uint32_t c = 0; c < ch; c++) {
                    const uint32_t v = sampleAt(s, x0 + x * dx, y0 + y * dy, c);
                    const size_t k = (size_t)x * ch + c;
                    if (s->depth == 16) {
                        cur[k * 2] = (uint8_t)(v >> 8);
                        cur[k * 2 + 1] = (uint8_t)v;
                    } else if (s->depth == 8) {
                        cur[k] = (uint8_t)v;
                    } else {
                        const size_t bit = k * s->depth;
                        cur[bit >> 3] |= (uint8_t)(v << (8 - s->depth - (bit & 7)));
                    }
                }
            }
            f[0] = (uint8_t)(filter++ % 5);
            for (size_t i = 0; i < n; i++) {
                const int a = i >= bpp ? cur[i - bpp] : 0, b = y ? prev[i] : 0;
                const int c = i >= bpp && y ? prev[i - bpp] : 0;
                int pred = 0;
                switch (f[0]) {
                case 1:
                    pred = a;
                    break;
                case 2:
                    pred = b;
                    break;
                case 3:
                    pred = (a + b) >> 1;
                    break;
                case 4:
                    pred = paethRef(a, b, c);
                    break;
                default:
                    break;
                }
                f[i + 1] = (uint8_t)(cur[i] - pred);
            }
            put(&raw, f, n + 1);
            memcpy(prev, cur, n);
        }
        free(prev);
        free(cur);
        free(f);
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put(&png, sig, 8);
    uint8_t ihdr[13] = {(uint8_t)(s->w >> 24),
                        (uint8_t)(s->w >> 16),
                        (uint8_t)(s->w >> 8),
                        (uint8_t)s->w,
                        (uint8_t)(s->h >> 24),
                        (uint8_t)(s->h >> 16),
                        (uint8_t)(s->h >> 8),
                        (uint8_t)s->h,
                        s->depth,
                        s->colour,
                        0,
                        0,
                        s->interlace};
    chunk(&png, "IHDR", ihdr, 13);
    if (s->colour == 3) {
        uint8_t p[768];
        for (uint32_t i = 0; i < s->palCount; i++) {
            for (uint32_t c = 0; c < 3; c++) {
                p[i * 3 + c] = pal(i, c);
            }
        }
        chunk(&png, "PLTE", p, s->palCount * 3);
        if (s->trnsCount >= 0) {
            uint8_t t[256];
            for (int i = 0; i < s->trnsCount; i++) {
                t[i] = palAlpha((uint32_t)i);
            }
            chunk(&png, "tRNS", t, (uint32_t)s->trnsCount);
        }
    } else if (s->key) {
        uint8_t t[6];
        for (int c = 0; c < 3; c++) {
            t[c * 2] = (uint8_t)(s->keyv[c] >> 8);
            t[c * 2 + 1] = (uint8_t)s->keyv[c];
        }
        chunk(&png, "tRNS", t, s->colour == 0 ? 2u : 6u);
    }
    /* the zlib stream split over two IDATs */
    mz_ulong zn = mz_compressBound((mz_ulong)raw.n);
    uint8_t *z = malloc(zn);
    mz_compress(z, &zn, raw.p, (mz_ulong)raw.n);
    chunk(&png, "IDAT", z, (uint32_t)(zn / 2));
    chunk(&png, "IDAT", z + zn / 2, (uint32_t)(zn - zn / 2));
    chunk(&png, "IEND", NULL, 0);
    free(z);
    free(raw.p);
    return png;
}

static void pngCase(const PngSpec *s)
{
    Buf b = makePng(s);
    TexpackImage img;
    const int rc = texpack_load_png(b.p, b.n, NULL, &img);
    CHECK(rc == 0 && img.fmt == RD_TEXEL_RGBA8 && img.w == s->w && img.h == s->h &&
              img.levels == 1 && img.lv[0].pitch == s->w * 4,
          "PNG colour %u depth %u%s %ux%u: rc %d", s->colour, s->depth,
          s->interlace ? " Adam7" : "", s->w, s->h, rc);
    int bad = 0;
    for (uint32_t y = 0; rc == 0 && y < s->h; y++) {
        for (uint32_t x = 0; x < s->w; x++) {
            uint8_t want[4];
            pngRef(s, x, y, want);
            const uint8_t *g = img.lv[0].data + ((size_t)y * s->w + x) * 4;
            if (memcmp(g, want, 4) != 0 && bad++ < 3) {
                printf("  colour %u depth %u (%u,%u): %u %u %u %u, expected %u %u %u %u\n",
                       s->colour, s->depth, x, y, g[0], g[1], g[2], g[3], want[0], want[1], want[2],
                       want[3]);
            }
        }
    }
    CHECK(bad == 0, "PNG colour %u depth %u%s: %d texels differ", s->colour, s->depth,
          s->interlace ? " Adam7" : "", bad);
    texpack_free_image(&img);
    free(b.p);
}

static void pngChecks(void)
{
    static const struct {
        uint8_t colour, depth;
    } kinds[] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1},
                 {3, 2}, {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};

    for (int il = 0; il < 2; il++) {
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            PngSpec s;
            memset(&s, 0, sizeof(s));
            s.w = il ? 13 : 11;
            s.h = il ? 11 : 9;
            s.colour = kinds[i].colour;
            s.depth = kinds[i].depth;
            s.interlace = (uint8_t)il;
            s.palCount = s.depth >= 8 ? 200 : (1u << s.depth);
            s.trnsCount = s.colour == 3 ? (int)(s.palCount / 2) : -1;
            pngCase(&s);
        }
    }
    /* Adam7 sizes that leave passes empty */
    for (uint32_t w = 1; w <= 3; w++) {
        for (uint32_t h = 1; h <= 3; h++) {
            PngSpec s;
            memset(&s, 0, sizeof(s));
            s.w = w;
            s.h = h;
            s.colour = 6;
            s.depth = 8;
            s.interlace = 1;
            s.trnsCount = -1;
            pngCase(&s);
        }
    }
    /* tRNS keys: grey 4-bit, grey 16-bit, RGB 8 and 16; a palette without
       tRNS (0x80 everywhere) */
    {
        PngSpec s;
        memset(&s, 0, sizeof(s));
        s.w = 9;
        s.h = 7;
        s.trnsCount = -1;
        s.key = 1;
        s.colour = 0;
        s.depth = 4;
        s.keyv[0] = 5;
        pngCase(&s);
        s.depth = 16;
        s.keyv[0] = 0x1234;
        pngCase(&s);
        s.colour = 2;
        s.depth = 8;
        s.keyv[0] = 10, s.keyv[1] = 20, s.keyv[2] = 30;
        pngCase(&s);
        s.depth = 16;
        s.keyv[0] = 0x0102, s.keyv[1] = 0x0304, s.keyv[2] = 0x0506;
        s.interlace = 1;
        pngCase(&s);
        s.key = 0;
        s.colour = 3;
        s.depth = 8;
        s.palCount = 256;
        s.trnsCount = -1;
        s.interlace = 0;
        pngCase(&s);
    }
    /* refusals: cut short (inside the image data), a bad checksum, not a
       PNG, no palette */
    {
        PngSpec s;
        memset(&s, 0, sizeof(s));
        s.w = 32;
        s.h = 32;
        s.colour = 6;
        s.depth = 8;
        s.trnsCount = -1;
        Buf b = makePng(&s);
        TexpackImage img;
        CHECK(texpack_load_png(b.p, b.n / 2, "half.png", &img) == -1 && img.blob == NULL,
              "a PNG cut in half refused");
        CHECK(texpack_load_png(b.p, b.n - 12, NULL, &img) == 0, "a PNG without IEND loads");
        texpack_free_image(&img);
        /* larger than the graphics card takes: refused at the header */
        texpack_set_max_side(16);
        CHECK(texpack_load_png(b.p, b.n, "big.png", &img) == -1 && img.blob == NULL,
              "a PNG wider than the card's largest texture refused");
        texpack_set_max_side(32);
        CHECK(texpack_load_png(b.p, b.n, NULL, &img) == 0, "a PNG at the card's largest loads");
        texpack_free_image(&img);
        texpack_set_max_side(0);
        b.p[8 + 8 + 3] ^= 1; /* IHDR's width: its CRC fails */
        CHECK(texpack_load_png(b.p, b.n, NULL, &img) == -1, "a bad IHDR checksum refused");
        b.p[8 + 8 + 3] ^= 1;
        b.p[1] = 'X';
        CHECK(texpack_load_png(b.p, b.n, NULL, &img) == -1, "not a PNG refused");
        CHECK(texpack_load_png(b.p, 4, NULL, &img) == -1, "4 bytes refused");
        free(b.p);
    }
}

/* ------------------------------------------------------- a real pack */

typedef struct Pick {
    char path[1024];
    int kind; /* 0 BC7, 1 BC3, 2 other DDS, 3 PNG */
} Pick;

enum { PER_KIND = 3 };

static Pick s_picks[4 * PER_KIND];
static int s_pickCount[4];
static int s_seen[4];

static int endsWith(const char *s, const char *ext)
{
    const size_t n = strlen(s), e = strlen(ext);
    if (n < e) {
        return 0;
    }
    for (size_t i = 0; i < e; i++) {
        char c = s[n - e + i];
        c = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        if (c != ext[i]) {
            return 0;
        }
    }
    return 1;
}

static int ddsKind(const char *path)
{
    uint8_t h[148];
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    const size_t n = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (n < 128) {
        return 2;
    }
    const uint32_t fourcc = (uint32_t)h[84] | ((uint32_t)h[85] << 8) | ((uint32_t)h[86] << 16) |
                            ((uint32_t)h[87] << 24);
    if (fourcc == FCC('D', 'X', '1', '0') && n >= 132 && h[128] == 98) {
        return 0;
    }
    if (fourcc == FCC('D', 'X', 'T', '5') || (fourcc == FCC('D', 'X', '1', '0') && h[128] == 77)) {
        return 1;
    }
    return 2;
}

static void walk(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d || depth > 6) {
        if (d) {
            closedir(d);
        }
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk(path, depth + 1);
            continue;
        }
        int kind = -1;
        if (endsWith(path, ".png")) {
            kind = 3;
        } else if (endsWith(path, ".dds")) {
            kind = ddsKind(path);
        }
        if (kind < 0) {
            continue;
        }
        s_seen[kind]++;
        /* spread the picks: every 7th file of its kind, up to PER_KIND */
        if (s_pickCount[kind] < PER_KIND && (s_seen[kind] % 7) == 1) {
            Pick *p = &s_picks[kind * PER_KIND + s_pickCount[kind]++];
            snprintf(p->path, sizeof(p->path), "%s", path);
            p->kind = kind;
        }
    }
    closedir(d);
}

static uint8_t *readFile(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = len > 0 ? malloc((size_t)len) : NULL;
    if (p && fread(p, 1, (size_t)len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return p;
}

static int packChecks(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("texpack_load_test: SKIP: no pack at %s\n", dir);
        return 77;
    }
    walk(dir, 0);
    printf("pack: %d BC7, %d BC3, %d other DDS, %d PNG files\n", s_seen[0], s_seen[1], s_seen[2],
           s_seen[3]);
    int loaded = 0;
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < s_pickCount[k]; i++) {
            const Pick *p = &s_picks[k * PER_KIND + i];
            size_t n = 0;
            uint8_t *data = readFile(p->path, &n);
            CHECK(data != NULL, "%s: read", p->path);
            if (!data) {
                continue;
            }
            TexpackImage img;
            const int rc = k == 3 ? texpack_load_png(data, n, p->path, &img)
                                  : texpack_load_dds(data, n, 1, p->path, &img);
            free(data);
            CHECK(rc == 0, "%s: loads", p->path);
            if (rc != 0) {
                continue;
            }
            const uint8_t wantFmt = k == 0 ? RD_TEXEL_BC7 : k == 1 ? RD_TEXEL_BC3 : img.fmt;
            CHECK(img.fmt == wantFmt && img.w >= 1 && img.h >= 1 && img.w <= 16384 &&
                      img.h <= 16384 && img.levels >= 1 && img.levels <= TEXPACK_IMAGE_LEVELS,
                  "%s: fmt %u %ux%u levels %u", p->path, img.fmt, img.w, img.h, img.levels);
            /* every level: its shape, and every byte read */
            uint32_t sum = 0;
            size_t bytes = 0;
            for (uint32_t l = 0; l < img.levels; l++) {
                const TexpackImageLevel *lv = &img.lv[l];
                const uint32_t w = img.w >> l ? img.w >> l : 1, h = img.h >> l ? img.h >> l : 1;
                const uint32_t bw = rd__texel_block_w(img.fmt);
                const size_t rowB = (size_t)((w + bw - 1) / bw) * rd__texel_block_bytes(img.fmt);
                CHECK(lv->w == w && lv->h == h && lv->pitch == rowB &&
                          lv->size == rowB * ((h + bw - 1) / bw),
                      "%s level %u: %ux%u pitch %u", p->path, l, lv->w, lv->h, lv->pitch);
                for (size_t b = 0; b < lv->size; b++) {
                    sum = sum * 31u + lv->data[b];
                }
                bytes += lv->size;
            }
            CHECK(bytes == img.bytes, "%s: levels cover the blob", p->path);
            printf("  %s: fmt %u %ux%u, %u level(s), %zu bytes (sum %08x)\n", p->path, img.fmt,
                   img.w, img.h, img.levels, img.bytes, sum);
            loaded++;
            texpack_free_image(&img);
        }
    }
    CHECK(loaded > 0, "no pack file loaded");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--pack") == 0) {
        return packChecks(argv[2]);
    }
    ddsChecks();
    pngChecks();
    if (failures) {
        printf("texpack_load_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("texpack_load_test: ok\n");
    return 0;
}
