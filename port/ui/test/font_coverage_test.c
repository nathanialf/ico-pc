/* font_coverage_test.c: every character of the five languages has a glyph
 * in the port's faces.
 *
 * CPU only: font.c's cmap, no device.  Walks
 *   - ui_StringsForEach: every entry of every language's table;
 *   - the staff roll's port lines (ico_roll_port_line, the lines the roll
 *     draws after the disc's, with its bitmap font: ASCII only);
 *   - font_corpus/<lang>.txt: the in-game text that is not a string (save
 *     screen values, the gallery's asset names, the roll's '@' and '\' signs);
 *   - with a base ELF: the seDef and adpcmFile names the gallery shows.
 * Only the text the port draws with its font: the game's own text (its
 * subtitles, the disc's roll lines, its menu words) keeps its texels.
 * It fails on a code point with no glyph, U+FFFD or malformed UTF-8, a C0 or
 * C1 control other than '\n', an empty entry, a corpus file that is missing.
 * The fallback ('?' for a missing glyph, logged once) is checked last.
 *
 * The faces (package GFONT): with the base ELF and the disc image the game
 * face is built from the disc's menu sheets (game_font.h, as the first run
 * extracts it) and loaded, and every code point is checked against the two
 * faces: it passes when the game face or Arimo has it.  The report lists,
 * per face, the characters it serves, and for the game face the sheet each
 * one was cut from.  ICO_GAME_FONT_OUT=<file> also writes the face's blob
 * there (rd_replay_tool --game-font reads it).
 *
 *   font_coverage_test CORPUS_DIR [BASE_ELF [DISC_IMAGE]]
 * Exit 0, 1 on a failure, 77 for a BASE_ELF that is named and absent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "achievements.h"
#include "ico_credits.h"
#include "strings.h"

/* credits.c's reads of the achievements (the roll lines need none) */
int ico_ach_find(const char *id)
{
    (void)id;
    return -1;
}

IcoAchState ico_ach_state(int i)
{
    (void)i;
    return ICO_ACH_LOCKED;
}

void ico_ach_stats(IcoAchStats *out)
{
    memset(out, 0, sizeof(*out));
}

static int failures;
static int s_lang; /* the language the walk is in */

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *const kLangs[UI_LANG_COUNT] = {"en", "fr", "de", "it", "es"};

#define BMP_SEEN 0x3000
static unsigned char s_seen[UI_LANG_COUNT][BMP_SEEN]; /* distinct code points a language draws */
static int s_visits[UI_LANG_COUNT];
static int s_codepoints; /* code points checked, all */

/* one entry: valid UTF-8, no control but '\n', a glyph for each code point */
static void checkText(int lang, const char *src, const char *what)
{
    const char *s = src;
    uint32_t cp;
    if (s == NULL || *s == '\0') {
        CHECK(0, "%s (%s): an empty entry", what, kLangs[lang]);
        return;
    }
    while ((cp = ui_Utf8Next(&s)) != 0) {
        s_codepoints++;
        if (cp == 0xFFFD) {
            CHECK(0, "%s (%s): U+FFFD or malformed UTF-8 in \"%.40s\"", what, kLangs[lang], src);
            continue;
        }
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
            if (cp != '\n') {
                CHECK(0, "%s (%s): control U+%04X in \"%.40s\"", what, kLangs[lang], (unsigned)cp,
                      src);
            }
            continue;
        }
        if (ui_FontFaceOf(cp) < 0) {
            CHECK(0, "%s (%s): no face has U+%04X in \"%.40s\"", what, kLangs[lang], (unsigned)cp,
                  src);
        }
        if (cp < BMP_SEEN) {
            s_seen[lang][cp] = 1;
        }
    }
}

static void visit(UiLang lang, const char *utf8, void *user)
{
    (void)user;
    s_visits[lang]++;
    checkText((int)lang, utf8, "string");
}

/* the roll's line as text: the {..} codes skipped, '@' the copyright sign,
   '\' the yen sign (the bitmap font's cells), any byte outside ASCII a
   failure (the bitmap font has no cell for it) */
static void checkRollLine(const char *str, const char *what, int idx)
{
    char text[512];
    size_t n = 0;
    int brace = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p && n + 3 < sizeof(text); p++) {
        if (*p == '{') {
            brace = 1;
        } else if (*p == '}') {
            brace = 0;
        } else if (!brace) {
            if (*p == '@') {
                memcpy(text + n, "\xC2\xA9", 2);
                n += 2;
            } else if (*p == '\\') {
                memcpy(text + n, "\xC2\xA5", 2);
                n += 2;
            } else if (*p < 0x20 || *p >= 0x7F || *p == '`') {
                CHECK(0, "%s %d: byte 0x%02X is not drawn (the roll's lines are ASCII)", what, idx,
                      *p);
            } else {
                text[n++] = (char)*p;
            }
        }
    }
    text[n] = '\0';
    if (n > 0) {
        checkText(0, text, what);
    }
}

static void checkCorpus(const char *dir)
{
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.txt", dir, kLangs[l]);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL, "the corpus file %s is missing", path);
        if (!f) {
            continue;
        }
        char line[1024];
        int provenance = 0, lines = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if (line[0] == '#') {
                provenance += strstr(line, "Provenance:") != NULL;
                continue;
            }
            if (n == 0) {
                continue;
            }
            lines++;
            checkText(l, line, "corpus");
        }
        fclose(f);
        CHECK(provenance == 1, "%s: %d provenance lines, expected 1", path, provenance);
        CHECK(lines > 0, "%s has no text", path);
    }
}

#ifdef FONT_COVERAGE_ELF
#include "game_font.h"
#include "vfs.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "tables.h"

extern SeDef seDef[];

static int checkElf(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("font_coverage: no base ELF (%s): sound names skipped\n", path);
        return 77;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *elf = (uint8_t *)malloc((size_t)size);
    char err[512];
    CHECK(elf && fread(elf, 1, (size_t)size, f) == (size_t)size, "read %s", path);
    fclose(f);
    CHECK(ico_tables_load_elf(elf, (size_t)size, err, sizeof(err)) == 0, "load tables: %s", err);
    free(elf);
    int names = 0;
    for (int i = 0; i < 105; i++) {
        CHECK(memchr(adpcmFile[i].path, 0, sizeof(adpcmFile[i].path)) != NULL,
              "adpcmFile[%d].path is not terminated", i);
        checkText(0, adpcmFile[i].path, "adpcmFile path");
        names++;
    }
    for (int i = 0; i < 1426; i++) {
        if (seDef[i].name[0]) {
            checkText(0, seDef[i].name, "seDef name");
            names++;
        }
    }
    printf("font_coverage: ELF: %d sound names\n", names);
    return 0;
}

static long fileSize(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n = -1;
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            n = ftell(f);
        }
        fclose(f);
    }
    return n;
}

/* the start-up step's file (ui_GameFontPrepare, the per-user folder's
   gamefont-<V>-<SHA-1>.bin): absent, it is built and written (no temporary
   left); present, it is read; corrupt, it is built and replaced */
static void checkSidecar(IcoVfs *vfs, const uint8_t *blob, size_t size)
{
    const char *tmpdir = getenv("TMPDIR");
    char path[1024], tmp[1100];
    snprintf(path, sizeof(path), "%s/font_coverage-gamefont-%ld.bin",
             tmpdir && *tmpdir ? tmpdir : ".", (long)size);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    remove(path);
    ico_vfs_set_disc(vfs);
    CHECK(ui_GameFontPrepare(path) == 0, "sidecar: built when absent");
    CHECK(fileSize(path) == (long)size, "sidecar: written, %ld bytes", fileSize(path));
    CHECK(fileSize(tmp) < 0, "sidecar: no temporary file left");
    CHECK(ui_GameFontPrepare(path) == 0, "sidecar: read back");
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("ICGF, but not a game face", f);
        fclose(f);
    }
    CHECK(ui_GameFontPrepare(path) == 0, "sidecar: a corrupt file is built again");
    CHECK(fileSize(path) == (long)size, "sidecar: the corrupt file replaced");
    char why[256];
    CHECK(ui_GameFontWriteFile("/nonexistent-dir/x/gamefont.bin", blob, size, why, sizeof(why)) !=
              0,
          "sidecar: an unwritable folder is reported");
    ico_vfs_set_disc(NULL);
    remove(path);
}

/* the game face from the disc image, after the tables (texProperty, texFile) */
static int buildGameFace(const char *iso)
{
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, iso);
    if (!vfs) {
        printf("font_coverage: no disc image (%s): the game face is not checked\n", iso);
        return 77;
    }
    uint8_t *blob = NULL;
    size_t size = 0;
    UiGfStats st;
    char why[256];
    CHECK(ui_GameFontBuild(vfs, &blob, &size, &st, why, sizeof(why)) == 0, "game face: %s", why);
    if (blob) {
        checkSidecar(vfs, blob, size);
    }
    ico_vfs_unmount(vfs);
    if (!blob) {
        return 0;
    }
    printf("font_coverage: game face: %d rectangles, %d lines (%d exact, %d aligned with %d "
           "splits, %d unaligned), %d letters, %d characters, %d kerning pairs, %zu bytes\n",
           st.sources, st.lines, st.exact, st.aligned, st.splits, st.unaligned, st.instances,
           st.glyphs, st.kerns, size);
    CHECK(st.unaligned == 0, "game face: %d lines not aligned", st.unaligned);
    CHECK(ui_GameFaceLoad(blob, size), "game face: the blob loads");
    const char *out = getenv("ICO_GAME_FONT_OUT");
    if (out && *out) {
        FILE *f = fopen(out, "wb");
        CHECK(f && fwrite(blob, 1, size, f) == size, "write %s", out);
        if (f) {
            fclose(f);
            printf("font_coverage: game face written to %s\n", out);
        }
    }
    free(blob);
    return 0;
}
#endif

/* UTF-8 of one code point (for the report) */
static void putCp(char *out, size_t *n, size_t cap, uint32_t cp)
{
    char b[5];
    int k = 0;
    if (cp < 0x80) {
        b[k++] = (char)cp;
    } else if (cp < 0x800) {
        b[k++] = (char)(0xC0 | (cp >> 6));
        b[k++] = (char)(0x80 | (cp & 0x3F));
    } else {
        b[k++] = (char)(0xE0 | (cp >> 12));
        b[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[k++] = (char)(0x80 | (cp & 0x3F));
    }
    if (*n + (size_t)k + 2 < cap) {
        memcpy(out + *n, b, (size_t)k);
        *n += (size_t)k;
        out[(*n)++] = ' ';
        out[*n] = '\0';
    }
}

/* per face, the characters the five languages draw (tables, the roll's port
   lines, corpus) that it serves; for the game face, per sheet */
static void reportFaces(void)
{
    static char line[8192];
    size_t n = 0;
    int game = 0, arimo = 0;
    line[0] = '\0';
    if (ui_GameFaceLoaded()) {
        /* the sheets, in the order the face names them */
        const char *sheets[64];
        int nsheets = 0;
        for (uint32_t cp = 0x21; cp < BMP_SEEN; cp++) {
            const char *sh;
            int any = 0;
            for (int l = 0; l < UI_LANG_COUNT; l++) {
                any |= s_seen[l][cp];
            }
            if (!any || !ui_GameFaceSource(cp, &sh, NULL, NULL)) {
                continue;
            }
            int k = 0;
            while (k < nsheets && strcmp(sheets[k], sh) != 0) {
                k++;
            }
            if (k == nsheets && nsheets < 64) {
                sheets[nsheets++] = sh;
            }
        }
        for (int k = 0; k < nsheets; k++) {
            n = 0;
            line[0] = '\0';
            int c = 0;
            for (uint32_t cp = 0x21; cp < BMP_SEEN; cp++) {
                const char *sh;
                int any = 0;
                for (int l = 0; l < UI_LANG_COUNT; l++) {
                    any |= s_seen[l][cp];
                }
                if (any && ui_GameFaceSource(cp, &sh, NULL, NULL) && strcmp(sh, sheets[k]) == 0) {
                    putCp(line, &n, sizeof(line), cp);
                    c++;
                }
            }
            game += c;
            printf("font_coverage: game face, %d from %s: %s\n", c, sheets[k], line);
        }
        uint32_t own[512];
        const int no = ui_GameFaceChars(own, 512);
        printf("font_coverage: game face: %d characters in the atlas\n", no);
    }
    n = 0;
    line[0] = '\0';
    for (uint32_t cp = 0x21; cp < BMP_SEEN; cp++) {
        int any = 0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            any |= s_seen[l][cp];
        }
        if (any && ui_FontFaceOf(cp) == UI_FACE_ARIMO) {
            putCp(line, &n, sizeof(line), cp);
            arimo++;
        }
    }
    printf("font_coverage: %s, %d characters: %s\n",
           ui_GameFaceLoaded() ? "Arimo (the fallback)" : "Arimo (no game face)", arimo, line);
    printf("font_coverage: faces: %d characters from the game's lettering, %d from Arimo\n", game,
           arimo);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: font_coverage_test CORPUS_DIR [BASE_ELF]\n");
        return 2;
    }
    CHECK(ui_FontInit(), "the embedded font parses");
    int elfRc = 0;
#ifdef FONT_COVERAGE_ELF
    /* the tables first: the game face is built from the disc through them */
    if (argc > 2 && argv[2][0]) {
        elfRc = checkElf(argv[2]);
        if (elfRc == 0 && argc > 3 && argv[3][0]) {
            buildGameFace(argv[3]);
        }
    }
#endif

    ui_StringsForEach(visit, NULL);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        CHECK(s_visits[l] > 100, "%s: only %d strings visited", kLangs[l], s_visits[l]);
    }
    /* every table entry of every language is present (ui_StringsForEach skips
       an empty one, so a hole would pass the walk) */
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int id = 1; id < UI_STR_COUNT; id++) {
            checkText(l, ui_StrIn((UiLang)l, (UiStrId)id), "table entry");
        }
    }

    const int port = ico_roll_port_count();
    CHECK(port > 0, "the roll has port lines");
    for (int k = 0; k < port; k++) {
        char **line = ico_roll_port_line(k);
        CHECK(line && *line, "port roll line %d", k);
        if (line && *line) {
            checkRollLine(*line, "port roll line", k);
        }
    }

    checkCorpus(argv[1]);
    reportFaces();

    /* the fallback: a code point outside the subset draws as '?', logged once */
    {
        UiGlyph q, cjk, cjk2;
        uint32_t seen[8];
        CHECK(!ui_FontHasGlyph(0x4E2D), "U+4E2D is outside the subset (the test's probe)");
        int before = ui_FontMissingSeen(NULL, 0);
        CHECK(ui_FontGlyph('?', 20, &q) && ui_FontGlyph(0x4E2D, 20, &cjk), "glyphs at 20 px");
        CHECK(cjk.w == q.w && cjk.h == q.h && cjk.advance == q.advance,
              "U+4E2D at 20 px is the '?' glyph");
        CHECK(ui_FontGlyph(0x4E2D, 30, &cjk2), "glyph at 30 px");
        CHECK(ui_FontMissingSeen(seen, 8) == before + 1 && seen[before] == 0x4E2D,
              "the missing code point is recorded once, not per size or draw");
    }

    int total = 0;
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        int n = 0;
        for (int c = 0; c < BMP_SEEN; c++) {
            n += s_seen[l][c];
        }
        printf("font_coverage: %s: %d strings, %d distinct code points\n", kLangs[l], s_visits[l],
               n);
        total += n;
    }
    printf("font_coverage: %d code points checked (%d distinct summed over languages), "
           "%d failures\n",
           s_codepoints, total, failures);
    if (failures) {
        return 1;
    }
    return elfRc == 77 ? 77 : 0;
}
