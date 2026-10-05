#include "box.h"
#include "sugiCommon.h"
#include "main.h"
#include "matrixDrive.h"
#include "fieldCollision.h"
#include "quaternion.h"
#include "DObj.h"
#include "Primitive.h"
#include "debug.h"
#include "gamesys.h"
#include "generator.h"
#include "frameDependSequence.h"
#include "item.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "tableSin.h"
#include <libvu0.h>
#include <string.h>
#include "motionFileManager.h"
#include "sceneManager.h"
#include "Matrix.h"
#include "motionManager.h"
#include "GifPacket.h"
#include <math.h>
#include "stageMultiBgaManager.h"
#include "DisplayP2O.h"
#include "lineManager.h"
#include "attackhit.h"
#include "obj_manager.h"
#include "memory.h"
#include "geometryManager.h"
#include "ios.h"
#include "particleEffect.h"
#include "objact.h"
#include "gobj.h"
#include "switch.c.inc"

/* The 416-byte box work block InitBoxGeo allocates and seeds from the
   template below, aligned(8). */
typedef struct BoxWork { /* field names derived */
    int serial;          /* 0x000, the box's number, mod 30 */
    GObj *holder;        /* 0x004, the character holding the box, mailed 25 when it falls */
    char pad008[8];
    float holdPoint[4]; /* 0x010, the hold point GetBoxHoldPoint last reported */
    int mode;     /* 0x020, 0 still, 1 auto move, 2 falling, 3 to 5 the fall's phases, 6 aligning */
    float scaleX; /* 0x024, the layout's X scale */
    float scaleZ; /* 0x028, the layout's Z scale */
    /* 0x02C, the collision ReInitBoxGeo restores: Sub15C.colData, a
       pointer held as a word (an int on the EE truncated it on x64) */
    ICO_WORD colData;
    int moveFrames; /* 0x030, the frames left of an auto move */
    char pad034[12];
    float vel[4];   /* 0x040, the move per frame */
    int frontPoint; /* 0x050, the path point nearest the front axle */
    int rearPoint;  /* 0x054, the path point nearest the rear axle */
    int route;      /* 0x058, the routeTable index */
    int pointCount; /* 0x05C, the route's point count */
    WallCfg wall;   /* 0x060, the wall the box last hit */
    char pad06C[4];
    float mtx[4][4];      /* 0x070, the box's matrix */
    float waterHeight[4]; /* 0x0B0, the water height GetWaterReaction reports */
    float tilt[4];        /* 0x0C0, the floating box's tilt */
    float tiltVel[4];     /* 0x0D0, the tilt's velocity */
    float lastOffset[4];  /* 0x0E0, the last frame's float offset */
    float tiltForce[4];   /* 0x0F0, the force added to the tilt each frame */
    float lastPos[4];     /* 0x100, the last frame's position */
    int moving;           /* 0x110, set while a push or pull is under way */
    int seStopped;        /* 0x114, set once the move sound has been stopped at the path's end */
    short floatPhase;     /* 0x118, the bob's sine phase */
    short pad11A;
    Sub15C *wheelDObj; /* 0x11C, the wheel DObj dispWheels draws */
    short wheelAngle;  /* 0x120, the wheels' X rotation */
    short pad122;
    float wheelRadius; /* 0x124 */
    float wheelHeight; /* 0x128 */
    float wheelFront;  /* 0x12C, the front axle's Z */
    float wheelRear;   /* 0x130, the rear axle's Z */
    float friction;    /* 0x134, the move's damping, 0.85 or 0.98 */
    int autoDir;       /* 0x138, the direction moveBoxAutoMatic last moved in */
    char pad13C[4];
    int stopWall; /* 0x140, set while a box-stop wall is ahead */
    char pad144[12];
    float tiltQuat[4];  /* 0x150, the slope tilt the root quaternion takes */
    Sub15C *effectDObj; /* 0x160, the effect DObj */
    int charHit;        /* 0x164, set when a character pushed the floating box this frame */
    char pad168[8];
    float floatAnchor[4];         /* 0x170, the floating box's resting X and Z */
    ICO_WORD_PTR(GObj *) subGObj; /* 0x180, the layouted sub GObj, held as a word */
    char pad184[12];
    float moveDir[4];                  /* 0x190, the direction of the last push */
} __attribute__((aligned(8))) BoxWork; /* derived name */

#ifdef ICO_HOST

/* colData holds Sub15C.colData across ReInitBoxGeo: it must keep the
   pointer's width (DIVERGENCES.md D10) */
_Static_assert(sizeof(((BoxWork *)0)->colData) == sizeof(((Sub15C *)0)->colData),
               "BoxWork.colData is narrower than Sub15C.colData");

#endif

static void landingSE(GObj *self)
{
    ExecuteSEPackage(self, 0x2);
}

static void fallDownStartSE(GObj *self)
{
    ExecuteSEPackage(self, 0x24);
}

static void pushStartSE(GObj *self)
{
    ExecuteSEPackage(self, 0x4);
}

static void pullStartSE(GObj *self)
{
    ExecuteSEPackage(self, 0xD);
}

static void wallHitSE(GObj *self)
{
    ExecuteSEPackage(self, 0x1E);
}

/* inlined into onPath and into ExecBoxMoveEndReaction */
static inline void stopBoxMoveSE(GObj *self) /* derived name */
{
    BoxWork *q = GOBJ_SUB(self)->work;

    StopSEPackage(self);
    StopSEPackageWithGroupVariation(self, 1);

    ExecuteSEPackage(self, 0x16);
    if (q->stopWall != 0) {
        wallHitSE(self);
        q->stopWall = 0;
    }
}

static void initFallDown(GObj *self)
{
    float pos[4];
    float pts[16];
    float n[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    GetRootPosition(pos, self);
    GOBJ_SUB(self)->colRotate = 0;
    *(int *)&GOBJ_SUB(self)->ctrl.animFrame = 0;
    if (p->wall.elem != 0) {
        GetWallGlobalInfo(pts, n, p->wall.elem,
                          (char *)GOBJ_SUB(p->wall.o.obj)->nodeMtx + (p->wall.o.node << 6));
        n[1] = 0.0f;
        sceVu0Normalize(n, n);
        SetIdentityQuaternion(GOBJ_SUB(self)->root.baseQuat);
        RotQuaternionY(GOBJ_SUB(self)->root.baseQuat, GetTableArcTan2(n[0], n[2]));
        GetRootQuaternion(GOBJ_SUB(self)->root.motionQuat, self);
        DivQuaternion(GOBJ_SUB(self)->root.motionQuat, GOBJ_SUB(self)->root.motionQuat,
                      GOBJ_SUB(self)->root.baseQuat);
        GetMatrixFromQuaternionPos(p->mtx[0], GOBJ_SUB(self)->root.baseQuat, (char *)pos);
        GOBJ_SUB(self)->ctrl.motion = 1143;
    } else {
        GOBJ_SUB(self)->ctrl.motion = 1143;
        GetRootQuaternion(GOBJ_SUB(self)->root.baseQuat, self);
    }
}

static int checkFieldContact(GObj *self, float lim)
{
    ClipWork w;
    float pos[4];
    float v[4];
    int r;

    GetRootPosition(pos, self);
    CopyVector(v, pos);
    v[1] -= GOBJ_SUB(self)->root.move[1];
    GetLowerPlaneCollision(&w, v);
    r = CheckFieldContact(&w, self, pos, lim);
    if (GOBJ_SUB(self)->parent.obj != 0) {
        UnlinkParentOfDObj(self);
    }
    GOBJ_SUB(self)->ctrl.floorAttr = 0;
    switch (r) {
    case 1:
        if (self != w.floor.o.obj) {
            if (GOBJ_SUB(self)->parent.obj != w.floor.o.obj ||
                GOBJ_SUB(self)->parent.node != w.floor.o.node) {
                LinkParentOfDObj(self, &w.floor.o);
                GOBJ_SUB(self)->ctrl.floorAttr = GetFloorAttribute(&w);
            }
        }
        w.pt[1][1] = w.pt[2][1] - 50.0f;
        SetDirectRootPosition(self, w.pt[1]);
        return 1;
    case 2:
        GOBJ_SUB(self)->ctrl.floorAttr = GetFloorAttribute(&w);
        return 2;
    }
    return 0;
}

/* Inlined into execNormalMove twice (once with ClipWall, once with
   ClipWallBoxStop) and into inertiaMove once; the two constant arguments
   fold, so each inlining carries only one of the two clip calls.  The work
   buffer and the root-position scratch are the caller's: execNormalMove's
   two expansions get two separate work buffers and share one output
   vector. */
static inline void checkBoxWallHit(GObj *self, ClipWork *w, float *base, float *out,
                                   int stop) /* derived name */
{
    BoxWork *p = GOBJ_SUB(self)->work;

    GetRootPosition(base, self);
    if (out != 0) {
        CopyVector(out, base);
    }
    w->radius = (p->scaleX < p->scaleZ ? p->scaleX : p->scaleZ) * 50.0f - 5.0f;
    base[1] += 40.0f;
    CopyVector(w->pt[0], base);
    CopyVector(w->pt[1], base);
    if (stop != 0) {
        ClipWallBoxStop(w);
    } else {
        ClipWall(w);
    }
    w->pt[2][1] -= 40.0f;
}

/* this file's uses of these do not fit the prototypes in the headers that
   declare them */

/* the two debug lines the wall fit prints; the second is EUC-JP, "this
   terrain is wrong (it is not cut to 100cm)" */

/* the record the clip work reports at +0x80: the contact point's x and z,
   and the hit flag the caller has just tested at +0x88.  The staging copy
   fills the point and the flag with two assignments; the read back is one
   assignment of the whole record. */
typedef struct { /* field names derived */
    float x;
    float z;
} BoxWallPt; /* derived name */

typedef struct { /* field names derived */
    BoxWallPt pt;
    int hit;
} BoxWallRec; /* derived name */

static int execNormalMove(GObj *self, int stop)
{
    ClipWork stopWork;
    float pos[4];
    ClipWork work;
    float wn[4];
#ifdef ICO_HOST
    WallCfg wnCfg;
#else
    BoxWallRec wn2;
#endif
    float plTop[4];
    float plSide[4];
    float up[4];
    float proj[4];
    float axis[4];
    float norm[4];
    float rot[4];
    BoxWork *p = GOBJ_SUB(self)->work;
    int ret = 1;
    float hw;
    float d;
    float dy;
    float adj;
    short ang;

    if (stop != 0) {
        checkBoxWallHit(self, &stopWork, pos, 0, 1);
        if (stopWork.wall.elem != 0) {
            ret = 0;
            SetDirectRootPosition(self, stopWork.pt[2]);
        }
    }

    if (checkFieldContact(self, 110.0f) != 1) {
        if (p->holder != 0) {
            iosOmSendMail(p->holder, 25, self);
            p->holder = 0;
        }
        initFallDown(self);
        fallDownStartSE(self);
        p->mode = 2;
    } else {
        if (stop == 0) {
            checkBoxWallHit(self, &work, wn, pos, 0);
            if (work.wall.elem != 0) {
#ifdef ICO_HOST
                wnCfg = work.wall;
#else
                wn2.pt = *(BoxWallPt *)&work.wall.o;
                wn2.hit = (int)work.wall.elem;
                *(BoxWallRec *)wn = wn2;
#endif

                hw = (p->scaleX < p->scaleZ ? p->scaleX : p->scaleZ) * 50.0f;

                CopyVector(up, pos);
                up[1] += 50.0f;
#ifdef ICO_HOST
                GetPureVerticalPlane(plTop, plSide, 0, &wnCfg, 0);
#else
                GetPureVerticalPlane(plTop, plSide, 0, (WallCfg *)wn, 0);
#endif

                d = plane_distance(up, plSide);

                if (hw - 10.0f < d) {
                    CopyQuaternion(p->tiltQuat, IdentityQuaternion);
                } else {
                    GetProjectionPosOfPlane(proj, plSide, up);
                    GetProjectionPosOfPlane(proj, plTop, proj);

                    dy = up[1] - proj[1];
                    if (dy < 60.0f) {
                        adj = dy * (1.0f - d / hw);
                        pos[1] = pos[1] - adj;
                        SetDirectRootPosition(self, pos);

                        axis[0] = 0.0f;
                        axis[1] = dy;
                        axis[2] = d + hw;
                        axis[3] = 0.0f;

                        _NormalizeVector(axis, axis);
                        _OuterProduct(norm, YUnitVector, plSide);
                        ang = GetTableArcTan2(axis[0], axis[2]);
                        SetQuaternionByAxisRotateV(rot, ang, norm);
                        debug_StdPrintfDummy("height: %f   dist: %f  ofs: %f %x \n", dy, d, adj,
                                             ang);

                        GetSlerpQuaternion(p->tiltQuat, rot, p->tiltQuat, 0.5f);
                    } else {
                        /* EUC-JP: "this terrain is wrong (it is not divided into 100 cm)" */
                        debug_StdPrintfDummy("この地形はおかしいです(100cmに区切られていません)\n");
                        GetSlerpQuaternion(p->tiltQuat, IdentityQuaternion, p->tiltQuat, 0.5f);
                    }
                }
            } else {
                GetSlerpQuaternion(p->tiltQuat, IdentityQuaternion, p->tiltQuat, 0.5f);
            }
            GetInverseQuaternion(
                axis, ICO_RAWP(char *, GOBJ_SUB(self), 0x60, (char *)GOBJ_SUB(self)->quat));
            MultiQuaternion(proj, axis, p->tiltQuat);
            SetRootQuaternion(self, proj);
        } else {
            SetIdentityQuaternion(p->tiltQuat);
        }
    }

    return ret;
}

/* inlined once, into execAutoMove */
static inline void setBoxStopWallFlag(GObj *self, float *vel) /* derived name */
{
    ClipWork w;
    float dir[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    _NormalizeVector(dir, vel);
    _ScaleVector(dir, dir, (p->scaleX < p->scaleZ ? p->scaleX : p->scaleZ) * 50.0f + 25.0f);
    GetRootPosition(w.pt[0], self);
    AddVectorXYZ(w.pt[1], w.pt[0], dir);
    ClipWallBoxStop(&w);
    if (w.wall.elem != 0) {
        p->stopWall = 1;
    } else {
        p->stopWall = 0;
    }
}

static int execAutoMove(GObj *self)
{
    float pos[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    GetRootPosition(pos, self);
    _AddVector(pos, pos, p->vel);
    SetDirectRootPosition(self, pos);
    execNormalMove(self, 0);
    setBoxStopWallFlag(self, (float *)p->vel);
    if (--p->moveFrames <= 0) {
        p->mode = 0;
    }
    return 1;
}

static inline float getAlign(float v, float g)
{
    if (0.0f <= v) {
        return (float)(int)((v + g * 0.5f) / g) * g;
    }
    return -getAlign(-v, g);
}

static inline void alignPosition(GObj *self, float *dst, float *src, float grid) /* derived name */
{
    float npos[4];
    char *n = (char *)(ICO_WORD)GOBJ_SUB(self);
    float cx = ICO_RAW(float, n, 0x50, GOBJ_SUB(self)->matrix[3][0]);
    float cz = ICO_RAW(float, n, 0x58, GOBJ_SUB(self)->matrix[3][2]);

    CopyVector(npos, src);
    npos[0] = getAlign(src[0] - cx, grid) + cx;
    npos[2] = getAlign(src[2] - cz, grid) + cz;

    CopyVector(dst, npos);
}

int AlignBox(GObj *self, float grid)
{
    float pos[4];
    float quat[4];
    Sub15C *sub = GOBJ_SUB(self);
    BoxWork *q = sub->work;

    GetInverseQuaternion(quat, ICO_RAWP(char *, sub, 0x60, (char *)sub->quat));
    SetRootQuaternion(self, quat);
    GetRootPosition(pos, self);
    alignPosition(self, pos, pos, grid);
    SetDirectRootPosition(self, pos);
    q->mode = 0;
    return 0;
}

/* Lines 558 to 560 are one call-site line, the same DObj-buffer setup
   ico2/omori/src/chain.c expands by hand in InitChainGeo: the wheel count is
   2 here, so the three allocation sizes are 2<<6, 2<<4 and 2*80 bytes, and
   the 560 the allocator records is the line of the call. */
static void initWheels(GObj *self, SObjSimpleSetting *lay)
{
    BoxWork *w = GOBJ_SUB(self)->work;
    int i;

    if (objLayout[self->labelId].accessary == 26 ||
        accessary[GOBJ_SUB(self)->accessary].model == 0x610) {
        w->wheelDObj = 0;
    } else {
        w->wheelDObj = CSVSYSTEM_InitDObj(accessary[GOBJ_SUB(self)->accessary].model, lay);
        if (w->wheelDObj->nodeMtx != 0) {
            iosFree((void *)(ICO_PHYS(w->wheelDObj->nodeMtx)));
        }
        if (w->wheelDObj->nodeQuat != 0) {
            iosFree((void *)(ICO_PHYS(w->wheelDObj->nodeQuat)));
        }
        w->wheelDObj->nodeMtx = 0;
        w->wheelDObj->nodeQuat = 0;
        w->wheelDObj->nodeMtx = (ICO_WORD)iosMallocDebug(ios_partition_seki, 128, __FILE__, 560);
        w->wheelDObj->nodeQuat = (ICO_WORD)iosMallocDebug(ios_partition_seki, 32, __FILE__, 560);
        w->wheelDObj->nodeNum = 2;
        if (w->wheelDObj->nodes != 0) {
            iosFree((void *)(ICO_PHYS(ICO_ADDR(w->wheelDObj->nodes))));
        }
        w->wheelDObj->nodes = iosMallocDebug(ios_partition_seki, 160, __FILE__, 560);

        for (i = 0; i < 2; i++) {
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->flags.ll &= ~1;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->flags.ll &= ~2;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->pos[0] = 0.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->pos[1] = 0.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->pos[2] = 0.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->pos[3] = 1.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->flags.ll &= ~4;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->fade = 0;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->alpha = 1.0f;
            }
            {
                char *e = (char *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                *(short *)(e + 0x3A) = 0;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->scale[0] = 1.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->scale[1] = 1.0f;
            }
            {
                struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->wheelDObj->nodes);
                e->scale[2] = 1.0f;
            }
        }
        w->wheelDObj->dispType = 2;

        /* the sub-object handle at 0x15C read through the file's IntFloat
           union, as the wheel-float stores are */
#ifdef ICO_HOST
        ((IntFloat *)&w->wheelHeight)->f = accessary[self->dobj->accessary].pivot[0];
        ((IntFloat *)&w->wheelFront)->f = accessary[self->dobj->accessary].pivot[1];
        ((IntFloat *)&w->wheelRear)->f = accessary[self->dobj->accessary].pivot[2];
#else
        ((IntFloat *)&w->wheelHeight)->f =
            accessary[*(int *)(((IntFloat *)&self->dobj)->i + 0x844)].pivot[0];
        ((IntFloat *)&w->wheelFront)->f =
            accessary[*(int *)(((IntFloat *)&self->dobj)->i + 0x844)].pivot[1];
        ((IntFloat *)&w->wheelRear)->f =
            accessary[*(int *)(((IntFloat *)&self->dobj)->i + 0x844)].pivot[2];
#endif
    }
}

/* inlined once, into action's case 0.  10430.3779f is 65536 / (2 * pi),
   the radian-to-angle-table factor (ico2/seki/src/Primitive.c spells it the
   same way). */
static inline void updateBoxWheelAngle(GObj *self) /* derived name */
{
    BoxWork *p = GOBJ_SUB(self)->work;

    if (p->wheelDObj != 0) {
        p->wheelAngle = (short)((float)p->wheelAngle - p->vel[2] * 10430.3779f / p->wheelRadius);
    }
}

static void dispWheels(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    if (p->wheelDObj == 0) {
        return;
    }
    CopyMatrix(MatrixDrive_GetMatrix(), *(void **)&GOBJ_SUB(self)->nodeMtx);
    MatrixDrive_TransMatrix(0.0f, p->wheelHeight, 0.0f);
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrix(0.0f, 0.0f, p->wheelFront);
    MatrixDrive_RotMatrixX(p->wheelAngle);
    CopyMatrix((void *)p->wheelDObj->nodeMtx, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
    MatrixDrive_TransMatrix(0.0f, 0.0f, p->wheelRear);
    MatrixDrive_RotMatrixX((short)(*(unsigned short *)&p->wheelAngle + 0x4000));
    CopyMatrix((char *)p->wheelDObj->nodeMtx + 0x40, MatrixDrive_GetMatrix());
    p2o_DispVU1DObjMulti(p->wheelDObj);
}

/* one 16-byte route point */
typedef float PathPt[4]; /* derived name */

/* The routes a box can be pushed along: each a list of points ending in one
   whose fourth word is the largest float (countPathPoints stops at a fourth
   word of 10.0f or more).  Routes 9 and 18 to 29 are empty. */
static PathPt route1[] = {
    {-3380.0f, -3500.0f, 100.0f, 1.0f},  {-3380.0f, -3500.0f, 5700.0f, 1.0f},
    {-3337.0f, -3500.0f, 5890.0f, 1.0f}, {-3200.0f, -3500.0f, 6050.0f, 1.0f},
    {-3150.0f, -3500.0f, 6090.0f, 1.0f}, {-2930.0f, -3500.0f, 6150.0f, 1.0f},
    {-1000.0f, -3500.0f, 6150.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route2[] = {
    {0.0f, -50.0f, 0.0f, 1.0f},          {-300.0f, -50.0f, 0.0f, 1.0f},
    {-1000.0f, -50.0f, 700.0f, 1.0f},    {-1000.0f, -50.0f, 1400.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route3[] = {
    {-525.0f, 1450.0f, -1650.0f, 1.0f},
    {-1700.0f, 1450.0f, -1650.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route4[] = {
    {6500.0f, -3470.0f, -1450.0f, 1.0f},
    {6500.0f, -3470.0f, 3400.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route5[] = {
    {500.0f, 2650.0f, -2050.0f, 1.0f},
    {500.0f, 2650.0f, -2600.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route6[] = {
    {-2890.0f, -3510.0f, 6150.0f, 1.0f},
    {1000.0f, -3510.0f, 6150.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route7[] = {
    {-3400.0f, -3450.0f, -2300.0f, 1.0f}, {-3400.0f, -3450.0f, 5680.0f, 1.0f},
    {-3290.0f, -3450.0f, 6000.0f, 1.0f},  {-3000.0f, -3450.0f, 6150.0f, 1.0f},
    {1100.0f, -3450.0f, 6150.0f, 1.0f},   {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route8[] = {
    {-4750.0f, 650.0f, 3300.0f, 1.0f},
    {-4750.0f, 650.0f, 3900.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route9[] = {
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route10[] = {
    {-380.0f, -50.0f, -570.0f, 1.0f},
    {-220.0f, -50.0f, -570.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route11[] = {
    {-380.0f, -50.0f, -1370.0f, 1.0f},
    {-220.0f, -50.0f, -1370.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route12[] = {
    {-380.0f, -50.0f, -2170.0f, 1.0f},
    {-220.0f, -50.0f, -2170.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route13[] = {
    {-380.0f, -50.0f, -2970.0f, 1.0f},
    {-220.0f, -50.0f, -2970.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route14[] = {
    {380.0f, -50.0f, -570.0f, 1.0f},
    {220.0f, -50.0f, -570.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route15[] = {
    {380.0f, -50.0f, -1370.0f, 1.0f},
    {220.0f, -50.0f, -1370.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route16[] = {
    {380.0f, -50.0f, -2170.0f, 1.0f},
    {220.0f, -50.0f, -2170.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route17[] = {
    {380.0f, -50.0f, -2970.0f, 1.0f},
    {220.0f, -50.0f, -2970.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 3.40282347e+38f},
}; /* derived name */

static PathPt route18[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route19[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route20[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route21[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route22[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route23[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route24[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route25[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route26[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route27[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route28[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

static PathPt route29[] = {{0.0f, 0.0f, 0.0f, 3.40282347e+38f}}; /* derived name */

/* the route a box's layout names (the low half of its kind word) indexes
   this table; route 0 is no route and the last two slots are empty */
static PathPt *routeTable[32] = {
    0,       route1,  route2,  route3,  route4,  route5,  route6,  route7,  route8,  route9,
    route10, route11, route12, route13, route14, route15, route16, route17, route18, route19,
    route20, route21, route22, route23, route24, route25, route26, route27, route28, route29,
}; /* derived name */

/* GetBoxHoldPoint's four hold-point candidates in the box's local frame and
   the four offsets added back after they are scaled by the box's half
   extents */
static float holdPointLocal[4][4] = {
    {0.0f, 0.0f, 50.0f, 1.0f},
    {50.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -50.0f, 1.0f},
    {-50.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

static float holdPointOffset[4][4] = {
    {0.0f, 0.0f, 10.0f, 1.0f},
    {10.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -10.0f, 1.0f},
    {-10.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

static BoxWork boxWorkInit = {
    0,
    0,
    {0},
    {0.0f, 0.0f, 0.0f, 1.0f},
    0,
    1.0f,
    1.0f,
    0,
    0,
    {0},
    {0.0f, 0.0f, 0.0f, 0.0f},
    2,
    1,
    0,
    0,
    {{0, -1}, 0},
    {0},
    {{0.0f}},
    {0},
    {0.0f},
    {0.0f},
    {0.0f},
    {0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    10.0f,
    30.0f,
    50.0f,
    -50.0f,
    0.9f,
    0,
    {0},
    0,
    {0},
    {0.0f, 0.0f, 0.0f, 1.0f},
    0,
    0,
    {0},
    {0.0f, 0.0f, 0.0f, 1.0f},
    0,
    {0},
    {0.0f, 0.0f, 1.0f, 0.0f},
}; /* derived name */

/* the Y axis the side plane is built from */
static float yAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f}; /* derived name */

/* inlined once, into InitBoxGeo; a route array ends at the first point
   whose fourth word is 10.0f or more */
static inline int countPathPoints(int route) /* derived name */
{
    PathPt *pts = routeTable[route];
    int i;

    for (i = 1; pts[i][3] < 10.0f; i++) {}

    return i;
}

/* The signed plane distance is what the projection is scaled by and its
   magnitude is what the nearest test keeps.  0.707 is the 45 degree axis
   test. */
static int getNearestPosition(float *out, int *pidx, int *path)
{
    float pos[4];
    float seg[4];
    float dir[4];
    float proj[4];
    float pl[4];
    float foot[4];
    PathPt *pts = routeTable[path[0]];
    float best = 3.40282347e+38f;
    int bi = -1;
    int start;
    int end;
    int i;
    float dist;
    float ad;
    float t;

    if (*pidx != -1) {
        start = *pidx - 1;
        start = 0 < start ? start : 1;
        end = *pidx + 2;
        end = path[1] < end ? path[1] : end;
    } else {
        start = 0;
        end = path[1] - 1;
    }

    for (i = start; i < end; i++) {
        sceVu0SubVector(seg, pts[i], pts[i - 1]);
        seg[1] = 0.0f;
        sceVu0Normalize(dir, seg);
        sceVu0OuterProduct(pl, yAxis, dir);
        pl[1] = 0.0f;
        sceVu0Normalize(pl, pl);
        pl[3] = -(pl[0] * pts[i][0] + pl[2] * pts[i][2]);
        dist = pl[0] * out[0] + pl[2] * out[2] + pl[3];
        ad = dist < 0.0f ? -dist : dist;
        pl[1] = 0.0f;
        pl[3] = 0.0f;
        sceVu0ScaleVector(proj, pl, dist);
        SubVectorXYZ(foot, out, proj);

        if (0.707f < (dir[0] < 0.0f ? -dir[0] : dir[0])) {
            t = (foot[0] - pts[i - 1][0]) / seg[0];
        } else {
            t = (foot[2] - pts[i - 1][2]) / seg[2];
        }
        if (0.0f <= t && t <= 1.0f) {
            CopyVector(out, foot);
            *pidx = i;
            return 0;
        }
        if (ad < best) {
            best = ad;
            bi = i;
            if (t < 0.0f) {
                CopyVector(pos, pts[i - 1]);
            } else {
                CopyVector(pos, pts[i]);
            }
        }
    }
    out[0] = pos[0];
    out[2] = pos[2];
    out[3] = 1.0f;

    *pidx = bi;
    return 1;
}

static void onPathInitialize(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;
    float front[4];
    float rear[4];
    /* the wheel offset, one variable assigned on each wheel's line */
    float ofs;
    Vec16 fv = {{0.0f, 0.0f, p->scaleZ * (ofs = 50.0f), 1.0f}};
    Vec16 rv = {{0.0f, 0.0f, p->scaleZ * (ofs = -50.0f), 1.0f}};
    float quat[4];

    p->frontPoint = p->rearPoint = -1;
    sceVu0ApplyMatrix(front, (void *)GOBJ_SUB(self)->nodeMtx, &fv);
    sceVu0ApplyMatrix(rear, (void *)GOBJ_SUB(self)->nodeMtx, &rv);
    getNearestPosition(front, (int *)&p->frontPoint, (int *)&p->route);
    getNearestPosition(rear, (int *)&p->rearPoint, (int *)&p->route);
    debug_StdPrintfDummy("front pos: %f, %f, %f\n", front[0], front[1], front[2]);
    debug_StdPrintfDummy("rear  pos: %f, %f, %f\n", rear[0], rear[1], rear[2]);
    if (distance_squared(front, rear) < 0.010000001f) {
        GetRootQuaternion(quat, self);
        RotQuaternionY(quat, 16384);
        SetRootQuaternion(self, quat);
        UpdateRootMatrix(self);
    }
}

/* this file's uses of these do not fit the prototypes in the headers that
   declare them */

/* the debug switch the wall-fit trace is printed under */

/* the four trace lines */

/* the two route colours onPath draws the route and its end points in, one
   word per channel, RGBA */
static int routeFrontColor[4] = {0, 128, 255, 128}; /* derived name */

static int routeRearColor[4] = {255, 128, 0, 128}; /* derived name */

/* 10430.378 is 32768 / pi, the radian-to-angle-table factor.  The
   quaternion's initialiser is mostly zero, and each wheel offset is a VECTOR
   record built by an initialiser, as onPathInitialize's are. */
static int onPath(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;
    Vec4 front;
    Vec4 rear;
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    VECTOR fwd = {0.0f, 0.0f, p->scaleZ * 50.0f, 1.0f};
    VECTOR bwd = {0.0f, 0.0f, p->scaleZ * -50.0f, 1.0f};
    PathPt *pts = routeTable[p->route];
    Vec4 mid;
    Vec4 ofs;
    float m[16];
    Vec4 dir;
    int hitFront;
    int hitRear;
    int i;

    sceVu0ApplyMatrix(&front, (void *)GOBJ_SUB(self)->nodeMtx, &fwd);
    sceVu0ApplyMatrix(&rear, (void *)GOBJ_SUB(self)->nodeMtx, &bwd);

    hitFront = getNearestPosition(front.f, (int *)&p->frontPoint, (int *)&p->route);
    hitRear = getNearestPosition(rear.f, (int *)&p->rearPoint, (int *)&p->route);

    RotQuaternionY(q, (short)(atan2f(front.f[0] - rear.f[0], front.f[2] - rear.f[2]) * 10430.378f));
    SetRootQuaternion(self, q);

    if (hitFront != 0 || hitRear != 0) {
        if (hitFront != 0) {
            GetMatrixFromQuaternion(m, q);
            CopyVector(&ofs, &bwd);
            ofs.f[2] -= 1.0f;
            sceVu0ApplyMatrix(&ofs, m, &ofs);
            AddVectorXYZ(&mid, &front, &ofs);
        } else {
            GetMatrixFromQuaternion(m, q);
            CopyVector(&ofs, &fwd);
            ofs.f[2] += 1.0f;
            sceVu0ApplyMatrix(&ofs, m, &ofs);
            AddVectorXYZ(&mid, &rear, &ofs);
        }

        if (2.0f < (p->vel[2] < 0.0f ? -p->vel[2] : p->vel[2])) {
            p->stopWall = 1;
        }

        if (hitFront != 0) {
            _SubVectorXYZ(&dir, &front, (char *)GOBJ_SUB(self)->nodeMtx + 0x30);
            debug_StdPrintfDummy("hit with front\n");
        }
        if (hitRear != 0) {
            _SubVectorXYZ(&dir, &rear, (char *)GOBJ_SUB(self)->nodeMtx + 0x30);
            debug_StdPrintfDummy("hit with rear\n");
        }
        _NormalizeVector(&dir, &dir);
        if (0.0f < _InnerProduct(&dir, p->moveDir)) {
            debug_StdPrintfDummy("se stopped\n");
            stopBoxMoveSE(self);
            p->seStopped = 1;
        } else {
            debug_StdPrintfDummy("but different orient, then se not stop\n");
        }

        mid.f[1] = front.f[1];
    } else {
        sceVu0AddVector(&mid, &front, &rear);
        sceVu0ScaleVector(&mid, &mid, 0.5f);
    }

    mid.f[3] = 1.0f;
    SetDirectRootPosition(self, &mid);

    UpdateRootMatrix(self);

    if (debug_skel_flag != 0) {
        gif_StartPacketPri(11);
        gif_SetZWrite(0);
        gif_SetZTest(1);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        for (i = 1; pts[i][3] < 10.0f; i++) {
            DrawLineG(pts[i - 1], routeFrontColor, pts[i], routeFrontColor, -1);
        }
        CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(self)->nodeMtx);
        MatrixDrive_TransMatrix(0.0f, 0.0f, p->scaleZ * 50.0f);
        prim_DispWireSphere(10.0f, routeFrontColor, 4, 4);
        CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(self)->nodeMtx);
        MatrixDrive_TransMatrix(0.0f, 0.0f, p->scaleZ * -50.0f);
        prim_DispWireSphere(10.0f, routeRearColor, 4, 4);
        gif_EndPacket();
    }

    return hitFront | hitRear;
}

/* no caller; float as sugipon's scalar getters */
inline float GetDistanceOfGObj(void *from, void *to)
{
    float v[4];
    float w[4];
    GetRootPosition(v, to);
    GetRootPosition(w, from);
    sceVu0SubVector(v, v, w);
    return FSqrt(sceVu0InnerProduct(v, v));
}

static int playAnimationCore(GObj *self)
{
    StreamElem mot;
    float dir[4];
    float pos[4];
    float q[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    GetFloatingMotion(&mot, GOBJ_SUB(self)->ctrl.animFrame, dir,
                      motionTable[GOBJ_SUB(self)->ctrl.motion], 1, 0, 0);
    dir[3] = 1.0f;
    sceVu0ApplyMatrix(pos, p->mtx[0], dir);
    CopyQuaternion(q, GOBJ_SUB(self)->root.baseQuat);
    MultiQuaternion(q, q, mot.q);
    RotQuaternionX(q, -32768);
    RotQuaternionY(q, -16384);
    MultiQuaternion(q, q, GOBJ_SUB(self)->root.motionQuat);
    SetRootQuaternion(self, q);
    sceVu0SubVector(&GOBJ_SUB(self)->root.move[0], pos, GOBJ_SUB(self)->root.last);
    CopyVector(GOBJ_SUB(self)->root.last, pos);
    SetRootPosition(self, pos);
    ExecFrameDependSequence(self);
    return UpdateFrameCounter(self);
}

/* inlined once, into execFallDown's case 3; the frame-rate divisor is the
   one moveBoxAutoMatic uses */
static inline void execBoxFall(GObj *self) /* derived name */
{
    float v[4];

    GetRootPosition(v, self);
#ifdef ICO_HOST
    GOBJ_SUB(self)->root.move[1] +=
        60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
        (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
#else
    ((IntFloat *)(*(char **)(((char *)self) + 0x15C) + 0x134))->f +=
        60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
        (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
#endif
    AddVectorXYZ(v, v, GOBJ_SUB(self)->root.move);
    SetRootPosition(self, v);
    if (LimitExistGeometry(v, GOBJ_SUB(self)->root.move) != 0) {
        ((BoxWork *)GOBJ_SUB(self)->work)->mode = -1;
    }
}

/* the local Z axis the floating box's facing is rebuilt from */
static float floatFacingAxis[4] = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

/* 0.31830987 is 1 / pi */
int MoveFloatingBox(GObj *self, GObj *other, float *dst, void *src, float lim)
{
    float pos[4];
    float opos[4];
    float tp[4];
    float m[16];
    BoxWork *w = GOBJ_SUB(self)->work;
    float dx;
    float dz;
    float len;

    GetRootMatrix(m, self);
    _ApplyMatrix(tp, m, src);
    GetRootPosition(pos, self);
    GetRootPosition(opos, other);

    dx = dst[0] - tp[0];
    dz = dst[2] - tp[2];
    len = _Sqrt(dx * dx + dz * dz);

    if (lim < len) {
        float over = len - lim;
        float ox;
        float oz;
        float tx;
        float tz;
        float l1;
        float l2;
        float px;
        float pz;
        float ax;
        float az;
        float d;
        int ang;

        dx = dx * (over / len);
        dz = dz * (over / len);

        ox = tp[0] - pos[0];
        oz = tp[2] - pos[2];

        tx = ox + dx * 0.2f;
        tz = oz + dz * 0.2f;
        l2 = FSqrt(tx * tx + tz * tz);
        l1 = FSqrt(ox * ox + oz * oz);
        px = tx * l1 / l2;
        pz = tz * l1 / l2;

        pos[0] = pos[0] + (tx - px);
        pos[2] = pos[2] + (tz - pz);
        ax = px - ox;
        az = pz - oz;
        d = FSqrt(ax * ax + az * az) * 32768.0f;
        ang = (short)(ox * az - oz * ax < 0.0f ? d / l1 * 0.31830987f : -d / l1 * 0.31830987f);

        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_RotMatrixY(
            GetTableArcTan2(GOBJ_SUB(self)->ctrl.dir[0], GOBJ_SUB(self)->ctrl.dir[2]));
        MatrixDrive_RotMatrixY(ang);
        sceVu0ApplyMatrix(GOBJ_SUB(self)->ctrl.dir, MatrixDrive_GetMatrix(), floatFacingAxis);

        SetRootPosition(self, pos);

        opos[0] = opos[0] - dx * 0.05f;
        opos[2] = opos[2] - dz * 0.05f;
        SetRootPosition(other, opos);
    }

    GetCylinderCollisionWithExceptOwnCollision(self, other, 70.0f, 50.0f, 0.5f, 0.5f, 0);

    w->charHit = 1;
    return 1;
}

/* the eight horizontal push-out directions the floating box is tested along */
static float floatPushDir[8][4] = {
    {0.0f, 0.0f, 1.0f, 1.0f},  {0.0f, 0.0f, -1.0f, 1.0f},  {1.0f, 0.0f, 0.0f, 1.0f},
    {-1.0f, 0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, -1.0f, 1.0f}, {1.0f, 0.0f, -1.0f, 1.0f},
    {1.0f, 0.0f, 1.0f, 1.0f},  {-1.0f, 0.0f, 1.0f, 1.0f},
}; /* derived name */

/* inlined once, into execFloating; the clip work, the matrix and the two
   scratch vectors are the caller's */
static inline void pushOutFloatingBox(ClipWork *cw, float *m, float *sv, float *dv, float *pos,
                                      float *q, float r) /* derived name */
{
    float *dir;
    float len;
    int i;

    memset(cw, 0, sizeof(ClipWork));
    /* the counter is only read by the test; the direction pointer walks
       up */
    for (i = 0, dir = floatPushDir[0]; i < 8; i++, dir += 4) {
        CopyVector(cw->pt[0], pos);
        GetMatrixFromQuaternionPos(m, q, pos);
        _ScaleVectorXYZ(sv, dir, r);
        _ApplyMatrix(cw->pt[1], m, sv);
        ClipWall(cw);
        if (cw->wall.elem != 0) {
            _SubVectorXYZ(dv, cw->pt[2], cw->pt[1]);
            len = VectorLengthSquare(dv);
            if (1.0f < len) {
                _ScaleVector(dv, dv, 1.0f / _Sqrt(len));
            }
            _AddVectorXYZ(pos, pos, dv);
        }
    }
}

/* the same prototype fieldCollision.h gives */

static void avoidCharGObj(GObj *self, GObj *chara)
{
    ClipWork w;
    float pos[4];
    int hit;

    w.radius = (30.0f < GOBJ_SUB(chara)->root.radius) ? GOBJ_SUB(chara)->root.radius : 30.0f;
    GetRootPosition(pos, chara);
    pos[1] += GOBJ_SUB(chara)->root.projHeight + 10.0f;
    CopyVector(&w, pos);
    CopyVector(w.pt[1], pos);
    w.filter.o.obj = self;
    w.filter.o.node = -1;
    w.filter.elem = 0;
    ClipWallE(&w);
    if (w.wall.elem != 0) {
        switch (GOBJ_SUB(chara)->ctrl.rootUpdateMode) {
        case 7:
        case 8:
        case 10:
        case 15:
        case 16:
            hit = GOBJ_SUB(chara)->root.wall.o.obj == self;
            break;
        default:
            hit = 1;
            break;
        }
        if (hit != 0) {
            GetCylinderCollisionWithExceptOwnCollision(self, chara, (w.radius + 50.0f) * 1.414f,
                                                       100.0f, 0.5f, 0.0f, 1);
            UpdateRootMatrix(self);
        }
    }
}

/* the two characters the floating box has to keep clear of, the boy and the
   girl, as sceneManager.c sets them */

/* the world Y axis the box's tilt is measured around */
static float floatTiltAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f}; /* derived name */

static void execFloating(GObj *self)
{
    ClipWork fw;
    float pos[4];
    float d[4];
    float g[4];
    float sub[4];
    float axis[4];
    float ofs[4];
    float acc[4];
    float q[4];
    float rot[4];
    float m[16];
    ClipWork cw;
    float cm[16];
    float sv[4];
    float dv[4];
    int hit;
    BoxWork *w = GOBJ_SUB(self)->work;
    float len;
    float r;

    if (boyGObj != 0) {
        if (w->charHit == 0) {
            GetCylinderCollisionWithExceptOwnCollision(self, boyGObj, 50.0f, 50.0f, 0.0f, 1.0f, 1);
            avoidCharGObj(self, boyGObj);
        }
    }
    if (girlGObj != 0) {
        GetCylinderCollisionWithExceptOwnCollision(self, girlGObj, 70.700005f, 50.0f, 0.0f, 1.0f,
                                                   1);
        avoidCharGObj(self, girlGObj);
    }
    GetRootPosition(pos, self);
    GetLowerPlaneCollision(&fw, pos);
    len = VectorLengthSquare(GOBJ_SUB(self)->root.move);
    if (100.0f < len) {
        _ScaleVectorXYZ(GOBJ_SUB(self)->root.move, GOBJ_SUB(self)->root.move, 3.0f / _Sqrt(len));
    }
    /* the three water-probe heights are additions of a negative offset */
    if (GetWaterReaction(w->waterHeight, &hit, &fw, pos, GOBJ_SUB(self)->root.move, pos[1] + -50.0f,
                         pos[1] + -25.0f, pos[1] + 50.0f, 0.9f,
                         60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * -0.1f *
                             (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1])) *
                             3.0f) != 0) {
        if (fw.pt[2][1] - 50.0f < pos[1]) {
            pos[1] = fw.pt[2][1] - 50.0f;
        }
        _SubVector(d, pos, w->floatAnchor);
        d[1] = 0.0f;
        /* 0.1f * 0.1f, written as the square, not 0.01f */
        if (0.1f * 0.1f < VectorLengthSquare(d)) {
            _SubVector(d, pos, w->floatAnchor);
            w->floatAnchor[0] = pos[0];
            w->floatAnchor[2] = pos[2];
        } else {
            pos[0] = w->floatAnchor[0];
            pos[2] = w->floatAnchor[2];
        }
        memset(g, 0, 0x10);
        g[1] = -35.0f;
        sceVu0ScaleVectorXYZ(acc, w->tilt, -0.01f);
        sceVu0AddVector(w->tiltVel, w->tiltVel, acc);
        sceVu0AddVector(w->tiltVel, w->tiltVel, w->tiltForce);
        sceVu0ScaleVectorXYZ(w->tiltVel, w->tiltVel, 0.95f);
        sceVu0AddVector(w->tilt, w->tilt, w->tiltVel);
        sceVu0OuterProduct(axis, floatTiltAxis, w->tilt);
        CopyQuaternion(q, IdentityQuaternion);
        RotQuaternionY(q,
                       GetTableArcTan2(GOBJ_SUB(self)->ctrl.dir[0], GOBJ_SUB(self)->ctrl.dir[2]));
        SetQuaternionByAxisRotateV(rot, (short)(VectorLength(w->tilt) * 20.48f / 50.0f), axis);
        MultiQuaternion(q, q, rot);
        SetRootQuaternion(self, q);
        GetMatrixFromQuaternion(m, rot);
        sceVu0ApplyMatrix(ofs, m, g);
        ofs[1] = 0.0f;
        sceVu0SubVector(sub, ofs, w->lastOffset);
        sceVu0SubVector(pos, pos, sub);
        CopyVector(w->lastOffset, ofs);
        r = w->scaleX > w->scaleZ ? w->scaleX * 50.0f : w->scaleZ * 50.0f;
        pushOutFloatingBox(&cw, cm, sv, dv, pos, q, r);
        _AddVectorXYZ(cw.pt[0], pos, ofs);
        cw.pt[0][3] = 0.0f;
        _SubVector(GOBJ_SUB(self)->root.move, cw.pt[0], w->lastPos);
        GOBJ_SUB(self)->root.move[3] = 0;
        CopyVector(w->lastPos, cw.pt[0]);
        SetRootPosition(self, pos);
    }
    GOBJ_SUB(self)->root.move[1] += GetTableSin(w->floatPhase) * 0.1f;
    w->floatPhase += 2048;
    w->charHit = 0;
    if (w->floatPhase == 0) {
        CopyVector(cw.pt[0], pos);
        cw.pt[0][1] = w->waterHeight[0];
        EntryStageMultiBgaManager(491, cw.pt[0], IdentityQuaternion);
    }
}

/* the facing a floating box starts with */
static float floatInitFacing[4] = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

static void initFloating(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    GOBJ_SUB(self)->colData = p->effectDObj->colData;
    GOBJ_SUB(self)->colRotate = 1;
    CopyQuaternion(GOBJ_SUB(self)->root.baseQuat, IdentityQuaternion);
    SetRootQuaternion(self, IdentityQuaternion);
    CopyVector(p->lastOffset, ZeroVector);
    CopyVector(p->tilt, ZeroVector);
    CopyVector(p->tiltVel, ZeroVector);
    GetRootPosition(p->lastPos, self);
    CopyVector(GOBJ_SUB(self)->ctrl.dir, floatInitFacing);
    p->floatPhase = 0;
    execFloating(self);
}

/* the range test, the range an integer converted at each compare (a nested
   inline in _checkItemBreak and _checkItemCollision) */
static inline int isNearItem(float *v, int r) /* derived name */
{
    if ((v[0] < 0.0f ? -v[0] : v[0]) < r && (v[1] < 0.0f ? -v[1] : v[1]) < r &&
        (v[2] < 0.0f ? -v[2] : v[2]) < r) {
        return 1;
    }
    return 0;
}

static int _checkItemBreak(void *pos)
{
    float p[4];
    float d[4];
    GObj *o;

    for (o = isysGObjSearchFromObjKindID_begin(19); o != 0;
         o = isysGObjSearchFromObjKindID_next(o)) {
        if (CheckItemDead(o) != 0) {
            continue;
        }
        GetRootPosition(p, o);
        _SubVectorXYZ(d, p, pos);
        if (isNearItem(d, 50) != 0) {
            BreakItemFromOutside(o);
        }
    }
    return 1;
}

static void initLanding(GObj *self)
{
    float pos[4];
    float plane[4];
    float v[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    GetRootPosition(pos, self);
    CopyVector(&GOBJ_SUB(self)->root.move[0], ZeroVector);
    *(int *)&GOBJ_SUB(self)->ctrl.animFrame = 0;
    GOBJ_SUB(self)->ctrl.motion = 1144;
    if (p->wall.elem != 0) {
        float d;

        GetPureVerticalPlane(0, plane, 0, &p->wall, 1);
        d = GetDistanceFromPlane(plane, pos);
        plane[3] = 0.0f;
        sceVu0ScaleVectorXYZ(v, plane, -(d - 50.0f));
        AddVectorXYZ(pos, pos, v);
    }
    _checkItemBreak(pos);
    GetMatrixFromQuaternionPos(p->mtx[0], GOBJ_SUB(self)->root.baseQuat, (char *)pos);
}

/* both are inlined once, into action's case 4, the inner one inside the
   outer one */
static inline void resetBoxRootQuaternion(GObj *self, float *q) /* derived name */
{
    GetInverseQuaternion(q, ICO_RAWP(char *, GOBJ_SUB(self), 0x60, (char *)GOBJ_SUB(self)->quat));
    SetRootQuaternion(self, q);
    GOBJ_SUB(self)->colRotate = 1;
}

static inline void playBoxAnimation(GObj *self, float *q) /* derived name */
{
    if (playAnimationCore(self) != 0) {
        BoxWork *p = GOBJ_SUB(self)->work;

        p->mode = 0;
        resetBoxRootQuaternion(self, q);
    }
}

/* inlined once, into execFallDown */
static inline void attackBoxFallCenter(GObj *self) /* derived name */
{
    float plane[4];
    float pos[4];
    BoxWork *q = GOBJ_SUB(self)->work;

    if (q->wall.elem != 0) {
        GetPureVerticalPlane(0, plane, 0, &q->wall, 1);
        plane[3] = 0.0f;
        GetRootPosition(pos, self);
        pos[1] += 50.0f;
        AttackCenter_WithDir(self, 17, pos, plane, 60.0f);
    }
}

static void execFallDown(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    switch (p->mode) {
    case 2:
        if (playAnimationCore(self) != 0) {
            p->mode = 3;
        }
        break;
    case 3:
        execBoxFall(self);
        break;
    }
    attackBoxFallCenter(self);
    switch (checkFieldContact(self, 50.0f)) {
    case 1:
        initLanding(self);
        p->mode = 4;
        landingSE(self);
        break;
    case 2:
        initFloating(self);
        p->mode = 5;
        landingSE(self);
        break;
    }
}

static void inertiaMove(GObj *self)
{
    float pos[4];
    float tmp[4];
    ClipWork w;
    float base[4];
    BoxWork *p = GOBJ_SUB(self)->work;

    if (onPath(self) != 0) {
        CopyVector(p->vel, ZeroVector);
    }
    GetRootPosition(pos, self);
    sceVu0ScaleVectorXYZ(p->vel, p->vel, p->friction);
    sceVu0ApplyMatrix(tmp, (void *)GOBJ_SUB(self)->nodeMtx, p->vel);
    sceVu0AddVector(pos, pos, tmp);
    SetRootPosition(self, pos);
    checkBoxWallHit(self, &w, base, pos, 1);
    if (w.wall.elem != 0) {
        SetRootPosition(self, pos);
        CopyVector(p->vel, ZeroVector);
    }
}

inline int IsThisBoxTruck(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    return p->route;
}

static void action(GObj *self)
{
    /* the float view carries the up vector */
    Vec4u v;
    BoxWork *p = GOBJ_SUB(self)->work;

    switch (p->mode) {
    case 0:
        if (p->route != 0) {
            inertiaMove(self);
            updateBoxWheelAngle(self);
            memset(&v, 0, 16);
            v.f[2] = 1.0f;
            _ApplyMatrix(GOBJ_SUB(self)->ctrl.dir, (void *)GOBJ_SUB(self)->nodeMtx, &v);
        }
        CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
        break;
    case 1:
    case 6:
        execAutoMove(self);
        CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
        break;
    case 2:
    case 3:
        execFallDown(self);
        break;
    case 4:
        playBoxAnimation(self, v.f);
        CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
        break;
    case 5:
        execFloating(self);
        break;
    case -1:
    default:
        debug_StdPrintfDummy("box die!!!\n");
        CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
        break;
    }
    if (p->mode != 6) {
        if (ICO_RAW(int, p->subGObj, 0x16C, ((GObj *)p->subGObj)->active) != 0) {
            ICO_RAW(int, p->subGObj, 0x16C, ((GObj *)p->subGObj)->active) = 0;
        }
    }
}

inline void GetBoxGlobalHoldPoint(float *out, GObj *self, float *local)
{
    float buf[16];
    GetRootMatrix(buf, self);
    sceVu0ApplyMatrix(out, buf, local);
}

/* The clip work buffer is declared in a block of its own after the
   candidate loop.  The first iteration measures with sugiCommon.h's
   distance_squared_b, the rest with distance_squared. */
int GetBoxHoldPoint(float *out, GObj *self, GObj *chara)
{
    float pos[4];
    float p[4];
    BoxWork *q = GOBJ_SUB(self)->work;
    int best = 0;
    float min = 0.0f;
    float d;
    int i;

    GetRootPosition(pos, chara);
    for (i = 0; i < 4; i++) {
        GetBoxGlobalHoldPoint(p, self, holdPointLocal[i]);
        if (i == 0) {
            min = distance_squared_b(p, pos);
            best = 0;
        } else {
            if ((d = distance_squared(p, pos)) < min) {
                min = d;
                best = i;
            }
        }
    }
    CopyVector(out, holdPointLocal[best]);
    out[0] *= q->scaleX;
    out[2] *= q->scaleZ;
    AddVectorXYZ(out, out, holdPointOffset[best]);
    q->holder = chara;
    CopyVector(q->holdPoint, out);
    {
        ClipWork w;

        memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
        GetBoxGlobalHoldPoint(w.pt[1], self, ZeroPoint);
        GetBoxGlobalHoldPoint(w.pt[0], self, out);
        ClipWall(&w);
        if (w.wall.elem != 0) {
            if (CompareAttribute(GetWallAttribute(&w), 0xB00) ||
                CompareAttribute(GetWallAttribute(&w), 0x400)) {
                return 0;
            }
        }
    }
    return 1;
}

inline int CanHoldBox(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    return p->mode == 0;
}

static inline void setupClipWork(ClipWork *w, GObj *obj, float *dir, float len,
                                 float h) /* derived name */
{
    float t[4];

    _ScaleVector(t, dir, len);
    GetRootPosition(w->pt[0], obj);
    w->pt[0][1] += h;
    _AddVectorXYZ(w->pt[1], w->pt[0], t);
}

static inline int checkBoxStopWall(GObj *obj, float *dir) /* derived name */
{
    ClipWork w;
    int r = 1;

    memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    setupClipWork(&w, obj, dir, 145.0f, 40.0f);
    ClipWallBoxStop(&w);
    if (w.wall.elem != 0) {
        r = 0;
    }
    return r;
}

static inline int checkMoveWall(GObj *obj, float *dir) /* derived name */
{
    ClipWork w;
    int r = 1;

    memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    setupClipWork(&w, obj, dir, 245.0f, 0.0f);
    ClipWall(&w);
    if (w.wall.elem != 0) {
        r = 0;
    }
    return r;
}

static inline int moveXPlus(float *ofs, float dist, float half, float radius)
{
    float w;
    float f0;
    int rv;
    half = half + radius;
    w = ofs[2];
    if (w < 0.0f) {
        if (-w < half)
            goto p4;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
p4:
    w = ofs[1];
    if (w < 0.0f) {
        if (-w < half)
            goto rng;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
rng:
    f0 = dist - half;
    if (!(f0 + radius < ofs[0])) {
        rv = 0;
        goto end;
    }
    if (ofs[0] < dist + half)
        return 1;
    rv = 0;
end:
    return rv;
}

static inline int moveXMinus(float *ofs, float dist, float half, float radius)
{
    float w;
    float f0;
    int rv;
    half = half + radius;
    w = ofs[2];
    if (w < 0.0f) {
        if (-w < half)
            goto p4;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
p4:
    w = ofs[1];
    if (w < 0.0f) {
        if (-w < half)
            goto rng;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
rng:
    f0 = dist - half;
    if (!(f0 + radius < -ofs[0])) {
        rv = 0;
        goto end;
    }
    if (-ofs[0] < dist + half)
        return 1;
    rv = 0;
end:
    return rv;
}

static inline int moveZPlus(float *ofs, float dist, float half, float radius)
{
    float w;
    float f0;
    int rv;
    half = half + radius;
    w = ofs[0];
    if (w < 0.0f) {
        if (-w < half)
            goto p4;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
p4:
    w = ofs[1];
    if (w < 0.0f) {
        if (-w < half)
            goto rng;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
rng:
    f0 = dist - half;
    if (!(f0 + radius < ofs[2])) {
        rv = 0;
        goto end;
    }
    if (ofs[2] < dist + half)
        return 1;
    rv = 0;
end:
    return rv;
}

static inline int moveZMinus(float *ofs, float dist, float half, float radius)
{
    float w;
    float f0;
    int rv;
    half = half + radius;
    w = ofs[0];
    if (w < 0.0f) {
        if (-w < half)
            goto p4;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
p4:
    w = ofs[1];
    if (w < 0.0f) {
        if (-w < half)
            goto rng;
        return 0;
    }
    rv = 0;
    if (!(w < half))
        goto end;
rng:
    f0 = dist - half;
    if (!(f0 + radius < -ofs[2])) {
        rv = 0;
        goto end;
    }
    if (-ofs[2] < dist + half)
        return 1;
    rv = 0;
end:
    return rv;
}

static inline int checkCharGObjs(GObj *obj, GObj *holder, float *dir) /* derived name */
{
    float pos[4];
    float pos2[4];
    float d[4];
    GObj **list;
    int (*move)(float *, float, float, float);
    float w = 50.0f;

    list = GetCharGObjList();
    GetRootPosition(pos, obj);
    if ((dir[0] < 0.0f ? -dir[0] : dir[0]) > (dir[2] < 0.0f ? -dir[2] : dir[2])) {
        if (0.0f <= dir[0]) {
            move = moveXPlus;
        } else {
            move = moveXMinus;
        }
    } else if (0.0f <= dir[2]) {
        move = moveZPlus;
    } else {
        move = moveZMinus;
    }
    while (*list != 0) {
        if (*list != holder) {
            GetRootPosition(pos2, *list);
            _SubVector(d, pos2, pos);
            if (move(d, w + w, w, GOBJ_SUB(*list)->root.radius + 5.0f) != 0) {
                return 0;
            }
        }
        list++;
    }
    return 1;
}

static int _checkItemCollision(void *pos)
{
    float p[4];
    float d[4];
    GObj *o;

    for (o = isysGObjSearchFromObjKindID_begin(19); o != 0;
         o = isysGObjSearchFromObjKindID_next(o)) {
        if (CheckItemDead(o) != 0) {
            continue;
        }
        GetRootPosition(p, o);
        _SubVectorXYZ(d, p, pos);
        if (isNearItem(d, 50) != 0) {
            return 0;
        }
    }
    return 1;
}

static inline int checkItemHit(GObj *obj, float *dir) /* derived name */
{
    float pos[4];
    float d[4];
    float to[4];

    _ScaleVectorXYZ(d, dir, 100.0f);
    GetRootPosition(pos, obj);
    _AddVectorXYZ(to, pos, d);
    return _checkItemCollision(to);
}

static int moveBoxAutoMatic(GObj *self, int dir)
{
    float v[4];
    float v2[4];
    BoxWork *p = GOBJ_SUB(self)->work;
    float t = 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    float w = t * t;
    int r;

    switch (dir) {
    default:
        p->friction = 0.85f;
        AddVectorXYZ(p->vel, p->vel, ZeroVector);
        break;
    case 1:
        memset(v, 0, 16);
        v[2] = w * 0.5f;
        p->friction = 0.98f;
        AddVectorXYZ(p->vel, p->vel, v);
        break;
    case -1:
        memset(v2, 0, 16);
        v2[2] = w * -0.5f;
        p->friction = 0.98f;
        AddVectorXYZ(p->vel, p->vel, v2);
        break;
    }
    if (p->autoDir != dir) {
        StopSEPackageWithGroupVariation(self, 1);
        if (dir != 0) {
            ExecuteSEPackageWithGroupVariation(self, 29, 1);
        }
    }
    p->autoDir = dir;
    if (onPath(self) != 0) {
        CopyVector(p->vel, ZeroVector);
    }
    UpdateRootMatrix(self);
    r = execNormalMove(self, 1);
    UpdateRootMatrix(self);
    return r;
}

int MoveBoxWithHoldPoint(GObj *self, float *holdPoint, GObj *holder, int focus, float *dir)
{
    float plane[4];
    float nv[4];
    float hp[4];
    float pos[4];
    float mv[4];
    BoxWork *q = GOBJ_SUB(self)->work;
    int idx;
    int hit;
    float dot;
    float dist;

    CopyVector(q->moveDir, dir);
    GetBoxGlobalHoldPoint(hp, self, holdPoint);
    GetRootPosition(pos, self);
    sceVu0SubVector(nv, hp, pos);
    sceVu0Normalize(nv, nv);

    dot = sceVu0InnerProduct(nv, hp);
    SetSimplePlane(plane, nv[0], nv[1], nv[2], -dot);

    idx = GetSkeltonFocusNode(holder, focus);
    dist = GetDistanceFromPlane(plane, (char *)GOBJ_SUB(holder)->nodeMtx + (idx << 6) + 0x30);

    sceVu0ScaleVector(mv, nv, dist);

    if (((BoxWork *)GOBJ_SUB(self)->work)->route != 0) {
        float m[16];

        _ScaleVector(mv, mv, 0.05f);
        MatrixDrive_SetTransposeMatrix(m, (void *)GOBJ_SUB(self)->nodeMtx);
        sceVu0ApplyMatrix(mv, m, mv);
        AddVectorXYZ(q->vel, q->vel, mv);
        if (onPath(self) != 0) {
            CopyVector(q->vel, ZeroVector);
        }
        if (stage_no == 8) {
            if (q->serial == 0 && q->seStopped == 0) {
                pushStartSE(self);
            }
        }
        ReviveCarryableItemsWithBoundary(pos, 100.0f);
    } else if (checkCharGObjs(self, holder, dir) && checkBoxStopWall(self, dir) &&
               CheckGeneratorCollision(self, dir) && checkItemHit(self, dir)) {
        q->moveFrames = (60 - systemStatus[0] * 10) / systemStatus[1] *
                        (GetNbMotionFrames(GOBJ_SUB(holder)->ctrl.motion) - 1) / 30;
        _ScaleVectorXYZ(q->vel, dir, 100.0f / (float)q->moveFrames);

        hit = checkMoveWall(self, dir);
        if (hit) {
            q->mode = 1;
        } else {
            float npos[4];
            float d[4];

            GetRootPosition(npos, self);
            _ScaleVector(d, dir, 100.0f);
            _AddVectorXYZ(npos, npos, d);
            q->mode = 6;
            alignPosition(self, npos, npos, 100.0f);
            npos[1] -= 1.0f;
            debug_StdPrintfDummy("near wall to %f, %f, %f\n", npos[0], npos[1], npos[2]);
            CopyVector((char *)ICO_RAW(
                           int,
                           ICO_RAW(int, q->subGObj, 0x15C, (ICO_WORD)((GObj *)q->subGObj)->dobj),
                           0xC, (ICO_WORD)((GObj *)q->subGObj)->dobj->nodeMtx) +
                           0x30,
                       npos);
            ICO_RAW(int, q->subGObj, 0x16C, ((GObj *)q->subGObj)->active) = 1;
        }
        if (GOBJ_SUB(holder)->ctrl.cliffWallHit != 0) {
            q->wall = GOBJ_SUB(holder)->root.cliffWall;
        }
    } else {
        return 0;
    }
    UpdateRootMatrix(self);
    {
        int rv = execNormalMove(self, 1);
        UpdateRootMatrix(self);
        return rv;
    }
}

inline int BoxRideFunc(ObjNode *node, GObj *rider)
{
    GObj *obj = node->obj;
    Sub15C *p15c = GOBJ_SUB(obj);
    BoxWork *s0 = p15c->work;
    char buf[32];
    if (s0->mode != 5) {
        return 0;
    }
    p15c->root.move[1] += 0.5f;
    GetRootPosition(buf + 0x10, obj);
    CopyVector(buf, GOBJ_SUB(rider)->root.pos);
    *(int *)(buf + 4) = 0;
    sceVu0AddVector(s0->tiltVel, s0->tiltVel, buf);
    return 1;
}

inline void ExecBoxMoveStartReaction(GObj *self, int dir)
{
    BoxWork *q = GOBJ_SUB(self)->work;
    if (q->route != 0) {
        if (q->moving != 0) {
            goto end;
        }
    }
    if (dir >= 0) {
        pushStartSE(self);
        q->seStopped = 0;
    } else {
        pullStartSE(self);
        q->seStopped = 0;
    }
end:
    q->moving = 1;
}

inline void ExecBoxMoveEndReaction(GObj *self)
{
    BoxWork *q = GOBJ_SUB(self)->work;
    if (q->route == 0 || q->moving != 0) {
        stopBoxMoveSE(self);
    }
    q->moving = 0;
}

void ReInitBoxGeo(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    debug_StdPrintfDummy("BOXREINIT\n");
    GOBJ_SUB(self)->colData = p->colData;
    if (checkFieldContact(self, 100000.0f) == 0) {
        /* EUC-JP: "the box is placed where there is no ground; its behaviour cannot be guaranteed (is the box before the collision definition?)" */
        debug_StdPrintfDummy(
            "\033[36m箱が地面の無いところに初期配置されています。\n動作が保証できません(コリジョン定義より前に箱がありませんか?)\033[m\n");
    } else {
        int m = GOBJ_SUB(self)->ctrl.floorAttr;

        if (m == 0x40 || m == 0x50) {
            initFloating(self);
            p->mode = 5;
            /* EUC-JP: "box initially placed on the water bottom" */
            debug_StdPrintfDummy("箱初期水底配置\n");
        } else {
            p->mode = 0;
            AlignBox(self, 100.0f);
            execNormalMove(self, 1);
            /* EUC-JP: "box initially placed normally" */
            debug_StdPrintfDummy("箱初期通常配置\n");
        }
    }
    UpdateRootMatrix(self);
}

/* the box serial counter */
static unsigned char boxSerial = 0; /* derived name */

/* the layout record's object word packs the route number in its low half
   and the sub-box model in its high half */
BoxWork *InitBoxGeo(GObj *self, SObjSimpleSetting *lay)
{
    BoxWork *w = iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(BoxWork, 416), __FILE__, 2017);
    GObj *o;
    GObj *g;
    int sub;

    GOBJ_SUB(self)->work = w;

    *w = boxWorkInit;

    w->serial = boxSerial;
    boxSerial = (boxSerial + 1) % 30;

    ((IntFloat *)&w->scaleX)->f = lay->scale[0];
    ((IntFloat *)&w->scaleZ)->f = lay->scale[2];
    ((IntFloat *)((char *)GOBJ_SUB(self)->nodes + 0x20))->f =
        ((IntFloat *)((char *)GOBJ_SUB(self)->nodes + 0x24))->f =
            ((IntFloat *)((char *)GOBJ_SUB(self)->nodes + 0x28))->f = 1.0f;

    w->colData = GOBJ_SUB(self)->colData;

    w->effectDObj = CSVSYSTEM_InitDObj(63, &InitialSObjSimpleSetting);

    w->route = lay->obj & 0xFFFF;
    GOBJ_SUB(self)->rideFunc = BoxRideFunc;

    g = CreateLayoutedGObj(0, 64, -1, 0, lay, 0, 7, 0);
    w->subGObj = (ICO_WORD_PTR(GObj *))g;

    GOBJ_SUB(g)->disp = 1;
    g->active = 0;

    if (w->route != 0) {
        w->pointCount = countPathPoints(w->route);
        w->friction = 0.98f;
        onPathInitialize(self);
        onPath(self);
        initWheels(self, lay);
        execNormalMove(self, 1);
        debug_StdPrintfDummy("%d\n", w->pointCount);

        if ((lay->obj & 0xFFFF0000) != 0) {
            SObjSimpleSetting r = *lay;
            ObjNode lnk = {self, 0};
            Vec4 v;
            Vec4 q;

            r.scale[0] = 1.0f;
            r.scale[1] = 1.0f;
            r.scale[2] = 1.0f;
            r.scale[3] = 1.0f;
            r.obj = 1;

            sub = accessary[GOBJ_SUB(self)->accessary].subModel;
            o = CreateLayoutedGObj(23, accessary[sub].model, sub, 0, &r, 0, 7, 0);

            LinkParentOfDObj(o, &lnk);

            q.f[0] = accessary[GOBJ_SUB(self)->accessary].subPos[0];
            q.f[1] = accessary[GOBJ_SUB(self)->accessary].subPos[1];
            q.f[2] = accessary[GOBJ_SUB(self)->accessary].subPos[2];
            q.f[3] = 1.0f;
            v = q;

            CopyVector(GOBJ_SUB(o)->root.pos, &v);

            memset(&q, 0, 16);
            q.f[3] = 1.0f;
            RotQuaternionY(
                &q, (short)(accessary[GOBJ_SUB(self)->accessary].subRotY * 32768.0f / 180.0f));
            CopyVector(GOBJ_SUB(o)->root.quat, &q);

            SetSwitchTriggerFunc(o, moveBoxAutoMatic);

            w->friction = 0.85f;
        }
        UpdateRootMatrix(self);
        return w;
    }

    w->tiltForce[0] = random_signed() * 50.0f * 0.5f;
    w->tiltForce[1] = random_signed() * 50.0f * 0.5f;
    ReInitBoxGeo(self);

    return w;
}

void BoxGeo(GObj *self)
{
    BoxWork *p = GOBJ_SUB(self)->work;

    action(self);
    UpdateRootMatrix(self);
    if ((*(int *)p)++ >= 0x1F) {
        *(int *)p = 0;
        gamesysObjInfoUniqDataSet(self);
    }
}

inline void BoxDL(GObj *self)
{
    BoxWork *q = GOBJ_SUB(self)->work;
    p2o_SetDefaultEnviroment();
    p2o_DispVU1(self);
    if (q->route != 0) {
        dispWheels(self);
    }
    if (systemStatus[5] != 0) {
        StopSEPackageWithGroupVariation(self, 1);
        ((BoxWork *)GOBJ_SUB(self)->work)->autoDir = 0;
    }
}

inline int BoxGeoRestore(float *dst, float *src)
{
    dst[0] = src[4];
    dst[1] = src[5];
    dst[2] = src[6];
    dst[4] = src[8];
    dst[5] = src[9];
    dst[6] = src[10];
    debug_StdPrintfDummy("%f, %f, %f\n", dst[8], dst[9], dst[10]);
    return 1;
}

inline int BoxExtGeoRestore(void)
{
    return 1;
}

inline int BoxMemoryFunc(void)
{
    return 1;
}

int GetBoxMode(GObj *self)
{
    BoxWork *q = GOBJ_SUB(self)->work;

    return q->mode;
}
