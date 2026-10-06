#include "typedef.h"
#include "sugiCommon.h"
#include "main.h"
#include "debug_exception.h"
#include "obj_manager.h"
#include "Primitive.h"
#include "lineManager.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "geometryManager.h"
#include "debug.h"
#include "pool.h"
#include "weapon.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "tableSin.h"
#include "Matrix.h"
#include "gv.h"
#include "fieldCollision.h"
#include "motionManager.h"
#include "DisplayP2O.h"
#include "GifPacket.h"
#include <libvu0.h>
#include <assert.h>

/* the motion being computed: its IK slerp rate, motions, quaternions, node
   positions, root and motion control, and the natural-geometry buffers */
static float ikSlerpRate; /* derived name */

static char *skelMotion; /* derived name */

static char *skelMotion2; /* derived name */

static char *skelQuat; /* derived name */

static char *nodePos; /* derived name */

static char *nodePos2; /* derived name */

static int skelNodeNum; /* derived name */

static struct MotRoot *skelRoot; /* derived name */

static struct MotCtrl *skelMotCtrl; /* derived name */

static int skelGeoType; /* derived name */

static int stepFocusNode; /* derived name */

static char *naturalNodePos; /* derived name */

static char *naturalMotion; /* derived name */

/* the current motion's motionKind record, read through a byte pointer */
static const MotionDef *skelMotDef; /* derived name */

/* in the order of each object's first user */
static float squareP0[4] = {-3.0f, 0.0f, -3.0f, 0.0f}; /* derived name */

static float squareP1[4] = {3.0f, 0.0f, 3.0f, 0.0f}; /* derived name */

static float squareP2[4] = {-3.0f, 0.0f, 3.0f, 0.0f}; /* derived name */

static float squareP3[4] = {3.0f, 0.0f, -3.0f, 0.0f}; /* derived name */

static sceVu0IVECTOR square2Color = {0xFF, 0x80, 0x00, 0x80}; /* derived name */

static float square2P0[4] = {-4.0f, 0.0f, -4.0f, 0.0f}; /* derived name */

static float square2P1[4] = {4.0f, 0.0f, 4.0f, 0.0f}; /* derived name */

static float square2P2[4] = {-4.0f, 0.0f, 4.0f, 0.0f}; /* derived name */

static float square2P3[4] = {4.0f, 0.0f, -4.0f, 0.0f}; /* derived name */

static float twistQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float twistQuatLow[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float twistQuatHigh[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float wallCheckBase[4] = {0.0f, -40.0f, 0.0f, 1.0f}; /* derived name */

static float wallCheckAhead[4] = {0.0f, 0.0f, 300.0f, 1.0f}; /* derived name */

static float cliffCheckBase[4] = {0.0f, 10.0f, 0.0f, 1.0f}; /* derived name */

static float cliffCheckAhead[4] = {0.0f, 0.0f, 300.0f, 1.0f}; /* derived name */

static sceVu0IVECTOR wallHitColor = {0xFF, 0x80, 0x00, 0x80}; /* derived name */

static sceVu0IVECTOR wallHitColor2 = {0x00, 0x80, 0xFF, 0x80}; /* derived name */

static float floorSlope[4] = {0.0f, 0.0f, 0.0f, 0.0f}; /* derived name */

static float sideWallProbe[4] = {0.0f, 0.0f, 10.0f, 0.0f}; /* derived name */

static float hangBack[4] = {0.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static float hangFront[4] = {0.0f, 0.0f, 50.0f, 1.0f}; /* derived name */

static float hangLeft[4] = {-30.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float hangRight[4] = {30.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float clipWallBack[4] = {0.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static float clipWallFront[4] = {0.0f, 0.0f, 50.0f, 1.0f}; /* derived name */

static float avoidLeft[4] = {-40.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float avoidRight[4] = {40.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static int footActPoints[] = {51, 47, 52, 48, -1}; /* derived name */

static int ropeActPoints[] = {51, 47, 52, 48, 22, 6, 11, 27, -1}; /* derived name */

static sceVu0IVECTOR reserverColor = {0x40, 0x60, 0x80, 0x80}; /* derived name */

static sceVu0IVECTOR reserverColor2 = {0xFF, 0x60, 0x40, 0x80}; /* derived name */

static float rootHeightVec[4] = {0.0f, 1.0f, 0.0f, 0.0f}; /* derived name */

static float scaleMatrix[16] = {
    /* derived name */
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
}; /* derived name */

static sceVu0IVECTOR dirColor = {0xFF, 0x60, 0x40, 0x80}; /* derived name */

static sceVu0IVECTOR dirColor2 = {0x00, 0x60, 0xFF, 0x80}; /* derived name */

static float ikXAxis[4]; /* derived name */

static float ikBendQuat[4]; /* derived name */

static float naturalMatrix[16]; /* derived name */

static float naturalTrans[4]; /* derived name */

static float rootMove[4]; /* derived name */

static float rootStep[4]; /* derived name */

static float rootDelta[4]; /* derived name */

/* The skeleton the motion being computed belongs to: its node array and its
   object.  Declared here for the functions above; the definitions follow
   motMan_rootUpdate.c.inc's ObjNode template. */
static SkelNode *skelNode; /* derived name */

/* the object being skeletonised, held as a word: GetMatrixOfMotion's store
   of it keeps its place among the display-object reads only as an int store
   (a GObj pointer lets two of them pass it), so its uses convert it */
static ICO_WORD skelGObj; /* derived name */

/* the collision display switches SetHitCollisionDisplay sets (the second
   one draws the wall and cliff rays) and the skeleton's scale */
static int hitColDisp = 0; /* derived name */

static int hitColRayDisp = 0; /* derived name */

/* GetGeometryOfMotion and GetMatrixOfMotion write it */
static float skelScale = 1.0f; /* derived name */

static ObjNode rootUpdateDirectPlayForStream(void);
static ObjNode rootUpdateXZ(int kind, int no);
static ObjNode rootUpdateXZ_MotPos(int kind, int no);
static ObjNode rootUpdateStepSolution(int kind);
static ObjNode rootUpdateHang(int kind, int hand1Pt, int hand0Pt);
static ObjNode rootUpdateSwim(void);
static ObjNode rootUpdateNodeFix(void);
static ObjNode rootUpdateY(void);
static ObjNode rootUpdateY_Rope(int actPt);
static ObjNode rootUpdateTrueMotion(int kind);
static ObjNode rootUpdateDirectPlay(int mode);
static ObjNode rootUpdateFly(void);
static ObjNode rootUpdateEnemyFly(void);
extern unsigned char objLayout[];

typedef struct { /* field names derived */
    int obj;
    int node;
} ActPt; /* derived name */

static void getInitialMatrix(Sub15C *obj, int idx);

#include <string.h>
#include <math.h>
#include <stdio.h>

static inline void dispSquare(int alpha) /* derived name */
{
    int col[4] = {0, 128 * alpha / 255, alpha, 128};
    DrawLineG(squareP0, col, squareP2, col, -1);
    DrawLineG(squareP2, col, squareP1, col, -1);
    DrawLineG(squareP1, col, squareP3, col, -1);
    DrawLineG(squareP3, col, squareP0, col, -1);
}

static void dispSquare2(int alpha)
{
    DrawLineG(square2P0, square2Color, square2P2, square2Color, -1);
    DrawLineG(square2P2, square2Color, square2P1, square2Color, -1);
    DrawLineG(square2P1, square2Color, square2P3, square2Color, -1);
    DrawLineG(square2P3, square2Color, square2P0, square2Color, -1);
}

#include "motMan_getFinalMatrix.c.inc"

inline void SetHitCollisionDisplay(int a, int b)
{
    hitColDisp = a;
    hitColRayDisp = b;
}

static inline int findActPointOrder(int *list, int kind) /* derived name */
{
    int *p = list;
    int i = 1;
    while (*p != -1) {
        if (*p++ == kind) {
            return i;
        }
        i++;
    }
    return 0;
}

static int findActPoint(int *list)
{
    int bestOrder = 255;
    int minVal = 249;
    int ret = -1;
    int i;

    if (skelRoot->noStepSearch != 0) {
        return -1;
    }
    for (i = 0; i < skelNodeNum; i++) {
        int order = findActPointOrder(list, skelNode[i].kind);
        int v;
        if (order == 0) {
            continue;
        }
        if (skelRoot->hand1.ikMode != 0) {
            int k = skelNode[i].kind;
            if (k == 6 || k == 11) {
                continue;
            }
        }
        if (skelRoot->hand0.ikMode != 0) {
            int k = skelNode[i].kind;
            if (k == 22 || k == 27) {
                continue;
            }
        }
        v = *(int *)(skelMotion + i * 32);
        if (v < minVal || (v == minVal && order < bestOrder)) {
            ret = i;
            minVal = v;
            bestOrder = order;
        }
    }
    return ret;
}

static int checkActPointWithHeight(int kind, float h)
{
    int i;

    if (skelRoot->hand1.ikMode != 0) {
        if (kind == 6 || kind == 11) {
            return -1;
        }
    }
    if (skelRoot->hand0.ikMode != 0) {
        if (kind == 22 || kind == 27) {
            return -1;
        }
    }
    for (i = 0; i < skelNodeNum; i++) {
        if (skelNode[i].kind == kind) {
            float d;
            if (*(int *)(skelMotion + i * 32) >= 249) {
                return -1;
            }
            d = *(float *)(nodePos + i * 16 + 4) + rootMove[1];
            if ((d < 0.0f ? -d : d) < h) {
                return kind;
            }
            return -1;
        }
    }
    return -1;
}

inline void GetWallVector(float *v, ClipWork *w)
{
    CopyVector(v, &w->normal);
    v[3] = 0.0f;
}

/* inlined here and into _checkCliffAndWall; the float limit is a
   literal */
static inline void clearCliffStatus(void) /* derived name */
{
    skelMotCtrl->flags &= ~0x10;
    skelMotCtrl->fieldWallHit = 0;
    skelMotCtrl->cliffEdge = 0;
    skelMotCtrl->cliffWallHit = 0;
    skelMotCtrl->cliffBack = 0;
    skelMotCtrl->cliffHeight = 3.40282347e+38f;
    skelMotCtrl->cliffDist = 3.40282347e+38f;
}

static void clearCollisionStatus(void)
{
    clearCliffStatus();

    skelMotCtrl->flags &= ~0x20;
    skelMotCtrl->wallHit = 0;
    skelMotCtrl->wallDist = 3.40282347e+38f;
    skelMotCtrl->wallFloorHeight = 3.40282347e+38f;
    skelMotCtrl->wallTopHeight = 3.40282347e+38f;
    skelRoot->cliffFloor = 0;

    skelMotCtrl->sideWall = 0;
    skelMotCtrl->sideWallDist = 3.40282347e+38f;
    skelMotCtrl->cliffDepth = 0.0f;

    skelMotCtrl->flags &= ~0x1000;
    skelMotCtrl->upperWall = 0;
    skelMotCtrl->upperWallDist = 3.40282347e+38f;

    skelMotCtrl->lastSlipFlags = skelMotCtrl->slipFlags;
    skelMotCtrl->slipFlags = 0;
    skelMotCtrl->waterDepth = 0.0f;

    skelMotCtrl->landed = 0;
}

static void checkUpperWallState(void)
{
    ClipWork buf;
    memset(&buf, 0, sizeof(buf));
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(wallCheckBase);
    CopyVector(&buf, (void *)(MatrixDrive_GetMatrix()[3]));
    sceVu0ApplyMatrix(buf.pt[1], MatrixDrive_GetMatrix(), wallCheckAhead);
    MatrixDrive_PopMatrix();
    ClipWall(&buf);
    if (buf.wall.elem != 0) {
        float a;
        struct MotCtrl *D;
        a = GetPointDistance(buf.pt[2], &buf);
        D = skelMotCtrl;
        D->upperWallDist = a;
        D->upperWall = 1;
        D->flags = D->flags | 0x1000;
    }
}

static void checkWallSideState(void)
{
    ClipWork buf = {{0}, {0}, 50.0f};
    float v[4];
    ClipWork *p = &buf;

    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(wallCheckBase);
    CopyVector((void *)p, (void *)(MatrixDrive_GetMatrix()[3]));
    sceVu0ApplyMatrix(p->pt[1], MatrixDrive_GetMatrix(), wallCheckAhead);
    MatrixDrive_PopMatrix();

    if (skelRoot->fieldWall != 0) {
        ClipWallField(p);
    } else {
        ClipWall(p);
    }
    if (hitColRayDisp != 0) {
        DrawCollisionRay(p);
    }
    if (p->wall.elem != 0) {
        SubVectorXYZ(v, p->pt[2], p);
        skelMotCtrl->sideWallDist = FSqrt(sceVu0InnerProduct(v, v)) + 50.0f;
        skelMotCtrl->sideWall = 1;
        CopyVector((void *)skelMotCtrl->sideWallNormal, (void *)&p->normal);
    }
}

static void checkWallState(int flag)
{
    ClipWork buf;
    ClipWork *p;
    float wv[4];
    ClipWork tmp;
    float sv[4];
    WallCfg cfg;
    WallCfg cfg2;

    memset(&buf, 0, sizeof(buf));
    p = &buf;
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(wallCheckBase);
    CopyVector((void *)p, (void *)(MatrixDrive_GetMatrix()[3]));
    sceVu0ApplyMatrix(p->pt[1], MatrixDrive_GetMatrix(), wallCheckAhead);
    MatrixDrive_PopMatrix();

    if (skelRoot->fieldWall != 0) {
        ClipWallField(p);
    } else {
        ClipWall(p);
    }
    if (hitColRayDisp != 0) {
        DrawCollisionRay(p);
    }
    if (p->wall.elem != 0) {
        tmp = *p;
        GetWallVector(wv, p);
        sceVu0ScaleVector(sv, wv, -200.0f);
        AddVectorXYZ(p->pt[1], p, sv);
        if (skelRoot->fieldWall != 0) {
            ClipWallField(p);
        } else {
            ClipWall(p);
        }
        if (p->wall.elem != 0) {
            if (p->wall.elem != tmp.wall.elem || p->wall.o.obj != tmp.wall.o.obj ||
                p->wall.o.node != tmp.wall.o.node) {
                if (distance_squared(p, p->pt[2]) > distance_squared(&tmp, tmp.pt[2])) {
                    *p = tmp;
                }
            }
            if (hitColRayDisp != 0) {
                DrawCollisionRay(p);
            }
            /* one statement: the record is filled a member at a time, then
               the whole twelve bytes are copied out as one unit */
            cfg = (cfg2.o = p->wall.o, cfg2.elem = p->wall.elem, cfg2);
            if (flag & 1) {
                float v[4];

                SubVectorXYZ(v, p->pt[2], p);
                skelMotCtrl->wallDist = FSqrt(sceVu0InnerProduct(v, v));
                /* PC port: a ray that hits the wall at its own start point
                   gives wallDist 0; the EE's div.s makes 1 / 0 +Fmax, IEEE
                   makes Inf */
                sceVu0ScaleVector(skelMotCtrl->wallDir, v, ps2_div(1.0f, skelMotCtrl->wallDist));
                skelMotCtrl->flags = skelMotCtrl->flags | 0x20;
                skelMotCtrl->pureWallAttr = skelMotCtrl->wallAttr = GetWallAttribute(p);
                /* The hit count reaches GetOrientOfWall as a pointer-typed
                   load, which is what lets it issue ahead of the two int
                   stores above it. */
                GetOrientOfWall(skelMotCtrl->wallNormal, p->wall.elem, &p->wall.o);
                skelRoot->wall = cfg;
                skelRoot->wallCount = -1;
                if (skelRoot->fieldWall != 0 && skelMotCtrl->wallAttr == 0x10000) {
                    skelMotCtrl->fieldWallHit = 1;
                } else {
                    skelMotCtrl->wallHit = 1;
                }
            }
            if (flag & 2) {
                float v2[4];
                float plane[4];

                GetPureVerticalPlane(plane, 0, 0, &cfg, 0);
                skelMotCtrl->wallTopHeight = -GetYDistanceFromPlane(plane, p->pt[0]) + -40.0f;
                sceVu0ScaleVector(v2, wv, -10.0f);
                AddVectorXYZ(p, p->pt[2], v2);
                CopyVector((void *)p->pt[1], (void *)p);
                p->pt[1][1] = p->pt[1][1] - 10000.0f;
                ClipFloorR(p);
                if (p->floor.elem != 0) {
                    skelMotCtrl->wallFloorHeight = (p->pt[2][1] - p->pt[0][1]) + -40.0f;
                    SetSimplePlane(skelRoot->cliffPlane, 0.0f, -1.0f, 0.0f, p->pt[2][1]);
                    skelRoot->cliffFloor = p->floor.elem;
                }
            }
        }
    }
}

static void checkCliffState(int first)
{
    ClipWork buf;
    float mv[4];
    ClipWork *p;
    float k;

    memset(&buf, 0, sizeof(buf));
    p = &buf;
    k = ((GObj *)skelGObj == boyGObj) ? -20.0f : 0.0f;
    cliffCheckBase[2] = k;
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(cliffCheckBase);
    CopyVector(p, (void *)(MatrixDrive_GetMatrix()[3]));
    sceVu0ApplyMatrix(p->pt[1], MatrixDrive_GetMatrix(), cliffCheckAhead);
    _ApplyMatrix(mv, MatrixDrive_GetMatrix(), ZUnitVector);
    MatrixDrive_PopMatrix();
    if (hitColRayDisp != 0) {
        DrawCollisionRay(p);
    }
    ClipWallR(p);
    if (p->wall.elem != 0) {
        float wv[4];
        float sc[4];
        float hit[4];
        ClipWork fp;
        float plane[4];
        WallCfg pl;
        WallCfg t;
        float d;
        float dd;

        GetWallVector(wv, p);
        sceVu0ScaleVector(sc, wv, 300.0f);
        AddVectorXYZ(p->pt[1], p, sc);
        ClipWallR(p);
        if (hitColRayDisp != 0) {
            DrawCollisionRay(p);
        }
        if (p->wall.elem != 0) {
            sceVu0ScaleVector(sc, wv, 10.0f);
            sceVu0AddVector(fp.pt[1], p->pt[2], sc);
            CopyVector(&fp, fp.pt[1]);
            pl = (t.o = p->wall.o, t.elem = p->wall.elem, t);
            GetPureVerticalPlane(plane, 0, 0, &pl, 0);
            d = GetDistanceFromPlane(plane, p->pt[2]);
            fp.pt[0][1] += d - 10.0f;
            ClipFloor(&fp);
            if (hitColRayDisp != 0) {
                DrawCollisionRay(&fp);
            }
            CopyVector(hit, p->pt[2]);
            if (fp.floor.elem == 0) {
                float dv[4];
                float nv[4];
                ClipWork w2;
                float ip;

                CopyVector(dv, wv);
                sc[1] = 0.0f;
                _NormalizeVector(nv, dv);
                ip = _InnerProduct(nv, mv);
                skelMotCtrl->cliffDist = GetPointDistance(p->pt[2], p) + k * ip;
                skelMotCtrl->cliffWallHit = 1;
                /* the wall-hit word is copied as the pointer it is, as
                   checkWallState reads it */
                skelRoot->cliffWall.elem = p->wall.elem;
                skelRoot->cliffWall.o = p->wall.o;
                skelRoot->cliffWallCount = -1;
                w2 = *p;
                d = GetPointDistance(p->pt[2], p);
                _ScaleVector(sc, wv, d + 10.0f);
                _AddVectorXYZ((w2.pt[1]), &w2, sc);
                w2.pt[0][1] = w2.pt[0][1] - 30.0f;
                w2.pt[1][1] = w2.pt[1][1] - 30.0f;
                ClipWall(&w2);
                if (hitColRayDisp != 0) {
                    DrawCollisionRay(&w2);
                }
                if (w2.wall.elem == 0) {
                    skelMotCtrl->cliffEdge = 1;
                    skelMotCtrl->flags |= 0x10;
                }
            }
            skelMotCtrl->pureCliffAttr = skelMotCtrl->wallAttr = GetWallAttribute(p);
            GetOrientOfWall(skelMotCtrl->cliffNormal, p->wall.elem, &p->wall.o);
            if (skelMotCtrl->cliffDist < 30.0f) {
                sceVu0ScaleVector(sc, wv, 10.0f);
                SubVectorXYZ(p, hit, sc);
                sceVu0ScaleVector(sc, wv, 300.0f);
                AddVectorXYZ(p->pt[1], p, sc);
                ClipWall(p);
                if (hitColRayDisp != 0) {
                    DrawCollisionRay(p);
                }
                if (p->wall.elem != 0) {
                    dd = distance_squared(p->pt[2], p);
                    skelMotCtrl->cliffBack = 1;
                    d = FSqrt(dd);
                    if (first == 0) {
                        if (d < skelMotCtrl->wallDist) {
                            skelMotCtrl->wallDist = d;
                        }
                    } else {
                        skelMotCtrl->wallDist = d;
                    }
                    sceVu0ScaleVector(sc, wv, 10.0f);
                    AddVectorXYZ(p, p->pt[2], sc);
                    CopyVector(p->pt[1], p);
                    p->pt[1][1] = p->pt[1][1] - 10000.0f;
                    ClipFloorR(p);
                    if (hitColRayDisp != 0) {
                        DrawCollisionRay(p);
                    }
                    if (p->floor.elem != 0) {
                        skelMotCtrl->wallFloorHeight = (p->pt[2][1] - p->pt[0][1]) + 10.0f;
                    }
                }
            }
            sceVu0ScaleVector(sc, wv, 10.0f);
            AddVectorXYZ(p, hit, sc);
            CopyVector(p->pt[1], p);
            p->pt[1][1] = p->pt[1][1] + 10000.0f;
            ClipFloorIH(p);
            if (hitColRayDisp != 0) {
                DrawCollisionRay(p);
            }
            if (p->floor.elem != 0) {
                skelMotCtrl->cliffHeight = (p->pt[2][1] - p->pt[0][1]) + 10.0f;
            }
        }
    }
}

static void _checkCliffAndWall(void)
{
    float v[4];
    float d;
    float t;

    if (skelMotCtrl->cliffWallCheck == 1 ||
        (skelMotCtrl->cliffWallCheck == 2 && skelMotCtrl->variation == 0)) {
        MatrixDrive_PushMatrix();
        checkCliffState(1);
        MatrixDrive_PopMatrix();
    }
    if (skelMotCtrl->cliffWallCheck == 1 ||
        (skelMotCtrl->cliffWallCheck == 2 && skelMotCtrl->variation == 1)) {
        MatrixDrive_PushMatrix();
        checkWallState(3);
        MatrixDrive_PopMatrix();
    }
    if (skelMotCtrl->cliffWallCheck == 1) {
        if ((skelMotCtrl->flags & 0x20) == 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(0.0f, -30.0f, 0.0f);
            checkWallState(3);
            MatrixDrive_PopMatrix();

            if ((skelMotCtrl->flags & 0x20) != 0) {
                skelMotCtrl->wallFloorHeight += 30.0f;
                skelMotCtrl->wallTopHeight += 30.0f;
            }
        }
        if ((GObj *)skelGObj == boyGObj && GOBJ_SUB(skelGObj)->ctrl.cliffEdge == 0) {
            _SubVectorXYZ(v, skelRoot->pos, skelRoot->last);
            v[1] = 0.0f;
            d = VectorLengthSquare(v);
            if (0.01f < d) {
                MatrixDrive_PushMatrix();
                UnitRotation(MatrixDrive_GetMatrix());
                _ScaleVectorXYZ(v, v, 1.0f / _Sqrt(d));
                MatrixDrive_GetMatrix()[0][0] = MatrixDrive_GetMatrix()[2][2] = v[2];
                MatrixDrive_GetMatrix()[0][2] = -v[0];
                MatrixDrive_GetMatrix()[2][0] = -MatrixDrive_GetMatrix()[0][2];
                checkCliffState(0);
                MatrixDrive_PopMatrix();
            }
        }
        if (skelMotCtrl->wallHit != 0 && skelMotCtrl->cliffEdge != 0) {
            t = skelMotCtrl->wallDist - skelMotCtrl->cliffDist;
            if ((t < 0.0f ? -t : t) < 10.0f) {
                clearCliffStatus();
            }
        }
    }
    if (hitColRayDisp != 0) {
        if (skelMotCtrl->wallHit != 0) {
            DrawGObjWallCollision(skelRoot->wall.o.obj, 0);
        }
    }
    if (GOBJ_SUB(skelGObj)->ctrl.wallHit != 0 && GOBJ_SUB(skelGObj)->root.wall.elem == 0) {
        debug_assertMessage(__FILE__, 822, "NOT ENTRY WCL\n");
        __assert(__FILE__, 822, "e");
    }
}

static void checkCliffAndWallStateOfLastPlane(void)
{
    _UnitMatrix(MatrixDrive_GetMatrix());
    {
        register float *p = skelRoot->pos;
        float r = GetYProjectionOfPlane(skelRoot->plane.f, skelRoot->pos);
        MatrixDrive_TransMatrix(p[0], r, skelRoot->pos[2]);
    }
    MultiMatrixByQuaternion(skelRoot->quat);
    MatrixDrive_PushMatrix();
    _checkCliffAndWall();
    MatrixDrive_PopMatrix();
    if (skelMotCtrl->sideWallCheck != 0) {
        MatrixDrive_PushMatrix();
        checkWallSideState();
        MatrixDrive_PopMatrix();
    }
}

static void checkCliffAndWallStateAtJump(void)
{
    _UnitMatrix(MatrixDrive_GetMatrix());
    {
        register float *p = skelRoot->pos;
        MatrixDrive_TransMatrix(p[0], p[1] + skelRoot->projHeight + 10.0f, p[2]);
    }
    MultiMatrixByQuaternion(skelRoot->quat);
    _checkCliffAndWall();
}

static void dispActNode(int id)
{
    if (id == -1) {
        return;
    }
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 128);
    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    CopyVector((void *)(MatrixDrive_GetMatrix()[3]),
               (void *)((char *)GOBJ_SUB(skelGObj)->nodeMtx + id * 64 + 48));
    MatrixDrive_ScaleMatrix(5.0f, 5.0f, 5.0f);
    dispSquare(255);
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

static void dispLastNode(void)
{
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 128);
    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrix(skelRoot->footPos[0], skelRoot->footPos[1], skelRoot->footPos[2]);
    MatrixDrive_ScaleMatrix(8.0f, 8.0f, 8.0f);
    dispSquare2(255);
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

#include "motMan_rootUpdate.c.inc"
#include "DObj.h"

static SkelNode *skelNode = 0; /* derived name */

static ICO_WORD skelGObj = 0; /* derived name */

static inline void calcMaxNodeHeight(int n) /* derived name */
{
    int i;
    skelRoot->projHeight = 0.0f;
    for (i = 0; i < n; i++) {
        if (skelRoot->projHeight < *(float *)(nodePos2 + i * 16 + 4)) {
            skelRoot->projHeight = *(float *)(nodePos2 + i * 16 + 4);
        }
    }
}

static void _getGeometryOfMotion(ObjNode *out, int second)
{
    float v[4];
    char q[16];
    int save180 = skelRoot->standNode;

    MatrixDrive_PushMatrix();
    PushQuaternion();
    CopyVector((void *)v, (void *)skelMotCtrl->dir);
    SetIdentityQuaternion(q);
    sceVu0Normalize((void *)v, (void *)v);
    RotQuaternionY(q, atan2f(v[0], v[2]) * 10430.378f);
    CopyQuaternion(skelRoot->quat, q);
    GetMatrixFromQuaternion((void *)MatrixDrive_GetMatrix(), skelRoot->quat);
    SetCurrentQuaternion(skelRoot->quat);

    naturalMotion = skelMotion;
    naturalNodePos = nodePos;
    pursueNaturalGeometry(0);

    if (second) {
        naturalMotion = skelMotion2;
        naturalNodePos = nodePos2;
        pursueNaturalGeometry(0);
    } else {
        int i;
        for (i = 0; i < skelNodeNum; i++) {
            CopyVector((void *)(nodePos2 + i * 16), (void *)(nodePos + i * 16));
        }
    }
    calcMaxNodeHeight(skelNodeNum);

    clearCollisionStatus();
    {
        skelRoot->lifting = 0;
        skelRoot->lift[0] = 0.0f;
        skelRoot->lift[1] = 0.0f;
        CopyVector(skelRoot->up, skelRoot->pos);
    }

    if (skelMotCtrl->stream != -1) {
        *out = rootUpdateDirectPlayForStream();
    } else {
        struct MotCtrl *pm;
        MatrixDrive_PushMatrix();
        pm = skelMotCtrl;
        switch (pm->rootUpdateMode) {
        default: {
            char buf[1024];
            sprintf(
                buf,
                "MAY BE MOTION ORIENT DATA WAS BROKEN\n(MOTIONNAME:\"%s\" ID:%d: rootUpdateMode:%d)\n",
                skelMotDef->name, pm->motion, pm->rootUpdateMode);
            debug_assertMessage(__FILE__, 997, buf);
            __assert(__FILE__, 997, "e");
        } break;
        case 1:
        case 20:
            *out = rootUpdateXZ(pm->rootUpdateMode, findActPoint(footActPoints));
            break;
        case 2:
        case 17:
            *out = rootUpdateXZ_MotPos(pm->rootUpdateMode, findActPoint(footActPoints));
            break;
        case 7:
        case 8:
        case 9:
        case 13:
        case 16:
            *out = rootUpdateStepSolution(pm->rootUpdateMode);
            break;
        case 10:
        case 15:
            *out = rootUpdateHang(pm->rootUpdateMode, checkActPointWithHeight(6, 10.0f),
                                  checkActPointWithHeight(22, 10.0f));
            break;
        case 11:
            *out = rootUpdateSwim();
            break;
        case 5:
            *out = rootUpdateNodeFix();
            break;
        case 4:
            rootUpdateY_Rope(findActPoint(ropeActPoints));
            break;
        case 3:
            *out = rootUpdateY();
            break;
        case 0:
        case 19:
            *out = rootUpdateTrueMotion(pm->rootUpdateMode);
            break;
        case 6:
        case 14:
            *out = rootUpdateDirectPlay(pm->rootUpdateMode);
            break;
        case 12:
            *out = rootUpdateFly();
            break;
        case 18:
            *out = rootUpdateEnemyFly();
            break;
        }
        MatrixDrive_PopMatrix();
    }

    skelMotCtrl->groundHeight = skelRoot->pos[1] + skelNode->pos[1] - skelRoot->footPos[1];

    MatrixDrive_PushMatrix();
    MatrixDrive_SetTransposeMatrix((void *)MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix());
    sceVu0ApplyMatrix(skelRoot->stepMove, MatrixDrive_GetMatrix(), skelRoot->move);
    MatrixDrive_PopMatrix();

    MatrixDrive_PopMatrix();
    PopQuaternion();

    if (skelRoot->standNode != -1 && save180 == -1) {
        skelMotCtrl->landed = 1;
    }
}

inline void getGeometryOfMotion(ObjNode *out, int second)
{
    MotOriReq buf;
    Sub15C *p;
    buf = *(MotOriReq *)&GOBJ_SUB(skelGObj)->root.wall;
    _getGeometryOfMotion(out, second);
    p = ((GObj *)skelGObj)->dobj;
    if (p->ctrl.keepWall != 0) {
        *(MotOriReq *)&p->root.wall = buf;
    }
}

static void execPositionReserver(GObj *self, ObjNode m)
{
    Sub15C *ext;
    float buf[4];
    float buf2[4];
    int flg;

    ext = GOBJ_SUB(self);
    if (ext->ctrl.loopFlag == 1) {
        if (ext->ctrl.posReserve == 0 || ext->ctrl.posReserve != ext->ctrl.loopFlag) {
            if (m.obj != 0) {
                if (!(skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0xF00000)) &&
                    !(*(long long *)&skelMotCtrl->ctrlFlags & ((long long)0x8008 << 30))) {
                    ext->ctrl.posReserve = ext->ctrl.loopFlag;
                    CopyVector(skelRoot->savePos, skelRoot->pos);
                    skelRoot->hitObj = m;
                    skelMotCtrl->reserveBlend = 0;
                }
            }
        } else {
            skelMotCtrl->reserveBlend = 2;
            CopyVector(skelRoot->pos, skelRoot->savePos);
        }
    }
    skelMotCtrl->reserveMoved = 1;
    if (GOBJ_SUB(self)->ctrl.posReserve == 1) {
        if (m.obj != 0) {
            CopyVector(buf, skelRoot->savePos);
            _ApplyMatrix(buf, ((char *)GOBJ_SUB(m.obj)->nodeMtx + m.node * 64), buf);
            if (distance_squared(buf, skelRoot->reservePos) == 0.0f) {
                skelMotCtrl->reserveMoved = 0;
            }
            buf[3] = 1.0f;
            CopyVector(skelRoot->reservePos, buf);
        }
        flg = skelMotCtrl->flags;
        if ((flg & 0x2000) || m.obj != skelRoot->hitObj.obj || m.node != skelRoot->hitObj.node ||
            (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0xF00000)) || (flg & 2)) {
            GOBJ_SUB(self)->ctrl.posReserve = 0;
        } else if (skelMotCtrl->reserveBlend != 0) {
            _InterVectorXYZ(skelRoot->pos, skelRoot->savePos, skelRoot->pos,
                            1.0f - (float)skelMotCtrl->reserveBlend * 0.5f);
            skelMotCtrl->reserveBlend -= 1;
        }
    }
    if (debug_skel_flag != 0) {
        if ((GObj *)skelGObj == boyGObj) {
            CopyVector(buf2, skelRoot->savePos);
            _UnitMatrix(MatrixDrive_GetMatrix());
            if (m.obj != 0) {
                _ApplyMatrix(buf2, ((char *)GOBJ_SUB(m.obj)->nodeMtx + m.node * 64), (char *)buf2);
                MatrixDrive_TransMatrixV(buf2);
                gif_StartPacketPri(11);
                if (GOBJ_SUB(self)->ctrl.posReserve == 1) {
                    prim_DispWireSphere(50.0f, reserverColor, 16, 8);
                } else {
                    prim_DispWireSphere(50.0f, reserverColor2, 16, 8);
                }
                gif_EndPacket();
            }
        }
    }
}

typedef struct MotHdrTag MotHdr; /* derived name */

/* the object's display object, read through geometryManager.h's SubHandle
   union, as motionOrientManager.c reads it */
#define MOWORK(self) (((SubHandle *)&((GObj *)(self))->dobj)->sub) /* derived name */

void GetGeometryOfMotion(void *self, void *m0, void *m1, float *v, float r, float *step, int k)
{
    ObjNode sh;
    float v2[4];

    sh = MOWORK(self)->parent;
    stepFocusNode = k;
    if ((char *)MOWORK(self)->local.obj != 0) {
        MOWORK(self)->localPos[3] = 1.0f;
        CopyVector(v2, MOWORK(self)->localPos);
        sceVu0ApplyMatrix(
            (int *)v2,
            (void *)(GOBJ_SUB(MOWORK(self)->local.obj)->nodeMtx + MOWORK(self)->local.node * 64),
            (char *)v2);
    } else {
        AddVectorXYZ(MOWORK(self)->localPos, MOWORK(self)->localPos, MOWORK(self)->localMove);
        CopyVector(v2, MOWORK(self)->localPos);
    }
    v2[1] = v2[1] + MOWORK(self)->localHeight;
    v2[3] = 1.0f;
    UnlinkParentOfDObj(self);

    skelNodeNum = MOWORK(self)->skelNodeNum;
    {
        float wk0[skelNodeNum][4], wk1[skelNodeNum][4];

        skelGObj = (ICO_WORD)self;
        nodePos = (char *)wk0;
        nodePos2 = (char *)wk1;
        skelMotion = m0;
        skelMotion2 = m1;
        skelScale = MOWORK(self)->nodes->scale[0];
        skelRoot = &MOWORK(self)->root;
        skelMotCtrl = &MOWORK(self)->ctrl;
        skelNode = MOWORK(self)->skel;
        skelMotDef = &motionKind[skelMotCtrl->motion];
        CopyVector(rootMove, v);
        CopyVector(rootStep, step);
        skelMotCtrl->flags = 0;
        sceVu0SubVector(rootDelta, skelRoot->last, skelRoot->clipFrom);
        if (MOWORK(self)->ctrl.keepStand != 0) {
            ObjNode tmp;
            getGeometryOfMotion(&tmp, r != 1.0f);
        } else {
            getGeometryOfMotion(&sh, r != 1.0f);
        }
        CopyVector(skelRoot->clipFrom, skelRoot->last);
        if (MOWORK(self)->ctrl.noStand == 1) {
            sh = InitialObjPointer;
        }
        AddVectorXYZ(MOWORK(self)->motionPos, skelRoot->pos, skelRoot->trans);
        MOWORK(self)->motionPos[3] = 1.0f;
        MOWORK(self)->motionPos[1] = MOWORK(self)->motionPos[1] * r + v2[1] * (1.0f - r);
        GetMatrixOfMotion(self, m1, MOWORK(self)->motionPos);
    }
    if (debug_wallcheck_flag != 0) {
        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 128);
        gif_SetZTest(0);
        gif_EndPacket();
        dispPlane((Vec4 *)MOWORK(self)->root.plane.f, MOWORK(self)->root.pos);
        gif_StartPacketPri(11);
        gif_SetZTest(1);
        gif_EndPacket();
    }
    skelRoot->last[3] = 1.0f;
    skelRoot->clipFrom[3] = 1.0f;
    skelRoot->pos[3] = 1.0f;
    skelRoot->move[3] = 0.0f;
    if (sh.obj != 0) {
        LinkParentOfDObj(self, &sh);
        if (GOBJ_SUB(sh.obj)->rideFunc != 0) {
            GOBJ_SUB(sh.obj)->rideFunc(&sh, self);
        }
    } else {
        MOWORK(self)->root.pos[1] = MOWORK(self)->root.pos[1] - MOWORK(self)->root.height;
        MOWORK(self)->root.last[1] = MOWORK(self)->root.last[1] - MOWORK(self)->root.height;
    }
    MOWORK(self)->motionPos[1] = MOWORK(self)->motionPos[1] - skelRoot->height;
    if (sh.obj != 0) {
        float m[16];
        MatrixDrive_SetTransposeMatrix((void *)m,
                                       (void *)(GOBJ_SUB(sh.obj)->nodeMtx + sh.node * 64));
        sceVu0ApplyMatrix((MOWORK(self)->motionPos), m, MOWORK(self)->motionPos);
    }
    execPositionReserver(self, sh);
}

/* GObj+8 is the index into objLayout, the 76-byte GenGeo table (ebrain.c
   types that array `GenGeo objLayout[]`; enemy_act.c indexes it with the same
   `obj[2]` field), read as an enumerated kind. */
typedef enum { GENGEO_KIND_0 = 0 } GenGeoKind; /* derived name */

void GetMatrixOfMotion(GObj *self, char *tbl, void *ofs)
{
    float v[4];
    float w[4];
    float p1[4];
    float p2[4];
    int i;

    /* the sub-object fields are read as int addresses */
    skelMotion = tbl;
    skelQuat = (char *)GOBJ_SUB(self)->nodeQuat;
    skelNode = GOBJ_SUB(self)->skel;
    skelScale = GOBJ_SUB(self)->nodes->scale[0];
    skelRoot = &GOBJ_SUB(self)->root;
    skelMotCtrl = &GOBJ_SUB(self)->ctrl;
    skelNodeNum = GOBJ_SUB(self)->skelNodeNum;
    /* PC port: GObj+8 is labelId and the row is a GenGeo (0x4C bytes and
       kind at 0x46 on the EE only; a 64-bit host has wider pointers in
       both records) */
    skelGeoType = ((GenGeo *)objLayout)[self->labelId].kind;
    skelGObj = (ICO_WORD)self;
    MatrixDrive_PushMatrix();
    PushQuaternion();

    GetMatrixFromQuaternion((void *)MatrixDrive_GetMatrix(), skelRoot->quat);
    SetCurrentQuaternion(skelRoot->quat);

    MatrixDrive_PushMatrix();

    rootHeightVec[1] = skelRoot->height;
    MatrixDrive_RotMatrixZ(skelRoot->twist);

    sceVu0ApplyMatrix(v, MatrixDrive_GetMatrix(), rootHeightVec);

    v[1] -= skelRoot->height;
    v[0] *= 0.5f;
    v[2] *= 0.5f;
    MatrixDrive_PopMatrix();

    if (skelMotCtrl->catchBoy != 0) {
        getFinalMatrix(0);
    } else {
        getFinalMatrixWithNaturalGeometry(0);
    }

    scaleMatrix[0] = scaleMatrix[5] = scaleMatrix[10] = skelScale;

    AddVectorXYZ(w, ofs, v);
    for (i = 0; i < skelNodeNum; i++) {
        char *nd = (char *)GOBJ_SUB(skelGObj)->nodeMtx + i * 64;
        char *pos = nd + 48;
        sceVu0MulMatrix(nd, nd, scaleMatrix);
        AddVectorXYZ(pos, pos, w);
    }
    MatrixDrive_PopMatrix();
    PopQuaternion();

    if (debug_actnode_flag != 0) {
        dispActNode(skelRoot->standNode);
        dispLastNode();
    }
    if (debug_skel_flag != 0) {
        int n;

        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        gif_StartPacketPri(11);
        n = GetSkeltonFocusNode(self, 35);
        CopyVector(w, (char *)GOBJ_SUB(skelGObj)->nodeMtx + n * 64 + 48);
        CopyVector(p1, skelRoot->lookPos);
        _SubVector(p2, p1, w);
        _NormalizeVector(p2, p2);
        _ScaleVector(p2, p2, 100.0f);
        _AddVector(p1, w, p2);
        gif_SetAlpha(1, 5, 128);
        if (skelRoot->lookIK != 0 && skelRoot->lookMode != 0) {
            DrawLineG(w, dirColor, p1, dirColor, -1);
        } else {
            DrawLineG(w, dirColor2, p1, dirColor2, -1);
        }
        gif_EndPacket();
    }
}

/* a file static; ico2/sugipon/src/motionManager2.c has its own of the same
   name */
static void dispSkeltonHierarchy(int node)
{
    if (skelNode[node].parent != -1) {
        float o[3] = {0.0f, 0.0f, 0.0f};
        float p[3] = {skelNode[node].pos[0], skelNode[node].pos[1], skelNode[node].pos[2]};
        float ax[3] = {0.0f, 5.0f, 0.0f};
        float ay[3] = {0.0f, 0.0f, 5.0f};
        float az[3] = {5.0f, 0.0f, 0.0f};
        sceVu0IVECTOR c0 = {0x40, 0x40, 0x40, 0x80};
        sceVu0IVECTOR c1 = {0x00, 0xFF, 0x00, 0x80};
        sceVu0IVECTOR c2 = {0x00, 0x80, 0xFF, 0x80};
        sceVu0IVECTOR c3 = {0xFF, 0x00, 0x00, 0x80};

        DrawLineG(o, c0, p, c0, -1);
        DrawLineG(o, c0, ax, c1, -1);
        DrawLineG(o, c0, ay, c2, -1);
        DrawLineG(o, c0, az, c3, -1);
    }
    MatrixDrive_PushMatrix();
    CopyMatrix(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(skelGObj)->nodeMtx + node * 64);
    if (skelNode[node].child == -1) {
        float o2[3] = {0.0f, 0.0f, 0.0f};
        float e[3] = {10.0f, 0.0f, 0.0f};
        sceVu0IVECTOR c = {0xFF, 0xFF, 0xFF, 0x80};

        DrawLineG(o2, c, e, c, -1);
    }
    if (skelNode[node].child != -1) {
        dispSkeltonHierarchy(skelNode[node].child);
    }
    MatrixDrive_PopMatrix();
    if (skelNode[node].sibling != -1) {
        dispSkeltonHierarchy(skelNode[node].sibling);
    }
}

/* a file static; ico2/sugipon/src/geometryManager.c has its own of the
   same name */

static void getInitialMatrix(Sub15C *obj, int idx)
{
    SkelNode *nd;
    char *mtx;

    nd = &obj->skel[idx];
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(nd->pos);
    MultiMatrixByQuaternion(nd->quat);
    switch (nd->kind) {
    case 19:
    case 20:
    case 22:
        MatrixDrive_RotMatrixY((short)((pad[0].ana[0] - 128) << 7));
        MatrixDrive_RotMatrixZ((short)((pad[0].ana[1] - 128) << 7));
        break;
    }
    mtx = (char *)obj->nodeMtx + idx * 64;
    CopyMatrix(mtx, (void *)MatrixDrive_GetMatrix());
    if (nd->child != -1) {
        getInitialMatrix(obj, nd->child);
    }
    MatrixDrive_PopMatrix();
    if (nd->sibling != -1) {
        getInitialMatrix(obj, nd->sibling);
    }
}

/* SkelTest and SkelTestGeo below pass their object, which it does not
 * read (the EE's definition declared no prototype). */
static void dispSkelton(GObj *self)
{
    float (*v)[4];
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 128);
    MatrixDrive_PushMatrix();
    v = MatrixDrive_GetMatrix();
    sceVu0UnitMatrix(v);
    dispSkeltonHierarchy(0);
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

void SkelTest(GObj *self)
{
    Sub15C *sub = GOBJ_SUB(self);
    SkelNode *v;
    skelGObj = (ICO_WORD)self;
    v = sub->skel;
    skelNode = v;
    if (v != 0) {
        p2o_DispVU1(self);
        if (debug_skel_flag != 0) {
            dispSkelton(self);
        }
    }
}

void SkelTestGeo(GObj *self)
{
    Sub15C *sub = GOBJ_SUB(self);
    SkelNode *v;
    int i;
    skelGObj = (ICO_WORD)self;
    v = sub->skel;
    skelNode = v;
    if (v != 0) {
        Sub15C *s2;
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_RotMatrixX(-32768);
        getInitialMatrix(GOBJ_SUB(self), 0);
        s2 = GOBJ_SUB(self);
        for (i = 0; i < s2->skelNodeNum; i++) {
            ICO_WORD e = s2->nodeMtx + i * 64;
            sceVu0MulMatrix((void *)e, &s2->matrix, (void *)e);
            s2 = GOBJ_SUB(self);
        }
        if (debug_skel_flag != 0) {
            dispSkelton(self);
        }
    }
}
