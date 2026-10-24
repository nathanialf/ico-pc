/*
 * port/core/region.h
 *
 * What differs between the discs the port plays, as the port's own code
 * needs it: one IcoRegionInfo per row of config/regions.txt, region_pal.c
 * (the European release SCES-50760), region_jpn.c (the Japanese release
 * SCPS-11003) and region_kor.c (the Korean release SCPS-56001), collected
 * in ico_region_info[] in the table's order. The
 * port names game data by number in many places (a layout, a menu row, a
 * stage, a story flag); each number here was resolved by name against that
 * disc's own sources and tables, never by a blanket shift: the two builds
 * differ in the middle of most tables.
 *
 * The selected region is the game's own index, ico_region (the game header
 * ico2/common/include/region.h, which the game code reads through the
 * ICO_<SET> predicates and PAL_<SET> selectors, docs/port/CONVENTIONS.md);
 * it is set once, by ico_region_select when the disc image is
 * identified, and is 0 (the European disc) until then, so code and tests
 * that never select one keep the European values.
 *
 * Plain data and look-ups, no other game header: compiled once (the
 * ico_region library), linked by the program and by every test program
 * that compiles game sources.
 */
#ifndef ICO_PORT_REGION_H
#define ICO_PORT_REGION_H

/* ico_region, ICO_REGION_<SLUG>, ICO_REGION_COUNT (the path, not the name:
   this header is region.h too) */
#include "../../ico2/common/include/region.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The button glyphs the port draws in its own texts (port/ui/layout_ext.c
   kGlyph), by button: the boot ELF's menu row that shows it and the texel
   rectangle of that row's sheet (the disc's own art). */
enum {
    ICO_GLYPH_CROSS,
    ICO_GLYPH_CIRCLE,
    ICO_GLYPH_SQUARE,
    ICO_GLYPH_TRIANGLE,
    ICO_GLYPH_L1,
    ICO_GLYPH_R1,
    ICO_GLYPH_L2,
    ICO_GLYPH_R2,
    ICO_GLYPH_LEFT,
    ICO_GLYPH_RIGHT,
    ICO_GLYPH_COUNT
};

typedef struct IcoGlyphRow {
    short row;        /* texProperty row */
    short u, v, w, h; /* the texel rectangle on its sheet */
    short disp_h;     /* the height the game shows it at (that row's dispH, y units) */
} IcoGlyphRow;

#define ICO_REGION_ACH_STAGES 11

typedef struct IcoRegionInfo {
    /* --- the disc ------------------------------------------------------ */
    const char *
        slug; /* "pal", "jpn", "kor", "ntsc", "aug6", "jun26", "cn": config/regions.txt's first column */
    const char *disc_id;      /* "SCES-50760": its third */
    const char *boot_elf;     /* "SCES_507.60": its fourth */
    const char *pack_serial;  /* texture and model pack folders (PCSX2's) */
    const char *archive_name; /* "ico.o2r", "ico-jpn.o2r", "ico-kor.o2r", "ico-ntsc.o2r", ... */
    const char *image_base;   /* "Ico_PAL", "Ico_JPN", "Ico_KOR", "Ico_USA" (ntsc), ... */
    /* config.toml keys kept per disc (options.c, achievements.c): the
       European disc keeps the names it always had ("" prefix and suffix) */
    const char *key_prefix; /* [mirror] <prefix>slot_N: "", "jpn_", "kor_" */
    const char *key_suffix; /* stats.stages<suffix>: "", "_jpn", "_kor" */

    /* --- the memory card --------------------------------------------- */
    const char *mc_save_dir; /* fumi/ios/mcard.c's product directory */
    int mc_product_size;     /* the bytes of McProductFile the disc's game writes */

    /* --- the game's options ------------------------------------------- */
    int has_50hz_row;      /* the game offers 50/60 Hz (the port's row too) */
    int has_game_language; /* the game picks a language (menus, subtitles) */
    /* the port's own text (port/ui): the language its Menu language row's
       Auto gives on this disc, a port/ui/strings.h UiLang (UI_LANG_JA 5,
       UI_LANG_KO 6), or -1 for the game's own language (has_game_language),
       else English (port/ui/ui_lang.c ui_lang_resolve); and the sheet
       language the disc's menu sheets are lettered in, the look and
       geometry of the port's words drawn over them (strings.h UI_SHEET_JA
       5, UI_SHEET_KO 6), or -1 for the game's own language's sheets (the
       European disc's five, settings.c ui_settings_apply_language).  Plain
       numbers here: port/core does not include port/ui's headers;
       ctest strings_complete holds them to the UiLang and UI_SHEET_* ids. */
    int ui_lang_default;
    int menu_sheet;
    int fixed_60hz;  /* NTSC only: systemStatus[0] is 0 (60 Hz) from the disc's mount */
    int pad_confirm; /* pad[0].flags bit the menus confirm with */
    int pad_back;    /* and cancel with */

    /* --- the picture --------------------------------------------------- */
    int reduction_crops_rows; /* gsb_Reduction's border crop keeps the top and
                                 bottom rows black (2 of 224, 8 of 256); 0: it
                                 keeps every row (rd_post.c rd__reduction_crop) */

    /* --- stages (stageData rows) --------------------------------------- */
    int stage_count;   /* rows of stageData */
    int stage_title;   /* the boot logo and title (STAGE_TITLE) */
    int stage_credits; /* STAFF1 (st13b), where the roll starts */
    int stage_end;     /* END: the save screen shows it as stage 38 */
    int stage_veil;    /* deja, the deja vision */
    int gallery_first; /* the game's own stages (1 logo, 2 title, 3..) */
    int gallery_last;
    int last_shipped_stage;                /* the last cutscene stage (26b4demo2) */
    int ach_stages[ICO_REGION_ACH_STAGES]; /* achievements.c's, in its order */
    int area_name_row_base;                /* the save screen's stage name row: stage + it */
    int area_name_end_as;                  /* stage_end shows as this stage's name */
    int start_stage_max;                   /* the highest stage start_stage/switch_to take */

    /* --- layouts (texLayout rows) -------------------------------------- */
    int layout_count;          /* LT_GAME_LAYOUT_COUNT */
    int layout_pause;          /* la_game_pause */
    int layout_title_continue; /* la_title_continue_or_new */
    int layout_title_new;      /* la_title_new_game_only */
    int layout_options;        /* la_game_option (Key config's way back) */
    int layout_key_config;     /* la_key_config */
    int layout_vibe;           /* la_vibe_select */
    int layout_play;           /* la_game_loop */
    int layout_scene;          /* la_game_demo (the empty layout of a scene) */
    int layout_mc_files;       /* la_mc_file_select */
    int layout_load;           /* la_load_processing */
    int layout_save_complete;  /* la_save_confirm_complete */

    /* --- menu rows (texProperty rows) ----------------------------------- */
    int property_count;    /* LT_GAME_PROPERTY_COUNT */
    int row_pause_options; /* the pause menu: Options, Back, End Game */
    int row_pause_back;
    int row_pause_end;
    int row_mc_picture; /* memory card file i's picture: + i */
    int row_mc_item;    /* and its item row: + i */
    /* the title's rows (settings.c placeTitle): Continue, New Game on
       la_title_continue_or_new, New Game on la_title_new_game_only, the
       copyright line (one row, or two halves: the second -1 for none) */
    int row_title_continue;
    int row_title_new;
    int row_title_new_only;
    int row_title_copyright;
    int row_title_copyright2;
    /* la_game_pause's rows, first .. last (exclusive) */
    int pause_first;
    int pause_last;
    IcoGlyphRow glyph[ICO_GLYPH_COUNT];

    /* --- stage animations, models -------------------------------------- */
    int anim_none;         /* stageTable's "no animation" sentinel */
    int chain_group_st10l; /* rd_content_chain.c: st10l_chain_down.bga's group */
    int chain_group_st19a; /* st19a_chain.bga's */
    int veil_model;        /* sandstorm.p2o in deja_lightning.bga's objects */
    int veil_kind;         /* those objects' kind */
    int reel_row;          /* stageTable row of film_noise_l4.bga */
    int veil_anim_row;     /* stageTable row of deja_lightning.bga */
    int mv_motion_kinds;   /* model viewer: motionKind rows before KEEP */
    int mv_orient_rows;    /* motionOrient rows */

    /* --- story flags ---------------------------------------------------- */
    int gflag_new_game;     /* gflagOn at New Game (layout_action.c) */
    int gflag_queen_dead;   /* st25a.c actSt25aQueenDeadChk */
    int gflag_beach_secret; /* end.c actSt27aEndChk */
    int gflag_count;

    /* --- the ending and the credits ------------------------------------ */
    int credits_song;          /* adpcmFile row of "ICO -You were there-" */
    int credits_roll_ms;       /* the song's position when the roll starts
                                  (credits_live.c SONG_AT_ROLL_MS) */
    int credits_entrance_exit; /* exitData row into STAFF1 */
    int credits_boy_label;     /* the boy's object record */

    /* --- sound (the gallery) --------------------------------------------- */
    int n_adpcm, n_sefile, n_sedef, n_selist, n_seenv;
    int stream_music_last;  /* adpcmFile 1..this: music and scenes */
    int stream_voice_first; /* this..stream_voice_last: Yorda's hints */
    int stream_voice_last;
    int gallery_title_theme; /* adpcmFile row of the title's theme */
} IcoRegionInfo;

/* Each row's table (region_<slug>.c) and all of them in config/regions.txt's
   order (region.c; ico_region indexes it). Pointers, since a C initialiser
   cannot copy another object. */
extern const IcoRegionInfo ico_region_info_pal;
extern const IcoRegionInfo ico_region_info_jpn;
extern const IcoRegionInfo ico_region_info_kor;
extern const IcoRegionInfo ico_region_info_ntsc;
extern const IcoRegionInfo ico_region_info_aug6;
extern const IcoRegionInfo ico_region_info_jun26;
extern const IcoRegionInfo ico_region_info_cn;
extern const IcoRegionInfo *const ico_region_info[ICO_REGION_COUNT];

/* The selected region's table: ico_region_info[ico_region]. */
const IcoRegionInfo *ico_region_cur(void);

/* Selects the region by its slug ("pal", "jpn", "kor"): 0, or -1 for a slug that
   is not a row (NULL too), which keeps the selection. The program calls it
   once, when the disc image is identified, before any game code runs. */
int ico_region_select(const char *slug);

/* The rows, 0 .. ico_region_count() - 1 (0 is the European one), and the
   row of a slug or a disc id ("SCPS-11003"); NULL for none. */
int ico_region_count(void);
const IcoRegionInfo *ico_region_at(int i);
const IcoRegionInfo *ico_region_by_slug(const char *slug);
const IcoRegionInfo *ico_region_by_disc_id(const char *disc_id);

/* The selected region's config.toml key: "<section>.<key_prefix><base>"
   (mirror.slot_3, mirror.jpn_slot_3; no section: no dot) and
   "<base><key_suffix>" (stats.stages, stats.stages_jpn). 0, or -1 when out
   (size bytes) is too small. */
int ico_region_key_prefixed(const char *section, const char *base, char *out, int size);
int ico_region_key_suffixed(const char *base, char *out, int size);

/* A stageData row the port names by its European number, on the selected
   disc: rows 0..57 are the same on both, and the Japanese table has no
   memoryPAL (58) or memoryPAL576 (59), so the rows from staff1 on are two
   lower there (58 and 59 themselves have no Japanese row; the port names
   neither). */
int ico_region_stage(int pal_stage);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_REGION_H */
