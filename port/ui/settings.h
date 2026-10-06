/*
 * port/ui/settings.h
 *
 * The port's Settings menu (Phase 6, package 6C).
 *
 * Every screen is a port layout in the layout extension (layout_ext.h),
 * drawn, navigated and faded by the game's own layout code
 * (common/src/layout_texture.c): the cursor moves on the rows' item links,
 * the selected row glows, Cross on a row with a right link opens that
 * layout, and the backdrop, fades and sounds are the game's.  The values are
 * port rows whose text is set with lt_ext_SetText from the screens' procs.
 *
 * Entry: a "Settings" row is chained after the Options screen (layout 58)
 * and after both title menus (12 "Continue / New Game", 13 "New Game"), each
 * in a one-row port layout on the game layout's link chain; the game rows
 * around it are repointed onto it (ui_SettingsInstall):
 *   Options 58:  325.downItem and 300.upItem -> the row; its up 325, down 300
 *                (the visibility skip of layout_texture.c steps over 325
 *                and 300 before the game is cleared, as it did)
 *   Title 12:    50.downItem -> the row; its up 50
 *   Title 13:    51.downItem -> the row; its up 51
 *   link:        58 -> row layout; 12 and 13 -> row layout -> 11 (was 11)
 *
 * Changes apply at once through the existing setters (ico_video_set,
 * ico_opt_set_*, the live binding table, NonLinearCameraMove,
 * systemStatus[0] with gsResetFunc) and are written with ico_video_save /
 * ico_config_save when a screen is left.
 */
#ifndef PORT_UI_SETTINGS_H
#define PORT_UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* init_layout_texture (ICO_HOST): builds the port layouts the first time
   and (re)points the game's rows at the entry rows.  Idempotent. */
void ui_SettingsInstall(void);
/* Whether item is one of the entry rows, the title's "Quit to desktop" rows
   included (the title procs leave Cross and START on them to
   default_item_select). */
int ui_SettingsEntryItem(int item);
/* The title procs: masked while their own rows are (the card check). */
void ui_SettingsTitleMask(int masked);

/* The New Game "Mirror mode" screen (renderer wave 7, R7c): la_vibe_select (common/src/layout_action.c,
   ICO_HOST) switches to it after the vibration choice; Cross or START on
   "Off" or "On" sets the run's mirror mode (ico_opt_set_mirror) and calls
   la_host_new_game_go (gflagOn(382), what the vibration screen did);
   Triangle goes back to the vibration screen (layout 9).  Enter returns the
   screen's layout with the cursor on "Off" and the run's value reset
   (ico_opt_mirror_reset), -1 when the menu is not built (the caller then
   starts the game as the original did). */
int ui_MirrorScreenEnter(void);
int ui_MirrorScreenLayout(void);
/* the "Off" (on = 0) and "On" (on = 1) rows */
int ui_MirrorScreenRow(int on);

/* "Quit to desktop" (package Q2): a port
   row under the title's Settings row (layouts 12 and 13, in the same
   chained entry layout) whose Cross opens a confirmation screen, "Quit to
   desktop?" with Yes and No (the cursor on No).  Cross on Yes writes what
   the Settings menu has not saved yet (ui_SettingsSave) and calls the quit
   handler once; Cross on No, Triangle or Circle return to the title with
   the cursor on the row.  The window build's handler (ui_host.c) posts
   SDL_EVENT_QUIT, so the program ends through the path closing the window
   takes (ico_window_pump returns 0, main returns, the atexit handlers flush
   the achievements, stop the audio and close the window).  Without a
   handler (the headless build) the request calls exit(0), which runs the
   same atexit handlers.  NULL restores that. */
void ui_SettingsSetQuitHandler(void (*fn)(void));
/* Package MV: Settings > Extras > Models opens the layout fn returns
   (port/game/model_viewer.c's model list), or nothing when it returns -1 or
   none is set ("extras: models not available" in the log). */
void ui_SettingsSetModelsHandler(int (*fn)(void));
int ui_QuitScreenLayout(void);
/* the "Yes" (yes = 1) and "No" (yes = 0) rows */
int ui_QuitScreenRow(int yes);
/* the title's "Quit to desktop" row of layout 12 or 13, -1 otherwise */
int ui_SettingsQuitRow(int gameLayout);

/* Circle in the menus (Q2): every port screen (the Settings pages, the
   lists, the mirror and quit screens) takes Circle as Triangle, always;
   the game's own menus do while [game] circle_back is on
   (port/ui/layout_ext.h lt_ext_BackButtons, Settings > Controls). */

/* --- for tests and docs -------------------------------------------------- */

typedef enum UiSettingsPage {
    UI_PAGE_MAIN = 0,
    UI_PAGE_DISPLAY,
    UI_PAGE_AUDIO,
    UI_PAGE_CONTROLS,
    UI_PAGE_GAMEPLAY,
    UI_PAGE_ACHIEVEMENTS,
    UI_PAGE_REMAP,
    UI_PAGE_EXTRAS, /* Music, Models, Credits (from the title only) */
    UI_PAGE_MUSIC,  /* Extras > Music, the music gallery (gallery.h) */
    UI_PAGE_COUNT
} UiSettingsPage;

/* What a row does. */
typedef enum UiSettingsOpt {
    UI_OPT_NONE = 0,
    /* a section: Cross opens it */
    UI_OPT_LINK,
    /* Display */
    UI_OPT_PRESET,
    UI_OPT_RESOLUTION,
    UI_OPT_ASPECT,
    UI_OPT_FULLSCREEN,
    UI_OPT_VSYNC,
    UI_OPT_FILTER,
    UI_OPT_FULL_HEIGHT,
    UI_OPT_FRAMERATE,    /* [video] framerate (R7b; stepped since R7d) */
    UI_OPT_CRT,          /* [video] crt and crt_mode in one row (package CRT) */
    UI_OPT_CRT_STRENGTH, /* [video] crt_strength, 0..100 % in tens */
    UI_OPT_VIDEO_MODE,
    /* Audio */
    UI_OPT_VOLUME,
    UI_OPT_MUSIC,   /* [audio] music (port/audio/mix_gain.h) */
    UI_OPT_EFFECTS, /* [audio] effects */
    UI_OPT_OUTPUT,  /* [audio] output: Auto, Stereo, Mono (port/game/options.h) */
    UI_OPT_DEVICE,  /* [audio] device: Default or a device name (audio_host.h) */
    /* Controls */
    UI_OPT_MOUSE_SENS,
    UI_OPT_CIRCLE_BACK, /* [game] circle_back (Q2) */
    /* Gameplay */
    UI_OPT_STICK_FIX,
    UI_OPT_YORDA,
    /* Main */
    UI_OPT_LANGUAGE,
    UI_OPT_DEVELOPER,
    /* actions */
    UI_OPT_BACK,
    /* list rows (achievements, remap targets) */
    UI_OPT_LIST,
    /* Extras: entries that open a gallery */
    UI_OPT_EXTRAS_MUSIC,
    UI_OPT_EXTRAS_MODELS,
    UI_OPT_EXTRAS_CREDITS /* locked until the ending has been reached */
} UiSettingsOpt;

/* The entry rows and the menu's layouts (-1 before ui_SettingsInstall). */
int ui_SettingsEntryRow(int gameLayout); /* 58, 12 or 13 */
int ui_SettingsEntryLayout(int gameLayout);
/* Package PHOTO: the Options screen's "Photo mode" row (under Settings,
   in the same port layout; masked and stepped over unless a stage runs,
   photo_ui.h ui_PhotoAvailable), -1 before the build. */
int ui_SettingsPhotoRow(void);
int ui_SettingsPageLayout(UiSettingsPage page);
/* A page's navigable rows in order: the label row of each, its option, its
   value row (-1 for none).  Returns the count. */
int ui_SettingsPageRows(UiSettingsPage page, int *labels, int *opts, int *values, int max);
/* The row index of an option on a page, -1 if it has none. */
int ui_SettingsRowOf(UiSettingsPage page, UiSettingsOpt opt);
/* The value text an option shows now. */
const char *ui_SettingsValueText(UiSettingsOpt opt);
/* Steps an option by dir (-1 left, +1 right) through its setter, as a
   left/right press on its row does. */
void ui_SettingsStep(UiSettingsOpt opt, int dir);
/* Writes what changed (ico_video_save, ico_input_write_bindings,
   ico_config_save), as leaving a screen does.  0, or -1. */
int ui_SettingsSave(void);
/* Forgets the layouts (tests: lt_ext_Reset first). */
void ui_SettingsReset(void);

/* The remap screen's capture: Cross on a target row starts it, the next
   press of a key, mouse button or gamepad source (ico_input_last_press) is
   bound to the target (ico_bindings_assign), a timeout cancels it. */
typedef struct UiRemapCapture {
    int active;        /* waiting for a press */
    int target;        /* ICO_T_* */
    unsigned int seq0; /* ico_input_last_press when it started */
    int ticks;         /* Main ticks waited */
    int cooldown;      /* ticks the menu ignores input after a capture */
} UiRemapCapture;

#define UI_REMAP_TIMEOUT_TICKS 250 /* 10 s at the PAL game's 25 ticks a second */
#define UI_REMAP_COOLDOWN_TICKS 3

enum { UI_CAPTURE_IDLE = 0, UI_CAPTURE_WAITING, UI_CAPTURE_BOUND, UI_CAPTURE_TIMEOUT };

void ui_RemapCaptureStart(UiRemapCapture *c, int target);
/* One Main tick: UI_CAPTURE_WAITING, UI_CAPTURE_BOUND (b changed),
   UI_CAPTURE_TIMEOUT, or UI_CAPTURE_IDLE when not active. */
int ui_RemapCaptureStep(UiRemapCapture *c, void *bindings /* IcoBindings */);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_H */
