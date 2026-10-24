/* texpack_export_test: the texture export (texpack_export.h).
 *
 *   texpack_export_test WORKDIR
 *   texpack_export_test --disc DISC NAMES WORKDIR
 *
 * The first form needs no disc: TIM2 files made here (rd_tex_test.c's
 * makeTim2 with a mipmap variant; PSMT8 with 256 entries, PSMT4 with 16
 * 16-bit entries, PSMCT32, PSMT8 with a 16-entry CLUT, a 24-bit CLUT, a
 * 48x20 picture, two levels, the same picture under two member names, a
 * subtitle file of 0x8800-byte records through texpack_walk_records, a
 * file that is no TIM2) are fed to the core in WORKDIR/texpack_export_out.
 * The files written must be exactly the names texpack_candidates gives
 * each bound level alone, each PNG padW x padH, a file already there kept
 * and flagged on-disk, the refusals and duplicates counted and flagged in
 * the manifest; a second run writes nothing, a run stopped after k items
 * says so, a DRY run writes no PNG but lists the same names.
 *
 * The second form (77 without the disc or texpack_disc_test's NAMES): a
 * DRY export of the disc lists exactly the names texpack_names.txt gives
 * every level alone (its tokens without "/chain"), less the textures the
 * runtime names nothing for (a short CLUT); and a run cancelled after 100
 * items stops there.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "rd_tex.h"
#include "texpack_export.h"
#include "texpack_name.h"
#include "texpack_walk.h"
#include "vfs.h"

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

/* ------------------------------------------------------------ strings */

typedef struct StrList {
    char **s;
    int n, cap;
} StrList;

static void strAdd(StrList *l, const char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->s = realloc(l->s, (size_t)l->cap * sizeof(char *));
        if (l->s == NULL) {
            printf("texpack_export_test: out of memory\n");
            exit(1);
        }
    }
    l->s[l->n] = malloc(strlen(s) + 1);
    strcpy(l->s[l->n++], s);
}

static int strCmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* sorted, each string once */
static void strUnique(StrList *l)
{
    int k = 0;
    if (l->n == 0) {
        return;
    }
    qsort(l->s, (size_t)l->n, sizeof(char *), strCmp);
    for (int i = 0; i < l->n; i++) {
        if (k > 0 && strcmp(l->s[k - 1], l->s[i]) == 0) {
            free(l->s[i]);
            continue;
        }
        l->s[k++] = l->s[i];
    }
    l->n = k;
}

static int strHas(const StrList *l, const char *s)
{
    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->s[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

static void strFree(StrList *l)
{
    for (int i = 0; i < l->n; i++) {
        free(l->s[i]);
    }
    free(l->s);
    memset(l, 0, sizeof(*l));
}

/* two unique lists: equal, else the first few differences printed */
static int strSame(const StrList *a, const StrList *b, const char *what)
{
    int i = 0;
    int j = 0;
    int shown = 0;
    int same = a->n == b->n;
    while (i < a->n || j < b->n) {
        int c = i >= a->n ? 1 : j >= b->n ? -1 : strcmp(a->s[i], b->s[j]);
        if (c == 0) {
            i++;
            j++;
            continue;
        }
        same = 0;
        if (shown++ < 8) {
            printf("  %s: %s only in the %s\n", what, c < 0 ? a->s[i] : b->s[j],
                   c < 0 ? "first" : "second");
        }
        if (c < 0) {
            i++;
        } else {
            j++;
        }
    }
    return same;
}

/* ------------------------------------------------------------ files */

static int readAll(const char *path, char **data, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    long n;
    *data = NULL;
    *size = 0;
    if (!f || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f) {
            fclose(f);
        }
        return -1;
    }
    *data = malloc((size_t)n + 1);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        free(*data);
        *data = NULL;
        fclose(f);
        return -1;
    }
    (*data)[n] = '\0';
    fclose(f);
    *size = (size_t)n;
    return 0;
}

static int writeAll(const char *path, const char *text)
{
    FILE *f = ico_fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int collectFile(const char *path, const char *name, void *user)
{
    (void)path;
    strAdd(user, name);
    return 0;
}

static int removeFile(const char *path, const char *name, void *user)
{
    (void)name;
    (void)user;
    (void)ico_remove(path);
    return 0;
}

/* dir made and emptied */
static void freshDir(const char *dir)
{
    (void)ico_mkdir(dir);
    (void)ico_dir_walk(dir, 0, removeFile, NULL);
}

/* the PNG's IHDR size: 0, or -1 */
static int pngSize(const char *path, uint32_t *w, uint32_t *h)
{
    uint8_t b[24];
    memset(b, 0, sizeof(b));
    FILE *f = ico_fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (n != sizeof(b) || memcmp(b + 12, "IHDR", 4) != 0) {
        return -1;
    }
    *w = (uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | b[19];
    *h = (uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | b[23];
    return 0;
}

/* the manifest's header count after "<word> " */
static int countOf(const char *manifest, const char *word)
{
    char key[64];
    const char *line = strstr(manifest, "# items ");
    snprintf(key, sizeof(key), " %s ", word);
    const char *p = line ? strstr(line, key) : NULL;
    if (line && strncmp(word, "items", 5) == 0) {
        return atoi(line + 8);
    }
    return p ? atoi(p + strlen(key)) : -1;
}

/* the manifest's row of member (a tab after it): its flags column into
   flags, its names into names (the names after their mark); 0 or -1 */
static int rowOf(const char *manifest, const char *member, char *flags, size_t fsize,
                 StrList *names)
{
    size_t ml = strlen(member);
    for (const char *p = manifest; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        if (*p == '#' || strncmp(p, member, ml) != 0 || p[ml] != '\t') {
            continue;
        }
        const char *col = p;
        for (int c = 0; c < 7 && col; c++) {
            col = strchr(col, '\t');
            col = col ? col + 1 : NULL;
        }
        if (col == NULL) {
            return -1;
        }
        const char *tab = strchr(col, '\t');
        const char *end = strchr(col, '\n');
        if (tab == NULL || end == NULL || tab > end) {
            return -1;
        }
        snprintf(flags, fsize, "%.*s", (int)(tab - col), col);
        if (names != NULL) {
            const char *q = tab + 1;
            while (q < end) {
                const char *sp = q;
                while (sp < end && *sp != ' ') {
                    sp++;
                }
                if (sp - q > 1 && *q != '-') {
                    char tok[128];
                    snprintf(tok, sizeof(tok), "%.*s", (int)(sp - q), q);
                    strAdd(names, tok);
                }
                q = sp + 1;
            }
        }
        return 0;
    }
    return -1;
}

/* every name token of every row ("<mark><name>@<level>" -> "<name>@<level>")
   into out, the members flagged flag into flagged (when not NULL) */
static void manifestNames(const char *manifest, StrList *out, const char *flag, StrList *flagged)
{
    for (const char *p = manifest; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        if (*p == '#' || *p == '\n') {
            continue;
        }
        const char *end = strchr(p, '\n');
        const char *col = p;
        for (int c = 0; c < 7 && col; c++) {
            col = strchr(col, '\t');
            col = col ? col + 1 : NULL;
        }
        if (col == NULL || end == NULL) {
            continue;
        }
        const char *tab = strchr(col, '\t');
        if (tab == NULL || tab > end) {
            continue;
        }
        if (flagged != NULL && flag != NULL) {
            char fl[256];
            snprintf(fl, sizeof(fl), "%.*s", (int)(tab - col), col);
            if (strstr(fl, flag) != NULL) {
                const char *t = strchr(p, '\t');
                char m[TEXPACK_WALK_NAME_MAX];
                snprintf(m, sizeof(m), "%.*s", (int)(t - p), p);
                strAdd(flagged, m);
            }
        }
        const char *q = tab + 1;
        while (q < end) {
            const char *sp = q;
            while (sp < end && *sp != ' ') {
                sp++;
            }
            if (sp - q > 1 && *q != '-') {
                char tok[128];
                snprintf(tok, sizeof(tok), "%.*s", (int)(sp - q - 1), q + 1);
                strAdd(out, tok);
            }
            q = sp + 1;
        }
    }
}

/* ------------------------------------------------------------ TIM2 files */

/* TIM2 image types (Texture.c psmTable) and CLUT types */
enum { T2_CT16 = 1, T2_CT24 = 2, T2_CT32 = 3, T2_T4 = 4, T2_T8 = 5 };

enum { C2_16 = 1, C2_24 = 2, C2_32 = 3, C2_LINEAR = 0x80 };

/* rd_tex_test.c's Tim2Spec with levels: level k is (w >> k) x (h >> k),
   the levels' texels back to back in image */
typedef struct Tim2Spec {
    int imageType, clutType;
    int w, h;
    int levels;
    const void *image;
    size_t imageSize;
    const void *clut; /* as stored in the file */
    size_t clutSize;
    int colors;
} Tim2Spec;

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static size_t texelBytes(int type, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * h;
    switch (type) {
    case T2_CT32:
        return n * 4;
    case T2_CT24:
        return n * 3;
    case T2_CT16:
        return n * 2;
    case T2_T8:
        return n;
    default:
        return (n + 1) / 2;
    }
}

/* rd_tex_test.c makeTim2's layout: file header, picture header, the mipmap
   header (levels > 1: two TBP words and a size per level, 32 or 48 bytes,
   Texture.c's kMipHeader), image, CLUT */
static uint8_t *makeTim2(const Tim2Spec *s, size_t *size)
{
    static const uint32_t kMipHeader[8] = {0, 0, 32, 32, 32, 48, 48, 48};
    int levels = s->levels ? s->levels : 1;
    size_t hdr = 0x30 + kMipHeader[levels];
    size_t total = 16 + hdr + s->imageSize + s->clutSize;
    uint8_t *f = calloc(1, total + 64);
    if (f == NULL) {
        exit(1);
    }
    memcpy(f, "TIM2", 4);
    f[4] = 4;
    f[6] = 1;
    uint8_t *pic = f + 16;
    put32(pic + 0x00, (uint32_t)(hdr + s->imageSize + s->clutSize));
    put32(pic + 0x04, (uint32_t)s->clutSize);
    put32(pic + 0x08, (uint32_t)s->imageSize);
    put16(pic + 0x0C, (uint32_t)hdr);
    put16(pic + 0x0E, (uint32_t)s->colors);
    pic[0x11] = (uint8_t)levels;
    pic[0x12] = (uint8_t)s->clutType;
    pic[0x13] = (uint8_t)s->imageType;
    put16(pic + 0x14, (uint32_t)s->w);
    put16(pic + 0x16, (uint32_t)s->h);
    if (levels > 1) {
        for (int k = 0; k < levels; k++) {
            put32(pic + 0x30 + 0x10 + 4 * k,
                  (uint32_t)texelBytes(s->imageType, (uint32_t)s->w >> k, (uint32_t)s->h >> k));
        }
    }
    memcpy(pic + hdr, s->image, s->imageSize);
    if (s->clutSize) {
        memcpy(pic + hdr + s->imageSize, s->clut, s->clutSize);
    }
    *size = total;
    return f;
}

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* n bytes of a pattern of its own per seed */
static uint8_t *pattern(size_t n, uint32_t seed)
{
    uint8_t *p = malloc(n ? n : 1);
    if (p == NULL) {
        exit(1);
    }
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)mix((uint32_t)i * 2654435761u + seed);
    }
    return p;
}

/* one synthetic TIM2 */
typedef struct Case {
    const char *member;
    Tim2Spec spec;
    uint8_t *file;
    size_t size;
} Case;

static void makeCase(Case *c, const char *member, int type, int clutType, int w, int h, int levels,
                     int colors, size_t clutBytes, uint32_t seed)
{
    size_t image = 0;
    for (int k = 0; k < (levels ? levels : 1); k++) {
        image += texelBytes(type, (uint32_t)w >> k, (uint32_t)h >> k);
    }
    memset(c, 0, sizeof(*c));
    c->member = member;
    c->spec.imageType = type;
    c->spec.clutType = clutType;
    c->spec.w = w;
    c->spec.h = h;
    c->spec.levels = levels;
    uint8_t *img = pattern(image, seed);
    uint8_t *clut = clutBytes ? pattern(clutBytes, seed ^ 0x55u) : NULL;
    c->spec.image = img;
    c->spec.imageSize = image;
    c->spec.clut = clut;
    c->spec.clutSize = clutBytes;
    c->spec.colors = colors;
    c->file = makeTim2(&c->spec, &c->size);
    free(img);
    free(clut);
}

/* ------------------------------------------------------------ the oracle */

/* the names the runtime gives every bound level of it alone, with each
   name's padded size: the hook's source (the CLUT as the format's 16 or 256
   entries), texpack_candidates per level, startLevel == level, no chain */
typedef struct Expect {
    StrList names; /* "<name>" */
    StrList toks;  /* "<name>@<level>" */
    uint32_t padW[512], padH[512];
} Expect;

static void expectItem(Expect *e, const TexpackWalkItem *it)
{
    TexpackName names[TEXPACK_MAX_CANDIDATES];
    memset(names, 0, sizeof(names));
    if (it->bad != TEXPACK_WALK_OK || it->clut24 || it->shortClut) {
        return;
    }
    TexpackSource src = it->src;
    if (src.clut != NULL) {
        src.clutColors = it->type == 4 ? 16 : 256;
    }
    for (uint32_t b = 0; b < src.levels; b++) {
        int n = texpack_candidates(&src, b, names, TEXPACK_MAX_CANDIDATES);
        for (int i = 0; i < n; i++) {
            char nm[TEXPACK_NAME_MAX];
            char tok[TEXPACK_NAME_MAX + 8];
            if (names[i].startLevel != b || names[i].mipChain ||
                texpack_format_name(&names[i], nm, sizeof(nm)) < 0) {
                continue;
            }
            snprintf(tok, sizeof(tok), "%s@%u", nm, b);
            strAdd(&e->toks, tok);
            if (!strHas(&e->names, nm) && e->names.n < 512) {
                e->padW[e->names.n] = 1u << src.lv[b].tw;
                e->padH[e->names.n] = 1u << src.lv[b].th;
                strAdd(&e->names, nm);
            }
        }
    }
}

/* ------------------------------------------------------------ no disc */

enum { C_T8, C_T4, C_CT32, C_SHORT, C_CLUT24, C_ODD, C_MIP, C_T8_AGAIN, C_JUNK, C_COUNT };

typedef struct Feed {
    TexpackExport *x;
    Expect *expect;
    int items;
} Feed;

static int feedItem(void *user, const TexpackWalkItem *it)
{
    Feed *f = user;
    if (f->expect != NULL) {
        expectItem(f->expect, it);
    }
    texpack_export_item(f->x, it);
    f->items++;
    return 0;
}

/* the cases and the subtitle file into x (expect filled when not NULL);
   stop after limit items (0: all).  The items fed. */
static int feed(TexpackExport *x, Case *cases, const uint8_t *jim, size_t jimSize, Expect *expect,
                int limit)
{
    Feed f = {x, expect, 0};
    TexpackWalkItem *it = malloc(sizeof(*it));
    if (it == NULL) {
        exit(1);
    }
    for (int i = 0; i < C_COUNT && (limit == 0 || f.items < limit); i++) {
        texpack_walk_tim2(cases[i].member, TEXPACK_WALK_TM2, cases[i].file, cases[i].size, it);
        feedItem(&f, it);
    }
    if (limit == 0 || f.items < limit) {
        int n = texpack_walk_records("data_EG01.jim", TEXPACK_WALK_JIM_LOOSE, jim, jimSize,
                                     feedItem, &f, NULL);
        CHECK(n == 2, "the subtitle file holds 2 records (%d)", n);
    }
    free(it);
    return f.items;
}

static int runSynthetic(const char *work)
{
    char dir[1100];
    char dryDir[1100];
    char path[1300];
    Case cases[C_COUNT];
    memset(cases, 0, sizeof(cases));

    snprintf(dir, sizeof(dir), "%s/texpack_export_out", work);
    snprintf(dryDir, sizeof(dryDir), "%s/texpack_export_dry", work);
    freshDir(dir);
    freshDir(dryDir);

    makeCase(&cases[C_T8], "object/t8.tm2", T2_T8, C2_32, 16, 16, 1, 256, 1024, 1);
    makeCase(&cases[C_T4], "object/t4.tm2", T2_T4, C2_16, 32, 16, 1, 16, 32, 2);
    makeCase(&cases[C_CT32], "object/ct32.tm2", T2_CT32, 0, 8, 8, 1, 0, 0, 3);
    makeCase(&cases[C_SHORT], "object/short.tm2", T2_T8, C2_32, 16, 16, 1, 16, 64, 4);
    makeCase(&cases[C_CLUT24], "object/clut24.tm2", T2_T8, C2_24, 16, 16, 1, 256, 768, 5);
    makeCase(&cases[C_ODD], "object/odd.tm2", T2_CT32, 0, 48, 20, 1, 0, 0, 6);
    makeCase(&cases[C_MIP], "object/mip.tm2", T2_CT32, 0, 16, 16, 2, 0, 0, 7);
    /* the same file as C_T8 under another name */
    makeCase(&cases[C_T8_AGAIN], "other/t8.tm2", T2_T8, C2_32, 16, 16, 1, 256, 1024, 1);
    memset(&cases[C_JUNK], 0, sizeof(cases[C_JUNK]));
    cases[C_JUNK].member = "object/junk.tm2";
    cases[C_JUNK].file = pattern(80, 9);
    cases[C_JUNK].size = 80;

    /* two records 0x8800 bytes apart */
    size_t jimSize = 2 * TEXPACK_WALK_RECORD;
    uint8_t *jim = calloc(1, jimSize);
    Case r0;
    Case r1;
    makeCase(&r0, "r0", T2_CT32, 0, 4, 4, 1, 0, 0, 10);
    makeCase(&r1, "r1", T2_T4, C2_32, 8, 8, 1, 16, 64, 11);
    CHECK(jim != NULL && r0.size < TEXPACK_WALK_RECORD && r1.size < TEXPACK_WALK_RECORD,
          "the records fit");
    memcpy(jim, r0.file, r0.size);
    memcpy(jim + TEXPACK_WALK_RECORD, r1.file, r1.size);

    /* the walk's flags */
    TexpackWalkItem *it = malloc(sizeof(*it));
    CHECK(it != NULL, "memory");
    texpack_walk_tim2("s", 0, cases[C_SHORT].file, cases[C_SHORT].size, it);
    CHECK(it->bad == 0 && it->shortClut && !it->clut24, "a 16-entry CLUT on PSMT8 is short");
    texpack_walk_tim2("c", 0, cases[C_CLUT24].file, cases[C_CLUT24].size, it);
    CHECK(it->bad == 0 && it->clut24, "a 24-bit CLUT is flagged");
    texpack_walk_tim2("t", 0, cases[C_T4].file, cases[C_T4].size, it);
    CHECK(it->bad == 0 && !it->shortClut && it->src.clutColors == 16, "PSMT4 with 16 entries");
    texpack_walk_tim2("j", 0, cases[C_JUNK].file, cases[C_JUNK].size, it);
    CHECK(it->bad == TEXPACK_WALK_NOT_TIM2, "no TIM2 tag");
    texpack_walk_tim2("m", 0, cases[C_MIP].file, cases[C_MIP].size, it);
    CHECK(it->bad == 0 && it->levels == 2 && it->src.lv[1].w == 8, "two levels");

    /* a file already there, under C_CT32's name */
    Expect pre;
    memset(&pre, 0, sizeof(pre));
    texpack_walk_tim2(cases[C_CT32].member, 0, cases[C_CT32].file, cases[C_CT32].size, it);
    expectItem(&pre, it);
    CHECK(pre.names.n == 1, "PSMCT32 8x8 has one name (%d)", pre.names.n);
    char kept[1300];
    snprintf(kept, sizeof(kept), "%s/%s.png", dir, pre.names.n ? pre.names.s[0] : "x");
    CHECK(writeAll(kept, "keep") == 0, "%s can be written", kept);

    /* 1. the export */
    Expect e;
    memset(&e, 0, sizeof(e));
    TexpackExport *x = texpack_export_open(dir, "SCES-50760", 0);
    CHECK(x != NULL, "open");
    if (x == NULL) {
        return 1;
    }
    int items = feed(x, cases, jim, jimSize, &e, 0);
    TexpackExportStatus st;
    int state = texpack_export_close(x, 0, items, &st);
    strUnique(&e.toks);
    CHECK(state == TEXPACK_EXPORT_DONE && st.state == state, "the run is done (%d)", state);
    CHECK(items == C_COUNT + 2 && st.done == items, "%d items, %d done", items, st.done);
    CHECK(st.tim2 == C_COUNT && st.jimRecords == 2, "tim2 %d, records %d", st.tim2, st.jimRecords);
    CHECK(st.written == e.names.n - 1, "written %d of %d names (one was there)", st.written,
          e.names.n);
    CHECK(st.skipped == 1, "already on disk %d", st.skipped);
    CHECK(st.refusedClut24 == 1 && st.refusedShortClut == 1 && st.refused == 2, "refused %d/%d",
          st.refusedClut24, st.refusedShortClut);
    CHECK(st.bad == 1 && st.failed == 0, "bad %d, failed %d", st.bad, st.failed);

    /* the files: exactly the names, each padW x padH, the old one kept */
    StrList files = {0};
    (void)ico_dir_walk(dir, 0, collectFile, &files);
    strUnique(&files);
    StrList want = {0};
    for (int i = 0; i < e.names.n; i++) {
        char f[TEXPACK_NAME_MAX + 8];
        snprintf(f, sizeof(f), "%s.png", e.names.s[i]);
        strAdd(&want, f);
    }
    strAdd(&want, "manifest.txt");
    strUnique(&want);
    CHECK(strSame(&files, &want, "files"), "the folder holds the names and the manifest (%d, %d)",
          files.n, want.n);
    for (int i = 0; i < e.names.n; i++) {
        uint32_t w = 0;
        uint32_t h = 0;
        snprintf(path, sizeof(path), "%s/%s.png", dir, e.names.s[i]);
        if (strcmp(path, kept) == 0) {
            continue;
        }
        CHECK(pngSize(path, &w, &h) == 0 && w == e.padW[i] && h == e.padH[i], "%s is %ux%u (%ux%u)",
              e.names.s[i], w, h, e.padW[i], e.padH[i]);
    }
    char *old = NULL;
    size_t oldSize = 0;
    CHECK(readAll(kept, &old, &oldSize) == 0 && oldSize == 4 && memcmp(old, "keep", 4) == 0,
          "the file that was there is kept");
    free(old);

    /* the manifest */
    char *man = NULL;
    size_t manSize = 0;
    snprintf(path, sizeof(path), "%s/manifest.txt", dir);
    CHECK(readAll(path, &man, &manSize) == 0, "the manifest is there");
    if (man != NULL) {
        char flags[256];
        CHECK(strncmp(man, "# ICO texture export SCES-50760 ", 32) == 0 &&
                  strstr(man, " complete\n") != NULL,
              "the first line: %.60s", man);
        CHECK(countOf(man, "items") == items && countOf(man, "written") == st.written &&
                  countOf(man, "already-on-disk") == 1 && countOf(man, "refused-clut24") == 1 &&
                  countOf(man, "refused-short-clut") == 1 && countOf(man, "bad") == 1 &&
                  countOf(man, "failed") == 0 && countOf(man, "tim2") == C_COUNT &&
                  countOf(man, "jim-records") == 2 && countOf(man, "duplicates") == st.duplicates,
              "the counts line");
        CHECK(strstr(man, "# Not in this export") != NULL &&
                  strstr(man, "Dump textures on") != NULL,
              "the line on what no export makes");
        CHECK(strstr(man, "# member\tkind\ttype\tsize\tWxH\tlevels\tclut\tflags\tnames\n") != NULL,
              "the columns line");
        CHECK(rowOf(man, "object/short.tm2", flags, sizeof(flags), NULL) == 0 &&
                  strstr(flags, "short-clut") != NULL,
              "short CLUT row: %s", flags);
        CHECK(rowOf(man, "object/clut24.tm2", flags, sizeof(flags), NULL) == 0 &&
                  strstr(flags, "clut24") != NULL,
              "CLUT24 row: %s", flags);
        CHECK(rowOf(man, "object/odd.tm2", flags, sizeof(flags), NULL) == 0 &&
                  strstr(flags, "unstable") != NULL,
              "48x20 row: %s", flags);
        CHECK(rowOf(man, "object/ct32.tm2", flags, sizeof(flags), NULL) == 0 &&
                  strstr(flags, "on-disk") != NULL,
              "the kept file's row: %s", flags);
        CHECK(rowOf(man, "object/junk.tm2", flags, sizeof(flags), NULL) == 0 &&
                  strcmp(flags, "bad:not-tim2") == 0,
              "the junk row: %s", flags);
        StrList first = {0};
        StrList again = {0};
        CHECK(rowOf(man, "object/t8.tm2", flags, sizeof(flags), &first) == 0 &&
                  strcmp(flags, "-") == 0 && first.n > 0,
              "the first PSMT8: %s", flags);
        CHECK(rowOf(man, "other/t8.tm2", flags, sizeof(flags), &again) == 0 &&
                  strcmp(flags, "duplicate") == 0 && again.n == first.n,
              "the second PSMT8: %s", flags);
        for (int i = 0; i < first.n && i < again.n; i++) {
            CHECK(first.s[i][0] == '+' && again.s[i][0] == '~' &&
                      strcmp(first.s[i] + 1, again.s[i] + 1) == 0,
                  "%s then %s", first.s[i], again.s[i]);
        }
        CHECK(st.duplicates == first.n, "duplicates %d, the PSMT8's names %d", st.duplicates,
              first.n);
        StrList mip = {0};
        CHECK(rowOf(man, "object/mip.tm2", flags, sizeof(flags), &mip) == 0 && mip.n == 2 &&
                  strstr(mip.s[0], "@0") != NULL && strstr(mip.s[1], "@1") != NULL,
              "two levels, a file each (%d)", mip.n);
        StrList t4 = {0};
        CHECK(rowOf(man, "object/t4.tm2", flags, sizeof(flags), &t4) == 0 && t4.n == 3,
              "a 16-bit CLUT: a name per TEXA mode (%d)", t4.n);
        CHECK(rowOf(man, "data_EG01.jim@0x8800", flags, sizeof(flags), NULL) == 0,
              "the second record has a row");
        /* the manifest's names are the oracle's */
        StrList listed = {0};
        manifestNames(man, &listed, NULL, NULL);
        strUnique(&listed);
        CHECK(strSame(&listed, &e.toks, "names"), "the manifest lists the names");
        strFree(&first);
        strFree(&again);
        strFree(&mip);
        strFree(&t4);
        strFree(&listed);
    }
    free(man);
    man = NULL;

    /* 2. again: nothing new */
    x = texpack_export_open(dir, "SCES-50760", 0);
    feed(x, cases, jim, jimSize, NULL, 0);
    state = texpack_export_close(x, 0, items, &st);
    CHECK(state == TEXPACK_EXPORT_DONE && st.written == 0 && st.skipped == e.names.n &&
              st.failed == 0,
          "a second run writes nothing (written %d, on disk %d of %d)", st.written, st.skipped,
          e.names.n);

    /* 3. stopped after 4 items */
    x = texpack_export_open(dir, "SCES-50760", 0);
    int fed = feed(x, cases, jim, jimSize, NULL, 4);
    state = texpack_export_close(x, 1, items, &st);
    CHECK(fed == 4 && st.done == 4 && state == TEXPACK_EXPORT_STOPPED, "stopped: %d done (%d)",
          st.done, state);
    if (readAll(path, &man, &manSize) == 0) {
        char want4[64];
        snprintf(want4, sizeof(want4), " stopped after 4 of %d\n", items);
        CHECK(strstr(man, want4) != NULL, "the stopped manifest: %.70s", man);
    }
    free(man);
    man = NULL;

    /* 4. DRY: no picture, the same names */
    x = texpack_export_open(dryDir, "SCES-50760", TEXPACK_EXPORT_DRY);
    feed(x, cases, jim, jimSize, NULL, 0);
    state = texpack_export_close(x, 0, items, &st);
    StrList dryFiles = {0};
    (void)ico_dir_walk(dryDir, 0, collectFile, &dryFiles);
    CHECK(state == TEXPACK_EXPORT_DONE && dryFiles.n == 1 && strHas(&dryFiles, "manifest.txt"),
          "a dry run writes only the manifest (%d files)", dryFiles.n);
    CHECK(st.written == e.names.n, "a dry run counts %d names (%d)", st.written, e.names.n);
    snprintf(path, sizeof(path), "%s/manifest.txt", dryDir);
    if (readAll(path, &man, &manSize) == 0) {
        StrList listed = {0};
        manifestNames(man, &listed, NULL, NULL);
        strUnique(&listed);
        CHECK(strSame(&listed, &e.toks, "dry names"), "the dry run lists the same names");
        CHECK(strstr(man, " complete dry run\n") != NULL, "the dry manifest says so");
        strFree(&listed);
    }
    free(man);

    /* no .tmp left anywhere */
    for (int i = 0; i < files.n; i++) {
        CHECK(strstr(files.s[i], ".tmp") == NULL, "%s is left", files.s[i]);
    }
    CHECK(texpack_export_open("", "x", 0) == NULL, "no folder, no run");
    CHECK(texpack_export_close(NULL, 0, 0, &st) == TEXPACK_EXPORT_FAILED, "close(NULL)");

    printf("texpack_export_test: %d names, %d items\n", e.names.n, items);
    for (int i = 0; i < C_COUNT; i++) {
        free(cases[i].file);
    }
    free(r0.file);
    free(r1.file);
    free(jim);
    free(it);
    strFree(&files);
    strFree(&want);
    strFree(&dryFiles);
    strFree(&e.names);
    strFree(&e.toks);
    strFree(&pre.names);
    strFree(&pre.toks);
    return failures ? 1 : 0;
}

/* ------------------------------------------------------------ the disc */

static int cancelAt(void *user)
{
    int *left = user;
    return (*left)-- <= 0;
}

static int runDisc(const char *disc, const char *namesPath, const char *work)
{
    char dir[1100];
    char path[1300];
    char *names = NULL;
    size_t namesSize = 0;
    FILE *probe = disc && disc[0] ? fopen(disc, "rb") : NULL;
    if (probe == NULL) {
        printf("texpack_export_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    if (readAll(namesPath, &names, &namesSize) != 0) {
        printf("texpack_export_test: SKIP (no %s: texpack_disc first)\n", namesPath);
        return 77;
    }
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("texpack_export_test: SKIP (%s is not a readable disc image)\n", disc);
        free(names);
        return 77;
    }

    /* the DRY export of the disc */
    snprintf(dir, sizeof(dir), "%s/texpack_export_disc", work);
    freshDir(dir);
    TexpackExportStatus st;
    int state = texpack_export_run(vfs, dir, "SCES-50760", TEXPACK_EXPORT_DRY, NULL, &st);
    CHECK(state == TEXPACK_EXPORT_DONE, "the dry run is done (%d)", state);
    StrList listed = {0};
    StrList shortClut = {0};
    char *man = NULL;
    size_t manSize = 0;
    snprintf(path, sizeof(path), "%s/manifest.txt", dir);
    CHECK(readAll(path, &man, &manSize) == 0, "the disc's manifest is there");
    if (man != NULL) {
        manifestNames(man, &listed, "short-clut", &shortClut);
    }
    strUnique(&listed);

    /* texpack_names.txt's level-alone tokens, less the short CLUTs */
    StrList want = {0};
    int lines = 0;
    for (char *p = names; p && *p;) {
        char *end = strchr(p, '\n');
        if (end == NULL) {
            break;
        }
        *end = '\0';
        lines++;
        char *sp = strchr(p, ' ');
        char member[TEXPACK_WALK_NAME_MAX];
        snprintf(member, sizeof(member), "%.*s", sp ? (int)(sp - p) : 0, p);
        char *colon = strstr(p, " :");
        if (colon != NULL && !strHas(&shortClut, member)) {
            for (char *t = strtok(colon + 2, " "); t != NULL; t = strtok(NULL, " ")) {
                if (strcmp(t, "refused") != 0 && strstr(t, "/chain") == NULL) {
                    strAdd(&want, t);
                }
            }
        }
        p = end + 1;
    }
    strUnique(&want);
    CHECK(strSame(&listed, &want, "disc names"),
          "the export's names are texpack_names.txt's (%d, %d)", listed.n, want.n);
    CHECK(st.done - st.bad == lines, "%d items less %d bad are the %d lines of the names file",
          st.done, st.bad, lines);
    CHECK(st.written == listed.n && st.duplicates > 0, "%d names once, %d duplicates", st.written,
          st.duplicates);
    printf("texpack_export_test: disc: %d items (%d tim2, %d records), %d names to write, %d "
           "duplicates, %d refused (%d short CLUT)\n",
           st.done, st.tim2, st.jimRecords, st.written, st.duplicates, st.refused,
           st.refusedShortClut);
    free(man);
    man = NULL;

    /* cancelled after 100 items */
    int left = 100;
    TexpackExportHooks h = {cancelAt, NULL, &left};
    state = texpack_export_run(vfs, dir, "SCES-50760", TEXPACK_EXPORT_DRY, &h, &st);
    CHECK(state == TEXPACK_EXPORT_STOPPED && st.done == 100, "cancelled: %d done (%d)", st.done,
          state);
    if (readAll(path, &man, &manSize) == 0) {
        CHECK(strstr(man, " stopped after 100 of ") != NULL, "the cancelled manifest: %.70s", man);
    }
    free(man);

    strFree(&listed);
    strFree(&shortClut);
    strFree(&want);
    free(names);
    ico_vfs_unmount(vfs);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    int rc;
    if (argc >= 2 && strcmp(argv[1], "--disc") == 0) {
        if (argc < 5) {
            printf("usage: texpack_export_test --disc DISC NAMES WORKDIR\n");
            return 2;
        }
        rc = runDisc(argv[2], argv[3], argv[4]);
    } else {
        rc = runSynthetic(argc > 1 ? argv[1] : ".");
    }
    if (rc == 77) {
        return 77;
    }
    if (failures) {
        printf("texpack_export_test: %d failures\n", failures);
        return 1;
    }
    printf("texpack_export_test: ok\n");
    return 0;
}
