/*
 * port/save/test/state_recover_test.c
 *
 * The rescue of an old save state's progress as a memory-card save
 * (port/save/state_recover.h, port/save/tools/state_recover_tool.c,
 * tools/state_recover.py).  CPU only, no game data: the card code
 * (ico2/fumi/ios/mcard.c) runs on the fiber scheduler over card folders in
 * the working directory.  Three modes:
 *
 *   state_recover_test unit SLUG
 *     the disc SLUG (config/regions.txt) with a synthetic progress: the side
 *     file written and read back; the blobs copied into the globals, each
 *     refusal (a missing global, another size, no checkpoint, another
 *     disc's stamp, an unknown disc); what the image holds (stage, cleared
 *     flag, flags, the object records, the character record); a first write
 *     to an empty card (slot 0, the product file made with ten empty
 *     previews and the state's serial and options), the files laid out byte
 *     by byte (the disc's game block, the option words where the disc has
 *     them, the byte sum; the product record in the disc's own layout and
 *     its sum); a second write to the lowest free slot with the card's
 *     product file kept but for that slot's preview and the current file;
 *     a taken slot refused, then replaced with overwrite; both read back
 *     through the game's own loader (iosMcLoadProductBlock,
 *     iosMcLoadGameBlock); a damaged product file refused with nothing
 *     written; a card with all ten slots taken refused.
 *
 *   state_recover_test files
 *     the fixtures for the Python reader (the European disc): three state
 *     files written by the state writer (port/platform/state_file.c) with
 *     the same game data at three builds' addresses: format 1 from Android
 *     (a library loaded high), format 2 from Linux (fixed addresses, a syms
 *     section), format 1 from Windows (image base 0x140000000 loaded
 *     elsewhere); the symbol tables of those builds: a 64-bit ELF library
 *     with the game data sections and a dynamic symbol table, the same
 *     inside a package (lib/arm64-v8a/libmain.so) beside a functions-only
 *     assets/libmain.syms.gz, GNU ld's ELF map, GNU ld's PE map with the
 *     .data$ico_a / .data$ico_n brackets; a functions-only nm table; an
 *     empty card folder.  In the working directory.
 *
 *   state_recover_test check
 *     after tools/state_recover.py ran on them (CMakeLists.txt): the three
 *     side files hold the same globals byte for byte, the header fields of
 *     their state; the card save the package's run wrote (slot 0 of the
 *     empty card) is the image's block, the option words and the sum, and
 *     its product record holds the slot's preview.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#endif

#include "ico_check.h"
#include "miniz.h"
#include "region.h" /* the game's: ICO_<SET> */
#include "typedef.h"
#include "../../core/region.h"
#include "../state_recover.h"
#include "state_file.h"

/* the host record the card code keeps (typedef.h McProductFile) */
extern McProductFile IosMcProductFile[];

/* --- a synthetic progress -------------------------------------------------------- */

#define IMAGE 25596

static unsigned char s_image[IMAGE];
static int s_status[12];
static int s_preview[6];
static int s_screen = 1, s_girl = 1;
static McProductFile s_prod[2];

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const char *stamp_of(const char *slug)
{
    if (strcmp(slug, "jpn") == 0) {
        return "10/23/01 02:02:43";
    }
    if (strcmp(slug, "kor") == 0 || strcmp(slug, "cn") == 0) {
        return "10/23/01 17:47:13";
    }
    if (strcmp(slug, "ntsc") == 0) {
        return "08/18/01 22:22:20";
    }
    if (strcmp(slug, "aug6") == 0) {
        return "08/07/01 14:51:53";
    }
    if (strcmp(slug, "jun26") == 0) {
        return "06/26/01 20:00:06";
    }
    return "12/12/01 17:53:37";
}

/* the record offsets of the selected disc, laid out here from gamesys.c's
   save functions (independently of state_recover.c's ico_recover_info) */
static int at_obj(int is2001)
{
    return 18 + 4 + (is2001 ? 46 : 54);
}

static int at_char(const char *slug)
{
    const int is2001 = !strcmp(slug, "ntsc") || !strcmp(slug, "aug6") || !strcmp(slug, "jun26");
    const int jun26 = !strcmp(slug, "jun26");
    const int pal = !strcmp(slug, "pal");
    int gen = 11277;

    if (!strcmp(slug, "jpn") || !strcmp(slug, "kor") || !strcmp(slug, "cn")) {
        gen = 11208;
    } else if (!strcmp(slug, "ntsc")) {
        gen = 10365;
    } else if (!strcmp(slug, "aug6")) {
        gen = 10251;
    } else if (jun26) {
        gen = 9744;
    }
    return at_obj(is2001) + 4 + (jun26 ? 7808 : 11648) +
           (pal     ? 424
            : jun26 ? 412
                    : 416) +
           gen + (is2001 ? 0 : 120);
}

static void make_progress(const char *slug, int stage, int sofa)
{
    const int is2001 = !strcmp(slug, "ntsc") || !strcmp(slug, "aug6") || !strcmp(slug, "jun26");
    unsigned char *rec;
    unsigned char *pkt;
    int i;

    memset(s_image, 0, sizeof s_image);
    memcpy(s_image, stamp_of(slug), 18);
    put32(s_image + 18, (uint32_t)stage);
    if (!is2001) {
        put32(s_image + 22, 1); /* gFlagGameClear */
        s_image[26] = 0x0F;     /* four story flags */
        s_image[26 + 49] = 0x80;
    } else {
        s_image[22] = 0x07; /* three */
        s_image[22 + 45] = 0x01;
    }
    put32(s_image + at_obj(is2001), 777777); /* gamesysTimeCount */
    rec = s_image + at_obj(is2001) + 4;
    rec[2] = 54; /* the boy's record: no 54 */
    rec[4] = (unsigned char)stage;
    rec[64 + 2] = 148; /* Yorda's: no 148 */
    rec[64 + 4] = (unsigned char)(stage - 1);
    pkt = s_image + at_char(slug);
    put32(pkt + 8, 1234);  /* the weapon */
    put32(pkt + 0xC, 148); /* in hand */
    put32(pkt + 0x10, (uint32_t)sofa);
    for (i = at_char(slug) + 64; i < IMAGE; i++) {
        s_image[i] = (unsigned char)(i * 13 + stage); /* the block's tail, carried whole */
    }
    memset(s_status, 0, sizeof s_status);
    s_status[0] = !strcmp(slug, "ntsc") || !strcmp(slug, "jpn") || !strcmp(slug, "kor") ||
                          !strcmp(slug, "cn") || !strcmp(slug, "aug6") || !strcmp(slug, "jun26")
                      ? 0
                      : 1;
    s_status[1] = 2;
    s_status[2] = 1;
    s_status[3] = 1;
    s_status[11] = 7;
    memset(s_preview, 0, sizeof s_preview);
    s_preview[0] = 12;                  /* the card save the session loaded */
    s_preview[is2001 ? 1 : 2] = 123456; /* the play time */
    memset(s_prod, 0, sizeof s_prod);
    s_prod[0].serial = 0x2345678;
    s_prod[0].soundMode = 1;
    s_prod[0].outputMode = 0;
    s_prod[0].vibration = 1;
    s_prod[0].controlType = 1;
    for (i = 0; i < 64; i++) {
        s_prod[0].padConf[i] = (char)(i * 3 + 1);
    }
    s_prod[0].cameraMove = 1;
    s_prod[0].palMode = 1;
    for (i = 0; i < 20; i++) {
        s_prod[0].file[i].stage = 0xFFFFFFFFu;
    }
}

static void make_side(IcoRecoverSide *s, const char *slug)
{
    memset(s, 0, sizeof *s);
    snprintf(s->disc, sizeof s->disc, "%s", slug);
    snprintf(s->commit, sizeof s->commit, "v0.9.5-482-g478482c286");
    snprintf(s->platform, sizeof s->platform, "android-arm64");
    snprintf(s->source, sizeof s->source, "slot1.ics");
    s->format = 1;
    s->state_stage = 46;
    s->play_frames = 123456;
    s->main_tick = 91730;
    s->saved_at = 1793000000;
    CHECK(ico_recover_side_add(s, "gameSysMainSaveBuff", s_image, IMAGE) == 0);
    CHECK(ico_recover_side_add(s, "systemStatus", s_status, sizeof s_status) == 0);
    CHECK(ico_recover_side_add(s, "IosMcPreviewInfo", s_preview, sizeof s_preview) == 0);
    CHECK(ico_recover_side_add(s, "optionScreenMode", &s_screen, 4) == 0);
    CHECK(ico_recover_side_add(s, "girlControlMode", &s_girl, 4) == 0);
    CHECK(ico_recover_side_add(s, "IosMcProductFile", s_prod, sizeof s_prod) == 0);
}

/* --- files ------------------------------------------------------------------------- */

static long slurp(const char *path, unsigned char *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, max, f);
    fclose(f);
    return (long)n;
}

static int spit(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(p, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static void rm_card(const char *card)
{
    char path[512];
    int r, i;

    for (r = 0; r < ico_region_count(); r++) {
        const char *d = ico_region_at(r)->mc_save_dir;
        static const char *const files[] = {"icon.sys", "boy_blk.ico"};

        for (i = 0; i < 10; i++) {
            snprintf(path, sizeof path, "%s/%s/game.%03d", card, d, i);
            remove(path);
        }
        for (i = 0; i < 2; i++) {
            snprintf(path, sizeof path, "%s/%s/%s", card, d, files[i]);
            remove(path);
        }
        snprintf(path, sizeof path, "%s/%s/%s", card, d, d);
        remove(path);
        snprintf(path, sizeof path, "%s/%s", card, d);
        RMDIR(path);
    }
    RMDIR(card);
}

static const char *cpath(const char *card, const char *name)
{
    static char buf[4][512];
    static int k;
    char *out = buf[k++ & 3];

    snprintf(out, sizeof buf[0], "%s/%s/%s", card, ico_region_cur()->mc_save_dir,
             name != NULL ? name : ico_region_cur()->mc_save_dir);
    return out;
}

static uint32_t byte_sum(const unsigned char *p, long n)
{
    uint32_t s = 0;
    long i;

    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return s;
}

/* the disc's game block as gameblock_write writes it, then the sum */
static int block_bytes(void)
{
    return ICO_JUN26 ? 20480 : ICO_NTSCAUG6JUN26 ? 25596 : 25588;
}

static int game_file_bytes(void)
{
    return block_bytes() + (ICO_NTSCAUG6JUN26 ? 0 : 8) + 4;
}

static void check_game_file(const char *card, int slot)
{
    static unsigned char file[26000];
    char name[16];
    long n;

    snprintf(name, sizeof name, "game.%03d", slot);
    n = slurp(cpath(card, name), file, sizeof file);
    CHECK(n == game_file_bytes(), "%s is %ld bytes, want %d", name, n, game_file_bytes());
    if (n != game_file_bytes()) {
        return;
    }
    CHECK(memcmp(file, s_image, (size_t)block_bytes()) == 0, "%s's block is not the image", name);
    if (!ICO_NTSCAUG6JUN26) {
        CHECK(get32(file + block_bytes()) == (uint32_t)s_screen);
        CHECK(get32(file + block_bytes() + 4) == (uint32_t)s_girl);
    }
    CHECK(get32(file + n - 4) == byte_sum(file, n - 4), "%s's sum", name);
}

/* the disc's product file as product_write writes the record p: the
   European record whole, the Japanese, Korean and Asian discs' first 0x1E8
   bytes, the 2001 discs' own record (mcard.c mc_product_to_2001) */
static int product_bytes(const McProductFile *p, unsigned char *out)
{
    int i;

    if (ICO_NTSCAUG6JUN26) {
        for (i = 0; i < 20; i++) {
            put32(out + i * 16, p->file[i].stage);
            put32(out + i * 16 + 4, (uint32_t)p->file[i].cleared);
            put32(out + i * 16 + 8, (uint32_t)p->file[i].playTime);
            put32(out + i * 16 + 12, (uint32_t)p->file[i].sofa);
        }
        put32(out + 0x140, (uint32_t)p->soundMode);
        put32(out + 0x144, (uint32_t)p->outputMode);
        put32(out + 0x148, (uint32_t)p->vibration);
        memcpy(out + 0x14C, p->padConf, 64);
        return 0x18C;
    }
    memcpy(out, p, (size_t)ico_region_cur()->mc_product_size);
    return ico_region_cur()->mc_product_size;
}

static void check_product_file(const char *card, const McProductFile *want)
{
    static unsigned char file[1024], exp[1024];
    const int n = product_bytes(want, exp);
    long got = slurp(cpath(card, NULL), file, sizeof file);

    CHECK(got == n + 4, "the product file is %ld bytes, want %d", got, n + 4);
    if (got != n + 4) {
        return;
    }
    CHECK(memcmp(file, exp, (size_t)n) == 0, "the product file's record");
    CHECK(get32(file + n) == byte_sum(file, n), "the product file's sum");
}

/* the preview la_save_processing step 4 makes from the image and the state */
static void preview_of(McFileInfo *f, int stage, int sofa)
{
    int w[5];

    memcpy(w, s_preview, sizeof w);
    w[0] = stage;
    if (ICO_NTSCAUG6JUN26) {
        w[2] = sofa != 0 ? sofa : -1;
    } else {
        w[3] = sofa != 0 ? sofa : -1;
        w[1] = 1;
    }
    memcpy(f, w, sizeof *f);
}

/* --- unit -------------------------------------------------------------------------- */

static void test_side_file(const char *slug)
{
    IcoRecoverSide a, b;
    char why[256];
    int i;

    make_progress(slug, 35, 0);
    make_side(&a, slug);
    CHECK(ico_recover_side_write("state_recover_side.rescue", &a) == 0);
    CHECK(ico_recover_side_read("state_recover_side.rescue", &b, why, sizeof why) == 0, "%s", why);
    CHECK_STR(b.disc, slug);
    CHECK_STR(b.commit, "v0.9.5-482-g478482c286");
    CHECK_STR(b.platform, "android-arm64");
    CHECK_STR(b.source, "slot1.ics");
    CHECK(b.format == 1 && b.state_stage == 46 && b.play_frames == 123456 && b.main_tick == 91730 &&
          b.saved_at == 1793000000);
    CHECK(b.n_blobs == a.n_blobs);
    for (i = 0; i < a.n_blobs && i < b.n_blobs; i++) {
        CHECK_STR(b.blobs[i].name, a.blobs[i].name);
        CHECK(b.blobs[i].size == a.blobs[i].size &&
              memcmp(b.blobs[i].data, a.blobs[i].data, a.blobs[i].size) == 0);
    }
    ico_recover_side_free(&b);
    /* cut short: no "end" */
    {
        static unsigned char raw[40000];
        long n = slurp("state_recover_side.rescue", raw, sizeof raw);

        CHECK(n > 100);
        CHECK(spit("state_recover_cut.rescue", raw, (size_t)(n - 4)) == 0);
        CHECK(ico_recover_side_read("state_recover_cut.rescue", &b, why, sizeof why) != 0);
        ico_recover_side_free(&b);
        CHECK(spit("state_recover_cut.rescue", "not a side file\n", 16) == 0);
        CHECK(ico_recover_side_read("state_recover_cut.rescue", &b, why, sizeof why) != 0);
        CHECK(strstr(why, "not a side file") != NULL, "%s", why);
        ico_recover_side_free(&b);
        remove("state_recover_cut.rescue");
    }
    remove("state_recover_side.rescue");
    ico_recover_side_free(&a);
}

static void test_apply(const char *slug)
{
    IcoRecoverSide s;
    IcoRecoverInfo info;
    char why[256];

    make_progress(slug, 35, 0);
    make_side(&s, slug);
    CHECK(ico_recover_apply(&s, why, sizeof why) == 0, "%s", why);
    CHECK_STR(ico_region_cur()->slug, slug);
    ico_recover_info(&info);
    CHECK(info.stamp == 1 && info.stage == 35);
    CHECK(info.block_bytes == block_bytes());
    CHECK(info.cleared == (ICO_NTSCAUG6JUN26 ? -1 : 1));
    CHECK(info.flags_set == (ICO_NTSCAUG6JUN26 ? 4 : 5), "%d flags", info.flags_set);
    CHECK(info.time_count == 777777);
    CHECK(info.boy_stage == 35 && info.girl_stage == 34);
    CHECK(info.weapon == 1234 && info.held == 148 && info.sofa == 0);
    CHECK(info.option_words == !ICO_NTSCAUG6JUN26);
    CHECK(info.image_bytes == at_char(slug) + (ICO_NTSCAUG6JUN26 ? 24 : 32) + 36);
    CHECK(info.image_bytes <= info.block_bytes);
    ico_recover_side_free(&s);

    /* refusals */
    make_side(&s, slug);
    s.blobs[1].size = 44; /* systemStatus */
    CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
    CHECK(strstr(why, "systemStatus is 44 bytes") != NULL, "%s", why);
    s.blobs[1].size = 48;
    snprintf(s.blobs[0].name, sizeof s.blobs[0].name, "gameSysMainSaveBuffX");
    CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
    CHECK(strstr(why, "no gameSysMainSaveBuff") != NULL, "%s", why);
    snprintf(s.blobs[0].name, sizeof s.blobs[0].name, "gameSysMainSaveBuff");
    ((int *)s.blobs[1].data)[3] = 0;
    CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
    CHECK(strstr(why, "no checkpoint") != NULL, "%s", why);
    ((int *)s.blobs[1].data)[3] = 1;
    s.blobs[0].data[3] ^= 0x10;
    CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
    CHECK(strstr(why, "version stamp") != NULL, "%s", why);
    memset(s.blobs[0].data, 0, 18); /* a blank stamp is the game's own (gamesysVersionDiff) */
    CHECK(ico_recover_apply(&s, why, sizeof why) == 0, "%s", why);
    ico_recover_info(&info);
    CHECK(info.stamp == 0);
    snprintf(s.disc, sizeof s.disc, "nowhere");
    CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
    CHECK(strstr(why, "nowhere") != NULL, "%s", why);
    ico_recover_side_free(&s);
    if (!ICO_NTSCAUG6JUN26) {
        /* the option words are required where the block carries them */
        make_side(&s, slug);
        snprintf(s.blobs[3].name, sizeof s.blobs[3].name, "optionScreenModeX");
        CHECK(ico_recover_apply(&s, why, sizeof why) != 0);
        CHECK(strstr(why, "no optionScreenMode") != NULL, "%s", why);
        ico_recover_side_free(&s);
    }
}

static void apply_progress(const char *slug, int stage, int sofa)
{
    IcoRecoverSide s;
    char why[256];

    make_progress(slug, stage, sofa);
    make_side(&s, slug);
    CHECK(ico_recover_apply(&s, why, sizeof why) == 0, "%s", why);
    ico_recover_side_free(&s);
}

static void test_card(const char *slug)
{
    const char *card = "state_recover_card";
    IcoRecoverResult res;
    McProductFile want, got;
    static unsigned char block[25600];
    int r, i, result;

    rm_card(card);
    /* an empty card: slot 0, the product file made */
    apply_progress(slug, 35, 0);
    r = ico_recover_write_card(card, -1, 0, &res);
    CHECK(r == 0, "first write: %d %s", r, res.why);
    CHECK(res.slot == 0 && res.product_existed == 0 && res.icons == 0 && res.used_mask == 0);
    CHECK(res.file_bytes == game_file_bytes());
    check_game_file(card, 0);
    want = s_prod[0];
    for (i = 0; i < 10; i++) {
        want.file[i].stage = 0xFFFFFFFFu;
    }
    preview_of(&want.file[0], 35, 0);
    if (!ICO_NTSCAUG6JUN26) {
        want.fileNo = 0;
    }
    check_product_file(card, &want);

    /* the next free slot, the card's record kept but for that slot */
    apply_progress(slug, 46, 331);
    r = ico_recover_write_card(card, -1, 0, &res);
    CHECK(r == 0, "second write: %d %s", r, res.why);
    CHECK(res.slot == 1 && res.product_existed == 1 && res.used_mask == 1u);
    check_game_file(card, 1);
    preview_of(&want.file[1], 46, 331);
    if (!ICO_NTSCAUG6JUN26) {
        want.fileNo = 1;
    }
    check_product_file(card, &want);

    /* the game's own loader reads both back */
    memset(block, 0, sizeof block);
    memset(&got, 0, sizeof got);
    CHECK(ico_recover_read_card(card, 1, block, sizeof block, &got, sizeof got, &result) == 0,
          "read back: %d", result);
    CHECK(memcmp(block, s_image, (size_t)block_bytes()) == 0);
    if (!ICO_NTSCAUG6JUN26) {
        CHECK(get32(block + 25588) == (uint32_t)s_screen &&
              get32(block + 25592) == (uint32_t)s_girl);
    }
    CHECK(memcmp(&got.file[1], &want.file[1], 16) == 0, "slot 1's preview read back");
    CHECK(memcmp(&got.file[0], &want.file[0], 16) == 0, "slot 0's preview read back");
    CHECK(got.soundMode == s_prod[0].soundMode && got.vibration == s_prod[0].vibration &&
          memcmp(got.padConf, s_prod[0].padConf, 64) == 0);
    if (!ICO_NTSCAUG6JUN26) {
        CHECK(got.serial == s_prod[0].serial && got.fileNo == 1);
    }

    /* a taken slot: refused and left as it was, then replaced with overwrite */
    {
        static unsigned char before[26000], after[26000];
        long nb = slurp(cpath(card, "game.000"), before, sizeof before);

        apply_progress(slug, 47, 0);
        r = ico_recover_write_card(card, 0, 0, &res);
        CHECK(r == 1 && strstr(res.why, "slot 0 holds a save") != NULL, "%d %s", r, res.why);
        CHECK(nb > 0 && slurp(cpath(card, "game.000"), after, sizeof after) == nb &&
                  memcmp(before, after, (size_t)nb) == 0,
              "the taken slot changed");
    }
    r = ico_recover_write_card(card, 0, 1, &res);
    CHECK(r == 0 && res.slot == 0, "overwrite: %d %s", r, res.why);
    check_game_file(card, 0);
    CHECK(ico_recover_write_card(card, 10, 0, &res) == 1);

    /* a damaged product file: refused, nothing written */
    {
        static unsigned char file[1024];
        long n = slurp(cpath(card, NULL), file, sizeof file);
        struct stat st;

        CHECK(n > 8);
        file[8] ^= 0xFF;
        CHECK(spit(cpath(card, NULL), file, (size_t)n) == 0);
        r = ico_recover_write_card(card, -1, 0, &res);
        CHECK(r == 1 && strstr(res.why, "damaged") != NULL, "%d %s", r, res.why);
        CHECK(stat(cpath(card, "game.002"), &st) != 0);
        file[8] ^= 0xFF;
        CHECK(spit(cpath(card, NULL), file, (size_t)n) == 0);
    }

    /* all ten slots taken */
    for (i = 2; i < 10; i++) {
        r = ico_recover_write_card(card, -1, 0, &res);
        CHECK(r == 0 && res.slot == i, "slot %d: %d %s", i, r, res.why);
    }
    r = ico_recover_write_card(card, -1, 0, &res);
    CHECK(r == 1 && strstr(res.why, "all ten slots") != NULL, "%d %s", r, res.why);
    rm_card(card);
}

/* --- the Python fixtures --------------------------------------------------------- */

#define GDATA_RAW 0x1000u
#define GBSS_RAW 0x10000u
/* the globals' places in the two ranges (the same in every fixture build) */
#define OFF_STATUS 0x40u    /* gdata */
#define OFF_STAGE 0x10u     /* gbss: stage_no */
#define OFF_SCREEN 0x20u    /* optionScreenMode */
#define OFF_GIRL 0x30u      /* girlControlMode */
#define OFF_IMAGE 0x100u    /* gameSysMainSaveBuff */
#define OFF_PRODUCT 0x6600u /* IosMcProductFile */
#define OFF_PREVIEW 0x6A00u /* IosMcPreviewInfo */

/* the Android library's link addresses and where it was loaded */
#define ELF_GDATA 0x10e0000ull
#define ELF_GBSS 0x1ca0000ull
#define ELF_BIAS 0x7a12340000ull
/* the Linux program's (fixed) */
#define LNX_GDATA 0x17b0000ull
#define LNX_GBSS 0x2370000ull
/* the Windows program's, image base 0x140000000 loaded at 0x7ff612340000 */
#define WIN_GDATA 0x1404ff000ull
#define WIN_GBSS 0x140500000ull
#define WIN_BIAS (0x7ff612340000ull - 0x140000000ull)

static unsigned char s_gdata[GDATA_RAW];
static unsigned char s_gbss[GBSS_RAW];

static void fill_ranges(void)
{
    uint32_t i;

    for (i = 0; i < GDATA_RAW; i++) {
        s_gdata[i] = (unsigned char)(i * 5 + 1);
    }
    for (i = 0; i < GBSS_RAW; i++) {
        s_gbss[i] = (unsigned char)(i * 11 + 3);
    }
    memcpy(s_gdata + OFF_STATUS, s_status, sizeof s_status);
    put32(s_gbss + OFF_STAGE, 46);
    memcpy(s_gbss + OFF_SCREEN, &s_screen, 4);
    memcpy(s_gbss + OFF_GIRL, &s_girl, 4);
    memcpy(s_gbss + OFF_IMAGE, s_image, IMAGE);
    memcpy(s_gbss + OFF_PRODUCT, s_prod, sizeof s_prod);
    memcpy(s_gbss + OFF_PREVIEW, s_preview, sizeof s_preview);
}

/* state_host.c StateRanges (RANG, and RAN2 with two more words) */
static unsigned char s_ranges[8 + 8 * 15 + 8 + 16];

static size_t make_ranges(int ran2, uint64_t image_lo, uint64_t gdata, uint64_t gbss)
{
    uint64_t v[15];
    int i;

    memset(s_ranges, 0, sizeof s_ranges);
    put32(s_ranges, ran2 ? 0x52414E32u : 0x52414E47u);
    memset(v, 0, sizeof v);
    v[0] = image_lo;
    v[1] = image_lo + 0x4000000;
    v[9] = gdata;
    v[10] = gdata + GDATA_RAW;
    v[11] = gbss;
    v[12] = gbss + GBSS_RAW;
    for (i = 0; i < 15; i++) {
        put32(s_ranges + 8 + i * 8, (uint32_t)v[i]);
        put32(s_ranges + 12 + i * 8, (uint32_t)(v[i] >> 32));
    }
    return ran2 ? sizeof s_ranges : 8 + 8 * 15 + 8;
}

static int write_state(const char *path, int version, const char *platform, uint64_t gdata,
                       uint64_t gbss, uint64_t image_lo)
{
    IcoStateHeader h;
    IcoStateSection s[5];
    IcoStateWriteResult res;
    static unsigned char arena[256];
    static const unsigned char syms[32] = {1, 2, 3};
    int n = 0;

    ico_state_header_init(&h, "v0.9.5-482-g478482c286");
    h.version = (uint32_t)version;
    snprintf(h.platform, sizeof h.platform, "%s", platform);
    snprintf(h.disc, sizeof h.disc, "pal");
    snprintf(h.keys, sizeof h.keys, "gameplay.mirror=false\n");
    h.saved_at = 1793000000;
    h.play_frames = 123456;
    h.stage_no = 46;
    h.main_tick = 91730;
    h.vsync = 183460;
    memset(s, 0, sizeof s);
    s[n].name = "arena";
    s[n].data = arena;
    s[n].bytes = sizeof arena;
    s[n++].addr = 0x3C00000000ull;
    s[n].name = "gdata";
    s[n].data = s_gdata;
    s[n].bytes = GDATA_RAW;
    s[n++].addr = gdata;
    s[n].name = "gbss";
    s[n].data = s_gbss;
    s[n].bytes = GBSS_RAW;
    s[n++].addr = gbss;
    s[n].name = "ranges";
    s[n].data = s_ranges;
    s[n++].bytes = make_ranges(version >= 2, image_lo, gdata, gbss);
    if (version >= 2) {
        s[n].name = "syms";
        s[n].data = syms;
        s[n++].bytes = sizeof syms;
    }
    if (ico_state_file_write(path, &h, s, n) != ICO_STATE_OK || ico_state_file_flush(&res) != 1) {
        return -1;
    }
    return res.result == ICO_STATE_OK ? 0 : -1;
}

/* a 64-bit little-endian ELF library: one PT_LOAD at 0, the sections
   .dynsym, .dynstr, ico_gdata, ico_gbss (no bits) and .shstrtab */
typedef struct FixSym {
    const char *name;
    uint64_t addr;
    uint64_t size;
    int bss;
} FixSym;

static void put64(unsigned char *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static size_t make_elf(unsigned char *out, size_t max, const FixSym *sy, int n)
{
    static const char shstr[] = "\0.dynsym\0.dynstr\0ico_gdata\0ico_gbss\0.shstrtab";

    /* the names' offsets in shstr */
    enum { N_DYNSYM = 1, N_DYNSTR = 9, N_GDATA = 17, N_GBSS = 27, N_SHSTR = 36 };

    size_t dynstr_off = 64 + 56, dynstr_n = 1, dynsym_off, shstr_off, sh_off, p;
    int i;

    memset(out, 0, max);
    p = dynstr_off + 1;
    for (i = 0; i < n; i++) {
        size_t l = strlen(sy[i].name) + 1;

        memcpy(out + p, sy[i].name, l);
        p += l;
    }
    dynstr_n = p - dynstr_off;
    dynsym_off = (p + 7) & ~(size_t)7;
    p = dynsym_off + 24; /* entry 0 is null */
    {
        size_t name = 1;

        for (i = 0; i < n; i++) {
            put32(out + p, (uint32_t)name);
            out[p + 4] = 0x11; /* STB_GLOBAL, STT_OBJECT */
            put16(out + p + 6, sy[i].bss ? 4u : 3u);
            put64(out + p + 8, sy[i].addr);
            put64(out + p + 16, sy[i].size);
            name += strlen(sy[i].name) + 1;
            p += 24;
        }
    }
    shstr_off = p;
    memcpy(out + p, shstr, sizeof shstr);
    p += sizeof shstr;
    sh_off = (p + 7) & ~(size_t)7;
    /* the ELF header */
    memcpy(out,
           "\x7f"
           "ELF",
           4);
    out[4] = 2; /* 64-bit */
    out[5] = 1; /* little-endian */
    out[6] = 1;
    put16(out + 16, 3);   /* ET_DYN */
    put16(out + 18, 183); /* EM_AARCH64 */
    put32(out + 20, 1);
    put64(out + 32, 64); /* e_phoff */
    put64(out + 40, sh_off);
    put16(out + 52, 64);
    put16(out + 54, 56); /* e_phentsize */
    put16(out + 56, 1);
    put16(out + 58, 64); /* e_shentsize */
    put16(out + 60, 6);
    put16(out + 62, 5); /* e_shstrndx */
    /* PT_LOAD at 0 */
    put32(out + 64, 1);
    put32(out + 68, 6);
    put64(out + 64 + 16, 0);
    put64(out + 64 + 32, 0x4000000);
    put64(out + 64 + 40, 0x4000000);
    /* section headers: name, type, flags, addr, offset, size, link, info, align, entsize */
    {
        struct {
            uint32_t name, type;
            uint64_t addr, off, size;
            uint32_t link;
            uint64_t entsize;
        } sh[6] = {{0, 0, 0, 0, 0, 0, 0},
                   {N_DYNSYM, 11, 0, dynsym_off, (uint64_t)(n + 1) * 24, 2, 24},
                   {N_DYNSTR, 3, 0, dynstr_off, dynstr_n, 0, 0},
                   {N_GDATA, 8, ELF_GDATA, 0, GDATA_RAW, 0, 0},
                   {N_GBSS, 8, ELF_GBSS, 0, GBSS_RAW, 0, 0},
                   {N_SHSTR, 3, 0, shstr_off, sizeof shstr, 0, 0}};

        for (i = 0; i < 6; i++) {
            unsigned char *e = out + sh_off + (size_t)i * 64;

            put32(e, sh[i].name);
            put32(e + 4, sh[i].type);
            put64(e + 16, sh[i].addr);
            put64(e + 24, sh[i].off);
            put64(e + 32, sh[i].size);
            put32(e + 40, sh[i].link);
            put32(e + 44, i == 1 ? 1u : 0u);
            put64(e + 48, 8);
            put64(e + 56, sh[i].entsize);
        }
    }
    return sh_off + 6 * 64;
}

static const char *const s_code_only = "0000000000bc72f0 t __on_dlclose\n"
                                       "0000000000bc7324 T atexit\n"
                                       "0000000000c00000 T gamesysMemorySave\n"
                                       "0000000000c00100 W iosMcManager\n";

static int write_text(const char *path, const char *text)
{
    return spit(path, text, strlen(text));
}

static int write_fixtures(void)
{
    static unsigned char elf[8192];
    const FixSym syms[] = {
        {"systemStatus", ELF_GDATA + OFF_STATUS, 48, 0},
        {"stage_no", ELF_GBSS + OFF_STAGE, 4, 1},
        {"optionScreenMode", ELF_GBSS + OFF_SCREEN, 4, 1},
        {"girlControlMode", ELF_GBSS + OFF_GIRL, 4, 1},
        {"gameSysMainSaveBuff", ELF_GBSS + OFF_IMAGE, IMAGE, 1},
        {"IosMcProductFile", ELF_GBSS + OFF_PRODUCT, 992, 1},
        {"IosMcPreviewInfo", ELF_GBSS + OFF_PREVIEW, 24, 1},
    };
    const size_t elf_n = make_elf(elf, sizeof elf, syms, (int)(sizeof syms / sizeof syms[0]));
    char map[4096];
    mz_zip_archive z;
    int fails = 0;

    make_progress("pal", 35, 0);
    fill_ranges();
    fails += write_state("state_v1.ics", 1, "android-arm64", ELF_BIAS + ELF_GDATA,
                         ELF_BIAS + ELF_GBSS, ELF_BIAS) != 0;
    fails += write_state("state_v2.ics", 2, "linux-x64", LNX_GDATA, LNX_GBSS, 0x400000) != 0;
    fails += write_state("state_win.ics", 1, "windows-x64", WIN_BIAS + WIN_GDATA,
                         WIN_BIAS + WIN_GBSS, WIN_BIAS + 0x140000000ull) != 0;
    fails += spit("libmain.so", elf, elf_n) != 0;
    memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_file(&z, "fake.apk", 0) ||
        !mz_zip_writer_add_mem(&z, "AndroidManifest.xml", "<manifest/>", 11,
                               MZ_DEFAULT_COMPRESSION) ||
        !mz_zip_writer_add_mem(&z, "assets/libmain.syms.gz", s_code_only, strlen(s_code_only),
                               MZ_DEFAULT_COMPRESSION) ||
        !mz_zip_writer_add_mem(&z, "lib/arm64-v8a/libmain.so", elf, elf_n,
                               MZ_DEFAULT_COMPRESSION) ||
        !mz_zip_writer_finalize_archive(&z)) {
        fails++;
    }
    mz_zip_writer_end(&z);
    fails += write_text("code_only.syms", s_code_only) != 0;
    /* GNU ld's ELF map: the output sections ico_gdata and ico_gbss, an input
       section, the symbol lines */
    snprintf(map, sizeof map,
             "Archive member included to satisfy reference by file (symbol)\n\n"
             "Memory Configuration\n\n"
             "Name             Origin             Length             Attributes\n"
             "*default*        0x0000000000000000 0xffffffffffffffff\n\n"
             "Linker script and memory map\n\n"
             "                0x0000000000400000                PROVIDE (__executable_start = "
             "SEGMENT_START (\"text-segment\", 0x400000))\n"
             ".text           0x0000000000401000   0x100000\n"
             " .text          0x0000000000401000      0x200 libico_game.a(gamesys.c.o)\n"
             "                0x0000000000401000                gamesysMemorySave\n"
             "\n"
             "ico_gdata       0x%016llx     0x%x\n"
             " *(ico_gdata)\n"
             " ico_gdata      0x%016llx       0x80 libico_game.a(main.c.o)\n"
             "                0x%016llx                systemStatus\n"
             "\n"
             "ico_gbss        0x%016llx    0x%x\n"
             " ico_gbss\n"
             "                0x%016llx     0x6a18 libico_game.a(several.c.o)\n"
             "                0x%016llx                stage_no\n"
             "                0x%016llx                optionScreenMode\n"
             "                0x%016llx                girlControlMode\n"
             "                0x%016llx                gameSysMainSaveBuff\n"
             "                0x%016llx                IosMcProductFile\n"
             "                0x%016llx                IosMcPreviewInfo\n",
             LNX_GDATA, GDATA_RAW, LNX_GDATA, LNX_GDATA + OFF_STATUS, LNX_GBSS, GBSS_RAW, LNX_GBSS,
             LNX_GBSS + OFF_STAGE, LNX_GBSS + OFF_SCREEN, LNX_GBSS + OFF_GIRL, LNX_GBSS + OFF_IMAGE,
             LNX_GBSS + OFF_PRODUCT, LNX_GBSS + OFF_PREVIEW);
    fails += write_text("ico_pc.map", map) != 0;
    /* GNU ld's PE map: the brackets of game_data_range.c */
    snprintf(map, sizeof map,
             "Archive member included to satisfy reference by file (symbol)\n\n"
             "Linker script and memory map\n\n"
             "                0x0000000140000000                __ImageBase = 0x140000000\n"
             ".data           0x00000001404db000   0x636a00\n"
             " .data$ico_a    0x%016llx       0x10 libico_platform.a(game_data_range.c.obj)\n"
             " *fill*         0x%016llx       0x10 \n"
             " .data$ico_m    0x%016llx       0x60 libico_game.a(main.c.obj)\n"
             "                0x%016llx                systemStatus\n"
             " .data$ico_n    0x%016llx       0x10 libico_platform.a(game_data_range.c.obj)\n"
             " .data$ico_p    0x%016llx     0x6a08 libico_game.a(several.c.obj)\n"
             "                0x%016llx                stage_no\n"
             "                0x%016llx                optionScreenMode\n"
             "                0x%016llx                girlControlMode\n"
             "                0x%016llx                gameSysMainSaveBuff\n"
             "                0x%016llx                IosMcProductFile\n"
             "                0x%016llx                IosMcPreviewInfo\n"
             " .data$ico_z    0x%016llx       0x10 libico_platform.a(game_data_range.c.obj)\n",
             WIN_GDATA, WIN_GDATA + 0x10, WIN_GDATA + 0x20, WIN_GDATA + OFF_STATUS, WIN_GBSS,
             WIN_GBSS + 0x10, WIN_GBSS + OFF_STAGE, WIN_GBSS + OFF_SCREEN, WIN_GBSS + OFF_GIRL,
             WIN_GBSS + OFF_IMAGE, WIN_GBSS + OFF_PRODUCT, WIN_GBSS + OFF_PREVIEW,
             WIN_GBSS + 0x6a18);
    fails += write_text("x64.map", map) != 0;
    ico_region_select("pal");
    rm_card("card");
    CHECK(fails == 0, "%d fixture(s) not written", fails);
    return fails;
}

static void check_side(const char *path, int format, const char *platform)
{
    IcoRecoverSide s;
    char why[256];
    const IcoRecoverBlob *b;

    if (ico_recover_side_read(path, &s, why, sizeof why) != 0) {
        CHECK(0, "%s", why);
        ico_recover_side_free(&s);
        return;
    }
    CHECK_STR(s.disc, "pal");
    CHECK_STR(s.platform, platform);
    CHECK_STR(s.commit, "v0.9.5-482-g478482c286");
    CHECK(s.format == format && s.state_stage == 46 && s.play_frames == 123456 &&
              s.main_tick == 91730 && s.saved_at == 1793000000,
          "%s's header fields", path);
    b = ico_recover_side_blob(&s, "gameSysMainSaveBuff");
    CHECK(b != NULL && b->size == IMAGE && memcmp(b->data, s_image, IMAGE) == 0, "%s image", path);
    b = ico_recover_side_blob(&s, "systemStatus");
    CHECK(b != NULL && b->size == 48 && memcmp(b->data, s_status, 48) == 0, "%s status", path);
    b = ico_recover_side_blob(&s, "IosMcPreviewInfo");
    CHECK(b != NULL && b->size == 24 && memcmp(b->data, s_preview, 24) == 0, "%s preview", path);
    b = ico_recover_side_blob(&s, "IosMcProductFile");
    CHECK(b != NULL && b->size == 992 && memcmp(b->data, s_prod, 992) == 0, "%s product", path);
    b = ico_recover_side_blob(&s, "optionScreenMode");
    CHECK(b != NULL && b->size == 4 && memcmp(b->data, &s_screen, 4) == 0, "%s screen", path);
    b = ico_recover_side_blob(&s, "girlControlMode");
    CHECK(b != NULL && b->size == 4 && memcmp(b->data, &s_girl, 4) == 0, "%s girl", path);
    CHECK(ico_recover_side_blob(&s, "stage_no") == NULL, "%s carries stage_no", path);
    ico_recover_side_free(&s);
}

static void check_fixtures(void)
{
    McProductFile want;
    int i;

    make_progress("pal", 35, 0);
    ico_region_select("pal");
    check_side("side_apk.rescue", 1, "android-arm64");
    check_side("side_map.rescue", 2, "linux-x64");
    check_side("side_win.rescue", 1, "windows-x64");
    /* the package's run wrote slot 0 of the empty card */
    check_game_file("card", 0);
    want = s_prod[0];
    for (i = 0; i < 10; i++) {
        want.file[i].stage = 0xFFFFFFFFu;
    }
    preview_of(&want.file[0], 35, 0);
    want.fileNo = 0;
    check_product_file("card", &want);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "unit") == 0) {
        if (ico_region_select(argv[2]) != 0) {
            fprintf(stderr, "state_recover_test: no disc %s\n", argv[2]);
            return 1;
        }
        printf("state_recover_test: the %s disc\n", argv[2]);
        test_side_file(argv[2]);
        test_apply(argv[2]);
        test_card(argv[2]);
        return ico_check_summary("state_recover");
    }
    if (argc >= 2 && strcmp(argv[1], "files") == 0) {
        write_fixtures();
        return ico_check_summary("state_recover_files");
    }
    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        check_fixtures();
        return ico_check_summary("state_recover_check");
    }
    fprintf(stderr, "usage: state_recover_test unit SLUG | files | check\n");
    return 1;
}
