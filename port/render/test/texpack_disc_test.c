/* texpack_disc_test: the PCSX2 names of every texture on the disc, and how
 * many of a texture pack's files they reach.
 *
 *   texpack_disc_test DISC [PACKDIR]
 *
 * DISC is the PAL image (exit 77 when it cannot be opened).  Every *.tm2
 * member of DATA.DF's packs (df_pack.h) is parsed as Texture.c's
 * tex_makeTexturePacket and texHostTexture read it (the levels, the CLUT
 * turned into CSM1 order when the file holds it in index order) and given
 * its candidate names (texpack_candidates) for every level the game can
 * bind, and so is every TIM2 record of the subtitle files (*.jim, loose
 * and in the packs).  They go to texpack_names.txt in the working directory, one line
 * per texture:
 *
 *   member type clutType WxH levels unstable : name@startLevel[/chain] ...
 *
 * with per-type counts on stdout.  PACKDIR (or ICO_TEXPACK_DIR) is a
 * PCSX2 replacements folder, read recursively: its file names are parsed
 * (texpack_parse_name) and matched against the disc's names, and the test
 * prints how many of the pack's files match a texture and how many
 * textures have a file.  Nothing is written to the disc or the pack.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "df_pack.h"
#include "rd_tex.h"
#include "texpack_name.h"
#include "vfs.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

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

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

/* Texture.c getTWTH: the smallest n with 2^n >= size, -1 past 1024 */
static int twth(uint32_t size)
{
    for (int i = 0; i < 11; i++) {
        if ((1u << i) >= size) {
            return i;
        }
    }
    return -1;
}

/* Texture.c texTBW: 64-texel units, PSMT8/PSMT4 rounded up to even */
static uint32_t tbwOf(uint32_t psm, uint32_t w)
{
    uint32_t n = (w + 63) >> 6;
    return psm == RDTEX_PSMT8 || psm == RDTEX_PSMT4 ? n + (n & 1) : n;
}

/* rd_tex.c rdtex_image_bytes for the five TIM2 formats (rd_tex.c is in
   ico_render, which needs a device library; this test does not) */
static size_t imageBytes(uint32_t psm, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * h;
    switch (psm) {
    case RDTEX_PSMCT32:
        return n * 4;
    case RDTEX_PSMCT24:
        return n * 3;
    case RDTEX_PSMCT16:
        return n * 2;
    case RDTEX_PSMT8:
        return n;
    default:
        return (n + 1) / 2;
    }
}

/* Texture.c tex_convertClutCSM2ToCSM1 (rd_tex.c rdtex_clut_to_csm1) */
static void clutToCsm1(uint8_t *clut, uint32_t entry)
{
    uint8_t tmp[256 * 4];
    memcpy(tmp, clut, 256 * entry);
    for (uint32_t i = 0; i < 256; i++) {
        memcpy(clut + rdtex_csm1_index(i, 256) * entry, tmp + i * entry, entry);
    }
}

/* ------------------------------------------------------------ names */

typedef struct Key {
    uint64_t tex0, clut;
    uint32_t bits;
    int tex;  /* the texture (disc) or file (pack) it came from */
    int kind; /* disc: 0 level 0 alone, 1 the chain from 0, 2 a level k > 0
                 alone, 3 a chain from k > 0 */
} Key;

static int keyCmp(const void *a, const void *b)
{
    const Key *x = a;
    const Key *y = b;
    if (x->tex0 != y->tex0) {
        return x->tex0 < y->tex0 ? -1 : 1;
    }
    if (x->clut != y->clut) {
        return x->clut < y->clut ? -1 : 1;
    }
    if (x->bits != y->bits) {
        return x->bits < y->bits ? -1 : 1;
    }
    return 0;
}

typedef struct KeyList {
    Key *k;
    int n, cap;
} KeyList;

static void keyAdd(KeyList *l, uint64_t tex0, uint64_t clut, uint32_t bits, int tex, int kind)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 1024;
        l->k = realloc(l->k, (size_t)l->cap * sizeof(Key));
        if (l->k == NULL) {
            printf("texpack_disc_test: out of memory\n");
            exit(1);
        }
    }
    l->k[l->n++] = (Key){tex0, clut, bits, tex, kind};
}

/* the first entry of sorted l equal to k, or -1 */
static int keyFind(const KeyList *l, const Key *k)
{
    int lo = 0;
    int hi = l->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (keyCmp(&l->k[mid], k) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < l->n && keyCmp(&l->k[lo], k) == 0 ? lo : -1;
}

/* ------------------------------------------------------------ the disc */

enum { TYPE_COUNT = 6 };

static const char *const kType[TYPE_COUNT] = {"NONE",    "PSMCT16", "PSMCT24",
                                              "PSMCT32", "PSMT4",   "PSMT8"};

typedef struct Stats {
    int textures, unstable, mipmapped, nonPow2, clut24, clut16x256, odd, scroll;
} Stats;

static Stats s_type[TYPE_COUNT];
static int s_tim2, s_bad, s_refused, s_names;
static KeyList s_disc;    /* every candidate, tex = the texture's number */
static char **s_texName;  /* member name per texture number */
static char *s_texScroll; /* per texture number: its CLUT scrolls (Texture.c tex_scrollClut) */
static int s_texCount;

/* one TIM2 member: its candidates into s_disc and a line of f */
static void doTexture(const char *member, const uint8_t *d, size_t size, FILE *f)
{
    static const uint32_t psmOf[TYPE_COUNT] = {
        0, RDTEX_PSMCT16, RDTEX_PSMCT24, RDTEX_PSMCT32, RDTEX_PSMT4, RDTEX_PSMT8};
    uint8_t clut[256 * 4];
    TexpackSource src;
    TexpackName names[TEXPACK_MAX_CANDIDATES];

    if (size < 16 + 0x30 || memcmp(d, "TIM2", 4) != 0) {
        s_bad++;
        printf("  %s: not a TIM2 file\n", member);
        return;
    }
    const uint8_t *pic = d + 16;
    uint32_t clutSize = rd32(pic + 0x04);
    uint32_t imageSize = rd32(pic + 0x08);
    uint32_t headerSize = rd16(pic + 0x0C);
    uint32_t clutColors = rd16(pic + 0x0E);
    uint32_t levels = pic[0x11];
    uint32_t clutType = pic[0x12];
    uint32_t type = pic[0x13];
    uint32_t w = rd16(pic + 0x14);
    uint32_t h = rd16(pic + 0x16);
    const uint8_t *image = pic + headerSize;

    if (type < 1 || type >= TYPE_COUNT || levels < 1 || levels > TEXPACK_MAX_LEVELS ||
        twth(w) < 0 || twth(h) < 0 || 16 + (size_t)headerSize + imageSize + clutSize > size) {
        s_bad++;
        printf("  %s: TIM2 header out of range (type %u, %u levels, %ux%u)\n", member, type, levels,
               w, h);
        return;
    }
    s_tim2++;
    Stats *st = &s_type[type];
    st->textures++;
    if (levels > 1) {
        st->mipmapped++;
    }
    if (w != (1u << twth(w)) || h != (1u << twth(h))) {
        st->nonPow2++;
    }

    memset(&src, 0, sizeof(src));
    src.psm = psmOf[type];
    src.levels = levels;
    size_t at = 0;
    for (uint32_t i = 0; i < levels; i++) {
        TexpackLevel *l = &src.lv[i];
        size_t bytes = levels == 1 ? imageSize : rd32(pic + 0x30 + 0x10 + 4 * i);
        l->tw = (uint8_t)(twth(w) - (int)i);
        l->th = (uint8_t)(twth(h) - (int)i);
        l->w = w >> i;
        l->h = h >> i;
        l->tbw = tbwOf(src.psm, l->w);
        l->pixels = image + at;
        CHECK(l->tbw * 64 >= (1u << l->tw), "%s level %u: TBW %u covers 2^%u", member, i, l->tbw,
              l->tw);
        CHECK(bytes >= imageBytes(src.psm, l->w, l->h) && at + bytes <= imageSize,
              "%s level %u: %zu bytes for %ux%u", member, i, bytes, l->w, l->h);
        if (at + bytes > imageSize || bytes < imageBytes(src.psm, l->w, l->h)) {
            return;
        }
        at += bytes;
    }

    if (type == 4 || type == 5) {
        uint32_t entry = (clutType & 0x3F) == 1 ? 2 : (clutType & 0x3F) == 2 ? 3 : 4;
        src.cpsm = entry == 2 ? RDTEX_PSMCT16 : entry == 3 ? RDTEX_PSMCT24 : RDTEX_PSMCT32;
        if (src.cpsm == RDTEX_PSMCT24) {
            st->clut24++;
        }
        if (entry == 2 && clutColors == 256) {
            st->clut16x256++;
        }
        /* tex_transVramClutTex uploads an 8x2 image for 16 entries, else
           16x16 (whatever the file holds) */
        src.clutColors = clutColors == 16 ? 16 : 256;
        if (clutColors != 16 && clutColors != 256) {
            st->odd++;
        }
        memset(clut, 0, sizeof(clut));
        memcpy(clut, image + imageSize, clutSize < sizeof(clut) ? clutSize : sizeof(clut));
        /* tex_convertClutCSM2ToCSM1 */
        if ((clutType >> 7) != 0 && clutColors == 256) {
            clutToCsm1(clut, entry);
        }
        src.clut = clut;
    }

    /* the ICO block after the headers (tex_makeTexturePacket): a CLUT
       scroll (csSpd, csStp, csBgn != csEnd: tex_textureAnimation) rewrites
       the CLUT while the game runs, so the names of the frames after the
       first cannot be made from the disc */
    static const uint32_t kMipHeader[8] = {0, 0, 32, 32, 32, 48, 48, 48};
    const uint8_t *ext = pic + 0x30 + kMipHeader[levels];
    int scroll = ext + 0x24 <= image && memcmp(ext, "ICO", 4) == 0 && rd32(ext + 0x1C) != 0 &&
                 rd32(ext + 0x20) != 0 && rd32(ext + 0x14) != rd32(ext + 0x18) &&
                 (type == 4 || type == 5);
    st->scroll += scroll;

    int tex = s_texCount++;
    s_texScroll = realloc(s_texScroll, (size_t)s_texCount);
    s_texScroll[tex] = (char)scroll;
    s_texName = realloc(s_texName, (size_t)s_texCount * sizeof(char *));
    s_texName[tex] = malloc(strlen(member) + 1);
    strcpy(s_texName[tex], member);

    int unstable = 0;
    int total = texpack_candidates(&src, 0, names, TEXPACK_MAX_CANDIDATES);
    if (total < 0) {
        CHECK(src.cpsm == RDTEX_PSMCT24, "%s: names refused (type %s, CLUT 0x%x)", member,
              kType[type], clutType);
        s_refused++;
        fprintf(f, "%s %s 0x%02x %ux%u %u - : refused\n", member, kType[type], clutType, w, h,
                levels);
        return;
    }
    fprintf(f, "%s %s 0x%02x %ux%u %u", member, kType[type], clutType, w, h, levels);
    char line[TEXPACK_MAX_CANDIDATES * (TEXPACK_NAME_MAX + 16)];
    size_t ll = 0;
    line[0] = 0;
    for (int i = 0; i < total; i++) {
        char nm[TEXPACK_NAME_MAX];
        CHECK(texpack_format_name(&names[i], nm, sizeof(nm)) > 0, "%s: name %d formats", member, i);
        unstable |= names[i].unstable;
        keyAdd(&s_disc, names[i].tex0Hash, names[i].clutHash, names[i].bits, tex,
               (names[i].startLevel > 0 ? 2 : 0) + names[i].mipChain);
        ll += (size_t)snprintf(line + ll, sizeof(line) - ll, " %s@%u%s", nm, names[i].startLevel,
                               names[i].mipChain ? "/chain" : "");
    }
    /* a bound level b > 0 asks for the levels k >= b: the tail of level 0's
       list, in the same order */
    for (uint32_t b = 1; b < levels; b++) {
        TexpackName sub[TEXPACK_MAX_CANDIDATES];
        int n = texpack_candidates(&src, b, sub, TEXPACK_MAX_CANDIDATES);
        CHECK(n > 0 && n <= total, "%s: bound level %u gives %d names", member, b, n);
        for (int i = 0; i < n && n <= total; i++) {
            const TexpackName *x = &sub[i];
            const TexpackName *y = &names[total - n + i];
            CHECK(x->tex0Hash == y->tex0Hash && x->clutHash == y->clutHash && x->bits == y->bits &&
                      x->startLevel >= b,
                  "%s: bound level %u name %d is level 0's tail", member, b, i);
        }
    }
    s_names += total;
    if (unstable) {
        st->unstable++;
    }
    fprintf(f, " %d :%s\n", unstable, line);
}

static int isJim(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && (strcmp(name + n - 4, ".jim") == 0 || strcmp(name + n - 4, ".JIM") == 0);
}

/* every TIM2 inside a file of records (a .jim) */
static void doRecords(const char *file, const uint8_t *d, size_t size, FILE *f)
{
    for (size_t off = 0; off + 16 <= size; off += 16) {
        if (memcmp(d + off, "TIM2", 4) == 0) {
            char name[300];
            snprintf(name, sizeof(name), "%s@0x%zx", file, off);
            doTexture(name, d + off, size - off, f);
        }
    }
}

/* ------------------------------------------------------------ the pack */

typedef struct Pack {
    KeyList names; /* tex = the file's number */
    char **files;
    int count, valid, region, invalid, ignored;
} Pack;

static int extOk(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0;
    }
    char e[8];
    size_t n = strlen(dot + 1);
    if (n >= sizeof(e)) {
        return 0;
    }
    for (size_t i = 0; i <= n; i++) {
        char c = dot[1 + i];
        e[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    return strcmp(e, "png") == 0 || strcmp(e, "dds") == 0;
}

static void packFile(Pack *p, const char *base)
{
    TexpackName n;
    if (!extOk(base)) {
        p->ignored++;
        return;
    }
    int rc = texpack_parse_name(base, &n);
    int idx = p->count++;
    p->files = realloc(p->files, (size_t)p->count * sizeof(char *));
    p->files[idx] = malloc(strlen(base) + 1);
    strcpy(p->files[idx], base);
    if (rc == 0) {
        p->valid++;
        keyAdd(&p->names, n.tex0Hash, n.clutHash, n.bits, idx, 0);
    } else if (rc == 1) {
        p->region++;
    } else {
        p->invalid++;
        printf("  pack: not a texture name, skipped: %s\n", base);
    }
}

static void packWalk(Pack *p, const char *dir, int depth)
{
    if (depth > 16) {
        return;
    }
#ifdef _WIN32
    char pat[1024];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    HANDLE hf = FindFirstFileA(pat, &fd);
    if (hf == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char path[1024];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            packWalk(p, path, depth + 1);
        } else {
            packFile(p, fd.cFileName);
        }
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[4096];
        struct stat sb;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &sb) != 0) {
            continue;
        }
        if (S_ISDIR(sb.st_mode)) {
            packWalk(p, path, depth + 1);
        } else {
            packFile(p, e->d_name);
        }
    }
    closedir(d);
#endif
}

static int dirExists(const char *dir)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(dir);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat sb;
    return stat(dir, &sb) == 0 && S_ISDIR(sb.st_mode);
#endif
}

static void packMatch(const char *dir)
{
    Pack p;
    memset(&p, 0, sizeof(p));
    packWalk(&p, dir, 0);
    printf("pack %s: %d texture files: %d names, %d region names, %d not names (%d other "
           "files)\n",
           dir, p.count, p.valid, p.region, p.invalid, p.ignored);

    /* the pack's files that a disc texture names, and the textures with a
       file */
    char *texHit = calloc((size_t)s_texCount + 1, 1);
    int fileHits = 0;
    int tex0Only = 0;
    int byKind[4] = {0, 0, 0, 0};
    int scrolled = 0;
    int clutOnly = 0;
    int printed = 0;
    KeyList byTex0 = {0};
    for (int i = 0; i < s_disc.n; i++) {
        keyAdd(&byTex0, s_disc.k[i].tex0, 0, 0, s_disc.k[i].tex, 0);
    }
    qsort(byTex0.k, (size_t)byTex0.n, sizeof(Key), keyCmp);
    for (int i = 0; i < p.names.n; i++) {
        Key *k = &p.names.k[i];
        int at = keyFind(&s_disc, k);
        if (at >= 0) {
            int kind = 3;
            fileHits++;
            for (; at < s_disc.n && keyCmp(&s_disc.k[at], k) == 0; at++) {
                texHit[s_disc.k[at].tex] = 1;
                kind = s_disc.k[at].kind < kind ? s_disc.k[at].kind : kind;
            }
            byKind[kind]++;
            continue;
        }
        Key t = {k->tex0, 0, 0, 0, 0};
        int at0 = keyFind(&byTex0, &t);
        if (at0 >= 0) {
            tex0Only++;
            scrolled += s_texScroll[byTex0.k[at0].tex];
        } else {
            int c = 0;
            for (int j = 0; j < s_disc.n && !c; j++) {
                c = s_disc.k[j].clut == k->clut && k->clut != 0;
            }
            clutOnly += c;
        }
        if (printed < 12) {
            printed++;
            printf("  pack file without a disc texture: %s\n", p.files[k->tex]);
        }
    }
    int texHits = 0;
    for (int i = 0; i < s_texCount; i++) {
        texHits += texHit[i];
    }
    printf("pack match: %d of %d named files match a disc texture (%.1f%%); %d of %d disc "
           "textures have a file\n",
           fileHits, p.valid, p.valid ? 100.0 * fileHits / p.valid : 0.0, texHits, s_texCount);
    printf("pack matches by name kind: %d level 0 alone, %d level 0's chain, %d a level k > 0 "
           "alone, %d a chain from k > 0\n",
           byKind[0], byKind[1], byKind[2], byKind[3]);
    printf("pack misses: %d with a disc texture's TEX0 hash under another CLUT (%d of them a "
           "texture whose CLUT scrolls), %d with only a disc CLUT hash, %d with neither\n",
           tex0Only, scrolled, clutOnly, p.valid - fileHits - tex0Only - clutOnly);
    CHECK(p.valid == 0 || fileHits > 0, "no pack file matches a disc texture");
    free(texHit);
    free(byTex0.k);
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    const char *pack = argc > 2 ? argv[2] : getenv("ICO_TEXPACK_DIR");
    FILE *probe = disc ? fopen(disc, "rb") : NULL;
    if (probe == NULL) {
        printf("texpack_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("texpack_disc_test: SKIP (%s is not a readable disc image)\n", disc);
        return 77;
    }
    IcoDfMember m;
    ico_df_find_member(vfs, "", &m); /* builds the index */
    int members = ico_df_index_members();
    CHECK(members > 0, "DATA.DF has members (%d)", members);

    FILE *f = fopen("texpack_names.txt", "w");
    CHECK(f != NULL, "texpack_names.txt can be written");
    if (f == NULL) {
        return 1;
    }
    for (int i = 0; i < members; i++) {
        const char *name = ico_df_member_name(i);
        size_t n = strlen(name);
        if (n < 4 || (strcmp(name + n - 4, ".tm2") != 0 && strcmp(name + n - 4, ".TM2") != 0)) {
            continue;
        }
        if (ico_df_member(i, &m) != 0) {
            CHECK(0, "member %d", i);
            continue;
        }
        uint8_t *d = malloc(m.size ? m.size : 1);
        if (d == NULL || ico_df_read_member(vfs, &m, d) != 0) {
            CHECK(0, "%s can be read", name);
            free(d);
            continue;
        }
        doTexture(name, d, m.size, f);
        free(d);
    }
    /* the subtitles: a TIM2 per 0x8800-byte record of the .jim files
       (jimaku.c reads them record by record into a buffer it hands to
       tex_InitTexture), loose in DATA.DF and in the packs */
    for (int i = 0;; i++) {
        const char *name = ico_df_entry_name(vfs, i);
        if (name == NULL) {
            break;
        }
        if (!isJim(name)) {
            continue;
        }
        int64_t n = ico_df_size(vfs, name);
        uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
        if (d == NULL || ico_df_read(vfs, name, 0, d, (size_t)n) != n) {
            CHECK(0, "%s can be read", name);
            free(d);
            continue;
        }
        doRecords(name, d, (size_t)n, f);
        free(d);
    }
    for (int i = 0; i < members; i++) {
        const char *name = ico_df_member_name(i);
        if (!isJim(name) || ico_df_member(i, &m) != 0) {
            continue;
        }
        uint8_t *d = malloc(m.size ? m.size : 1);
        if (d == NULL || ico_df_read_member(vfs, &m, d) != 0) {
            CHECK(0, "%s can be read", name);
            free(d);
            continue;
        }
        doRecords(name, d, m.size, f);
        free(d);
    }
    fclose(f);
    qsort(s_disc.k, (size_t)s_disc.n, sizeof(Key), keyCmp);

    printf("disc: %d TIM2 members (%d not readable), %d names, %d refused (24-bit CLUT)\n", s_tim2,
           s_bad, s_names, s_refused);
    printf("  %-8s %8s %9s %9s %9s %7s %9s %9s %12s\n", "type", "textures", "mipmapped", "non-pow2",
           "unstable", "CLUT24", "CT16x256", "odd CLUT", "CLUT scroll");
    for (int t = 1; t < TYPE_COUNT; t++) {
        Stats *s = &s_type[t];
        printf("  %-8s %8d %9d %9d %9d %7d %9d %9d %12d\n", kType[t], s->textures, s->mipmapped,
               s->nonPow2, s->unstable, s->clut24, s->clut16x256, s->odd, s->scroll);
    }
    CHECK(s_tim2 > 0, "the disc has TIM2 textures");
    int clut24 = 0;
    for (int t = 1; t < TYPE_COUNT; t++) {
        clut24 += s_type[t].clut24;
    }
    CHECK(s_refused <= clut24, "only 24-bit CLUTs are refused (%d refused, %d CLUT24)", s_refused,
          clut24);
    printf("names written to texpack_names.txt\n");

    if (pack != NULL && *pack != 0 && dirExists(pack)) {
        packMatch(pack);
    } else {
        printf("pack: none (%s)\n", pack && *pack ? pack : "no folder given");
    }
    ico_vfs_unmount(vfs);
    if (failures) {
        printf("texpack_disc_test: %d failures\n", failures);
        return 1;
    }
    printf("texpack_disc_test: ok\n");
    return 0;
}
