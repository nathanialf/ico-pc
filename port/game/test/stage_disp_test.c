/*
 * port/game/test/stage_disp_test.c
 *
 * stage_DispAnimation (ico2/seki/src/StageAnimation.c, compiled in below
 * so its record table is in reach) on the disc bodies the main twin serves:
 * the European, Japanese, Korean, Chinese and North American discs.  The
 * European build leaves stageTable row 0x42 (66) out of the walk, the
 * Japanese, Korean and Chinese builds row 62; upstream's block names
 * neither the North American disc nor the August 2001 build, so those draw
 * every row.  On the North American disc row 66 is deja_lightning.bga, the
 * stage animation that places the dream's room (the walls, the floor, the
 * sandstorm sheets: region_ntsc.c veil_anim_row): with the European test
 * applied to it, the dream was the sky alone (issue 221).
 *
 * Two playing rows on each disc: the disc's deja_lightning.bga row
 * (ico_region_info[]->veil_anim_row) and the European skip row 66; each has
 * one object with its display flag on.  Checks, per disc:
 *   - the walk ran: bga_DispLightning called once, every row it reaches
 *     drawn (reg_DispObj with that row's object)
 *   - deja_lightning.bga's row drawn on every disc
 *   - row 66 left out on the European disc, drawn on the North American one
 *     (its deja_lightning.bga row)
 *   - row 62 left out on the Japanese, Korean and Chinese discs, row 66
 *     drawn there
 * And stage_PlayBgAnimation's step on the European, Japanese and North
 * American discs (the frame-rate formula on the last two).
 * CPU only; no disc data beyond the region table.
 */
#include <stdio.h>
#include <string.h>

#include "../../core/region.h"
#include "../../test/ico_check.h"

#include "../../../ico2/seki/src/StageAnimation.c"

/* --- what StageAnimation.c needs ----------------------------------------- */
int ScreenHeight = 448;
int debug_font_flag, debug_font_flag2;
int graphics_ready;
unsigned char *ico_arena_cached_base;
struct IosMemPart *ios_partition_seki;
int lock_execIcoMisc;
StgBgaSet objAction[1];
ObjKindEnt objKindData[1];
GenGeo objLayout[1];
const StgObjDat objTableScene[1];
const StgPre stageData[1];
StageAnimDef stageTable[1];
int stage_no;
int systemStatus[12];

static Sub15C *s_drawn[8];
static int s_drawnCount, s_lightning;

void reg_DispObj(Sub15C *o)
{
    if (s_drawnCount < 8) {
        s_drawn[s_drawnCount] = o;
    }
    s_drawnCount++;
}

void bga_DispLightning(void)
{
    s_lightning++;
}

#ifdef ICO_RD
void rd_note_game_hide(const char *model, int why)
{
    (void)model;
    (void)why;
}
#endif

struct Sub15C *CSVSYSTEM_InitDObj(int id, SObjSimpleSetting *lay)
{
    (void)id;
    (void)lay;
    return 0;
}

void CopyQuaternion(void *dst, void *src)
{
    (void)dst;
    (void)src;
}

void SetIdentityQuaternion(void *q)
{
    (void)q;
}

void _CopyVector(void *dst, void *src)
{
    (void)dst;
    (void)src;
}

void _InitCurrentMatrix(void) {}

void _UnitMatrix(void *dst)
{
    (void)dst;
}

void bga_ApplyDObject(struct BgaDObjEnt *p, GObj **objs, int n, int no)
{
    (void)p;
    (void)objs;
    (void)n;
    (void)no;
}

void bga_CalcAnimation(BgaHeader *p, int loop, int reset)
{
    (void)p;
    (void)loop;
    (void)reset;
}

void bga_CalcSdfCamera(char *data, int loop)
{
    (void)data;
    (void)loop;
}

int bga_CheckAnimationFinish(BgaHeader *p)
{
    (void)p;
    return 0;
}

int bga_CheckAnimationFrame(BgaHeader *p, int frame, int reset)
{
    (void)p;
    (void)frame;
    (void)reset;
    return 0;
}

int bga_CheckAnimationFrameIn(BgaHeader *p, int in, int out)
{
    (void)p;
    (void)in;
    (void)out;
    return 0;
}

int bga_CheckSdfCameraFinish(char *data)
{
    (void)data;
    return 0;
}

int bga_CheckSdfCameraFrame(char *data, int frame, int reset)
{
    (void)data;
    (void)frame;
    (void)reset;
    return 0;
}

int bga_CheckSdfCameraFrameIn(char *data, int in, int out)
{
    (void)data;
    (void)in;
    (void)out;
    return 0;
}

void bga_InitBGA(void) {}

char *bga_InitData(char *data)
{
    return data;
}

void bga_ResetAnimation(void) {}

void bga_ResetCamera(void) {}

void bga_SetCamFrame(char *data, int frame, int mode, int loop)
{
    (void)data;
    (void)frame;
    (void)mode;
    (void)loop;
}

void bga_SetFrame(BgaHeader *p, int frame, int mode, int loop)
{
    (void)p;
    (void)frame;
    (void)mode;
    (void)loop;
}

void bga_SetUniqAnimationFlag(int val)
{
    (void)val;
}

void debug_Assert(char *fmt, ...)
{
    (void)fmt;
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a;
    (void)b;
    (void)c;
    (void)fmt;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    (void)file;
    (void)line;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    (void)file;
    (void)line;
    (void)mes;
}

int freeseki(void *ptr)
{
    (void)ptr;
    return 0;
}

void ico_assert(const char *file, int line, const char *failedexpr)
{
    fprintf(stderr, "FAIL assert %s:%d: %s\n", file, line, failedexpr);
    ico_check_failures++;
}

void *iosFree(void *ptr)
{
    return ptr;
}

void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line)
{
    (void)part;
    (void)size;
    (void)file;
    (void)line;
    return 0;
}

void *isysGObjAdd(void (*fn)(struct GObj *), int a1, int a2)
{
    (void)fn;
    (void)a1;
    (void)a2;
    return 0;
}

void isysGObjKindTableAdd(struct GObj *g, int kind)
{
    (void)g;
    (void)kind;
}

void isysGObjLinkObjDL(void *self, void *dl, unsigned char kind, int key, unsigned int drawMask)
{
    (void)self;
    (void)dl;
    (void)kind;
    (void)key;
    (void)drawMask;
}

GProc *isysGObjProcAdd(GObj *g, void (*fn)(), int noThread, int pri)
{
    (void)g;
    (void)fn;
    (void)noThread;
    (void)pri;
    return 0;
}

void light_KillAllFixLight(void) {}

/* --- the two rows ---------------------------------------------------------- */
static StageAnimDef s_def[2];
static BgaHeader s_head[2];
static GObj s_obj[2];
static Sub15C s_dobj[2];

/* row i: stageTable row no, playing (mode 0), one object with its display
   flag on */
static void setRow(int i, int no)
{
    StageAnim *e = &stageAnimTable[i];

    memset(e, 0, sizeof(*e));
    memset(&s_def[i], 0, sizeof(s_def[i]));
    memset(&s_head[i], 0, sizeof(s_head[i]));
    memset(&s_obj[i], 0, sizeof(s_obj[i]));
    memset(&s_dobj[i], 0, sizeof(s_dobj[i]));
    s_def[i].no = no;
    s_head[i].mode = 0;
    s_dobj[i].disp = 1;
    s_obj[i].dobj = &s_dobj[i];
    e->entry1 = &s_def[i];
    e->entry2 = &s_head[i];
    e->obj[0] = &s_obj[i];
    e->flags.b.count = 1;
}

static int drawn(int i)
{
    for (int k = 0; k < s_drawnCount && k < 8; k++) {
        if (s_drawn[k] == &s_dobj[i]) {
            return 1;
        }
    }
    return 0;
}

static void walk(int region, int otherRow)
{
    const char *slug = region == ICO_REGION_PAL    ? "pal"
                       : region == ICO_REGION_JPN  ? "jpn"
                       : region == ICO_REGION_KOR  ? "kor"
                       : region == ICO_REGION_CN   ? "cn"
                       : region == ICO_REGION_NTSC ? "ntsc"
                                                   : "?";
    const int deja = ico_region_info[region]->veil_anim_row;

    ico_region = region;
    setRow(0, deja);
    setRow(1, otherRow);
    stageAnimCount = 2;
    s_drawnCount = 0;
    s_lightning = 0;
    stage_DispAnimation();

    CHECK(s_lightning == 1, "%s: the walk did not run to its end (%d)", slug, s_lightning);
    CHECK(deja > 0, "%s: no deja_lightning.bga row in the region table", slug);
    CHECK(drawn(0), "%s: deja_lightning.bga's row %d (the dream's room) not drawn", slug, deja);
    printf("stage_disp: %s: row %d (deja_lightning.bga) %s, row %d %s\n", slug, deja,
           drawn(0) ? "drawn" : "left out", otherRow, drawn(1) ? "drawn" : "left out");
}

/* stage_PlayBgAnimation's next frame for one playing row of step 2 from
   frame 10, with the frame step fs (systemStatus[1]) at 60 Hz: the Japanese,
   Korean, Chinese and North American builds scale the step by 30 / (60 /
   fs), the European one adds it as it is at 60 Hz (fs 2 is the game's own,
   where both give 12; fs 1 tells them apart) */
static unsigned char s_arena[256] __attribute__((aligned(16)));

static float step(int region, int fs)
{
    float v[4] = {0};
    float q[4] = {0};
    BgaAnim *a = (BgaAnim *)(s_arena + 64);

    ico_region = region;
    ico_arena_cached_base = s_arena;
    setRow(0, 7);
    s_head[0].start = 0.0f;
    s_head[0].end = 1000.0f;
    s_head[0].step = 2.0f;
    s_head[0].anim = (IcoEEWord)((unsigned char *)a - s_arena);
    stageAnimCount = 1;
    systemStatus[0] = 0;
    systemStatus[1] = fs;
    systemStatus[5] = 0;
    s_drawnCount = 0;
    const float r = stage_PlayBgAnimation(7, 10.0f, v, q);
    CHECK(s_drawnCount == 1, "region %d: the row was not played (%d draws)", region, s_drawnCount);
    systemStatus[1] = 2;
    ico_arena_cached_base = 0;
    return r;
}

int main(void)
{
    /* the step (issue 221's audit): the North American disc takes the
       frame-rate formula, as its build does */
    CHECK(step(ICO_REGION_PAL, 2) == 12.0f);
    CHECK(step(ICO_REGION_PAL, 1) == 12.0f, "pal: the step is added as it is at 60 Hz");
    CHECK(step(ICO_REGION_JPN, 1) == 11.0f);
    CHECK(step(ICO_REGION_NTSC, 2) == 12.0f);
    CHECK(step(ICO_REGION_NTSC, 1) == 11.0f,
          "ntsc: the step not scaled by the frame step (the European arm)");
    printf("stage_disp: step at frame step 1: pal %.1f, ntsc %.1f\n", step(ICO_REGION_PAL, 1),
           step(ICO_REGION_NTSC, 1));

    /* the European disc: row 66 left out, deja_lightning.bga (79) drawn */
    walk(ICO_REGION_PAL, 0x42);
    CHECK(ico_region_info[ICO_REGION_PAL]->veil_anim_row != 0x42);
    CHECK(!drawn(1), "pal: row 66 drawn (the European build leaves it out)");
    CHECK(s_drawnCount == 1, "pal: %d draws, want 1", s_drawnCount);

    /* the Japanese, Korean and Chinese discs: row 62 left out, 66 drawn */
    const int jpnkorcn[] = {ICO_REGION_JPN, ICO_REGION_KOR, ICO_REGION_CN};
    for (int i = 0; i < 3; i++) {
        walk(jpnkorcn[i], 62);
        CHECK(!drawn(1), "region %d: row 62 drawn (that build leaves it out)", jpnkorcn[i]);
        CHECK(s_drawnCount == 1, "region %d: %d draws, want 1", jpnkorcn[i], s_drawnCount);
        walk(jpnkorcn[i], 0x42);
        CHECK(drawn(1), "region %d: row 66 left out (only the European build does)", jpnkorcn[i]);
        CHECK(s_drawnCount == 2, "region %d: %d draws, want 2", jpnkorcn[i], s_drawnCount);
    }

    /* the North American disc: every row drawn; its row 66 IS
       deja_lightning.bga (issue 221) */
    CHECK(ico_region_info[ICO_REGION_NTSC]->veil_anim_row == 0x42,
          "ntsc: deja_lightning.bga is no longer row 66: this test's premise changed");
    walk(ICO_REGION_NTSC, 62);
    CHECK(drawn(1), "ntsc: row 62 left out (upstream's block does not name this disc)");
    CHECK(s_drawnCount == 2, "ntsc: %d draws, want 2", s_drawnCount);

    ico_region = ICO_REGION_PAL;
    return ico_check_summary("stage_disp");
}
