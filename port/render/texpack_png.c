/* texpack_png.c: PNG files of PCSX2 texture packs (texpack.h
 * texpack_load_png), a small reader on miniz's inflate (tinfl).
 *
 * PCSX2 reads packs with libpng and keeps two cases: RGBA rows as they are
 * (the file's alpha byte is the GS alpha, 0x80 = opaque) and RGB rows with
 * alpha 0x80 (GSTextureReplacementLoaders.cpp PNGLoader).  This reader
 * gives the same texels for those and makes the rest of the format usable
 * the same way: every colour type (grey, RGB, palette, grey + alpha, RGBA),
 * bit depths 1, 2, 4, 8 and 16 (16-bit samples keep their high byte, grey
 * under 8 bits is scaled to 0..255), the five row filters, Adam7
 * interlacing, and tRNS: a palette's alpha bytes are taken as stored (like
 * an RGBA file's), a grey or RGB colour key gives alpha 0 to the texels
 * that match it (compared at the file's bit depth); everything without
 * alpha gets 0x80.  Chunk CRCs are checked: a critical chunk that fails
 * refuses the file, a tRNS that fails is ignored (libpng's defaults).
 * Rows end once the image data has every row; IEND is not needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "rd_internal.h"
#include "texpack.h"

/* larger than any texture a GPU takes; keeps w * h * 4 far from overflow */
#define PNG_MAX_SIDE 32768u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int refuse(const char *file, const char *why, TexpackImage *out)
{
    memset(out, 0, sizeof(*out));
    if (file != NULL) {
        fprintf(stderr, "textures: %s: %s; the game's texture is kept\n", file, why);
    }
    return -1;
}

typedef struct PngInfo {
    uint32_t w, h;
    uint8_t depth, colour, interlace;
    uint32_t channels; /* samples per pixel */
    uint8_t pal[256][4];
    uint32_t palCount;
    int hasKey;
    uint16_t key[3]; /* tRNS colour key: grey, or R G B, at the file's depth */
} PngInfo;

/* Adam7: the passes' first column and row and their steps */
static const uint8_t s_adamX0[7] = {0, 4, 0, 2, 0, 1, 0};
static const uint8_t s_adamY0[7] = {0, 0, 4, 0, 2, 0, 1};
static const uint8_t s_adamDx[7] = {8, 8, 4, 4, 2, 2, 1};
static const uint8_t s_adamDy[7] = {8, 8, 8, 4, 4, 2, 2};

static uint32_t passSize(uint32_t extent, uint32_t start, uint32_t step)
{
    return extent > start ? (extent - start + step - 1) / step : 0;
}

static size_t rowBytes(const PngInfo *pi, uint32_t w)
{
    return ((size_t)w * pi->channels * pi->depth + 7) / 8;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    const int p = (int)a + (int)b - (int)c;
    const int pa = abs(p - (int)a), pb = abs(p - (int)b), pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

/* undoes the filters of h rows of n bytes (each after its filter byte) in
   place; bpp the bytes of a whole pixel (at least 1).  0, or -1 for a bad
   filter type. */
static int unfilter(uint8_t *rows, uint32_t h, size_t n, uint32_t bpp)
{
    uint8_t *prev = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *f = rows + (size_t)y * (n + 1);
        uint8_t *r = f + 1;
        switch (f[0]) {
        case 0:
            break;
        case 1:
            for (size_t i = bpp; i < n; i++) {
                r[i] = (uint8_t)(r[i] + r[i - bpp]);
            }
            break;
        case 2:
            for (size_t i = 0; prev && i < n; i++) {
                r[i] = (uint8_t)(r[i] + prev[i]);
            }
            break;
        case 3:
            for (size_t i = 0; i < n; i++) {
                const uint32_t a = i >= bpp ? r[i - bpp] : 0, b = prev ? prev[i] : 0;
                r[i] = (uint8_t)(r[i] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < n; i++) {
                const uint8_t a = i >= bpp ? r[i - bpp] : 0, b = prev ? prev[i] : 0;
                const uint8_t c = (i >= bpp && prev) ? prev[i - bpp] : 0;
                r[i] = (uint8_t)(r[i] + paeth(a, b, c));
            }
            break;
        default:
            return -1;
        }
        prev = r;
    }
    return 0;
}

/* sample k (0-based) of a row at the file's depth (16-bit: the whole
   value) */
static uint32_t sample(const PngInfo *pi, const uint8_t *row, size_t k)
{
    switch (pi->depth) {
    case 16:
        return ((uint32_t)row[k * 2] << 8) | row[k * 2 + 1];
    case 8:
        return row[k];
    default: {
        const size_t bit = k * pi->depth;
        const uint32_t shift = 8u - pi->depth - (uint32_t)(bit & 7);
        return (row[bit >> 3] >> shift) & ((1u << pi->depth) - 1u);
    }
    }
}

/* a sample scaled to 8 bits: the high byte of 16, grey under 8 bits
   replicated up (libpng's expand) */
static uint8_t to8(const PngInfo *pi, uint32_t v)
{
    switch (pi->depth) {
    case 16:
        return (uint8_t)(v >> 8);
    case 8:
        return (uint8_t)v;
    case 4:
        return (uint8_t)(v * 17u);
    case 2:
        return (uint8_t)(v * 85u);
    default:
        return v ? 0xFF : 0x00;
    }
}

/* one pixel x of an unfiltered row to RGBA8 (raw GS alpha) */
static void pixel(const PngInfo *pi, const uint8_t *row, uint32_t x, uint8_t *o)
{
    const size_t k = (size_t)x * pi->channels;
    switch (pi->colour) {
    case 0: { /* grey */
        const uint32_t g = sample(pi, row, k);
        o[0] = o[1] = o[2] = to8(pi, g);
        o[3] = pi->hasKey && g == pi->key[0] ? 0x00 : 0x80;
        break;
    }
    case 2: { /* RGB */
        const uint32_t r = sample(pi, row, k), g = sample(pi, row, k + 1),
                       b = sample(pi, row, k + 2);
        o[0] = to8(pi, r);
        o[1] = to8(pi, g);
        o[2] = to8(pi, b);
        o[3] = pi->hasKey && r == pi->key[0] && g == pi->key[1] && b == pi->key[2] ? 0x00 : 0x80;
        break;
    }
    case 3: { /* palette; an index past the palette reads black, as libpng's */
        const uint32_t i = sample(pi, row, k);
        if (i < pi->palCount) {
            memcpy(o, pi->pal[i], 4);
        } else {
            o[0] = o[1] = o[2] = 0;
            o[3] = 0x80;
        }
        break;
    }
    case 4: /* grey + alpha */
        o[0] = o[1] = o[2] = to8(pi, sample(pi, row, k));
        o[3] = to8(pi, sample(pi, row, k + 1));
        break;
    default: /* RGBA */
        o[0] = to8(pi, sample(pi, row, k));
        o[1] = to8(pi, sample(pi, row, k + 1));
        o[2] = to8(pi, sample(pi, row, k + 2));
        o[3] = to8(pi, sample(pi, row, k + 3));
        break;
    }
}

static int validDepth(uint8_t colour, uint8_t depth)
{
    switch (colour) {
    case 0:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2:
    case 4:
    case 6:
        return depth == 8 || depth == 16;
    default:
        return 0;
    }
}

int texpack_load_png(const uint8_t *data, size_t size, const char *file, TexpackImage *out)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    PngInfo pi;
    uint8_t *idat = NULL, *raw = NULL, *blob = NULL;
    size_t idatLen = 0;
    const char *why = NULL;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memset(&pi, 0, sizeof(pi));
    if (data == NULL || size < 8 || memcmp(data, sig, 8) != 0) {
        return refuse(file, "not a PNG file", out);
    }
    /* the chunks: IHDR first, PLTE, tRNS, the IDATs (concatenated) */
    size_t pos = 8;
    int haveHeader = 0, done = 0, cut = 0;
    while (!done && why == NULL && size - pos >= 12) {
        const uint32_t len = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        if (len > size - pos - 12) {
            cut = 1;
            break; /* cut short: what came before may still hold every row */
        }
        const uint8_t *body = data + pos + 8;
        const int critical = (type[0] & 0x20) == 0;
        const uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, type, (size_t)len + 4);
        const int crcOk = crc == be32(body + len);
        pos += (size_t)len + 12;
        if (!crcOk) {
            if (critical) {
                why = "a damaged PNG (checksum)";
            }
            continue; /* an ancillary chunk that fails its checksum is ignored */
        }
        if (!haveHeader) {
            if (memcmp(type, "IHDR", 4) != 0 || len != 13) {
                why = "a damaged PNG header";
                break;
            }
            pi.w = be32(body);
            pi.h = be32(body + 4);
            pi.depth = body[8];
            pi.colour = body[9];
            pi.interlace = body[12];
            if (pi.w == 0 || pi.h == 0 || pi.w > PNG_MAX_SIDE || pi.h > PNG_MAX_SIDE) {
                why = "the PNG size is out of range";
            } else if (texpack_max_side() &&
                       (pi.w > texpack_max_side() || pi.h > texpack_max_side())) {
                /* before any row is allocated: a huge file costs nothing */
                why = "the picture is larger than this graphics card can show";
            } else if (!validDepth(pi.colour, pi.depth) || body[10] != 0 || body[11] != 0 ||
                       pi.interlace > 1) {
                why = "a PNG format this reader does not know";
            }
            static const uint8_t channels[7] = {1, 0, 3, 1, 2, 0, 4};
            pi.channels = channels[pi.colour < 7 ? pi.colour : 1];
            haveHeader = 1;
            continue;
        }
        if (memcmp(type, "PLTE", 4) == 0) {
            if (len % 3 != 0 || len / 3 > 256 || len == 0) {
                why = "a damaged PNG palette";
                break;
            }
            pi.palCount = len / 3;
            for (uint32_t i = 0; i < pi.palCount; i++) {
                pi.pal[i][0] = body[i * 3];
                pi.pal[i][1] = body[i * 3 + 1];
                pi.pal[i][2] = body[i * 3 + 2];
                pi.pal[i][3] = 0x80;
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (pi.colour == 3) {
                for (uint32_t i = 0; i < len && i < 256; i++) {
                    pi.pal[i][3] = body[i];
                }
            } else if (pi.colour == 0 && len >= 2) {
                pi.hasKey = 1;
                pi.key[0] = (uint16_t)((body[0] << 8) | body[1]);
            } else if (pi.colour == 2 && len >= 6) {
                pi.hasKey = 1;
                for (int c = 0; c < 3; c++) {
                    pi.key[c] = (uint16_t)((body[c * 2] << 8) | body[c * 2 + 1]);
                }
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *grown = realloc(idat, idatLen + len + 1);
            if (grown == NULL) {
                why = "not enough memory for the PNG";
                break;
            }
            idat = grown;
            memcpy(idat + idatLen, body, len);
            idatLen += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            done = 1;
        } else if (critical) {
            why = "a PNG with a part this reader does not know";
        }
    }
    if (why == NULL && !haveHeader) {
        why = "a damaged PNG header";
    }
    if (why == NULL && pi.colour == 3 && pi.palCount == 0) {
        why = "a palette PNG without its palette";
    }
    if (why == NULL && idatLen == 0) {
        why = cut ? "the PNG file is cut short" : "a PNG without image data";
    }

    /* the filtered rows of every pass, one after the other */
    const uint32_t passes = pi.interlace ? 7u : 1u;
    size_t rawLen = 0;
    for (uint32_t p = 0; why == NULL && p < passes; p++) {
        const uint32_t pw = pi.interlace ? passSize(pi.w, s_adamX0[p], s_adamDx[p]) : pi.w;
        const uint32_t ph = pi.interlace ? passSize(pi.h, s_adamY0[p], s_adamDy[p]) : pi.h;
        if (pw && ph) {
            rawLen += (rowBytes(&pi, pw) + 1) * ph;
        }
    }
    if (why == NULL) {
        raw = malloc(rawLen);
        tinfl_decompressor *inf = raw ? tinfl_decompressor_alloc() : NULL;
        if (inf == NULL) {
            why = "not enough memory for the PNG";
        } else {
            size_t inLen = idatLen, outLen = rawLen;
            tinfl_init(inf);
            const tinfl_status st = tinfl_decompress(inf, idat, &inLen, raw, raw, &outLen,
                                                     TINFL_FLAG_PARSE_ZLIB_HEADER |
                                                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            /* HAS_MORE_OUTPUT: every row is there, the stream goes on
               (libpng ignores the extra too) */
            if ((st != TINFL_STATUS_DONE && st != TINFL_STATUS_HAS_MORE_OUTPUT) ||
                outLen < rawLen) {
                why = cut ? "the PNG file is cut short" : "the PNG image data is damaged";
            }
            tinfl_decompressor_free(inf);
        }
    }
    if (why == NULL) {
        blob = malloc((size_t)pi.w * pi.h * 4);
        if (blob == NULL) {
            why = "not enough memory for the PNG";
        }
    }
    const uint32_t bpp = (pi.channels * pi.depth + 7) / 8;
    uint8_t *rows = raw;
    for (uint32_t p = 0; why == NULL && p < passes; p++) {
        const uint32_t x0 = pi.interlace ? s_adamX0[p] : 0, y0 = pi.interlace ? s_adamY0[p] : 0;
        const uint32_t dx = pi.interlace ? s_adamDx[p] : 1, dy = pi.interlace ? s_adamDy[p] : 1;
        const uint32_t pw = passSize(pi.w, x0, dx), ph = passSize(pi.h, y0, dy);
        if (pw == 0 || ph == 0) {
            continue;
        }
        const size_t n = rowBytes(&pi, pw);
        if (unfilter(rows, ph, n, bpp ? bpp : 1u) != 0) {
            why = "a damaged PNG (row filter)";
            break;
        }
        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t *r = rows + (size_t)y * (n + 1) + 1;
            uint8_t *o = blob + ((size_t)(y0 + y * dy) * pi.w + x0) * 4;
            for (uint32_t x = 0; x < pw; x++) {
                pixel(&pi, r, x, o + (size_t)x * dx * 4);
            }
        }
        rows += (n + 1) * ph;
    }
    free(idat);
    free(raw);
    if (why != NULL) {
        free(blob);
        return refuse(file, why, out);
    }
    out->fmt = RD_TEXEL_RGBA8;
    out->w = pi.w;
    out->h = pi.h;
    out->levels = 1;
    out->lv[0].data = blob;
    out->lv[0].w = pi.w;
    out->lv[0].h = pi.h;
    out->lv[0].pitch = pi.w * 4;
    out->lv[0].size = (size_t)pi.w * pi.h * 4;
    out->blob = blob;
    out->bytes = out->lv[0].size;
    return 0;
}
