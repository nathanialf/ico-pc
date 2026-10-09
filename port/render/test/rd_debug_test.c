/* rd_debug_test.c: common/src/debug.c on the host.
 *
 * debug.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the window
 * build compiles them (ICO_HOST, ICO_RD), sifdev_host.c for host0:, the
 * rest of the game stubbed below (none of it runs on the paths tested).
 *
 * Checks, on the recording (no device needed):
 *   - the debug font: debug_Init builds the glyph packets; debug_Printf of
 *     "-" records, in list 12, UI space, the points the VU1 routine
 *     (START_DEBUG_FONT) makes of the packet: every glyph and outline pixel
 *     of the 8 x 8 bitmap row 0x7F (made here as debug_makeBackImage does),
 *     at the cursor plus (k, 2 j) in 12.4, Z 0x7FFFFFFF, the text colour with
 *     alpha 0x70 or black with 0x60; a second glyph one advance
 *     (12 x 512 / 640 px) to the right; a space adds no point;
 *   - the debug menu: developer mode on, SELECT opens it, and the frame 66
 *     frames later holds exactly the points of its strings (the title, the
 *     first eleven captions read through the 64-bit DbgMenuItem stride, the
 *     bug list), the first row in the cursor colour;
 *   - the option list: CIRCLE on "Debug Mode" and the next frame holds the
 *     points of "DEBUG MODE", its help line and the 13 option lines as
 *     debug_Mode formats them from the retail values;
 *   - the font window: debug_PrintFontWindow then debug_FlushFont draws the
 *     line with debug_window_flag set and nothing without;
 *   - the option file ([dev] debug_option): TRIANGLE on the option page
 *     writes host0:thisIsYourDebugOption (a temporary folder here); with
 *     developer mode and debug_option 1 debug_VariableInit loads it back,
 *     with debug_option 0 or developer mode off it keeps the retail values;
 *   - nothing reached the decoder as an undecoded register.
 * Then on a Vulkan device (exit 77 without one): a printed glyph changes
 * SCENE where its points fall, and debug_SnapShot writes a PNG under
 * host0:screenshots/.
 *
 * Exit 0, 1 on a mismatch, 77 when there is no device (after the recording
 * checks passed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "vk/rhi_vk.h"
#include "GifHost.h"
#include <sifdev.h>

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------ debug.c's entry points */
void debug_Init(void);
void debug_VariableInit(void);
void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...);
void debug_PrintFontWindow(int col, const char *fmt, ...);
void debug_FlushFont(void);
void debug_Menu(void);
void debug_Menu_off(void);
int debug_SnapShot(int idx);
void dl_Init(void);
void dl_SetDLPriority(int pri);
void dl_Swap(void);
void dl_Clear(void);
extern int debug_window_flag;
extern int debug_font_flag;
extern void debug_ClearFontWindow(void);
extern int debug_hair_tight_level;
extern int debug_zoom_per;
extern int debug_snapshot_size;

/* ------------------------------------------ what the four files import */
int ScreenWidth = 512, ScreenHeight = 512;

float center_X = 2048.0f, center_Y = 2048.0f;

int screenOffsetX, screenOffsetY;

int fbKeep;

void *ios_partition_common, *ios_partition_seki, *ios_partition_sugipon, *ios_partition_smotion,
    *ios_partition_dmotion, *ios_partition_hara, *ios_partition_oomori, *ios_partition_horagai,
    *ios_partition_sound, *ios_partition_sound_semi;

void *dmaVif;

/* port/platform/hwregs.c: the EE timer registers debug_Init starts */
__attribute__((aligned(16))) volatile unsigned char ico_hw_eeio[0x10000];

/* the game's PadState (typedef.h), pad[16] (main.h) */
typedef struct {
    int now, flags, rel, rep, old;
    unsigned int hist[16];
    unsigned char ana[4];
} TestPad;

TestPad pad[16];

int systemStatus[12];

int frame_count, stage_no, game_pause, enable_game_pause, girlControlMode;

int gFlagGameClear, debugAdpcmOn, debugKindOld, seEnvForceClose, scpBoyControlReadDisable;

int mpegPlayReturnStage;

void *boyGObj, *CurrentTargetGObjSub;

/* tables and records debug.c reaches only from the paths not tested */
_Alignas(64) unsigned char mc[16384];

unsigned char db[1024], stageData[106 * 404], seDef[0x600 * 0x48], adpcmFile[0x70 * 0x40],
    initFunc[64 * 64], objKindData[256 * 64], brainGirl[8192], gameSysMainSaveBuff[32768],
    gameSysMemoryFuncList[256], seKind[256], iosPadConfDefault[256];

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

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d %s\n", file, line, mes);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

void mc_Reset(void) {}

float GetTableSin(short angle)
{
    (void)angle;
    return 0.0f;
}

float GetTableCos(short angle)
{
    (void)angle;
    return 1.0f;
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

int sceGsSyncPath(int mode, int timeout)
{
    (void)mode;
    (void)timeout;
    return 0;
}

/* port/game/options.c, driven by the test */
static int s_dev, s_option;

int ico_opt_developer_mode(void)
{
    return s_dev;
}

int ico_opt_debug_option(void)
{
    return s_option;
}

/* port/platform/host_config.c: ico_host0_set_root replaces it here */
int ico_host_pref_dir(char *out, size_t size)
{
    snprintf(out, size, ".");
    return 0;
}

/* The rest: functions the debug menu's other entries and tools call.  None
   runs here; each fails the test if it does. */
#define UNUSED_FN(name)                                                                            \
    int name(void);                                                                                \
    int name(void)                                                                                 \
    {                                                                                              \
        printf("FAIL: %s called\n", #name);                                                        \
        failures++;                                                                                \
        return 0;                                                                                  \
    }

UNUSED_FN(ACTGame_SetActors_Debug)
UNUSED_FN(CameraSetMode)
UNUSED_FN(ClipCollision)
UNUSED_FN(CopyVector)
UNUSED_FN(DebugDisp1Collision)
UNUSED_FN(DebugDispBox)
UNUSED_FN(DebugHintStart)
UNUSED_FN(DrawCollision)
UNUSED_FN(DrawCollisionRay)
UNUSED_FN(EffectTool)
UNUSED_FN(GetRootPosition)
UNUSED_FN(GetWallAttribute)
UNUSED_FN(IsTopHint)
UNUSED_FN(MatrixDrive_GetMatrix)
UNUSED_FN(MatrixDrive_PopMatrix)
UNUSED_FN(MatrixDrive_PushMatrix)
UNUSED_FN(MatrixDrive_TransMatrixV)
UNUSED_FN(MotionViewer)
UNUSED_FN(SgGetSlotStatus)
UNUSED_FN(_ACTDebugPrint)
UNUSED_FN(_AddVector)
UNUSED_FN(backStageDebugTimeZero)
UNUSED_FN(backStageProcessInStage)
UNUSED_FN(brainCheckView)
UNUSED_FN(brainGetLevel)
UNUSED_FN(debug_CameraEditor)
UNUSED_FN(debug_TargetGObj)
UNUSED_FN(debug_WayTool)
UNUSED_FN(gamesysMemoryLoad)
UNUSED_FN(gamesysMemorySave)
UNUSED_FN(gflagOff)
UNUSED_FN(gflagOn)
UNUSED_FN(gsb_Init)
UNUSED_FN(gsb_StageSetting)
UNUSED_FN(iosMcChdirProduct)
UNUSED_FN(iosMcDelete)
UNUSED_FN(iosMcFormat)
UNUSED_FN(iosMcGetBlockSaveInfo)
UNUSED_FN(iosMcGetDir)
UNUSED_FN(iosMcGetInfo)
UNUSED_FN(iosMcLoadGameBlock)
UNUSED_FN(iosMcLoadProductBlock)
UNUSED_FN(iosMcSaveGameBlock)
UNUSED_FN(iosMcSaveIconBlock)
UNUSED_FN(iosMcSaveProductBlock)
UNUSED_FN(iosMcSync)
UNUSED_FN(iosMcTest)
UNUSED_FN(iosMcUnformat)
UNUSED_FN(iosPadConnect)
UNUSED_FN(iosPadGetStick)
UNUSED_FN(iosPadRead)
UNUSED_FN(iosPadStickCameraCoord)
UNUSED_FN(isysGObjGetExist_begin)
UNUSED_FN(isysGObjGetExist_next)
UNUSED_FN(kanbanInit)
UNUSED_FN(prim_DispWireSphere)
UNUSED_FN(sceVu0UnitMatrix)
UNUSED_FN(scpAdpcmPlayRequestFunc)
UNUSED_FN(scpTriggerPosBall)
UNUSED_FN(soundDataSegAllClose)
UNUSED_FN(soundReverbDepthGet)
UNUSED_FN(soundReverbDepthSet)
UNUSED_FN(soundSeDefPlay)
UNUSED_FN(soundSeGroupStop)
UNUSED_FN(staffRollStart)
UNUSED_FN(stgmgrForceSwitch)
UNUSED_FN(tex_ListTool)

/* called by debug_VariableInit */
void ChangeFieldCollisionDebugMode(int on)
{
    (void)on;
}

/* GsBase.h's sceVu0IVECTOR copy (debug_PrintCharacter) */
void _CopyIVector(int *d, const int *s)
{
    memcpy(d, s, 16);
}

/* ---------------------------------------------------- reading the frame */

typedef struct Pt {
    int x, y;
    uint32_t z;
    uint8_t rgba[4];
} Pt;

#define MAX_PTS 65536

typedef struct Pts {
    int n;
    int other; /* list 12 commands that are not point batches */
    Pt p[MAX_PTS];
} Pts;

static Pts s_pts;

static void collectPts(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Pts *o = user;
    const RdFrame *f = rd__LastFrame();
    (void)index;
    (void)s;
    if (list != 12 || c->type != RDC_SCREEN) {
        return;
    }
    if (c->b[0] != RD_PRIM_POINTS || c->b[1] != RD_SPACE_UI) {
        o->other++;
        return;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    for (uint32_t i = 0; i < c->u[1] && o->n < MAX_PTS; i++) {
        o->p[o->n].x = v[i].x;
        o->p[o->n].y = v[i].y;
        o->p[o->n].z = v[i].z;
        memcpy(o->p[o->n].rgba, v[i].rgba, 4);
        o->n++;
    }
}

/* closes the frame and reads list 12's points */
static const Pts *endFrame(void)
{
    dl_Swap();
    const RdFrame *f = rd__LastFrame();
    s_pts.n = 0;
    s_pts.other = 0;
    if (f) {
        RdStateBlock st = f->startState;
        rd__Walk(f, 0, &st, collectPts, &s_pts);
    }
    return &s_pts;
}

/* the points of one character: glyph (alpha 0x70) and outline (0x60) */
static int s_glyph[256], s_outline[256];

static void measureGlyphs(void)
{
    for (int c = 1; c < 256; c++) {
        char s[2] = {(char)c, 0};
        debug_Printf(0, 0, 0xFFFFFF00u, "%s", s);
        const Pts *p = endFrame();
        for (int i = 0; i < p->n; i++) {
            if (p->p[i].rgba[3] == 0x70) {
                s_glyph[c]++;
            } else if (p->p[i].rgba[3] == 0x60) {
                s_outline[c]++;
            }
        }
    }
}

static int strPts(const char *s)
{
    int n = 0;
    for (; *s; s++) {
        n += s_glyph[(unsigned char)*s] + s_outline[(unsigned char)*s];
    }
    return n;
}

static int strGlyphPts(const char *s)
{
    int n = 0;
    for (; *s; s++) {
        n += s_glyph[(unsigned char)*s];
    }
    return n;
}

/* --------------------------------------------------------- the checks */

/* '-' (glyph 0x2D, bitmap rows 00 00 00 7F 00 00 00 00) as
   debug_makeBackImage and debug_MakeFont turn it into points */
static int expectedDash(int px, int py, const uint8_t col[4], Pt *out)
{
    static const unsigned char rows[8] = {0, 0, 0, 0x7F, 0, 0, 0, 0};
    unsigned short a[16] = {0}, b[16] = {0};
    int n = 0;
    for (int j = 0; j < 8; j++) {
        b[j + 1] = (unsigned short)(rows[j] << 1);
    }
    a[0] = b[0] | (b[0] << 1) | (b[0] >> 1) | b[1] | (b[1] << 1) | (b[1] >> 1);
    for (int j = 1; j < 9; j++) {
        a[j] = b[j - 1] | (b[j - 1] << 1) | (b[j - 1] >> 1) | b[j] | (b[j] << 1) | (b[j] >> 1) |
               b[j + 1] | (b[j + 1] << 1) | (b[j + 1] >> 1);
    }
    for (int j = 0; j < 10; j++) {
        a[j] = (unsigned short)(a[j] << 1);
    }
    for (int j = 0; j < 10; j++) {
        a[j] = a[j] & (unsigned short)~b[j];
    }
    for (int j = 0; j < 9; j++) {
        unsigned short m0 = b[j], m1 = a[j];
        for (int k = 0; k < 10; k++, m0 >>= 1, m1 >>= 1) {
            if ((m0 & 1) || (m1 & 1)) {
                Pt *p = &out[n++];
                p->x = ((px + k) * 16) & 0xFFFF;
                p->y = ((py + j * 2) * 16) & 0xFFFF;
                p->z = 0x7FFFFFFFu;
                if (m0 & 1) {
                    memcpy(p->rgba, col, 4);
                } else {
                    static const uint8_t black[4] = {0, 0, 0, 0x60};
                    memcpy(p->rgba, black, 4);
                }
            }
        }
    }
    return n;
}

static void checkFont(void)
{
    const uint8_t col[4] = {0xFF, 0x80, 0x00, 0x70};
    Pt want[128];
    /* debug_PrintCharacter's cursor: x * W / 640 + 2048 - W / 2,
       y * H / 224 + 2048 - H / 2 - 1 */
    int px = 100 * 512 / 640 + 2048 - 256, py = 50 * 512 / 224 + 2048 - 256 - 1;
    int n = expectedDash(px, py, col, want);

    debug_Printf(100, 50, 0xFF800000u, "-");
    const Pts *p = endFrame();
    CHECK(n > 7 && p->n == n, "'-': %d points recorded, %d expected", p->n, n);
    CHECK(p->other == 0, "list 12 holds %d other screen commands", p->other);
    for (int i = 0; i < n && i < p->n; i++) {
        const Pt *a = &p->p[i], *b = &want[i];
        if (a->x != b->x || a->y != b->y || a->z != b->z || memcmp(a->rgba, b->rgba, 4) != 0) {
            CHECK(0, "'-' point %d: %d,%d z %08x rgba %u %u %u %u, expected %d,%d %08x %u %u %u %u",
                  i, a->x, a->y, a->z, a->rgba[0], a->rgba[1], a->rgba[2], a->rgba[3], b->x, b->y,
                  b->z, b->rgba[0], b->rgba[1], b->rgba[2], b->rgba[3]);
            break;
        }
    }

    /* "- -": the second dash two advances to the right (the space's MSCAL 12
       adds no point), the advance being ScreenWidth * 12 / 640 = 9.6 px */
    debug_Printf(100, 50, 0xFF800000u, "- -");
    p = endFrame();
    CHECK(p->n == 2 * n, "'- -': %d points, %d expected", p->n, 2 * n);
    if (p->n == 2 * n) {
        /* the cursor after two advances, in the VU's float arithmetic */
        float adv = (float)512 * 12.0f / 640.0f, cur = (float)px;
        int k0 = p->p[0].x / 16 - px;
        cur += adv;
        cur += adv;
        CHECK(p->p[n].x == (int)(((float)k0 + cur) * 16.0f) && p->p[n].y == p->p[0].y,
              "second dash at %d,%d, first at %d,%d", p->p[n].x, p->p[n].y, p->p[0].x, p->p[0].y);
    }
}

static const char *const kMenuCaptions[11] = {
    "Debug Mode",    "Free Camera",   "Stage Select", "Target Object", "Stage Setting", "Way Test",
    "Camera Editor", "Motion Viewer", "Effect Tool",  "TextureList",   "Snap Shot"};

static void checkMenu(void)
{
    char buf[128];
    const Pts *p;
    int want, frame;

    s_dev = 1;
    systemStatus[1] = 2;
    /* SELECT opens the menu: nothing drawn that tick */
    pad[0].flags = 0x100;
    debug_Menu();
    p = endFrame();
    CHECK(p->n == 0, "the opening tick draws %d points", p->n);
    pad[0].flags = 0;
    /* 66 ticks: the captions are whole (csvScroll 130) and the blink line
       is off on the 66th (menuBlink back at 0) */
    for (frame = 1; frame <= 66; frame++) {
        debug_Menu();
        p = endFrame();
    }
    want = strPts("DEBUG MENU");
    for (int i = 0; i < 11; i++) {
        snprintf(buf, sizeof(buf), "  %s", kMenuCaptions[i]);
        want += strPts(buf);
    }
    want += strPts("FIXED BUG ID LIST") + strPts("B2850   TOO HARD TO PICK UP LASER") +
            strPts("B2890   ILLEGAL CAGE CHAIN DYNAMICS");
    CHECK(p->n == want, "menu: %d points, %d for its strings", p->n, want);
    {
        int cursor = 0;
        for (int i = 0; i < p->n; i++) {
            if (p->p[i].rgba[0] == 0xFF && p->p[i].rgba[1] == 0x40 && p->p[i].rgba[2] == 0x40 &&
                p->p[i].rgba[3] == 0x70) {
                cursor++;
            }
        }
        CHECK(cursor == strGlyphPts("  Debug Mode"), "menu: %d cursor-colour points, %d expected",
              cursor, strGlyphPts("  Debug Mode"));
    }

    /* CIRCLE on "Debug Mode", then the option page */
    pad[0].flags = 0x20;
    debug_Menu();
    endFrame();
    pad[0].flags = 0;
    debug_Menu();
    p = endFrame();
    {
        /* debugOption[70..75], [0..6] (debug.c) and their values after
           debug_VariableInit, systemStatus {1, 2}: "%c%s : %s(%d)" or
           "%c%s : %d", '>' on the cursor's (0) */
        static const struct {
            const char *name, *val;
            int n;
        } opt[13] = {{" LWSKYOMI LOOKONLY  ", "Off", 0},         {" ONE HIT ONLY       ", "Off", 0},
                     {" IGNORE DODGE       ", "Off", 0},         {" GAME CLEAR COUNT   ", NULL, 0},
                     {" GIRL PAD CONTROL   ", "Off", 0},         {" NO BREAST HANG     ", "Off", 0},
                     {" FrameStep          ", "Double", 2},      {" NTSC/PAL           ", "PAL", 1},
                     {" IgnoreDemoCamera   ", "Off", 0},         {" DebugFrameStep     ", NULL, 15},
                     {" DebugFont          ", "Through-Low", 0}, {" DebugFont2         ", "Off", 0},
                     {" DebugFont3         ", "Off", 0}};

        want = strPts("DEBUG MODE") + strPts("Push '\202' to save debug options.");
        for (int i = 0; i < 13; i++) {
            if (opt[i].val != NULL) {
                snprintf(buf, sizeof(buf), "%c%s : %s(%d)", i == 6 ? '>' : ' ', opt[i].name,
                         opt[i].val, opt[i].n);
            } else {
                snprintf(buf, sizeof(buf), "%c%s : %d", ' ', opt[i].name, opt[i].n);
            }
            want += strPts(buf);
        }
        CHECK(p->n == want, "option page: %d points, %d for its strings", p->n, want);
    }
}

static void checkFontWindow(void)
{
    debug_PrintFontWindow(0xFFFFFF00, "hello\n");
    debug_window_flag = 0;
    debug_FlushFont();
    const Pts *p = endFrame();
    CHECK(p->n == 0, "font window off: %d points", p->n);
    debug_window_flag = 1;
    debug_FlushFont();
    p = endFrame();
    CHECK(p->n == strPts("hello"), "font window: %d points, %d for \"hello\"", p->n,
          strPts("hello"));
    debug_window_flag = 0;
}

static void checkOptionFile(const char *root)
{
    char path[1100];
    FILE *f;

    /* TRIANGLE on the option page saves the table (the menu is still on
       the page from checkMenu) */
    debug_hair_tight_level = 55;
    pad[0].flags = 0x10;
    debug_Menu();
    endFrame();
    pad[0].flags = 0;
    CHECK(ico_host0_path("host0:thisIsYourDebugOption", path, sizeof(path), 0) == 0, "path");
    f = fopen(path, "rb");
    CHECK(f != NULL, "%s not written", path);
    if (f) {
        char line[64] = "";
        CHECK(fgets(line, sizeof(line), f) != NULL && strcmp(line, "# FrameStep          \n") == 0,
              "first line '%s'", line);
        fclose(f);
    }
    (void)root;

    /* developer mode on, debug_option 1: the file's values */
    s_dev = 1;
    s_option = 1;
    debug_zoom_per = 0;
    debug_VariableInit();
    CHECK(debug_hair_tight_level == 55, "debug_option 1: hair tight %d, 55 from the file",
          debug_hair_tight_level);
    CHECK(debug_zoom_per == 100, "zoom %d", debug_zoom_per);
    /* debug_option 0: the retail values */
    s_option = 0;
    debug_VariableInit();
    CHECK(debug_hair_tight_level == 20, "debug_option 0: hair tight %d, retail 20",
          debug_hair_tight_level);
    /* developer mode off: the retail values whatever debug_option says */
    s_dev = 0;
    s_option = 1;
    debug_VariableInit();
    CHECK(debug_hair_tight_level == 20, "developer mode off: hair tight %d, retail 20",
          debug_hair_tight_level);
    s_option = 0;
    /* developer mode off: host0: is not reached (retail's cdrom0: path) */
    remove(path);
}

/* The developer text under the mirror mode.  The present
 * flips the picture, so the target the overlay is drawn into (SCENE, or
 * DISPLAY after the reduction) must hold it flipped about its centre (pixel
 * p at w - 1 - p): glyphs, outlines and backdrops together, for an
 * asymmetric string ("F7"), the font window and the developer menu, as for
 * the other UI.  Each overlay is drawn twice per pass: what a frame leaves
 * in the state reaches the next frame's lists, so both passes start from the
 * state the overlay itself leaves. */
static void sceneFrame(RdTargetId id)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(id), id == RD_TARGET_DISPLAY ? (RdTarget){0} : rd_Target(id), 512,
                 id == RD_TARGET_DISPLAY ? 256 : 512, 1);
    rd_ClearTarget(rd_Target(id), black, 1, 0);
}

static void drawOverlay(int kind)
{
    sceneFrame(kind == 3 ? RD_TARGET_DISPLAY : RD_TARGET_SCENE);
    switch (kind) {
    case 0:
    case 3:
        debug_font_flag |= 2;                                       /* the backdrop sprite too */
        debug_Printf(100, kind == 3 ? 130 : 50, 0xFF800000u, "F7"); /* DISPLAY is 256 high */
        break;
    case 1:
        debug_PrintFontWindow(0xFFFFFF00, "hello\nworld 7\n");
        debug_window_flag = 1;
        debug_FlushFont();
        debug_window_flag = 0;
        break;
    default:
        /* SELECT opens the menu, 66 ticks later it is whole */
        dl_Swap();
        for (int step = 0; step < 67; step++) {
            sceneFrame(RD_TARGET_SCENE);
            pad[0].flags = step == 0 ? 0x100 : 0;
            debug_Menu();
            dl_Swap();
        }
        pad[0].flags = 0;
        return;
    }
    dl_Swap();
}

static void checkMirroredText(const char *scale)
{
    static uint8_t a[1024 * 1024 * 4], b[1024 * 1024 * 4];
    static const char *const kName[4] = {"text F7", "font window", "menu", "text F7 on DISPLAY"};
    const int saveFlag = debug_font_flag;
    for (int kind = 0; kind < 4; kind++) {
        const RdTargetId id = kind == 3 ? RD_TARGET_DISPLAY : RD_TARGET_SCENE;
        uint32_t w = 0, h = 0;
        for (int mirror = 0; mirror < 2; mirror++) {
            rd_SetMirror(mirror);
            s_dev = 1;
            systemStatus[1] = 2;
            for (int rep = 0; rep < 2; rep++) {
                if (kind == 2) {
                    debug_Menu_off();
                }
                if (kind == 1) {
                    debug_ClearFontWindow();
                }
                drawOverlay(kind);
            }
            if (!rd__ReadTarget(rd_Target(id), mirror ? b : a, sizeof(a), &w, &h) || w < 512) {
                CHECK(0, "readback (%s, mirror %d)", kName[kind], mirror);
                rd_SetMirror(0);
                debug_font_flag = saveFlag;
                return;
            }
        }
        rd_SetMirror(0);
        int lit = 0, bad = 0;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t *p = &a[((size_t)y * w + x) * 4];
                const uint8_t *q = &b[((size_t)y * w + (w - 1 - x)) * 4];
                lit += p[0] != 0;
                if (memcmp(p, q, 4) != 0 && bad++ < 3) {
                    printf("  %s: differs at %u,%u: %u,%u,%u,%u vs %u,%u,%u,%u\n", kName[kind], x,
                           y, p[0], p[1], p[2], p[3], q[0], q[1], q[2], q[3]);
                }
            }
        }
        printf("  mirrored developer overlay (%s), %s: %d lit pixels, %d differ from the flip\n",
               scale, kName[kind], lit, bad);
        CHECK(lit > 20, "%s drew %d lit pixels", kName[kind], lit);
        CHECK(bad == 0, "%s: %d pixels differ from the flipped unmirrored overlay", kName[kind],
              bad);
    }
    debug_font_flag = saveFlag;
}

static void checkPixels(const char *root)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    char path[1100];

    if (!px) {
        failures++;
        return;
    }
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), black, 1, 0);
    debug_Printf(100, 50, 0xFF800000u, "-");
    dl_Swap();
    if (!rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) || w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        return;
    }
    /* the dash's glyph row: y = py + 8, x = px + 1 .. px + 7 (screen
       coordinates are GS - 2048 + W / 2) */
    {
        int gx = 100 * 512 / 640 + 1 + 3, gy = 50 * 512 / 224 - 1 + 8;
        const uint8_t *in = &px[(gy * 512 + gx) * 4];
        const uint8_t *out = &px[(gy * 512 + gx + 40) * 4];
        CHECK(in[0] > out[0] && in[1] > out[1], "glyph pixel %u,%u,%u vs %u,%u,%u", in[0], in[1],
              in[2], out[0], out[1], out[2]);
    }
    free(px);
    checkMirroredText("Original");

    /* the snapshot: SnapSize None writes nothing, 1x1 a PNG */
    debug_snapshot_size = 0;
    CHECK(debug_SnapShot(0) == -1, "SnapSize None");
    debug_snapshot_size = 1;
    s_dev = 1;
    CHECK(debug_SnapShot(0) == 1, "snapshot");
    snprintf(path, sizeof(path), "%s/screenshots/snap0000000.png", root);
    {
        FILE *f = fopen(path, "rb");
        unsigned char sig[8] = {0};
        CHECK(f != NULL, "%s not written", path);
        if (f) {
            CHECK(fread(sig, 1, 8, f) == 8 && memcmp(sig, "\x89PNG\r\n\x1a\n", 8) == 0,
                  "not a PNG");
            fclose(f);
            remove(path);
        }
    }
    debug_snapshot_size = 0;
}

int main(int argc, char **argv)
{
    rd__SetNotImplementedFatal(true); /* a stub command replayed stops the test */
    char root[1024];

    snprintf(root, sizeof(root), "%s/rd_debug_host0", argc > 1 ? argv[1] : ".");
    ico_host0_set_root(root);

    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    systemStatus[0] = 1;
    systemStatus[1] = 2;
    debug_VariableInit();
    debug_Init();
    measureGlyphs();
    CHECK(s_glyph[' '] == 0 && s_outline[' '] == 0, "space has points");
    CHECK(s_glyph['A'] > 0 && s_outline['A'] > 0, "'A' has no points");
    checkFont();
    checkMenu();
    checkFontWindow();
    checkOptionFile(root);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
    rd_Shutdown();
    if (failures) {
        printf("rd_debug_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("rd_debug_test: recording ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    checkPixels(root);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
    /* the mirrored overlay again at scene scale 2 (Enhanced) */
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 2.0f;
    if (rd_Init(512, 512, &st, NULL)) {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        dl_Clear();
        checkMirroredText("Enhanced 2x");
        rd_Shutdown();
    } else {
        CHECK(0, "rd_Init at scene scale 2");
    }
    if (failures) {
        printf("rd_debug_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_debug_test: ok\n");
    return 0;
}
