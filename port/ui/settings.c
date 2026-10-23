/*
 * port/ui/settings.c
 *
 * The Settings menu (settings.h).
 * Port layouts in the layout extension, run by the game's layout code; this
 * file builds them, repoints the game's rows at the entry rows, and holds
 * the screens' procs: the value texts, left/right on a value, Cross on an
 * action, Triangle or Circle back, the scrolling lists, the remap capture,
 * the saves when a screen is left, and the title's "Quit to desktop" row
 * with its confirmation.
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "achievements.h"
#include "appearance.h"
#include "audio_host.h"
#include "config.h"
#include "font.h"
#include "gallery.h"
#include "ico_credits.h"
#include "ico_gamestate.h"
#include "input.h"
#include "layout_ext.h"
#include "menu_text.h"
#include "menu_font.h"
#include "mix_gain.h"
#include "options.h"
#include "photo_mode.h"
#include "photo_ui.h"
#include "popup.h"
#include "strings.h"
#include "sysconf.h"
#include "touch_edit.h"    /* the touch layout edits (port/input) */
#include "touch_edit_ui.h" /* the Edit layout page */
#include "ui_hint.h"
#include "ui_list.h"
#include "video_options.h"
#include "../core/region.h" /* ico_region_cur: the running disc */
#include "ui_lang.h"        /* ui_settings_resolve_language */

#ifdef ICO_RD
#include "rd.h" /* rd_set_mirror */
#endif

/* the number of elements of an array */
#define N_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* --- the game's side (common/; layout_texture.h declares the lt_* calls) -- */

extern PadState pad[16];
extern int NonLinearCameraMove; /* the language, 2..6 */
extern int systemStatus[12];    /* [0]: 1 PAL 50 Hz, 0 60 Hz */
extern int gFlagGameClear;
extern int stage_no;             /* common/src/main.c: the stage running, 1 the title */
extern int gsResetFunc(int val); /* debug.c: gsb_Init, the boot screen's reset */
extern void CUR_SE(void);        /* layout_action.c: the menus' sounds */
extern void POSITIVE_SE(void);
extern void NEGATIVE_SE(void);
extern void la_host_leave(void); /* layout_action.c (ICO_HOST) */
/* fumi/sound/s_init.c: the PS2's stereo (0) or mono (1) output */
extern int soundOutputModeGet(void);
extern void soundOutputModeSet(int mode);
/* layout_action.c: what la_vibe_select's confirm did after
   the vibration choice, gflagOn(382): the new game starts */
extern void la_host_new_game_go(void);
/* the game's Options screen's settings (common/src/main.c,
   fumi/ios/pad.c): its film effect 0..4 (only once the game is cleared),
   hold type A 0 or B 1, players 1 (0) or 2 (1), and the vibration switch;
   the brightness step is systemStatus[11].  The game's saves write them
   (fumi/ios/mcard.c product_write, gameblock_write); the hold type is read
   back from [gameplay] hold_type, not from the card (applyHoldType). */
extern int optionScreenMode;
extern int optionControlType;
extern int girlControlMode;
extern int iosPadActRequestEnable;
/* layout_action.c: a film effect in force with the stage
   animations la_game_option starts and stops for it */
extern void la_host_film_effect(int mode);
/* port/platform/trace_host.h (ico_pc): the Main ticks so far, with the
   clock the seed of Extras > Characters' Randomize */
extern unsigned int ico_host_main_ticks(void);

/* on the port's screens the face button that does not confirm goes back
   as Triangle does (Circle on the European disc, Cross on the Japanese and Korean ones:
   layout_ext.h lt_ext_port_back_buttons), whatever [game] circle_back says
   (that switch is for the game's own menus, lt_ext_back_buttons): no PS2
   behaviour to keep here, and the button has no other use on them (a
   remap capture takes it before this check); they confirm with the
   disc's confirm (lt_ext_confirm_button: Cross, Circle on the Japanese
   and Korean discs) */
#define PAD_BACK (lt_ext_port_back_buttons())
#define PAD_OK (lt_ext_confirm_button())

/* The game's numbers are the running disc's (port/core/region.h
   ico_region; the European disc's in the comments).  The game layouts the
   menu is entered from: the pause menu (57), the title's Continue and New
   Game (12) and New Game only (13) */
#define LAYOUT_PAUSE (ico_region_cur()->layout_pause)
#define LAYOUT_TITLE_CONTINUE (ico_region_cur()->layout_title_continue)
#define LAYOUT_TITLE_NEW (ico_region_cur()->layout_title_new)
/* the game's Options screen (58: not reached, but la_key_config's way
   back when the pause menu was left as the game's) and its button
   configuration screen (59: Settings > Controls opens it) */
#define LAYOUT_GAME_OPTIONS (ico_region_cur()->layout_options)
#define LAYOUT_KEY_CONFIG (ico_region_cur()->layout_key_config)
/* the pause menu's rows: Options (opens Settings, 294), Back (295), End
   Game (296), and the layout's range (292 .. 297) */
#define ROW_PAUSE_OPTIONS (ico_region_cur()->row_pause_options)
#define ROW_PAUSE_BACK (ico_region_cur()->row_pause_back)
#define ROW_PAUSE_END (ico_region_cur()->row_pause_end)
/* the title's rows: Continue (49), New Game (50; 51 on the New-Game-only
   layout), the copyright line (48, and on the Japanese and Korean discs its
   second half) */
#define ROW_TITLE_CONTINUE (ico_region_cur()->row_title_continue)
#define ROW_TITLE_NEW (ico_region_cur()->row_title_new)
#define ROW_TITLE_NEW_ONLY (ico_region_cur()->row_title_new_only)
#define ROW_TITLE_COPYRIGHT (ico_region_cur()->row_title_copyright)
#define ROW_TITLE_COPYRIGHT2 (ico_region_cur()->row_title_copyright2)
/* the pause menu's pitch in the PAL data (Options 50, Back 70) */
#define PAUSE_PITCH 20

/* the brightness step's range (la_adjust_screen) */
#define BRIGHTNESS_MAX 14
/* its default: la_adjust_screen's Triangle (0x1BE0F8, "Default", row 391)
   and script/src/e3.c set 7; here Square, Triangle being Back */
#define BRIGHTNESS_DEFAULT 7
#define FILM_EFFECTS 5

enum { ENTRY_PAUSE, ENTRY_TITLE12, ENTRY_TITLE13, ENTRY_COUNT };

/* the game layout of an entry */
static int entryGame(int e)
{
    return e == ENTRY_PAUSE     ? LAYOUT_PAUSE
           : e == ENTRY_TITLE12 ? LAYOUT_TITLE_CONTINUE
                                : LAYOUT_TITLE_NEW;
}

/* geometry, in the layout grid (dispX pixels of 640, dispY field lines of 226) */
#define HEADER_Y 12
#define LABEL_X 44
#define LABEL_W 300 /* right-aligned, ending at x 344 as the Options rectangles end at 364 */
/* a stepped value: centred between its two arrows, as the Options screen's
   values sit between rows 309 and 310 */
#define ARROW_L_X 362
#define ARROW_R_X 556
#define ARROW_W 14
#define STEP_X 378
#define STEP_W 176
#define NOTE_Y 196
#define HEADER_SIZE 30.0f
#define NOTE_SIZE 19.0f
#define REMAP_ITEMS (ICO_T_COUNT + 2) /* the targets, Reset, Back */

/* the pause menu's Photo mode row: under Options, its letters starting
   where the pause rows' do (menu_text.c's left anchor, 7 texels into the
   rectangle at x 40; display_texture's box starts a quarter pixel in, the
   lettering a quarter before the anchor) */
#define PAUSE_LETTERS_IN 6
/* ends at x 296, before the journey's lines (STATS_X); "Photo mode" is
   short in every language */
#define PAUSE_ROW_W 250
/* The title is laid out by the port (placeTitle): its four rows, Continue
   (49), New Game (50, and 51 on the New-Game-only layout), Options and
   "Quit to desktop", one game pitch apart (20 field lines, the Options
   screen's), and the copyright line (48) below Quit with the same space
   between its capitals and Quit's as between the rows' (19 field lines:
   its capitals are 24 output pixels at 960 x 720, the rows' 31).  The PAL
   data has Continue at 135, New Game at 165 and the copyright line at 195,
   the next step below New Game, which left no room for the port rows.
   Where the block can go was measured on window runs' titles (960 x 720
   Original, 1920 x 1440 Enhanced at full height): the copyright line's sprite, its rim and
   descenders included, ends 17 pixels above the picture's end at 195 (5 field
   lines), and the logo ends 384 pixels down; Continue at 119 and the
   copyright line at 198 leave about 6 pixels at either end. */
#define TITLE_PITCH 20
#define TITLE_CONTINUE_Y 119
#define TITLE_COPYRIGHT_STEP 19
/* the rows: 0 Continue, 1 New Game, 2 Settings, 3 Quit */
#define TITLE_Y(row) (TITLE_CONTINUE_Y + (row) * TITLE_PITCH)
#define TITLE_COPYRIGHT_Y (TITLE_Y(3) + TITLE_COPYRIGHT_STEP)
/* where the PAL data has Continue, New Game and the copyright line */
#define PAL_CONTINUE_Y 135
#define PAL_NEW_GAME_Y 165
#define PAL_COPYRIGHT_Y 195

/* Main's sixteen, and the rc builds' Share a bug report (ICO_RC_TOOLS) */
#define MAX_ROWS (17 + ICO_RC_TOOLS)

typedef struct Row {
    int opt;   /* UiSettingsOpt */
    int label; /* the navigable row */
    int value; /* the value row (ownerItem = label), -1 */
    int note;  /* shown while label is selected, -1 */
    int noteStr;
    int link; /* UiSettingsPage the row opens (UI_OPT_LINK), -1 */
} Row;

typedef struct Page {
    int layout;
    int header;
    int parent; /* UiSettingsPage, -1: where the menu was entered from */
    int count;
    Row rows[MAX_ROWS];
    /* the scrolling lists (achievements, remap; ui_list.h) */
    int isList;
    UiList list;
    int version; /* the Main page's version row, -1: none (ui_settings_set_version) */
} Page;

static Page s_pages[UI_PAGE_COUNT];
static int s_built;
static int s_warned;
static int s_entryRow[ENTRY_COUNT] = {-1, -1, -1}; /* [ENTRY_PAUSE]: 294, once installed */
static int s_quitRow[ENTRY_COUNT] = {-1, -1, -1};  /* the title's "Quit to desktop" */
static int s_entryLayout[ENTRY_COUNT] = {-1, -1, -1};
static int s_quitHeader = -1;
/* the Android build asks "Quit game" (the phone's app is not
   a desktop program); the host says so with ui_settings_set_quit_is_game */
static int (*s_quitIsGame)(void);

static int quitIsGame(void)
{
    return s_quitIsGame != NULL && s_quitIsGame() == 1;
}

static UiStrId quitRowStr(void)
{
    return quitIsGame() ? UI_STR_QUIT_GAME : UI_STR_QUIT_DESKTOP;
}

static UiStrId quitConfirmStr(void)
{
    return quitIsGame() ? UI_STR_QUIT_GAME_CONFIRM : UI_STR_QUIT_CONFIRM;
}

/* the pause menu's "Photo mode" under Options, in a
   port layout chained after 57; opens photo_ui.c's layout (only while a
   stage runs: placePause) */
static int s_photoRow = -1;

/* The journey's lines on the pause menu's right (pauseStats): a backdrop,
   then a label and a value row per line, after the Photo mode row in its
   layout; hidden by default */
enum {
    STAT_PLAY_TIME,
    STAT_DEATHS,
    STAT_CAPTURES,
    STAT_SAVES,
    STAT_ENEMIES,
    STAT_NEWGAME_PLUS,
    STAT_MIRROR,
    STAT_ACHIEVEMENTS,
    STAT_ASSISTS, /* the heading; the active assists one a line under it */
    STAT_ASSIST_1,
    STAT_ASSIST_2,
    STAT_ASSIST_3,
    STAT_AREA,
    STAT_LINES
};

static int s_statsBuilt;
static int s_statBack = -1;
static int s_statLabel[STAT_LINES], s_statValue[STAT_LINES];
static int s_origin = -1; /* the game layout the menu returns to (the pause menu from install) */

/* The video mode arms the tick rate (the game's timers are armed at the
   rate in force), so it changes only when Settings was opened from the
   title; from the pause menu its value shows "(title only)". */
static int onTitle(void)
{
    return s_origin == LAYOUT_TITLE_CONTINUE || s_origin == LAYOUT_TITLE_NEW;
}

/* the title is up invisibly while Options is to open on a page
   (Characters' way back, model_viewer.c): s_titleReturn is the page (-1 for
   none), s_titleDecided whether the title's card check has decided (its
   procs' last TitleMask(0)), s_titleHandoff the frames of the switch to the
   page, in which the title layout is still current and the logo stays down */
static int s_titleReturn = -1;
static int s_titleDecided;
static int s_titleHandoff;

static int isTitleLayout(int l)
{
    return l == LAYOUT_TITLE_CONTINUE || l == LAYOUT_TITLE_NEW;
}

static int s_restoreTitle = -1; /* a game layout whose defaultItem to restore */
static int s_restoreDefault;
static int s_dirtyVideo, s_dirtyConfig, s_dirtyBindings;
/* the touch layout edits changed (ico_input_live_touch_edits): the Snap to
   grid and Grid size rows, the Resets, the Edit layout page's Done */
static int s_dirtyTouch;
static UiRemapCapture s_capture;
static char s_text[256];

/* the remap screen's order: the face buttons first, as a pad is read */
static const int kRemapOrder[ICO_T_COUNT] = {
    ICO_T_CROSS,     ICO_T_CIRCLE,      ICO_T_SQUARE,      ICO_T_TRIANGLE,    ICO_T_L1,
    ICO_T_R1,        ICO_T_L2,          ICO_T_R2,          ICO_T_L3,          ICO_T_R3,
    ICO_T_START,     ICO_T_SELECT,      ICO_T_UP,          ICO_T_DOWN,        ICO_T_LEFT,
    ICO_T_RIGHT,     ICO_T_LSTICK_UP,   ICO_T_LSTICK_DOWN, ICO_T_LSTICK_LEFT, ICO_T_LSTICK_RIGHT,
    ICO_T_RSTICK_UP, ICO_T_RSTICK_DOWN, ICO_T_RSTICK_LEFT, ICO_T_RSTICK_RIGHT};

/* --------------------------------------------------------------- rows */

static int s_uid;

int ui_settings_add_row(int x, int y, int w, int h, int selectable, int owner, int strId,
                        const char *text, float size, int align)
{
    LtProperty r;
    memset(&r, 0, sizeof(r));
    r.word0 = r.word4 = r.word8 = r.wordC = -1;
    r.ownerItem = owner;
    r.up = r.down = r.left = r.right = -1;
    r.rightItem = r.leftItem = r.downItem = r.upItem = -1;
    r.word40 = 1;
    r.dispX = x;
    r.dispY = y;
    r.dispW = w;
    r.dispH = h;
    /* a texel rectangle of its own, so the layout's fade-cancel check
       (rows that look the same in the old and new layout skip the fade)
       never pairs two port rows, or a port row with a game row */
    r.texV = 1000 + s_uid++;
    r.selectable = selectable ? 1 : 0;
    LtExtText t = {strId, text, size, align};
    return lt_ext_add_property(&r, &t);
}

static LtProperty *P(int index)
{
    return lt_ext_prop(index);
}

static int addLayout(int first, int last, float colA, int (*proc)(int, int), int def)
{
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = last;
    l.fadeInTime = 0.3f;
    l.fadeOutTime = 0.1f;
    l.colA = colA;
    l.proc = proc;
    l.procFirst = 1;
    l.defaultItem = def;
    l.curItem = def;
    l.link = -1;
    return lt_ext_add_layout(&l);
}

/* ------------------------------------------------------------- values */

static int stepIndex(int i, int n, int dir)
{
    return ((i + dir) % n + n) % n;
}

static const float kMouseSens[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};
/* the Mouse camera speed row's steps; the last one is "Instant" */
static const float kMouseCamSpeed[] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f, 10.0f};

/* the Frame rate row's values, in the order Right steps them
   (ico_video_parse_framerate / ico_video_framerate_name: "original",
   "uncapped" or N presents a second) */
static const int kFramerates[] = {
    ICO_FRAMERATE_ORIGINAL, ICO_FRAMERATE_UNCAPPED, 60, 120, 144, 240};

/* The next value from fr: a listed value steps (and wraps) in the list; a
   cap the list does not hold (framerate = 100 in the file) steps to the
   listed cap above it (Right) or below it (Left), past the ends to
   "original" (Right) or "uncapped" (Left) */
static int stepFramerate(int fr, int dir)
{
    for (int i = 0; i < N_OF(kFramerates); i++) {
        if (kFramerates[i] == fr) {
            return kFramerates[stepIndex(i, N_OF(kFramerates), dir)];
        }
    }
    if (dir > 0) {
        for (int i = 2; i < N_OF(kFramerates); i++) {
            if (kFramerates[i] > fr) {
                return kFramerates[i];
            }
        }
        return ICO_FRAMERATE_ORIGINAL;
    }
    for (int i = N_OF(kFramerates) - 1; i >= 2; i--) {
        if (kFramerates[i] < fr) {
            return kFramerates[i];
        }
    }
    return ICO_FRAMERATE_UNCAPPED;
}

/* the CRT filter row's names of the modes (ICO_CRT_* order) */
static const int kCrtStr[ICO_CRT_MODES] = {UI_STR_VAL_CRT_SCANLINES, UI_STR_VAL_CRT_CONSUMER,
                                           UI_STR_VAL_CRT_TRINITRON, UI_STR_VAL_CRT_PVM,
                                           UI_STR_VAL_CRT_SHADOW};

/* the CRT filter in force (a mode at a strength above 0, as
   rd__crt_on sees it): the scene renders at 1x, and the Resolution row reads
   "1x (CRT)" and does not step; the file's resolution is kept and is in
   force again with the filter off */
static int crtForcesNative(const IcoVideoOptions *o)
{
    return o->crt && o->crtStrength > 0.0f;
}

static IcoBindings *liveBindings(void)
{
    IcoBindings *b = ico_input_live_bindings();
    if (b->mouse_sens == 0.0f) {
        /* not filled by the device layer (headless, tests): the config's */
        ico_input_reload_bindings(b);
    }
    return b;
}

/* [audio] volume, music or effects clamped to 0..1 as the output applies
   them (NaN as 0, port/audio/volume.c, mix_gain.c): a value outside it in
   the file would overflow the int conversions below */
static double unit01(const char *path)
{
    double v = ico_config_get_float(path, 1.0);
    if (!(v > 0.0)) {
        return 0.0;
    }
    return v < 1.0 ? v : 1.0;
}

/* a 0..1 setting stepped by a tenth, clamped */
static double stepTenth(double v, int dir)
{
    int t = (int)(v * 10.0 + 0.5) + dir;
    return (t < 0 ? 0 : t > 10 ? 10 : t) / 10.0;
}

/* [audio] output (port/game/options.h) in force: the game's mode set from
   an explicit key; with `restore` (the row stepped to Auto) the game's own
   mode back.  Install passes 0, so with Auto the game's mode is never
   touched there; nothing is sent when the mode is the game's already. */
static void applyOutputMode(int restore)
{
    int cur = soundOutputModeGet();
    int m = ico_opt_output_resolve(cur);
    if (m != cur && (restore || ico_opt_output_mode() != ICO_OUTPUT_AUTO)) {
        soundOutputModeSet(m);
    }
}

/* [gameplay] hold_type (port/game/options.h) is the game's hold type: the
   card's value is not read back (fumi/ios/mcard.c), so install and the row
   set the variable.  Idempotent. */
static void applyHoldType(void)
{
    optionControlType = ico_opt_hold_type();
}

/* the value box's text for [audio] device: Default, or the name, cut with
   an ellipsis where even the shrink to fit (down to 60 %, layout_ext.c)
   would leave it wider than the box (less a margin before the arrow) */
static const char *deviceText(char *buf, unsigned size)
{
    const char *name = ico_config_get_string("audio.device", "");
    if (name[0] == '\0') {
        return ui_str(UI_STR_VAL_DEFAULT_DEVICE);
    }
    /* n bytes of the name, on a character's first byte, and "…" after a cut */
    const size_t len = strlen(name);
    size_t n = len;
    if (size < 8) {
        return "";
    }
    if (n > size - 4) {
        n = size - 4;
        while (n > 0 && ((unsigned char)name[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    for (;;) {
        snprintf(buf, size, "%.*s%s", (int)n, name, n < len ? "\xE2\x80\xA6" : "");
        if (n == 0 || ui_measure_menu_text(UI_MENU_TEXT_SIZE * 0.6f, buf) <= (float)(STEP_W - 8)) {
            break;
        }
        do {
            n--;
        } while (n > 0 && ((unsigned char)name[n] & 0xC0u) == 0x80u);
    }
    return buf;
}

/* Settings > Audio > Output device: Default ("") and the devices' names,
   the config's name stepped to the next; a name the list lacks (a device
   unplugged) counts as Default */
static void stepDevice(int dir)
{
    static char names[16][ICO_AUDIO_DEVICE_NAME_MAX];
    int n = ico_audio_sdl_devices(names, 16);
    const char *cur = ico_config_get_string("audio.device", "");
    int i = 0;
    for (int k = 0; k < n; k++) {
        if (strcmp(names[k], cur) == 0) {
            i = k + 1;
        }
    }
    i = stepIndex(i, n + 1, dir);
    const char *to = i == 0 ? "" : names[i - 1];
    if (strcmp(to, cur) == 0) {
        return;
    }
    ico_config_set_string("audio.device", to);
    ico_audio_sdl_reopen(to); /* the stream moves now; -1 without an output */
    s_dirtyConfig = 1;
}

static int (*s_texturePackCount)(void); /* replacements installed, when known */
static int (*s_touchQuery)(void);       /* a touch screen exists, when known */
static UiTouchEditHost s_touchEditHost; /* the layout editor's hooks (window_host.c) */
static int s_touchEditHostSet;

static int touchPresent(void)
{
    return s_touchQuery != NULL && s_touchQuery() != 0;
}

/* the editor host is there: every hook, and an editor */
static int touchEditHosted(void)
{
    const UiTouchEditHost *h = &s_touchEditHost;
    return s_touchEditHostSet && h->editor != NULL && h->begin != NULL && h->end != NULL &&
           h->rule != NULL && h->layout != NULL && h->editor() != NULL;
}

int ui_settings_touch_edit_host(UiTouchEditHost *out)
{
    if (!touchEditHosted()) {
        return 0;
    }
    if (out != NULL) {
        *out = s_touchEditHost;
    }
    return 1;
}

void ui_settings_touch_dirty(void)
{
    s_dirtyTouch = 1;
}

/* the posture the phone is in now (the host's live layout), FULL without
   a host: the one Reset layout clears */
static int touchRule(void)
{
    const int r = touchEditHosted() ? s_touchEditHost.rule() : ICO_TOUCH_RULE_FULL;
    return r >= 0 && r < ICO_TOUCH_POSTURES ? r : ICO_TOUCH_RULE_FULL;
}

/* the host's graphics-driver hooks (settings.h) */
static UiGpuDriverHost s_gpuHost;
static int s_gpuHostSet;
static int s_gpuPending; /* an install is running: installPoll every frame */
static int
    s_gpuFileCur; /* the zip of the drivers folder the Add from the drivers folder row shows */

/* the renderers the build links (ui_settings_set_backend_names): the config
   values the Graphics API row writes after "" (Automatic) */
#define MAX_BACKENDS 8
static const char *s_backendNames[MAX_BACKENDS];
static int s_backendCount;

void ui_settings_set_backend_names(const char *const *names, int count)
{
    s_backendCount = 0;
    for (int i = 0; names != NULL && i < count && i < MAX_BACKENDS; i++) {
        if (names[i] != NULL && names[i][0] != '\0') {
            s_backendNames[s_backendCount++] = names[i];
        }
    }
}

/* the Graphics API row: Automatic is index 0, then each name; the config's
   value decides (a value the build does not link reads as Automatic when
   stepped and shows as written) */
static int backendCurrent(void)
{
    const char *v = ico_config_get_string("video.backend", "");
    for (int i = 0; i < s_backendCount; i++) {
        if (v != NULL && strcmp(v, s_backendNames[i]) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/* a renderer's name as the player knows it (proper names, not translated) */
static const char *backendLabel(const char *name)
{
    if (strcmp(name, "vulkan") == 0) {
        return "Vulkan";
    }
    if (strcmp(name, "opengl") == 0) {
        return "OpenGL";
    }
    if (strcmp(name, "d3d12") == 0) {
        return "Direct3D 12";
    }
    if (strcmp(name, "d3d11") == 0) {
        return "Direct3D 11";
    }
    return name;
}

static const char *backendText(char *buf, unsigned size)
{
    const char *v = ico_config_get_string("video.backend", "");
    if (v == NULL || v[0] == '\0') {
        return ui_str(UI_STR_VAL_GRAPHICS_AUTOMATIC);
    }
    snprintf(buf, size, "%s", backendLabel(v));
    return buf;
}

static void stepBackend(int dir)
{
    if (s_backendCount < 2) {
        return;
    }
    const int to = stepIndex(backendCurrent(), s_backendCount + 1, dir);
    const char *v = to == 0 ? "" : s_backendNames[to - 1];
    ico_config_set_string("video.backend", v);
    s_dirtyConfig = 1;
    fprintf(stderr, "settings: Graphics API set to %s (used from the next start)\n",
            v[0] ? v : "automatic");
}

/* the zips waiting in the drivers folder, 0 without a host */
static int gpuFileCount(void)
{
    const int n = s_gpuHostSet ? s_gpuHost.fileCount() : 0;
    return n > 0 ? n : 0;
}

/* the shown zip, kept inside the list (a zip came or went since) */
static int gpuFileIndex(void)
{
    const int n = gpuFileCount();
    if (s_gpuFileCur >= n) {
        s_gpuFileCur = n - 1;
    }
    if (s_gpuFileCur < 0) {
        s_gpuFileCur = 0;
    }
    return s_gpuFileCur;
}

static int gpuSelected(void)
{
    const int n = s_gpuHost.count();
    const int i = s_gpuHost.selected();
    return i >= 0 && i < n ? i : -1;
}

/* a name cut to the Driver rows' value box (the shrink to fit does the rest) */
static const char *gpuFit(char *buf, unsigned size, const char *name)
{
    name = name != NULL ? name : "";
    const size_t len = strlen(name);
    size_t n = len;
    if (size < 8) {
        return "";
    }
    for (;;) {
        snprintf(buf, size, "%.*s%s", (int)n, name, n < len ? "\xE2\x80\xA6" : "");
        if (n == 0 || ui_measure_menu_text(UI_MENU_TEXT_SIZE * 0.6f, buf) <= (float)(STEP_W - 8)) {
            break;
        }
        do {
            n--;
        } while (n > 0 && ((unsigned char)name[n] & 0xC0u) == 0x80u);
    }
    return buf;
}

/* the Driver row's text: Built-in or the chosen name, cut to its box; a
   driver that did not start last time is said in the page's note
   (gpuRefresh), where a sentence fits */
static const char *gpuText(char *buf, unsigned size)
{
    const int i = gpuSelected();
    if (i < 0) {
        return ui_str(UI_STR_VAL_GPU_BUILTIN);
    }
    return gpuFit(buf, size, s_gpuHost.name(i));
}

/* the Add from the drivers folder row's text: the shown zip's name */
static const char *gpuFileText(char *buf, unsigned size)
{
    return gpuFit(buf, size, s_gpuHost.fileName(gpuFileIndex()));
}

/* the page's one note: the Adreno-and-restart line, or that the chosen
   driver did not start last time.  A driver that does not start is
   remembered as failed and the choice goes back to Built-in (the host,
   gpu_driver_android.c), and choosing a driver again forgets the failure:
   so the failed note shows while Built-in is chosen and an installed
   driver is marked failed (or, for a host that keeps a failed choice, while
   the chosen one is) */
static int gpuShowFailed(void)
{
    const int i = gpuSelected();
    if (i >= 0) {
        return s_gpuHost.lastFailed(i) != 0;
    }
    const int n = s_gpuHost.count();
    for (int k = 0; k < n; k++) {
        if (s_gpuHost.lastFailed(k)) {
            return 1;
        }
    }
    return 0;
}

static void gpuRefresh(Page *pg)
{
    const int note = gpuShowFailed() ? UI_STR_GPU_DRIVER_FAILED : UI_STR_GPU_DRIVER_NOTE;
    for (int k = 0; k < pg->count; k++) {
        if (pg->rows[k].note >= 0) {
            pg->rows[k].noteStr = note;
        }
    }
}

/* Driver: Built-in, then each installed driver, around; the host stores the
   choice, the config is written on leaving the page */
static void stepGpuDriver(int dir)
{
    if (!s_gpuHostSet) {
        return;
    }
    const int cur = gpuSelected();
    const int to = stepIndex(cur + 1, s_gpuHost.count() + 1, dir) - 1;
    if (to != cur) {
        s_gpuHost.select(to);
        s_dirtyConfig = 1;
    }
}

/* Add from the drivers folder: Left and Right go round the zips there */
static void stepGpuFile(int dir)
{
    const int n = gpuFileCount();
    if (n > 0) {
        s_gpuFileCur = stepIndex(gpuFileIndex(), n, dir);
    }
}

/* Add a driver: the host opens its file picker and copies the file in the
   background; gpuPoll takes the result */
static void gpuAdd(void)
{
    if (s_gpuPending || !s_gpuHostSet || !s_gpuHost.adreno()) {
        return;
    }
    if (s_gpuHost.installBegin() == 0) {
        s_gpuPending = 1;
    }
}

/* Add from the drivers folder: the host copies the zip in the background
   and gpuPoll takes the result, as for the picker */
static void gpuAddFile(void)
{
    if (s_gpuPending || !s_gpuHostSet || !s_gpuHost.adreno() || gpuFileCount() == 0) {
        return;
    }
    if (s_gpuHost.installFileBegin(gpuFileIndex()) == 0) {
        s_gpuPending = 1;
    }
}

static void gpuPoll(void)
{
    if (!s_gpuPending || !s_gpuHostSet) {
        return;
    }
    const int r = s_gpuHost.installPoll();
    if (r == UI_GPU_INSTALL_PENDING) {
        return;
    }
    s_gpuPending = 0;
    if (r == UI_GPU_INSTALL_ADDED) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_ADDED), "");
    } else if (r == UI_GPU_INSTALL_BAD) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_BAD), "");
    } else if (r == UI_GPU_INSTALL_NOSPACE) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_NOSPACE), "");
    } else if (r == UI_GPU_INSTALL_NO_PICKER) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_NO_PICKER), "");
    } else if (r == UI_GPU_INSTALL_NO_PICKER_APP) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_NO_PICKER_APP), "");
    } else if (r == UI_GPU_INSTALL_UNREADABLE) {
        ui_popup_push(ui_str(UI_STR_GPU_DRIVER_UNREADABLE), "");
    } /* cancelled: nothing to say */
}

/* Remove this driver: the built-in one is chosen first, then the host
   deletes the files */
static void gpuRemove(void)
{
    const int i = s_gpuHostSet ? gpuSelected() : -1;
    if (i < 0) {
        return;
    }
    s_gpuHost.select(-1);
    s_gpuHost.remove(i);
    s_dirtyConfig = 1;
    ui_popup_push(ui_str(UI_STR_GPU_DRIVER_REMOVED), "");
}

void ui_settings_set_gpu_driver_host(const UiGpuDriverHost *host)
{
    s_gpuHostSet = host != NULL && host->count != NULL && host->name != NULL &&
                   host->selected != NULL && host->select != NULL && host->lastFailed != NULL &&
                   host->adreno != NULL && host->installBegin != NULL &&
                   host->installPoll != NULL && host->remove != NULL && host->fileCount != NULL &&
                   host->fileName != NULL && host->installFileBegin != NULL;
    if (s_gpuHostSet) {
        s_gpuHost = *host;
    }
    s_gpuPending = 0;
}

/* Extras > Add to Steam: the host's job, and the popup for its answer */
static int (*s_steamFn)(void);

void ui_settings_set_steam_handler(int (*fn)(void))
{
    s_steamFn = fn;
}

/* Main > Save a picture for a bug report: the host's frame dump, armed */
static void (*s_frameDumpFn)(void);

void ui_settings_set_frame_dump_handler(void (*fn)(void))
{
    s_frameDumpFn = fn;
}

static void reportPicture(void)
{
    if (s_frameDumpFn == NULL) {
        return;
    }
    s_frameDumpFn();
    fprintf(stderr, "settings: a picture for a bug report in 3 s\n");
    ui_popup_push(ui_str(UI_STR_REPORT_PICTURE_ARMED), ui_str(UI_STR_REPORT_PICTURE_ARMED_BODY));
}

/* Main > Share a bug report (rc builds alone: ICO_RC_TOOLS; Android): the
   host's share menu of the report files.  A release keeps nothing here */
#if ICO_RC_TOOLS
static void (*s_shareReportFn)(void);

void ui_settings_set_share_report_handler(void (*fn)(void))
{
    s_shareReportFn = fn;
}

static void shareReport(void)
{
    if (s_shareReportFn == NULL) {
        return;
    }
    fprintf(stderr, "settings: share a bug report (an rc build's row)\n");
    s_shareReportFn();
}
#else
void ui_settings_set_share_report_handler(void (*fn)(void))
{
    (void)fn; /* no such row outside an rc build */
}
#endif

/* Main > Disc: the host's answer for each disc's game data, asked once */
static int (*s_discFn)(const char *archiveName);
static int s_discShown = -1;
static unsigned int s_discPresent; /* bit i: row i's game data is there (discRowShown) */

void ui_settings_set_disc_handler(int (*fn)(const char *archiveName))
{
    s_discFn = fn;
    s_discShown = -1;
}

/* the Disc row shows once two or more discs' game data are there (any two of
   config/regions.txt's rows); s_discPresent remembers which, for the step */
static int discRowShown(void)
{
    if (s_discFn == NULL) {
        return 0;
    }
    if (s_discShown < 0) {
        int n = 0;
        s_discPresent = 0;
        for (int i = 0; i < ico_region_count(); i++) {
            if (s_discFn(ico_region_at(i)->archive_name) != 0) {
                s_discPresent |= 1u << i;
                n++;
            }
        }
        s_discShown = n > 1;
    }
    return s_discShown;
}

/* the disc the row shows: [game] disc's, else the one playing (with several
   discs' game data and no pin the next start plays the disc played last,
   this one) */
static int discRow(void)
{
    const int pin = ico_config_disc_pin();
    return pin >= 0 ? pin : ico_region;
}

static void steamAdd(void)
{
    static const struct {
        UiStrId title, body;
    } kAnswers[] = {
        [UI_STEAM_ADDED] = {UI_STR_STEAM_ADDED, UI_STR_STEAM_ADDED_BODY},
        [UI_STEAM_EXISTS] = {UI_STR_STEAM_EXISTS, UI_STR_COUNT},
        [UI_STEAM_RUNNING] = {UI_STR_STEAM_RUNNING, UI_STR_STEAM_RUNNING_BODY},
        [UI_STEAM_NOT_FOUND] = {UI_STR_STEAM_NOT_FOUND, UI_STR_STEAM_NOT_FOUND_BODY},
        [UI_STEAM_NO_USER] = {UI_STR_STEAM_NO_USER, UI_STR_STEAM_NO_USER_BODY},
        [UI_STEAM_FAILED] = {UI_STR_STEAM_FAILED, UI_STR_STEAM_FAILED_BODY},
    };

    int r;

    if (s_steamFn == NULL) {
        return;
    }
    r = s_steamFn();
    if (r < 0 || r >= (int)N_OF(kAnswers)) {
        r = UI_STEAM_FAILED;
    }
    fprintf(stderr, "extras: add to steam: %d\n", r);
    ui_popup_push(ui_str(kAnswers[r].title),
                  kAnswers[r].body == UI_STR_COUNT ? "" : ui_str(kAnswers[r].body));
}

/* the Touch screen page's rows (and its link on Controls, optShown) */
static int isTouchOpt(int opt)
{
    return opt == UI_OPT_TOUCH_MODE || opt == UI_OPT_TOUCH_SIZE || opt == UI_OPT_TOUCH_OPACITY ||
           opt == UI_OPT_TOUCH_EDIT || opt == UI_OPT_TOUCH_SNAP || opt == UI_OPT_TOUCH_GRID ||
           opt == UI_OPT_TOUCH_RESET || opt == UI_OPT_TOUCH_RESET_ALL;
}

/* Edit layout opens the editor only while the overlay is on (Touch
   controls not Off) and the device layer has an editor */
/* the Touch controls mode in force: a phone never keeps Off (touch.h
   ico_touch_effective_mode; a gamepad may be connected, but the row's
   choices are the ones a phone can always get back from) */
static int touchModeInForce(void)
{
    return ico_touch_effective_mode(liveBindings()->touch_mode, 0, ico_video_android());
}

static int touchEditLocked(void)
{
    return touchModeInForce() == ICO_TOUCH_MODE_OFF || !touchEditHosted();
}

/* the Grid size row's position in ico_touch_edit_grids: the nearest of
   the three to the live value */
static int touchGridIndex(const IcoTouchEdits *e)
{
    int best = 0;
    for (int i = 1; i < N_OF(ico_touch_edit_grids); i++) {
        const float d = ico_touch_edit_grids[i] - e->grid,
                    db = ico_touch_edit_grids[best] - e->grid;
        if ((d < 0.0f ? -d : d) < (db < 0.0f ? -db : db)) {
            best = i;
        }
    }
    return best;
}

/* a table string with one %d (UI_STR_VAL_MM) and its number */
static const char *intText(UiStrId id, int v, char *buf, unsigned size)
{
    const char *t = ui_str(id);
    const char *at = strstr(t, "%d");
    if (at != NULL) {
        snprintf(buf, size, "%.*s%d%s", (int)(at - t), t, v, at + 2);
    } else {
        snprintf(buf, size, "%d %s", v, t);
    }
    return buf;
}

/* the Touch opacity row's steps, percent */
static const int kTouchOpacity[] = {25, 50, 75, 100};

/* a texture pack was found at start (texpack_count through the
   host's hook; none without one) */
static int texturePackInstalled(void)
{
    return s_texturePackCount != NULL && s_texturePackCount() > 0;
}

static int (*s_modelPackCount)(void); /* replacement models installed, when known */
static char s_version[32];            /* the package label, "": a development build */

/* a model pack was found at start (modelpack_count through the
   host's hook; none without one) */
static int modelPackInstalled(void)
{
    return s_modelPackCount != NULL && s_modelPackCount() > 0;
}

static const char *onOff(int v)
{
    return ui_str(v ? UI_STR_ON : UI_STR_OFF);
}

/* The game's own language (NonLinearCameraMove, 2..6), -1 on a disc that
   has none (port/core/region.h has_game_language: the Japanese and
   Korean ones) */
static int gameLanguage(void)
{
    if (ico_region_cur()->has_game_language) {
        return NonLinearCameraMove;
    }
    return -1;
}

static void setGameLanguage(int game)
{
    if (ico_region_cur()->has_game_language) {
        NonLinearCameraMove = game;
    }
}

/* a UiLang's own name (the Menu language row) */
static const char *menuLanguageName(int lang)
{
    static const int ids[UI_LANG_COUNT] = {UI_STR_LANG_EN, UI_STR_LANG_FR, UI_STR_LANG_DE,
                                           UI_STR_LANG_IT, UI_STR_LANG_ES, UI_STR_LANG_JA,
                                           UI_STR_LANG_KO};
    return ui_str((UiStrId)ids[lang >= 0 && lang < UI_LANG_COUNT ? lang : 0]);
}

void ui_settings_apply_language(void)
{
    /* the port's text: [ui] language, else the disc's (ui_lang.c) */
    ui_set_language(ui_settings_resolve_language());
    /* the look of the port's words over the disc's sheets: the sheets the
       disc's region row names (port/core/region.h menu_sheet: the Japanese
       disc's, menu_font.c kSheetInk[UI_SHEET_JA]), else the game's own
       language's (the European disc's five sheets), whatever language the
       port's text is in */
    const int sheet = ico_region_cur()->menu_sheet;
    ui_set_sheet_language(sheet >= 0            ? sheet
                          : gameLanguage() >= 0 ? (int)ui_lang_from_game(gameLanguage())
                                                : (int)UI_LANG_EN);
}

static const char *languageName(int game)
{
    static const int ids[5] = {UI_STR_LANG_EN, UI_STR_LANG_FR, UI_STR_LANG_DE, UI_STR_LANG_IT,
                               UI_STR_LANG_ES};
    int i = game - ICO_GAME_LANGUAGE_ENGLISH;
    return ui_str((UiStrId)ids[i >= 0 && i < 5 ? i : 0]);
}

/* the game's brightness step as gsb_controlBrightness clamps it */
static int brightness(void)
{
    const int v = systemStatus[11];
    return v < 0 ? 0 : v > BRIGHTNESS_MAX ? BRIGHTNESS_MAX : v;
}

/* The Resolution row's cycle: Window, then these scales, then Auto.  A
   scale from the file that is not in the list (5x, 7x) or a WxH reads -1. */
static const int kResScales[] = {1, 2, 3, 4, 6, 8, 12, 16};
#define RES_STEPS ((int)N_OF(kResScales))

/* the position in the cycle: 0 Window, 1..RES_STEPS the scales, -1 neither */
static int resolutionIndex(const IcoVideoOptions *o)
{
    for (int i = 0; i < RES_STEPS; i++) {
        if (o->resScale == kResScales[i]) {
            return i + 1;
        }
    }
    if (o->resW == 0 && o->resScale == 0) {
        return 0;
    }
    return -1; /* a WxH or another N from the file */
}

/* ------------------------------------------------------------- Extras
 * Settings > Extras.  Music, Models and Credits leave the stage, so they
 * show from the title only (masked and skipped by the cursor from the
 * pause menu); each row's hook returns the layout to open, or -1 when it
 * cannot open (not built, off the title, locked: a log line, nothing
 * else).  Characters (the characters' colours) shows from both
 * entries, so the pause menu has the Extras row too, with Characters and
 * Back on its page. */

static int isExtrasOpt(int opt)
{
    return opt == UI_OPT_EXTRAS_MUSIC || opt == UI_OPT_EXTRAS_MODELS ||
           opt == UI_OPT_EXTRAS_CREDITS;
}

/* true once the ending has been reached: the port's ending
   achievement or its clear count, or [dev] unlock_credits (credits.c,
   ico_credits_unlocked) */
static int creditsUnlocked(void)
{
    return ico_credits_unlocked();
}

/* the music gallery's page (gallery.h) */
static int extrasMusic(void)
{
    if (!s_built || s_pages[UI_PAGE_MUSIC].layout < 0) {
        return -1;
    }
    gallery_enter();
    return s_pages[UI_PAGE_MUSIC].layout;
}

/* the model viewer's list (port/game/model_viewer.c registers
   it), -1 without one */
static int (*s_modelsEnter)(void);
static int (*s_windowModeQuery)(void);  /* the window's truth, when installed */
static unsigned (*s_aaMaskQuery)(void); /* the Anti-aliasing list, when installed */

/* the RdAntiAlias values the row offers, a bit each (Off always) */
static unsigned aaMask(void)
{
    unsigned m;
    if (s_aaMaskQuery) {
        m = s_aaMaskQuery();
    } else {
#ifdef ICO_RD
        m = rd_aa_supported_mask();
#else
        m = (1u << ICO_AA_OFF) | (1u << ICO_AA_SSAA2) | (1u << ICO_AA_SSAA4);
#endif
    }
    return m | (1u << ICO_AA_OFF);
}

/* the Anti-aliasing row is the Enhanced presets': under Original it stays
   Off and does not step (ico_video_anti_alias) */
static int aaLocked(void)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    return ico_video_preset(&o) == ICO_VIDEO_ORIGINAL;
}

/* the order the row steps through (the numbers are the enum's, shared with
   the renderer and the saved options, and do not follow it) */
static const int kAaOrder[ICO_AA_COUNT] = {ICO_AA_OFF,   ICO_AA_MSAA2, ICO_AA_MSAA4,
                                           ICO_AA_MSAA8, ICO_AA_SSAA2, ICO_AA_SSAA4};

static int (*s_aaBlockedQuery)(void);

static int aaBlocked(void)
{
    if (s_aaBlockedQuery) {
        return s_aaBlockedQuery();
    }
#ifdef ICO_RD
    return rd_aa_msaa_blocked();
#else
    return 0;
#endif
}

/* the value the row shows and steps from: the stored one (Off under
   Original, ico_video_anti_alias), whether the renderer draws it or not */
static int aaStored(void)
{
    const int aa = ico_video_anti_alias();
    return aa <= ICO_AA_OFF || aa >= ICO_AA_COUNT ? ICO_AA_OFF : aa;
}

/* whether the stored value is the one drawn: not when the device cannot
   do it, or can but no depth path works with it (MSAA) */
static int aaInForce(void)
{
    const int aa = aaStored();
    if (aa != ICO_AA_OFF && !(aaMask() & (1u << aa))) {
        return 0;
    }
    return !(aa >= ICO_AA_MSAA2 && aa <= ICO_AA_MSAA8 && aaBlocked());
}

void ui_settings_set_models_handler(int (*fn)(void))
{
    s_modelsEnter = fn;
}

static int extrasModels(void)
{
    return s_modelsEnter != NULL ? s_modelsEnter() : -1;
}

/* the credits: the ending from the staff roll's first scene
   (ico_credits.h).  The menu closes on the game's empty layout while the
   stage changes, as leaving it saves first;
   the title comes back with the cursor on Settings.  Locked: nothing. */
static void gameCursorOn(int to, int row);

static int extrasCredits(void)
{
    if (!creditsUnlocked()) {
        fprintf(stderr, "credits: locked (the ending has not been reached)\n");
        return -1;
    }
    if (!onTitle()) {
        return -1;
    }
    ui_settings_save();
    int to = ico_credits_start();
    if (to < 0) {
        return -1;
    }
    la_host_leave();
    gameCursorOn(s_origin,
                 s_entryRow[s_origin == LAYOUT_TITLE_NEW ? ENTRY_TITLE13 : ENTRY_TITLE12]);
    return to;
}

static int extrasOpen(int opt)
{
    static const struct {
        int opt;
        const char *name;
        int (*open)(void);
    } kExtras[] = {{UI_OPT_EXTRAS_MUSIC, "music", extrasMusic},
                   {UI_OPT_EXTRAS_MODELS, "models", extrasModels},
                   {UI_OPT_EXTRAS_CREDITS, "credits", extrasCredits}};

    for (int i = 0; i < N_OF(kExtras); i++) {
        if (kExtras[i].opt == opt) {
            int to = kExtras[i].open();
            /* a locked Credits press has logged "credits: locked" */
            if (to < 0 && !(opt == UI_OPT_EXTRAS_CREDITS && !creditsUnlocked())) {
                fprintf(stderr, "extras: %s not available\n", kExtras[i].name);
            }
            return to;
        }
    }
    return -1;
}

/* --------------------------------------------------------- Characters
 * Settings > Extras > Characters, the characters' colours
 * (port/game/appearance.h): a stepped row per part (UI_OPT_CHAR_ICO_SKIN +
 * the part), its value Original, a palette colour's name or "Tone N", and
 * a swatch right of its arrows (the part's colour as the texture holds it,
 * before lighting); Randomize and Reset to original; Square on a row is
 * Original.  From the pause menu the scene behind the menu changes as the
 * colours do.  From the title the page runs inside the model viewer
 * (ui_settings_set_characters_host, port/game/model_viewer.c): Extras'
 * Characters row loads Ico's model, and the page becomes a panel at the
 * left of the screen (charactersPlace), the model on the right, with a
 * Switch row (and L1 / R1) for the other character and a line of button
 * prompts; Triangle goes back to the title, which opens Settings again on
 * Extras.  There the panel shows only the rows of the character on screen
 * and Randomize and Reset change only that character; the pause menu's
 * page has both characters' rows and its Randomize and Reset change both.
 * Skin rows step through the tones, then the colours.  The values and the
 * steps are the same code in both places.
 * One note row under the page, its text chosen at each refresh: the
 * title's, the pause menu's, or the texture pack's while a pack is on
 * (the pack's pictures replace the recoloured ones); in the viewer only
 * the texture pack's shows. */

_Static_assert(UI_OPT_CHAR_YORDA_DRESS - UI_OPT_CHAR_ICO_SKIN + 1 == ICO_APP_PART_COUNT,
               "a Characters row for each part, in IcoAppPart's order");
_Static_assert(UI_STR_COLOUR_BLACK - UI_STR_COLOUR_RED + 1 == ICO_APP_COLOURS,
               "a name for each palette colour, in the palette's order");

/* the swatch geometry: right of the value's right arrow (it ends at x 570),
   half the row's 36-unit box tall; the lettering sits in the box's top half, so
   the swatch starts at the row's y to share the letters' middle */
#define SWATCH_X 578
#define SWATCH_W 36
#define SWATCH_H 18
#define SWATCH_DY 0

/* Inside the model viewer (title): the page as a panel at the left, the
   model at the right of it (model_viewer.c's characters shift): the
   heading over the panel, the labels right-aligned to x 162, the value
   between its arrows to x 314, the swatch to x 346, on the letters'
   middle; a smaller em on the closer pitch; the rows of the character on
   screen only (optShown), Back after the last; the note (the texture
   pack's) at 176 across the screen, the prompts under it */
#define CV_ROW_Y0 30
#define CV_ROW_PITCH 11
#define CV_HEADER_X 16
#define CV_HEADER_Y 8
#define CV_HEADER_W 332
#define CV_HEADER_SIZE 26.0f
#define CV_LABEL_X 16
#define CV_LABEL_W 146
#define CV_ARROW_L_X 172
#define CV_STEP_X 184
#define CV_STEP_W 116
#define CV_ARROW_R_X 302
#define CV_ARROW_W 12
#define CV_SWATCH_X 322
#define CV_SWATCH_W 24
#define CV_SWATCH_H 16
#define CV_SWATCH_DY 3
#define CV_SIZE 20.0f
#define CV_NOTE_Y 176

static int s_charSwatch[ICO_APP_PART_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
static UiCharactersHost s_charsHost; /* model_viewer.c's calls, all NULL without one */
static UiHint s_charHint;            /* the viewer's prompts, under the panel */

static int isCharOpt(int opt)
{
    return opt >= UI_OPT_CHAR_ICO_SKIN && opt <= UI_OPT_CHAR_YORDA_DRESS;
}

static IcoAppPart charPart(int opt)
{
    return (IcoAppPart)(opt - UI_OPT_CHAR_ICO_SKIN);
}

void ui_settings_set_characters_host(const UiCharactersHost *host)
{
    if (host != NULL) {
        s_charsHost = *host;
    } else {
        memset(&s_charsHost, 0, sizeof(s_charsHost));
    }
}

static int charsHosted(void)
{
    return s_charsHost.enter != NULL && s_charsHost.shown != NULL && s_charsHost.switchTo != NULL &&
           s_charsHost.leave != NULL;
}

/* the character the viewer shows (0 Ico, 1 Yorda), -1 while it loads or
   leaves, -2 when it is not running Characters */
static int charsShown(void)
{
    return charsHosted() ? s_charsHost.shown() : -2;
}

int ui_settings_characters_in_viewer(void)
{
    return charsShown() != -2;
}

/* The character whose rows the viewer's panel shows: the one on screen,
   or, while a model loads, the one asked for (the rows change with the
   Switch press, not a second later); -1 outside the viewer (the pause
   menu's page shows both characters' rows). */
static int s_charsRows;

static int charsRowsFor(void)
{
    const int shown = charsShown();
    if (shown == -2) {
        return -1;
    }
    if (shown >= 0) {
        s_charsRows = shown;
    }
    return s_charsRows;
}

/* Original, the colour's name, or the skin tone ("Tone %d" with its
   number put where the translation has %d); a skin part has the tones
   first, then the colours */
static const char *charValue(int opt, char *buf, unsigned size)
{
    const IcoAppPart p = charPart(opt);
    const int v = ico_appearance_get(p);
    if (v <= 0 || v >= ico_appearance_choices(p)) {
        return ui_str(UI_STR_VAL_ORIGINAL);
    }
    const int tones = ico_appearance_is_skin(p) ? ICO_APP_TONES : 0;
    if (v <= tones) {
        const char *t = ui_str(UI_STR_CHAR_TONE);
        const char *at = strstr(t, "%d");
        if (at != NULL) {
            snprintf(buf, size, "%.*s%d%s", (int)(at - t), t, v, at + 2);
        } else {
            snprintf(buf, size, "%s %d", t, v);
        }
        return buf;
    }
    return ui_str((UiStrId)(UI_STR_COLOUR_RED + v - tones - 1));
}

/* the swatch's colour for lt_ext_add_rect / lt_ext_set_rect_color: the
   part's 8-bit colour at the GS scale (0x80 = 1.0), opaque */
static void swatchColour(IcoAppPart p, unsigned char rgba[4])
{
    const unsigned c = ico_appearance_swatch(p);
    rgba[0] = (unsigned char)(((c >> 16) & 0xFFu) * 0x80u / 255u);
    rgba[1] = (unsigned char)(((c >> 8) & 0xFFu) * 0x80u / 255u);
    rgba[2] = (unsigned char)((c & 0xFFu) * 0x80u / 255u);
    rgba[3] = 0x80;
}

/* Extras' Characters row from the title: the viewer loads Ico's model and
   the page opens as its panel.  What changed is written first (the stage
   changes); -1 (a log line) when the viewer cannot start. */
static int pageFirstNav(Page *pg);

static int charsEnter(void)
{
    if (!charsHosted() || s_pages[UI_PAGE_CHARACTERS].layout < 0) {
        fprintf(stderr, "appearance: the viewer did not open (%s)\n",
                charsHosted() ? "the page was not built" : "no viewer");
        return -1;
    }
    ui_settings_save();
    s_charsRows = 0; /* Ico first */
    if (s_charsHost.enter() != 0) {
        fprintf(stderr, "appearance: the viewer did not open\n");
        return -1;
    }
    la_host_leave();
    Page *pg = &s_pages[UI_PAGE_CHARACTERS];
    lt_ext_layout(pg->layout)->defaultItem = pageFirstNav(pg);
    return pg->layout;
}

static const char *rawValue(int opt, char *buf, unsigned size)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    if (isCharOpt(opt)) {
        return charValue(opt, buf, size);
    }
    switch (opt) {
    case UI_OPT_PRESET: {
        static const int presetStr[3] = {UI_STR_VAL_ORIGINAL, UI_STR_VAL_ENHANCED,
                                         UI_STR_VAL_CUSTOM};
        return ui_str(presetStr[ico_video_preset(&o)]);
    }
    case UI_OPT_RESOLUTION:
        if (crtForcesNative(&o)) {
            return "1x (CRT)";
        }
        if (o.resScale == ICO_RES_AUTO) {
            /* "Auto", with the scale once the window lowered it */
            if (ico_video_auto_scale() > 0) {
                snprintf(buf, size, "%s (%dx)", ui_str(UI_STR_VAL_AUTO), ico_video_auto_scale());
                return buf;
            }
            return ui_str(UI_STR_VAL_AUTO);
        }
        if (resolutionIndex(&o) == 0) {
            return ui_str(UI_STR_VAL_WINDOW);
        }
#ifdef ICO_RD
        if (o.resScale > 0 && rd_scene_scale_lowered() > 0 &&
            rd_scene_scale_lowered() < o.resScale) {
            /* the graphics card could not hold it: the scale in use after it */
            snprintf(buf, size, "%dx (%dx)", o.resScale, rd_scene_scale_lowered());
            return buf;
        }
#endif
        return ico_video_resolution_name(&o, buf, size);
    case UI_OPT_ASPECT:
        return o.aspect == ICO_ASPECT_AUTO ? ui_str(UI_STR_VAL_AUTO)
                                           : ico_video_aspect_name(o.aspect);
    case UI_OPT_ANTI_ALIAS: {
        static const int aaStr[ICO_AA_COUNT] = {UI_STR_AA_OFF,   UI_STR_AA_SSAA2, UI_STR_AA_SSAA4,
                                                UI_STR_AA_MSAA2, UI_STR_AA_MSAA4, UI_STR_AA_MSAA8};
        const int aa = aaStored();
        if (aaInForce()) {
            return ui_str(aaStr[aa]);
        }
        snprintf(buf, size, "%s%s", ui_str(aaStr[aa]), ui_str(UI_STR_AA_UNAVAILABLE));
        return buf;
    }
    case UI_OPT_WINDOW_MODE: {
        /* the window's own answer when the host installed one */
        const int m = s_windowModeQuery ? s_windowModeQuery() : o.windowMode;

        return ui_str(m == ICO_WINDOW_BORDERLESS   ? UI_STR_VAL_BORDERLESS
                      : m == ICO_WINDOW_FULLSCREEN ? UI_STR_OPT_FULLSCREEN
                                                   : UI_STR_VAL_WINDOWED);
    }
    case UI_OPT_VSYNC:
        return onOff(o.vsync);
    case UI_OPT_FILTER:
        return ui_str(o.filter == ICO_FILTER_TRILINEAR     ? UI_STR_VAL_TRILINEAR
                      : o.filter == ICO_FILTER_ANISOTROPIC ? UI_STR_VAL_ANISOTROPIC
                                                           : UI_STR_VAL_ORIGINAL);
    case UI_OPT_FULL_HEIGHT:
        return onOff(o.fullHeight);
    case UI_OPT_TEXTURE_PACK:
        /* "None installed" while no pack is found (the row then
           does not step) */
        return texturePackInstalled() ? onOff(o.texturePack) : ui_str(UI_STR_VAL_NONE_INSTALLED);
    case UI_OPT_DUMP_TEXTURES:
        return onOff(o.dumpTextures);
    case UI_OPT_MODEL_PACK:
        return modelPackInstalled() ? onOff(o.modelPack) : ui_str(UI_STR_VAL_NONE_INSTALLED);
    case UI_OPT_DUMP_MODELS:
        return onOff(o.dumpModels);
    case UI_OPT_EFFECT_GLOW:
        return onOff(o.effectGlow);
    case UI_OPT_EFFECT_DEPTH_OF_FIELD:
        return onOff(o.effectDepthOfField);
    case UI_OPT_EFFECT_SOFTENING:
        return onOff(o.effectSoftening);
    case UI_OPT_EFFECT_MOTION_BLUR:
        /* Off, Original (the game's own, the default) or Smooth */
        return !o.effectMotionBlur        ? ui_str(UI_STR_OFF)
               : o.effectMotionBlurSmooth ? ui_str(UI_STR_VAL_SMOOTH)
                                          : ui_str(UI_STR_VAL_ORIGINAL);
    case UI_OPT_EFFECT_FOG:
        return onOff(o.effectFog);
    case UI_OPT_EFFECT_WATER:
        return ui_str(o.effectWaterSharp ? UI_STR_VAL_SHARP : UI_STR_VAL_ORIGINAL);
    case UI_OPT_EFFECT_CINEMATIC_BARS:
        return onOff(o.effectCinematicBars);
    case UI_OPT_FULL_PIXEL:
        return onOff(o.fullPixel);
    case UI_OPT_FRAMERATE:
        /* the option as set, in force in both presets */
        if (o.framerate == ICO_FRAMERATE_ORIGINAL) {
            return ui_str(UI_STR_VAL_ORIGINAL);
        }
        if (o.framerate == ICO_FRAMERATE_UNCAPPED) {
            return ui_str(UI_STR_VAL_UNCAPPED);
        }
        snprintf(buf, size, "%d %s", o.framerate, ui_str(UI_STR_FPS_UNIT));
        return buf;
    case UI_OPT_CRT:
        /* [video] crt and crt_mode as one value */
        return ui_str(
            !o.crt ? UI_STR_OFF
                   : kCrtStr[o.crtMode >= 0 && o.crtMode < ICO_CRT_MODES ? o.crtMode
                                                                         : ICO_CRT_CONSUMER]);
    case UI_OPT_CRT_STRENGTH:
        snprintf(buf, size, "%d %%", (int)(o.crtStrength * 100.0f + 0.5f));
        return buf;
    case UI_OPT_BRIGHTNESS:
        if (brightness() == BRIGHTNESS_DEFAULT) {
            snprintf(buf, size, "%d (%s)", brightness(), ui_str(UI_STR_VAL_DEFAULT));
        } else {
            snprintf(buf, size, "%d", brightness());
        }
        return buf;
    case UI_OPT_VIDEO_MODE:
        if (!onTitle()) {
            snprintf(buf, size, "%s (%s)",
                     ui_str(systemStatus[0] != 0 ? UI_STR_VAL_PAL50 : UI_STR_VAL_60HZ),
                     ui_str(UI_STR_VIDEO_MODE_TITLE_ONLY));
            return buf;
        }
        return ui_str(systemStatus[0] != 0 ? UI_STR_VAL_PAL50 : UI_STR_VAL_60HZ);
    case UI_OPT_VOLUME: {
        snprintf(buf, size, "%d %%", (int)(unit01("audio.volume") * 100.0 + 0.5));
        return buf;
    }
    case UI_OPT_MUSIC:
    case UI_OPT_EFFECTS:
        snprintf(
            buf, size, "%d %%",
            (int)(unit01(opt == UI_OPT_MUSIC ? "audio.music" : "audio.effects") * 100.0 + 0.5));
        return buf;
    case UI_OPT_OUTPUT: {
        /* Auto shows the game's mode in force */
        int live = soundOutputModeGet() == 1 ? UI_STR_VAL_MONO : UI_STR_VAL_STEREO;
        if (ico_opt_output_mode() == ICO_OUTPUT_AUTO) {
            snprintf(buf, size, "%s (%s)", ui_str(UI_STR_VAL_AUTO), ui_str((UiStrId)live));
            return buf;
        }
        return ui_str(ico_opt_output_mode() == ICO_OUTPUT_MONO ? UI_STR_VAL_MONO
                                                               : UI_STR_VAL_STEREO);
    }
    case UI_OPT_DEVICE:
        return deviceText(buf, size);
    case UI_OPT_GPU_DRIVER:
        return s_gpuHostSet ? gpuText(buf, size) : ui_str(UI_STR_VAL_GPU_BUILTIN);
    case UI_OPT_GPU_ADD_FILE:
        return gpuFileCount() > 0 ? gpuFileText(buf, size) : "";
    case UI_OPT_GRAPHICS_API:
        return backendText(buf, size);
    case UI_OPT_STICK_FIX:
        return onOff(ico_opt_stick_fix());
    case UI_OPT_ACH_POPUPS:
        return onOff(ico_ach_popups_enabled());
    case UI_OPT_MOUSE_SENS:
        snprintf(buf, size, "%.2f", (double)liveBindings()->mouse_sens);
        return buf;
    case UI_OPT_MOUSE_CAMERA:
        return onOff(liveBindings()->mouse_camera);
    case UI_OPT_MOUSE_INVERT:
        return onOff(liveBindings()->mouse_invert_y);
    case UI_OPT_MOUSE_SPEED: {
        const float v = liveBindings()->mouse_camera_speed;
        if (v >= kMouseCamSpeed[N_OF(kMouseCamSpeed) - 1]) {
            return ui_str(UI_STR_VAL_INSTANT);
        }
        snprintf(buf, size, "%.1fx", (double)v);
        return buf;
    }
    case UI_OPT_MOUSE_RANGE:
        return ui_str(liveBindings()->mouse_full_range ? UI_STR_VAL_RANGE_FULL
                                                       : UI_STR_VAL_RANGE_NORMAL);
    case UI_OPT_MOUSE_RETURN:
        return onOff(liveBindings()->mouse_return);
    case UI_OPT_CIRCLE_BACK:
        return onOff(ico_opt_circle_back());
    case UI_OPT_VIBRATION:
        return onOff(iosPadActRequestEnable);
    case UI_OPT_HOLD_TYPE:
        return ui_str(ico_opt_hold_type() == ICO_HOLD_TOGGLE ? UI_STR_VAL_HOLD_B
                                                             : UI_STR_VAL_HOLD_A);
    case UI_OPT_TOUCH_MODE: {
        static const int modeStr[3] = {UI_STR_OFF, UI_STR_VAL_AUTO, UI_STR_VAL_ALWAYS};
        const int m = touchModeInForce();
        return ui_str(modeStr[m >= 0 && m < 3 ? m : 1]);
    }
    case UI_OPT_TOUCH_SIZE: {
        static const int sizeStr[3] = {UI_STR_VAL_SMALL, UI_STR_VAL_MEDIUM, UI_STR_VAL_LARGE};
        const int z = liveBindings()->touch_size;
        return ui_str(sizeStr[z >= 0 && z < 3 ? z : 1]);
    }
    case UI_OPT_TOUCH_OPACITY:
        snprintf(buf, size, "%d %%", liveBindings()->touch_opacity);
        return buf;
    case UI_OPT_TOUCH_SNAP:
        return onOff(ico_input_live_touch_edits()->snap);
    case UI_OPT_TOUCH_GRID: {
        const IcoTouchEdits *e = ico_input_live_touch_edits();
        return intText(UI_STR_VAL_MM, (int)(ico_touch_edit_grids[touchGridIndex(e)] + 0.5f), buf,
                       size);
    }
    case UI_OPT_FILM_EFFECT:
        if (optionScreenMode <= 0 || optionScreenMode >= FILM_EFFECTS) {
            return ui_str(UI_STR_OFF);
        }
        snprintf(buf, size, "%d", optionScreenMode);
        return buf;
    case UI_OPT_PLAYERS:
        return girlControlMode != 0 ? "2" : "1";
    case UI_OPT_YORDA:
        return onOff(ico_opt_yorda_safe());
    case UI_OPT_DOOR_FIX:
        return onOff(ico_opt_door_fix());
    case UI_OPT_NATURAL_CHAINS:
        return onOff(ico_opt_natural_chains());
    case UI_OPT_LANGUAGE:
        return languageName(gameLanguage());
    case UI_OPT_MENU_LANGUAGE: {
        const int pref = ico_opt_ui_language();
        return pref < 0 ? ui_str(UI_STR_VAL_AUTO) : menuLanguageName(pref);
    }
    case UI_OPT_DISC: {
        static const UiStrId kDisc[ICO_REGION_COUNT] = {UI_STR_DISC_PAL, UI_STR_DISC_JPN,
                                                        UI_STR_DISC_KOR};
        _Static_assert(ICO_REGION_COUNT == 3 && ICO_REGION_PAL == 0 && ICO_REGION_JPN == 1 &&
                           ICO_REGION_KOR == 2,
                       "a Disc value for each row of config/regions.txt");
        return ui_str(kDisc[discRow()]);
    }
    case UI_OPT_DEVELOPER:
        return onOff(ico_opt_developer_mode());
    case UI_OPT_EXTRAS_CREDITS:
        return creditsUnlocked() ? "" : ui_str(UI_STR_ACH_LOCKED);
    default:
        return "";
    }
}

/* the game's settings, shown from the pause menu only (as its Options
   screen was: a load sets them from the save, so a change on the title
   would not last); the film effect and players once the game is cleared
   (layout_texture.c lt_property_visible).  Hold type is not one: it is
   kept in [gameplay] hold_type, so it shows on the title too. */
static int isGameOpt(int opt)
{
    return opt == UI_OPT_BRIGHTNESS || opt == UI_OPT_VIBRATION || opt == UI_OPT_BUTTON_CONFIG ||
           opt == UI_OPT_FILM_EFFECT || opt == UI_OPT_PLAYERS;
}

static int optShown(int opt, int link)
{
    if (opt == UI_OPT_CIRCLE_BACK) {
        /* the European disc's: Circle confirms on the Japanese and Korean ones */
        return lt_ext_has_circle_back();
    }
    if (opt == UI_OPT_LINK && link == UI_PAGE_GPU_DRIVER) {
        /* only where a host answers for the driver, and only on an Adreno
           chip, the one kind a driver package exists for (the page is
           hidden elsewhere) */
        return s_gpuHostSet && s_gpuHost.adreno() != 0;
    }
    if (opt == UI_OPT_LINK && link == UI_PAGE_GRAPHICS) {
        return s_backendCount >= 2; /* one renderer: nothing to choose */
    }
    if (opt == UI_OPT_LINK && link == UI_PAGE_TOUCH) {
        return touchPresent(); /* Controls > Touch screen */
    }
    if (opt == UI_OPT_GPU_ADD) {
        return s_gpuHostSet && s_gpuHost.adreno() != 0;
    }
    if (opt == UI_OPT_GPU_ADD_FILE) {
        return s_gpuHostSet && s_gpuHost.adreno() != 0 && gpuFileCount() > 0;
    }
    if (opt == UI_OPT_GPU_REMOVE) {
        return s_gpuHostSet && gpuSelected() >= 0;
    }
    if (isExtrasOpt(opt)) {
        return onTitle(); /* the Extras row itself shows from both */
    }
    if (opt == UI_OPT_EXTRAS_STEAM) {
        return onTitle() && s_steamFn != NULL;
    }
    if (opt == UI_OPT_CHAR_SWITCH) {
        return ui_settings_characters_in_viewer();
    }
    if (isCharOpt(opt)) {
        /* in the viewer only the rows of the character shown */
        const int c = charsRowsFor();
        return c < 0 || ico_appearance_character(charPart(opt)) == c;
    }
    if (isGameOpt(opt) && onTitle()) {
        return 0;
    }
    if (opt == UI_OPT_FILM_EFFECT || opt == UI_OPT_PLAYERS) {
        return gFlagGameClear != 0;
    }
    if (opt == UI_OPT_DUMP_TEXTURES || opt == UI_OPT_DUMP_MODELS) {
        return ico_opt_developer_mode(); /* for pack authors */
    }
    if (opt == UI_OPT_REPORT_PICTURE) {
        /* the window build, developer mode or not: a player's report row */
        return s_frameDumpFn != NULL;
    }
#if ICO_RC_TOOLS
    if (opt == UI_OPT_SHARE_REPORT) {
        /* an rc build on a phone: the files folder is out of reach there */
        return ico_video_android() && s_shareReportFn != NULL;
    }
#endif
    if (isTouchOpt(opt)) {
        return touchPresent(); /* a phone, a tablet, a touch screen */
    }
    if (opt == UI_OPT_WINDOW_MODE) {
        return !ico_video_android(); /* the phone's window is the screen */
    }
    if (opt == UI_OPT_MOUSE_CAMERA || opt == UI_OPT_MOUSE_SENS || opt == UI_OPT_MOUSE_INVERT ||
        opt == UI_OPT_MOUSE_SPEED || opt == UI_OPT_MOUSE_RANGE || opt == UI_OPT_MOUSE_RETURN) {
        return !ico_video_android(); /* no mouse camera on a phone */
    }
    if (opt == UI_OPT_VIDEO_MODE && !ico_region_cur()->has_50hz_row) {
        return 0; /* the Japanese and Korean discs are 60 Hz only */
    }
    if (opt == UI_OPT_DISC) {
        return discRowShown(); /* two or more discs' game data */
    }
    if (opt == UI_OPT_LANGUAGE) {
        /* the game's own language: the European disc's five */
        return ico_region_cur()->has_game_language;
    }
    if (opt == UI_OPT_VIDEO_MODE || opt == UI_OPT_MODEL_PACK) {
        /* it changes only from the title (onTitle); the pause menu's
           Display page has no room for a row that cannot step once Texture
           pack is there (fourteen rows do not fit 13 lines apart); Model
           pack is the title's too, for the same reason */
        return onTitle();
    }
    return 1;
}

int ui_settings_opt_shown(UiSettingsOpt opt)
{
    return optShown((int)opt, -1);
}

static int steppable(int opt)
{
    return (opt >= UI_OPT_PRESET && opt <= UI_OPT_DEVELOPER) /* includes the Effects rows */ ||
           opt == UI_OPT_GPU_DRIVER || opt == UI_OPT_GPU_ADD_FILE || opt == UI_OPT_GRAPHICS_API;
}

static int resolutionLocked(void)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    return crtForcesNative(&o);
}

/* a press changes the option now: the video mode only from the title, the
   resolution not under the CRT filter */
static int canStep(int opt)
{
    return steppable(opt) && (opt != UI_OPT_VIDEO_MODE || onTitle()) && optShown(opt, -1) &&
           (opt != UI_OPT_RESOLUTION || !resolutionLocked()) &&
           (opt != UI_OPT_ANTI_ALIAS || !aaLocked()) &&
           (opt != UI_OPT_TEXTURE_PACK || texturePackInstalled()) &&
           (opt != UI_OPT_MODEL_PACK || modelPackInstalled());
}

const char *ui_settings_value_text(UiSettingsOpt opt)
{
    char raw[sizeof(s_text)];
    snprintf(s_text, sizeof(s_text), "%s", rawValue(opt, raw, sizeof(raw)));
    return s_text;
}

/* The display rows that are a plain On/Off switch: their field in o, else
   NULL. */
static int *videoSwitch(IcoVideoOptions *o, UiSettingsOpt opt)
{
    switch (opt) {
    case UI_OPT_VSYNC:
        return &o->vsync;
    case UI_OPT_FULL_HEIGHT:
        return &o->fullHeight;
    case UI_OPT_DUMP_TEXTURES:
        return &o->dumpTextures;
    case UI_OPT_DUMP_MODELS:
        return &o->dumpModels;
    case UI_OPT_EFFECT_GLOW:
        return &o->effectGlow;
    case UI_OPT_EFFECT_DEPTH_OF_FIELD:
        return &o->effectDepthOfField;
    case UI_OPT_EFFECT_SOFTENING:
        return &o->effectSoftening;
    case UI_OPT_EFFECT_FOG:
        return &o->effectFog;
    case UI_OPT_EFFECT_WATER:
        return &o->effectWaterSharp;
    case UI_OPT_EFFECT_CINEMATIC_BARS:
        return &o->effectCinematicBars;
    case UI_OPT_FULL_PIXEL:
        return &o->fullPixel;
    default:
        return NULL;
    }
}

void ui_settings_step(UiSettingsOpt opt, int dir)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    int video = 0;
    if (isCharOpt(opt)) {
        /* Original, then the colours or tones, around; the
           textures follow within a frame (Texture.c reads the serial) */
        const IcoAppPart p = charPart(opt);
        ico_appearance_set(p, stepIndex(ico_appearance_get(p), ico_appearance_choices(p), dir));
        s_dirtyConfig = 1;
        return;
    }
    switch (opt) {
    case UI_OPT_PRESET:
        /* the shortcut: the two named presets toggle; Custom goes Enhanced on
           Right and Original on Left.  It writes the four rows even while
           the CRT filter locks Resolution (crtForcesNative) */
        {
            int cur = ico_video_preset(&o);
            ico_video_set_preset(
                &o, cur == ICO_VIDEO_CUSTOM
                        ? (dir > 0 ? ICO_VIDEO_ENHANCED : ICO_VIDEO_ORIGINAL)
                        : (cur == ICO_VIDEO_ENHANCED ? ICO_VIDEO_ORIGINAL : ICO_VIDEO_ENHANCED));
        }
        video = 1;
        break;
    case UI_OPT_RESOLUTION: {
        if (crtForcesNative(&o)) {
            return; /* 1x while the CRT filter is on */
        }
        /* Window, the scales, then Auto (index RES_STEPS + 1) */
        int i = o.resScale == ICO_RES_AUTO ? RES_STEPS + 1 : resolutionIndex(&o);
        if (i < 0 && o.resW == 0 && o.resH == 0 && o.resScale > 0) {
            /* a scale from the file that is not in the list (5x, 9x): the
               nearest entry in the direction pressed */
            i = dir > 0 ? RES_STEPS + 1 : 0;
            for (int k = 0; k < RES_STEPS; k++) {
                if (dir > 0 && kResScales[k] > o.resScale) {
                    i = k + 1;
                    break;
                }
                if (dir < 0 && kResScales[k] < o.resScale) {
                    i = k + 1;
                }
            }
        } else if (i < 0) {
            i = dir > 0 ? 0 : RES_STEPS; /* a WxH: Window on Right, the top scale on Left */
        } else {
            i = stepIndex(i, RES_STEPS + 2, dir);
        }
        o.resW = o.resH = 0;
        o.resScale = i == RES_STEPS + 1 ? ICO_RES_AUTO : (i == 0 ? 0 : kResScales[i - 1]);
        video = 1;
        break;
    }
    case UI_OPT_ASPECT:
        o.aspect = stepIndex(o.aspect, ICO_ASPECT_COUNT, dir);
        video = 1;
        break;
    case UI_OPT_ANTI_ALIAS: {
        if (aaLocked()) {
            return; /* Off under Original */
        }
        /* the display order, around, from the stored value, over the values
           the device can draw at all */
        const unsigned m = aaMask();
        int pos = 0;
        for (int k = 0; k < ICO_AA_COUNT; k++) {
            if (kAaOrder[k] == aaStored()) {
                pos = k;
            }
        }
        int a = kAaOrder[pos];
        for (int k = 0; k < ICO_AA_COUNT; k++) {
            pos = stepIndex(pos, ICO_AA_COUNT, dir);
            if (m & (1u << kAaOrder[pos])) {
                a = kAaOrder[pos];
                break;
            }
        }
        o.antiAlias = a;
        video = 1;
        break;
    }
    case UI_OPT_WINDOW_MODE:
        /* from what the window is: Windowed, Borderless, Fullscreen */
        o.windowMode = stepIndex(s_windowModeQuery ? s_windowModeQuery() : o.windowMode,
                                 ICO_WINDOW_COUNT, dir);
        video = 1;
        break;
    case UI_OPT_FILTER:
        o.filter = stepIndex(o.filter, 3, dir);
        video = 1;
        break;
    case UI_OPT_TEXTURE_PACK:
        if (!texturePackInstalled()) {
            return; /* "None installed": nothing to switch */
        }
        o.texturePack = !o.texturePack;
        video = 1;
        break;
    case UI_OPT_MODEL_PACK:
        if (!modelPackInstalled()) {
            return; /* "None installed": nothing to switch */
        }
        o.modelPack = !o.modelPack;
        video = 1;
        break;
    case UI_OPT_VSYNC:
    case UI_OPT_FULL_HEIGHT:
    case UI_OPT_DUMP_TEXTURES:
    case UI_OPT_DUMP_MODELS:
    case UI_OPT_EFFECT_GLOW:
    case UI_OPT_EFFECT_DEPTH_OF_FIELD:
    case UI_OPT_EFFECT_SOFTENING:
    case UI_OPT_EFFECT_FOG:
    case UI_OPT_EFFECT_WATER:
    case UI_OPT_EFFECT_CINEMATIC_BARS:
    case UI_OPT_FULL_PIXEL: {
        /* the plain On/Off display rows (and Water reflections' Original
           and Sharp) */
        int *const on = videoSwitch(&o, opt);
        *on = !*on;
        video = 1;
        break;
    }
    case UI_OPT_EFFECT_MOTION_BLUR: {
        /* Off, Original, Smooth, around (Right from Original is Smooth);
           Off keeps the way chosen last in the file's mode key */
        int i = !o.effectMotionBlur ? 0 : o.effectMotionBlurSmooth ? 2 : 1;
        i = stepIndex(i, 3, dir);
        o.effectMotionBlur = i != 0;
        if (i) {
            o.effectMotionBlurSmooth = i == 2;
        }
        video = 1;
        break;
    }
    case UI_OPT_FRAMERATE:
        o.framerate = stepFramerate(o.framerate, dir);
        video = 1;
        break;
    case UI_OPT_CRT: {
        /* Off, Scanlines, Consumer TV, Trinitron, PVM, Shadow mask,
           around */
        int i = o.crt ? o.crtMode + 1 : 0;
        i = stepIndex(i, ICO_CRT_MODES + 1, dir);
        o.crt = i != 0;
        if (i) {
            o.crtMode = i - 1;
        }
        video = 1;
        break;
    }
    case UI_OPT_CRT_STRENGTH:
        o.crtStrength = (float)stepTenth(o.crtStrength, dir);
        video = 1;
        break;
    case UI_OPT_VIDEO_MODE:
        /* as kanbanBoot.c step 201: the value, then gsb_Init on a change */
        systemStatus[0] = systemStatus[0] != 0 ? 0 : 1;
        gsResetFunc(0);
        ico_sysconf_set_video_mode(systemStatus[0]);
        s_dirtyConfig = 1;
        break;
    case UI_OPT_VOLUME: {
        double v = stepTenth(unit01("audio.volume"), dir);
        ico_config_set_float("audio.volume", v);
        ico_audio_set_volume(v); /* live; the SDL output reads it per block */
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_MUSIC:
    case UI_OPT_EFFECTS: {
        const char *key = opt == UI_OPT_MUSIC ? "audio.music" : "audio.effects";
        double v = stepTenth(unit01(key), dir);
        ico_config_set_float(key, v);
        /* live: the voices' volumes are re-issued (mix_gain.h) */
        ico_audio_set_gain(opt == UI_OPT_MUSIC ? ICO_AUDIO_CAT_MUSIC : ICO_AUDIO_CAT_EFFECTS, v);
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_OUTPUT: {
        /* Auto, Stereo, Mono; the game's mode follows at once */
        int m = stepIndex(ico_opt_output_mode() + 1, 3, dir) - 1;
        ico_opt_set_output_mode(m);
        ico_config_set_string("audio.output", ico_opt_output_name(m));
        applyOutputMode(1);
        if (m != ICO_OUTPUT_AUTO) {
            /* the rest of what the Options screen's Sound row (308)
               does (applyOutputMode set the mode): the game's own mode, the
               one its saves write and Auto shows, is this one too.  The
               key is saved with the page's others on leaving, not at each
               step (ico_opt_output_toggled's save) */
            ico_opt_output_record(m);
        }
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_DEVICE:
        stepDevice(dir);
        break;
    case UI_OPT_GPU_DRIVER:
        stepGpuDriver(dir);
        break;
    case UI_OPT_GPU_ADD_FILE:
        stepGpuFile(dir);
        break;
    case UI_OPT_GRAPHICS_API:
        stepBackend(dir);
        break;
    case UI_OPT_STICK_FIX:
        ico_opt_set_stick_fix(!ico_opt_stick_fix());
        ico_config_set_bool("gameplay.stick_fix", ico_opt_stick_fix());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_ACH_POPUPS:
        ico_ach_set_popups(!ico_ach_popups_enabled());
        ico_config_set_bool("game.achievements", ico_ach_popups_enabled());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_MOUSE_SENS: {
        IcoBindings *b = liveBindings();
        int i, best = 3;
        for (i = 0; i < N_OF(kMouseSens); i++) {
            if (kMouseSens[i] <= b->mouse_sens + 0.001f) {
                best = i;
            }
        }
        best += dir;
        best = best < 0 ? 0 : best >= N_OF(kMouseSens) ? N_OF(kMouseSens) - 1 : best;
        b->mouse_sens = kMouseSens[best];
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_MOUSE_CAMERA:
        /* live from the next vsync (the window reads the
           table for the capture, input_sdl.c steps it) */
        liveBindings()->mouse_camera = !liveBindings()->mouse_camera;
        s_dirtyBindings = 1;
        break;
    case UI_OPT_MOUSE_INVERT:
        liveBindings()->mouse_invert_y = !liveBindings()->mouse_invert_y;
        s_dirtyBindings = 1;
        break;
    case UI_OPT_MOUSE_SPEED: {
        IcoBindings *b = liveBindings();
        int i, best = 1;
        for (i = 0; i < N_OF(kMouseCamSpeed); i++) {
            if (kMouseCamSpeed[i] <= b->mouse_camera_speed + 0.001f) {
                best = i;
            }
        }
        best += dir;
        best = best < 0 ? 0 : best >= N_OF(kMouseCamSpeed) ? N_OF(kMouseCamSpeed) - 1 : best;
        b->mouse_camera_speed = kMouseCamSpeed[best];
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_MOUSE_RANGE:
        liveBindings()->mouse_full_range = !liveBindings()->mouse_full_range;
        s_dirtyBindings = 1;
        break;
    case UI_OPT_MOUSE_RETURN:
        liveBindings()->mouse_return = !liveBindings()->mouse_return;
        s_dirtyBindings = 1;
        break;
    case UI_OPT_TOUCH_MODE: {
        /* live from the next vsync (input_sdl.c reads the table); a phone
           steps between Auto and Always only: Off there would leave no
           way to turn the buttons back on (ico_touch_effective_mode) */
        IcoBindings *b = liveBindings();
        const int m = touchModeInForce();

        if (ico_video_android()) {
            b->touch_mode = m == ICO_TOUCH_MODE_AUTO ? ICO_TOUCH_MODE_ALWAYS : ICO_TOUCH_MODE_AUTO;
        } else {
            b->touch_mode = stepIndex(m >= 0 && m < 3 ? m : 1, 3, dir);
        }
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_TOUCH_SIZE: {
        /* the zones are rebuilt at the next vsync */
        IcoBindings *b = liveBindings();
        b->touch_size =
            stepIndex(b->touch_size >= 0 && b->touch_size < 3 ? b->touch_size : 1, 3, dir);
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_TOUCH_OPACITY: {
        /* the step nearest the value (a file may say 60), then around */
        IcoBindings *b = liveBindings();
        int i, best = 0;
        for (i = 1; i < N_OF(kTouchOpacity); i++) {
            const int d = kTouchOpacity[i] - b->touch_opacity,
                      db = kTouchOpacity[best] - b->touch_opacity;
            if ((d < 0 ? -d : d) < (db < 0 ? -db : db)) {
                best = i;
            }
        }
        b->touch_opacity = kTouchOpacity[stepIndex(best, N_OF(kTouchOpacity), dir)];
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_TOUCH_SNAP: {
        /* [touch] snap: the editor's drops round to the grid */
        IcoTouchEdits *e = ico_input_live_touch_edits();
        e->snap = !e->snap;
        e->gen++;
        s_dirtyTouch = 1;
        break;
    }
    case UI_OPT_TOUCH_GRID: {
        /* [touch] grid: 2, 4, 8 mm, around */
        IcoTouchEdits *e = ico_input_live_touch_edits();
        e->grid =
            ico_touch_edit_grids[stepIndex(touchGridIndex(e), N_OF(ico_touch_edit_grids), dir)];
        e->gen++;
        s_dirtyTouch = 1;
        break;
    }
    case UI_OPT_CIRCLE_BACK:
        /* the game menus' alias, live from the next press */
        ico_opt_set_circle_back(!ico_opt_circle_back());
        ico_config_set_bool("game.circle_back", ico_opt_circle_back());
        lt_ext_set_circle_back(ico_opt_circle_back());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_YORDA:
        ico_opt_set_yorda_safe(!ico_opt_yorda_safe());
        ico_config_set_bool("gameplay.yorda_safe", ico_opt_yorda_safe());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_DOOR_FIX:
        ico_opt_set_door_fix(!ico_opt_door_fix());
        ico_config_set_bool("gameplay.door_fix", ico_opt_door_fix());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_NATURAL_CHAINS:
        ico_opt_set_natural_chains(!ico_opt_natural_chains());
        ico_config_set_bool("gameplay.natural_chains", ico_opt_natural_chains());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_LANGUAGE: {
        /* as kanbanBoot.c step 102 stores it; the game's text and subtitles
           follow at their next load, the port's strings at once while the
           menu language is Auto */
        int i = gameLanguage() - ICO_GAME_LANGUAGE_ENGLISH;
        if (gameLanguage() < 0) {
            break; /* no language of the game's own (the row is hidden) */
        }
        i = i < 0 || i > 4 ? 0 : stepIndex(i, 5, dir);
        setGameLanguage(ICO_GAME_LANGUAGE_ENGLISH + i);
        ui_settings_apply_language();
        ico_sysconf_set_language(ico_game_to_scf_language(gameLanguage()));
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_DISC: {
        /* [game] disc, read at the next start (main_host.c mount_game_data):
           the next disc whose game data is there (discRowShown), wrapping */
        const int n = ico_region_count();
        int i = discRow();
        for (int k = 0; k < n; k++) {
            i = stepIndex(i, n, dir);
            if (discRowShown() && (s_discPresent >> i) & 1u) {
                break;
            }
        }
        ico_config_set_string("game.disc", ico_region_at(i)->slug);
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_MENU_LANGUAGE: {
        /* Auto, then the seven tables (UiLang order): [ui] language */
        const int i = stepIndex(ico_opt_ui_language() + 1, UI_LANG_COUNT + 1, dir) - 1;
        ico_opt_set_ui_language(i);
        ico_config_set_string("ui.language", ico_opt_ui_language_name(i));
        ui_settings_apply_language();
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_BRIGHTNESS: {
        /* as la_adjust_screen: a step at a time, no wrap */
        int v = brightness() + dir;
        systemStatus[11] = v < 0 ? 0 : v > BRIGHTNESS_MAX ? BRIGHTNESS_MAX : v;
        break;
    }
    case UI_OPT_VIBRATION:
        /* la_game_option's row 313 */
        iosPadActRequestEnable = iosPadActRequestEnable == 0;
        break;
    case UI_OPT_HOLD_TYPE:
        /* row 318, kept in [gameplay] hold_type */
        ico_opt_set_hold_type(!ico_opt_hold_type());
        ico_config_set_string("gameplay.hold_type", ico_opt_hold_type_name(ico_opt_hold_type()));
        applyHoldType();
        s_dirtyConfig = 1;
        break;
    case UI_OPT_FILM_EFFECT: {
        /* row 300: 0..4 around, the stage animations with it */
        int m = optionScreenMode >= 0 && optionScreenMode < FILM_EFFECTS ? optionScreenMode : 0;
        la_host_film_effect(stepIndex(m, FILM_EFFECTS, dir));
        break;
    }
    case UI_OPT_PLAYERS:
        /* row 325 */
        girlControlMode = girlControlMode == 0;
        break;
    case UI_OPT_DEVELOPER:
        ico_opt_set_developer_mode(!ico_opt_developer_mode());
        ico_config_set_bool("gameplay.developer_mode", ico_opt_developer_mode());
        s_dirtyConfig = 1;
        if (!ico_opt_developer_mode() && o.dumpModels) {
            /* Dump models is switched off with developer mode, as Dump
               textures is below */
            o.dumpModels = 0;
            video = 1;
        }
        if (!ico_opt_developer_mode() && o.dumpTextures) {
            /* Dump textures lives under Developer mode; with its row
               hidden it is switched off too, or the dumps would go on being
               written on every start with no row to stop them */
            o.dumpTextures = 0;
            video = 1;
        }
        break;
    default:
        break;
    }
    if (video) {
        ico_video_set(&o);
        s_dirtyVideo = 1;
    }
}

int ui_settings_save(void)
{
    int r = 0;
    if (s_dirtyBindings) {
        IcoBindings *b = ico_input_live_bindings();
        if (ico_input_write_bindings(b) < 0) {
            r = -1;
        }
        s_dirtyConfig = 1;
    }
    if (s_dirtyTouch) {
        /* [touch]: snap, grid and each edited control's line */
        if (ico_input_write_touch_edits(ico_input_live_touch_edits()) < 0) {
            r = -1;
        }
        s_dirtyConfig = 1;
    }
    if (s_dirtyVideo) {
        /* writes [video] and saves the whole file */
        if (ico_video_save() != 0) {
            r = -1;
        }
    } else if (s_dirtyConfig) {
        if (ico_config_save() != 0) {
            r = -1;
        }
    }
    if (s_dirtyBindings) {
        /* the table again from what was written: what the next run loads */
        ico_input_reload_bindings(ico_input_live_bindings());
    }
    if (s_dirtyTouch) {
        /* the edits again from what was written, as the bindings */
        ico_input_reload_touch_edits(ico_input_live_touch_edits());
    }
    if (r != 0) {
        fprintf(stderr, "settings: could not write %s\n", ico_config_toml_path());
    }
    s_dirtyVideo = s_dirtyConfig = s_dirtyBindings = s_dirtyTouch = 0;
    return r;
}

int ui_settings_save_on_quit(void)
{
    if (!(s_dirtyVideo || s_dirtyConfig || s_dirtyBindings || s_dirtyTouch || ico_config_dirty())) {
        return 0;
    }
    fprintf(stderr, "settings: saving the changed settings on quit\n");
    int r = ui_settings_save();
    /* a setter outside the pages (the language the game chose, ...) */
    if (ico_config_dirty() && ico_config_save() != 0) {
        fprintf(stderr, "settings: could not write %s\n", ico_config_toml_path());
        r = -1;
    }
    return r;
}

/* ------------------------------------------------------------ capture */

void ui_remap_capture_start(UiRemapCapture *c, int target)
{
    memset(c, 0, sizeof(*c));
    c->active = 1;
    c->target = target;
    c->seq0 = ico_input_last_press(NULL, NULL);
}

int ui_remap_capture_step(UiRemapCapture *c, void *bindings)
{
    int kind, code;
    if (!c->active) {
        return UI_CAPTURE_IDLE;
    }
    unsigned int seq = ico_input_last_press(&kind, &code);
    if (seq != c->seq0 &&
        ico_bindings_assign((IcoBindings *)bindings, c->target, kind, code) == 0) {
        c->active = 0;
        c->cooldown = UI_REMAP_COOLDOWN_TICKS;
        return UI_CAPTURE_BOUND;
    }
    c->seq0 = seq; /* a press that cannot be bound is skipped */
    if (++c->ticks >= UI_REMAP_TIMEOUT_TICKS) {
        c->active = 0;
        c->cooldown = UI_REMAP_COOLDOWN_TICKS;
        return UI_CAPTURE_TIMEOUT;
    }
    return UI_CAPTURE_WAITING;
}

/* -------------------------------------------------------------- texts */

static void targetName(int t, char *buf, unsigned size)
{
    static const char *const shoulder[] = {"L2", "R2", "L1", "R1"};
    int dir;
    switch (t) {
    case ICO_T_L2:
    case ICO_T_R2:
    case ICO_T_L1:
    case ICO_T_R1:
        snprintf(buf, size, "%s", shoulder[t]);
        return;
    case ICO_T_L3:
        snprintf(buf, size, "L3");
        return;
    case ICO_T_R3:
        snprintf(buf, size, "R3");
        return;
    case ICO_T_TRIANGLE:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_TRIANGLE));
        return;
    case ICO_T_CIRCLE:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_CIRCLE));
        return;
    case ICO_T_CROSS:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_CROSS));
        return;
    case ICO_T_SQUARE:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_SQUARE));
        return;
    case ICO_T_START:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_START));
        return;
    case ICO_T_SELECT:
        snprintf(buf, size, "%s", ui_str(UI_STR_BTN_SELECT));
        return;
    default:
        break;
    }
    static const int dirs[4] = {UI_STR_DIR_UP, UI_STR_DIR_DOWN, UI_STR_DIR_LEFT, UI_STR_DIR_RIGHT};
    int what;
    if (t >= ICO_T_UP && t <= ICO_T_LEFT) {
        /* UP RIGHT DOWN LEFT */
        static const int d[4] = {0, 3, 1, 2};
        dir = d[t - ICO_T_UP];
        what = UI_STR_BTN_DPAD;
    } else if (t >= ICO_T_LSTICK_UP && t <= ICO_T_LSTICK_RIGHT) {
        dir = t - ICO_T_LSTICK_UP;
        what = UI_STR_STICK_LEFT;
    } else {
        dir = t - ICO_T_RSTICK_UP;
        what = UI_STR_STICK_RIGHT;
    }
    snprintf(buf, size, "%s %s", ui_str((UiStrId)what), ui_str((UiStrId)dirs[dir & 3]));
}

/* gamepad sources by position (ICO_GP_*); the names are UI_STR_PAD_*, in order */
_Static_assert(UI_STR_PAD_RSTICK_DOWN - UI_STR_PAD_SOUTH == ICO_GP_RY_POS - ICO_GP_SOUTH,
               "the UI_STR_PAD_* names follow the ICO_GP_* sources");

static const char *padName(int src)
{
    return ui_str((UiStrId)(UI_STR_PAD_SOUTH + src - ICO_GP_SOUTH));
}

/* the mouse buttons, after the language's word for the mouse (the one
   photo mode's help panel uses) */
static const char *const kMouseButtons[ICO_MOUSE_BUTTONS] = {"", "L", "R", "M", "X1", "X2"};

/* One column of the remap screen: the keyboard's keys and mouse buttons,
   or the gamepad's sources, of target t. */
static void sourcesText(const IcoBindings *b, int t, int gamepad, char *buf, size_t size)
{
    size_t n = 0;
    char mouse[32];
    buf[0] = '\0';
    for (int i = 0; i < ICO_BIND_MAX * 2; i++) {
        const char *name = NULL;
        if (gamepad) {
            if (i < ICO_BIND_MAX && b->gp[t][i]) {
                name = padName(b->gp[t][i]);
            }
        } else if (i < ICO_BIND_MAX) {
            if (b->kb[t][i]) {
                name = ico_key_name(b->kb[t][i]);
            }
        } else if (b->mouse[t][i - ICO_BIND_MAX]) {
            snprintf(mouse, sizeof(mouse), "%s %s", ui_str(UI_STR_PHOTO_MOUSE_PREFIX),
                     kMouseButtons[b->mouse[t][i - ICO_BIND_MAX]]);
            name = mouse;
        }
        if (name) {
            snprintf(buf + n, size - n, "%s%s", n ? ", " : "", name);
            n = strlen(buf);
        }
    }
    if (n == 0) {
        snprintf(buf, size, "-");
    }
}

/* Breaks text into lines of at most width x units at size (a note): at a
   space, or for the Japanese text between two characters where a line may
   break (strings.h ui_break_before), a line break put in there; a Korean
   word alone wider than the line between two of its Hangul characters
   (strings.h ui_hangul_char), the Korean text breaking at its spaces
   otherwise. */
static void wrapText(const char *text, float size, float width, char *out, unsigned outSize)
{
    unsigned n = 0, lineStart = 0, lastSpace = 0, lastBreak = 0, lastInWord = 0;
    uint32_t prev = 0;
    for (const char *p = text; *p;) {
        /* a whole UTF-8 sequence at a time: the line is measured only on a
           character's end (a cut "'" measured as U+FFFD) */
        const char *next = p;
        const uint32_t cp = ui_utf8_next(&next);
        const unsigned len = (unsigned)(next - p);
        if (n + len + 1 > outSize) {
            break;
        }
        if (n > lineStart && ui_break_before(prev, cp)) {
            lastBreak = n;
        }
        if (n > lineStart && ui_hangul_char(prev) && ui_hangul_char(cp)) {
            lastInWord = n;
        }
        memcpy(out + n, p, len);
        out[n + len] = '\0';
        if (*p == ' ') {
            lastSpace = n;
        }
        n += len;
        p = next;
        prev = cp;
        if ((lastSpace > lineStart || lastBreak > lineStart || lastInWord > lineStart) &&
            ui_measure_menu_text(size, out + lineStart) > width) {
            if (lastSpace > lineStart && lastSpace >= lastBreak) {
                out[lastSpace] = '\n';
                lineStart = lastSpace + 1;
            } else if (n + 2 <= outSize) {
                /* the Japanese break, else inside the Korean word */
                const unsigned at = lastBreak > lineStart ? lastBreak : lastInWord;
                memmove(out + at + 1, out + at, n - at + 1);
                out[at] = '\n';
                n++;
                lineStart = at + 1;
            }
        }
    }
    out[n] = '\0';
}

/* A note row's text, wrapped; redone only when the string or the language
   changes (the wrap measures the text). */
static int s_noteStr[LT_EXT_MAX_PROPERTIES];
static int s_noteLang[LT_EXT_MAX_PROPERTIES];

/* A note's words: the pack and dump notes name the running disc's folder
   (its serial, port/core/region.h pack_serial: SCES-50760 or SCPS-11003)
   through their %s (strings.h ui_str_serial) */
static const char *noteText(int strId, char *out, size_t size)
{
    if (ui_str_takes_serial((UiStrId)strId)) {
        return ui_str_serial((UiStrId)strId, out, size);
    }
    return ui_str((UiStrId)strId);
}

static void setNote(int row, int strId)
{
    char buf[256], text[256];
    int i = row - LT_GAME_PROPERTY_COUNT;
    if (i >= 0 && i < LT_EXT_MAX_PROPERTIES && s_noteStr[i] == strId &&
        s_noteLang[i] == (int)ui_get_language() + 1) {
        return;
    }
    wrapText(noteText(strId, text, sizeof(text)), NOTE_SIZE, 580.0f, buf, sizeof(buf));
    lt_ext_set_text(row, buf);
    if (i >= 0 && i < LT_EXT_MAX_PROPERTIES) {
        s_noteStr[i] = strId;
        s_noteLang[i] = (int)ui_get_language() + 1;
    }
}

/* ------------------------------------------------------------ building */

static int settingsProc(int first, int item);
static const UiListDef kAchDef, kRemapDef, kGalDef;
/* the music gallery's progress bar (its fill, the elapsed and total times;
   the rim and track never change after they are added) and its transport,
   a line of the game's button glyphs */
static int s_galFill = -1, s_galTime = -1, s_galTotal = -1;
static UiHint s_galHint;
static int entryProc(int first, int item);
static void buildNewGameScreen(void);
static void buildQuitScreen(void);
static int s_quitLayout = -1;

/* the music gallery's page, in field
   lines: the list, the status line, the progress bar (its rim, the track
   inside it, the times beside it) and the transport */
#define GAL_LIST_Y 38
#define GAL_LIST_PITCH 16
#define GAL_STATUS_Y 168
#define GAL_BAR_X 150
#define GAL_BAR_W 340
#define GAL_BAR_Y 188
#define GAL_BAR_H 6 /* y units: three field lines */
#define GAL_TIME_GAP 12
#define GAL_HINT_Y 200

static void buildGalleryBar(void)
{
    static const unsigned char kRim[4] = {0, 0, 0, 0x50};
    static const unsigned char kTrack[4] = {0x26, 0x25, 0x22, 0x80};
    /* the letters' colour: the row's own */
    static const unsigned char kFill[4] = {0x80, 0x80, 0x80, 0x80};
    /* the rim a field line round the track */
    (void)lt_ext_add_rect(GAL_BAR_X - 2, GAL_BAR_Y - 1, GAL_BAR_W + 4, GAL_BAR_H + 4, kRim);
    (void)lt_ext_add_rect(GAL_BAR_X, GAL_BAR_Y, GAL_BAR_W, GAL_BAR_H, kTrack);
    s_galFill = lt_ext_add_rect(GAL_BAR_X, GAL_BAR_Y, GAL_BAR_W, GAL_BAR_H, kFill);
    lt_ext_set_fill(s_galFill, 0.0f);
    /* the times' capitals on the bar's middle line (a label's capitals sit
       6.5 field lines below its 30-unit box's top) */
    const int ty = GAL_BAR_Y + GAL_BAR_H / 4 - 6;
    s_galTime = ui_settings_add_row(GAL_BAR_X - GAL_TIME_GAP - 100, ty, 100, 30, 0, -1, 0, " ",
                                    NOTE_SIZE, UI_ALIGN_RIGHT);
    s_galTotal = ui_settings_add_row(GAL_BAR_X + GAL_BAR_W + GAL_TIME_GAP, ty, 100, 30, 0, -1, 0,
                                     " ", NOTE_SIZE, UI_ALIGN_LEFT);
    ui_hint_build(&s_galHint, GAL_HINT_Y, NOTE_SIZE, ui_hint_gallery, UI_HINT_GAL_COUNT);
}

/* A page's first row and pitch (field lines) for n rows shown.  Every page
   keeps Back's box inside the 226 lines and above the notes:
     Display      from 36, 14 apart (at most eleven rows shown; Video mode
                  and Model pack are the title's)
     Characters   from 40, 13 apart (twelve rows: Back at 183, its box to
                  219); inside the model viewer CV_ROW_Y0 and CV_ROW_PITCH
     Main         from 40: up to 8 rows 19 apart, 9 rows 17, 10 rows 15,
                  11 rows 14, 12 rows 13, 13 rows 12 (Back at 184, its
                  box to 224), 14 rows 11 (Back at 183, its box to 223),
                  15 rows 10 (Back at 180, its box to 220: the Disc row
                  with Language, Graphics driver and the developer rows)
     Controls     from 40, up to 9 rows 18 apart, 10 rows 16 (Back at 184, its
                  box to 220), 11 or 12 rows 13 (Back at 170 or 183, its
                  box to 206 or 219); 13 to 15 from
                  30, 11 apart (Back at 184, its box to 220: fourteen,
                  the pause menu's with the mouse rows and Touch screen,
                  Back at 173); 16 rows 10 apart (Back at 180, its box to
                  216; no entry shows as many now)
     Touch screen from 40, 18 apart (nine rows: Back at 184, its box to 220)
     Effects      from 40, 18 apart, ten rows 16 (Back at 184, its box to 220);
                  eleven or twelve rows 13 (twelve: Back at 183); thirteen
                  (the title's) from 34, 13 apart (Back at 190, its box to
                  226); fourteen (the pause menu's, with Brightness) from
                  30, 12 apart (Back at 186, its box to 222)
     the others   from 40, 18 apart */
static int pagePitch(int page, int n, int *y0)
{
    if (page == UI_PAGE_DISPLAY) {
        (void)n;
        *y0 = 36;
        return 14;
    }
    *y0 = 40;
    if (page == UI_PAGE_EFFECTS && n > 13) {
        *y0 = 30;
        return 12; /* fourteen rows: Back at 186, its box to 222 */
    }
    if (page == UI_PAGE_EFFECTS && n > 12) {
        *y0 = 34;
        return 13; /* thirteen rows: Back at 190, its box to 226 */
    }
    if (page == UI_PAGE_EFFECTS && n > 10) {
        return 13; /* eleven or twelve: Back at 173 or 183 */
    }
    if (page == UI_PAGE_EFFECTS && n > 9) {
        return 16; /* ten rows: Back at 184, its box to 220 */
    }
    if (page == UI_PAGE_CHARACTERS) {
        if (ui_settings_characters_in_viewer()) {
            /* inside the viewer: the shown character's rows, Switch,
               Randomize, Reset and Back (Ico's eleven, Yorda's six) 11
               apart from 30, Back at 140 or 85, clear of the note at 176
               and the prompts at 196 */
            *y0 = CV_ROW_Y0;
            return CV_ROW_PITCH;
        }
        return 13;
    }
    if (page == UI_PAGE_MAIN && n > 17) {
        /* eighteen, an rc build's with the Graphics link: from 26, Back at
           179, its box to 219 */
        *y0 = 26;
        return 9;
    }
    if (page == UI_PAGE_MAIN && n > 16) {
        /* seventeen (every row but Share a bug report, which an rc build
           adds: the Graphics link and the Graphics driver link both): from
           26, Back at 186, its box to 226 */
        *y0 = 26;
        return 10;
    }
    if (page == UI_PAGE_MAIN && n > 15) {
        /* sixteen (every row: the Graphics driver link, Disc, Save a
           picture for a bug report and both dumps): from 30, Back at 180,
           its box to 220 */
        *y0 = 30;
        return 10;
    }
    if (page == UI_PAGE_MAIN) {
        return n > 14   ? 10
               : n > 13 ? 11
               : n > 12 ? 12
               : n > 11 ? 13
               : n > 10 ? 14
               : n > 9  ? 15
               : n > 8  ? 17
                        : 19;
    }
    if (page == UI_PAGE_CONTROLS && n > 9) {
        /* the touch rows, the mouse rows and the game's pause-menu rows
           make the page longer than the others (the table above) */
        if (n > 15) {
            /* sixteen: Back at 180, its box to 216, clear of the note */
            *y0 = 30;
            return 10;
        }
        if (n > 12) {
            *y0 = 30;
            return 11;
        }
        /* eleven (the title's with the mouse rows, no touch screen) or
           twelve 13 apart: Back at 170 or 183 */
        return n > 10 ? 13 : 16;
    }
    return 18;
}

static int rowY(int page, int i)
{
    int y0;
    /* the place a row is built at; layoutPage spaces the shown ones */
    const int pitch = pagePitch(page,
                                page == UI_PAGE_MAIN       ? 8
                                : page == UI_PAGE_CONTROLS ? 7
                                                           : 12,
                                &y0);
    return y0 + pitch * i;
}

/* The version in the Main page's top-right corner, small and greyed: the
   package label (ui_settings_set_version), right-aligned to the header's
   right edge (x 620) above the header (its box starts at field line
   HEADER_Y 12): the box is field lines 3..12, the capitals' middle at y 13
   (y units; the capitals of size 17 about 7..19). Not selectable, no
   strings-table entry; nothing in a development build. */
#define VERSION_X 420
#define VERSION_Y 3
#define VERSION_W 200
#define VERSION_H 18
#define VERSION_SIZE 17.0f

static void addVersion(Page *pg)
{
    pg->version = -1;
    if (s_version[0] == '\0') {
        return;
    }
    pg->version = ui_settings_add_row(VERSION_X, VERSION_Y, VERSION_W, VERSION_H, 0, -1, 0,
                                      s_version, VERSION_SIZE, UI_ALIGN_RIGHT);
    lt_ext_set_dim(pg->version, 1);
}

static void addHeader(Page *pg, int strId)
{
    pg->header = ui_settings_add_row(20, HEADER_Y, 600, 40, 0, -1, strId, NULL, HEADER_SIZE,
                                     UI_ALIGN_CENTER);
    P(pg->header)->centerX = 1;
}

/* label (+ value) rows of one option */
static void addOption(Page *pg, int pageId, int opt, int strId, int link)
{
    Row *r = &pg->rows[pg->count];
    int y = rowY(pageId, pg->count);
    int h = pageId == UI_PAGE_MAIN ? 40 : 36;
    r->opt = opt;
    r->link = link;
    r->note = -1;
    r->value = -1;
    r->label =
        ui_settings_add_row(LABEL_X, y, LABEL_W, h, 1, -1, strId, NULL, 0.0f, UI_ALIGN_RIGHT);
    if (steppable(opt)) {
        r->value =
            ui_settings_add_row(STEP_X, y, STEP_W, h, 1, r->label, 0, " ", 0.0f, UI_ALIGN_CENTER);
        const int al = ui_settings_add_row(ARROW_L_X, y, ARROW_W, h, 1, r->label, 0, "\xE2\x80\xB9",
                                           0.0f, UI_ALIGN_LEFT);
        const int ar = ui_settings_add_row(ARROW_R_X, y, ARROW_W, h, 1, r->label, 0, "\xE2\x80\xBA",
                                           0.0f, UI_ALIGN_LEFT);
        /* the mouse pointer's clicks: the arrows are Left and
           Right, the value itself steps as Right does */
        lt_ext_set_pointer_role(r->value, LT_POINTER_STEP);
        lt_ext_set_pointer_role(al, LT_POINTER_LEFT);
        lt_ext_set_pointer_role(ar, LT_POINTER_RIGHT);
    }
    pg->count++;
}

static void addNote(Page *pg, int opt, int strId)
{
    for (int i = 0; i < pg->count; i++) {
        if (pg->rows[i].opt == opt) {
            int n =
                ui_settings_add_row(20, NOTE_Y, 600, 30, 0, -1, 0, " ", NOTE_SIZE, UI_ALIGN_CENTER);
            P(n)->centerX = 1;
            P(n)->defaultMask = 1;
            pg->rows[i].note = n;
            pg->rows[i].noteStr = strId;
            setNote(n, strId);
            return;
        }
    }
}

/* the navigable rows in a loop, as the Options screen's */
static void linkRows(Page *pg)
{
    int idx[MAX_ROWS], n = 0;
    for (int i = 0; i < pg->count; i++) {
        idx[n++] = pg->rows[i].label;
    }
    for (int i = 0; i < n; i++) {
        P(idx[i])->downItem = idx[(i + 1) % n];
        P(idx[i])->upItem = idx[(i + n - 1) % n];
    }
}

static int pageFirstNav(Page *pg)
{
    return pg->count > 0 ? pg->rows[0].label : -1;
}

static void finishPage(Page *pg, int first, int last)
{
    pg->layout = addLayout(first, last + 1, 0.6f, settingsProc, pageFirstNav(pg));
}

static void buildOptionPage(int id, int header, const int *opts, const int *strs, const int *links,
                            int n, int parent)
{
    Page *pg = &s_pages[id];
    memset(pg, 0, sizeof(*pg));
    pg->parent = parent;
    pg->version = -1;
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    addHeader(pg, header);
    for (int i = 0; i < n && pg->count < MAX_ROWS; i++) { /* the tables are asserted to fit */
        addOption(pg, id, opts[i], strs[i], links ? links[i] : -1);
    }
    switch (id) {
    case UI_PAGE_MAIN:
        addVersion(pg);
        addNote(pg, UI_OPT_LANGUAGE, UI_STR_LANGUAGE_NOTE);
        addNote(pg, UI_OPT_MENU_LANGUAGE, UI_STR_MENU_LANGUAGE_NOTE);
        addNote(pg, UI_OPT_DISC, UI_STR_DISC_NOTE);
        addNote(pg, UI_OPT_DEVELOPER, UI_STR_DEVELOPER_NOTE);
        addNote(pg, UI_OPT_DUMP_TEXTURES, UI_STR_DUMP_TEXTURES_NOTE);
        addNote(pg, UI_OPT_DUMP_MODELS, UI_STR_DUMP_MODELS_NOTE);
        addNote(pg, UI_OPT_REPORT_PICTURE, UI_STR_REPORT_PICTURE_NOTE);
#if ICO_RC_TOOLS
        addNote(pg, UI_OPT_SHARE_REPORT, UI_STR_SHARE_REPORT_NOTE);
#endif
        break;
    case UI_PAGE_DISPLAY:
        addNote(pg, UI_OPT_RESOLUTION, UI_STR_RESOLUTION_CRT_NOTE);
        addNote(pg, UI_OPT_ANTI_ALIAS, UI_STR_OPT_AA_NOTE);
        addNote(pg, UI_OPT_TEXTURE_PACK, UI_STR_TEXTURE_PACK_NOTE);
        addNote(pg, UI_OPT_MODEL_PACK, UI_STR_MODEL_PACK_NOTE);
        break;
    case UI_PAGE_AUDIO:
        break;
    case UI_PAGE_EFFECTS:
        addNote(pg, UI_OPT_EFFECT_GLOW, UI_STR_EFFECTS_NOTE);
        addNote(pg, UI_OPT_EFFECT_MOTION_BLUR, UI_STR_EFFECT_MOTION_BLUR_NOTE);
        addNote(pg, UI_OPT_EFFECT_WATER, UI_STR_EFFECT_WATER_NOTE);
        break;
    case UI_PAGE_GPU_DRIVER:
        /* one note under the page, on every row but Back */
        addNote(pg, UI_OPT_GPU_DRIVER, UI_STR_GPU_DRIVER_NOTE);
        for (int i = 1; i < pg->count; i++) {
            if (pg->rows[i].opt != UI_OPT_BACK) {
                pg->rows[i].note = pg->rows[0].note;
                pg->rows[i].noteStr = UI_STR_GPU_DRIVER_NOTE;
            }
        }
        break;
    case UI_PAGE_GRAPHICS:
        addNote(pg, UI_OPT_GRAPHICS_API, UI_STR_GRAPHICS_API_NOTE);
        break;
    case UI_PAGE_CONTROLS:
        addNote(pg, UI_OPT_BUTTON_CONFIG, UI_STR_BUTTON_CONFIG_NOTE);
        addNote(pg, UI_OPT_HOLD_TYPE, UI_STR_HOLD_TYPE_NOTE);
        addNote(pg, UI_OPT_STICK_FIX, UI_STR_STICK_FIX_ALSO_NOTE);
        addNote(pg, UI_OPT_CIRCLE_BACK, UI_STR_CIRCLE_BACK_NOTE);
        break;
    case UI_PAGE_TOUCH:
        addNote(pg, UI_OPT_TOUCH_MODE, UI_STR_TOUCH_NOTE);
        /* Edit layout's: what it does, or why it is locked (refreshPage) */
        addNote(pg, UI_OPT_TOUCH_EDIT, UI_STR_TOUCH_EDIT_NOTE);
        break;
    case UI_PAGE_GAMEPLAY:
        addNote(pg, UI_OPT_YORDA, UI_STR_OPT_YORDA_NOTE);
        addNote(pg, UI_OPT_DOOR_FIX, UI_STR_OPT_DOOR_FIX_NOTE);
        addNote(pg, UI_OPT_NATURAL_CHAINS, UI_STR_OPT_NATURAL_CHAINS_NOTE);
        addNote(pg, UI_OPT_PLAYERS, UI_STR_PLAYERS_NOTE);
        break;
    case UI_PAGE_EXTRAS:
        /* the locked style's value: Credits shows it until unlocked */
        for (int i = 0; i < pg->count; i++) {
            Row *r = &pg->rows[i];
            if (r->opt == UI_OPT_EXTRAS_CREDITS) {
                r->value = ui_settings_add_row(STEP_X, rowY(id, i), STEP_W, 36, 1, r->label, 0, " ",
                                               0.0f, UI_ALIGN_CENTER);
            }
        }
        addNote(pg, UI_OPT_EXTRAS_CREDITS, UI_STR_EXTRAS_LOCKED_NOTE);
        addNote(pg, UI_OPT_ACH_POPUPS, UI_STR_ACH_POPUPS_ALSO_NOTE);
        break;
    case UI_PAGE_CHARACTERS: {
        /* a swatch right of each colour row's arrows, then one note row
           for every row but Back (its text chosen at refresh:
           charactersRefresh) */
        for (int i = 0; i < pg->count; i++) {
            const Row *r = &pg->rows[i];
            if (isCharOpt(r->opt)) {
                unsigned char rgba[4];
                swatchColour(charPart(r->opt), rgba);
                s_charSwatch[charPart(r->opt)] =
                    lt_ext_add_rect(SWATCH_X, rowY(id, i) + SWATCH_DY, SWATCH_W, SWATCH_H, rgba);
            }
        }
        int n = ui_settings_add_row(20, NOTE_Y, 600, 30, 0, -1, 0, " ", NOTE_SIZE, UI_ALIGN_CENTER);
        P(n)->centerX = 1;
        P(n)->defaultMask = 1;
        setNote(n, UI_STR_CHAR_NOTE_TITLE);
        for (int i = 0; i < pg->count; i++) {
            if (pg->rows[i].opt != UI_OPT_BACK) {
                pg->rows[i].note = n;
                pg->rows[i].noteStr = UI_STR_CHAR_NOTE_TITLE;
            }
        }
        /* inside the viewer: its prompts, the game's button glyphs */
        ui_hint_build(&s_charHint, NOTE_Y, NOTE_SIZE, ui_hint_chars, UI_HINT_CHAR_COUNT);
        break;
    }
    default:
        break;
    }
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    linkRows(pg);
    finishPage(pg, first, last);
}

static void buildListPage(int id, int header, const UiListDef *def, int parent)
{
    Page *pg = &s_pages[id];
    memset(pg, 0, sizeof(*pg));
    pg->parent = parent;
    pg->isList = 1;
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    addHeader(pg, header);
    int remap = id == UI_PAGE_REMAP;
    UiListStyle st;
    memset(&st, 0, sizeof(st));
    st.pitch = 18;
    if (remap) {
        /* the column heads */
        ui_settings_add_row(230, 32, 180, 30, 0, -1, UI_STR_REMAP_KEYBOARD, NULL, NOTE_SIZE,
                            UI_ALIGN_LEFT);
        ui_settings_add_row(420, 32, 190, 30, 0, -1, UI_STR_REMAP_GAMEPAD, NULL, NOTE_SIZE,
                            UI_ALIGN_LEFT);
        st.y0 = 50;
        st.label = (UiListCol){40, 180, 24.0f, UI_ALIGN_LEFT};
        st.colA = (UiListCol){230, 180, 21.0f, UI_ALIGN_LEFT};
        st.colB = (UiListCol){420, 190, 21.0f, UI_ALIGN_LEFT};
        st.statusY = 198;
        /* under the hint: the menus keep the gamepad's buttons by position */
        P(ui_settings_add_row(20, 214, 600, 14, 0, -1, UI_STR_REMAP_MENU_NOTE, NULL, 17.0f,
                              UI_ALIGN_CENTER))
            ->centerX = 1;
    } else if (id == UI_PAGE_MUSIC) {
        /* the label (the asset's name), an ambience's stage at the right,
           in the list pages' sizes on a closer pitch; the status, the
           progress bar and the transport below */
        st.y0 = GAL_LIST_Y;
        st.pitch = GAL_LIST_PITCH;
        st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
        st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
        st.statusY = GAL_STATUS_Y;
    } else {
        st.y0 = 40;
        st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
        st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
        st.statusY = NOTE_Y;
    }
    ui_list_build(&pg->list, def, NULL, &st);
    if (id == UI_PAGE_MUSIC) {
        buildGalleryBar();
    }
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        Row *r = &pg->rows[i];
        r->opt = UI_OPT_LIST;
        r->note = -1;
        r->link = -1;
        r->label = pg->list.label[i];
        r->value = pg->list.colA[i];
        pg->count++;
    }
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    finishPage(pg, first, last);
}

/* Touch screen > Edit layout: photo mode's one masked row (photo_ui.c
   ui_photo_build), nothing drawn and no cursor (curItem -1), over the
   menus' shade; settingsProc hands the page to the touch layout editor
   (touch_edit_ui.c) and the overlay draws it (touch_ui.c) */
static void buildTouchEditorPage(void)
{
    Page *pg = &s_pages[UI_PAGE_TOUCH_LAYOUT];
    memset(pg, 0, sizeof(*pg));
    pg->parent = UI_PAGE_TOUCH;
    pg->version = -1;
    pg->layout = -1;
    const int row = ui_settings_add_row(120, 200, 400, 40, 0, -1, UI_STR_OPT_TOUCH_EDIT, NULL, 0.0f,
                                        UI_ALIGN_CENTER);
    if (row < 0) {
        return;
    }
    P(row)->defaultMask = 1;
    pg->header = row;
    pg->layout = addLayout(row, row + 1, 0.6f, settingsProc, -1);
}

static void addEntryLayout(int e, int first, int last)
{
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = last + 1;
    l.proc = entryProc;
    l.procFirst = 1;
    l.defaultItem = -1;
    l.curItem = -1;
    l.link = -1;
    s_entryLayout[e] = lt_ext_add_layout(&l);
}

/* --------------------------------------------- the journey's lines */

/* The pause menu's right side: from x 300 to 610, inside the 4:3 picture's
   free space right of the game's rows (x 40 .. 252) and between its two
   black bars (field lines 0 .. 35 and 200 .. 230).  One line every
   STATS_PITCH field lines from STATS_Y, the label left and the value right
   in a box as tall as the pitch (y units are half field lines): thirteen
   lines end at field line 197. */
#define STATS_X 300
#define STATS_W 310
#define STATS_Y 41
#define STATS_PITCH 12
#define STATS_GAP 10 /* between a label and its value */
#define STATS_BACK_MARGIN 8

static const int kStatLabel[STAT_LINES] = {UI_STR_STATS_PLAY_TIME,
                                           UI_STR_STATS_DEATHS,
                                           UI_STR_STATS_CAPTURES,
                                           UI_STR_STATS_SAVES,
                                           UI_STR_STATS_ENEMIES,
                                           UI_STR_OPT_NEWGAME_PLUS,
                                           UI_STR_OPT_MIRROR,
                                           UI_STR_SECTION_ACHIEVEMENTS,
                                           UI_STR_STATS_ASSISTS,
                                           0,
                                           0,
                                           0,
                                           UI_STR_STATS_AREA};

/* the backdrop, then each line's label and value; returns the last row */
static int buildStats(void)
{
    /* the gallery rim's shade: the scene shows through */
    static const unsigned char kBack[4] = {0, 0, 0, 0x50};
    s_statBack = lt_ext_add_rect(STATS_X - STATS_BACK_MARGIN, STATS_Y,
                                 STATS_W + 2 * STATS_BACK_MARGIN, 2 * STATS_PITCH, kBack);
    int last = s_statBack;
    if (s_statBack >= 0) {
        P(s_statBack)->defaultMask = 1;
    }
    for (int i = 0; i < STAT_LINES; i++) {
        const int y = STATS_Y + i * STATS_PITCH;
        s_statLabel[i] =
            ui_settings_add_row(STATS_X, y, STATS_W, 2 * STATS_PITCH, 0, -1, kStatLabel[i],
                                kStatLabel[i] ? NULL : " ", NOTE_SIZE, UI_ALIGN_LEFT);
        s_statValue[i] = ui_settings_add_row(STATS_X, y, STATS_W, 2 * STATS_PITCH, 0, -1, 0, " ",
                                             NOTE_SIZE, UI_ALIGN_RIGHT);
        if (s_statLabel[i] < 0 || s_statValue[i] < 0) {
            return -1;
        }
        P(s_statLabel[i])->defaultMask = P(s_statValue[i])->defaultMask = 1;
        last = s_statValue[i];
    }
    s_statsBuilt = 1;
    return last;
}

/* The play time as the save screen shows it: layout_action.c playTime's
   frames over ((60 - s0 * 10) / s1) * s1, clamped to 99:59:59 */
static void playTimeText(unsigned int frames, char *buf, size_t size)
{
    const int s1 = systemStatus[1] > 0 ? systemStatus[1] : 1;
    const int fps = ((60 - systemStatus[0] * 10) / s1) * s1;
    unsigned int h = 0, m = 0, sec = 0;
    if (fps > 0) {
        const unsigned int f = (unsigned int)fps;
        sec = (frames / f) % 60;
        m = (frames / (f * 60)) % 60;
        h = frames / (f * 3600);
    }
    if (h >= 100) {
        h = 99;
        m = 59;
        sec = 59;
    }
    snprintf(buf, size, "%02u:%02u:%02u", h, m, sec);
}

/* The couch's name the save screen shows for this stage
   (layout_action.c _la_set_preview_info: row stage + 134), where its sheet
   has one: the rows of stages 3 .. 38 (after row 172 the stage alone does
   not pick the row: two are chosen by the couch's own id, the switch in
   _la_set_preview_info, and from 176 they are the card slots' words);
   NULL elsewhere, so the line is left out (the stages' own names are
   developer labels, not for players) */
static const char *areaName(int stage)
{
    /* the running disc's end stage (63, 61 on the Japanese and Korean discs) as the
       save screen maps it; stages 3 .. 38 are the same rows on every disc,
       and the menu text table's rows (the European disc's, stage + 134)
       name them in the port's text, in the port's language (these lines
       are the port's, not a sheet's) */
    if (stage == ico_region_cur()->stage_end) {
        stage = ico_region_cur()->area_name_end_as;
    }
    if (stage < 3 || stage > 38) {
        return NULL;
    }
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        if (ui_menu_text_rows[i].row == stage + ico_region_info_pal.area_name_row_base) {
            return ui_str((UiStrId)ui_menu_text_items[ui_menu_text_rows[i].item].str);
        }
    }
    return NULL;
}

/* the achievements unlocked (not ICO_ACH_LOCKED) */
static int achUnlocked(void)
{
    const int n = ico_ach_count();
    int got = 0;
    for (int i = 0; i < n; i++) {
        got += ico_ach_state(i) != ICO_ACH_LOCKED;
    }
    return got;
}

/* Shows the lines (show) or hides them, each pause frame: the layout code
   puts every row back to its defaultMask before the procs run, and the
   default follows the last frame's choice so the lines fade with the menu.
   Shown while a stage runs (not the title's stage 1) and photo mode is
   off; the visible lines are packed from the top, the assists' only those
   on, the area's only where the save screen names one. */
static void pauseStats(int show)
{
    char text[STAT_LINES][96];
    int on[STAT_LINES];
    if (!s_statsBuilt) {
        return;
    }
    if (show) {
        ui_settings_apply_language();
        const int got = achUnlocked();
        const int n = ico_ach_count();
        playTimeText(ico_gs_play_frames(), text[STAT_PLAY_TIME], sizeof(text[0]));
        snprintf(text[STAT_DEATHS], sizeof(text[0]), "%u", ico_gs_run_game_overs());
        snprintf(text[STAT_CAPTURES], sizeof(text[0]), "%u", ico_gs_run_captures());
        snprintf(text[STAT_SAVES], sizeof(text[0]), "%u", ico_gs_run_saves());
        snprintf(text[STAT_ENEMIES], sizeof(text[0]), "%u", ico_gs_run_enemies());
        snprintf(text[STAT_NEWGAME_PLUS], sizeof(text[0]), "%s", onOff(gFlagGameClear != 0));
        snprintf(text[STAT_MIRROR], sizeof(text[0]), "%s", onOff(ico_opt_mirror()));
        snprintf(text[STAT_ACHIEVEMENTS], sizeof(text[0]), "%d / %d", got, n);
        for (int i = 0; i < STAT_LINES; i++) {
            on[i] = i < STAT_ASSISTS;
        }
        if (ico_gs_run_partial()) {
            /* an older save kept no count of these: hidden rather than
               counted from the load, until a New Game */
            on[STAT_SAVES] = on[STAT_ENEMIES] = 0;
        }
        const int assist[3] = {ico_opt_yorda_safe(), ico_opt_stick_fix(), ico_opt_developer_mode()};
        static const int kAssistStr[3] = {UI_STR_OPT_YORDA, UI_STR_OPT_STICK_FIX,
                                          UI_STR_OPT_DEVELOPER_MODE};
        int k = STAT_ASSIST_1;
        for (int a = 0; a < 3; a++) {
            if (assist[a]) {
                snprintf(text[k], sizeof(text[0]), "%s", ui_str((UiStrId)kAssistStr[a]));
                on[k++] = 1;
            }
        }
        on[STAT_ASSISTS] = k > STAT_ASSIST_1;
        text[STAT_ASSISTS][0] = '\0';
        const char *area = areaName(stage_no);
        on[STAT_AREA] = area != NULL;
        snprintf(text[STAT_AREA], sizeof(text[0]), "%s", area ? area : "");
    } else {
        memset(on, 0, sizeof(on));
    }
    int shown = 0;
    for (int i = 0; i < STAT_LINES; i++) {
        const int vis = on[i];
        LtProperty *l = P(s_statLabel[i]), *v = P(s_statValue[i]);
        l->defaultMask = v->defaultMask = vis ? 0 : 1;
        lt_mask_property(s_statLabel[i], vis ? 0 : 1);
        lt_mask_property(s_statValue[i], vis ? 0 : 1);
        if (!vis) {
            continue;
        }
        /* the value at the right; the label's box ends before it, so a long
           label in another language is set smaller (layout_ext.c's fit)
           instead of running into the value */
        const int y = STATS_Y + shown * STATS_PITCH;
        const float vw = text[i][0] ? ui_measure_menu_text(NOTE_SIZE, text[i]) : 0.0f;
        int lw = STATS_W - (int)(vw + 0.999f) - (text[i][0] ? STATS_GAP : 0);
        lw = lw < STATS_W / 3 ? STATS_W / 3 : lw;
        l->dispY = v->dispY = y;
        l->dispW = lw;
        lt_ext_set_text(s_statValue[i], text[i][0] ? text[i] : " ");
        if (kStatLabel[i]) {
            lt_ext_set_str(s_statLabel[i], kStatLabel[i]);
        }
        shown++;
    }
    if (s_statBack >= 0) {
        LtProperty *b = P(s_statBack);
        b->defaultMask = shown ? 0 : 1;
        lt_mask_property(s_statBack, shown ? 0 : 1);
        /* three field lines over the first line's box and one under the
           last's: thirteen lines end at 198, above the lower bar (200) */
        b->dispY = STATS_Y - 3;
        b->dispH = 2 * (shown * STATS_PITCH + 4);
    }
}

static void buildEntries(void)
{
    /* the pause menu: the game's Options row opens the menu (repoint), and
       "Photo mode" goes under it, alone in a port layout chained after 57
       (placePause sets its place, mask and links) */
    s_entryRow[ENTRY_PAUSE] = ROW_PAUSE_OPTIONS;
    s_quitRow[ENTRY_PAUSE] = -1;
    s_photoRow = ui_settings_add_row(0, 0, PAUSE_ROW_W, 40, 1, -1, UI_STR_PHOTO_MODE, NULL, 0.0f,
                                     UI_ALIGN_LEFT);
    /* the journey's lines in the same layout (the rows are contiguous),
       placed and filled by pauseStats */
    int last = buildStats();
    addEntryLayout(ENTRY_PAUSE, s_photoRow, last >= 0 ? last : s_photoRow);
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        /* "Options": the pause menu's word (UI_STR_MT_OPTIONS, the
           sheets' own wording in each language), a text row in the menus'
           look like Continue and New Game around it, centred as "Quit to
           desktop" is; the y and the height are set with the game's rows
           (placeTitle) */
        int row = ui_settings_add_row(120, 0, 400, 40, 1, -1, UI_STR_MT_OPTIONS, NULL, 0.0f,
                                      UI_ALIGN_CENTER);
        P(row)->centerX = 1;
        /* hidden unless the title's proc shows it, as New Game (49 to 51
           are masked by default) */
        P(row)->defaultMask = 1;
        P(row)->right = s_pages[UI_PAGE_MAIN].layout;
        s_entryRow[e] = row;
        /* "Quit to desktop" under Settings, in the same layout (the
           rows are contiguous); Cross opens the confirmation */
        int q =
            ui_settings_add_row(120, 0, 400, 40, 1, -1, quitRowStr(), NULL, 0.0f, UI_ALIGN_CENTER);
        P(q)->centerX = 1;
        P(q)->defaultMask = 1;
        P(q)->right = s_quitLayout;
        P(q)->upItem = row;
        P(row)->downItem = q;
        s_quitRow[e] = q;
        addEntryLayout(e, row, q);
    }
}

static void build(void)
{
    /* Extras (after Achievements), from both entries (its Music,
       Models and Credits only from the title, layoutPage) */
    /* Save a picture for a bug report above Developer mode, always shown
       (with a host handler), and in an rc build Share a bug report below it
       (ICO_RC_TOOLS, on a phone); Dump textures and Dump models under
       Developer mode, shown while it is on */
    static const int mainOpts[] = {
        UI_OPT_LINK,           UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_LINK,
        UI_OPT_LINK,           UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_LANGUAGE,
        UI_OPT_MENU_LANGUAGE,  UI_OPT_DISC,          UI_OPT_LINK,        UI_OPT_LINK,
        UI_OPT_REPORT_PICTURE,
#if ICO_RC_TOOLS
        UI_OPT_SHARE_REPORT,
#endif
        UI_OPT_DEVELOPER,      UI_OPT_DUMP_TEXTURES, UI_OPT_DUMP_MODELS, UI_OPT_BACK};
    static const int mainStrs[] = {UI_STR_SECTION_DISPLAY,      UI_STR_SECTION_EFFECTS,
                                   UI_STR_SECTION_GRAPHICS,     UI_STR_SECTION_GPU_DRIVER,
                                   UI_STR_SECTION_AUDIO,        UI_STR_SECTION_CONTROLS,
                                   UI_STR_SECTION_GAMEPLAY,     UI_STR_SECTION_LANGUAGE,
                                   UI_STR_OPT_MENU_LANGUAGE,    UI_STR_OPT_DISC,
                                   UI_STR_SECTION_ACHIEVEMENTS, UI_STR_EXTRAS,
                                   UI_STR_OPT_REPORT_PICTURE,
#if ICO_RC_TOOLS
                                   UI_STR_OPT_SHARE_REPORT,
#endif
                                   UI_STR_OPT_DEVELOPER_MODE,   UI_STR_OPT_DUMP_TEXTURES,
                                   UI_STR_OPT_DUMP_MODELS,      UI_STR_BACK};
    static const int mainLinks[] = {UI_PAGE_DISPLAY,
                                    UI_PAGE_EFFECTS,
                                    UI_PAGE_GRAPHICS,
                                    UI_PAGE_GPU_DRIVER,
                                    UI_PAGE_AUDIO,
                                    UI_PAGE_CONTROLS,
                                    UI_PAGE_GAMEPLAY,
                                    -1,
                                    -1,
                                    -1,
                                    UI_PAGE_ACHIEVEMENTS,
                                    UI_PAGE_EXTRAS,
                                    -1,
#if ICO_RC_TOOLS
                                    -1,
#endif
                                    -1,
                                    -1,
                                    -1,
                                    -1};
    /* the Graphics API page (its link needs two or more renderers) */
    static const int apiOpts[] = {UI_OPT_GRAPHICS_API, UI_OPT_BACK};
    static const int apiStrs[] = {UI_STR_OPT_GRAPHICS_API, UI_STR_BACK};
    /* Android's Graphics driver page (its link needs a host) */
    static const int gpuOpts[] = {UI_OPT_GPU_DRIVER, UI_OPT_GPU_ADD, UI_OPT_GPU_ADD_FILE,
                                  UI_OPT_GPU_REMOVE, UI_OPT_BACK};
    static const int gpuStrs[] = {UI_STR_OPT_GPU_DRIVER, UI_STR_GPU_DRIVER_ADD,
                                  UI_STR_GPU_DRIVER_ADD_FILE, UI_STR_GPU_DRIVER_REMOVE,
                                  UI_STR_BACK};
    static const int fxOpts[] = {UI_OPT_FULL_HEIGHT,
                                 UI_OPT_FILTER,
                                 UI_OPT_BRIGHTNESS,
                                 UI_OPT_CRT,
                                 UI_OPT_CRT_STRENGTH,
                                 UI_OPT_EFFECT_GLOW,
                                 UI_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_OPT_EFFECT_SOFTENING,
                                 UI_OPT_EFFECT_MOTION_BLUR,
                                 UI_OPT_EFFECT_FOG,
                                 UI_OPT_EFFECT_WATER,
                                 UI_OPT_EFFECT_CINEMATIC_BARS,
                                 UI_OPT_FULL_PIXEL,
                                 UI_OPT_BACK};
    static const int fxStrs[] = {UI_STR_OPT_FULL_HEIGHT,
                                 UI_STR_OPT_FILTERING,
                                 UI_STR_OPT_BRIGHTNESS,
                                 UI_STR_OPT_CRT,
                                 UI_STR_OPT_CRT_STRENGTH,
                                 UI_STR_OPT_EFFECT_GLOW,
                                 UI_STR_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_STR_OPT_EFFECT_SOFTENING,
                                 UI_STR_OPT_EFFECT_MOTION_BLUR,
                                 UI_STR_OPT_EFFECT_FOG,
                                 UI_STR_OPT_EFFECT_WATER,
                                 UI_STR_OPT_EFFECT_CINEMATIC_BARS,
                                 UI_STR_OPT_FULL_PIXEL,
                                 UI_STR_BACK};
    /* Characters after Credits (from both entries; the other three
       from the title only) */
    static const int extrasOpts[] = {
        UI_OPT_EXTRAS_MUSIC, UI_OPT_EXTRAS_MODELS, UI_OPT_EXTRAS_CREDITS,
        UI_OPT_LINK,         UI_OPT_EXTRAS_STEAM,  UI_OPT_ACH_POPUPS,
        UI_OPT_BACK};
    static const int extrasStrs[] = {UI_STR_EXTRAS_MUSIC,
                                     UI_STR_EXTRAS_MODELS,
                                     UI_STR_EXTRAS_CREDITS,
                                     UI_STR_SECTION_CHARACTERS,
                                     UI_STR_EXTRAS_STEAM,
                                     UI_STR_OPT_ACH_POPUPS,
                                     UI_STR_BACK};
    static const int extrasLinks[] = {-1, -1, -1, UI_PAGE_CHARACTERS, -1, -1, -1};
    /* the characters' colours, one row per IcoAppPart; Switch shows
       only inside the model viewer */
    static const int charOpts[] = {UI_OPT_CHAR_ICO_SKIN,
                                   UI_OPT_CHAR_ICO_PONCHO_NAVY,
                                   UI_OPT_CHAR_ICO_PONCHO_PINK,
                                   UI_OPT_CHAR_ICO_PONCHO_LIGHT,
                                   UI_OPT_CHAR_ICO_PONCHO_DARK,
                                   UI_OPT_CHAR_ICO_TUNIC,
                                   UI_OPT_CHAR_ICO_SHORTS,
                                   UI_OPT_CHAR_YORDA_SKIN,
                                   UI_OPT_CHAR_YORDA_DRESS,
                                   UI_OPT_CHAR_SWITCH,
                                   UI_OPT_CHAR_RANDOMIZE,
                                   UI_OPT_CHAR_RESET,
                                   UI_OPT_BACK};
    static const int charStrs[] = {UI_STR_CHAR_ICO_SKIN,
                                   UI_STR_CHAR_ICO_PONCHO_NAVY,
                                   UI_STR_CHAR_ICO_PONCHO_PINK,
                                   UI_STR_CHAR_ICO_PONCHO_LIGHT,
                                   UI_STR_CHAR_ICO_PONCHO_DARK,
                                   UI_STR_CHAR_ICO_TUNIC,
                                   UI_STR_CHAR_ICO_SHORTS,
                                   UI_STR_CHAR_YORDA_SKIN,
                                   UI_STR_CHAR_YORDA_DRESS,
                                   UI_STR_CHAR_SWITCH_YORDA,
                                   UI_STR_CHAR_RANDOMIZE,
                                   UI_STR_CHAR_RESET,
                                   UI_STR_BACK};
    /* every Display row always shown, Frame rate included; Full-height
       picture, Texture filtering and Brightness (the pause menu's) are the
       Effects page's, first there */
    static const int dispOpts[] = {UI_OPT_PRESET,       UI_OPT_RESOLUTION,  UI_OPT_ASPECT,
                                   UI_OPT_ANTI_ALIAS,   UI_OPT_WINDOW_MODE, UI_OPT_VSYNC,
                                   UI_OPT_TEXTURE_PACK, UI_OPT_MODEL_PACK,  UI_OPT_FRAMERATE,
                                   UI_OPT_VIDEO_MODE,   UI_OPT_BACK};
    static const int dispStrs[] = {UI_STR_OPT_PRESET,
                                   UI_STR_OPT_RESOLUTION,
                                   UI_STR_OPT_ASPECT,
                                   UI_STR_OPT_AA,
                                   UI_STR_OPT_WINDOW_MODE,
                                   UI_STR_OPT_VSYNC,
                                   UI_STR_OPT_TEXTURE_PACK,
                                   UI_STR_OPT_MODEL_PACK,
                                   UI_STR_OPT_FRAMERATE,
                                   UI_STR_OPT_VIDEO_MODE,
                                   UI_STR_BACK};
    static const int audioOpts[] = {UI_OPT_VOLUME, UI_OPT_MUSIC,  UI_OPT_EFFECTS,
                                    UI_OPT_OUTPUT, UI_OPT_DEVICE, UI_OPT_BACK};
    static const int audioStrs[] = {UI_STR_OPT_VOLUME, UI_STR_OPT_MUSIC_VOL, UI_STR_OPT_EFFECTS_VOL,
                                    UI_STR_OPT_OUTPUT, UI_STR_OPT_DEVICE,    UI_STR_BACK};
    /* the game's Button configuration, Vibration and Hold type after
       Remap, from the pause menu; the Touch screen page's link before
       Back, with a touch screen; the mouse camera's rows (not on
       Android) */
    static const int ctlOpts[] = {UI_OPT_LINK,        UI_OPT_BUTTON_CONFIG, UI_OPT_VIBRATION,
                                  UI_OPT_HOLD_TYPE,   UI_OPT_STICK_FIX,     UI_OPT_MOUSE_CAMERA,
                                  UI_OPT_MOUSE_SENS,  UI_OPT_MOUSE_INVERT,  UI_OPT_MOUSE_SPEED,
                                  UI_OPT_MOUSE_RANGE, UI_OPT_MOUSE_RETURN,  UI_OPT_CIRCLE_BACK,
                                  UI_OPT_LINK,        UI_OPT_BACK};
    static const int ctlStrs[] = {UI_STR_OPT_REMAP,        UI_STR_OPT_BUTTON_CONFIG,
                                  UI_STR_OPT_VIBRATION,    UI_STR_OPT_HOLD_TYPE,
                                  UI_STR_OPT_STICK_FIX,    UI_STR_OPT_MOUSE_CAMERA,
                                  UI_STR_OPT_MOUSE_SENS,   UI_STR_OPT_MOUSE_INVERT,
                                  UI_STR_OPT_MOUSE_SPEED,  UI_STR_OPT_MOUSE_RANGE,
                                  UI_STR_OPT_MOUSE_RETURN, UI_STR_OPT_CIRCLE_BACK,
                                  UI_STR_SECTION_TOUCH,    UI_STR_BACK};
    static const int ctlLinks[] = {UI_PAGE_REMAP, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                   UI_PAGE_TOUCH, -1};
    /* Controls > Touch screen: the overlay's three rows, then the layout
       editor's (Edit layout opens UI_PAGE_TOUCH_LAYOUT) */
    static const int touchOpts[] = {
        UI_OPT_TOUCH_MODE,  UI_OPT_TOUCH_SIZE,      UI_OPT_TOUCH_OPACITY,
        UI_OPT_TOUCH_EDIT,  UI_OPT_TOUCH_SNAP,      UI_OPT_TOUCH_GRID,
        UI_OPT_TOUCH_RESET, UI_OPT_TOUCH_RESET_ALL, UI_OPT_BACK};
    static const int touchStrs[] = {
        UI_STR_OPT_TOUCH_MODE, UI_STR_OPT_TOUCH_SIZE,  UI_STR_OPT_TOUCH_OPACITY,
        UI_STR_OPT_TOUCH_EDIT, UI_STR_OPT_TOUCH_SNAP,  UI_STR_OPT_TOUCH_GRID,
        UI_STR_TOUCH_RESET,    UI_STR_TOUCH_RESET_ALL, UI_STR_BACK};
    static const int touchLinks[] = {-1, -1, -1, UI_PAGE_TOUCH_LAYOUT, -1, -1, -1, -1, -1};
    /* the game's Film effect and Players, once the game is cleared */
    static const int gameOpts[] = {
        UI_OPT_YORDA,       UI_OPT_STICK_FIX, UI_OPT_DOOR_FIX, UI_OPT_NATURAL_CHAINS,
        UI_OPT_FILM_EFFECT, UI_OPT_PLAYERS,   UI_OPT_BACK};
    static const int gameStrs[] = {UI_STR_OPT_YORDA,
                                   UI_STR_OPT_STICK_FIX,
                                   UI_STR_OPT_DOOR_FIX,
                                   UI_STR_OPT_NATURAL_CHAINS,
                                   UI_STR_OPT_FILM_EFFECT,
                                   UI_STR_OPT_PLAYERS,
                                   UI_STR_BACK};
    _Static_assert(sizeof(dispOpts) == sizeof(dispStrs), "a string for each Display row");
    _Static_assert(sizeof(ctlOpts) == sizeof(ctlStrs) && sizeof(ctlOpts) == sizeof(ctlLinks),
                   "a string and a link for each Controls row");
    _Static_assert(sizeof(touchOpts) == sizeof(touchStrs) &&
                       sizeof(touchOpts) == sizeof(touchLinks),
                   "a string and a link for each Touch screen row");
    _Static_assert(N_OF(touchOpts) <= MAX_ROWS, "the touchOpts rows fit the page's row budget");
    _Static_assert(sizeof(fxOpts) == sizeof(fxStrs), "a string for each Effects row");
    _Static_assert(sizeof(gpuOpts) == sizeof(gpuStrs), "a string for each Graphics driver row");
    _Static_assert(sizeof(gameOpts) == sizeof(gameStrs), "a string for each Gameplay row");
    _Static_assert(sizeof(extrasOpts) == sizeof(extrasStrs) &&
                       sizeof(extrasOpts) == sizeof(extrasLinks),
                   "a string and a link for each Extras row");
    _Static_assert(sizeof(charOpts) == sizeof(charStrs), "a string for each Characters row");
    _Static_assert(N_OF(charOpts) <= MAX_ROWS, "the Characters rows fit");
    _Static_assert(N_OF(mainOpts) <= MAX_ROWS, "the mainOpts rows fit the page's row budget");
    _Static_assert(N_OF(dispOpts) <= MAX_ROWS, "the dispOpts rows fit the page's row budget");
    _Static_assert(N_OF(audioOpts) <= MAX_ROWS, "the audioOpts rows fit the page's row budget");
    _Static_assert(N_OF(fxOpts) <= MAX_ROWS, "the fxOpts rows fit the page's row budget");
    _Static_assert(N_OF(gpuOpts) <= MAX_ROWS, "the gpuOpts rows fit the page's row budget");
    _Static_assert(N_OF(ctlOpts) <= MAX_ROWS, "the ctlOpts rows fit the page's row budget");
    _Static_assert(N_OF(gameOpts) <= MAX_ROWS, "the gameOpts rows fit the page's row budget");
    _Static_assert(N_OF(extrasOpts) <= MAX_ROWS, "the extrasOpts rows fit the page's row budget");

    ui_font_init(); /* the notes are wrapped by measuring */
    memset(s_pages, 0, sizeof(s_pages));
    /* the pages first (their layouts are the links' targets), then the
       entry rows */
    _Static_assert(sizeof(mainOpts) == sizeof(mainStrs) && sizeof(mainOpts) == sizeof(mainLinks),
                   "a string and a link for each main row");
    buildOptionPage(UI_PAGE_MAIN, UI_STR_SETTINGS, mainOpts, mainStrs, mainLinks, N_OF(mainOpts),
                    -1);
    buildOptionPage(UI_PAGE_DISPLAY, UI_STR_SECTION_DISPLAY, dispOpts, dispStrs, NULL,
                    N_OF(dispOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_AUDIO, UI_STR_SECTION_AUDIO, audioOpts, audioStrs, NULL,
                    N_OF(audioOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_CONTROLS, UI_STR_SECTION_CONTROLS, ctlOpts, ctlStrs, ctlLinks,
                    N_OF(ctlOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_TOUCH, UI_STR_SECTION_TOUCH, touchOpts, touchStrs, touchLinks,
                    N_OF(touchOpts), UI_PAGE_CONTROLS);
    buildTouchEditorPage();
    buildOptionPage(UI_PAGE_GAMEPLAY, UI_STR_SECTION_GAMEPLAY, gameOpts, gameStrs, NULL,
                    N_OF(gameOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_EFFECTS, UI_STR_SECTION_EFFECTS, fxOpts, fxStrs, NULL, N_OF(fxOpts),
                    UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_GRAPHICS, UI_STR_SECTION_GRAPHICS, apiOpts, apiStrs, NULL,
                    N_OF(apiOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_GPU_DRIVER, UI_STR_SECTION_GPU_DRIVER, gpuOpts, gpuStrs, NULL,
                    N_OF(gpuOpts), UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_EXTRAS, UI_STR_EXTRAS, extrasOpts, extrasStrs, extrasLinks,
                    N_OF(extrasOpts), UI_PAGE_MAIN);
    /* the layout extension's budget (layout_ext.h): the Characters page
       is 50 rows (its heading, nine colour rows of four: label, value and
       the two arrows, Randomize, Reset, Back, nine swatches, the note) and
       one layout, and the model viewer's panel on it 15 more (Switch, the
       prompt line's six words and eight glyphs).  settings_test's
       testBudget keeps 64 rows and 4 layouts spare; developer mode logs
       the count at install */
    buildOptionPage(UI_PAGE_CHARACTERS, UI_STR_SECTION_CHARACTERS, charOpts, charStrs, NULL,
                    N_OF(charOpts), UI_PAGE_EXTRAS);
    buildListPage(UI_PAGE_ACHIEVEMENTS, UI_STR_SECTION_ACHIEVEMENTS, &kAchDef, UI_PAGE_MAIN);
    buildListPage(UI_PAGE_REMAP, UI_STR_OPT_REMAP, &kRemapDef, UI_PAGE_CONTROLS);
    buildListPage(UI_PAGE_MUSIC, UI_STR_EXTRAS_MUSIC, &kGalDef, UI_PAGE_EXTRAS);
    /* the section rows open their pages (and Edit layout its page: a row
       of its own opt with a link, refreshPage locks it) */
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        for (int i = 0; i < s_pages[p].count; i++) {
            Row *r = &s_pages[p].rows[i];
            if (r->link >= 0) {
                P(r->label)->right = s_pages[r->link].layout;
            }
        }
    }
    /* Button configuration opens the game's own screen (la_key_config
       comes back through ui_settings_key_config_back) */
    {
        const int bc = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG);
        if (bc >= 0) {
            P(bc)->right = LAYOUT_KEY_CONFIG;
        }
    }
    buildQuitScreen();
    buildEntries();
    if (s_photoRow >= 0) {
        P(s_photoRow)->right = ui_photo_build();
    }
    buildNewGameScreen();
    if (ico_opt_developer_mode()) {
        /* the layout extension's budget (layout_ext.h): what Settings and
           its screens use of the tables */
        fprintf(stderr, "settings: layout extension: %d of %d properties, %d of %d layouts\n",
                lt_ext_prop_count(), LT_EXT_MAX_PROPERTIES, lt_ext_layout_count(),
                LT_EXT_MAX_LAYOUTS);
    }
}

/* ------------------------------------------------------ the New Game screen
 * The New Game screen, between the vibration choice and the start
 * (settings.h ui_new_game_screen_enter): two labelled rows, "Mirror mode" and
 * "New Game+", each with "Off" and "On" side by side (left/right through
 * their item links, up/down between the rows), and a line of explanation
 * for the row the cursor is on, in the lower half where the vibration
 * screen has its rows (the title stage's logo is above).  Each row keeps
 * its own choice: the cursor moving down or up lands on the other row's
 * chosen item, and that row's choice stays lit while the cursor is away. */

static int s_newGameLayout = -1;
static int s_newGameRow[2][2] = {{-1, -1}, {-1, -1}}; /* [row][off, on] */
static int s_newGameNote = -1;
static int s_newGameChosen;
static int s_mirrorChoice, s_ngpChoice; /* each row's Off (0) or On (1) */

#define LAYOUT_VIBE_SELECT (ico_region_cur()->layout_vibe) /* la_vibe_select's screen (9) */
#define NEW_GAME_ROW_Y 112
#define NEW_GAME_ROW_H 34
#define NEW_GAME_NOTE_Y 182 /* with its 30 lines, clear of the layout's bottom (212) */

static int newGameScreenProc(int first, int item);

static void buildNewGameScreen(void)
{
    static const int kLabel[2] = {UI_STR_OPT_MIRROR, UI_STR_OPT_NEWGAME_PLUS};
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    for (int r = 0; r < 2; r++) {
        const int y = NEW_GAME_ROW_Y + r * NEW_GAME_ROW_H;
        /* the label right-aligned up to x 300, Off and On after it */
        ui_settings_add_row(40, y, 260, NEW_GAME_ROW_H, 0, -1, kLabel[r], NULL, 0.0f,
                            UI_ALIGN_RIGHT);
        s_newGameRow[r][0] = ui_settings_add_row(320, y, 110, NEW_GAME_ROW_H, 1, -1, UI_STR_OFF,
                                                 NULL, 0.0f, UI_ALIGN_CENTER);
        s_newGameRow[r][1] = ui_settings_add_row(440, y, 110, NEW_GAME_ROW_H, 1, -1, UI_STR_ON,
                                                 NULL, 0.0f, UI_ALIGN_CENTER);
        P(s_newGameRow[r][0])->rightItem = s_newGameRow[r][1];
        P(s_newGameRow[r][1])->leftItem = s_newGameRow[r][0];
    }
    s_newGameNote = ui_settings_add_row(20, NEW_GAME_NOTE_Y, 600, 30, 0, -1, 0, " ", NOTE_SIZE,
                                        UI_ALIGN_CENTER);
    P(s_newGameNote)->centerX = 1;
    setNote(s_newGameNote, UI_STR_MIRROR_SCREEN);
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    s_newGameLayout = addLayout(first, last + 1, 0.6f, newGameScreenProc, s_newGameRow[0][0]);
}

/* The rows' state from the cursor: the cursor's row takes the item it is
   on as its choice; up and down land on the other row's choice (the two
   rows in a loop, as the Settings pages'); the choice of the row the
   cursor is not on stays lit (an item whose owner is the cursor's item is
   not dimmed, layout_texture.c display_texture); the note explains the
   cursor's row. */
static void newGameSync(void)
{
    const int cur = lt_ext_layout(s_newGameLayout)->curItem;
    int curRow = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            if (cur == s_newGameRow[r][c]) {
                curRow = r;
                *(r == 0 ? &s_mirrorChoice : &s_ngpChoice) = c;
            }
        }
    }
    const int choice[2] = {s_mirrorChoice, s_ngpChoice};
    for (int r = 0; r < 2; r++) {
        const int other = s_newGameRow[1 - r][choice[1 - r]];
        for (int c = 0; c < 2; c++) {
            LtProperty *e = P(s_newGameRow[r][c]);
            e->downItem = e->upItem = other;
            e->ownerItem = r != curRow && c == choice[r] ? cur : -1;
        }
    }
    setNote(s_newGameNote, curRow == 0 ? UI_STR_MIRROR_SCREEN : UI_STR_NEWGAME_PLUS_SCREEN);
}

int ui_new_game_screen_enter(void)
{
    if (!s_built || s_newGameLayout < 0) {
        return -1;
    }
    LtProp *l = lt_ext_layout(s_newGameLayout);
    l->defaultItem = l->curItem = s_newGameRow[0][0];
    s_newGameChosen = 0;
    /* no run until the choice: a cleared save's new game (la_load_processing
       returns to the vibration screen) does not keep the loaded slot's flag
       on the title stage behind this screen */
    ico_opt_mirror_reset();
    s_mirrorChoice = 0;
    /* New Game+ follows how the player came here: on from a finished save
       (la_load_processing carries its cleared flag into the new game), off
       from the title's New Game (which clears the flag) */
    s_ngpChoice = gFlagGameClear ? 1 : 0;
    newGameSync();
    return s_newGameLayout;
}

int ui_new_game_screen_layout(void)
{
    return s_newGameLayout;
}

int ui_new_game_screen_row(int row, int on)
{
    return row == 0 || row == 1 ? s_newGameRow[row][on ? 1 : 0] : -1;
}

static int newGameScreenProc(int first, int item)
{
    (void)item;
    ui_settings_apply_language();
    if (first) {
        s_newGameChosen = 0;
    }
    newGameSync();
    /* as la_vibe_select: input only once faded in, and the choice once (the
       stage change stops the layout procs in the same tick) */
    if (s_newGameChosen || lt_fade_status() != 2) {
        return -1;
    }
    int flags = pad[0].flags;
    if (flags & (PAD_OK | LT_PAD_START)) {
        s_newGameChosen = 1;
        POSITIVE_SE();
        ico_opt_set_mirror(s_mirrorChoice);
        /* the game's only record of a second playthrough: the chosen value
           wins over the save's (la_host_new_game_go's gflagInit leaves it),
           and the save and every place the game checks it follow */
        gFlagGameClear = s_ngpChoice;
        la_host_new_game_go();
        return -1;
    }
    if (flags & PAD_BACK) {
        NEGATIVE_SE();
        return LAYOUT_VIBE_SELECT;
    }
    return -1;
}

/* ---------------------------------------------------------- the quit screen
 * "Quit to desktop?" with Yes and No side by side (left/right through
 * their item links, the cursor on No), opened by the title's Quit row
 * (settings.h ui_settings_set_quit_handler), laid out as the New Game screen. */

static int s_quitYesNo[2] = {-1, -1}; /* No, Yes */
static int s_quitChosen;
static void (*s_quitHandler)(void);

static int quitScreenProc(int first, int item);

static void buildQuitScreen(void)
{
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    int h = ui_settings_add_row(20, 112, 600, 40, 0, -1, quitConfirmStr(), NULL, HEADER_SIZE,
                                UI_ALIGN_CENTER);
    P(h)->centerX = 1;
    s_quitHeader = h;
    s_quitYesNo[1] =
        ui_settings_add_row(200, 146, 110, 40, 1, -1, UI_STR_MT_YES, NULL, 0.0f, UI_ALIGN_CENTER);
    s_quitYesNo[0] =
        ui_settings_add_row(330, 146, 110, 40, 1, -1, UI_STR_MT_NO, NULL, 0.0f, UI_ALIGN_CENTER);
    P(s_quitYesNo[1])->rightItem = s_quitYesNo[0];
    P(s_quitYesNo[0])->leftItem = s_quitYesNo[1];
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    s_quitLayout = addLayout(first, last + 1, 0.6f, quitScreenProc, s_quitYesNo[0]);
}

int ui_settings_capturing(void)
{
    return s_capture.active || s_capture.cooldown > 0;
}

void ui_settings_set_window_mode_query(int (*fn)(void))
{
    s_windowModeQuery = fn;
}

void ui_settings_set_aa_mask_query(unsigned (*fn)(void))
{
    s_aaMaskQuery = fn;
}

void ui_settings_set_aa_blocked_query(int (*fn)(void))
{
    s_aaBlockedQuery = fn;
}

void ui_settings_set_model_pack_count(int (*fn)(void))
{
    s_modelPackCount = fn;
}

void ui_settings_set_version(const char *label)
{
    snprintf(s_version, sizeof(s_version), "%s", label != NULL ? label : "");
}

int ui_settings_version_row(void)
{
    return s_built ? s_pages[UI_PAGE_MAIN].version : -1;
}

void ui_settings_set_texture_pack_count(int (*fn)(void))
{
    s_texturePackCount = fn;
}

void ui_settings_set_touch_query(int (*fn)(void))
{
    s_touchQuery = fn;
}

void ui_settings_set_touch_edit_host(const UiTouchEditHost *h)
{
    s_touchEditHostSet = h != NULL;
    if (h != NULL) {
        s_touchEditHost = *h;
    } else {
        memset(&s_touchEditHost, 0, sizeof(s_touchEditHost));
    }
}

void ui_settings_set_quit_is_game(int (*fn)(void))
{
    s_quitIsGame = fn;
    if (s_built && s_quitHeader >= 0) {
        /* installed after the build: the labels follow */
        lt_ext_set_str(s_quitHeader, quitConfirmStr());
        for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
            if (s_quitRow[e] >= 0) {
                lt_ext_set_str(s_quitRow[e], quitRowStr());
            }
        }
    }
}

void ui_settings_set_quit_handler(void (*fn)(void))
{
    s_quitHandler = fn;
}

int ui_quit_screen_layout(void)
{
    return s_built ? s_quitLayout : -1;
}

int ui_quit_screen_row(int yes)
{
    return s_quitYesNo[yes ? 1 : 0];
}

static void requestQuit(void)
{
    /* what a Settings page writes when it is left, should anything be
       pending; the achievements and the audio are the atexit handlers' */
    ui_settings_save();
    if (s_quitHandler != NULL) {
        s_quitHandler();
        return;
    }
    fprintf(stderr, "settings: quit to desktop\n");
    fflush(stderr);
    exit(0);
}

static int quitScreenProc(int first, int item)
{
    (void)item;
    ui_settings_apply_language();
    LtProp *l = lt_ext_layout(s_quitLayout);
    if (first) {
        s_quitChosen = 0; /* the switch put the cursor on No (the default) */
    }
    if (s_quitChosen || lt_fade_status() != 2) {
        return -1;
    }
    int flags = pad[0].flags;
    int to = s_origin == LAYOUT_TITLE_CONTINUE ? LAYOUT_TITLE_CONTINUE : LAYOUT_TITLE_NEW;
    if ((flags & PAD_OK) && l->curItem == s_quitYesNo[1]) {
        s_quitChosen = 1;
        POSITIVE_SE();
        requestQuit();
        return -1;
    }
    if (flags & (PAD_OK | PAD_BACK)) {
        NEGATIVE_SE();
        la_host_leave();
        gameCursorOn(to, s_quitRow[to == LAYOUT_TITLE_NEW ? ENTRY_TITLE13 : ENTRY_TITLE12]);
        return to;
    }
    return -1;
}

/* whether the title's game rows are at (Continue, New Game, copyright) */
static int titleRowsAt(int cont, int newGame, int copyright)
{
    return texProperty[ROW_TITLE_CONTINUE].dispY == cont &&
           texProperty[ROW_TITLE_NEW].dispY == newGame &&
           texProperty[ROW_TITLE_NEW_ONLY].dispY == newGame &&
           texProperty[ROW_TITLE_COPYRIGHT].dispY == copyright &&
           (ROW_TITLE_COPYRIGHT2 < 0 || texProperty[ROW_TITLE_COPYRIGHT2].dispY == copyright);
}

/* The title's rows on one pitch (TITLE_Y): the game's rows moved in the
   loaded table (the textures and the menu text alike, so classic menu
   text gets the same places), the port rows of both title layouts in New
   Game's box height at the game rows' size (UI_MENU_TEXT_SIZE), centred as
   they are.  New Game keeps its place on the New-Game-only layout (51 is
   50's y, as in the PAL data, so the switch from 12 to 13 still pairs
   them in the fade-cancel check). */
static void placeTitle(void)
{
    texProperty[ROW_TITLE_CONTINUE].dispY = TITLE_Y(0);
    texProperty[ROW_TITLE_NEW].dispY = texProperty[ROW_TITLE_NEW_ONLY].dispY = TITLE_Y(1);
    texProperty[ROW_TITLE_COPYRIGHT].dispY = TITLE_COPYRIGHT_Y;
    if (ROW_TITLE_COPYRIGHT2 >= 0) {
        texProperty[ROW_TITLE_COPYRIGHT2].dispY = TITLE_COPYRIGHT_Y;
    }
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        const int rows[2] = {s_entryRow[e], s_quitRow[e]};
        for (int i = 0; i < 2; i++) {
            if (rows[i] >= 0) {
                P(rows[i])->dispY = TITLE_Y(2 + i);
                P(rows[i])->dispH = texProperty[ROW_TITLE_NEW].dispH;
                /* lettered as the title's words are (the Japanese title sheet; the Korean
                   disc has no title variant: the same geometry) */
                lt_ext_set_title_sheet(rows[i], 1);
            }
        }
    }
}

/* The pause menu: "Photo mode" one pitch under Options while a stage
   runs (ui_photo_available), with Back (295) one pitch lower to make room
   (End Game, 296, is further down in the PAL data); otherwise masked and
   stepped over through the item links (layout_texture.c's visibility skip
   does not look at masks), Back in its own place: Options -> Back and Back
   -> Options directly, as without the row.  The row's letters start where the
   pause rows' do. */
static void placePause(void)
{
    LtProperty *opt = &texProperty[ROW_PAUSE_OPTIONS], *back = &texProperty[ROW_PAUSE_BACK];
    if (s_photoRow < 0) {
        return;
    }
    const int on = ui_photo_available();
    LtProperty *ph = P(s_photoRow);
    ph->dispX = opt->dispX + PAUSE_LETTERS_IN;
    ph->dispY = opt->dispY + PAUSE_PITCH;
    ph->dispH = opt->dispH;
    ph->defaultMask = on ? 0 : 1;
    lt_mask_property(s_photoRow, on ? 0 : 1);
    ph->upItem = ROW_PAUSE_OPTIONS;
    ph->downItem = ROW_PAUSE_BACK;
    back->dispY = opt->dispY + (on ? 2 : 1) * PAUSE_PITCH;
    opt->downItem = on ? s_photoRow : ROW_PAUSE_BACK;
    back->upItem = on ? s_photoRow : ROW_PAUSE_OPTIONS;
}

/* whether the pause menu's rows are the PAL ones (or as placePause and
   repoint left them) */
static int pauseRowsOk(void)
{
    const LtProperty *o = &texProperty[ROW_PAUSE_OPTIONS], *b = &texProperty[ROW_PAUSE_BACK];
    const int ph = s_photoRow;
    return texLayout[LAYOUT_PAUSE].first == ico_region_cur()->pause_first &&
           texLayout[LAYOUT_PAUSE].last == ico_region_cur()->pause_last &&
           (o->right == LAYOUT_GAME_OPTIONS || o->right == s_pages[UI_PAGE_MAIN].layout) &&
           (o->downItem == ROW_PAUSE_BACK || o->downItem == ph) &&
           (b->upItem == ROW_PAUSE_OPTIONS || b->upItem == ph) &&
           (b->dispY == o->dispY + PAUSE_PITCH || b->dispY == o->dispY + 2 * PAUSE_PITCH) &&
           texProperty[ROW_PAUSE_END].dispY >= o->dispY + 3 * PAUSE_PITCH;
}

/* The game's rows, pointed at the entry rows.  Checked against the loaded
   tables first: a table that does not look like the PAL data is left
   alone (logged once). */
static void repoint(void)
{
    if (!pauseRowsOk() || texLayout[LAYOUT_TITLE_CONTINUE].first != ROW_TITLE_CONTINUE ||
        texLayout[LAYOUT_TITLE_NEW].first != ROW_TITLE_NEW_ONLY ||
        !(titleRowsAt(PAL_CONTINUE_Y, PAL_NEW_GAME_Y, PAL_COPYRIGHT_Y) ||
          titleRowsAt(TITLE_Y(0), TITLE_Y(1), TITLE_COPYRIGHT_Y))) {
        if (!s_warned) {
            fprintf(stderr, "settings: the layout tables are not the expected ones; no entry\n");
            s_warned = 1;
        }
        return;
    }
    placeTitle();
    /* the pause menu's Options opens Settings (the game's Options
       screen, 58, is not reached: its settings are on the pages) */
    texProperty[ROW_PAUSE_OPTIONS].right = s_pages[UI_PAGE_MAIN].layout;
    placePause();
    const int gameRow[ENTRY_COUNT] = {0, ROW_TITLE_NEW, ROW_TITLE_NEW_ONLY};
    for (int e = 0; e < ENTRY_COUNT; e++) {
        int g = entryGame(e);
        if (e != ENTRY_PAUSE) {
            texProperty[gameRow[e]].downItem = s_entryRow[e];
            P(s_entryRow[e])->upItem = gameRow[e];
        }
        if (texLayout[g].link != s_entryLayout[e]) {
            lt_ext_layout(s_entryLayout[e])->link = texLayout[g].link;
            texLayout[g].link = s_entryLayout[e];
        }
    }
}

#ifdef ICO_RD
/* the renderer follows the run's mirror mode (options.h listener) */
static void mirrorChanged(int on)
{
    rd_set_mirror(on);
}
#endif

void ui_settings_install(void)
{
    /* [game] circle_back, before the game's menus read a press */
    lt_ext_set_circle_back(ico_opt_circle_back());
    if (s_origin < 0) {
        s_origin = LAYOUT_PAUSE;
    }
    /* the port's text in the menu language ([ui] language) */
    ui_settings_apply_language();
    /* [audio] output: an explicit stereo or mono is the game's from the
       start (the card's system file is read later: fumi/ios/mcard.c's hook) */
    applyOutputMode(0);
    /* [gameplay] hold_type, before any pad word is read */
    applyHoldType();
    if (!s_built) {
        build();
        s_built = 1;
#ifdef ICO_RD
        ico_opt_set_mirror_listener(mirrorChanged);
#endif
    }
    repoint();
}

void ui_settings_reset(void)
{
    s_built = 0;
    s_warned = 0;
    s_origin = LAYOUT_PAUSE;
    s_restoreTitle = -1;
    s_titleReturn = -1;
    s_titleDecided = s_titleHandoff = 0;
    s_dirtyVideo = s_dirtyConfig = s_dirtyBindings = s_dirtyTouch = 0;
    ui_touch_edit_page_reset();
    memset(&s_capture, 0, sizeof(s_capture));
    for (int e = 0; e < ENTRY_COUNT; e++) {
        s_entryRow[e] = s_entryLayout[e] = s_quitRow[e] = -1;
    }
    s_photoRow = -1;
    s_statsBuilt = 0;
    s_statBack = -1;
    for (int i = 0; i < STAT_LINES; i++) {
        s_statLabel[i] = s_statValue[i] = -1;
    }
    ui_photo_reset();
    memset(s_pages, 0, sizeof(s_pages));
    /* the wrapped notes are set again on the rebuilt rows */
    memset(s_noteStr, 0, sizeof(s_noteStr));
    memset(s_noteLang, 0, sizeof(s_noteLang));
    s_newGameLayout = -1;
    s_newGameRow[0][0] = s_newGameRow[0][1] = s_newGameRow[1][0] = s_newGameRow[1][1] = -1;
    s_newGameNote = -1;
    s_newGameChosen = s_mirrorChoice = s_ngpChoice = 0;
    s_quitLayout = s_quitYesNo[0] = s_quitYesNo[1] = -1;
    s_quitChosen = 0;
    for (int i = 0; i < ICO_APP_PART_COUNT; i++) {
        s_charSwatch[i] = -1;
    }
    memset(&s_charHint, 0, sizeof(s_charHint));
}

void ui_settings_title_return(int page)
{
    s_titleReturn = (page >= 0 && page < UI_PAGE_COUNT) ? page : -1;
    s_titleDecided = 0;
    s_titleHandoff = 0;
}

int ui_settings_title_return_pending(void)
{
    return s_titleReturn >= 0;
}

int ui_settings_title_decided(void)
{
    return s_titleDecided;
}

int ui_settings_entry_item(int item)
{
    /* a pending return: every item, so no confirm gets through */
    if (s_titleReturn >= 0) {
        return 1;
    }
    /* the title's port rows (the pause menu's entry is the game's row) */
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        if (item >= 0 && (item == s_entryRow[e] || item == s_quitRow[e])) {
            return 1;
        }
    }
    return 0;
}

void ui_settings_title_mask(int masked)
{
    const int pending = s_titleReturn >= 0;
    const int cur = current_layout_id;
    s_titleDecided = !masked;
    if (pending) {
        masked = 1;
    }
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        if (s_entryRow[e] >= 0) {
            lt_mask_property(s_entryRow[e], masked);
        }
        if (s_quitRow[e] >= 0) {
            lt_mask_property(s_quitRow[e], masked);
        }
    }
    if (pending) {
        /* nothing of the title's menu is seen or reachable: every row of
           this layout and its chain */
        lt_item_select_disable = 1;
        if (isTitleLayout(cur)) {
            int n = 0;
            for (int l = cur; l >= 0 && n < 8; n++) {
                const LtProp *lp = lt_ext_layout(l);
                for (int r = lp->first; r < lp->last; r++) {
                    lt_mask_property(r, 1);
                }
                l = lp->link;
            }
        }
    }
}

/* the entry (ENTRY_*) whose game layout is gameLayout, or -1 */
static int entryOf(int gameLayout)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        if (entryGame(e) == gameLayout) {
            return e;
        }
    }
    return -1;
}

int ui_settings_entry_row(int gameLayout)
{
    const int e = entryOf(gameLayout);
    return e >= 0 ? s_entryRow[e] : -1;
}

int ui_settings_quit_row(int gameLayout)
{
    const int e = entryOf(gameLayout);
    return e >= 0 ? s_quitRow[e] : -1;
}

int ui_settings_photo_row(void)
{
    return s_photoRow;
}

int ui_settings_photo_back(void)
{
    if (s_photoRow >= 0) {
        gameCursorOn(LAYOUT_PAUSE, s_photoRow);
    }
    return LAYOUT_PAUSE;
}

int ui_settings_reopen_page(int page)
{
    const int cur = current_layout_id;
    if (!s_built || page < 0 || page >= UI_PAGE_COUNT || s_pages[page].layout < 0 ||
        !isTitleLayout(cur)) {
        return -1;
    }
    /* as entryProc on an entry from this title: onTitle() holds, each page
       opens on its first row ... */
    s_origin = cur;
    s_titleHandoff = s_titleReturn >= 0;
    s_titleReturn = -1;
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        if (s_pages[p].layout >= 0 && !s_pages[p].isList) {
            lt_ext_layout(s_pages[p].layout)->defaultItem = pageFirstNav(&s_pages[p]);
        }
    }
    /* ... but the pages above this one, each on the row that leads down
       (Main on Extras), so Back retraces the path */
    for (int child = page; s_pages[child].parent >= 0; child = s_pages[child].parent) {
        Page *up = &s_pages[s_pages[child].parent];
        for (int i = 0; i < up->count; i++) {
            if (up->rows[i].opt == UI_OPT_LINK && up->rows[i].link == child && up->layout >= 0) {
                lt_ext_layout(up->layout)->defaultItem = up->rows[i].label;
            }
        }
    }
    /* and Extras, back from the viewer's Characters, on its Characters row */
    if (page == UI_PAGE_EXTRAS) {
        const Page *ex = &s_pages[page];
        for (int i = 0; i < ex->count; i++) {
            if (ex->rows[i].opt == UI_OPT_LINK && ex->rows[i].link == UI_PAGE_CHARACTERS) {
                lt_ext_layout(ex->layout)->defaultItem = ex->rows[i].label;
            }
        }
    }
    return s_pages[page].layout;
}

int ui_settings_key_config_back(void)
{
    const int row = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG);
    /* the Options screen opened it when the pause menu was left as the
       game's (tables not the PAL ones: no repoint) */
    if (!s_built || row < 0 || s_pages[UI_PAGE_CONTROLS].layout < 0 ||
        texProperty[ROW_PAUSE_OPTIONS].right != s_pages[UI_PAGE_MAIN].layout) {
        return LAYOUT_GAME_OPTIONS;
    }
    lt_ext_layout(s_pages[UI_PAGE_CONTROLS].layout)->defaultItem = row;
    return s_pages[UI_PAGE_CONTROLS].layout;
}

int ui_settings_entry_layout(int gameLayout)
{
    const int e = entryOf(gameLayout);
    return e >= 0 ? s_entryLayout[e] : -1;
}

int ui_settings_page_layout(UiSettingsPage page)
{
    return s_built && page >= 0 && page < UI_PAGE_COUNT ? s_pages[page].layout : -1;
}

int ui_settings_page_rows(UiSettingsPage page, int *labels, int *opts, int *values, int max)
{
    int n = 0;
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return 0;
    }
    for (int i = 0; i < s_pages[page].count && n < max; i++, n++) {
        const Row *r = &s_pages[page].rows[i];
        if (labels) {
            labels[n] = r->label;
        }
        if (opts) {
            opts[n] = r->opt;
        }
        if (values) {
            values[n] = r->value;
        }
    }
    return n;
}

int ui_settings_note_row_of(UiSettingsPage page, UiSettingsOpt opt)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return -1;
    }
    for (int i = 0; i < s_pages[page].count; i++) {
        if (s_pages[page].rows[i].opt == (int)opt) {
            return s_pages[page].rows[i].note;
        }
    }
    return -1;
}

int ui_settings_row_of(UiSettingsPage page, UiSettingsOpt opt)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return -1;
    }
    for (int i = 0; i < s_pages[page].count; i++) {
        if (s_pages[page].rows[i].opt == (int)opt) {
            return s_pages[page].rows[i].label;
        }
    }
    return -1;
}

/* --------------------------------------------------------------- procs */

static Page *pageOfLayout(int layout, int *id)
{
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        if (s_pages[p].layout == layout && layout >= 0) {
            if (id) {
                *id = p;
            }
            return &s_pages[p];
        }
    }
    return NULL;
}

static int entryProc(int first, int item)
{
    (void)first;
    (void)item;
    int cur = current_layout_id;
    if (cur == LAYOUT_PAUSE || cur == LAYOUT_TITLE_CONTINUE || cur == LAYOUT_TITLE_NEW) {
        s_origin = cur;
        /* the menu opens on its first row each time, as Options did, and so
           does each page under it (a page's default can be left on a row
           this entry hides, as Button configuration's OK leaves Controls') */
        for (int p = 0; p < UI_PAGE_COUNT; p++) {
            if (s_pages[p].layout >= 0 && !s_pages[p].isList) {
                lt_ext_layout(s_pages[p].layout)->defaultItem = pageFirstNav(&s_pages[p]);
            }
        }
    }
    if (cur == LAYOUT_PAUSE) {
        placePause();
        pauseStats(ui_photo_available() && !ico_photo_active());
    }
    if (s_restoreTitle >= 0 && cur == s_restoreTitle) {
        /* the cursor came back to the row that left; the layout's own
           default is the game's again for its next showing */
        texLayout[s_restoreTitle].defaultItem = s_restoreDefault;
        s_restoreTitle = -1;
    }
    return -1;
}

/* The game layout `to` (the pause menu or a title) opens with the cursor on
   row; entryProc gives the layout its own default back on the next frame. */
static void gameCursorOn(int to, int row)
{
    if (s_restoreTitle != to) {
        s_restoreDefault = texLayout[to].defaultItem;
    }
    s_restoreTitle = to;
    texLayout[to].defaultItem = row;
}

/* Leaves page pg for layout `to` (a page's or the game's), the cursor of the
   layout returned to on the row that led here; saves what changed. */
static int leaveTo(int pageId, int to)
{
    NEGATIVE_SE();
    ui_settings_save();
    la_host_leave();
    int toPage;
    if (pageOfLayout(to, &toPage)) {
        for (int i = 0; i < s_pages[toPage].count; i++) {
            if (s_pages[toPage].rows[i].link == pageId) {
                lt_ext_layout(to)->defaultItem = s_pages[toPage].rows[i].label;
            }
        }
    } else if (to == LAYOUT_PAUSE) {
        /* on Options, as the Options screen's Triangle came back */
        gameCursorOn(LAYOUT_PAUSE, ROW_PAUSE_OPTIONS);
    } else if (isTitleLayout(to)) {
        gameCursorOn(to, s_entryRow[to == LAYOUT_TITLE_NEW ? ENTRY_TITLE13 : ENTRY_TITLE12]);
    }
    return to;
}

static int parentLayout(const Page *pg)
{
    return pg->parent >= 0 ? s_pages[pg->parent].layout : s_origin;
}

/* ------------------------------------------------- the list pages (ui_list.h)
 * Achievements and the remap targets: what each page's items are; the
 * window, the scrolling and the rows are ui_list.c's. */

/* the items: the pop-ups switch, the achievements, then Back */
static int achCount(void *user)
{
    (void)user;
    return ico_ach_count() + 2;
}

static void achFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    const int n = ico_ach_count();
    if (d == 0) {
        out->labelStr = UI_STR_OPT_ACH_POPUPS;
        out->colA = onOff(ico_ach_popups_enabled());
        return;
    }
    if (d == n + 1) {
        out->labelStr = UI_STR_BACK;
        return;
    }
    const int a = d - 1;
    IcoAchState st = ico_ach_state(a);
    int hidden = ico_ach_hidden(a) && st == ICO_ACH_LOCKED;
    out->label = hidden ? "???" : ui_str((UiStrId)ico_ach_title_str(a));
    out->colAStr = st == ICO_ACH_UNLOCKED ? UI_STR_ACH_STATE_UNLOCKED : UI_STR_ACH_LOCKED;
}

/* the header carries the count; the status line is the cursor's
   description; the pop-ups switch's value steps on a click, as an Options
   row's value does */
static void achDecorate(void *user, int d)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_ACHIEVEMENTS];
    int n = ico_ach_count(), got = achUnlocked();
    snprintf(s_text, sizeof(s_text), "%s   %d / %d", ui_str(UI_STR_SECTION_ACHIEVEMENTS), got, n);
    lt_ext_set_text(pg->header, s_text);
    char buf[256];
    if (d >= 1 && d <= n) {
        int hidden = ico_ach_hidden(d - 1) && ico_ach_state(d - 1) == ICO_ACH_LOCKED;
        wrapText(hidden ? "???" : ui_str((UiStrId)ico_ach_desc_str(d - 1)), NOTE_SIZE, 580.0f, buf,
                 sizeof(buf));
        lt_ext_set_text(pg->list.status, buf);
    } else {
        lt_ext_set_text(pg->list.status, "");
    }
    const int s = 0 - pg->list.offset;
    if (s >= 0 && s < UI_LIST_SLOTS) {
        lt_ext_set_pointer_role(pg->list.colA[s], LT_POINTER_STEP);
    }
}

/* Left and Right flip the pop-ups switch, as on an Options row */
static int achInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_ACHIEVEMENTS];
    const int n = ico_ach_count();
    if ((flags & PAD_BACK) || ((flags & PAD_OK) && d == n + 1)) {
        return leaveTo(UI_PAGE_ACHIEVEMENTS, parentLayout(pg));
    }
    if (d == 0 && (flags & (LT_PAD_LEFT | LT_PAD_RIGHT))) {
        ui_settings_step(UI_OPT_ACH_POPUPS, (flags & LT_PAD_LEFT) ? -1 : 1);
        CUR_SE();
        return -1;
    }
    return UI_LIST_PASS;
}

static const UiListDef kAchDef = {achCount, achFill, NULL, achInput, achDecorate};

static int remapCount(void *user)
{
    (void)user;
    return REMAP_ITEMS;
}

static void remapFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    static char lab[96], colA[96], colB[96];
    if (d < ICO_T_COUNT) {
        int t = kRemapOrder[d];
        targetName(t, lab, sizeof(lab));
        out->label = lab;
        if (s_capture.active && s_capture.target == t) {
            out->colA = out->colB = "\xE2\x80\xA6"; /* ... */
        } else {
            IcoBindings *b = liveBindings();
            sourcesText(b, t, 0, colA, sizeof(colA));
            sourcesText(b, t, 1, colB, sizeof(colB));
            out->colA = colA;
            out->colB = colB;
        }
    } else {
        out->labelStr = d == ICO_T_COUNT ? UI_STR_REMAP_RESET : UI_STR_BACK;
    }
}

static void remapDecorate(void *user, int d)
{
    (void)user;
    (void)d;
    setNote(s_pages[UI_PAGE_REMAP].list.status, s_capture.active ? UI_STR_REMAP_PRESS
                                                : lt_ext_confirm_glyph() == LT_GLYPH_CIRCLE
                                                    ? UI_STR_REMAP_HINT_CIRCLE
                                                    : UI_STR_REMAP_HINT);
}

static int remapInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_REMAP];
    if (s_capture.active) {
        lt_item_select_disable = 1;
        int r = ui_remap_capture_step(&s_capture, liveBindings());
        if (r == UI_CAPTURE_BOUND) {
            s_dirtyBindings = 1;
            POSITIVE_SE();
        } else if (r == UI_CAPTURE_TIMEOUT) {
            NEGATIVE_SE();
        }
        return -1;
    }
    if (s_capture.cooldown > 0) {
        /* the captured press must not also move or confirm */
        s_capture.cooldown--;
        lt_item_select_disable = 1;
        return -1;
    }
    if (flags & PAD_BACK) {
        return leaveTo(UI_PAGE_REMAP, parentLayout(pg));
    }
    if (d >= 0 && d < ICO_T_COUNT) {
        if (flags & PAD_OK) {
            POSITIVE_SE();
            ui_remap_capture_start(&s_capture, kRemapOrder[d]);
            lt_item_select_disable = 1;
            return -1;
        }
        if (flags & LT_PAD_SQUARE) {
            CUR_SE();
            ico_bindings_clear(liveBindings(), kRemapOrder[d]);
            s_dirtyBindings = 1;
            return -1;
        }
    } else if (d == ICO_T_COUNT && (flags & PAD_OK)) {
        /* the bindings only; sensitivity and the other [input] values stay */
        IcoBindings def, *b = liveBindings();
        ico_bindings_defaults(&def);
        memcpy(b->kb, def.kb, sizeof(b->kb));
        memcpy(b->mouse, def.mouse, sizeof(b->mouse));
        memcpy(b->gp, def.gp, sizeof(b->gp));
        memcpy(b->walk, def.walk, sizeof(b->walk));
        s_dirtyBindings = 1;
        POSITIVE_SE();
        return -1;
    } else if (d == ICO_T_COUNT + 1 && (flags & PAD_OK)) {
        return leaveTo(UI_PAGE_REMAP, parentLayout(pg));
    }
    return UI_LIST_PASS;
}

static const UiListDef kRemapDef = {remapCount, remapFill, NULL, remapInput, remapDecorate};

/* --- the music gallery (gallery.h): its items, the play keys, and the
   engine's tick while the page is up */

static int galCount(void *user)
{
    (void)user;
    return gallery_count();
}

static void galFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    static char lab[UI_LIST_SLOTS][96], col[UI_LIST_SLOTS][96];
    static int k;
    k = (k + 1) % UI_LIST_SLOTS;
    out->label = gallery_label(d, lab[k], sizeof(lab[k]));
    out->colA = gallery_col_a(d, col[k], sizeof(col[k]));
}

static int galHeading(void *user, int d)
{
    (void)user;
    const GalleryItem *it = gallery_item(d);
    return it != NULL && it->kind == GAL_K_HEADING;
}

/* m:ss */
static void galClock(char *buf, size_t n, float s)
{
    int t = s > 0.0f ? (int)s : 0;
    snprintf(buf, n, "%d:%02d", t / 60, t % 60);
}

/* the bar and the times: the item sounding or paused, empty when none */
static void galBar(void)
{
    float el, tot;
    char a[16], b[16];
    if (s_galFill < 0) {
        return;
    }
    if (gallery_position(&el, &tot) == 0) {
        lt_ext_set_fill(s_galFill, tot > 0.0f ? el / tot : 0.0f);
        galClock(a, sizeof(a), el);
        if (tot > 0.0f) {
            galClock(b, sizeof(b), tot);
        } else {
            snprintf(b, sizeof(b), "-:--");
        }
    } else {
        lt_ext_set_fill(s_galFill, 0.0f);
        galClock(a, sizeof(a), 0.0f);
        galClock(b, sizeof(b), 0.0f);
    }
    lt_ext_set_text(s_galTime, a);
    lt_ext_set_text(s_galTotal, b);
}

/* headings stand out to the left; the status line: the group, the file and
   whether the entry under the cursor plays; the bar; Cross's word */
static void galDecorate(void *user, int d)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_MUSIC];
    for (int s = 0; s < UI_LIST_SLOTS; s++) {
        int item = ui_list_item_at(&pg->list, s);
        P(pg->list.label[s])->dispX = galHeading(NULL, item) ? 24 : 40;
    }
    const GalleryItem *it = gallery_item(d);
    char asset[128];
    if (gallery_tables() == NULL) {
        lt_ext_set_text(pg->list.status, ui_str(UI_STR_GAL_EMPTY));
    } else if (it == NULL || it->kind == GAL_K_BACK) {
        lt_ext_set_text(pg->list.status, "");
    } else if (it->kind == GAL_K_HEADING) {
        snprintf(s_text, sizeof(s_text), "%s", gallery_asset(d, asset, sizeof(asset)));
        lt_ext_set_text(pg->list.status, s_text);
    } else {
        int state = gallery_paused() == d    ? UI_STR_GAL_PAUSED
                    : gallery_playing() == d ? UI_STR_GAL_PLAYING
                                             : UI_STR_GAL_STOPPED;
        snprintf(s_text, sizeof(s_text), "%s  \xC2\xB7  %s  \xC2\xB7  %s",
                 ui_str((UiStrId)gallery_group_str(it->group)),
                 gallery_asset(d, asset, sizeof(asset)), ui_str((UiStrId)state));
        lt_ext_set_text(pg->list.status, s_text);
    }
    galBar();
    if (s_galHint.n > 0) {
        /* Cross pauses the entry under the cursor while it sounds */
        int pausing = d >= 0 && gallery_playing() == d && gallery_paused() != d;
        ui_hint_set_str(&s_galHint, UI_HINT_GAL_PLAY,
                        pausing ? UI_STR_HINT_PAUSE : UI_STR_HINT_PLAY);
        ui_hint_layout(&s_galHint);
    }
}

static int galLeave(void)
{
    gallery_leave();
    int to = leaveTo(UI_PAGE_MUSIC, parentLayout(&s_pages[UI_PAGE_MUSIC]));
    /* back on the Music row */
    int row = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
    if (to == s_pages[UI_PAGE_EXTRAS].layout && row >= 0) {
        lt_ext_layout(to)->defaultItem = row;
    }
    return to;
}

static int galInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_MUSIC];
    const GalleryItem *it = gallery_item(d);
    if ((flags & PAD_BACK) || ((flags & PAD_OK) && it != NULL && it->kind == GAL_K_BACK)) {
        return galLeave();
    }
    if ((flags & PAD_OK) && it != NULL && it->kind != GAL_K_HEADING) {
        gallery_toggle(d);
        return -1;
    }
    if (flags & LT_PAD_SQUARE) {
        gallery_stop();
        return -1;
    }
    if ((flags & (LT_PAD_L1 | LT_PAD_R1)) && d >= 0) {
        /* the previous or next entry: the cursor on it, and it plays */
        int k = gallery_step(d, (flags & LT_PAD_L1) ? -1 : 1);
        int n = ui_list_count(&pg->list), shown = ui_list_shown(&pg->list);
        if (k >= 0) {
            if (k < pg->list.offset) {
                pg->list.offset = k > 0 && galHeading(NULL, k - 1) ? k - 1 : k;
            } else if (k >= pg->list.offset + shown) {
                pg->list.offset = k - shown + 1;
            }
            pg->list.offset = pg->list.offset > n - shown ? n - shown : pg->list.offset;
            lt_ext_layout(pg->layout)->curItem = pg->list.label[k - pg->list.offset];
            gallery_play(k);
        }
        return -1;
    }
    if ((flags & (LT_PAD_LEFT | LT_PAD_RIGHT)) && d >= 0) {
        /* the next or previous group, at the top of the window */
        int k = gallery_jump_group(d, (flags & LT_PAD_LEFT) ? -1 : 1);
        int n = ui_list_count(&pg->list), shown = ui_list_shown(&pg->list);
        if (k >= 0) {
            int head = k > 0 && galHeading(NULL, k - 1) ? k - 1 : k;
            pg->list.offset = head > n - shown ? n - shown : head;
            lt_ext_layout(pg->layout)->curItem = pg->list.label[k - pg->list.offset];
            CUR_SE();
        }
        return -1;
    }
    return UI_LIST_PASS;
}

static const UiListDef kGalDef = {galCount, galFill, galHeading, galInput, galDecorate};

/* the locked style (a row greyed, with a note while the cursor is on it) */
static int rowLocked(const Row *r)
{
    if (r->opt == UI_OPT_RESOLUTION) {
        return resolutionLocked(); /* under the CRT filter */
    }
    if (r->opt == UI_OPT_ANTI_ALIAS) {
        return aaLocked(); /* Off under Original */
    }
    if (r->opt == UI_OPT_TEXTURE_PACK) {
        return !texturePackInstalled(); /* "None installed" */
    }
    if (r->opt == UI_OPT_MODEL_PACK) {
        return !modelPackInstalled();
    }
    if (r->opt == UI_OPT_TOUCH_EDIT) {
        return touchEditLocked(); /* Touch controls Off, or no editor */
    }
    return r->opt == UI_OPT_EXTRAS_CREDITS && !creditsUnlocked();
}

/* A page's rows as the entry in force shows them (optShown): Extras'
   Music, Models and Credits only from the title, the game's settings only
   from the pause menu, its Film effect and Players once the game is
   cleared.  The visible rows are spaced evenly (pagePitch), linked in a
   loop that skips the hidden ones, and the hidden ones are masked with
   their values. */
static void layoutPage(Page *pg, int id)
{
    int shown[MAX_ROWS], n = 0;
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        const int show = optShown(r->opt, r->link);
        P(r->label)->defaultMask = !show;
        lt_mask_property(r->label, !show);
        /* a stepped value's row and its two arrows, added after the label */
        const int nv = r->value < 0 ? 0 : steppable(r->opt) ? 3 : 1;
        for (int j = 0; j < nv; j++) {
            P(r->value + j)->defaultMask = !show;
            lt_mask_property(r->value + j, !show);
        }
        if (show) {
            shown[n++] = i;
        }
    }
    int y0;
    const int pitch = pagePitch(id, n, &y0);
    for (int k = 0; k < n; k++) {
        Row *r = &pg->rows[shown[k]];
        const int y = y0 + pitch * k;
        P(r->label)->dispY = y;
        const int nv = r->value < 0 ? 0 : steppable(r->opt) ? 3 : 1;
        for (int j = 0; j < nv; j++) {
            P(r->value + j)->dispY = y;
        }
        P(r->label)->downItem = pg->rows[shown[(k + 1) % n]].label;
        P(r->label)->upItem = pg->rows[shown[(k + n - 1) % n]].label;
    }
    if (n == 0) {
        return;
    }
    /* a hidden row is never the cursor's: its links lead to the shown rows
       around it, and a cursor or default left on one (Button configuration's
       OK from the pause menu, then the title) moves to the first shown row */
    LtProp *lay = pg->layout >= 0 ? lt_ext_layout(pg->layout) : NULL;
    for (int i = 0, k = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        if (k < n && shown[k] == i) {
            k++;
            continue;
        }
        P(r->label)->downItem = pg->rows[shown[k % n]].label;
        P(r->label)->upItem = pg->rows[shown[(k + n - 1) % n]].label;
        if (lay && lay->curItem == r->label) {
            lay->curItem = pg->rows[shown[0]].label;
        }
        if (lay && lay->defaultItem == r->label) {
            lay->defaultItem = pg->rows[shown[0]].label;
        }
    }
}

/* the notes shown only while their row is locked (Anti-aliasing's says
   what the row does: on the cursor, locked or not) */
static int lockedNote(const Row *r)
{
    return isExtrasOpt(r->opt) || r->opt == UI_OPT_RESOLUTION;
}

/* a row's x, width and text size (0: the page's) */
static void placeRow(int row, int x, int w, float size)
{
    if (row >= 0) {
        P(row)->dispX = x;
        P(row)->dispW = w;
        lt_ext_set_size(row, size);
    }
}

/* the page's places: the Options page's, or the viewer's panel (CV_*);
   set at each refresh, so the same rows serve the pause menu after the
   title's viewer and back */
static void charactersPlace(Page *pg, int viewer)
{
    P(pg->header)->dispX = viewer ? CV_HEADER_X : 20;
    P(pg->header)->dispY = viewer ? CV_HEADER_Y : HEADER_Y;
    P(pg->header)->dispW = viewer ? CV_HEADER_W : 600;
    P(pg->header)->centerX = viewer ? 0 : 1;
    lt_ext_set_size(pg->header, viewer ? CV_HEADER_SIZE : HEADER_SIZE);
    for (int i = 0; i < pg->count; i++) {
        const Row *r = &pg->rows[i];
        placeRow(r->label, viewer ? CV_LABEL_X : LABEL_X, viewer ? CV_LABEL_W : LABEL_W,
                 viewer ? CV_SIZE : 0.0f);
        if (r->value >= 0 && steppable(r->opt)) {
            placeRow(r->value, viewer ? CV_STEP_X : STEP_X, viewer ? CV_STEP_W : STEP_W,
                     viewer ? CV_SIZE : 0.0f);
            placeRow(r->value + 1, viewer ? CV_ARROW_L_X : ARROW_L_X, viewer ? CV_ARROW_W : ARROW_W,
                     viewer ? CV_SIZE : 0.0f);
            placeRow(r->value + 2, viewer ? CV_ARROW_R_X : ARROW_R_X, viewer ? CV_ARROW_W : ARROW_W,
                     viewer ? CV_SIZE : 0.0f);
        }
        if (isCharOpt(r->opt) && s_charSwatch[charPart(r->opt)] >= 0) {
            LtProperty *sw = P(s_charSwatch[charPart(r->opt)]);
            sw->dispX = viewer ? CV_SWATCH_X : SWATCH_X;
            sw->dispW = viewer ? CV_SWATCH_W : SWATCH_W;
            sw->dispH = viewer ? CV_SWATCH_H : SWATCH_H;
        }
        if (r->note >= 0) {
            P(r->note)->dispY = viewer ? CV_NOTE_Y : NOTE_Y;
        }
    }
    /* the model shows through: no shade over the picture */
    lt_ext_layout(pg->layout)->colA = viewer ? 0.0f : 0.6f;
    /* the prompts only there */
    for (int i = 0; i < s_charHint.n; i++) {
        ui_hint_show(&s_charHint, i, viewer);
    }
    if (s_charHint.n > 0) {
        ui_hint_layout(&s_charHint);
    }
}

/* the note while the viewer shows the page: only the texture pack's */
static int charsNoteHidden(int id, const Row *r)
{
    return id == UI_PAGE_CHARACTERS && ui_settings_characters_in_viewer() &&
           r->noteStr != UI_STR_CHAR_NOTE_PACK;
}

/* Characters' note and swatches, on their rows.  With a texture pack on,
   the note says its pictures of Ico and Yorda, if it has any, replace the
   colours: a pack names its files by the texture's hash (texpack_name.h),
   so which textures it covers is known only once each is loaded, not from
   this page */
static void charactersRefresh(Page *pg)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    const int shown = charsShown();
    const int viewer = shown != -2;
    const int note = o.texturePack && texturePackInstalled() ? UI_STR_CHAR_NOTE_PACK
                     : onTitle()                             ? UI_STR_CHAR_NOTE_TITLE
                                                             : UI_STR_CHAR_NOTE_PAUSE;
    charactersPlace(pg, viewer);
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        if (r->note >= 0) {
            r->noteStr = note;
        }
        if (r->opt == UI_OPT_CHAR_SWITCH && shown >= 0) {
            /* the character not on screen */
            lt_ext_set_str(r->label,
                           shown == 1 ? UI_STR_CHAR_SWITCH_ICO : UI_STR_CHAR_SWITCH_YORDA);
        }
        if (isCharOpt(r->opt) && s_charSwatch[charPart(r->opt)] >= 0) {
            /* the swatch shows with its row (layoutPage masked the other
               character's rows in the viewer) */
            const int sw = s_charSwatch[charPart(r->opt)];
            const int hidden = P(r->label)->defaultMask;
            unsigned char rgba[4];
            swatchColour(charPart(r->opt), rgba);
            lt_ext_set_rect_color(sw, rgba);
            P(sw)->dispY = P(r->label)->dispY + (viewer ? CV_SWATCH_DY : SWATCH_DY);
            P(sw)->defaultMask = hidden;
            lt_mask_property(sw, hidden);
        }
    }
}

static void refreshPage(Page *pg, int id, int cur)
{
    if (pg->isList) {
        ui_list_refresh(&pg->list, cur);
        return;
    }
    layoutPage(pg, id);
    if (id == UI_PAGE_CHARACTERS) {
        charactersRefresh(pg);
    }
    if (id == UI_PAGE_GPU_DRIVER && s_gpuHostSet) {
        gpuRefresh(pg);
    }
    if (id == UI_PAGE_EXTRAS) {
        /* from the title with the viewer there, Characters opens in it
           (settingsProc), not as the page the link would switch to */
        for (int i = 0; i < pg->count; i++) {
            Row *r = &pg->rows[i];
            if (r->opt == UI_OPT_LINK && r->link == UI_PAGE_CHARACTERS) {
                P(r->label)->right =
                    onTitle() && charsHosted() ? -1 : s_pages[UI_PAGE_CHARACTERS].layout;
            }
        }
    }
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        int locked = rowLocked(r);
        if (r->value >= 0) {
            lt_ext_set_text(r->value, ui_settings_value_text((UiSettingsOpt)r->opt));
        }
        if (r->opt == UI_OPT_TOUCH_EDIT) {
            /* locked: Cross does nothing, the note says why */
            P(r->label)->right = locked ? -1 : s_pages[UI_PAGE_TOUCH_LAYOUT].layout;
            r->noteStr = locked ? UI_STR_TOUCH_EDIT_LOCKED : UI_STR_TOUCH_EDIT_NOTE;
        }
        if (isExtrasOpt(r->opt) || r->opt == UI_OPT_RESOLUTION || r->opt == UI_OPT_ANTI_ALIAS ||
            r->opt == UI_OPT_TEXTURE_PACK || r->opt == UI_OPT_MODEL_PACK ||
            r->opt == UI_OPT_TOUCH_EDIT) {
            /* the locked style: the label and its value greyed */
            lt_ext_set_dim(r->label, locked);
            if (r->value >= 0) {
                lt_ext_set_dim(r->value, locked);
            }
        }
        if (r->note >= 0) {
            setNote(r->note, r->noteStr);
            if (cur == r->label && (!lockedNote(r) || locked) && !charsNoteHidden(id, r)) {
                lt_mask_property(r->note, 0);
            }
        }
    }
}

static int settingsProc(int first, int item)
{
    int id;
    Page *pg = pageOfLayout(current_layout_id, &id);
    LtProp *lay = lt_ext_layout(current_layout_id);
    (void)item;
    if (pg == NULL) {
        return -1;
    }
    /* the port's strings follow the game's language (ui_host.c does too) */
    ui_settings_apply_language();
    gpuPoll(); /* an install the page started, on any page */
    if (id == UI_PAGE_TOUCH_LAYOUT) {
        /* the touch layout editor has the page: begun on the first call,
           stepped every call (the pad word once the page has faded in), and
           left with the cursor back on Edit layout, what changed saved */
        const int ready = lt_fade_status() == 2;
        unsigned int fl = 0, held = 0;
        if (ready) {
            lt_analog2Pad();
            fl = (unsigned int)pad[0].flags;
            held = (unsigned int)pad[0].now;
        }
        if (ui_touch_edit_proc(first, fl, held) && ready) {
            return leaveTo(id, parentLayout(pg));
        }
        return -1;
    }
    if (id == UI_PAGE_MUSIC) {
        gallery_tick(); /* the engine, once a Main tick while the page is up */
    }
    if (first) {
        if (pg->isList) {
            ui_list_reset(&pg->list);
        }
        memset(&s_capture, 0, sizeof(s_capture));
    }
    refreshPage(pg, id, lay->curItem);
    if (lt_fade_status() != 2) {
        return -1;
    }
    lt_analog2Pad();
    int flags = pad[0].flags;
    if (id == UI_PAGE_MUSIC && gallery_script_leave()) {
        return galLeave(); /* the ICO_GALLERY_PLAY script's leave entry */
    }
    if (pg->isList) {
        int r = ui_list_proc(&pg->list, lay, flags);
        refreshPage(pg, id, lay->curItem);
        return r;
    }
    if (id == UI_PAGE_CHARACTERS && ui_settings_characters_in_viewer()) {
        if (charsShown() < 0) {
            return -1; /* a model loading, or the viewer leaving */
        }
        const int cross = flags & PAD_OK;
        if ((flags & PAD_BACK) ||
            (cross && lay->curItem == ui_settings_row_of(UI_PAGE_CHARACTERS, UI_OPT_BACK))) {
            /* Triangle, or Back: to the title, what changed written */
            NEGATIVE_SE();
            ui_settings_save();
            la_host_leave();
            s_charsHost.leave();
            return -1;
        }
        const int onSwitch =
            cross && lay->curItem == ui_settings_row_of(UI_PAGE_CHARACTERS, UI_OPT_CHAR_SWITCH);
        if ((flags & (LT_PAD_L1 | LT_PAD_R1)) || onSwitch) {
            POSITIVE_SE();
            s_charsRows = 1 - charsShown();
            s_charsHost.switchTo(s_charsRows);
            refreshPage(pg, id, lay->curItem);
            return -1;
        }
    }
    if (flags & PAD_BACK) {
        return leaveTo(id, parentLayout(pg));
    }
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        if (r->label != lay->curItem) {
            continue;
        }
        if ((flags & (LT_PAD_LEFT | LT_PAD_RIGHT)) && canStep(r->opt)) {
            ui_settings_step((UiSettingsOpt)r->opt, (flags & LT_PAD_LEFT) ? -1 : 1);
            CUR_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & LT_PAD_SQUARE) && r->opt == UI_OPT_BRIGHTNESS && canStep(r->opt)) {
            /* the adjust screen's Default */
            systemStatus[11] = BRIGHTNESS_DEFAULT;
            CUR_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & LT_PAD_SQUARE) && isCharOpt(r->opt)) {
            /* the part's Original, as Brightness's Default */
            ico_appearance_set(charPart(r->opt), 0);
            s_dirtyConfig = 1;
            CUR_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & PAD_OK) && (r->opt == UI_OPT_CHAR_RANDOMIZE || r->opt == UI_OPT_CHAR_RESET)) {
            /* in the viewer only the character shown, from the
               pause menu both */
            const int c = charsRowsFor();
            if (r->opt == UI_OPT_CHAR_RANDOMIZE) {
                const unsigned seed = (unsigned)time(NULL) ^ ico_host_main_ticks();
                if (c >= 0) {
                    ico_appearance_randomize_character(c, seed);
                } else {
                    ico_appearance_randomize(seed);
                }
            } else if (c >= 0) {
                ico_appearance_reset_character(c);
            } else {
                ico_appearance_reset();
            }
            s_dirtyConfig = 1;
            POSITIVE_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_LINK && r->link == UI_PAGE_CHARACTERS &&
            onTitle() && charsHosted()) {
            /* Characters from the title, inside the model viewer */
            int to = charsEnter();
            if (to >= 0) {
                POSITIVE_SE();
                return to;
            }
        }
        if ((flags & PAD_OK) &&
            (r->opt == UI_OPT_TOUCH_RESET || r->opt == UI_OPT_TOUCH_RESET_ALL)) {
            /* the posture the phone is in now, or every posture */
            const int rule = r->opt == UI_OPT_TOUCH_RESET ? touchRule() : -1;
            IcoTouchEdits *e = ico_input_live_touch_edits();
            fprintf(stderr, "touch: reset layout (%s, %s)\n",
                    rule < 0 ? "all postures" : ico_touch_posture_name(rule),
                    ico_touch_edits_any(e, rule) ? "edited before" : "nothing edited");
            ico_touch_edits_reset(e, rule);
            s_dirtyTouch = 1;
            POSITIVE_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_EXTRAS_STEAM) {
            POSITIVE_SE();
            steamAdd();
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_REPORT_PICTURE) {
            POSITIVE_SE();
            reportPicture();
        }
#if ICO_RC_TOOLS
        if ((flags & PAD_OK) && r->opt == UI_OPT_SHARE_REPORT) {
            POSITIVE_SE();
            shareReport();
        }
#endif
        if ((flags & PAD_OK) && r->opt == UI_OPT_GPU_ADD) {
            POSITIVE_SE();
            gpuAdd();
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_GPU_ADD_FILE) {
            POSITIVE_SE();
            gpuAddFile();
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_GPU_REMOVE) {
            POSITIVE_SE();
            gpuRemove();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & PAD_OK) && r->opt == UI_OPT_BACK) {
            return leaveTo(id, parentLayout(pg));
        }
        if ((flags & PAD_OK) && isExtrasOpt(r->opt)) {
            int to = extrasOpen(r->opt);
            if (to >= 0) {
                POSITIVE_SE();
                return to;
            }
        }
        break;
    }
    return -1;
}

int ui_settings_covers_title(void)
{
    if (s_titleHandoff && !isTitleLayout(current_layout_id)) {
        s_titleHandoff = 0;
    }
    /* from the first frame of a pending return to the switch
       to its page, the logo is down too */
    if (s_built && (s_titleReturn >= 0 || s_titleHandoff)) {
        return 1;
    }
    return s_built && onTitle() && pageOfLayout(current_layout_id, NULL) != NULL;
}
