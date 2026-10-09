/*
 * port/ui/settings.h
 *
 * The port's Settings menu.
 *
 * Every screen is a port layout in the layout extension (layout_ext.h),
 * drawn, navigated and faded by the game's own layout code
 * (common/src/layout_texture.c): the cursor moves on the rows' item links,
 * the selected row glows, Cross on a row with a right link opens that
 * layout, and the backdrop, fades and sounds are the game's.  The values are
 * port rows whose text is set with lt_ext_set_text from the screens' procs.
 *
 * Entry: the pause menu's Options row (294, layout 57) opens the main page
 * (its right link repointed; the game's Options screen, layout 58, is no
 * longer reached, its settings are on the pages), and an "Options" row (the
 * Settings menu's entry) is chained after both title menus (12 "Continue / New
 * Game", 13 "New Game"), each in a one-row port layout on the game layout's
 * link chain; the game rows around it are repointed onto it
 * (ui_settings_install):
 *   Pause 57:    294.right -> the main page; "Photo mode" under Options
 *                (a port row in a port layout chained after 57, Back 295
 *                one pitch lower while it shows)
 *   Title 12:    50.downItem -> the row; its up 50
 *   Title 13:    51.downItem -> the row; its up 51
 *   link:        57 -> photo layout; 12 and 13 -> row layout -> 11 (was 11)
 *
 * Changes apply at once through the existing setters (ico_video_set,
 * ico_opt_set_*, the live binding table, NonLinearCameraMove,
 * systemStatus[0] with gsResetFunc) and are written with ico_video_save /
 * ico_config_save when a screen is left.  The game's own settings
 * (Brightness, Vibration, Hold type, Film effect, Players) are its
 * variables, written by the game's saves as the Options screen left them
 * (fumi/ios/mcard.c), never into the port config; they show only from the
 * pause menu, as the Options screen did (a load sets them from the save).
 */
#ifndef PORT_UI_SETTINGS_H
#define PORT_UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* init_layout_texture (ICO_HOST): builds the port layouts the first time
   and (re)points the game's rows at the entry rows.  Idempotent. */
void ui_settings_install(void);
/* Whether item is one of the entry rows, the title's "Quit to desktop" rows
   included (the title procs leave Cross and START on them to
   default_item_select). */
int ui_settings_entry_item(int item);
/* The title procs: masked while their own rows are (the card check). */
void ui_settings_title_mask(int masked);
/* Leaving Character Customization, the title runs unseen
   until its card check has decided, then Options opens on page
   (ui_settings_reopen_page).  TitleReturn(page) arms it, TitleReturn(-1)
   cancels; while armed every row of the title's layouts is masked and
   confirms are blocked (TitleMask, EntryItem), the logo is hidden
   (CoversTitle).  Decided: the procs' last TitleMask said the check is
   done.  ReopenPage ends the pending state. */
void ui_settings_title_return(int page /* UiSettingsPage or -1 */);
/* whether a TitleReturn is armed (tests) */
int ui_settings_title_return_pending(void);
int ui_settings_title_decided(void);
/* A Settings page opened from the title is the current layout (Extras and
   its Music page, Achievements and the remap list among them); not from
   the pause menu (port/game/title_logo.c hides the title's logo under it). */
int ui_settings_covers_title(void);

/* The New Game screen: la_vibe_select (common/src/layout_action.c)
   switches to it after the vibration choice.  Two rows, Mirror mode (row
   0) and New Game+ (row 1), each "Off" or "On"; Cross or START anywhere
   confirms both: it sets the run's mirror mode (ico_opt_set_mirror) and
   New Game+ (gFlagGameClear), then calls la_host_new_game_go
   (gflagOn(382), what the vibration screen did).  Triangle goes back to
   the vibration screen (layout 9).  Enter returns the screen's layout with
   the cursor on Mirror mode's "Off" and the run's value reset
   (ico_opt_mirror_reset), -1 when the menu is not built (the caller then
   starts the game as the original did). */
int ui_new_game_screen_enter(void);
int ui_new_game_screen_layout(void);
/* row 0 (Mirror mode) or 1 (New Game+): the "Off" (on = 0) and "On"
   (on = 1) items; -1 for a row the screen does not have (tests) */
int ui_new_game_screen_row(int row, int on);

/* "Quit to desktop": a port row under the title's Options row (layouts 12 and 13, in the same
   chained entry layout) whose Cross opens a confirmation screen, "Quit to
   desktop?" with Yes and No (the cursor on No).  Cross on Yes writes what
   the Settings menu has not saved yet (ui_settings_save) and calls the quit
   handler once; Cross on No, Triangle or Circle return to the title with
   the cursor on the row.  The window build's handler (ui_host.c) posts
   SDL_EVENT_QUIT, so the program ends through the path closing the window
   takes (ico_window_pump returns 0, main returns, the atexit handlers flush
   the achievements, stop the audio and close the window).  Without a
   handler (the headless build) the request calls exit(0), which runs the
   same atexit handlers.  NULL restores that. */
void ui_settings_set_quit_handler(void (*fn)(void));
/* The Window mode row shows, and steps from, fn's answer (an ICO_WINDOW_*
   value: windowed, borderless or fullscreen) instead of the option when one
   is installed; the window host installs it so the row follows Alt+Enter and
   the window manager.  NULL removes it (headless, tests). */
void ui_settings_set_window_mode_query(int (*fn)(void));
/* Display > Texture pack reads fn (texpack_count, the replacements
   found at start) to tell an installed pack from none ("None installed",
   the row then does not step); NULL, or none installed, is none. */
void ui_settings_set_texture_pack_count(int (*fn)(void));
/* Likewise the number of replacement models found (modelpack_count): the
   Model pack row reads "None installed" and does not step while it is 0 or
   no hook is set. */
void ui_settings_set_model_pack_count(int (*fn)(void));

/* Settings > Controls shows the touch overlay's rows (Touch
   controls, Touch size, Touch opacity) only while fn reports a touch screen
   (input_sdl.h ico_input_sdl_touch_present); NULL, or 0, hides them. */
void ui_settings_set_touch_query(int (*fn)(void));

/* The Android build's title Quit row and its confirmation
   read "Quit game" / "Quit the game?" while fn returns 1 (the phone's app is
   not a desktop program); no hook, or 0, keeps "Quit to desktop".  Asked
   when the menu is built, and again when fn is installed afterwards. */
void ui_settings_set_quit_is_game(int (*fn)(void));

/* Settings > Graphics driver (Android).  The page and its
   link on the main page (after Effects) exist only while a host is installed
   (ui_settings_set_gpu_driver_host); port/ui knows nothing of the platform, the
   host (window_host.c) answers for it.  Indices run 0..count()-1 over the
   installed drivers, sorted as the host likes; -1 means the phone's own
   (built-in) driver.  Every pointer must be set.  The strings returned stay
   valid until the next call into the host. */
#define UI_GPU_INSTALL_PENDING 0   /* the file picker or the copy is still running */
#define UI_GPU_INSTALL_ADDED 1     /* a driver was installed (count() grew) */
#define UI_GPU_INSTALL_BAD 2       /* the file is not a usable driver package */
#define UI_GPU_INSTALL_CANCELLED 3 /* the picker was dismissed; the page says nothing */
#define UI_GPU_INSTALL_NOSPACE 4   /* the device ran out of space */

typedef struct UiGpuDriverHost {
    int (*count)(void);         /* drivers installed */
    const char *(*name)(int i); /* display name of driver i */
    int (*selected)(void);      /* index chosen for the next start, -1 built-in */
    void (*select)(int i);      /* choose i (-1 built-in) for the next start; the host
                                        stores it (config key), the page marks the config
                                        dirty so it is written on leaving */
    int (*lastFailed)(int i);   /* nonzero: driver i did not start at the last start */
    int (*adreno)(void);        /* nonzero: the GPU is an Adreno (Add a driver shows) */
    int (*installBegin)(void);  /* open the picker; 0 started, -1 could not start */
    int (*installPoll)(void);   /* UI_GPU_INSTALL_*; called every frame while pending */
    void (*remove)(int i);      /* delete driver i (the page then selects built-in) */
} UiGpuDriverHost;

/* NULL (or never called) hides the link and the page. The struct is copied. */
void ui_settings_set_gpu_driver_host(const UiGpuDriverHost *host);
/* Settings > Extras > Models opens the layout fn returns
   (port/game/model_viewer.c's model list), or nothing when it returns -1 or
   none is set ("extras: models not available" in the log). */
void ui_settings_set_models_handler(int (*fn)(void));

/* Extras > Characters from the title runs inside the model viewer
   (port/game/model_viewer.c ico_mv_characters_*): the page's rows on the
   left, the character's model on the right, every change shown on the
   model at once.  The viewer hands Settings these calls (NULL removes
   them; then the title opens the page as the pause menu does):
     enter     Cross on Extras' Characters row from the title: the viewer
               loads Ico's model; 0, or -1 when it cannot (the page does
               not open, a log line)
     shown     the character on screen: 0 Ico, 1 Yorda; -1 while a model
               loads or the viewer leaves (the page takes no input); -2
               when the viewer is not running Characters (the page is the
               Options page: the pause menu)
     switchTo  the Switch row and L1 / R1: load the other character
     leave     Triangle or Back: to the title, which then opens Settings
               on Extras (ui_settings_reopen_page) */
typedef struct UiCharactersHost {
    int (*enter)(void);
    int (*shown)(void);
    void (*switchTo)(int character);
    void (*leave)(void);
} UiCharactersHost;

void ui_settings_set_characters_host(const UiCharactersHost *host);
/* whether the Characters page is the viewer's panel now (host->shown() is
   not -2) */
int ui_settings_characters_in_viewer(void);
/* The way back from the viewer's Characters: the menu opened again
   as if from the title layout in force (current_layout_id, 12 or 13), its
   pages' cursors on the path Main > ... > page (Main on Extras, Extras on
   its Characters row); returns the page's layout to switch to, -1 when
   the menu is not built or the current layout is not a title's. */
int ui_settings_reopen_page(int page /* UiSettingsPage */);
/* the quit confirmation's layout (tests) */
int ui_quit_screen_layout(void);
/* the "Yes" (yes = 1) and "No" (yes = 0) rows (tests) */
int ui_quit_screen_row(int yes);
/* the title's "Quit to desktop" row of layout 12 or 13, -1 otherwise
   (tests) */
int ui_settings_quit_row(int gameLayout);

/* Circle in the menus: every port screen (the Settings pages, the lists,
   the mirror and quit screens) takes Circle as Triangle, always; the game's
   own menus do while [game] circle_back is on (port/ui/layout_ext.h
   lt_ext_back_buttons, Settings > Controls). */

/* --- the pages, the options and the calls on them -------------------------
   Some of these are production calls (model_viewer.c, main_host.c,
   photo_ui.c, layout_action.c, ui_mouse.c use them); the ones marked
   (tests) are the tests' way in. */

typedef enum UiSettingsPage {
    UI_PAGE_MAIN = 0,
    UI_PAGE_DISPLAY,
    UI_PAGE_AUDIO,
    UI_PAGE_CONTROLS,
    UI_PAGE_GAMEPLAY,
    UI_PAGE_ACHIEVEMENTS,
    UI_PAGE_REMAP,
    UI_PAGE_EXTRAS,  /* Music, Models, Credits (from the title only), Characters */
    UI_PAGE_MUSIC,   /* Extras > Music, the music gallery (gallery.h) */
    UI_PAGE_EFFECTS, /* Effects: the game's own picture effects, On/Off (issue 11) */
    /* Extras > Characters, the characters' colours (port/game/
       appearance.h): nine stepped colour rows, Randomize, Reset, Back */
    UI_PAGE_CHARACTERS,
    UI_PAGE_GPU_DRIVER, /* Graphics driver (Android; main page link needs a host) */
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
    UI_OPT_WINDOW_MODE,
    UI_OPT_VSYNC,
    UI_OPT_FILTER,
    UI_OPT_FULL_HEIGHT,
    UI_OPT_FRAMERATE,    /* [video] framerate, stepped through its values */
    UI_OPT_CRT,          /* [video] crt and crt_mode in one row */
    UI_OPT_CRT_STRENGTH, /* [video] crt_strength, 0..100 % in tens */
    UI_OPT_TEXTURE_PACK, /* [video] texture_pack: On/Off, "None installed" without a pack */
    UI_OPT_MODEL_PACK, /* [video] model_pack: On/Off, "None installed" without a pack; title only */
    UI_OPT_BRIGHTNESS, /* the game's brightness step, systemStatus[11] 0..14 */
    UI_OPT_VIDEO_MODE,
    /* Effects ([video] effect_*; On/Off, outside the preset) */
    UI_OPT_EFFECT_GLOW,
    UI_OPT_EFFECT_DEPTH_OF_FIELD,
    UI_OPT_EFFECT_SOFTENING,
    UI_OPT_EFFECT_MOTION_BLUR,
    UI_OPT_EFFECT_FOG,
    UI_OPT_EFFECT_CINEMATIC_BARS,
    /* Audio */
    UI_OPT_VOLUME,
    UI_OPT_MUSIC,   /* [audio] music (port/audio/mix_gain.h) */
    UI_OPT_EFFECTS, /* [audio] effects */
    UI_OPT_OUTPUT,  /* [audio] output: Auto, Stereo, Mono (port/game/options.h) */
    UI_OPT_DEVICE,  /* [audio] device: Default or a device name (audio_host.h) */
    /* Controls */
    UI_OPT_MOUSE_SENS,
    UI_OPT_MOUSE_CAMERA, /* [input] mouse_camera, On/Off; not on Android */
    UI_OPT_MOUSE_INVERT, /* [input] mouse_invert_y, On/Off; not on Android */
    UI_OPT_MOUSE_SPEED,  /* [input] mouse_camera_speed, 0.5x to Instant; not on Android */
    UI_OPT_MOUSE_RANGE,  /* [input] mouse_full_range, Normal/Full; not on Android */
    UI_OPT_MOUSE_RETURN, /* [input] mouse_return, On/Off; not on Android */
    UI_OPT_CIRCLE_BACK,  /* [game] circle_back */
    UI_OPT_VIBRATION,    /* the game's iosPadActRequestEnable */
    UI_OPT_HOLD_TYPE,    /* the game's optionControlType, A 0 or B 1 */
    /* the touch overlay ([input] touch_*), shown with a touch screen */
    UI_OPT_TOUCH_MODE,    /* Off, Auto, Always */
    UI_OPT_TOUCH_SIZE,    /* Small, Medium, Large */
    UI_OPT_TOUCH_OPACITY, /* 25, 50, 75, 100 % */
    /* Gameplay */
    UI_OPT_STICK_FIX,
    UI_OPT_YORDA,
    UI_OPT_FILM_EFFECT, /* the game's optionScreenMode 0..4, once cleared */
    UI_OPT_PLAYERS,     /* the game's girlControlMode, 1 or 2, once cleared */
    UI_OPT_ACH_POPUPS,  /* [game] achievements, On/Off: the achievement pop-ups, on the
                           Achievements page before Back */
    /* Main */
    UI_OPT_LANGUAGE,
    UI_OPT_DUMP_TEXTURES, /* [video] dump_textures, for pack authors (developer mode) */
    UI_OPT_DUMP_MODELS,   /* [video] dump_models, for pack makers (developer mode) */
    /* Extras > Characters, one row per IcoAppPart in its order
       (UI_OPT_CHAR_ICO_SKIN + part): Original, a palette colour or a skin
       tone ([characters], appearance.h) */
    UI_OPT_CHAR_ICO_SKIN,
    UI_OPT_CHAR_ICO_PONCHO_NAVY,
    UI_OPT_CHAR_ICO_PONCHO_PINK,
    UI_OPT_CHAR_ICO_PONCHO_LIGHT,
    UI_OPT_CHAR_ICO_PONCHO_DARK,
    UI_OPT_CHAR_ICO_TUNIC,
    UI_OPT_CHAR_ICO_SHORTS,
    UI_OPT_CHAR_YORDA_SKIN,
    UI_OPT_CHAR_YORDA_DRESS,
    UI_OPT_DEVELOPER,
    /* actions */
    UI_OPT_BACK,
    /* list rows (achievements, remap targets) */
    UI_OPT_LIST,
    /* Extras: entries that open a gallery */
    UI_OPT_EXTRAS_MUSIC,
    UI_OPT_EXTRAS_MODELS,
    UI_OPT_EXTRAS_CREDITS, /* locked until the ending has been reached */
    /* Controls > Button configuration opens the game's own screen
       (layout 59, la_key_config), whose OK comes back to Controls */
    UI_OPT_BUTTON_CONFIG,
    /* Extras > Characters' actions: every part a random colour
       (ico_appearance_randomize), every part Original; in the viewer only
       the parts of the character shown (ico_appearance_randomize_character,
       ico_appearance_reset_character) */
    UI_OPT_CHAR_RANDOMIZE,
    UI_OPT_CHAR_RESET,
    /* the viewer's Characters only: load the other character */
    UI_OPT_CHAR_SWITCH,
    /* Graphics driver page: the stepped Driver row, Add a
       driver (file picker), Remove this driver */
    UI_OPT_GPU_DRIVER,
    UI_OPT_GPU_ADD,
    UI_OPT_GPU_REMOVE
} UiSettingsOpt;

/* The entry rows and the menu's layouts (-1 before ui_settings_install):
   for the pause menu (57) the game's Options row (294) and the port layout
   chained after 57 (the Photo mode row's); the title's (12, 13) port rows
   and their layouts (tests). */
int ui_settings_entry_row(int gameLayout); /* 57, 12 or 13 */
int ui_settings_entry_layout(int gameLayout);
/* The pause menu's "Photo mode" row under Options (masked and stepped over
   unless a stage runs, photo_ui.h ui_photo_available), -1 before the build
   (tests). */
int ui_settings_photo_row(void);
/* photo_ui.c's way back: the pause menu (57) with the cursor on the row. */
int ui_settings_photo_back(void);
/* la_key_config's OK: Settings > Controls with the cursor on Button
   configuration, or the game's Options screen (58) when the pause menu
   still opens it (before the build, or tables not the PAL ones). */
int ui_settings_key_config_back(void);
int ui_settings_page_layout(UiSettingsPage page);
/* A page's navigable rows in order: the label row of each, its option, its
   value row (-1 for none).  Returns the count (tests). */
int ui_settings_page_rows(UiSettingsPage page, int *labels, int *opts, int *values, int max);
/* The row index of an option on a page, -1 if it has none. */
int ui_settings_row_of(UiSettingsPage page, UiSettingsOpt opt);
/* the note row under a page's option, -1 for none (tests) */
int ui_settings_note_row_of(UiSettingsPage page, UiSettingsOpt opt);
/* The value text an option shows now (tests). */
const char *ui_settings_value_text(UiSettingsOpt opt);
/* Steps an option by dir (-1 left, +1 right) through its setter, as a
   left/right press on its row does (tests). */
void ui_settings_step(UiSettingsOpt opt, int dir);
/* Writes what changed (ico_video_save, ico_input_write_bindings,
   ico_config_save), as leaving a screen does.  0, or -1. */
int ui_settings_save(void);
/* What is still unwritten when the game closes (the window's close
   button, the title's Quit to desktop): ui_settings_save's
   pending pages, then any setting changed outside them (ico_config_dirty),
   so a change made on a page the player never left (Characters inside the
   model viewer, the pause menu's pages) is in the file at the next start.
   Logged when anything was pending.  0, or -1. */
int ui_settings_save_on_quit(void);
/* Forgets the layouts (tests: lt_ext_reset first). */
void ui_settings_reset(void);

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

void ui_remap_capture_start(UiRemapCapture *c, int target);
/* Whether the remap screen is waiting for a press (or in the
   few ticks after one), when the mouse pointer leaves the menu alone so a
   click is bound rather than chosen (port/ui/ui_mouse.c) */
int ui_settings_capturing(void);
/* One Main tick: UI_CAPTURE_WAITING, UI_CAPTURE_BOUND (b changed),
   UI_CAPTURE_TIMEOUT, or UI_CAPTURE_IDLE when not active. */
int ui_remap_capture_step(UiRemapCapture *c, void *bindings /* IcoBindings */);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_H */
