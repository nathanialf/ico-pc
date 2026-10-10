/*
 * port/ui/strings.h
 *
 * The port's UI strings in the game's five languages: one table per
 * language (port/ui/strings_<lang>.c), indexed by UiStrId, UTF-8. Every
 * word the port draws (the Settings menu, the popups, the achievements, the
 * galleries, photo mode, the model viewer, Characters, the Graphics driver
 * page), and the menus' words transcribed from the PAL sheets
 * (UI_STR_MT_*).
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
    UI_STR_OPT_YORDA,      /* [gameplay] yorda_safe */
    UI_STR_OPT_YORDA_NOTE, /* its explanation */
    /* Language: each language's own name, the same in every table */
    UI_STR_LANG_EN,
    UI_STR_LANG_FR,
    UI_STR_LANG_DE,
    UI_STR_LANG_IT,
    UI_STR_LANG_ES,
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
    UI_STR_REMAP_KEYBOARD,
    UI_STR_REMAP_GAMEPAD,
    UI_STR_REMAP_HINT,
    UI_STR_REMAP_MENU_NOTE, /* Remap controls: the menus keep the gamepad's buttons by position */
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
    /* model packs: Settings > Display, "Model pack" (On/Off, or "None
       installed" without a pack; the title's Options only) and its note;
       the developer row "Dump models" and its note */
    UI_STR_OPT_MODEL_PACK,
    UI_STR_MODEL_PACK_NOTE,
    UI_STR_OPT_DUMP_MODELS,
    UI_STR_DUMP_MODELS_NOTE,
    /* Options > Effects (issue 11): the page, its five switches, its note */
    UI_STR_SECTION_EFFECTS,
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
    /* the word before a mouse button's name on a key cap ("Mouse left") */
    UI_STR_PHOTO_MOUSE_PREFIX,
    /* the D-pad's Up and Down named on the photo panel (the game's button
       pictures have no up or down arrow) */
    UI_STR_PHOTO_UP,
    UI_STR_PHOTO_DOWN,
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
    UI_STR_COUNT
} UiStrId;

void ui_set_language(UiLang lang);
UiLang ui_get_language(void);
/* the game's language number (2..6) to a UiLang; English otherwise */
UiLang ui_lang_from_game(int nonLinearCameraMove);
/* the string of id in the current language; "" for an unknown id; a
   missing translation falls back to English */
const char *ui_str(UiStrId id);
/* the same in a given language (tests, the Language section) */
const char *ui_str_in(UiLang lang, UiStrId id);

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

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_STRINGS_H */
