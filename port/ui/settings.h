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
 * (its right link repointed; the game's Options screen, layout 58, is not
 * reached, its settings are on the pages), and an "Options" row (the
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
 * ico_opt_set_*, the live binding table, NonLinearCameraMove (the
 * European disc's language),
 * systemStatus[0] with gsResetFunc) and are written with ico_video_save /
 * ico_config_save when a screen is left.  The game's own settings
 * (Brightness, Vibration, Hold type, Film effect, Players) are its
 * variables, written by the game's saves as the Options screen left them
 * (fumi/ios/mcard.c), never into the port config; they show only from the
 * pause menu, as the Options screen did (a load sets them from the save).
 * Hold type is the exception: [gameplay] hold_type keeps it, it shows on
 * the title too, and a load does not change it.
 */
#ifndef PORT_UI_SETTINGS_H
#define PORT_UI_SETTINGS_H

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
/* The Anti-aliasing row's list: the RdAntiAlias values (a bit each, as
   rd_aa_supported_mask) the row steps through.  Without a query the
   renderer's own answer (a build without it: Off and the supersampling
   values). */
void ui_settings_set_aa_mask_query(unsigned (*fn)(void));
/* Whether the renderer has switched MSAA off (rd_aa_msaa_blocked: no way of
   reading the multisampled depth works on this device); the row then shows
   the stored value with " (off: not available)".  Without a query the
   renderer's own answer (no renderer: never). */
void ui_settings_set_aa_blocked_query(int (*fn)(void));
/* Display > Texture pack reads fn (texpack_count, the replacements
   found at start) to tell an installed pack from none ("None installed",
   the row then does not step); NULL, or none installed, is none. */
void ui_settings_set_texture_pack_count(int (*fn)(void));
/* Likewise the number of replacement models found (modelpack_count): the
   Model pack row reads "None installed" and does not step while it is 0 or
   no hook is set. */
void ui_settings_set_model_pack_count(int (*fn)(void));
/* The package label (v0.7.1, v0.7.1-rc1), shown small in the top-right
   corner of the Options menu's main page; NULL or "" (a development build)
   shows nothing.  Copied (31 bytes at most); read when the pages are built
   (ui_settings_install), so it is set before. */
void ui_settings_set_version(const char *label);
/* the Main page's version row, -1 when none (tests) */
int ui_settings_version_row(void);

/* Settings > Controls shows its Touch screen row (the link to the
   UI_PAGE_TOUCH page: Touch controls, Touch size, Touch opacity, Edit
   layout, Snap to grid, Grid size, Reset layout, Reset all layouts) only
   while fn reports a touch screen (input_sdl.h ico_input_sdl_touch_present);
   NULL, or 0, hides it. */
void ui_settings_set_touch_query(int (*fn)(void));

/* Settings > Controls > Touch screen > Edit layout (the touch layout
   editor, port/input/touch_edit.h): the device layer's editor and its
   hooks. editor() the one IcoTouchEditor (NULL before init); begin()
   cancels the mapper's fingers and starts editing the posture the phone is
   in; end(keep) stops, keep copying the work into the live edits; rule()
   the live layout's ICO_TOUCH_RULE_*; layout() the layout the overlay is
   drawn from now (the preview while editing). NULL, or no host, locks the
   Edit layout row. */
struct IcoTouchEditor;
struct IcoTouchLayout;

typedef struct UiTouchEditHost {
    struct IcoTouchEditor *(*editor)(void);
    void (*begin)(void);
    void (*end)(int keep);
    int (*rule)(void);
    const struct IcoTouchLayout *(*layout)(void);
} UiTouchEditHost;

void ui_settings_set_touch_edit_host(const UiTouchEditHost *h);
/* The host installed, every hook set and editor() not NULL: 1 and *out
   filled, else 0 (touch_edit_ui.c, tests). */
int ui_settings_touch_edit_host(UiTouchEditHost *out);
/* The live edits changed outside the pages' steps (the Edit layout page's
   Done): ui_settings_save writes them (ico_input_write_touch_edits) with
   the rest. */
void ui_settings_touch_dirty(void);

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
#define UI_GPU_INSTALL_PENDING 0       /* the file picker or the copy is still running */
#define UI_GPU_INSTALL_ADDED 1         /* a driver was installed (count() grew) */
#define UI_GPU_INSTALL_BAD 2           /* the file was read and is not a usable driver package */
#define UI_GPU_INSTALL_CANCELLED 3     /* the picker was dismissed; the page says nothing */
#define UI_GPU_INSTALL_NOSPACE 4       /* the device ran out of space */
#define UI_GPU_INSTALL_NO_PICKER 5     /* the file picker could not be opened */
#define UI_GPU_INSTALL_NO_PICKER_APP 6 /* no app on the device can choose a file */
#define UI_GPU_INSTALL_UNREADABLE 7    /* the chosen file could not be opened or copied */

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
    /* the driver zips waiting in the game's drivers folder (the way in when
       the picker does not open): how many, the name of file i, and start
       adding file i (0 started, -1 could not start); the same installPoll
       answers */
    int (*fileCount)(void);
    const char *(*fileName)(int i);
    int (*installFileBegin)(int i);
    /* the drivers folder's path as the player finds it in a file manager
       ("Android/data/com.defnf.icopc/files/drivers"); named by the popups
       and notes that send the player to it. May be NULL (a plain "drivers"
       then). */
    const char *(*folderPath)(void);
    /* nonzero: the previous start lost the graphics device on the phone's
       own Qualcomm driver (the Graphics driver row's note then names the
       driver package route). May be NULL (0). */
    int (*lostOnStock)(void);
} UiGpuDriverHost;

/* NULL (or never called) hides the link and the page. The struct is copied. */
void ui_settings_set_gpu_driver_host(const UiGpuDriverHost *host);
/* Settings > Graphics (the page after Effects on the main page): the
   renderers this build links, as the names the config key [video] backend
   takes ("vulkan", "opengl", "d3d12", "d3d11"), in the order the Graphics
   API row steps them after Automatic.  At most 8 are kept; the strings must
   outlive the menu (the renderer's own names do).  The page and its link
   show only with two or more names; NULL or a count under two hides them.
   The row stores the choice in the config (written on leaving the page) and
   the window reads it at the next start. */
void ui_settings_set_backend_names(const char *const *names, int count);
/* Settings > Graphics > Apply: the renderer switched now, in the game, by
   the window (port/platform/window_switch.c), which installs this host.
   running() names the renderer running, as the backend names above;
   switchBegin(name, why) asks for a switch to name (why: UI_GFX_WHY_*) and
   answers with a UI_GFX_SWITCH_*: PENDING when it runs later (switchPoll
   then answers, every frame, until it is no longer PENDING), else what
   came of it at once; kept() is the player's Keep (the window logs it);
   automatic() (may be NULL) names the renderer Automatic started this run
   (its fallback after a renderer that did not start, or the restart's),
   NULL when that is Vulkan or the player chose the renderer at the start.
   FAILED means name did not start and the renderer that ran before is
   back; REFUSED that nothing was tried.  The Apply row shows while the
   Graphics API row's value is not the renderer running (Automatic counts
   as automatic()'s, else "vulkan"); Apply then turns the page into a 10 s
   countdown with Keep and Revert: Keep writes the choice, Revert, Circle
   or the end of the countdown switch back.  From Apply until Keep the
   config holds the value the page found (every save writes that one, the
   row shows the choice), and so does it after a FAILED Apply.  NULL (or
   never called) hides Apply; the struct is copied. */
#define UI_GFX_SWITCH_DONE 0
#define UI_GFX_SWITCH_PENDING 1
#define UI_GFX_SWITCH_FAILED (-1)
#define UI_GFX_SWITCH_REFUSED (-2)
#define UI_GFX_WHY_APPLY 0   /* the Apply row */
#define UI_GFX_WHY_REVERT 1  /* Revert or Circle in the countdown */
#define UI_GFX_WHY_TIMEOUT 2 /* the countdown ran out */
/* the countdown, in seconds of ui_settings_vsync's calls */
#define UI_GFX_KEEP_SECONDS 10

typedef struct UiGraphicsHost {
    const char *(*running)(void);
    int (*switchBegin)(const char *name, int why);
    int (*switchPoll)(void);
    void (*kept)(void);
    const char *(*automatic)(void);
} UiGraphicsHost;

void ui_settings_set_graphics_host(const UiGraphicsHost *host);
/* One vsync passed (ui_host.c ui_host_vsync, at the popups' rate,
   popup.h): the Apply countdown, and a switch the page waits for. */
void ui_settings_vsync(void);
/* Settings > Extras > Add to Steam (desktop only): fn adds the program to
   the player's Steam library and returns one of UI_STEAM_*; the menu
   answers with a popup. NULL (or never called) hides the row; Android never
   sets it. */
#define UI_STEAM_ADDED 0     /* added */
#define UI_STEAM_EXISTS 1    /* it was in the library already */
#define UI_STEAM_RUNNING 2   /* Steam runs: nothing was changed */
#define UI_STEAM_NOT_FOUND 3 /* no Steam on this computer */
#define UI_STEAM_NO_USER 4   /* no Steam account to add it to */
#define UI_STEAM_FAILED 5    /* the shortcut list could not be changed */
void ui_settings_set_steam_handler(int (*fn)(void));
/* Settings > Extras > Export textures (the title only): every picture on the
   disc saved under its PCSX2 name into the dumps folder, a job the host runs
   in the background (window_host.c over port/render/texpack_export.h; the
   UI knows nothing of it).  begin() starts it: 0 started, -1 not (the menu
   shows the failed popup at once); poll() fills *out and returns its state,
   called by the row every frame and by ui_settings_vsync while an export the
   menu started has not ended, so its end popup comes on any screen;
   cancel() asks a running one to stop.  dir is the folder as the player
   reads it ("textures/SCES-50760/dumps"), for the popups.  NULL (or never
   called) hides the row (the headless build, tests); every pointer must be
   set; the struct is copied. */
#define UI_TEXEXPORT_IDLE 0    /* none since the start */
#define UI_TEXEXPORT_RUNNING 1 /* walking the disc */
#define UI_TEXEXPORT_DONE 2    /* went through every picture */
#define UI_TEXEXPORT_STOPPED 3 /* cancel() stopped it */
#define UI_TEXEXPORT_FAILED 4  /* the disc could not be read or nothing could be written */

typedef struct UiTextureExportStatus {
    int state;   /* UI_TEXEXPORT_* */
    int done;    /* pictures handled */
    int total;   /* pictures expected (0 until the disc's index is read) */
    int written; /* files written */
    char dir[96];
} UiTextureExportStatus;

typedef struct UiTextureExportHost {
    int (*begin)(void);
    int (*poll)(UiTextureExportStatus *out);
    void (*cancel)(void);
} UiTextureExportHost;

void ui_settings_set_texture_export_host(const UiTextureExportHost *host);
/* Main > Save a picture for a bug report (always shown, directly above
   Developer mode: a player's row, no developer mode needed): fn arms the host's frame dump (window_host.c frame_dump: the
   picture, the frame's rd dump and the log in a zip in the dumps folder,
   as F12 makes) to run 3 seconds later, time to leave the menu; the menu
   answers with a popup that says so.  NULL (or never called) hides the
   row (the headless build, tests). */
void ui_settings_set_frame_dump_handler(void (*fn)(void));
/* Main > Share a bug report, in an rc build alone (ICO_RC_TOOLS above; a
   release has no such row, string or host code), on Android alone, directly
   below Save a picture for a bug report: fn hands the newest report files
   (the log, the run before's, the newest dump zip and presented picture,
   the newest report zip: share_report.h) to the system's share menu
   (window_host.c share_report_arm, host_android.c ico_android_share_files).
   NULL (or never called) hides the row.  With ICO_RC_TOOLS 0 the call keeps
   nothing (the rc_tools_off check in settings_extra_test). */
void ui_settings_set_share_report_handler(void (*fn)(void));
/* Main > Disc: fn answers whether a disc's game data (its archive_name,
   port/core/region.h) is there (port/platform/host_config.c
   ico_host_disc_archive_present); the row shows when every disc's is, and is
   asked once after each call. NULL (or never called) hides the row. */
void ui_settings_set_disc_handler(int (*fn)(const char *archive_name));
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
    UI_PAGE_EFFECTS, /* Effects: the game's own picture effects, On/Off */
    /* Extras > Characters, the characters' colours (port/game/
       appearance.h): nine stepped colour rows, Randomize, Reset, Back */
    UI_PAGE_CHARACTERS,
    UI_PAGE_GPU_DRIVER, /* Graphics driver (Android; main page link needs a host) */
    /* Controls > Touch screen (shown with a touch screen): the overlay's
       rows, Edit layout, Snap to grid, Grid size, the two Resets, Back */
    UI_PAGE_TOUCH,
    /* Touch screen > Edit layout: one masked row; the touch layout editor
       (touch_edit_ui.c) takes the page's input and the overlay draws it */
    UI_PAGE_TOUCH_LAYOUT,
    /* Graphics (main page, after Effects): the Graphics API row; the link
       shows only while the build has more than one renderer
       (ui_settings_set_backend_names) */
    UI_PAGE_GRAPHICS,
    /* Extras > Game variants (port/game/variants.h): the eight [variants]
       rows, the Debug menu (Developer mode only), Back */
    UI_PAGE_VARIANTS,
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
    UI_OPT_ANTI_ALIAS, /* [video] anti_aliasing, stepped through rd_aa_supported_mask */
    UI_OPT_WINDOW_MODE,
    UI_OPT_VSYNC,
    UI_OPT_FILTER,
    UI_OPT_FULL_HEIGHT,
    UI_OPT_FRAMERATE,    /* [video] framerate, stepped through its values */
    UI_OPT_SHOW_FPS,     /* [video] show_fps: Off/On, default Off (the Frame rate counter) */
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
    /* [video] effect_motion_blur and effect_motion_blur_mode: Off/Original/Smooth,
       default Original */
    UI_OPT_EFFECT_MOTION_BLUR,
    UI_OPT_EFFECT_FOG,
    UI_OPT_EFFECT_WATER, /* [video] effect_water_sharp: Original/Sharp, default Original */
    UI_OPT_EFFECT_CINEMATIC_BARS,
    UI_OPT_FULL_PIXEL, /* [video] full_pixel; On/Off, last Effects row, outside the preset */
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
    UI_OPT_HOLD_TYPE,    /* [gameplay] hold_type: A (hold) or B (toggle) */
    /* Controls > Touch screen: the touch overlay ([input] touch_*) and
       its layout edits ([touch]), shown with a touch screen */
    UI_OPT_TOUCH_MODE,    /* Off, Auto, Always */
    UI_OPT_TOUCH_SIZE,    /* Small, Medium, Large */
    UI_OPT_TOUCH_OPACITY, /* 25, 50, 75, 100 % */
    UI_OPT_TOUCH_SNAP,    /* [touch] snap: On/Off */
    UI_OPT_TOUCH_GRID,    /* [touch] grid: 2, 4 or 8 mm */
    /* Gameplay */
    UI_OPT_STICK_FIX,
    UI_OPT_YORDA,
    UI_OPT_PIT_TIME,       /* [gameplay] pit_time: Half, Original, Longer, Double, Triple */
    UI_OPT_DOOR_FIX,       /* [gameplay] door_fix, the doorway fix */
    UI_OPT_NATURAL_CHAINS, /* [gameplay] natural_chains (port/render/rd_chain.h) */
    UI_OPT_SKIP_OPENING,   /* [gameplay] skip_opening: the boot starts at the title */
    UI_OPT_FILM_EFFECT,    /* the game's optionScreenMode 0..4, once cleared */
    UI_OPT_PLAYERS,        /* the game's girlControlMode, 1 or 2, once cleared */
    UI_OPT_ACH_POPUPS,     /* [game] achievements, On/Off: the achievement pop-ups, on the
                              Achievements page before Back */
    /* Main */
    UI_OPT_LANGUAGE,      /* the game's own language (the European disc only) */
    UI_OPT_MENU_LANGUAGE, /* [ui] language: the port's text, Auto or a UiLang */
    UI_OPT_DISC,          /* [game] disc: the disc the next start plays; shown only with both
                             discs' game data (ui_settings_set_disc_handler) */
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
    /* Extras > Game variants, one row per IcoVariantId in its order
       (UI_OPT_VAR_TOUGHNESS + id): [variants] keys, port/game/variants.h */
    UI_OPT_VAR_TOUGHNESS,
    UI_OPT_VAR_ONE_HIT,
    UI_OPT_VAR_NEVER_DODGE,
    UI_OPT_VAR_AGGRESSION,
    UI_OPT_VAR_NO_HINTS,
    UI_OPT_VAR_BARRIER,
    UI_OPT_VAR_NO_LOOK,
    UI_OPT_VAR_IGNORE_CAMS,
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
    /* the stepped zip in the drivers folder; Cross adds it (shown while
       the folder holds a zip) */
    UI_OPT_GPU_ADD_FILE,
    UI_OPT_GPU_REMOVE,
    /* Graphics page: the stepped Graphics API row (config key video.backend) */
    UI_OPT_GRAPHICS_API,
    /* Graphics page: Apply (shown while the row's value is not the renderer
       running and the host is set), then the countdown's Keep and Revert
       (shown only in it) */
    UI_OPT_GRAPHICS_APPLY,
    UI_OPT_GRAPHICS_KEEP,
    UI_OPT_GRAPHICS_REVERT,
    /* Extras > Add to Steam (desktop; shown from the title while a host
       handler is set) */
    UI_OPT_EXTRAS_STEAM,
    /* Extras > Export textures (shown from the title while a host is set,
       ui_settings_set_texture_export_host); Stop exporting while it runs */
    UI_OPT_EXTRAS_EXPORT_TEXTURES,
    /* Controls > Touch screen's actions: Edit layout (opens
       UI_PAGE_TOUCH_LAYOUT; locked while Touch controls is Off or without
       an editor host), Reset layout (the posture the phone is in now) and
       Reset all layouts (every posture) */
    UI_OPT_TOUCH_EDIT,
    UI_OPT_TOUCH_RESET,
    UI_OPT_TOUCH_RESET_ALL,
    /* Main's row "Save a picture for a bug report", above Developer mode:
       the host's frame dump, 3 s later (ui_settings_set_frame_dump_handler) */
    UI_OPT_REPORT_PICTURE,
    /* Game variants' Debug menu: a press row, shown while Developer mode
       is on (ico_variants_debug_menu_pressed) */
    UI_OPT_VAR_DEBUG_MENU,
#if ICO_RC_TOOLS
    /* Main's row "Share a bug report" below it, rc builds on Android alone
       (ui_settings_set_share_report_handler) */
    UI_OPT_SHARE_REPORT,
#endif
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
/* Sets the language of the port's text (ui_set_language) by the Menu
   language row's rule (ui_lang.h ui_settings_resolve_language: [ui]
   language, else the disc's region row's ui_lang_default (Japanese on the
   Japanese disc), the game's own language on the European disc, else
   English) and the sheets' look language (ui_set_sheet_language: the
   region row's menu_sheet, UI_SHEET_JA on the Japanese disc, else the
   game's language on the European one); every screen of the port calls it before
   it draws its words, so a change of either language shows at once. */
void ui_settings_apply_language(void);
/* Whether a row of opt is shown now (its page masks it otherwise): the
   running disc, the platform, the entry and the game's state decide
   (tests). */
int ui_settings_opt_shown(UiSettingsOpt opt);
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
   ico_input_write_touch_edits, ico_config_save), as leaving a screen
   does.  0, or -1. */
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
