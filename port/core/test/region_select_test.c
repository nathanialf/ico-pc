/*
 * port/core/test/region_select_test.c
 *
 * The region selection and the region table (port/core/region.h): both
 * slugs select their row and the game's index ico_region follows, nothing
 * else selects; the disc ids map to their rows; the per-disc config keys;
 * the stage rows on each disc (ico_region_stage); ico_region_info[] in
 * config/regions.txt's order (the file, the program's argument, read at
 * test time); and every Japanese value the table was resolved
 * to (region_jpn.c), so a change to one is a change made on purpose.
 * CPU only.
 */
#include <stdio.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "region.h"

static void test_select(void)
{
    /* the European region before any selection */
    CHECK(ico_region == ICO_REGION_PAL && ico_region_cur() == &ico_region_info_pal);
    CHECK(ico_region_select("jpn") == 0);
    CHECK(ico_region == ICO_REGION_JPN && ico_region_cur() == &ico_region_info_jpn);
    CHECK(ICO_JPN && !ICO_PAL && PAL_JPN(1, 2) == 2 && LN(10, 20) == 20);
    /* an unknown slug, a disc id and NULL keep the selection */
    CHECK(ico_region_select("usa") == -1 && ico_region == ICO_REGION_JPN);
    CHECK(ico_region_select("SCES-50760") == -1 && ico_region == ICO_REGION_JPN);
    CHECK(ico_region_select(NULL) == -1 && ico_region == ICO_REGION_JPN);
    CHECK(ico_region_select("pal") == 0);
    CHECK(ico_region == ICO_REGION_PAL && ico_region_cur() == &ico_region_info_pal);
    CHECK(ICO_PAL && !ICO_JPN && PAL_JPN(1, 2) == 1 && LN(10, 20) == 10);

    CHECK(ico_region_count() == ICO_REGION_COUNT && ICO_REGION_COUNT == 2);
    CHECK(ico_region_at(0) == &ico_region_info_pal && ico_region_at(1) == &ico_region_info_jpn &&
          ico_region_at(2) == NULL && ico_region_at(-1) == NULL);
    CHECK(ico_region_info[ICO_REGION_PAL] == &ico_region_info_pal &&
          ico_region_info[ICO_REGION_JPN] == &ico_region_info_jpn);
    CHECK(ico_region_by_slug("pal") == &ico_region_info_pal &&
          ico_region_by_slug("jpn") == &ico_region_info_jpn && ico_region_by_slug("usa") == NULL &&
          ico_region_by_slug(NULL) == NULL);
    CHECK(ico_region_by_disc_id("SCES-50760") == &ico_region_info_pal &&
          ico_region_by_disc_id("SCPS-11003") == &ico_region_info_jpn &&
          ico_region_by_disc_id("SCUS-97113") == NULL && ico_region_by_disc_id("jpn") == NULL &&
          ico_region_by_disc_id(NULL) == NULL);
}

/* ico_region_info[] against config/regions.txt, row for row: slug, disc id
   and boot ELF (the upstream macro and the product name have no column in
   the table) */
static void test_regions_txt(const char *path)
{
    FILE *f = path != NULL ? fopen(path, "r") : NULL;
    char line[256];
    int row = 0;

    CHECK(f != NULL, "cannot read the table %s", path != NULL ? path : "(no argument)");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char slug[32], macro[64], disc[32], boot[32];

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        CHECK(sscanf(line, "%31s %63s %31s %31s", slug, macro, disc, boot) == 4);
        CHECK(row < ICO_REGION_COUNT);
        if (row < ICO_REGION_COUNT) {
            const IcoRegionInfo *r = ico_region_info[row];

            CHECK(strcmp(r->slug, slug) == 0 && strcmp(r->disc_id, disc) == 0 &&
                  strcmp(r->boot_elf, boot) == 0);
            /* row 0 is the base: no upstream macro */
            CHECK((row == 0) == (strcmp(macro, "-") == 0));
        }
        row++;
    }
    fclose(f);
    CHECK(row == ICO_REGION_COUNT);
}

/* every row: the same shapes, every id inside its table */
static void test_consistency(void)
{
    int i;

    for (i = 0; i < ico_region_count(); i++) {
        const IcoRegionInfo *r = ico_region_at(i);
        int g;

        CHECK(strcmp(r->pack_serial, r->disc_id) == 0);
        /* the save directory: B + E (Europe) or I (Japan) + the serial + "ico" */
        CHECK(strlen(r->mc_save_dir) == 15 && strncmp(r->mc_save_dir + 2, r->disc_id, 10) == 0 &&
              strcmp(r->mc_save_dir + 12, "ico") == 0);
        CHECK(r->stage_credits < r->stage_count && r->stage_end < r->stage_count &&
              r->start_stage_max == r->stage_count - 1 && r->last_shipped_stage < r->stage_count);
        CHECK(r->layout_pause < r->layout_count && r->layout_key_config < r->layout_count &&
              r->layout_options < r->layout_count && r->layout_save_complete < r->layout_count);
        CHECK(r->row_pause_end < r->property_count && r->row_mc_item + 9 < r->property_count);
        for (g = 0; g < ICO_GLYPH_COUNT; g++) {
            CHECK(r->glyph[g].row > 0 && r->glyph[g].row < r->property_count && r->glyph[g].w > 0 &&
                  r->glyph[g].h > 0);
        }
        CHECK(r->pad_confirm != r->pad_back);
        CHECK(r->fixed_60hz == !r->has_50hz_row);
        CHECK(r->n_adpcm > r->stream_voice_last && r->gallery_title_theme <= r->stream_music_last);
    }
    CHECK(strcmp(ico_region_info_pal.key_prefix, "") == 0 &&
          strcmp(ico_region_info_pal.key_suffix, "") == 0);
    CHECK(strcmp(ico_region_info_jpn.key_prefix, ico_region_info_pal.key_prefix) != 0 &&
          strcmp(ico_region_info_jpn.key_suffix, ico_region_info_pal.key_suffix) != 0);
    CHECK(ico_region_info_pal.mc_product_size == 0x1F0);
}

/* the Japanese values, as region_jpn.c resolved them by name: every field */
static void test_jpn_values(void)
{
    const IcoRegionInfo *j = &ico_region_info_jpn;
    static const int ach[] = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39};
    int i;

#define STR(f, v) CHECK_STR(j->f, v)
#define NUM(f, v) CHECK(j->f == (v))
#define GLYPH(g, r, u_, v_, w_, h_, d_)                                                            \
    CHECK(j->glyph[g].row == (r) && j->glyph[g].u == (u_) && j->glyph[g].v == (v_) &&              \
          j->glyph[g].w == (w_) && j->glyph[g].h == (h_) && j->glyph[g].disp_h == (d_))
    STR(slug, "jpn");
    STR(disc_id, "SCPS-11003");
    STR(boot_elf, "SCPS_110.03");
    STR(pack_serial, "SCPS-11003");
    STR(archive_name, "ico-jpn.o2r");
    STR(image_base, "Ico_JPN");
    STR(key_prefix, "jpn_");
    STR(key_suffix, "_jpn");
    STR(mc_save_dir, "BISCPS-11003ico");
    NUM(mc_product_size, 0x1E8);
    NUM(has_50hz_row, 0);
    NUM(has_game_language, 0);
    NUM(fixed_60hz, 1);
    NUM(pad_confirm, 0x20);
    NUM(pad_back, 0x40);
    NUM(stage_count, 104);
    NUM(stage_title, 1);
    NUM(stage_credits, 58);
    NUM(stage_end, 61);
    NUM(stage_veil, 40);
    NUM(gallery_first, 1);
    NUM(gallery_last, 39);
    NUM(last_shipped_stage, 56);
    NUM(area_name_row_base, 105);
    NUM(area_name_end_as, 38);
    NUM(start_stage_max, 103);
    NUM(layout_count, 76);
    NUM(layout_pause, 54);
    NUM(layout_title_continue, 9);
    NUM(layout_title_new, 10);
    NUM(layout_options, 55);
    NUM(layout_key_config, 56);
    NUM(layout_vibe, 6);
    NUM(layout_play, 51);
    NUM(layout_scene, 52);
    NUM(layout_mc_files, 11);
    NUM(layout_load, 22);
    NUM(layout_save_complete, 38);
    NUM(property_count, 412);
    NUM(row_pause_options, 271);
    NUM(row_pause_back, 272);
    NUM(row_pause_end, 273);
    NUM(row_mc_picture, 23);
    NUM(row_mc_item, 33);
    NUM(row_title_continue, 20);
    NUM(row_title_new, 21);
    NUM(row_title_new_only, 22);
    NUM(row_title_copyright, 18);
    NUM(row_title_copyright2, 19);
    NUM(pause_first, 269);
    NUM(pause_last, 274);
    NUM(anim_none, 949);
    NUM(chain_group_st10l, 731);
    NUM(chain_group_st19a, 636);
    NUM(veil_model, 1598);
    NUM(veil_kind, 35);
    NUM(reel_row, 66);
    NUM(veil_anim_row, 75);
    NUM(mv_motion_kinds, 1145);
    NUM(mv_orient_rows, 2468);
    NUM(gflag_new_game, 382);
    NUM(gflag_queen_dead, 338);
    NUM(gflag_beach_secret, 354);
    NUM(gflag_count, 400);
    NUM(credits_song, 47);
    NUM(credits_roll_ms, 151050);
    NUM(credits_entrance_exit, 216);
    NUM(credits_boy_label, 54);
    NUM(n_adpcm, 105);
    NUM(n_sefile, 104);
    NUM(n_sedef, 1426);
    NUM(n_selist, 3837);
    NUM(n_seenv, 425);
    NUM(stream_music_last, 100);
    NUM(stream_voice_first, 101);
    NUM(stream_voice_last, 104);
    NUM(gallery_title_theme, 56);
    GLYPH(ICO_GLYPH_CROSS, 155, 224, 240, 32, 15, 30);
    GLYPH(ICO_GLYPH_CIRCLE, 321, 192, 240, 32, 15, 30);
    GLYPH(ICO_GLYPH_SQUARE, 320, 160, 240, 32, 15, 30);
    GLYPH(ICO_GLYPH_TRIANGLE, 319, 128, 240, 32, 15, 30);
    GLYPH(ICO_GLYPH_L1, 326, 0, 160, 64, 20, 40);
    GLYPH(ICO_GLYPH_R1, 323, 128, 160, 64, 20, 40);
    GLYPH(ICO_GLYPH_L2, 325, 64, 160, 64, 20, 40);
    GLYPH(ICO_GLYPH_R2, 324, 192, 160, 64, 20, 40);
    GLYPH(ICO_GLYPH_LEFT, 278, 198, 20, 20, 20, 40);
    GLYPH(ICO_GLYPH_RIGHT, 279, 230, 20, 20, 20, 40);
#undef STR
#undef NUM
#undef GLYPH
    CHECK(sizeof(ach) == sizeof(j->ach_stages));
    for (i = 0; i < ICO_REGION_ACH_STAGES; i++) {
        CHECK(j->ach_stages[i] == ach[i]);
    }
}

static void test_keys(void)
{
    char k[64];

    CHECK(ico_region_select("pal") == 0);
    CHECK(ico_region_key_prefixed("mirror", "slot_3", k, sizeof(k)) == 0 &&
          strcmp(k, "mirror.slot_3") == 0);
    CHECK(ico_region_key_prefixed(NULL, "slot_3", k, sizeof(k)) == 0 && strcmp(k, "slot_3") == 0);
    CHECK(ico_region_key_suffixed("stats.stages", k, sizeof(k)) == 0 &&
          strcmp(k, "stats.stages") == 0);
    CHECK(ico_region_select("jpn") == 0);
    CHECK(ico_region_key_prefixed("mirror", "slot_3", k, sizeof(k)) == 0 &&
          strcmp(k, "mirror.jpn_slot_3") == 0);
    CHECK(ico_region_key_suffixed("stats.stages", k, sizeof(k)) == 0 &&
          strcmp(k, "stats.stages_jpn") == 0);
    CHECK(ico_region_key_prefixed("run", "slot_0", k, 8) == -1);
    CHECK(ico_region_key_suffixed("stats.stages", k, 12) == -1);
    CHECK(ico_region_select("pal") == 0);
}

static void test_stage(void)
{
    CHECK(ico_region_select("pal") == 0);
    CHECK(ico_region_stage(0) == 0 && ico_region_stage(57) == 57 && ico_region_stage(58) == 58 &&
          ico_region_stage(60) == 60 && ico_region_stage(105) == 105);
    CHECK(ico_region_select("jpn") == 0);
    CHECK(ico_region_stage(0) == 0 && ico_region_stage(57) == 57 && ico_region_stage(58) == 56 &&
          ico_region_stage(105) == 103);
    /* the European rows the table names land on the Japanese ones */
    CHECK(ico_region_stage(ico_region_info_pal.stage_credits) == ico_region_info_jpn.stage_credits);
    CHECK(ico_region_stage(ico_region_info_pal.stage_end) == ico_region_info_jpn.stage_end);
    CHECK(ico_region_stage(ico_region_info_pal.start_stage_max) ==
          ico_region_info_jpn.start_stage_max);
    CHECK(ico_region_stage(ico_region_info_pal.stage_veil) == ico_region_info_jpn.stage_veil);
    CHECK(ico_region_select("pal") == 0);
}

int main(int argc, char **argv)
{
    test_select();
    test_regions_txt(argc > 1 ? argv[1] : NULL);
    test_consistency();
    test_jpn_values();
    test_keys();
    test_stage();
    return ico_check_summary("region_select_test");
}
