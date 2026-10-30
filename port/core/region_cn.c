/*
 * port/core/region_cn.c
 *
 * The Asian retail release, SCPS-55001 (region.h). The Asian build is the
 * Korean program in nearly every function (upstream's docs/TARGETS.md "Asian
 * retail (cn)": 5693 of 5727 paired functions equal the Korean build's, 33
 * of the other 34 the Japanese build's twin) with the Japanese records and
 * data: of the 74 member rows, the 72 members' C (tools/gen_data_c.py
 * --version cn, in the PAL record's form) is identical to the Japanese
 * ELF's, member for member (checked by comparing the written C of all 72),
 * and differs from the Korean ELF's in tex-property (412 rows, Korean 406),
 * texture-layout (the Japanese property ranges and default items) and
 * staffroll_dat (the copyright year 2001). So every table-derived number
 * below is the Japanese disc's (region_jpn.c), each one read again by name
 * on this disc's tables: the stage rows (logo 1, deja 40, 26b4demo2 56,
 * staff1 58, end 61), the texture-layout procs (la_game_pause 54,
 * la_title_continue_or_new 9, la_game_option 55, la_key_config 56,
 * la_vibe_select 6, la_game_loop 51, la_mc_file_select 11, ...), the
 * texProperty rows the glyphs and the title, pause and memory card rows
 * name, stageTable's .bga rows, adpcmFile's streams and the counts of
 * config/data_schema.cn.txt. What the Korean program decides is the code's:
 * the memory card directory BISCPS-55001ico (mcard.c), the boot file
 * SCPS_550.01 (cdvd.c, a DVD, DATA.DF 153600 sectors into the file), and
 * the ids the code names, which are the Japanese ones (layout_action.c:
 * lt_mask_property(n + 105) under the Japanese/Asian arm, 99 only under
 * VERSION_KOR). The disc's menu sheets are lettered in Traditional Chinese
 * (Tex_menu01/option.tm2: the option screen, the memory card screens), and
 * the port's text is Chinese on it (port/ui/strings_zh.c, UI_LANG_ZH); the
 * sheets' look over the port's words (UI_SHEET_ZH) starts from the Japanese
 * sheets' numbers until ctest menu_look_cn measures the disc.
 */
#include "region.h"

const IcoRegionInfo ico_region_info_cn = {
    .slug = "cn",
    .disc_id = "SCPS-55001",   /* SYSTEM.CNF BOOT2 cdrom0:\SCPS_550.01;1 */
    .boot_elf = "SCPS_550.01", /* ico2/fumi/ios/cdvd.c, the Asian disc name */
    .pack_serial = "SCPS-55001",
    .archive_name = "ico-cn.o2r",
    .image_base = "Ico_CN",
    .key_prefix = "cn_",
    .key_suffix = "_cn",

    .mc_save_dir = "BISCPS-55001ico", /* ico2/fumi/ios/mcard.c iOSMcSaveSeg, the CN arm */
    .mc_product_size = 0x1E8,         /* the Japanese McProductFile (typedef.h: JPN, KOR and
                                 CN share it): no cameraMove/palMode, aligned 8 */

    /* IosMcPreviewInfo's words (layout_action.c): stage, cleared, play time
       (la_playtime_count's [2]), sofa (la_save_processing's [3]) */
    .mc_preview_play = 2,
    .mc_preview_sofa = 3,

    .has_50hz_row = 0,      /* NTSC only: no 50 Hz branches (docs/TARGETS.md) */
    .has_game_language = 0, /* kanbanBootMcCheck has no language step; jimaku.c
                               no per-language subtitles */
    /* has_film_effect: the Korean program's la_game_option: screenModeItem_kor */
    .has_film_effect = 1,
    /* hold_type_own: A (the Korean program's: the shared _ACTCommonMailTest
       arm, optionControlType 0 at boot) */
    .hold_type_own = 0,
    .ui_lang_default = 7, /* UI_LANG_ZH: the port's text Traditional Chinese, as the disc's */
    .menu_sheet = 7,      /* UI_SHEET_ZH: Tex_menu01's Traditional Chinese sheets */
    .fixed_60hz = 1,      /* NTSC only: systemStatus starts 0, 60 Hz (a 30 Hz Main tick) */
    .pad_confirm = 0x20,  /* Circle: la_boot_confirm_memory_card under the JPN/KOR/CN arm */
    .pad_back = 0x40,     /* Cross: layout_action.c, as the Japanese and Korean discs */

    .reduction_crops_rows =
        0, /* GsBase.c gsb_Reduction_jpn (JPN||KOR||CN): SCISSOR rows 0 .. H/2-1 */

    /* stageData: rows 0..57 are the same; memoryPAL (58) and memoryPAL576
       (59) are gone, so the rows from staff1 on are 2 lower */
    .stage_count = 104,  /* data_schema.cn.txt stage-all */
    .stage_title = 1,    /* stageData[1] logo, as PAL */
    .stage_credits = 58, /* stageData[58] staff1 STAFF1 (st13b) */
    .stage_end = 61,     /* stageData[61] end; layout_action.c:1115 */
    .stage_veil = 40,    /* stageData[40] deja, as PAL */
    .gallery_first = 1,  /* rows 1..39 as PAL (logo .. beach) */
    .gallery_last = 39,
    .last_shipped_stage = 56, /* stageData[56] 26b4demo2, as PAL */
    /* gate 11, grave 13, windmill 15, symmetry_L 18, taki 22, gondola 25,
       watertower 26, symmetry_R 27, cliff 31, queen 37, beach 39: as PAL */
    .ach_stages = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39},
    .area_name_row_base = 105, /* layout_action.c:1127 lt_mask_property(n + 105) */
    .area_name_end_as = 38,    /* layout_action.c:1115-1116 */
    .start_stage_max = 103,

    /* texLayout: 76 rows; the procs from la_vibe_select on are 3 lower */
    .layout_count = 76,         /* data_schema.cn.txt texture-layout 76 */
    .layout_pause = 54,         /* texLayout[54] la_game_pause */
    .layout_title_continue = 9, /* texLayout[9] la_title_continue_or_new */
    .layout_title_new = 10,     /* texLayout[10] la_title_new_game_only */
    .layout_options = 55,       /* texLayout[55] la_game_option; layout_action.c:3076 */
    .layout_key_config = 56,    /* texLayout[56] la_key_config */
    .layout_vibe = 6,           /* texLayout[6] la_vibe_select */
    .layout_play = 51,          /* texLayout[51] la_game_loop */
    .layout_scene = 52,         /* texLayout[52] la_game_demo */
    .layout_mc_files = 11,      /* texLayout[11] la_mc_file_select; layout_action.c:1026 */
    .layout_load_files = 16,    /* texLayout[16] la_mc_load_file_select, its link 11 */
    .layout_load = 22,          /* texLayout[22] la_load_processing */
    .layout_save_complete = 38, /* texLayout[38] la_save_confirm_complete */

    /* texProperty: 412 rows; the shift is not one number (-29 in the
       memory card screens, -28/-24 after them, -23 from the pause menu on) */
    .property_count = 412,    /* data_schema.cn.txt tex-property 412 */
    .row_pause_options = 271, /* la_game_pause's rows 269..273, its 3rd..5th */
    .row_pause_back = 272,    /* (the layout's default item, texLayout[54]) */
    .row_pause_end = 273,
    .row_mc_picture = 23, /* layout_action.c:1033 i + 23 */
    .row_mc_item = 33,    /* layout_action.c:1032 i + 33 */
    .glyph =
        {
            /* Tex_menu01/option.tm2. The key config's button rows 319..326
           (layout_action.c:2879 i * 8 + 319 + slot: Triangle, Square,
           Circle, Cross, R1, R2, L2, L1 as PAL's 342..349); Cross is the
           save prompts' Back glyph (155, as PAL's Triangle 184 there: the
           Japanese menus cancel with Cross); the arrows are la_game_option's
           value arrows 278/279 (PAL 301/302). disp_h is each row's own
           dispH in the Japanese texProperty (30 for the 32 x 15 buttons,
           40 for the 64 x 20 shoulder labels and the arrows), as PAL's
           column is its rows' */
            [ICO_GLYPH_CROSS] = {155, 224, 240, 32, 15, 30},
            [ICO_GLYPH_CIRCLE] = {321, 192, 240, 32, 15, 30},
            [ICO_GLYPH_SQUARE] = {320, 160, 240, 32, 15, 30},
            [ICO_GLYPH_TRIANGLE] = {319, 128, 240, 32, 15, 30},
            [ICO_GLYPH_L1] = {326, 0, 160, 64, 20, 40},
            [ICO_GLYPH_R1] = {323, 128, 160, 64, 20, 40},
            [ICO_GLYPH_L2] = {325, 64, 160, 64, 20, 40},
            [ICO_GLYPH_R2] = {324, 192, 160, 64, 20, 40},
            [ICO_GLYPH_LEFT] = {278, 198, 20, 20, 20, 40},
            [ICO_GLYPH_RIGHT] = {279, 230, 20, 20, 20, 40},
        },
    /* texLayout[9] {20, 22} and [10] {22, 23}: Continue 20 at y 135, New
       Game 21 and 22 at 165 (PAL 49, 50, 51); the copyright line is two
       halves on this disc, 18 and 19 at y 195 (dispX 64 and 319) */
    .row_title_continue = 20,
    .row_title_new = 21,
    .row_title_new_only = 22,
    .row_title_copyright = 18,
    .row_title_copyright2 = 19,
    .pause_first = 269, /* texLayout[54] {269, 274} */
    .pause_last = 274,

    .anim_none = 949,         /* stage-anim 950 rows, the last */
    .chain_group_st10l = 731, /* stageTable st10l_chain_down.bga: group 731 as PAL
                                      (its row id is 380, PAL 384) */
    .chain_group_st19a = 636, /* st19a_chain.bga: group 636 as PAL (row 140, PAL 144) */
    .veil_model = 1598,       /* objTableScene[175] of deja_lightning.bga's
                                      165..177 (PAL 1614 at the same place;
                                      model-path 1621 rows, PAL 1637) */
    .veil_kind = 35,          /* the same objects' kind */
    .reel_row = 66,           /* stageTable film_noise_l4.bga */
    .veil_anim_row = 75,      /* stageTable deja_lightning.bga */
    .mv_motion_kinds = 1145,  /* motion-def 1148 rows as PAL */
    .mv_orient_rows = 2468,   /* motion-orient 2468 rows as PAL */

    .gflag_new_game = 382,     /* layout_action.c gflagOn(382): no selector at the site */
    .gflag_queen_dead = 338,   /* st25a.c gflagOn(338): no selector at the site */
    .gflag_beach_secret = 354, /* end.c gflagOn(354): no selector at the site */
    .gflag_count = 400,        /* gflag.c has no region block */

    .credits_song = 47, /* adpcmFile[47] event/39_8.int, the table as PAL */
    /* the song's position at the roll at 30 Hz: the Asian end.c (the Japanese chain) runs
       the same scene chain as PAL's (the ids aside), each scene a stage
       animation run to its end, and with systemStatus[0] 0 BgAnimation.c
       plays them at 1.0 a tick, as the European core does in its 60 Hz
       mode (0.828 a tick only at 50 Hz). Measured on the European core at
       60 Hz (issue 101's log, a whole ending: the song opens at Main tick
       119656, two after actEndDemo06's layout 55 at 119654; the roll
       starts at 124183, between layouts 54 and 55 of actStaff1): 4527
       ticks of 2 NTSC fields, 151,051 ms (PAL's 25 Hz run: 3777 ticks,
       151,080 ms). Not run on the Asian disc (a Japanese stand-in: the same
       end.c scene chain, measured on the European core) */
    .credits_roll_ms = 151050,
    .credits_entrance_exit = 216, /* exitData[216] leads to 58 (STAFF1); PAL's to 60 */
    .credits_boy_label = 54,      /* act-game.c ACTGame_SetActors_Debug, unchanged */
    /* DATA.DF's directory (182 entries) holds STGSTAFF1.DF, STGSTAFF2.DF and STGSTAFF3.DF */
    .credits_staff_packs = 1,

    .n_adpcm = 105, /* data_schema.cn.txt: these five as PAL */
    .n_sefile = 104,
    .n_sedef = 1426,
    .n_selist = 3837,
    .n_seenv = 425,
    .stream_music_last = 100,  /* adpcmFile names as PAL's, row for row */
    .stream_voice_first = 101, /* event2/hint1_1.int .. hint2_2.int */
    .stream_voice_last = 104,
    .gallery_title_theme = 56, /* adpcmFile[56] event2/50.int as PAL */
};
