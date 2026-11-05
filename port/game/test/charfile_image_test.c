/*
 * port/game/test/charfile_image_test.c
 *
 * ico_char_file_image's twins (ico2/common/src/charFileManager.c, the PC
 * port block at its end), CPU only, by their text: the file cannot be built
 * here without the whole game, so what the hook promises is checked in the
 * source.
 *
 * Checked: each twin (ReadModelImage, ReadModelImage_jun26,
 * ReadTextureImage, ReadTextureImage_ntscaug6jun26) is its loader's body
 * (ReadModelFile, ReadModelFile_jun26, ReadTextureFile,
 * ReadTextureFile_ntscaug6jun26) line for line, but for the read from the
 * cdvd handle, which is a copy of the image, the two skips, which are
 * nothing, and the call of the 2001 or demo arm, which is the twin's; no
 * line of the block reads from a handle; the dispatcher looks the extension
 * up as the pack loader does (the first of initFunc's 26 rows), tells the
 * two loaders by their addresses and counts the member as the pack loop
 * does; and the pack loader (ico2/fumi/ios/cdvd.c) still hands each member
 * to its row's function with the directory's words and the pack's segment,
 * then counts it.  A change to a loader or to the pack loop without its twin
 * fails here.
 *
 * charfile_image_test CHARFILEMANAGER_C CDVD_C.  Exit 0, 1 on a mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct {
    char *text;  /* the file, its line ends made NULs */
    char **line; /* the lines */
    int n;
} Src;

static int readSrc(const char *path, Src *s)
{
    FILE *f = fopen(path, "rb");
    long size;

    memset(s, 0, sizeof(*s));
    if (f == NULL) {
        printf("charfile_image_test: cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        printf("charfile_image_test: cannot size %s\n", path);
        return -1;
    }
    s->text = malloc((size_t)size + 1);
    s->line = malloc(((size_t)size + 2) * sizeof(char *));
    if (s->text == NULL || s->line == NULL || fread(s->text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        printf("charfile_image_test: cannot read %s\n", path);
        return -1;
    }
    fclose(f);
    s->text[size] = '\0';
    s->line[s->n++] = s->text;
    for (long i = 0; i < size; i++) {
        if (s->text[i] == '\n') {
            s->text[i] = '\0';
            if (i + 1 < size) {
                s->line[s->n++] = &s->text[i + 1];
            }
        }
    }
    for (int i = 0; i < s->n; i++) {
        size_t len = strlen(s->line[i]);
        if (len > 0 && s->line[i][len - 1] == '\r') {
            s->line[i][len - 1] = '\0';
        }
    }
    return 0;
}

static void freeSrc(Src *s)
{
    free(s->text);
    free(s->line);
}

/* the first line from `from` that starts with prefix, or -1 */
static int findLine(const Src *s, int from, const char *prefix)
{
    const size_t n = strlen(prefix);
    for (int i = from < 0 ? 0 : from; i < s->n; i++) {
        if (strncmp(s->line[i], prefix, n) == 0) {
            return i;
        }
    }
    return -1;
}

/* the first line from `from` that holds needle, or -1 */
static int findText(const Src *s, int from, const char *needle)
{
    for (int i = from < 0 ? 0 : from; i < s->n; i++) {
        if (strstr(s->line[i], needle) != NULL) {
            return i;
        }
    }
    return -1;
}

/* the body of the function whose definition starts with sig: the lines
   between its "{" and "}" at the line start; 0 and [*first, *end), or -1 */
static int body(const Src *s, const char *sig, int *first, int *end)
{
    int i = findLine(s, 0, sig);
    if (i < 0) {
        return -1;
    }
    /* a declaration ends in ';' before any "{" */
    while (i >= 0 && strchr(s->line[i], ';') != NULL && strchr(s->line[i], '{') == NULL) {
        i = findLine(s, i + 1, sig);
    }
    if (i < 0) {
        return -1;
    }
    int open = -1;
    for (int k = i; k < s->n && k < i + 6; k++) {
        if (strcmp(s->line[k], "{") == 0) {
            open = k;
            break;
        }
    }
    if (open < 0) {
        return -1;
    }
    for (int k = open + 1; k < s->n; k++) {
        if (strcmp(s->line[k], "}") == 0) {
            *first = open + 1;
            *end = k;
            return 0;
        }
    }
    return -1;
}

/* the loader's line as the twin writes it: the same indent, the text
   substituted where the twin differs */
static const struct {
    const char *loader, *twin;
} kSubs[] = {
    {"ReadModelFile_jun26(h, name, size, id, kind, word08, part);",
     "ReadModelImage_jun26(image, name, size, id, kind, word08, part);"},
    {"ReadTextureFile_ntscaug6jun26(h, name, size, id, kind, word08, seg);",
     "ReadTextureImage_ntscaug6jun26(image, name, size, id, kind, word08, seg);"},
    {"iosCdvdHandlerRead(h, p, size);", "memcpy(p, image, size);"},
    {"iosCdvdHandlerRead(h, buf, size);", "memcpy(buf, image, size);"},
    {"iosCdvdHandlerRead(h, 0, size);", "(void)image;"},
    {"iosCdvdHandlerRead(h, 0, 0);", "(void)image;"},
};

static void expected(const char *loader, char *out, size_t cap)
{
    const char *t = loader;
    while (*t == ' ') {
        t++;
    }
    for (size_t k = 0; k < sizeof(kSubs) / sizeof(kSubs[0]); k++) {
        if (strcmp(t, kSubs[k].loader) == 0) {
            snprintf(out, cap, "%.*s%.200s", (int)(t - loader), loader, kSubs[k].twin);
            return;
        }
    }
    snprintf(out, cap, "%.400s", loader);
}

static void checkTwin(const Src *s, const char *loaderSig, const char *twinSig, int *reads)
{
    int lf = 0, le = 0, tf = 0, te = 0;
    char want[512];

    const int haveLoader = body(s, loaderSig, &lf, &le) == 0;
    const int haveTwin = body(s, twinSig, &tf, &te) == 0;
    CHECK(haveLoader, "no definition starting \"%s\"", loaderSig);
    CHECK(haveTwin, "no definition starting \"%s\"", twinSig);
    if (!haveLoader || !haveTwin) {
        return;
    }
    CHECK(le - lf == te - tf, "%s: %d lines, its twin %s %d", loaderSig, le - lf, twinSig, te - tf);
    for (int k = 0; k < le - lf && k < te - tf; k++) {
        expected(s->line[lf + k], want, sizeof(want));
        CHECK(strcmp(want, s->line[tf + k]) == 0,
              "%s line %d (file line %d): the loader's \"%.200s\" wants \"%.200s\" in the twin, "
              "which has \"%.200s\" (file line %d)",
              loaderSig, k + 1, lf + k + 1, s->line[lf + k], want, s->line[tf + k], tf + k + 1);
        if (strstr(s->line[lf + k], "iosCdvdHandlerRead(") != NULL) {
            (*reads)++;
        }
    }
}

int main(int argc, char **argv)
{
    Src cfm, cdvd;

    if (argc < 3) {
        printf("usage: charfile_image_test CHARFILEMANAGER_C CDVD_C\n");
        return 1;
    }
    if (readSrc(argv[1], &cfm) != 0 || readSrc(argv[2], &cdvd) != 0) {
        return 1;
    }

    /* the twins */
    int reads = 0;
    checkTwin(&cfm, "static void ReadModelFile_jun26(", "static void ReadModelImage_jun26(",
              &reads);
    checkTwin(&cfm, "void ReadModelFile(", "static void ReadModelImage(", &reads);
    checkTwin(&cfm, "static void ReadTextureFile_ntscaug6jun26(",
              "static void ReadTextureImage_ntscaug6jun26(", &reads);
    checkTwin(&cfm, "void ReadTextureFile(", "static void ReadTextureImage(", &reads);
    /* each loader reads its member and skips one already loaded or empty:
       two reads in each of the four */
    CHECK(reads == 8, "%d reads from the handle in the four loaders, want 8", reads);

    /* the block reads no handle */
    const int block = findText(&cfm, 0, "PC port: a pack member registered from an image");
    CHECK(block > 0, "the PC port block's comment");
    for (int i = block > 0 ? block : cfm.n; i < cfm.n; i++) {
        CHECK(strstr(cfm.line[i], "iosCdvdHandlerRead(h") == NULL &&
                  strstr(cfm.line[i], "iosCdvdHandlerRead(self") == NULL,
              "file line %d reads from a handle: %.200s", i + 1, cfm.line[i]);
    }

    /* the dispatcher */
    int df = 0, de = 0;
    const int haveDispatch = body(&cfm, "int ico_char_file_image(", &df, &de) == 0;
    CHECK(haveDispatch, "no ico_char_file_image");
    if (haveDispatch) {
        static const char *const kNeed[] = {
            "if (name[i] == '.') {",
            "for (i = 0; ext != 0 && i < 26; i++) {",
            "if (strcmp(ext, initFunc[i].ext) == 0) {",
            "if ((AnyFn)initFunc[row].func == (AnyFn)ReadModelFile) {",
            "ReadModelImage(image, (ICO_WORD)name, size, id, kind, word08, seg);",
            "} else if ((AnyFn)initFunc[row].func == (AnyFn)ReadTextureFile) {",
            "ReadTextureImage(image, name, size, id, kind, word08, seg);",
            "debugCdvdLoadInfoSegAdd(seg, row, size);",
        };
        int at = df;
        for (size_t k = 0; k < sizeof(kNeed) / sizeof(kNeed[0]); k++) {
            int i = findText(&cfm, at, kNeed[k]);
            CHECK(i >= 0 && i < de, "ico_char_file_image: no \"%s\" in its order", kNeed[k]);
            if (i >= 0) {
                at = i + 1;
            }
        }
    }

    /* the pack loader the dispatcher stands in for */
    static const char *const kLoop[] = {
        "for (i = 0; i < 26; i++) {",
        "if (strcmp(ext, initFunc[i].ext) == 0) {",
        "for (i = 0; i < len; i++, p--) {",
        "if (*p == '.') {",
        "static void iosCdvdMgrPackLoad(IosCdvdHandle *self)",
        "f = getPackLoader(pk->name, &kind);",
        "f(self, pk->name, pk->size, pk->id, pk->kind, pk->word08, self->seg);",
        "debugCdvdLoadInfoSegAdd(self->seg, kind, pk->size);",
    };
    int at = 0;
    for (size_t k = 0; k < sizeof(kLoop) / sizeof(kLoop[0]); k++) {
        int i = findText(&cdvd, at, kLoop[k]);
        CHECK(i >= 0, "cdvd.c: no \"%s\" after line %d", kLoop[k], at);
        if (i >= 0) {
            at = i + 1;
        }
    }

    freeSrc(&cfm);
    freeSrc(&cdvd);
    if (failures == 0) {
        printf("charfile_image_test: the four twins are their loaders' bodies, the dispatcher "
               "the pack loop's\n");
    }
    return failures ? 1 : 0;
}
