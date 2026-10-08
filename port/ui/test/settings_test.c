/* settings_test.c: the Settings menu (Phase 6, 6C).  CPU only.
 *
 *   - the menu builds over fake game tables shaped like the PAL ones: the
 *     pages and their rows, in order, with the expected labels;
 *   - the navigation repoint: the pause menu's Options (294) opens the
 *     menu (S1), the title's 50/51 lead to the entry rows, the link
 *     chains, idempotence;
 *   - the entry rows' places from the table data: the title's evenly
 *     spaced between New Game and the copyright line, masked by default as
 *     New Game; the pause menu's Photo mode row under Options;
 *   - through the real layout_texture.c: the cursor in layout 57 moves onto
 *     Options, Cross opens the menu, a right press on Language changes the
 *     language, Triangle goes back to 57 with the cursor on Options;
 *   - the game's Options screen's settings on the pages (S1): their game
 *     variables, the pause-only and cleared-only rows, Button
 *     configuration's way to the game's screen and back;
 *   - value cycling: each option's setter and its text;
 *   - the save: config.toml holds what changed;
 *   - the remap capture: a key, a gamepad source, the duplicate taken off
 *     the other target, the timeout, and the screen's Cross / capture /
 *     write path;
 *   - the boot skip's mapping over synthetic configs (language codes,
 *     video_mode, the card override).
 *
 * Built a second time with SETTINGS_RENDER (settings_render): the same
 * menu run through rd on a Vulkan device (lavapipe here; exit 77 without
 * one) with the window build's GifPacket.c, DisplayList.c and DmaPacket.c,
 * and each screen's SCENE written as a PNG beside the test for a look;
 * (settings_<screen>.png): no game data, the backdrop over a flat colour;
 * package DEF: then each screen presented at Enhanced 1920 x 1080
 * (settings_<screen>_1080.png), the port's rows deferred (RDC_OVERLAY_TEXT
 * items, drawn on the output) and the game's rows their texture sprites
 * (package TXT2: a screen of game rows alone, the save preview, records no
 * item).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "typedef.h"
#include "main.h"
#include "layout_texture.h"
#include "charFileManager.h"
#include "StageManager.h"
#include <libscf.h>
#include "achievements.h"
#include "config.h"
#include "font.h"
#include "gallery.h"
#include "host_config.h"
#include "ico_credits.h"
#include "ico_gamestate.h"
#include "input.h"
#include "layout_ext.h"
#include "audio_host.h"
#include "menu_text.h"
#include "mix_gain.h"
#include "options.h"
#include "photo_mode.h"
#include "photo_ui.h"
#include "settings.h"
#include "strings.h"
#include "sysconf.h"
#include "ui_hint.h"
#include "ui_list.h"
#include "video_options.h"

#ifdef SETTINGS_RENDER

#include "DisplayList.h"
#include "GifPacket.h"
#include "GifHost.h"
#include "rd_internal.h"
#include "ui_internal.h"

#endif

static int failures;

#ifdef _WIN32

static void setEnv(const char *k, const char *v)
{
    _putenv_s(k, v ? v : "");
}

#else

static void setEnv(const char *k, const char *v)
{
    if (v) {
        setenv(k, v, 1);
    } else {
        unsetenv(k);
    }
}

#endif
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------- what the game files import (stubs) */
int ScreenWidth = 512, ScreenHeight = 512;

float center_X = 2048.0f, center_Y = 2048.0f;

LtProp texLayout[LT_GAME_LAYOUT_COUNT];

LtProperty texProperty[LT_GAME_PROPERTY_COUNT];

TexRec texFile[1];

const StgPre stageData[110];

PadState pad[16];

StageSetting GlobalStageSetting;

int gFlagGameClear;

int stage_no; /* package PHOTO: 0 here, a stage number in testPhoto */

/* package PHOTO: photo mode's pivot (photo_ui.c): no camera target here */
void *default_cameratarget_gobj;

void GetRootPosition(void *pos, void *obj)
{
    (void)obj;
    memset(pos, 0, 16);
}

int systemStatus[12] = {1, 2};

int frame_count = 100;

int layout_boot_flag;

int title_demo_mode;

unsigned int stage_after_skipping_demo;

int mpegPlayReturnStage;

int NonLinearCameraMove = 2;

static int s_resets, s_sounds[3], s_leaves;

int gsResetFunc(int val)
{
    (void)val;
    s_resets++;
    return 1;
}

void CUR_SE(void)
{
    s_sounds[0]++;
}

void POSITIVE_SE(void)
{
    s_sounds[1]++;
}

void NEGATIVE_SE(void)
{
    s_sounds[2]++;
}

void la_host_leave(void)
{
    s_leaves++;
}

static int s_newGames;

/* fumi/sound/s_init.c: the game's stereo (0) or mono (1) output */
static int s_outputMode, s_outputSets;

int soundOutputModeGet(void)
{
    return s_outputMode;
}

void soundOutputModeSet(int mode)
{
    s_outputMode = mode;
    s_outputSets++;
}

/* port/audio/out_sdl.c's device list and reopen (the window build's) */
static int s_devCount;
static const char *s_devNames[3];
static char s_reopened[ICO_AUDIO_DEVICE_NAME_MAX];
static int s_reopens;

int ico_audio_sdl_devices(char names[][ICO_AUDIO_DEVICE_NAME_MAX], int max)
{
    int n = s_devCount < max ? s_devCount : max;
    for (int i = 0; i < n; i++) {
        snprintf(names[i], ICO_AUDIO_DEVICE_NAME_MAX, "%s", s_devNames[i]);
    }
    return n;
}

int ico_audio_sdl_reopen(const char *name)
{
    snprintf(s_reopened, sizeof(s_reopened), "%s", name ? name : "");
    s_reopens++;
    return 0;
}

/* layout_action.c (R7c): the New Game screen's confirm starts the game */
void la_host_new_game_go(void)
{
    s_newGames++;
}

/* S1: the game's Options screen's settings (common/src/main.c,
   fumi/ios/pad.c), and layout_action.c's film effect (la_game_option's
   stage animations), which records the mode it was given */
int optionScreenMode, optionControlType, girlControlMode;
int iosPadActRequestEnable = 1;
static int s_filmCalls, s_filmLast = -1;

void la_host_film_effect(int mode)
{
    s_filmCalls++;
    s_filmLast = mode;
    optionScreenMode = mode;
}

#ifdef SETTINGS_RENDER
static void bindFakeSheet(int no);
#endif

void tex_TransTexture(int no, int pri)
{
    (void)pri;
#ifdef SETTINGS_RENDER
    bindFakeSheet(no);
#else
    (void)no;
#endif
}

int tex_GetTextureNo(char *name)
{
    (void)name;
    return 0;
}

void *tex_GetTextureData(int no)
{
    (void)no;
    return NULL;
}

void tex_SetSamplingType(void *t, int mag, int min)
{
    (void)t;
    (void)mag;
    (void)min;
}

int soundSeDefPlay(int no, unsigned int a, float *b, int c)
{
    (void)no;
    (void)a;
    (void)b;
    (void)c;
    return -1;
}

void gflagInit(void) {}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void __assert(const char *file, int line, const char *e)
{
    printf("__assert %s:%d %s\n", file, line, e);
    abort();
}

float GetTableSin(short angle)
{
    (void)angle;
    return 0.5f;
}

#ifdef SETTINGS_RENDER

/* what GifPacket.c, DisplayList.c and DmaPacket.c import (as ui_test) */
int screenOffsetX, screenOffsetY;

int fbKeep;

void *ios_partition_common;

void *dmaVif;

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part;
    (void)file;
    (void)line;
    return calloc(1, (size_t)size);
}

void iosFree(void *p)
{
    free(p);
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

void mc_Reset(void) {}

float GetTableCos(short angle)
{
    (void)angle;
    return 0.5f;
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch;
    (void)addr;
}

#else

/* the layout's packets: nothing is recorded in this test */
void gif_StartPacketPri(int pri)
{
    (void)pri;
}

void gif_SetZTest(int on)
{
    (void)on;
}

void gif_SetZWrite(int on)
{
    (void)on;
}

void gif_SetAlpha(long long alpha, long long mode, long long fix)
{
    (void)alpha;
    (void)mode;
    (void)fix;
}

void gif_SpriteSensitive(void *r, long long z, void *uv, void *col, int prim)
{
    (void)r;
    (void)z;
    (void)uv;
    (void)col;
    (void)prim;
}

void gif_EndPacket(void) {}

void gif_SpriteSensitiveOffset(void *r, long long z, void *uv, void *col, int prim)
{
    (void)r;
    (void)z;
    (void)uv;
    (void)col;
    (void)prim;
}

void gif_PointOffset(int *v, long long z, void *col, int prim)
{
    (void)v;
    (void)z;
    (void)col;
    (void)prim;
}

void gif_SetGsReg(long long reg, long long data)
{
    (void)reg;
    (void)data;
}

#endif

/* ------------------------------------------------------- fake tables */

static void setRow(int i, int up, int down, int downItem, int upItem, int left, int right, int y)
{
    LtProperty *r = &texProperty[i];
    memset(r, 0, sizeof(*r));
    r->word0 = -1;
    r->ownerItem = -1;
    r->up = up;
    r->down = down;
    r->left = left;
    r->right = right;
    r->rightItem = r->leftItem = -1;
    r->downItem = downItem;
    r->upItem = upItem;
    r->dispY = y;
    r->dispW = 128;
    r->dispH = 40;
    r->texFileNo = 21;
    r->texV = i; /* distinct rows */
    r->selectable = 1;
}

static void setLayout(int i, int first, int last, int def, int link)
{
    LtProp *l = &texLayout[i];
    memset(l, 0, sizeof(*l));
    l->first = first;
    l->last = last;
    l->fadeInTime = 0.3f;
    l->fadeOutTime = 0.1f;
    l->procFirst = 1;
    l->defaultItem = l->curItem = def;
    l->link = link;
}

/* the PAL rows the entry touches, with their shipped links (tex-property.c) */
static void fakeTables(void)
{
    memset(texLayout, 0, sizeof(texLayout));
    memset(texProperty, 0, sizeof(texProperty));
    for (int i = 0; i < LT_GAME_PROPERTY_COUNT; i++) {
        setRow(i, -1, -1, -1, -1, -1, -1, 0);
    }
    setLayout(54, 292, 292, -1, -1);
    texLayout[54].fadeInTime = 0.0f;
    texLayout[54].fadeOutTime = 0.0f;
    setLayout(57, 292, 297, 295, -1);
    setLayout(58, 297, 333, 308, -1);
    setLayout(59, 333, 392, 336, -1); /* the game's button configuration */
    setRow(336, -1, -1, -1, -1, -1, -1, 52);
    /* the pause menu (S1): Options (opens 58 in the PAL data), Back, End
       Game, their letters 7 texels into rectangles at x 40 */
    setRow(294, -1, -1, 295, -1, -1, 58, 50);
    setRow(295, -1, -1, 296, 294, -1, -1, 70);
    setRow(296, -1, -1, -1, 295, -1, 61, 120);
    for (int i = 294; i <= 296; i++) {
        texProperty[i].dispX = 40;
    }
    setLayout(11, 48, 49, -1, -1);
    setLayout(12, 49, 51, 49, 11);
    setLayout(13, 51, 52, 51, 11);
    setLayout(9, 44, 46, 44, -1); /* the vibration screen (R7c's Triangle) */
    setRow(44, -1, -1, 45, -1, -1, -1, 100);
    setRow(45, -1, -1, -1, 44, -1, -1, 120);
    /* Options: 300 screen mode and 325 girl control are shown only after
       the game is cleared (layout_texture.c lt_property_visible) */
    setRow(300, -1, -1, 308, 325, 57, -1, 45);
    setRow(308, -1, -1, 313, 300, 57, -1, 65);
    setRow(313, -1, -1, 318, 308, 57, -1, 85);
    setRow(318, -1, -1, 323, 313, 57, -1, 105);
    setRow(323, -1, -1, 324, 318, 57, 59, 125);
    setRow(324, -1, -1, 325, 323, 57, 60, 145);
    setRow(325, -1, -1, 300, 324, 57, -1, 165);
    setRow(330, -1, -1, 300, 324, 57, 57, 1);
    /* the labels' x (the Settings row ends where their letters end) */
    texProperty[323].dispX = 108;
    texProperty[324].dispX = 236;
    texProperty[325].dispX = 236;
    /* title: the copyright line (48), Continue (49) and New Game (50, 51),
       centred in 20-field-line boxes; 49..51 masked by default */
    setRow(48, -1, -1, -1, -1, -1, -1, 195);
    setRow(49, -1, -1, 50, -1, -1, -1, 135);
    setRow(50, -1, -1, -1, 49, -1, -1, 165);
    setRow(51, -1, -1, -1, -1, -1, -1, 165);
    for (int i = 48; i <= 51; i++) {
        texProperty[i].dispW = 0;
        texProperty[i].centerX = 1;
        texProperty[i].defaultMask = i != 48;
    }
    /* the button glyphs' rows (layout_ext.h): their PAL rectangles, and a
       texture number per sheet (1 buttons.tm2, 2 menu_PAL_02, 3
       menu_PAL_01) that settings_render binds to a drawn stand-in */
    for (int g = 0; g < LT_GLYPH_COUNT; g++) {
        int uv[4];
        int r = lt_ext_GlyphSource(g, uv);
        texProperty[r].texU = uv[0];
        texProperty[r].texV = uv[1];
        texProperty[r].texW = uv[2];
        texProperty[r].texH = uv[3];
        texProperty[r].texNo = g <= LT_GLYPH_TRIANGLE ? 1 : g <= LT_GLYPH_R2 ? 2 : 3;
    }
}

/* ------------------------------------------------------- frames */

#ifdef SETTINGS_RENDER

/* a mid-tone like the fogged title, under the layout's dark backdrop */
static const uint8_t kBg[4] = {150, 140, 120, 0x80};

/* package DEF: the frames end with the reduction, so a present shows them */
static int s_reduce;

#endif

static void frame(int flags)
{
    frame_count++;
    pad[0].flags = flags;
    pad[0].now = flags;
#ifdef SETTINGS_RENDER
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kBg, 1, 0);
    exec_layout_texture();
    if (s_reduce) {
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
        dl_SetDLPriority(12);
        rd_Post(RD_POST_REDUCTION, &pp);
    }
    dl_Swap();
#else
    exec_layout_texture();
#endif
}

/* runs frames until the layout is current and settled (fade state 2) */
static int settle(int layout, int max)
{
    for (int i = 0; i < max; i++) {
        if (current_layout_id == layout && lt_fade_status() == 2) {
            return 1;
        }
        frame(0);
    }
    return current_layout_id == layout && lt_fade_status() == 2;
}

static void press(int flags)
{
    frame(flags);
    frame(0);
    frame(0); /* exec_layout_texture clears flags for two frames after a move */
}

/* ------------------------------------------------------- tests */

static char s_dir[1024];

static void path(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", s_dir, name);
}

static void writeFile(const char *p, const char *text)
{
    FILE *f = fopen(p, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void useConfig(const char *text)
{
    char p[1100];
    path(p, sizeof(p), "settings_test.toml");
    writeFile(p, text);
    ico_config_reset(p, "");
    ico_sysconf_reset();
    ico_opt_reload();
    ico_video_reload();
}

static int labelsAre(UiSettingsPage page, const int *opts, const int *strs, int n)
{
    int labels[32], got[32];
    int c = ui_SettingsPageRows(page, labels, got, NULL, 32);
    int ok = c == n;
    for (int i = 0; ok && i < n; i++) {
        ok = got[i] == opts[i] &&
             (strs[i] < 0 || strcmp(lt_ext_RowText(labels[i]), ui_Str((UiStrId)strs[i])) == 0);
        if (!ok) {
            printf("  page %d row %d: opt %d (want %d) \"%s\"\n", page, i, got[i], opts[i],
                   lt_ext_RowText(labels[i]));
        }
    }
    if (c != n) {
        printf("  page %d: %d rows, want %d\n", page, c, n);
    }
    return ok;
}

static void testBuild(void)
{
    static const int mainOpts[] = {UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_LINK,
                                   UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_LANGUAGE,
                                   UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_DEVELOPER,
                                   UI_OPT_DUMP_TEXTURES, UI_OPT_DUMP_MODELS, UI_OPT_BACK};
    static const int fxOpts[] = {UI_OPT_EFFECT_GLOW,      UI_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_OPT_EFFECT_SOFTENING, UI_OPT_EFFECT_MOTION_BLUR,
                                 UI_OPT_EFFECT_FOG,       UI_OPT_BACK};
    static const int fxStrs[] = {UI_STR_OPT_EFFECT_GLOW,      UI_STR_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_STR_OPT_EFFECT_SOFTENING, UI_STR_OPT_EFFECT_MOTION_BLUR,
                                 UI_STR_OPT_EFFECT_FOG,       UI_STR_BACK};
    static const int mainStrs[] = {
        UI_STR_SECTION_DISPLAY,      UI_STR_SECTION_EFFECTS,  UI_STR_SECTION_AUDIO,
        UI_STR_SECTION_CONTROLS,     UI_STR_SECTION_GAMEPLAY, UI_STR_SECTION_LANGUAGE,
        UI_STR_SECTION_ACHIEVEMENTS, UI_STR_EXTRAS,           UI_STR_OPT_DEVELOPER_MODE,
        UI_STR_OPT_DUMP_TEXTURES,    UI_STR_OPT_DUMP_MODELS,  UI_STR_BACK};
    static const int dispOpts[] = {
        UI_OPT_PRESET, UI_OPT_RESOLUTION,   UI_OPT_ASPECT,     UI_OPT_FULLSCREEN,  UI_OPT_VSYNC,
        UI_OPT_FILTER, UI_OPT_TEXTURE_PACK, UI_OPT_MODEL_PACK, UI_OPT_FULL_HEIGHT, UI_OPT_FRAMERATE,
        UI_OPT_CRT,    UI_OPT_CRT_STRENGTH, UI_OPT_BRIGHTNESS, UI_OPT_VIDEO_MODE,  UI_OPT_BACK};
    static const int dispStrs[] = {
        UI_STR_OPT_PRESET,       UI_STR_OPT_RESOLUTION, UI_STR_OPT_ASPECT,
        UI_STR_OPT_FULLSCREEN,   UI_STR_OPT_VSYNC,      UI_STR_OPT_FILTERING,
        UI_STR_OPT_TEXTURE_PACK, UI_STR_OPT_MODEL_PACK, UI_STR_OPT_FULL_HEIGHT,
        UI_STR_OPT_FRAMERATE,    UI_STR_OPT_CRT,        UI_STR_OPT_CRT_STRENGTH,
        UI_STR_OPT_BRIGHTNESS,   UI_STR_OPT_VIDEO_MODE, UI_STR_BACK};
    static const int audioOpts[] = {UI_OPT_VOLUME, UI_OPT_MUSIC,  UI_OPT_EFFECTS,
                                    UI_OPT_OUTPUT, UI_OPT_DEVICE, UI_OPT_BACK};
    static const int audioStrs[] = {UI_STR_OPT_VOLUME, UI_STR_OPT_MUSIC_VOL, UI_STR_OPT_EFFECTS_VOL,
                                    UI_STR_OPT_OUTPUT, UI_STR_OPT_DEVICE,    UI_STR_BACK};
    static const int ctlOpts[] = {UI_OPT_LINK,      UI_OPT_BUTTON_CONFIG, UI_OPT_VIBRATION,
                                  UI_OPT_HOLD_TYPE, UI_OPT_MOUSE_SENS,    UI_OPT_CIRCLE_BACK,
                                  UI_OPT_BACK};
    static const int ctlStrs[] = {
        UI_STR_OPT_REMAP,      UI_STR_OPT_BUTTON_CONFIG, UI_STR_OPT_VIBRATION, UI_STR_OPT_HOLD_TYPE,
        UI_STR_OPT_MOUSE_SENS, UI_STR_OPT_CIRCLE_BACK,   UI_STR_BACK};
    static const int gameOpts[] = {UI_OPT_YORDA, UI_OPT_STICK_FIX, UI_OPT_FILM_EFFECT,
                                   UI_OPT_PLAYERS, UI_OPT_BACK};
    static const int gameStrs[] = {UI_STR_OPT_YORDA, UI_STR_OPT_STICK_FIX, UI_STR_OPT_FILM_EFFECT,
                                   UI_STR_OPT_PLAYERS, UI_STR_BACK};
    static const int listOpts[8] = {UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST,
                                    UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST};
    static const int listStrs[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    CHECK(labelsAre(UI_PAGE_MAIN, mainOpts, mainStrs, 11), "main page rows");
    CHECK(labelsAre(UI_PAGE_EFFECTS, fxOpts, fxStrs, 6), "effects rows");
    {
        static const int extrasOpts[] = {UI_OPT_EXTRAS_MUSIC, UI_OPT_EXTRAS_MODELS,
                                         UI_OPT_EXTRAS_CREDITS, UI_OPT_BACK};
        static const int extrasStrs[] = {UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                                         UI_STR_EXTRAS_CREDITS, UI_STR_BACK};
        CHECK(labelsAre(UI_PAGE_EXTRAS, extrasOpts, extrasStrs, 4), "Extras page rows");
    }
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 14),
          "display rows (Frame rate without a framerate key)");
    CHECK(labelsAre(UI_PAGE_AUDIO, audioOpts, audioStrs, 6), "audio rows");
    CHECK(labelsAre(UI_PAGE_CONTROLS, ctlOpts, ctlStrs, 7), "controls rows");
    CHECK(labelsAre(UI_PAGE_GAMEPLAY, gameOpts, gameStrs, 5), "gameplay rows");
    CHECK(labelsAre(UI_PAGE_ACHIEVEMENTS, listOpts, listStrs, 8), "achievement slots");
    CHECK(labelsAre(UI_PAGE_REMAP, listOpts, listStrs, 8), "remap slots");

    /* sections open their pages; the rows loop like the Options rows */
    int main = ui_SettingsRowOf(UI_PAGE_MAIN, UI_OPT_LINK);
    CHECK(lt_ext_Prop(main)->right == ui_SettingsPageLayout(UI_PAGE_DISPLAY), "Display opens");
    int labels[16], n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    CHECK(lt_ext_Prop(labels[n - 1])->downItem == labels[0] &&
              lt_ext_Prop(labels[0])->upItem == labels[n - 1],
          "main rows wrap");
    /* each page is a contiguous range of port rows, its own layout */
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        LtProp *l = lt_ext_Layout(ui_SettingsPageLayout((UiSettingsPage)p));
        CHECK(ui_SettingsPageLayout((UiSettingsPage)p) >= LT_GAME_LAYOUT_COUNT &&
                  l->first >= LT_GAME_PROPERTY_COUNT && l->last > l->first && l->proc != NULL,
              "page %d layout", p);
    }
    /* the gameplay option's explanation */
    int yorda = ui_SettingsRowOf(UI_PAGE_GAMEPLAY, UI_OPT_YORDA);
    CHECK(yorda >= 0, "the Yorda row");
    /* the Gameplay page holds the stick fix beside Yorda's */
    CHECK(ui_SettingsRowOf(UI_PAGE_GAMEPLAY, UI_OPT_STICK_FIX) >= 0, "the stick fix row");
    CHECK(ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX) < 0, "not on Controls");
    /* package TXT2: no Menu text or Font row (one behaviour: the game's
       words keep their texels, the port's text is the game face) */
    {
        int rows[16];
        const int n = ui_SettingsPageRows(UI_PAGE_DISPLAY, rows, NULL, NULL, 16);
        for (int k = 0; k < n; k++) {
            CHECK(strcmp(lt_ext_RowText(rows[k]), "Menu text") != 0 &&
                      strcmp(lt_ext_RowText(rows[k]), "Font") != 0,
                  "Display row %d: \"%s\"", k, lt_ext_RowText(rows[k]));
        }
    }
    /* the New Game screen */
    int ml = ui_NewGameScreenLayout();
    CHECK(ml >= LT_GAME_LAYOUT_COUNT && lt_ext_Layout(ml)->proc != NULL, "New Game screen layout");
    static const char *const kNgLabel[2] = {"Mirror mode", "New Game+"};
    for (int r = 0; r < 2; r++) {
        const int off = ui_NewGameScreenRow(r, 0), on = ui_NewGameScreenRow(r, 1);
        CHECK(off >= 0 && on >= 0 && strcmp(lt_ext_RowText(off - 1), kNgLabel[r]) == 0 &&
                  strcmp(lt_ext_RowText(off), "Off") == 0 && strcmp(lt_ext_RowText(on), "On") == 0,
              "New Game screen row %d: %s Off / On", r, kNgLabel[r]);
        CHECK(lt_ext_Prop(off)->rightItem == on && lt_ext_Prop(on)->leftItem == off,
              "row %d: Off and On side by side", r);
        CHECK(lt_ext_Prop(off)->dispY == lt_ext_Prop(on)->dispY &&
                  lt_ext_Prop(off - 1)->dispY == lt_ext_Prop(off)->dispY,
              "row %d: the label, Off and On on one line", r);
    }
    CHECK(lt_ext_Prop(ui_NewGameScreenRow(1, 0))->dispY >
              lt_ext_Prop(ui_NewGameScreenRow(0, 0))->dispY,
          "New Game+ under Mirror mode");
    CHECK(ui_NewGameScreenRow(2, 0) == -1 && ui_NewGameScreenRow(-1, 1) == -1, "no third row");
    {
        /* every row of the screen inside the layout, the note's bottom
           at 212 at most */
        const LtProp *l = lt_ext_Layout(ml);
        for (int k = l->first; k < l->last; k++) {
            CHECK(lt_ext_Prop(k)->dispY >= 100 &&
                      lt_ext_Prop(k)->dispY + lt_ext_Prop(k)->dispH <= 212,
                  "New Game screen row %d in 100..212 (%d+%d)", k, lt_ext_Prop(k)->dispY,
                  lt_ext_Prop(k)->dispH);
        }
    }

    /* R7d: the Frame rate row in either preset, its value from the file */
    useConfig("[video]\npreset = \"enhanced\"\nframerate = \"144\"\n");
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SettingsInstall();
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 14), "display rows (Enhanced)");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FRAMERATE), "144 fps") == 0, "framerate 144 (%s)",
          ui_SettingsValueText(UI_OPT_FRAMERATE));
}

/* Review finding 1: Settings (its Music page and the photo, quit and
   New Game screens included) and the model viewer's two screens built
   together stay inside the layout extension, with room to spare, and no
   row or layout came back -1.  The viewer's rows are built as
   port/game/model_viewer.c build() builds them (a heading, a list of 8
   slots with a right column, its hint; a list of 8 slots, two hints, three
   rows; two layouts). */
static int budgetCount(void *user)
{
    (void)user;
    return 3;
}

static void budgetFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    (void)d;
    out->label = "model";
}

static void testBudget(void)
{
    enum { MARGIN = 64 };

    static const UiListDef def = {budgetCount, budgetFill, NULL, NULL, NULL};
    static UiList list, anim;
    static UiHint listHint, sticks, keys;
    UiListStyle st;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    const int settingsRows = lt_ext_PropCount(), settingsLayouts = lt_ext_LayoutCount();
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        int rows[64];
        const int n = ui_SettingsPageRows((UiSettingsPage)p, rows, NULL, NULL, 64);
        CHECK(ui_SettingsPageLayout((UiSettingsPage)p) >= LT_GAME_LAYOUT_COUNT,
              "page %d has its layout", p);
        for (int k = 0; k < n && k < 64; k++) {
            CHECK(rows[k] >= LT_GAME_PROPERTY_COUNT, "page %d row %d: index %d", p, k, rows[k]);
        }
    }
    /* the model viewer's screens, after Settings as in the game */
    const int first = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    const int header = ui_SettingsAddRow(20, 12, 600, 40, 0, -1, UI_STR_EXTRAS_MODELS, NULL, 30.0f,
                                         UI_ALIGN_CENTER);
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_ListBuild(&list, &def, NULL, &st);
    ui_HintBuild(&listHint, 196, 19.0f, ui_hint_mv_list, UI_HINT_MV_LIST_COUNT);
    const int l1 = lt_ext_AddLayout(
        &(LtProp){.first = first, .last = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount(), .link = -1});
    const int first2 = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    memset(&st, 0, sizeof(st));
    st.y0 = 30;
    st.pitch = 15;
    st.label = (UiListCol){404, 216, 18.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){620, 4, 18.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_ListBuild(&anim, &def, NULL, &st);
    ui_HintBuild(&sticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_HintBuild(&keys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    int last = -1;
    for (int k = 0; k < 3; k++) {
        last = ui_SettingsAddRow(24, 10 + 14 * k, 360, 30, 0, -1, 0, " ", 19.0f, UI_ALIGN_LEFT);
        CHECK(last >= LT_GAME_PROPERTY_COUNT, "viewer row %d: index %d", k, last);
    }
    const int l2 = lt_ext_AddLayout(&(LtProp){
        .first = first2, .last = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount(), .link = -1});
    CHECK(header >= 0 && l1 >= 0 && l2 >= 0, "the viewer's heading and layouts (%d, %d, %d)",
          header, l1, l2);
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        CHECK(list.label[i] >= 0 && list.colA[i] >= 0 && anim.label[i] >= 0 && anim.colA[i] >= 0,
              "viewer list slot %d", i);
    }
    CHECK(list.status >= 0 && anim.status >= 0, "the viewer lists' status rows");
    const int rows = lt_ext_PropCount(), layouts = lt_ext_LayoutCount();
    printf("settings_test: layout extension: Settings %d rows, %d layouts; with the model viewer "
           "%d of %d rows, %d of %d layouts\n",
           settingsRows, settingsLayouts, rows, LT_EXT_MAX_PROPERTIES, layouts, LT_EXT_MAX_LAYOUTS);
    CHECK(rows + MARGIN <= LT_EXT_MAX_PROPERTIES, "%d rows leave %d of %d spare (at least %d)",
          rows, LT_EXT_MAX_PROPERTIES - rows, LT_EXT_MAX_PROPERTIES, MARGIN);
    CHECK(layouts + 4 <= LT_EXT_MAX_LAYOUTS, "%d layouts leave %d of %d spare (at least 4)",
          layouts, LT_EXT_MAX_LAYOUTS - layouts, LT_EXT_MAX_LAYOUTS);
    /* a bad index reads and writes a scratch row, never memory outside the
       tables */
    LtProperty *bad = lt_ext_Prop(-1);
    CHECK(bad != NULL && lt_ext_PropIndex(bad) == -1, "lt_ext_Prop(-1) is a scratch row");
    bad->downItem = 5;
    CHECK(lt_ext_Prop(LT_GAME_PROPERTY_COUNT + rows)->downItem == 0,
          "past the last row: a fresh scratch row");
    CHECK(lt_ext_PropIndex(lt_ext_Prop(LT_GAME_PROPERTY_COUNT - 1)) == LT_GAME_PROPERTY_COUNT - 1,
          "the game's last row is the game's");
}

/* R7d: the Frame rate row steps original, uncapped, 60, 120, 144, 240 */
static void testFramerate(void)
{
    static const int want[] = {ICO_FRAMERATE_UNCAPPED, 60, 120, 144, 240, ICO_FRAMERATE_ORIGINAL};
    static const char *const text[] = {"Uncapped", "60 fps",  "120 fps",
                                       "144 fps",  "240 fps", "Original"};
    char p[1100];
    IcoVideoOptions o;

    useConfig("[video]\nframerate = \"original\"\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    CHECK(ui_SettingsRowOf(UI_PAGE_DISPLAY, UI_OPT_FRAMERATE) >= 0, "the row (Original preset)");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FRAMERATE), "Original") == 0, "framerate Original");
    for (int i = 0; i < 6; i++) {
        ui_SettingsStep(UI_OPT_FRAMERATE, 1);
        ico_video_get(&o);
        CHECK(o.framerate == want[i] &&
                  strcmp(ui_SettingsValueText(UI_OPT_FRAMERATE), text[i]) == 0,
              "Right %d: %d \"%s\"", i + 1, o.framerate, ui_SettingsValueText(UI_OPT_FRAMERATE));
    }
    ui_SettingsStep(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 240, "Left from original wraps to 240");
    /* the preset is not changed by the row; the rate is in force in the
       Original preset too (F2) */
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && ico_video_framerate() == 240,
          "Original preset: the row's rate in force");

    /* a cap the list does not hold steps to its neighbours */
    useConfig("[video]\nframerate = \"100\"\n");
    ui_SettingsStep(UI_OPT_FRAMERATE, 1);
    ico_video_get(&o);
    CHECK(o.framerate == 120, "100 Right: 120 (%d)", o.framerate);
    useConfig("[video]\nframerate = \"100\"\n");
    ui_SettingsStep(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 60, "100 Left: 60 (%d)", o.framerate);
    useConfig("[video]\nframerate = \"30\"\n");
    ui_SettingsStep(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == ICO_FRAMERATE_UNCAPPED, "30 Left: uncapped (%d)", o.framerate);
    useConfig("[video]\nframerate = \"500\"\n");
    ui_SettingsStep(UI_OPT_FRAMERATE, 1);
    ico_video_get(&o);
    CHECK(o.framerate == ICO_FRAMERATE_ORIGINAL, "500 Right: original (%d)", o.framerate);
    useConfig("[video]\nframerate = \"500\"\n");
    ui_SettingsStep(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 240, "500 Left: 240 (%d)", o.framerate);

    /* persisted as [video] framerate, the string ico_video_parse_framerate reads */
    ui_SettingsStep(UI_OPT_FRAMERATE, -1); /* 240 -> 144 */
    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get(t, "video.framerate") != NULL &&
              strcmp(ico_toml_get(t, "video.framerate"), "144") == 0,
          "[video] framerate = \"144\"");
    ico_toml_free(t);
    ui_SettingsStep(UI_OPT_FRAMERATE, -1); /* 120 */
    ui_SettingsStep(UI_OPT_FRAMERATE, -1); /* 60 */
    ui_SettingsStep(UI_OPT_FRAMERATE, -1); /* uncapped */
    CHECK(ui_SettingsSave() == 0, "save");
    t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get(t, "video.framerate") != NULL &&
              strcmp(ico_toml_get(t, "video.framerate"), "uncapped") == 0,
          "[video] framerate = \"uncapped\"");
    ico_toml_free(t);
}

/* P2: the Preset row reads Original, Enhanced or Custom, and its step is the
   shortcut; the CRT note under Resolution; the Fullscreen row's query */
static int enterMain(int title);
static int openPage(int mainL, int mainRow, UiSettingsPage page);
static int s_fsAnswer, s_fsAsked;

static int fakeFullscreen(void)
{
    s_fsAsked++;
    return s_fsAnswer;
}

static int rowWithPrefix(UiSettingsPage page, const char *prefix)
{
    LtProp *l = lt_ext_Layout(ui_SettingsPageLayout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strncmp(lt_ext_RowText(j), prefix, strlen(prefix)) == 0) {
            return j;
        }
    }
    return -1;
}

static void checkSavedPreset(const char *want)
{
    char p[1100];
    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    const char *v = t ? ico_toml_get(t, "video.preset") : NULL;
    CHECK(v != NULL && strcmp(v, want) == 0, "[video] preset = \"%s\" (%s)", want, v ? v : "none");
    ico_toml_free(t);
}

static void testPreset(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    ico_video_get(&o);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Original") == 0 && o.resScale == 1 &&
              o.resW == 0 && o.aspect == ICO_ASPECT_4_3 && o.filter == ICO_FILTER_ORIGINAL &&
              !o.fullHeight,
          "fresh: Original, 1x, 4:3, original filter, half height");
    ui_SettingsStep(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Enhanced") == 0 && o.resScale == 0 &&
              o.aspect == ICO_ASPECT_AUTO && o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight,
          "Right: Enhanced, window, auto, anisotropic, full height");
    checkSavedPreset("enhanced");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Custom") == 0, "an edited row: Custom");
    checkSavedPreset("enhanced"); /* Custom is stored as the four rows */
    ui_SettingsStep(UI_OPT_PRESET, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Enhanced") == 0, "Custom, Right: Enhanced");
    ui_SettingsStep(UI_OPT_PRESET, -1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Original") == 0, "Enhanced, Left: Original");
    checkSavedPreset("original");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Custom") == 0, "Custom again");
    ui_SettingsStep(UI_OPT_PRESET, -1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PRESET), "Original") == 0, "Custom, Left: Original");

    /* the shortcut writes the four rows while the CRT filter locks Resolution */
    ui_SettingsStep(UI_OPT_CRT, 1);
    ui_SettingsStep(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.crt == 1 &&
              strcmp(ui_SettingsValueText(UI_OPT_RESOLUTION), "1x (CRT)") == 0,
          "Preset under the CRT filter: resolution kept for later, row still 1x (CRT)");
    ui_SettingsStep(UI_OPT_CRT, -1);
    ico_video_get(&o);
    CHECK(o.crt == 0, "crt off again (%d)", o.crt);

    /* the Fullscreen row follows the query when one is installed */
    ico_video_get(&o);
    o.fullscreen = 1;
    ico_video_set(&o);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FULLSCREEN), "On") == 0, "no query: the option");
    s_fsAnswer = 0;
    s_fsAsked = 0;
    ui_SettingsSetFullscreenQuery(fakeFullscreen);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FULLSCREEN), "Off") == 0, "the query says Off");
    s_fsAsked = 0;
    ui_SettingsStep(UI_OPT_FULLSCREEN, 1);
    ico_video_get(&o);
    CHECK(s_fsAsked > 0 && o.fullscreen == 1, "step: from the query's Off to On (%d, asked %d)",
          o.fullscreen, s_fsAsked);
    ui_SettingsSetFullscreenQuery(NULL);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FULLSCREEN), "On") == 0, "uninstalled: the option");

    /* the note: the CRT one under Resolution, only while locked (one line,
       clear of the Back row below) */
    for (int title = 1; title >= 0; title--) {
        int mainL = enterMain(title);
        int dispL = openPage(mainL, 0, UI_PAGE_DISPLAY);
        int rows[16], n = ui_SettingsPageRows(UI_PAGE_DISPLAY, rows, NULL, NULL, 16);
        int preset = n > 0 ? rows[0] : -1, res = n > 1 ? rows[1] : -1;
        int cn = rowWithPrefix(UI_PAGE_DISPLAY, "CRT filter:");
        CHECK(cn >= 0, "title %d: the CRT note exists", title);
        CHECK(cn >= 0 && strchr(lt_ext_RowText(cn), '\n') == NULL, "title %d: the note is one line",
              title);
        lt_ext_Layout(dispL)->curItem = preset;
        frame(0);
        CHECK(cn >= 0 && lt_ext_Prop(cn)->masked == 1, "title %d: the cursor on Preset: no note",
              title);
        lt_ext_Layout(dispL)->curItem = res;
        frame(0);
        CHECK(cn >= 0 && lt_ext_Prop(cn)->masked == 1,
              "title %d: CRT off, the cursor on Resolution: no note", title);
        ico_video_get(&o);
        o.crt = 1;
        o.crtStrength = 1.0f;
        ico_video_set(&o);
        frame(0);
        CHECK(cn >= 0 && lt_ext_Prop(cn)->masked == 0, "title %d: CRT on, on Resolution: the note",
              title);
        lt_ext_Layout(dispL)->curItem = preset;
        frame(0);
        CHECK(cn >= 0 && lt_ext_Prop(cn)->masked == 1, "title %d: CRT on, cursor elsewhere", title);
        o.crt = 0;
        ico_video_set(&o);
    }
}

static void testRepoint(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    stage_no = 0; /* no stage: no Photo mode row */
    ui_SettingsInstall();
    int s57 = ui_SettingsEntryRow(57), s12 = ui_SettingsEntryRow(12), s13 = ui_SettingsEntryRow(13);
    int ph = ui_SettingsPhotoRow(), mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    CHECK(s57 == 294 && ph >= LT_GAME_PROPERTY_COUNT && s12 > ph && s13 > s12, "entry rows");
    CHECK(ui_SettingsEntryRow(58) == -1 && ui_SettingsEntryLayout(58) == -1,
          "nothing on the Options screen");
    /* S1: the pause menu's Options opens the menu, not the Options screen */
    CHECK(texProperty[294].right == mainL, "294: Cross opens the menu (%d)",
          texProperty[294].right);
    CHECK(texLayout[57].link == ui_SettingsEntryLayout(57) &&
              lt_ext_Layout(ui_SettingsEntryLayout(57))->link == -1 &&
              lt_ext_Layout(ui_SettingsEntryLayout(57))->first == ph,
          "57 -> the Photo mode row's layout");
    CHECK(texProperty[294].downItem == 295 && texProperty[295].upItem == 294 &&
              texProperty[295].dispY == 70 && lt_ext_Prop(ph)->defaultMask,
          "no stage: the row masked, Options <-> Back as in the PAL data");
    CHECK(texLayout[58].link == -1, "the Options screen's chain untouched");
    CHECK(texProperty[50].downItem == s12 && lt_ext_Prop(s12)->upItem == 50, "title 12");
    CHECK(texProperty[51].downItem == s13 && lt_ext_Prop(s13)->upItem == 51, "title 13");
    CHECK(texLayout[12].link == ui_SettingsEntryLayout(12) &&
              lt_ext_Layout(ui_SettingsEntryLayout(12))->link == 11 &&
              texLayout[13].link == ui_SettingsEntryLayout(13) &&
              lt_ext_Layout(ui_SettingsEntryLayout(13))->link == 11,
          "12 and 13 -> entry -> 11");
    CHECK(ui_SettingsEntryItem(s12) && !ui_SettingsEntryItem(294) && !ui_SettingsEntryItem(ph) &&
              !ui_SettingsEntryItem(50),
          "entry items: the title's port rows");
    int count = lt_ext_PropCount();
    ui_SettingsInstall();
    CHECK(lt_ext_PropCount() == count && texLayout[57].link == ui_SettingsEntryLayout(57) &&
              lt_ext_Layout(ui_SettingsEntryLayout(57))->link == -1 &&
              lt_ext_Layout(ui_SettingsEntryLayout(12))->link == 11,
          "a second install changes nothing");
    /* tables that are not the PAL ones are left alone */
    texLayout[57].first = 0;
    texProperty[294].right = 58;
    ui_SettingsInstall();
    CHECK(texProperty[294].right == 58, "unexpected tables: no repoint");
}

/* The copyright line's capitals (row 48, a texture): 25 output pixels at
   960 x 720 on a window run's title, in y units (720 / 448 pixels each) */
#define COPYRIGHT_CAPS 15.5f

/* The entry rows on the game's grid: the title laid out by the port, Continue (49), New
   Game (50, and 51 at the same y), Settings and "Quit to desktop" one pitch
   apart, the same space between their capitals, and between Quit's and
   the copyright line's (48; its capitals 15.5 y units, measured; the
   others' through ui_FontMetrics) to within a field line, the copyright
   line at most 5 field lines below the PAL one (the room measured below
   it); the port rows in New Game's box height, at the game rows'
   size, centred, masked by default as 49..51; a reinstall over reloaded
   PAL rows places them again; in Options the row continues the labels'
   pitch (323 -> 324 -> 325) and ends where their letters end (the menu
   text table's right anchor). */
static void testPlacement(void)
{
    useConfig("version = 1\n");
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            fakeTables();
            lt_ext_Reset();
            ui_SettingsReset();
        } else {
            /* the tables reloaded from the disc: the PAL rows again */
            texProperty[49].dispY = 135;
            texProperty[50].dispY = texProperty[51].dispY = 165;
            texProperty[48].dispY = 195;
        }
        ui_SettingsInstall();
        if (pass == 1) {
            CHECK(texProperty[50].dispY != 165, "a reinstall places the title again");
            ui_SettingsInstall();
        }
    }
    const int copyright = texProperty[48].dispY;
    CHECK(copyright > 195 - 20 && copyright <= 195 + 5,
          "the copyright line at %d: inside the picture (at most 5 below 195)", copyright);
    CHECK(texProperty[51].dispY == texProperty[50].dispY,
          "New Game at the same y on both title layouts");
    for (int g = 12; g <= 13; g++) {
        const LtProperty *ng = &texProperty[g == 12 ? 50 : 51];
        const int si = ui_SettingsEntryRow(g), qi = ui_SettingsQuitRow(g);
        const LtProperty *s = lt_ext_Prop(si);
        const LtProperty *q = lt_ext_Prop(qi);
        const LtProperty *rows[5] = {&texProperty[49], ng, s, q, &texProperty[48]};
        /* capitals' middles (2 y units a field line; the game's and the
           port's rows share the anchor, layout_ext.c) and heights */
        float a, d, capGame, capS, capQ;
        ui_FontMetrics(UI_MENU_TEXT_SIZE, &a, &d, &capGame);
        ui_FontMetrics(lt_ext_RowSize(si), &a, &d, &capS);
        ui_FontMetrics(lt_ext_RowSize(qi), &a, &d, &capQ);
        const float cap[5] = {capGame, capGame, capS, capQ, COPYRIGHT_CAPS};
        const int pitch = ng->dispY - rows[0]->dispY;
        float gaps[4];
        int even = pitch > 0;
        for (int i = 0; i < 4; i++) {
            even = even && (i == 3 || rows[i + 1]->dispY - rows[i]->dispY == pitch);
            gaps[i] = (2.0f * (float)rows[i + 1]->dispY - cap[i + 1] * 0.5f) -
                      (2.0f * (float)rows[i]->dispY + cap[i] * 0.5f);
        }
        CHECK(even && rows[4]->dispY > q->dispY,
              "title %d: Continue %d, New Game %d, Settings %d, Quit %d one pitch apart, the "
              "copyright %d below",
              g, rows[0]->dispY, ng->dispY, s->dispY, q->dispY, rows[4]->dispY);
        CHECK(gaps[0] > 0.0f && gaps[0] == gaps[1] && gaps[1] == gaps[2] &&
                  gaps[3] - gaps[2] < 2.0f && gaps[2] - gaps[3] < 2.0f,
              "title %d: the space between the capitals %.2f, %.2f, %.2f, to the copyright %.2f "
              "y units",
              g, gaps[0], gaps[1], gaps[2], gaps[3]);
        CHECK(lt_ext_RowSize(si) == UI_MENU_TEXT_SIZE && lt_ext_RowSize(qi) == UI_MENU_TEXT_SIZE,
              "title %d: the port rows at the game rows' size", g);
        CHECK(s->dispH == ng->dispH && q->dispH == ng->dispH && s->centerX == ng->centerX &&
                  q->centerX == ng->centerX,
              "title %d: New Game's box height and centring", g);
        CHECK(ng->defaultMask && s->defaultMask && q->defaultMask,
              "title %d: masked by default as New Game", g);
    }
    /* S1: the pause menu's Photo mode row one pitch under Options while a
       stage runs, Back one pitch lower, End Game where it was; its letters
       start where Options' do (display_texture's box: x from dispX + 1/4;
       the game row's letters at dispX - 1/4 + the item's left anchor) */
    stage_no = 11;
    ui_SettingsInstall();
    const LtProperty *r294 = &texProperty[294];
    const LtProperty *ph = lt_ext_Prop(ui_SettingsPhotoRow());
    CHECK(ph->dispY == r294->dispY + 20 && texProperty[295].dispY == r294->dispY + 40 &&
              texProperty[296].dispY == 120 && ph->dispH == r294->dispH && !ph->defaultMask,
          "pause: Options %d, Photo mode %d, Back %d, End Game %d", r294->dispY, ph->dispY,
          texProperty[295].dispY, texProperty[296].dispY);
    CHECK(lt_ext_RowSize(ui_SettingsPhotoRow()) == UI_MENU_TEXT_SIZE,
          "pause: the row at the game rows' size");
    const UiMenuTextItem *it = NULL;
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        if (ui_menu_text_rows[i].row == 294) {
            it = &ui_menu_text_items[ui_menu_text_rows[i].item];
        }
    }
    CHECK(it && it->align == UI_ALIGN_LEFT, "Options (294) in the menu text table");
    if (it) {
        const float portStart = (float)ph->dispX + 0.25f;
        const float gameStart = (float)r294->dispX + it->x - 0.25f;
        CHECK(portStart - gameStart <= 0.5f && gameStart - portStart <= 0.5f,
              "pause: the row starts at x %.2f, the rows' letters at %.2f", portStart, gameStart);
    }
    stage_no = 0;
    ui_SettingsInstall();
    CHECK(texProperty[295].dispY == r294->dispY + 20 && lt_ext_Prop(ui_SettingsPhotoRow())->masked,
          "no stage: Back in its own place (%d)", texProperty[295].dispY);
}

/* Settings from the pause menu: 57 with the cursor on Options (294), Cross */
static void pauseToMain(void)
{
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    texLayout[57].curItem = 294;
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_MAIN), 60), "Options: the menu (%d)",
          current_layout_id);
}

static void testNavigation(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    stage_no = 0;
    init_layout_texture(2); /* installs; layout 54 */
    CHECK(ui_SettingsEntryLayout(57) >= 0, "installed by init_layout_texture");
    settle(54, 4);
    /* S1: the pause menu opens on Back (295); Up is Options, whose Cross
       opens the Settings menu */
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    CHECK(texLayout[57].curItem == 295, "on Back (%d)", texLayout[57].curItem);
    press(0x1000);
    CHECK(texLayout[57].curItem == 294, "up: Options (%d)", texLayout[57].curItem);
    press(0x40); /* Cross */
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "Cross opens the Settings menu (%d)", current_layout_id);
    /* down to Language (row 6), right: French */
    for (int i = 0; i < 5; i++) {
        press(0x4000);
    }
    CHECK(lt_ext_Layout(mainL)->curItem == ui_SettingsRowOf(UI_PAGE_MAIN, UI_OPT_LANGUAGE),
          "on Language");
    int cur0 = s_sounds[0];
    press(0x2000);
    CHECK(NonLinearCameraMove == 3 && ui_GetLanguage() == UI_LANG_FR, "right: French (%d)",
          NonLinearCameraMove);
    CHECK(s_sounds[0] > cur0, "the cursor sound");
    int labels[16], values[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, values, 16);
    CHECK(strstr(lt_ext_RowText(values[5]), "Français") != NULL, "the value row: %s",
          lt_ext_RowText(values[5]));
    CHECK(strcmp(lt_ext_RowText(labels[0]), "Affichage") == 0, "labels follow the language");
    CHECK(strcmp(ico_config_get_string("game.language", ""), "fr") == 0, "[game] language");
    press(0x8000); /* left: English again */
    CHECK(NonLinearCameraMove == 2, "left: English");
    /* Triangle: back to the pause menu, the cursor on Options; its own
       default (Back) again for the next pause */
    press(0x10);
    CHECK(settle(57, 60), "Triangle: the pause menu again (%d)", current_layout_id);
    CHECK(texLayout[57].curItem == 294, "the cursor on Options (%d)", texLayout[57].curItem);
    frame(0);
    CHECK(texLayout[57].defaultItem == 295, "the pause menu's own default again (%d)",
          texLayout[57].defaultItem);

    /* into Controls -> Remap, capture a key on the first row (Cross) */
    press(0x40);
    CHECK(settle(mainL, 60), "the menu again");
    CHECK(lt_ext_Layout(mainL)->curItem == labels[0], "opens on its first row");
    press(0x4000);
    press(0x4000);
    press(0x4000);
    press(0x40); /* Controls */
    int ctlL = ui_SettingsPageLayout(UI_PAGE_CONTROLS);
    CHECK(settle(ctlL, 60), "Controls");
    press(0x40); /* Remap controls */
    int remapL = ui_SettingsPageLayout(UI_PAGE_REMAP);
    CHECK(settle(remapL, 60), "the remap screen");
    ui_SettingsPageRows(UI_PAGE_REMAP, labels, NULL, values, 16);
    CHECK(strcmp(lt_ext_RowText(labels[0]), "Cross") == 0 &&
              strstr(lt_ext_RowText(values[0]), "Space") != NULL,
          "row 1: Cross, Space (%s, %s)", lt_ext_RowText(labels[0]), lt_ext_RowText(values[0]));
    press(0x40); /* capture */
    frame(0);
    ico_input_note_press(ICO_SRC_KEY, ICO_KEY_K);
    frame(0);
    IcoBindings *b = ico_input_live_bindings();
    CHECK(b->kb[ICO_T_CROSS][0] == ICO_KEY_K && b->kb[ICO_T_CROSS][1] == 0, "Cross is K");
    CHECK(b->kb[ICO_T_RSTICK_DOWN][0] == 0, "K is off the right stick");
    frame(0);
    frame(0);
    frame(0);
    CHECK(strstr(lt_ext_RowText(values[0]), "K") != NULL, "the row shows K: %s",
          lt_ext_RowText(values[0]));
    /* scrolling: down past the eighth row */
    for (int i = 0; i < 9; i++) {
        press(0x4000);
    }
    CHECK(strcmp(lt_ext_RowText(labels[0]), "Circle") == 0 ||
              strcmp(lt_ext_RowText(labels[0]), "Square") == 0,
          "the list scrolled (%s)", lt_ext_RowText(labels[0]));
    press(0x10); /* back to Controls: the bindings are written */
    CHECK(settle(ctlL, 60), "Triangle: Controls");
    CHECK(strcmp(ico_config_get_string("input.kb.cross", ""), "K") == 0, "input.kb.cross = K");
    CHECK(strcmp(ico_config_get_string("input.kb.rstick_down", ""), "none") == 0,
          "input.kb.rstick_down = none");
    CHECK(ico_config_get_string("input.kb.circle", NULL) == NULL, "unchanged rows not written");
    ico_input_reload_bindings(b);
    CHECK(b->kb[ICO_T_CROSS][0] == ICO_KEY_K, "reloaded from the config");
}

/* Package PHOTO (S1: in the pause menu): the "Photo mode" row exists only
 * while a stage runs (masked and stepped over on stage 0 or 1, the
 * title's); with one, it sits under Options, Cross opens the photo layout
 * (no dimming, the row masked), whose proc turns the left stick into an
 * orbit, Cross into a capture, Square into the HUD's toggle, and Triangle
 * back to the pause menu with the cursor on the row. */
static void testPhoto(void)
{
    useConfig("version = 1\n[photo]\nstick_speed = 2.0\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ico_photo_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    gFlagGameClear = 0;
    stage_no = 1; /* the title's stage */
    init_layout_texture(2);
    const int ph = ui_SettingsPhotoRow(), pl = ui_PhotoLayout();
    CHECK(ph >= LT_GAME_PROPERTY_COUNT && pl >= LT_GAME_LAYOUT_COUNT &&
              lt_ext_Prop(ph)->right == pl,
          "the row (%d) opens the photo layout (%d)", ph, pl);
    CHECK(strcmp(lt_ext_RowText(ph), ui_Str(UI_STR_PHOTO_MODE)) == 0, "labelled \"%s\"",
          lt_ext_RowText(ph));
    CHECK(lt_ext_Prop(ph)->left == -1, "Triangle on the row: the pause menu's own (it resumes)");
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    CHECK(lt_ext_Prop(ph)->masked && texProperty[294].downItem == 295 &&
              texProperty[295].upItem == 294,
          "no stage: masked and stepped over");
    texLayout[57].curItem = 294;
    press(0x4000);
    CHECK(texLayout[57].curItem == 295, "no stage: down from Options is Back (%d)",
          texLayout[57].curItem);
    stage_no = 11; /* st04a: a stage runs */
    frame(0);
    CHECK(!lt_ext_Prop(ph)->masked && texProperty[294].downItem == ph &&
              lt_ext_Prop(ph)->upItem == 294 && lt_ext_Prop(ph)->downItem == 295 &&
              texProperty[295].upItem == ph,
          "a stage: shown under Options");
    CHECK(lt_ext_Prop(ph)->dispY == texProperty[294].dispY + 20 &&
              texProperty[295].dispY == texProperty[294].dispY + 40,
          "one pitch below Options, Back one lower (%d, %d)", lt_ext_Prop(ph)->dispY,
          texProperty[295].dispY);
    texLayout[57].curItem = 294;
    press(0x4000);
    CHECK(texLayout[57].curItem == ph, "down from Options: the row (%d)", texLayout[57].curItem);
    press(0x4000);
    CHECK(texLayout[57].curItem == 295, "down from the row: Back (%d)", texLayout[57].curItem);
    press(0x1000);
    CHECK(texLayout[57].curItem == ph, "up from Back: the row (%d)", texLayout[57].curItem);
    press(0x40);
    CHECK(settle(pl, 60) && ico_photo_active(), "Cross: photo mode (%d)", current_layout_id);
    CHECK(lt_ext_Layout(pl)->colA == 0.0f && lt_ext_Prop(lt_ext_Layout(pl)->first)->masked,
          "no dimming, nothing drawn");
    pad[0].ana[2] = 255; /* the left stick right */
    for (int i = 0; i < 25; i++) {
        frame(0);
    }
    pad[0].ana[2] = 128;
    IcoPhotoState st;
    ico_photo_get(&st);
    /* a second at full deflection past the dead zone: 90 x 2 degrees */
    CHECK(st.yaw > 3.0f && st.yaw < 3.3f && st.pitch == 0.0f, "orbit: yaw %.3f, pitch %.3f",
          (double)st.yaw, (double)st.pitch);
    press(0x40);
    CHECK(ico_photo_take_capture() == 1 && ico_photo_take_capture() == 0 && current_layout_id == pl,
          "Cross: one capture, the mode stays");
    CHECK(ico_photo_hud(), "the HUD shown");
    press(0x80);
    CHECK(!ico_photo_hud() && ico_photo_active(), "Square: the HUD hidden");
    press(0x10);
    CHECK(settle(57, 60) && !ico_photo_active(), "Triangle: the pause menu again (%d)",
          current_layout_id);
    CHECK(texLayout[57].curItem == ph, "the cursor on the row (%d)", texLayout[57].curItem);
    frame(0);
    CHECK(texLayout[57].defaultItem == 295, "the pause menu's own default again (%d)",
          texLayout[57].defaultItem);
    stage_no = 0;
}

/* P6: the journey's lines on the pause menu's right.  The game state comes
 * through the view's sampler (ico_gamestate.h) as the program's comes from
 * the game: twelve minutes 34 s of play, three game overs, a capture, five
 * enemies defeated and a save in this run.  Shown while a stage runs, the
 * labels from x 300 and every line between the black bars (field lines 38
 * to 198), the values the port's figures; hidden on the title's stage and
 * in photo mode; the assists' line only with one on; the area only where
 * the save screen names one; the game's rows where they were. */
static IcoGsSnapshot s_gs;

static void gsSampler(IcoGsSnapshot *out)
{
    *out = s_gs;
}

/* the pause entry layout's label row with this text (shown or not), -1 */
static int statLabel(const char *text)
{
    const LtProp *l = lt_ext_Layout(ui_SettingsEntryLayout(57));
    for (int r = l->first; r < l->last; r++) {
        const char *t = lt_ext_RowText(r);
        if (r != ui_SettingsPhotoRow() && t && strcmp(t, text) == 0) {
            return r;
        }
    }
    return -1;
}

/* the value beside a shown label (its row follows the label's), NULL when
   the line is hidden */
static const char *statValue(int strId)
{
    const int r = statLabel(ui_Str((UiStrId)strId));
    if (r < 0 || lt_ext_Prop(r)->masked || lt_ext_Prop(r + 1)->masked) {
        return NULL;
    }
    return lt_ext_RowText(r + 1);
}

/* the shown stats rows (labels and values), and whether every one sits in
   the panel's place */
static int statsShown(int *placed)
{
    const LtProp *l = lt_ext_Layout(ui_SettingsEntryLayout(57));
    int n = 0;
    *placed = 1;
    for (int r = ui_SettingsPhotoRow() + 1; r < l->last; r++) {
        const LtProperty *e = lt_ext_Prop(r);
        if (e->masked) {
            continue;
        }
        n++;
        /* y units are half field lines */
        if (e->dispX < 290 || e->dispX + e->dispW > 620 || e->dispY < 38 ||
            e->dispY + e->dispH / 2 > 198) {
            printf("  stats row %d at x %d w %d, y %d h %d\n", r, e->dispX, e->dispW, e->dispY,
                   e->dispH);
            *placed = 0;
        }
    }
    return n;
}

static void testPauseStats(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ico_photo_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    gFlagGameClear = 0;
    NonLinearCameraMove = 2;
    ico_gs_reset();
    ico_gs_set_sampler(gsSampler);
    memset(&s_gs, 0, sizeof(s_gs));
    s_gs.valid = 1;
    s_gs.stage_no = 11;
    s_gs.system_status[0] = 1; /* PAL: 50 frames a second of play time */
    s_gs.system_status[1] = 2;
    s_gs.layout = 54;
    s_gs.mc_preview[2] = (12 * 60 + 34) * 50 + 49;
    ico_gs_tick();
    for (int i = 0; i < 3; i++) {
        ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    }
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 0);
    for (int i = 0; i < 5; i++) {
        ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, i);
    }
    ico_gs_tick();
    s_gs.layout = 41; /* "File saved." */
    ico_gs_tick();
    s_gs.layout = 57;
    ico_gs_tick();
    CHECK(ico_gs_run_game_overs() == 3 && ico_gs_run_captures() == 1 && ico_gs_run_enemies() == 5 &&
              ico_gs_run_saves() == 1,
          "the run: %u game overs, %u captures, %u enemies, %u saves", ico_gs_run_game_overs(),
          ico_gs_run_captures(), ico_gs_run_enemies(), ico_gs_run_saves());

    stage_no = 1; /* the title's stage */
    init_layout_texture(2);
    const int ph = ui_SettingsPhotoRow();
    const LtProp *el = lt_ext_Layout(ui_SettingsEntryLayout(57));
    CHECK(el->first == ph && el->last > ph + 2 * 13, "the lines after Photo mode (%d .. %d)",
          el->first, el->last);
    for (int r = ph + 1; r < el->last; r++) {
        CHECK(lt_ext_Prop(r)->defaultMask && !lt_ext_Prop(r)->selectable,
              "row %d hidden by default, not selectable", r);
    }
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    int placed;
    CHECK(statsShown(&placed) == 0, "the title's stage: no line shown");
    CHECK(statValue(UI_STR_STATS_DEATHS) == NULL, "the title's stage: Deaths hidden");

    stage_no = 11; /* st04a: a stage runs (Main Gate on the save screen) */
    frame(0);
    const int shown = statsShown(&placed);
    CHECK(shown > 0 && placed, "a stage: %d rows shown, all in the panel's place", shown);
    const char *v;
    v = statValue(UI_STR_STATS_PLAY_TIME);
    CHECK(v && strcmp(v, "00:12:34") == 0, "Play time \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_DEATHS);
    CHECK(v && strcmp(v, "3") == 0, "Deaths \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_CAPTURES);
    CHECK(v && strcmp(v, "1") == 0, "Yorda captured \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_SAVES);
    CHECK(v && strcmp(v, "1") == 0, "Saves \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_ENEMIES);
    CHECK(v && strcmp(v, "5") == 0, "Enemies \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_NEWGAME_PLUS);
    CHECK(v && strcmp(v, "Off") == 0, "New Game+ \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_MIRROR);
    CHECK(v && strcmp(v, "Off") == 0, "Mirror mode \"%s\"", v ? v : "(hidden)");
    char want[32];
    snprintf(want, sizeof(want), "0 / %d", ico_ach_count());
    v = statValue(UI_STR_SECTION_ACHIEVEMENTS);
    CHECK(v && strcmp(v, want) == 0, "Achievements \"%s\" (want \"%s\")", v ? v : "(hidden)", want);
    v = statValue(UI_STR_STATS_AREA);
    CHECK(v && strcmp(v, ui_Str(UI_STR_MT_LOC_MAIN_GATE)) == 0, "Area \"%s\"", v ? v : "(hidden)");
    CHECK(statValue(UI_STR_STATS_ASSISTS) == NULL, "no assist on: no Assists line");
    /* the lines packed from the top, a pitch apart */
    const int pt = statLabel(ui_Str(UI_STR_STATS_PLAY_TIME)),
              de = statLabel(ui_Str(UI_STR_STATS_DEATHS)),
              ar = statLabel(ui_Str(UI_STR_STATS_AREA)),
              ac = statLabel(ui_Str(UI_STR_SECTION_ACHIEVEMENTS));
    CHECK(lt_ext_Prop(pt)->dispX == 300 &&
              lt_ext_Prop(pt + 1)->dispX + lt_ext_Prop(pt + 1)->dispW <= 610,
          "from x 300 to 610");
    CHECK(lt_ext_Prop(de)->dispY > lt_ext_Prop(pt)->dispY &&
              lt_ext_Prop(ar)->dispY ==
                  lt_ext_Prop(ac)->dispY + (lt_ext_Prop(de)->dispY - lt_ext_Prop(pt)->dispY),
          "Area right under Achievements when no assist is on");
    /* the Photo mode row's box ends before the lines; the game's rows
       where they were */
    CHECK(lt_ext_Prop(ph)->dispX + lt_ext_Prop(ph)->dispW < 300, "Photo mode's box ends at x %d",
          lt_ext_Prop(ph)->dispX + lt_ext_Prop(ph)->dispW);
    CHECK(texProperty[294].dispX == 40 && texProperty[294].dispY == 50 &&
              texProperty[295].dispY == 90 && texProperty[296].dispY == 120,
          "the pause rows at 50, 90, 120 (%d, %d, %d)", texProperty[294].dispY,
          texProperty[295].dispY, texProperty[296].dispY);

    /* the game moves on: the values follow each frame */
    s_gs.mc_preview[2] = 100 * 3600 * 50;
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ico_gs_tick();
    gFlagGameClear = 1;
    ico_opt_set_mirror(1);
    frame(0);
    v = statValue(UI_STR_STATS_PLAY_TIME);
    CHECK(v && strcmp(v, "99:59:59") == 0, "Play time clamped as the save screen's: \"%s\"",
          v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_DEATHS);
    CHECK(v && strcmp(v, "4") == 0, "Deaths \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_NEWGAME_PLUS);
    CHECK(v && strcmp(v, "On") == 0, "New Game+ \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_MIRROR);
    CHECK(v && strcmp(v, "On") == 0, "Mirror mode \"%s\"", v ? v : "(hidden)");

    /* the assists: the line and one line for each on, nothing for those off */
    ico_opt_set_yorda_safe(1);
    ico_opt_set_developer_mode(1);
    frame(0);
    const int as = statLabel(ui_Str(UI_STR_STATS_ASSISTS));
    CHECK(as >= 0 && !lt_ext_Prop(as)->masked, "an assist on: the Assists line");
    if (as >= 0) {
        CHECK(!lt_ext_Prop(as + 3)->masked &&
                  strcmp(lt_ext_RowText(as + 3), ui_Str(UI_STR_OPT_YORDA)) == 0 &&
                  !lt_ext_Prop(as + 5)->masked &&
                  strcmp(lt_ext_RowText(as + 5), ui_Str(UI_STR_OPT_DEVELOPER_MODE)) == 0 &&
                  lt_ext_Prop(as + 7)->masked,
              "the two on, one a line (\"%s\", \"%s\")", lt_ext_RowText(as + 3),
              lt_ext_RowText(as + 5));
    }
    statsShown(&placed);
    CHECK(placed, "with the assists, every line between the bars");
    ico_opt_set_stick_fix(1);
    frame(0);
    CHECK(statsShown(&placed) == 2 * 13 + 1 && placed,
          "every line (the three assists and the area): still between the bars");
    /* each language: a label and its value never overlap once the label is
       set to fit its box (layout_ext.c: down to 60 %) */
    for (int g = 2; g <= 6; g++) {
        NonLinearCameraMove = g;
        frame(0);
        for (int r = ph + 2; r + 1 < el->last; r += 2) {
            const LtProperty *lab = lt_ext_Prop(r), *val = lt_ext_Prop(r + 1);
            if (lab->masked) {
                continue;
            }
            const float lw = ui_MeasureText(lt_ext_RowSize(r), lt_ext_RowText(r));
            const float vw = ui_MeasureText(lt_ext_RowSize(r + 1), lt_ext_RowText(r + 1));
            /* each as drawn: its natural width, or its box's when set to fit */
            const int blank = strcmp(lt_ext_RowText(r), " ") == 0;
            const float lDrawn = blank ? 0.0f : lw < (float)lab->dispW ? lw : (float)lab->dispW;
            const float vDrawn = vw < (float)val->dispW ? vw : (float)val->dispW;
            CHECK((blank || lw * 0.6f <= (float)lab->dispW) && vw * 0.6f <= (float)val->dispW &&
                      lDrawn + vDrawn <= (float)val->dispW,
                  "language %d: \"%s\" (%.0f in %d) and \"%s\" (%.0f in %d)", g, lt_ext_RowText(r),
                  (double)lw, lab->dispW, lt_ext_RowText(r + 1), (double)vw, val->dispW);
        }
    }
    NonLinearCameraMove = 2;
    ico_opt_set_yorda_safe(0);
    ico_opt_set_developer_mode(0);
    ico_opt_set_stick_fix(0);
    ico_opt_set_mirror(0);
    gFlagGameClear = 0;
    frame(0);
    CHECK(statValue(UI_STR_STATS_ASSISTS) == NULL && lt_ext_Prop(as + 3)->masked,
          "the assists off again: hidden");

    /* a stage the save screen has no name for: no Area line */
    stage_no = 39;
    frame(0);
    CHECK(statValue(UI_STR_STATS_AREA) == NULL && statValue(UI_STR_STATS_DEATHS) != NULL,
          "no name for the beach: the Area line left out");
    stage_no = 11;
    /* a run from a save made before v0.4.0: Saves and Enemies hidden (their
       counts would start at the load), the other lines stay */
    {
        IcoGsRun r;
        ico_gs_run_get(&r);
        r.partial = 1;
        ico_gs_run_set(&r);
        frame(0);
        CHECK(statValue(UI_STR_STATS_SAVES) == NULL && statValue(UI_STR_STATS_ENEMIES) == NULL &&
                  statValue(UI_STR_STATS_DEATHS) != NULL &&
                  statValue(UI_STR_STATS_CAPTURES) != NULL,
              "a partial run: Saves and Enemies hidden, Deaths and Yorda captured shown");
        r.partial = 0;
        ico_gs_run_set(&r);
        frame(0);
        CHECK(statValue(UI_STR_STATS_SAVES) != NULL && statValue(UI_STR_STATS_ENEMIES) != NULL,
              "a whole run: both shown again");
    }
    /* photo mode: hidden */
    ico_photo_enter();
    frame(0);
    CHECK(statsShown(&placed) == 0, "photo mode: hidden");
    ico_photo_exit();
    frame(0);
    CHECK(statsShown(&placed) > 0, "back from photo mode: shown");
    /* back on the title's stage: hidden again */
    stage_no = 1;
    frame(0);
    CHECK(statsShown(&placed) == 0, "the title's stage again: hidden");
    stage_no = 0;
    ico_gs_set_sampler(NULL);
    ico_gs_reset();
}

/* R7c: the New Game screen, run by the real layout code: two rows, Mirror
 * mode and New Game+.  The cursor starts on Mirror mode's Off, Right moves
 * to On, Down to New Game+'s choice and Up back to Mirror mode's; each row
 * keeps its own choice, the other row's choice stays lit, and the note
 * follows the cursor's row.  New Game+ starts on when the game was
 * finished (gFlagGameClear set: a finished save's new game), off
 * otherwise; Cross or START writes both choices (the run's mirror mode,
 * gFlagGameClear) and starts the game once; Triangle goes back to the
 * vibration screen (layout 9). */
static void testNewGameScreen(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    init_layout_texture(2);
    settle(54, 4);
    int ml = ui_NewGameScreenEnter();
    const int mOff = ui_NewGameScreenRow(0, 0), mOn = ui_NewGameScreenRow(0, 1);
    const int nOff = ui_NewGameScreenRow(1, 0), nOn = ui_NewGameScreenRow(1, 1);
    const int note = lt_ext_Layout(ml)->last - 1;
    CHECK(ml >= 0, "the screen is there once installed");
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen (%d)", current_layout_id);
    LtProp *l = lt_ext_Layout(ml);
    CHECK(l->curItem == mOff, "the cursor on Mirror mode's Off");
    CHECK(lt_ext_Prop(nOff)->ownerItem == mOff && lt_ext_Prop(nOn)->ownerItem == -1,
          "New Game+ Off lit (a plain New Game)");
    CHECK(strncmp(lt_ext_RowText(note), "Plays the game flipped", 22) == 0,
          "the note explains Mirror mode (%s)", lt_ext_RowText(note));
    press(0x2000); /* right */
    CHECK(l->curItem == mOn, "right: Mirror mode On");
    CHECK(lt_ext_Prop(nOff)->ownerItem == mOn, "New Game+ Off still lit");
    press(0x4000); /* down */
    CHECK(l->curItem == nOff, "down: New Game+'s choice, Off");
    CHECK(lt_ext_Prop(mOn)->ownerItem == nOff && lt_ext_Prop(mOff)->ownerItem == -1,
          "Mirror mode On stays lit");
    CHECK(lt_ext_Prop(nOff)->ownerItem == -1 && lt_ext_Prop(nOn)->ownerItem == -1,
          "the cursor's row: the cursor alone");
    CHECK(strncmp(lt_ext_RowText(note), "New Game+ plays", 15) == 0,
          "the note explains New Game+ (%s)", lt_ext_RowText(note));
    press(0x2000);
    CHECK(l->curItem == nOn, "right: New Game+ On");
    press(0x1000); /* up */
    CHECK(l->curItem == mOn, "up: Mirror mode's choice, On, kept");
    CHECK(lt_ext_Prop(nOn)->ownerItem == mOn, "New Game+ On lit");
    press(0x4000);
    CHECK(l->curItem == nOn, "down again: New Game+ On, kept");
    press(0x4000); /* the rows in a loop */
    CHECK(l->curItem == mOn, "down from the last row: Mirror mode's choice");
    int games = s_newGames;
    ico_opt_set_mirror(0);
    press(0x40); /* Cross */
    CHECK(ico_opt_mirror() == 1, "Cross: the run is mirrored");
    CHECK(gFlagGameClear == 1, "Cross: New Game+ On sets the cleared flag");
    CHECK(s_newGames == games + 1, "the game starts (gflagOn(382))");
    press(0x40);
    press(0x800);
    CHECK(s_newGames == games + 1, "once");

    /* from a finished save (gFlagGameClear set): New Game+ starts On, and
       Off chosen there clears the flag; START confirms */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 1;
    ml = ui_NewGameScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen again");
    CHECK(l->curItem == mOff, "the cursor on Mirror mode's Off again");
    CHECK(lt_ext_Prop(nOn)->ownerItem == mOff && lt_ext_Prop(nOff)->ownerItem == -1,
          "New Game+ On lit (a finished save)");
    press(0x4000);
    CHECK(l->curItem == nOn, "down: New Game+ On");
    press(0x8000); /* left */
    CHECK(l->curItem == nOff, "left: New Game+ Off");
    press(0x1000);
    CHECK(l->curItem == mOff, "up: Mirror mode Off, kept");
    press(0x800); /* START */
    CHECK(ico_opt_mirror() == 0 && s_newGames == games + 2, "START: not mirrored, the game starts");
    CHECK(gFlagGameClear == 0, "New Game+ Off: the first journey even after finishing");

    /* a plain New Game with New Game+ chosen On */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 0;
    ml = ui_NewGameScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen a third time");
    press(0x4000);
    CHECK(l->curItem == nOff, "a plain New Game: New Game+ Off");
    press(0x2000);
    press(0x40);
    CHECK(gFlagGameClear == 1 && ico_opt_mirror() == 0 && s_newGames == games + 3,
          "New Game+ On from a plain New Game");

    /* Triangle: the vibration screen, the flag as it was */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 1;
    ml = ui_NewGameScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen a fourth time");
    press(0x4000);
    press(0x8000);
    press(0x10);
    CHECK(settle(9, 60), "Triangle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games + 3, "no game started");
    CHECK(gFlagGameClear == 1, "Triangle: the cleared flag untouched");
    gFlagGameClear = 0;

    /* not built: -1 (la_vibe_select then starts the game itself) */
    ui_SettingsReset();
    CHECK(ui_NewGameScreenEnter() == -1 && ui_NewGameScreenLayout() == -1, "not built: -1");
}

/* Q2: the title's "Quit to desktop" row under Settings (layouts 12 and 13,
 * in the entry layout), its confirmation screen run by the real layout
 * code: the cursor starts on No; Cross on No, Triangle and Circle return
 * to the title with the cursor on the row and the title's own default
 * restored after; Cross on Yes saves what is pending and calls the quit
 * handler once. */
static int s_quits;

static void countQuit(void)
{
    s_quits++;
}

static void testQuit(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    s_quits = 0;
    ui_SettingsSetQuitHandler(countQuit);
    init_layout_texture(2);
    settle(54, 4);
    int s12 = ui_SettingsEntryRow(12), s13 = ui_SettingsEntryRow(13);
    int q12 = ui_SettingsQuitRow(12), q13 = ui_SettingsQuitRow(13);
    int ql = ui_QuitScreenLayout(), yes = ui_QuitScreenRow(1), no = ui_QuitScreenRow(0);
    CHECK(q12 == s12 + 1 && q13 == s13 + 1, "the quit rows follow the Settings rows (%d %d)", q12,
          q13);
    CHECK(ui_SettingsQuitRow(57) == -1, "no quit row in the pause menu");
    CHECK(lt_ext_Prop(s13)->downItem == q13 && lt_ext_Prop(q13)->upItem == s13 &&
              lt_ext_Prop(q13)->downItem == -1 && lt_ext_Prop(q13)->left == -1,
          "Settings <-> Quit");
    CHECK(lt_ext_Prop(q13)->dispY > lt_ext_Prop(s13)->dispY &&
              lt_ext_Prop(s13)->dispY > texProperty[51].dispY,
          "below Settings, which is below New Game");
    CHECK(ql >= LT_GAME_LAYOUT_COUNT && lt_ext_Prop(q13)->right == ql &&
              lt_ext_Prop(q12)->right == ql,
          "Cross: the confirmation");
    CHECK(ui_SettingsEntryItem(q12) && ui_SettingsEntryItem(q13), "entry items (no game start)");
    LtProp *el = lt_ext_Layout(ui_SettingsEntryLayout(13));
    CHECK(el->first == s13 && el->last == q13 + 1, "one entry layout, two rows");
    CHECK(strcmp(lt_ext_RowText(q13), "Quit to desktop") == 0, "the label: %s",
          lt_ext_RowText(q13));
    static const char *const kQuit[5] = {"Quit to desktop?", "Quitter vers le bureau ?",
                                         "Zum Desktop beenden?", "Uscire al desktop?",
                                         "\xC2\xBFSalir al escritorio?"};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_StrIn(kLangs[i], UI_STR_QUIT_CONFIRM), kQuit[i]) == 0 &&
                  ui_StrIn(kLangs[i], UI_STR_QUIT_DESKTOP)[0] != '\0',
              "the question in language %d: %s", i, ui_StrIn(kLangs[i], UI_STR_QUIT_CONFIRM));
    }
    ui_SettingsTitleMask(1);
    CHECK(lt_ext_Prop(q12)->masked && lt_ext_Prop(q13)->masked && lt_ext_Prop(s13)->masked,
          "masked with the title's rows");
    ui_SettingsTitleMask(0);

    lt_switch_layout(13);
    CHECK(settle(13, 60), "the title");
    CHECK(texLayout[13].curItem == 51, "on New Game");
    press(0x4000);
    CHECK(texLayout[13].curItem == s13, "down: Settings");
    press(0x4000);
    CHECK(texLayout[13].curItem == q13, "down: Quit to desktop");
    press(0x40);
    CHECK(settle(ql, 60), "Cross: the confirmation (%d)", current_layout_id);
    CHECK(lt_ext_Layout(ql)->curItem == no, "the cursor on No");
    press(0x40); /* Cross on No */
    CHECK(settle(13, 60), "No: the title");
    CHECK(texLayout[13].curItem == q13, "the cursor on the quit row");
    frame(0);
    CHECK(texLayout[13].defaultItem == 51, "the title's own default again (%d)",
          texLayout[13].defaultItem);
    static const int kBack[2] = {0x10, 0x20};
    for (int i = 0; i < 2; i++) {
        press(0x40);
        CHECK(settle(ql, 60), "the confirmation again");
        int neg = s_sounds[2];
        press(kBack[i]);
        CHECK(settle(13, 60), "%s: the title", i ? "Circle" : "Triangle");
        CHECK(texLayout[13].curItem == q13 && s_sounds[2] > neg, "on the row, the cancel sound");
    }
    CHECK(s_quits == 0, "no quit yet");
    /* Yes: a pending change is written, the handler runs once */
    press(0x40);
    CHECK(settle(ql, 60), "the confirmation a fourth time");
    CHECK(lt_ext_Layout(ql)->curItem == no, "on No again");
    press(0x8000);
    CHECK(lt_ext_Layout(ql)->curItem == yes, "left: Yes");
    ui_SettingsStep(UI_OPT_STICK_FIX, 1);
    press(0x40);
    CHECK(s_quits == 1, "Cross on Yes: the quit handler");
    press(0x40);
    press(0x20);
    CHECK(s_quits == 1 && current_layout_id == ql, "once, and the screen stays");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1,
          "the pending change written before the quit");
    if (t) {
        ico_toml_free(t);
    }
    ico_opt_set_stick_fix(0);
    ui_SettingsSetQuitHandler(NULL);
}

/* Q2: Circle leaves every port screen as Triangle does, even with the game
 * menus' alias off ([game] circle_back = false): the Settings pages, the
 * two lists, the menu itself (to the pause menu) and the New Game screen
 * (the quit screen: testQuit). */
static void testCirclePortScreens(void)
{
    useConfig("version = 1\n[game]\ncircle_back = false\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    init_layout_texture(2);
    settle(54, 4);
    CHECK(lt_ext_BackButtons() == 0x10, "the game menus' alias off");
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    pauseToMain();

    static const struct {
        int row; /* main page row */
        UiSettingsPage page;
    } kPages[] = {{0, UI_PAGE_DISPLAY},  {1, UI_PAGE_EFFECTS},  {2, UI_PAGE_AUDIO},
                  {3, UI_PAGE_CONTROLS}, {4, UI_PAGE_GAMEPLAY}, {6, UI_PAGE_ACHIEVEMENTS}};

    int labels[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (unsigned i = 0; i < sizeof(kPages) / sizeof(kPages[0]); i++) {
        lt_ext_Layout(mainL)->curItem = labels[kPages[i].row];
        press(0x40);
        CHECK(settle(ui_SettingsPageLayout(kPages[i].page), 60), "page %d opens", kPages[i].page);
        int leaves = s_leaves;
        press(0x20);
        CHECK(settle(mainL, 60) && s_leaves == leaves + 1, "Circle: page %d back to the menu",
              kPages[i].page);
        CHECK(lt_ext_Layout(mainL)->curItem == labels[kPages[i].row], "on its row");
    }
    /* Controls -> Remap -> Circle -> Controls */
    lt_ext_Layout(mainL)->curItem = labels[3];
    press(0x40);
    int ctlL = ui_SettingsPageLayout(UI_PAGE_CONTROLS);
    CHECK(settle(ctlL, 60), "Controls");
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_REMAP), 60), "Remap");
    press(0x20);
    CHECK(settle(ctlL, 60), "Circle: Remap back to Controls");
    press(0x20);
    CHECK(settle(mainL, 60), "Circle: Controls back to the menu");
    press(0x20);
    CHECK(settle(57, 60), "Circle: the menu back to the pause menu");
    CHECK(texLayout[57].curItem == 294, "on Options");
    /* the New Game screen: Circle is Triangle there (the vibration screen) */
    lt_switch_layout(54);
    settle(54, 60);
    int ml = ui_NewGameScreenEnter(), games = s_newGames;
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen");
    press(0x20);
    CHECK(settle(9, 60), "Circle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games, "no game started");
}

/* Q2: the game's own menus through the real layout_texture.c: the Options
 * screen's rows (S1: no longer reached, its links the PAL data's) go back
 * to the pause menu (57) through their left link, which
 * default_item_select follows on Triangle, and on Circle while [game]
 * circle_back is on (the default); off, Circle does nothing there and
 * Triangle still goes back. */
static void testCircleGameMenu(void)
{
    for (int on = 1; on >= 0; on--) {
        useConfig(on ? "version = 1\n" : "version = 1\n[game]\ncircle_back = false\n");
        fakeTables();
        lt_ext_Reset();
        ui_SettingsReset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        gFlagGameClear = 0;
        init_layout_texture(2);
        settle(54, 4);
        CHECK(lt_ext_BackButtons() == (on ? 0x30 : 0x10), "circle_back %d: the bits", on);
        static const int kRows[2] = {308, 323}; /* two game rows */
        for (int r = 0; r < 2; r++) {
            int row = kRows[r];
            lt_switch_layout(58);
            CHECK(settle(58, 60), "Options");
            texLayout[58].curItem = row;
            press(0x20);
            if (on) {
                CHECK(settle(57, 60), "circle_back on: Circle on %d goes back to the pause menu",
                      row);
            } else {
                for (int i = 0; i < 30; i++) {
                    frame(0);
                }
                CHECK(current_layout_id == 58 && texLayout[58].curItem == row,
                      "circle_back off: Circle on %d does nothing (%d)", row, current_layout_id);
                press(0x10);
                CHECK(settle(57, 60), "circle_back off: Triangle on %d still goes back", row);
            }
        }
    }
}

static void testValues(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    IcoVideoOptions o;

    ui_SettingsStep(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED &&
              strstr(ui_SettingsValueText(UI_OPT_PRESET), "Enhanced"),
          "preset: Enhanced");
    CHECK(strstr(ui_SettingsValueText(UI_OPT_RESOLUTION), "Window") != NULL, "resolution: Window");
    ui_SettingsStep(UI_OPT_RESOLUTION, 1);
    ui_SettingsStep(UI_OPT_RESOLUTION, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 2 && strstr(ui_SettingsValueText(UI_OPT_RESOLUTION), "2x"),
          "resolution: 2x (%s)", ui_SettingsValueText(UI_OPT_RESOLUTION));
    ui_SettingsStep(UI_OPT_RESOLUTION, -1);
    ui_SettingsStep(UI_OPT_RESOLUTION, -1);
    ui_SettingsStep(UI_OPT_RESOLUTION, -1);
    ico_video_get(&o);
    CHECK(o.resScale == 4, "resolution wraps to 4x");
    /* Enhanced's aspect is Auto: Right wraps to 4:3, then 16:10, 16:9, 21:9, 32:9 */
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_16_9 && strstr(ui_SettingsValueText(UI_OPT_ASPECT), "16:9"),
          "aspect 16:9");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_21_9 && strstr(ui_SettingsValueText(UI_OPT_ASPECT), "21:9"),
          "aspect 21:9");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_32_9 && strstr(ui_SettingsValueText(UI_OPT_ASPECT), "32:9"),
          "aspect 32:9");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    CHECK(strstr(ui_SettingsValueText(UI_OPT_ASPECT), "Auto") != NULL, "aspect Auto");
    /* Enhanced's filter is anisotropic, its height full */
    ui_SettingsStep(UI_OPT_FILTER, -1);
    ico_video_get(&o);
    CHECK(o.filter == ICO_FILTER_TRILINEAR, "filter steps back to trilinear");
    ui_SettingsStep(UI_OPT_FILTER, -1);
    ui_SettingsStep(UI_OPT_FILTER, -1);
    ico_video_get(&o);
    CHECK(o.filter == ICO_FILTER_ANISOTROPIC, "filter wraps to anisotropic");
    ui_SettingsStep(UI_OPT_FULLSCREEN, 1);
    ui_SettingsStep(UI_OPT_VSYNC, 1);
    ui_SettingsStep(UI_OPT_FULL_HEIGHT, 1);
    ico_video_get(&o);
    CHECK(o.fullscreen == 1 && o.vsync == 0 && o.fullHeight == 0, "the three toggles");
    CHECK(strstr(ui_SettingsValueText(UI_OPT_VSYNC), "Off") != NULL, "vsync Off");

    /* package CRT: the CRT filter row cycles Off, Scanlines, Consumer TV,
       Trinitron, PVM, (CRT2) Shadow mask and around, setting [video] crt and
       crt_mode together; the strength steps in tens, clamped at 0 and 100 % */
    {
        static const char *const names[7] = {"Off", "Scanlines",   "Consumer TV", "Trinitron",
                                             "PVM", "Shadow mask", "Off"};
        static const int modes[7] = {
            -1, ICO_CRT_SCANLINES, ICO_CRT_CONSUMER, ICO_CRT_TRINITRON, ICO_CRT_PVM, ICO_CRT_SHADOW,
            -1};
        for (int i = 0; i < 7; i++) {
            ico_video_get(&o);
            CHECK(strcmp(ui_SettingsValueText(UI_OPT_CRT), names[i]) == 0 &&
                      o.crt == (modes[i] >= 0) && (modes[i] < 0 || o.crtMode == modes[i]),
                  "crt row %d: \"%s\" (crt %d mode %d)", i, ui_SettingsValueText(UI_OPT_CRT), o.crt,
                  o.crtMode);
            ui_SettingsStep(UI_OPT_CRT, 1);
        }
        ui_SettingsStep(UI_OPT_CRT, -1); /* back from Scanlines to Off */
        ui_SettingsStep(UI_OPT_CRT, -1); /* around to Shadow mask */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_SHADOW &&
                  strcmp(ui_SettingsValueText(UI_OPT_CRT), "Shadow mask") == 0,
              "crt row: Left wraps to Shadow mask");
        ui_SettingsStep(UI_OPT_CRT, -1); /* to PVM */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_PVM &&
                  strcmp(ui_SettingsValueText(UI_OPT_CRT), "PVM") == 0,
              "crt row: Left again to PVM");
        CHECK(strcmp(ui_SettingsValueText(UI_OPT_CRT_STRENGTH), "100 %") == 0,
              "crt strength 100 %% (%s)", ui_SettingsValueText(UI_OPT_CRT_STRENGTH));
        ui_SettingsStep(UI_OPT_CRT_STRENGTH, 1);
        ui_SettingsStep(UI_OPT_CRT_STRENGTH, -1);
        ui_SettingsStep(UI_OPT_CRT_STRENGTH, -1);
        ico_video_get(&o);
        CHECK(strcmp(ui_SettingsValueText(UI_OPT_CRT_STRENGTH), "80 %") == 0 &&
                  o.crtStrength > 0.79f && o.crtStrength < 0.81f,
              "crt strength clamps at 100 %%, steps to 80 %% (%s)",
              ui_SettingsValueText(UI_OPT_CRT_STRENGTH));
        for (int i = 0; i < 12; i++) {
            ui_SettingsStep(UI_OPT_CRT_STRENGTH, -1);
        }
        CHECK(strcmp(ui_SettingsValueText(UI_OPT_CRT_STRENGTH), "0 %") == 0,
              "crt strength clamps at 0 %%");
        for (int i = 0; i < 7; i++) {
            ui_SettingsStep(UI_OPT_CRT_STRENGTH, 1);
        }

        /* package CRT2: under the filter the Resolution row reads "1x (CRT)"
           and does not step; the file's 4x is kept and back with it off */
        CHECK(strcmp(ui_SettingsValueText(UI_OPT_RESOLUTION), "1x (CRT)") == 0,
              "resolution under the CRT filter: \"%s\"", ui_SettingsValueText(UI_OPT_RESOLUTION));
        ui_SettingsStep(UI_OPT_RESOLUTION, 1);
        ui_SettingsStep(UI_OPT_RESOLUTION, -1);
        ui_SettingsStep(UI_OPT_RESOLUTION, -1);
        ico_video_get(&o);
        CHECK(o.resScale == 4 && o.resW == 0, "resolution locked under the CRT filter (%d)",
              o.resScale);
        ui_SettingsStep(UI_OPT_CRT, 1); /* Shadow mask */
        ui_SettingsStep(UI_OPT_CRT, 1); /* Off */
        ico_video_get(&o);
        CHECK(o.crt == 0 && strstr(ui_SettingsValueText(UI_OPT_RESOLUTION), "4x") != NULL,
              "resolution back to 4x with the filter off (\"%s\")",
              ui_SettingsValueText(UI_OPT_RESOLUTION));
        ui_SettingsStep(UI_OPT_RESOLUTION, 1);
        ico_video_get(&o);
        CHECK(o.resScale == 0, "resolution steps again with the filter off (%d)", o.resScale);
        ui_SettingsStep(UI_OPT_RESOLUTION, -1);
        ui_SettingsStep(UI_OPT_CRT, -1); /* Shadow mask */
        ui_SettingsStep(UI_OPT_CRT, -1); /* PVM */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_PVM && o.resScale == 4,
              "crt back on, PVM; resolution 4x kept");
    }

    systemStatus[0] = 1;
    int resets = s_resets;
    CHECK(strstr(ui_SettingsValueText(UI_OPT_VIDEO_MODE), "PAL 50 Hz") != NULL, "PAL 50 Hz");
    ui_SettingsStep(UI_OPT_VIDEO_MODE, 1);
    CHECK(systemStatus[0] == 0 && s_resets == resets + 1 &&
              strstr(ui_SettingsValueText(UI_OPT_VIDEO_MODE), "60 Hz") != NULL,
          "60 Hz with the boot screen's reset");
    CHECK(strcmp(ico_config_get_string("video.video_mode", ""), "60hz") == 0, "video_mode 60hz");
    ui_SettingsStep(UI_OPT_VIDEO_MODE, 1);
    CHECK(systemStatus[0] == 1, "back to 50 Hz");

    ui_SettingsStep(UI_OPT_VOLUME, -1);
    CHECK(strstr(ui_SettingsValueText(UI_OPT_VOLUME), "90 %") != NULL, "volume 90 %%");
    ui_SettingsStep(UI_OPT_STICK_FIX, 1);
    CHECK(ico_opt_stick_fix() == 1 && strstr(ui_SettingsValueText(UI_OPT_STICK_FIX), "On"),
          "stick fix on");
    ui_SettingsStep(UI_OPT_YORDA, 1);
    CHECK(ico_opt_yorda_safe() == 1, "yorda_safe on");
    ui_SettingsStep(UI_OPT_DEVELOPER, 1);
    CHECK(ico_opt_developer_mode() == 1, "developer mode on");
    ui_SettingsStep(UI_OPT_MOUSE_SENS, 1);
    CHECK(ico_input_live_bindings()->mouse_sens == 1.25f, "mouse sensitivity 1.25");
    /* Q2: Circle goes back, on by default; the step turns the game menus'
       alias off at once and sets the key */
    CHECK(ico_opt_circle_back() == 1 && lt_ext_CircleBack() == 1 &&
              strcmp(ui_SettingsValueText(UI_OPT_CIRCLE_BACK), "On") == 0 &&
              lt_ext_BackButtons() == 0x30,
          "circle_back: on by default");
    ui_SettingsStep(UI_OPT_CIRCLE_BACK, 1);
    CHECK(ico_opt_circle_back() == 0 && lt_ext_BackButtons() == 0x10 &&
              strcmp(ui_SettingsValueText(UI_OPT_CIRCLE_BACK), "Off") == 0 &&
              ico_config_get_bool("game.circle_back", 1) == 0,
          "circle_back: off (Triangle alone)");
    ui_SettingsStep(UI_OPT_CIRCLE_BACK, -1);
    CHECK(ico_opt_circle_back() == 1 && lt_ext_BackButtons() == 0x30, "circle_back: on again");
    NonLinearCameraMove = 6;
    ui_SettingsStep(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2, "language wraps ES -> EN");

    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "the file");
    if (t) {
        /* the rows were stepped off Enhanced: Custom, saved as "enhanced" */
        CHECK(strcmp(ico_toml_get(t, "video.preset") ? ico_toml_get(t, "video.preset") : "",
                     "enhanced") == 0,
              "[video] preset");
        CHECK(ico_toml_get_bool(t, "video.fullscreen", 0) == 1, "[video] fullscreen");
        CHECK(ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1 &&
                  ico_toml_get_bool(t, "gameplay.yorda_safe", 0) == 1 &&
                  ico_toml_get_bool(t, "gameplay.developer_mode", 0) == 1,
              "[gameplay]");
        CHECK(ico_toml_get_float(t, "audio.volume", 0) > 0.89 &&
                  ico_toml_get_float(t, "audio.volume", 0) < 0.91,
              "[audio] volume");
        CHECK(ico_toml_get_float(t, "input.mouse_sensitivity", 0) == 1.25,
              "[input] mouse_sensitivity");
        CHECK(strcmp(ico_toml_get(t, "game.language") ? ico_toml_get(t, "game.language") : "",
                     "en") == 0,
              "[game] language");
        CHECK(strcmp(ico_toml_get(t, "video.video_mode") ? ico_toml_get(t, "video.video_mode") : "",
                     "pal50") == 0,
              "[video] video_mode");
        CHECK(ico_toml_get_bool(t, "video.crt", 0) == 1 &&
                  strcmp(ico_toml_get(t, "video.crt_mode") ? ico_toml_get(t, "video.crt_mode") : "",
                         "pvm") == 0 &&
                  ico_toml_get_float(t, "video.crt_strength", 0) > 0.69 &&
                  ico_toml_get_float(t, "video.crt_strength", 0) < 0.71,
              "[video] crt, crt_mode, crt_strength");
        ico_toml_free(t);
    }
}

/* Settings > Audio: the music and effects gains, the output mode against
   the game's own (the card's, the Options row's), the device list */
/* config.toml's [audio] output as the file holds it ("-" without one) */
static void outputOnDisk(char *out, size_t n)
{
    char p[1024];
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    const char *v = t ? ico_toml_get(t, "audio.output") : NULL;
    snprintf(out, n, "%s", v ? v : "-");
    ico_toml_free(t);
}

static void testAudio(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ico_audio_gain_reset();
    s_outputMode = 0;
    s_outputSets = 0;
    ui_SettingsInstall();
    CHECK(s_outputSets == 0, "auto: install leaves the game's mode alone");
    /* every audio row steps (the steppable range) */
    for (int o = UI_OPT_VOLUME; o <= UI_OPT_DEVICE; o++) {
        int row = ui_SettingsRowOf(UI_PAGE_AUDIO, (UiSettingsOpt)o);
        int labels[16], values[16];
        int n = ui_SettingsPageRows(UI_PAGE_AUDIO, labels, NULL, values, 16);
        int has = 0;
        for (int i = 0; i < n; i++) {
            has |= labels[i] == row && values[i] >= 0;
        }
        CHECK(row >= 0 && has, "audio opt %d has a value box", o);
    }

    /* music and effects: 0 % to 100 % in tens, live */
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MUSIC), "100 %") == 0, "music 100 %% (%s)",
          ui_SettingsValueText(UI_OPT_MUSIC));
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECTS), "100 %") == 0, "effects 100 %%");
    ui_SettingsStep(UI_OPT_MUSIC, -1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MUSIC), "90 %") == 0, "music 90 %%");
    CHECK(ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC) == 3686, "music gain 0.9 live (%d)",
          ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC));
    CHECK(ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS) == 4096, "effects untouched");
    ui_SettingsStep(UI_OPT_MUSIC, 1);
    ui_SettingsStep(UI_OPT_MUSIC, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MUSIC), "100 %") == 0 &&
              ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC) == 4096,
          "music clamps at 100 %%");
    for (int i = 0; i < 12; i++) {
        ui_SettingsStep(UI_OPT_EFFECTS, -1);
    }
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECTS), "0 %") == 0 &&
              ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS) == 0,
          "effects clamps at 0 %%");
    ui_SettingsStep(UI_OPT_EFFECTS, 1);
    ui_SettingsStep(UI_OPT_EFFECTS, 1);
    ui_SettingsStep(UI_OPT_EFFECTS, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECTS), "30 %") == 0, "effects 30 %% (%s)",
          ui_SettingsValueText(UI_OPT_EFFECTS));

    /* output: Auto shows the game's mode; Stereo and Mono set it and (S1,
       what the Options screen's Sound row did) make it the game's own, so
       Auto keeps the last one chosen */
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Auto (Stereo)") == 0, "output Auto (%s)",
          ui_SettingsValueText(UI_OPT_OUTPUT));
    s_outputMode = 1; /* the game's Options row: Mono */
    ico_opt_output_toggled(1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Auto (Mono)") == 0, "Auto (Mono)");
    CHECK(strcmp(ico_config_get_string("audio.output", "auto"), "auto") == 0,
          "auto: the Options row leaves the key");
    char outFile0[32], outFile[32];
    outputOnDisk(outFile0, sizeof(outFile0));
    ui_SettingsStep(UI_OPT_OUTPUT, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Stereo") == 0 && s_outputMode == 0 &&
              strcmp(ico_config_get_string("audio.output", ""), "stereo") == 0,
          "output Stereo, the game's mode set");
    ui_SettingsStep(UI_OPT_OUTPUT, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Mono") == 0 && s_outputMode == 1,
          "output Mono");
    outputOnDisk(outFile, sizeof(outFile));
    CHECK(strcmp(outFile, outFile0) == 0,
          "Stereo and Mono steps leave config.toml to the save on leaving (%s, was %s)", outFile,
          outFile0);
    ui_SettingsStep(UI_OPT_OUTPUT, -1);
    ui_SettingsStep(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_AUTO && s_outputMode == 0 &&
              strcmp(ico_config_get_string("audio.output", ""), "auto") == 0 &&
              strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Auto (Stereo)") == 0,
          "Auto again: the game's own mode, the last chosen (Stereo; %s)",
          ui_SettingsValueText(UI_OPT_OUTPUT));
    ui_SettingsStep(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_MONO, "Left from Auto wraps to Mono");
    /* explicit: the card's mode does not win, the Options row's change is
       written back */
    s_outputMode = 0; /* the card's system file: Stereo */
    soundOutputModeSet(ico_opt_output_card(soundOutputModeGet()));
    CHECK(s_outputMode == 1, "mono wins over the card's stereo");
    s_outputMode = 0; /* the Options row: Stereo */
    ico_opt_output_toggled(0);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_STEREO &&
              strcmp(ico_config_get_string("audio.output", ""), "stereo") == 0,
          "the Options row's Stereo written back");
    path(p, sizeof(p), "settings_test.toml");
    {
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get(t, "audio.output") != NULL &&
                  strcmp(ico_toml_get(t, "audio.output"), "stereo") == 0,
              "and saved at once");
        ico_toml_free(t);
    }
    /* auto: the card's value is the game's */
    ui_SettingsStep(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_AUTO, "auto");
    s_outputMode = 1;
    soundOutputModeSet(ico_opt_output_card(soundOutputModeGet()));
    CHECK(s_outputMode == 1, "auto: the card's mono kept");
    /* an explicit key is the game's from install on */
    useConfig("[audio]\noutput = \"mono\"\n");
    s_outputMode = 0;
    ui_SettingsInstall();
    CHECK(s_outputMode == 1, "output = mono at install");

    /* the device: Default, then each device; a long name cut to fit */
    useConfig("version = 1\n");
    s_devCount = 2;
    s_devNames[0] = "Speakers (Realtek High Definition Audio)";
    s_devNames[1] = "A very long name for a USB audio interface with eight outputs and more";
    s_reopens = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DEVICE), "Default") == 0, "device Default (%s)",
          ui_SettingsValueText(UI_OPT_DEVICE));
    ui_SettingsStep(UI_OPT_DEVICE, 1);
    CHECK(s_reopens == 1 && strcmp(s_reopened, s_devNames[0]) == 0 &&
              strcmp(ico_config_get_string("audio.device", ""), s_devNames[0]) == 0,
          "device: the first, reopened");
    {
        const char *v = ui_SettingsValueText(UI_OPT_DEVICE);
        CHECK(ui_MeasureText(UI_MENU_TEXT_SIZE * 0.6f, v) <= 176.0f, "the name fits (%s)", v);
    }
    ui_SettingsStep(UI_OPT_DEVICE, 1);
    {
        const char *v = ui_SettingsValueText(UI_OPT_DEVICE);
        size_t n = strlen(v);
        CHECK(n > 3 && strcmp(v + n - 3, "\xE2\x80\xA6") == 0 &&
                  strncmp(v, s_devNames[1], 10) == 0 &&
                  ui_MeasureText(UI_MENU_TEXT_SIZE * 0.6f, v) <= 176.0f,
              "the long name cut with an ellipsis (%s)", v);
    }
    ui_SettingsStep(UI_OPT_DEVICE, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DEVICE), "Default") == 0 && s_reopened[0] == '\0' &&
              s_reopens == 3,
          "device wraps to Default");
    ui_SettingsStep(UI_OPT_DEVICE, -1);
    CHECK(strcmp(s_reopened, s_devNames[1]) == 0, "Left from Default: the last device");
    /* a device no longer there counts as Default */
    useConfig("[audio]\ndevice = \"Gone\"\n");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DEVICE), "Gone") == 0, "the name as set");
    ui_SettingsStep(UI_OPT_DEVICE, 1);
    CHECK(strcmp(s_reopened, s_devNames[0]) == 0, "unknown steps from Default");
    /* no devices (headless): Default only, nothing reopened */
    s_devCount = 0;
    useConfig("version = 1\n");
    s_reopens = 0;
    ui_SettingsStep(UI_OPT_DEVICE, 1);
    CHECK(s_reopens == 0 && strcmp(ui_SettingsValueText(UI_OPT_DEVICE), "Default") == 0,
          "no devices: Default");
    /* other languages */
    s_outputMode = 0;
    ui_SetLanguage(UI_LANG_DE);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DEVICE), "Standard") == 0, "Standard");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_OUTPUT), "Automatisch (Stereo)") == 0,
          "Automatisch (Stereo) (%s)", ui_SettingsValueText(UI_OPT_OUTPUT));
    ui_SetLanguage(UI_LANG_EN);

    /* the save */
    useConfig("version = 1\n");
    ui_SettingsStep(UI_OPT_MUSIC, -1);
    ui_SettingsStep(UI_OPT_OUTPUT, 1);
    CHECK(ui_SettingsSave() == 0, "save");
    {
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get_float(t, "audio.music", 0) > 0.89 &&
                  ico_toml_get_float(t, "audio.music", 0) < 0.91 &&
                  strcmp(ico_toml_get(t, "audio.output") ? ico_toml_get(t, "audio.output") : "",
                         "stereo") == 0 &&
                  ico_toml_get_float(t, "audio.effects", 0) == 1.0,
              "[audio] music, output, effects in the file");
        ico_toml_free(t);
    }
    ico_audio_gain_reset();
    s_outputMode = 0;
}

static void testCapture(void)
{
    IcoBindings b;
    UiRemapCapture c;
    ico_bindings_defaults(&b);
    ui_RemapCaptureStart(&c, ICO_T_TRIANGLE);
    CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_WAITING, "waiting");
    ico_input_note_press(ICO_SRC_PAD, ICO_GP_SOUTH);
    CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_BOUND && !c.active, "bound");
    CHECK(b.gp[ICO_T_TRIANGLE][0] == ICO_GP_SOUTH && b.gp[ICO_T_CROSS][0] == 0,
          "south moves from Cross to Triangle");
    CHECK(b.kb[ICO_T_TRIANGLE][0] == ICO_KEY_R, "the keyboard row is kept");
    CHECK(c.cooldown == UI_REMAP_COOLDOWN_TICKS, "cooldown");
    CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_IDLE, "idle afterwards");
    /* a press before the capture started is not taken */
    ico_input_note_press(ICO_SRC_KEY, ICO_KEY_Q);
    ui_RemapCaptureStart(&c, ICO_T_L1);
    for (int i = 0; i < UI_REMAP_TIMEOUT_TICKS - 1; i++) {
        CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_WAITING, "still waiting");
        if (!c.active) {
            break;
        }
    }
    CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_TIMEOUT, "times out");
    CHECK(b.kb[ICO_T_L1][0] == ICO_KEY_TAB, "the timeout leaves the binding");
    /* a mouse button */
    ui_RemapCaptureStart(&c, ICO_T_R2);
    ico_input_note_press(ICO_SRC_MOUSE, 4);
    CHECK(ui_RemapCaptureStep(&c, &b) == UI_CAPTURE_BOUND && b.mouse[ICO_T_R2][0] == 4 &&
              b.kb[ICO_T_R2][0] == ICO_KEY_X,
          "mouse x1 onto R2, the key kept");
    /* clear and the config text */
    ico_bindings_clear(&b, ICO_T_R2);
    char buf[64];
    CHECK(strcmp(ico_bindings_row_text(&b, ICO_SRC_KEY, ICO_T_R2, buf, sizeof(buf)), "none") == 0,
          "cleared: none");
    CHECK(strcmp(ico_bindings_row_text(&b, ICO_SRC_KEY, ICO_T_L1, buf, sizeof(buf)),
                 "Tab, Backquote") == 0,
          "two keys: %s", buf);
    IcoBindings c2;
    ico_bindings_defaults(&c2);
    CHECK(ico_bindings_set(&c2, "kb.l1", "Tab, Backquote") == 0 && c2.kb[ICO_T_L1][1] != 0,
          "the written text reads back");
}

static void testBootSkip(void)
{
    static const int scf[6] = {0, 1, 2, 3, 4, 5};
    static const int game[6] = {2, 2, 3, 6, 4, 5};
    for (int i = 0; i < 6; i++) {
        CHECK(ico_scf_to_game_language(scf[i]) == game[i], "scf %d -> %d", scf[i], game[i]);
    }
    for (int g = 2; g <= 6; g++) {
        CHECK(ico_scf_to_game_language(ico_game_to_scf_language(g)) == g, "round trip %d", g);
    }
    useConfig("[game]\nlanguage = \"de\"\n[video]\nvideo_mode = \"60hz\"\n");
    CHECK(ico_boot_language() == 4, "language de -> 4");
    CHECK(ico_boot_video_mode() == 0, "video_mode 60hz -> 0");
    useConfig("[game]\nlanguage = \"it\"\n[video]\nvideo_mode = \"pal50\"\n");
    CHECK(ico_boot_language() == 5 && ico_boot_video_mode() == 1, "it, pal50");
    useConfig("[game]\nlanguage = \"auto\"\n");
    setEnv("LC_ALL", "es_ES.UTF-8");
    /* the host's locale (SDL's preferred locales in the window build, the
       environment headless) */
    CHECK(ico_boot_language() == ico_scf_to_game_language(ico_sysconf_host_language()),
          "auto: the host's locale");
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("es_ES.UTF-8")) == 6, "es_ES -> 6");
    CHECK(ico_boot_video_mode() == 0, "no video_mode: 60 Hz");
    setEnv("LC_ALL", "ja_JP.UTF-8");
    ico_sysconf_reset();
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("ja_JP.UTF-8")) == 2,
          "a language the game lacks: English (2)");
    setEnv("LC_ALL", NULL);
    useConfig("[video]\nvideo_mode = \"pal\"\n");
    CHECK(ico_boot_video_mode() == 0, "an invalid value: ignored (60 Hz)");
    ico_sysconf_set_language(ICO_SCF_LANGUAGE_FRENCH);
    CHECK(sceScfGetLanguage() == ICO_SCF_LANGUAGE_FRENCH &&
              strcmp(ico_config_get_string("game.language", ""), "fr") == 0,
          "the setter");
}

static int rowShown(UiSettingsPage page, UiSettingsOpt opt);

/* U1: the Video mode row changes only when Settings was opened from the
   title; from the pause menu Left and Right leave it. Pad names and the
   frame-rate words are translated. */
static void testVideoGate(void)
{
    for (int title = 0; title < 2; title++) {
        useConfig("version = 1\n");
        fakeTables();
        lt_ext_Reset();
        ui_SettingsReset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        NonLinearCameraMove = 2;
        init_layout_texture(2);
        settle(54, 4);
        if (title) {
            int s13 = ui_SettingsEntryRow(13);
            lt_switch_layout(13);
            CHECK(settle(13, 60), "the title");
            press(0x4000);
            CHECK(texLayout[13].curItem == s13, "on Settings");
            press(0x40);
        } else {
            pauseToMain();
        }
        int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
        CHECK(settle(mainL, 60), "the menu (title %d)", title);
        press(0x40); /* Display */
        int dispL = ui_SettingsPageLayout(UI_PAGE_DISPLAY);
        CHECK(settle(dispL, 60), "Display");
        int labels[16], opts[16];
        int n = ui_SettingsPageRows(UI_PAGE_DISPLAY, labels, opts, NULL, 16);
        int vm = 0, downs = 0;
        if (!title) {
            /* v0.4.0: from the pause menu the row is not shown (it cannot
               step there, and the page has no room once Texture pack is
               in); its value still says why */
            CHECK(!rowShown(UI_PAGE_DISPLAY, UI_OPT_VIDEO_MODE), "pause: no Video mode row");
            systemStatus[0] = 1;
            CHECK(strstr(ui_SettingsValueText(UI_OPT_VIDEO_MODE), "(title only)") != NULL,
                  "pause: the value reads \"%s\"", ui_SettingsValueText(UI_OPT_VIDEO_MODE));
            continue;
        }
        while (vm < n && opts[vm] != UI_OPT_VIDEO_MODE) {
            /* S1: Brightness, from the pause menu only, is above it */
            downs += !lt_ext_Prop(labels[vm])->defaultMask;
            vm++;
        }
        CHECK(downs == (title ? vm - 1 : vm), "title %d: Brightness %s", title,
              title ? "hidden" : "shown");
        for (int i = 0; i < downs; i++) {
            press(0x4000);
        }
        CHECK(lt_ext_Layout(dispL)->curItem == labels[vm], "on Video mode (%d of %d)", vm, n);
        systemStatus[0] = 1;
        CHECK((strstr(ui_SettingsValueText(UI_OPT_VIDEO_MODE), "(title only)") != NULL) == !title,
              "title %d: the value reads \"%s\"", title, ui_SettingsValueText(UI_OPT_VIDEO_MODE));
        press(0x2000);
        CHECK(systemStatus[0] == (title ? 0 : 1), "title %d: Right on Video mode: %d", title,
              systemStatus[0]);
        systemStatus[0] = 1;
        press(0x8000);
        CHECK(systemStatus[0] == (title ? 0 : 1), "title %d: Left on Video mode: %d", title,
              systemStatus[0]);
        systemStatus[0] = 1;
    }
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    static const char *const kUncapped[5] = {"Uncapped", "Illimit\xC3\xA9", "Unbegrenzt",
                                             "Illimitato", "Sin l\xC3\xADmite"};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_StrIn(kLangs[i], UI_STR_VAL_UNCAPPED), kUncapped[i]) == 0,
              "Uncapped in language %d", i);
        CHECK(strcmp(ui_StrIn(kLangs[i], UI_STR_FPS_UNIT), "fps") == 0, "fps in language %d", i);
        CHECK(ui_StrIn(kLangs[i], UI_STR_VIDEO_MODE_TITLE_ONLY)[0] != '\0', "note %d", i);
        for (int id = UI_STR_PAD_SOUTH; id <= UI_STR_PAD_RSTICK_DOWN; id++) {
            CHECK(ui_StrIn(kLangs[i], (UiStrId)id)[0] != '\0', "pad name %d in language %d", id, i);
        }
    }
    CHECK(strcmp(ui_StrIn(UI_LANG_FR, UI_STR_PAD_DPAD_UP), "Croix haut") == 0 &&
              strcmp(ui_StrIn(UI_LANG_DE, UI_STR_PAD_DPAD_UP), "Kreuz oben") == 0 &&
              strcmp(ui_StrIn(UI_LANG_EN, UI_STR_PAD_L1), "L1") == 0,
          "pad names");
}

/* --------------------------------------------- Extras and the shared lists */

/* stderr to a file for a stretch: the log lines the menus write */
static int s_errSaved = -1;

static void errCapture(void)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static void errRelease(char *out, size_t n)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    dup2(s_errSaved, 2);
    close(s_errSaved);
    clearerr(stderr);
    out[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(out, 1, n - 1, f);
        out[got] = '\0';
        fclose(f);
    }
}

/* the row of layout page whose text is str, -1 */
static int rowWithText(UiSettingsPage page, const char *str)
{
    LtProp *l = lt_ext_Layout(ui_SettingsPageLayout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strcmp(lt_ext_RowText(j), str) == 0) {
            return j;
        }
    }
    return -1;
}

/* Settings from the title (13) or from the pause menu (57) to the main
   page */
static int enterMain(int title)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    ui_SetLanguage(UI_LANG_EN);
    init_layout_texture(2);
    settle(54, 4);
    if (title) {
        lt_switch_layout(13);
        CHECK(settle(13, 60), "the title");
        press(0x4000);
        CHECK(texLayout[13].curItem == ui_SettingsEntryRow(13), "on Settings");
        press(0x40);
    } else {
        pauseToMain();
    }
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "the menu (title %d)", title);
    return mainL;
}

/* package MV: a stand-in for the model viewer's list (ui_SettingsSetModelsHandler) */
static int s_modelsCalls;

static int fakeModels(void)
{
    s_modelsCalls++;
    return ui_SettingsPageLayout(UI_PAGE_ACHIEVEMENTS);
}

/* package CRED: the Credits row unlocked by [dev] unlock_credits (the
   ending achievement is the player's way; credits_test and achievements_test
   check those), its locked look gone, and Cross with an engine that starts:
   the menu leaves for the game's empty layout (55) with the flag on and the
   title's cursor kept on Settings; with no engine, a failed start stays. */
static int s_fakeBegins;

static int fakeCreditsBegin(void)
{
    s_fakeBegins++;
    ico_credits_set_active(1);
    return 0;
}

static void testCredits(int mainL, int exL)
{
    static const IcoCreditsEngine kFake = {fakeCreditsBegin};
    char log[512];
    int el[8], eo[8], ev[8];

    useConfig("version = 1\n[dev]\nunlock_credits = true\n");
    press(0x40);
    CHECK(settle(exL, 60), "Extras, unlocked");
    ui_SettingsPageRows(UI_PAGE_EXTRAS, el, eo, ev, 8);
    lt_ext_Layout(exL)->curItem = el[2];
    frame(0);
    int note = rowWithText(UI_PAGE_EXTRAS, "Finish the game to unlock");
    CHECK(ev[2] >= 0 && strcmp(lt_ext_RowText(ev[2]), "") == 0 && lt_ext_RowDim(el[2]) == 0 &&
              lt_ext_RowDim(ev[2]) == 0,
          "Credits unlocked: value \"%s\", not greyed", ev[2] >= 0 ? lt_ext_RowText(ev[2]) : "-");
    CHECK(note >= 0 && lt_ext_Prop(note)->masked == 1, "no locked note on the unlocked Credits");
    /* no engine (this program has none): a failed start, the page stays */
    ico_credits_set_engine(NULL);
    errCapture();
    press(0x40);
    errRelease(log, sizeof(log));
    CHECK(strstr(log, "credits: enter") == NULL && strstr(log, "credits: failed") != NULL &&
              current_layout_id == exL && !ico_credits_active(),
          "Credits with no engine: fails and stays (\"%s\")", log);
    /* an engine: the playback starts and the menu leaves for layout 55 (the
       game's empty layout; here an empty one shaped like 54) */
    setLayout(55, 292, 292, -1, -1);
    texLayout[55].fadeInTime = 0.0f;
    texLayout[55].fadeOutTime = 0.0f;
    ico_credits_set_engine(&kFake);
    s_fakeBegins = 0;
    errCapture();
    press(0x40);
    errRelease(log, sizeof(log));
    CHECK(s_fakeBegins == 1 && strstr(log, "credits: enter") != NULL && ico_credits_active(),
          "Credits starts the playback (\"%s\")", log);
    CHECK(settle(55, 60), "the menu leaves for the game's empty layout (%d)", current_layout_id);
    CHECK(texLayout[13].defaultItem == ui_SettingsEntryRow(13),
          "the title comes back on Settings (%d)", texLayout[13].defaultItem);
    ico_credits_set_engine(NULL);
    ico_credits_set_active(0);
    useConfig("version = 1\n");
    (void)mainL;
}

/* Settings > Extras: a row of the main page after Achievements, from the
   title only; Music, Models, Credits and Back; the entries are placeholders
   that log; Credits shows the locked style. */
static void testExtras(void)
{
    char log[512];
    for (int title = 0; title < 2; title++) {
        int mainL = enterMain(title);
        int labels[16], opts[16];
        int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, opts, NULL, 16);
        int ex = ui_SettingsRowOf(UI_PAGE_MAIN, UI_OPT_LINK), idx = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(lt_ext_RowText(labels[i]), "Extras") == 0) {
                ex = labels[i];
                idx = i;
            }
        }
        /* v0.4.0: Dump textures (developer mode only, hidden here) before
           Back */
        CHECK(n == 12 && idx == 7, "Extras is the row after Achievements (index %d of %d)", idx, n);
        CHECK(lt_ext_Prop(ex)->right == ui_SettingsPageLayout(UI_PAGE_EXTRAS), "Extras opens");
        CHECK(lt_ext_Prop(ex)->defaultMask == !title, "title %d: the Extras row is %s", title,
              title ? "shown" : "hidden (masked)");
        /* the cursor: Achievements, Down */
        lt_ext_Layout(mainL)->curItem = labels[6];
        press(0x4000);
        CHECK(lt_ext_Layout(mainL)->curItem == (title ? ex : labels[8]),
              "title %d: Down from Achievements lands on %s", title,
              title ? "Extras" : "Developer");
        /* the rows below follow: Back's y, one pitch table for each entry */
        CHECK(title ? lt_ext_Prop(labels[11])->dispY == 40 + 15 * 9
                    : lt_ext_Prop(labels[11])->dispY == 40 + 17 * 8,
              "title %d: Back at y %d", title, lt_ext_Prop(labels[11])->dispY);
        if (!title) {
            continue;
        }
        /* open it */
        press(0x40);
        int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "the Extras page");
        int el[8], eo[8], ev[8];
        int en = ui_SettingsPageRows(UI_PAGE_EXTRAS, el, eo, ev, 8);
        CHECK(en == 4 && lt_ext_Layout(exL)->curItem == el[0], "four rows, the cursor on Music");
        CHECK(strcmp(lt_ext_RowText(el[0]), "Music") == 0 &&
                  strcmp(lt_ext_RowText(el[1]), "Models") == 0 &&
                  strcmp(lt_ext_RowText(el[2]), "Credits") == 0 &&
                  strcmp(lt_ext_RowText(el[3]), "Back") == 0,
              "Music, Models, Credits, Back");
        /* the locked style on Credits: greyed label and value, the note on
           the cursor only */
        CHECK(ev[2] >= 0 && strcmp(lt_ext_RowText(ev[2]), "Locked") == 0 &&
                  lt_ext_RowDim(el[2]) == 1 && lt_ext_RowDim(ev[2]) == 1 &&
                  lt_ext_RowDim(el[0]) == 0,
              "Credits: locked value \"%s\", greyed", ev[2] >= 0 ? lt_ext_RowText(ev[2]) : "-");
        int note = rowWithText(UI_PAGE_EXTRAS, "Finish the game to unlock");
        CHECK(note >= 0, "the locked note");
        frame(0);
        CHECK(note >= 0 && lt_ext_Prop(note)->masked == 1, "no note on Music");
        press(0x4000);
        press(0x4000);
        CHECK(lt_ext_Layout(exL)->curItem == el[2], "on Credits");
        CHECK(note >= 0 && lt_ext_Prop(note)->masked == 0, "the note on Credits");
        /* Music opens the gallery (testGallery has the page itself) */
        lt_ext_Layout(exL)->curItem = el[0];
        frame(0);
        press(0x40);
        int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
        CHECK(galL >= 0 && settle(galL, 60), "Cross on Music opens the gallery");
        press(0x10);
        CHECK(settle(exL, 60) && lt_ext_Layout(exL)->curItem == el[0],
              "Triangle: back on the Music row");
        /* Models only logs, and stays on the page; Credits (locked) too */
        const char *want[3] = {"music", "models", "credits"};
        for (int k = 1; k < 3; k++) {
            lt_ext_Layout(exL)->curItem = el[k];
            frame(0);
            errCapture();
            press(0x40);
            errRelease(log, sizeof(log));
            char line[64];
            snprintf(line, sizeof(line), "extras: %s not available", want[k]);
            /* Models: "not available"; the locked Credits: "credits: locked"
               alone (review nit: no false "not available") */
            CHECK((k == 2 ? strstr(log, line) == NULL : strstr(log, line) != NULL) &&
                      current_layout_id == exL,
                  "Cross on %s logs and stays (\"%s\")", want[k], log);
            CHECK(k != 2 || (strstr(log, "credits: locked") != NULL && !ico_credits_active()),
                  "Cross on the locked Credits: \"%s\"", log);
        }
        /* package MV: with the model viewer's handler (port/game/
           model_viewer.c registers its list), Models opens the layout it
           returns; here the achievements page stands in for the list */
        s_modelsCalls = 0;
        ui_SettingsSetModelsHandler(fakeModels);
        lt_ext_Layout(exL)->curItem = el[1];
        frame(0);
        press(0x40);
        CHECK(s_modelsCalls == 1 && settle(ui_SettingsPageLayout(UI_PAGE_ACHIEVEMENTS), 60),
              "Models opens the handler's layout (%d calls)", s_modelsCalls);
        ui_SettingsSetModelsHandler(NULL);
        press(0x10);
        CHECK(settle(mainL, 60), "back from the handler's layout");
        lt_ext_Layout(mainL)->curItem = ex;
        frame(0);
        press(0x40);
        CHECK(settle(exL, 60), "Extras after the handler's layout");
        /* Back and Triangle return to the main page, the cursor on Extras */
        lt_ext_Layout(exL)->curItem = el[3];
        press(0x40);
        CHECK(settle(mainL, 60) && lt_ext_Layout(mainL)->curItem == ex,
              "Back: the cursor on Extras");
        press(0x40);
        CHECK(settle(exL, 60), "Extras again");
        press(0x10);
        CHECK(settle(mainL, 60), "Triangle: the menu");
        testCredits(mainL, exL);
    }

    /* the strings, five languages */
    static const char *const want[5][5] = {
        {"Extras", "Music", "Models", "Credits", "Finish the game to unlock"},
        {"Extras", "Musique", "Mod\xC3\xA8les",
         "Cr\xC3\xA9"
         "dits",
         NULL},
        {"Extras", "Musik", "Modelle", "Mitwirkende", NULL},
        {"Extra", "Musica", "Modelli", "Crediti", NULL},
        {"Extras", "M\xC3\xBAsica", "Modelos",
         "Cr\xC3\xA9"
         "ditos",
         NULL}};
    static const int ids[5] = {UI_STR_EXTRAS, UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                               UI_STR_EXTRAS_CREDITS, UI_STR_EXTRAS_LOCKED_NOTE};
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int i = 0; i < 5; i++) {
            const char *got = ui_StrIn((UiLang)l, (UiStrId)ids[i]);
            CHECK(want[l][i]
                      ? strcmp(got, want[l][i]) == 0
                      : (got[0] != '\0' && strcmp(got, ui_StrIn(UI_LANG_EN, (UiStrId)ids[i])) != 0),
                  "string %d in language %d: \"%s\"", i, l, got);
        }
    }

    /* the layout extension's budget (layout_ext.h): what is used, and the
       developer-mode line */
    useConfig("version = 1\n[gameplay]\ndeveloper_mode = true\n");
    lt_ext_Reset();
    ui_SettingsReset();
    errCapture();
    ui_SettingsInstall();
    errRelease(log, sizeof(log));
    printf("settings_test: %d of %d properties, %d of %d layouts used\n", lt_ext_PropCount(),
           LT_EXT_MAX_PROPERTIES, lt_ext_LayoutCount(), LT_EXT_MAX_LAYOUTS);
    CHECK(lt_ext_PropCount() < LT_EXT_MAX_PROPERTIES, "property budget (%d of %d)",
          lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES);
    CHECK(lt_ext_LayoutCount() < LT_EXT_MAX_LAYOUTS, "layout budget (%d of %d)",
          lt_ext_LayoutCount(), LT_EXT_MAX_LAYOUTS);
    char want_line[96];
    snprintf(want_line, sizeof(want_line), "layout extension: %d of %d properties",
             lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES);
    CHECK(strstr(log, want_line) != NULL, "developer mode prints the budget (\"%s\")", log);
}

/* The shared list pages (ui_list.h) on a list of its own: 20 items, three
   headings (item 0, items 5 and 6 together, item 19) the cursor skips. */
static int s_fills;

static int tlCount(void *u)
{
    return *(int *)u;
}

static int tlHeading(void *u, int k)
{
    (void)u;
    return k == 0 || k == 5 || k == 6 || k == 19;
}

static void tlFill(void *u, int k, UiListSlot *out)
{
    static char text[8][16];
    (void)u;
    s_fills++;
    char *t = text[k % 8];
    snprintf(t, 16, "item %d", k);
    out->label = t;
    out->colAStr = UI_STR_ON;
}

/* one tick: the proc, then the layout's move (default_item_select) */
static int s_hdrDecorated;

static void tlDecorate(void *u, int cur)
{
    (void)u;
    s_hdrDecorated = cur;
}

static int s_moveSounds; /* default_item_select's cursor sounds */

/* the engine's order in exec_layout_texture: the layout's proc, then
   default_item_select unless the proc set lt_item_select_disable (reset at
   the end of the frame) */
static void listStep(UiList *l, LtProp *lay, int flags)
{
    ui_ListProc(l, lay, flags);
    int before = lay->curItem;
    if (!(flags & 0x50) && lt_item_select_disable == 0) {
        const LtProperty *e = lt_ext_Prop(lay->curItem);
        if ((flags & 0x1000) && e->upItem >= 0) {
            lay->curItem = e->upItem;
        } else if ((flags & 0x4000) && e->downItem >= 0) {
            lay->curItem = e->downItem;
        }
    }
    if (lay->curItem != before) {
        s_moveSounds++;
    }
    lt_item_select_disable = 0;
    ui_ListRefresh(l, lay->curItem);
}

/* ----------------------------------------------------------- the gallery
 * Settings > Extras > Music (gallery.h) over a fake engine: a few streams
 * in each group, the heading skip, Cross / Square / Left / Right and leaving.
 * The list from the PAL tables is gallery_test's. */

static AdpcmDataRec s_fakeAdpcm[105];
static int s_galPlays, s_galStops, s_galLeaves, s_galLastKey = -1, s_galPaused;
static const GalleryItem *s_galCur;

static int galTables(GalleryTables *t)
{
    static const struct {
        int no;
        const char *path;
    } kRows[] = {{1, "sound/ICO_ADPCM/battle.int"},
                 {6, "sound/ICO_ADPCM/event/01.int"},
                 {60, "sound/ICO_ADPCM/event2/54.int"},
                 {61, "sound/ICO_ADPCM/event2/55.int"},
                 {101, "sound/ICO_ADPCM/event2/hint1_1.int"}};

    for (unsigned i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
        snprintf(s_fakeAdpcm[kRows[i].no].path, sizeof(s_fakeAdpcm[0].path), "%s", kRows[i].path);
        s_fakeAdpcm[kRows[i].no].channels = 2;
        s_fakeAdpcm[kRows[i].no].pitch = 44100;
    }
    memset(t, 0, sizeof(*t));
    t->adpcm = s_fakeAdpcm;
    t->adpcmCount = 105;
    return 0;
}

static int galPlay(const GalleryItem *it)
{
    s_galPaused = 0;
    s_galPlays++;
    s_galLastKey = it->key;
    s_galCur = it;
    return 0;
}

static void galStop(void)
{
    s_galPaused = 0;
    s_galStops++;
    s_galCur = NULL;
}

static void galLeaveHook(void)
{
    s_galLeaves++;
    s_galCur = NULL;
}

static const GalleryItem *galPlaying(void)
{
    return s_galCur;
}

/* a stream pauses; an effect cannot */
static int galPause(int on)
{
    if (s_galCur == NULL || s_galCur->kind != GAL_K_STREAM) {
        return -1;
    }
    s_galPaused = on;
    return 0;
}

/* 42 s into 4:25 */
static int galPosition(float *el, float *tot)
{
    if (s_galCur == NULL) {
        return -1;
    }
    *el = 42.4f;
    *tot = 265.6f;
    return 0;
}

static const GalleryEngine kFakeEngine = {galTables, NULL,       galLeaveHook, galPlay,    galStop,
                                          NULL,      galPlaying, galPause,     galPosition};

/* the port row of a page with that text right after a glyph row */
static int glyphBefore(UiSettingsPage page, const char *word)
{
    int t = rowWithText(page, word);
    return t > 0 && lt_ext_IsGlyphRow(lt_ext_Prop(t - 1)) ? t - 1 : -1;
}

static int fillRow(UiSettingsPage page)
{
    LtProp *l = lt_ext_Layout(ui_SettingsPageLayout(page));
    int last = -1;
    /* the third rect of the page: the rim, the track, the fill */
    int n = 0;
    for (int j = l->first; j < l->last; j++) {
        LtProperty *p = lt_ext_Prop(j);
        if (p->texFileNo >= 0x7000 && !lt_ext_IsGlyphRow(p) && ++n == 3) {
            last = j;
        }
    }
    return last;
}

/* every glyph's source row and texel rectangle, as the PAL texProperty
   table holds them (the boot ELF's rows: 182/344/343/184 the face buttons on
   buttons.tm2, 349/346/348/347 L1 R1 L2 R2 on menu_PAL_02 at v 240, 301/302
   the arrows on menu_PAL_01), and the box each draws beside a 27-unit label */
static void testGlyphSources(void)
{
    static const struct {
        int glyph, row, u, v, w, h, boxW, boxH;
    } k[] = {
        {LT_GLYPH_CROSS, 182, 32, 30, 32, 30, 32, 30},
        {LT_GLYPH_CIRCLE, 344, 0, 30, 32, 30, 32, 30},
        {LT_GLYPH_SQUARE, 343, 32, 0, 32, 30, 32, 30},
        {LT_GLYPH_TRIANGLE, 184, 0, 0, 32, 30, 32, 30},
        {LT_GLYPH_L1, 349, 420, 240, 40, 15, 40, 30},
        {LT_GLYPH_R1, 346, 340, 240, 40, 15, 40, 30},
        {LT_GLYPH_L2, 348, 460, 240, 40, 15, 40, 30},
        {LT_GLYPH_R2, 347, 380, 240, 40, 15, 40, 30},
        {LT_GLYPH_LEFT, 301, 490, 130, 20, 20, 20, 40},
        {LT_GLYPH_RIGHT, 302, 490, 150, 20, 20, 20, 40},
        {LT_GLYPH_OPTIONS, 294, 384, 120, 128, 20, 128, 40},
    };

    CHECK(sizeof(k) / sizeof(k[0]) == LT_GLYPH_COUNT, "a source listed for every glyph");
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        int uvwh[4], bw = 0, bh = 0;
        int row = lt_ext_GlyphSource(k[i].glyph, uvwh);
        lt_ext_GlyphBox(k[i].glyph, UI_MENU_TEXT_SIZE, &bw, &bh);
        CHECK(row == k[i].row && uvwh[0] == k[i].u && uvwh[1] == k[i].v && uvwh[2] == k[i].w &&
                  uvwh[3] == k[i].h,
              "glyph %d: row %d (%d,%d %dx%d)", k[i].glyph, row, uvwh[0], uvwh[1], uvwh[2],
              uvwh[3]);
        CHECK(bw == k[i].boxW && bh == k[i].boxH, "glyph %d: box %dx%d", k[i].glyph, bw, bh);
    }
}

/* The title's Options rows: the pause menu's word, row 294's texels (its
   sheet, so each language's own word), drawn as a game row, centred and
   cut to the word; with tables that are not the PAL ones, the label */
static void testTitleOptionsWord(void)
{
    const int mainL = enterMain(1);
    (void)mainL;
    const LtProperty *src = &texProperty[294];
    for (int g = 12; g <= 13; g++) {
        const int row = ui_SettingsEntryRow(g);
        const LtProperty *e = lt_ext_Prop(row);
        CHECK(row >= 0 && lt_ext_IsGlyphRow(e) && !lt_ext_IsTextRow(e) &&
                  lt_ext_GlyphTexNo(e) == src->texNo && src->texNo >= 0,
              "title %d: Options draws row 294's texture (%d, %d)", g, lt_ext_GlyphTexNo(e),
              src->texNo);
        CHECK(e->texU == src->texU && e->texV == src->texV && e->texH == src->texH && e->texW > 0 &&
                  e->texW <= src->texW,
              "title %d: row 294's rectangle (%d,%d %dx%d of %d,%d %dx%d)", g, e->texU, e->texV,
              e->texW, e->texH, src->texU, src->texV, src->texW, src->texH);
        CHECK(e->centerX && e->dispW == 0 && e->dispH == texProperty[50].dispH && e->selectable,
              "title %d: a game row's box, centred (%d x %d)", g, e->dispW, e->dispH);
        CHECK(strcmp(lt_ext_RowText(row), "Options") == 0, "title %d: its label Options (%s)", g,
              lt_ext_RowText(row));
    }
    /* the cut: 7 texels each side of the lettering (English: texels 7 to
       84 of the rectangle), so the word's middle is the row's */
    const int row = ui_SettingsEntryRow(13);
    CHECK(lt_ext_Prop(row)->texW == 92, "the cut: %d texels", lt_ext_Prop(row)->texW);
    ui_SetLanguage(UI_LANG_DE);
    NonLinearCameraMove = 4; /* German */
    lt_switch_layout(13);
    settle(13, 60);
    CHECK(lt_ext_Prop(row)->texW == 105, "German: the cut follows the word (%d)",
          lt_ext_Prop(row)->texW);
    NonLinearCameraMove = 2;
    ui_SetLanguage(UI_LANG_EN);
    settle(13, 60);
    frame(0);
    /* every language's word: the main page's heading and the label */
    static const char *const kWord[5] = {"Options", "Options", "Optionen", "Opzioni", "Opción"};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_StrIn(kLangs[i], UI_STR_SETTINGS), kWord[i]) == 0 &&
                  strcmp(ui_StrIn(kLangs[i], UI_STR_MT_OPTIONS), kWord[i]) == 0,
              "language %d: the menu is the game's word %s (%s)", i, kWord[i],
              ui_StrIn(kLangs[i], UI_STR_SETTINGS));
    }
    /* not the PAL rectangle: the label in the port's lettering */
    texProperty[294].texW = 100;
    CHECK(lt_ext_IsTextRow(lt_ext_Prop(row)), "tables not PAL: the label instead");
    texProperty[294].texW = 128;
}

static void testGallery(void)
{
    gallery_SetEngine(&kFakeEngine);
    int mainL = enterMain(1);
    int ml[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
    lt_ext_Layout(mainL)->curItem = ml[7];
    press(0x40);
    int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
    CHECK(settle(exL, 60), "gallery: Extras");
    lt_ext_Layout(exL)->curItem = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
    frame(0);
    press(0x40);
    int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
    CHECK(settle(galL, 60), "gallery: the page");
    frame(0);
    int lab[16], opts[16];
    ui_SettingsPageRows(UI_PAGE_MUSIC, lab, opts, NULL, 16);
    /* Soundtrack, battle, event/01, Scene sounds, event2/54, event2/55,
       Ambience, Voice: the assets' names, no column */
    CHECK(strcmp(lt_ext_RowText(lab[0]), "Soundtrack") == 0 &&
              strcmp(lt_ext_RowText(lab[1]), "battle") == 0 &&
              strcmp(lt_ext_RowText(lab[2]), "event/01") == 0 &&
              strcmp(lt_ext_RowText(lab[3]), "Scene sounds") == 0 &&
              strcmp(lt_ext_RowText(lab[4]), "event2/54") == 0,
          "gallery rows: \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", lt_ext_RowText(lab[0]),
          lt_ext_RowText(lab[1]), lt_ext_RowText(lab[2]), lt_ext_RowText(lab[3]),
          lt_ext_RowText(lab[4]));
    CHECK(strcmp(lt_ext_RowText(lab[1] + 1), "") == 0, "no column for a stream (\"%s\")",
          lt_ext_RowText(lab[1] + 1));
    CHECK(lt_ext_Prop(lab[0])->dispX < lt_ext_Prop(lab[1])->dispX, "headings stand out");
    /* the transport: each word after its glyph (a texture row: the fake
       tables hold the glyphs' rectangles), on one line */
    static const char *const kWords[] = {"Previous", "Play", "Stop", "Next", "Section", "Back"};
    int lastX = -1;
    for (unsigned w = 0; w < sizeof(kWords) / sizeof(kWords[0]); w++) {
        int g = glyphBefore(UI_PAGE_MUSIC, kWords[w]);
        int t = rowWithText(UI_PAGE_MUSIC, kWords[w]);
        CHECK(g >= 0, "the transport's \"%s\" after a glyph", kWords[w]);
        if (g < 0) {
            continue;
        }
        LtProperty *gp = lt_ext_Prop(g), *tp = lt_ext_Prop(t);
        CHECK(!lt_ext_IsTextRow(gp) && lt_ext_GlyphTexNo(gp) > 0, "\"%s\": a texture glyph",
              kWords[w]);
        CHECK(gp->dispX + gp->dispW <= tp->dispX && gp->dispX > lastX,
              "\"%s\": glyph then word, left to right", kWords[w]);
        /* the glyph's middle on the word's capitals (6.5 field lines into
           its box), within a field line */
        float mid = (float)gp->dispY + (float)gp->dispH * 0.25f;
        CHECK(mid > (float)tp->dispY + 5.4f && mid < (float)tp->dispY + 7.6f,
              "\"%s\": glyph centred on the word (%.1f, box %d)", kWords[w], mid, tp->dispY);
        lastX = tp->dispX;
    }
    int fill = fillRow(UI_PAGE_MUSIC);
    CHECK(fill >= 0 && lt_ext_RowFill(fill) == 0.0f, "the bar empty while nothing plays");
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:00") >= 0, "0:00 while nothing plays");
    LtProp *l = lt_ext_Layout(galL);
    CHECK(l->curItem == lab[1], "the cursor skips the heading onto battle");
    press(0x40);
    CHECK(s_galPlays == 1 && s_galLastKey == 1, "Cross plays stream 1");
    frame(0);
    CHECK(rowWithText(UI_PAGE_MUSIC, "Soundtrack  \xC2\xB7  battle.int  \xC2\xB7  Playing") >= 0,
          "the status: group, file, Playing");
    CHECK(fill >= 0 && lt_ext_RowFill(fill) > 0.15f && lt_ext_RowFill(fill) < 0.17f,
          "the bar at 42.4 of 265.6 s (%.3f)", fill >= 0 ? lt_ext_RowFill(fill) : -1.0f);
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:42") >= 0 && rowWithText(UI_PAGE_MUSIC, "4:25") >= 0,
          "the times 0:42 and 4:25");
    CHECK(glyphBefore(UI_PAGE_MUSIC, "Pause") >= 0, "Cross's word is Pause while it plays");
    press(0x40);
    CHECK(s_galPaused == 1 && s_galPlays == 1, "Cross again pauses it");
    frame(0);
    CHECK(rowWithText(UI_PAGE_MUSIC, "Soundtrack  \xC2\xB7  battle.int  \xC2\xB7  Paused") >= 0,
          "the status: Paused");
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:42") >= 0, "the bar kept while paused");
    press(0x40);
    CHECK(s_galPaused == 0 && s_galPlays == 1, "Cross again resumes it");
    press(0x0008);
    CHECK(s_galPlays == 2 && s_galLastKey == 6 &&
              strcmp(lt_ext_RowText(l->curItem), "event/01") == 0,
          "R1: the next entry, under the cursor (%d)", s_galLastKey);
    press(0x0008);
    CHECK(s_galLastKey == 60 && strcmp(lt_ext_RowText(l->curItem), "event2/54") == 0,
          "R1 over the Scene sounds heading");
    press(0x0004);
    CHECK(s_galLastKey == 6, "L1: the previous entry");
    press(0x0004);
    press(0x80);
    CHECK(s_galStops == 1, "Square stops");
    frame(0);
    CHECK(fill >= 0 && lt_ext_RowFill(fill) == 0.0f, "the bar empty once stopped");
    press(0x4000);
    press(0x4000);
    CHECK(strcmp(lt_ext_RowText(l->curItem), "event2/54") == 0,
          "Down skips the Scene sounds heading");
    press(0x8000);
    CHECK(strcmp(lt_ext_RowText(l->curItem), "battle") == 0,
          "Left: back to the soundtrack's first entry");
    press(0x2000);
    CHECK(strcmp(lt_ext_RowText(l->curItem), "event2/54") == 0,
          "Right: the scene sounds' first entry (\"%s\")", lt_ext_RowText(l->curItem));
    press(0x2000);
    CHECK(strcmp(lt_ext_RowText(l->curItem), "event2/hint1_1") == 0,
          "Right: over the empty Ambience to the voices (\"%s\")", lt_ext_RowText(l->curItem));
    press(0x10);
    CHECK(settle(exL, 60) && s_galLeaves == 1, "Triangle leaves the gallery");
    gallery_SetEngine(NULL);
}

static int tlPlain(void *u, int k)
{
    (void)u;
    (void)k;
    return 0;
}

/* a wrap through the engine's order (proc, then default_item_select) lands
   on the end row, with one cursor sound and no extra move */
static void testListWrap(int count)
{
    UiListDef def = {tlCount, tlFill, tlPlain, NULL, tlDecorate};
    UiListStyle st;
    UiList l;
    LtProp lay;
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    lt_ext_Reset();
    ui_SettingsReset();
    ui_ListBuild(&l, &def, &count, &st);
    memset(&lay, 0, sizeof(lay));
    ui_ListReset(&l);
    ui_ListRefresh(&l, -1);
    int shown = ui_ListShown(&l);
    /* Up from the first item wraps to the last */
    lay.curItem = l.label[0];
    ui_ListRefresh(&l, lay.curItem);
    int snd = s_sounds[0] + s_moveSounds;
    listStep(&l, &lay, 0x1000);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == count - 1 && l.offset == count - shown,
          "%d items: Up from the first wraps to the last (item %d, offset %d)", count,
          ui_ListItemOfRow(&l, lay.curItem), l.offset);
    CHECK(s_sounds[0] + s_moveSounds == snd + 1, "%d items: one cursor sound on the Up wrap (%d)",
          count, s_sounds[0] + s_moveSounds - snd);
    listStep(&l, &lay, 0);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == count - 1, "%d items: the next frame stays", count);
    /* Down from the last item wraps to the first */
    snd = s_sounds[0] + s_moveSounds;
    listStep(&l, &lay, 0x4000);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 0 && l.offset == 0,
          "%d items: Down from the last wraps to the first (item %d, offset %d)", count,
          ui_ListItemOfRow(&l, lay.curItem), l.offset);
    CHECK(s_sounds[0] + s_moveSounds == snd + 1, "%d items: one cursor sound on the Down wrap (%d)",
          count, s_sounds[0] + s_moveSounds - snd);
}

/* --------------------------------------- the game's settings (S1) */

static int rowShown(UiSettingsPage page, UiSettingsOpt opt)
{
    const int row = ui_SettingsRowOf(page, opt);
    return row >= 0 && !lt_ext_Prop(row)->defaultMask && !lt_ext_Prop(row)->masked;
}

/* the main page's row `mainRow` (all rows counted), Cross: its page */
static int openPage(int mainL, int mainRow, UiSettingsPage page)
{
    int labels[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    lt_ext_Layout(mainL)->curItem = labels[mainRow];
    press(0x40);
    const int l = ui_SettingsPageLayout(page);
    CHECK(settle(l, 60), "page %d opens (%d)", page, current_layout_id);
    return l;
}

/* the page in front: its shown rows top to bottom at least 13 field lines
   apart, below the header, the last one's box inside the 226 lines */
static void checkPageFits(UiSettingsPage page, const char *what)
{
    int labels[16];
    const int n = ui_SettingsPageRows(page, labels, NULL, NULL, 16);
    int prev = -1, last = -1, shown = 0;
    for (int i = 0; i < n; i++) {
        shown += !lt_ext_Prop(labels[i])->defaultMask;
    }
    /* v0.5.0: the title's fourteen Display rows (Model pack) 12 lines apart */
    const int gap = page == UI_PAGE_DISPLAY && shown > 13 ? 12 : 13;
    for (int i = 0; i < n; i++) {
        const LtProperty *r = lt_ext_Prop(labels[i]);
        if (r->defaultMask) {
            continue;
        }
        CHECK(prev < 0 ? r->dispY >= 34 : r->dispY >= prev + gap,
              "%s: row %d at y %d (the one above at %d)", what, i, r->dispY, prev);
        prev = r->dispY;
        last = labels[i];
    }
    CHECK(last >= 0 && lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH <= 226,
          "%s: the last row's box ends at %d", what,
          last >= 0 ? lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH : -1);
}

/* the note row of `page` whose text starts with prefix, -1 */
static int noteStarting(UiSettingsPage page, const char *prefix)
{
    LtProp *l = lt_ext_Layout(ui_SettingsPageLayout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strncmp(lt_ext_RowText(j), prefix, strlen(prefix)) == 0) {
            return j;
        }
    }
    return -1;
}

/* S1: the game's Options screen's settings on the pages.  Brightness
 * (Display), Button configuration, Vibration and Hold type (Controls) show
 * from the pause menu only, Film effect and Players (Gameplay) only there
 * once the game is cleared; the hidden rows are stepped over and the pages
 * still fit.  Their values are the game's variables; Film effect goes
 * through la_host_film_effect (the stage animations); Button configuration
 * opens the game's layout 59, whose OK comes back to Controls on the row. */
static void testGameOptions(void)
{
    for (int title = 1; title >= 0; title--) {
        gFlagGameClear = 0;
        stage_no = title ? 1 : 11;
        int mainL = enterMain(title);
        openPage(mainL, 0, UI_PAGE_DISPLAY);
        CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_BRIGHTNESS) == !title, "title %d: Brightness %s",
              title, title ? "hidden" : "shown");
        checkPageFits(UI_PAGE_DISPLAY, title ? "Display (title)" : "Display (pause)");
        press(0x10);
        CHECK(settle(mainL, 60), "Display: back");
        const int ctlL = openPage(mainL, 3, UI_PAGE_CONTROLS);
        CHECK(rowShown(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG) == !title &&
                  rowShown(UI_PAGE_CONTROLS, UI_OPT_VIBRATION) == !title &&
                  rowShown(UI_PAGE_CONTROLS, UI_OPT_HOLD_TYPE) == !title,
              "title %d: the game's controls %s", title, title ? "hidden" : "shown");
        checkPageFits(UI_PAGE_CONTROLS, title ? "Controls (title)" : "Controls (pause)");
        press(0x4000);
        CHECK(lt_ext_Layout(ctlL)->curItem ==
                  ui_SettingsRowOf(UI_PAGE_CONTROLS,
                                   title ? UI_OPT_MOUSE_SENS : UI_OPT_BUTTON_CONFIG),
              "title %d: down from Remap", title);
        press(0x10);
        CHECK(settle(mainL, 60), "Controls: back");
        openPage(mainL, 4, UI_PAGE_GAMEPLAY);
        CHECK(!rowShown(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT) &&
                  !rowShown(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS),
              "title %d, not cleared: no Film effect or Players", title);
        checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (not cleared)");
        press(0x10);
        CHECK(settle(mainL, 60), "Gameplay: back");
    }

    /* cleared, from the pause menu */
    gFlagGameClear = 1;
    stage_no = 11;
    int mainL = enterMain(0);
    const int gameL = openPage(mainL, 4, UI_PAGE_GAMEPLAY);
    const int film = ui_SettingsRowOf(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT);
    const int players = ui_SettingsRowOf(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS);
    CHECK(rowShown(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT) &&
              rowShown(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS),
          "cleared: Film effect and Players");
    checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (cleared)");
    press(0x4000);
    press(0x4000);
    CHECK(lt_ext_Layout(gameL)->curItem == film, "down twice from Yorda: Film effect");
    optionScreenMode = 0;
    s_filmCalls = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FILM_EFFECT), "Off") == 0, "film effect Off (%s)",
          ui_SettingsValueText(UI_OPT_FILM_EFFECT));
    press(0x2000);
    CHECK(s_filmCalls == 1 && s_filmLast == 1 && optionScreenMode == 1 &&
              strcmp(ui_SettingsValueText(UI_OPT_FILM_EFFECT), "1") == 0,
          "Right: film effect 1 through la_host_film_effect (%d calls, %d)", s_filmCalls,
          s_filmLast);
    press(0x8000);
    press(0x8000);
    CHECK(s_filmCalls == 3 && s_filmLast == 4 && optionScreenMode == 4,
          "Left twice: Off, then around to 4 (%d)", optionScreenMode);
    la_host_film_effect(0);
    press(0x4000);
    CHECK(lt_ext_Layout(gameL)->curItem == players, "down: Players");
    girlControlMode = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_PLAYERS), "1") == 0, "Players 1");
    press(0x2000);
    CHECK(girlControlMode == 1 && strcmp(ui_SettingsValueText(UI_OPT_PLAYERS), "2") == 0,
          "Right: Players 2 (girlControlMode %d)", girlControlMode);
    const int pnote = noteStarting(UI_PAGE_GAMEPLAY, "2: a second");
    CHECK(pnote >= 0 && !lt_ext_Prop(pnote)->masked, "the Players note on the cursor");
    press(0x8000);
    CHECK(girlControlMode == 0, "Left: Players 1");
    press(0x10);
    CHECK(settle(mainL, 60), "Gameplay: back");

    /* Controls: Vibration and Hold type, Button configuration to 59 and back */
    const int ctlL = openPage(mainL, 3, UI_PAGE_CONTROLS);
    const int bc = ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG);
    CHECK(bc >= 0 && lt_ext_Prop(bc)->right == 59, "Button configuration opens 59");
    press(0x4000);
    press(0x4000);
    CHECK(lt_ext_Layout(ctlL)->curItem == ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_VIBRATION),
          "on Vibration");
    iosPadActRequestEnable = 1;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_VIBRATION), "On") == 0, "vibration On");
    press(0x2000);
    CHECK(iosPadActRequestEnable == 0 && strcmp(ui_SettingsValueText(UI_OPT_VIBRATION), "Off") == 0,
          "Right: vibration off (%d)", iosPadActRequestEnable);
    press(0x8000);
    CHECK(iosPadActRequestEnable == 1, "Left: on again");
    press(0x4000);
    optionControlType = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_HOLD_TYPE), "A (hold)") == 0, "hold type A (%s)",
          ui_SettingsValueText(UI_OPT_HOLD_TYPE));
    press(0x2000);
    CHECK(optionControlType == 1 &&
              strcmp(ui_SettingsValueText(UI_OPT_HOLD_TYPE), "B (toggle)") == 0,
          "Right: hold type B (%d)", optionControlType);
    const int hnote = noteStarting(UI_PAGE_CONTROLS, "A: Yorda");
    CHECK(hnote >= 0 && !lt_ext_Prop(hnote)->masked, "the Hold type note on the cursor");
    press(0x2000);
    CHECK(optionControlType == 0, "Right again: A");
    press(0x1000);
    press(0x1000);
    CHECK(lt_ext_Layout(ctlL)->curItem == bc, "up twice: Button configuration");
    press(0x40);
    CHECK(settle(59, 60), "Cross: the game's button configuration (%d)", current_layout_id);
    /* la_key_config's OK returns what ui_SettingsKeyConfigBack gives */
    const int to = ui_SettingsKeyConfigBack();
    CHECK(to == ctlL && lt_ext_Layout(ctlL)->defaultItem == bc,
          "its OK: Controls with the cursor on the row (%d)", to);
    lt_switch_layout(to);
    CHECK(settle(ctlL, 60) && lt_ext_Layout(ctlL)->curItem == bc, "back on Button configuration");
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    press(0x10);
    CHECK(settle(57, 60) && texLayout[57].curItem == 294, "the menu: back to Options");

    /* End Game, then the title's Settings > Controls: the cursor on Remap,
       not on the Button configuration row the OK above left as Controls'
       default (hidden from the title; its Cross would open 59) */
    stage_no = 1;
    lt_switch_layout(13);
    CHECK(settle(13, 60), "End Game: the title");
    texLayout[13].curItem = ui_SettingsEntryRow(13);
    press(0x40);
    CHECK(settle(mainL, 60), "the title's Settings");
    openPage(mainL, 3, UI_PAGE_CONTROLS);
    const int remap = ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_LINK);
    CHECK(lt_ext_Layout(ctlL)->curItem == remap, "title: Controls opens on Remap (%d, not %d)",
          lt_ext_Layout(ctlL)->curItem, bc);
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_REMAP), 60) && current_layout_id != 59,
          "title: Cross opens Remap, not 59 (%d)", current_layout_id);
    press(0x10);
    CHECK(settle(ctlL, 60), "Remap: back");
    /* a hidden row the cursor lands on anyway (a default set behind the
       entry's back) moves to the first shown row; its links skip the
       hidden rows */
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    lt_ext_Layout(ctlL)->defaultItem = bc;
    openPage(mainL, 3, UI_PAGE_CONTROLS);
    frame(0);
    CHECK(lt_ext_Layout(ctlL)->curItem == remap && lt_ext_Layout(ctlL)->defaultItem == remap,
          "title: off the hidden row (%d)", lt_ext_Layout(ctlL)->curItem);
    CHECK(lt_ext_Prop(bc)->upItem == remap &&
              lt_ext_Prop(bc)->downItem == ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_MOUSE_SENS),
          "title: the hidden row's links lead to shown rows (%d, %d)", lt_ext_Prop(bc)->upItem,
          lt_ext_Prop(bc)->downItem);
    for (int i = 0; i < 12; i++) {
        press(0x4000);
        CHECK(rowShown(UI_PAGE_CONTROLS, UI_OPT_LINK) &&
                  !lt_ext_Prop(lt_ext_Layout(ctlL)->curItem)->defaultMask,
              "title: Down %d on a shown row (%d)", i, lt_ext_Layout(ctlL)->curItem);
    }
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    stage_no = 11;
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu again");

    /* Brightness: 0..14 a step at a time, no wrap (la_adjust_screen) */
    systemStatus[11] = 7;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_BRIGHTNESS), "7 (Default)") == 0,
          "brightness 7, the default (%s)", ui_SettingsValueText(UI_OPT_BRIGHTNESS));
    ui_SettingsStep(UI_OPT_BRIGHTNESS, 1);
    CHECK(systemStatus[11] == 8 && strcmp(ui_SettingsValueText(UI_OPT_BRIGHTNESS), "8") == 0,
          "brightness 8 (%s)", ui_SettingsValueText(UI_OPT_BRIGHTNESS));
    for (int i = 0; i < 10; i++) {
        ui_SettingsStep(UI_OPT_BRIGHTNESS, 1);
    }
    CHECK(systemStatus[11] == 14, "brightness stops at 14 (%d)", systemStatus[11]);
    for (int i = 0; i < 20; i++) {
        ui_SettingsStep(UI_OPT_BRIGHTNESS, -1);
    }
    CHECK(systemStatus[11] == 0 && strcmp(ui_SettingsValueText(UI_OPT_BRIGHTNESS), "0") == 0,
          "brightness stops at 0 (%d)", systemStatus[11]);
    /* Square on the row: the default again (the adjust screen's Default) */
    {
        const int mainB = enterMain(0);
        const int dispL = openPage(mainB, 0, UI_PAGE_DISPLAY);
        lt_ext_Layout(dispL)->curItem = ui_SettingsRowOf(UI_PAGE_DISPLAY, UI_OPT_BRIGHTNESS);
        frame(0);
        press(0x0080);
        CHECK(systemStatus[11] == 7 && settle(dispL, 4), "Square: brightness 7 again (%d)",
              systemStatus[11]);
        systemStatus[11] = 3;
        lt_ext_Layout(dispL)->curItem = ui_SettingsRowOf(UI_PAGE_DISPLAY, UI_OPT_VIDEO_MODE);
        press(0x0080);
        CHECK(systemStatus[11] == 3, "Square on another row: no reset (%d)", systemStatus[11]);
        press(0x10);
        CHECK(settle(mainB, 60), "Display: back");
    }
    systemStatus[11] = 0;

    /* the game's variables, never the port config */
    CHECK(ico_config_get_string("video.brightness", NULL) == NULL &&
              ico_config_get_string("input.vibration", NULL) == NULL,
          "no port keys for the game's settings");
    /* the strings in every language */
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    static const int kIds[] = {
        UI_STR_OPT_BRIGHTNESS,    UI_STR_OPT_VIBRATION,     UI_STR_OPT_HOLD_TYPE,
        UI_STR_OPT_BUTTON_CONFIG, UI_STR_OPT_FILM_EFFECT,   UI_STR_OPT_PLAYERS,
        UI_STR_VAL_HOLD_A,        UI_STR_VAL_HOLD_B,        UI_STR_HOLD_TYPE_NOTE,
        UI_STR_PLAYERS_NOTE,      UI_STR_BUTTON_CONFIG_NOTE};
    for (int i = 0; i < 5; i++) {
        for (unsigned k = 0; k < sizeof(kIds) / sizeof(kIds[0]); k++) {
            CHECK(ui_StrIn(kLangs[i], (UiStrId)kIds[k])[0] != '\0', "string %d in language %d",
                  kIds[k], i);
        }
    }
    gFlagGameClear = 0;
    stage_no = 0;
}

static void testList(void)
{
    testListWrap(20);
    testListWrap(5);

    int count = 20;
    UiListDef def = {tlCount, tlFill, tlHeading, NULL, tlDecorate};
    UiListStyle st;
    UiList l;
    LtProp lay;
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    lt_ext_Reset();
    ui_SettingsReset();
    ui_ListBuild(&l, &def, &count, &st);
    CHECK(lt_ext_PropCount() == UI_LIST_SLOTS * 2 + 1 && l.colB[0] == -1,
          "the list adds two rows a slot and the status line (%d)", lt_ext_PropCount());
    memset(&lay, 0, sizeof(lay));

    /* refresh counts: every slot filled from its item, empty past the end */
    s_fills = 0;
    ui_ListRefresh(&l, -1);
    CHECK(s_fills == UI_LIST_SLOTS && strcmp(lt_ext_RowText(l.label[0]), "item 0") == 0 &&
              strcmp(lt_ext_RowText(l.label[7]), "item 7") == 0 &&
              strcmp(lt_ext_RowText(l.colA[2]), "On") == 0 && s_hdrDecorated == -1,
          "refresh fills %d slots (%d)", UI_LIST_SLOTS, s_fills);
    count = 5;
    s_fills = 0;
    ui_ListRefresh(&l, l.label[3]);
    CHECK(s_fills == 5 && ui_ListShown(&l) == 5 &&
              strcmp(lt_ext_RowText(l.label[4]), "item 4") == 0 &&
              lt_ext_RowText(l.label[5])[0] == '\0' && lt_ext_RowText(l.colA[7])[0] == '\0' &&
              s_hdrDecorated == 3 && ui_ListItemAt(&l, 6) == -1,
          "5 items: 5 fills, the rest empty (%d)", s_fills);
    count = 0;
    s_fills = 0;
    ui_ListRefresh(&l, -1);
    CHECK(s_fills == 0 && ui_ListShown(&l) == 0 && lt_ext_RowText(l.label[0])[0] == '\0',
          "no items: no fills");
    count = 20;

    /* the cursor starts on item 1 (item 0 is a heading); Down to item 4 */
    ui_ListReset(&l);
    ui_ListRefresh(&l, -1);
    lay.curItem = l.label[1];
    for (int i = 0; i < 3; i++) {
        listStep(&l, &lay, 0x4000);
    }
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 4, "item 4 (%d)", ui_ListItemOfRow(&l, lay.curItem));
    /* Down onto the headings 5 and 6: the next tick goes on to 7 */
    listStep(&l, &lay, 0x4000);
    listStep(&l, &lay, 0);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 7 && l.offset == 0,
          "Down skips the headings 5 and 6 (%d)", ui_ListItemOfRow(&l, lay.curItem));
    /* Up from there: 6 and 5 are headings, back to 4 */
    listStep(&l, &lay, 0x1000);
    listStep(&l, &lay, 0);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 4, "Up skips them (%d)",
          ui_ListItemOfRow(&l, lay.curItem));
    /* the last slot scrolls the window */
    lay.curItem = l.label[7];
    listStep(&l, &lay, 0);
    int beforeScroll = l.offset;
    listStep(&l, &lay, 0x4000);
    CHECK(l.offset == beforeScroll + 1 && ui_ListItemOfRow(&l, lay.curItem) == 8,
          "Down at the last slot scrolls by one (offset %d, item %d)", l.offset,
          ui_ListItemOfRow(&l, lay.curItem));
    CHECK(strcmp(lt_ext_RowText(l.label[0]), "item 1") == 0, "the window shows item 1 first");
    /* the end: 18, then the heading 19, wrapping to 1 (0 is a heading) */
    lay.curItem = l.label[6];
    l.offset = 12; /* items 12..19 */
    ui_ListRefresh(&l, lay.curItem);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 18, "item 18");
    listStep(&l, &lay, 0x4000);
    listStep(&l, &lay, 0);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 1 && l.offset == 0,
          "Down past the last heading wraps to item 1 (item %d, offset %d)",
          ui_ListItemOfRow(&l, lay.curItem), l.offset);
    /* Up from item 1: the heading 0, then the wrap to 18 under the heading 19 */
    listStep(&l, &lay, 0x1000);
    listStep(&l, &lay, 0);
    CHECK(ui_ListItemOfRow(&l, lay.curItem) == 18 && l.offset == 12,
          "Up past the first heading wraps to item 18 (item %d, offset %d)",
          ui_ListItemOfRow(&l, lay.curItem), l.offset);
    CHECK(strcmp(lt_ext_RowText(l.label[7]), "item 19") == 0,
          "the last window ends on the heading");
    /* Cross and Triangle do not move or scroll the list */
    int off = l.offset, cur = lay.curItem;
    listStep(&l, &lay, 0x40 | 0x4000);
    CHECK(l.offset == off && lay.curItem == cur, "Cross does not scroll");
}

/* package L1: ui_SettingsCoversTitle, which hides the title's logo
   (port/game/title_logo.c): every page opened from the title, Extras and
   its Music page among them; not the title's own layouts, the mirror or
   quit screens, nor any page opened from the pause menu */
static void testCoversTitle(void)
{
    for (int title = 0; title < 2; title++) {
        int mainL = enterMain(title);
        CHECK(ui_SettingsCoversTitle() == title, "title %d: the main page", title);
        for (int p = 0; p < UI_PAGE_COUNT; p++) {
            int l = ui_SettingsPageLayout((UiSettingsPage)p);
            CHECK(l >= 0, "page %d has a layout", p);
            lt_switch_layout(l);
            CHECK(settle(l, 60), "page %d up", p);
            CHECK(ui_SettingsCoversTitle() == title, "title %d: page %d covers %d", title, p,
                  ui_SettingsCoversTitle());
        }
        int others[3] = {title ? 13 : 57, ui_QuitScreenLayout(), ui_NewGameScreenLayout()};
        for (int k = 0; k < 3; k++) {
            if (others[k] < 0) {
                continue;
            }
            lt_switch_layout(others[k]);
            CHECK(settle(others[k], 60), "layout %d up", others[k]);
            CHECK(!ui_SettingsCoversTitle(), "title %d: layout %d does not cover", title,
                  others[k]);
        }
        /* the menu again from where it was entered: covering again */
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60) && ui_SettingsCoversTitle() == title, "title %d: the menu again",
              title);
    }
}

#ifdef SETTINGS_RENDER

/* SCENE (512 x 512 for the 640 x 448 grid) to a PNG at 4:3, 683 x 512 */
static void snap(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4), *out = malloc(683 * 512 * 4);
    if (!px || !out || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h)) {
        CHECK(0, "SCENE readback for %s", name);
        free(px);
        free(out);
        return;
    }
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 683; x++) {
            memcpy(&out[(y * 683 + x) * 4], &px[(y * 512 + x * 512 / 683) * 4], 4);
        }
    }
    path(p, sizeof(p), name);
    rd_WritePng(p, out, 683, 512, 683 * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
    free(out);
}

/* T1: SCENE at the Enhanced 4x scale (2048 x 2048) to a PNG at 4:3,
   2731 x 2048: the port font's rows as a 4x output shows them */
static void snap4(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)2048 * 2048 * 4;
    uint8_t *px = malloc(n), *out = malloc((size_t)2731 * 2048 * 4);
    if (!px || !out || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, n, &w, &h) || w != 2048 ||
        h != 2048) {
        CHECK(0, "4x SCENE readback for %s (%ux%u)", name, w, h);
        free(px);
        free(out);
        return;
    }
    for (int y = 0; y < 2048; y++) {
        for (int x = 0; x < 2731; x++) {
            memcpy(&out[((size_t)y * 2731 + (size_t)x) * 4],
                   &px[((size_t)y * 2048 + (size_t)x * 2048 / 2731) * 4], 4);
        }
    }
    path(p, sizeof(p), name);
    rd_WritePng(p, out, 2731, 2048, 2731 * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
    free(out);
}

/* The button glyphs' sheets: no disc data here, so drawn stand-ins with the
   real sheets' layout (buttons.tm2's triangle, square, circle and cross in
   32 x 30 cells; menu_PAL_01 / 02's L1, R1 and the value arrows at their
   rectangles), bound by tex_TransTexture through a TEX0 the resolver maps:
   the snapshots show where and how large the glyphs are, the window build's
   dumps show the game's own. */
#define FAKE_TBP 0x1000
static RdTex s_sheet[2];

static void plot(uint8_t *px, int w, int x, int y, const uint8_t c[3])
{
    uint8_t *p = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
    p[3] = 0x80;
}

/* a thick segment (a, b) inside the box x0..x1, y0..y1 */
static void segment(uint8_t *px, int w, float ax, float ay, float bx, float by, float th,
                    const uint8_t c[3])
{
    int x0 = (int)(ax < bx ? ax : bx) - 3, x1 = (int)(ax > bx ? ax : bx) + 3;
    int y0 = (int)(ay < by ? ay : by) - 3, y1 = (int)(ay > by ? ay : by) + 3;
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float t = l2 > 0.0f ? ((x + 0.5f - ax) * dx + (y + 0.5f - ay) * dy) / l2 : 0.0f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            float ex = ax + t * dx - (x + 0.5f), ey = ay + t * dy - (y + 0.5f);
            if (ex * ex + ey * ey <= th * th * 0.25f) {
                plot(px, w, x, y, c);
            }
        }
    }
}

static void makeSheets(void)
{
    static uint8_t b[64 * 64 * 4], m[512 * 256 * 4];
    static const uint8_t green[3] = {0x30, 0xE0, 0x90}, pink[3] = {0xE8, 0xA0, 0xC0},
                         red[3] = {0xF0, 0x78, 0x60}, blue[3] = {0x78, 0x98, 0xE8},
                         white[3] = {0xE8, 0xE8, 0xE8};
    memset(b, 0, sizeof(b));
    memset(m, 0, sizeof(m));
    segment(b, 64, 16, 5, 4, 26, 3, green);
    segment(b, 64, 4, 26, 28, 26, 3, green);
    segment(b, 64, 28, 26, 16, 5, 3, green);
    segment(b, 64, 38, 5, 58, 5, 3, pink);
    segment(b, 64, 58, 5, 58, 25, 3, pink);
    segment(b, 64, 58, 25, 38, 25, 3, pink);
    segment(b, 64, 38, 25, 38, 5, 3, pink);
    for (int k = 0; k < 32; k++) {
        float a0 = (float)k * 6.2831853f / 32.0f, a1 = (float)(k + 1) * 6.2831853f / 32.0f;
        segment(b, 64, 16 + 10 * cosf(a0), 45 + 10 * sinf(a0), 16 + 10 * cosf(a1),
                45 + 10 * sinf(a1), 3, red);
    }
    segment(b, 64, 38, 35, 58, 55, 3, blue);
    segment(b, 64, 58, 35, 38, 55, 3, blue);
    /* L1 (420, 240) and R1 (340, 240), 40 x 15: the letters as strokes */
    for (int k = 0; k < 2; k++) {
        float x = k == 0 ? 428.0f : 348.0f;
        if (k == 0) {
            segment(m, 512, x, 242, x, 252, 2, white); /* L */
            segment(m, 512, x, 252, x + 7, 252, 2, white);
        } else {
            segment(m, 512, x, 242, x, 252, 2, white); /* R */
            segment(m, 512, x, 242, x + 6, 242, 2, white);
            segment(m, 512, x + 6, 242, x + 6, 247, 2, white);
            segment(m, 512, x + 6, 247, x, 247, 2, white);
            segment(m, 512, x + 2, 247, x + 7, 252, 2, white);
        }
        segment(m, 512, x + 14, 244, x + 17, 242, 2, white); /* 1 */
        segment(m, 512, x + 17, 242, x + 17, 252, 2, white);
    }
    /* the value arrows (490, 130) and (490, 150), 20 x 20 */
    segment(m, 512, 504, 133, 496, 140, 2, white);
    segment(m, 512, 496, 140, 504, 147, 2, white);
    segment(m, 512, 496, 153, 504, 160, 2, white);
    segment(m, 512, 504, 160, 496, 167, 2, white);
    /* the pause menu's Options (384, 120), 128 x 20: the English word's
       extent on the sheet (texels 7 to 84), a framed bar with a cross */
    segment(m, 512, 392, 124, 468, 124, 2, white);
    segment(m, 512, 392, 136, 468, 136, 2, white);
    segment(m, 512, 392, 124, 392, 136, 2, white);
    segment(m, 512, 468, 124, 468, 136, 2, white);
    segment(m, 512, 392, 124, 468, 136, 2, red);
    s_sheet[0] = rd_CreateTexture(64, 64, b, RD_TEXA_80_80, "settings_render buttons");
    s_sheet[1] = rd_CreateTexture(512, 256, m, RD_TEXA_80_80, "settings_render menu sheet");
}

static RdTex sheetResolve(unsigned long long tex0, int list)
{
    (void)list;
    unsigned tbp = (unsigned)(tex0 & 0x3FFF);
    return tbp == FAKE_TBP + 0x40                             ? s_sheet[0]
           : tbp >= FAKE_TBP + 0x80 && tbp <= FAKE_TBP + 0xC0 ? s_sheet[1]
                                                              : (RdTex){0};
}

static void bindFakeSheet(int no)
{
    if (no < 1 || no > 3) {
        return;
    }
    const unsigned long long w = no == 1 ? 64 : 512, h = no == 1 ? 64 : 256;
    const unsigned long long tw = no == 1 ? 6 : 9, th = no == 1 ? 6 : 8;
    unsigned long long tex0 = (unsigned long long)(FAKE_TBP + 0x40 * no) | (w / 64) << 14 |
                              tw << 26 | th << 30 | 1ull << 34;
    (void)h;
    gif_StartPacketPri(11);
    gif_SetGsReg(0x06, (long long)tex0);
    gif_EndPacket();
}

/* the model viewer's prompts and top-left rows as port/game/model_viewer.c
   lays them out (its layout needs the game; the prompt lines are
   ui_hint.c's own), over no model */
static int viewerLayout(void)
{
    static UiHint sticks, keys;
    int first = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    int name = ui_SettingsAddRow(24, 10, 360, 30, 0, -1, 0, "Ico", 24.0f, UI_ALIGN_LEFT);
    ui_SettingsAddRow(24, 26, 360, 30, 0, -1, 0, "Animation: BOY STAND  \xC2\xB7  Loop", 19.0f,
                      UI_ALIGN_LEFT);
    ui_SettingsAddRow(24, 38, 360, 30, 0, -1, 0, "Frame 117 / 299", 19.0f, UI_ALIGN_LEFT);
    ui_HintBuild(&sticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_HintBuild(&keys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    l.colA = 0.0f;
    l.procFirst = 1;
    l.defaultItem = l.curItem = -1;
    l.link = -1;
    (void)name;
    return lt_ext_AddLayout(&l);
}

/* package DEF: the RDC_OVERLAY_TEXT items of the last frame */
static int textItems(void)
{
    const RdFrame *f = rd__LastFrame();
    int n = 0;
    for (int l = 0; f && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            n += f->lists[l].cmds[i].type == RDC_OVERLAY_TEXT &&
                 f->lists[l].cmds[i].b[0] == RD_OTEXT_ITEM;
        }
    }
    return n;
}

/* package DEF: the screen presented at Enhanced 1920 x 1080 (16:9) to
   name_1080.png: the port's rows deferred (drawn on the output at its
   resolution), the game's rows their texture sprites.  wantItems: whether
   the screen has port text (some RDC_OVERLAY_TEXT items) or game rows
   alone (none: package TXT2, the game's words are never text) */
static void snap1080(const char *name, int wantItems)
{
    const uint32_t w = 1920, h = 1080;
    uint8_t *px = malloc((size_t)w * h * 4);
    char file[256], p[1400];
    frame(0);
    const int items = textItems();
    CHECK(wantItems ? items > 0 : items == 0, "%s: %d deferred items (%s)", name, items,
          wantItems ? "some" : "none");
    uint32_t ow = 0, oh = 0;
    if (!px || !rd_ReadPresented(px, &ow, &oh) || ow != w || oh != h) {
        CHECK(0, "%s: the presented output", name);
        free(px);
        return;
    }
    snprintf(file, sizeof(file), "%s_1080.png", name);
    path(p, sizeof(p), file);
    rd_WritePng(p, px, w, h, w * 4, 0);
    printf("settings_render: %s (%d items)\n", p, items);
    free(px);
}

/* the save screen's values: layout 14's
   slot numbers (files 1, 2 and 5 used, the others empty) and layout 15's
   play time "12:34:56", at the PAL rows' places, their texel rectangles the
   menu text table's, everything else of the two layouts masked.  Package
   TXT2: game rows, so their texture sprites and no text item (the fake
   tables have no sheet for them: the shot shows nothing of the figures) */
static void fakeSaveRows(void)
{
    for (int r = 52; r < 176; r++) {
        setRow(r, -1, -1, -1, -1, -1, -1, 0);
        texProperty[r].masked = 1;
        texProperty[r].selectable = 0;
    }
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        const int r = ui_menu_text_rows[i].row;
        if (r >= 52 && r < 176) {
            const UiMenuTextItem *it = &ui_menu_text_items[ui_menu_text_rows[i].item];
            texProperty[r].texU = it->u;
            texProperty[r].texV = it->v;
            texProperty[r].texW = it->w;
            texProperty[r].texH = it->h;
            texProperty[r].dispW = 0;
            texProperty[r].dispH = 30;
        }
    }
    static const int slotX[5] = {210, 260, 310, 360, 410};
    for (int i = 0; i < 10; i++) {
        const int used = i == 0 || i == 1 || i == 4;
        LtProperty *e = &texProperty[(used ? 62 : 52) + i];
        e->dispX = i == 9 ? 404 : slotX[i % 5];
        e->dispY = i < 5 ? 70 : 90;
        e->masked = 0;
    }
    /* layout_action.c _la_set_preview_info: digit d of a pair at row base +
       (d + 9) % 10; hours 76 / 86, minutes 96 / 106, seconds 116 / 126,
       the colons 74 and 75 */
    static const int base[6] = {76, 86, 96, 106, 116, 126};
    static const int pos[6] = {240, 260, 300, 320, 360, 380};
    static const int digits[6] = {1, 2, 3, 4, 5, 6};
    for (int k = 0; k < 6; k++) {
        LtProperty *e = &texProperty[base[k] + (digits[k] + 9) % 10];
        e->dispX = pos[k];
        e->dispY = 160;
        e->masked = 0;
    }
    texProperty[74].dispX = 280;
    texProperty[75].dispX = 340;
    texProperty[74].dispY = texProperty[75].dispY = 160;
    texProperty[74].masked = texProperty[75].masked = 0;
    for (int r = 52; r < 176; r++) {
        texProperty[r].defaultMask = texProperty[r].masked; /* kept by the switch */
    }
    setLayout(14, 52, 74, -1, 15);
    setLayout(15, 74, 176, -1, -1);
    texLayout[14].fadeInTime = texLayout[15].fadeInTime = 0.0f;
}

/* GFONT: the game face the font_coverage test built from the disc
   (gamefont.bin beside the snapshots, fixture gamefont); without it the
   port's text is Arimo alone, as without a disc */
static void loadGameFace(void)
{
    char p[1100];
    path(p, sizeof(p), "gamefont.bin");
    FILE *f = fopen(p, "rb");
    if (!f) {
        printf("settings_render: no %s (no disc): the text is Arimo alone\n", p);
        return;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *blob = n > 0 ? malloc((size_t)n) : NULL;
    if (blob && fread(blob, 1, (size_t)n, f) == (size_t)n) {
        CHECK(ui_GameFaceLoad(blob, (size_t)n), "the game face in %s loads", p);
        printf("settings_render: the game face from %s\n", p);
    }
    free(blob);
    fclose(f);
}

static int render(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("settings_render: SKIP: no usable Vulkan device\n");
        return 77;
    }
    ui_FontForgetTextures();
    loadGameFace();
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_SetGsFrame(&fr);
    ui_SetScale(1.0f);
    ui__SetRecordHook(gif_HostFlush);
    makeSheets();
    gif_HostSetTex0Resolver(sheetResolve);
    dl_Init();
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    init_layout_texture(2);
    settle(54, 4);
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    lt_switch_layout(mainL);
    CHECK(settle(mainL, 60), "the menu");
    for (int i = 0; i < 4; i++) {
        press(0x4000); /* to Language: its note */
    }
    frame(0);
    snap("settings_main.png");

    static const struct {
        int row; /* main page row */
        int downs;
        const char *name;
    } pages[] = {{0, 0, "settings_display.png"},     {0, 8, "settings_display_videomode.png"},
                 {1, 0, "settings_effects.png"},     {2, 0, "settings_audio.png"},
                 {3, 1, "settings_controls.png"},    {4, 0, "settings_gameplay.png"},
                 {6, 0, "settings_achievements.png"}};

    int labels[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        lt_ext_Layout(mainL)->curItem = labels[pages[i].row];
        press(0x40);
        for (int k = 0; k < 30; k++) {
            frame(0);
        }
        for (int k = 0; k < pages[i].downs; k++) {
            press(0x4000);
        }
        frame(0);
        snap(pages[i].name);
        press(0x10);
        CHECK(settle(mainL, 60), "back to the menu from %s", pages[i].name);
    }
    /* Audio with a device name too long for its box (cut with an
       ellipsis) and Mono set, the cursor on the device row */
    ico_config_set_string("audio.device",
                          "Speakers (USB Audio Interface with a very long product name)");
    ico_config_set_string("audio.output", "mono");
    ico_opt_reload();
    lt_ext_Layout(mainL)->curItem = labels[2];
    press(0x40);
    for (int k = 0; k < 30; k++) {
        frame(0);
    }
    for (int k = 0; k < 4; k++) {
        press(0x4000);
    }
    frame(0);
    snap("settings_audio_device.png");
    press(0x10);
    CHECK(settle(mainL, 60), "back to the menu from Audio");
    ico_config_set_string("audio.device", "");
    ico_config_set_string("audio.output", "auto");
    ico_opt_reload();
    /* Controls -> Remap, then a capture in progress */
    lt_ext_Layout(mainL)->curItem = labels[3];
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_CONTROLS), 60), "Controls");
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_REMAP), 60), "Remap");
    frame(0);
    snap("settings_remap.png");
    NonLinearCameraMove = 3; /* French: the gamepad column's names */
    frame(0);
    snap("settings_remap_fr.png");
    NonLinearCameraMove = 2;
    frame(0);
    press(0x4000);
    press(0x40);
    frame(0);
    snap("settings_remap_capture.png");
    /* R7c: the New Game screen, the cursor on Mirror mode's On, New Game+
       Off lit; then the cursor on New Game+ (its note), Mirror mode On lit */
    int ml = ui_NewGameScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen");
    press(0x2000);
    frame(0);
    snap("settings_new_game_screen.png");
    press(0x4000);
    frame(0);
    snap("settings_new_game_screen_ngp.png");
    NonLinearCameraMove = 3; /* French: the longest note */
    frame(0);
    frame(0);
    snap("settings_new_game_screen_ngp_fr.png");
    NonLinearCameraMove = 2;
    frame(0);
    /* Q2: the quit confirmation, the cursor on Yes */
    int ql = ui_QuitScreenLayout();
    lt_switch_layout(ql);
    CHECK(settle(ql, 60), "the quit screen");
    press(0x8000);
    frame(0);
    snap("settings_quit_screen.png");
    /* the title (New Game only): Options, the pause menu's word centred
       (the stand-in's framed bar), Quit to desktop under it */
    lt_switch_layout(13);
    CHECK(settle(13, 60), "the title");
    {
        /* shown, as the title's proc (la_title) shows them once its menu
           is up */
        const int opt = ui_SettingsEntryRow(13), quit = ui_SettingsQuitRow(13);
        lt_ext_Prop(opt)->defaultMask = lt_ext_Prop(quit)->defaultMask = 0;
        ui_SettingsTitleMask(0);
        texLayout[13].curItem = opt;
        frame(0);
        snap("settings_title_entry.png");
        lt_ext_Prop(opt)->defaultMask = lt_ext_Prop(quit)->defaultMask = 1;
        ui_SettingsTitleMask(1);
    }
    /* T1: the Settings and Display screens at Enhanced 4x, full height, the
       atlas at a 960-line output's scale (settings_main_4x.png,
       settings_display_4x.png) */
    {
        RdSettings e = *rd_GetSettings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 4.0f;
        e.aspect = 4.0f / 3.0f;
        e.fullHeightScene = 1;
        e.outputWidth = 1280;
        e.outputHeight = 960;
        rd_SetSettings(&e);
        ui_SetScale(ui_ScaleFor(1, e.outputHeight));
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 4x");
        lt_ext_Layout(mainL)->curItem = labels[5]; /* Language: its note */
        frame(0);
        frame(0);
        snap4("settings_main_4x.png");
        lt_ext_Layout(mainL)->curItem = labels[0];
        press(0x40);
        CHECK(settle(ui_SettingsPageLayout(UI_PAGE_DISPLAY), 60), "Display at 4x");
        frame(0);
        snap4("settings_display_4x.png");
    }
    /* the title entry (last: it makes Extras part of the main page): the
       main page with its Extras row, and the Extras page with the cursor on
       the locked Credits row, at the same 4x */
    {
        lt_switch_layout(13);
        CHECK(settle(13, 60), "the title");
        press(0x4000);
        press(0x40);
        CHECK(settle(mainL, 60), "the menu from the title");
        int ml[16];
        ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
        lt_ext_Layout(mainL)->curItem = ml[6];
        frame(0);
        frame(0);
        snap4("settings_main_title_4x.png");
        lt_ext_Layout(mainL)->curItem = ml[7];
        press(0x40);
        int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "Extras at 4x");
        press(0x4000);
        press(0x4000);
        frame(0);
        snap4("settings_extras_4x.png");
        /* the music gallery over the fake engine, the cursor on an entry
           that plays */
        gallery_SetEngine(&kFakeEngine);
        lt_ext_Layout(exL)->curItem = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
        frame(0);
        press(0x40);
        int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
        CHECK(settle(galL, 60), "the gallery at 4x");
        press(0x40);
        frame(0);
        snap4("settings_music_4x.png");
        press(0x10); /* Back leaves for Extras, the Music row under the cursor */
        CHECK(settle(exL, 60), "back to Extras from the gallery at 4x");
        gallery_SetEngine(NULL);
    }
    /* package DEF: every screen presented at Enhanced 1080p (snap1080) */
    {
        RdSettings e = *rd_GetSettings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 0.0f;
        e.sceneWidth = e.sceneHeight = 0;
        e.aspect = 16.0f / 9.0f;
        e.fullHeightScene = 0;
        e.outputWidth = 1920;
        e.outputHeight = 1080;
        rd_SetSettings(&e);
        ui_SetScale(ui_ScaleFor(1, e.outputHeight));
        ui_InstallDeferredText(1);
        s_reduce = 1;
        int ml[16];
        ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 1080p");
        lt_ext_Layout(mainL)->curItem = ml[5];
        frame(0);
        snap1080("settings_main", 1);

        static const struct {
            int row, downs;
            const char *name;
        } pg[] = {{0, 0, "settings_display"},  {1, 0, "settings_effects"},
                  {2, 0, "settings_audio"},    {3, 1, "settings_controls"},
                  {4, 0, "settings_gameplay"}, {6, 0, "settings_achievements"},
                  {7, 2, "settings_extras"}};

        for (unsigned i = 0; i < sizeof(pg) / sizeof(pg[0]); i++) {
            lt_ext_Layout(mainL)->curItem = ml[pg[i].row];
            press(0x40);
            for (int k = 0; k < 30; k++) {
                frame(0);
            }
            for (int k = 0; k < pg[i].downs; k++) {
                press(0x4000);
            }
            snap1080(pg[i].name, 1);
            press(0x10);
            CHECK(settle(mainL, 60), "back to the menu from %s at 1080p", pg[i].name);
        }
        int mir = ui_NewGameScreenEnter();
        lt_switch_layout(mir);
        CHECK(settle(mir, 60), "the New Game screen at 1080p");
        snap1080("settings_new_game_screen", 1);
        press(0x4000);
        snap1080("settings_new_game_screen_ngp", 1);
        /* P6: the pause menu with the journey's lines on its right (a
           stage running, an assist on, New Game+ on), then in French (the
           longest labels) */
        {
            ico_gs_reset();
            ico_gs_set_sampler(gsSampler);
            memset(&s_gs, 0, sizeof(s_gs));
            s_gs.valid = 1;
            s_gs.stage_no = 11;
            s_gs.system_status[0] = 1;
            s_gs.system_status[1] = 2;
            s_gs.layout = 57;
            s_gs.mc_preview[2] = (2 * 3600 + 12 * 60 + 34) * 50;
            ico_gs_tick();
            for (int i = 0; i < 3; i++) {
                ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
            }
            for (int i = 0; i < 41; i++) {
                ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, i);
            }
            ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 0);
            ico_gs_tick();
            stage_no = 11;
            gFlagGameClear = 1;
            ico_opt_set_yorda_safe(1);
            lt_switch_layout(57);
            CHECK(settle(57, 60), "the pause menu at 1080p");
            snap1080("settings_pause_stats", 1);
            NonLinearCameraMove = 3;
            frame(0);
            snap1080("settings_pause_stats_fr", 1);
            NonLinearCameraMove = 2;
            ico_opt_set_yorda_safe(0);
            gFlagGameClear = 0;
            stage_no = 0;
            ico_gs_set_sampler(NULL);
            ico_gs_reset();
        }
        int ql = ui_QuitScreenLayout();
        lt_switch_layout(ql);
        CHECK(settle(ql, 60), "the quit screen at 1080p");
        press(0x8000);
        snap1080("settings_quit_screen", 1);
        /* the music gallery with a stream playing (the fake engine: 42 s
           of 4:25), its bar and transport; then the model viewer's rows
           and prompts */
        {
            gallery_SetEngine(&kFakeEngine);
            lt_switch_layout(13);
            CHECK(settle(13, 60), "the title at 1080p");
            press(0x4000);
            press(0x40);
            CHECK(settle(mainL, 60), "the menu from the title at 1080p");
            lt_ext_Layout(mainL)->curItem = ml[7];
            press(0x40);
            int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
            CHECK(settle(exL, 60), "Extras at 1080p");
            lt_ext_Layout(exL)->curItem = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
            frame(0);
            press(0x40);
            int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
            CHECK(settle(galL, 60), "the gallery at 1080p");
            press(0x40);
            snap1080("settings_music", 1);
            press(0x10);
            CHECK(settle(exL, 60), "back to Extras at 1080p");
            gallery_SetEngine(NULL);
            int vl = viewerLayout();
            lt_switch_layout(vl);
            CHECK(settle(vl, 60), "the viewer's rows at 1080p");
            snap1080("settings_viewer", 1);
        }
        /* package TXT: the save screen's slot numbers and play time */
        fakeSaveRows();
        lt_switch_layout(14);
        CHECK(settle(14, 60), "the save screen at 1080p");
        snap1080("settings_save_preview", 0);
        s_reduce = 0;
        ui_InstallDeferredText(0);
    }
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded writes", gif_HostUndecodedTotal());
    ui__SetRecordHook(NULL);
    ui_FontShutdown();
    rd_Shutdown();
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    return render();
}

#else

/* v0.4.0: Display > Texture pack ("None installed" without a pack, the
   step then doing nothing; On/Off with one), its note, Dump textures on
   the main page in developer mode only, and the [video] keys' round trip
   through the config. */
static int s_packCount;

static int fakePackCount(void)
{
    return s_packCount;
}

/* the texture pack note sits on the Display page, whose notes must stay one
   line to keep clear of Back: checked in all five languages */
static void testTexturePackNoteLines(void)
{
    int mainL = enterMain(1);
    int tn;
    s_packCount = 883;
    ui_SettingsSetTexturePackCount(fakePackCount);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    tn = rowWithPrefix(UI_PAGE_DISPLAY, "PCSX2 packs:");
    CHECK(tn >= 0, "the texture pack note on the Display page");
    for (int lang = 0; lang < UI_LANG_COUNT && tn >= 0; lang++) {
        const char *want = ui_StrIn((UiLang)lang, UI_STR_TEXTURE_PACK_NOTE);
        NonLinearCameraMove = 2 + lang; /* the game's language: 2 EN .. 6 ES */
        frame(0);
        CHECK(strncmp(lt_ext_RowText(tn), want, 8) == 0, "lang %d: the note is %s (%s)", lang, want,
              lt_ext_RowText(tn));
        CHECK(strchr(lt_ext_RowText(tn), '\n') == NULL, "lang %d: the note is one line: %s", lang,
              lt_ext_RowText(tn));
    }
    NonLinearCameraMove = 2;
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsSetTexturePackCount(NULL);
}

static void testTexturePack(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "no hook: None installed (%s)", ui_SettingsValueText(UI_OPT_TEXTURE_PACK));
    ui_SettingsSetTexturePackCount(fakePackCount);
    s_packCount = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "none found: None installed");
    ui_SettingsStep(UI_OPT_TEXTURE_PACK, 1);
    ico_video_get(&o);
    CHECK(o.texturePack == 1 &&
              strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "none installed: the step does nothing");
    s_packCount = 883;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "On") == 0, "a pack: On");
    ui_SettingsStep(UI_OPT_TEXTURE_PACK, 1);
    ico_video_get(&o);
    CHECK(o.texturePack == 0 && strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "Off") == 0,
          "Right: Off");
    ui_SettingsStep(UI_OPT_TEXTURE_PACK, -1);
    ico_video_get(&o);
    CHECK(o.texturePack == 1 && strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "On") == 0,
          "Left: On");
    {
        /* the Display page's notes are one line, clear of Back (as the CRT
           note); the main page's dump note at most two */
        int tn = rowWithPrefix(UI_PAGE_DISPLAY, "PCSX2 packs:");
        int dn = rowWithPrefix(UI_PAGE_MAIN, "For pack makers:");
        const char *nl;
        CHECK(tn >= 0, "the Display note");
        CHECK(tn >= 0 && strchr(lt_ext_RowText(tn), '\n') == NULL,
              "the Display note is one line: %s", tn >= 0 ? lt_ext_RowText(tn) : "");
        CHECK(dn >= 0, "the dump row's note");
        nl = dn >= 0 ? strchr(lt_ext_RowText(dn), '\n') : NULL;
        CHECK(nl == NULL || strchr(nl + 1, '\n') == NULL, "the dump note is at most two lines");
    }
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DUMP_TEXTURES), "Off") == 0, "dump: Off");
    ui_SettingsStep(UI_OPT_DUMP_TEXTURES, 1);
    ico_video_get(&o);
    CHECK(o.dumpTextures == 1 && strcmp(ui_SettingsValueText(UI_OPT_DUMP_TEXTURES), "On") == 0,
          "dump: On");

    /* the round trip: both keys saved, the config-only ones absent at
       their defaults */
    char p[1100];
    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.texture_pack", 0) == 1 &&
              ico_toml_get_bool(t, "video.dump_textures", 0) == 1 &&
              !ico_toml_has(t, "video.texture_pack_budget_mb") &&
              !ico_toml_has(t, "video.texture_pack_precache"),
          "saved: texture_pack, dump_textures; no budget or precache key");
    ico_toml_free(t);
    useConfig("version = 1\n[video]\ntexture_pack = false\ndump_textures = false\n"
              "texture_pack_budget_mb = 512\ntexture_pack_precache = false\n");
    ico_video_get(&o);
    CHECK(!o.texturePack && !o.dumpTextures && o.texturePackBudgetMb == 512 &&
              !o.texturePackPrecache,
          "read back: off, off, 512 MB, no precache");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_TEXTURE_PACK), "Off") == 0, "the row: Off");
    o.texturePackBudgetMb = 4096;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0, "save the budget");
    t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.texture_pack", 1) == 0 &&
              ico_toml_get_int(t, "video.texture_pack_budget_mb", 0) == 4096 &&
              ico_toml_get_bool(t, "video.texture_pack_precache", 1) == 0,
          "saved: off, 4096 MB, no precache");
    ico_toml_free(t);
    useConfig("version = 1\n[video]\ntexture_pack_budget_mb = 5\n");
    ico_video_get(&o);
    CHECK(o.texturePack && o.texturePackPrecache && o.texturePackBudgetMb == ICO_TEXPACK_BUDGET_MIN,
          "defaults on, a budget under the minimum clamped (%d)", o.texturePackBudgetMb);
    CHECK(o.texturePackCacheMb == 0, "the RAM cache's limit automatic by default (%d)",
          o.texturePackCacheMb);
    /* the RAM cache's limit, its own key: 0 or 128..65536, saved when set */
    useConfig("version = 1\n[video]\ntexture_pack_cache_mb = 5\n");
    ico_video_get(&o);
    CHECK(o.texturePackCacheMb == ICO_TEXPACK_CACHE_MIN && o.texturePackBudgetMb == 2048,
          "a cache limit under the minimum clamped (%d), the budget its own (%d)",
          o.texturePackCacheMb, o.texturePackBudgetMb);
    useConfig("version = 1\n[video]\ntexture_pack_cache_mb = 3000\n");
    ico_video_get(&o);
    CHECK(o.texturePackCacheMb == 3000, "cache limit 3000 MB read (%d)", o.texturePackCacheMb);
    o.texturePackCacheMb = 6000;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0, "save the cache limit");
    t = ico_toml_load(p);
    CHECK(t && ico_toml_get_int(t, "video.texture_pack_cache_mb", 0) == 6000 &&
              !ico_toml_has(t, "video.texture_pack_budget_mb"),
          "saved: 6000 MB of cache, no budget key");
    ico_toml_free(t);

    /* Developer mode off switches Dump textures off with it: its row hides,
       and the dumps must not go on being written with no row to stop them */
    useConfig("version = 1\n[video]\ndump_textures = true\n");
    ico_opt_set_developer_mode(1);
    ico_video_get(&o);
    CHECK(o.dumpTextures == 1, "dump on from the file");
    ui_SettingsStep(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(!ico_opt_developer_mode() && o.dumpTextures == 0, "Developer off: the dump off too");
    ui_SettingsStep(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(ico_opt_developer_mode() && o.dumpTextures == 0,
          "Developer on again: the dump stays off until chosen");
    ico_opt_set_developer_mode(0);

    /* Dump textures is shown in developer mode only */
    int mainL = enterMain(1);
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "dump row hidden");
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "dump row shown in developer mode");
    {
        int labels[16];
        const int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
        const LtProperty *back = lt_ext_Prop(labels[n - 1]);
        CHECK(back->dispY == 40 + 13 * 11, "twelve rows 13 lines apart: Back at %d", back->dispY);
        CHECK(back->dispY + back->dispH <= 226, "Back's box ends at %d", back->dispY + back->dispH);
        checkPageFits(UI_PAGE_MAIN, "Main (title, developer mode)");
    }
    ico_opt_set_developer_mode(0);
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "hidden again");
    /* the Display page with the row, from the title */
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_TEXTURE_PACK), "the Texture pack row");
    checkPageFits(UI_PAGE_DISPLAY, "Display with Texture pack (title)");
    ui_SettingsSetTexturePackCount(NULL);
    useConfig("version = 1\n");
}

/* v0.5.0: Display > Model pack (title only; "None installed" without the
   hook or with a count of 0, the step then doing nothing; On/Off with
   one), its note, Dump models on the main page in developer mode only
   (switched off with it), and the [video] keys' round trip. */
static void testModelPack(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    ui_SettingsSetModelPackCount(NULL);
    ico_opt_set_developer_mode(0);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 0, "defaults: model pack on, dump off");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "None installed") == 0,
          "no hook: None installed (%s)", ui_SettingsValueText(UI_OPT_MODEL_PACK));
    ui_SettingsSetModelPackCount(fakePackCount);
    s_packCount = 0;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "None installed") == 0,
          "a count of 0: None installed");
    ui_SettingsStep(UI_OPT_MODEL_PACK, 1);
    ico_video_get(&o);
    CHECK(o.modelPack == 1, "none installed: the step does nothing");
    s_packCount = 12;
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "On") == 0, "a pack: On");
    ui_SettingsStep(UI_OPT_MODEL_PACK, 1);
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "Off") == 0,
          "Right: Off");
    ui_SettingsStep(UI_OPT_MODEL_PACK, -1);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "On") == 0,
          "Left: On");
    CHECK(o.texturePack == 1, "the texture pack row untouched");

    /* shown from the title only; both entries fit */
    int mainL = enterMain(1);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_MODEL_PACK), "title: the Model pack row");
    checkPageFits(UI_PAGE_DISPLAY, "Display with Model pack (title)");
    {
        int tn = rowWithPrefix(UI_PAGE_DISPLAY, "Model packs:");
        CHECK(tn >= 0 && strchr(lt_ext_RowText(tn), '\n') == NULL, "the Model pack note: one line");
    }
    mainL = enterMain(0);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(!rowShown(UI_PAGE_DISPLAY, UI_OPT_MODEL_PACK), "pause menu: the Model pack row hidden");
    checkPageFits(UI_PAGE_DISPLAY, "Display (pause)");

    /* Dump models: developer mode only, switched off with it */
    mainL = enterMain(1);
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_MODELS), "dump models row hidden");
    ui_SettingsStep(UI_OPT_DEVELOPER, 1);
    CHECK(ico_opt_developer_mode(), "Developer on");
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_MODELS), "dump models row shown in developer mode");
    checkPageFits(UI_PAGE_MAIN, "Main (title, developer mode, Dump models)");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_DUMP_MODELS), "Off") == 0, "dump models: Off");
    ui_SettingsStep(UI_OPT_DUMP_MODELS, 1);
    ico_video_get(&o);
    CHECK(o.dumpModels == 1 && strcmp(ui_SettingsValueText(UI_OPT_DUMP_MODELS), "On") == 0,
          "dump models: On");
    {
        int dn = rowWithPrefix(UI_PAGE_MAIN, "For pack makers: saves each model");
        const char *nl = dn >= 0 ? strchr(lt_ext_RowText(dn), '\n') : NULL;
        CHECK(dn >= 0, "the dump models note");
        CHECK(nl == NULL || strchr(nl + 1, '\n') == NULL,
              "the dump models note: at most two lines");
    }
    char p[1100];
    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.model_pack", 0) == 1 &&
              ico_toml_get_bool(t, "video.dump_models", 0) == 1,
          "saved: model_pack, dump_models");
    ico_toml_free(t);
    ui_SettingsStep(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(!ico_opt_developer_mode() && o.dumpModels == 0, "Developer off: dump models off too");
    useConfig("version = 1\n[video]\nmodel_pack = false\n");
    ico_video_get(&o);
    CHECK(!o.modelPack, "model_pack = false read back");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MODEL_PACK), "Off") == 0, "the row: Off");
    ui_SettingsSetModelPackCount(NULL);
    useConfig("version = 1\n");
}

/* issue 11: Options > Effects.  The link sits under Display on the main page,
   the five rows read On by default, a step flips one and ui_SettingsSave
   writes [video] effect_*; the Main page fits in all four entries */
static void testEffects(void)
{
    static const struct {
        UiSettingsOpt opt;
        const char *key;
    } kFx[] = {{UI_OPT_EFFECT_GLOW, "video.effect_glow"},
               {UI_OPT_EFFECT_DEPTH_OF_FIELD, "video.effect_depth_of_field"},
               {UI_OPT_EFFECT_SOFTENING, "video.effect_softening"},
               {UI_OPT_EFFECT_MOTION_BLUR, "video.effect_motion_blur"},
               {UI_OPT_EFFECT_FOG, "video.effect_fog"}};

    useConfig("version = 1\n");
    int mainL = enterMain(1);
    int fxL = openPage(mainL, 1, UI_PAGE_EFFECTS);
    CHECK(fxL == ui_SettingsPageLayout(UI_PAGE_EFFECTS), "the Effects link opens the page");
    CHECK(rowWithPrefix(UI_PAGE_EFFECTS, "Effects") >= 0, "the page is called Effects");
    checkPageFits(UI_PAGE_EFFECTS, "Effects");
    CHECK(rowWithPrefix(UI_PAGE_EFFECTS, "The game") >= 0, "the Effects note");
    press(0x10);
    CHECK(settle(mainL, 60), "Effects: back");

    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_SettingsValueText(kFx[i].opt), "On") == 0, "effect %d: On by default", i);
        ui_SettingsStep(kFx[i].opt, 1);
        CHECK(strcmp(ui_SettingsValueText(kFx[i].opt), "Off") == 0, "effect %d: Off", i);
        CHECK(ui_SettingsSave() == 0, "save %d", i);
        char p[1100];
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL, "config %d", i);
        for (int j = 0; t && j < 5; j++) {
            CHECK(ico_toml_get_bool(t, kFx[j].key, 1) == (j > i), "%s after step %d", kFx[j].key,
                  i);
        }
        ico_toml_free(t);
    }
    for (int i = 0; i < 5; i++) {
        ui_SettingsStep(kFx[i].opt, -1);
        CHECK(strcmp(ui_SettingsValueText(kFx[i].opt), "On") == 0, "effect %d: On again", i);
    }

    for (int title = 1; title >= 0; title--) {
        for (int dev = 0; dev <= 1; dev++) {
            ico_opt_set_developer_mode(dev);
            enterMain(title);
            for (int k = 0; k < 4; k++) {
                frame(0);
            }
            checkPageFits(UI_PAGE_MAIN, title ? (dev ? "Main (title, developer)" : "Main (title)")
                                              : (dev ? "Main (pause, developer)" : "Main (pause)"));
        }
    }
    ico_opt_set_developer_mode(0);
    useConfig("version = 1\n");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    testBuild();
    testBudget();
    testRepoint();
    testPlacement();
    testNavigation();
    testPhoto();
    testPauseStats();
    testNewGameScreen();
    testQuit();
    testCirclePortScreens();
    testCircleGameMenu();
    testValues();
    testAudio();
    testVideoGate();
    testFramerate();
    testPreset();
    testCapture();
    testBootSkip();
    testExtras();
    testGlyphSources();
    testTitleOptionsWord();
    testGallery();
    testList();
    testGameOptions();
    testCoversTitle();
    testTexturePack();
    testTexturePackNoteLines();
    testModelPack();
    testEffects();
    if (failures) {
        printf("settings_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_test: ok\n");
    return 0;
}

#endif /* SETTINGS_RENDER */
