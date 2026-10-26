/*
 * port/core/region_kor.c
 *
 * The Korean release, SCPS-56001 (region.h). The Korean build is the
 * Japanese program with its text textures in Korean (upstream's
 * docs/TARGETS.md "Korean retail": shared VERSION_JPN || VERSION_KOR arms,
 * the Japanese records, ids, NTSC timing and memory map); its one
 * renumbering is the tex-property table (406 rows, JPN 412: property ids
 * 3-6 lower than JPN's, 6 lower from JPN row 13 on). Each id below was
 * resolved by name on the Korean boot ELF's tables, read with
 * tools/gen_data_c.py --version kor (the C of each member, in the PAL
 * record's form) beside the Japanese ELF's (--version jpn): a layout by its
 * proc, a menu row by its layout and its place in it (the same texture
 * rectangle and screen place as the Japanese row), a stage by its stageData
 * name, an animation by its .bga, a stream by its file. Of the 75 member
 * rows only tex-property, texture-layout (its property ranges and default
 * items) and staffroll_dat's .rodata (the copyright year 2002) differ from
 * the Japanese ELF's (config/data_members.kor.txt), so every stage,
 * animation, model, sound and stream id is the Japanese one; a sample of
 * each was read again by name (the comments say which). Game-code line
 * numbers are the merged tree's (upstream c2f4ed40e converted).
 */
#include "region.h"

const IcoRegionInfo ico_region_info_kor = {
    .slug = "kor",
    .disc_id = "SCPS-56001",   /* SYSTEM.CNF BOOT2 cdrom0:\SCPS_560.01;1 */
    .boot_elf = "SCPS_560.01", /* ico2/fumi/ios/cdvd.c's Korean disc name */
    .pack_serial = "SCPS-56001",
    .archive_name = "ico-kor.o2r",
    .image_base = "Ico_KOR",
    .key_prefix = "kor_",
    .key_suffix = "_kor",

    .mc_save_dir = "BISCPS-56001ico", /* ico2/fumi/ios/mcard.c iOSMcSaveSeg_kor */
    .mc_product_size = 0x1E8,         /* the Japanese McProductFile (TARGETS.md):
                                 no cameraMove/palMode, aligned 8 */

    .has_50hz_row = 0,      /* NTSC only: no 50 Hz branches (docs/TARGETS.md) */
    .has_game_language = 0, /* kanbanBootMcCheck has no language step; jimaku.c
                               no per-language subtitles */
    /* has_film_players: la_game_option: screenModeItem_kor, choiceItem_kor + 2,
       screenModeAnim_jpn (film_noise) */
    .has_film_players = 1,
    /* hold_type_own: A (the shared _ACTCommonMailTest arm: optionControlType
       0 lets go when R1 is up; main.c optionControlType 0 at boot) */
    .hold_type_own = 0,
    .ui_lang_default = 6, /* UI_LANG_KO: the port's text Korean, as the disc's */
    .menu_sheet = 6,      /* UI_SHEET_KO: Tex_menu01's Korean sheets */
    .fixed_60hz = 1,      /* NTSC only: systemStatus starts 0, 60 Hz (a 30 Hz Main tick) */
    .pad_confirm = 0x20,  /* Circle: la_boot_confirm_memory_card under JPN||KOR */
    .pad_back = 0x40,     /* Cross: layout_action.c:150,2494,3066 */

    .reduction_crops_rows = 0, /* GsBase.c gsb_Reduction_jpn (JPN||KOR): SCISSOR rows 0 .. H/2-1 */

    /* stageData: the Japanese table, row for row (the kor and jpn C of
       stage-all are equal); read by name on the Korean ELF: 1 logo, 40 deja,
       56 26b4demo2, 58 staff1 "STAFF1 (st13b)", 61 end "END", and the
       achievement stages below. As the Japanese table, memoryPAL (58) and
       memoryPAL576 (59) are gone, so the rows from staff1 on are PAL's - 2 */
    .stage_count = 104,  /* data_schema.kor.txt stage-all */
    .stage_title = 1,    /* stageData[1] logo, as PAL */
    .stage_credits = 58, /* stageData[58] staff1 STAFF1 (st13b) */
    .stage_end = 61,     /* stageData[61] end; layout_action.c:1117 PAL_JPNKOR(63, 61) */
    .stage_veil = 40,    /* stageData[40] deja, as PAL */
    .gallery_first = 1,  /* rows 1..39 as PAL (logo .. beach) */
    .gallery_last = 39,
    .last_shipped_stage = 56, /* stageData[56] 26b4demo2, as PAL */
    /* gate 11, grave 13, windmill 15, symmetry_L 18, taki 22, gondola 25,
       watertower 26, symmetry_R 27, cliff 31, queen 37, beach 39: as PAL */
    .ach_stages = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39},
    .area_name_row_base = 99, /* layout_action.c:1129 lt_mask_property(n + PAL_JPN_KOR(134,
                                 105, 99)): la_mc_preview_info's row 99 (JPN 105) */
    .area_name_end_as = 38,   /* layout_action.c:1117-1118 */
    .start_stage_max = 103,

    /* texLayout: 76 rows, every row's proc the Japanese row's (the kor and
       jpn texture-layout C name the same proc at every index; only the
       property ranges and default items move); as the Japanese table, the
       procs from la_vibe_select on are PAL's - 3 */
    .layout_count = 76,         /* data_schema.kor.txt texture-layout 76 */
    .layout_pause = 54,         /* texLayout[54] la_game_pause */
    .layout_title_continue = 9, /* texLayout[9] la_title_continue_or_new */
    .layout_title_new = 10,     /* texLayout[10] la_title_new_game_only */
    .layout_options = 55,       /* texLayout[55] la_game_option; layout_action.c:3076 */
    .layout_key_config = 56,    /* texLayout[56] la_key_config */
    .layout_vibe = 6,           /* texLayout[6] la_vibe_select */
    .layout_play = 51,          /* texLayout[51] la_game_loop */
    .layout_scene = 52,         /* texLayout[52] la_game_demo */
    .layout_mc_files = 11,      /* texLayout[11] la_mc_file_select; layout_action.c:1026 */
    .layout_load = 22,          /* texLayout[22] la_load_processing */
    .layout_save_complete = 38, /* texLayout[38] la_save_confirm_complete */

    /* texProperty: 406 rows (JPN 412). The Korean la_boot_no_memory_card
       and la_boot_no_free_area hold 2 rows each where the Japanese hold 5
       (texLayout[1] {1, 3}, [2] {3, 5}; JPN {1, 6}, {6, 11}), so every row
       from the Japanese 11 on is 6 lower: each layout's range, matched row
       for row with the Japanese one, has the same texture rectangle and
       screen place in every row the fields below name (the rows that differ,
       la_mc_preview_info's digits at texV 185 for 180, and some option and
       adjust-screen labels, are Korean lettering widths none of them use).
       The game code's PAL_JPN_KOR ids say the same (layout_action.c) */
    .property_count = 406,    /* data_schema.kor.txt tex-property 406 */
    .row_pause_options = 265, /* la_game_pause's rows 263..267, its 3rd..5th */
    .row_pause_back = 266,    /* (the layout's default item, texLayout[54]) */
    .row_pause_end = 267,
    .row_mc_picture = 17, /* layout_action.c:1033 i + PAL_JPN_KOR(52, 23, 17) */
    .row_mc_item = 27,    /* layout_action.c:1032 i + PAL_JPN_KOR(62, 33, 27) */
    .glyph =
        {
            /* Tex_menu01/option.tm2. The key config's button rows 313..320
           (layout_action.c:2885 i * 8 + PAL_JPN_KOR(342, 319, 313) + slot:
           Triangle, Square, Circle, Cross, R1, R2, L2, L1, the Japanese
           319..326); Cross is the save prompts' Back glyph (149, JPN 155:
           la_mc_load_current_slot_select's row, the Korean menus cancel with
           Cross as the Japanese do); the arrows are la_game_option's value
           arrows 272/273 (JPN 278/279). Each row's rectangle and dispH are
           the Japanese row's (30 for the 32 x 15 buttons, 40 for the 64 x 20
           shoulder labels and the arrows) */
            [ICO_GLYPH_CROSS] = {149, 224, 240, 32, 15, 30},
            [ICO_GLYPH_CIRCLE] = {315, 192, 240, 32, 15, 30},
            [ICO_GLYPH_SQUARE] = {314, 160, 240, 32, 15, 30},
            [ICO_GLYPH_TRIANGLE] = {313, 128, 240, 32, 15, 30},
            [ICO_GLYPH_L1] = {320, 0, 160, 64, 20, 40},
            [ICO_GLYPH_R1] = {317, 128, 160, 64, 20, 40},
            [ICO_GLYPH_L2] = {319, 64, 160, 64, 20, 40},
            [ICO_GLYPH_R2] = {318, 192, 160, 64, 20, 40},
            [ICO_GLYPH_LEFT] = {272, 198, 20, 20, 20, 40},
            [ICO_GLYPH_RIGHT] = {273, 230, 20, 20, 20, 40},
        },
    /* texLayout[9] {14, 16} and [10] {16, 17} (JPN {20, 22}, {22, 23}):
       Continue 14 at y 135, New Game 15 and 16 at 165 (layout_action.c:832,
       901 PAL_JPN_KOR(49, 20, 14) .. (51, 22, 16)); the copyright line is
       two halves as on the Japanese disc, 12 and 13 at y 195 (dispX 64 and
       319) in texLayout[8] {12, 14} */
    .row_title_continue = 14,
    .row_title_new = 15,
    .row_title_new_only = 16,
    .row_title_copyright = 12,
    .row_title_copyright2 = 13,
    .pause_first = 263, /* texLayout[54] {263, 268} (JPN {269, 274}) */
    .pause_last = 268,

    /* stageTable, objTableScene and modelData: the Japanese tables (the
       kor and jpn C equal); read by name on the Korean ELF: stageTable[949]
       "NULL" (group -1), [66] film_noise_l4.bga, [75] deja_lightning.bga
       (objects 165..177), [140] st19a_chain.bga group 636, [380]
       st10l_chain_down.bga group 731, objTableScene[175] {1598, 35} */
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

    /* the story flags: no selector at any of these sites in the merged
       tree, so the Korean program's ids are the Japanese (and PAL) ones */
    .gflag_new_game = 382,     /* layout_action.c:746,775 gflagOn(382) */
    .gflag_queen_dead = 338,   /* st25a.c:654 gflagOn(338) */
    .gflag_beach_secret = 354, /* end.c:1075 gflagOn(354) */
    .gflag_count = 400,        /* gflag.c has no region block */

    /* the ending: adpcmFile and exitData the Japanese tables (the kor and
       jpn C equal), read by name on the Korean ELF; end.c's scene chain is
       the Japanese one (no Korean block in end.c or BgAnimation.c), so the
       roll's song position is the Japanese value */
    .credits_song = 47, /* adpcmFile[47] event/39_8.int, the table as PAL */
    /* the song's position at the roll at 30 Hz: the Japanese end.c runs
       the same scene chain as PAL's (the ids aside), each scene a stage
       animation run to its end, and with systemStatus[0] 0 BgAnimation.c
       plays them at 1.0 a tick, as the European core does in its 60 Hz
       mode (0.828 a tick only at 50 Hz). Measured on the European core at
       60 Hz (issue 101's log, a whole ending: the song opens at Main tick
       119656, two after actEndDemo06's layout 55 at 119654; the roll
       starts at 124183, between layouts 54 and 55 of actStaff1): 4527
       ticks of 2 NTSC fields, 151,051 ms (PAL's 25 Hz run: 3777 ticks,
       151,080 ms). Not yet run on the Japanese or Korean disc (the user's
       rc check) */
    .credits_roll_ms = 151050,
    .credits_entrance_exit = 216, /* exitData[216] leads to 58 (STAFF1) on the Korean ELF */
    .credits_boy_label = 54,      /* act-game.c ACTGame_SetActors_Debug, unchanged */
    /* DATA.DF's directory (182 entries) holds STGSTAFF1.DF, STGSTAFF2.DF and STGSTAFF3.DF */
    .credits_staff_packs = 1,

    /* sound: data_schema.kor.txt's counts are the Japanese (and PAL) ones,
       the tables equal to the Japanese ELF's; adpcmFile read by name on the
       Korean ELF: [56] event2/50.int, [100] event2/88.int, [101]
       event2/hint1_1.int .. [104] event2/hint2_2.int */
    .n_adpcm = 105,
    .n_sefile = 104,
    .n_sedef = 1426,
    .n_selist = 3837,
    .n_seenv = 425,
    .stream_music_last = 100,  /* adpcmFile[100] event2/88.int, the last scene stream */
    .stream_voice_first = 101, /* event2/hint1_1.int .. hint2_2.int */
    .stream_voice_last = 104,
    .gallery_title_theme = 56, /* adpcmFile[56] event2/50.int as PAL */
};
