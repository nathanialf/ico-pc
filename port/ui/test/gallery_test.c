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
 *     languages, has a glyph for every character (ui_font_has_glyph);
 *   - Left and Right jump between the groups.
 * With the disc image as well (argv[2]), the streams not on the disc are
 * left out and every listed bank's .hd and .bd are found in a pack
 * (port/data/df_pack.h).  Exit 77 without the ELF.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "df_pack.h"
#include "font.h"
#include "gallery.h"
#include "strings.h"
#include "tables.h"
#include "vfs.h"

/* the video mode gallery.c reads: 60 Hz, two vsyncs a Main tick */
int systemStatus[12] = {0, 2};

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

/* GalleryTables.seInBank from the disc: the bank's .hd from its pack */
static int s_hdRefused;

static int hdHas(int bank, int prog, int tone)
{
    static int cached = -1;
    static unsigned char *hd;
    static uint32_t size;
    if (bank != cached) {
        IcoDfMember m;
        free(hd);
        hd = NULL;
        size = 0;
        cached = bank;
        if (ico_df_find_member(s_vfs, seFile[bank].hdPath, &m) == 0 && (hd = malloc(m.size)) &&
            ico_df_read_member(s_vfs, &m, hd) == 0) {
            size = m.size;
        }
    }
    return hd && size ? gallery_hd_has(hd, size, prog, tone) : -1;
}

static int seInBank(int bank, int prog, int tone)
{
    int r = hdHas(bank, prog, tone);
    s_hdRefused += r == 0;
    return r;
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
        if (cp >= 0x20 && !ui_font_has_glyph(cp)) {
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

/* a stream's time on synthetic records (gallery.h) */
/* a stream of sectors that play below s_blankFrom and are blank (0xFF,
   the end flag) from it */
static long long s_blankFrom;
static int s_reads;

static int fakeSector(void *user, uint64_t off, uint8_t *buf)
{
    (void)user;
    s_reads++;
    memset(buf, (long long)(off / 0x800) >= s_blankFrom ? 0xFF : 0x0C, 0x800);
    if ((long long)(off / 0x800) < s_blankFrom) {
        for (int i = 0; i < 0x800; i += 16) {
            buf[i + 1] = 2;
        }
    }
    return 0;
}

static int discSector(void *user, uint64_t off, uint8_t *buf)
{
    return ico_df_read(s_vfs, (const char *)user, off, buf, 0x800) == 0x800 ? 0 : -1;
}

static void timeMaths(void)
{
    AdpcmDataRec r;
    memset(&r, 0, sizeof(r));
    r.sectors = 2032;
    r.pitch = 44068;
    r.channels = 2;
    CHECK(gallery_stream_bytes(&r) == 4161536.0, "the pass of 2032 sectors");
    double t = gallery_stream_seconds(&r, gallery_stream_bytes(&r));
    CHECK(t > 82.62 && t < 82.64, "battle.int's 2032 sectors at 44068 Hz: %.3f s", t);
    r.channels = 1;
    r.pitch = 18000;
    t = gallery_stream_seconds(&r, 16.0 * 18000.0 / 28.0 * 3.0);
    CHECK(t > 2.999 && t < 3.001, "mono at 18000 Hz: %.3f s", t);

    GalleryStreamClock c;
    gallery_clock_reset(&c);
    const uint32_t ring = 0x1E0000;
    gallery_clock_step(&c, 0x1000, ring, GALLERY_SPU_RING); /* not this ring: not keyed */
    CHECK(!c.started && c.played == 0, "a NAX outside the ring does not start the clock");
    uint32_t nax = ring;
    unsigned long long want = 0;
    for (int i = 0; i < 1000; i++) {
        nax = ring + (nax - ring + 1008) % GALLERY_SPU_RING; /* a Main tick at 44.1 kHz */
        gallery_clock_step(&c, nax, ring, GALLERY_SPU_RING);
        want += 1008;
    }
    CHECK(c.started && c.played == want, "1000 steps of 1008 bytes over the ring's wrap: %llu",
          c.played);
    gallery_clock_step(&c, nax, ring, GALLERY_SPU_RING);
    CHECK(c.played == want && c.step == 0, "a paused voice (NAX still) plays nothing");
    gallery_clock_step(&c, nax - 2, ring, GALLERY_SPU_RING);
    CHECK(c.played == want, "a voice looping one block (NAX back 2) plays nothing");
    c.played = 1000;
    c.step = 100;
    CHECK(gallery_clock_at_end(&c, 2, 2100.0) && !gallery_clock_at_end(&c, 2, 2101.0),
          "the end within half a step");

    uint8_t buf[0x1800];
    for (size_t i = 0; i < sizeof(buf); i += 16) {
        buf[i] = 0x0C;
        buf[i + 1] = 2;
    }
    CHECK(gallery_stream_end_block(buf, sizeof(buf), 2) == -1, "no end block");
    buf[0x1410 + 1] = 0xFF;
    CHECK(gallery_stream_end_block(buf, sizeof(buf), 2) == 0x1000, "a blank block's sector");

    uint8_t hd[0x60];
    memset(hd, 0, sizeof(hd));
    hd[0x0C] = 'S', hd[0x0D] = 'S', hd[0x0E] = 'h', hd[0x0F] = 'd';
    hd[0x1C] = 0x40;
    const unsigned short tbl[] = {2, 8, 0xFFFF, 12, 3, 0, 0, 0};
    for (int i = 0; i < 8; i++) {
        hd[0x40 + 2 * i] = (uint8_t)tbl[i];
        hd[0x41 + 2 * i] = (uint8_t)(tbl[i] >> 8);
    }
    CHECK(gallery_hd_has(hd, sizeof(hd), 0, 3) == 1 && gallery_hd_has(hd, sizeof(hd), 0, 4) == 0,
          "program 0 has tones 0 to 3");
    CHECK(gallery_hd_has(hd, sizeof(hd), 1, 0) == 0, "program 1 is absent");
    CHECK(gallery_hd_has(hd, sizeof(hd), 2, 0) == 1 && gallery_hd_has(hd, sizeof(hd), 2, 1) == 0,
          "program 2 has tone 0");
    CHECK(gallery_hd_has(hd, sizeof(hd), 3, 0) == 0, "program 3 is past the last");
    /* an SE table offset near 2^32: off + 2 must not wrap past the size check */
    hd[0x1C] = 0xFE, hd[0x1D] = 0xFF, hd[0x1E] = 0xFF, hd[0x1F] = 0xFF;
    CHECK(gallery_hd_has(hd, sizeof(hd), 0, 0) == 0, "a table offset of 0xFFFFFFFE is out");
    hd[0x1C] = 0x40, hd[0x1D] = 0, hd[0x1E] = 0, hd[0x1F] = 0;
    hd[0x0C] = 0;
    CHECK(gallery_hd_has(hd, sizeof(hd), 0, 0) == 0, "no magic, no program");

    /* the blank tail by halving: blank from sector 37 of 100, from 0, none */
    s_blankFrom = 37;
    s_reads = 0;
    CHECK(gallery_stream_blank_from(100 * 0x800, fakeSector, NULL) == 37 * 0x800 && s_reads <= 9,
          "blank from sector 37 (%d reads)", s_reads);
    s_blankFrom = 0;
    CHECK(gallery_stream_blank_from(100 * 0x800, fakeSector, NULL) == 0, "blank from the start");
    s_blankFrom = 100;
    s_reads = 0;
    CHECK(gallery_stream_blank_from(100 * 0x800, fakeSector, NULL) == -1 && s_reads == 1,
          "no blank tail: one read");
}

int main(int argc, char **argv)
{
    timeMaths();
    if (failures) {
        printf("gallery_test: %d failure(s) in the time maths\n", failures);
        return 1;
    }
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
                       s_vfs ? onDisc : NULL,
                       s_vfs ? seInBank : NULL};
    int n = gallery_build(&t);
    printf("     %d items (%s)\n", n, s_vfs ? "streams checked on the disc" : "no disc image");
    CHECK(n > 100, "a list (%d items)", n);
    CHECK(gallery_item(n - 1)->kind == GAL_K_BACK, "Back last");

    /* keys in range, groups in order, headings, duplicates */
    int lastGroup = -1, headings[GAL_G_COUNT] = {0}, items[GAL_G_COUNT] = {0};
    int section = -1;
    char buf[160], buf2[160];
    for (int i = 0; i < n; i++) {
        const GalleryItem *it = gallery_item(i);
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
            const GalleryItem *o = gallery_item(k);
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
        const GalleryItem *it = gallery_item(i);
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
        CHECK(strcmp(gallery_label(i, buf, sizeof(buf)), want) == 0,
              "stream %d is named \"%s\" (\"%s\")", it->key, want, buf);
        CHECK(gallery_col_a(i, buf2, sizeof(buf2))[0] == '\0', "stream %d has no column", it->key);
        if (it->key <= 100) {
            int scene = strstr(path, "/event2/") != NULL && it->key != 56;
            CHECK(it->group == (scene ? GAL_G_SCENE : GAL_G_SOUNDTRACK),
                  "stream %d (%s) in group %d", it->key, path, it->group);
        }
        streams++;
    }
    {
        int i47 = gallery_find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 47, -1);
        CHECK(i47 >= 0 && strcmp(gallery_label(i47, buf, sizeof(buf)), "event/39_8") == 0,
              "stream 47 is event/39_8");
        CHECK(gallery_find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 56, -1) >= 0,
              "the title theme in the soundtrack");
        CHECK(gallery_find(GAL_G_SCENE, GAL_K_STREAM, 55, -1) >= 0,
              "event2/00 in the scene sounds");
    }
    printf("     %d streams named by their files\n", streams);

    /* L1 and R1: the previous and next entry that plays */
    {
        int f = gallery_find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 1, -1);
        int lastSt = -1;
        for (int i = 0; i < n; i++) {
            if (gallery_item(i)->group == GAL_G_SOUNDTRACK &&
                gallery_item(i)->kind == GAL_K_STREAM) {
                lastSt = i;
            }
        }
        int nx = gallery_step(lastSt, 1);
        CHECK(nx >= 0 && gallery_item(nx)->group == GAL_G_SCENE &&
                  gallery_item(nx)->kind == GAL_K_STREAM,
              "R1 from the last soundtrack entry: the first scene sound (%d)", nx);
        CHECK(gallery_step(nx, -1) == lastSt, "L1 back over the heading");
        int pv = gallery_step(f, -1);
        CHECK(pv >= 0 && gallery_item(pv)->kind == GAL_K_SE && pv == n - 2,
              "L1 from the first entry wraps to the last effect (%d of %d)", pv, n);
        CHECK(gallery_step(pv, 1) == f, "R1 from the last effect wraps to the first entry");
    }

    /* every text the page shows */
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        ui_set_language((UiLang)l);
        for (int i = 0; i < n; i++) {
            unsigned cp = glyphMissing(gallery_label(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d label \"%s\": U+%04X", i, buf, cp);
            cp = glyphMissing(gallery_col_a(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d column: U+%04X", i, cp);
            cp = glyphMissing(gallery_asset(i, buf, sizeof(buf)));
            CHECK(cp == 0, "item %d asset: U+%04X", i, cp);
        }
        for (int s = UI_STR_GAL_SOUNDTRACK; s <= UI_STR_HINT_SECTION; s++) {
            const char *txt = ui_str_in((UiLang)l, (UiStrId)s);
            CHECK(txt[0] != '\0' && glyphMissing(txt) == 0, "string %d in language %d: \"%s\"", s,
                  l, txt);
        }
    }
    ui_set_language(UI_LANG_EN);

    /* Left and Right between the groups */
    int first = gallery_find(GAL_G_SOUNDTRACK, GAL_K_STREAM, 1, -1);
    int scene = gallery_jump_group(first, 1);
    CHECK(scene >= 0 && gallery_item(scene)->group == GAL_G_SCENE &&
              gallery_item(scene - 1)->kind == GAL_K_HEADING,
          "Right: the first scene sound (%d)", scene);
    CHECK(gallery_jump_group(scene, -1) == first, "Left: back to the first soundtrack entry");
    CHECK(gallery_item(gallery_jump_group(first, -1))->kind == GAL_K_BACK,
          "Left from the first: Back");

    /* with the disc: every listed bank's files are in a pack */
    if (s_vfs) {
        int banks = 0;
        for (int i = 0; i < n; i++) {
            const GalleryItem *it = gallery_item(i);
            if (it->kind != GAL_K_SE || (i > 0 && gallery_item(i - 1)->kind == GAL_K_SE &&
                                         gallery_item(i - 1)->bank == it->bank)) {
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
    /* with the disc: every listed effect's row is in its bank's header,
       and the streams' files: the table's pass plus the ring pad, and no
       end block inside the pass but event/40's blank */
    if (s_vfs) {
        int listed = 0;
        for (int i = 0; i < n; i++) {
            const GalleryItem *it = gallery_item(i);
            if (it->kind != GAL_K_SE) {
                continue;
            }
            int row = listRowOf(it->bank, seDef[it->key].kind);
            CHECK(row >= 0 && hdHas(it->bank, seList[row].prog, seList[row].tone) == 1,
                  "effect %d (%s) of bank %d: program %d tone %d in the header", it->key,
                  seDef[it->key].name, it->bank, row >= 0 ? seList[row].prog : -1,
                  row >= 0 ? seList[row].tone : -1);
            listed++;
        }
        printf("     %d effects listed, %d rows left out (not in their bank's header)\n", listed,
               s_hdRefused);
        CHECK(s_hdRefused > 0, "some rows are not in their bank's header (%d)", s_hdRefused);
        int ended = 0;
        for (int no = 1; no <= 104; no++) {
            const char *b = strrchr(adpcmFile[no].path, '/');
            b = b ? b + 1 : adpcmFile[no].path;
            if (!onDisc(no)) {
                continue;
            }
            long long pass = (long long)gallery_stream_bytes(&adpcmFile[no]);
            long long disc = (long long)ico_df_size(s_vfs, b);
            CHECK(disc == pass + 0x5C000, "stream %d (%s): %lld bytes, the pass %lld + 0x5C000", no,
                  b, disc, pass);
            unsigned char *buf = malloc((size_t)pass);
            long e = -1;
            if (buf && ico_df_read(s_vfs, b, 0, buf, (size_t)pass) == pass) {
                e = gallery_stream_end_block(buf, (size_t)pass, adpcmFile[no].channels);
            } else {
                CHECK(0, "stream %d: read", no);
            }
            free(buf);
            if (e >= 0) {
                ended++;
                printf("     stream %d (%s): an end block at byte 0x%lX, %.1f s of %.1f s\n", no,
                       adpcmFile[no].path, e, gallery_stream_seconds(&adpcmFile[no], (double)e),
                       gallery_stream_seconds(&adpcmFile[no], (double)pass));
            }
            CHECK(e < 0 || (no == 50 && e == 0x93000), "stream %d: an end block at 0x%lX", no, e);
            /* the engine's halving from the pass's last sector finds the same */
            CHECK(gallery_stream_blank_from((uint64_t)pass, discSector, (void *)b) == e,
                  "stream %d: the blank tail by halving at the whole scan's 0x%lX", no, e);
        }
        CHECK(ended == 1, "one stream ends early on the disc (%d)", ended);
    }
    free(elf);
    if (failures) {
        printf("gallery_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gallery_test: ok\n");
    return 0;
}
