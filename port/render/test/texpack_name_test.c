/* texpack_name_test: the PCSX2 texture names (texpack_name.h), CPU only.
 *
 *   xxh3      xxh3_64 against xxHash v0.8.2's own sanity vectors
 *             (cli/xsum_sanity_check.c, XSUM_XXH3_testdata: seed 0, the
 *             buffer XSUM_fillTestBuffer makes)
 *   swizzle   texpack_block_offset against PCSX2's block layout rebuilt
 *             here from GSBlock.h's column writes (the SSE path of
 *             WriteColumn32/16/8/4 run on an emulated 128-bit vector; the
 *             column tables of GSTables.cpp describe the same layout),
 *             every entry; bijections, spot values, a 64x32 PSMT8 round
 *             trip
 *   clut      the CLUT hash against GSClut.cpp's path emulated the same
 *             way: the CLUT image written into GS blocks, then
 *             WriteCLUT_T32_I8_CSM1 + ReadCLUT_T32_I8, WriteCLUT_T16_I8_CSM1
 *             + Expand16, and the 16-entry versions; AEM on a 0x0000 entry
 *   expanded  the bytes of levels below a block (CT32 4x4, PSMT8 8x8,
 *             PSMT4 16x16) and of CT16 (three TEXA names) and CT24
 *   names     candidates (single, chain, k > bound level), unstable,
 *             refusals, formatting and parsing (PCSX2's sscanf formats,
 *             the reference pack's two malformed names, -mipN)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_tex.h"
#include "texpack_name.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------ xxh3 */

static void testXxh3(void)
{
    /* XSUM_XXH3_testdata, seed 0, as text ("length:hash") */
    static const char *const kVec[] = {
        "0:2D06800538D394C2",    "1:C44BDFF4074EECDB",    "6:27B56A84CD2D7325",
        "12:A713DAF0DFBB77E7",   "24:A3FE70BF9D3510EB",   "48:397DA259ECBA1F11",
        "80:BCDEFBBB2C47C90A",   "195:CD94217EE362EC3A",  "403:CDEB804D65C6DEA4",
        "512:617E49599013CB6B",  "2048:DD59E2C3A5F038E0", "2099:C6B9D9B3FC9AC765",
        "2240:6E73A90539CF2948", "2367:CB37AEB9E5D361ED"};

    enum { SANITY = 2367 };

    static uint8_t buf[SANITY];
    uint64_t gen = 2654435761u; /* PRIME32 */

    for (int i = 0; i < SANITY; i++) {
        buf[i] = (uint8_t)(gen >> 56);
        gen *= 11400714785074694797ull; /* PRIME64 */
    }
    for (size_t v = 0; v < sizeof(kVec) / sizeof(kVec[0]); v++) {
        char *end;
        size_t len = (size_t)strtoul(kVec[v], &end, 10);
        uint64_t want = strtoull(end + 1, NULL, 16);
        uint64_t got = xxh3_64(len ? buf : NULL, len);
        CHECK(got == want, "xxh3_64 of %zu bytes: %016llx, xxHash says %016llx", len,
              (unsigned long long)got, (unsigned long long)want);
    }
    printf("xxh3: %zu vectors\n", sizeof(kVec) / sizeof(kVec[0]));
}

/* ------------------------------------------------------------ a 128-bit vector */

/* GSVector4i as bytes, little-endian lanes, with the operations GSBlock.h
   and GSClut.cpp's SSE paths use (GSVector4i.h: upl/uph = punpckl/h,
   sw* = the four-register swaps, yxwz = pshufd, yxwzlh = pshuflw+pshufhw,
   mix4 = the nibble blend) */
typedef struct V {
    uint8_t b[16];
} V;

static V vload(const uint8_t *p)
{
    V v;
    memcpy(v.b, p, 16);
    return v;
}

/* interleave units of n bytes from the low (hi = 0) or high half */
static V unpack(V a, V c, int n, int hi)
{
    V r;
    int base = hi ? 8 : 0;
    for (int i = 0; i < 8 / n; i++) {
        memcpy(r.b + 2 * i * n, a.b + base + i * n, (size_t)n);
        memcpy(r.b + (2 * i + 1) * n, c.b + base + i * n, (size_t)n);
    }
    return r;
}

/* GSVector4i::sw8/16/32/64 (a, b, c, d) for n = 1/2/4/8 */
static void sw(int n, V *a, V *b, V *c, V *d)
{
    V e = *a;
    V f = *c;
    *a = unpack(e, *b, n, 0);
    *c = unpack(e, *b, n, 1);
    *b = unpack(f, *d, n, 0);
    *d = unpack(f, *d, n, 1);
}

/* yxwz: swap the 32-bit lanes of each pair */
static V yxwz(V a)
{
    V r;
    for (int i = 0; i < 4; i++) {
        memcpy(r.b + 4 * i, a.b + 4 * (i ^ 1), 4);
    }
    return r;
}

/* yxwzlh: the same on the 16-bit lanes of each half */
static V yxwzlh(V a)
{
    V r;
    for (int i = 0; i < 8; i++) {
        memcpy(r.b + 2 * i, a.b + 2 * (i ^ 1), 2);
    }
    return r;
}

/* mix4(a, b): a = a's low nibbles under b's low nibbles (b << 4 blended
   with the 0x0f mask), b = a's high nibbles under b's high ones */
static void mix4(V *a, V *b)
{
    V c;
    V d;
    for (int i = 0; i < 16; i++) {
        c.b[i] = (uint8_t)((a->b[i] & 0x0F) | (b->b[i] << 4));
        d.b[i] = (uint8_t)((a->b[i] >> 4) | (b->b[i] & 0xF0));
    }
    *a = c;
    *b = d;
}

/* ------------------------------------------------------------ GSBlock.h */

/* WriteBlock32/16/8/4 of one 256-byte block from src (pitch bytes per
   row): the SSE, aligned path of WriteColumn32/16/8/4 for columns 0-3 */
static void writeBlock(uint32_t psm, uint8_t *dst, const uint8_t *src, int pitch)
{
    for (int i = 0; i < 4; i++) {
        V v0;
        V v1;
        V v2;
        V v3;
        V *d = (V *)(void *)dst;
        const uint8_t *s0;
        switch (psm) {
        case RDTEX_PSMCT32:
            s0 = src + 2 * i * pitch;
            v0 = vload(s0), v1 = vload(s0 + 16), v2 = vload(s0 + pitch);
            v3 = vload(s0 + pitch + 16);
            sw(8, &v0, &v2, &v1, &v3);
            d[i * 4 + 0] = v0, d[i * 4 + 1] = v1, d[i * 4 + 2] = v2, d[i * 4 + 3] = v3;
            break;
        case RDTEX_PSMCT16:
            s0 = src + 2 * i * pitch;
            v0 = vload(s0), v1 = vload(s0 + 16), v2 = vload(s0 + pitch);
            v3 = vload(s0 + pitch + 16);
            sw(2, &v0, &v1, &v2, &v3);
            sw(8, &v0, &v1, &v2, &v3);
            d[i * 4 + 0] = v0, d[i * 4 + 1] = v2, d[i * 4 + 2] = v1, d[i * 4 + 3] = v3;
            break;
        case RDTEX_PSMT8:
            s0 = src + 4 * i * pitch;
            v0 = vload(s0), v1 = vload(s0 + pitch), v2 = vload(s0 + 2 * pitch);
            v3 = vload(s0 + 3 * pitch);
            if ((i & 1) == 0) {
                v2 = yxwz(v2), v3 = yxwz(v3);
            } else {
                v0 = yxwz(v0), v1 = yxwz(v1);
            }
            sw(1, &v0, &v2, &v1, &v3);
            sw(2, &v0, &v1, &v2, &v3);
            sw(8, &v0, &v1, &v2, &v3);
            d[i * 4 + 0] = v0, d[i * 4 + 1] = v2, d[i * 4 + 2] = v1, d[i * 4 + 3] = v3;
            break;
        default: /* PSMT4 */
            s0 = src + 4 * i * pitch;
            v0 = vload(s0), v1 = vload(s0 + pitch), v2 = vload(s0 + 2 * pitch);
            v3 = vload(s0 + 3 * pitch);
            if ((i & 1) == 0) {
                v2 = yxwzlh(v2), v3 = yxwzlh(v3);
            } else {
                v0 = yxwzlh(v0), v1 = yxwzlh(v1);
            }
            /* sw4(v0, v2, v1, v3) is mix4 on both pairs and then sw8 */
            mix4(&v0, &v2);
            mix4(&v1, &v3);
            sw(1, &v0, &v2, &v1, &v3);
            sw(1, &v0, &v1, &v2, &v3);
            sw(1, &v0, &v2, &v1, &v3);
            sw(8, &v0, &v2, &v1, &v3);
            d[i * 4 + 0] = v0, d[i * 4 + 1] = v1, d[i * 4 + 2] = v2, d[i * 4 + 3] = v3;
            break;
        }
    }
}

/* PCSX2's column table of psm (where texel (x, y) of a block lands, in
   units of the texel), rebuilt by writing tagged blocks: tab[y * bw + x] */
static void columnTable(uint32_t psm, uint32_t *tab)
{
    uint32_t bw;
    uint32_t bh;
    uint8_t src[256];
    uint8_t dst[256];
    texpack_block_size(psm, &bw, &bh);
    uint32_t n = bw * bh;
    int pitch = (int)(psm == RDTEX_PSMT4     ? bw / 2
                      : psm == RDTEX_PSMT8   ? bw
                      : psm == RDTEX_PSMCT16 ? bw * 2
                                             : bw * 4);

    if (psm == RDTEX_PSMT4) {
        /* 512 texels in nibbles: one pass per bit of the texel number */
        uint32_t pos[512];
        memset(pos, 0, sizeof(pos));
        for (int bit = 0; bit < 9; bit++) {
            memset(src, 0, sizeof(src));
            for (uint32_t t = 0; t < n; t++) {
                if ((t >> bit) & 1) {
                    src[t >> 1] |= (uint8_t)(t & 1 ? 0x10 : 0x01);
                }
            }
            writeBlock(psm, dst, src, pitch);
            for (uint32_t o = 0; o < 512; o++) {
                uint32_t nib = o & 1 ? dst[o >> 1] >> 4 : dst[o >> 1] & 15;
                pos[o] |= (nib & 1) << bit;
            }
        }
        for (uint32_t o = 0; o < 512; o++) {
            tab[pos[o]] = o;
        }
        return;
    }
    memset(src, 0, sizeof(src));
    for (uint32_t t = 0; t < n; t++) {
        switch (psm) {
        case RDTEX_PSMT8:
            src[t] = (uint8_t)t;
            break;
        case RDTEX_PSMCT16:
            src[2 * t] = (uint8_t)t, src[2 * t + 1] = (uint8_t)(t >> 8);
            break;
        default:
            src[4 * t] = (uint8_t)t;
            break;
        }
    }
    writeBlock(psm, dst, src, pitch);
    for (uint32_t o = 0; o < n; o++) {
        uint32_t t = psm == RDTEX_PSMT8     ? dst[o]
                     : psm == RDTEX_PSMCT16 ? (uint32_t)dst[2 * o] | (uint32_t)dst[2 * o + 1] << 8
                                            : dst[4 * o];
        tab[t] = o;
    }
}

static void testSwizzle(void)
{
    static const uint32_t kPsm[4] = {RDTEX_PSMCT32, RDTEX_PSMCT16, RDTEX_PSMT8, RDTEX_PSMT4};
    static const char *const kName[4] = {"PSMCT32", "PSMCT16", "PSMT8", "PSMT4"};
    uint32_t tab[512];
    uint8_t seen[512];

    for (int p = 0; p < 4; p++) {
        uint32_t bw = 0;
        uint32_t bh = 0;
        int bad = 0;
        CHECK(texpack_block_size(kPsm[p], &bw, &bh) == 0 && bw * bh <= 512, "%s has a block",
              kName[p]);
        columnTable(kPsm[p], tab);
        memset(seen, 0, sizeof(seen));
        for (uint32_t y = 0; y < bh; y++) {
            for (uint32_t x = 0; x < bw; x++) {
                uint32_t o = texpack_block_offset(kPsm[p], x, y);
                if (o != tab[y * bw + x] && bad++ < 4) {
                    CHECK(0, "%s (%u,%u): %u, PCSX2's column write puts it at %u", kName[p], x, y,
                          o, tab[y * bw + x]);
                }
                if (o < bw * bh) {
                    seen[o]++;
                }
            }
        }
        CHECK(bad == 0, "%s: %d entries differ from PCSX2's", kName[p], bad);
        int dup = 0;
        for (uint32_t o = 0; o < bw * bh; o++) {
            dup += seen[o] != 1;
        }
        CHECK(dup == 0, "%s: a bijection onto 0..%u (%d offsets not hit once)", kName[p],
              bw * bh - 1, dup);
        CHECK(texpack_block_offset(kPsm[p], bw, 0) == UINT32_MAX &&
                  texpack_block_offset(kPsm[p], 0, bh) == UINT32_MAX,
              "%s: outside the block refused", kName[p]);
    }
    /* spot values (GSTables.cpp's columnTable32/16/8/4) */
    CHECK(texpack_block_offset(RDTEX_PSMCT32, 2, 0) == 4, "CT32 (2,0) -> 4");
    CHECK(texpack_block_offset(RDTEX_PSMCT32, 0, 1) == 2, "CT32 (0,1) -> 2");
    CHECK(texpack_block_offset(RDTEX_PSMCT32, 7, 7) == 63, "CT32 (7,7) -> 63");
    CHECK(texpack_block_offset(RDTEX_PSMCT16, 8, 0) == 1, "CT16 (8,0) -> 1");
    CHECK(texpack_block_offset(RDTEX_PSMCT16, 0, 2) == 32, "CT16 (0,2) -> 32");
    CHECK(texpack_block_offset(RDTEX_PSMT8, 0, 2) == 33, "PSMT8 (0,2) -> 33");
    CHECK(texpack_block_offset(RDTEX_PSMT8, 4, 2) == 1, "PSMT8 (4,2) -> 1");
    CHECK(texpack_block_offset(RDTEX_PSMT8, 0, 4) == 96, "PSMT8 (0,4) -> 96");
    CHECK(texpack_block_offset(RDTEX_PSMT4, 0, 2) == 65, "PSMT4 (0,2) -> 65");
    CHECK(texpack_block_offset(RDTEX_PSMT4, 0, 4) == 192, "PSMT4 (0,4) -> 192");
    CHECK(texpack_block_offset(RDTEX_PSMCT24, 2, 0) == 4, "CT24 is laid out as CT32");
    CHECK(texpack_block_offset(RDTEX_PSMCT16S, 8, 0) == 1, "CT16S is laid out as CT16");
    CHECK(texpack_block_offset(RDTEX_PSMT8H, 0, 0) == UINT32_MAX, "PSMT8H has no block path");
    CHECK(texpack_block_size(RDTEX_PSMZ32, NULL, NULL) == -1, "PSMZ32 refused");
    CHECK(texpack_block_size(7, NULL, NULL) == -1, "an unknown psm refused");

    /* a 64x32 PSMT8 level through the block path and back */
    enum { W = 64, H = 32 };

    uint8_t img[W * H];
    uint8_t out[W * H];
    uint8_t clut[1024];
    TexpackSource s;
    memset(&s, 0, sizeof(s));
    memset(clut, 0, sizeof(clut));
    for (int i = 0; i < W * H; i++) {
        img[i] = (uint8_t)(i * 7 + (i >> 6));
    }
    s.psm = RDTEX_PSMT8, s.levels = 1;
    s.lv[0] = (TexpackLevel){6, 5, W, H, 2, img};
    s.cpsm = RDTEX_PSMCT32, s.clutColors = 256, s.clut = clut;
    size_t n = texpack_hash_bytes(&s, 0, 0, NULL, 0);
    CHECK(n == W * H, "64x32 PSMT8: %zu bytes", n);
    CHECK(texpack_hash_bytes(&s, 0, 0, out, sizeof(out)) == W * H, "64x32 PSMT8 written");
    int wrong = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t blk = (size_t)(y / 16) * (W / 16) + (size_t)(x / 16);
            uint32_t o = texpack_block_offset(RDTEX_PSMT8, (uint32_t)x % 16, (uint32_t)y % 16);
            wrong += out[blk * 256 + o] != img[y * W + x];
        }
    }
    CHECK(wrong == 0, "64x32 PSMT8 round trip: %d texels wrong", wrong);
    printf("swizzle: four column layouts rebuilt from PCSX2's column writes and compared\n");
}

/* ------------------------------------------------------------ GSClut.cpp */

/* a CLUT image of cpsm written into GS memory at CBP 0 the way
   tex_transVramClutTex uploads it: 256 entries as a 16x16 image (CT32
   blocks 0-3, blockTable32's 2x2 corner; CT16 blocks 0 and 1,
   blockTable16's first column), 16 entries as an 8x2 image (rows 0-1 of
   block 0) */
static void clutToGs(uint32_t cpsm, const uint8_t *img, uint32_t colors, uint8_t *gs)
{
    uint32_t eb = cpsm == RDTEX_PSMCT32 ? 4 : 2;
    uint8_t tile[256];

    memset(gs, 0, 1024);
    if (colors == 16) {
        uint32_t bw = cpsm == RDTEX_PSMCT32 ? 8 : 16;
        memset(tile, 0, sizeof(tile));
        for (uint32_t y = 0; y < 2; y++) {
            memcpy(tile + y * bw * eb, img + y * 8 * eb, 8 * eb);
        }
        writeBlock(cpsm, gs, tile, (int)(bw * eb));
        return;
    }
    if (cpsm == RDTEX_PSMCT32) {
        static const int kBlock[2][2] = {{0, 1}, {2, 3}};
        for (int by = 0; by < 2; by++) {
            for (int bx = 0; bx < 2; bx++) {
                writeBlock(cpsm, gs + 256 * kBlock[by][bx], img + by * 8 * 64 + bx * 32, 64);
            }
        }
    } else {
        for (int by = 0; by < 2; by++) {
            writeBlock(cpsm, gs + 256 * by, img + by * 8 * 32, 32);
        }
    }
}

/* GSClut::WriteCLUT_T32_I4_CSM1's SSE path (src: one block) */
static void clut32I4(const uint8_t *src, uint16_t *clut)
{
    V v0 = vload(src);
    V v1 = vload(src + 16);
    V v2 = vload(src + 32);
    V v3 = vload(src + 48);
    sw(2, &v0, &v1, &v2, &v3);
    sw(4, &v0, &v1, &v2, &v3);
    sw(2, &v0, &v2, &v1, &v3);
    memcpy(clut, v0.b, 16), memcpy(clut + 8, v2.b, 16);
    memcpy(clut + 256, v1.b, 16), memcpy(clut + 264, v3.b, 16);
}

/* GSClut::ReadCLUT_T32_I4 */
static void read32I4(const uint16_t *clut, uint8_t *dst)
{
    V v0;
    V v1;
    V v2;
    V v3;
    memcpy(v0.b, clut, 16), memcpy(v1.b, clut + 8, 16);
    memcpy(v2.b, clut + 256, 16), memcpy(v3.b, clut + 264, 16);
    sw(2, &v0, &v2, &v1, &v3);
    memcpy(dst, v0.b, 16), memcpy(dst + 16, v1.b, 16);
    memcpy(dst + 32, v2.b, 16), memcpy(dst + 48, v3.b, 16);
}

/* GSClut's m_buff32 (the bytes PaletteKeyHash hashes) for a CLUT of
   cpsm in GS memory gs, as Read32 leaves it for a palette of n entries
   under TEXA (ta0, aem, ta1), CSA 0 */
static void gsClutRead(uint32_t cpsm, const uint8_t *gs, uint32_t n, int ta0, int aem, int ta1,
                       uint8_t *buff32)
{
    uint16_t clut[512];
    memset(clut, 0, sizeof(clut));
    if (cpsm == RDTEX_PSMCT32) {
        if (n == 256) {
            /* WriteCLUT_T32_I8_CSM1: the source column for off = i << 4 is
               clutTableT32I8[off & 0x70] | (off & 0x80); of that table
               only these eight entries are ever read */
            static const int kCol[8] = {0, 64, 16, 80, 32, 96, 48, 112};
            for (int i = 0; i < 16; i++) {
                int off = i << 4;
                int s = kCol[(off & 0x70) >> 4] | (off & 0x80);
                clut32I4(gs + 4 * s, clut + off);
            }
            for (int i = 0; i < 256; i += 16) {
                read32I4(clut + i, buff32 + 4 * i); /* ReadCLUT_T32_I8 */
            }
        } else {
            clut32I4(gs, clut);
            read32I4(clut, buff32);
        }
        return;
    }
    if (n == 256) {
        /* WriteCLUT_T16_I8_CSM1 */
        for (int i = 0; i < 32; i += 4) {
            V v0 = vload(gs + 16 * i);
            V v1 = vload(gs + 16 * (i + 1));
            V v2 = vload(gs + 16 * (i + 2));
            V v3 = vload(gs + 16 * (i + 3));
            sw(2, &v0, &v1, &v2, &v3);
            sw(4, &v0, &v1, &v2, &v3);
            sw(2, &v0, &v2, &v1, &v3);
            memcpy(clut + 8 * i, v0.b, 16), memcpy(clut + 8 * (i + 1), v2.b, 16);
            memcpy(clut + 8 * (i + 2), v1.b, 16), memcpy(clut + 8 * (i + 3), v3.b, 16);
        }
    } else {
        /* WriteCLUT_T16_I4_CSM1 through clutTableT16I4 */
        static const int kT16I4[16] = {0, 2, 8, 10, 16, 18, 24, 26, 4, 6, 12, 14, 20, 22, 28, 30};
        for (int i = 0; i < 16; i++) {
            clut[i] = (uint16_t)(gs[2 * kT16I4[i]] | gs[2 * kT16I4[i] + 1] << 8);
        }
    }
    /* Expand16 */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = clut[i];
        uint32_t a = (uint32_t)(c & 0x8000 ? ta1 : ta0);
        if (aem && c == 0) {
            a = 0;
        }
        uint32_t v = (c & 0x1F) << 3 | (c & 0x3E0) << 6 | (c & 0x7C00) << 9 | a << 24;
        buff32[4 * i] = (uint8_t)v, buff32[4 * i + 1] = (uint8_t)(v >> 8);
        buff32[4 * i + 2] = (uint8_t)(v >> 16), buff32[4 * i + 3] = (uint8_t)(v >> 24);
    }
}

static void testClut(void)
{
    static const int kTexa[3][3] = {{0x80, 0, 0x80}, {0x7F, 1, 0x81}, {0x80, 1, 0x80}};
    uint8_t img[256 * 4];
    uint8_t gs[1024];
    uint8_t buff[1024];
    uint8_t pix[32 * 16];
    TexpackSource s;
    TexpackName nm;

    memset(pix, 0x5A, sizeof(pix));
    for (int c = 0; c < 4; c++) {
        uint32_t cpsm = c < 2 ? RDTEX_PSMCT32 : RDTEX_PSMCT16;
        uint32_t colors = c & 1 ? 16 : 256;
        uint32_t eb = cpsm == RDTEX_PSMCT32 ? 4 : 2;
        for (uint32_t i = 0; i < colors * eb; i++) {
            img[i] = (uint8_t)(i * 37 + 11 + (i >> 8));
        }
        if (cpsm == RDTEX_PSMCT16) {
            /* an all-zero entry and an alpha-bit-only one, for AEM */
            img[2 * 3] = 0, img[2 * 3 + 1] = 0;
            img[2 * 5] = 0, img[2 * 5 + 1] = 0x80;
        }
        memset(&s, 0, sizeof(s));
        s.psm = colors == 256 ? RDTEX_PSMT8 : RDTEX_PSMT4;
        s.levels = 1;
        s.lv[0] = (TexpackLevel){colors == 256 ? 4 : 5, 4, colors == 256 ? 16 : 32, 16, 2, pix};
        s.cpsm = cpsm, s.clutColors = colors, s.clut = img;
        clutToGs(cpsm, img, colors, gs);
        int modes = cpsm == RDTEX_PSMCT16 ? 3 : 1;
        uint64_t prev = 0;
        for (int t = 0; t < modes; t++) {
            gsClutRead(cpsm, gs, colors, kTexa[t][0], kTexa[t][1], kTexa[t][2], buff);
            CHECK(texpack_compute_name(&s, 0, t, 0, &nm) == 0, "CLUT case %d computes", c);
            CHECK(nm.clutHash == xxh3_64(buff, colors * 4),
                  "%s CLUT of %u entries, TEXA mode %d: the hash of PCSX2's m_buff32",
                  cpsm == RDTEX_PSMCT32 ? "CT32" : "CT16", colors, t);
            CHECK(nm.hasClut && nm.bits == (s.psm | (uint32_t)s.lv[0].tw << 6 | 4u << 10),
                  "CLUT case %d: bits %08x without TEXA", c, nm.bits);
            CHECK(nm.texa == (cpsm == RDTEX_PSMCT16 ? t : TEXPACK_TEXA_ANY),
                  "CLUT case %d: texa %d", c, nm.texa);
            if (t > 0) {
                CHECK(nm.clutHash != prev, "CT16 CLUT: TEXA mode %d changes the hash", t);
            }
            prev = nm.clutHash;
        }
        /* the rule the code uses: entry i is memory entry
           rdtex_csm1_index(i), the middle-run swap for 256 entries (CT16
           as well as CT32), straight for 16 */
        gsClutRead(cpsm, gs, colors, 0x80, 0, 0x80, buff);
        int bad = 0;
        for (uint32_t i = 0; i < colors; i++) {
            uint32_t m = rdtex_csm1_index(i, colors);
            if (cpsm == RDTEX_PSMCT32) {
                bad += memcmp(buff + 4 * i, img + 4 * m, 4) != 0;
            } else {
                uint32_t v = img[2 * m] | (uint32_t)img[2 * m + 1] << 8;
                uint32_t e = (v & 0x1F) << 3 | (v & 0x3E0) << 6 | (v & 0x7C00) << 9;
                uint32_t g =
                    buff[4 * i] | (uint32_t)buff[4 * i + 1] << 8 | (uint32_t)buff[4 * i + 2] << 16;
                bad += g != e;
            }
        }
        CHECK(bad == 0, "case %d: PCSX2's entry i is memory entry rdtex_csm1_index(i) (%d off)", c,
              bad);
        if (colors == 256) {
            CHECK(memcmp(buff + 4 * 8, buff + 4 * 16, 4) != 0, "case %d: not the identity", c);
        }
    }

    /* AEM zeroes a 0x0000 entry's alpha only, never 0x8000's */
    memset(&s, 0, sizeof(s));
    uint8_t c16[32];
    uint8_t want[64];
    memset(want, 0, sizeof(want));
    for (int i = 0; i < 16; i++) {
        c16[2 * i] = 0x00, c16[2 * i + 1] = 0x04; /* blue 1: 0x08 in byte 2 */
        want[4 * i + 2] = 0x08;
        want[4 * i + 3] = 0x7F; /* TA0 of mode 1 */
    }
    c16[0] = 0, c16[1] = 0; /* entry 0: 0x0000 */
    want[2] = 0, want[3] = 0;
    c16[2] = 0, c16[3] = 0x80; /* entry 1: 0x8000 */
    want[4 + 2] = 0, want[4 + 3] = 0x81;
    s.psm = RDTEX_PSMT4, s.levels = 1;
    s.lv[0] = (TexpackLevel){5, 4, 32, 16, 2, pix};
    s.cpsm = RDTEX_PSMCT16, s.clutColors = 16, s.clut = c16;
    CHECK(texpack_compute_name(&s, 0, 1, 0, &nm) == 0 && nm.clutHash == xxh3_64(want, 64),
          "CT16 under 7F/AEM/81: 0x0000 transparent, 0x8000 TA1");
    for (int i = 0; i < 16; i++) {
        want[4 * i + 3] = 0x80;
    }
    CHECK(texpack_compute_name(&s, 0, 0, 0, &nm) == 0 && nm.clutHash == xxh3_64(want, 64),
          "CT16 under 80/80: every entry 0x80, 0x0000 too");
    printf("clut: CT32 and CT16, 16 and 256 entries, against GSClut's path\n");
}

/* ------------------------------------------------------------ expanded path */

static void testExpanded(void)
{
    TexpackSource s;
    TexpackName nm[TEXPACK_MAX_CANDIDATES];
    uint8_t buf[2048];
    uint8_t px[16 * 8 * 4];
    uint8_t clut[256 * 4];

    memset(px, 0, sizeof(px));
    memset(clut, 0, sizeof(clut));

    /* CT32 4x4: below the 8x8 block, the texels as they are */
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 64; i++) {
        px[i] = (uint8_t)(i * 13 + 1);
    }
    s.psm = RDTEX_PSMCT32, s.levels = 1;
    s.lv[0] = (TexpackLevel){2, 2, 4, 4, 1, px};
    CHECK(texpack_hash_bytes(&s, 0, 0, buf, sizeof(buf)) == 64 && memcmp(buf, px, 64) == 0,
          "CT32 4x4: 64 bytes, the texels in rows");
    CHECK(texpack_compute_name(&s, 0, 2, 0, nm) == 0 && nm[0].tex0Hash == xxh3_64(px, 64) &&
              nm[0].bits == (0u | 2u << 6 | 2u << 10) && !nm[0].hasClut &&
              nm[0].texa == TEXPACK_TEXA_ANY,
          "CT32 4x4 name: the hash of the texels, no TEXA");

    /* PSMT8 8x8: one byte per index */
    memset(&s, 0, sizeof(s));
    s.psm = RDTEX_PSMT8, s.levels = 1;
    s.lv[0] = (TexpackLevel){3, 3, 8, 8, 2, px};
    s.cpsm = RDTEX_PSMCT32, s.clutColors = 256, s.clut = clut;
    CHECK(texpack_hash_bytes(&s, 0, 0, buf, sizeof(buf)) == 64 && memcmp(buf, px, 64) == 0,
          "PSMT8 8x8: 64 index bytes");

    /* PSMT4 16x16 (below 32x16): one byte per index, low nibble first */
    s.psm = RDTEX_PSMT4;
    s.lv[0] = (TexpackLevel){4, 4, 16, 16, 2, px};
    s.clutColors = 16;
    CHECK(texpack_hash_bytes(&s, 0, 0, buf, sizeof(buf)) == 256 && buf[0] == (px[0] & 15) &&
              buf[1] == (px[0] >> 4) && buf[63] == (px[31] >> 4),
          "PSMT4 16x16: 256 index bytes");

    /* CT16 16x8: always expanded (fmsk is not all ones), through TEXA */
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 16 * 8; i++) {
        uint32_t v = (uint32_t)(i * 0x1234u + 0x0421u) & 0xFFFF;
        px[2 * i] = (uint8_t)v, px[2 * i + 1] = (uint8_t)(v >> 8);
    }
    px[0] = 0, px[1] = 0;    /* 0x0000 */
    px[2] = 0, px[3] = 0x80; /* 0x8000 */
    s.psm = RDTEX_PSMCT16, s.levels = 1;
    s.lv[0] = (TexpackLevel){4, 3, 16, 8, 1, px};
    CHECK(texpack_hash_bytes(&s, 0, 1, buf, sizeof(buf)) == 16 * 8 * 4, "CT16 16x8: RGBA32");
    CHECK(buf[3] == 0 && buf[7] == 0x81 && buf[4] == 0 && buf[11] == ((px[5] & 0x80) ? 0x81 : 0x7F),
          "CT16 under 7F/AEM/81: 0x0000 transparent, 0x8000 TA1");
    {
        uint32_t c = px[4] | (uint32_t)px[5] << 8;
        uint32_t v = buf[8] | (uint32_t)buf[9] << 8 | (uint32_t)buf[10] << 16;
        CHECK(v == ((c & 0x1F) << 3 | (c & 0x3E0) << 6 | (c & 0x7C00) << 9),
              "CT16 colour bits in place: %06x from %04x", v, c);
    }
    int n = texpack_candidates(&s, 0, nm, TEXPACK_MAX_CANDIDATES);
    CHECK(n == 3, "CT16 16x8: three names, one per TEXA (%d)", n);
    if (n == 3) {
        CHECK(nm[0].tex0Hash != nm[1].tex0Hash && nm[1].tex0Hash != nm[2].tex0Hash &&
                  nm[0].tex0Hash != nm[2].tex0Hash,
              "CT16: three TEX0 hashes");
        CHECK(nm[0].bits == (2u | 4u << 6 | 3u << 10 | 0x80u << 15 | 0x80u << 24) &&
                  nm[1].bits == (2u | 4u << 6 | 3u << 10 | 0x7Fu << 15 | 1u << 23 | 0x81u << 24) &&
                  nm[2].bits == (2u | 4u << 6 | 3u << 10 | 0x80u << 15 | 1u << 23 | 0x80u << 24),
              "CT16 bits carry TEXA: %08x %08x %08x", nm[0].bits, nm[1].bits, nm[2].bits);
        CHECK(nm[0].texa == 0 && nm[1].texa == 1 && nm[2].texa == 2, "CT16 texa recorded");
    }

    /* CT24 2x2 */
    memset(&s, 0, sizeof(s));
    memset(px, 0, 12);
    px[3] = 1;
    s.psm = RDTEX_PSMCT24, s.levels = 1;
    s.lv[0] = (TexpackLevel){1, 1, 2, 2, 1, px};
    CHECK(texpack_hash_bytes(&s, 0, 2, buf, sizeof(buf)) == 16 && buf[3] == 0 && buf[4] == 1 &&
              buf[7] == 0x80,
          "CT24 under AEM: black transparent, the rest TA0");
    printf("expanded: CT32, PSMT8, PSMT4, CT16, CT24\n");
}

/* ------------------------------------------------------------ names */

static void testNames(void)
{
    TexpackSource s;
    TexpackName nm[TEXPACK_MAX_CANDIDATES];
    TexpackName one;
    uint8_t l0[32 * 32];
    uint8_t l1[16 * 16];
    uint8_t clut[1024];
    uint8_t bytes[32 * 32 + 16 * 16];

    for (int i = 0; i < (int)sizeof(l0); i++) {
        l0[i] = (uint8_t)(i * 5 + 3);
    }
    for (int i = 0; i < (int)sizeof(l1); i++) {
        l1[i] = (uint8_t)(i * 3 + 1);
    }
    for (int i = 0; i < (int)sizeof(clut); i++) {
        clut[i] = (uint8_t)(i ^ 0x55);
    }
    /* a two-level PSMT8: level 0 alone, the chain 0..1, level 1 alone */
    memset(&s, 0, sizeof(s));
    s.psm = RDTEX_PSMT8, s.levels = 2;
    s.lv[0] = (TexpackLevel){5, 5, 32, 32, 2, l0};
    s.lv[1] = (TexpackLevel){4, 4, 16, 16, 2, l1};
    s.cpsm = RDTEX_PSMCT32, s.clutColors = 256, s.clut = clut;
    int n = texpack_candidates(&s, 0, nm, TEXPACK_MAX_CANDIDATES);
    CHECK(n == 3, "two levels bound at 0: three names (%d)", n);
    if (n == 3) {
        CHECK(nm[0].startLevel == 0 && !nm[0].mipChain && nm[1].startLevel == 0 && nm[1].mipChain &&
                  nm[2].startLevel == 1 && !nm[2].mipChain,
              "order: single, chain, k > 0");
        CHECK(nm[0].tex0Hash != nm[1].tex0Hash && nm[1].tex0Hash != nm[2].tex0Hash &&
                  nm[0].tex0Hash != nm[2].tex0Hash,
              "three distinct TEX0 hashes");
        CHECK(nm[0].bits == nm[1].bits && nm[0].bits == (19u | 5u << 6 | 5u << 10) &&
                  nm[2].bits == (19u | 4u << 6 | 4u << 10),
              "bits of the start level");
        CHECK(nm[0].clutHash == nm[2].clutHash, "the CLUT hash does not depend on the level");
        size_t a = texpack_hash_bytes(&s, 0, 0, bytes, sizeof(bytes));
        size_t b = texpack_hash_bytes(&s, 1, 0, bytes + a, sizeof(bytes) - a);
        CHECK(a == 1024 && b == 256 && nm[1].tex0Hash == xxh3_64(bytes, a + b) &&
                  nm[0].tex0Hash == xxh3_64(bytes, a) && nm[2].tex0Hash == xxh3_64(bytes + a, b),
              "chain = the levels' bytes one after the other");
    }
    n = texpack_candidates(&s, 1, nm, TEXPACK_MAX_CANDIDATES);
    CHECK(n == 1 && nm[0].startLevel == 1, "bound at 1: level 1 alone (%d)", n);
    CHECK(texpack_candidates(&s, 2, nm, TEXPACK_MAX_CANDIDATES) == -1, "bound past the levels");
    CHECK(texpack_candidates(&s, 0, nm, 2) == 2, "max caps the list");
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == 0 && !one.unstable, "full levels: stable");

    /* a level smaller than its GS size: unstable */
    s.lv[0].w = 24;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == 0 && one.unstable, "24 of 32 wide: unstable");
    s.lv[0].w = 32;

    /* refusals */
    s.cpsm = RDTEX_PSMCT24;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == -1 &&
              texpack_candidates(&s, 0, nm, TEXPACK_MAX_CANDIDATES) == -1,
          "a 24-bit CLUT refused");
    s.cpsm = RDTEX_PSMCT32;
    s.clut = NULL;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == -1, "a palette format without a CLUT");
    s.clut = clut;
    s.levels = TEXPACK_MAX_LEVELS + 1;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == -1, "more than 7 levels refused");
    s.levels = 2;
    CHECK(texpack_compute_name(&s, 2, 0, 0, &one) == -1, "a start level past the levels");
    s.psm = RDTEX_PSMZ32;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == -1, "PSMZ32 refused");
    s.psm = RDTEX_PSMT8;
    /* a 16-entry TIM2 CLUT under PSMT8: the rest is stale VRAM */
    s.clutColors = 16;
    CHECK(texpack_compute_name(&s, 0, 0, 0, &one) == 0 && one.unstable,
          "PSMT8 with 16 CLUT entries: unstable");
    printf("names: candidates, unstable, refusals\n");
}

static void testText(void)
{
    TexpackName n;
    TexpackName p;
    char buf[TEXPACK_NAME_MAX];

    memset(&n, 0, sizeof(n));
    n.tex0Hash = 0x0e997f3381dbc55eull, n.clutHash = 0x6050a3a10f92b8beull, n.bits = 0x1994;
    n.hasClut = 1;
    CHECK(texpack_format_name(&n, buf, sizeof(buf)) == 41 &&
              strcmp(buf, "e997f3381dbc55e-6050a3a10f92b8be-00001994") == 0,
          "CLUT name, the hashes not zero-padded: %s", buf);
    n.hasClut = 0, n.tex0Hash = 0, n.bits = 0x2214;
    CHECK(texpack_format_name(&n, buf, sizeof(buf)) == 10 && strcmp(buf, "0-00002214") == 0,
          "direct name: %s", buf);
    n.tex0Hash = UINT64_MAX, n.clutHash = UINT64_MAX, n.bits = UINT32_MAX, n.hasClut = 1;
    CHECK(texpack_format_name(&n, buf, sizeof(buf)) == 42, "the longest name: 42 characters");
    CHECK(texpack_format_name(&n, buf, 42) == -1, "no room for the terminator");

    /* parse: the reference pack's names, round trips */
    CHECK(texpack_parse_name("e997f3381dbc55e-6050a3a10f92b8be-00001994.dds", &p) == 0 &&
              p.tex0Hash == 0x0e997f3381dbc55eull && p.clutHash == 0x6050a3a10f92b8beull &&
              p.bits == 0x1994 && p.hasClut && p.regionW == 0,
          "a pack name parses");
    CHECK(texpack_parse_name("af85949420f0ab2f-3e511c50f1b988b4-00006214.png", &p) == 0 &&
              p.bits == 0x2214,
          "bit 14 (the old TCC) is cleared: %08x", p.bits);
    CHECK(texpack_parse_name("8e193980afae08a-00001994.png", &p) == 0 && !p.hasClut &&
              p.tex0Hash == 0x8e193980afae08aull && p.bits == 0x1994,
          "a direct name parses");
    CHECK(texpack_parse_name("ad352332a6da418d-713c8f992e57439b-000026530.dds", &p) == -1,
          "the pack's nine-digit bits name is not a name");
    CHECK(texpack_parse_name("5a351caf28440929-563e6763219b0a0-00001dd3 - copia.dds", &p) == -1,
          "the pack's ' - copia' copy is not a name");
    CHECK(texpack_parse_name("e997f3381dbc55e-6050a3a10f92b8be-00001994-mip1.dds", &p) == -1,
          "a -mip1 level file is not indexed");
    CHECK(texpack_parse_name("e997f3381dbc55e-6050a3a10f92b8be-00001994", &p) == -1,
          "no extension: no name");
    CHECK(texpack_parse_name("Instructions.txt", &p) == -1, "a text file");
    CHECK(texpack_parse_name("e997f3381dbc55e-6050a3a10f92b8be-r128x64-00001994.png", &p) == 1 &&
              p.regionW == 128 && p.regionH == 64 && p.hasClut && p.bits == 0x1994,
          "a region name: 1, its size");
    CHECK(texpack_parse_name("e997f3381dbc55e-r16x8-00000002.png", &p) == 1 && !p.hasClut &&
              p.regionW == 16 && p.regionH == 8,
          "a direct region name");
    /* the old form: SourceRegion bits, min/max X in the low 32, Y above */
    CHECK(texpack_parse_name("e997f3381dbc55e-6050a3a10f92b8be-r00300010000a0002-00001994.png",
                             &p) == 1 &&
              p.regionW == 0x0a - 0x02 && p.regionH == 0x30 - 0x10,
          "an old region name: %ux%u", p.regionW, p.regionH);
    for (int i = 0; i < 4; i++) {
        char file[TEXPACK_NAME_MAX + 4];
        memset(&n, 0, sizeof(n));
        n.tex0Hash = 0x123456789abcdefull >> (i * 13);
        n.clutHash = i & 1 ? 0xfedcba9876543210ull >> i : 0;
        n.hasClut = (uint8_t)(i & 1);
        n.bits = 0x13u | (uint32_t)i << 6 | 0x80u << 24;
        CHECK(texpack_format_name(&n, file, TEXPACK_NAME_MAX) > 0, "round trip %d formats", i);
        strcat(file, ".png");
        CHECK(texpack_parse_name(file, &p) == 0 && p.tex0Hash == n.tex0Hash &&
                  p.clutHash == n.clutHash && p.bits == n.bits && p.hasClut == n.hasClut,
              "round trip %d: %s", i, file);
    }
    printf("text: format and parse\n");
}

int main(void)
{
    testXxh3();
    testSwizzle();
    testClut();
    testExpanded();
    testNames();
    testText();
    if (failures) {
        printf("texpack_name_test: %d failures\n", failures);
        return 1;
    }
    printf("texpack_name_test: ok\n");
    return 0;
}
