/* texpack_name.c: the names PCSX2 gives textures.  texpack_name.h has the
 * rules and where in PCSX2 each one comes from. */
#include "texpack_name.h"
#include <stdlib.h>
#include <string.h>
#include "rd_tex.h"

/* TEX0.TA0 / TEXA.AEM / TEX0.TA1 of the three RdTexA modes (rd_state.h):
   80/0/80, 7F/1/81, 80/1/80 */
#define TEXA_MODES 3

static void texaFields(int texa, uint32_t *ta0, uint32_t *aem, uint32_t *ta1)
{
    switch (texa) {
    case 1:
        *ta0 = 0x7F, *aem = 1, *ta1 = 0x81;
        break;
    case 2:
        *ta0 = 0x80, *aem = 1, *ta1 = 0x80;
        break;
    default:
        *ta0 = 0x80, *aem = 0, *ta1 = 0x80;
        break;
    }
}

/* ------------------------------------------------------------ formats */

static int isPalette(uint32_t psm)
{
    return psm == RDTEX_PSMT8 || psm == RDTEX_PSMT4 || psm == RDTEX_PSMT8H ||
           psm == RDTEX_PSMT4HL || psm == RDTEX_PSMT4HH;
}

/* CLUT entries a palette format reads (GSLocalMemory m_psm[].pal) */
static uint32_t paletteSize(uint32_t psm)
{
    return psm == RDTEX_PSMT8 || psm == RDTEX_PSMT8H ? 256 : 16;
}

/* the direct formats whose TEXA PCSX2 keeps in the name (m_psm[].pal == 0
   && fmt > 0) */
static int texaInBits(uint32_t psm)
{
    return psm == RDTEX_PSMCT24 || psm == RDTEX_PSMCT16 || psm == RDTEX_PSMCT16S;
}

static int is16(uint32_t psm)
{
    return psm == RDTEX_PSMCT16 || psm == RDTEX_PSMCT16S;
}

/* the block path: all 32 bits of the word are the format's (m_psm[].fmsk) */
static int fullMask(uint32_t psm)
{
    return psm == RDTEX_PSMCT32 || psm == RDTEX_PSMT8 || psm == RDTEX_PSMT4;
}

int texpack_BlockSize(uint32_t psm, uint32_t *bw, uint32_t *bh)
{
    uint32_t w;
    uint32_t h;

    switch (psm) {
    case RDTEX_PSMCT32:
    case RDTEX_PSMCT24:
    case RDTEX_PSMT8H:
    case RDTEX_PSMT4HL:
    case RDTEX_PSMT4HH:
        w = 8, h = 8;
        break;
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
        w = 16, h = 8;
        break;
    case RDTEX_PSMT8:
        w = 16, h = 16;
        break;
    case RDTEX_PSMT4:
        w = 32, h = 16;
        break;
    default:
        return -1;
    }
    if (bw != NULL) {
        *bw = w;
    }
    if (bh != NULL) {
        *bh = h;
    }
    return 0;
}

#define BIT(v, n) (((v) >> (n)) & 1u)

/* GSTables.cpp columnTable32/16/8/4 in closed form; texpack_name_test
   rebuilds the tables from GSBlock.h's column writes and compares every
   entry */
uint32_t texpack_BlockOffset(uint32_t psm, uint32_t x, uint32_t y)
{
    switch (psm) {
    case RDTEX_PSMCT32:
    case RDTEX_PSMCT24:
        if (x >= 8 || y >= 8) {
            return UINT32_MAX;
        }
        return BIT(x, 0) | BIT(y, 0) << 1 | ((x >> 1) & 3) << 2 | ((y >> 1) & 3) << 4;
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
        if (x >= 16 || y >= 8) {
            return UINT32_MAX;
        }
        return BIT(x, 3) | BIT(x, 0) << 1 | BIT(y, 0) << 2 | BIT(x, 1) << 3 | BIT(x, 2) << 4 |
               BIT(y, 1) << 5 | BIT(y, 2) << 6;
    case RDTEX_PSMT8:
        if (x >= 16 || y >= 16) {
            return UINT32_MAX;
        }
        return BIT(y, 1) | BIT(x, 3) << 1 | BIT(x, 0) << 2 | BIT(y, 0) << 3 | BIT(x, 1) << 4 |
               (BIT(x, 2) ^ BIT(y, 1) ^ BIT(y, 2)) << 5 | BIT(y, 2) << 6 | BIT(y, 3) << 7;
    case RDTEX_PSMT4:
        if (x >= 32 || y >= 16) {
            return UINT32_MAX;
        }
        return BIT(y, 1) | BIT(x, 3) << 1 | BIT(x, 4) << 2 | BIT(x, 0) << 3 | BIT(y, 0) << 4 |
               BIT(x, 1) << 5 | (BIT(x, 2) ^ BIT(y, 1) ^ BIT(y, 2)) << 6 | BIT(y, 2) << 7 |
               BIT(y, 3) << 8;
    default:
        return UINT32_MAX;
    }
}

/* ------------------------------------------------------------ texels */

/* texel (x, y) of a level as GS memory holds it: the index for the
   palette formats (the H formats' bits of their word), the raw word for
   the direct ones; 0 outside the image (PCSX2 hashes stale VRAM there) */
static uint32_t texel(uint32_t psm, const TexpackLevel *l, uint32_t x, uint32_t y)
{
    const uint8_t *p = l->pixels;
    size_t i;

    if (x >= l->w || y >= l->h || p == NULL) {
        return 0;
    }
    i = (size_t)y * l->w + x;
    switch (psm) {
    case RDTEX_PSMT4:
        return i & 1 ? p[i >> 1] >> 4 : p[i >> 1] & 0x0F;
    case RDTEX_PSMT8:
        return p[i];
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
        return (uint32_t)p[2 * i] | (uint32_t)p[2 * i + 1] << 8;
    case RDTEX_PSMCT24:
        return (uint32_t)p[3 * i] | (uint32_t)p[3 * i + 1] << 8 | (uint32_t)p[3 * i + 2] << 16;
    default: {
        uint32_t v = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 |
                     (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
        switch (psm) {
        case RDTEX_PSMT8H:
            return v >> 24;
        case RDTEX_PSMT4HL:
            return (v >> 24) & 0x0F;
        case RDTEX_PSMT4HH:
            return v >> 28;
        default:
            return v;
        }
    }
    }
}

/* GSBlock.h Expand16to32 / GSClut.cpp Expand16 */
static uint32_t expand16(uint32_t c, uint32_t ta0, uint32_t aem, uint32_t ta1)
{
    uint32_t a = c & 0x8000 ? ta1 : ta0;
    if (aem && c == 0) {
        a = 0;
    }
    return (c & 0x1F) << 3 | (c & 0x3E0) << 6 | (c & 0x7C00) << 9 | a << 24;
}

/* GSBlock.h Expand24to32 */
static uint32_t expand24(uint32_t c, uint32_t ta0, uint32_t aem)
{
    c &= 0xFFFFFF;
    return c | (aem && c == 0 ? 0 : ta0 << 24);
}

static void put32(uint8_t *o, uint32_t v)
{
    o[0] = (uint8_t)v;
    o[1] = (uint8_t)(v >> 8);
    o[2] = (uint8_t)(v >> 16);
    o[3] = (uint8_t)(v >> 24);
}

static int levelOk(const TexpackSource *src, uint32_t level)
{
    const TexpackLevel *l;

    if (src == NULL || src->levels < 1 || src->levels > TEXPACK_MAX_LEVELS ||
        level >= src->levels || texpack_BlockSize(src->psm, NULL, NULL) != 0) {
        return 0;
    }
    l = &src->lv[level];
    return l->tw <= 10 && l->th <= 10;
}

size_t texpack_HashBytes(const TexpackSource *src, uint32_t level, int texa, uint8_t *out,
                         size_t cap)
{
    const TexpackLevel *l;
    uint32_t psm;
    uint32_t tw;
    uint32_t th;
    uint32_t bw;
    uint32_t bh;
    uint32_t ta0;
    uint32_t aem;
    uint32_t ta1;
    size_t n;

    if (!levelOk(src, level)) {
        return 0;
    }
    l = &src->lv[level];
    psm = src->psm;
    tw = 1u << l->tw;
    th = 1u << l->th;
    texpack_BlockSize(psm, &bw, &bh);

    if (tw >= bw && th >= bh && fullMask(psm)) {
        /* the block path: 256-byte blocks in raster order, each as the GS
           write swizzle leaves it (HashTextureLevel's BlockPtr loop) */
        n = (size_t)(tw / bw) * (th / bh) * 256;
        if (out == NULL || cap < n) {
            return n;
        }
        memset(out, 0, n);
        for (uint32_t by = 0; by < th / bh; by++) {
            for (uint32_t bx = 0; bx < tw / bw; bx++) {
                uint8_t *blk = out + ((size_t)by * (tw / bw) + bx) * 256;
                for (uint32_t y = 0; y < bh; y++) {
                    for (uint32_t x = 0; x < bw; x++) {
                        uint32_t v = texel(psm, l, bx * bw + x, by * bh + y);
                        uint32_t o = texpack_BlockOffset(psm, x, y);
                        if (psm == RDTEX_PSMT4) {
                            blk[o >> 1] |= (uint8_t)(o & 1 ? v << 4 : v);
                        } else if (psm == RDTEX_PSMT8) {
                            blk[o] = (uint8_t)v;
                        } else {
                            put32(blk + 4 * o, v);
                        }
                    }
                }
            }
        }
        return n;
    }

    /* the expanded path: th rows of tw texels, one byte per index for the
       palette formats (rtxP), RGBA32 for the direct ones (rtx: CT32 as is,
       CT24 and CT16 through TEXA) */
    n = (size_t)tw * th * (isPalette(psm) ? 1 : 4);
    if (out == NULL || cap < n) {
        return n;
    }
    texaFields(texa, &ta0, &aem, &ta1);
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t v = texel(psm, l, x, y);
            size_t i = (size_t)y * tw + x;
            if (isPalette(psm)) {
                out[i] = (uint8_t)v;
            } else if (is16(psm)) {
                put32(out + 4 * i, expand16(v, ta0, aem, ta1));
            } else if (psm == RDTEX_PSMCT24) {
                put32(out + 4 * i, expand24(v, ta0, aem));
            } else {
                put32(out + 4 * i, v);
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------ names */

/* whether src's name changes with TEXA: the texels (CT24, CT16) or the
   CLUT (a 16-bit one) */
static int dependsOnTexa(const TexpackSource *src)
{
    if (texaInBits(src->psm)) {
        return 1;
    }
    return isPalette(src->psm) && is16(src->cpsm);
}

static int sourceOk(const TexpackSource *src)
{
    if (src == NULL || src->levels < 1 || src->levels > TEXPACK_MAX_LEVELS ||
        texpack_BlockSize(src->psm, NULL, NULL) != 0) {
        return 0;
    }
    if (isPalette(src->psm)) {
        if (src->clut == NULL || (src->clutColors != 16 && src->clutColors != 256)) {
            return 0;
        }
        if (src->cpsm != RDTEX_PSMCT32 && !is16(src->cpsm)) {
            return 0; /* a 24-bit CLUT: not a GS CLUT */
        }
    }
    return 1;
}

/* GSClut::Read32's m_buff32, hashed by PaletteKeyHash: index order, CSM1
   undone (entry i is memory entry rdtex_Csm1Index(i): the same for a 16-
   and a 32-bit CLUT, both read as a 16x16 image of two or four blocks),
   16-bit entries through Expand16 */
static uint64_t clutHash(const TexpackSource *src, int texa, int *unstable)
{
    uint8_t buf[256 * 4];
    const uint8_t *c = src->clut;
    uint32_t n = paletteSize(src->psm);
    uint32_t ta0;
    uint32_t aem;
    uint32_t ta1;

    texaFields(texa, &ta0, &aem, &ta1);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t m = rdtex_Csm1Index(i, src->clutColors);
        uint32_t v = 0;
        if (m >= src->clutColors) {
            /* past the TIM2's CLUT: stale VRAM in PCSX2 */
            *unstable = 1;
        } else if (src->cpsm == RDTEX_PSMCT32) {
            v = (uint32_t)c[4 * m] | (uint32_t)c[4 * m + 1] << 8 | (uint32_t)c[4 * m + 2] << 16 |
                (uint32_t)c[4 * m + 3] << 24;
        } else {
            v = (uint32_t)c[2 * m] | (uint32_t)c[2 * m + 1] << 8;
        }
        if (is16(src->cpsm)) {
            /* a missing entry is a zero in VRAM, expanded like the rest */
            v = expand16(v, ta0, aem, ta1);
        }
        put32(buf + 4 * i, v);
    }
    return xxh3_64(buf, (size_t)n * 4);
}

int texpack_ComputeName(const TexpackSource *src, uint32_t startLevel, int texa, int mipChain,
                        TexpackName *out)
{
    uint32_t last;
    size_t total = 0;
    size_t at = 0;
    uint8_t *buf;
    int unstable = 0;
    int dep;

    if (out == NULL || !sourceOk(src) || startLevel >= src->levels) {
        return -1;
    }
    dep = dependsOnTexa(src);
    if (dep && (texa < 0 || texa >= TEXA_MODES)) {
        return -1;
    }
    if (!dep) {
        texa = 0;
    }
    last = mipChain ? src->levels - 1 : startLevel;
    for (uint32_t k = startLevel; k <= last; k++) {
        size_t n = texpack_HashBytes(src, k, texa, NULL, 0);
        const TexpackLevel *l = &src->lv[k];
        if (n == 0) {
            return -1;
        }
        total += n;
        if (l->w < (1u << l->tw) || l->h < (1u << l->th)) {
            unstable = 1;
        }
    }
    /* streaming XXH3 over the levels equals one XXH3 over their bytes
       one after the other */
    buf = malloc(total);
    if (buf == NULL) {
        return -1;
    }
    for (uint32_t k = startLevel; k <= last; k++) {
        at += texpack_HashBytes(src, k, texa, buf + at, total - at);
    }

    memset(out, 0, sizeof(*out));
    out->tex0Hash = xxh3_64(buf, total);
    free(buf);

    out->bits = (src->psm & 0x3F) | (uint32_t)src->lv[startLevel].tw << 6 |
                (uint32_t)src->lv[startLevel].th << 10;
    if (texaInBits(src->psm)) {
        uint32_t ta0;
        uint32_t aem;
        uint32_t ta1;
        texaFields(texa, &ta0, &aem, &ta1);
        out->bits |= ta0 << 15 | aem << 23 | ta1 << 24;
    }
    if (isPalette(src->psm)) {
        out->hasClut = 1;
        out->clutHash = clutHash(src, texa, &unstable);
    }
    out->startLevel = (uint8_t)startLevel;
    out->texa = dep ? (uint8_t)texa : TEXPACK_TEXA_ANY;
    out->mipChain = (uint8_t)(last > startLevel);
    out->unstable = (uint8_t)unstable;
    return 0;
}

/* one structure (start level, chain or not) at each TEXA it needs */
static int addNames(const TexpackSource *src, uint32_t start, int chain, TexpackName *out, int have,
                    int max)
{
    int modes = dependsOnTexa(src) ? TEXA_MODES : 1;
    int first = have;

    for (int t = 0; t < modes && have < max; t++) {
        if (t > 0 && isPalette(src->psm)) {
            /* only the 16-bit CLUT moves with TEXA: the texels' hash stays */
            int unstable = out[first].unstable;
            out[have] = out[first];
            out[have].clutHash = clutHash(src, t, &unstable);
            out[have].texa = (uint8_t)t;
            out[have].unstable = (uint8_t)unstable;
        } else if (texpack_ComputeName(src, start, t, chain, &out[have]) != 0) {
            return -1;
        }
        have++;
    }
    return have;
}

int texpack_Candidates(const TexpackSource *src, uint32_t boundLevel, TexpackName *out, int max)
{
    int have = 0;

    if (out == NULL || max < 0 || !sourceOk(src) || boundLevel >= src->levels) {
        return -1;
    }
    for (uint32_t k = boundLevel; k < src->levels && have < max; k++) {
        have = addNames(src, k, 0, out, have, max);
        if (have >= 0 && k + 1 < src->levels) {
            /* a chain of one level would be the single name again */
            have = addNames(src, k, 1, out, have, max);
        }
        if (have < 0) {
            return -1;
        }
    }
    return have;
}

/* ------------------------------------------------------------ text */

/* v in lower-case hex, no padding unless width says so */
static size_t hexOut(char *p, uint64_t v, int width)
{
    char tmp[16];
    size_t n = 0;

    do {
        tmp[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v != 0);
    while ((int)n < width) {
        tmp[n++] = '0';
    }
    for (size_t i = 0; i < n; i++) {
        p[i] = tmp[n - 1 - i];
    }
    return n;
}

int texpack_FormatName(const TexpackName *n, char *buf, size_t size)
{
    char tmp[TEXPACK_NAME_MAX];
    size_t len = 0;

    if (n == NULL || buf == NULL) {
        return -1;
    }
    len += hexOut(tmp + len, n->tex0Hash, 0);
    tmp[len++] = '-';
    if (n->hasClut) {
        len += hexOut(tmp + len, n->clutHash, 0);
        tmp[len++] = '-';
    }
    len += hexOut(tmp + len, n->bits, 8);
    if (len + 1 > size) {
        return -1;
    }
    memcpy(buf, tmp, len);
    buf[len] = 0;
    return (int)len;
}

/* sscanf's reading of one number as ParseReplacementName's formats ask:
   leading white space, an optional '+', for hex an optional "0x", then at
   least one digit; width (0 = none) caps the characters taken, prefix
   included, as "%08x" does.  Returns the characters consumed, 0 for no
   number.  A value past 64 bits is refused (glibc saturates; no real
   name has one). */
static size_t scanNum(const char *s, int hex, int width, uint64_t *out)
{
    size_t i = 0;
    size_t lim = width > 0 ? (size_t)width : (size_t)-1;
    size_t digits = 0;
    uint64_t v = 0;

    while (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')) {
        i++;
    }
    size_t start = i;
    if (s[i] == '+' && i - start < lim) {
        i++;
    }
    if (hex && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X') && i + 2 - start < lim) {
        int d = s[i + 2];
        if ((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')) {
            i += 2;
        }
    }
    while (i - start < lim) {
        int c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (hex && c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (hex && c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            break;
        }
        if (hex ? (v >> 60) != 0 : v > (UINT64_MAX - d) / 10) {
            return 0;
        }
        v = hex ? v << 4 | d : v * 10 + d;
        digits++;
        i++;
    }
    if (digits == 0) {
        return 0;
    }
    *out = v;
    return i;
}

/* One of PCSX2's formats against s.  fmt: 'H' a %llx, 'U' a %u, 'B' the
   %08x bits, any other character itself; a '.' must follow the last
   field.  The numbers go to v[] in order.  1 on a match. */
static int scanFormat(const char *s, const char *fmt, uint64_t *v)
{
    int nv = 0;

    for (; *fmt != 0; fmt++) {
        size_t used;
        switch (*fmt) {
        case 'H':
            used = scanNum(s, 1, 0, &v[nv]);
            break;
        case 'U':
            used = scanNum(s, 0, 0, &v[nv]);
            if (used != 0 && v[nv] > UINT32_MAX) {
                return 0;
            }
            break;
        case 'B':
            used = scanNum(s, 1, 8, &v[nv]);
            break;
        default:
            if (*s != *fmt) {
                return 0;
            }
            s++;
            continue;
        }
        if (used == 0) {
            return 0;
        }
        s += used;
        nv++;
    }
    return *s == '.';
}

int texpack_ParseName(const char *fileName, TexpackName *out)
{
    uint64_t v[5];
    TexpackName n;
    int region = 1;

    if (fileName == NULL || out == NULL) {
        return -1;
    }
    memset(&n, 0, sizeof(n));
    /* ParseReplacementName's order */
    if (scanFormat(fileName, "H-H-rUxU-B", v)) {
        n.tex0Hash = v[0], n.clutHash = v[1], n.regionW = (uint32_t)v[2];
        n.regionH = (uint32_t)v[3], n.bits = (uint32_t)v[4], n.hasClut = 1;
    } else if (scanFormat(fileName, "H-rUxU-B", v)) {
        n.tex0Hash = v[0], n.regionW = (uint32_t)v[1], n.regionH = (uint32_t)v[2];
        n.bits = (uint32_t)v[3];
    } else if (scanFormat(fileName, "H-H-rH-B", v)) {
        /* the old region form: SourceRegion bits, s16 min/max per axis */
        n.tex0Hash = v[0], n.clutHash = v[1], n.bits = (uint32_t)v[3], n.hasClut = 1;
        n.regionW = (uint32_t)((int16_t)(v[2] >> 16) - (int16_t)v[2]);
        n.regionH = (uint32_t)((int16_t)(v[2] >> 48) - (int16_t)(v[2] >> 32));
    } else if (scanFormat(fileName, "H-rH-B", v)) {
        n.tex0Hash = v[0], n.bits = (uint32_t)v[2];
        n.regionW = (uint32_t)((int16_t)(v[1] >> 16) - (int16_t)v[1]);
        n.regionH = (uint32_t)((int16_t)(v[1] >> 48) - (int16_t)(v[1] >> 32));
    } else if (scanFormat(fileName, "H-H-B", v)) {
        n.tex0Hash = v[0], n.clutHash = v[1], n.bits = (uint32_t)v[2], n.hasClut = 1;
        region = 0;
    } else if (scanFormat(fileName, "H-B", v)) {
        n.tex0Hash = v[0], n.bits = (uint32_t)v[1];
        region = 0;
    } else {
        return -1;
    }
    n.bits &= ~(1u << 14); /* RemoveUnusedBits: the old TCC bit */
    *out = n;
    return region;
}
