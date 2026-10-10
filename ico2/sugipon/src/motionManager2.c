#include "typedef.h"
#include "eeword.h"
#include "ee_view.h"
#include "sugiCommon.h"
#include "debug.h"
#include "debug_exception.h"
#include "s_init.h"
#include "lineManager.h"
#include "tableSin.h"
#include <string.h>
#include <math.h>
#include "pool.h"
#include "geometryManager.h"
#include "main.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "fieldCollision.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "motionManager.h"
#include "motionManager2.h"
#include <libvu0.h>
#include <assert.h>

/* The head of one stream motion frame GetStreamShapeMotion reads the shape
   weights from: the weights follow the 16-byte head and skipNum 8-byte words,
   shapeNum of them, and only a frame whose shapeMode is 0 carries them. */
typedef struct StreamShapeHdr { /* field names derived */
    char pad0[1];
    signed char shapeMode;
    unsigned char skipNum;
    unsigned char shapeNum;
} StreamShapeHdr; /* derived name */

int GetWaterReaction(float *outH, int *outFlag, ClipWork *info, float *pos, float *vel, float h0,
                     float h1, float h2, float scaleIn, float amp)
{
    float drain[4];
    float waterH;
    float scale;
    float r;
    GObj *pool;

    if (outFlag != 0) {
        *outFlag = 0;
    }
    if (info->floor.elem != 0) {
        if (CompareAttribute(GetFloorAttribute(info), 0x50) != 0) {
            scale = scaleIn;
            pool = info->floor.o.obj;
            waterH = GetPoolGlobalHeightDetail(pool, pos);
            GetPoolGlobalDrainVector(drain, pool);
            if (outFlag != 0) {
                if (1.0000001e-06f < VectorLengthSquare(drain)) {
                    *outFlag = 1;
                }
            }
            if (waterH < h0) {
                vel[1] = vel[1] + amp;
            } else if (waterH < h1) {
                vel[1] = vel[1] + amp * (h1 - waterH) / (h1 - h0);
            } else if (waterH < h2) {
                r = (waterH - h1) / (h2 - h1);
                vel[1] = vel[1] +
                         (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
                              (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1])) +
                          amp * (1.0f - r)) *
                             r;
            } else {
                vel[1] =
                    vel[1] + 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
                                 (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
                scale = 1.0f;
                CopyVector(drain, ZeroVector);
            }
            vel[0] = vel[0] * scale;
            vel[1] = vel[1] * scale;
            vel[2] = vel[2] * scale;
            sceVu0AddVector(vel, vel, drain);
            AddVectorXYZ(pos, pos, vel);
            if (outH != 0) {
                *outH = waterH;
            }
            return 1;
        }
    }
    return 0;
}

#define ABSF(x) ((x) < 0.0f ? -(x) : (x)) /* derived name */

/* a one-line by-value wrapper: each of the six call sites copies the plane
   into its own frame slot and passes that address */
static inline float getPlaneY(Vec4 pl, float *p) /* derived name */
{
    return GetYProjectionOfPlane(pl.f, p);
}

/* The loop counts 0..10 and offsets by 5.  `plane` is retargeted at the
   local copy, so the six by-value argument copies and the CopyVector read
   through one pointer. */
void dispPlane(Vec4 *plane, float *pos)
{
    Vec4 pl = *plane;
    Col4 tmpl = {{0x00, 0x80, 0xFF, 0x80}};
    Col4 col;
    Col4 black;
    Vec4 buf[12];
    float p0[4];
    float p1[4];
    float p2[4];
    float grid[4];
    int i;

    plane = &pl;
    memset(&black, 0, sizeof(black));
    black.c[3] = 0x80;
    CopyVector(grid, pos);
    grid[0] = (float)(int)(pos[0] / 50.0f) * 50.0f;
    grid[2] = (float)(int)(pos[2] / 50.0f) * 50.0f;
    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    gif_SetAlpha(1, 5, 128);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < 11; i++) {
        float ax = 1.0f - ABSF(pos[0] - (grid[0] + (float)(i - 5) * 50.0f)) / 250.0f;
        float az = 1.0f - ABSF(pos[2] - (grid[2] + (float)(i - 5) * 50.0f)) / 250.0f;

        ax = (ax < 0.0f) ? 0.0f : ax;
        az = (az < 0.0f) ? 0.0f : az;
        CopyVector(&buf[10], plane);
        CopyIVector(col.c, tmpl.c);
        col.c[0] = (int)((float)col.c[0] * ax);
        col.c[1] = (int)((float)col.c[1] * ax);
        col.c[2] = (int)((float)col.c[2] * ax);
        CopyVector(p0, grid);
        p0[0] = p0[0] + (float)(i - 5) * 50.0f;
        p0[2] = pos[2] - 250.0f;
        p0[1] = getPlaneY(*plane, p0);
        CopyVector(p1, grid);
        p1[0] = p1[0] + (float)(i - 5) * 50.0f;
        p1[2] = pos[2] + 250.0f;
        p1[1] = getPlaneY(*plane, p1);
        CopyVector(p2, grid);
        p2[0] = p2[0] + (float)(i - 5) * 50.0f;
        p2[2] = pos[2];
        p2[1] = getPlaneY(*plane, p2);
        DrawLineG(p0, &black, p2, &col, 0);
        DrawLineG(p1, &black, p2, &col, 0);
        CopyIVector(col.c, tmpl.c);
        col.c[0] = (int)((float)col.c[0] * az);
        col.c[1] = (int)((float)col.c[1] * az);
        col.c[2] = (int)((float)col.c[2] * az);
        CopyVector(p0, grid);
        p0[0] = pos[0] - 250.0f;
        p0[2] = p0[2] + (float)(i - 5) * 50.0f;
        p0[1] = getPlaneY(*plane, p0);
        CopyVector(p1, grid);
        p1[0] = pos[0] + 250.0f;
        p1[2] = p1[2] + (float)(i - 5) * 50.0f;
        p1[1] = getPlaneY(*plane, p1);
        CopyVector(p2, grid);
        p2[0] = pos[0];
        p2[2] = p2[2] + (float)(i - 5) * 50.0f;
        p2[1] = getPlaneY(*plane, p2);
        DrawLineG(p0, &black, p2, &col, 0);
        DrawLineG(p1, &black, p2, &col, 0);
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

void GetOrientOfWallOfGObj(float *dir, GObj *obj)
{
    CopyVector(dir, obj->dobj->ctrl.wallNormal);
}

void GetOrientOfCliffOfGObj(float *dir, GObj *obj)
{
    CopyVector(dir, obj->dobj->ctrl.cliffNormal);
}

void SetMotionDirection(GObj *self, float *dir)
{
    Sub15C *base = self->dobj;
    struct MotCtrl *s2 = &base->ctrl;
    Sub15C *sub;
    if (dir[0] == 0.0f && dir[2] == 0.0f) {
        return;
    }
    CopyVector(s2->dir, dir);
    s2->dir[1] = 0.0f;
    s2->dir[3] = 1.0f;
    sceVu0Normalize(s2->dir, s2->dir);
    sub = self->dobj;
    if (sub->parent.obj == 0) {
        return;
    }
    LocalizeDirectionOrient(self, &sub->parent);
}

void _GetMotionDirection(float *dir, GObj *obj)
{
    GetGlobalDirectionOrient(dir, obj, obj->dobj->ctrl.dir);
}

void SetMotionDirectionWithLimit(GObj *self, float *dir, float lim0, float lim1)
{
    float q[4];
    float inv[4];
    float m[16];
    float v[4];
    short ang;
    int a;

    GetRootQuaternion(q, self);
    GetInverseQuaternion(inv, q);
    GetMatrixFromQuaternion(m, inv);
    sceVu0ApplyMatrix(v, m, dir);
    sceVu0Normalize((int *)v, (int *)v);
    ang = atan2f(v[0], v[2]) * 10430.378f;
    a = (ang >= 0) ? ang : -ang;
    if (lim1 * 32768.0f / 180.0f < a) {
        return;
    }
    if (a < lim0 * 32768.0f / 180.0f) {
        SetMotionDirection(self, dir);
        return;
    }
    if (ang < 0) {
        RotQuaternionY(q, (short)(int)(-(lim0 * 32768.0f / 180.0f)));
    } else {
        RotQuaternionY(q, (short)(int)(lim0 * 32768.0f / 180.0f));
    }
    GetMatrixFromQuaternion(m, q);
    sceVu0ApplyMatrix(v, m, ZUnitVector);
    SetMotionDirection(self, v);
}

void GetRootPosOfNextFrame(float *pos, GObj *obj)
{
    struct MotRoot *sub = &obj->dobj->root;
    CopyVector(pos, sub->move);
    SubVectorXYZ(pos, pos, sub);
}

void AdjustMotionHeightToField(GObj *obj)
{
    struct MotRoot *sub = &obj->dobj->root;
    sub->footPos[1] = GetYProjectionOfPlane(sub->plane.f, sub->footPos);
    debug_StdPrintfDummy("Adjust Motion Height To Field. --------------\n");
}

void GetLowerPlaneCollision(ClipWork *w, float *pos)
{
    CopyVector(w->pt[0], pos);
    CopyVector(w->pt[1], w->pt[0]);
    w->pt[1][1] = w->pt[1][1] + 10000.0f;
    ClipFloor(w);
}

static void getLowerPlaneCollisionE(ClipWork *w, float *pos)
{
    CopyVector(w->pt[0], pos);
    CopyVector(w->pt[1], w->pt[0]);
    w->pt[1][1] = w->pt[1][1] + 10000.0f;
    ClipFloorE(w);
}

/* inlined into AdjustMotionHeightToNearestField and InitMotionGeoInfo */
static inline int adjustMotionHeightToNearestField(struct MotRoot *r, float *pos) /* derived name */
{
    ClipWork buf;
    float p[4];

    CopyVector(p, pos);
    p[1] = p[1] - 100.0f;
    if (r->filter.o.obj != 0) {
        buf.filter = r->filter;
        getLowerPlaneCollisionE(&buf, p);
    } else {
        GetLowerPlaneCollision(&buf, p);
    }
    if (buf.floor.elem == 0) {
        return 0;
    }
    CopyVector((&r->plane), (&buf.normal));
    sceVu0CopyVector(r->footPos, buf.pt[2]);
    r->footPos[3] = 1.0f;
    return 1;
}

/* RotQuaternionZ's second parameter is `int`, not `short`: calcFootIK's dev line
   1035 passes the raw GetTableArcSin result with no sign extension, and its 1040
   site sign-extends explicitly.  InitMotionGeoInfo's site carries the (short). */

static int calcFootIK(SkelNode *skel, char *arg, int node, float scale, float ratio)
{
    float q0[4];
    float q1[4];
    float qa[4];
    float qb[4];
    float qc[4];
    float qi[4];
    float v[4];
    float dir[4];
    float qt[4];
    float qu[4];
    float qv[4];
    SkelNode *p;
    char *dstq;
    char *qk;
    float *m;
    int i;
    int j;
    int k;
    int n;
    short ang;
    short ang2;

    memset(qa, 0, 16);
    qa[3] = 1.0f;
    memset(qb, 0, 16);
    qb[3] = 1.0f;
    memset(qc, 0, 16);
    qc[3] = 1.0f;

    SetIdentityQuaternion(qi);
    RotQuaternionX(qi, -0x8000);
    RotQuaternionY(qi, -0x8000);

    i = node;
    while (i != -1) {
        MultiQuaternion(qb, arg + i * 32 + 16, qb);
        MultiQuaternion(qc, skel[i].quat, qc);
        i = skel[i].parent;
    }
    MultiQuaternion(qc, qi, qc);

    i = skel[node].parent;
    while (i != -1) {
        MultiQuaternion(qa, arg + i * 32 + 16, qa);
        i = skel[i].parent;
    }

    DivQuaternion(q0, qc, qa);
    DivQuaternion(q1, qb, qc);

    p = &skel[node];

    GetMatrixFromQuaternion(MatrixDrive_GetMatrix(), qc);
    MatrixDrive_PushMatrix();
    MultiMatrixByQuaternion(q1);
    for (n = 0; n < 2; n++) {
        j = p->child;
        if (j == -1) {
            return 0;
        }
        p = &skel[j];
        sceVu0ScaleVectorXYZ(v, p->pos, scale);
        MatrixDrive_TransMatrixV(v);
        MultiMatrixByQuaternion(arg + j * 32 + 16);
    }
    CopyVector(dir, (MatrixDrive_GetMatrix()[3]));
    MatrixDrive_PopMatrix();

    m = (float *)MatrixDrive_GetMatrix();
    sceVu0TransposeMatrix(m, m);
    sceVu0ApplyMatrix(dir, m, dir);
    sceVu0Normalize((int *)dir, (int *)dir);
    ang = GetTableArcSin(dir[1]);
    ang2 = -GetTableArcSin(dir[2]);

    CopyQuaternion(qt, qc);
    RotQuaternionZ(qt, ang);
    RotQuaternionY(qt, ang2);
    DivQuaternion(qt, qb, qt);

    dstq = arg + node * 32 + 16;
    CopyQuaternion(dstq, q0);
    RotQuaternionZ(dstq, (short)(int)((float)ang * ratio));
    RotQuaternionY(dstq, ang2);
    MultiQuaternion(dstq, dstq, qt);

    CopyQuaternion(qv, qb);
    k = node;
    for (n = 1; n >= 0; n--) {
        k = skel[k].child;
        qk = arg + k * 32 + 16;
        MultiQuaternion(qv, qv, qk);
    }
    CopyQuaternion(qu, qa);
    MultiQuaternion(qu, qu, arg + node * 32 + 16);
    MultiQuaternion(qu, qu, arg + skel[node].child * 32 + 16);
    DivQuaternion(qk, qv, qu);
    return ang;
}

/* The motion geometry record the actor sub-object carries at its own +0xA0,
   and the default every actor starts from: the same block typedef.h's MotRoot
   describes, and its fields carry MotRoot's names at the same offsets (the
   readers and writers are motionManager.c's and the rootUpdates').
   InitMotionGeoInfo writes the position at 0x0 and the root quaternion at
   0x30, SetSimplePlane builds the field plane at 0x130, GetRootPosOfNextFrame
   reads the next-frame position at 0x90 and AdjustMotionHeightToField
   projects the field position at 0x1B0 onto that plane.  The two hand records
   at 0x210 and 0x270 are typedef.h's HandRec.  MotRoot leaves 0x10C to 0x11F
   and 0x1E0 to 0x1FF unread; this default puts -1 and -1.0f there, so those
   words keep type-and-offset names.  The default is this record and not a
   struct MotRoot because MotRoot is 16-byte aligned (its plane) and the
   default is 8-byte aligned in the object's .data. */
typedef struct {     /* field names derived */
    Vec4 pos;        /* 0x0, the position InitMotionGeoInfo is handed */
    Vec4 trans;      /* 0x10 */
    Vec4 baseQuat;   /* 0x20 */
    Vec4 rot;        /* 0x30, the root quaternion */
    Vec4 motionQuat; /* 0x40 */
    short twist;     /* 0x50 */
    char pad52[2];
    float twistRate; /* 0x54 */
    char pad58[8];
    Vec4 up;        /* 0x60 */
    Vec4 savePos;   /* 0x70, takes a copy of the initial position */
    ObjNode hitObj; /* 0x80 */
    char pad88[8];
    Vec4 nextPos;  /* 0x90, the root position of the next frame */
    Vec4 step;     /* 0xA0 */
    Vec4 itemQuat; /* 0xB0 */
    float height;  /* 0xC0 */
    char padC4[12];
    Vec4 delta;         /* 0xD0 */
    WallCfg wall;       /* 0xE0 */
    int wallCount;      /* 0xEC */
    WallCfg cliffWall;  /* 0xF0 */
    int cliffWallCount; /* 0xFC */
    WallCfg aheadWall;  /* 0x100 */
    int word10C;

    /* PC port: InitMotionGeoInfo copies this record over a MotRoot, so the
       host layout must be MotRoot's.  MotRoot has 20 unread bytes at 0x10C
       (not a WallCfg, which is 24 bytes on a 64-bit host) and a 16-byte
       aligned plane; with the EE types every field from filter on landed 8
       or 16 bytes off on x64 (armTwist read as 0: package 2I). */
    struct {
        int o[2];
        int elem;
    } wall110;

    int word11C;
    WallCfg filter; /* 0x120 */
    char pad12C[4];
    Vec16 plane;      /* 0x130, the field plane under the actor */
    int lastField;    /* 0x140 */
    void *cliffFloor; /* 0x144 */
    char pad148[8];
    Vec4 last;     /* 0x150, takes a copy of the initial position */
    Vec4 clipFrom; /* 0x160, takes a copy of the initial position */
    Vec4 stepMove; /* 0x170 */
    int standNode; /* 0x180, -1 for none */
    char pad184[12];
    Vec4 focusPos;    /* 0x190 */
    Vec4 focusLocal;  /* 0x1A0 */
    Vec4 fieldPos;    /* 0x1B0, the position projected onto the field plane */
    Vec4 reservePos;  /* 0x1C0 */
    float projHeight; /* 0x1D0 */
    char pad1D4[12];
    Vec4 vec1E0;
    Vec4 vec1F0;
    int liftOn;    /* 0x200 */
    int lifting;   /* 0x204 */
    float lift[2]; /* 0x208 */
    HandRec hand1; /* 0x210 */
    HandRec hand0; /* 0x270 */
    Vec4 armTwist; /* 0x2D0 */
    int lookMode;  /* 0x2E0 */
    char pad2E4[12];
    Vec4 lookPos; /* 0x2F0 */
    short h;      /* 0x300 */
    short p;      /* 0x302 */
    short b;      /* 0x304 */
    char pad306[2];
    int noStepSearch; /* 0x308 */
    int gravity;      /* 0x30C */
    int slopeIK;      /* 0x310 */
    int stairStep;    /* 0x314 */
    int lookIK;       /* 0x318 */
    int handTurnIK;   /* 0x31C */
    int fieldWall;    /* 0x320 */
    int fuchiMode;    /* 0x324 */
    int cylinder;     /* 0x328 */
    int avgWallPlane; /* 0x32C */
    int flag330;      /* 0x330 */
    int flag334;      /* 0x334 */
    float radius;     /* 0x338 */
    float radiusTo;   /* 0x33C */
    float radiusFrom; /* 0x340 */
    char pad344[12];
    Vec4 cliffPlane; /* 0x350 */
    int handIK;      /* 0x360 */
    int stepNode;    /* 0x364 */
    char pad368[8];
    Vec4 holdPoint;  /* 0x370 */
    int ropeState;   /* 0x380 */
    ICO_WORD fixObj; /* 0x384, MotRoot's type: a word wide enough for a host pointer */
    int fixNode;     /* 0x388 */
    char pad38C[4];
    Vec4 fixQuat;         /* 0x390 */
    Vec4 fixPos;          /* 0x3A0 */
    float fixWeight;      /* 0x3B0 */
    unsigned int fixMode; /* 0x3B4 */
    float footIKRate;     /* 0x3B8 */
    float ikRate0;        /* 0x3BC */
    float handRate;       /* 0x3C0 */
    float ikRate1;        /* 0x3C4 */
    float ikRate2;        /* 0x3C8 */
    char pad3CC[4];
} MotionGeoInfo; /* derived name */

/* InitMotionGeoInfo's copy is only right if the two layouts agree: every
   member at MotRoot's member of the same name (or the one at its EE offset:
   rot is quat, nextPos move, fieldPos footPos), the words MotRoot keeps as
   pads (0x10C to 0x11F, 0x1E0 to 0x1FF) inside those pads, and the same size
   (tools/template_audit.py). */
/* clang-format off */
#define MGI_FIELDS(X) /* derived name */ \
    X(pos) X(trans) X(baseQuat) X(motionQuat) X(twist) X(twistRate) X(up) X(savePos) X(hitObj) \
    X(step) X(itemQuat) X(height) X(delta) X(wall) X(wallCount) X(cliffWall) X(cliffWallCount) \
    X(aheadWall) X(filter) X(plane) X(lastField) X(cliffFloor) X(last) X(clipFrom) X(stepMove) \
    X(standNode) X(focusPos) X(focusLocal) X(reservePos) X(projHeight) X(liftOn) X(lifting) \
    X(lift) X(hand1) X(hand0) X(armTwist) X(lookMode) X(lookPos) X(h) X(p) X(b) X(noStepSearch) \
    X(gravity) X(slopeIK) X(stairStep) X(lookIK) X(handTurnIK) X(fieldWall) X(fuchiMode) \
    X(cylinder) X(avgWallPlane) X(flag330) X(flag334) X(radius) X(radiusTo) X(radiusFrom) \
    X(cliffPlane) X(handIK) X(stepNode) X(holdPoint) X(ropeState) X(fixObj) X(fixNode) X(fixQuat) \
    X(fixPos) X(fixWeight) X(fixMode) X(footIKRate) X(ikRate0) X(handRate) X(ikRate1) X(ikRate2)
/* clang-format on */
#define MGI_SAME(f) ICO_LAYOUT_AT(MotionGeoInfo, f, struct MotRoot, f);

MGI_FIELDS(MGI_SAME)
ICO_LAYOUT_AT(MotionGeoInfo, rot, struct MotRoot, quat);

ICO_LAYOUT_AT(MotionGeoInfo, nextPos, struct MotRoot, move);

ICO_LAYOUT_AT(MotionGeoInfo, fieldPos, struct MotRoot, footPos);

_Static_assert(__builtin_offsetof(MotionGeoInfo, word10C) >=
                       __builtin_offsetof(struct MotRoot, _pad10C) &&
                   __builtin_offsetof(MotionGeoInfo, wall110) >=
                       __builtin_offsetof(struct MotRoot, _pad10C) &&
                   __builtin_offsetof(MotionGeoInfo, word11C) + 4 <=
                       __builtin_offsetof(struct MotRoot, filter),
               "MotionGeoInfo's words at 0x10C are not inside MotRoot's pad");

_Static_assert(__builtin_offsetof(MotionGeoInfo, vec1E0) >=
                       __builtin_offsetof(struct MotRoot, _pad1D4) &&
                   __builtin_offsetof(MotionGeoInfo, vec1F0) + 16 <=
                       __builtin_offsetof(struct MotRoot, liftOn),
               "MotionGeoInfo's vectors at 0x1E0 are not inside MotRoot's pad");

ICO_LAYOUT_SIZE(MotionGeoInfo, struct MotRoot);

#undef MGI_SAME
#undef MGI_FIELDS

/* the record InitMotionGeoInfo copies over every new actor's geometry
   state */
static MotionGeoInfo motionGeoInfoTemplate = {
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0,
    {0},
    1.0f,
    {0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {0, -1},
    {0},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0.0f,
    {0},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    {{0, -1}, 0},
    -1,
    {{0, -1}, 0},
    -1,
    {{0, -1}, 0},
    -1,
    {{0, -1}, 0},
    -1,
    {{0, -1}, 0},
    {0},
    {{0.0f, -1.0f, 0.0f, 0.0f}},
    0,
    0,
    {0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    -1,
    {0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0.0f,
    {0},
    {{0.0f, -1.0f, 0.0f, 0.0f}},
    {{0.0f, -1.0f, 0.0f, 0.0f}},
    1,
    0,
    {0.0f, 0.0f},
    {0,
     0,
     -1,
     {0},
     {0.0f, 0.0f, 0.0f, 1.0f},
     0,
     0,
     {0},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     0.5f,
     0,
     0},
    {0,
     0,
     -1,
     {0},
     {0.0f, 0.0f, 0.0f, 1.0f},
     0,
     0,
     {0},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     0.5f,
     0,
     0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0,
    {0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0,
    0,
    0,
    {0},
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0.0f,
    0.0f,
    0.0f,
    {0},
    {{0.0f, -1.0f, 0.0f, 0.0f}},
    1,
    0,
    {0},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    0,
    0,
    0,
    {0},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    0.0f,
    0,
    1.0f,
    0.09f,
    0.15f,
    0.1f,
    0.1f,
}; /* derived name */

/* The motion state record the actor sub-object carries at its own +0x470,
   and the default every actor starts from: the block typedef.h's MotCtrl
   describes, with MotCtrl's names at the same offsets.  SetMotionDirection
   writes the direction at 0xB0, SetMotionPlaySpeedRatio the ratio at 0x48,
   ForMotionViewer_GetCurrentMotion and ForMotionViewer_GetCurrentAnimationFrame
   read the motion at 0x30 and the frame at 0x3C, CheckPureWallAttribute,
   CheckPureCliffAttribute, CheckWallAttribute and CheckFloorAttribute the four
   attributes at 0x17C to 0x188, GetRopeHangablePos the height at 0x1A8, and
   InitMotionStateInfo itself writes the two sound groups at 0x1AC.  Its
   vectors are Vec4, so the default is 8-byte aligned; with MotCtrl's float
   arrays the template copy is a word copy with an alignment test (measured).
   PC port: the record is copied whole over a MotCtrl, so on the host it
   must have MotCtrl's host layout.  Vec4's long long view aligns it to 8
   bytes where MotCtrl's float[4] has 4, and MotCtrl's pickedWeapon is a
   pointer: with the EE spelling every field from dir on landed 4 bytes
   late (floorFit took orientKind's 0, so no actor fitted the root to the
   floor in a direct-move motion).  The host
   spells those members with MotCtrl's alignment and width; the asserts
   after InitMotionStateInfo check every offset. */

typedef union { /* derived name */
    float f[4];
} MsiVec4; /* derived name */

#define MSI_VEC4 MsiVec4  /* derived name */
#define MSI_WORD ICO_WORD /* derived name */

typedef struct MotionStateInfo { /* field names derived */
    int stream;                  /* 0x0 */
    int oriFrom;                 /* 0x4 */
    int oriTo;                   /* 0x8 */
    int shifted;                 /* 0xC */
    int ctrlFlags;               /* 0x10 */
    unsigned int flags;          /* 0x14 */
    int shiftStop;               /* 0x18 */
    int *shiftReq;               /* 0x1C */
    int *shiftNext;              /* 0x20 */
    int shiftFrom;               /* 0x24 */
    int shiftMode;               /* 0x28 */
    int request;                 /* 0x2C */
    int motion;                  /* 0x30 */
    int noAlt;                   /* 0x34 */
    int shiftReady;              /* 0x38 */
    float animFrame;             /* 0x3C */
    float lastFrame;             /* 0x40 */
    float playTime;              /* 0x44 */
    float speedRatio;            /* 0x48 */
    float playRate;              /* 0x4C */
    float frameRatio;            /* 0x50 */
    int waterDrag;               /* 0x54 */
    int justShifted;             /* 0x58 */
    int frameEnd;                /* 0x5C */
    int keepUpdateMode;          /* 0x60 */
    int updateModeChanged;       /* 0x64 */
    int rootUpdateMode;          /* 0x68 */
    int parallelEnded;           /* 0x6C */
    int parallel;                /* 0x70 */
    int orientUpdateOff;         /* 0x74 */
    int noStand;                 /* 0x78 */
    int posReserve;              /* 0x7C */
    int loopFlag;                /* 0x80 */
    int reserveBlend;            /* 0x84 */
    int reserveMoved;            /* 0x88 */
    int step;                    /* 0x8C */
    int orientReq;               /* 0x90 */
    int lastMotion;              /* 0x94 */
    int lastNoAlt;               /* 0x98 */
    int shiftFrame;              /* 0x9C */
    int blendCount;              /* 0xA0 */
    int blendFrames;             /* 0xA4 */
    char padA8[8];
    MSI_VEC4 dir;       /* 0xB0 */
    MSI_VEC4 lastDir;   /* 0xC0 */
    int orientKind;     /* 0xD0 */
    int floorFit;       /* 0xD4 */
    int wallReact;      /* 0xD8 */
    int cliffWallCheck; /* 0xDC */
    int catchBoy;       /* 0xE0 */
    int sideWallCheck;  /* 0xE4 */
    int variation;      /* 0xE8 */
    float fallHeight;   /* 0xEC */
    float groundHeight; /* 0xF0 */
    int wallHit;        /* 0xF4 */
    int cliffEdge;      /* 0xF8 */
    int cliffWallHit;   /* 0xFC */
    int cliffBack;      /* 0x100 */
    int fieldWallHit;   /* 0x104 */
    int upperWall;      /* 0x108 */
    int sideWall;       /* 0x10C */
    float cliffHeight;  /* 0x110 */
    float cliffDist;    /* 0x114 */
    char pad118[8];
    MSI_VEC4 cliffNormal;  /* 0x120 */
    float wallFloorHeight; /* 0x130 */
    float wallTopHeight;   /* 0x134 */
    float wallDist;        /* 0x138 */
    char pad13C[4];
    MSI_VEC4 wallDir;        /* 0x140 */
    MSI_VEC4 wallNormal;     /* 0x150 */
    MSI_VEC4 sideWallNormal; /* 0x160 */
    float upperWallDist;     /* 0x170 */
    float sideWallDist;      /* 0x174 */
    float cliffDepth;        /* 0x178 */
    int pureWallAttr;        /* 0x17C */
    int pureCliffAttr;       /* 0x180 */
    int wallAttr;            /* 0x184 */
    int floorAttr;           /* 0x188 */
    char pad18C[4];
    int frameFlag1;        /* 0x190 */
    int frameFlag2;        /* 0x194 */
    int trigger1;          /* 0x198 */
    int trigger1Done;      /* 0x19C */
    int trigger2;          /* 0x1A0 */
    int trigger2Done;      /* 0x1A4 */
    float ropeHangPos;     /* 0x1A8 */
    int seGroup[2];        /* 0x1AC */
    int slipFlags;         /* 0x1B4 */
    int lastSlipFlags;     /* 0x1B8 */
    int slipOn;            /* 0x1BC */
    MSI_WORD pickedWeapon; /* 0x1C0 */
    int keepWall;          /* 0x1C4 */
    int keepStand;         /* 0x1C8 */
    int landed;            /* 0x1CC */
    float waterY;          /* 0x1D0 */
    float waterDepth;      /* 0x1D4 */
    GObj *pool;            /* 0x1D8 */
    int contactFlags;      /* 0x1DC */
    int mailDelay;         /* 0x1E0 */
    int noFieldClip;       /* 0x1E4 */
    int seMute;            /* 0x1E8 */
    char pad1EC[4];
} MotionStateInfo; /* derived name */

/* the record InitMotionStateInfo copies over every new actor's motion
   state */
static MotionStateInfo motionStateInfoTemplate = {
    -1,
    -1,
    -1,
    0,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
    0.0f,
    1,
    0,
    0,
    0,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    -1,
    0,
    0,
    0,
    1,
    0,
    {0},
    {{0.0f, 0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, 1.0f, 0.0f}},
    0,
    1,
    1,
    1,
    1,
    0,
    0,
    0.0f,
    0.0f,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1.5e+02f,
    0.0f,
    {0},
    {{0.0f, 0.0f, 1.0f, 1.0f}},
    0.0f,
    0.0f,
    7e+02f,
    {0},
    {{0.0f, 0.0f, 1.0f, 1.0f}},
    {{0.0f, 0.0f, 1.0f, 1.0f}},
    {{0.0f, 0.0f, 1.0f, 1.0f}},
    0.0f,
    0.0f,
    0.0f,
    0,
    0,
    0,
    0,
    {0},
    0,
    0,
    0,
    0,
    0,
    0,
    1e+02f,
    {-1, -1},
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    0.0f,
    0.0f,
    0,
    0,
    0,
    0,
    0,
}; /* derived name */

void InitMotionGeoInfo(struct MotRoot *self, float x, float y, float z, float rx, float ry,
                       float rz)
{
    *(MotionGeoInfo *)self = motionGeoInfoTemplate;
    self->pos[0] = x;
    self->pos[1] = y;
    self->pos[2] = z;
    CopyVector(self->last, self->pos);
    CopyVector(self->savePos, self->pos);
    CopyVector(self->clipFrom, self->pos);
    RotQuaternionY(self->quat, -(int)(ry * 10430.378f));
    RotQuaternionX(self->quat, -(int)(rx * 10430.378f));
    RotQuaternionZ(self->quat, (short)-(int)(rz * 10430.378f));
    RegularizeQuaternion(self->quat);
    adjustMotionHeightToNearestField(self, self->pos);
    SetSimplePlane(self->plane.f, 0.0f, -1.0f, 0.0f, y);
    CopyVector(self->footPos, self->pos);
}

/* The skeleton-display state DispSkelton hands to dispSkeltonHierarchy
   through file scope: the object, the motion and the nodes.  Nothing reads
   skelMotion back.  The motion is held as a word: typed void *, the three
   stores in DispSkelton reorder (measured). */
static void *skelGObj; /* derived name */

static ICO_WORD skelMotion; /* derived name */

static SkelNode *skelNodes; /* derived name */

/* a file static; ico2/sugipon/src/motionManager.c has its own of the same
   name */
static void dispSkeltonHierarchy(int node)
{
    if (skelNodes[node].parent != -1) {
        float o[3] = {0.0f, 0.0f, 0.0f};
        float p[3] = {skelNodes[node].pos[0], skelNodes[node].pos[1], skelNodes[node].pos[2]};
        float ax[3] = {0.0f, 5.0f, 0.0f};
        float ay[3] = {0.0f, 0.0f, 5.0f};
        float az[3] = {5.0f, 0.0f, 0.0f};
        Col4 c0 = {{0x40, 0x40, 0x40, 0x80}};
        Col4 c1 = {{0x00, 0xFF, 0x00, 0x80}};
        Col4 c2 = {{0x00, 0x80, 0xFF, 0x80}};
        Col4 c3 = {{0xFF, 0x00, 0x00, 0x80}};

        DrawLineG(o, &c0, p, &c0, -1);
        DrawLineG(o, &c0, ax, &c1, -1);
        DrawLineG(o, &c0, ay, &c2, -1);
        DrawLineG(o, &c0, az, &c3, -1);
    }
    MatrixDrive_PushMatrix();
    CopyMatrix(MatrixDrive_GetMatrix(), (char *)((GObj *)skelGObj)->dobj->nodeMtx + node * 64);
    if (skelNodes[node].child == -1) {
        float o2[3] = {0.0f, 0.0f, 0.0f};
        float e[3] = {10.0f, 0.0f, 0.0f};
        Col4 c = {{0xFF, 0xFF, 0xFF, 0x80}};

        DrawLineG(o2, &c, e, &c, -1);
    }
    if (skelNodes[node].child != -1) {
        dispSkeltonHierarchy(skelNodes[node].child);
    }
    MatrixDrive_PopMatrix();
    if (skelNodes[node].sibling != -1) {
        dispSkeltonHierarchy(skelNodes[node].sibling);
    }
}

/* SetSkeltonDispSwitch's switch for DispSkelton's debug draw */
static int skeltonDispSwitch = 0; /* derived name */

void DispSkelton(GObj *self, ICO_WORD_PTR(void *) motion)
{
    /* the skeleton, read as a void * word */
    skelNodes = ICO_RAW(void *, GOBJ_SUB(self), 0x8C, GOBJ_SUB(self)->skel);
    skelMotion = (ICO_WORD)motion;
    skelGObj = self;

    if (skeltonDispSwitch) {
        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 128);
        MatrixDrive_PushMatrix();
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        dispSkeltonHierarchy(0);
        MatrixDrive_PopMatrix();
        gif_EndPacket();
    }
}

/* the motion record table SlopeIKControl indexes by the IK block's 0x30
   word; the two slope rates are the only fields this file reaches.  Declared
   here: motionOrientManager.h reaches ico2/fumi's files through typedef.h,
   and commonact.c declares the table char []. */
extern const MotionDef motionKind[];

/* SlopeIKControl's two slope helpers */
static inline float getSlopeDifference(GObj *self, char *arg, Sub15C *p) /* derived name */
{
    char *q = arg + 0x10;
    float v[4];
    Vec4 up = {{0.0f, 1.0f, 0.0f, 1.0f}};
    float y0;
    float y1;

    GetRootMatrix(MatrixDrive_GetMatrix(), self);
    MultiMatrixByQuaternion(q);
    CopyVector(v, MatrixDrive_GetMatrix()[3]);
    MatrixDrive_TransMatrixV(&up);
    y0 = GetYProjectionOfPlane(p->root.plane.f, v);
    y1 = GetYProjectionOfPlane(p->root.plane.f, MatrixDrive_GetMatrix()[3]);
    return y1 - y0;
}

static inline float getSlopeRatio(float d, float rate) /* derived name */
{
    float t = d * rate;
    float r = 1.0f;

    if (t > 0.0f) {
        if (t < 0.5f) {
            r = r + t * 0.2f;
        } else {
            r = 1.1f - (t - 0.5f) * 0.45f;
        }
    } else {
        r = r + t * 0.3f;
    }
    return (r > 0.3f) ? r : 0.3f;
}

void SlopeIKControl(GObj *self, char *mot, float *v, Vec4 *vel, int n)
{
    struct MotCtrl *ik;
    struct MotRoot *sub;
    int n0;
    int n1;
    int rec;
    float d;
    float r0 = 1.0f;
    float r1 = 1.0f;

    ik = &GOBJ_SUB(self)->ctrl;
    sub = &GOBJ_SUB(self)->root;
    if (ik->rootUpdateMode < 3) {
        if (ik->rootUpdateMode > 0) {
            if (sub->slopeIK != 0) {
                n0 = GOBJ_SUB(self)->focusNodes[49];
                n1 = GOBJ_SUB(self)->focusNodes[45];
                if (n0 != -1 && n1 != -1) {
                    SkelNode *skel = GOBJ_SUB(self)->skel;

                    calcFootIK(skel, mot, n0, GOBJ_SUB(self)->nodes->scale[0], sub->footIKRate);
                    calcFootIK(skel, mot, n1, GOBJ_SUB(self)->nodes->scale[0], sub->footIKRate);
                    vel->f[0] = vel->f[0] * sub->footIKRate;
                    vel->f[2] = vel->f[2] * sub->footIKRate;
                }
                d = getSlopeDifference(self, mot, GOBJ_SUB(self));
                rec = ik->motion;
                r1 = getSlopeRatio(d, motionKind[rec].rate0);
                r0 = getSlopeRatio(d, motionKind[rec].rate1);
            }
        }
    }
    sub->footIKRate = sub->footIKRate +
                      (r1 - sub->footIKRate) *
                          (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.1f);
    ik->playRate = (r0 > 1.0f) ? 1.0f : r0;
}

static const char motMan2File[] = __FILE__; /* derived name */

/* AdjustRootPositionToVerticalSidePlaneOfWall was about to push into the
   wall, so the position was clipped */
static const char adjustRootClippedMsg[] =
    "AdjustRootPositionToVerticalSidePlaneOfWallが壁の中に突入させようとしたのでクリップしました\n"; /* derived name */

/* AdjustVerticalSidePlaneOfWall: the vertical walls are close together, so
   the corrected position was set to their midpoint */
static const char adjustWallMidpointMsg[] =
    "AdjustVerticalSidePlaneOfWall:垂直壁が近接しているので補正位置をその中点としました\n"; /* derived name */

static const char illegalCompressMsg[] =
    "Illegal compress formatID(%d) appeard... ignore.\n"; /* derived name */

/* the four wall corners in edge order, closed back onto corner 0, so a walk of
   i = 0..3 takes the pair (corner[i], corner[i+1]) */
static int wallLineCorner[8] = {0, 1, 3, 2, 0, 0, 0, 0}; /* derived name */

/* the colour DebugDisp1Collision draws a wall outline in: white, half alpha */
static int wallLineColor[4] = {255, 255, 255, 128}; /* derived name */

/* the same closed corner walk, used to pick the wall edge a position sits on */
static int wallEdgeCorner[8] = {0, 1, 3, 2, 0, 0, 0, 0}; /* derived name */

/* the matrix that turns a wall into the XY plane: the identity with the wall
   normal's X and Z written into the four rotation slots before every use */
static float wallAlignMatrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
}; /* derived name */

/* the half turn about Z every motion node's quaternion is multiplied by */
static float nodeFlipQuaternion[4] = {0.0f, 0.0f, -1.0f, 0.0f}; /* derived name */

int GetPureVerticalPlaneOfCurrentPosition(void *plane0, void *plane1, float *ptsIn, WallCfg *cfg,
                                          int flip, float *pos)
{
    float local[4][4];
    float nrm[4];
    float best[4];
    float bestNrm[4];
    float plane[4];
    float work[4];
    float *pts;
    int i;
    int bestIdx;
    int *t;
    int *tbl;
    float bestDist;
    float d;
    GObj *obj;
    int sh;
    Sub15C *p15c;
    ICO_WORD v_c;

    tbl = wallEdgeCorner;
    pts = (ptsIn != 0) ? ptsIn : (float *)local;
    bestIdx = -1;

    bestDist = 3.40282347e+38f;

    obj = cfg->o.obj;
    sh = cfg->o.node << 6;
    p15c = obj->dobj;
    v_c = p15c->nodeMtx;
    GetWallGlobalInfo(pts, nrm, cfg->elem, (void *)(v_c + sh));
    nrm[1] = 0;
    sceVu0Normalize((int *)nrm, (int *)nrm);
    t = tbl;
    for (i = 0; i < 4; i++, t++) {
        sceVu0SubVector(work, pts + t[1] * 4, pts + t[0] * 4);
        sceVu0Normalize((int *)work, (int *)work);
        sceVu0OuterProduct(work, work, nrm);
        if (flip != 0) {
            sceVu0ScaleVectorXYZ(work, work, -1.0f);
        }
        if (work[1] < -0.1f) {
            SetSimplePlane(plane, work[0], work[1], work[2],
                           -sceVu0InnerProduct(work, pts + t[0] * 4));
            d = GetYDistanceFromPlane(plane, pos);
            if (d < 0.0f) {
                d = -d;
            }
            if (d < bestDist) {
                bestIdx = i;
                CopyVector(best, work);
                CopyVector(bestNrm, nrm);
                bestDist = d;
            }
        }
    }
    if (bestIdx == -1) {
        debug_assertMessage(motMan2File, 1360, "!!");
        __assert(motMan2File, 1360, "e");
    }
    if (plane0 != 0) {
        SetSimplePlane(plane0, best[0], best[1], best[2],
                       -sceVu0InnerProduct(best, pts + tbl[bestIdx] * 4));
    }
    if (plane1 != 0) {
        SetSimplePlane(plane1, bestNrm[0], bestNrm[1], bestNrm[2],
                       -sceVu0InnerProduct(bestNrm, pts + tbl[bestIdx] * 4));
    }
    return bestIdx;
}

static void getVerticalElementOfWallNormal(int *self, int *p, WallCfg *cfg)
{
    GObj *obj = cfg->o.obj;
    int sh = cfg->o.node << 6;
    Sub15C *p15c = obj->dobj;
    ICO_WORD v_c = p15c->nodeMtx;

    GetWallGlobalInfo(self, p, cfg->elem, (void *)(v_c + sh));
    p[1] = 0;
    _NormalizeVector(p, p);
}

void AdjustVerticalSidePlaneOfWall(float *out, WallCfg *cfg, float *pos, float t)
{
    float pts[4][4];
    float nrm[4];
    float v[4];
    float pa[4];
    float pb[4];
    float p0[4];
    float p1[4];
    int maxIdx;
    int minIdx;
    int i;
    float minV;
    float maxV;
    float d;
    float t2;
    float d0;
    float d1;

    t2 = t + t;
    minIdx = 0;
    maxIdx = 0;
    getVerticalElementOfWallNormal((int *)pts, (int *)nrm, cfg);
    wallAlignMatrix[0] = wallAlignMatrix[10] = nrm[2];
    wallAlignMatrix[2] = nrm[0];
    wallAlignMatrix[8] = -nrm[0];
    _SetCurrentMatrix(wallAlignMatrix);
    _ApplyCurrentMatrix(v, pts);
    maxV = minV = v[0];
    for (i = 1; i < 4; i++) {
        _ApplyCurrentMatrix(v, pts[i]);
        if (maxV < v[0]) {
            maxV = v[0];
            maxIdx = i;
        }
        if (v[0] < minV) {
            minV = v[0];
            minIdx = i;
        }
    }
    _OuterProduct(pa, YUnitVector, nrm);
    pa[3] = -_InnerProduct(pa, pts[minIdx]);
    _OuterProduct(pb, nrm, YUnitVector);
    pb[3] = -_InnerProduct(pb, pts[maxIdx]);
    d0 = plane_distance(pos, pa);
    d1 = plane_distance(pos, pb);
    d = pa[3] + pb[3];
    if (((d < 0.0f) ? -d : d) < t2) {
        GetProjectionOfPlane(p0, pa, pos);
        GetProjectionOfPlane(p1, pb, pos);
        _InterVectorXYZ(out, p0, p1, 0.5f);
        debug_StdPrintfDummy(adjustWallMidpointMsg);
    } else if (d0 < t) {
        GetProjectionOfPlaneWithKeepAway(out, pa, pos, t);
    } else if (d1 < t) {
        GetProjectionOfPlaneWithKeepAway(out, pb, pos, t);
    } else {
        CopyVector(out, pos);
    }
    out[3] = 1.0f;
}

int GetPureVerticalPlane(void *plane0, void *plane1, float *ptsIn, WallCfg *cfg, int flip)
{
    float local[4][4];
    float up[4];
    float best[4];
    float bestUp[4];
    float work[4];
    float *pts;
    int i;
    int bestIdx;
    int *t;
    int *tbl;

    tbl = wallEdgeCorner;
    pts = (ptsIn != 0) ? ptsIn : (float *)local;
    bestIdx = 0;
    getVerticalElementOfWallNormal((int *)pts, (int *)up, cfg);
    for (i = 0, t = tbl; i < 4; i++, t++) {
        sceVu0SubVector(work, pts + t[1] * 4, pts + t[0] * 4);
        sceVu0Normalize((int *)work, (int *)work);
        sceVu0OuterProduct(work, work, up);
        if (flip != 0) {
            sceVu0ScaleVectorXYZ(work, work, -1.0f);
        }
        if (i == 0 || work[1] < best[1]) {
            CopyVector(best, work);
            bestIdx = i;
            CopyVector(bestUp, up);
        }
    }
    if (plane0 != 0) {
        SetSimplePlane(plane0, best[0], best[1], best[2],
                       -sceVu0InnerProduct(best, pts + tbl[bestIdx] * 4));
    }
    if (plane1 != 0) {
        SetSimplePlane(plane1, bestUp[0], bestUp[1], bestUp[2],
                       -sceVu0InnerProduct(bestUp, pts + tbl[bestIdx] * 4));
    }
    return bestIdx;
}

typedef struct { /* field names derived */
    float x, y, z, w;
} __attribute__((aligned(16))) Vec4f; /* derived name */

typedef struct { /* field names derived */
    unsigned char n;
    signed char adj : 7;
    unsigned char neg : 1;
} MotS16Hdr; /* derived name */

/* 2^e as a float, built by repeated multiply/divide so the exponent can
   exceed a single shift's range */
static inline float motPow2(int e) /* derived name */
{
    float s = 1.0f;

    if (e > 0) {
        while (e >= 31) {
            s *= (float)(1 << 31);
            e -= 31;
        }
        s *= (float)(1 << e);
    } else {
        e = -e;
        while (e >= 31) {
            s /= (float)(1 << 31);
            e -= 31;
        }
        s /= (float)(1 << e);
    }
    return s;
}

/* one 16-bit mini-float (sign:1 exp:5 mantissa:10) */
static inline float motDecodeS16(int h) /* derived name */
{
    float m = (float)(h & 0x3FF) + 1024.0f;
    float s = motPow2(-((h >> 10) & 0x1F) - 10);

    if ((h >> 15) & 1) {
        m = -m;
    }
    return m * s;
}

/* VU0's Q register, which carries the root from motSqrtStart to motSqrtEnd */
static float motSqrtQ; /* derived name */

/* the VU0 square root split in two so the Q-pipeline latency is covered by
   the vector copy in between */
static inline void motSqrtStart(float d) /* derived name */
{
    motSqrtQ = ps2_sqrt(1.0f - d);
}

static inline float motSqrtEnd(void) /* derived name */
{
    return motSqrtQ;
}

void _getS16MotRotElem(void *dst, void *src)
{
    Vec4f v = {motDecodeS16(*(unsigned short *)((char *)src + 2)),
               motDecodeS16(*(unsigned short *)((char *)src + 4)),
               motDecodeS16(*(unsigned short *)((char *)src + 6)), 1.0f};
    float d = _InnerProduct((float *)&v, (float *)&v);
    if (d > 1.0f) {
        d = 1.0f;
    }
    motSqrtStart(d);

    *(int *)dst = *(unsigned char *)src;
    sceVu0CopyVector((char *)dst + 0x10, &v);
    *(float *)((char *)dst + 0x1C) = motSqrtEnd();
    if (*(signed char *)((char *)src + 1) < 0) {
        *(float *)((char *)dst + 0x1C) = -*(float *)((char *)dst + 0x1C);
    }
    *(float *)((char *)dst + 0x1C) += (float)((MotS16Hdr *)src)->adj * 0.001f;
}

typedef struct { /* field names derived */
    unsigned char n;
    unsigned char s;
    float x, y, z;
} MotElemF; /* derived name */

typedef struct { /* field names derived */
    unsigned char n;
    unsigned char s;
    unsigned short a, b, c;
} MotElemS; /* derived name */

/* the body of _getMotRotElem, which _getMotion inlines and _getMotRotElem
   calls */
static inline void getMotRotElem(char *dst, char *src) /* derived name */
{
    float sum;

    sum = *(float *)(src + 0x4) * *(float *)(src + 0x4) +
          *(float *)(src + 0x8) * *(float *)(src + 0x8) +
          *(float *)(src + 0xC) * *(float *)(src + 0xC);
    sum = (sum > 1.0f) ? 1.0f : sum;
    *(int *)dst = *(unsigned char *)src;
    *(float *)(dst + 0x1C) = FSqrt(1.0f - sum);
    *(float *)(dst + 0x10) = *(float *)(src + 0x4);
    *(float *)(dst + 0x14) = *(float *)(src + 0x8);
    *(float *)(dst + 0x18) = *(float *)(src + 0xC);
    if (*(signed char *)(src + 1) < 0) {
        *(float *)(dst + 0x1C) = -*(float *)(dst + 0x1C);
    }
}

void _getMotion(void *dst, void *m, int node, int frame)
{
    int type;

    char *mm = (char *)m;

    type = ICO_EEPTR(unsigned char *, *(int *)(mm + 8))[node];
    switch (type) {
    default:
        debug_StdPrintfDummy(illegalCompressMsg, type);
        SetIdentityQuaternion((char *)dst + 0x10);
        break;
    case 1: {
        int off = frame * 0x10;
        getMotRotElem((char *)dst,
                      ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]) + off);
        break;
    }
    case 2: {
        char *p = ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]);
        MotElemF e = {ICO_EEPTR(unsigned char *, *(int *)p)[frame], *(unsigned char *)(p + 4),
                      *(float *)(p + 8), *(float *)(p + 0xC), *(float *)(p + 0x10)};
        getMotRotElem((char *)dst, (char *)&e);
        break;
    }
    case 3: {
        char *p = ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]);
        MotElemF e = {ICO_EEPTR(unsigned char *, *(int *)p)[frame], *(unsigned char *)(p + 8),
                      *(float *)(p + 0xC), *(float *)(p + 0x10),
                      ICO_EEPTR(float *, *(int *)(p + 4))[frame]};
        getMotRotElem((char *)dst, (char *)&e);
        break;
    }
    case 4: {
        char *p = ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]);
        _getS16MotRotElem(dst, &((MotElemS *)p)[frame]);
        break;
    }
    case 5: {
        char *p = ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]);
        MotElemS e = {ICO_EEPTR(unsigned char *, *(int *)p)[frame], *(unsigned char *)(p + 4),
                      *(unsigned short *)(p + 6), *(unsigned short *)(p + 8),
                      *(unsigned short *)(p + 0xA)};
        _getS16MotRotElem(dst, &e);
        break;
    }
    case 6: {
        char *p = ICO_EEPTR(char *, ICO_EEPTR(int *, *(int *)(mm + 0xC))[node]);
        MotElemS e = {ICO_EEPTR(unsigned char *, *(int *)p)[frame], *(unsigned char *)(p + 8),
                      *(unsigned short *)(p + 0xA), *(unsigned short *)(p + 0xC),
                      ICO_EEPTR(unsigned short *, *(int *)(p + 4))[frame]};
        _getS16MotRotElem(dst, &e);
        break;
    }
    }
}

/* the root-position copy GetMotionRootPos and GetStreamMotion both
   expand */
static inline void getRootPos(float *dst, float *src) /* derived name */
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 1.0f;

    dst[0] = -dst[0];
    dst[1] = -dst[1];
}

int GetStreamMotion(StreamElem *dst, float *out, char *node, SkelNode *skel)
{
    float quat[4];
    int i;
    int n = *(unsigned char *)(node + 2);

    memset(quat, 0, 16);
    quat[3] = 1.0f;

    if (*(signed char *)(node + 1) == 0) {
        getRootPos(out, (float *)(node + 4));

        RotQuaternionX(quat, -0x8000);
        RotQuaternionY(quat, -0x8000);

        for (i = 0; i < n; i++) {
            _getS16MotRotElem(&dst[i], node + 0x10 + i * 8);
            if (skel[i].parent == -1) {
                MultiQuaternion(dst[i].q, quat, dst[i].q);
            }
            *(int *)&dst[i] = 0;
        }
        return 1;
    }
    for (i = 0; i < n; i++) {
        CopyVector(out, ZeroPoint);
        *(int *)&dst[i] = 0;
        CopyQuaternion(dst[i].q, quat);
    }
    return 0;
}

/* copyMotionWithNodeHrc is a nested function inside CopyMotionWithNodeHrc:
 * the parent passes it a static chain, through which it reaches
 * dst/src/flag/hrc. */

/* The nested copyMotionWithNodeHrc as a file-scope function (clang has no
   nested functions); the parent's dst, src, hrc and flag are parameters. */
static void copyMotionWithNodeHrc(StreamElem *dst, StreamElem *src, SkelNode *hrc, int flag,
                                  int n) /* derived name */
{
    dst[n] = src[n];
    if (flag == 0) {
        *(int *)&dst[n] = 250;
    }
    if (hrc[n].child != -1) {
        copyMotionWithNodeHrc(dst, src, hrc, flag, hrc[n].child);
    }
    if (hrc[n].sibling != -1) {
        copyMotionWithNodeHrc(dst, src, hrc, flag, hrc[n].sibling);
    }
}

void CopyMotionWithNodeHrc(StreamElem *dst, StreamElem *src, SkelNode *hrc, int node, int flag)
{
    dst[node] = src[node];
    if (flag == 0) {
        *(int *)&dst[node] = 250;
    }
    if (hrc[node].child != -1) {
        copyMotionWithNodeHrc(dst, src, hrc, flag, hrc[node].child);
    }
}

/* the bodies of GetMotionRootPos and GetBlendedMotionRootPos, which their
   callers inline and the two exported functions call */
static inline void getMotionRootPos(float *dst, void *motion, int idx) /* derived name */
{
    float *src = ICO_EEPTR(float *, *(int *)((char *)motion + 4) + idx * 0xC);
    getRootPos(dst, src);
}

static inline void getBlendedMotionRootPos(float *dst, float *a, float *b,
                                           float t) /* derived name */
{
    float u = 1.0f - t;
    dst[0] = a[0] * t + b[0] * u;
    dst[1] = a[1] * t + b[1] * u;
    dst[2] = a[2] * t + b[2] * u;
}

/* the bodies of GetMotion (five sites) and GetBlendedMotion (one), which
   GetFloatingMotion inlines and the two exported functions call */
static inline void getMotion(char *dst, float *root, void *motion, int idx, unsigned char *mask,
                             int count, SkelNode *hrc) /* derived name */
{
    int i;

    if (mask != 0) {
        for (i = 0; i < count; i++) {
            if (mask[i] == 0) {
                _getMotion(dst + i * 0x20, motion, i, idx);
            }
        }
    } else {
        for (i = 0; i < count; i++) {
            _getMotion(dst + i * 0x20, motion, i, idx);
        }
    }

    if (hrc != 0) {
        i = 0;
        do {
            MultiQuaternion(dst + i * 32 + 16, nodeFlipQuaternion, dst + i * 32 + 16);
            i = hrc[i].sibling;
        } while (i != -1);
    } else {
        for (i = 0; i < count; i++) {
            MultiQuaternion(dst + i * 32 + 16, nodeFlipQuaternion, dst + i * 32 + 16);
        }
    }
    if (root != 0) {
        getMotionRootPos(root, motion, idx);
    }
}

static inline void getBlendedMotion(StreamElem *dst, float *root, StreamElem *a, float *rootA,
                                    StreamElem *b, float *rootB, float t, unsigned char *mask,
                                    int count) /* derived name */
{
    int i;
    float u = 1.0f - t;

    if (mask != 0) {
        for (i = 0; i < count; i++) {
            if (mask[i] != 0) {
                dst[i] = a[i];
            } else {
                *(int *)&dst[i] = (float)*(int *)&a[i] * t + (float)*(int *)&b[i] * u;
                GetSlerpQuaternionNoRegularize(dst[i].q, a[i].q, b[i].q, t);
            }
        }
    } else {
        for (i = 0; i < count; i++) {
            dst[i] = a[i];
        }
    }
    if (root != 0) {
        getBlendedMotionRootPos(root, rootA, rootB, t);
    }
}

void GetFloatingMotion(StreamElem *dst, float t, float *root, void *motion, int count,
                       unsigned char *mask, SkelNode *hrc)
{
    float rootA[4];
    float rootB[4];
    StreamElem buf0[count];
    StreamElem buf1[count];
    int idx;
    int idx1;
    float frac;

    t = t - (*(int *)motion - 1) * (int)(t / (*(int *)motion - 1));
    idx = (int)t;
    idx1 = idx + 1;
    frac = t - (float)idx;
    if (frac == 0.0f) {
        getMotion((char *)dst, root, motion, idx, 0, count, hrc);
        return;
    }
    if (frac < 0.5f) {
        getMotion((char *)buf0, rootA, motion, idx, 0, count, hrc);
        getMotion((char *)buf1, rootB, motion, idx1, mask, count, hrc);
        getBlendedMotion(dst, root, buf0, rootA, buf1, rootB, 1.0f - frac, mask, count);
    } else {
        getMotion((char *)buf0, rootA, motion, idx, mask, count, hrc);
        getMotion((char *)buf1, rootB, motion, idx1, 0, count, hrc);
        getBlendedMotion(dst, root, buf1, rootB, buf0, rootA, frac, mask, count);
    }
}

void MakeMirrorMotion(StreamElem *a, SkelNode *b)
{
    int i;
    int n;
    float buf[4];
    StreamElem tmp;

    for (i = 0; b[i].mirror != -1; i++) {
        n = b[i].mirror;
        if (i < n) {
            continue;
        }
        if (i != n) {
            goto swap;
        }
        GetInverseQuaternion(buf, b[i].quat);
        MultiQuaternion(buf, buf, a[i].q);
        GetMirrorQuaternion(buf, buf, 4);
        MultiQuaternion(a[i].q, b[i].quat, buf);
        continue;
    swap:
        {
            StreamElem *pn = (StreamElem *)((char *)a + n * 0x20);
            StreamElem *pi = (StreamElem *)((char *)a + i * 0x20);
            tmp = *pi;
            *pi = *pn;
            *pn = tmp;
        }
        GetMirrorQuaternion(a[i].q, a[i].q, 4);
        GetMirrorQuaternion(a[b[i].mirror].q, a[b[i].mirror].q, 4);
    }
}

/* the body of GetShapeMotion, which GetFloatingShapeMotion inlines and
   GetShapeMotion calls */
static inline void getShapeMotion(float *dst, char *motion, int idx, int count) /* derived name */
{
    int i = 0;
    int m = *(int *)motion - 1;
    idx = idx - m * (idx / m);
    for (; i < count; i++) {
        char *t = ICO_EEPTR(char *, *(int *)(motion + 0x10));
        int *elem = ICO_EEPTR(int *, *(int *)(ICO_EEPTR(char *, *(int *)(t + 4)) + i * 4));
        if (elem != 0) {
            dst[i] = ((float *)elem)[idx];
        } else {
            dst[i] = 0;
        }
    }
}

void GetFloatingShapeMotion(float *dst, char *m, float t, int count)
{
    int i;
    float frac;

    t = t - (*(int *)m - 1) * (int)(t / (*(int *)m - 1));
    frac = t - (int)t;
    if (frac == 0.0f) {
        getShapeMotion(dst, m, (int)t, count);
    } else {
        float buf0[count], buf1[count];

        getShapeMotion(buf0, m, (int)t, count);
        getShapeMotion(buf1, m, (int)t + 1, count);
        for (i = 0; i < count; i++) {
            dst[i] = buf0[i] * (1.0f - frac) + buf1[i] * frac;
        }
    }
}

typedef struct { /* field names derived */
    int a;
    int b;
    int c;
} WallWork; /* derived name */

void FeedbackWallWorkInfoToBrainSystem(GObj *self)
{
    Sub15C *p = self->dobj;
    char *d = (char *)self->act;
    p->root.wall = p->root.aheadWall;
    GOBJ_ACT(self)->env.motOriReq.a.wall = p->root.aheadWall;
}

void *GetMotionPointer(GObj *self)
{
    return &self->dobj->motion;
}

int GetCollisionOfLastActiveField(GObj *self)
{
    return self->dobj->root.lastField;
}

int CheckFieldContact(ClipWork *info, GObj *self, float *pos, float lim)
{
    float h;
    float dy;
    float ph;
    float d;

    if (info->floor.elem != 0) {
        h = GOBJ_SUB(self)->root.move[1];
        dy = info->pt[2][1] - pos[1];
        if (CompareAttribute(GetFloorAttribute(info), 0x50) != 0) {
            if (h >= 0.0f) {
                ph = GetPoolGlobalHeight(info->floor.o.obj);
                d = info->pt[2][1] - ph;
                if (dy < lim) {
                    if (d > 0.0f) {
                        if ((GOBJ_SUB(self)->ctrl.contactFlags & 1) == 0 && h > 5.0f) {
                            SetFallDownSplash(info->floor.o.obj, self);
                            GOBJ_SUB(self)->ctrl.contactFlags |= 1;
                        }
                    }
                    return 1;
                }
                if (d > 0.0f) {
                    if (ph - pos[1] < lim * 0.8f) {
                        if ((GOBJ_SUB(self)->ctrl.contactFlags & 1) == 0 && h > 5.0f) {
                            SetFallDownSplash(info->floor.o.obj, self);
                            GOBJ_SUB(self)->ctrl.contactFlags |= 1;
                        }
                        return 2;
                    }
                }
            }
        } else {
            if (dy < lim) {
                return 1;
            }
        }
    }
    return 0;
}

/* the body of DebugDisp1CollisionWithColor, which the next function
   inlines and DebugDisp1CollisionWithColor calls */
static inline void debugDisp1CollisionWithColor(WallCfg *cfg, void *color) /* derived name */
{
    float pts[5][4];
    int i;
    GObj *obj = cfg->o.obj;
    int sh = cfg->o.node << 6;
    ICO_WORD v_c = obj->dobj->nodeMtx;

    GetWallGlobalInfo(pts, pts[4], cfg->elem, (void *)(v_c + sh));
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 0x80);
    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < 4; i++) {
        DrawLineG(pts[wallLineCorner[i]], color, pts[wallLineCorner[i + 1]], color, -1);
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

void DebugDisp1Collision(WallCfg *cfg)
{
    debugDisp1CollisionWithColor(cfg, wallLineColor);
}

void DebugDisp1CollisionWithColor(WallCfg *cfg, void *color)
{
    debugDisp1CollisionWithColor(cfg, color);
}

/* the body of GetSkeltonFocusNode, which SetMotionBlendlessNode, the two
   GetDifferenceFromWall*Plane and the node fix mode setter inline and
   GetSkeltonFocusNode calls */
static inline int getSkeltonFocusNode(GObj *self, int focus) /* derived name */
{
    return GOBJ_SUB(self)->focusNodes[focus];
}

void SetMotionBlendlessNode(GObj *self, int *node)
{
    char *blend;
    int i;

    blend = self->dobj->blendless;
    ClearMotionBlendlessNode(self);
    for (i = 0; node[i] != -1; i++) {
        int idx = getSkeltonFocusNode(self, node[i]);
        if (idx != -1) {
            blend[idx] = 1;
        }
    }
}

void ClearMotionBlendlessNode(GObj *self)
{
    int i = 0;
    char *arr = self->dobj->blendless;
    while (i < GOBJ_SUB(self)->skelNodeNum) {
        arr[i] = 0;
        i++;
    }
}

void InitMotionStateInfo(struct MotCtrl *self)
{
    *(MotionStateInfo *)self = motionStateInfoTemplate;
    self->seGroup[0] = soundSeGroupGet();
    self->seGroup[1] = soundSeGroupGet();
}

/* PC port: the template copy above is only right while the record has
   MotCtrl's host layout (the comment at MotionStateInfo): every member at
   MotCtrl's offset, and the same size. */
/* clang-format off */
#define MSI_FIELDS(X) /* derived name */ \
    X(stream) X(oriFrom) X(oriTo) X(shifted) X(ctrlFlags) X(flags) X(shiftStop) X(shiftReq) \
    X(shiftNext) X(shiftFrom) X(shiftMode) X(request) X(motion) X(noAlt) X(shiftReady) \
    X(animFrame) X(lastFrame) X(playTime) X(speedRatio) X(playRate) X(frameRatio) X(waterDrag) \
    X(justShifted) X(frameEnd) X(keepUpdateMode) X(updateModeChanged) X(rootUpdateMode) \
    X(parallelEnded) X(parallel) X(orientUpdateOff) X(noStand) X(posReserve) X(loopFlag) \
    X(reserveBlend) X(reserveMoved) X(step) X(orientReq) X(lastMotion) X(lastNoAlt) \
    X(shiftFrame) X(blendCount) X(blendFrames) X(dir) X(lastDir) X(orientKind) X(floorFit) \
    X(wallReact) X(cliffWallCheck) X(catchBoy) X(sideWallCheck) X(variation) X(fallHeight) \
    X(groundHeight) X(wallHit) X(cliffEdge) X(cliffWallHit) X(cliffBack) X(fieldWallHit) \
    X(upperWall) X(sideWall) X(cliffHeight) X(cliffDist) X(cliffNormal) X(wallFloorHeight) \
    X(wallTopHeight) X(wallDist) X(wallDir) X(wallNormal) X(sideWallNormal) X(upperWallDist) \
    X(sideWallDist) X(cliffDepth) X(pureWallAttr) X(pureCliffAttr) X(wallAttr) X(floorAttr) \
    X(frameFlag1) X(frameFlag2) X(trigger1) X(trigger1Done) X(trigger2) X(trigger2Done) \
    X(ropeHangPos) X(seGroup) X(slipFlags) X(lastSlipFlags) X(slipOn) X(pickedWeapon) \
    X(keepWall) X(keepStand) X(landed) X(waterY) X(waterDepth) X(pool) X(contactFlags) \
    X(mailDelay) X(noFieldClip) X(seMute)
/* clang-format on */
#define MSI_SAME(f) /* derived name */                                                             \
    _Static_assert(__builtin_offsetof(MotionStateInfo, f) ==                                       \
                       __builtin_offsetof(struct MotCtrl, f),                                      \
                   "MotionStateInfo." #f " is not at MotCtrl." #f);

MSI_FIELDS(MSI_SAME)
_Static_assert(sizeof(MotionStateInfo) == sizeof(struct MotCtrl), "MotionStateInfo size");

#undef MSI_SAME
#undef MSI_FIELDS

int GetSkeltonFocusNode(GObj *self, int focus)
{
    return getSkeltonFocusNode(self, focus);
}

int AdjustMotionHeightToNearestField(GObj *self)
{
    float pos[4];
    struct MotRoot *o = &self->dobj->root;

    GetRootPosition(pos, self);
    return adjustMotionHeightToNearestField(o, pos);
}

void SetRootUpdateMode(GObj *self, int val)
{
    self->dobj->ctrl.rootUpdateMode = val;
}

float ForMotionViewer_GetCurrentAnimationFrame(GObj *self)
{
    return GOBJ_SUB(self)->ctrl.animFrame;
}

int ForMotionViewer_GetCurrentMotion(GObj *self)
{
    return self->dobj->ctrl.motion;
}

void EnableMotionOrientUpdate(GObj *self)
{
    self->dobj->ctrl.orientUpdateOff = 0;
}

void DisableMotionOrientUpdate(GObj *self)
{
    self->dobj->ctrl.orientUpdateOff = 1;
}

int CheckFloorAttribute(GObj *self, int attr)
{
    Sub15C *sub = self->dobj;
    return CompareAttribute(sub->ctrl.floorAttr, attr);
}

int CheckWallAttribute(GObj *self, int attr)
{
    Sub15C *sub = self->dobj;
    return CompareAttribute(sub->ctrl.wallAttr, attr);
}

int CheckPureWallAttribute(GObj *self, int attr)
{
    Sub15C *sub = self->dobj;
    return CompareAttribute(sub->ctrl.pureWallAttr, attr);
}

int CheckPureCliffAttribute(GObj *self, int attr)
{
    Sub15C *sub = self->dobj;
    return CompareAttribute(sub->ctrl.pureCliffAttr, attr);
}

int GetStreamShapeMotion(float *dst, StreamShapeHdr *hdr)
{
    int i, n, skip;
    float *src, *p;
    if (hdr->shapeMode == 0 && (skip = hdr->skipNum, (n = hdr->shapeNum)) != 0) {
        int o = skip * 8 + 0x10;
        src = (float *)(ICO_WORD)o;
        p = (float *)((char *)hdr + (ICO_WORD)src);
        src = p;
        for (i = 0; i < n; i++)
            *dst++ = *src++;
        return 1;
    }
    return 0;
}

float GetDifferenceFromWallUpperField(GObj *self, int node)
{
    Sub15C *e = self->dobj;
    int idx = (e->focusNodes)[node];
    return GetYDistanceFromPlane(e->root.cliffPlane,
                                 (float *)((char *)e->nodeMtx + idx * 0x40 + 0x30));
}

float GetDifferenceFromLastField(GObj *self, int node)
{
    Sub15C *e = self->dobj;
    int idx = (e->focusNodes)[node];
    return GetYDistanceFromPlane(e->root.plane.f,
                                 (float *)((char *)e->nodeMtx + idx * 0x40 + 0x30));
}

float GetDifferenceFromLowerField(GObj *self, int node)
{
    ClipWork buf;
    Sub15C *ctrl;
    int idx;
    ctrl = self->dobj;
    idx = ((signed char *)ctrl->focusNodes)[node];
    GetLowerPlaneCollision(&buf, (float *)(ctrl->nodeMtx + (idx << 6) + 0x30));
    if (buf.floor.elem == 0) {
        return 3.40282347e+38f;
    }
    return buf.pt[2][1] - buf.pt[0][1];
}

float GetDifferenceFromWallLowerPlane(GObj *self, int node)
{
    float pos[4];
    float pts[4][4];
    int idx;

    idx = getSkeltonFocusNode(self, node);
    GetPureVerticalPlane(pos, 0, pts[0], &self->dobj->root.wall, 1);
    return GetYDistanceFromPlane(pos,
                                 (float *)((char *)GOBJ_SUB(self)->nodeMtx + idx * 0x40 + 0x30));
}

float GetDifferenceFromWallUpperPlane(GObj *self, int node)
{
    float pos[4];
    float pts[4][4];
    int idx;

    idx = getSkeltonFocusNode(self, node);
    GetPureVerticalPlane(pos, 0, pts[0], &self->dobj->root.wall, 0);
    return GetYDistanceFromPlane(pos,
                                 (float *)((char *)GOBJ_SUB(self)->nodeMtx + idx * 0x40 + 0x30));
}

void DisableChangeRootUpdateMode(GObj *self)
{
    Sub15C *sub = self->dobj;
    sub->ctrl.keepUpdateMode = 1;
}

void EnableChangeRootUpdateMode(GObj *self)
{
    Sub15C *sub = self->dobj;
    sub->ctrl.keepUpdateMode = 0;
}

float GetRopeHangablePos(GObj *self)
{
    Sub15C *sub = self->dobj;
    return ICO_RAW(float, sub, 0x618, sub->ctrl.ropeHangPos);
}

int GetMotionFrameFlag1(GObj *self)
{
    Sub15C *sub = self->dobj;
    return sub->ctrl.frameFlag1;
}

int GetMotionFrameFlag2(GObj *self)
{
    Sub15C *sub = self->dobj;
    return sub->ctrl.frameFlag2;
}

float GetHeightOfFieldPlaneDifference(GObj *a, GObj *b)
{
    Sub15C *pa;
    Sub15C *pb;
    float r1;
    float r2;
    pa = a->dobj;
    r1 = GetYProjectionOfPlane(pa->root.plane.f, pa->root.pos);
    pb = b->dobj;
    r2 = GetYProjectionOfPlane(pb->root.plane.f, pb->root.pos);
    return r1 - r2;
}

float GetHeightOfWallFromGObj(GObj *self)
{
    Sub15C *sub = self->dobj;
    return sub->ctrl.wallFloorHeight;
}

float GetHeightOfCliffFromGObj(GObj *self)
{
    Sub15C *sub = self->dobj;
    return sub->ctrl.cliffHeight;
}

void InitMotionRotElem(int *elem, int count)
{
    int *p;
    int i;
    if (count <= 0)
        return;
    p = elem;
    i = count;
loop:
    p[0] = 0;
    {
        int *call_arg = p + 4;
        p += 8;
        SetIdentityQuaternion(call_arg);
    }
    --i;
    if (i != 0)
        goto loop;
}

void SetMotionNodeFixModeParameter(GObj *self, GObj *obj, int mode, int node, void *quat, float x,
                                   float y, float z, float w)
{
    float vec[4] = {x, y, z, 1.0f};

    GOBJ_SUB(self)->root.fixObj = (ICO_WORD)obj;
    GOBJ_SUB(self)->root.fixNode = getSkeltonFocusNode(obj, node);
    GOBJ_SUB(self)->root.fixMode = mode;
    CopyVector(GOBJ_SUB(self)->root.fixPos, vec);
    CopyQuaternion(GOBJ_SUB(self)->root.fixQuat, quat);
    GOBJ_SUB(self)->root.fixWeight = w;
}

void GetRootProjectionPosOfGObj(float *pos, GObj *obj)
{
    GetRootPosition(pos, obj);
    pos[1] += obj->dobj->root.projHeight;
}

void SetMotionPlaySpeedRatio(GObj *self, float val)
{
    GOBJ_SUB(self)->ctrl.speedRatio = val;
}

void ClearMotionGeometryInfo(GObj *self)
{
    Sub15C *p = self->dobj;
    float *p1 = p->root.focusPos;
    struct MotRoot *p2 = &p->root;
    CopyVector(p1, ZeroVector);
    sceVu0AddVector(p->root.footPos, p2->pos, p1);
    p2->standNode = -1;
}

void SetSkeltonDispSwitch(int val)
{
    skeltonDispSwitch = val;
}

void CopyMotion(StreamElem *dst, StreamElem *src, int n)
{
    if (n <= 0)
        return;
    do {
        *dst = *src;
        n--;
        src++;
        dst++;
    } while (n != 0);
}

void GetMotionRootPos(float *dst, void *motion, int idx)
{
    getMotionRootPos(dst, motion, idx);
}

void GetMotion(char *dst, float *root, void *motion, int idx, unsigned char *mask, int count,
               SkelNode *hrc)
{
    getMotion(dst, root, motion, idx, mask, count, hrc);
}

void GetBlendedMotion(StreamElem *dst, float *root, StreamElem *a, float *rootA, StreamElem *b,
                      float *rootB, float t, unsigned char *mask, int count)
{
    getBlendedMotion(dst, root, a, rootA, b, rootB, t, mask, count);
}

void GetFloatingMotionRootPos(float *dst, void *m, float t)
{
    float buf0[4];
    float buf1[4];
    int n = *(int *)m - 1;
    int i;

    t = t - n * (int)(t / n);
    i = (int)t;

    getMotionRootPos(buf0, m, i);
    getMotionRootPos(buf1, m, i + 1);

    getBlendedMotionRootPos(dst, buf0, buf1, 1.0f - (t - i));
}

void GetShapeMotion(float *dst, char *motion, int idx, int count)
{
    getShapeMotion(dst, motion, idx, count);
}

/* the release bodies are empty; every call site passes the actor's GObj */
void LockForceGroundParent(GObj *gobj) {}

void UnlockForceGroundParent(GObj *gobj) {}

void GetOutOutsideOfWall(GObj *obj, float threshold)
{
    int buf0[4];
    float buf1[4];
    if (GOBJ_SUB(obj)->root.wall.elem != 0) {
        float dot;
        GetRootPosition(buf0, obj);
        GetGlobalWallPlane(buf1, &obj->dobj->root.wall);
        /* sugiCommon.h's plane_distance */
        dot = plane_distance(buf0, buf1);
        if (dot < threshold) {
            GetProjectionOfPlaneWithKeepAway(buf0, buf1, buf0, threshold);
        }
        SetDirectRootPosition(obj, buf0);
    }
}

void AdjustRootPositionToVerticalSidePlaneOfWall(void *self, void *wall, float dist)
{
    ClipWork buf;
    memset(&buf, 0, sizeof(buf));
    GetRootPosition(&buf, self);
    AdjustVerticalSidePlaneOfWall(buf.pt[1], wall, buf.pt[0], dist);
    ClipWall(&buf);
    if (buf.wall.elem != 0) {
        SetDirectRootPosition(self, buf.pt[2]);
        debug_StdPrintfDummy(adjustRootClippedMsg);
    } else {
        SetDirectRootPosition(self, buf.pt[1]);
    }
}

void fitYToPlane(long long *src, float *dest)
{
    Vec4 buf;
    buf.ll[0] = src[0];
    buf.ll[1] = src[1];
    dest[1] = GetYProjectionOfPlane(buf.f, dest);
}

void GetBlendedMotionRootPos(float *dst, float *a, float *b,
                             float t) /* same note as GetMotionRootPos */
{
    getBlendedMotionRootPos(dst, a, b, t);
}

void _getMotRotElem(char *dst, char *src)
{
    getMotRotElem(dst, src);
}
