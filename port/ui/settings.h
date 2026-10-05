/*
 * port/ui/settings.h
 *
 * The port's Settings menu (Phase 6, package 6C; docs/port/SETTINGS.md for
 * the player, docs/port/UI.md "Settings menu" for how it is built).
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
/* Whether item is one of the entry rows (the title procs leave Cross and
   START on it to default_item_select). */
int ui_SettingsEntryItem(int item);
/* The title procs: masked while their own rows are (the card check). */
void ui_SettingsTitleMask(int masked);

/* --- for tests and docs -------------------------------------------------- */

typedef enum UiSettingsPage {
    UI_PAGE_MAIN = 0,
    UI_PAGE_DISPLAY,
    UI_PAGE_AUDIO,
    UI_PAGE_CONTROLS,
    UI_PAGE_GAMEPLAY,
    UI_PAGE_ACHIEVEMENTS,
    UI_PAGE_REMAP,
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
    UI_OPT_FRAMERATE, /* [video] framerate (R7b), shown read-only when present */
    UI_OPT_VIDEO_MODE,
    /* Audio */
    UI_OPT_VOLUME,
    /* Controls */
    UI_OPT_STICK_FIX,
    UI_OPT_MOUSE_SENS,
    /* Gameplay */
    UI_OPT_YORDA,
    UI_OPT_MIRROR_INFO,
    /* Main */
    UI_OPT_LANGUAGE,
    UI_OPT_DEVELOPER,
    /* actions */
    UI_OPT_BACK,
    UI_OPT_REMAP_RESET,
    /* list rows (achievements, remap targets) */
    UI_OPT_LIST
} UiSettingsOpt;

/* The entry rows and the menu's layouts (-1 before ui_SettingsInstall). */
int ui_SettingsEntryRow(int gameLayout); /* 58, 12 or 13 */
int ui_SettingsEntryLayout(int gameLayout);
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
