/*
 * port/game/test/state_host_test.c
 *
 * Save states' decisions (port/game/state_rules.h), without the game, in
 * the folder given (the build folder under ctest; "." without one):
 *
 *   conditions   a sampled state that may save, then every field changed
 *                alone: each refusal reachable (off, not on the pause
 *                menu, a stage switch, the card, the disc, a movie, photo
 *                mode, the credits, the music gallery) and named; the
 *                order when several hold; a load needs no pause; the pause
 *                menu, a port page over it, a game layout, the title
 *   texts        every result has its own log words
 *   slot paths   slot1.ics .. slot4.png; slot 0, slot 5 and a path that
 *                does not fit refused with ""
 *   [dev] keys   "<tick>:<slot>" and "<tick>", and the malformed ones
 *   listing      a folder that does not exist and an empty one: four empty
 *                slots; then states written with each validity field
 *                changed (platform, build, disc, a simulation key) listed
 *                as present and not loadable with the reason, a good one
 *                loadable with its play time, stage, time, commit and
 *                picture, a damaged file present and BAD_FILE
 *   thumbnail    the size for 640 x 448, a narrow picture, a box average
 *                and opaque alpha
 *
 * The full save and load is the headless round trip's (A5,
 * state_roundtrip_headless).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "state_file.h"
#include "state_rules.h"
#include "../../test/ico_check.h"

static char base[512];

/* a sampled state on the pause menu with nothing busy */
static IcoStateConds ok_conds(void)
{
    IcoStateConds c;

    memset(&c, 0, sizeof c);
    c.available = 1;
    c.layout = 57;
    c.layout_pause = 57;
    c.layout_count = 80;
    c.stage = 12;
    c.stage_title = 1;
    c.paused = 1;
    c.disc_idle = 1;
    return c;
}

static void test_conditions(void)
{
    IcoStateConds c = ok_conds();
    const char *what = "x";

    CHECK(ico_state_conditions(&c, 1, &what) == ICO_STATE_OK);
    CHECK_STR(what, "");
    CHECK(ico_state_on_pause_menu(&c));
    /* a port page (Options, Save states) over the pause menu */
    c.layout = 85;
    CHECK(ico_state_on_pause_menu(&c) && ico_state_conditions(&c, 1, NULL) == ICO_STATE_OK);
    /* a game layout other than the pause menu */
    c.layout = 58;
    CHECK(!ico_state_on_pause_menu(&c));
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_NOT_PAUSED);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_OK, "a load needs no pause");
    c = ok_conds();
    c.paused = 0;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_NOT_PAUSED);
    c = ok_conds();
    c.stage = 1; /* the title, with a page open */
    c.layout = 85;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_NOT_PAUSED);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_OK, "a load from the title");

    c = ok_conds();
    c.available = 0;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_OFF);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_OFF);
    c = ok_conds();
    c.stage_switch = 1;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_STAGE_SWITCH);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_STAGE_SWITCH);
    c = ok_conds();
    c.card_busy = 1;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_CARD_BUSY);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_CARD_BUSY);
    c = ok_conds();
    c.disc_idle = 0;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_DISC_BUSY);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_DISC_BUSY);
    c = ok_conds();
    c.movie = 1;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_MOVIE);
    c = ok_conds();
    c.photo = 1;
    CHECK(ico_state_conditions(&c, 1, &what) == ICO_STATE_PHOTO);
    CHECK_STR(what, "photo mode");
    c = ok_conds();
    c.credits = 1;
    CHECK(ico_state_conditions(&c, 0, &what) == ICO_STATE_PHOTO);
    CHECK_STR(what, "the credits");
    c = ok_conds();
    c.gallery = 1;
    CHECK(ico_state_conditions(&c, 1, &what) == ICO_STATE_PHOTO);
    CHECK_STR(what, "the music gallery");

    /* the order: off, pause, stage switch, card, disc, movie, photo */
    c = ok_conds();
    c.paused = 0;
    c.stage_switch = 1;
    c.card_busy = 1;
    c.disc_idle = 0;
    c.movie = 1;
    c.photo = 1;
    CHECK(ico_state_conditions(&c, 1, NULL) == ICO_STATE_NOT_PAUSED);
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_STAGE_SWITCH);
    c.stage_switch = 0;
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_CARD_BUSY);
    c.card_busy = 0;
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_DISC_BUSY);
    c.disc_idle = 1;
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_MOVIE);
    c.movie = 0;
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_PHOTO);
    c.available = 0;
    CHECK(ico_state_conditions(&c, 0, NULL) == ICO_STATE_OFF);
}

static void test_texts(void)
{
    int a, b;

    for (a = ICO_STATE_OK; a <= ICO_STATE_BUSY; a++) {
        const char *t = ico_state_result_text((IcoStateResult)a);

        CHECK(t != NULL && t[0] != '\0' && strcmp(t, "unknown") != 0, "result %d has no text", a);
        for (b = ICO_STATE_OK; b < a; b++) {
            CHECK(strcmp(t, ico_state_result_text((IcoStateResult)b)) != 0,
                  "results %d and %d share \"%s\"", a, b, t);
        }
    }
    CHECK_STR(ico_state_result_text(ICO_STATE_OTHER_SETTINGS), "settings differ");
}

static void test_paths(void)
{
    char p[64];

    CHECK(ico_state_slot_path(p, sizeof p, "d", 1, ".ics") == 0);
    CHECK_STR(p, "d/slot1.ics");
    CHECK(ico_state_slot_path(p, sizeof p, "d", 4, ".png") == 0);
    CHECK_STR(p, "d/slot4.png");
    CHECK(ico_state_slot_path(p, sizeof p, "d", 0, ".ics") == -1 && p[0] == '\0');
    CHECK(ico_state_slot_path(p, sizeof p, "d", 5, ".ics") == -1 && p[0] == '\0');
    CHECK(ico_state_slot_path(p, sizeof p, "", 1, ".ics") == -1 && p[0] == '\0');
    CHECK(ico_state_slot_path(p, 8, "dir", 1, ".ics") == -1 && p[0] == '\0');
}

static void test_dev_keys(void)
{
    unsigned int t;
    int s;

    CHECK(ico_state_parse_at("1200:1", &t, &s) == 0 && t == 1200 && s == 1);
    CHECK(ico_state_parse_at("7:4", &t, &s) == 0 && t == 7 && s == 4);
    CHECK(ico_state_parse_at("1500", &t, NULL) == 0 && t == 1500);
    CHECK(ico_state_parse_at("1500", &t, &s) == -1 && t == 0 && s == 0, "the slot is needed");
    CHECK(ico_state_parse_at("1500:2", &t, NULL) == -1, "no slot for the hash");
    CHECK(ico_state_parse_at("100:5", &t, &s) == -1 && t == 0);
    CHECK(ico_state_parse_at("100:0", &t, &s) == -1);
    CHECK(ico_state_parse_at("0:1", &t, &s) == -1);
    CHECK(ico_state_parse_at("100:", &t, &s) == -1);
    CHECK(ico_state_parse_at("100:1x", &t, &s) == -1);
    CHECK(ico_state_parse_at("-5:1", &t, &s) == -1);
    CHECK(ico_state_parse_at("", &t, &s) == -1);
    CHECK(ico_state_parse_at(NULL, &t, &s) == -1);
    CHECK(ico_state_parse_at("99999999999:1", &t, &s) == -1);
}

/* the headers the listing checks against */
static IcoStateHeader now, h;

static void make_now(void)
{
    ico_state_header_init(&now, "test-commit");
    snprintf(now.disc, sizeof now.disc, "pal");
    snprintf(now.keys, sizeof now.keys, "gameplay.mirror=false\n");
    now.settings_hash = 0x1234;
}

static void write_state(const char *dir, int slot, const IcoStateHeader *hd)
{
    static unsigned char bytes[64];
    char p[640];
    IcoStateSection s;
    IcoStateWriteResult w;

    memset(&s, 0, sizeof s);
    s.name = "arena";
    s.data = bytes;
    s.bytes = sizeof bytes;
    CHECK(ico_state_slot_path(p, sizeof p, dir, slot, ".ics") == 0);
    CHECK(ico_state_file_write(p, hd, &s, 1) == ICO_STATE_OK);
    CHECK(ico_state_file_flush(&w) == 1 && w.result == ICO_STATE_OK, "write %s: %s", p, w.why);
}

static void write_bytes(const char *path, const char *text)
{
    FILE *f = ico_fopen(path, "wb");

    CHECK(f != NULL, "cannot write %s", path);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_listing(void)
{
    IcoStateSlotInfo s[ICO_STATE_SLOTS];
    char d1[600], d2[600], p[640];
    int i;

    make_now();
    /* no folder, then an empty one */
    snprintf(d1, sizeof d1, "%s/state_host_none", base);
    memset(s, 0x5A, sizeof s);
    ico_state_list_dir(d1, &now, s);
    for (i = 0; i < ICO_STATE_SLOTS; i++) {
        CHECK(!s[i].present && !s[i].loadable && s[i].why_not == ICO_STATE_EMPTY &&
                  s[i].png_path[0] == '\0' && s[i].play_frames == 0,
              "slot %d of a missing folder", i + 1);
    }
    snprintf(d1, sizeof d1, "%s/state_host_a", base);
    snprintf(d2, sizeof d2, "%s/state_host_b", base);
    (void)ico_mkdir(d1);
    (void)ico_mkdir(d2);
    for (i = 1; i <= ICO_STATE_SLOTS; i++) {
        CHECK(ico_state_slot_path(p, sizeof p, d1, i, ".ics") == 0);
        (void)ico_remove(p);
        CHECK(ico_state_slot_path(p, sizeof p, d1, i, ".png") == 0);
        (void)ico_remove(p);
        CHECK(ico_state_slot_path(p, sizeof p, d2, i, ".ics") == 0);
        (void)ico_remove(p);
        CHECK(ico_state_slot_path(p, sizeof p, d2, i, ".png") == 0);
        (void)ico_remove(p);
    }
    ico_state_list_dir(d1, &now, s);
    for (i = 0; i < ICO_STATE_SLOTS; i++) {
        CHECK(!s[i].present && s[i].why_not == ICO_STATE_EMPTY, "slot %d of an empty folder",
              i + 1);
    }

    /* folder a: a good state with a picture, another platform, another
       build, another disc */
    h = now;
    h.saved_at = 1761800000;
    h.play_frames = 54321;
    h.stage_no = 23;
    write_state(d1, 1, &h);
    CHECK(ico_state_slot_path(p, sizeof p, d1, 1, ".png") == 0);
    write_bytes(p, "png");
    h = now;
    snprintf(h.platform, sizeof h.platform, "other-x64");
    write_state(d1, 2, &h);
    ico_state_header_init(&h, "another-commit");
    snprintf(h.disc, sizeof h.disc, "pal");
    snprintf(h.keys, sizeof h.keys, "%s", now.keys);
    h.settings_hash = now.settings_hash;
    write_state(d1, 3, &h);
    h = now;
    snprintf(h.disc, sizeof h.disc, "jpn");
    write_state(d1, 4, &h);
    ico_state_list_dir(d1, &now, s);
    CHECK(s[0].present && s[0].loadable && s[0].why_not == ICO_STATE_OK);
    CHECK(s[0].play_frames == 54321 && s[0].stage_no == 23 && s[0].saved_at == 1761800000);
    CHECK_STR(s[0].build_commit, "test-commit");
    CHECK(strstr(s[0].png_path, "slot1.png") != NULL, "the picture: %s", s[0].png_path);
    CHECK(s[1].present && !s[1].loadable && s[1].why_not == ICO_STATE_OTHER_PLATFORM);
    CHECK(s[1].png_path[0] == '\0');
    CHECK(s[2].present && !s[2].loadable && s[2].why_not == ICO_STATE_OTHER_BUILD);
    CHECK_STR(s[2].build_commit, "another-commit");
    CHECK(s[3].present && !s[3].loadable && s[3].why_not == ICO_STATE_OTHER_DISC);

    /* folder b: a simulation key that differs, a damaged file, slot 3
       empty, slot 4 good; then the key changed back */
    h = now;
    snprintf(h.keys, sizeof h.keys, "gameplay.mirror=true\n");
    write_state(d2, 1, &h);
    CHECK(ico_state_slot_path(p, sizeof p, d2, 2, ".ics") == 0);
    write_bytes(p, "not a state at all");
    write_state(d2, 4, &now);
    ico_state_list_dir(d2, &now, s);
    CHECK(s[0].present && !s[0].loadable && s[0].why_not == ICO_STATE_OTHER_SETTINGS);
    CHECK(s[1].present && !s[1].loadable && s[1].why_not == ICO_STATE_BAD_FILE);
    CHECK(!s[2].present && s[2].why_not == ICO_STATE_EMPTY);
    CHECK(s[3].present && s[3].loadable);
    snprintf(now.keys, sizeof now.keys, "gameplay.mirror=true\n");
    ico_state_list_dir(d2, &now, s);
    CHECK(s[0].loadable, "loadable once the key is the same");
    CHECK(!s[3].loadable && s[3].why_not == ICO_STATE_OTHER_SETTINGS);
    /* no header to check against: present, not loadable */
    ico_state_list_dir(d2, NULL, s);
    CHECK(s[0].present && !s[0].loadable);
}

static void test_thumbnail(void)
{
    static uint8_t src[4 * 4 * 4], dst[2 * 2 * 4];
    int w, hh, i;

    ico_state_thumb_size(640, 448, &w, &hh);
    CHECK(w == 320 && hh == 224, "%dx%d", w, hh);
    ico_state_thumb_size(1920, 1080, &w, &hh);
    CHECK(w == 320 && hh == 180, "%dx%d", w, hh);
    ico_state_thumb_size(100, 50, &w, &hh);
    CHECK(w == 100 && hh == 50, "a narrow picture keeps its size: %dx%d", w, hh);
    ico_state_thumb_size(4000, 1, &w, &hh);
    CHECK(w == 320 && hh == 1, "%dx%d", w, hh);
    /* 4x4 -> 2x2: each output the average of its 2x2 block */
    for (i = 0; i < 16; i++) {
        src[i * 4 + 0] = (uint8_t)(i * 10);
        src[i * 4 + 1] = 200;
        src[i * 4 + 2] = (uint8_t)((i & 1) ? 255 : 0);
        src[i * 4 + 3] = 7;
    }
    ico_state_thumb_scale(src, 4, 4, dst, 2, 2);
    /* top left: pixels 0, 1, 4, 5 */
    CHECK(dst[0] == 25 && dst[1] == 200 && dst[2] == 128 && dst[3] == 255, "%d %d %d %d", dst[0],
          dst[1], dst[2], dst[3]);
    /* bottom right: pixels 10, 11, 14, 15 */
    CHECK(dst[12] == 125 && dst[15] == 255, "%d", dst[12]);
}

int main(int argc, char **argv)
{
    snprintf(base, sizeof base, "%s", argc > 1 ? argv[1] : ".");
    test_conditions();
    test_texts();
    test_paths();
    test_dev_keys();
    test_listing();
    test_thumbnail();
    return ico_check_summary("state_host");
}
