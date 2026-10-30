/*
 * port/core/region_pal.c
 *
 * The European release, SCES-50760 (region.h). Each value is the one the
 * port used before the table existed, at the file and line given (the
 * tree at the table's introduction, the first, two-core attempt: a line
 * may have moved since, the names have not), and the shared ico2/ tree's
 * sources and the PAL boot ELF's tables behind it.
 */
#include "region.h"

const IcoRegionInfo ico_region_info_pal = {
    .slug = "pal",
    .disc_id = "SCES-50760",
    .boot_elf = "SCES_507.60",
    .pack_serial = "SCES-50760", /* texpack.c:675, modelpack.c:756 */
    .archive_name = "ico.o2r",
    .image_base = "Ico_PAL",
    .key_prefix = "", /* options.c:133 mirror.slot_N */
    .key_suffix = "", /* achievements.c:462 stats.stages */

    .mc_save_dir = "BESCES-50760ico", /* ico2/fumi/ios/mcard.c:36 */
    .mc_product_size = 0x1F0,         /* typedef.h:1441, layout_asserts.c:1039 */

    /* IosMcPreviewInfo's words (layout_action.c): stage, cleared, play time
       (la_playtime_count's [2]), sofa (la_save_processing's [3]) */
    .mc_preview_play = 2,
    .mc_preview_sofa = 3,

    .has_50hz_row = 1,
    .has_game_language = 1,
    /* has_film_effect: la_game_option: screenModeItem_pal, screenModeAnim_pal
       (film_noise_l1..l4) */
    .has_film_effect = 1,
    /* hold_type_own: A, the game's own (_ACTCommonMailTest: optionControlType
       0 lets go when R1 is up; main.c optionControlType 0 at boot) */
    .hold_type_own = 0,
    .ui_lang_default = -1, /* Auto: the game's language (NonLinearCameraMove) */
    .menu_sheet = -1,      /* the game's language's sheets, one of the five */
    .fixed_60hz = 0,       /* systemStatus[0] from the boot's video mode (gamestate.c:359-367) */
    .pad_confirm = 0x40,   /* Cross: layout_action.c:148 */
    .pad_back = 0x10,      /* Triangle: layout_action.c:150 */

    .reduction_crops_rows = 1, /* GsBase.c gsb_Reduction: SCISSOR rows crop .. H/2-1-crop */

    .stage_count = 106,       /* data_schema.pal.txt stage-all */
    .stage_title = 1,         /* gamestate.c:220, title_logo.c:15 */
    .stage_credits = 60,      /* ico_credits.h:31 STAFF1 */
    .stage_end = 63,          /* layout_action.c:1113, settings.c:2513 */
    .stage_veil = 40,         /* photo_view.c:180 deja */
    .gallery_first = 1,       /* gallery.c:22 */
    .gallery_last = 39,       /* gallery.c:23 */
    .last_shipped_stage = 56, /* gallery.c:24 */
    .ach_stages = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39}, /* achievements.c:28-38 */
    .area_name_row_base = 134, /* layout_action.c:1125, settings.c:2505 */
    .area_name_end_as = 38,    /* layout_action.c:1114 */
    .start_stage_max = 105,    /* gamestate.c:516 */

    .layout_count = 80,          /* layout_ext.h:41 */
    .layout_pause = 57,          /* settings.c:91 */
    .layout_title_continue = 12, /* settings.c:92, title_logo.c:17 */
    .layout_title_new = 13,      /* settings.c:93, title_logo.c:18 */
    .layout_options = 58,        /* settings.c:96, layout_action.c:3076 */
    .layout_key_config = 59,     /* settings.c:97, mouse_look.h:42 */
    .layout_vibe = 9,            /* texLayout[9] la_vibe_select */
    .layout_play = 54,           /* ui_mouse.c:31, mouse_look.h:37 */
    .layout_scene = 55,          /* ui_mouse.c:32, mouse_look.h:38, credits.c:66 */
    .layout_mc_files = 14,       /* ui_mouse.c:35, layout_action.c:1024 */
    .layout_load_files = 19,     /* texLayout[19] la_mc_load_file_select, its link 14 */
    .layout_load = 25,           /* gamestate.c:217 */
    .layout_save_complete = 41,  /* gamestate.c:218 */

    .property_count = 436,    /* layout_ext.h:42 */
    .row_pause_options = 294, /* settings.c:99 */
    .row_pause_back = 295,    /* settings.c:100 */
    .row_pause_end = 296,     /* settings.c:101 */
    .row_mc_picture = 52,     /* ui_mouse.c:34, layout_action.c:1031 */
    .row_mc_item = 62,        /* ui_mouse.c:34, layout_action.c:1030 */
    .glyph =
        {
            /* layout_ext.c:230-240: text/buttons.tm2, menu_PAL_02, menu_PAL_01 */
            [ICO_GLYPH_CROSS] = {182, 32, 30, 32, 30, 30},
            [ICO_GLYPH_CIRCLE] = {344, 0, 30, 32, 30, 30},
            [ICO_GLYPH_SQUARE] = {343, 32, 0, 32, 30, 30},
            [ICO_GLYPH_TRIANGLE] = {184, 0, 0, 32, 30, 30},
            [ICO_GLYPH_L1] = {349, 420, 240, 40, 15, 30},
            [ICO_GLYPH_R1] = {346, 340, 240, 40, 15, 30},
            [ICO_GLYPH_L2] = {348, 460, 240, 40, 15, 30},
            [ICO_GLYPH_R2] = {347, 380, 240, 40, 15, 30},
            [ICO_GLYPH_LEFT] = {301, 490, 130, 20, 20, 40},
            [ICO_GLYPH_RIGHT] = {302, 490, 150, 20, 20, 40},
        },
    .row_title_continue = 49,  /* settings.c:3201, texLayout[12].first */
    .row_title_new = 50,       /* settings.c:3201, row 49's downItem */
    .row_title_new_only = 51,  /* settings.c:3202, texLayout[13].first */
    .row_title_copyright = 48, /* settings.c:3202, at y 195 */
    .row_title_copyright2 = -1,
    .pause_first = 292, /* settings.c:3261, texLayout[57] */
    .pause_last = 297,

    .anim_none = 972,         /* stage-anim 973 rows, the last */
    .chain_group_st10l = 731, /* rd_content_chain.c:29 */
    .chain_group_st19a = 636, /* rd_content_chain.c:36 */
    .veil_model = 1614,       /* photo_view.c:181 */
    .veil_kind = 35,          /* photo_view.c:182 */
    .reel_row = 70,           /* photo_view.c:250 film_noise_l4.bga */
    .veil_anim_row = 79,      /* photo_view.c:251 deja_lightning.bga */
    .mv_motion_kinds = 1145,  /* model_viewer.h:65 */
    .mv_orient_rows = 2468,   /* model_viewer.h:66 */

    .gflag_new_game = 382,     /* gamestate.c:219, layout_action.c:747 */
    .gflag_queen_dead = 338,   /* achievements.c:42, st25a.c:565 */
    .gflag_beach_secret = 354, /* achievements.c:43, end.c:1063 */
    .gflag_count = 400,        /* credits_live.c:85 */

    .credits_song = 47,           /* credits_live.c:71 event/39_8.int */
    .credits_roll_ms = 151080,    /* credits_live.c:72, 3777 ticks at 25 Hz */
    .credits_entrance_exit = 216, /* credits_live.c:80 */
    .credits_boy_label = 54,      /* credits_live.c:81, act-game.c SetActors_Debug */
    /* DATA.DF's directory (192 entries) holds STGSTAFF1.DF, STGSTAFF2.DF and STGSTAFF3.DF */
    .credits_staff_packs = 1,

    .n_adpcm = 105,            /* gallery_play.c:97 */
    .n_sefile = 104,           /* gallery_play.c:98 */
    .n_sedef = 1426,           /* gallery_play.c:99 */
    .n_selist = 3837,          /* gallery_play.c:100 */
    .n_seenv = 425,            /* gallery_play.c:101 */
    .stream_music_last = 100,  /* gallery.c:16 */
    .stream_voice_first = 101, /* gallery.c:17 */
    .stream_voice_last = 104,  /* gallery.c:18 */
    .gallery_title_theme = 56, /* gallery.h:166 */
};
