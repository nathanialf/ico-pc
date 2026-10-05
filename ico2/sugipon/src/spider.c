#include "sugiCommon.h"
#include "spider.h"
#include "debug.h"
#include "gamesys.h"
#include "memory.h"
#include "obj_manager.h"
#include "generator.h"
#include "Matrix.h"
#include "Primitive.h"
#include "a_p_1.h"
#include "act_a_p_1.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "spiderGroupManager.h"
#include <stdlib.h>
#include "main.h"
#include "GifPacket.h"
#include "ios.h"

/* the file name and the two group-wake messages */
static const char spiderFile[] = __FILE__; /* derived name */

/* tried to wake a spider group that already has a parent; this is invalid */
static const char spiderWakeHasParentMsg[] =
    "親のいる蜘蛛グループを起こそうとしました。これは無効です\n"; /* derived name */

/* tried to wake a spider group with no parent written in the table; invalid */
static const char spiderWakeNoParentMsg[] =
    "蜘蛛グループを起こそうとしましたが、表に親が書かれていません。これは無効です\n"; /* derived name */

/* The 64-byte record InitSpiderLayoutGeo allocates and hangs at the object's
   work word: the group's state (-1 laid out, 0 entered in the group manager,
   1 awake, 2 calling the master back), the member spiders, and the counters
   SpiderLayoutGeo runs. */
typedef struct SpiderWork { /* field names derived */
    int state;              /* 0x00 */
    char pad04[28];         /* 0x04 */
    int n;                  /* 0x20, member count */
    GObj **members;         /* 0x24, the member AP1 objects */
    int awake;              /* 0x28 */
    unsigned int entryWait; /* 0x2C */
    unsigned int infoWait;  /* 0x30 */
    int kind;               /* 0x34, index into the kind table */
    int wakeFrom;           /* 0x38, first member to wake, -1 for none */
    int revived;            /* 0x3C */
} SpiderWork;               /* derived name */

/* the three words MemorySpiderLayout saves */
typedef struct SpiderMemory { /* field names derived */
    int awake;                /* 0x0 */
    int alive;                /* 0x4 */
    int revived;              /* 0x8 */
} SpiderMemory;               /* derived name */

SpiderWork *InitSpiderLayoutGeo(GObj *self, SObjSimpleSetting *lay)
{
    SObjSimpleSetting l;
    SpiderWork *w;
    int n;
    int i;
    int k;

    w = iosMallocDebug(ios_partition_sugipon, sizeof(SpiderWork), spiderFile, 43);
    l = *lay;

    k = lay->obj;
    n = spiderDef[k].count;
    w->n = n;
    w->kind = k;
    w->members = iosMallocDebug(ios_partition_sugipon, n * sizeof(GObj *), spiderFile, 47);
    w->awake = 0;
    w->state = -1;
    w->entryWait = 0;
    w->infoWait = 0;
    w->wakeFrom = -1;
    w->revived = 0;

    for (i = 0; i < n; i++) {
        l.rot[1] = random_signed_b() * 3.1415927f;
        w->members[i] = MakeAP1GObj(&l);
        SetAP1VisualState(w->members[i], 0);
    }
    return w;
}

static inline void wakeSpiderGroup(GObj *self) /* derived name */
{
    SpiderWork *w;
    int n;
    int i;

    w = GOBJ_SUB(self)->work;

    n = w->n;
    w->awake = 1;
    for (i = 0; i < n; i++) {
        WakeUpAP1(w->members[i]);
        SetAP1VisualState(w->members[i], 1);
    }
}

void WakeUpLayoutedSpiders(GObj *self)
{
    wakeSpiderGroup(self);
    ExecuteSEPackage(self, 106);
}

static inline void setSpiderGroupHost(GObj *self, GObj *host) /* derived name */
{
    SpiderWork *w;
    int i;

    w = GOBJ_SUB(self)->work;
    for (i = 0; i < w->n; i++) {
        if (w->members[i] != 0) {
            SetAP1HostGObj(w->members[i], host);
        }
    }
}

static inline void setSpiderGroupPrior(GObj *self) /* derived name */
{
    SpiderWork *w;
    int i;

    w = GOBJ_SUB(self)->work;
    for (i = 0; i < w->n; i++) {
        if (w->members[i] != 0) {
            SetAP1PriorLevel(w->members[i], 1);
        }
    }
}

static inline void callSpidersToBoy(GObj *self) /* derived name */
{
    if (boyGObj != 0) {
        setSpiderGroupHost(self, boyGObj);
    }
}

static inline int callSpidersToGirl(GObj *self) /* derived name */
{
    if (girlGObj != 0) {
        setSpiderGroupHost(self, girlGObj);
        return 1;
    }
    return 0;
}

int CallSpidersToReviveEnemy(GObj *self)
{
    SpiderWork *w;

    w = GOBJ_SUB(self)->work;
    if (w->state == 1) {
        if (callSpidersToGirl(self)) {
            EntryToSpiderGroupManagerForReviveMaster(self, girlGObj);
            w->state = 2;
            setSpiderGroupPrior(self);
        } else {
            callSpidersToBoy(self);
        }
    }
    return 1;
}

/* the layout entry count SpiderLayoutGeo prints */
static int spiderEntryCount = 0; /* derived name */

static inline void setAllSpiderPositions(GObj *self, float *pos) /* derived name */
{
    SpiderWork *w;
    int n;
    int i;

    w = GOBJ_SUB(self)->work;
    n = w->n;
    for (i = 0; i < n; i++) {
        SetDirectRootPosition(w->members[i], pos);
    }
}

void SpiderLayoutGeo(GObj *self)
{
    float pos[4];
    SpiderWork *w;
    int i;

    w = GOBJ_SUB(self)->work;
    switch (w->state) {
    case -1: {
        GObj *host = self->dobj->parent.obj;

        if (host != 0 && host->kind != 33) {
            setSpiderGroupHost(self, host);
        } else {
            callSpidersToBoy(self);
            if (spiderDef[w->kind].targetGirl == 1) {
                if (callSpidersToGirl(self) == 0) {
                    /* an order came to target the heroine, but this stage has no heroine */
                    debug_StdPrintfDummy(
                        "蜘蛛のターゲットをヒロインにせよと言う命令がありましたが\nこのステージにヒロインはいません。\n");
                }
            }
        }
        if (w->entryWait++ >= 11) {
            if (w->revived == 0) {
                debug_StdPrintfDummy("entry %d\n", spiderEntryCount++);
                EntrySpiderGroupManager(self);
                w->state = 0;
            } else {
                debug_StdPrintfDummy("entry revived %d\n", spiderEntryCount++);
                EntryRevivedSpiderGroupManager(self);
                w->state = 0;
            }
        }
        break;
    }
    case 0:
        if (w->wakeFrom != -1) {
            wakeSpiderGroup(self);
            for (i = w->wakeFrom; i < w->n; i++) {
                iosOmSendMail(w->members[i], 223, w->members[i]);
                SetAP1VisualState(w->members[i], 0);
            }
            w->state = 1;
        } else {
            GObj *gen = self->dobj->parent.obj;

            if (gen != 0 && gen->kind != 33 && IsActCharDead(gen) == 0) {
                if (GOBJ_ACT(gen)->mother != 0) {
                    GetGeneratorSafePosition(pos, GOBJ_ACT(gen)->mother);
                    setAllSpiderPositions(self, pos);
                }
                WakeUpLayoutedSpiders(self);
                w->state = 1;
            }
        }
        break;
    case 2:
    case 3:
        break;
    case 1:
    default: {
        GObj *dead = self->dobj->parent.obj;

        if (dead != 0 && dead->kind != 33) {
            if (IsActCharDead(dead) != 0) {
                CallSpidersToReviveEnemy(self);
            }
        }
        break;
    }
    }

    if (w->infoWait++ >= 31) {
        w->infoWait = 0;
        gamesysObjInfoUniqDataSet(self);
    }
}

/* the debug display's selected line */
int sgSelLine = 0;

/* the white the debug wire sphere is drawn in */
static int spiderWireColor[4] = {255, 255, 255, 255}; /* derived name */

/* the debug display's formats */
static const char spiderStatusFmt[] = "%c SE:%s AI:%s"; /* derived name */

static const char spiderRestoreFmt[] = "restore: %p\n"; /* derived name */

static const char spiderWakeFmt[] = "     WAKE: %s\n"; /* derived name */

static const char spiderAliveFmt[] = "    ALIVE: %d\n"; /* derived name */

static const char spiderReviveFmt[] = "   REVIVE: %d\n"; /* derived name */

void DispAllMemberOfSpider(GObj *self, int *col)
{
    SpiderWork *g;
    GObj *p;
    int i;

    g = GOBJ_SUB(self)->work;
    for (i = 0; i < g->n; i++) {
        if (g->members[i] != 0) {
            _UnitMatrix(MatrixDrive_GetMatrix());
            GetRootPosition(MatrixDrive_GetMatrix()[3], g->members[i]);
            MatrixDrive_RotMatrixX((short)rand());
            MatrixDrive_RotMatrixY((short)rand());
            MatrixDrive_RotMatrixZ((short)rand());
            gif_StartPacketPri(11);
            gif_SetZTest(1);
            gif_SetAlpha(1, 5, 128);
            prim_DispWireSphere(50.0f, col, 4, 4);
            gif_EndPacket();
            debug_PrintfDummy(400, sgInfoLine * 10 + 50,
                              (col[0] << 24) | (col[1] << 16) | (col[2] << 8) | 0xFF,
                              spiderStatusFmt, sgInfoLine == sgSelLine ? 62 : 32,
                              GetAP1Mode(g->members[i]), GetAP1AIMode(g->members[i]));
            if (sgInfoLine == sgSelLine) {
                gif_StartPacketPri(11);
                gif_SetZTest(1);
                gif_SetAlpha(1, 5, 128);
                prim_DispWireSphere(100.0f, spiderWireColor, 4, 4);
                gif_EndPacket();
            }
            sgInfoLine++;
        }
    }

    p = self->dobj->parent.obj;
    if (p != 0 && p->kind != 33) {
        _UnitMatrix(MatrixDrive_GetMatrix());
        GetRootPosition(MatrixDrive_GetMatrix()[3], self->dobj->parent.obj);
        MatrixDrive_RotMatrixX((short)rand());
        MatrixDrive_RotMatrixY((short)rand());
        MatrixDrive_RotMatrixZ((short)rand());
        gif_StartPacketPri(11);
        gif_SetZTest(1);
        gif_SetAlpha(1, 5, 128);
        prim_DispWireSphere(100.0f, col, 4, 4);
        gif_EndPacket();
    }
}

void SetSpiderGroupReviveStatus(GObj *self)
{
    SpiderWork *p = GOBJ_SUB(self)->work;
    p->revived = 1;
    gamesysObjInfoUniqDataSet(self);
    debug_StdPrintfDummy("SET %d\n", self->labelId);
}

int DeadAllSpiders(GObj *gp)
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int i;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            iosOmSendMail(o, 38, o);
        }
    }
    return 0;
}

/* shared by GetAliveSpiders and MemorySpiderLayout */
static inline int CountAliveSpiders(GObj *gp) /* derived name */
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int i;
    int n = 0;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            if (IsActCharDead(o) == 0) {
                n++;
            }
        }
    }
    return n;
}

int GetAliveSpiders(GObj *gp)
{
    Sub15C *oi = gp->dobj;
    SpiderWork *sg = oi->work;
    GObj *own = oi->parent.obj;

    if (own != 0 && own->kind != 33 && IsActCharDead(own) == 0) {
        return sg->n;
    }
    if (sg->awake) {
        return CountAliveSpiders(gp);
    }
    return -1;
}

/* the body of DeleteSpiderFromLayoutGroup, which DeleteAllSpidersOfLayoutGroup
   inlines and DeleteSpiderFromLayoutGroup calls */
static inline GObj *DeleteSpiderFromLayoutGroup_inl(GObj *gp, int idx) /* derived name */
{
    GObj **arr = ((SpiderWork *)GOBJ_SUB(gp)->work)->members;
    GObj *r = arr[idx];
    arr[idx] = 0;
    return r;
}

GObj *DeleteSpiderFromLayoutGroup(GObj *self, int idx)
{
    return DeleteSpiderFromLayoutGroup_inl(self, idx);
}

/* clear the dead members out of a spider group */
static inline void RemoveDeadLayoutSpiders(SpiderWork *sg) /* derived name */
{
    int i;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            if (IsActCharDead(o)) {
                sg->members[i] = 0;
            }
        }
    }
}

int GetNearestOfLayoutSpiders(float *dist, GObj *gp, void *center)
{
    float pos[4];
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int nearest = -1;
    int i;

    RemoveDeadLayoutSpiders(sg);

    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            float d;

            GetRootPosition(pos, o);
            d = distance_squared(center, pos);
            if (d < *dist) {
                nearest = i;
                *dist = d;
            }
        }
    }
    return nearest;
}

int CheckSpidersInsideOfReviveRange(int *out, GObj *gp, void *center)
{
    float pos[4];
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    GObj **p = sg->members;
    int i;
    int n = 0;

    for (i = 0; i < sg->n; i++, p++) {
        if (*p != 0) {
            if (IsActCharDead(*p) == 0) {
                GetRootPosition(pos, *p);

                if (distance_squared(pos, center) < 10000.0f) {
                    out[n] = i;
                    n++;
                }
            }
        }
    }
    return n;
}

int RestoreSpiderLayoutGeo(void)
{
    return 1;
}

int RestoreSpiderLayoutExtGeo(GObj *self, GamesysObjInfo *info)
{
    SpiderWork *p = GOBJ_SUB(self)->work;
    int *ex = info->work;

    if (info->work[0]) {
        p->wakeFrom = ex[1];
    }
    if (ex[2]) {
        p->revived = 1;
    }
    debug_StdPrintfDummy(spiderRestoreFmt, self);
    debug_StdPrintfDummy(spiderWakeFmt, info->work[0] ? "YES" : "NO");
    debug_StdPrintfDummy(spiderAliveFmt, ex[1]);
    debug_StdPrintfDummy(spiderReviveFmt, ex[2]);
    return 1;
}

/* the debug display's line counter */
int sgInfoLine = 0;

int MemorySpiderLayout(SpiderMemory *dst, GObj *gp)
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int awake = sg->awake;

    dst->awake = awake;
    if (awake) {
        dst->alive = CountAliveSpiders(gp);
    } else {
        dst->alive = 0;
    }
    dst->revived = sg->revived;
    return 1;
}

/* shared by WakeUpSpidersFromGenerator and SpiderLayoutGeo */
static inline void SetLayoutedSpidersRootPosition(GObj *gp, void *pos) /* derived name */
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int num = sg->n;
    int i;
    for (i = 0; i < num; i++) {
        SetDirectRootPosition(sg->members[i], pos);
    }
}

void WakeUpSpidersFromGenerator(GObj *gp)
{
    float pos[4];
    GObj *gen = gp->dobj->parent.obj;

    if (gen != 0) {
        if (gen->kind != 33) {
            debug_StdPrintfDummy(spiderWakeHasParentMsg);
            return;
        }
    } else {
        debug_StdPrintfDummy(spiderWakeNoParentMsg);
        return;
    }
    GetGeneratorSafePosition(pos, gen);
    SetLayoutedSpidersRootPosition(gp, pos);
    WakeUpLayoutedSpiders(gp);
}

void DeleteAllSpidersOfLayoutGroup(GObj *gp)
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int i;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            SetAP1DeadStatus(o);
            DeleteSpiderFromLayoutGroup_inl(gp, i);
        }
    }
    sg->n = 0;
}

void SleepSpiderGroup(GObj *gp)
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int i;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            iosOmSendMail(o, 32, o);
        }
    }
}

void WakeupSpiderGroup(GObj *gp)
{
    SpiderWork *sg = GOBJ_SUB(gp)->work;
    int i;
    for (i = 0; i < sg->n; i++) {
        GObj *o = sg->members[i];
        if (o != 0) {
            iosOmSendMail(o, 31, o);
        }
    }
}
