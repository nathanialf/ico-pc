/*
 * port/ui/strings.h
 *
 * The port's UI strings in the European disc's five languages, in
 * Japanese and in Korean: one table per language (port/ui/strings_<lang>.c), indexed by
 * UiStrId, UTF-8. Every word the port draws (the Settings menu, the popups, the achievements, the
 * galleries, photo mode, the model viewer, Characters, the Graphics driver
 * page), and the menus' words transcribed from the PAL sheets
 * (UI_STR_MT_*).
 */
#ifndef PORT_UI_STRINGS_H
#define PORT_UI_STRINGS_H

#include <stddef.h>
#include <stdint.h>

/* ICO_RC_TOOLS: the release candidates' own tooling (CMake -DICO_RC_TOOLS=ON,
   which tools/package_*.sh pass for a label ending in -rc<N> alone; Gradle
   -PicoRcTools=true; docs/BUILDING.md "Release candidate tools").  0 in
   every other build, which then has none of it: today the Main row "Share
   a bug report" (settings.h ui_settings_set_share_report_handler). */
#ifndef ICO_RC_TOOLS
#define ICO_RC_TOOLS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The port's languages: the European disc's five, in the order of the
   game's language numbers as NonLinearCameraMove holds them after the
   boot screen (kanbanBoot.c 101-102) and texFile names them (2 EG, 3 FR,
   4 GR, 5 IT, 6 SP), then Japanese, Korean and Traditional Chinese (the
   port's own text: the Japanese, Korean and Asian discs' own words are
   their sheets); UI_LANG_COUNT
   tables, each defining every UiStrId (ctest strings_complete). */
typedef enum UiLang {
    UI_LANG_EN = 0,
    UI_LANG_FR,
    UI_LANG_DE,
    UI_LANG_IT,
    UI_LANG_ES,
    UI_LANG_JA,
    UI_LANG_KO,
    UI_LANG_ZH,
    UI_LANG_COUNT
} UiLang;

/* The European disc's languages (UI_LANG_EN .. UI_LANG_ES): its sheets
   letter the menus in each, and the menu text table's geometry
   (menu_text.h UiMenuTextItem) is measured per each. */
#define UI_LANG_PAL_COUNT 5

/* The sheet languages, the language of the sheets the game's menus show:
   the European disc's five (their UiLang numbers), the Japanese disc's
   sheets, UI_SHEET_JA, the Korean disc's, UI_SHEET_KO, and the Asian
   disc's (Traditional Chinese), UI_SHEET_ZH (their UiLang numbers too).  The look of the port's text drawn in the sheets' style
   (menu_font.c kSheetInk) is measured per sheet language.  Which one a
   disc's menus show is its region row's (port/core/region.h
   IcoRegionInfo.menu_sheet, settings.c ui_settings_apply_language). */
#define UI_SHEET_JA 5
#define UI_SHEET_KO 6
#define UI_SHEET_ZH 7
#define UI_LANG_SHEET_COUNT 8
/* The Japanese disc's title sheet: not a sheet language (ui_set_sheet_language
   ignores it) but the geometry of the port's rows on the title screen, whose
   words the title sheet spaces apart more than the other sheets
   (menu_font.c sheetGeo, ui_menu_text_set_title).  The Korean disc's title
   sheet letters its words as its other sheets do (menu_font.c KO_GEO_*), so
   it has no title geometry of its own, nor has the Asian disc's (Traditional
   Chinese; menu_font.c ZH_GEO_*). */
#define UI_SHEET_JA_TITLE 8

typedef enum UiStrId {
    UI_STR_NONE = 0, /* "" */
    /* the menu and its sections */
    UI_STR_SETTINGS,
    UI_STR_SECTION_DISPLAY,
    UI_STR_SECTION_CONTROLS,
    UI_STR_SECTION_GAMEPLAY,
    UI_STR_SECTION_LANGUAGE,
    UI_STR_BACK,
    /* values */
    UI_STR_ON,
    UI_STR_OFF,
    /* Display */
    UI_STR_OPT_PRESET,
    UI_STR_VAL_ORIGINAL,
    UI_STR_VAL_ENHANCED,
    UI_STR_OPT_FULLSCREEN,
    UI_STR_OPT_RESOLUTION,
    UI_STR_OPT_ASPECT,
    UI_STR_OPT_VSYNC,
    UI_STR_OPT_FILTERING,
    UI_STR_OPT_FULL_HEIGHT,
    UI_STR_OPT_MIRROR,
    /* Controls */
    UI_STR_OPT_REMAP,
    UI_STR_OPT_MOUSE_CAMERA,
    UI_STR_OPT_VIBRATION,
    /* Gameplay */
    UI_STR_OPT_STICK_FIX,
    UI_STR_OPT_YORDA,         /* [gameplay] yorda_safe */
    UI_STR_OPT_YORDA_NOTE,    /* its explanation */
    UI_STR_OPT_PIT_TIME,      /* [gameplay] pit_time: the Time to rescue Yorda row */
    UI_STR_OPT_PIT_TIME_NOTE, /* its explanation */
    UI_STR_VAL_PIT_HALF,      /* its values (Original is UI_STR_VAL_ORIGINAL) */
    UI_STR_VAL_PIT_LONGER,
    UI_STR_VAL_PIT_DOUBLE,
    UI_STR_VAL_PIT_TRIPLE,
    /* Language: each language's own name, the same in every table */
    UI_STR_LANG_EN,
    UI_STR_LANG_FR,
    UI_STR_LANG_DE,
    UI_STR_LANG_IT,
    UI_STR_LANG_ES,
    UI_STR_LANG_JA,
    /* Developer mode */
    UI_STR_OPT_DEVELOPER_MODE,
    UI_STR_DEVELOPER_NOTE,
    /* popups */
    UI_STR_POPUP_TEST_TITLE,
    UI_STR_POPUP_TEST_BODY,
    /* achievements: title and
       description of each, in port/game/achievements.c's order */
    UI_STR_ACH_OPENING,
    UI_STR_ACH_OPENING_DESC,
    UI_STR_ACH_HAND,
    UI_STR_ACH_HAND_DESC,
    UI_STR_ACH_GATE,
    UI_STR_ACH_GATE_DESC,
    UI_STR_ACH_WINDMILL,
    UI_STR_ACH_WINDMILL_DESC,
    UI_STR_ACH_GRAVE,
    UI_STR_ACH_GRAVE_DESC,
    UI_STR_ACH_WATERFALL,
    UI_STR_ACH_WATERFALL_DESC,
    UI_STR_ACH_GONDOLA,
    UI_STR_ACH_GONDOLA_DESC,
    UI_STR_ACH_WATERTOWER,
    UI_STR_ACH_WATERTOWER_DESC,
    UI_STR_ACH_CLIFF,
    UI_STR_ACH_CLIFF_DESC,
    UI_STR_ACH_WINGS,
    UI_STR_ACH_WINGS_DESC,
    UI_STR_ACH_QUEEN,
    UI_STR_ACH_QUEEN_DESC,
    UI_STR_ACH_QUEEN_DEFEATED,
    UI_STR_ACH_QUEEN_DEFEATED_DESC,
    UI_STR_ACH_BEACH,
    UI_STR_ACH_BEACH_DESC,
    UI_STR_ACH_CLEAR,
    UI_STR_ACH_CLEAR_DESC,
    UI_STR_ACH_CLEAR2,
    UI_STR_ACH_CLEAR2_DESC,
    UI_STR_ACH_FIRST_KILL,
    UI_STR_ACH_FIRST_KILL_DESC,
    UI_STR_ACH_KILLS_25,
    UI_STR_ACH_KILLS_25_DESC,
    UI_STR_ACH_KILLS_100,
    UI_STR_ACH_KILLS_100_DESC,
    UI_STR_ACH_RESCUE,
    UI_STR_ACH_RESCUE_DESC,
    UI_STR_ACH_HAND_10,
    UI_STR_ACH_HAND_10_DESC,
    UI_STR_ACH_HAND_60,
    UI_STR_ACH_HAND_60_DESC,
    UI_STR_ACH_FIRST_SAVE,
    UI_STR_ACH_FIRST_SAVE_DESC,
    UI_STR_ACH_SOFAS_5,
    UI_STR_ACH_SOFAS_5_DESC,
    UI_STR_ACH_SWORD,
    UI_STR_ACH_SWORD_DESC,
    UI_STR_ACH_QUEEN_SWORD,
    UI_STR_ACH_QUEEN_SWORD_DESC,
    UI_STR_ACH_LIGHT_BLADE,
    UI_STR_ACH_LIGHT_BLADE_DESC,
    UI_STR_ACH_NO_CAPTURE,
    UI_STR_ACH_NO_CAPTURE_DESC,
    UI_STR_ACH_NO_GAMEOVER,
    UI_STR_ACH_NO_GAMEOVER_DESC,
    UI_STR_ACH_FAST,
    UI_STR_ACH_FAST_DESC,
    UI_STR_ACH_SECRET,
    UI_STR_ACH_SECRET_DESC,
    /* the Settings menu */
    UI_STR_SECTION_AUDIO,
    UI_STR_SECTION_ACHIEVEMENTS,
    UI_STR_OPT_VOLUME,
    UI_STR_OPT_MOUSE_SENS,
    UI_STR_OPT_VIDEO_MODE,
    UI_STR_VAL_PAL50,
    UI_STR_VAL_60HZ,
    UI_STR_OPT_FRAMERATE,
    UI_STR_VAL_WINDOW,
    UI_STR_VAL_AUTO,
    UI_STR_VAL_TRILINEAR,
    UI_STR_VAL_ANISOTROPIC,
    UI_STR_LANGUAGE_NOTE,
    UI_STR_OPT_MENU_LANGUAGE,  /* the Menu language row ([ui] language) */
    UI_STR_MENU_LANGUAGE_NOTE, /* its explanation */
    UI_STR_OPT_DISC,           /* the Disc row ([game] disc), with two or more discs' game data */
    UI_STR_DISC_NOTE,          /* its explanation */
    UI_STR_DISC_PAL,           /* its values, in config/regions.txt's order */
    UI_STR_DISC_JPN,
    UI_STR_DISC_KOR,
    UI_STR_DISC_NTSC,
    UI_STR_DISC_AUG6,
    UI_STR_DISC_JUN26,
    UI_STR_DISC_CN,
    UI_STR_REMAP_KEYBOARD,
    UI_STR_REMAP_GAMEPAD,
    UI_STR_REMAP_HINT,
    UI_STR_REMAP_HINT_CIRCLE, /* the same where Circle confirms (the Japanese disc) */
    UI_STR_REMAP_MENU_NOTE,   /* Remap controls: the menus keep the gamepad's buttons by position */
    UI_STR_REMAP_PRESS,
    UI_STR_REMAP_RESET,
    /* Settings > Controls, the touch overlay's rows (shown
       with a touch screen), their values and the note */
    UI_STR_OPT_TOUCH_MODE,
    UI_STR_OPT_TOUCH_SIZE,
    UI_STR_OPT_TOUCH_OPACITY,
    UI_STR_VAL_ALWAYS,
    UI_STR_VAL_SMALL,
    UI_STR_VAL_MEDIUM,
    UI_STR_VAL_LARGE,
    UI_STR_TOUCH_NOTE,
    UI_STR_BTN_CROSS,
    UI_STR_BTN_CIRCLE,
    UI_STR_BTN_SQUARE,
    UI_STR_BTN_TRIANGLE,
    UI_STR_BTN_START,
    UI_STR_BTN_SELECT,
    UI_STR_BTN_DPAD,
    UI_STR_STICK_LEFT,
    UI_STR_STICK_RIGHT,
    UI_STR_DIR_UP,
    UI_STR_DIR_DOWN,
    UI_STR_DIR_LEFT,
    UI_STR_DIR_RIGHT,
    /* gamepad sources on the remap screen, in ICO_GP_* order from SOUTH */
    UI_STR_PAD_SOUTH,
    UI_STR_PAD_EAST,
    UI_STR_PAD_WEST,
    UI_STR_PAD_NORTH,
    UI_STR_PAD_BACK,
    UI_STR_PAD_START,
    UI_STR_PAD_L3,
    UI_STR_PAD_R3,
    UI_STR_PAD_L1,
    UI_STR_PAD_R1,
    UI_STR_PAD_DPAD_UP,
    UI_STR_PAD_DPAD_DOWN,
    UI_STR_PAD_DPAD_LEFT,
    UI_STR_PAD_DPAD_RIGHT,
    UI_STR_PAD_L2,
    UI_STR_PAD_R2,
    UI_STR_PAD_LSTICK_LEFT,
    UI_STR_PAD_LSTICK_RIGHT,
    UI_STR_PAD_LSTICK_UP,
    UI_STR_PAD_LSTICK_DOWN,
    UI_STR_PAD_RSTICK_LEFT,
    UI_STR_PAD_RSTICK_RIGHT,
    UI_STR_PAD_RSTICK_UP,
    UI_STR_PAD_RSTICK_DOWN,
    UI_STR_VAL_UNCAPPED,
    UI_STR_FPS_UNIT,
    UI_STR_VIDEO_MODE_TITLE_ONLY,
    UI_STR_ACH_LOCKED,
    UI_STR_ACH_STATE_UNLOCKED,
    /* mirror mode */
    UI_STR_MIRROR_SCREEN, /* the New Game screen's explanation of Mirror mode */
    /* New Game+: the New Game screen's second row and its explanation */
    UI_STR_OPT_NEWGAME_PLUS,
    UI_STR_NEWGAME_PLUS_SCREEN,
    /* Settings > Display, "CRT filter" and "CRT strength" */
    UI_STR_OPT_CRT,
    UI_STR_OPT_CRT_STRENGTH,
    UI_STR_VAL_CRT_SCANLINES,
    UI_STR_VAL_CRT_CONSUMER,
    UI_STR_VAL_CRT_TRINITRON,
    UI_STR_VAL_CRT_PVM,
    UI_STR_VAL_CRT_SHADOW,
    UI_STR_VAL_CUSTOM,
    UI_STR_RESOLUTION_CRT_NOTE,
    /* the title's "Quit to desktop" row and its confirmation, and
       Settings > Controls, "Circle goes back" ([game] circle_back) */
    UI_STR_QUIT_DESKTOP,
    UI_STR_QUIT_CONFIRM,
    UI_STR_OPT_CIRCLE_BACK,
    UI_STR_CIRCLE_BACK_NOTE,
    /* the game's menu text, transcribed from the PAL sheets
       (text/menu_PAL_xx, scei.tm2, title.tm2) with their wording and
       capitalisation; menu_text.c maps the texProperty rows to them, and
       the rows are drawn as these words in the sheets' look (menu_font.h) */
    UI_STR_MT_LANG_ENGLISH,
    UI_STR_MT_LANG_FRANCAIS,
    UI_STR_MT_LANG_DEUTSCH,
    UI_STR_MT_LANG_ITALIANO,
    UI_STR_MT_LANG_ESPANOL,
    UI_STR_MT_HZ50,
    UI_STR_MT_HZ60,
    UI_STR_MT_NO_CARD,
    UI_STR_MT_NEED_START,
    UI_STR_MT_NO_SPACE_START,
    UI_STR_MT_YES,
    UI_STR_MT_NO,
    UI_STR_MT_VIBRATION,
    UI_STR_MT_ACTIVATE,
    UI_STR_MT_DEACTIVATE,
    UI_STR_MT_CONTINUE,
    UI_STR_MT_NEW_GAME,
    UI_STR_MT_DO_NOT_REMOVE,
    UI_STR_MT_ACCESSING,
    UI_STR_MT_LOC_OLD_BRIDGE,
    UI_STR_MT_LOC_TROLLEY_A,
    UI_STR_MT_LOC_MAIN_GATE,
    UI_STR_MT_LOC_GRAVEYARD,
    UI_STR_MT_LOC_WINDMILL,
    UI_STR_MT_LOC_STONE_PILLAR,
    UI_STR_MT_LOC_EAST_ARENA,
    UI_STR_MT_LOC_EAST_REFLECTOR,
    UI_STR_MT_LOC_WATERFALL,
    UI_STR_MT_LOC_GONDOLA,
    UI_STR_MT_LOC_WATER_TOWER,
    UI_STR_MT_LOC_WEST_IDOL_STAIRS,
    UI_STR_MT_LOC_TROLLEY_B,
    UI_STR_MT_LOC_CRANE,
    UI_STR_MT_LOC_SANDY_BEACH,
    UI_STR_MT_SLOT_1,
    UI_STR_MT_SLOT_2,
    UI_STR_MT_OK,
    UI_STR_MT_BACK,
    UI_STR_MT_SELECT_SLOT,
    UI_STR_MT_SELECT_LOAD,
    UI_STR_MT_NO_SAVE_DATA,
    UI_STR_MT_LOADING,
    UI_STR_MT_SAVE_Q,
    UI_STR_MT_NO_SPACE,
    UI_STR_MT_NEED_360,
    UI_STR_MT_SELECT_SAVE,
    UI_STR_MT_OVERWRITE,
    UI_STR_MT_NOT_FORMATTED,
    UI_STR_MT_FORMATTING,
    UI_STR_MT_SAVING,
    UI_STR_MT_FILE_SAVED,
    UI_STR_MT_RESUME_GAME,
    UI_STR_MT_END_GAME,
    UI_STR_MT_GAME_WILL_END,
    UI_STR_MT_SAVE_FAILED,
    UI_STR_MT_FORMAT_FAILED,
    UI_STR_MT_LOAD_FAILED,
    UI_STR_MT_NO_ACCESS,
    UI_STR_MT_OPTIONS,
    UI_STR_MT_PAUSE_BACK,
    UI_STR_MT_FILM_EFFECT,
    UI_STR_MT_SOUND,
    UI_STR_MT_STEREO,
    UI_STR_MT_MONO,
    UI_STR_MT_HOLD_TYPE,
    UI_STR_MT_BUTTON_CONFIG,
    UI_STR_MT_BRIGHTNESS,
    UI_STR_MT_PLAYERS,
    UI_STR_MT_JUMP,
    UI_STR_MT_ATTACK,
    UI_STR_MT_ACTION,
    UI_STR_MT_RELEASE,
    UI_STR_MT_HOLD_HAND_CALL,
    UI_STR_MT_ZOOM,
    UI_STR_MT_DEFAULT,
    UI_STR_MT_ADJUST_HINT,
    UI_STR_MT_DARK,
    UI_STR_MT_LIGHT,
    UI_STR_MT_CONTINUE_Q,
    /* the digit and letter tiles: the save screens' slot numbers and play
       time, the Options values (the same glyphs on the five sheets) */
    UI_STR_MT_DIGIT_0,
    UI_STR_MT_DIGIT_1,
    UI_STR_MT_DIGIT_2,
    UI_STR_MT_DIGIT_3,
    UI_STR_MT_DIGIT_4,
    UI_STR_MT_DIGIT_5,
    UI_STR_MT_DIGIT_6,
    UI_STR_MT_DIGIT_7,
    UI_STR_MT_DIGIT_8,
    UI_STR_MT_DIGIT_9,
    UI_STR_MT_DIGIT_10,
    UI_STR_MT_COLON,
    UI_STR_MT_VAL_A,
    UI_STR_MT_VAL_B,
    /* the Options screen's player count tiles (the European sheets' 1 and
       2, the Japanese disc's ひとり and ふたり) */
    UI_STR_MT_PLAYERS_1,
    UI_STR_MT_PLAYERS_2,
    UI_STR_EXTRAS,
    UI_STR_EXTRAS_MUSIC,
    UI_STR_EXTRAS_MODELS,
    UI_STR_EXTRAS_CREDITS,
    UI_STR_EXTRAS_LOCKED_NOTE,
    /* Settings > Audio; Stereo and Mono are the
       game's own words (UI_STR_MT_STEREO, UI_STR_MT_MONO) */
    UI_STR_OPT_MUSIC_VOL,
    UI_STR_OPT_EFFECTS_VOL,
    UI_STR_OPT_OUTPUT,
    UI_STR_VAL_STEREO,
    UI_STR_VAL_MONO,
    UI_STR_OPT_DEVICE,
    UI_STR_VAL_DEFAULT_DEVICE,
    UI_STR_VAL_DEFAULT, /* a value at its default (Settings > Brightness) */
    /* Settings > Extras > Music, the music gallery */
    UI_STR_GAL_SOUNDTRACK,
    UI_STR_GAL_SCENE,
    UI_STR_GAL_AMBIENCE,
    UI_STR_GAL_VOICE,
    UI_STR_GAL_SE,
    UI_STR_GAL_PLAYING,
    UI_STR_GAL_STOPPED,
    UI_STR_GAL_EMPTY,
    UI_STR_GAL_PAUSED,
    /* its transport's words, beside the game's button glyphs (ui_hint.h) */
    UI_STR_HINT_PLAY,
    UI_STR_HINT_PAUSE,
    UI_STR_HINT_STOP,
    UI_STR_HINT_PREV,
    UI_STR_HINT_NEXT,
    UI_STR_HINT_SECTION,
    /* Extras > Models: the viewer's words,
       then the model names (port/game/model_viewer_table.c) */
    UI_STR_MV_ANIMATION,
    UI_STR_MV_LOOP,
    UI_STR_MV_FRAME,
    UI_STR_MV_NO_ANIMATIONS,
    /* its prompts' words (ui_hint.h; Loop, Animation, Models and Back are
       the strings above and UI_STR_EXTRAS_MODELS, UI_STR_BACK) */
    UI_STR_MV_HINT_VIEW,
    UI_STR_MV_HINT_TITLE,
    UI_STR_MV_HINT_PLAY,
    UI_STR_MV_HINT_TURN,
    UI_STR_MV_HINT_ZOOM,
    UI_STR_MV_HINT_MOVE,
    UI_STR_MV_HINT_SAVE,
    UI_STR_MV_SAVED_FMT,
    UI_STR_MV_SAVED_NONE,
    UI_STR_MV_ICO,
    UI_STR_MV_YORDA,
    UI_STR_MV_QUEEN,
    UI_STR_MV_SHADOW,
    UI_STR_MV_SHADOW_WINGED,
    UI_STR_MV_SHADOW_HORNED,
    UI_STR_MV_SHADOW_HORNED_WINGED,
    UI_STR_MV_SHADOW_BULL,
    UI_STR_MV_SHADOW_BULL_WINGED,
    UI_STR_MV_SHADOW_LARVA,
    UI_STR_MV_SHADOW_BUTTERFLY,
    UI_STR_MV_SHADOW_SLUG,
    UI_STR_MV_SHADOW_BONES,
    UI_STR_MV_BIRD,
    UI_STR_MV_GUARD,
    UI_STR_MV_CAGE,
    UI_STR_MV_BOMB,
    UI_STR_MV_POT,
    UI_STR_MV_BARREL,
    UI_STR_MV_LEVER,
    UI_STR_MV_SWORD,
    UI_STR_MV_MAGIC_SWORD,
    UI_STR_MV_COUCH,
    /* the pause menu's Photo mode: the row, the HUD's lines, the
       capture's popup; FOV takes the vertical field of view in degrees (%d) */
    UI_STR_PHOTO_MODE,
    UI_STR_PHOTO_FOV,
    UI_STR_PHOTO_SAVED,
    UI_STR_PHOTO_FAILED,
    /* the HUD title's camera and speed
       (SPEED takes the speed's word, %s) */
    UI_STR_PHOTO_CAM_FREE,
    UI_STR_PHOTO_CAM_ORBIT,
    UI_STR_PHOTO_SPEED,
    UI_STR_PHOTO_SPEED_SLOW,
    UI_STR_PHOTO_SPEED_NORMAL,
    UI_STR_PHOTO_SPEED_FAST,
    /* the game's Options screen's settings on the Settings
       pages (the labels in the game's own words; Vibration is
       UI_STR_OPT_VIBRATION above), Hold type's values and the notes */
    UI_STR_OPT_BRIGHTNESS,
    UI_STR_OPT_HOLD_TYPE,
    UI_STR_OPT_BUTTON_CONFIG,
    UI_STR_OPT_FILM_EFFECT,
    UI_STR_OPT_PLAYERS,
    UI_STR_OPT_ACH_POPUPS, /* Achievements page: the achievement pop-ups, On/Off */
    UI_STR_VAL_HOLD_A,
    UI_STR_VAL_HOLD_B,
    UI_STR_VAL_HOLD_DISC_A, /* Hold type: the disc's own, A on this disc */
    UI_STR_VAL_HOLD_DISC_B, /* the disc's own, B */
    UI_STR_HOLD_TYPE_NOTE,
    UI_STR_STICK_FIX_ALSO_NOTE,  /* Controls: this row is also under Gameplay */
    UI_STR_ACH_POPUPS_ALSO_NOTE, /* Extras: this row is also under Achievements */
    UI_STR_PLAYERS_NOTE,
    UI_STR_BUTTON_CONFIG_NOTE,
    /* texture packs: Settings > Display, "Texture pack" (On/Off, or "None
       installed" without a pack) and its note; the developer row "Dump
       textures" and its note */
    UI_STR_OPT_TEXTURE_PACK,
    UI_STR_VAL_NONE_INSTALLED,
    UI_STR_TEXTURE_PACK_NOTE,
    UI_STR_OPT_DUMP_TEXTURES,
    UI_STR_DUMP_TEXTURES_NOTE,
    /* model packs: Settings > Display, "Model pack" (On/Off, or "None
       installed" without a pack; the title's Options only) and its note;
       the developer row "Dump models" and its note */
    UI_STR_OPT_MODEL_PACK,
    UI_STR_MODEL_PACK_NOTE,
    UI_STR_OPT_DUMP_MODELS,
    UI_STR_DUMP_MODELS_NOTE,
    /* the Main row "Save a picture for a bug report" (settings.h
       ui_settings_set_frame_dump_handler), its note, and the popup its
       press shows */
    UI_STR_OPT_REPORT_PICTURE,
    UI_STR_REPORT_PICTURE_NOTE,
    UI_STR_REPORT_PICTURE_ARMED,
    UI_STR_REPORT_PICTURE_ARMED_BODY,
#if ICO_RC_TOOLS
    /* the rc builds' Main row "Share a bug report" (settings.h
       ui_settings_set_share_report_handler) and its note; not in a release */
    UI_STR_OPT_SHARE_REPORT,
    UI_STR_SHARE_REPORT_NOTE,
#endif
    /* Options > Effects: the page, its five switches, its note */
    UI_STR_SECTION_EFFECTS,
    /* Options > Picture and Options > Developer: the pages' names (Main's links) */
    UI_STR_SECTION_PICTURE,
    UI_STR_SECTION_DEVELOPER,
    UI_STR_OPT_EFFECT_GLOW,
    UI_STR_OPT_EFFECT_DEPTH_OF_FIELD,
    UI_STR_OPT_EFFECT_SOFTENING,
    UI_STR_OPT_EFFECT_MOTION_BLUR,
    UI_STR_OPT_EFFECT_FOG,
    UI_STR_EFFECTS_NOTE,
    /* the pause menu's journey lines (settings.c pauseStats): the labels;
       New Game+, Mirror mode, Achievements and the assists' names are the
       options' own words, the values the port's figures and On / Off */
    UI_STR_STATS_PLAY_TIME,
    UI_STR_STATS_DEATHS,
    UI_STR_STATS_CAPTURES,
    UI_STR_STATS_SAVES,
    UI_STR_STATS_ENEMIES,
    UI_STR_STATS_ASSISTS,
    UI_STR_STATS_AREA,
    /* the photo panel's action words, each beside its button
       picture or key (photo_ui.c) */
    UI_STR_PHOTO_ACT_MOVE,
    UI_STR_PHOTO_ACT_LOOK,
    UI_STR_PHOTO_ACT_RISE,
    UI_STR_PHOTO_ACT_CIRCLE,
    UI_STR_PHOTO_ACT_NEARFAR,
    UI_STR_PHOTO_ACT_ZOOM,
    UI_STR_PHOTO_ACT_ROLL,
    UI_STR_PHOTO_ACT_SWITCH,
    UI_STR_PHOTO_ACT_SPEED,
    UI_STR_PHOTO_ACT_RESET,
    UI_STR_PHOTO_ACT_SAVE,
    UI_STR_PHOTO_ACT_HIDE,
    UI_STR_PHOTO_ACT_BACK,
    UI_STR_PHOTO_ACT_FILM, /* the Film effect button, then the popup's title is UI_STR_OPT_FILM_EFFECT */
    /* the popup's value for photo mode's Film effect 5 (photo_mode.h
       ICO_PHOTO_FILM_DEJA): the deja vision's grain veil */
    UI_STR_PHOTO_FILM_DEJA,
    /* the word before a mouse button's name on a key cap ("Mouse left") */
    UI_STR_PHOTO_MOUSE_PREFIX,
    /* the D-pad's Up and Down named on the photo panel (the game's button
       pictures have no up or down arrow) */
    UI_STR_PHOTO_UP,
    UI_STR_PHOTO_DOWN,
    UI_STR_PHOTO_RIGHT, /* and Right, which cycles the Film effect */
    /* Options > Extras > Characters (settings.c, port/game/
       appearance.h): the page, its nine colour rows, Randomize and Reset,
       a skin tone's value ("%d" is the tone's number), the page's note
       (from the title, from the pause menu, with a texture pack on) and
       the 24 colours in the palette's order (red .. black) */
    UI_STR_SECTION_CHARACTERS,
    UI_STR_CHAR_ICO_SKIN,
    UI_STR_CHAR_ICO_PONCHO_NAVY,
    UI_STR_CHAR_ICO_PONCHO_PINK,
    UI_STR_CHAR_ICO_PONCHO_LIGHT,
    UI_STR_CHAR_ICO_PONCHO_DARK,
    UI_STR_CHAR_ICO_TUNIC,
    UI_STR_CHAR_ICO_SHORTS,
    UI_STR_CHAR_YORDA_SKIN,
    UI_STR_CHAR_YORDA_DRESS,
    UI_STR_CHAR_RANDOMIZE,
    UI_STR_CHAR_RESET,
    UI_STR_CHAR_TONE,
    UI_STR_CHAR_NOTE_TITLE,
    UI_STR_CHAR_NOTE_PAUSE,
    UI_STR_CHAR_NOTE_PACK,
    UI_STR_COLOUR_RED,
    UI_STR_COLOUR_CRIMSON,
    UI_STR_COLOUR_ROSE,
    UI_STR_COLOUR_PINK,
    UI_STR_COLOUR_MAGENTA,
    UI_STR_COLOUR_PLUM,
    UI_STR_COLOUR_VIOLET,
    UI_STR_COLOUR_INDIGO,
    UI_STR_COLOUR_NAVY,
    UI_STR_COLOUR_BLUE,
    UI_STR_COLOUR_SKY,
    UI_STR_COLOUR_TEAL,
    UI_STR_COLOUR_CYAN,
    UI_STR_COLOUR_GREEN,
    UI_STR_COLOUR_MOSS,
    UI_STR_COLOUR_OLIVE,
    UI_STR_COLOUR_GOLD,
    UI_STR_COLOUR_ORANGE,
    UI_STR_COLOUR_RUST,
    UI_STR_COLOUR_BROWN,
    UI_STR_COLOUR_SAND,
    UI_STR_COLOUR_WHITE,
    UI_STR_COLOUR_GREY,
    UI_STR_COLOUR_BLACK,
    /* Characters inside the model viewer (title screen): the row
       that loads the other character, and two of its button prompts */
    UI_STR_CHAR_SWITCH_YORDA,
    UI_STR_CHAR_SWITCH_ICO,
    UI_STR_CHAR_HINT_COLOUR,
    UI_STR_CHAR_HINT_CHARACTER,
    /* Display > Window mode (Fullscreen reuses UI_STR_OPT_FULLSCREEN) */
    UI_STR_OPT_WINDOW_MODE,
    UI_STR_VAL_WINDOWED,
    UI_STR_VAL_BORDERLESS,
    /* Effects > Cinematic bars */
    UI_STR_OPT_EFFECT_CINEMATIC_BARS,
    /* Effects > Full pixel */
    UI_STR_OPT_FULL_PIXEL,
    /* Settings > Graphics: the page and its link, the Graphics API row,
       Automatic and the note */
    UI_STR_SECTION_GRAPHICS,
    UI_STR_OPT_GRAPHICS_API,
    UI_STR_VAL_GRAPHICS_AUTOMATIC,
    UI_STR_GRAPHICS_API_NOTE,
    /* Settings > Graphics > Apply: the row and its note, the countdown's
       note (one %d, the seconds left), Keep, Revert, and the popup when the
       renderer chosen did not start (two %s: that renderer, the one kept) */
    UI_STR_GRAPHICS_APPLY,
    UI_STR_GRAPHICS_APPLY_NOTE,
    UI_STR_GRAPHICS_KEEP_ASK,
    UI_STR_GRAPHICS_KEEP,
    UI_STR_GRAPHICS_REVERT,
    UI_STR_GRAPHICS_FAILED,
    /* Settings > Graphics driver (Android), Quit game */
    UI_STR_SECTION_GPU_DRIVER,
    UI_STR_OPT_GPU_DRIVER,
    UI_STR_VAL_GPU_BUILTIN,
    UI_STR_GPU_DRIVER_ADD,
    UI_STR_GPU_DRIVER_REMOVE,
    UI_STR_GPU_DRIVER_NOTE,
    UI_STR_GPU_DRIVER_ADDED,
    UI_STR_GPU_DRIVER_BAD,
    UI_STR_GPU_DRIVER_REMOVED,
    UI_STR_GPU_DRIVER_FAILED,
    UI_STR_GPU_DRIVER_NOSPACE,
    UI_STR_GPU_DRIVER_NO_PICKER,
    UI_STR_GPU_DRIVER_NO_PICKER_APP,
    UI_STR_GPU_DRIVER_UNREADABLE,
    UI_STR_GPU_DRIVER_ADD_FILE,
    UI_STR_GPU_DRIVER_CANCELLED,
    UI_STR_GPU_DRIVER_NOTE_LOST,
    UI_STR_QUIT_GAME,
    UI_STR_QUIT_GAME_CONFIRM,
    /* Controls > Invert mouse up/down (the mouse camera's
       up and down swapped, in play and in photo mode) */
    UI_STR_OPT_MOUSE_INVERT,
    /* Controls > Mouse camera speed, Mouse camera range and Camera swings
       back, and the values Normal, Full and Instant */
    UI_STR_OPT_MOUSE_SPEED,
    UI_STR_OPT_MOUSE_RANGE,
    UI_STR_OPT_MOUSE_RETURN,
    UI_STR_VAL_RANGE_NORMAL,
    UI_STR_VAL_RANGE_FULL,
    UI_STR_VAL_INSTANT,
    /* Gameplay > Doorway fix ([gameplay] door_fix) and its explanation */
    UI_STR_OPT_DOOR_FIX,
    UI_STR_OPT_DOOR_FIX_NOTE,
    /* Gameplay > Natural chains ([gameplay] natural_chains) and its explanation */
    UI_STR_OPT_NATURAL_CHAINS,
    UI_STR_OPT_NATURAL_CHAINS_NOTE,
    /* Gameplay > Skip the opening ([gameplay] skip_opening) and its explanation */
    UI_STR_OPT_SKIP_OPENING,
    UI_STR_OPT_SKIP_OPENING_NOTE,
    /* Display > Anti-aliasing ([video] anti_aliasing), its note and values */
    UI_STR_OPT_AA,
    UI_STR_OPT_AA_NOTE,
    /* Picture > Downsampling ([video] downsample): the row, its value (Off is
       UI_STR_OFF) and its note */
    UI_STR_OPT_DOWNSAMPLE,
    UI_STR_VAL_DOWNSAMPLE_BOX,
    UI_STR_OPT_DOWNSAMPLE_NOTE,
    UI_STR_AA_OFF,
    UI_STR_AA_SSAA2,
    UI_STR_AA_SSAA4,
    UI_STR_AA_MSAA2,
    UI_STR_AA_MSAA4,
    UI_STR_AA_MSAA8,
    UI_STR_AA_UNAVAILABLE, /* appended to a value that is not in force: " (off: not available)" */
    /* Extras > Add to Steam: the row and the popups (title, then body) */
    UI_STR_EXTRAS_STEAM,
    UI_STR_STEAM_ADDED,
    UI_STR_STEAM_ADDED_BODY,
    UI_STR_STEAM_EXISTS,
    UI_STR_STEAM_RUNNING,
    UI_STR_STEAM_RUNNING_BODY,
    UI_STR_STEAM_NOT_FOUND,
    UI_STR_STEAM_NOT_FOUND_BODY,
    UI_STR_STEAM_NO_USER,
    UI_STR_STEAM_NO_USER_BODY,
    UI_STR_STEAM_FAILED,
    UI_STR_STEAM_FAILED_BODY,
    /* Extras > Export textures: the row, its label while running, its note,
       the progress note and the end popups (titles, bodies) */
    UI_STR_EXTRAS_EXPORT_TEXTURES,
    UI_STR_EXPORT_TEXTURES_STOP,
    UI_STR_EXPORT_TEXTURES_NOTE,
    UI_STR_EXPORT_TEXTURES_PROGRESS,
    UI_STR_EXPORT_TEXTURES_DONE,
    UI_STR_EXPORT_TEXTURES_DONE_BODY,
    UI_STR_EXPORT_TEXTURES_STOPPED,
    UI_STR_EXPORT_TEXTURES_STOPPED_BODY,
    UI_STR_EXPORT_TEXTURES_FAILED,
    UI_STR_EXPORT_TEXTURES_FAILED_BODY,
    /* Effects > Water reflections: Original (UI_STR_VAL_ORIGINAL) or Sharp */
    UI_STR_OPT_EFFECT_WATER,
    UI_STR_VAL_SHARP,
    UI_STR_EFFECT_WATER_NOTE,
    /* Effects > Motion blur: Off (UI_STR_OFF), Original (UI_STR_VAL_ORIGINAL)
       or Smooth, and the note under the row */
    UI_STR_VAL_SMOOTH,
    /* Display > Frame rate counter (the value is Off/On) */
    UI_STR_OPT_SHOW_FPS,
    UI_STR_EFFECT_MOTION_BLUR_NOTE,
    /* the Korean language's own name, the same in every table (the Menu
       language row, after UI_STR_LANG_JA) */
    UI_STR_LANG_KO,
    /* Traditional Chinese's, likewise (after UI_STR_LANG_KO) */
    UI_STR_LANG_ZH,
    /* Controls > Touch screen: the link and its page's rows (Touch
       controls, size and opacity are UI_STR_OPT_TOUCH_*), the Grid size
       value ("%d mm", one %d), the Edit layout row's notes */
    UI_STR_SECTION_TOUCH,
    UI_STR_OPT_TOUCH_EDIT,
    UI_STR_OPT_TOUCH_SNAP,
    UI_STR_OPT_TOUCH_GRID,
    UI_STR_VAL_MM,
    UI_STR_TOUCH_RESET,
    UI_STR_TOUCH_RESET_ALL,
    UI_STR_TOUCH_EDIT_NOTE,
    UI_STR_TOUCH_EDIT_LOCKED,
    /* the touch layout editor's words (port/ui/touch_ui.c): the toolbar's
       keys, the Reset key's confirm strip, the word on a hidden control,
       the names of the controls that have no button word (the D-pad is
       UI_STR_BTN_DPAD, the faces UI_STR_BTN_*), the hint line and the two
       notes that replace it */
    UI_STR_TED_DONE,
    UI_STR_TED_UNDO,
    UI_STR_TED_RESET,
    UI_STR_TED_SNAP,
    UI_STR_TED_GRID,
    UI_STR_TED_SIZE,
    UI_STR_TED_OPACITY,
    UI_STR_TED_ALL,
    UI_STR_TED_THIS_SCREEN,
    UI_STR_TED_ALL_SCREENS,
    UI_STR_TED_CANCEL,
    UI_STR_TED_HIDDEN,
    UI_STR_TED_OFF,
    UI_STR_TED_STICK,
    UI_STR_TED_LOOK,
    UI_STR_TED_HINT,
    UI_STR_TED_OVERLAP,
    UI_STR_TED_COVERED,
    /* Android's first start, the copy of the chosen disc image into the app
       (port/platform/android/iso_import.h, main_host.c): the box when the
       space is short (printf: the GB needed as two numbers, the whole and
       the tenth, the folder, the GB free as two numbers), the box when the
       copy failed (the folder, the GB needed as two numbers), and the
       progress line, its second form for an image over
       ICO_ISO_LONG_COPY_BYTES (the 4.4 GB magazine demo) */
    UI_STR_ISO_NEED_SPACE,
    UI_STR_ISO_COPY_FAILED,
    UI_STR_ISO_COPYING,
    UI_STR_ISO_COPYING_LONG,
    /* Extras > Game variants (port/game/variants.h): the page's name (also the
       Extras link), the eight rows with their notes and values (a note ends
       with whether the row switches achievements off) and the Debug menu
       row (shown with Developer mode); Original is UI_STR_VAL_ORIGINAL */
    UI_STR_SECTION_VARIANTS,
    UI_STR_OPT_VAR_TOUGHNESS,
    UI_STR_OPT_VAR_TOUGHNESS_NOTE,
    UI_STR_VAL_VAR_AMERICAN,
    UI_STR_VAL_VAR_NOT_TOUGHER,
    UI_STR_OPT_VAR_ONE_HIT,
    UI_STR_OPT_VAR_ONE_HIT_NOTE,
    UI_STR_OPT_VAR_NEVER_DODGE,
    UI_STR_OPT_VAR_NEVER_DODGE_NOTE,
    UI_STR_OPT_VAR_AGGRESSION,
    UI_STR_VAL_VAR_AGG_OWN,
    UI_STR_VAL_VAR_AGG_BOLD_PATIENT,
    UI_STR_VAL_VAR_AGG_BOLD_QUICK,
    UI_STR_VAL_VAR_AGG_WARY_PATIENT,
    UI_STR_VAL_VAR_AGG_WARY_QUICK,
    UI_STR_OPT_VAR_AGGRESSION_NOTE,
    UI_STR_OPT_VAR_NO_HINTS,
    UI_STR_OPT_VAR_NO_HINTS_NOTE,
    UI_STR_OPT_VAR_BARRIER,
    UI_STR_VAL_VAR_HITS_5,
    UI_STR_VAL_VAR_HITS_7,
    UI_STR_OPT_VAR_BARRIER_NOTE,
    UI_STR_OPT_VAR_NO_LOOK,
    UI_STR_OPT_VAR_NO_LOOK_NOTE,
    UI_STR_OPT_VAR_IGNORE_CAMS,
    UI_STR_OPT_VAR_IGNORE_CAMS_NOTE,
    UI_STR_OPT_VAR_DEBUG_MENU,
    UI_STR_OPT_VAR_DEBUG_MENU_NOTE,
    /* PC port: the notes of three rows on the 2001 discs: Shadow toughness on
       the June 2001 demo (American is not offered: its Shadows are its own),
       and No hints and No cutscene look-around on the three 2001 discs
       (those versions have neither, so the rows change nothing there) */
    UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO,
    UI_STR_OPT_VAR_NO_HINTS_NOTE_2001,
    UI_STR_OPT_VAR_NO_LOOK_NOTE_2001,
    UI_STR_COUNT
} UiStrId;

void ui_set_language(UiLang lang);
UiLang ui_get_language(void);
/* The sheet language (UI_LANG_EN .. UI_LANG_ES, UI_SHEET_JA, UI_SHEET_KO or UI_SHEET_ZH): the look
   of the port's text in the sheets' style (menu_font.c's ink per sheet
   language) and the menu text table's geometry follow it, whatever
   language the port's own text is in.  Until ui_set_sheet_language is
   called it follows ui_set_language (the port's language when that is one
   of the European disc's, else English); a value outside the sheet
   languages is ignored. */
void ui_set_sheet_language(int sheetLang);
int ui_sheet_language(void);
/* the game's language number (2..6) to a UiLang; English otherwise */
UiLang ui_lang_from_game(int nonLinearCameraMove);
/* the string of id in the current language; "" for an unknown id; a
   missing translation falls back to English */
const char *ui_str(UiStrId id);
/* the same in a given language (tests, the Language section) */
const char *ui_str_in(UiLang lang, UiStrId id);

/* Line breaking for the Japanese text, which has no spaces: whether cp is
   a wide (CJK, fullwidth) character, and whether a line may break between
   prev and cp without a space (a wide character on either side, not before
   closing punctuation, the prolonged sound mark, small kana or an iteration
   mark, not after an opening bracket: the usual line-start and line-end
   rules).  Latin text breaks at its spaces only, as always: two Latin
   characters never give a break.  settings.c's notes and the popups
   (popup.c) break with these. */
int ui_wide_char(uint32_t cp);
int ui_break_before(uint32_t prev, uint32_t cp);
/* Korean text is written with spaces between its words and breaks at them,
   as Latin text does: Hangul is not a wide character (ui_wide_char, so
   ui_break_before gives no break between two Hangul characters), though
   it is as wide as one on the screen.  ui_hangul_char says whether cp is Hangul (the jamo, the
   compatibility and extended jamo, the syllables); the wrappers break
   between two Hangul characters only when one word alone is wider than
   the line (settings.c wrapText, popup.c wrapWide), and count a Hangul
   character two columns as a wide one (achievements.c). */
int ui_hangul_char(uint32_t cp);

/* The strings that name the running disc's pack folder (its PCSX2 serial,
   port/core/region.h IcoRegionInfo.pack_serial: SCES-50760, SCPS-11003):
   the texture and model pack notes and the two dump notes.  Each table's
   text for them is a printf format with one %s, the serial, and no other
   conversion (ctest strings_complete checks it); ui_str_takes_serial says
   which ids these are, and ui_str_serial writes id's text in the current
   language with the selected region's serial into buf (n bytes, cut at a
   character's end), any other id's text unchanged; it returns buf. */
int ui_str_takes_serial(UiStrId id);
const char *ui_str_serial(UiStrId id, char *buf, size_t n);

/* Every string the port draws from a table, for the font coverage test:
   fn(lang, utf8, user) once for each non-empty entry of each language's
   table (the Settings menu, notes, Extras, achievements, popups, the menu
   words UI_STR_MT_*).  A missing translation is not visited (ui_str_in
   would give the English one, which is visited under UI_LANG_EN).  The
   subtitles and the staff roll's lines are the game's pictures and data,
   not port tables. */
typedef void (*UiStringFn)(UiLang lang, const char *utf8, void *user);
void ui_strings_for_each(UiStringFn fn, void *user);

/* the tables, one per strings_<lang>.c, UI_STR_COUNT entries */
extern const char *const ui_strings_en[UI_STR_COUNT];
extern const char *const ui_strings_fr[UI_STR_COUNT];
extern const char *const ui_strings_de[UI_STR_COUNT];
extern const char *const ui_strings_it[UI_STR_COUNT];
extern const char *const ui_strings_es[UI_STR_COUNT];
extern const char *const ui_strings_ja[UI_STR_COUNT];
extern const char *const ui_strings_ko[UI_STR_COUNT];
extern const char *const ui_strings_zh[UI_STR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_STRINGS_H */
