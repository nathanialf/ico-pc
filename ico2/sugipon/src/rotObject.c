#include "rotObject.h"
#include "gamesys.h"
#include "memory.h"
#include "fieldCollision.h"
#include "DisplayP2O.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "motionManager2.h"
#include "tableSin.h"
#include "debug.h"
#include <math.h>
#include <libvu0.h>
#include "ios.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include "sceneManager.h"

/* the name every iosMallocDebug in this file reports itself under */
static const char rotObjectFile[] = "src/rotObject.c"; /* derived name */

/* the phase the next rotating object's uniq-data save counter starts at,
   cycling through 30 so the objects' saves fall on different frames */
static unsigned char rotObjectPhase = 0; /* derived name */

/* the 64-byte work block InitRotObjectGeo allocates for a rotating object */
typedef struct RotObjWork { /* field names derived */
    int kind;               /* 0x00, the layout's object word: 3 for a turn limited to a range */
    char pad04[12];
    float pos[4]; /* 0x10, the layout position */
    short angle;  /* 0x20, the drive turn */
    short pad22;
    int turnCount;  /* 0x24, the turn summed over the moves, 65536 a revolution */
    float limitMax; /* 0x28, kind 3: the largest turn, from the layout's scale z */
    float limitMin; /* 0x2C, kind 3: the smallest turn, from the layout's scale x */
    int saveCount;  /* 0x30, the frame counter of the uniq-data save */
    int lock;       /* 0x34, SetRotObjectLockFlag: no move while set */
    float rate;     /* 0x38, the turn per push, the layout's scale y (1 below 0.05) */
    float armScale; /* 0x3C, 100 over the arm radius */
} RotObjWork;       /* derived name */

/* the two words MemoryRotObject saves in the object's gamesys info record and
   RestoreRotObjectExtGeo and GetRotObjectGameSysObjInfoExtData read back */
typedef struct RotObjMemory { /* field names derived */
    unsigned short angle;     /* 0x0 */
    short pad02;
    int turnCount; /* 0x4 */
} RotObjMemory;    /* derived name */

static void moveStartSE(GObj *self)
{
    ExecuteSEPackage(self, 53);
}

static void moveEndSE(GObj *self)
{
    StopSEPackage(self);
    ExecuteSEPackage(self, 58);
}

void RotObjectGeo(GObj *self)
{
    RotObjWork *p = GOBJ_SUB(self)->work;
    if (p->saveCount++ >= 31) {
        p->saveCount = 0;
        gamesysObjInfoUniqDataSet(self);
    }
}

static inline void getRotObjectDriveMatrix(GObj *gobj, void *dst) /* derived name */
{
    float v[4];
    Sub15C *sub = GOBJ_SUB(gobj);
    RotObjWork *w = sub->work;

    GetRootMatrix(MatrixDrive_GetMatrix(), gobj);
    MatrixDrive_RotMatrixY(w->angle);
    _ApplyMatrix(v, MatrixDrive_GetMatrix(), ZUnitVector);
    v[1] = 0.0f;
    _NormalizeVector(v, v);
    UnitRotation(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY(atan2f(v[0], v[2]) * 10430.378f);
    CopyMatrix(dst, MatrixDrive_GetMatrix());
}

void GetRotObjectHoldPoint(void *pos, void *dir, WallCfg *wall, GObj *holder)
{
    struct { /* field names derived */
        float plane[4];
        float pos[4];
        float mtx[4][4];
    } buf;

    GetRootPosition(buf.pos, holder);
    GetGlobalWallPlane(buf.plane, wall);
    sceVu0ScaleVectorXYZ(dir, buf.plane, -1.0f);
    *(int *)((char *)dir + 0xC) = 0;
    AdjustVerticalSidePlaneOfWall(pos, wall, buf.pos, 10.0f);
    GetProjectionPosOfPlane(pos, buf.plane, pos);
    /* the hold point trace, switched off */
    if (0) {
        debug_StdPrintfDummy("%s\n", "GetRotObjectHoldPoint");
        debug_StdPrintfDummy("\t%f, %f, %f\n", ((float *)pos)[0], ((float *)pos)[1],
                             ((float *)pos)[2]);
    }
    MatrixDrive_SetTransposeMatrix(buf.mtx,
                                   (char *)GOBJ_SUB(wall->o.obj)->nodeMtx + (wall->o.node << 6));
    sceVu0ApplyMatrix(pos, buf.mtx, pos);
    sceVu0ApplyMatrix(dir, buf.mtx, dir);
    *(float *)((char *)pos + 4) = -50.0f;
    sceVu0Normalize(dir, dir);
}

int MoveRotObjectWithHoldPoint(GObj *bar, float *hold, void *self, float *dir, float *up)
{
    RotObjWork *w = GOBJ_SUB(bar)->work;
    float q[4];
    float m[16];
    float tm[16];
    float p[4];
    char *h;
    float vy;
    float len;
    float sl;
    float ang;
    float k;

    if (w->lock != 0)
        return 0;
    if (bar->active == 0) {
        w->turnCount = 0;
        return 0;
    }
    getRotObjectDriveMatrix(bar, m);
    {
        float v[4];
        float o[4];
        MatrixDrive_SetTransposeMatrix(tm, m);
        sceVu0ApplyMatrix(q, tm, dir);
        CopyVector(p, up);
        p[1] = 0.0f;
        p[3] = 0.0f;
        _ApplyMatrix(p, tm, p);
        _OuterProduct(v, p, hold);
        vy = v[1];
        len = FSqrt(hold[0] * hold[0] + hold[2] * hold[2]);
        sceVu0ScaleVector(q, q, len / FSqrt(q[0] * q[0] + q[2] * q[2]));
        sceVu0OuterProduct(o, q, hold);
        sl = _GetLengthXZ(q, hold);
        if (o[1] < 0.0f)
            sl = -sl;
        if (sl * vy < 0.0f)
            return 0;
    }
    ang = -atan2f(sl, len);
    ang *= w->rate;
    k = len * 0.01f * w->armScale;
    if (k > 1.0f)
        k = 1.0f;
    k *= k;
    k *= k;
    ang *= k;
    switch (w->kind) {
    case 2:
        if (0.0f <= ang)
            return 0;
        break;
    case 3:
        if (GOBJ_SUB(bar)->parent.obj != 0) {
            float r;

            h = (char *)GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->nodeMtx;
            ((Vec4 *)(h + 0x30))->f[1] += ang * 31.83098793f;
            CopyVector(GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->root.pos,
                       (char *)GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->nodeMtx + 0x30);
            r = -((Vec4 *)(h + 0x30))->f[1];
            if (w->limitMax < r) {
                ((Vec4 *)(h + 0x30))->f[1] = -w->limitMax;
                CopyVector(GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->root.pos,
                           (char *)GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->nodeMtx + 0x30);
                return 0;
            } else if (r < w->limitMin) {
                ((Vec4 *)(h + 0x30))->f[1] = -w->limitMin;
                CopyVector(GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->root.pos,
                           (char *)GOBJ_SUB(GOBJ_SUB(bar)->parent.obj)->nodeMtx + 0x30);
                return 0;
            }
        }
        break;
    }
    /* the function's name trace, switched off */
    if (0) {
        debug_StdPrintfDummy("%s\n", "MoveRotObjectWithHoldPoint");
    }
    w->turnCount += ang * 10430.378f;
    w->angle += ang * 10430.378f;
    return 1;
}

void ExecRotObjectMoveStartReaction(GObj *self)
{
    moveStartSE(self);
}

void ExecRotObjectMoveEndReaction(GObj *self)
{
    moveEndSE(self);
}

void SetRotObjectArmRadius(GObj *self, float radius)
{
    RotObjWork *w = GOBJ_SUB(self)->work;

    w->armScale = 100.0f / radius;
}

void GetRotObjectGlobalHoldGeometry(void *pos, void *dir, GObj *bar, void *holdPos, void *holdDir)
{
    float m[16];

    getRotObjectDriveMatrix(bar, m);
    sceVu0ApplyMatrix(pos, m, holdPos);
    sceVu0ApplyMatrix(dir, m, holdDir);
    /* the function's name trace, switched off */
    if (0) {
        debug_StdPrintfDummy("%s\n", "GetRotObjectGlobalHoldGeometry");
    }
}

RotObjWork *InitRotObjectGeo(GObj *gobj, SObjSimpleSetting *src)
{
    RotObjWork *p = iosMallocDebug(ios_partition_sugipon, sizeof(RotObjWork), rotObjectFile, 57);

    p->saveCount = rotObjectPhase;
    rotObjectPhase = (rotObjectPhase + 1) % 30;

    CopyVector(p->pos, src->pos);
    p->kind = src->obj;
    p->pos[3] = 1.0f;
    p->angle = (short)(int)(src->rot[1] * 32768.0f / 180.0f);
    p->turnCount = 0;
    p->limitMax = p->limitMin = 0.0f;
    p->lock = 0;
    p->rate = src->scale[1] < 0.05f ? 1.0f : src->scale[1];
    p->armScale = 1.0f;

    if (p->kind == 3) {
        p->limitMax = src->scale[2];
        p->limitMin = src->scale[0];
        CopyVector(((SubHandle *)&gobj->dobj)->sub->root.pos, ZeroPoint);
    }
    {
        struct DObjNode *q = GOBJ_SUB(gobj)->nodes;
        q->scale[0] = q->scale[1] = q->scale[2] = 1.0f;
    }
    return p;
}

void GetRotObjectGameSysObjInfoExtData(short *angle, int *turnCount, GamesysObjInfo *info)
{
    RotObjMemory *m = (RotObjMemory *)info->work;

    *angle = m->angle;
    *turnCount = m->turnCount;
}

void RotObjectDL(GObj *gobj)
{
    getRotObjectDriveMatrix(gobj, (void *)GOBJ_SUB(gobj)->nodeMtx);
    p2o_DispVU1(gobj);
}

float GetRotObjectRotCount(GObj *self)
{
    RotObjWork *w = GOBJ_SUB(self)->work;

    return (float)w->turnCount * (1.0f / 65536.0f);
}

/* the +Z unit vector the drive matrix is applied to */
static float zPlusVector[4] = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

int GetRotObjectZPlusDirection(GObj *gobj)
{
    float m[16];
    float v[4];

    getRotObjectDriveMatrix(gobj, m);
    sceVu0ApplyMatrix(v, m, zPlusVector);
    v[1] = 0.0f;
    sceVu0Normalize(v, v);
    return GetTableArcTan2(v[0], v[2]);
}

int RestoreRotObjectGeo(void)
{
    return 1;
}

int RestoreRotObjectExtGeo(GObj *self, GamesysObjInfo *info)
{
    RotObjWork *p = GOBJ_SUB(self)->work;
    RotObjMemory *m = (RotObjMemory *)info->work;

    p->angle = m->angle;
    p->turnCount = m->turnCount;
    return 1;
}

int MemoryRotObject(RotObjMemory *mem, GObj *self)
{
    RotObjWork *p = GOBJ_SUB(self)->work;
    mem->angle = p->angle;
    mem->turnCount = p->turnCount;
    return 1;
}

void SetRotObjectLockFlag(GObj *self, int lock)
{
    RotObjWork *w = GOBJ_SUB(self)->work;

    w->lock = lock;
}
