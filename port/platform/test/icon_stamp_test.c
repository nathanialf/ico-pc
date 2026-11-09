/*
 * port/platform/test/icon_stamp_test.c
 *
 * icon_stamp.c, the parts of tools/ico_icon_stamp.c with no Windows call:
 * an .ico made by the capture's writer (ico_icon_write, app_icon_ico.c, on
 * a 256 x 256 RGBA test image) into the exe's resources: four RT_ICON
 * images (ids 1..4, each the .ico image's bytes in place: the 16/32/48 BMP
 * headers, the 256 PNG signature) and the RT_GROUP_ICON directory (6 + 4 x
 * 14 bytes: each entry the .ico entry's first 12 bytes and its id); the
 * refusals of a bad file; the stamp text written and read back as the
 * resource bytes hold it; the command-line quoting split back by the rules
 * CommandLineToArgvW and the C runtime use; the stamped copy's PE check on
 * a small PE32+ laid out as the game's exe (.text, .rsrc, .reloc), passing
 * with .rsrc grown and .reloc moved, refusing a changed code byte, entry
 * point, import directory, section name or flags, a missing resource
 * directory, a cut file, a short SizeOfImage, another machine, a file that
 * is not PE.  The resource update itself
 * (BeginUpdateResourceW ... EndUpdateResourceW) runs only on Windows.
 * argv[1]: a folder the test may write in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h"
#include "host_fs.h"
#include "icon_stamp.h"

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
    *n = 0;
    if (f == NULL) {
        return NULL;
    }
    uint8_t *buf = malloc(1 << 20);
    if (buf != NULL) {
        *n = fread(buf, 1, 1 << 20, f);
    }
    fclose(f);
    return buf;
}

/* the C runtime's split of one argument at *p (the rules of
   CommandLineToArgvW for every argument after the program's): 2n
   backslashes before a quote give n and the quote opens or closes, 2n+1
   give n and a literal quote, others are literal */
static int split_one(const char **p, char *out, size_t size)
{
    const char *s = *p;
    size_t k = 0;
    int quoted = 0;
    while (*s == ' ') {
        s++;
    }
    if (*s == '\0') {
        return -1;
    }
    while (*s != '\0' && (quoted || *s != ' ')) {
        size_t slashes = 0;
        while (s[slashes] == '\\') {
            slashes++;
        }
        if (s[slashes] == '"') {
            for (size_t i = 0; i < slashes / 2 && k + 1 < size; i++) {
                out[k++] = '\\';
            }
            if (slashes % 2 == 1) {
                if (k + 1 < size) {
                    out[k++] = '"';
                }
            } else {
                quoted = !quoted;
            }
            s += slashes + 1;
        } else if (slashes > 0) {
            for (size_t i = 0; i < slashes && k + 1 < size; i++) {
                out[k++] = '\\';
            }
            s += slashes;
        } else {
            if (k + 1 < size) {
                out[k++] = *s;
            }
            s++;
        }
    }
    out[k] = '\0';
    *p = s;
    return 0;
}

static void test_build(const char *folder)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/icon_stamp_test.ico", folder);
    /* a letter-like block: red over clear */
    uint8_t *img = calloc(256 * 256, 4);
    for (int y = 32; y < 224; y++) {
        for (int x = 96; x < 160; x++) {
            uint8_t *p = img + ((size_t)y * 256 + x) * 4;
            p[0] = 200;
            p[3] = 255;
        }
    }
    uint8_t png[400];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(png, sig, 8);
    for (size_t i = 8; i < sizeof(png); i++) {
        png[i] = (uint8_t)(i * 13);
    }
    CHECK(ico_icon_write(path, png, sizeof(png), img) == 0);
    free(img);
    size_t n = 0;
    uint8_t *ico = slurp(path, &n);
    CHECK(ico != NULL && n > 6 + 16 * 4);
    if (ico == NULL) {
        return;
    }

    static IcoIconStampResources r;
    const char *why = NULL;
    CHECK(ico_icon_stamp_build(ico, n, &r, &why) == 0);
    CHECK(r.count == 4);
    CHECK(r.groupSize == 6 + 14 * 4);
    CHECK(rd16(r.group) == 0 && rd16(r.group + 2) == 1 && rd16(r.group + 4) == 4);
    static const int sizes[4] = {16, 32, 48, 256};
    size_t end = 6 + 16 * 4;
    for (int i = 0; i < 4 && r.count == 4; i++) {
        const uint8_t *e = ico + 6 + 16 * i;
        const uint8_t *g = r.group + 6 + 14 * i;
        const IcoIconStampImage *im = &r.image[i];
        /* the group entry: the .ico entry's first 12 bytes, then the id */
        CHECK(memcmp(g, e, 12) == 0);
        CHECK(rd16(g + 12) == (unsigned)(i + 1));
        CHECK(im->id == i + 1);
        CHECK(im->width == sizes[i] && im->height == sizes[i]);
        CHECK(im->bitCount == 32);
        CHECK(rd32(g + 8) == im->size);
        /* the RT_ICON payload is the .ico image in place */
        CHECK(im->data == ico + rd32(e + 12));
        CHECK(im->size == rd32(e + 8));
        CHECK(rd32(e + 12) == end); /* contiguous, as the writer lays them */
        end += im->size;
        if (sizes[i] < 256) {
            const size_t s = (size_t)sizes[i];
            CHECK(!im->png);
            CHECK(rd32(im->data) == 40);        /* BITMAPINFOHEADER */
            CHECK(rd32(im->data + 8) == 2 * s); /* the XOR bitmap and the mask */
            CHECK(im->size == 40 + s * s * 4 + ((s + 31) / 32) * 4 * s);
        } else {
            CHECK(im->png);
            CHECK(g[0] == 0 && g[1] == 0); /* 256 is written 0 */
            CHECK(im->size == sizeof(png));
            CHECK(memcmp(im->data, png, sizeof(png)) == 0);
        }
    }
    CHECK(end == n);

    /* refusals: each names why */
    uint8_t *bad = malloc(n);
    memcpy(bad, ico, n);
    bad[2] = 2; /* a cursor file */
    why = NULL;
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1 && why != NULL && r.count == 0);
    memcpy(bad, ico, n);
    bad[4] = 0; /* no images */
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1);
    bad[4] = 33; /* more than an exe's group takes */
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1);
    memcpy(bad, ico, n);
    CHECK(ico_icon_stamp_build(bad, 6 + 16 * 3, &r, &why) == -1); /* the directory cut */
    CHECK(ico_icon_stamp_build(bad, n - 1, &r, &why) == -1);      /* the PNG cut */
    memset(bad + 6 + 16 * 3 + 8, 0, 4);                           /* an empty image */
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1);
    memcpy(bad, ico, n);
    bad[6 + 16 * 0 + 12] = 4; /* an offset inside the directory */
    bad[6 + 16 * 0 + 13] = 0;
    bad[6 + 16 * 0 + 14] = 0;
    bad[6 + 16 * 0 + 15] = 0;
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1);
    memcpy(bad, ico, n);
    bad[6 + 16 * 3 + 15] = 0x80; /* an offset far past the end */
    CHECK(ico_icon_stamp_build(bad, n, &r, &why) == -1);
    CHECK(ico_icon_stamp_build(NULL, 0, &r, &why) == -1);
    CHECK(ico_icon_stamp_build(ico, 5, &r, &why) == -1);
    CHECK(ico_icon_stamp_build(ico, n, NULL, &why) == -1);
    free(bad);
    free(ico);
    (void)ico_remove(path);
}

static void test_stamp(void)
{
    char s[64], back[64];
    CHECK(ico_icon_stamp_text(s, sizeof(s), "v0.11.1", "SCES-50760") == 0);
    CHECK(strcmp(s, "v0.11.1 SCES-50760") == 0);
    /* the resource holds the bytes without a terminator; read back equal */
    uint8_t res[64];
    const size_t n = strlen(s);
    memcpy(res, s, n);
    CHECK(ico_icon_stamp_read(back, sizeof(back), res, n) == 0 && strcmp(back, s) == 0);
    /* a line end or NUL ends it (a .txt read raw) */
    memcpy(res + n, "\r\nmore", 6);
    CHECK(ico_icon_stamp_read(back, sizeof(back), res, n + 6) == 0 && strcmp(back, s) == 0);
    res[n] = '\0';
    CHECK(ico_icon_stamp_read(back, sizeof(back), res, n + 1) == 0 && strcmp(back, s) == 0);
    CHECK(ico_icon_stamp_read(back, 5, res, n) == -1 && back[0] == '\0');
    CHECK(ico_icon_stamp_read(back, sizeof(back), NULL, 3) == -1);
    CHECK(ico_icon_stamp_read(back, sizeof(back), res, 0) == -1);
    /* a development build's icon */
    CHECK(ico_icon_stamp_text(s, sizeof(s), "", "SCPS-11003") == 0 &&
          strcmp(s, "dev SCPS-11003") == 0);
    CHECK(ico_icon_stamp_text(s, sizeof(s), NULL, "SCPS-11003") == 0 &&
          strcmp(s, "dev SCPS-11003") == 0);
    CHECK(ico_icon_stamp_text(s, sizeof(s), "v0.11.1", "") == -1 && s[0] == '\0');
    CHECK(ico_icon_stamp_text(s, sizeof(s), "v0.11.1", NULL) == -1);
    CHECK(ico_icon_stamp_text(s, 8, "v0.11.1", "SCES-50760") == -1 && s[0] == '\0');
}

static void test_quote(void)
{
    static const char *const args[] = {
        "C:\\Games\\ico pc\\x64\\tools\\ico_icon_stamp.exe",
        "C:\\Games\\ico pc\\x64\\ico_pc_x64.exe",
        "C:\\Users\\n\\AppData\\Roaming\\ico-pc\\ico-pc\\icon\\ico-pc-SCES-50760.ico",
        "v0.11.1 SCES-50760",
        "C:\\trailing\\",
        "a\"b",
        "a\\\"b",
        "",
        "\\\\server\\share\\x.log",
    };
    const int count = (int)(sizeof(args) / sizeof(args[0]));
    char cmd[2048];
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        CHECK(ico_icon_stamp_quote(cmd, sizeof(cmd), &used, args[i]) == 0);
    }
    CHECK(used == strlen(cmd));
    static const char head[] = "\"C:\\Games\\ico pc\\x64\\tools\\ico_icon_stamp.exe\" \"C:";
    CHECK(strncmp(cmd, head, sizeof(head) - 1) == 0);
    const char *p = cmd;
    char one[512];
    for (int i = 0; i < count; i++) {
        CHECK(split_one(&p, one, sizeof(one)) == 0);
        if (strcmp(one, args[i]) != 0) {
            printf("FAIL quote %d: \"%s\" came back \"%s\"\n", i, args[i], one);
            fails++;
        }
    }
    CHECK(split_one(&p, one, sizeof(one)) == -1);

    /* exact forms */
    used = 0;
    CHECK(ico_icon_stamp_quote(cmd, sizeof(cmd), &used, "C:\\a\\") == 0 &&
          strcmp(cmd, "\"C:\\a\\\\\"") == 0);
    used = 0;
    CHECK(ico_icon_stamp_quote(cmd, sizeof(cmd), &used, "a\\\"b") == 0 &&
          strcmp(cmd, "\"a\\\\\\\"b\"") == 0);
    used = 0;
    CHECK(ico_icon_stamp_quote(cmd, sizeof(cmd), &used, "x") == 0 &&
          ico_icon_stamp_quote(cmd, sizeof(cmd), &used, "y") == 0 &&
          strcmp(cmd, "\"x\" \"y\"") == 0);

    /* too long: the line so far kept */
    char small[12];
    used = 0;
    CHECK(ico_icon_stamp_quote(small, sizeof(small), &used, "abc") == 0 && used == 5);
    CHECK(ico_icon_stamp_quote(small, sizeof(small), &used, "defghij") == -1);
    CHECK(used == 5 && strcmp(small, "\"abc\"") == 0);
    CHECK(ico_icon_stamp_quote(small, sizeof(small), &used, "de") == 0 &&
          strcmp(small, "\"abc\" \"de\"") == 0);
    CHECK(ico_icon_stamp_quote(small, sizeof(small), NULL, "x") == -1);
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, v & 0xFFFF);
    wr16(p + 2, v >> 16);
}

/* one section header: name, virtual size and address, raw size and offset,
   flags; the raw bytes filled with a pattern of seed */
static void pe_section(uint8_t *f, int i, const char *name, uint32_t vsize, uint32_t va,
                       uint32_t raw, uint32_t at, uint32_t flags, unsigned seed)
{
    uint8_t *s = f + 0x188 + 40 * i;
    memset(s, 0, 40);
    memcpy(s, name, strlen(name));
    wr32(s + 8, vsize);
    wr32(s + 12, va);
    wr32(s + 16, raw);
    wr32(s + 20, at);
    wr32(s + 36, flags);
    for (uint32_t k = 0; k < raw; k++) {
        f[at + k] = (uint8_t)(k * seed + seed);
    }
}

/* a small PE32+ program as the game's exe is laid out (.text, .rsrc, then
   .reloc after it); stamped: .rsrc grown to 0x1200 bytes and .reloc moved
   after it, as EndUpdateResourceW does.  The file's size. */
static size_t make_pe(uint8_t *f, int stamped)
{
    const uint32_t rsrcRaw = stamped ? 0x1200 : 0x200;
    const uint32_t relocVa = stamped ? 0x4000 : 0x3000;
    const uint32_t relocAt = 0x600 + rsrcRaw;
    const size_t n = relocAt + 0x200;
    memset(f, 0, n);
    f[0] = 'M';
    f[1] = 'Z';
    wr32(f + 0x3C, 0x80);
    memcpy(f + 0x80, "PE\0\0", 4);
    uint8_t *coff = f + 0x84;
    wr16(coff, 0x8664);
    wr16(coff + 2, 3);
    wr16(coff + 16, 240);
    uint8_t *opt = coff + 20; /* 0x98 */
    wr16(opt, 0x20B);
    wr32(opt + 16, 0x1010);     /* the entry point */
    wr32(opt + 24, 0x40000000); /* the image base 0x140000000 */
    wr32(opt + 28, 1);
    wr32(opt + 56, relocVa + 0x1000); /* SizeOfImage */
    wr32(opt + 108, 16);
    uint8_t *dir = opt + 112;
    wr32(dir + 8 * 1, 0x1100); /* imports */
    wr32(dir + 8 * 1 + 4, 0x40);
    wr32(dir + 8 * 2, 0x2000); /* resources */
    wr32(dir + 8 * 2 + 4, rsrcRaw - 0x10);
    wr32(dir + 8 * 5, relocVa); /* relocations */
    wr32(dir + 8 * 5 + 4, 0x10);
    wr32(dir + 8 * 9, 0x1180); /* TLS */
    wr32(dir + 8 * 9 + 4, 0x28);
    pe_section(f, 0, ".text", 0x200, 0x1000, 0x200, 0x400, 0x60000020u, 7);
    pe_section(f, 1, ".rsrc", rsrcRaw, 0x2000, rsrcRaw, 0x600, 0x40000040u, stamped ? 11 : 5);
    pe_section(f, 2, ".reloc", 0x10, relocVa, 0x200, relocAt, 0x42000040u, 3);
    return n;
}

static void test_pe(void)
{
    static uint8_t a[0x4000], b[0x4000];
    const char *why = NULL;
    const size_t an = make_pe(a, 0);
    size_t bn = make_pe(b, 1);
    CHECK(ico_icon_stamp_pe_check(a, an, a, an, &why) == 0);
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == 0);
    /* a byte of the code changed */
    b[0x400 + 17] ^= 1;
    why = NULL;
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1 && why != NULL);
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 16, 0x1020); /* the entry point */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 112 + 8, 0x1104); /* the imports moved */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 112 + 16, 0); /* no resources */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    wr32(b + 0x188 + 36, 0x60000060u); /* the code's flags */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    b[0x188] = 'X'; /* the code section's name */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn - 1, &why) == -1); /* .reloc cut */
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 56, 0x4000); /* SizeOfImage short of .reloc */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    wr16(b + 0x84, 0x14C); /* another machine */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    b[0x80] = 'X'; /* not a PE file */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    CHECK(ico_icon_stamp_pe_check(a, an, NULL, 0, &why) == -1);
    CHECK(ico_icon_stamp_pe_check(a, 0x100, a, an, &why) == -1); /* the program cut */

    /* the sections out of order or overlapping */
    bn = make_pe(b, 1);
    wr32(b + 0x188 + 40 * 2 + 12, 0x2800); /* .reloc inside the grown .rsrc */
    wr32(b + 0x98 + 112 + 8 * 5, 0x2800);
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    /* the relocations outside every section */
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 112 + 8 * 5, 0x3800); /* between .rsrc and .reloc */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    bn = make_pe(b, 1);
    wr32(b + 0x98 + 112 + 8 * 5 + 4, 0x20); /* past .reloc's 0x10 bytes */
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    /* the COFF symbol table pointer: as rc5's stamped exe had it (unchanged
       from the program, so inside the moved sections), refused; zeroed by
       ico_icon_stamp_clear_symbols, accepted; past the sections with the
       symbols in the file, accepted; past the file's end, refused */
    bn = make_pe(b, 1);
    wr32(b + 0x84 + 8, 0x600 + 0x200 + 0x200); /* the program's end of sections */
    wr32(b + 0x84 + 12, 81634);
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == -1);
    CHECK(ico_icon_stamp_clear_symbols(b, 0x400) == 0);
    CHECK(rd32(b + 0x84 + 8) == 0 && rd32(b + 0x84 + 12) == 0);
    CHECK(ico_icon_stamp_pe_check(a, an, b, bn, &why) == 0);
    {
        static uint8_t c[0x4000];
        size_t cn = make_pe(c, 1);
        wr32(c + 0x84 + 8, (uint32_t)cn); /* right after the sections */
        wr32(c + 0x84 + 12, 2);
        CHECK(ico_icon_stamp_pe_check(a, an, c, cn + 40, &why) == 0);
        CHECK(ico_icon_stamp_pe_check(a, an, c, cn + 30, &why) == -1); /* symbols cut */
        wr32(c + 0x84 + 8, (uint32_t)cn + 0x100);
        CHECK(ico_icon_stamp_pe_check(a, an, c, cn + 40, &why) == -1); /* past the end */
    }
    /* the program itself may carry symbols */
    {
        static uint8_t d[0x4000];
        const size_t dn = make_pe(d, 0);
        wr32(d + 0x84 + 8, (uint32_t)dn);
        wr32(d + 0x84 + 12, 1);
        bn = make_pe(b, 1);
        CHECK(ico_icon_stamp_pe_check(d, dn + 18, b, bn, &why) == 0);
    }
    CHECK(ico_icon_stamp_clear_symbols(b, 0x20) == -1);
    b[0] = 'X';
    CHECK(ico_icon_stamp_clear_symbols(b, 0x400) == -1);
    CHECK(ico_icon_stamp_clear_symbols(NULL, 0x400) == -1);
}

/* the helper's refusal line and the game's reading of it */
static void test_refusal(void)
{
    char out[64];
    CHECK(ico_icon_stamp_refusal(out, sizeof(out), "v0.11.1 SCES-50760") == 0 &&
          strcmp(out, "refused stamp \"v0.11.1 SCES-50760\":") == 0);
    CHECK(ico_icon_stamp_refusal(out, sizeof(out), "") == -1 && out[0] == '\0');
    CHECK(ico_icon_stamp_refusal(out, 8, "v0.11.1 SCES-50760") == -1 && out[0] == '\0');
    const char *line = "2026-11-09 10:00:00 ico_icon_stamp: refused stamp \"v0.11.1 SCES-50760\": "
                       "the stamped copy is not the same program (x); the game does not try "
                       "this icon again | exe C:\\g\\ico_pc_x64.exe | icon C:\\u\\a.ico";
    CHECK(ico_icon_stamp_refused(line, "v0.11.1 SCES-50760") == 1);
    CHECK(ico_icon_stamp_refused(line, "v0.11.2 SCES-50760") == 0); /* a new icon */
    CHECK(ico_icon_stamp_refused(line, "v0.11.1 SCES-5076") == 0);
    CHECK(ico_icon_stamp_refused(line, "v0.11.1 SCPS-15101") == 0); /* another disc */
    CHECK(ico_icon_stamp_refused("2026-11-09 10:00:00 ico_icon_stamp: not stamped: the program "
                                 "file was still in use after 10000 ms | exe x | icon y",
                                 "v0.11.1 SCES-50760") == 0);
    CHECK(ico_icon_stamp_refused("2026-11-09 10:00:00 ico_icon_stamp: stamped: 4 images (1 PNG), "
                                 "stamp \"v0.11.1 SCES-50760\"",
                                 "v0.11.1 SCES-50760") == 0);
    CHECK(ico_icon_stamp_refused("", "v0.11.1 SCES-50760") == 0);
    CHECK(ico_icon_stamp_refused(NULL, "v0.11.1 SCES-50760") == 0);
    CHECK(ico_icon_stamp_refused(line, "") == 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: icon_stamp_test <folder>\n");
        return 2;
    }
    test_build(argv[1]);
    test_stamp();
    test_quote();
    test_pe();
    test_refusal();
    if (fails) {
        printf("icon_stamp_test: %d failures\n", fails);
        return 1;
    }
    printf("icon_stamp_test: ok\n");
    return 0;
}
