/*
 * port/platform/icon_stamp.c
 *
 * The exe's icon resources from an .ico file, the stamp text and the
 * command-line quoting (icon_stamp.h).  The formats:
 *
 *   ICONDIR         reserved 0, type 1, count                     6 bytes
 *   ICONDIRENTRY    width, height (0 = 256), colours, reserved,
 *                   planes, bit count, bytes in resource,
 *                   file offset (4 bytes)                         16 bytes
 *   GRPICONDIR      the same 6 bytes
 *   GRPICONDIRENTRY the ICONDIRENTRY's first 12 bytes, then the
 *                   RT_ICON id (2 bytes) in place of the offset   14 bytes
 *
 * all little-endian and packed.  An RT_ICON resource is the image's bytes
 * exactly as the .ico holds them.
 */
#include "icon_stamp.h"

#include <stdio.h>
#include <string.h>

static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int refuse(const char **why, const char *text)
{
    if (why != NULL) {
        *why = text;
    }
    return -1;
}

int ico_icon_stamp_build(const uint8_t *ico, size_t n, IcoIconStampResources *out, const char **why)
{
    static const uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (out == NULL) {
        return refuse(why, "no output");
    }
    memset(out, 0, sizeof(*out));
    if (ico == NULL || n < 6) {
        return refuse(why, "shorter than an icon directory");
    }
    if (rd16(ico) != 0 || rd16(ico + 2) != 1) {
        return refuse(why, "not an icon file (reserved 0, type 1)");
    }
    const unsigned count = rd16(ico + 4);
    if (count == 0) {
        return refuse(why, "no images");
    }
    if (count > ICO_ICON_STAMP_MAX) {
        return refuse(why, "more images than an exe's icon takes here (32)");
    }
    if (n < 6 + 16 * (size_t)count) {
        return refuse(why, "the directory runs past the file's end");
    }
    uint8_t *g = out->group;
    memcpy(g, ico, 6); /* reserved, type, count */
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = ico + 6 + 16 * (size_t)i;
        const uint32_t bytes = rd32(e + 8);
        const uint32_t at = rd32(e + 12);
        if (bytes == 0) {
            return refuse(why, "an empty image");
        }
        if (at < 6 + 16 * (size_t)count || at > n || bytes > n - at) {
            return refuse(why, "an image runs past the file's end");
        }
        IcoIconStampImage *im = &out->image[i];
        im->data = ico + at;
        im->size = bytes;
        im->id = (uint16_t)(i + 1);
        im->width = (uint16_t)(e[0] != 0 ? e[0] : 256);
        im->height = (uint16_t)(e[1] != 0 ? e[1] : 256);
        im->bitCount = (uint16_t)rd16(e + 6);
        im->png = bytes >= 8 && memcmp(im->data, pngSig, 8) == 0;
        uint8_t *ge = g + 6 + 14 * (size_t)i;
        memcpy(ge, e, 12); /* width .. bytes in resource */
        le16(ge + 12, im->id);
    }
    out->count = (int)count;
    out->groupSize = 6 + 14 * (size_t)count;
    return 0;
}

int ico_icon_stamp_text(char *out, size_t size, const char *label, const char *disc)
{
    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (disc == NULL || disc[0] == '\0') {
        return -1;
    }
    const char *l = label != NULL && label[0] != '\0' ? label : "dev";
    const int r = snprintf(out, size, "%s %s", l, disc);
    if (r < 0 || (size_t)r >= size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

int ico_icon_stamp_read(char *out, size_t size, const uint8_t *data, size_t n)
{
    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (data == NULL || n == 0) {
        return -1;
    }
    size_t len = 0;
    while (len < n && data[len] != '\0' && data[len] != '\r' && data[len] != '\n') {
        len++;
    }
    if (len >= size) {
        return -1;
    }
    memcpy(out, data, len);
    out[len] = '\0';
    return 0;
}

/* one byte onto out at *at, when it fits with the terminator */
static int put(char *out, size_t size, size_t *at, char c)
{
    if (*at + 1 >= size) {
        return -1;
    }
    out[(*at)++] = c;
    return 0;
}

int ico_icon_stamp_quote(char *out, size_t size, size_t *used, const char *arg)
{
    if (out == NULL || used == NULL || arg == NULL || *used >= size) {
        return -1;
    }
    size_t at = *used;
    int ok = (at == 0 || put(out, size, &at, ' ') == 0) && put(out, size, &at, '"') == 0;
    for (const char *p = arg; ok && *p != '\0';) {
        size_t slashes = 0;
        while (p[slashes] == '\\') {
            slashes++;
        }
        const char next = p[slashes];
        /* backslashes are literal unless a quote follows: then each is
           doubled (the quote's own escape adds one more) */
        const size_t copies = next == '"' || next == '\0' ? slashes * 2 : slashes;
        for (size_t k = 0; ok && k < copies; k++) {
            ok = put(out, size, &at, '\\') == 0;
        }
        p += slashes;
        if (next == '\0') {
            break;
        }
        if (next == '"') {
            ok = ok && put(out, size, &at, '\\') == 0;
        }
        ok = ok && put(out, size, &at, *p) == 0;
        p++;
    }
    ok = ok && put(out, size, &at, '"') == 0;
    if (!ok) {
        out[*used] = '\0';
        return -1;
    }
    out[at] = '\0';
    *used = at;
    return 0;
}

/* --- the PE check ------------------------------------------------------------ */

typedef struct Pe {
    const uint8_t *p;
    size_t n;
    unsigned machine;
    unsigned sections;
    const uint8_t *opt; /* the optional header */
    const uint8_t *sec; /* the section table */
    unsigned dirs;      /* NumberOfRvaAndSizes */
    const uint8_t *dir; /* the data directories */
    uint64_t imageBase;
    uint32_t entry;
    uint32_t sizeOfImage;
} Pe;

static int pe_parse(const uint8_t *p, size_t n, Pe *pe)
{
    memset(pe, 0, sizeof(*pe));
    if (p == NULL || n < 0x40 || p[0] != 'M' || p[1] != 'Z') {
        return -1;
    }
    const uint32_t at = rd32(p + 0x3C);
    if (at > n || n - at < 24 || memcmp(p + at, "PE\0\0", 4) != 0) {
        return -1;
    }
    const uint8_t *coff = p + at + 4;
    const unsigned optSize = rd16(coff + 16);
    pe->machine = rd16(coff);
    pe->sections = rd16(coff + 2);
    pe->opt = coff + 20;
    if ((size_t)(pe->opt - p) + optSize > n || optSize < 24) {
        return -1;
    }
    const unsigned magic = rd16(pe->opt);
    size_t dirAt;
    if (magic == 0x20B && optSize >= 112) { /* PE32+ */
        pe->imageBase = (uint64_t)rd32(pe->opt + 24) | (uint64_t)rd32(pe->opt + 28) << 32;
        pe->dirs = rd32(pe->opt + 108);
        dirAt = 112;
    } else if (magic == 0x10B && optSize >= 96) { /* PE32 */
        pe->imageBase = rd32(pe->opt + 28);
        pe->dirs = rd32(pe->opt + 92);
        dirAt = 96;
    } else {
        return -1;
    }
    pe->entry = rd32(pe->opt + 16);
    pe->sizeOfImage = rd32(pe->opt + 56);
    if (pe->dirs > 16 || dirAt + 8 * (size_t)pe->dirs > optSize) {
        return -1;
    }
    pe->dir = pe->opt + dirAt;
    pe->sec = pe->opt + optSize;
    if ((size_t)(pe->sec - p) + 40 * (size_t)pe->sections > n || pe->sections == 0) {
        return -1;
    }
    pe->p = p;
    pe->n = n;
    return 0;
}

/* data directory i: rva and size (0, 0 when absent) */
static void pe_dir(const Pe *pe, unsigned i, uint32_t *rva, uint32_t *size)
{
    *rva = i < pe->dirs ? rd32(pe->dir + 8 * i) : 0;
    *size = i < pe->dirs ? rd32(pe->dir + 8 * i + 4) : 0;
}

int ico_icon_stamp_pe_check(const uint8_t *a, size_t an, const uint8_t *b, size_t bn,
                            const char **why)
{
    Pe x, y;
    if (pe_parse(a, an, &x) != 0) {
        return refuse(why, "the program is not a PE file");
    }
    if (pe_parse(b, bn, &y) != 0) {
        return refuse(why, "the stamped copy is not a PE file");
    }
    if (x.machine != y.machine || x.imageBase != y.imageBase || x.entry != y.entry) {
        return refuse(why, "the machine, image base or entry point changed");
    }
    uint32_t rsrcA, sizeA, rsrcB, sizeB;
    pe_dir(&x, 2, &rsrcA, &sizeA);
    pe_dir(&y, 2, &rsrcB, &sizeB);
    if (rsrcB == 0 || sizeB == 0) {
        return refuse(why, "the stamped copy has no resources");
    }
    static const unsigned kept[] = {1, 3, 9, 12}; /* import, exception, TLS, IAT */
    for (size_t k = 0; k < sizeof(kept) / sizeof(kept[0]); k++) {
        uint32_t ra, sa, rb, sb;
        pe_dir(&x, kept[k], &ra, &sa);
        pe_dir(&y, kept[k], &rb, &sb);
        if (ra != rb || sa != sb) {
            return refuse(why, "an import, exception or TLS directory moved");
        }
    }
    for (unsigned i = 0; i < y.sections; i++) {
        const uint8_t *s = y.sec + 40 * (size_t)i;
        const uint32_t va = rd32(s + 12), vsize = rd32(s + 8);
        const uint32_t raw = rd32(s + 16), at = rd32(s + 20);
        if ((raw != 0 && (at > bn || raw > bn - at)) ||
            (uint64_t)va + (vsize > raw ? vsize : raw) > y.sizeOfImage) {
            return refuse(why, "a section of the stamped copy lies outside the file or the image");
        }
    }
    for (unsigned i = 0; i < x.sections; i++) {
        const uint8_t *s = x.sec + 40 * (size_t)i;
        const uint32_t va = rd32(s + 12);
        if (rsrcA != 0 && va >= rsrcA) {
            continue; /* the resource section and the ones after it may move */
        }
        if (i >= y.sections) {
            return refuse(why, "a section before the resources is missing");
        }
        const uint8_t *t = y.sec + 40 * (size_t)i;
        if (memcmp(s, t, 8) != 0 || memcmp(s + 8, t + 8, 12) != 0 || rd32(s + 36) != rd32(t + 36)) {
            return refuse(why, "a section before the resources changed its name, place or flags");
        }
        const uint32_t raw = rd32(s + 16), atA = rd32(s + 20), atB = rd32(t + 20);
        if (raw == 0) {
            continue;
        }
        if (atA > an || raw > an - atA) {
            return refuse(why, "a section of the program lies outside its file");
        }
        if (memcmp(a + atA, b + atB, raw) != 0) {
            return refuse(why, "a section before the resources changed its bytes");
        }
    }
    return 0;
}
