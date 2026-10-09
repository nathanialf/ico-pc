/* font_coverage_test.c: every character of the five languages has a glyph
 * in the port's faces.
 *
 * CPU only: font.c's cmap, no device.  Walks
 *   - ui_strings_for_each: every entry of every language's table;
 *   - the staff roll's port lines (ico_roll_port_line, the lines the roll
 *     draws after the disc's, with its bitmap font: ASCII only);
 *   - font_corpus/<lang>.txt: the in-game text that is not a string (save
 *     screen values, the gallery's asset names, the roll's '@' and '\' signs);
 *   - with a base ELF: the seDef and adpcmFile names the gallery shows.
 * Only the text the port draws with its font: the game's own text (its
 * subtitles, the disc's roll lines) keeps its texels; its menu words are
 * the UI_STR_MT_* strings above.
 * It fails on a code point with no glyph, U+FFFD or malformed UTF-8, a C0 or
 * C1 control other than '\n', an empty entry, a corpus file that is missing.
 * The fallback ('?' for a missing glyph, logged once) is checked last.
 *
 * Arimo is the only face (the menus' text is Arimo in the sheets' look);
 * the report lists the characters it serves.
 *
 *   font_coverage_test CORPUS_DIR [BASE_ELF]
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
    while ((cp = ui_utf8_next(&s)) != 0) {
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
        if (!ui_font_has_glyph(cp)) {
            CHECK(0, "%s (%s): Arimo has no U+%04X in \"%.40s\"", what, kLangs[lang], (unsigned)cp,
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

/* the characters the five languages draw (tables, the roll's port lines,
   corpus) that Arimo serves */
static void reportFaces(void)
{
    static char line[8192];
    size_t n = 0;
    int arimo = 0;
    line[0] = '\0';
    for (uint32_t cp = 0x21; cp < BMP_SEEN; cp++) {
        int any = 0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            any |= s_seen[l][cp];
        }
        if (any && ui_font_has_glyph(cp)) {
            putCp(line, &n, sizeof(line), cp);
            arimo++;
        }
    }
    printf("font_coverage: Arimo, %d characters: %s\n", arimo, line);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: font_coverage_test CORPUS_DIR [BASE_ELF]\n");
        return 2;
    }
    CHECK(ui_font_init(), "the embedded font parses");
    int elfRc = 0;
#ifdef FONT_COVERAGE_ELF
    if (argc > 2 && argv[2][0]) {
        elfRc = checkElf(argv[2]);
    }
#endif

    ui_strings_for_each(visit, NULL);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        CHECK(s_visits[l] > 100, "%s: only %d strings visited", kLangs[l], s_visits[l]);
    }
    /* every table entry of every language is present (ui_strings_for_each skips
       an empty one, so a hole would pass the walk) */
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int id = 1; id < UI_STR_COUNT; id++) {
            checkText(l, ui_str_in((UiLang)l, (UiStrId)id), "table entry");
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
        CHECK(!ui_font_has_glyph(0x4E2D), "U+4E2D is outside the subset (the test's probe)");
        int before = ui_font_missing_seen(NULL, 0);
        CHECK(ui_font_glyph('?', 20, &q) && ui_font_glyph(0x4E2D, 20, &cjk), "glyphs at 20 px");
        CHECK(cjk.w == q.w && cjk.h == q.h && cjk.advance == q.advance,
              "U+4E2D at 20 px is the '?' glyph");
        CHECK(ui_font_glyph(0x4E2D, 30, &cjk2), "glyph at 30 px");
        CHECK(ui_font_missing_seen(seen, 8) == before + 1 && seen[before] == 0x4E2D,
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
