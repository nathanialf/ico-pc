/*
 * port/core/region_aug6.c
 *
 * The August 2001 preview disc, PROTO-20010806 (region.h): an earlier build of the game
 * than the North American release. Each value was resolved by name
 * against this disc's own sources and tables, as region_ntsc.c's were:
 * upstream's sources at 3947e0641 with their VERSION_ blocks resolved for
 * VERSION_AUG6 (tools/gen_data_c.py version_lines; cited by file and
 * function), and the preview ELF's tables read in the PAL record's form with
 * tools/gen_data_c.py --version aug6 (the table, the row and the name that
 * identifies it: a layout by its proc, a menu row by its screen and its place
 * in the layout, a stage by its stageData name, an animation by its .bga, a
 * sound by its file). This build is a program of its own: its stageData rows,
 * texLayout, texProperty positions, exit and sound lists are the North
 * American disc's, row for row (compared table against table), but its
 * story flags are numbered one lower from 300, its animation, model and
 * motion lists are shorter by a few rows, and its code differs in many
 * places. Values with no difference from the North American disc say so and
 * why; values the disc has no counterpart for say what the port does
 * instead; one value is not measured (credits_roll_ms).
 */
#include "region.h"

const IcoRegionInfo ico_region_info_aug6 = {
    .slug = "aug6",
    .disc_id = "PROTO-20010806", /* the disc has no product code: the row's own id
                                    (config/regions.txt); SYSTEM.CNF BOOT2
                                    cdrom0:\SCUS_971.13;1 */
    .boot_elf = "SCUS_971.13",   /* the North American disc boots the same name:
                                    port/data/extract.c ico_disc_identify tells them
                                    apart by the ELF's SHA-1 */
    .pack_serial = "SCUS-97113", /* the folder PCSX2 names this disc's textures by (its
                                    BOOT2 SCUS_971.13), shared with the North American
                                    disc: pack files are named by texture hash, so a USA
                                    pack serves both; the disc id stays PROTO-20010806
                                    everywhere else */
    .archive_name = "ico-aug6.o2r",
    .image_base = "Ico_AUG6",
    .key_prefix = "aug6_", /* the slug, as the other rows' */
    .key_suffix = "_aug6",

    .mc_save_dir = "BASCUS-97113ico", /* ico2/fumi/ios/mcard.c iOSMcSaveSeg[0], as the North
                                         American disc (its card folder is its own: below) */
    .mc_product_size = 0x18C,         /* typedef.h McProductFile: file[20] 0x140, soundMode,
                                         outputMode, vibration, padConf[64] (0x14C):
                                         mcard.c product_write's sizeof; no cameraMove or
                                         palMode, no 8-byte member */
    /* PC port: the preview's card is a folder of its own, memcard-preview
       beside memcard: its records are not the North American disc's (mcard.c
       product file summaries stage, cleared, playTime, sofa against stage,
       playTime, sofa, word10; story flags from 300 on one lower, gflag_new_game
       348 against 349), so one card would mix them */
    .mc_card_suffix = "-preview",

    .has_50hz_row = 0,      /* NTSC only: main.c systemStatus starts {0, 2, ...}, nothing sets
                               word 0 (no 50 Hz video mode) */
    .has_game_language = 0, /* kanbanBoot.c sets no NonLinearCameraMove (PAL's language
                               step and texFile's cameraMove are not in it) */
    /* has_hold_film_players: as the North American disc (la_game_option_ntscaug6,
       _ACTCommonMailTest_aug6, reel_row -1) */
    .has_hold_film_players = 0,
    .ui_lang_default = 0, /* UI_LANG_EN: the disc's text is English */
    .menu_sheet = 0,      /* UI_LANG_EN: the disc's Tex_menu01 sheets are lettered in
                               English; their own ink is not measured (menu_font.c
                               kSheetInk has no entry for this disc), so the European
                               English look is the one used */
    .fixed_60hz = 1,      /* NTSC only: a 30 Hz Main tick */
    .pad_confirm = 0x40,  /* Cross: layout_action.c la_vibe_select (pad[0].flags & 0x840,
                               Cross or Start), as PAL */
    .pad_back = 0x10,     /* Triangle: la_vibe_select (pad[0].flags & 0x10) == 0, as PAL */

    .reduction_crops_rows = 0, /* GsBase.c gsb_Reduction: SCISSOR rows 0 .. H/2-1 (the
                                  European one crops 2 or 8), as the Japanese and North American discs */

    /* stageData: 104 rows, name for name the North American disc's (compared
       row by row; no memoryPAL, memoryPAL576), so the rows from staff1 on are
       2 lower than PAL's */
    .stage_count = 104,  /* data_schema.aug6.txt stage-all */
    .stage_title = 1,    /* stageData[1] logo, as PAL */
    .stage_credits = 58, /* stageData[58] staff1 STAFF1 (st13b) */
    .stage_end = 61,     /* stageData[61] end */
    .stage_veil = 40,    /* stageData[40] deja, as PAL */
    .gallery_first = 1,  /* rows 1..39 as PAL (logo .. beach) */
    .gallery_last = 39,
    .last_shipped_stage = 56, /* stageData[56] 26b4demo2, as PAL */
    /* gate 11, grave 13, windmill 15, symmetry_L 18, taki 22, gondola 25,
       watertower 26, symmetry_R 27, cliff 31, queen 37, beach 39: as PAL */
    .ach_stages = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39},
    .area_name_row_base = 98, /* layout_action.c _la_set_preview_info lt_mask_property(n + 98) */
    /* _la_set_preview_info maps no stage on this disc (PAL's `n == 63 -> 38`
       is not in it): END keeps its own number, outside 3 .. 55, and the save
       screen shows no name for it; the port's areaName (settings.c) does the
       same with END as END (61 is past its last named stage) */
    .area_name_end_as = 61,
    .start_stage_max = 103,

    /* texLayout: 75 rows, identical to the North American disc's (compared
       row by row); the procs from la_vibe_select on are 3 lower than PAL's up
       to la_save_game_memory_card_check, 4 from there on (one unnamed row
       fewer before it) */
    .layout_count = 75,         /* data_schema.aug6.txt texture-layout 75 */
    .layout_pause = 53,         /* texLayout[53] la_game_pause */
    .layout_title_continue = 9, /* texLayout[9] la_title_continue_or_new */
    .layout_title_new = 10,     /* texLayout[10] la_title_new_game_only */
    .layout_options = 54,       /* texLayout[54] la_game_option (row 263's right) */
    .layout_key_config = 55,    /* texLayout[55] la_key_config */
    .layout_vibe = 6,           /* texLayout[6] la_vibe_select */
    .layout_play = 50,          /* texLayout[50] la_game_loop */
    .layout_scene = 51,         /* texLayout[51] la_game_demo */
    .layout_mc_files = 11,      /* texLayout[11] la_mc_file_select */
    .layout_load = 22,          /* texLayout[22] la_load_processing */
    .layout_save_complete = 37, /* texLayout[37] la_save_confirm_complete */

    /* texProperty: 384 rows, numbered as the North American disc's (every
       row's position, sheet and links compared; 52 texFile rows: Tex_menu01's
       English sheets, no per-language copies) */
    .property_count = 384,    /* data_schema.aug6.txt tex-property 384 */
    .row_pause_options = 263, /* la_game_pause's rows 261..265, its 3rd..5th: 263's */
    .row_pause_back = 264,    /* right is la_game_option (54), 265's la_end_confirm */
    .row_pause_end = 265,     /* (57), as PAL's 294 and 296 lead to 58 and 61 */
    .row_mc_picture = 17,     /* layout_action.c la_mc_file_select lt_mask_property(i + 17, 1) */
    .row_mc_item = 27,        /* and lt_mask_property(i + 27, 0) (PAL 52, 62) */
    .glyph =
        {
            /* Tex_menu01/option.tm2, the Japanese and North American discs' sheet
           geometry (rows 146, 148, 270, 271 and 291..298 hold the same sheet,
           rectangle and display size here). The
           key config's button rows 291..298 (layout_action.c keyconfigMaskAll
           and la_key_config, i * 8 + 291 + slot; keyConfigCode's order Triangle, Square, Circle, Cross,
           R1, R2, L2, L1 as PAL's 342..349); Cross and Triangle are the save
           prompts' Confirm and Back glyphs (146 at x 180 and 148 at x 364,
           as PAL's 182 and 184: this disc confirms with Cross and goes back
           with Triangle, as PAL); the arrows are la_game_option's value
           arrows 270/271 (PAL 301/302). disp_h is each row's own dispH (30
           for the 32 x 15 buttons, 40 for the 64 x 20 shoulder labels and
           the arrows) */
            [ICO_GLYPH_CROSS] = {146, 224, 240, 32, 15, 30},
            [ICO_GLYPH_CIRCLE] = {293, 192, 240, 32, 15, 30},
            [ICO_GLYPH_SQUARE] = {292, 160, 240, 32, 15, 30},
            [ICO_GLYPH_TRIANGLE] = {148, 128, 240, 32, 15, 30},
            [ICO_GLYPH_L1] = {298, 0, 160, 64, 20, 40},
            [ICO_GLYPH_R1] = {295, 128, 160, 64, 20, 40},
            [ICO_GLYPH_L2] = {297, 64, 160, 64, 20, 40},
            [ICO_GLYPH_R2] = {296, 192, 160, 64, 20, 40},
            [ICO_GLYPH_LEFT] = {270, 198, 20, 20, 20, 40},
            [ICO_GLYPH_RIGHT] = {271, 230, 20, 20, 20, 40},
        },
    /* texLayout[9] {14, 16} and [10] {16, 17}: Continue 14 at y 135, New
       Game 15 (14's downItem) and 16 at 165 (PAL 49, 50, 51); the copyright
       line is two halves, as on the Japanese disc: 12 and 13 at y 195
       (dispX 64 and 320, Tex_menu01/scei.tm2), texLayout[8] {12, 14} */
    .row_title_continue = 14,
    .row_title_new = 15,
    .row_title_new_only = 16,
    .row_title_copyright = 12,
    .row_title_copyright2 = 13,
    .pause_first = 261, /* texLayout[53] {261, 266} */
    .pause_last = 266,

    .anim_none = 814,         /* stage-anim 815 rows, the last ("NULL") */
    .chain_group_st10l = 731, /* stageTable st10l_chain_down.bga: group 731 as PAL
                                 (its row is 348, PAL 384) */
    .chain_group_st19a = 636, /* st19a_chain.bga: group 636 as PAL (row 123, PAL 144) */
    .veil_model = 1558,       /* objTableScene[158] of deja_lightning.bga's 148..160 (PAL
                                 1614 at the same place); modelData[1558]'s modelPath is
                                 object/sdf/obj_com04/model/sandstorm.p2o; model-path 1576
                                 rows, PAL 1637 */
    .veil_kind = 35,          /* the same objects' kind, as PAL */
    /* stageTable has no film_noise_l4.bga on this disc (815 rows; film_noise.bga
       only): the deja vision's film reel the photo view replays has no row; -1
       for none (photo_view.c must leave the reel out; owed to the merge) */
    .reel_row = -1,
    .veil_anim_row = 66,     /* stageTable deja_lightning.bga (PAL 79) */
    .mv_motion_kinds = 1086, /* motion-def 1089 rows: 1086 motions, then KEEP, ALWAYS, NULL */
    .mv_orient_rows = 2139,  /* data_schema.aug6.txt motion-orient 2139 */

    .gflag_new_game = 348,   /* layout_action.c la_vibe_select: gflagOn(348) after
                                gflagInit (PAL 382; keepFlagNo {354, 350, 349, 351, 348}) */
    .gflag_queen_dead = 306, /* ico2/script/src/st25a.c actSt25aQueenDeadChk: gflagOn(306) */
    /* end.c actSt27aEndChk sets no flag for a held item on this disc (PAL's
       gflagOn(354) when GetCharHeldItem is 3 is not in it): -1 for none
       (achievements.c must not test it; owed to the merge) */
    .gflag_beach_secret = -1,
    .gflag_count = 368, /* ico2/script/src/gflag.c gFlag[46]: 368 flags (PAL 50 bytes, 400;
                           credits_live.c asserts 400 on every disc: owed to the merge) */

    .credits_song = 47, /* adpcmFile[47] event/39_6.int, the ed6 request of
                           end.c actEndDemo06Chk (PAL's 47 is event/39_8.int) */
    /* NOT MEASURED. This disc's ending chain is the North American disc's, not
       PAL's (in actEndDemo06Chk the song follows ed5's adpcmFile[46] instead
       of opening the function), so the European measurement does not carry
       over. The European 60 Hz figure stands in, as on the North American
       disc, until a headless run of the ending on this disc measures it (the
       song's position at the roll: credits_live.c SONG_AT_ROLL_MS) */
    .credits_roll_ms = 151050,
    .credits_entrance_exit = 212, /* exitData[212] {1575, 672, -1620} leads to 58 (STAFF1), as
                                     PAL's exitData[216] to 60 */
    .credits_boy_label = 50,      /* act-game.c ACTGame_SetActors_Debug's first row {50, 1,
                                     hasBoy} (PAL 54; the North American disc 51) */
    /* DATA.DF's directory (172 entries) holds STGSTAFF1.DF, STGSTAFF2.DF and STGSTAFF3.DF */
    .credits_staff_packs = 1,

    .n_adpcm = 96, /* data_schema.aug6.txt: adpcmfile 96, sefile 104, sedef 1389, selist
                      3806, se-env 425 */
    .n_sefile = 104,
    .n_sedef = 1389,
    .n_selist = 3806,
    .n_seenv = 425,
    .stream_music_last = 95, /* adpcmFile 1..95: music and scenes (the last, event2/85.int) */
    /* no hint voices on this disc (no event2/hint*.int rows: this build has
       no hint system): an empty range past the table */
    .stream_voice_first = 96,
    .stream_voice_last = 95,
    .gallery_title_theme = 54, /* adpcmFile[54] event2/50.int (PAL 56) */
};
