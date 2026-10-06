/* settings_test.c: the Settings menu (Phase 6, 6C; docs/port/UI.md
 * "Settings menu", docs/port/SETTINGS.md).  CPU only.
 *
 *   - the menu builds over fake game tables shaped like the PAL ones: the
 *     pages and their rows, in order, with the expected labels;
 *   - the navigation repoint: the Options screen's rows 325/300 and the
 *     title's 50/51 lead to the entry rows, the link chains, idempotence;
 *   - the entry rows' places from the table data: the title's evenly
 *     spaced between New Game and the copyright line, masked by default as
 *     New Game; the Options row on the labels' pitch and right edge;
 *   - through the real layout_texture.c: the cursor in layout 58 moves from
 *     324 onto the Settings row (325 and 300 hidden before the game is
 *     cleared), Cross opens the menu, a right press on Language changes the
 *     language, Triangle goes back to 58 with the cursor on the row;
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
 * and each screen's SCENE written as a PNG beside the test for a look
 * (settings_<screen>.png): no game data, the backdrop over a flat colour.
 */
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
#include "config.h"
#include "font.h"
#include "host_config.h"
#include "input.h"
#include "layout_ext.h"
#include "menu_text.h"
#include "options.h"
#include "settings.h"
#include "strings.h"
#include "sysconf.h"
#include "ui_list.h"
#include "video_options.h"

#ifdef SETTINGS_RENDER

#include "DisplayList.h"
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

/* layout_action.c (R7c): the mirror screen's confirm starts the game */
void la_host_new_game_go(void)
{
    s_newGames++;
}

void tex_TransTexture(int no, int pri)
{
    (void)no;
    (void)pri;
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

void soundSeDefPlay(int no, unsigned int a, int b, int c)
{
    (void)no;
    (void)a;
    (void)b;
    (void)c;
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
}

/* ------------------------------------------------------- frames */

#ifdef SETTINGS_RENDER

/* a mid-tone like the fogged title, under the layout's dark backdrop */
static const uint8_t kBg[4] = {150, 140, 120, 0x80};

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
    static const int mainOpts[] = {UI_OPT_LINK, UI_OPT_LINK,      UI_OPT_LINK,
                                   UI_OPT_LINK, UI_OPT_LANGUAGE,  UI_OPT_LINK,
                                   UI_OPT_LINK, UI_OPT_DEVELOPER, UI_OPT_BACK};
    static const int mainStrs[] = {
        UI_STR_SECTION_DISPLAY,  UI_STR_SECTION_AUDIO,      UI_STR_SECTION_CONTROLS,
        UI_STR_SECTION_GAMEPLAY, UI_STR_SECTION_LANGUAGE,   UI_STR_SECTION_ACHIEVEMENTS,
        UI_STR_EXTRAS,           UI_STR_OPT_DEVELOPER_MODE, UI_STR_BACK};
    static const int dispOpts[] = {UI_OPT_PRESET,      UI_OPT_RESOLUTION, UI_OPT_ASPECT,
                                   UI_OPT_FULLSCREEN,  UI_OPT_VSYNC,      UI_OPT_FILTER,
                                   UI_OPT_FULL_HEIGHT, UI_OPT_FRAMERATE,  UI_OPT_VIDEO_MODE,
                                   UI_OPT_MENU_TEXT,   UI_OPT_BACK};
    static const int dispStrs[] = {UI_STR_OPT_PRESET,
                                   UI_STR_OPT_RESOLUTION,
                                   UI_STR_OPT_ASPECT,
                                   UI_STR_OPT_FULLSCREEN,
                                   UI_STR_OPT_VSYNC,
                                   UI_STR_OPT_FILTERING,
                                   UI_STR_OPT_FULL_HEIGHT,
                                   UI_STR_OPT_FRAMERATE,
                                   UI_STR_OPT_VIDEO_MODE,
                                   UI_STR_OPT_MENU_TEXT,
                                   UI_STR_BACK};
    static const int audioOpts[] = {UI_OPT_VOLUME, UI_OPT_BACK};
    static const int audioStrs[] = {UI_STR_OPT_VOLUME, UI_STR_BACK};
    static const int ctlOpts[] = {UI_OPT_LINK, UI_OPT_MOUSE_SENS, UI_OPT_CIRCLE_BACK, UI_OPT_BACK};
    static const int ctlStrs[] = {UI_STR_OPT_REMAP, UI_STR_OPT_MOUSE_SENS, UI_STR_OPT_CIRCLE_BACK,
                                  UI_STR_BACK};
    static const int gameOpts[] = {UI_OPT_YORDA, UI_OPT_STICK_FIX, UI_OPT_BACK};
    static const int gameStrs[] = {UI_STR_OPT_YORDA, UI_STR_OPT_STICK_FIX, UI_STR_BACK};
    static const int listOpts[8] = {UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST,
                                    UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST};
    static const int listStrs[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SetLanguage(UI_LANG_EN);
    ui_SettingsInstall();
    CHECK(labelsAre(UI_PAGE_MAIN, mainOpts, mainStrs, 9), "main page rows");
    {
        static const int extrasOpts[] = {UI_OPT_EXTRAS_MUSIC, UI_OPT_EXTRAS_MODELS,
                                         UI_OPT_EXTRAS_CREDITS, UI_OPT_BACK};
        static const int extrasStrs[] = {UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                                         UI_STR_EXTRAS_CREDITS, UI_STR_BACK};
        CHECK(labelsAre(UI_PAGE_EXTRAS, extrasOpts, extrasStrs, 4), "Extras page rows");
    }
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 11),
          "display rows (Frame rate without a framerate key)");
    CHECK(labelsAre(UI_PAGE_AUDIO, audioOpts, audioStrs, 2), "audio rows");
    CHECK(labelsAre(UI_PAGE_CONTROLS, ctlOpts, ctlStrs, 4), "controls rows");
    CHECK(labelsAre(UI_PAGE_GAMEPLAY, gameOpts, gameStrs, 3), "gameplay rows");
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
    /* P3: the menu text row: the port font by default, Classic restores the
       textures (port/ui/menu_text.h) and is written as [game]
       classic_menu_text */
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MENU_TEXT), "Port font") == 0 && !ui_MenuTextClassic(),
          "menu text: Port font (%s)", ui_SettingsValueText(UI_OPT_MENU_TEXT));
    ui_SettingsStep(UI_OPT_MENU_TEXT, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MENU_TEXT), "Classic") == 0 && ui_MenuTextClassic() &&
              ico_config_get_bool("game.classic_menu_text", 0) == 1,
          "menu text: Classic");
    ui_SettingsStep(UI_OPT_MENU_TEXT, -1);
    CHECK(!ui_MenuTextClassic() && ico_config_get_bool("game.classic_menu_text", 1) == 0,
          "menu text: Port font again");
    /* the New Game screen */
    int ml = ui_MirrorScreenLayout();
    CHECK(ml >= LT_GAME_LAYOUT_COUNT && lt_ext_Layout(ml)->proc != NULL, "mirror screen layout");
    CHECK(ui_MirrorScreenRow(0) >= 0 && ui_MirrorScreenRow(1) >= 0 &&
              strcmp(lt_ext_RowText(ui_MirrorScreenRow(0)), "Off") == 0 &&
              strcmp(lt_ext_RowText(ui_MirrorScreenRow(1)), "On") == 0,
          "mirror screen rows Off / On");
    CHECK(lt_ext_Prop(ui_MirrorScreenRow(0))->rightItem == ui_MirrorScreenRow(1) &&
              lt_ext_Prop(ui_MirrorScreenRow(1))->leftItem == ui_MirrorScreenRow(0),
          "Off and On side by side");

    /* R7d: the Frame rate row in either preset, its value from the file */
    useConfig("[video]\npreset = \"enhanced\"\nframerate = \"144\"\n");
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SettingsInstall();
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 11), "display rows (Enhanced)");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_FRAMERATE), "144 fps") == 0, "framerate 144 (%s)",
          ui_SettingsValueText(UI_OPT_FRAMERATE));
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
    CHECK(o.preset == ICO_VIDEO_ORIGINAL && ico_video_framerate() == 240,
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

static void testRepoint(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    ui_SettingsInstall();
    int s58 = ui_SettingsEntryRow(58), s12 = ui_SettingsEntryRow(12), s13 = ui_SettingsEntryRow(13);
    CHECK(s58 >= LT_GAME_PROPERTY_COUNT && s12 > s58 && s13 > s12, "entry rows");
    CHECK(texProperty[325].downItem == s58 && texProperty[300].upItem == s58, "325/300 -> row");
    CHECK(lt_ext_Prop(s58)->upItem == 325 && lt_ext_Prop(s58)->downItem == 300, "row -> 325/300");
    CHECK(lt_ext_Prop(s58)->left == 57, "Triangle on the row: the pause menu");
    CHECK(lt_ext_Prop(s58)->right == ui_SettingsPageLayout(UI_PAGE_MAIN), "Cross: the menu");
    CHECK(texLayout[58].link == ui_SettingsEntryLayout(58) &&
              lt_ext_Layout(ui_SettingsEntryLayout(58))->link == -1,
          "58 -> entry layout");
    CHECK(texProperty[50].downItem == s12 && lt_ext_Prop(s12)->upItem == 50, "title 12");
    CHECK(texProperty[51].downItem == s13 && lt_ext_Prop(s13)->upItem == 51, "title 13");
    CHECK(texLayout[12].link == ui_SettingsEntryLayout(12) &&
              lt_ext_Layout(ui_SettingsEntryLayout(12))->link == 11 &&
              texLayout[13].link == ui_SettingsEntryLayout(13) &&
              lt_ext_Layout(ui_SettingsEntryLayout(13))->link == 11,
          "12 and 13 -> entry -> 11");
    CHECK(ui_SettingsEntryItem(s12) && ui_SettingsEntryItem(s58) && !ui_SettingsEntryItem(50),
          "entry items");
    int count = lt_ext_PropCount();
    ui_SettingsInstall();
    CHECK(lt_ext_PropCount() == count && texLayout[58].link == ui_SettingsEntryLayout(58) &&
              lt_ext_Layout(ui_SettingsEntryLayout(12))->link == 11,
          "a second install changes nothing");
    /* tables that are not the PAL ones are left alone */
    texLayout[58].first = 0;
    texProperty[325].downItem = 300;
    ui_SettingsInstall();
    CHECK(texProperty[325].downItem == 300, "unexpected tables: no repoint");
}

/* The copyright line's capitals (row 48, a texture): 25 output pixels at
   960 x 720 on a window run's title, in y units (720 / 448 pixels each) */
#define COPYRIGHT_CAPS 15.5f

/* The entry rows on the game's grid (docs/port/UI.md, open items 9 and 10,
   "Title rows (V2)"): the title laid out by the port, Continue (49), New
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
    const LtProperty *r323 = &texProperty[323], *r324 = &texProperty[324];
    const LtProperty *r325 = &texProperty[325];
    const LtProperty *o = lt_ext_Prop(ui_SettingsEntryRow(58));
    CHECK(r325->dispY - r324->dispY == r324->dispY - r323->dispY && o->dispY == r325->dispY,
          "Options: the row at %d, the labels' pitch from 324 (%d)", o->dispY, r324->dispY);
    /* display_texture's box: x from dispX + 1/4 (its inset), dispW - 1
       wide; the game row's letters end at dispX - 1/4 + the item's right
       anchor (menu_text.c ui_MenuTextDraw: the half-texel uv inset) */
    const UiMenuTextItem *it = NULL;
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        if (ui_menu_text_rows[i].row == 324) {
            it = &ui_menu_text_items[ui_menu_text_rows[i].item];
        }
    }
    CHECK(it && it->align == UI_ALIGN_RIGHT, "Brightness (324) in the menu text table");
    if (it) {
        const float portEnd = (float)(o->dispX + o->dispW) - 0.75f;
        const float gameEnd = (float)r324->dispX + it->x - 0.25f;
        CHECK(portEnd - gameEnd <= 0.5f && gameEnd - portEnd <= 0.5f,
              "Options: the row ends at x %.2f, the labels' letters at %.2f", portEnd, gameEnd);
    }
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
    init_layout_texture(2); /* installs; layout 54 */
    int s58 = ui_SettingsEntryRow(58);
    CHECK(s58 >= 0, "installed by init_layout_texture");
    settle(54, 4);
    lt_switch_layout(58);
    CHECK(settle(58, 40), "the Options screen");
    texLayout[58].curItem = 324;
    press(0x4000); /* down: 325 is hidden, the Settings row */
    CHECK(texLayout[58].curItem == s58, "down from 324: the Settings row (%d)",
          texLayout[58].curItem);
    CHECK(lt_ext_Prop(s58)->dispY == texProperty[325].dispY,
          "in 325's place before the game is cleared");
    press(0x4000); /* down: 300 hidden, 308 */
    CHECK(texLayout[58].curItem == 308, "down from the row: 308 (%d)", texLayout[58].curItem);
    press(0x1000); /* up from 308: 300 hidden, the row */
    CHECK(texLayout[58].curItem == s58, "up from 308: the row");
    press(0x40); /* Cross */
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "Cross opens the Settings menu (%d)", current_layout_id);
    /* down to Language (row 5), right: French */
    for (int i = 0; i < 4; i++) {
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
    CHECK(strstr(lt_ext_RowText(values[4]), "Français") != NULL, "the value row: %s",
          lt_ext_RowText(values[4]));
    CHECK(strcmp(lt_ext_RowText(labels[0]), "Affichage") == 0, "labels follow the language");
    CHECK(strcmp(ico_config_get_string("game.language", ""), "fr") == 0, "[game] language");
    press(0x8000); /* left: English again */
    CHECK(NonLinearCameraMove == 2, "left: English");
    /* Triangle: back to 58, the cursor on the row */
    press(0x10);
    CHECK(settle(58, 60), "Triangle: Options again");
    CHECK(texLayout[58].curItem == s58, "the cursor on the Settings row");
    /* after the game is cleared the row sits under 325 */
    gFlagGameClear = 1;
    frame(0);
    CHECK(lt_ext_Prop(s58)->dispY == 2 * texProperty[325].dispY - texProperty[324].dispY,
          "one Options pitch below 325 once cleared");
    gFlagGameClear = 0;

    /* into Controls -> Remap, capture a key on the first row (Cross) */
    press(0x40);
    CHECK(settle(mainL, 60), "the menu again");
    CHECK(lt_ext_Layout(mainL)->curItem == labels[0], "opens on its first row");
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

/* R7c: the New Game "Mirror mode" screen, run by the real layout code: the
 * cursor starts on Off, Right moves to On, Cross sets the run's value and
 * starts the game once; a second Enter starts on Off again and Cross picks
 * Off; Triangle goes back to the vibration screen (layout 9). */
static void testMirrorScreen(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    init_layout_texture(2);
    settle(54, 4);
    int ml = ui_MirrorScreenEnter();
    int off = ui_MirrorScreenRow(0), on = ui_MirrorScreenRow(1);
    CHECK(ml >= 0, "the screen is there once installed");
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the mirror screen (%d)", current_layout_id);
    CHECK(lt_ext_Layout(ml)->curItem == off, "the cursor on Off");
    press(0x2000); /* right */
    CHECK(lt_ext_Layout(ml)->curItem == on, "right: On");
    int games = s_newGames;
    ico_opt_set_mirror(0);
    press(0x40); /* Cross */
    CHECK(ico_opt_mirror() == 1, "Cross on On: the run is mirrored");
    CHECK(s_newGames == games + 1, "the game starts (gflagOn(382))");
    press(0x40);
    press(0x800);
    CHECK(s_newGames == games + 1, "once");

    /* again: the cursor back on Off, START picks it */
    lt_switch_layout(54);
    settle(54, 60);
    ml = ui_MirrorScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the mirror screen again");
    CHECK(lt_ext_Layout(ml)->curItem == off, "the cursor on Off again");
    press(0x2000);
    press(0x8000); /* left: back to Off */
    CHECK(lt_ext_Layout(ml)->curItem == off, "left: Off");
    press(0x800); /* START */
    CHECK(ico_opt_mirror() == 0 && s_newGames == games + 2, "START on Off: not mirrored");

    /* Triangle: the vibration screen */
    lt_switch_layout(54);
    settle(54, 60);
    ml = ui_MirrorScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the mirror screen a third time");
    press(0x10);
    CHECK(settle(9, 60), "Triangle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games + 2, "no game started");

    /* not built: -1 (la_vibe_select then starts the game itself) */
    ui_SettingsReset();
    CHECK(ui_MirrorScreenEnter() == -1 && ui_MirrorScreenLayout() == -1, "not built: -1");
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
    CHECK(ui_SettingsQuitRow(58) == -1, "no quit row in Options");
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
 * two lists, the menu itself (to the Options screen) and the mirror screen
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
    int s58 = ui_SettingsEntryRow(58), mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    lt_switch_layout(58);
    CHECK(settle(58, 40), "Options");
    texLayout[58].curItem = s58;
    press(0x40);
    CHECK(settle(mainL, 60), "the menu");

    static const struct {
        int row; /* main page row */
        UiSettingsPage page;
    } kPages[] = {{0, UI_PAGE_DISPLAY},
                  {1, UI_PAGE_AUDIO},
                  {2, UI_PAGE_CONTROLS},
                  {3, UI_PAGE_GAMEPLAY},
                  {5, UI_PAGE_ACHIEVEMENTS}};

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
    lt_ext_Layout(mainL)->curItem = labels[2];
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
    CHECK(settle(58, 60), "Circle: the menu back to Options");
    CHECK(texLayout[58].curItem == s58, "on the Settings row");
    /* the mirror screen: Circle is Triangle there (the vibration screen) */
    lt_switch_layout(54);
    settle(54, 60);
    int ml = ui_MirrorScreenEnter(), games = s_newGames;
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the mirror screen");
    press(0x20);
    CHECK(settle(9, 60), "Circle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games, "no game started");
}

/* Q2: the game's own menus through the real layout_texture.c: the Options
 * screen's rows go back to the pause menu (57) through their left link,
 * which default_item_select follows on Triangle, and on Circle while
 * [game] circle_back is on (the default); off, Circle does nothing there
 * and Triangle still goes back. */
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
        static const int kRows[2] = {308, -1}; /* a game row, then the Settings row */
        for (int r = 0; r < 2; r++) {
            int row = kRows[r] >= 0 ? kRows[r] : ui_SettingsEntryRow(58);
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
    CHECK(o.preset == ICO_VIDEO_ENHANCED && strstr(ui_SettingsValueText(UI_OPT_PRESET), "Enhanced"),
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
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_16_9 && strstr(ui_SettingsValueText(UI_OPT_ASPECT), "16:9"),
          "aspect 16:9");
    ui_SettingsStep(UI_OPT_ASPECT, 1);
    CHECK(strstr(ui_SettingsValueText(UI_OPT_ASPECT), "Auto") != NULL, "aspect Auto");
    ui_SettingsStep(UI_OPT_FILTER, -1);
    ico_video_get(&o);
    CHECK(o.filter == ICO_FILTER_ANISOTROPIC, "filter wraps to anisotropic");
    ui_SettingsStep(UI_OPT_FULLSCREEN, 1);
    ui_SettingsStep(UI_OPT_VSYNC, 1);
    ui_SettingsStep(UI_OPT_FULL_HEIGHT, 1);
    ico_video_get(&o);
    CHECK(o.fullscreen == 1 && o.vsync == 0 && o.fullHeight == 1, "the three toggles");
    CHECK(strstr(ui_SettingsValueText(UI_OPT_VSYNC), "Off") != NULL, "vsync Off");

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
        ico_toml_free(t);
    }
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
    CHECK(ico_boot_video_mode(1) == 0, "video_mode 60hz -> 0");
    CHECK(ico_boot_card_language(3) == 4 && ico_boot_card_video_mode(1) == 0,
          "explicit values over the card's");
    useConfig("[game]\nlanguage = \"it\"\n[video]\nvideo_mode = \"pal50\"\n");
    CHECK(ico_boot_language() == 5 && ico_boot_video_mode(0) == 1, "it, pal50");
    useConfig("[game]\nlanguage = \"auto\"\n");
    setEnv("LC_ALL", "es_ES.UTF-8");
    /* the host's locale (SDL's preferred locales in the window build, the
       environment headless) */
    CHECK(ico_boot_language() == ico_scf_to_game_language(ico_sysconf_host_language()),
          "auto: the host's locale");
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("es_ES.UTF-8")) == 6, "es_ES -> 6");
    CHECK(ico_boot_video_mode(1) == 1 && ico_boot_video_mode(0) == 0,
          "no video_mode: the value in force");
    CHECK(ico_boot_card_language(5) == 5 && ico_boot_card_video_mode(0) == 0,
          "auto / absent: the card's");
    setEnv("LC_ALL", "ja_JP.UTF-8");
    ico_sysconf_reset();
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("ja_JP.UTF-8")) == 2,
          "a language the game lacks: English (2)");
    setEnv("LC_ALL", NULL);
    useConfig("[video]\nvideo_mode = \"pal\"\n");
    CHECK(ico_boot_video_mode(1) == 1 && ico_boot_video_mode(0) == 0, "an invalid value: ignored");
    ico_sysconf_set_language(ICO_SCF_LANGUAGE_FRENCH);
    CHECK(sceScfGetLanguage() == ICO_SCF_LANGUAGE_FRENCH &&
              strcmp(ico_config_get_string("game.language", ""), "fr") == 0,
          "the setter");
}

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
        } else {
            int s58 = ui_SettingsEntryRow(58);
            lt_switch_layout(58);
            CHECK(settle(58, 40), "the Options screen");
            texLayout[58].curItem = s58;
        }
        press(0x40);
        int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
        CHECK(settle(mainL, 60), "the menu (title %d)", title);
        press(0x40); /* Display */
        int dispL = ui_SettingsPageLayout(UI_PAGE_DISPLAY);
        CHECK(settle(dispL, 60), "Display");
        int labels[16], opts[16];
        int n = ui_SettingsPageRows(UI_PAGE_DISPLAY, labels, opts, NULL, 16);
        int vm = 0;
        while (vm < n && opts[vm] != UI_OPT_VIDEO_MODE) {
            vm++;
        }
        for (int i = 0; i < vm; i++) {
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

/* Settings from the title (13) or from the Options screen (58) to the main
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
    } else {
        lt_switch_layout(58);
        CHECK(settle(58, 40), "the Options screen");
        texLayout[58].curItem = ui_SettingsEntryRow(58);
    }
    press(0x40);
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "the menu (title %d)", title);
    return mainL;
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
        CHECK(n == 9 && idx == 6, "Extras is the row after Achievements (index %d of %d)", idx, n);
        CHECK(lt_ext_Prop(ex)->right == ui_SettingsPageLayout(UI_PAGE_EXTRAS), "Extras opens");
        CHECK(lt_ext_Prop(ex)->defaultMask == !title, "title %d: the Extras row is %s", title,
              title ? "shown" : "hidden (masked)");
        /* the cursor: Achievements, Down */
        lt_ext_Layout(mainL)->curItem = labels[5];
        press(0x4000);
        CHECK(lt_ext_Layout(mainL)->curItem == (title ? ex : labels[7]),
              "title %d: Down from Achievements lands on %s", title,
              title ? "Extras" : "Developer");
        /* the rows below follow: Back's y, one pitch table for each entry */
        CHECK(title ? lt_ext_Prop(labels[8])->dispY == 40 + 17 * 8
                    : lt_ext_Prop(labels[8])->dispY == 40 + 19 * 7,
              "title %d: Back at y %d", title, lt_ext_Prop(labels[8])->dispY);
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
        /* every entry only logs, and stays on the page */
        const char *want[3] = {"music", "models", "credits"};
        for (int k = 0; k < 3; k++) {
            lt_ext_Layout(exL)->curItem = el[k];
            frame(0);
            errCapture();
            press(0x40);
            errRelease(log, sizeof(log));
            char line[64];
            snprintf(line, sizeof(line), "extras: %s not available yet", want[k]);
            CHECK(strstr(log, line) != NULL && current_layout_id == exL,
                  "Cross on %s logs and stays (\"%s\")", want[k], log);
        }
        /* Back and Triangle return to the main page, the cursor on Extras */
        lt_ext_Layout(exL)->curItem = el[3];
        press(0x40);
        CHECK(settle(mainL, 60) && lt_ext_Layout(mainL)->curItem == ex,
              "Back: the cursor on Extras");
        press(0x40);
        CHECK(settle(exL, 60), "Extras again");
        press(0x10);
        CHECK(settle(mainL, 60), "Triangle: the menu");
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

static void listStep(UiList *l, LtProp *lay, int flags)
{
    ui_ListProc(l, lay, flags);
    if (!(flags & 0x50)) {
        const LtProperty *e = lt_ext_Prop(lay->curItem);
        if ((flags & 0x1000) && e->upItem >= 0) {
            lay->curItem = e->upItem;
        } else if ((flags & 0x4000) && e->downItem >= 0) {
            lay->curItem = e->downItem;
        }
    }
    ui_ListRefresh(l, lay->curItem);
}

static void testList(void)
{
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
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_SetGsFrame(&fr);
    ui_SetScale(1.0f);
    ui__SetRecordHook(gif_HostFlush);
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
    } pages[] = {{0, 0, "settings_display.png"},  {0, 8, "settings_display_videomode.png"},
                 {1, 0, "settings_audio.png"},    {2, 1, "settings_controls.png"},
                 {3, 0, "settings_gameplay.png"}, {5, 0, "settings_achievements.png"}};

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
    /* Controls -> Remap, then a capture in progress */
    lt_ext_Layout(mainL)->curItem = labels[2];
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
    /* R7c: the New Game mirror screen, the cursor on On */
    int ml = ui_MirrorScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the mirror screen");
    press(0x2000);
    frame(0);
    snap("settings_mirror_screen.png");
    /* Q2: the quit confirmation, the cursor on Yes */
    int ql = ui_QuitScreenLayout();
    lt_switch_layout(ql);
    CHECK(settle(ql, 60), "the quit screen");
    press(0x8000);
    frame(0);
    snap("settings_quit_screen.png");
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
        lt_ext_Layout(mainL)->curItem = labels[4]; /* Language: its note */
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
        lt_ext_Layout(mainL)->curItem = ml[5];
        frame(0);
        frame(0);
        snap4("settings_main_title_4x.png");
        lt_ext_Layout(mainL)->curItem = ml[6];
        press(0x40);
        int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "Extras at 4x");
        press(0x4000);
        press(0x4000);
        frame(0);
        snap4("settings_extras_4x.png");
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

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    testBuild();
    testRepoint();
    testPlacement();
    testNavigation();
    testMirrorScreen();
    testQuit();
    testCirclePortScreens();
    testCircleGameMenu();
    testValues();
    testVideoGate();
    testFramerate();
    testCapture();
    testBootSkip();
    testExtras();
    testList();
    if (failures) {
        printf("settings_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_test: ok\n");
    return 0;
}

#endif /* SETTINGS_RENDER */
