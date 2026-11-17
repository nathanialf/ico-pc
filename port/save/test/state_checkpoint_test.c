/*
 * port/save/test/state_checkpoint_test.c
 *
 * Where a save state keeps the game's checkpoint (port/save/state_checkpoint.h),
 * without the game.  CPU only; the state files it writes go to the folder
 * given (state_file.c's writer, read back by ico_ckpt_state_read).
 *
 *   globals   the six globals' names and sizes
 *   table     the committed table (tools/state_checkpoint_table.py): every
 *             entry inside its own ranges, an entry for every release from
 *             v0.9.6 to v0.11.8 on Android, Windows and Linux, the lookup by
 *             commit text and platform (the reporter's v0.11.5 build: the
 *             image at gbss + 0x2ac5d0, tools/state_recover.py's place on his
 *             state), none for another platform or an empty commit
 *   record    the "ckpt" section written and read back; a damaged one (magic,
 *             version, cut short, a global at another size) refused; an
 *             unknown name skipped, a missing global left unknown
 *   search    the image by its contents: one (the game's own stamp table
 *             beside it rejected), none, two that differ, two identical
 *             copies, a corrupt image (stage 0, past the disc's last stage,
 *             a cleared word of 7, cut by the section's end), a 2001 disc
 *             (no cleared word)
 *   route     every IcoStateResult for this version's state and another's;
 *             another build's syms verdict, each refusal reason by its kind
 *             (legacy table, page words, layout first): checkpoint, a
 *             translation: exact
 *   boxes     the one box each route leaves (exact: the ordinary words;
 *             the checkpoint: its notice alone, even with the exact path's
 *             blocker recorded; a load that did nothing: its refusal), and
 *             the notice's words in the eight languages: not opening as the
 *             refusal does, the version named once, the page's line plain
 *   syms      the block list of a syms record (versions 2 and 3 with units,
 *             data and points before the blocks): found, absent, damaged
 *   states    three state files through the whole decision: format 1 (no
 *             syms, no record: this program's layout, then the bytes alone
 *             with the checkpoint unknown), format 2 (the table's entry of
 *             its commit; its difficulty record through the syms block list;
 *             systemStatus word 3 clear: no checkpoint), and a state of
 *             v0.11.9 on (its own record: found with no table at all); a
 *             release candidate's state (a commit no table names) at
 *             v0.11.8's Android sizes found by the two released layouts that
 *             agree; a layout its bytes do not confirm (stage_no) passed
 *             over for the search; another disc refused
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ico_check.h"
#include "state_checkpoint.h"
#include "state_file.h"
#include "state_xbuild.h"
#include "strings.h" /* port/ui: the boxes' words */

#define GDATA 0x400u
#define GBSS 0x10000u
#define IMG_AT 0x1230u
#define STATUS_AT 0x40u
#define PREVIEW_AT 0x100u
#define STAGE_AT 0x20u
#define SCREEN_AT 0x30u
#define GIRL_AT 0x34u

static unsigned char s_gdata[GDATA], s_gbss[GBSS];

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_image(unsigned char *p, const char *disc, int stage, uint32_t cleared,
                      unsigned seed)
{
    unsigned i;

    for (i = 0; i < ICO_CKPT_IMAGE_BYTES; i++) {
        p[i] = (unsigned char)(i * 31u + seed);
    }
    memcpy(p, ico_ckpt_stamp(disc), ICO_CKPT_STAMP_BYTES);
    wr32(p + 18, (uint32_t)stage);
    if (!ico_ckpt_disc_2001(disc)) {
        wr32(p + 22, cleared);
    }
}

/* gdata: the game's own stamp table (gamesys.c: kor, jpn, pal, ntsc, aug6,
   jun26, each with its NUL), systemStatus, stage_no and the option words;
   gbss: the image and the preview */
static void make_memory(const char *disc, int stage_no, int stage, uint32_t word3)
{
    static const char *const order[6] = {"kor", "jpn", "pal", "ntsc", "aug6", "jun26"};
    int i;

    memset(s_gdata, 0, sizeof s_gdata);
    memset(s_gbss, 0, sizeof s_gbss);
    for (i = 0; i < 6; i++) {
        memcpy(s_gdata + 0x200 + 18 * i, ico_ckpt_stamp(order[i]), ICO_CKPT_STAMP_BYTES);
    }
    wr32(s_gdata + STATUS_AT + 12, word3);
    wr32(s_gdata + STAGE_AT, (uint32_t)stage_no);
    wr32(s_gdata + SCREEN_AT, 2);
    wr32(s_gdata + GIRL_AT, 1);
    for (i = 0; i < 24; i++) {
        s_gbss[PREVIEW_AT + i] = (unsigned char)(0xA0 + i);
    }
    put_image(s_gbss + IMG_AT, disc, stage, 0, 7);
}

static void self_places(IcoCkptPlace *g)
{
    g[ICO_CKPT_G_IMAGE] = (IcoCkptPlace){1, IMG_AT, ICO_CKPT_IMAGE_BYTES};
    g[ICO_CKPT_G_STATUS] = (IcoCkptPlace){0, STATUS_AT, 48};
    g[ICO_CKPT_G_PREVIEW] = (IcoCkptPlace){1, PREVIEW_AT, 24};
    g[ICO_CKPT_G_SCREEN] = (IcoCkptPlace){0, SCREEN_AT, 4};
    g[ICO_CKPT_G_GIRL] = (IcoCkptPlace){0, GIRL_AT, 4};
    g[ICO_CKPT_G_STAGE] = (IcoCkptPlace){0, STAGE_AT, 4};
}

static IcoCkptLayout s_test_table[1];

static void make_layout(IcoCkptLayout *l, const char *tag, const char *commit, const char *platform)
{
    memset(l, 0, sizeof *l);
    l->tag = tag;
    l->commit = commit;
    l->platform = platform;
    l->gdata_bytes = GDATA;
    l->gbss_bytes = GBSS;
    self_places(l->g);
    l->asset = "test-asset";
    l->sha256 = "0";
}

/* --- globals, table, record ----------------------------------------------------------- */

static void test_globals(void)
{
    CHECK_STR(ico_ckpt_global_name(ICO_CKPT_G_IMAGE), "gameSysMainSaveBuff");
    CHECK(ico_ckpt_global_size(ICO_CKPT_G_IMAGE) == 25596u);
    CHECK(ico_ckpt_global_size(ICO_CKPT_G_STATUS) == 48u);
    CHECK(ico_ckpt_global_size(ICO_CKPT_G_PREVIEW) == 24u);
    CHECK(ico_ckpt_global_size(ICO_CKPT_G_STAGE) == 4u);
    CHECK_STR(ico_ckpt_global_name(ICO_CKPT_G_COUNT), "");
    CHECK_STR(ico_ckpt_stamp("cn"), ico_ckpt_stamp("kor"));
    CHECK(ico_ckpt_stamp("ps3") == NULL);
    CHECK(ico_ckpt_disc_2001("ntsc") && !ico_ckpt_disc_2001("pal"));
    CHECK(ico_ckpt_disc_last_stage("pal") == 105 && ico_ckpt_disc_last_stage("jun26") == 102);
}

static void test_table(void)
{
    static const char *const releases[] = {"v0.9.6",  "v0.9.7",  "v0.10.0", "v0.10.1", "v0.10.2",
                                           "v0.10.3", "v0.10.4", "v0.10.5", "v0.11.0", "v0.11.1",
                                           "v0.11.2", "v0.11.3", "v0.11.4", "v0.11.5", "v0.11.6",
                                           "v0.11.7", "v0.11.8"};
    static const char *const plats[] = {"android-arm64", "windows-x64", "linux-x64"};
    const IcoCkptLayout *e;
    size_t r, p;
    int i, k;

    CHECK(ico_ckpt_table_count >= 51, "%d entries", ico_ckpt_table_count);
    for (i = 0; i < ico_ckpt_table_count; i++) {
        const IcoCkptLayout *t = &ico_ckpt_table[i];

        CHECK(t->g[ICO_CKPT_G_IMAGE].range == 0 || t->g[ICO_CKPT_G_IMAGE].range == 1,
              "%s %s: no image", t->tag, t->platform);
        CHECK(strlen(t->sha256) == 64, "%s %s: sha256", t->tag, t->platform);
        for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
            const IcoCkptPlace *g = &t->g[k];
            const uint32_t lim = g->range == 1 ? t->gbss_bytes : t->gdata_bytes;

            if (g->range == ICO_CKPT_RANGE_NONE) {
                continue;
            }
            CHECK(g->size == ico_ckpt_global_size(k) && g->off <= lim && lim - g->off >= g->size,
                  "%s %s: %s at %d+0x%x outside its range", t->tag, t->platform,
                  ico_ckpt_global_name(k), g->range, g->off);
        }
    }
    for (r = 0; r < sizeof releases / sizeof releases[0]; r++) {
        for (p = 0; p < sizeof plats / sizeof plats[0]; p++) {
            int found = 0;

            for (i = 0; i < ico_ckpt_table_count; i++) {
                found |= strcmp(ico_ckpt_table[i].tag, releases[r]) == 0 &&
                         strcmp(ico_ckpt_table[i].platform, plats[p]) == 0;
            }
            CHECK(found, "no %s %s entry", releases[r], plats[p]);
        }
    }
    e = ico_ckpt_table_find(ico_ckpt_table, ico_ckpt_table_count, "v0.11.4-211-g194e7bbe41",
                            "android-arm64");
    CHECK(e != NULL && strcmp(e->tag, "v0.11.5") == 0);
    CHECK(e != NULL && e->g[ICO_CKPT_G_IMAGE].range == 1 &&
          e->g[ICO_CKPT_G_IMAGE].off == 0x2ac5d0u);
    CHECK(ico_ckpt_table_find(ico_ckpt_table, ico_ckpt_table_count, "v0.11.4-211-g194e7bbe41",
                              "ios-arm64") == NULL);
    CHECK(ico_ckpt_table_find(ico_ckpt_table, ico_ckpt_table_count, "", "android-arm64") == NULL);
}

static void test_record(void)
{
    unsigned char rec[ICO_CKPT_RECORD_BYTES + 32];
    IcoCkptPlace g[ICO_CKPT_G_COUNT], back[ICO_CKPT_G_COUNT];
    int k;

    self_places(g);
    g[ICO_CKPT_G_GIRL].range = ICO_CKPT_RANGE_NONE;
    CHECK(ico_ckpt_record_write(g, rec, 10) == 0);
    CHECK(ico_ckpt_record_write(g, rec, sizeof rec) == ICO_CKPT_RECORD_BYTES);
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES, back) == 0);
    for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
        CHECK(back[k].range == g[k].range, "%s range", ico_ckpt_global_name(k));
        if (g[k].range != ICO_CKPT_RANGE_NONE) {
            CHECK(back[k].off == g[k].off && back[k].size == g[k].size, "%s",
                  ico_ckpt_global_name(k));
        }
    }
    /* cut short, another magic, another version */
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES - 1, back) == -1);
    CHECK(back[ICO_CKPT_G_IMAGE].range == ICO_CKPT_RANGE_NONE);
    rec[0] ^= 1;
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES, back) == -1);
    rec[0] ^= 1;
    rec[4] = 2;
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES, back) == -1);
    rec[4] = 1;
    /* the image at another size: damaged */
    wr32(rec + 16 + 28, 1000);
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES, back) == -1);
    wr32(rec + 16 + 28, ICO_CKPT_IMAGE_BYTES);
    /* the preview's entry renamed: skipped, the preview unknown, the rest read */
    memcpy(rec + 16 + 32 * ICO_CKPT_G_PREVIEW, "somethingElse", 14);
    CHECK(ico_ckpt_record_read(rec, ICO_CKPT_RECORD_BYTES, back) == 0);
    CHECK(back[ICO_CKPT_G_PREVIEW].range == ICO_CKPT_RANGE_NONE);
    CHECK(back[ICO_CKPT_G_IMAGE].range == 1 && back[ICO_CKPT_G_IMAGE].off == IMG_AT);
}

/* --- the content search ----------------------------------------------------------------------- */

static void test_search(void)
{
    IcoCkptSections s;
    IcoCkptPlace at;
    int passed = -1, rejected = -1, stage = 0;

    s.p[0] = s_gdata;
    s.n[0] = GDATA;
    s.p[1] = s_gbss;
    s.n[1] = GBSS;
    make_memory("pal", 11, 11, 1);
    /* one: the image; the stamp table's European stamp is followed by the
       North American one's text, not a stage */
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_ONE);
    CHECK(at.range == 1 && at.off == IMG_AT && at.size == ICO_CKPT_IMAGE_BYTES);
    CHECK(passed == 1 && rejected == 1, "passed %d rejected %d", passed, rejected);
    CHECK(ico_ckpt_image_valid(s_gbss + IMG_AT, GBSS - IMG_AT, "pal", &stage) && stage == 11);
    /* jun26's stamp is the table's last: followed by zeros (stage 0) */
    CHECK(ico_ckpt_search(&s, "jun26", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    CHECK(passed == 0 && rejected == 1);
    /* none */
    memset(s_gbss + IMG_AT, 0, ICO_CKPT_IMAGE_BYTES);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    CHECK(at.range == ICO_CKPT_RANGE_NONE && passed == 0 && rejected == 1);
    /* two that differ */
    put_image(s_gbss + 0x10, "pal", 11, 0, 1);
    put_image(s_gbss + 0x10 + ICO_CKPT_IMAGE_BYTES + 0x40, "pal", 12, 1, 2);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_SEVERAL);
    CHECK(passed == 2 && at.range == ICO_CKPT_RANGE_NONE);
    /* two identical copies: one image */
    put_image(s_gbss + 0x10 + ICO_CKPT_IMAGE_BYTES + 0x40, "pal", 11, 0, 1);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_ONE);
    CHECK(passed == 2 && at.range == 1 && at.off == 0x10);
    /* corrupt images: each rejected */
    memset(s_gbss, 0, GBSS);
    put_image(s_gbss + 0x10, "pal", 0, 0, 1);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    CHECK(rejected == 2, "rejected %d", rejected);
    put_image(s_gbss + 0x10, "pal", 106, 0, 1);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    put_image(s_gbss + 0x10, "pal", 11, 7, 1);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    memset(s_gbss, 0, GBSS);
    memcpy(s_gbss + GBSS - 100, ico_ckpt_stamp("pal"), ICO_CKPT_STAMP_BYTES);
    wr32(s_gbss + GBSS - 100 + 18, 11);
    CHECK(ico_ckpt_search(&s, "pal", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
    CHECK(rejected == 2);
    /* a 2001 disc: no cleared word after the stage */
    memset(s_gbss, 0, GBSS);
    put_image(s_gbss + 0x10, "ntsc", 40, 0, 3);
    wr32(s_gbss + 0x10 + 22, 7);
    CHECK(ico_ckpt_search(&s, "ntsc", &at, &passed, &rejected) == ICO_CKPT_SEARCH_ONE);
    CHECK(at.off == 0x10 && rejected == 1);
    /* an unknown disc */
    CHECK(ico_ckpt_search(&s, "ps3", &at, &passed, &rejected) == ICO_CKPT_SEARCH_NONE);
}

/* --- the route ------------------------------------------------------------------------------------- */

static void test_route(void)
{
    static const struct {
        IcoStateResult r;
        IcoCkptRoute same, other;
    } t[] = {
        {ICO_STATE_OK, ICO_CKPT_ROUTE_EXACT, ICO_CKPT_ROUTE_EXACT},
        {ICO_STATE_OFF, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_NOT_PAUSED, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_STAGE_SWITCH, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_CARD_BUSY, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_DISC_BUSY, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_MOVIE, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_PHOTO, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_EMPTY, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_BAD_FILE, ICO_CKPT_ROUTE_CHECKPOINT, ICO_CKPT_ROUTE_CHECKPOINT},
        {ICO_STATE_OTHER_BUILD, ICO_CKPT_ROUTE_CHECKPOINT, ICO_CKPT_ROUTE_CHECKPOINT},
        {ICO_STATE_OTHER_DISC, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_OTHER_PLATFORM, ICO_CKPT_ROUTE_CHECKPOINT, ICO_CKPT_ROUTE_CHECKPOINT},
        {ICO_STATE_OTHER_SETTINGS, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_CHECKPOINT},
        {ICO_STATE_SELF_CHECK, ICO_CKPT_ROUTE_CHECKPOINT, ICO_CKPT_ROUTE_CHECKPOINT},
        {ICO_STATE_IO, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_BUSY, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
        {ICO_STATE_PERMADEATH, ICO_CKPT_ROUTE_REFUSED, ICO_CKPT_ROUTE_REFUSED},
    };

    size_t i;

    /* every result once: the enum's last is ICO_STATE_PERMADEATH */
    CHECK(sizeof t / sizeof t[0] == (size_t)ICO_STATE_PERMADEATH + 1);
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        CHECK(ico_ckpt_route(t[i].r, 0) == t[i].same, "result %d, this version: %s", (int)t[i].r,
              ico_ckpt_route_text(ico_ckpt_route(t[i].r, 0)));
        CHECK(ico_ckpt_route(t[i].r, 1) == t[i].other, "result %d, another version: %s",
              (int)t[i].r, ico_ckpt_route_text(ico_ckpt_route(t[i].r, 1)));
    }
    CHECK_STR(ico_ckpt_route_text(ICO_CKPT_ROUTE_CHECKPOINT), "checkpoint");
}

/* the combined verdict: another build's state through the syms check (the
   legacy tables' naming of an earlier Android record included), each reason
   ico_xb_check or state_host.c's xb_probe gives, by the kind it gives it
   under (port/platform/state_xbuild.c, state_host.c): only a translation
   resumes exactly, every refusal continues from the checkpoint */
static void test_xb_route(void)
{
    static const struct {
        int kind;
        const char *reason;
    } t[] = {
        {ICO_XB_OK,
         "everything it points into is unchanged (a legacy table named it, or its own record)"},
        {ICO_XB_DATA, "the game's data layout changed (named first)"},
        {ICO_XB_DATA, "the variable X is missing here / changed size / the start-up data changed"},
        {ICO_XB_DATA, "the text ... is missing here / is in more than one place here"},
        {ICO_XB_DATA, "the page address ... lands into a page here"},
        {ICO_XB_CODE, "the function X changed / is missing here (a page word's producer too)"},
        {ICO_XB_BLOCKS, "the block X changed size / exists only here / is missing here"},
        {ICO_XB_PINNED, "it was made before the pages its threads keep in registers were recorded"},
        {ICO_XB_PINNED, "its build has no legacy table here"},
        {ICO_XB_PINNED,
         "its version's legacy table could not read it (the table is another build's)"},
        {ICO_XB_PINNED, "it holds a program address no symbol names"},
        {ICO_XB_PINNED, "the version that made it had no symbol file / its program lay below 4 GB"},
        {ICO_XB_PINNED, "its code was compared under other rules"},
        {ICO_XB_PINNED, "it points into the program in more places than the record holds"},
        {ICO_XB_BAD, "the symbols record is missing, damaged or could not be read"},
    };

    size_t i;

    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        const IcoStateResult r = ico_ckpt_xb_result(t[i].kind);
        const IcoCkptRoute k = ico_ckpt_route(r, 1);

        CHECK(k == (t[i].kind == ICO_XB_OK ? ICO_CKPT_ROUTE_EXACT : ICO_CKPT_ROUTE_CHECKPOINT),
              "%s: %s", t[i].reason, ico_ckpt_route_text(k));
    }
    CHECK(ico_ckpt_xb_result(ICO_XB_BAD) == ICO_STATE_BAD_FILE);
    CHECK(ico_ckpt_xb_result(ICO_XB_PINNED) == ICO_STATE_OTHER_BUILD);
    CHECK(ico_ckpt_xb_result(99) == ICO_STATE_OTHER_BUILD); /* a later kind: not exact */
}

/* the one box each route leaves (main_host.c states_done through
   ico_ckpt_box), and the notice's words in the eight languages: a load that
   continued from its checkpoint shows the notice alone, as a success, never
   the refusal; the refusal only for a load that did nothing */
static void test_boxes(void)
{
    static const char *const *const tables[] = {ui_strings_en, ui_strings_fr, ui_strings_de,
                                                ui_strings_it, ui_strings_es, ui_strings_ja,
                                                ui_strings_ko, ui_strings_zh};
    static const char *const names[] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"};
    size_t i;

    /* exact: the ordinary words, whatever an earlier refusal left */
    CHECK(ico_ckpt_box(1, ICO_STATE_OK, 0, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_DONE);
    CHECK(ico_ckpt_box(0, ICO_STATE_OK, 0, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_DONE);
    /* the checkpoint: the notice, even with the exact path's blocker still
       recorded (the refusal the route left behind) */
    CHECK(ico_ckpt_box(1, ICO_STATE_OK, 1, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_NOTICE);
    CHECK(ico_ckpt_box(1, ICO_STATE_OK, 1, ICO_STATE_BLOCK_OLD) == ICO_CKPT_BOX_NOTICE);
    CHECK(ico_ckpt_box(1, ICO_STATE_OK, 1, ICO_STATE_BLOCK_PINNED) == ICO_CKPT_BOX_NOTICE);
    CHECK(ico_ckpt_box(1, ICO_STATE_OK, 1, ICO_STATE_BLOCK_DATA) == ICO_CKPT_BOX_NOTICE);
    /* a save never gets the notice */
    CHECK(ico_ckpt_box(0, ICO_STATE_OK, 1, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_DONE);
    /* loads that did nothing: their refusal only */
    CHECK(ico_ckpt_box(1, ICO_STATE_OTHER_BUILD, 0, ICO_STATE_BLOCK_NO_CHECKPOINT) ==
          ICO_CKPT_BOX_VERSION);
    CHECK(ico_ckpt_box(1, ICO_STATE_OTHER_DISC, 0, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_REFUSAL);
    CHECK(ico_ckpt_box(1, ICO_STATE_PERMADEATH, 0, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_REFUSAL);
    CHECK(ico_ckpt_box(1, ICO_STATE_OTHER_SETTINGS, 0, ICO_STATE_BLOCK_NONE) ==
          ICO_CKPT_BOX_SETTINGS);
    CHECK(ico_ckpt_box(1, ICO_STATE_STAGE_SWITCH, 0, ICO_STATE_BLOCK_NONE) == ICO_CKPT_BOX_REFUSAL);
    /* the words: the notice is not the refusal's (their first lines differ:
       the notice opens with what was done, the refusal with what could not
       be), it names the version once, and the page's line has no number */
    for (i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        const char *ok = tables[i][UI_STR_STATES_CKPT_LOADED];
        const char *no = tables[i][UI_STR_STATES_CKPT_NONE];
        const char *row = tables[i][UI_STR_STATES_CKPT_ROW];
        const char *nl = ok != NULL ? strchr(ok, '\n') : NULL;
        const size_t first = nl != NULL ? (size_t)(nl - ok) : (ok != NULL ? strlen(ok) : 0);

        CHECK(ok != NULL && no != NULL && row != NULL, "%s: a checkpoint string missing", names[i]);
        if (ok == NULL || no == NULL || row == NULL) {
            continue;
        }
        CHECK(strncmp(ok, no, first < 12 ? first : 12) != 0,
              "%s: the notice opens as the refusal does: \"%s\"", names[i], ok);
        CHECK(strstr(ok, "%s") != NULL && strstr(strstr(ok, "%s") + 2, "%") == NULL,
              "%s: the notice names the version once", names[i]);
        CHECK(strchr(row, '%') == NULL, "%s: the page's line takes no value", names[i]);
    }
}

/* --- a syms record ------------------------------------------------------------------------------- */

/* state_xbuild.c's layout: the header (64 bytes, 80 in version 3), units 48,
   data 48 (40 in version 1), points 48 (64 in version 3), blocks 40, then
   (version 3) the list, pages and producers, the names, the pool */
static size_t make_syms(unsigned char *out, unsigned ver, uint64_t rec_off, uint64_t rec_size)
{
    const uint32_t nu = 3, nd = 2, np = 4, nb = 3;
    const size_t hb = ver >= 3 ? 80 : 64, pb = ver >= 3 ? 64 : 48;
    const char names[] = "a.first\0difficulty.record\0spu2.state\0";
    const uint32_t name_at[3] = {0, 8, 26};
    const uint64_t offs[3] = {0, rec_off, 0}, sizes[3] = {16, rec_size, 64};
    const uint32_t flags[3] = {0, 1, 2};
    unsigned char *b;
    size_t n;
    uint32_t i;

    n = hb + nu * 48 + nd * 48 + np * pb;
    memset(out, 0x5A, n);
    memset(out, 0, hb);
    wr32(out, 0x534D5953u);
    wr32(out + 4, ver);
    wr32(out + 20, nu);
    wr32(out + 24, nd);
    wr32(out + 28, np);
    wr32(out + 32, nb);
    wr32(out + 36, (uint32_t)sizeof names);
    wr32(out + 40, 0x89ABCDEFu);
    wr32(out + 44, 0x01234567u);
    for (i = 0; i < nb; i++) {
        b = out + n + 40 * i;
        memset(b, 0, 40);
        wr32(b + 8, (uint32_t)sizes[i]);
        wr32(b + 16, (uint32_t)offs[i]);
        wr32(b + 24, flags[i]);
        wr32(b + 28, name_at[i]);
    }
    n += 40 * nb;
    memcpy(out + n, names, sizeof names);
    return n + sizeof names;
}

static void test_syms(void)
{
    static unsigned char syms[4096];
    uint64_t off = 0, size = 0, layout = 0;
    unsigned flags = 0, ver;
    size_t n;

    for (ver = 2; ver <= 3; ver++) {
        n = make_syms(syms, ver, 0x1234, 96);
        CHECK(ico_ckpt_syms_block(syms, n, "difficulty.record", &off, &size, &flags, &layout) == 1,
              "version %u", ver);
        CHECK(off == 0x1234 && size == 96 && flags == 1 && layout == 0x0123456789ABCDEFull);
        CHECK(ico_ckpt_syms_block(syms, n, "spu2.state", &off, &size, &flags, NULL) == 1 &&
              flags == 2 && size == 64);
        CHECK(ico_ckpt_syms_block(syms, n, "no.such", &off, &size, &flags, NULL) == 0);
        CHECK(ico_ckpt_syms_block(syms, n - 1, "difficulty.record", &off, &size, &flags, NULL) ==
              -1);
        syms[0] ^= 1;
        CHECK(ico_ckpt_syms_block(syms, n, "difficulty.record", &off, &size, &flags, NULL) == -1);
        syms[0] ^= 1;
    }
}

/* --- state files ----------------------------------------------------------------------------------- */

static unsigned char s_reg[512];
static unsigned char s_syms[4096];

static int write_state(const char *path, unsigned version, const char *commit, const char *disc,
                       int stage_no, int with_syms, const void *ckpt, size_t ckpt_n)
{
    IcoStateHeader *h = (IcoStateHeader *)calloc(1, sizeof *h);
    IcoStateSection s[6];
    IcoStateWriteResult res;
    int n = 0, rc;

    if (h == NULL) {
        return -1;
    }
    ico_state_header_init(h, commit);
    h->version = version;
    snprintf(h->platform, sizeof h->platform, "test-x64");
    snprintf(h->disc, sizeof h->disc, "%s", disc);
    h->stage_no = stage_no;
    snprintf(h->keys, sizeof h->keys, "gameplay.x=1\n");
    memset(s, 0, sizeof s);
    s[n].name = "arena";
    s[n].data = s_reg;
    s[n++].bytes = 64;
    s[n].name = "gdata";
    s[n].data = s_gdata;
    s[n++].bytes = GDATA;
    s[n].name = "gbss";
    s[n].data = s_gbss;
    s[n++].bytes = GBSS;
    s[n].name = "reg";
    s[n].data = s_reg;
    s[n++].bytes = sizeof s_reg;
    if (with_syms) {
        s[n].name = "syms";
        s[n].data = s_syms;
        s[n++].bytes = make_syms(s_syms, 2, 0x80, 96);
    }
    if (ckpt != NULL) {
        s[n].name = "ckpt";
        s[n].data = ckpt;
        s[n++].bytes = ckpt_n;
    }
    rc = ico_state_file_write(path, h, s, n) == ICO_STATE_OK && ico_state_file_flush(&res) == 1 &&
                 res.result == ICO_STATE_OK
             ? 0
             : -1;
    free(h);
    return rc;
}

static void test_states(const char *dir)
{
    static IcoCkptState st;
    IcoCkptLayout self;
    IcoCkptInput in;
    IcoCkptPlan plan;
    const unsigned char *rec = NULL;
    char path[1024], why[160];
    unsigned char ckpt[ICO_CKPT_RECORD_BYTES];
    IcoCkptPlace g[ICO_CKPT_G_COUNT];
    int i;

    for (i = 0; i < (int)sizeof s_reg; i++) {
        s_reg[i] = (unsigned char)(0x10 + i);
    }
    make_layout(&self, "this", "v9.9.9-0-gself", "test-x64");

    /* format 1 (v0.9.6 - v0.9.7): no syms, no record, a commit no table names */
    make_memory("pal", 14, 12, 1);
    snprintf(path, sizeof path, "%s/ckpt_v1.ics", dir);
    CHECK(write_state(path, 1, "v0.9.6-1-gtest", "pal", 14, 0, NULL, 0) == 0);
    CHECK(ico_ckpt_state_read(path, &st) == 0, "%s", st.why);
    CHECK(st.h.version == 1 && st.p[ICO_CKPT_S_SYMS] == NULL && st.p[ICO_CKPT_S_CKPT] == NULL);
    ico_ckpt_state_input(&st, &self, &in);
    CHECK(ico_ckpt_locate(&in, &plan) == 0, "%s", plan.why);
    CHECK(plan.by == ICO_CKPT_BY_LAYOUT && plan.checkpoint == 1 && plan.image_stage == 12,
          "by %d checkpoint %d stage %d", plan.by, plan.checkpoint, plan.image_stage);
    CHECK(plan.g[ICO_CKPT_G_IMAGE] == st.p[ICO_CKPT_S_GBSS] + IMG_AT);
    CHECK(plan.g[ICO_CKPT_G_PREVIEW] == st.p[ICO_CKPT_S_GBSS] + PREVIEW_AT);
    CHECK(ico_ckpt_state_block(&st, "difficulty.record", 96, &rec, why, sizeof why) == 0);
    /* the same state with no layout to go by: the image by its bytes, the
       rest unknown */
    ico_ckpt_state_input(&st, NULL, &in);
    CHECK(ico_ckpt_locate(&in, &plan) == 0, "%s", plan.why);
    CHECK(plan.by == ICO_CKPT_BY_SEARCH && plan.checkpoint == -1 && plan.image_stage == 12);
    CHECK(plan.g[ICO_CKPT_G_STATUS] == NULL && plan.g[ICO_CKPT_G_PREVIEW] == NULL);
    CHECK(plan.place[ICO_CKPT_G_IMAGE].range == 1 && plan.place[ICO_CKPT_G_IMAGE].off == IMG_AT);
    /* a layout whose stage_no is not the header's is passed over */
    in.stage_no = 15;
    in.self = &self;
    CHECK(ico_ckpt_locate(&in, &plan) == 0 && plan.by == ICO_CKPT_BY_SEARCH);
    ico_ckpt_state_free(&st);

    /* format 2 (v0.10.0 - v0.11.8): the table's entry of its commit, its
       difficulty record by the syms block list; no checkpoint since the
       journey started (the image an older journey's) */
    make_memory("kor", 4, 3, 0);
    make_layout(&s_test_table[0], "v0.11.5", "v0.11.4-211-gtest", "test-x64");
    snprintf(path, sizeof path, "%s/ckpt_v2.ics", dir);
    CHECK(write_state(path, 2, "v0.11.4-211-gtest", "kor", 4, 1, NULL, 0) == 0);
    CHECK(ico_ckpt_state_read(path, &st) == 0, "%s", st.why);
    ico_ckpt_state_input(&st, NULL, &in);
    in.table = s_test_table;
    in.table_n = 1;
    CHECK(ico_ckpt_locate(&in, &plan) == 0, "%s", plan.why);
    CHECK(plan.by == ICO_CKPT_BY_TABLE && plan.checkpoint == 0, "by %d checkpoint %d", plan.by,
          plan.checkpoint);
    CHECK(strstr(plan.source, "v0.11.5") != NULL, "%s", plan.source);
    CHECK(ico_ckpt_state_block(&st, "difficulty.record", 96, &rec, why, sizeof why) == 1 &&
          rec == st.p[ICO_CKPT_S_REG] + 0x80 && rec[0] == 0x90);
    CHECK(ico_ckpt_state_block(&st, "difficulty.record", 100, &rec, why, sizeof why) == -1);
    CHECK(ico_ckpt_state_block(&st, "spu2.state", 64, &rec, why, sizeof why) == -1); /* audio */
    /* word 3 set: a checkpoint of this journey */
    wr32(st.p[ICO_CKPT_S_GDATA] + STATUS_AT + 12, 1);
    CHECK(ico_ckpt_locate(&in, &plan) == 0 && plan.by == ICO_CKPT_BY_TABLE &&
          plan.checkpoint == 1 && plan.image_stage == 3);
    /* the table's entry at other sizes: not its build after all; the search */
    s_test_table[0].gbss_bytes = GBSS + 16;
    CHECK(ico_ckpt_locate(&in, &plan) == 0 && plan.by == ICO_CKPT_BY_SEARCH);
    s_test_table[0].gbss_bytes = GBSS;
    /* another disc's bytes: nothing found */
    in.disc = "pal";
    CHECK(ico_ckpt_locate(&in, &plan) == -1 && plan.by == ICO_CKPT_BY_NONE);
    ico_ckpt_state_free(&st);

    /* a state from v0.11.9 on: its own record; no table, no layout needed */
    make_memory("jpn", 30, 29, 1);
    self_places(g);
    CHECK(ico_ckpt_record_write(g, ckpt, sizeof ckpt) == sizeof ckpt);
    snprintf(path, sizeof path, "%s/ckpt_v3.ics", dir);
    CHECK(write_state(path, 2, "v0.11.9-5-gfuture", "jpn", 30, 1, ckpt, sizeof ckpt) == 0);
    CHECK(ico_ckpt_state_read(path, &st) == 0, "%s", st.why);
    ico_ckpt_state_input(&st, NULL, &in);
    in.table = NULL;
    in.table_n = 0;
    CHECK(ico_ckpt_locate(&in, &plan) == 0, "%s", plan.why);
    CHECK(plan.by == ICO_CKPT_BY_RECORD && plan.checkpoint == 1 && plan.image_stage == 29);
    CHECK(plan.g[ICO_CKPT_G_SCREEN] == st.p[ICO_CKPT_S_GDATA] + SCREEN_AT);
    /* a record that does not fit: the search after it, its reason kept */
    wr32(st.p[ICO_CKPT_S_GDATA] + STAGE_AT, 31);
    CHECK(ico_ckpt_locate(&in, &plan) == 0 && plan.by == ICO_CKPT_BY_SEARCH);
    ico_ckpt_state_free(&st);

    /* a release candidate's Android state (a commit no table names) at
       v0.11.8's sizes: the released layouts of those sizes agree and its
       bytes confirm them */
    {
        const IcoCkptLayout *l = ico_ckpt_table_find(ico_ckpt_table, ico_ckpt_table_count,
                                                     "v0.11.7-68-gaeb00e6100", "android-arm64");
        unsigned char *gd, *gb;

        CHECK(l != NULL);
        if (l != NULL) {
            gd = (unsigned char *)calloc(1, l->gdata_bytes);
            gb = (unsigned char *)calloc(1, l->gbss_bytes);
            CHECK(gd != NULL && gb != NULL);
            if (gd != NULL && gb != NULL) {
                memset(&in, 0, sizeof in);
                in.commit = "v0.11.7-66-gaf8507442c";
                in.platform = "android-arm64";
                in.disc = "pal";
                in.stage_no = 11;
                in.sec.p[0] = gd;
                in.sec.n[0] = l->gdata_bytes;
                in.sec.p[1] = gb;
                in.sec.n[1] = l->gbss_bytes;
                in.table = ico_ckpt_table;
                in.table_n = ico_ckpt_table_count;
                put_image((l->g[ICO_CKPT_G_IMAGE].range ? gb : gd) + l->g[ICO_CKPT_G_IMAGE].off,
                          "pal", 11, 0, 9);
                wr32((l->g[ICO_CKPT_G_STAGE].range ? gb : gd) + l->g[ICO_CKPT_G_STAGE].off, 11);
                wr32((l->g[ICO_CKPT_G_STATUS].range ? gb : gd) + l->g[ICO_CKPT_G_STATUS].off + 12,
                     1);
                CHECK(ico_ckpt_locate(&in, &plan) == 0, "%s", plan.why);
                CHECK(plan.by == ICO_CKPT_BY_LAYOUT && plan.layouts >= 2 && plan.checkpoint == 1,
                      "by %d layouts %d (%s)", plan.by, plan.layouts, plan.source);
            }
            free(gd);
            free(gb);
        }
    }
    /* a damaged file whose game sections hold more than the PS2's main
       memory (each under the bound, the two together over it): refused by
       their sizes before anything is allocated for them */
    {
        const size_t gd_n = 1u << 20, gb_n = (size_t)ICO_CKPT_EE_RAM - gd_n + 16u;
        unsigned char *gb = (unsigned char *)calloc(1, gb_n);
        IcoStateHeader *h = (IcoStateHeader *)calloc(1, sizeof *h);
        IcoStateSection sec[3];
        IcoStateWriteResult res;

        if (gb != NULL && h != NULL) {
            ico_state_header_init(h, "v0.9.6-1-gtest");
            snprintf(h->platform, sizeof h->platform, "test-x64");
            snprintf(h->disc, sizeof h->disc, "pal");
            memset(sec, 0, sizeof sec);
            sec[0].name = "gdata";
            sec[0].data = gb;
            sec[0].bytes = gd_n;
            sec[1].name = "gbss";
            sec[1].data = gb;
            sec[1].bytes = gb_n;
            sec[2].name = "reg";
            sec[2].data = s_reg;
            sec[2].bytes = sizeof s_reg;
            snprintf(path, sizeof path, "%s/ckpt_big.ics", dir);
            CHECK(ico_state_file_write(path, h, sec, 3) == ICO_STATE_OK &&
                  ico_state_file_flush(&res) == 1 && res.result == ICO_STATE_OK);
            CHECK(ico_ckpt_state_read(path, &st) == -1 && strstr(st.why, "gbss") != NULL &&
                      strstr(st.why, "more than a state holds") != NULL,
                  "the game's two sections over 32 MB: %s", st.why);
            ico_ckpt_state_free(&st);
            remove(path);
        }
        free(gb);
        free(h);
    }
    /* no file */
    snprintf(path, sizeof path, "%s/ckpt_none.ics", dir);
    CHECK(ico_ckpt_state_read(path, &st) == -1 && strstr(st.why, "no file") != NULL);
    ico_ckpt_state_free(&st);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    test_globals();
    test_table();
    test_record();
    test_search();
    test_route();
    test_xb_route();
    test_boxes();
    test_syms();
    test_states(dir);
    return ico_check_summary("state_checkpoint");
}
