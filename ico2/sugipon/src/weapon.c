#include "debug.h"
#include "box.h"
#include "sugiCommon.h"
#include "weapon.h"
#include "memory.h"
#include "gobj.h"
#include "act-game.h"
#include "DisplayP2O.h"
#include "GifPacket.h"
#include "StageAnimation.h"
#include "enemy.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "lineManager.h"
#include "motionManager2.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "tableSin.h"
#include "torch.h"
#include <libvu0.h>
#include <math.h>
#include <string.h>
#include "Matrix.h"
#include "matrixDrive.h"
#include "main.h"
#include "fieldCollision.h"
#include "ios.h"
#include "sceneManager.h"
#include "DObj.h"
#include "ico_gamestate.h" /* port: achievement signals */

static void calcDynamicGeometry(struct GObj *g);

/* The work record InitWeaponGeo and InitDemoQueensSword allocate and the
   template they initialise it from: 224 bytes, 8-aligned.  The fields are the
   ones this file reads. */
typedef struct {       /* field names derived */
    int kind;          /* 0x00: the switch in InitWeaponGeo */
    int state;         /* 0x04: 2 while the weapon falls from a fumble */
    GObj *holder;      /* 0x08: the holding object, 0 when none */
    int holderId;      /* 0x0C: -1 when none */
    float hit[4][4];   /* 0x10: checkHit's positions at 0x20, 0x30, 0x40 */
    int count;         /* 0x50 */
    GObj **objs;       /* 0x54, the torch objects the weapon carries */
    char *buf;         /* 0x58 */
    GObj *sword;       /* 0x5C */
    int fumbleTime;    /* 0x60 */
    int fumbleFrame;   /* 0x64 */
    float fumbleSpeed; /* 0x68 */
    char pad6C[4];
    float fumbleFrom[4]; /* 0x70 */
    float fumbleTo[4];   /* 0x80 */
    float fumbleQuat[4]; /* 0x90 */
    int fumbleSlot;      /* 0xA0: the drop-table row, 0 to 6 */
    int bladeOn;         /* 0xA4: the laser blade is out */
    float bladeLength;   /* 0xA8: the blade's drawn length */
    float bladeTimer;    /* 0xAC: frames the blade has been growing */
    float *net;          /* 0xB0, the insect net's sway, two floats */
    Sub15C *model0;      /* 0xB4, the blade's DObj */
    Sub15C *model1;      /* 0xB8, the blade tip's DObj */
    int humAnim;         /* 0xBC: the blade's BG animation handle */
    int offsetMode;      /* 0xC0: SetWeaponOffsetMode */
    char padC4[12];
    float tipPos[4];                      /* 0xD0: the blade tip */
} __attribute__((aligned(8))) WeaponWork; /* derived name */

void torchOnOfWeaponSE(GObj *torch)
{
    ExecuteSEPackage(torch, 66);
}

void torchOffOfWeaponSE(GObj *torch)
{
    StopSEPackage(torch);
    ExecuteSEPackage(torch, 67);
}

void weaponHitReactionSE(GObj *g)
{
    ExecuteSEPackage(g, 68);
}

void weaponFumbleSE(GObj *g)
{
    ExecuteSEPackage(g, 92);
}

void weaponStickSE(GObj *g)
{
    ExecuteSEPackage(g, 93);
}

static inline void releaseWeaponHolder(WeaponWork *w) /* derived name */
{
    if (w->holder != 0) {
        w->holder->dobj->ctrl.pickedWeapon = 0;
    }
    w->holderId = -1;
}

void ReleaseWeaponWithFumbleTargetPos(GObj *g, void *pos, void *quat, void *rot, float t)
{
    Sub15C *p = GOBJ_SUB(g);
    WeaponWork *w = (WeaponWork *)p->work;

    releaseWeaponHolder(w);
    w->state = 2;
    w->holder = 0;
    if (rot != 0) {
        CopyQuaternion(p->root.itemQuat, rot);
    }
    w->fumbleTime = (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) * t);
    w->fumbleFrame = 0;
    GetRootPosition(w->fumbleFrom, g);
    CopyVector(w->fumbleTo, pos);
    CopyQuaternion(w->fumbleQuat, quat);
    w->fumbleSpeed = (w->fumbleTo[1] - w->fumbleFrom[1]) / t - t * 490.0f;
    weaponFumbleSE(g);
}

/* the position vector this function builds and hands on: 8-byte aligned,
   built by aggregate initialisers */
typedef struct { /* field names derived */
    float x, y, z, w;
} __attribute__((aligned(8))) FumbleVec; /* derived name */

static inline int fumbleTargetBlocked(FumbleVec *p) /* derived name */
{
    GObj *o;
    FumbleVec tmp;

    for (o = isysGObjSearchFromObjKindID_begin(17); o != 0;
         o = isysGObjSearchFromObjKindID_next(o)) {
        float dx;
        float dz;

        GetRootPosition(&tmp, o);
        dx = tmp.x - p->x;
        if (((dx < 0.0f) ? -dx : dx) <= 50.0f) {
            dz = tmp.z - p->z;
            if (((dz < 0.0f) ? -dz : dz) <= 50.0f) {
                return 1;
            }
        }
    }
    return 0;
}

/* declared here: ico2/omori/src/attackhit.c declares the table with its
   own record and includes weapon.h */
extern WeaponDef weaponKind[];
/* declared here as three rows to a weapon slot, the way it is read; the
   definition (weapon-fumble-def) is the flat row array */
extern FumbleRow weaponFumbleGeo[][3];

#define FUMBLE_ROW(i, w) (&weaponFumbleGeo[(w)->fumbleSlot][i]) /* derived name */

int ReleaseWeaponWithFumbleSequential(GObj *g)
{
    WeaponWork *w = GOBJ_SUB(g)->work;
    FumbleVec a;
    int i;

    for (i = 0; i < 3; i++) {
        a = (FumbleVec){FUMBLE_ROW(i, w)->pos[0], -FUMBLE_ROW(i, w)->pos[1],
                        FUMBLE_ROW(i, w)->pos[2], 1.0f};
        if (!fumbleTargetBlocked(&a)) {
            break;
        }
        /* "point %d, candidate %d overlaps a box; checking the next candidate" */
        debug_StdPrintfDummy(
            "    第%dポイントの第%d候補は箱と重なっています。次の候補をチェックします\n",
            w->fumbleSlot, i);
    }
    /* "decided: point %d, candidate %d" */
    debug_StdPrintfDummy("決定: 第%dポイント 第%d候補 %f, %f, %f\n", w->fumbleSlot, i,
                         FUMBLE_ROW(i, w)->pos[0], FUMBLE_ROW(i, w)->pos[1],
                         FUMBLE_ROW(i, w)->pos[2]);
    {
        FumbleVec pos = {FUMBLE_ROW(i, w)->pos[0], -FUMBLE_ROW(i, w)->pos[1],
                         FUMBLE_ROW(i, w)->pos[2], 1.0f};
        float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        RotQuaternionY(quat, (short)(FUMBLE_ROW(i, w)->rotY * 182.04445f));
        RotQuaternionX(quat, (short)(-FUMBLE_ROW(i, w)->rotX * 182.04445f));
        RotQuaternionZ(quat, (short)(FUMBLE_ROW(i, w)->rotZ * 182.04445f));
        RotQuaternionX(rot, 8192);
        ReleaseWeaponWithFumbleTargetPos(g, &pos, quat, rot, 2.0f);
    }
    w->fumbleSlot = w->fumbleSlot + 1;
    if (w->fumbleSlot == 7) {
        w->fumbleSlot = 0;
        return 1;
    }
    return 0;
}

static WeaponWork swordWorkTemplate = {
    0,
    0,
    0,
    -1,
    {{0.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 0.0f, 1.0f}},
    0,
    0,
    0,
    0,
    0,
    0,
    0.0f,
    {0},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    0,
    0,
    0.0f,
    0.0f,
    0,
    0,
    0,
    0,
    0,
    {0},
    {0.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

/* the offsets the two path helpers below set the z of and push along */
static float pathOfsFwd[4] = {0.0f, 0.0f, 1.0f, 1.0f}; /* derived name */

static float pathOfsBack[4] = {0.0f, 0.0f, 1.0f, 1.0f}; /* derived name */

static inline void addWeaponPathOffset(Sub15C *p, struct MotRoot *rp, float d) /* derived name */
{
    float m[16];
    float v[4];
    float *q = p->root.quat;

    GetMatrixFromQuaternion(m, q);
    pathOfsFwd[2] = d;
    _ApplyMatrix(v, m, pathOfsFwd);
    _AddVectorXYZ(rp->pos, rp->pos, v);
}

static inline void subWeaponPathOffset(Sub15C *p, struct MotRoot *rp, float d) /* derived name */
{
    float m[16];
    float v[4];
    float *q = p->root.quat;

    GetMatrixFromQuaternion(m, q);
    pathOfsBack[2] = -d;
    _ApplyMatrix(v, m, pathOfsBack);
    _AddVectorXYZ(rp->pos, rp->pos, v);
}

static int calcDynamicPathGeometry(GObj *g)
{
    Sub15C *p = GOBJ_SUB(g);
    WeaponWork *w = (WeaponWork *)p->work;
    float d = weaponKind[w->kind].grip;
    struct MotRoot *rp = &p->root;
    float a;
    float b;
    float t;

    addWeaponPathOffset(p, rp, d);
    a = (float)w->fumbleFrame;
    b = (float)w->fumbleTime;
    t = a / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    _InterVectorXYZ(rp->pos, w->fumbleTo, w->fumbleFrom, a / b);
    rp->pos[1] = w->fumbleFrom[1] + w->fumbleSpeed * t + t * 490.0f * t;
    MultiQuaternion(p->root.quat, p->root.quat, p->root.itemQuat);
    subWeaponPathOffset(p, rp, d);
    w->fumbleFrame = w->fumbleFrame + 1;
    if (w->fumbleFrame >= w->fumbleTime) {
        w->state = 0;
        CopyVector(rp->pos, w->fumbleTo);
        CopyVector(p->root.move, ZeroVector);
        CopyQuaternion(p->root.quat, w->fumbleQuat);
        UpdateRootMatrix(g);
        weaponStickSE(g);
        return 1;
    }
    UpdateRootMatrix(g);
    return 0;
}

/* the query calcDynamicGeometry starts from: all clear but the radius */
static const ClipWork collWorkInit = {{{0.0f}}, {{0.0f}}, 10.0f}; /* derived name */

/* the offset the wall test pushes the blade tip along, its z set per test */
static float hitOfs[4] = {0.0f, 0.0f, 1.0f, 1.0f}; /* derived name */

/* calcDynamicGeometry's TTY trace, built only when DEBUG is defined */
static __inline__ void dynGeoDebugHook(void) /* derived name */
{
#ifdef DEBUG
    scePrintf("calcDynamicGeometry\n");
#endif
}

static void calcDynamicGeometry(GObj *g)
{
    Sub15C *p = g->dobj;
    WeaponWork *w = p->work;
    struct MotRoot *rp = &p->root;
    float d = weaponKind[w->kind].grip;
    float r = weaponKind[w->kind].length - d;
    ClipWork cc = collWorkInit;
    int hitA;
    int hitB;

    addWeaponPathOffset(p, rp, d);
    {
        sceVu0FMATRIX m1;
        sceVu0FVECTOR v1;
        sceVu0FMATRIX m2;
        sceVu0FVECTOR v2;
        sceVu0FVECTOR dir1;
        sceVu0FVECTOR dir2;
        sceVu0FVECTOR nrm;
        sceVu0FVECTOR tan;
        sceVu0FVECTOR axis;

        hitA = 0;
        hitB = 0;
        GetMatrixFromQuaternionPos(m1, p->root.quat, rp->pos);
        rp->move[1] =
            rp->move[1] + 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
                              (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        _AddVectorXYZ(rp->pos, rp->pos, rp->move);
        MultiQuaternion(p->root.quat, rp->itemQuat, p->root.quat);
        GetMatrixFromQuaternionPos(m2, p->root.quat, rp->pos);

        hitOfs[2] = r;
        _ApplyMatrix(cc.pt[0], m1, hitOfs);
        _ApplyMatrix(cc.pt[1], m2, hitOfs);
        CopyVector(v1, cc.pt[1]);
        _SubVector(dir1, cc.pt[1], cc.pt[0]);

        ClipCollision(&cc);
        if (cc.wall.elem != 0 || cc.floor.elem != 0) {
            hitA = 1;
            GetReflectionElement(&cc, 0.7f, 0.7f);
            CopyVector(v1, cc.reflect.pos);
            CopyVector(dir1, cc.reflect.dir);
            if (36.0f < VectorLengthSquare(cc.reflect.bounce)) {
                if (cc.wall.elem) {
                    GOBJ_SUB(g)->ctrl.wallAttr = GetWallAttribute(&cc);
                } else {
                    GOBJ_SUB(g)->ctrl.wallAttr = GetFloorAttribute(&cc);
                }
                weaponHitReactionSE(g);
            }
        }

        hitOfs[2] = -r;
        _ApplyMatrix(cc.pt[0], m1, hitOfs);
        _ApplyMatrix(cc.pt[1], m2, hitOfs);
        CopyVector(v2, cc.pt[1]);
        _SubVector(dir2, cc.pt[1], cc.pt[0]);

        ClipCollision(&cc);
        if (cc.wall.elem != 0 || cc.floor.elem != 0) {
            hitB = 1;
            GetReflectionElement(&cc, 0.7f, 0.7f);
            CopyVector(v2, cc.reflect.pos);
            CopyVector(dir2, cc.reflect.dir);
            if (36.0f < VectorLengthSquare(cc.reflect.bounce)) {
                if (cc.wall.elem) {
                    GOBJ_SUB(g)->ctrl.wallAttr = GetWallAttribute(&cc);
                } else {
                    GOBJ_SUB(g)->ctrl.wallAttr = GetFloorAttribute(&cc);
                }
                weaponHitReactionSE(g);
            }
        }

        if (cc.floor.elem) {
            w->state = 0;
            CopyVector(rp->move, ZeroVector);
            CopyQuaternion(p->root.quat, IdentityQuaternion);
        } else {
            dynGeoDebugHook();
            if (hitA || hitB) {
                _InterVector(rp->move, dir1, dir2, 0.5f);
                _InterVector(rp->pos, v1, v2, 0.5f);
                *(int *)&rp->move[3] = 0;
                rp->pos[3] = 1.0f;
                if (hitA) {
                    _SubVector(tan, dir1, rp->move);
                } else {
                    _SubVector(tan, rp->move, dir2);
                }
                _SubVector(nrm, v1, rp->pos);
                _NormalizeVector(nrm, nrm);
                _OuterProduct(axis, nrm, tan);
                {
                    sceVu0FVECTOR qr;

                    {
                        sceVu0FVECTOR sc = {0.0f, VectorLength(tan), r, 0.0f};

                        _NormalizeVector(sc, sc);
                        SetQuaternionByAxisRotateV(qr, (short)-GetTableArcTan2(sc[1], sc[2]), axis);
                    }
                    CopyQuaternion(rp->itemQuat, qr);
                    MultiQuaternion(p->root.quat, qr, p->root.quat);
                    {
                        sceVu0FMATRIX m3;
                        sceVu0FVECTOR v5;
                        sceVu0FVECTOR v6;
                        float eA;
                        float eB;
                        float e;

                        eA = 0.0f;
                        eB = 0.0f;
                        GetMatrixFromQuaternionPos(m3, p->root.quat, rp->pos);
                        hitOfs[2] = r;
                        _ApplyMatrix(v5, m3, hitOfs);
                        if (v5[1] > v1[1]) {
                            eA = v5[1] - v1[1];
                        }
                        hitOfs[2] = -r;
                        _ApplyMatrix(v6, m3, hitOfs);
                        if (v6[1] > v2[1]) {
                            eB = v6[1] - v2[1];
                        }
                        e = (eB < eA) ? eA : eB;
                        if (0.0f < e) {
                            rp->pos[1] = rp->pos[1] - (e + 1.0f);
                            GetSlerpQuaternion(rp->itemQuat, rp->itemQuat, IdentityQuaternion,
                                               0.9f);
                        }
                    }
                }
            }
            dynGeoDebugHook();
            RegularizeQuaternion(p->root.quat);
            VectorLengthSquare(rp->move);
            dynGeoDebugHook();
        }
        subWeaponPathOffset(p, rp, d);
        UpdateRootMatrix(g);
    }
}

/* the body of SetWeaponOffsetMode, which getGeometry and InitWeaponGeo
   inline and SetWeaponOffsetMode calls */
static inline void setWeaponOffsetMode(GObj *g, int v) /* derived name */
{
    ((WeaponWork *)GOBJ_SUB(g)->work)->offsetMode = v;
}

static void getGeometry(GObj *g)
{
    float pos[4];
    float quat[4];
    Sub15C *p = GOBJ_SUB(g);
    WeaponWork *w = (WeaponWork *)p->work;
    struct MotRoot *rp = &p->root;

    if (w->holder != 0) {
        Sub15C *d = w->holder->dobj;
        int n = w->holderId;

        CopyMatrix(MatrixDrive_GetMatrix(), (char *)d->nodeMtx + n * 64);
        MatrixDrive_TransMatrix(7.0f, -3.0f, 0.0f);
        CopyVector(pos, MatrixDrive_GetMatrix()[3]);
        CopyQuaternion(quat, (char *)d->nodeQuat + n * 16);
        if (GOBJ_SUB(w->holder)->skel[n].kind == 22) {
            RotQuaternionY(quat, -32768);
        }
        sceVu0SubVector(p->root.move, pos, rp->pos);
        {
            float *rq = p->root.quat;

            DivQuaternion(p->root.itemQuat, quat, rq);
            CopyVector(rp->pos, pos);
            CopyQuaternion(rq, quat);
        }
        UpdateRootMatrix(g);
        setWeaponOffsetMode(g, 0);
    } else {
        switch (w->state) {
        default:
            break;
        case 1:
            calcDynamicGeometry(g);
            break;
        case 2:
            calcDynamicPathGeometry(g);
            break;
        }
    }
}

void WeaponCurPos(GObj *self, void *center, void *from, void *to)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    CopyVector(center, p->hit[1]);
    CopyVector(from, p->hit[2]);
    CopyVector(to, p->hit[3]);
}

void WeaponHitEffect(GObj *self, void *enemy)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    CheckEnemyHit(enemy, p->hit[1], p->hit[2], p->hit[3]);
}

void ExecWeaponHitReaction(GObj *self)
{
    weaponHitReactionSE(self);
}

/* the blade tip in the sword's own frame */
static float swordTip[4] = {0.0f, 0.0f, 80.0f, 1.0f}; /* derived name */

static void checkHit(GObj *g)
{
    float pos[4];
    float quat[4];
    float v0[4];
    float v1[4];
    Sub15C *p = GOBJ_SUB(g);
    WeaponWork *w = (WeaponWork *)p->work;

    if (w->holder == 0) {
        return;
    }
    MatrixDrive_PushMatrix();
    GetMatrixFromQuaternionPos(MatrixDrive_GetMatrix(), p->root.quat, p->root.pos);
    MatrixDrive_TransMatrixV((char *)swordTip);
    CopyVector(v0, MatrixDrive_GetMatrix()[3]);
    GetInverseQuaternion(quat, p->root.itemQuat);
    MultiQuaternion(quat, p->root.quat, quat);
    SubVectorXYZ(pos, p->root.pos, p->root.move);
    GetMatrixFromQuaternionPos(MatrixDrive_GetMatrix(), quat, pos);
    MatrixDrive_TransMatrixV((char *)swordTip);
    CopyVector(v1, MatrixDrive_GetMatrix()[3]);
    MatrixDrive_PopMatrix();
    CopyVector(w->hit[1], v0);
    CopyVector(w->hit[2], v1);
    CopyVector(w->hit[3], p->root.pos);
}

/* the queen's sword offset, its z set per sword */
static float queenSwordOfs[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static void initializeQueenzSword(GObj *g, int index, SObjSimpleSetting *lay)
{
    WeaponWork *w = GOBJ_SUB(g)->work;
    ObjNode lnk = {g, index};
    SObjSimpleSetting r;
    SObjSimpleSetting r2;
    int i;
    GObj *o;
    GObj *o2;

    r = *lay;
    r.obj = (lay->obj & 0xFF00) ? 5 : 4;

    w->count = 1;
    w->objs = iosMallocDebug(ios_partition_sugipon, 1 * sizeof(GObj *), __FILE__, 759);

    for (i = 0; i < 1; i++) {
        /* PC port: a division by the literal 0: 0 / 0 for the only sword,
           which the EE's div.s makes +Fmax and IEEE makes NaN */
        queenSwordOfs[2] = ps2_div(weaponKind[w->kind].length * (float)i, 0.0f);
        o = CreateLayoutedGObj(10, 75, -1, i == 0, &r, -1, 7, 0);
        LinkParentOfDObj(o, &lnk);
        CopyVector(GOBJ_SUB(o)->root.pos, queenSwordOfs);
        w->objs[i] = o;
    }

    r2 = *lay;
    r2.obj = 13;
    o2 = CreateLayoutedGObj(46, 11, -1, 0, &r2, -1, 7, 0);
    GOBJ_SUB(o2)->parent = lnk;
    w->sword = o2;
}

typedef float WeaponVec[4] __attribute__((aligned(8))); /* derived name */

void *InitWeaponGeo(GObj *g, SObjSimpleSetting *lay)
{
    WeaponWork *w = iosMallocDebug(ios_partition_sugipon, sizeof(WeaponWork), __FILE__, 820);
    int i;

    GOBJ_SUB(g)->work = w;

    *w = swordWorkTemplate;
    w->kind = lay->obj & 0xFF;

    for (i = 0; i < GOBJ_SUB(g)->nodeNum; i++) {
        switch (w->kind) {
        case 0:
            break;

        case 1: {
            ObjNode lnk = {g, i};
            WeaponVec v = {0.0f, 0.0f, weaponKind[w->kind].length, 1.0f};
            GObj *o;
            SObjSimpleSetting r = *lay;

            r.obj = (lay->obj & 0xFF00) != 0;
            o = CreateLayoutedGObj(10, 75, -1, 1, &r, -1, 7, 1);
            LinkParentOfDObj(o, &lnk);
            CopyVector(GOBJ_SUB(o)->root.pos, v);
            SetTorchLife(o, (60 - systemStatus[0] * 10) / systemStatus[1] * 15,
                         (60 - systemStatus[0] * 10) / systemStatus[1] * 3);
            w->count = 1;
            w->objs = iosMallocDebug(ios_partition_sugipon, 1 * sizeof(GObj *), __FILE__, 848);
            w->objs[0] = o;
            w->buf = iosMallocDebug(ios_partition_sugipon, 352, __FILE__, 856);
            break;
        }

        case 5:
            initializeQueenzSword(g, i, lay);
            w->buf = iosMallocDebug(ios_partition_sugipon, 352, __FILE__, 861);
            break;

        case 7:
            w->buf = iosMallocDebug(ios_partition_sugipon, 352, __FILE__, 865);
            break;

        case 8:
        case 9:
            w->buf = iosMallocDebug(ios_partition_sugipon, 352, __FILE__, 870);
            w->net = iosMallocDebug(ios_partition_sugipon, 8, __FILE__, 871);
            w->model0 = CSVSYSTEM_InitDObj(accessary[SUBHANDLE_OF(g)->sub->accessary].model, lay);
            w->model1 = CSVSYSTEM_InitDObj(accessary[SUBHANDLE_OF(g)->sub->accessary].model2, lay);
            CopyQuaternion(g->dobj->root.quat, g->dobj->quat);
            UpdateRootMatrix(g);
            setWeaponOffsetMode(g, 1);
            break;

        default:
            w->buf = iosMallocDebug(ios_partition_sugipon, 352, __FILE__, 884);
            break;
        }
    }
    return w;
}

static void dispLaserSword(GObj *g, float t)
{
    WeaponWork *w = GOBJ_SUB(g)->work;

    p2o_DispVU1(g);
    if (1.0f < t) {
        CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(g)->nodeMtx);
        MatrixDrive_TransMatrix(0.0f, 0.0f, 7.5f);
        MatrixDrive_ScaleMatrix(1.0f, 1.0f, t / 100.0f);
        CopyMatrix((void *)w->model0->nodeMtx, MatrixDrive_GetMatrix());
        p2o_DispVU1DObj(w->model0);
        MatrixDrive_TransMatrix(w->net[0] * 0.1f, w->net[1] * 0.1f, 0.0f);
        CopyMatrix((void *)w->model1->nodeMtx, MatrixDrive_GetMatrix());
        p2o_DispVU1DObj(w->model1);
    }
}

/* the insect net's line colour and its handle, end to end */
static int netColor[4] = {128, 128, 128, 128}; /* derived name */

static float netHandleStart[4] = {0.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static float netHandleEnd[4] = {0.0f, 0.0f, 100.0f, 1.0f}; /* derived name */

typedef struct { /* field names derived */
    float f[4];
} __attribute__((aligned(16))) NetVec; /* derived name */

void dispInsectNet(GObj *g)
{
    int i;

    CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(g)->nodeMtx);
    gif_StartPacketPri(11);
    gif_SetZTest(1);
    gif_SetZWrite(1);
    gif_SetAlpha(1, 7, 128);
    DrawLineG((int *)netHandleStart, netColor, (int *)netHandleEnd, netColor, 0);
    for (i = 0; i <= 65535; i += 4096) {
        NetVec p = {
            {GetTableCos((short)i) * 30.0f, 0.0f, GetTableSin((short)i) * 30.0f + 130.0f, 1.0f}};
        NetVec q = {{GetTableCos((short)(i + 4096)) * 30.0f, 0.0f,
                     GetTableSin((short)(i + 4096)) * 30.0f + 130.0f, 1.0f}};

        DrawLineG(&p, netColor, &q, netColor, 0);
    }
    gif_EndPacket();
}

static void dispBlur(GObj *g)
{
    WeaponWork *w = GOBJ_SUB(g)->work;

    if (weaponKind[w->kind].blur != -1) {
        WeaponDef *e = &weaponKind[w->kind];
        void *s = w->buf;
        GifColor c = {e->color[0], e->color[1], e->color[2], e->color[3]};

        _SetCurrentMatrix(matrixptr + 0x100);
        gif_StartPacketPri(2);
        switch (e->blur) {
        case 0:
        default:
            gif_SetAlpha(1, 7, 128);
            break;
        case 1:
            gif_SetAlpha(1, 5, 128);
            break;
        case 2:
            gif_SetAlpha(1, 6, 128);
            break;
        }
        gif_SetZTest(1);
        gif_SetZWrite(1);
        gif_DrawStripF(s, c, 22, 1);
        gif_EndPacket();
    }
    if (w->kind == 8) {
        if (12.0f < w->bladeLength) {
            _UnitMatrix(MatrixDrive_GetMatrix());
            gif_StartPacketPri(2);
            gif_SetAlpha(1, 5, 128);
        }
        gif_EndPacket();
    }
}

static void calcBlur(GObj *g, float t)
{
    float q1[4];   /* 0x00 */
    float q2[4];   /* 0x10 */
    float d[4];    /* 0x20 */
    float p[4];    /* 0x30 */
    float m[4][4]; /* 0x40 */
    float n[4];    /* 0x80 */
    float a[4];    /* 0x90 */
    float b[4];    /* 0xA0 */
    float sub[4];  /* 0xB0 */
    float tmp[4];  /* 0xC0 */
    Sub15C *e = GOBJ_SUB(g);
    WeaponWork *w = (WeaponWork *)e->work;
    char *base;
    PEPartRec *vtx;
    char *dst1;
    char *dst2;
    PEGeo *pd;
    int h;
    int i;
    int j;
    int k;
    float ang;
    float r;

    GetInverseQuaternion(q1, e->root.itemQuat);
    MultiQuaternion(q1, e->root.quat, q1);
    SubVectorXYZ(d, e->root.pos, e->root.move);
    if (weaponKind[w->kind].blur == -1) {
        return;
    }
    base = w->buf;
    GetMatrixFromQuaternion(m, q1);
    _ApplyMatrix(a, m, ZUnitVector);
    GetMatrixFromQuaternion(m, e->root.quat);
    _ApplyMatrix(b, m, ZUnitVector);
    _OuterProduct(n, b, a);
    _NormalizeVector(n, n);
    ang = acosf(_InnerProduct(a, b)) * 10430.378f;
    _ScaleVectorXYZ(a, a, t);
    a[3] = 1.0f;
    for (i = 0; i < 11; i++) {
        float rr = (float)i / 10.0f;

        SetQuaternionByAxisRotateVWithNoRegularize(q2, (short)(ang * (float)i / 10.0f), n);
        sceVu0InterVector(p, e->root.pos, d, rr);
        GetMatrixFromQuaternionPos(m, q2, p);
        CopyVector(base + i * 32, m[3]);
        sceVu0ApplyMatrix(base + (i * 32 + 16), m, a);
    }
    if (w->kind >= 10) {
        return;
    }
    if (w->kind < 8) {
        return;
    }
    if (t > 12.0f) {
        h = SetParticleEffect(50, e->root.pos, IdentityQuaternion);
        if (h != -1) {
            pd = GetParticleEffectData(h);
            vtx = pd->parts;
            dst1 = pd->prim->vtx;
            dst2 = pd->prim->vtxNext;
            ExecParticleEffect(h);
            for (j = 0; j < pd->n; j++) {
                k = j * 10 / pd->n;
                r = random_unit();
                _InterVector(vtx->pos, base + (k + 1) * 32, base + ((k + 1) * 32 + 16), r);
                _InterVector(tmp, base + k * 32, base + (k * 32 + 16), r);
                _SubVector(sub, vtx->pos, tmp);
                _ScaleVector(vtx->vel, sub,
                             pd->pkg->speed * (pd->pkg->speedRand * random_signed() + 1.0f));
                sceVu0CopyVector(dst1, vtx->pos);
                dst1 += 32;
                sceVu0CopyVector(dst2, vtx->pos);
                dst2 += 32;
                vtx++;
            }
        }
    }
}

/* declared here: motionOrientManager.h reaches ico2/fumi's files through
   typedef.h, and commonact.c declares the table char [] */
extern const MotionDef motionKind[];

/* The MatrixDrive matrix, viewed as the union of float and int arrays this
   codebase uses for VU0 data. */
typedef union { /* field names derived */
    float f[4][4];
    int i[4][4];
} WeaponMatrix; /* derived name */

void WeaponGeo(GObj *g)
{
    WeaponWork *w = GOBJ_SUB(g)->work;
    int kind;
    int i;
    int n;
    float t;
    float a;

    getGeometry(g);
    checkHit(g);

    kind = w->kind;
    w->bladeOn = 0;
    if (w->kind >= 10 || kind < 8) {
        if (w->state == 1 ||
            (w->holder != 0 && motionKind[w->holder->dobj->ctrl.motion].flags2.bits.weaponSwing)) {
            calcBlur(g, weaponKind[kind].length);
            w->bladeOn = 1;
        }
    } else {
        weaponKind[kind].length = 40.0f;
        for (i = 0; i < 2; i++) {
            w->net[i] = random_signed_b();
        }
        if (w->holder != 0) {
            if (w->holder == boyGObj && ACTGame_FLAG_TETSUNAGI_VISUAL()) {
                weaponKind[w->kind].length = 270.0f;
            }
            if (w->bladeTimer >= 30.0f) {
                w->bladeLength += (weaponKind[w->kind].length - w->bladeLength) * 0.4f;
            } else {
                w->bladeTimer = w->bladeTimer + 1.0f;
                if (w->bladeTimer == 29.0f) {
                    ExecuteDirectSE(g, 0x101E7);
                }
            }
        } else {
            w->bladeLength += (0.0f - w->bladeLength) * 0.1f;
            w->bladeTimer = 0.0f;
        }
        calcBlur(g, w->bladeLength);
        w->bladeOn = 1;
        t = w->bladeLength;
        stage_SetLoopFlag(473, 1);
        if (t > 1.0f) {
            n = (int)stage_PlayBgAnimation(473, (float)w->humAnim,
                                           (char *)GOBJ_SUB(g)->nodeMtx + 0x30, IdentityQuaternion);
            if (systemStatus[5] == 0) {
                w->humAnim = n;
            }
        } else {
            stage_PlayBgAnimation(473, 0.0f, (char *)GOBJ_SUB(g)->nodeMtx + 0x30,
                                  IdentityQuaternion);
            w->humAnim = 0;
        }
        stage_SetLoopFlag(473, 0);
    }

    GetRootMatrix(MatrixDrive_GetMatrix(), g);
    CopyVector(w->tipPos, MatrixDrive_GetMatrix()[3]);
    if (w->offsetMode != 0) {
        float v[4] = {0.0f, 0.0f, 1.0f, 0.0f};

        v[3] = 0.0f;
        sceVu0ApplyMatrix(v, MatrixDrive_GetMatrix(), v);
        a = -atan2f(v[1], _Sqrt(1.0f - v[1] * v[1]));
        ((WeaponMatrix *)MatrixDrive_GetMatrix())->f[3][1] -=
            GetTableSin((short)(a * 10430.378f)) * 70.0f;
        CopyMatrix((char *)GOBJ_SUB(g)->nodeMtx, MatrixDrive_GetMatrix());
    }
}

void WeaponDL(GObj *g)
{
    WeaponWork *w = GOBJ_SUB(g)->work;

    switch (w->kind) {
    default:
        p2o_SetDefaultEnviroment();
        p2o_DispVU1(g);
        break;
    case 7:
        dispInsectNet(g);
        break;
    case 8:
    case 9:
        dispLaserSword(g, w->bladeLength);
        break;
    case 0:
        break;
    }
    if (w->bladeOn != 0) {
        dispBlur(g);
    }
}

void PickupWeapon(GObj *self, GObj *holder, int focus)
{
    WeaponWork *p = GOBJ_SUB(self)->work;

    p->holder = holder;
    p->holderId = GetSkeltonFocusNode(holder, focus);
    GOBJ_SUB(holder)->ctrl.pickedWeapon = self;
    ico_gs_signal(holder == boyGObj ? ICO_GS_EV_WEAPON : ICO_GS_EV_NONE, p->kind);
}

GObj *CheckSwapableWeapon(GObj *self, float dist)
{
    GObj *found = 0;
    float best = dist * dist;
    GObj *g = isysGObjSearchFromObjKindID_begin(14);
    float pos[4];
    float d;

    GetRootPosition(pos, self);

    for (; g != 0; g = isysGObjSearchFromObjKindID_next(g)) {
        WeaponWork *w;
        float *wp;

        if (g == self)
            continue;

        w = GOBJ_SUB(g)->work;
        if (w->kind == 0)
            continue;

        if (w->holder != 0)
            continue;

        if (g->active == 0)
            continue;

        wp = w->tipPos;
        if (stage_no == 4 && g->labelId != 128)
            continue;

        d = distance_squared(pos, wp);
        if (d < best) {
            found = g;
            best = d;
        }
    }
    return found;
}

void ReleaseWeapon(GObj *self)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    if (p->holder) {
        p->holder->dobj->ctrl.pickedWeapon = 0;
    }
    p->holder = 0;
    p->holderId = -1;
    p->state = 0;
}

int CheckWeaponKind(GObj *self)
{
    return ((WeaponWork *)GOBJ_SUB(self)->work)->kind;
}

void LightTorchOnOfWeapon(GObj *self)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    int i;

    if (p->count) {
        torchOnOfWeaponSE(p->objs[0]);
    }
    for (i = 0; i < p->count; i++) {
        LightTorchOn(p->objs[i]);
    }
}

void LightTorchOnOfWeaponWithNoSE(GObj *self)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    int i;

    if (p->count) {
        torchOnOfWeaponSE(p->objs[0]);
    }
    for (i = 0; i < p->count; i++) {
        LightTorchOn(p->objs[i]);
    }
}

void LightTorchOffOfWeapon(GObj *self)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    int i;

    for (i = 0; i < p->count; i++) {
        LightTorchOff(p->objs[i]);
    }
}

GObj *GetTorchGObjOfWeapon(GObj *self)
{
    WeaponWork *p = GOBJ_SUB(self)->work;
    if (p->count) {
        return p->objs[0];
    }
    return 0;
}

/* ReleaseWeapon's body is expanded in place here.  LightTorchOnOfWeapon and
 * LightTorchOnOfWeaponWithNoSE come from one source body. */
void ReleaseWeaponWithFumble(GObj *self, float *move, float *quat)
{
    Sub15C *e = GOBJ_SUB(self);
    WeaponWork *w = (WeaponWork *)e->work;
    struct MotRoot *root = &e->root;

    if (w->holder) {
        w->holder->dobj->ctrl.pickedWeapon = 0;
    }
    w->holder = 0;
    w->holderId = -1;
    w->state = 1;

    if (quat) {
        CopyQuaternion(root->itemQuat, quat);
    }
    CopyVector(root->move, move);
    root->move[3] = 0.0f;
}

int InitWeaponFumbleSequence(GObj *self)
{
    ((WeaponWork *)GOBJ_SUB(self)->work)->fumbleSlot = 0;
    return 1;
}

float GetWeaponWeight(GObj *self)
{
    return (float)weaponKind[((WeaponWork *)GOBJ_SUB(self)->work)->kind].weight;
}

void SetWeaponTorchChainReactionFlagAll(int flag)
{
    GObj *g;
    WeaponWork *w;
    int i;

    for (g = isysGObjSearchFromObjKindID_begin(14); g; g = isysGObjSearchFromObjKindID_next(g)) {
        w = GOBJ_SUB(g)->work;
        if (w->kind == 1) {
            for (i = 0; i < w->count; i++) {
                SetTorchChainReactionFlag(w->objs[i], flag);
            }
        }
    }
}

void *InitDemoQueensSword(GObj *g, SObjSimpleSetting *lay)
{
    WeaponWork *w;
    int i;

    w = (WeaponWork *)iosMallocDebug(ios_partition_sugipon, sizeof(WeaponWork), __FILE__, 802);
    GOBJ_SUB(g)->work = w;
    *w = swordWorkTemplate;
    for (i = 0; i < GOBJ_SUB(g)->nodeNum; i++) {
        initializeQueenzSword(g, i, lay);
    }
    return w;
}

void ExecDemoQueensSword(GObj *g)
{
    Sub15C *e = GOBJ_SUB(g);
    WeaponWork *w = e->work;
    w->sword->active = e->disp;
}

void SetWeaponOffsetMode(GObj *self, int mode)
{
    setWeaponOffsetMode(self, mode);
}
