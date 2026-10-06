/*
 * port/ui/settings.c
 *
 * The Settings menu (settings.h; docs/port/SETTINGS.md, docs/port/UI.md).
 * Port layouts in the layout extension, run by the game's layout code; this
 * file builds them, repoints the game's rows at the entry rows, and holds
 * the screens' procs: the value texts, left/right on a value, Cross on an
 * action, Triangle or Circle back, the scrolling lists, the remap capture,
 * the saves when a screen is left, and the title's "Quit to desktop" row
 * with its confirmation (Q2).
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "audio_host.h"
#include "config.h"
#include "font.h"
#include "gallery.h"
#include "ico_credits.h"
#include "input.h"
#include "layout_ext.h"
#include "menu_text.h"
#include "mix_gain.h"
#include "options.h"
#include "photo_ui.h"
#include "strings.h"
#include "sysconf.h"
#include "ui_hint.h"
#include "ui_list.h"
#include "video_options.h"

#ifdef ICO_RD
#include "rd.h" /* rd_SetMirror (R7c) */
#endif

/* --- the game's side (common/; layout_texture.h declares the lt_* calls) -- */

extern PadState pad[16];
extern int NonLinearCameraMove; /* the language, 2..6 */
extern int systemStatus[12];    /* [0]: 1 PAL 50 Hz, 0 60 Hz */
extern int gFlagGameClear;
extern int gsResetFunc(int val); /* debug.c: gsb_Init, the boot screen's reset */
extern void CUR_SE(void);        /* layout_action.c: the menus' sounds */
extern void POSITIVE_SE(void);
extern void NEGATIVE_SE(void);
extern void la_host_leave(void); /* layout_action.c (ICO_HOST) */
/* fumi/sound/s_init.c: the PS2's stereo (0) or mono (1) output */
extern int soundOutputModeGet(void);
extern void soundOutputModeSet(int mode);
/* layout_action.c (ICO_HOST, R7c): what la_vibe_select's confirm did after
   the vibration choice, gflagOn(382): the new game starts */
extern void la_host_new_game_go(void);

/* the pad's trigger bits (keyInput.c's logical word) */
#define PAD_L1 0x0004
#define PAD_R1 0x0008
#define PAD_TRIANGLE 0x0010
#define PAD_CIRCLE 0x0020
#define PAD_CROSS 0x0040
#define PAD_START 0x0800
#define PAD_SQUARE 0x0080
#define PAD_UP 0x1000
#define PAD_RIGHT 0x2000
#define PAD_DOWN 0x4000
#define PAD_LEFT 0x8000
/* Q2: on the port's screens Circle goes back as Triangle does, whatever
   [game] circle_back says (that switch is for the game's own menus,
   layout_ext.h lt_ext_BackButtons): no PS2 behaviour to keep here, and
   Circle has no other use on them (a remap capture takes it before this
   check) */
#define PAD_BACK (PAD_TRIANGLE | PAD_CIRCLE)

/* the game layouts the menu is entered from */
#define LAYOUT_PAUSE_OPTIONS 58
#define LAYOUT_TITLE_CONTINUE 12
#define LAYOUT_TITLE_NEW 13

enum { ENTRY_OPTIONS, ENTRY_TITLE12, ENTRY_TITLE13, ENTRY_COUNT };

static const int kEntryGame[ENTRY_COUNT] = {LAYOUT_PAUSE_OPTIONS, LAYOUT_TITLE_CONTINUE,
                                            LAYOUT_TITLE_NEW};

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

/* where the Options screen's Settings row goes: the girl-control row (325)
   is hidden until the game is cleared, so the row takes its place, and moves
   one Options pitch (325 less 324: 20 field lines) below it once 325 shows
   (entryProc, from the loaded rows).  Right-aligned where the Options
   labels' letters end, x 357 (menu_text.c's right anchors: 236 + 121 for
   Vibration, Brightness and Players, 172 + 185 Film Effect, 108 + 248
   Button Configuration), not at their rectangles' end (364) */
#define OPTIONS_LABELS_END 357
/* The title is laid out by the port (placeTitle): its four rows, Continue
   (49), New Game (50, and 51 on the New-Game-only layout), Settings and
   "Quit to desktop", one game pitch apart (20 field lines, the Options
   screen's), and the copyright line (48) below Quit with the same space
   between its capitals and Quit's as between the rows' (19 field lines:
   its capitals are 24 output pixels at 960 x 720, the rows' 31).  The PAL
   data has Continue at 135, New Game at 165 and the copyright line at 195,
   the next step below New Game, which left no room for the port rows.
   Where the block can go was measured on window runs' titles (960 x 720
   Original, 1920 x 1440 Enhanced at full height; docs/port/UI.md, "Title
   rows (V2)"): the copyright line's sprite, its rim and descenders
   included, ends 17 pixels above the picture's end at 195 (5 field
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

#define MAX_ROWS 16

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
} Page;

static Page s_pages[UI_PAGE_COUNT];
static int s_built;
static int s_warned;
static int s_entryRow[ENTRY_COUNT] = {-1, -1, -1};
static int s_quitRow[ENTRY_COUNT] = {-1, -1, -1}; /* Q2: the title's "Quit to desktop" */
static int s_entryLayout[ENTRY_COUNT] = {-1, -1, -1};
/* package PHOTO: "Photo mode" under the Options screen's Settings row, in
   the same port layout; opens photo_ui.c's layout (only while a stage runs:
   entryProc) */
static int s_photoRow = -1;
static int s_origin = LAYOUT_PAUSE_OPTIONS; /* the game layout the menu returns to */

/* The video mode arms the tick rate (the game's timers are armed at the
   rate in force), so it changes only when Settings was opened from the
   title; from the pause menu its value shows "(title only)". */
static int onTitle(void)
{
    return s_origin == LAYOUT_TITLE_CONTINUE || s_origin == LAYOUT_TITLE_NEW;
}

static int s_restoreTitle = -1; /* a title layout whose defaultItem to restore */
static int s_restoreDefault;
static int s_dirtyVideo, s_dirtyConfig, s_dirtyBindings;
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

int ui_SettingsAddRow(int x, int y, int w, int h, int selectable, int owner, int strId,
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
    return lt_ext_AddProperty(&r, &t);
}

static LtProperty *P(int index)
{
    return lt_ext_Prop(index);
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
    return lt_ext_AddLayout(&l);
}

/* ------------------------------------------------------------- values */

static int stepIndex(int i, int n, int dir)
{
    return ((i + dir) % n + n) % n;
}

static const float kMouseSens[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};
#define MOUSE_SENS_N ((int)(sizeof(kMouseSens) / sizeof(kMouseSens[0])))

/* R7d: the Frame rate row's values, in the order Right steps them
   (ico_video_parse_framerate / ico_video_framerate_name: "original",
   "uncapped" or N presents a second) */
static const int kFramerates[] = {
    ICO_FRAMERATE_ORIGINAL, ICO_FRAMERATE_UNCAPPED, 60, 120, 144, 240};
#define FRAMERATE_N ((int)(sizeof(kFramerates) / sizeof(kFramerates[0])))

/* The next value from fr: a listed value steps (and wraps) in the list; a
   cap the list does not hold (framerate = 100 in the file) steps to the
   listed cap above it (Right) or below it (Left), past the ends to
   "original" (Right) or "uncapped" (Left) */
static int stepFramerate(int fr, int dir)
{
    for (int i = 0; i < FRAMERATE_N; i++) {
        if (kFramerates[i] == fr) {
            return kFramerates[stepIndex(i, FRAMERATE_N, dir)];
        }
    }
    if (dir > 0) {
        for (int i = 2; i < FRAMERATE_N; i++) {
            if (kFramerates[i] > fr) {
                return kFramerates[i];
            }
        }
        return ICO_FRAMERATE_ORIGINAL;
    }
    for (int i = FRAMERATE_N - 1; i >= 2; i--) {
        if (kFramerates[i] < fr) {
            return kFramerates[i];
        }
    }
    return ICO_FRAMERATE_UNCAPPED;
}

/* package CRT: the CRT filter row's names of the modes (ICO_CRT_* order) */
static const int kCrtStr[ICO_CRT_MODES] = {UI_STR_VAL_CRT_SCANLINES, UI_STR_VAL_CRT_CONSUMER,
                                           UI_STR_VAL_CRT_TRINITRON, UI_STR_VAL_CRT_PVM,
                                           UI_STR_VAL_CRT_SHADOW};

/* package CRT2: the CRT filter in force (a mode at a strength above 0, as
   rd__CrtOn sees it): the scene renders at 1x, and the Resolution row reads
   "1x (CRT)" and does not step; the file's resolution is kept and is in
   force again with the filter off (docs/port/DISPLAY.md "CRT filter") */
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

/* the value box's text for [audio] device: Default, or the name, cut with
   an ellipsis where even the shrink to fit (down to 60 %, layout_ext.c)
   would leave it wider than the box (less a margin before the arrow) */
static const char *deviceText(char *buf, unsigned size)
{
    const char *name = ico_config_get_string("audio.device", "");
    if (name[0] == '\0') {
        return ui_Str(UI_STR_VAL_DEFAULT_DEVICE);
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
        if (n == 0 || ui_MeasureText(UI_MENU_TEXT_SIZE * 0.6f, buf) <= (float)(STEP_W - 8)) {
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

static const char *onOff(int v)
{
    return ui_Str(v ? UI_STR_ON : UI_STR_OFF);
}

static const char *languageName(int game)
{
    static const int ids[5] = {UI_STR_LANG_EN, UI_STR_LANG_FR, UI_STR_LANG_DE, UI_STR_LANG_IT,
                               UI_STR_LANG_ES};
    int i = game - ICO_GAME_LANGUAGE_ENGLISH;
    return ui_Str((UiStrId)ids[i >= 0 && i < 5 ? i : 0]);
}

static int resolutionIndex(const IcoVideoOptions *o)
{
    if (o->resScale >= 1 && o->resScale <= 4) {
        return o->resScale;
    }
    if (o->resW == 0 && o->resScale == 0) {
        return 0;
    }
    return -1; /* a WxH or a larger N from the file */
}

/* ------------------------------------------------------------- Extras
 * Settings > Extras (docs/port/EXTRAS.md), shown from the title only: the
 * galleries leave the stage, and the pause menu has no Extras row (it is
 * masked and skipped by the cursor).  Music, Models and Credits are
 * placeholders until their packages land: each row's hook returns the layout
 * to open, or -1 when the entry is not there yet (a log line, nothing else). */

static int isExtrasOpt(int opt)
{
    return opt == UI_OPT_EXTRAS_MUSIC || opt == UI_OPT_EXTRAS_MODELS ||
           opt == UI_OPT_EXTRAS_CREDITS;
}

/* package CRED: true once the ending has been reached: the port's ending
   achievement or its clear count, or [dev] unlock_credits (credits.c,
   ico_credits_unlocked; docs/port/EXTRAS.md, "Credits") */
static int creditsUnlocked(void)
{
    return ico_credits_unlocked();
}

/* the music gallery's page (gallery.h, docs/port/MUSIC.md) */
static int extrasMusic(void)
{
    if (!s_built || s_pages[UI_PAGE_MUSIC].layout < 0) {
        return -1;
    }
    gallery_Enter();
    return s_pages[UI_PAGE_MUSIC].layout;
}

/* package MV: the model viewer's list (port/game/model_viewer.c registers
   it; docs/port/EXTRAS.md, "Models"), -1 without one */
static int (*s_modelsEnter)(void);

void ui_SettingsSetModelsHandler(int (*fn)(void))
{
    s_modelsEnter = fn;
}

static int extrasModels(void)
{
    return s_modelsEnter != NULL ? s_modelsEnter() : -1;
}

/* package CRED: the credits: the ending from the staff roll's first scene
   (ico_credits.h; docs/port/EXTRAS.md, "Credits").  The menu closes on the
   game's empty layout while the stage changes, as leaving it saves first;
   the title comes back with the cursor on Settings.  Locked: nothing. */
static void titleCursorOn(int to, int row);

static int extrasCredits(void)
{
    if (!creditsUnlocked()) {
        fprintf(stderr, "credits: locked (the ending has not been reached)\n");
        return -1;
    }
    if (!onTitle()) {
        return -1;
    }
    ui_SettingsSave();
    int to = ico_credits_start();
    if (to < 0) {
        return -1;
    }
    la_host_leave();
    titleCursorOn(s_origin,
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

    for (unsigned i = 0; i < sizeof(kExtras) / sizeof(kExtras[0]); i++) {
        if (kExtras[i].opt == opt) {
            int to = kExtras[i].open();
            if (to < 0) {
                fprintf(stderr, "extras: %s not available yet\n", kExtras[i].name);
            }
            return to;
        }
    }
    return -1;
}

static const char *rawValue(int opt, char *buf, unsigned size)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    switch (opt) {
    case UI_OPT_PRESET:
        return ui_Str(o.preset == ICO_VIDEO_ENHANCED ? UI_STR_VAL_ENHANCED : UI_STR_VAL_ORIGINAL);
    case UI_OPT_RESOLUTION:
        if (crtForcesNative(&o)) {
            return "1x (CRT)";
        }
        if (resolutionIndex(&o) == 0) {
            return ui_Str(UI_STR_VAL_WINDOW);
        }
        return ico_video_resolution_name(&o, buf, size);
    case UI_OPT_ASPECT:
        return o.aspect == ICO_ASPECT_AUTO ? ui_Str(UI_STR_VAL_AUTO)
                                           : ico_video_aspect_name(o.aspect);
    case UI_OPT_FULLSCREEN:
        return onOff(o.fullscreen);
    case UI_OPT_VSYNC:
        return onOff(o.vsync);
    case UI_OPT_FILTER:
        return ui_Str(o.filter == ICO_FILTER_TRILINEAR     ? UI_STR_VAL_TRILINEAR
                      : o.filter == ICO_FILTER_ANISOTROPIC ? UI_STR_VAL_ANISOTROPIC
                                                           : UI_STR_VAL_ORIGINAL);
    case UI_OPT_FULL_HEIGHT:
        return onOff(o.fullHeight);
    case UI_OPT_FRAMERATE:
        /* the option as set, in force in both presets (F2) */
        if (o.framerate == ICO_FRAMERATE_ORIGINAL) {
            return ui_Str(UI_STR_VAL_ORIGINAL);
        }
        if (o.framerate == ICO_FRAMERATE_UNCAPPED) {
            return ui_Str(UI_STR_VAL_UNCAPPED);
        }
        snprintf(buf, size, "%d %s", o.framerate, ui_Str(UI_STR_FPS_UNIT));
        return buf;
    case UI_OPT_CRT:
        /* package CRT: [video] crt and crt_mode as one value */
        return ui_Str(
            !o.crt ? UI_STR_OFF
                   : kCrtStr[o.crtMode >= 0 && o.crtMode < ICO_CRT_MODES ? o.crtMode
                                                                         : ICO_CRT_CONSUMER]);
    case UI_OPT_CRT_STRENGTH:
        snprintf(buf, size, "%d %%", (int)(o.crtStrength * 100.0f + 0.5f));
        return buf;
    case UI_OPT_VIDEO_MODE:
        if (!onTitle()) {
            snprintf(buf, size, "%s (%s)",
                     ui_Str(systemStatus[0] != 0 ? UI_STR_VAL_PAL50 : UI_STR_VAL_60HZ),
                     ui_Str(UI_STR_VIDEO_MODE_TITLE_ONLY));
            return buf;
        }
        return ui_Str(systemStatus[0] != 0 ? UI_STR_VAL_PAL50 : UI_STR_VAL_60HZ);
    case UI_OPT_MENU_TEXT:
        return ui_Str(ico_opt_classic_menu_text() ? UI_STR_VAL_CLASSIC : UI_STR_VAL_PORT_FONT);
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
            snprintf(buf, size, "%s (%s)", ui_Str(UI_STR_VAL_AUTO), ui_Str((UiStrId)live));
            return buf;
        }
        return ui_Str(ico_opt_output_mode() == ICO_OUTPUT_MONO ? UI_STR_VAL_MONO
                                                               : UI_STR_VAL_STEREO);
    }
    case UI_OPT_DEVICE:
        return deviceText(buf, size);
    case UI_OPT_STICK_FIX:
        return onOff(ico_opt_stick_fix());
    case UI_OPT_MOUSE_SENS:
        snprintf(buf, size, "%.2f", (double)liveBindings()->mouse_sens);
        return buf;
    case UI_OPT_CIRCLE_BACK:
        return onOff(ico_opt_circle_back());
    case UI_OPT_YORDA:
        return onOff(ico_opt_yorda_safe());
    case UI_OPT_LANGUAGE:
        return languageName(NonLinearCameraMove);
    case UI_OPT_DEVELOPER:
        return onOff(ico_opt_developer_mode());
    case UI_OPT_EXTRAS_CREDITS:
        return creditsUnlocked() ? "" : ui_Str(UI_STR_ACH_LOCKED);
    default:
        return "";
    }
}

static int steppable(int opt)
{
    return opt >= UI_OPT_PRESET && opt <= UI_OPT_DEVELOPER;
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
    return steppable(opt) && (opt != UI_OPT_VIDEO_MODE || onTitle()) &&
           (opt != UI_OPT_RESOLUTION || !resolutionLocked());
}

const char *ui_SettingsValueText(UiSettingsOpt opt)
{
    char raw[sizeof(s_text)];
    snprintf(s_text, sizeof(s_text), "%s", rawValue(opt, raw, sizeof(raw)));
    return s_text;
}

void ui_SettingsStep(UiSettingsOpt opt, int dir)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    int video = 0;
    switch (opt) {
    case UI_OPT_PRESET:
        o.preset = o.preset == ICO_VIDEO_ENHANCED ? ICO_VIDEO_ORIGINAL : ICO_VIDEO_ENHANCED;
        video = 1;
        break;
    case UI_OPT_RESOLUTION: {
        if (crtForcesNative(&o)) {
            return; /* package CRT2: 1x while the CRT filter is on */
        }
        int i = resolutionIndex(&o);
        i = i < 0 ? (dir > 0 ? 0 : 4) : stepIndex(i, 5, dir);
        o.resW = o.resH = 0;
        o.resScale = i;
        video = 1;
        break;
    }
    case UI_OPT_ASPECT:
        o.aspect = stepIndex(o.aspect, 4, dir);
        video = 1;
        break;
    case UI_OPT_FULLSCREEN:
        o.fullscreen = !o.fullscreen;
        video = 1;
        break;
    case UI_OPT_VSYNC:
        o.vsync = !o.vsync;
        video = 1;
        break;
    case UI_OPT_FILTER:
        o.filter = stepIndex(o.filter, 3, dir);
        video = 1;
        break;
    case UI_OPT_FULL_HEIGHT:
        o.fullHeight = !o.fullHeight;
        video = 1;
        break;
    case UI_OPT_FRAMERATE:
        o.framerate = stepFramerate(o.framerate, dir);
        video = 1;
        break;
    case UI_OPT_CRT: {
        /* package CRT: Off, Scanlines, Consumer TV, Trinitron, PVM, (CRT2)
           Shadow mask, around */
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
    case UI_OPT_MENU_TEXT:
        /* P3: the game's menu text from its textures or the port font; the
           next frame draws it */
        ico_opt_set_classic_menu_text(!ico_opt_classic_menu_text());
        ico_config_set_bool("game.classic_menu_text", ico_opt_classic_menu_text());
        ui_MenuTextSetClassic(ico_opt_classic_menu_text());
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
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_DEVICE:
        stepDevice(dir);
        break;
    case UI_OPT_STICK_FIX:
        ico_opt_set_stick_fix(!ico_opt_stick_fix());
        ico_config_set_bool("gameplay.stick_fix", ico_opt_stick_fix());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_MOUSE_SENS: {
        IcoBindings *b = liveBindings();
        int i, best = 3;
        for (i = 0; i < MOUSE_SENS_N; i++) {
            if (kMouseSens[i] <= b->mouse_sens + 0.001f) {
                best = i;
            }
        }
        best += dir;
        best = best < 0 ? 0 : best >= MOUSE_SENS_N ? MOUSE_SENS_N - 1 : best;
        b->mouse_sens = kMouseSens[best];
        s_dirtyBindings = 1;
        break;
    }
    case UI_OPT_CIRCLE_BACK:
        /* Q2: the game menus' alias, live from the next press */
        ico_opt_set_circle_back(!ico_opt_circle_back());
        ico_config_set_bool("game.circle_back", ico_opt_circle_back());
        lt_ext_SetCircleBack(ico_opt_circle_back());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_YORDA:
        ico_opt_set_yorda_safe(!ico_opt_yorda_safe());
        ico_config_set_bool("gameplay.yorda_safe", ico_opt_yorda_safe());
        s_dirtyConfig = 1;
        break;
    case UI_OPT_LANGUAGE: {
        /* as kanbanBoot.c step 102 stores it; the game's text and subtitles
           follow at their next load, the port's strings at once */
        int i = NonLinearCameraMove - ICO_GAME_LANGUAGE_ENGLISH;
        i = i < 0 || i > 4 ? 0 : stepIndex(i, 5, dir);
        NonLinearCameraMove = ICO_GAME_LANGUAGE_ENGLISH + i;
        ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
        ico_sysconf_set_language(ico_game_to_scf_language(NonLinearCameraMove));
        s_dirtyConfig = 1;
        break;
    }
    case UI_OPT_DEVELOPER:
        ico_opt_set_developer_mode(!ico_opt_developer_mode());
        ico_config_set_bool("gameplay.developer_mode", ico_opt_developer_mode());
        s_dirtyConfig = 1;
        break;
    default:
        break;
    }
    if (video) {
        ico_video_set(&o);
        s_dirtyVideo = 1;
    }
}

int ui_SettingsSave(void)
{
    int r = 0;
    if (s_dirtyBindings) {
        IcoBindings *b = ico_input_live_bindings();
        if (ico_input_write_bindings(b) < 0) {
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
    if (r != 0) {
        fprintf(stderr, "settings: could not write %s\n", ico_config_toml_path());
    }
    s_dirtyVideo = s_dirtyConfig = s_dirtyBindings = 0;
    return r;
}

/* ------------------------------------------------------------ capture */

void ui_RemapCaptureStart(UiRemapCapture *c, int target)
{
    memset(c, 0, sizeof(*c));
    c->active = 1;
    c->target = target;
    c->seq0 = ico_input_last_press(NULL, NULL);
}

int ui_RemapCaptureStep(UiRemapCapture *c, void *bindings)
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
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_TRIANGLE));
        return;
    case ICO_T_CIRCLE:
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_CIRCLE));
        return;
    case ICO_T_CROSS:
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_CROSS));
        return;
    case ICO_T_SQUARE:
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_SQUARE));
        return;
    case ICO_T_START:
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_START));
        return;
    case ICO_T_SELECT:
        snprintf(buf, size, "%s", ui_Str(UI_STR_BTN_SELECT));
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
    snprintf(buf, size, "%s %s", ui_Str((UiStrId)what), ui_Str((UiStrId)dirs[dir & 3]));
}

/* gamepad sources by position (ICO_GP_*); the names are UI_STR_PAD_*, in order */
static const char *padName(int src)
{
    return ui_Str((UiStrId)(UI_STR_PAD_SOUTH + src - ICO_GP_SOUTH));
}

static const char *const kMouseNames[ICO_MOUSE_BUTTONS] = {"",        "Mouse L",  "Mouse R",
                                                           "Mouse M", "Mouse X1", "Mouse X2"};

/* One column of the remap screen: the keyboard's keys and mouse buttons,
   or the gamepad's sources, of target t. */
static void sourcesText(const IcoBindings *b, int t, int gamepad, char *buf, size_t size)
{
    size_t n = 0;
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
            name = kMouseNames[b->mouse[t][i - ICO_BIND_MAX]];
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

/* Breaks text into lines of at most width x units at size (a note). */
static void wrapText(const char *text, float size, float width, char *out, unsigned outSize)
{
    unsigned n = 0, lineStart = 0, lastSpace = 0;
    for (const char *p = text; *p && n + 1 < outSize; p++) {
        out[n] = *p;
        out[n + 1] = '\0';
        if (*p == ' ') {
            lastSpace = n;
        }
        n++;
        if (lastSpace > lineStart && ui_MeasureText(size, out + lineStart) > width) {
            out[lastSpace] = '\n';
            lineStart = lastSpace + 1;
        }
    }
    out[n] = '\0';
}

/* A note row's text, wrapped; redone only when the string or the language
   changes (the wrap measures the text). */
static int s_noteStr[LT_EXT_MAX_PROPERTIES];
static int s_noteLang[LT_EXT_MAX_PROPERTIES];

static void setNote(int row, int strId)
{
    char buf[256];
    int i = row - LT_GAME_PROPERTY_COUNT;
    if (i >= 0 && i < LT_EXT_MAX_PROPERTIES && s_noteStr[i] == strId &&
        s_noteLang[i] == (int)ui_GetLanguage() + 1) {
        return;
    }
    wrapText(ui_Str((UiStrId)strId), NOTE_SIZE, 580.0f, buf, sizeof(buf));
    lt_ext_SetText(row, buf);
    if (i >= 0 && i < LT_EXT_MAX_PROPERTIES) {
        s_noteStr[i] = strId;
        s_noteLang[i] = (int)ui_GetLanguage() + 1;
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
static void buildMirrorScreen(void);
static void buildQuitScreen(void);
static int s_quitLayout = -1;
static void titleCursorOn(int to, int row);

/* the music gallery's page (docs/port/MUSIC.md, "The page"), in field
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
    (void)lt_ext_AddRect(GAL_BAR_X - 2, GAL_BAR_Y - 1, GAL_BAR_W + 4, GAL_BAR_H + 4, kRim);
    (void)lt_ext_AddRect(GAL_BAR_X, GAL_BAR_Y, GAL_BAR_W, GAL_BAR_H, kTrack);
    s_galFill = lt_ext_AddRect(GAL_BAR_X, GAL_BAR_Y, GAL_BAR_W, GAL_BAR_H, kFill);
    lt_ext_SetFill(s_galFill, 0.0f);
    /* the times' capitals on the bar's middle line (a label's capitals sit
       6.5 field lines below its 30-unit box's top) */
    const int ty = GAL_BAR_Y + GAL_BAR_H / 4 - 6;
    s_galTime = ui_SettingsAddRow(GAL_BAR_X - GAL_TIME_GAP - 100, ty, 100, 30, 0, -1, 0, " ",
                                  NOTE_SIZE, UI_ALIGN_RIGHT);
    s_galTotal = ui_SettingsAddRow(GAL_BAR_X + GAL_BAR_W + GAL_TIME_GAP, ty, 100, 30, 0, -1, 0, " ",
                                   NOTE_SIZE, UI_ALIGN_LEFT);
    ui_HintBuild(&s_galHint, GAL_HINT_Y, NOTE_SIZE, ui_hint_gallery, UI_HINT_GAL_COUNT);
}

static int rowY(int page, int i)
{
    if (page == UI_PAGE_DISPLAY) {
        /* thirteen rows (P3: Menu text joined Video mode; package CRT: the
           CRT filter and its strength): 14 field lines apart from 36, so
           Back still ends inside the 226 lines */
        return 36 + 14 * i;
    }
    return page == UI_PAGE_MAIN ? 40 + 19 * i : 40 + 18 * i;
}

static void addHeader(Page *pg, int strId)
{
    pg->header =
        ui_SettingsAddRow(20, HEADER_Y, 600, 40, 0, -1, strId, NULL, HEADER_SIZE, UI_ALIGN_CENTER);
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
    r->label = ui_SettingsAddRow(LABEL_X, y, LABEL_W, h, 1, -1, strId, NULL, 0.0f, UI_ALIGN_RIGHT);
    if (steppable(opt)) {
        r->value =
            ui_SettingsAddRow(STEP_X, y, STEP_W, h, 1, r->label, 0, " ", 0.0f, UI_ALIGN_CENTER);
        ui_SettingsAddRow(ARROW_L_X, y, ARROW_W, h, 1, r->label, 0, "\xE2\x80\xB9", 0.0f,
                          UI_ALIGN_LEFT);
        ui_SettingsAddRow(ARROW_R_X, y, ARROW_W, h, 1, r->label, 0, "\xE2\x80\xBA", 0.0f,
                          UI_ALIGN_LEFT);
    }
    pg->count++;
}

static void addNote(Page *pg, int opt, int strId)
{
    for (int i = 0; i < pg->count; i++) {
        if (pg->rows[i].opt == opt) {
            int n =
                ui_SettingsAddRow(20, NOTE_Y, 600, 30, 0, -1, 0, " ", NOTE_SIZE, UI_ALIGN_CENTER);
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
    int first = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT;
    addHeader(pg, header);
    for (int i = 0; i < n; i++) {
        addOption(pg, id, opts[i], strs[i], links ? links[i] : -1);
    }
    switch (id) {
    case UI_PAGE_MAIN:
        addNote(pg, UI_OPT_LANGUAGE, UI_STR_LANGUAGE_NOTE);
        addNote(pg, UI_OPT_DEVELOPER, UI_STR_DEVELOPER_NOTE);
        break;
    case UI_PAGE_DISPLAY:
        break;
    case UI_PAGE_AUDIO:
        break;
    case UI_PAGE_CONTROLS:
        addNote(pg, UI_OPT_CIRCLE_BACK, UI_STR_CIRCLE_BACK_NOTE);
        break;
    case UI_PAGE_GAMEPLAY:
        addNote(pg, UI_OPT_YORDA, UI_STR_OPT_YORDA_NOTE);
        break;
    case UI_PAGE_EXTRAS:
        /* the locked style's value: Credits shows it until unlocked */
        for (int i = 0; i < pg->count; i++) {
            Row *r = &pg->rows[i];
            if (r->opt == UI_OPT_EXTRAS_CREDITS) {
                r->value = ui_SettingsAddRow(STEP_X, rowY(id, i), STEP_W, 36, 1, r->label, 0, " ",
                                             0.0f, UI_ALIGN_CENTER);
            }
        }
        addNote(pg, UI_OPT_EXTRAS_CREDITS, UI_STR_EXTRAS_LOCKED_NOTE);
        break;
    default:
        break;
    }
    int last = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT - 1;
    linkRows(pg);
    finishPage(pg, first, last);
}

static void buildListPage(int id, int header, const UiListDef *def, int parent)
{
    Page *pg = &s_pages[id];
    memset(pg, 0, sizeof(*pg));
    pg->parent = parent;
    pg->isList = 1;
    int first = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT;
    addHeader(pg, header);
    int remap = id == UI_PAGE_REMAP;
    UiListStyle st;
    memset(&st, 0, sizeof(st));
    st.pitch = 18;
    if (remap) {
        /* the column heads */
        ui_SettingsAddRow(230, 32, 180, 30, 0, -1, UI_STR_REMAP_KEYBOARD, NULL, NOTE_SIZE,
                          UI_ALIGN_LEFT);
        ui_SettingsAddRow(420, 32, 190, 30, 0, -1, UI_STR_REMAP_GAMEPAD, NULL, NOTE_SIZE,
                          UI_ALIGN_LEFT);
        st.y0 = 50;
        st.label = (UiListCol){40, 180, 24.0f, UI_ALIGN_LEFT};
        st.colA = (UiListCol){230, 180, 21.0f, UI_ALIGN_LEFT};
        st.colB = (UiListCol){420, 190, 21.0f, UI_ALIGN_LEFT};
        st.statusY = 198;
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
    ui_ListBuild(&pg->list, def, NULL, &st);
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
    int last = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT - 1;
    finishPage(pg, first, last);
}

static void buildEntries(void)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        int title = e != ENTRY_OPTIONS;
        /* the y and the title rows' box are set with the game's rows
           (placeTitle, entryProc); the title rows' box is wider than the
           game's so the longer translations keep the game rows' size */
        int row =
            title ? ui_SettingsAddRow(120, 0, 400, 40, 1, -1, UI_STR_SETTINGS, NULL, 0.0f,
                                      UI_ALIGN_CENTER)
                  : ui_SettingsAddRow(OPTIONS_LABELS_END - LABEL_W, texProperty[325].dispY, LABEL_W,
                                      40, 1, -1, UI_STR_SETTINGS, NULL, 0.0f, UI_ALIGN_RIGHT);
        if (title) {
            P(row)->centerX = 1;
            /* hidden unless the title's proc shows it, as New Game (49 to
               51 are masked by default; open item 9) */
            P(row)->defaultMask = 1;
        }
        P(row)->right = s_pages[UI_PAGE_MAIN].layout;
        if (!title) {
            /* Triangle goes back to the pause menu, as on every Options row */
            P(row)->left = 57;
        }
        s_entryRow[e] = row;
        s_quitRow[e] = -1;
        if (!title) {
            /* package PHOTO: one Options pitch below Settings (entryProc
               sets the y with Settings'); Triangle back to the pause menu */
            int ph = ui_SettingsAddRow(OPTIONS_LABELS_END - LABEL_W, texProperty[325].dispY + 20,
                                       LABEL_W, 40, 1, -1, UI_STR_PHOTO_MODE, NULL, 0.0f,
                                       UI_ALIGN_RIGHT);
            P(ph)->left = 57;
            P(ph)->upItem = row;
            P(row)->downItem = ph;
            s_photoRow = ph;
        }
        if (title) {
            /* Q2: "Quit to desktop" under Settings, in the same layout (the
               rows are contiguous); Cross opens the confirmation */
            int q = ui_SettingsAddRow(120, 0, 400, 40, 1, -1, UI_STR_QUIT_DESKTOP, NULL, 0.0f,
                                      UI_ALIGN_CENTER);
            P(q)->centerX = 1;
            P(q)->defaultMask = 1;
            P(q)->right = s_quitLayout;
            P(q)->upItem = row;
            P(row)->downItem = q;
            s_quitRow[e] = q;
        }
        LtProp l;
        memset(&l, 0, sizeof(l));
        l.first = row;
        l.last =
            (s_quitRow[e] >= 0 ? s_quitRow[e] : (!title && s_photoRow >= 0 ? s_photoRow : row)) + 1;
        l.proc = entryProc;
        l.procFirst = 1;
        l.defaultItem = -1;
        l.curItem = -1;
        l.link = -1;
        s_entryLayout[e] = lt_ext_AddLayout(&l);
    }
}

static void build(void)
{
    /* Extras (after Achievements) is shown only when Settings was opened
       from the title (layoutMain) */
    static const int mainOpts[] = {UI_OPT_LINK, UI_OPT_LINK,      UI_OPT_LINK,
                                   UI_OPT_LINK, UI_OPT_LANGUAGE,  UI_OPT_LINK,
                                   UI_OPT_LINK, UI_OPT_DEVELOPER, UI_OPT_BACK};
    static const int mainStrs[] = {
        UI_STR_SECTION_DISPLAY,  UI_STR_SECTION_AUDIO,      UI_STR_SECTION_CONTROLS,
        UI_STR_SECTION_GAMEPLAY, UI_STR_SECTION_LANGUAGE,   UI_STR_SECTION_ACHIEVEMENTS,
        UI_STR_EXTRAS,           UI_STR_OPT_DEVELOPER_MODE, UI_STR_BACK};
    static const int mainLinks[] = {UI_PAGE_DISPLAY,
                                    UI_PAGE_AUDIO,
                                    UI_PAGE_CONTROLS,
                                    UI_PAGE_GAMEPLAY,
                                    -1,
                                    UI_PAGE_ACHIEVEMENTS,
                                    UI_PAGE_EXTRAS,
                                    -1,
                                    -1};
    static const int extrasOpts[] = {UI_OPT_EXTRAS_MUSIC, UI_OPT_EXTRAS_MODELS,
                                     UI_OPT_EXTRAS_CREDITS, UI_OPT_BACK};
    static const int extrasStrs[] = {UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                                     UI_STR_EXTRAS_CREDITS, UI_STR_BACK};
    int dispOpts[16], dispStrs[16], nd = 0;
    static const int dispAll[][2] = {{UI_OPT_PRESET, UI_STR_OPT_PRESET},
                                     {UI_OPT_RESOLUTION, UI_STR_OPT_RESOLUTION},
                                     {UI_OPT_ASPECT, UI_STR_OPT_ASPECT},
                                     {UI_OPT_FULLSCREEN, UI_STR_OPT_FULLSCREEN},
                                     {UI_OPT_VSYNC, UI_STR_OPT_VSYNC},
                                     {UI_OPT_FILTER, UI_STR_OPT_FILTERING},
                                     {UI_OPT_FULL_HEIGHT, UI_STR_OPT_FULL_HEIGHT},
                                     {UI_OPT_FRAMERATE, UI_STR_OPT_FRAMERATE},
                                     {UI_OPT_CRT, UI_STR_OPT_CRT},
                                     {UI_OPT_CRT_STRENGTH, UI_STR_OPT_CRT_STRENGTH},
                                     {UI_OPT_VIDEO_MODE, UI_STR_OPT_VIDEO_MODE},
                                     {UI_OPT_MENU_TEXT, UI_STR_OPT_MENU_TEXT},
                                     {UI_OPT_BACK, UI_STR_BACK}};
    for (unsigned i = 0; i < sizeof(dispAll) / sizeof(dispAll[0]); i++) {
        /* R7d: every row always shown, Frame rate included (stepped by
           ui_SettingsStep since R7d; read-only from the config in R7b) */
        dispOpts[nd] = dispAll[i][0];
        dispStrs[nd++] = dispAll[i][1];
    }
    static const int audioOpts[] = {UI_OPT_VOLUME, UI_OPT_MUSIC,  UI_OPT_EFFECTS,
                                    UI_OPT_OUTPUT, UI_OPT_DEVICE, UI_OPT_BACK};
    static const int audioStrs[] = {UI_STR_OPT_VOLUME, UI_STR_OPT_MUSIC_VOL, UI_STR_OPT_EFFECTS_VOL,
                                    UI_STR_OPT_OUTPUT, UI_STR_OPT_DEVICE,    UI_STR_BACK};
    static const int ctlOpts[] = {UI_OPT_LINK, UI_OPT_MOUSE_SENS, UI_OPT_CIRCLE_BACK, UI_OPT_BACK};
    static const int ctlStrs[] = {UI_STR_OPT_REMAP, UI_STR_OPT_MOUSE_SENS, UI_STR_OPT_CIRCLE_BACK,
                                  UI_STR_BACK};
    static const int ctlLinks[] = {UI_PAGE_REMAP, -1, -1, -1};
    static const int gameOpts[] = {UI_OPT_YORDA, UI_OPT_STICK_FIX, UI_OPT_BACK};
    static const int gameStrs[] = {UI_STR_OPT_YORDA, UI_STR_OPT_STICK_FIX, UI_STR_BACK};

    ui_FontInit(); /* the notes are wrapped by measuring */
    memset(s_pages, 0, sizeof(s_pages));
    /* the pages first (their layouts are the links' targets), then the
       entry rows */
    buildOptionPage(UI_PAGE_MAIN, UI_STR_SETTINGS, mainOpts, mainStrs, mainLinks, 9, -1);
    buildOptionPage(UI_PAGE_DISPLAY, UI_STR_SECTION_DISPLAY, dispOpts, dispStrs, NULL, nd,
                    UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_AUDIO, UI_STR_SECTION_AUDIO, audioOpts, audioStrs, NULL, 6,
                    UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_CONTROLS, UI_STR_SECTION_CONTROLS, ctlOpts, ctlStrs, ctlLinks, 4,
                    UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_GAMEPLAY, UI_STR_SECTION_GAMEPLAY, gameOpts, gameStrs, NULL, 3,
                    UI_PAGE_MAIN);
    buildOptionPage(UI_PAGE_EXTRAS, UI_STR_EXTRAS, extrasOpts, extrasStrs, NULL, 4, UI_PAGE_MAIN);
    buildListPage(UI_PAGE_ACHIEVEMENTS, UI_STR_SECTION_ACHIEVEMENTS, &kAchDef, UI_PAGE_MAIN);
    buildListPage(UI_PAGE_REMAP, UI_STR_OPT_REMAP, &kRemapDef, UI_PAGE_CONTROLS);
    buildListPage(UI_PAGE_MUSIC, UI_STR_EXTRAS_MUSIC, &kGalDef, UI_PAGE_EXTRAS);
    /* the section rows open their pages */
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        for (int i = 0; i < s_pages[p].count; i++) {
            Row *r = &s_pages[p].rows[i];
            if (r->opt == UI_OPT_LINK && r->link >= 0) {
                P(r->label)->right = s_pages[r->link].layout;
            }
        }
    }
    buildQuitScreen();
    buildEntries();
    if (s_photoRow >= 0) {
        P(s_photoRow)->right = ui_PhotoBuild(s_photoRow);
    }
    buildMirrorScreen();
    if (ico_opt_developer_mode()) {
        /* the layout extension's budget (layout_ext.h): what Settings and
           its screens use of the tables */
        fprintf(stderr, "settings: layout extension: %d of %d properties, %d of %d layouts\n",
                lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES, lt_ext_LayoutCount(),
                LT_EXT_MAX_LAYOUTS);
    }
}

/* ------------------------------------------------- the mirror screen (R7c)
 * The New Game "Mirror mode" screen, between the vibration choice and the
 * start (settings.h ui_MirrorScreenEnter): the header, "Off" and "On" side
 * by side (left/right through their item links, the cursor on "Off"), and
 * a line of explanation, in the lower half where the vibration screen has
 * its rows (the title stage's logo is above). */

static int s_mirrorLayout = -1;
static int s_mirrorRow[2] = {-1, -1};
static int s_mirrorChosen;

#define LAYOUT_VIBE_SELECT 9 /* la_vibe_select's screen */

static int mirrorScreenProc(int first, int item);

static void buildMirrorScreen(void)
{
    int first = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT;
    int h = ui_SettingsAddRow(20, 112, 600, 40, 0, -1, UI_STR_OPT_MIRROR, NULL, HEADER_SIZE,
                              UI_ALIGN_CENTER);
    P(h)->centerX = 1;
    s_mirrorRow[0] =
        ui_SettingsAddRow(200, 146, 110, 40, 1, -1, UI_STR_OFF, NULL, 0.0f, UI_ALIGN_CENTER);
    s_mirrorRow[1] =
        ui_SettingsAddRow(330, 146, 110, 40, 1, -1, UI_STR_ON, NULL, 0.0f, UI_ALIGN_CENTER);
    P(s_mirrorRow[0])->rightItem = s_mirrorRow[1];
    P(s_mirrorRow[1])->leftItem = s_mirrorRow[0];
    int n = ui_SettingsAddRow(20, 182, 600, 30, 0, -1, 0, " ", NOTE_SIZE, UI_ALIGN_CENTER);
    P(n)->centerX = 1;
    setNote(n, UI_STR_MIRROR_SCREEN);
    int last = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT - 1;
    s_mirrorLayout = addLayout(first, last + 1, 0.6f, mirrorScreenProc, s_mirrorRow[0]);
}

int ui_MirrorScreenEnter(void)
{
    if (!s_built || s_mirrorLayout < 0) {
        return -1;
    }
    LtProp *l = lt_ext_Layout(s_mirrorLayout);
    l->defaultItem = l->curItem = s_mirrorRow[0];
    s_mirrorChosen = 0;
    /* no run until the choice: a cleared save's new game (la_load_processing
       returns to the vibration screen) does not keep the loaded slot's flag
       on the title stage behind this screen */
    ico_opt_mirror_reset();
    return s_mirrorLayout;
}

int ui_MirrorScreenLayout(void)
{
    return s_mirrorLayout;
}

int ui_MirrorScreenRow(int on)
{
    return s_mirrorRow[on ? 1 : 0];
}

static int mirrorScreenProc(int first, int item)
{
    (void)item;
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    if (first) {
        s_mirrorChosen = 0;
    }
    /* as la_vibe_select: input only once faded in, and the choice once (the
       stage change stops the layout procs in the same tick) */
    if (s_mirrorChosen || lt_fade_status() != 2) {
        return -1;
    }
    int flags = pad[0].flags;
    if (flags & (PAD_CROSS | PAD_START)) {
        s_mirrorChosen = 1;
        POSITIVE_SE();
        ico_opt_set_mirror(lt_ext_Layout(s_mirrorLayout)->curItem == s_mirrorRow[1]);
        la_host_new_game_go();
        return -1;
    }
    if (flags & PAD_BACK) {
        NEGATIVE_SE();
        return LAYOUT_VIBE_SELECT;
    }
    return -1;
}

/* ------------------------------------------------- the quit screen (Q2)
 * "Quit to desktop?" with Yes and No side by side (left/right through
 * their item links, the cursor on No), opened by the title's Quit row
 * (settings.h ui_SettingsSetQuitHandler), laid out as the mirror screen. */

static int s_quitYesNo[2] = {-1, -1}; /* No, Yes */
static int s_quitChosen;
static void (*s_quitHandler)(void);

static int quitScreenProc(int first, int item);

static void buildQuitScreen(void)
{
    int first = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT;
    int h = ui_SettingsAddRow(20, 112, 600, 40, 0, -1, UI_STR_QUIT_CONFIRM, NULL, HEADER_SIZE,
                              UI_ALIGN_CENTER);
    P(h)->centerX = 1;
    s_quitYesNo[1] =
        ui_SettingsAddRow(200, 146, 110, 40, 1, -1, UI_STR_MT_YES, NULL, 0.0f, UI_ALIGN_CENTER);
    s_quitYesNo[0] =
        ui_SettingsAddRow(330, 146, 110, 40, 1, -1, UI_STR_MT_NO, NULL, 0.0f, UI_ALIGN_CENTER);
    P(s_quitYesNo[1])->rightItem = s_quitYesNo[0];
    P(s_quitYesNo[0])->leftItem = s_quitYesNo[1];
    int last = lt_ext_PropCount() + LT_GAME_PROPERTY_COUNT - 1;
    s_quitLayout = addLayout(first, last + 1, 0.6f, quitScreenProc, s_quitYesNo[0]);
}

void ui_SettingsSetQuitHandler(void (*fn)(void))
{
    s_quitHandler = fn;
}

int ui_QuitScreenLayout(void)
{
    return s_built ? s_quitLayout : -1;
}

int ui_QuitScreenRow(int yes)
{
    return s_quitYesNo[yes ? 1 : 0];
}

static void requestQuit(void)
{
    /* what a Settings page writes when it is left, should anything be
       pending; the achievements and the audio are the atexit handlers' */
    ui_SettingsSave();
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
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    LtProp *l = lt_ext_Layout(s_quitLayout);
    if (first) {
        s_quitChosen = 0; /* the switch put the cursor on No (the default) */
    }
    if (s_quitChosen || lt_fade_status() != 2) {
        return -1;
    }
    int flags = pad[0].flags;
    int to = s_origin == LAYOUT_TITLE_CONTINUE ? LAYOUT_TITLE_CONTINUE : LAYOUT_TITLE_NEW;
    if ((flags & PAD_CROSS) && l->curItem == s_quitYesNo[1]) {
        s_quitChosen = 1;
        POSITIVE_SE();
        requestQuit();
        return -1;
    }
    if (flags & (PAD_CROSS | PAD_BACK)) {
        NEGATIVE_SE();
        la_host_leave();
        titleCursorOn(to, s_quitRow[to == LAYOUT_TITLE_NEW ? ENTRY_TITLE13 : ENTRY_TITLE12]);
        return to;
    }
    return -1;
}

/* whether the title's game rows are at (Continue, New Game, copyright) */
static int titleRowsAt(int cont, int newGame, int copyright)
{
    return texProperty[49].dispY == cont && texProperty[50].dispY == newGame &&
           texProperty[51].dispY == newGame && texProperty[48].dispY == copyright;
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
    texProperty[49].dispY = TITLE_Y(0);
    texProperty[50].dispY = texProperty[51].dispY = TITLE_Y(1);
    texProperty[48].dispY = TITLE_COPYRIGHT_Y;
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        const int rows[2] = {s_entryRow[e], s_quitRow[e]};
        for (int i = 0; i < 2; i++) {
            if (rows[i] >= 0) {
                P(rows[i])->dispY = TITLE_Y(2 + i);
                P(rows[i])->dispH = texProperty[50].dispH;
            }
        }
    }
}

/* Package PHOTO: the "Photo mode" row under Settings while a stage runs
   (ui_PhotoAvailable); otherwise masked and stepped over through the item
   links (layout_texture.c's visibility skip does not look at masks):
   Settings -> 300 and 300 -> Settings as before the row existed. */
static void photoLinks(void)
{
    const int s58 = s_entryRow[ENTRY_OPTIONS];
    if (s_photoRow < 0 || s58 < 0) {
        return;
    }
    const int on = ui_PhotoAvailable();
    P(s_photoRow)->defaultMask = on ? 0 : 1;
    lt_mask_property(s_photoRow, on ? 0 : 1);
    P(s_photoRow)->upItem = s58;
    P(s_photoRow)->downItem = 300;
    P(s58)->downItem = on ? s_photoRow : 300;
    texProperty[300].upItem = on ? s_photoRow : s58;
}

/* The game's rows, pointed at the entry rows.  Checked against the loaded
   tables first: a table that does not look like the PAL data is left
   alone (logged once). */
static void repoint(void)
{
    LtProperty *r325 = &texProperty[325], *r300 = &texProperty[300];
    int s58 = s_entryRow[ENTRY_OPTIONS];
    if (texLayout[58].first != 297 || texLayout[58].last != 333 || r325->upItem != 324 ||
        (r325->downItem != 300 && r325->downItem != s58) || texLayout[12].first != 49 ||
        texLayout[13].first != 51 ||
        !(titleRowsAt(PAL_CONTINUE_Y, PAL_NEW_GAME_Y, PAL_COPYRIGHT_Y) ||
          titleRowsAt(TITLE_Y(0), TITLE_Y(1), TITLE_COPYRIGHT_Y))) {
        if (!s_warned) {
            fprintf(stderr, "settings: the layout tables are not the expected ones; no entry\n");
            s_warned = 1;
        }
        return;
    }
    placeTitle();
    r325->downItem = s58;
    r300->upItem = s58;
    P(s58)->upItem = 325;
    P(s58)->downItem = 300;
    photoLinks();
    if (texLayout[58].link != s_entryLayout[ENTRY_OPTIONS]) {
        lt_ext_Layout(s_entryLayout[ENTRY_OPTIONS])->link = texLayout[58].link;
        texLayout[58].link = s_entryLayout[ENTRY_OPTIONS];
    }
    static const int gameRow[3] = {0, 50, 51};
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        int g = kEntryGame[e];
        texProperty[gameRow[e]].downItem = s_entryRow[e];
        P(s_entryRow[e])->upItem = gameRow[e];
        if (texLayout[g].link != s_entryLayout[e]) {
            lt_ext_Layout(s_entryLayout[e])->link = texLayout[g].link;
            texLayout[g].link = s_entryLayout[e];
        }
    }
}

#ifdef ICO_RD
/* R7c: the renderer follows the run's mirror mode (options.h listener) */
static void mirrorChanged(int on)
{
    rd_SetMirror(on);
}
#endif

void ui_SettingsInstall(void)
{
    /* P3: [game] classic_menu_text, before the layouts draw */
    ui_MenuTextSetClassic(ico_opt_classic_menu_text());
    /* Q2: [game] circle_back, before the game's menus read a press */
    lt_ext_SetCircleBack(ico_opt_circle_back());
    /* [audio] output: an explicit stereo or mono is the game's from the
       start (the card's system file is read later: fumi/ios/mcard.c's hook) */
    applyOutputMode(0);
    if (!s_built) {
        build();
        s_built = 1;
#ifdef ICO_RD
        ico_opt_set_mirror_listener(mirrorChanged);
#endif
    }
    repoint();
}

void ui_SettingsReset(void)
{
    s_built = 0;
    s_warned = 0;
    s_origin = LAYOUT_PAUSE_OPTIONS;
    s_restoreTitle = -1;
    s_dirtyVideo = s_dirtyConfig = s_dirtyBindings = 0;
    memset(&s_capture, 0, sizeof(s_capture));
    for (int e = 0; e < ENTRY_COUNT; e++) {
        s_entryRow[e] = s_entryLayout[e] = s_quitRow[e] = -1;
    }
    s_photoRow = -1;
    ui_PhotoReset();
    memset(s_pages, 0, sizeof(s_pages));
    /* the wrapped notes are set again on the rebuilt rows */
    memset(s_noteStr, 0, sizeof(s_noteStr));
    memset(s_noteLang, 0, sizeof(s_noteLang));
    s_mirrorLayout = s_mirrorRow[0] = s_mirrorRow[1] = -1;
    s_mirrorChosen = 0;
    s_quitLayout = s_quitYesNo[0] = s_quitYesNo[1] = -1;
    s_quitChosen = 0;
}

int ui_SettingsEntryItem(int item)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        if (item >= 0 && (item == s_entryRow[e] || item == s_quitRow[e])) {
            return 1;
        }
    }
    return 0;
}

void ui_SettingsTitleMask(int masked)
{
    for (int e = ENTRY_TITLE12; e <= ENTRY_TITLE13; e++) {
        if (s_entryRow[e] >= 0) {
            lt_mask_property(s_entryRow[e], masked);
        }
        if (s_quitRow[e] >= 0) {
            lt_mask_property(s_quitRow[e], masked);
        }
    }
}

int ui_SettingsEntryRow(int gameLayout)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        if (kEntryGame[e] == gameLayout) {
            return s_entryRow[e];
        }
    }
    return -1;
}

int ui_SettingsQuitRow(int gameLayout)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        if (kEntryGame[e] == gameLayout) {
            return s_quitRow[e];
        }
    }
    return -1;
}

int ui_SettingsPhotoRow(void)
{
    return s_photoRow;
}

int ui_SettingsEntryLayout(int gameLayout)
{
    for (int e = 0; e < ENTRY_COUNT; e++) {
        if (kEntryGame[e] == gameLayout) {
            return s_entryLayout[e];
        }
    }
    return -1;
}

int ui_SettingsPageLayout(UiSettingsPage page)
{
    return s_built && page >= 0 && page < UI_PAGE_COUNT ? s_pages[page].layout : -1;
}

int ui_SettingsPageRows(UiSettingsPage page, int *labels, int *opts, int *values, int max)
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

int ui_SettingsRowOf(UiSettingsPage page, UiSettingsOpt opt)
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
    if (cur == LAYOUT_PAUSE_OPTIONS || cur == LAYOUT_TITLE_CONTINUE || cur == LAYOUT_TITLE_NEW) {
        s_origin = cur;
        /* the menu opens on its first row each time, as Options does */
        if (s_pages[UI_PAGE_MAIN].layout >= 0) {
            LtProp *m = lt_ext_Layout(s_pages[UI_PAGE_MAIN].layout);
            m->defaultItem = pageFirstNav(&s_pages[UI_PAGE_MAIN]);
        }
    }
    if (cur == LAYOUT_PAUSE_OPTIONS && s_entryRow[ENTRY_OPTIONS] >= 0) {
        /* in 325's place, or one Options pitch below it once it shows */
        const LtProperty *r324 = &texProperty[324], *r325 = &texProperty[325];
        P(s_entryRow[ENTRY_OPTIONS])->dispY =
            r325->dispY + (gFlagGameClear ? r325->dispY - r324->dispY : 0);
        if (s_photoRow >= 0) {
            P(s_photoRow)->dispY =
                P(s_entryRow[ENTRY_OPTIONS])->dispY + (r325->dispY - r324->dispY);
            photoLinks();
        }
    }
    if (s_restoreTitle >= 0 && cur == s_restoreTitle) {
        /* the cursor came back to the Settings row; the title's own
           default is the game's again for its next showing */
        lt_ext_Layout(s_restoreTitle)->defaultItem = s_restoreDefault;
        s_restoreTitle = -1;
    }
    return -1;
}

/* The title layout `to` opens with the cursor on row (a port row of its
   entry layout); entryProc gives the title its own default back on the
   next frame. */
static void titleCursorOn(int to, int row)
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
    ui_SettingsSave();
    la_host_leave();
    int toPage;
    if (pageOfLayout(to, &toPage)) {
        for (int i = 0; i < s_pages[toPage].count; i++) {
            if (s_pages[toPage].rows[i].link == pageId) {
                lt_ext_Layout(to)->defaultItem = s_pages[toPage].rows[i].label;
            }
        }
    } else if (to == LAYOUT_PAUSE_OPTIONS) {
        /* as la_key_config and la_adjust_screen put it on their rows */
        texLayout[58].defaultItem = s_entryRow[ENTRY_OPTIONS];
    } else if (to == LAYOUT_TITLE_CONTINUE || to == LAYOUT_TITLE_NEW) {
        titleCursorOn(to, s_entryRow[to == LAYOUT_TITLE_NEW ? ENTRY_TITLE13 : ENTRY_TITLE12]);
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

static int achCount(void *user)
{
    (void)user;
    return ico_ach_count() + 1; /* the achievements, then Back */
}

static void achFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    if (d == ico_ach_count()) {
        out->labelStr = UI_STR_BACK;
        return;
    }
    IcoAchState st = ico_ach_state(d);
    int hidden = ico_ach_hidden(d) && st == ICO_ACH_LOCKED;
    out->label = hidden ? "???" : ui_Str((UiStrId)ico_ach_title_str(d));
    out->colAStr = st == ICO_ACH_UNLOCKED ? UI_STR_ACH_STATE_UNLOCKED : UI_STR_ACH_LOCKED;
}

/* the header carries the count; the status line is the cursor's description */
static void achDecorate(void *user, int d)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_ACHIEVEMENTS];
    int n = ico_ach_count(), got = 0;
    for (int i = 0; i < n; i++) {
        got += ico_ach_state(i) != ICO_ACH_LOCKED;
    }
    snprintf(s_text, sizeof(s_text), "%s   %d / %d", ui_Str(UI_STR_SECTION_ACHIEVEMENTS), got, n);
    lt_ext_SetText(pg->header, s_text);
    char buf[256];
    if (d >= 0 && d < n) {
        int hidden = ico_ach_hidden(d) && ico_ach_state(d) == ICO_ACH_LOCKED;
        wrapText(hidden ? "???" : ui_Str((UiStrId)ico_ach_desc_str(d)), NOTE_SIZE, 580.0f, buf,
                 sizeof(buf));
        lt_ext_SetText(pg->list.status, buf);
    } else {
        lt_ext_SetText(pg->list.status, "");
    }
}

static int achInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_ACHIEVEMENTS];
    if ((flags & PAD_BACK) || ((flags & PAD_CROSS) && d == ico_ach_count())) {
        return leaveTo(UI_PAGE_ACHIEVEMENTS, parentLayout(pg));
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
    setNote(s_pages[UI_PAGE_REMAP].list.status,
            s_capture.active ? UI_STR_REMAP_PRESS : UI_STR_REMAP_HINT);
}

static int remapInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_REMAP];
    if (s_capture.active) {
        lt_item_select_disable = 1;
        int r = ui_RemapCaptureStep(&s_capture, liveBindings());
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
        if (flags & PAD_CROSS) {
            POSITIVE_SE();
            ui_RemapCaptureStart(&s_capture, kRemapOrder[d]);
            lt_item_select_disable = 1;
            return -1;
        }
        if (flags & PAD_SQUARE) {
            CUR_SE();
            ico_bindings_clear(liveBindings(), kRemapOrder[d]);
            s_dirtyBindings = 1;
            return -1;
        }
    } else if (d == ICO_T_COUNT && (flags & PAD_CROSS)) {
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
    } else if (d == ICO_T_COUNT + 1 && (flags & PAD_CROSS)) {
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
    return gallery_Count();
}

static void galFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    static char lab[UI_LIST_SLOTS][96], col[UI_LIST_SLOTS][96];
    static int k;
    k = (k + 1) % UI_LIST_SLOTS;
    out->label = gallery_Label(d, lab[k], sizeof(lab[k]));
    out->colA = gallery_ColA(d, col[k], sizeof(col[k]));
}

static int galHeading(void *user, int d)
{
    (void)user;
    const GalleryItem *it = gallery_Item(d);
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
    if (gallery_Position(&el, &tot) == 0) {
        lt_ext_SetFill(s_galFill, tot > 0.0f ? el / tot : 0.0f);
        galClock(a, sizeof(a), el);
        if (tot > 0.0f) {
            galClock(b, sizeof(b), tot);
        } else {
            snprintf(b, sizeof(b), "-:--");
        }
    } else {
        lt_ext_SetFill(s_galFill, 0.0f);
        galClock(a, sizeof(a), 0.0f);
        galClock(b, sizeof(b), 0.0f);
    }
    lt_ext_SetText(s_galTime, a);
    lt_ext_SetText(s_galTotal, b);
}

/* headings stand out to the left; the status line: the group, the file and
   whether the entry under the cursor plays; the bar; Cross's word */
static void galDecorate(void *user, int d)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_MUSIC];
    for (int s = 0; s < UI_LIST_SLOTS; s++) {
        int item = ui_ListItemAt(&pg->list, s);
        P(pg->list.label[s])->dispX = galHeading(NULL, item) ? 24 : 40;
    }
    const GalleryItem *it = gallery_Item(d);
    char asset[128];
    if (gallery_Tables() == NULL) {
        lt_ext_SetText(pg->list.status, ui_Str(UI_STR_GAL_EMPTY));
    } else if (it == NULL || it->kind == GAL_K_BACK) {
        lt_ext_SetText(pg->list.status, "");
    } else if (it->kind == GAL_K_HEADING) {
        snprintf(s_text, sizeof(s_text), "%s", gallery_Asset(d, asset, sizeof(asset)));
        lt_ext_SetText(pg->list.status, s_text);
    } else {
        int state = gallery_Paused() == d    ? UI_STR_GAL_PAUSED
                    : gallery_Playing() == d ? UI_STR_GAL_PLAYING
                                             : UI_STR_GAL_STOPPED;
        snprintf(s_text, sizeof(s_text), "%s  \xC2\xB7  %s  \xC2\xB7  %s",
                 ui_Str((UiStrId)gallery_GroupStr(it->group)),
                 gallery_Asset(d, asset, sizeof(asset)), ui_Str((UiStrId)state));
        lt_ext_SetText(pg->list.status, s_text);
    }
    galBar();
    if (s_galHint.n > 0) {
        /* Cross pauses the entry under the cursor while it sounds */
        int pausing = d >= 0 && gallery_Playing() == d && gallery_Paused() != d;
        ui_HintSetStr(&s_galHint, UI_HINT_GAL_PLAY, pausing ? UI_STR_HINT_PAUSE : UI_STR_HINT_PLAY);
        ui_HintLayout(&s_galHint);
    }
}

static int galLeave(void)
{
    gallery_Leave();
    int to = leaveTo(UI_PAGE_MUSIC, parentLayout(&s_pages[UI_PAGE_MUSIC]));
    /* back on the Music row */
    int row = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
    if (to == s_pages[UI_PAGE_EXTRAS].layout && row >= 0) {
        lt_ext_Layout(to)->defaultItem = row;
    }
    return to;
}

static int galInput(void *user, int d, int flags)
{
    (void)user;
    Page *pg = &s_pages[UI_PAGE_MUSIC];
    const GalleryItem *it = gallery_Item(d);
    if ((flags & PAD_BACK) || ((flags & PAD_CROSS) && it != NULL && it->kind == GAL_K_BACK)) {
        return galLeave();
    }
    if ((flags & PAD_CROSS) && it != NULL && it->kind != GAL_K_HEADING) {
        gallery_Toggle(d);
        return -1;
    }
    if (flags & PAD_SQUARE) {
        gallery_Stop();
        return -1;
    }
    if ((flags & (PAD_L1 | PAD_R1)) && d >= 0) {
        /* the previous or next entry: the cursor on it, and it plays */
        int k = gallery_Step(d, (flags & PAD_L1) ? -1 : 1);
        int n = ui_ListCount(&pg->list), shown = ui_ListShown(&pg->list);
        if (k >= 0) {
            if (k < pg->list.offset) {
                pg->list.offset = k > 0 && galHeading(NULL, k - 1) ? k - 1 : k;
            } else if (k >= pg->list.offset + shown) {
                pg->list.offset = k - shown + 1;
            }
            pg->list.offset = pg->list.offset > n - shown ? n - shown : pg->list.offset;
            lt_ext_Layout(pg->layout)->curItem = pg->list.label[k - pg->list.offset];
            gallery_Play(k);
        }
        return -1;
    }
    if ((flags & (PAD_LEFT | PAD_RIGHT)) && d >= 0) {
        /* the next or previous group, at the top of the window */
        int k = gallery_JumpGroup(d, (flags & PAD_LEFT) ? -1 : 1);
        int n = ui_ListCount(&pg->list), shown = ui_ListShown(&pg->list);
        if (k >= 0) {
            int head = k > 0 && galHeading(NULL, k - 1) ? k - 1 : k;
            pg->list.offset = head > n - shown ? n - shown : head;
            lt_ext_Layout(pg->layout)->curItem = pg->list.label[k - pg->list.offset];
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
        return resolutionLocked(); /* package CRT2 */
    }
    return r->opt == UI_OPT_EXTRAS_CREDITS && !creditsUnlocked();
}

/* The main page's rows as the entry in force shows them: the Extras row
   only from the title.  The visible rows are spaced evenly (the nine of the
   title on a 17 line pitch so Back stays above the notes, the eight of the
   pause menu on the original 19), linked in a loop that skips the hidden
   one, and the hidden one is masked. */
static void layoutMain(Page *pg)
{
    int shown[MAX_ROWS], n = 0;
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        int show = !(r->opt == UI_OPT_LINK && r->link == UI_PAGE_EXTRAS) || onTitle();
        P(r->label)->defaultMask = !show;
        lt_mask_property(r->label, !show);
        if (show) {
            shown[n++] = i;
        }
    }
    int pitch = n > 8 ? 17 : 19;
    for (int k = 0; k < n; k++) {
        Row *r = &pg->rows[shown[k]];
        int y = 40 + pitch * k;
        P(r->label)->dispY = y;
        if (r->value >= 0) {
            /* the value and its two arrows, added after the label */
            for (int j = 0; j < 3; j++) {
                P(r->value + j)->dispY = y;
            }
        }
        P(r->label)->downItem = pg->rows[shown[(k + 1) % n]].label;
        P(r->label)->upItem = pg->rows[shown[(k + n - 1) % n]].label;
    }
}

static void refreshPage(Page *pg, int id, int cur)
{
    if (pg->isList) {
        ui_ListRefresh(&pg->list, cur);
        return;
    }
    if (id == UI_PAGE_MAIN) {
        layoutMain(pg);
    }
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        int locked = rowLocked(r);
        if (r->value >= 0) {
            lt_ext_SetText(r->value, ui_SettingsValueText((UiSettingsOpt)r->opt));
        }
        if (isExtrasOpt(r->opt) || r->opt == UI_OPT_RESOLUTION) {
            /* the locked style: the label and its value greyed */
            lt_ext_SetDim(r->label, locked);
            if (r->value >= 0) {
                lt_ext_SetDim(r->value, locked);
            }
        }
        if (r->note >= 0) {
            setNote(r->note, r->noteStr);
            if (cur == r->label && (!isExtrasOpt(r->opt) || locked)) {
                lt_mask_property(r->note, 0);
            }
        }
    }
}

static int settingsProc(int first, int item)
{
    int id;
    Page *pg = pageOfLayout(current_layout_id, &id);
    LtProp *lay = lt_ext_Layout(current_layout_id);
    (void)item;
    if (pg == NULL) {
        return -1;
    }
    /* the port's strings follow the game's language (ui_host.c does too) */
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    if (id == UI_PAGE_MUSIC) {
        gallery_Tick(); /* the engine, once a Main tick while the page is up */
    }
    if (first) {
        if (pg->isList) {
            ui_ListReset(&pg->list);
        }
        memset(&s_capture, 0, sizeof(s_capture));
    }
    refreshPage(pg, id, lay->curItem);
    if (lt_fade_status() != 2) {
        return -1;
    }
    lt_analog2Pad();
    int flags = pad[0].flags;
    if (id == UI_PAGE_MUSIC && gallery_ScriptLeave()) {
        return galLeave(); /* the ICO_GALLERY_PLAY script's leave entry */
    }
    if (pg->isList) {
        int r = ui_ListProc(&pg->list, lay, flags);
        refreshPage(pg, id, lay->curItem);
        return r;
    }
    if (flags & PAD_BACK) {
        return leaveTo(id, parentLayout(pg));
    }
    for (int i = 0; i < pg->count; i++) {
        Row *r = &pg->rows[i];
        if (r->label != lay->curItem) {
            continue;
        }
        if ((flags & (PAD_LEFT | PAD_RIGHT)) && canStep(r->opt)) {
            ui_SettingsStep((UiSettingsOpt)r->opt, (flags & PAD_LEFT) ? -1 : 1);
            CUR_SE();
            refreshPage(pg, id, lay->curItem);
        }
        if ((flags & PAD_CROSS) && r->opt == UI_OPT_BACK) {
            return leaveTo(id, parentLayout(pg));
        }
        if ((flags & PAD_CROSS) && isExtrasOpt(r->opt)) {
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
