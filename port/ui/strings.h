/*
 * port/ui/strings.h
 *
 * The port's own UI strings in the game's five languages (Phase 6, 6B).  One table per language,
 * port/ui/strings_<lang>.c, indexed by UiStrId; UTF-8.  Only what the
 * Settings menu (6C) and the popups need: the plan's Settings sections
 * (Display, Controls, Gameplay, Language, Developer mode), the gameplay
 * options (stick_fix, yorda_safe, mirror) and the rd
 * settings; 6C renames or adds ids as it needs.
 */
#ifndef PORT_UI_STRINGS_H
#define PORT_UI_STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The game's language numbers, as NonLinearCameraMove holds them after the
   boot screen (kanbanBoot.c 101-102) and texFile names them (2 EG, 3 FR,
   4 GR, 5 IT, 6 SP); UI_LANG_COUNT tables. */
typedef enum UiLang {
    UI_LANG_EN = 0,
    UI_LANG_FR,
    UI_LANG_DE,
    UI_LANG_IT,
    UI_LANG_ES,
    UI_LANG_COUNT
} UiLang;

typedef enum UiStrId {
    UI_STR_NONE = 0, /* "" */
    /* the menu and its sections */
    UI_STR_SETTINGS,
    UI_STR_SECTION_DISPLAY,
    UI_STR_SECTION_CONTROLS,
    UI_STR_SECTION_GAMEPLAY,
    UI_STR_SECTION_LANGUAGE,
    UI_STR_SECTION_DEVELOPER,
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
    UI_STR_OPT_INTERPOLATION,
    UI_STR_OPT_FILTERING,
    UI_STR_OPT_FULL_HEIGHT,
    UI_STR_OPT_MIRROR,
    /* Controls */
    UI_STR_OPT_REMAP,
    UI_STR_OPT_MOUSE_CAMERA,
    UI_STR_OPT_INVERT_X,
    UI_STR_OPT_INVERT_Y,
    UI_STR_OPT_VIBRATION,
    /* Gameplay */
    UI_STR_OPT_STICK_FIX,
    UI_STR_OPT_YORDA,      /* [gameplay] yorda_safe */
    UI_STR_OPT_YORDA_NOTE, /* its explanation */
    /* Language: each language's own name, the same in every table */
    UI_STR_LANG_EN,
    UI_STR_LANG_FR,
    UI_STR_LANG_DE,
    UI_STR_LANG_IT,
    UI_STR_LANG_ES,
    UI_STR_OPT_SKIP_BOOT,
    /* Developer mode */
    UI_STR_OPT_DEVELOPER_MODE,
    UI_STR_DEVELOPER_NOTE,
    /* popups */
    UI_STR_POPUP_TEST_TITLE,
    UI_STR_POPUP_TEST_BODY,
    UI_STR_ACHIEVEMENT_UNLOCKED,
    /* achievements (package 6E): title and
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
    /* the Settings menu (6C) */
    UI_STR_SECTION_AUDIO,
    UI_STR_SECTION_ACHIEVEMENTS,
    UI_STR_OPT_VOLUME,
    UI_STR_OPT_MOUSE_SENS,
    UI_STR_OPT_VIDEO_MODE,
    UI_STR_VAL_PAL50,
    UI_STR_VAL_60HZ,
    UI_STR_VIDEO_MODE_NOTE,
    UI_STR_OPT_FRAMERATE,
    UI_STR_VAL_WINDOW,
    UI_STR_VAL_AUTO,
    UI_STR_VAL_TRILINEAR,
    UI_STR_VAL_ANISOTROPIC,
    UI_STR_LANGUAGE_NOTE,
    UI_STR_REMAP_KEYBOARD,
    UI_STR_REMAP_GAMEPAD,
    UI_STR_REMAP_HINT,
    UI_STR_REMAP_PRESS,
    UI_STR_REMAP_RESET,
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
    /* mirror mode (renderer wave 7, R7c) */
    UI_STR_MIRROR_SCREEN, /* the New Game screen's explanation of Mirror mode */
    /* New Game+: the New Game screen's second row and its explanation */
    UI_STR_OPT_NEWGAME_PLUS,
    UI_STR_NEWGAME_PLUS_SCREEN,
    /* package CRT: Settings > Display, "CRT filter" and "CRT strength" */
    UI_STR_OPT_CRT,
    UI_STR_OPT_CRT_STRENGTH,
    UI_STR_VAL_CRT_SCANLINES,
    UI_STR_VAL_CRT_CONSUMER,
    UI_STR_VAL_CRT_TRINITRON,
    UI_STR_VAL_CRT_PVM,
    UI_STR_VAL_CRT_SHADOW, /* package CRT2 */
    UI_STR_VAL_CUSTOM,
    UI_STR_RESOLUTION_CRT_NOTE,
    /* Q2: the title's "Quit to desktop" row and its confirmation, and
       Settings > Controls, "Circle goes back" ([game] circle_back) */
    UI_STR_QUIT_DESKTOP,
    UI_STR_QUIT_CONFIRM,
    UI_STR_OPT_CIRCLE_BACK,
    UI_STR_CIRCLE_BACK_NOTE,
    /* P3: the game's menu text, transcribed from the PAL sheets
       (text/menu_PAL_xx, scei.tm2, title.tm2) with their wording and
       capitalisation; menu_text.c maps the texProperty rows to them.  Never
       drawn (the game's rows draw their textures): the game face's builder
       cuts its letters from the sheets by matching these words */
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
    /* Extras > Models (package MV): the viewer's words,
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
    /* the pause menu's Photo mode (package PHOTO; S1 moved the row from
       the Options screen to the pause menu): the row, the HUD's lines, the
       capture's popup; FOV takes the vertical field of view in degrees (%d) */
    UI_STR_PHOTO_MODE,
    UI_STR_PHOTO_HUD_MOVE,
    UI_STR_PHOTO_HUD_LENS,
    UI_STR_PHOTO_HUD_KEYS,
    UI_STR_PHOTO_FOV,
    UI_STR_PHOTO_SAVED,
    UI_STR_PHOTO_FAILED,
    /* package S1: the game's Options screen's settings on the Settings
       pages (the labels in the game's own words; Vibration is
       UI_STR_OPT_VIBRATION above), Hold type's values and the notes */
    UI_STR_OPT_BRIGHTNESS,
    UI_STR_OPT_HOLD_TYPE,
    UI_STR_OPT_BUTTON_CONFIG,
    UI_STR_OPT_FILM_EFFECT,
    UI_STR_OPT_PLAYERS,
    UI_STR_VAL_HOLD_A,
    UI_STR_VAL_HOLD_B,
    UI_STR_HOLD_TYPE_NOTE,
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
    UI_STR_COUNT
} UiStrId;

void ui_SetLanguage(UiLang lang);
UiLang ui_GetLanguage(void);
/* the game's language number (2..6) to a UiLang; English otherwise */
UiLang ui_LangFromGame(int nonLinearCameraMove);
/* the string of id in the current language; "" for an unknown id; a
   missing translation falls back to English */
const char *ui_Str(UiStrId id);
/* the same in a given language (tests, the Language section) */
const char *ui_StrIn(UiLang lang, UiStrId id);

/* Every string the port draws from a table, for the font coverage test:
   fn(lang, utf8, user) once for each non-empty entry of each language's
   table (the Settings menu, notes, Extras, achievements, popups, the menu
   words UI_STR_MT_*).  A missing translation is not visited (ui_StrIn
   would give the English one, which is visited under UI_LANG_EN).  The
   subtitles and the staff roll's lines are the game's pictures and data,
   not port tables. */
typedef void (*UiStringFn)(UiLang lang, const char *utf8, void *user);
void ui_StringsForEach(UiStringFn fn, void *user);

/* the tables, one per strings_<lang>.c, UI_STR_COUNT entries */
extern const char *const ui_strings_en[UI_STR_COUNT];
extern const char *const ui_strings_fr[UI_STR_COUNT];
extern const char *const ui_strings_de[UI_STR_COUNT];
extern const char *const ui_strings_it[UI_STR_COUNT];
extern const char *const ui_strings_es[UI_STR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_STRINGS_H */
