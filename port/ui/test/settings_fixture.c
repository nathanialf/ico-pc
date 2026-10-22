/* settings_fixture.c: the fakes and helpers the settings test programs
 * share (see settings_fixture.h). */
#include "settings_fixture.h"

int failures;

#ifdef _WIN32

void setEnv(const char *k, const char *v)
{
    _putenv_s(k, v ? v : "");
}

#else

void setEnv(const char *k, const char *v)
{
    if (v) {
        setenv(k, v, 1);
    } else {
        unsetenv(k);
    }
}

#endif

/* ------------------------------------- what the game files import (stubs) */
int ScreenWidth = 512, ScreenHeight = 512;

float center_X = 2048.0f, center_Y = 2048.0f;

LtProp texLayout[LT_GAME_LAYOUT_COUNT_MAX];

LtProperty texProperty[LT_GAME_PROPERTY_COUNT_MAX];

TexRec texFile[1];

const StgPre stageData[110];

PadState pad[16];

StageSetting GlobalStageSetting;

int gFlagGameClear;

int stage_no; /* 0 here, a stage number in testPhoto */

/* photo mode's pivot (photo_ui.c): no camera target here */
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

int s_resets, s_sounds[3], s_leaves;

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

int s_newGames;
int s_mcSaveSlots;

/* fumi/sound/s_init.c: the game's stereo (0) or mono (1) output */
int s_outputMode, s_outputSets;

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
int s_devCount;
const char *s_devNames[3];
char s_reopened[ICO_AUDIO_DEVICE_NAME_MAX];
int s_reopens;

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

/* layout_action.c: the New Game screen's confirm starts the game */
void la_host_new_game_go(void)
{
    s_newGames++;
}

/* layout_action.c: the memory card screen's mouse pick of a slot, on the Save screen only */
int la_host_mc_slot_pickable(int slot)
{
    return s_mcSaveSlots && slot >= 0 && slot < 10;
}

/* the game's Options screen's settings, now on the port's pages (common/src/main.c,
   fumi/ios/pad.c), and layout_action.c's film effect (la_game_option's
   stage animations), which records the mode it was given */
int optionScreenMode, optionControlType, girlControlMode;
int iosPadActRequestEnable = 1;
int s_filmCalls, s_filmLast = -1;

void la_host_film_effect(int mode)
{
    s_filmCalls++;
    s_filmLast = mode;
    optionScreenMode = mode;
}

/* port/platform/trace_host.c (ico_pc): the Main ticks, Characters'
   Randomize seed with the clock */
unsigned int ico_host_main_ticks(void)
{
    return 0;
}

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

void setRow(int i, int up, int down, int downItem, int upItem, int left, int right, int y)
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

void setLayout(int i, int first, int last, int def, int link)
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

/* The fixture's tables are the European disc's numbers; on another disc's
   region (a test's "jpn" or "kor" run) the same rows at that disc's
   numbers, read from its row (port/core/region_<slug>.c): the layouts the
   fixture builds (the vibration, title, play, scene, pause, options and
   key config layouts; the Japanese title's extra layout and the two after
   key config sit at the same distance from them as on the Japanese disc,
   where layout 8 precedes la_title_continue_or_new and 60, 61 follow
   la_key_config), and the rows of the title, the vibration screen (the
   copyright's row less 4 .. 2, as on the Japanese disc: 14 .. 16 against
   18) and the pause, Options and button configuration screens (the rows
   from 292 on move with the pause menu's Options row). */
int fxLayout(int pal)
{
    const IcoRegionInfo *r = ico_region_cur();
    switch (pal) {
    case 9:
        return r->layout_vibe;
    case 11:
        return r->layout_title_continue - 1;
    case 12:
        return r->layout_title_continue;
    case 13:
        return r->layout_title_new;
    case 54:
        return r->layout_play;
    case 55:
        return r->layout_scene;
    case 57:
        return r->layout_pause;
    case 58:
        return r->layout_options;
    case 59:
        return r->layout_key_config;
    case 60:
        return r->layout_key_config + 1;
    case 61:
        return r->layout_key_config + 2;
    default:
        return pal;
    }
}

int fxRow(int pal)
{
    const IcoRegionInfo *r = ico_region_cur();
    if (pal >= 292 && pal <= 392) {
        return pal + (r->row_pause_options - 294);
    }
    switch (pal) {
    case 44:
    case 45:
    case 46:
        return r->row_title_copyright + (pal - 48); /* la_vibe_select's rows, its default the 2nd */
    case 48:
        return r->row_title_copyright; /* the copyright line's first half */
    case 49:
        return r->row_title_continue;
    case 50:
        return r->row_title_new;
    case 51:
        return r->row_title_new_only;
    case 52:
        return r->row_title_new_only + 1; /* la_title_new_game_only's end */
    default:
        return pal;
    }
}

/* the PAL rows the entry touches, with their shipped links (tex-property.c) */
void fakeTables(void)
{
    memset(texLayout, 0, sizeof(texLayout));
    memset(texProperty, 0, sizeof(texProperty));
    for (int i = 0; i < LT_GAME_PROPERTY_COUNT; i++) {
        setRow(i, -1, -1, -1, -1, -1, -1, 0);
    }
    setLayout(fxLayout(54), fxRow(292), fxRow(292), -1, -1);
    texLayout[fxLayout(54)].fadeInTime = 0.0f;
    texLayout[fxLayout(54)].fadeOutTime = 0.0f;
    setLayout(fxLayout(57), fxRow(292), fxRow(297), fxRow(295), -1);
    setLayout(fxLayout(58), fxRow(297), fxRow(333), fxRow(308), -1);
    setLayout(fxLayout(59), fxRow(333), fxRow(392), fxRow(336),
              -1); /* the game's button configuration */
    setRow(fxRow(336), -1, -1, -1, -1, -1, -1, 52);
    /* the pause menu: Options (opens 58 in the PAL data), Back, End
       Game, their letters 7 texels into rectangles at x 40 */
    setRow(fxRow(294), -1, -1, fxRow(295), -1, -1, fxLayout(58), 50);
    setRow(fxRow(295), -1, -1, fxRow(296), fxRow(294), -1, -1, 70);
    setRow(fxRow(296), -1, -1, -1, fxRow(295), -1, fxLayout(61), 120);
    for (int i = 294; i <= 296; i++) {
        texProperty[fxRow(i)].dispX = 40;
    }
    setLayout(fxLayout(11), fxRow(48), fxRow(49), -1, -1);
    setLayout(fxLayout(12), fxRow(49), fxRow(51), fxRow(49), fxLayout(11));
    setLayout(fxLayout(13), fxRow(51), fxRow(52), fxRow(51), fxLayout(11));
    setLayout(fxLayout(9), fxRow(44), fxRow(46), fxRow(44),
              -1); /* the vibration screen (its Triangle goes back) */
    setRow(fxRow(44), -1, -1, fxRow(45), -1, -1, -1, 100);
    setRow(fxRow(45), -1, -1, -1, fxRow(44), -1, -1, 120);
    /* Options: 300 screen mode and 325 girl control are shown only after
       the game is cleared (layout_texture.c lt_property_visible) */
    setRow(fxRow(300), -1, -1, fxRow(308), fxRow(325), fxLayout(57), -1, 45);
    setRow(fxRow(308), -1, -1, fxRow(313), fxRow(300), fxLayout(57), -1, 65);
    setRow(fxRow(313), -1, -1, fxRow(318), fxRow(308), fxLayout(57), -1, 85);
    setRow(fxRow(318), -1, -1, fxRow(323), fxRow(313), fxLayout(57), -1, 105);
    setRow(fxRow(323), -1, -1, fxRow(324), fxRow(318), fxLayout(57), fxLayout(59), 125);
    setRow(fxRow(324), -1, -1, fxRow(325), fxRow(323), fxLayout(57), fxLayout(60), 145);
    setRow(fxRow(325), -1, -1, fxRow(300), fxRow(324), fxLayout(57), -1, 165);
    setRow(fxRow(330), -1, -1, fxRow(300), fxRow(324), fxLayout(57), fxLayout(57), 1);
    /* the labels' x (the Settings row ends where their letters end) */
    texProperty[fxRow(323)].dispX = 108;
    texProperty[fxRow(324)].dispX = 236;
    texProperty[fxRow(325)].dispX = 236;
    /* title: the copyright line (48), Continue (49) and New Game (50, 51),
       centred in 20-field-line boxes; 49..51 masked by default */
    setRow(fxRow(48), -1, -1, -1, -1, -1, -1, 195);
    setRow(fxRow(49), -1, -1, fxRow(50), -1, -1, -1, 135);
    setRow(fxRow(50), -1, -1, -1, fxRow(49), -1, -1, 165);
    setRow(fxRow(51), -1, -1, -1, -1, -1, -1, 165);
    for (int i = 48; i <= 51; i++) {
        texProperty[fxRow(i)].dispW = 0;
        texProperty[fxRow(i)].centerX = 1;
        texProperty[fxRow(i)].defaultMask = i != 48;
    }
    if (ico_region_cur()->row_title_copyright2 >= 0) {
        /* the Japanese program's title: the copyright line is two halves (the
           layout fxLayout(11) holds both, set above) */
        const int h2 = ico_region_cur()->row_title_copyright2;
        setRow(h2, -1, -1, -1, -1, -1, -1, 195);
        texProperty[h2].dispW = 0;
        texProperty[h2].centerX = 1;
    }
    /* the button glyphs' rows (layout_ext.h): their PAL rectangles, and a
       texture number per sheet (1 buttons.tm2, 2 menu_PAL_02, 3
       menu_PAL_01) that settings_render binds to a drawn stand-in */
    for (int g = 0; g < LT_GLYPH_COUNT; g++) {
        int uv[4];
        int r = lt_ext_glyph_source(g, uv);
        texProperty[r].texU = uv[0];
        texProperty[r].texV = uv[1];
        texProperty[r].texW = uv[2];
        texProperty[r].texH = uv[3];
        texProperty[r].texNo = g <= LT_GLYPH_TRIANGLE ? 1 : g <= LT_GLYPH_R2 ? 2 : 3;
    }
    /* the pause menu's Options (294): its PAL rectangle on menu_PAL_01, a
       row of the menu text table (drawn as text in the sheets' look) */
    texProperty[fxRow(294)].texU = 384;
    texProperty[fxRow(294)].texV = 120;
    texProperty[fxRow(294)].texW = 128;
    texProperty[fxRow(294)].texH = 20;
    texProperty[fxRow(294)].texNo = 3;
}

/* ------------------------------------------------------- frames */

#ifdef SETTINGS_RENDER

/* a mid-tone like the fogged title, under the layout's dark backdrop */
static const uint8_t kBg[4] = {150, 140, 120, 0x80};

/* the frames end with the reduction, so a present shows them */
int s_reduce;

#endif

void frame(int flags)
{
    frame_count++;
    pad[0].flags = flags;
    pad[0].now = flags;
#ifdef SETTINGS_RENDER
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBg, 1, 0);
    exec_layout_texture();
    if (s_reduce) {
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
        dl_SetDLPriority(12);
        rd_post(RD_POST_REDUCTION, &pp);
    }
    dl_Swap();
#else
    exec_layout_texture();
#endif
}

/* runs frames until the layout is current and settled (fade state 2) */
int settle(int layout, int max)
{
    for (int i = 0; i < max; i++) {
        if (current_layout_id == layout && lt_fade_status() == 2) {
            return 1;
        }
        frame(0);
    }
    return current_layout_id == layout && lt_fade_status() == 2;
}

void press(int flags)
{
    frame(flags);
    frame(0);
    frame(0); /* exec_layout_texture clears flags for two frames after a move */
}

/* ------------------------------------------------------- tests */

char s_dir[1024];

void path(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", s_dir, name);
}

void writeFile(const char *p, const char *text)
{
    FILE *f = fopen(p, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

void useConfig(const char *text)
{
    char p[1100];
    path(p, sizeof(p), "settings_test.toml");
    writeFile(p, text);
    ico_config_reset(p, "");
    ico_sysconf_reset();
    ico_opt_reload();
    ico_video_reload();
}

int labelsAre(UiSettingsPage page, const int *opts, const int *strs, int n)
{
    int labels[32], got[32];
    int c = ui_settings_page_rows(page, labels, got, NULL, 32);
    int ok = c == n;
    for (int i = 0; ok && i < n; i++) {
        ok = got[i] == opts[i] &&
             (strs[i] < 0 || strcmp(lt_ext_row_text(labels[i]), ui_str((UiStrId)strs[i])) == 0);
        if (!ok) {
            printf("  page %d row %d: opt %d (want %d) \"%s\"\n", page, i, got[i], opts[i],
                   lt_ext_row_text(labels[i]));
        }
    }
    if (c != n) {
        printf("  page %d: %d rows, want %d\n", page, c, n);
    }
    return ok;
}

/* Settings from the pause menu: 57 with the cursor on Options (294), Cross */
void pauseToMain(void)
{
    lt_switch_layout(fxLayout(57));
    CHECK(settle(fxLayout(57), 40), "the pause menu");
    texLayout[fxLayout(57)].curItem = fxRow(294);
    press(lt_ext_confirm_button());
    CHECK(settle(ui_settings_page_layout(UI_PAGE_MAIN), 60), "Options: the menu (%d)",
          current_layout_id);
}

/* Settings from the title (13) or from the pause menu (57) to the main
   page */
int enterMainKeep(int title);

int enterMain(int title)
{
    useConfig("version = 1\n");
    return enterMainKeep(title);
}

/* enterMain on the config file as it is (a new start of the game reads
   what the last one wrote) */
int enterMainKeep(int title)
{
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    ui_set_language(UI_LANG_EN);
    init_layout_texture(2);
    settle(fxLayout(54), 4);
    if (title) {
        const int t = fxLayout(13);
        lt_switch_layout(t);
        CHECK(settle(t, 60), "the title");
        press(0x4000);
        CHECK(texLayout[t].curItem == ui_settings_entry_row(t), "on Settings");
        press(lt_ext_confirm_button());
    } else {
        pauseToMain();
    }
    int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "the menu (title %d)", title);
    return mainL;
}

/* the main page's row `mainRow` (all rows counted), Cross: its page */
int openPage(int mainL, int mainRow, UiSettingsPage page)
{
    int labels[16];
    const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    const int want = ui_settings_page_layout(page);
    /* the row that opens the page (mainRow, or the one whose link it is:
       the main page's rows moved when Menu language came in) */
    if (mainRow < 0 || mainRow >= n || lt_ext_prop(labels[mainRow])->right != want) {
        for (int i = 0; i < n; i++) {
            if (lt_ext_prop(labels[i])->right == want) {
                mainRow = i;
                break;
            }
        }
    }
    lt_ext_layout(mainL)->curItem = labels[mainRow];
    press(lt_ext_confirm_button());
    const int l = ui_settings_page_layout(page);
    CHECK(settle(l, 60), "page %d opens (%d)", page, current_layout_id);
    return l;
}

/* --- helpers shared by the settings test programs --- */

/* the pause menu journey lines' game state, set by the tests (ico_gs_set_sampler) */
IcoGsSnapshot s_gs;

void gsSampler(IcoGsSnapshot *out)
{
    *out = s_gs;
}

/* the row of layout page whose text is str, -1 */
int rowWithText(UiSettingsPage page, const char *str)
{
    LtProp *l = lt_ext_layout(ui_settings_page_layout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strcmp(lt_ext_row_text(j), str) == 0) {
            return j;
        }
    }
    return -1;
}

/* the page in front: its shown rows top to bottom at least 13 field lines
   apart (11 on a page of more than 13 rows, which starts higher: the
   Controls page with the touch rows), below the header, the last one's box
   inside the 226 lines */
void checkPageFits(UiSettingsPage page, const char *what)
{
    int labels[16];
    const int n = ui_settings_page_rows(page, labels, NULL, NULL, 16);
    int prev = -1, last = -1, shown = 0;
    for (int i = 0; i < n; i++) {
        shown += !lt_ext_prop(labels[i])->defaultMask;
    }
    /* Controls is the page that grows past the others (pagePitch) */
    const int tall = page == UI_PAGE_CONTROLS ? 12 : 13;
    /* the Main page's thirteen rows (Menu language, developer mode and
       both dumps) 12 apart, fifteen (the Graphics driver link and Disc
       too) 10 apart, sixteen (Save a picture for a bug report too) 10
       apart from 30 (pagePitch) */
    const int gap = shown > 15 && page == UI_PAGE_CONTROLS ? 10
                    : shown > 14 && page == UI_PAGE_MAIN   ? 10
                    : shown > tall                         ? 11
                    : shown == 13 && page == UI_PAGE_MAIN  ? 12
                                                           : 13;
    const int top = shown > tall ? 30 : 34;
    for (int i = 0; i < n; i++) {
        const LtProperty *r = lt_ext_prop(labels[i]);
        if (r->defaultMask) {
            continue;
        }
        CHECK(prev < 0 ? r->dispY >= top : r->dispY >= prev + gap,
              "%s: row %d at y %d (the one above at %d)", what, i, r->dispY, prev);
        prev = r->dispY;
        last = labels[i];
    }
    CHECK(last >= 0 && lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH <= 226,
          "%s: the last row's box ends at %d", what,
          last >= 0 ? lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH : -1);
}

/* the note row of `page` whose text starts with prefix, -1 */
int noteStarting(UiSettingsPage page, const char *prefix)
{
    LtProp *l = lt_ext_layout(ui_settings_page_layout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strncmp(lt_ext_row_text(j), prefix, strlen(prefix)) == 0) {
            return j;
        }
    }
    return -1;
}

/* the texture and model pack count the Display page asks for, set by the tests */
int s_packCount;

int fakePackCount(void)
{
    return s_packCount;
}

/* Settings > Extras > Music's engine, faked: a few streams in each group and
   the calls counted (gallery.h) */
AdpcmDataRec s_fakeAdpcm[105];
int s_galPlays, s_galStops, s_galLeaves, s_galLastKey = -1, s_galPaused;
const GalleryItem *s_galCur;

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

const GalleryEngine kFakeEngine = {galTables, NULL,       galLeaveHook, galPlay,    galStop,
                                   NULL,      galPlaying, galPause,     galPosition};
