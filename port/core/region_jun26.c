/*
 * port/core/region_jun26.c
 *
 * The June 2001 magazine demo, SCUS-97158 (region.h): the game on the
 * Official PlayStation Magazine's demo disc, in the disc's CUS97113 folder
 * (its ELF CUS97113/SCPS_000.00 boots from the magazine's menu SCUS_971.58).
 * Each value was resolved by name against this disc's own sources and
 * tables, as region_ntsc.c's and region_aug6.c's were: upstream's sources at
 * 3947e0641 with their VERSION_ blocks resolved for VERSION_JUN26 (cited by
 * file and function), and the demo ELF's tables read in the PAL record's
 * form with tools/gen_data_c.py --version jun26 (the table, the row and the
 * name that identifies it: a layout by its proc, a menu row by its screen
 * and its place in the layout, a stage by its stageData name, an animation
 * by its .bga, a sound by its file). This build is older than the August
 * 2001 preview: its stageData has 103 rows (a memory row before staff1, the
 * E3 rows at its end), its texLayout and texProperty rows are numbered as
 * the preview's and the North American disc's (every row used here compared),
 * its story flags are numbered its own way (16 lower than the preview's at
 * the New Game flag), and its sound and animation lists are shorter. The
 * demo's DATA.DF holds 49 of the retail discs' 172 members, so most stages
 * the tables name have no data on the disc: the stage numbers below are the
 * tables', not a list of what the demo can load. Values with no counterpart
 * on this disc say what the port does instead; one value is not measured
 * (credits_roll_ms).
 */
#include "region.h"

const IcoRegionInfo ico_region_info_jun26 = {
    .slug = "jun26",
    .disc_id = "SCUS-97158",     /* the magazine disc's product code; SYSTEM.CNF BOOT2
                                  cdrom0:\SCUS_971.58;1 (the magazine's menu) */
    .boot_elf = "SCUS_971.58",   /* BOOT2's file, the menu: the row's own name
                                  (config/regions.txt); the game's ELF, which the
                                  table loader opens, is CUS97113/SCPS_000.00
                                  (port/data/discs.c's elf_path) */
    .pack_serial = "SCUS-97158", /* PCSX2 names a disc's folder by BOOT2's serial: the
                                    demo disc's, not the North American disc's whose
                                    game it carries (and the row's id, as every row's) */
    .archive_name = "ico-jun26.o2r",
    .image_base = "Ico_JUN26",
    .key_prefix = "jun26_", /* the slug, as the other rows' */
    .key_suffix = "_jun26",

    .mc_save_dir = "BISCUS-97113ico", /* ico2/fumi/ios/mcard.c iOSMcSaveSeg[0] and the
                                         product file's path "/BISCUS-97113ico" (the
                                         demo's own, with I; the North American disc's
                                         is BASCUS-97113ico; the port keeps the saves
                                         apart by slug) */
    .mc_product_size = 0x18C,         /* typedef.h McProductFile (the NTSC/AUG6/JUN26
                                         arm): file[20] 0x140, soundMode, outputMode,
                                         vibration, padConf[64] (0x14C): mcard.c
                                         product_write's sizeof */

    .has_50hz_row = 0,      /* NTSC only: main.c systemStatus starts {0, 2, ...}, nothing
                               sets word 0 (no 50 Hz video mode) */
    .has_game_language = 0, /* main.c takes the magazine menu's language from sceDemoStart
                               (demoDisc.c) and only prints it; main.c sets
                               NonLinearCameraMove 0: the game picks no language */
    .ui_lang_default = 0,   /* UI_LANG_EN: the disc's text is English */
    .menu_sheet = 0,        /* UI_LANG_EN: the disc's Tex_menu01 sheets (texFile 47..51)
                               are lettered in English; their own ink is not measured
                               (menu_font.c kSheetInk has no entry for this disc), so
                               the European English look is the one used */
    .fixed_60hz = 1,        /* NTSC only: a 30 Hz Main tick */
    .pad_confirm = 0x40,    /* Cross: layout_action.c la_vibe_select (pad[0].flags & 0x40)
                               and PSH_POSITIVE, as PAL */
    .pad_back = 0x10,       /* Triangle: la_vibe_select's PSH_NEGATIVE (pad[0].flags &
                               0x10), as PAL */

    .reduction_crops_rows = 0, /* GsBase.c gsb_Reduction (the AUG6/JUN26 arm): SCISSOR rows
                                  0 .. H/2-1 (the European one crops 2 or 8), as the
                                  North American disc */

    /* stageData: 103 rows (no memoryPAL, memoryPAL576; a memory row at 58
       before staff1; the E3 rows 91..102 at the end) */
    .stage_count = 103,  /* data_schema.jun26.txt stage-all */
    .stage_title = 1,    /* stageData[1] logo, as PAL */
    .stage_credits = 59, /* stageData[59] staff1 STAFF1 (st13b) */
    .stage_end = 61,     /* stageData[61] end */
    .stage_veil = 40,    /* stageData[40] deja, as PAL */
    .gallery_first = 1,  /* rows 1..39 as PAL (logo .. beach) */
    .gallery_last = 39,
    .last_shipped_stage = 57, /* stageData[57] 26b4demo2 (56 is 26b4demo) */
    /* gate 11, grave 13, windmill 15, symmetry_L 18, taki 22, gondola 25,
       watertower 26, symmetry_R 27, cliff 31, queen 37, beach 39: as PAL */
    .ach_stages = {11, 13, 15, 18, 22, 25, 26, 27, 31, 37, 39},
    .area_name_row_base = 98, /* layout_action.c _la_set_preview_info (the AUG6/JUN26 arm)
                                 lt_mask_property(n + 98) */
    /* _la_set_preview_info maps no stage on this disc (the `n == 61 -> 38`
       of the other arm is not in it): END keeps its own number, outside
       3 .. 55, and the save screen shows no name for it; the port's areaName
       (settings.c) does the same with END as END */
    .area_name_end_as = 61,
    .start_stage_max = 102,

    /* texLayout: 75 rows, the procs at the North American disc's and the
       preview's places (compared row by row for every row used here) */
    .layout_count = 75,         /* data_schema.jun26.txt texture-layout 75 */
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

    /* texProperty: 384 rows (52 texFile rows: Tex_menu01's English sheets,
       no per-language copies) */
    .property_count = 384,    /* data_schema.jun26.txt tex-property 384 */
    .row_pause_options = 263, /* la_game_pause's rows 261..265, its 3rd..5th: 263's */
    .row_pause_back = 264,    /* right is la_game_option (54), 265's la_end_confirm */
    .row_pause_end = 265,     /* (57), as PAL's 294 and 296 lead to 58 and 61 */
    .row_mc_picture = 17,     /* layout_action.c la_mc_file_select lt_mask_property(i + 17, 1) */
    .row_mc_item = 27,        /* and lt_mask_property(i + 27, 0) (PAL 52, 62) */
    .glyph =
        {
            /* Tex_menu01/option.tm2 (texFile 47), each row's rectangle and
           dispH read from this disc's texProperty: the North American disc's
           and the preview's numbers. The key config's button rows 291..298
           (layout_action.c la_key_config's mask loop, i + 291; Triangle,
           Square, Circle, Cross at u 128..224 v 240, R1, R2, L2, L1 at u
           128, 192, 64, 0 v 160); Cross and Triangle are the save prompts'
           Confirm and Back glyphs (146 at x 180 and 148 at x 364: this disc
           confirms with Cross and goes back with Triangle, as PAL); the
           arrows are la_game_option's value arrows 270/271. disp_h is each
           row's own dispH (30 for the 32 x 15 buttons, 40 for the 64 x 20
           shoulder labels and the arrows) */
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
       Game 15 (14's downItem) and 16 at 165; the copyright line is two
       halves: 12 and 13 at y 195 (dispX 64 and 320), texLayout[8] {12, 14} */
    .row_title_continue = 14,
    .row_title_new = 15,
    .row_title_new_only = 16,
    .row_title_copyright = 12,
    .row_title_copyright2 = 13,
    .pause_first = 261, /* texLayout[53] {261, 266} */
    .pause_last = 266,

    .anim_none = 763,         /* stage-anim 764 rows, the last ("NULL", no -1) */
    .chain_group_st10l = 731, /* stageTable st10l_chain_down.bga: group 731 as PAL
                                 (its row is 315, PAL 384) */
    .chain_group_st19a = 636, /* st19a_chain.bga: group 636 as PAL (row 110, PAL 144) */
    /* deja_lightning.bga's objects (objTableScene 154..159: kind 35, models
       666..670 st13_deja1..6.p2o and 263 4a_gate.p2o) hold no sandstorm:
       no modelData row of this disc's 1474 names sandstorm.p2o, so there is
       no veil to find; -1 for none (photo_view.c veilObject then finds no
       object, its "NULL without one") */
    .veil_model = -1,
    .veil_kind = 35, /* deja_lightning.bga's objects' kind, as PAL */
    /* stageTable has no film_noise_l4.bga on this disc (764 rows;
       film_noise.bga only, row 51): the deja vision's film reel the photo
       view replays has no row; -1 for none (photo_view.c must leave the reel
       out; owed to the merge) */
    .reel_row = -1,
    .veil_anim_row = 60,     /* stageTable st13c/anim/deja_lightning.bga (PAL 79) */
    .mv_motion_kinds = 1165, /* motion-def 1168 rows: 1165 motions, then KEEP, ALWAYS, NULL
                                (model_viewer.h MV_MOTION_TABLE_SIZE is this disc's motionTable,
                                1170) */
    .mv_orient_rows = 2166,  /* data_schema.jun26.txt motion-orient 2166 */

    .gflag_new_game = 333,   /* layout_action.c la_vibe_select (the JUN26 arm): gflagOn(333)
                                after gflagInit (PAL 382; keepFlagNo {339, 335, 334, 336,
                                333}) */
    .gflag_queen_dead = 292, /* ico2/script/src/st25a.c actSt25aQueenDeadChk: gflagOn(292) */
    /* end.c actSt27aEndChk (the JUN26 twin) sets only flag 308 and starts
       actSt27aEndDemo: no flag for a held item (PAL's gflagOn(354) when
       GetCharHeldItem is 3 is not in it); -1 for none (achievements.c must
       not test it; owed to the merge) */
    .gflag_beach_secret = -1,
    .gflag_count = 352, /* ico2/script/src/gflag.c gFlag[44]: 352 flags (PAL 50 bytes, 400;
                           credits_live.c asserts 400 on every disc: owed to the merge) */

    /* adpcmFile[46] event/39.int, the one song of this disc's ending:
       end.c actEndDemo01Chk requests it (ed1) and no later scene requests
       another (PAL's ending plays event/39_1.int .. 39_8.int, the roll's
       song its 47) */
    .credits_song = 46,
    /* NOT MEASURED. This disc's ending plays one file from its first scene
       (event/39.int, 9724 blocks), not PAL's chain, so the European
       measurement does not carry over. The European 60 Hz figure stands in,
       as on the North American and preview discs, until a headless run of
       the ending on this disc measures it (the song's position at the roll:
       credits_live.c SONG_AT_ROLL_MS) */
    .credits_roll_ms = 151050,
    .credits_entrance_exit = 212, /* exitData[212] {1575, 672, -1620} leads to 59 (STAFF1), as
                                     PAL's exitData[216] to 60 */
    .credits_boy_label = 47,      /* act-game.c ACTGame_SetActors_Debug's first row (the JUN26
                                     arm) {47, 1, hasBoy} (PAL 54) */
    /* NONE: CUS97113/DFDATAS/DATA.DF's directory (49 entries) has no STGSTAFF*.DF
       (nor STGEND.DF, STG26B2.DF, STGLOG.DF or STGTTL.DF): stageData[59] staff1 names
       STGSTAFF1, which this disc does not carry, so Extras > Credits refuses here */
    .credits_staff_packs = 0,

    .n_adpcm = 90, /* data_schema.jun26.txt: adpcmfile 90, sefile 104, sedef 1337, selist
                      3781, se-env 420 */
    .n_sefile = 104,
    .n_sedef = 1337,
    .n_selist = 3781,
    .n_seenv = 420,
    .stream_music_last = 89, /* adpcmFile 1..89: music and scenes (the last, event2/84.int) */
    /* no hint voices on this disc (no event2/hint*.int rows: this build has
       no hint system): an empty range past the table */
    .stream_voice_first = 90,
    .stream_voice_last = 89,
    .gallery_title_theme = 51, /* adpcmFile[51] event2/50.int: op.c's title request
                                  scpAdpcmPlayRequestFunc(51, &titleAdpcm, ...) (PAL 56) */
};
