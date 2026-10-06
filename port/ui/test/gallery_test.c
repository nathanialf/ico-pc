/*
 * port/ui/test/gallery_test.c
 *
 * ctest gallery (CPU): the music gallery's list (port/ui/gallery.c) built
 * from the PAL base ELF's tables as the game loads them (port/data/
 * tables.c, as tables_test does), and checked:
 *   - every item's key is in range for its kind (a stream on the table, an
 *     effect whose bank has a row for its kind, an ambience's seEnv row
 *     playing it);
 *   - the groups come in order, each with one heading (one per bank for the
 *     sound effects), and Back last;
 *   - no key twice in a group (in a bank's section for the effects);
 *   - every stream is named by its asset (its file under sound/ICO_ADPCM/
 *     without the .int, event/39_8), with no column; the score (battle.int,
 *     event/, the title theme) is the soundtrack, event2/ the scene sounds;
 *   - L1 and R1 step to the previous and next entry that plays;
 *   - every label, column, heading and gallery string, in the five
 *     languages, has a glyph for every character (ui_FontHasGlyph);
 *   - Left and Right jump between the groups.
 * With the disc image as well (argv[2]), the streams not on the disc are
 * left out and every listed bank's .hd and .bd are found in a pack
 * (port/data/df_pack.h).  Exit 77 without the ELF.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "df_pack.h"
#include "font.h"
#include "gallery.h"
#include "strings.h"
#include "tables.h"
#include "vfs.h"

extern SeDef seDef[];

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

static IcoVfs *s_vfs;

static int onDisc(int no)
{
    const char *p = strrchr(adpcmFile[no].path, '/');
    return ico_df_has(s_vfs, p ? p + 1 : adpcmFile[no].path) == 1;
}

static unsigned char *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = malloc((size_t)n);
    if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

/* every code point of a UTF-8 string has a glyph; the first missing one */
static unsigned glyphMissing(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = *p++;
        int more = 0;
        if (cp >= 0xF0) {
            cp &= 7;
            more = 3;
        } else if (cp >= 0xE0) {
            cp &= 15;
            more = 2;
        } else if (cp >= 0xC0) {
            cp &= 31;
            more = 1;
        }
        while (more-- > 0 && *p) {
            cp = cp << 6 | (*p++ & 63);
        }
        if (cp >= 0x20 && !ui_FontHasGlyph(cp)) {
            return cp;
        }
    }
    return 0;
}

static int listRowOf(int bank, int idx)
{
    for (int j = 0; j < 3837; j++) {
        if (seList[j].num == bank && seList[j].idx == idx) {
            return j;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    char err[512];
    size_t size;
    unsigned char *elf = argc > 1 ? readFile(argv[1], &size) : NULL;
    if (elf == NULL) {
        printf("gallery_test: no base ELF (%s); skipped\n", argc > 1 ? argv[1] : "none given");
        return 77;
    }
    if (ico_tables_load_elf(elf, size, err, sizeof(err)) != 0) {
        printf("gallery_test: the table loader refused the ELF: %s\n", err);
        return 1;
    }
    if (argc > 2) {
        FILE *f = fopen(argv[2], "rb");
        if (f != NULL) {
            fclose(f);
            s_vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[2]);
        }
    }
    GalleryTables t = {adpcmFile,
                       105,
                       seFile,
                       104,
                       seDef,
                       1426,
                       seList,
                       3837,
                       seEnv,
                       425,
                       stageData,
                       106,
                       s_vfs ? onDisc : NULL};
    int n = gallery_Build(&t);
    printf("     %d items (%s)\n", n, s_vfs ? "streams checked on the disc" : "no disc image");
    CHECK(n > 100, "a list (%d items)", n);
    CHECK(gallery_Item(n - 1)->kind == GAL_K_BACK, "Back last");

    /* keys in range, groups in order, headings, duplicates */
    int lastGroup = -1, headings[GAL_G_COUNT] = {0}, items[GAL_G_COUNT] = {0};
    int section = -1;
    char buf[160], buf2[160];
    for (int i = 0; i < n; i++) {
        const GalleryItem *it = gallery_Item(i);
        CHECK(it->group >= lastGroup, "item %d: group %d after %d", i, it->group, lastGroup);
        if (it->group != lastGroup && it->group != GAL_G_BACK) {
            CHECK(it->kind == GAL_K_HEADING, "item %d: group %d opens with a heading", i,
                  it->group);
        }
        lastGroup = it->group;
        if (it->kind == GAL_K_HEADING) {
            headings[it->group]++;
            section = i;
            if (it->group == GAL_G_SE) {
                CHECK(it->bank > 0 && it->bank < 104, "item %d: an effects heading names a bank",
                      i);
            }
            continue;
        }
        items[it->group]++;
        if (it->kind == GAL_K_STREAM) {
            CHECK(it->key >= 1 && it->key <= 104 && adpcmFile[it->key].channels > 0,
                  "item %d: stream %d", i, it->key);
            CHECK(it->group == (it->key > 100 ? GAL_G_VOICE : it->group),
                  "item %d: stream %d in group %d", i, it->key, it->group);
            if (s_vfs) {
                CHECK(onDisc(it->key), "item %d: stream %d is on the disc", i, it->key);
            }
        } else if (it->kind == GAL_K_SE) {
            CHECK(it->key > 0 && it->key < 1426 && it->bank > 0 && it->bank < 104,
                  "item %d: effect %d bank %d", i, it->key, it->bank);
            CHECK(listRowOf(it->bank, seDef[it->key].kind) >= 0,
                  "item %d: bank %d has effect %d's kind", i, it->bank, it->key);
            if (it->group == GAL_G_AMBIENCE) {
                CHECK(it->env >= 0 && it->env < 425 && seEnv[it->env].se == it->key &&
                          it->stage > 0 && it->env >= stageData[it->stage].seEnvFirst &&
                          it->env < stageData[it->stage].seEnvLast,
                      "item %d: ambience %d from seEnv %d of stage %d", i, it->key, it->env,
                      it->stage);
            }
        }
        /* no key twice in its group's section */
        for (int k = section + 1; k < i; k++) {
            const GalleryItem *o = gallery_Item(k);
            CHECK(!(o->kind == it->kind && o->key == it->key), "item %d: key %d twice (item %d)", i,
                  it->key, k);
        }
    }
    for (int g = GAL_G_SOUNDTRACK; g <= GAL_G_VOICE; g++) {
        CHECK(headings[g] == 1, "group %d has one heading (%d)", g, headings[g]);
        CHECK(items[g] > 0, "group %d has entries", g);
    }
    CHECK(headings[GAL_G_SE] > 40 && items[GAL_G_SE] > 1000, "sound effects: %d banks, %d entries",
          headings[GAL_G_SE], items[GAL_G_SE]);
    CHECK(items[GAL_G_SOUNDTRACK] + items[GAL_G_SCENE] == (s_vfs ? 96 : 100),
          "the streams 1..100 on the list (%d + %d)", items[GAL_G_SOUNDTRACK], items[GAL_G_SCENE]);
    printf(
        "     soundtrack %d, scene sounds %d, ambience %d, voice %d, sound effects %d in %d banks\n",
        items[GAL_G_SOUNDTRACK], items[GAL_G_SCENE], items[GAL_G_AMBIENCE], items[GAL_G_VOICE],
        items[GAL_G_SE], headings[GAL_G_SE]);

    /* the names: every stream by its asset, with no column, in the group
       its folder gives (the title theme, event2/50.int, with the score) */
    int streams = 0;
    for (int i = 0; i < n; i++) {
        const GalleryItem *it = gallery_Item(i);
        if (it->kind != GAL_K_STREAM) {
            continue;
        }
        const char *path = adpcmFile[it->key].path;
        char want[64];
        snprintf(want, sizeof(want), "%s", path + strlen("sound/ICO_ADPCM/"));
        char *dot = strrchr(want, '.');
        CHECK(strncmp(path, "sound/ICO_ADPCM/", 16) == 0 && dot && strcmp(dot, ".int") == 0,
              "stream %d's file %s", it->key, path);
        if (dot) {
            *dot = '\0';
        }
        CHECK(strcmp(gallery_Label(i, buf, sizeof(buf)), want) == 0,
              "stream %d is named \"%s\" (\"%s\")", it->key, want, buf);
        CHECK(gallery_ColA(i, buf2, sizeof(buf2))[0] == '\0', "stream %d has no column", it->key);
        if (it->key <= 100) {
            int scene = strstr(path, "/event2/") != NULL && it->key != 56;
            CHECK(it->group == (scene ? GAL_G_SCENE : GAL_G_SOUNDTRACK),
                  "stream %d (%s) in group %d", it->key, path, it->group);
        }
        streams++;
    }
    {
        int i47 = gallery_Find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 47, -1);
        CHECK(i47 >= 0 && strcmp(gallery_Label(i47, buf, sizeof(buf)), "event/39_8") == 0,
              "stream 47 is event/39_8");
        CHECK(gallery_Find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 56, -1) >= 0,
              "the title theme in the soundtrack");
        CHECK(gallery_Find(GAL_G_SCENE, GAL_K_STREAM, 55, -1) >= 0,
              "event2/00 in the scene sounds");
    }
    printf("     %d streams named by their files\n", streams);

    /* L1 and R1: the previous and next entry that plays */
    {
        int f = gallery_Find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 1, -1);
        int lastSt = -1;
        for (int i = 0; i < n; i++) {
            if (gallery_Item(i)->group == GAL_G_SOUNDTRACK &&
                gallery_Item(i)->kind == GAL_K_STREAM) {
                lastSt = i;
            }
        }
        int nx = gallery_Step(lastSt, 1);
        CHECK(nx >= 0 && gallery_Item(nx)->group == GAL_G_SCENE &&
                  gallery_Item(nx)->kind == GAL_K_STREAM,
              "R1 from the last soundtrack entry: the first scene sound (%d)", nx);
        CHECK(gallery_Step(nx, -1) == lastSt, "L1 back over the heading");
        int pv = gallery_Step(f, -1);
        CHECK(pv >= 0 && gallery_Item(pv)->kind == GAL_K_SE && pv == n - 2,
              "L1 from the first entry wraps to the last effect (%d of %d)", pv, n);
        CHECK(gallery_Step(pv, 1) == f, "R1 from the last effect wraps to the first entry");
    }

    /* every text the page shows */
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        ui_SetLanguage((UiLang)l);
        for (int i = 0; i < n; i++) {
            unsigned cp = glyphMissing(gallery_Label(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d label \"%s\": U+%04X", i, buf, cp);
            cp = glyphMissing(gallery_ColA(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d column: U+%04X", i, cp);
            cp = glyphMissing(gallery_Asset(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d asset: U+%04X", i, cp);
        }
        for (int s = UI_STR_GAL_SOUNDTRACK; s <= UI_STR_HINT_SECTION; s++) {
            const char *txt = ui_StrIn((UiLang)l, (UiStrId)s);
            CHECK(txt[0] != '\0' && glyphMissing(txt) == 0, "string %d in language %d: \"%s\"", s,
                  l, txt);
        }
    }
    ui_SetLanguage(UI_LANG_EN);

    /* Left and Right between the groups */
    int first = gallery_Find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 1, -1);
    int scene = gallery_JumpGroup(first, 1);
    CHECK(scene >= 0 && gallery_Item(scene)->group == GAL_G_SCENE &&
              gallery_Item(scene - 1)->kind == GAL_K_HEADING,
          "Right: the first scene sound (%d)", scene);
    CHECK(gallery_JumpGroup(scene, -1) == first, "Left: back to the first soundtrack entry");
    CHECK(gallery_Item(gallery_JumpGroup(first, -1))->kind == GAL_K_BACK,
          "Left from the first: Back");

    /* with the disc: every listed bank's files are in a pack */
    if (s_vfs) {
        int banks = 0;
        for (int i = 0; i < n; i++) {
            const GalleryItem *it = gallery_Item(i);
            if (it->kind != GAL_K_SE || (i > 0 && gallery_Item(i - 1)->kind == GAL_K_SE &&
                                         gallery_Item(i - 1)->bank == it->bank)) {
                continue;
            }
            IcoDfMember m;
            CHECK(ico_df_find_member(s_vfs, seFile[it->bank].hdPath, &m) == 0 &&
                      ico_df_find_member(s_vfs, seFile[it->bank].bdPath, &m) == 0,
                  "bank %d (%s) is in a pack", it->bank, seFile[it->bank].hdPath);
            banks++;
        }
        printf("     %d bank runs checked against %d packs (%d members)\n", banks,
               ico_df_index_packs(), ico_df_index_members());
    }
    free(elf);
    if (failures) {
        printf("gallery_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gallery_test: ok\n");
    return 0;
}
