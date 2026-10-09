#include "a_p_1.h"
#include "DObj.h"
#include "debug.h"
#include "debug_exception.h"
#include "sceneManager.h"
#include "memory.h"
#include "obj_manager.h"
#include "fieldCollision.h"
#include "DisplayP2O.h"
#include "enemyParts.h"
#include "geometryManager.h"
#include "pool.h"
#include <stdlib.h>
#include "typedef.h"
#include "sugiCommon.h"
#include "matrixDrive.h"
#include "main.h"
#include "ios.h"
#include "Matrix.h"
#include "motionManager2.h"
#include "frameDependSequence.h"
#include "attackhit.h"
#include "spider.h"
#include <assert.h>

/* short (float), as tableSin.h and the definition: an int declaration
   reads the upper half of the return register, which the callee leaves
   unspecified (see quaternion.c; issue 19) */
extern short GetTableArcCos(float x);
/* as in tableSin.h, which this file does not include */
extern short GetTableArcTan2(float y, float x);

typedef struct { /* field names derived */
    float m[4];
} __attribute__((aligned(16))) Vec4A_P_1; /* derived name */

/* One of the four limbs: its motion state (0 at rest, 1 stepping, 2
 * swinging), the frame of that motion, the foot's current point, the point it
 * rests at in the body frame, and the knee and the tip the arm solver
 * places. */
typedef struct {    /* field names derived */
    int state;      /* 0x00 */
    int count;      /* 0x04 */
    int pad8[2];    /* 0x08 */
    Vec4A_P_1 pos;  /* 0x10 */
    Vec4A_P_1 home; /* 0x20 */
    Vec4A_P_1 knee; /* 0x30 */
    Vec4A_P_1 tip;  /* 0x40 */
} AP1Part;          /* derived name */

/* The clip table the two collision segments are read from: each entry is a
 * pair of endpoints the root matrix is applied to. */
typedef struct { /* field names derived */
    Vec4A_P_1 a; /* 0x00 */
    Vec4A_P_1 b; /* 0x10 */
} AP1ColSeg;     /* derived name */

/* The 0x280-byte work record InitAP1 allocates into the object's work word:
 * the layout row, whether the body is its own skeleton (1) or two arm objects
 * (0), the mode, the four limbs, the two collision hits, the nine focus nodes,
 * the two arm objects, the eye, the up vector, three motion parameters, the
 * body's smoothed attitude and position, its matrix and the root matrix, and
 * three counters. */
typedef struct {     /* field names derived */
    int layout;      /* 0x000, the row of spiderDef */
    int skel;        /* 0x004 */
    int mode;        /* 0x008 */
    int padC;        /* 0x00C */
    AP1Part part[4]; /* 0x010 */
    WallCfg hit[2];  /* 0x150 */
    int word168;     /* 0x168 */
    int word16C;     /* 0x16C */
    int focus[9];    /* 0x170, the skeleton nodes of ap1FocusNode: the
                           body's, then each limb's pair; calcSubMission
                           reaches a pair from &focus[1] and &focus[2] */
    Sub15C *arm[2];  /* 0x194, the two arm objects when skel is 0 */
    EnemyEye *eye;   /* 0x19C */
    int pad1A0[4];   /* 0x1A0 */
    Vec4A_P_1 up;    /* 0x1B0 */
    float tilt;      /* 0x1C0, the forward tilt the speed gives */
    float roll;      /* 0x1C4, the roll the sideways motion gives */
    float sink;      /* 0x1C8, 0 to 1, how far the body sinks and tips forward */
    int pad1CC;      /* 0x1CC */
    Vec4A_P_1 quat;  /* 0x1D0 */
    Vec4A_P_1 pos;   /* 0x1E0 */
    float mtx[16];   /* 0x1F0 */
    float root[16];  /* 0x230 */
    int blink;       /* 0x270 */
    int settleCount; /* 0x274, the first 10 frames reset the position info */
    int visible;     /* 0x278 */
    int pad27C;      /* 0x27C */
} AP1Work;           /* derived name */

static int standMot(GObj *self);
static int walkMot(GObj *self);
static int rollingMot(GObj *self);
static void attackMotInit(GObj *self);
static int attackMot(GObj *self);

/* the mode names */
static char *ap1ModeName[9] = {"ST", "WA", "RO", "AT", "DE",
                               "D1", "D2", "TH", "SL"}; /* derived name */

/* the part record every part slot starts from: three unit w components */
static AP1Part ap1PartInit = {0,
                              0,
                              {0, 0}, /* derived name */
                              {{0.0f, 0.0f, 0.0f, 1.0f}},
                              {{0.0f, 0.0f, 0.0f, 1.0f}},
                              {{0.0f, 0.0f, 0.0f, 1.0f}},
                              {{0.0f, 0.0f, 0.0f, 0.0f}}}; /* derived name */

/* the skeleton nodes InitAP1 looks the nine focus points up by */
static int ap1FocusNode[9] = {44, 3, 4, 19, 20, 45, 46, 49, 50}; /* derived name */

static Vec4A_P_1 ap1DownVector = {{0.0f, -1.0f, 0.0f, 0.0f}}; /* derived name */

static Vec4A_P_1 ap1AttackAxis = {{-1.0f, 0.0f, 0.0f, 0.0f}}; /* derived name */

/* the two segment pairs fitToCol clips the body against */
static AP1ColSeg ap1ColSegA[2] = {
    {{{0.0f, -40.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 40.0f, 1.0f}}}, /* derived name */
    {{{0.0f, 0.0f, 40.0f, 1.0f}}, {{0.0f, 40.0f, 0.0f, 1.0f}}}}; /* derived name */

static AP1ColSeg ap1ColSegB[2] = {
    {{{0.0f, -40.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, -40.0f, 1.0f}}}, /* derived name */
    {{{0.0f, 0.0f, -40.0f, 1.0f}}, {{0.0f, 40.0f, 0.0f, 1.0f}}}}; /* derived name */

/* the part offsets in the body frame */
static Vec4A_P_1 ap1PartOffset[6] = {{{20.0f, 0.0f, 80.0f, 1.0f}}, /* derived name */
                                     {{-20.0f, 0.0f, 80.0f, 1.0f}},  {{50.0f, 0.0f, -20.0f, 1.0f}},
                                     {{-50.0f, 0.0f, -20.0f, 1.0f}}, {{-50.0f, 0.0f, 0.0f, 1.0f}},
                                     {{-50.0f, 0.0f, 0.0f, 1.0f}}}; /* derived name */

static float ap1LayoutUp[4] = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

/* the clip records fitToCol and rolling fill */
static ClipWork ap1PartClip = {{{0.0f}}}; /* derived name */

static ClipWork ap1RollClip = {{{0.0f}}, {{0.0f}}, 20.0f}; /* derived name */

static float ap1ArmScale[16] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, /* derived name */
                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

static float ap1ArmOffset[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, /* derived name */
                                 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 0.0f, 1.0f};

static float ap1BodyPos[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float ap1BodyMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, /* derived name */
                                  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

/* per motion kind: the entry and the per-frame function */
int (*motFuncList[8][2])(GObj *) = {
    {0, standMot},
    {0, walkMot},
    {0, rollingMot},
    {(int (*)(GObj *))attackMotInit, attackMot},
};

/* the eye offset UpdateEnemyEye is handed, then an alternative nothing reads */
static float ap1EyeMatrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,   0.0f,  0.0f,  /* derived name */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -20.0f, 30.0f, 1.0f}; /* derived name */

static float ap1EyeMatrixAlt[16] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,   0.0f,  0.0f,  /* derived name */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -10.0f, 35.0f, 1.0f}; /* derived name */

static float ap1HeadScale[16] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, /* derived name */
                                 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

static inline void applyPartOrients(GObj *g) /* derived name */
{
    char *tbl = (char *)ap1PartOffset;
    Mtx44 m;
    AP1Work *q = GOBJ_SUB(g)->work;
    int i;

    GetRootMatrix(m.m, g);
    for (i = 0; i < 4; i++) {
        _ApplyMatrix(&q->part[i].pos, &m, (tbl + i * 0x10));
        _ApplyMatrix(&q->part[i].home, &m, (tbl + i * 0x10));
    }
}

/* the name every iosMallocDebug and assert in this file reports itself under */
static const char a_p_1File[] = "src/a_p_1.c"; /* derived name */

/* the banner the failed-node assert prints above its message */
static const char warningBanner[] = "--- WARNING!! ----\n"; /* derived name */

typedef union { /* field names derived */
    int i;
    long long ll;
} AP1Flag; /* derived name */

AP1Work *InitAP1(GObj *self, SObjSimpleSetting *arg)
{
    AP1Work *p;
    Sub15C *d;
    int i;

    p = iosMallocDebug(ios_partition_sugipon, sizeof(AP1Work), a_p_1File, 228);
    GOBJ_SUB(self)->work = p;
    p->layout = arg->obj;
    p->skel = 1;
    p->word16C = 0;
    p->mode = 7;
    p->word168 = 0;
    p->roll = 0.0f;
    p->sink = 0.0f;
    p->tilt = 0.0f;
    p->blink = rand() & 0x1F;
    p->settleCount = 0;
    p->visible = 1;
    ap1LayoutUp[2] = spiderDef[p->layout].upZ;
    CopyVector(&p->up, ap1LayoutUp);
    _UnitMatrix(p->mtx);
    _UnitMatrix(p->root);
    GetRootQuaternion(&p->quat, self);
    GetRootPosition(p->pos.m, self);
    for (i = 0; i < 4; i++) {
        p->part[i] = ap1PartInit;
    }
    for (i = 0; i < 2; i++) {
        p->hit[i] = InitialColInfo;
    }
    applyPartOrients(self);
    if (p->skel == 0) {
        d = CSVSYSTEM_InitDObj(7, arg);
        p->arm[0] = d;
        if (d->nodeMtx != 0) {
            iosFree((void *)ICO_PHYS(d->nodeMtx));
        }
        if (p->arm[0]->nodeQuat != 0) {
            iosFree((void *)ICO_PHYS(p->arm[0]->nodeQuat));
        }
        p->arm[0]->nodeMtx = 0;
        p->arm[0]->nodeQuat = 0;
        p->arm[0]->nodeMtx = (ICO_WORD)iosMallocDebug(ios_partition_seki, 256, a_p_1File, 261);
        p->arm[0]->nodeQuat = (ICO_WORD)iosMallocDebug(ios_partition_seki, 64, a_p_1File, 261);
        p->arm[0]->nodeNum = 4;
        if (p->arm[0]->nodes != 0) {
            iosFree((void *)ICO_PHYS(ICO_ADDR(p->arm[0]->nodes)));
        }
        p->arm[0]->nodes = iosMallocDebug(ios_partition_seki, 320, a_p_1File, 261);
        {
            int n;

            for (n = 0; n < 4; n++) {
                p->arm[0]->nodes[n].flags.ll &= ~1;
                p->arm[0]->nodes[n].flags.ll &= ~2;
                p->arm[0]->nodes[n].pos[0] = 0.0f;
                p->arm[0]->nodes[n].pos[1] = 0.0f;
                p->arm[0]->nodes[n].pos[2] = 0.0f;
                p->arm[0]->nodes[n].pos[3] = 1.0f;
                p->arm[0]->nodes[n].flags.ll &= ~4;
                p->arm[0]->nodes[n].fade = 0;
                p->arm[0]->nodes[n].alpha = 1.0f;
                ((short *)&p->arm[0]->nodes[n].flags)[1] = 0;
                p->arm[0]->nodes[n].scale[0] = 1.0f;
                p->arm[0]->nodes[n].scale[1] = 1.0f;
                p->arm[0]->nodes[n].scale[2] = 1.0f;
            }
        }
        p->arm[0]->dispType = 2;
        d = CSVSYSTEM_InitDObj(8, arg);
        p->arm[1] = d;
        if (d->nodeMtx != 0) {
            iosFree((void *)ICO_PHYS(d->nodeMtx));
        }
        if (p->arm[1]->nodeQuat != 0) {
            iosFree((void *)ICO_PHYS(p->arm[1]->nodeQuat));
        }
        p->arm[1]->nodeMtx = 0;
        p->arm[1]->nodeQuat = 0;
        p->arm[1]->nodeMtx = (ICO_WORD)iosMallocDebug(ios_partition_seki, 256, a_p_1File, 264);
        p->arm[1]->nodeQuat = (ICO_WORD)iosMallocDebug(ios_partition_seki, 64, a_p_1File, 264);
        p->arm[1]->nodeNum = 4;
        if (p->arm[1]->nodes != 0) {
            iosFree((void *)ICO_PHYS(ICO_ADDR(p->arm[1]->nodes)));
        }
        p->arm[1]->nodes = iosMallocDebug(ios_partition_seki, 320, a_p_1File, 264);
        {
            int n;

            for (n = 0; n < 4; n++) {
                p->arm[1]->nodes[n].flags.ll &= ~1;
                p->arm[1]->nodes[n].flags.ll &= ~2;
                p->arm[1]->nodes[n].pos[0] = 0.0f;
                p->arm[1]->nodes[n].pos[1] = 0.0f;
                p->arm[1]->nodes[n].pos[2] = 0.0f;
                p->arm[1]->nodes[n].pos[3] = 1.0f;
                p->arm[1]->nodes[n].flags.ll &= ~4;
                p->arm[1]->nodes[n].fade = 0;
                p->arm[1]->nodes[n].alpha = 1.0f;
                ((short *)&p->arm[1]->nodes[n].flags)[1] = 0;
                p->arm[1]->nodes[n].scale[0] = 1.0f;
                p->arm[1]->nodes[n].scale[1] = 1.0f;
                p->arm[1]->nodes[n].scale[2] = 1.0f;
            }
        }
        p->arm[1]->dispType = 2;
    } else {
        for (i = 0; i < 9; i++) {
            p->focus[i] = GetSkeltonFocusNode(self, *(int *)((char *)ap1FocusNode + i * 4));
            if (p->focus[i] == -1) {
                debug_assertMessage(a_p_1File, 269, warningBanner);
                __assert(a_p_1File, 269, "e");
            }
        }
        p->arm[1] = 0;
        p->arm[0] = 0;
    }
    p->eye = InitEnemyEye(10, 0, 10);
    return p;
}

/* quaternion.h is not included here: this file's SetQuaternionByAxisRotateV
 * and RotQuaternionY calls pass the angle as an int (quaternion.h: short) */
extern void GetMatrixFromQuaternion(void *mtx, void *q);
extern void MultiQuaternion(void *dst, void *a, void *b);
extern void SetQuaternionByAxisRotateV(void *dst, int ang, void *axis);

static void yAxisRotFitting(GObj *self, void *nrm)
{
    Vec4A_P_1 down;
    Vec4A_P_1 axis;
    Mtx44 m;
    Vec4A_P_1 rot;
    Vec4A_P_1 q;
    int r;
    float f;

    GetRootQuaternion(&q, self);
    GetMatrixFromQuaternion(&m, &q);
    _ApplyMatrix(&down, &m, &ap1DownVector);
    f = _InnerProduct(&down, nrm);
    r = GetTableArcCos(f);
    if (r != 0) {
        _OuterProduct(&axis, nrm, &down);
        _NormalizeVector(&axis, &axis);
        SetQuaternionByAxisRotateV(&rot, r, &axis);
        MultiQuaternion(&q, &rot, &q);
        SetRootQuaternion(self, &q);
    }
}

static void zAxisRotFitting(GObj *self, void *nrm)
{
    Vec4A_P_1 front;
    Vec4A_P_1 axis;
    Mtx44 m;
    Vec4A_P_1 rot;
    Vec4A_P_1 q;
    int r;
    float f;

    GetRootQuaternion(&q, self);
    GetMatrixFromQuaternion(&m, &q);
    _ApplyMatrix(&front, &m, ZUnitVector);
    f = _InnerProduct(&front, nrm);
    r = GetTableArcCos(f);
    if (r != 0) {
        _OuterProduct(&axis, nrm, &front);
        _NormalizeVector(&axis, &axis);
        SetQuaternionByAxisRotateV(&rot, r, &axis);
        MultiQuaternion(&q, &rot, &q);
        SetRootQuaternion(self, &q);
    }
}

static inline int clipAndTakeHit(WallCfg *dst, ClipWork *col) /* derived name */
{
    ClipCollision(col);
    if (col->wall.elem != 0) {
        dst->elem = col->wall.elem;
        dst->o = col->wall.o;
        return 1;
    }
    if (col->floor.elem != 0) {
        dst->elem = col->floor.elem;
        dst->o = col->floor.o;
        return 1;
    }
    return 0;
}

/* void (void *, void *) here, void (float *, float *) in quaternion.h */
extern void GetInverseQuaternion(void *dst, void *src);

static inline void fitYawToVector(GObj *self, Vec4A_P_1 *dir) /* derived name */
{
    Vec4A_P_1 q;
    Vec4A_P_1 qi;
    Vec4A_P_1 rot;
    Mtx44 mm;
    Vec4A_P_1 v;

    GetRootQuaternion(&q, self);
    GetInverseQuaternion(&qi, &q);
    GetMatrixFromQuaternion(&mm, &qi);
    _ApplyMatrix(&v, &mm, dir);
    v.m[2] = 0.0f;
    _NormalizeVector(&v, &v);
    SetQuaternionByAxisRotateV(&rot, (short)-GetTableArcTan2(v.m[0], -v.m[1]), ZUnitVector);
    MultiQuaternion(&q, &q, &rot);
    SetRootQuaternion(self, &q);
}

static inline int clipPartPair(WallCfg *dst, Mtx44 *m, AP1ColSeg *tbl, Vec4A_P_1 *pos,
                               Vec4A_P_1 *nrm) /* derived name */
{
    int i;

    for (i = 0; i < 2; i++) {
        _ApplyMatrix(ap1PartClip.pt[0], m, &tbl[i].a);
        _ApplyMatrix(ap1PartClip.pt[1], m, &tbl[i].b);
        if (clipAndTakeHit(dst, &ap1PartClip)) {
            CopyVector(pos, ap1PartClip.pt[2]);
            CopyVector(nrm, &ap1PartClip.normal);
            return 1;
        }
    }
    dst->elem = 0;
    return 0;
}

/* a limb's hit: setPartHit marks it, resetPartHit first puts the limb back
   at its rest point */
static inline void setPartHit(AP1Part *part) /* derived name */
{
    part->state = 1;
    part->count = 0;
}

static inline void resetPartHit(AP1Part *part, float *orient) /* derived name */
{
    CopyVector(&part->home, orient);
    setPartHit(part);
}

static int fitToCol(GObj *self, int walking)
{
    Mtx44 m;
    Vec4A_P_1 posA;
    Vec4A_P_1 posB;
    Vec4A_P_1 nrmA;
    Vec4A_P_1 nrmB;
    Vec4A_P_1 nrm;
    Vec4A_P_1 dir;
    Vec4A_P_1 pos;
    AP1Work *p;
    int hit1;
    int hit2;

    p = GOBJ_SUB(self)->work;
    GetRootMatrix(m.m, self);
    hit1 = clipPartPair(&p->hit[0], &m, ap1ColSegA, &posA, &nrmA);
    hit2 = clipPartPair(&p->hit[1], &m, ap1ColSegB, &posB, &nrmB);
    if (hit1) {
        if (hit2) {
            _SubVectorXYZ(&dir, &posA, &posB);
            _NormalizeVector(&dir, &dir);
            zAxisRotFitting(self, &dir);
            _InterVector(&nrm, &nrmA, &nrmB, 0.5f);
            _NormalizeVector(&nrm, &nrm);
            fitYawToVector(self, &nrm);
            _InterVector(&pos, &posA, &posB, 0.5f);
            SetRootPosition(self, &pos);
            {
                Mtx44 tm;
                Vec4A_P_1 t;
                int i;
                float rangeSq = 10000.0f;
                char *tbl;

                MatrixDrive_SetTransposeMatrix(tm.m, m.m);
                tbl = (char *)ap1PartOffset;
                /* lim holds each bound of the test in turn, z the component
                   under test */
                for (i = 0; i < 4; i++) {
                    AP1Part *part = &p->part[i];
                    float lim;
                    float z;

                    _ApplyMatrix(&t, &tm, &part->pos);
                    if (part->state != 0) {
                        continue;
                    }
                    if (walking != 0) {
                        if (i == 0) {
                            if (p->part[1].state == 0) {
                                goto reset;
                            }
                        }
                        if (i == 3) {
                            if (p->part[2].state == 0) {
                                goto reset;
                            }
                        }
                    }
                    if (distance_squared(&pos, &part->pos) > rangeSq) {
                        goto reset;
                    }
                    z = t.m[2];
                    if (i & 2) {
                        lim = 0.0f;
                        if (z > lim) {
                            goto reset;
                        }
                        lim = -50.0f;
                    } else {
                        lim = 20.0f;
                    }
                    if (z < lim) {
                        goto reset;
                    }
                    z = t.m[0];
                    if (i & 1) {
                        lim = -10.0f;
                        if (z > lim) {
                            goto reset;
                        }
                    } else {
                        lim = 10.0f;
                        if (z < lim) {
                            goto reset;
                        }
                    }
                    continue;
                reset:
                    resetPartHit(part, (float *)(tbl + i * 0x10));
                }
            }
            return -1;
        }
        fitYawToVector(self, &nrmA);
        return -1;
    }
    if (hit2) {
        fitYawToVector(self, &nrmB);
    }
    return 2;
}

typedef union { /* field names derived */
    int i;
    float f;
} AP1Val; /* derived name */

static int walkMot(GObj *self)
{
    Vec4A_P_1 pos;
    Vec4A_P_1 v;
    Mtx44 m;
    Mtx44 tm;
    Vec4A_P_1 out;
    AP1Work *p = GOBJ_SUB(self)->work;
    int ret = fitToCol(self, 1);
    int i;
    int n;

    if (ret != -1)
        return ret;

    GetRootPosition(pos.m, self);
    GetRootMatrix(m.m, self);
    _ApplyMatrix(&v, &m, &p->up);
    n = 0;
    for (i = 0; i < 4; i++) {
        if (p->part[i].state == 0) {
            n++;
        }
    }
    _ScaleVector(&v, &v, ((float)n * 0.25f + 0.5f) * 0.5f);
    _ScaleVector(GOBJ_SUB(self)->root.move, GOBJ_SUB(self)->root.move, 0.8f);
    _AddVectorXYZ(GOBJ_SUB(self)->root.move, GOBJ_SUB(self)->root.move, &v);
    MatrixDrive_SetTransposeMatrix(tm.m, m.m);
    _ApplyMatrix(&out, &tm, GOBJ_SUB(self)->root.move);
    /* the two stores go through the file's AP1Val view */
    ((AP1Val *)&p->roll)->f = out.m[0];
    ((AP1Val *)&p->tilt)->f = VectorLength(GOBJ_SUB(self)->root.move) * 0.1f;
    _AddVectorXYZ(&pos, &pos, GOBJ_SUB(self)->root.move);
    SetRootPosition(self, &pos);
    p->sink = 0.0f;
    return 1;
}

static int rolling(GObj *self)
{
    WallCfg info;

    if (GOBJ_SUB(self)->parent.obj != 0) {
        UnlinkParentOfDObj(self);
    }
    GOBJ_SUB(self)->root.move[1] +=
        60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
        (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
    _AddVectorXYZ(GOBJ_SUB(self)->root.pos, GOBJ_SUB(self)->root.pos, GOBJ_SUB(self)->root.move);
    {
        ClipWork *col = &ap1RollClip;
        CopyVector(col->pt[0], GOBJ_SUB(self)->root.last);
        CopyVector(col->pt[1], GOBJ_SUB(self)->root.pos);
        col->pt[0][1] -= 50.0f;
        if (clipAndTakeHit(&info, col)) {
            CopyVector(GOBJ_SUB(self)->root.pos, ap1RollClip.pt[2]);
            CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
            yAxisRotFitting(self, &ap1RollClip.normal);
            LinkParentOfDObj(self, &info.o);
            UpdateRootMatrix(self);
            applyPartOrients(self);
            {
                ClipWork *col = &ap1RollClip;
                if (col->wall.elem != 0) {
                    GOBJ_SUB(self)->ctrl.floorAttr = GetWallAttribute(col);
                }
                if (CheckWallAttribute(self, 0x50) != 0) {
                    if (GetPoolGlobalHeight(col->wall.o.obj) <
                        GOBJ_SUB(self)->root.pos[1] + 50.0f) {
                        iosOmSendMail(self, 0x26, self);
                    }
                }
            }
            {
                ClipWork *col = &ap1RollClip;
                if (col->floor.elem != 0) {
                    GOBJ_SUB(self)->ctrl.floorAttr = GetFloorAttribute(col);
                    if (CheckFloorAttribute(self, 0x50) != 0) {
                        if (GetPoolGlobalHeight(col->floor.o.obj) <
                            GOBJ_SUB(self)->root.pos[1] + 50.0f) {
                            iosOmSendMail(self, 0x26, self);
                        }
                    }
                }
            }
            return 0;
        }
    }
    {
        ClipWork *col = &ap1RollClip;
        col->pt[1][1] += 500.0f;
        ClipFloor(col);
        if (CheckFieldContact(col, self, GOBJ_SUB(self)->root.pos, 50.0f) == 2) {
            CopyVector(GOBJ_SUB(self)->root.move, ZeroVector);
            iosOmSendMail(self, 0x1A, self);
        }
    }
    return -1;
}

/* the point the spider's bite attack is centred on, 50 units down its own arm */
static const Vec4A_P_1 attackCenterOffset = {50.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

/* the law of cosines for the two arm segments, one named local per
 * line */
static inline short armCosine(float a, float b, float c) /* derived name */
{
    float aa = a * a;
    float bb = b * b;
    float cc = c * c;

    return (short)(int)GetTableArcCos((aa + bb - cc) / (2.0f * a * b));
}

static void calcSubMission(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    Vec4A_P_1 base;
    Vec4A_P_1 axis;
    Vec4A_P_1 rq;
    Mtx44 tm;
    Vec4A_P_1 q;
    Vec4A_P_1 w;
    Vec4A_P_1 dir;
    Vec4A_P_1 v;
    Vec4A_P_1 v2;
    Mtx44 rm;
    Vec4A_P_1 lv;
    Vec4A_P_1 save;
    Vec4A_P_1 save2;
    Vec4A_P_1 atk;
    int i;
    int ang;

    CopyMatrix(MatrixDrive_GetMatrix(), p->mtx);
    MatrixDrive_RotMatrixZ(0x4000);
    MatrixDrive_RotMatrixX(0x4000);
    CopyVector(&base, MatrixDrive_GetMatrix()[3]);
    GetRootQuaternion(&rq, self);
    _ApplyMatrix(&axis, MatrixDrive_GetMatrix(), &ap1AttackAxis);
    MatrixDrive_SetTransposeMatrix(tm.m, MatrixDrive_GetMatrix());

    for (i = 0; i < 4; i++) {
        AP1Part *part = &p->part[i];
        float len;

        switch (part->state) {
        case 1: {
            float t = (float)part->count / 10.0f;

            CopyVector(&w, &part->home);
            w.m[1] -= (t < 0.5f) ? t * 2.0f * 20.0f : (1.0f - t) * 2.0f * 20.0f;
            _ApplyMatrix(&q, p->root, &w);
            _InterVectorXYZ(&part->pos, &q, &part->pos, t);
            if ((part->count += 1) >= 10) {
                part->state = 0;
            }
            break;
        }
        case 2: {
            float t = (float)part->count / 10.0f;

            MatrixDrive_PushMatrix();
            _UnitMatrix(MatrixDrive_GetMatrix());
            {
                float f = t * 49152.0f + -32768.0f;

                MatrixDrive_RotMatrixY((short)(int)(i == 0 ? -f : f));
            }
            _ApplyMatrix(&v, MatrixDrive_GetMatrix(), &part->home);
            v.m[3] = 1.0f;
            _ApplyMatrix(&dir, p->root, &v);
            _InterVectorXYZ(&part->pos, &dir, &part->pos, t);
            MatrixDrive_PopMatrix();
            if ((part->count += 1) >= 10) {
                ExecuteSEPackage(self, 0x68);
                CopyVector(&part->home, (char *)ap1PartOffset + i * 0x10);
                part->state = 1;
                part->count = 0;
            }
            break;
        }
        }

        MatrixDrive_PushMatrix();

        len = _GetLength(&base, &part->pos);
        ang = armCosine(len, 50.0f, 50.0f);
        _SubVector(&dir, &part->pos, &base);
        _NormalizeVector(&dir, &dir);

        if (part->state == 2) {
            _OuterProduct(&w, &dir, &axis);
        } else {
            _OuterProduct(&w, &dir, &axis);
        }
        _ApplyMatrix(&lv, &tm, &dir);
        MatrixDrive_TurnXObjectMatrixYZ(lv.m[0], lv.m[1], lv.m[2]);

        _ScaleVector(&v, &dir, 50.0f);
        SetQuaternionByAxisRotateV(&q, (short)-ang, &w);
        GetMatrixFromQuaternion(&rm, &q);
        _ApplyMatrix(&v, &rm, &v);
        _AddVectorXYZ(&part->knee, &base, &v);

        CopyVector(&save, MatrixDrive_GetMatrix()[3]);
        _MulMatrix(MatrixDrive_GetMatrix(), &rm, MatrixDrive_GetMatrix());
        CopyVector(MatrixDrive_GetMatrix()[3], &save);

        if (p->skel != 0) {
            _MulMatrix((char *)GOBJ_SUB(self)->nodeMtx + ((&p->focus[1])[i * 2] << 6),
                       MatrixDrive_GetMatrix(), ap1ArmScale);
        } else {
            _MulMatrix((char *)p->arm[0]->nodeMtx + (i << 6), MatrixDrive_GetMatrix(), ap1ArmScale);
        }

        _ScaleVector(&v2, &dir, 50.0f);
        SetQuaternionByAxisRotateV(&q, (short)ang, &w);
        GetMatrixFromQuaternion(&rm, &q);
        _ApplyMatrix(&v2, &rm, &v2);
        _AddVectorXYZ(&part->tip, &part->knee, &v2);

        _MulMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix(), ap1ArmOffset);

        CopyVector(&save2, MatrixDrive_GetMatrix()[3]);
        _MulMatrix(MatrixDrive_GetMatrix(), &rm, MatrixDrive_GetMatrix());
        _MulMatrix(MatrixDrive_GetMatrix(), &rm, MatrixDrive_GetMatrix());
        CopyVector(MatrixDrive_GetMatrix()[3], &save2);

        if (part->state == 2) {
            atk = attackCenterOffset;
            _ApplyMatrix(&atk, MatrixDrive_GetMatrix(), &atk);
            _AttackCenter(self, -1, atk.m, 0, 30.0f, 0);
        }

        if (p->skel != 0) {
            _MulMatrix((char *)GOBJ_SUB(self)->nodeMtx + ((&p->focus[2])[i * 2] << 6),
                       MatrixDrive_GetMatrix(), ap1ArmScale);
        } else {
            _MulMatrix((char *)p->arm[1]->nodeMtx + (i << 6), MatrixDrive_GetMatrix(), ap1ArmScale);
        }

        MatrixDrive_PopMatrix();
    }
}

/* as in quaternion.h, which this file does not include */
extern void RotQuaternionX(void *q, short ang);
/* as in quaternion.h, which this file does not include */
extern void RotQuaternionZ(void *q, short ang);
/* void (void *, void *, void *) here, void (float *, float *, float *) in quaternion.h */
extern void GetMatrixFromQuaternionPos(void *m, void *q, void *pos);
/* as in quaternion.h, which this file does not include */
extern void GetSlerpQuaternion(void *dst, void *a, void *b, float t);

static void updateMatrix(GObj *self)
{
    float pos[4];
    float quat[4];
    float mtx[16];
    AP1Work *p = GOBJ_SUB(self)->work;

    CopyVector(GOBJ_SUB(self)->root.last, GOBJ_SUB(self)->root.pos);
    UpdateRootMatrix(self);
    CopyMatrix(p->root, (void *)GOBJ_SUB(self)->nodeMtx);

    ap1BodyPos[1] = ((float)p->blink * 0.03125f < 0.5f)
                        ? ((float)p->blink * 0.03125f) * 2.0f * 5.0f + -10.0f
                        : (1.0f - (float)p->blink * 0.03125f) * 2.0f * 5.0f + -10.0f;
    ap1BodyPos[1] -= p->sink * 25.0f;
    ap1BodyPos[2] = p->sink * 50.0f;

    GetRootPosition(pos, self);
    GetRootQuaternion(quat, self);

    RotQuaternionX(quat, (short)(int)(p->sink * 8192.0f));
    RotQuaternionX(quat, (short)(int)(p->tilt * 4096.0f));
    GetMatrixFromQuaternionPos(mtx, quat, pos);
    _ApplyMatrix(pos, mtx, ap1BodyPos);
    RotQuaternionZ(quat, (short)(int)(-p->roll * 2048.0f));
    _InterVectorXYZ(&p->pos, pos, &p->pos, 0.5f);
    GetSlerpQuaternion(&p->quat, quat, &p->quat, 0.1f);
    GetMatrixFromQuaternionPos(p->mtx, &p->quat, &p->pos);
    _MulMatrix((void *)GOBJ_SUB(self)->nodeMtx, p->mtx, ap1BodyMatrix);
}

static void resetPositionInfo(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    GetRootPosition(p->pos.m, self);
    GetRootQuaternion(&p->quat, self);
    ResetEnemyEye(p->eye);
}

static inline void stepAP1BlinkTimer(GObj *g) /* derived name */
{
    AP1Work *q = GOBJ_SUB(g)->work;
    int t = q->blink + 1;

    q->blink = t;
    if (t > 32) {
        q->blink = 0;
    }
}

void AP1Geo(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    float d;

    switch (p->mode) {
    default:
        if (p->settleCount < 10) {
            p->settleCount = p->settleCount + 1;
            resetPositionInfo(self);
        }
        p->mode = motFuncList[p->mode][1](self);
        stepAP1BlinkTimer(self);
        break;

    case 5:
        p->mode = 4;
        break;

    case 4:
        p->mode = 6;
        break;

    case 6:
        self->active = 0;
        break;

    case 7:
        break;
    }
    updateMatrix(self);
    calcSubMission(self);
    _MulMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(self)->nodeMtx, ap1EyeMatrix);
    UpdateEnemyEye(p->eye, MatrixDrive_GetMatrix(), 1.0f);
    if (p->skel != 0) {
        CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(self)->nodeMtx);
        MatrixDrive_RotMatrixZ(0x4000);
        MatrixDrive_RotMatrixX(0x4000);
        _MulMatrix((void *)GOBJ_SUB(self)->nodeMtx, MatrixDrive_GetMatrix(), ap1HeadScale);
    }
    d = GOBJ_SUB(self)->matrix[3][1] - *(float *)((char *)GOBJ_SUB(self)->nodeMtx + 0x34);
    if ((d < 0.0f) ? ((d = -d) > 10000.0f) : (d > 10000.0f)) {
        GOBJ_SUB(self)->ctrl.floorAttr = 0x800;
        /* EUC-JP: "fall-death request from the spider slipping free" */
        debug_StdPrintfDummy("蜘蛛の抜けによる落下死リクエスト\n");
    }
}

void AP1DL(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;

    if (p->mode < 5) {
        if (p->visible != 0) {
            p2o_SetDefaultEnviroment();
            p2o_DispVU1(self);
            if (p->skel == 0) {
                p2o_DispVU1DObjMulti(p->arm[0]);
                p2o_DispVU1DObjMulti(p->arm[1]);
            }
            DispEnemyEye(p->eye);
        }
    }
}

int GetAP1SpecType(GObj *self)
{
    return ((AP1Work *)GOBJ_SUB(self)->work)->layout;
}

void SetAP1VisualState(GObj *self, int visible)
{
    ((AP1Work *)GOBJ_SUB(self)->work)->visible = visible;
}

/* the angle passes as an int here (quaternion.h: short) */
extern void RotQuaternionY(void *q, int ang);
/* quaternion.h is not included in this file (see RotQuaternionY above) */
extern void RegularizeQuaternion(void *q);

int AP1Turn(GObj *self, short angle)
{
    Vec4A_P_1 q;
    int s = ((AP1Work *)GOBJ_SUB(self)->work)->mode;
    if (s < 6) {
        if (s >= 2)
            goto out;
    }
    GetRootQuaternion(&q, self);
    RotQuaternionY(&q, angle);
    RegularizeQuaternion(&q);
    SetRootQuaternion(self, &q);
    updateMatrix(self);
    return 1;
out:
    return 0;
}

int AP1MotReqForce(GObj *self, int mode)
{
    AP1Work *p = GOBJ_SUB(self)->work;

    p->mode = mode;
    if (motFuncList[mode][0] != 0) {
        motFuncList[mode][0](self);
    }
    return 1;
}

int AP1MotReq(GObj *self, int mode)
{
    int s = ((AP1Work *)GOBJ_SUB(self)->work)->mode;
    if (s < 6) {
        if (s >= 2)
            return 0;
    }
    AP1MotReqForce(self, mode);
    return 1;
}

int AP1JumpReq(GObj *self, int mode, void *vel)
{
    int flag;
    Sub15C *p = GOBJ_SUB(self);
    AP1Work *q = (AP1Work *)p->work;
    if (q->mode < 6) {
        if (q->mode >= 2) {
            flag = 0;
            goto check;
        }
    }
    AP1MotReqForce(self, mode);
    flag = 1;
check:
    if (flag != 0) {
        Sub15C *pp = GOBJ_SUB(self);
        AP1Work *qq = (AP1Work *)pp->work;
        _ApplyMatrix(pp->root.move, qq->root, vel);
        return 1;
    }
    return 0;
}

GObj *MakeAP1GObj(SObjSimpleSetting *setting)
{
    return CreateLayoutedGObj(62, spiderDef[setting->obj].layout, -1, 0, setting, 0, 7, 1);
}

ICO_WORD_PTR(char *) GetAP1Mode(GObj *self)
{
    return (ICO_WORD_PTR(char *))ap1ModeName[((AP1Work *)GOBJ_SUB(self)->work)->mode];
}

static int standMot(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    int ret = fitToCol(self, 0);
    if (ret != -1)
        return ret;
    p->tilt = 0.0f;
    p->roll = 0.0f;
    p->sink = 0.0f;
    return 0;
}

static int rollingMot(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    int ret = rolling(self);
    if (ret != -1)
        return ret;
    p->tilt = 0.0f;
    p->roll = 0.0f;
    p->sink = 0.0f;
    return 2;
}

typedef struct { /* field names derived */
    int state;   /* 0x00 */
    float frame; /* 0x04 */
    char pad08[24];
    Vec4A_P_1 vec; /* 0x20 */
} AP1MotCtrl;      /* derived name */

/* two helpers, each expanded twice into attackMotInit and attackMot */
static inline void setAP1MotCtrlState(AP1MotCtrl *m, int state) /* derived name */
{
    m->state = state;
    m->frame = 0.0f;
}

static inline void setAP1MotCtrlVector(AP1MotCtrl *m, Vec4A_P_1 *v) /* derived name */
{
    CopyVector(&m->vec, v);
    setAP1MotCtrlState(m, 0);
}

static void attackMotInit(GObj *self)
{
    Vec4A_P_1 pos;
    Mtx44 mtx;
    Vec4A_P_1 dir;
    AP1Work *p = GOBJ_SUB(self)->work;

    GetRootPosition(pos.m, boyGObj);
    MatrixDrive_SetTransposeMatrix(mtx.m, p->root);
    _ApplyMatrix(&dir, &mtx, &pos);
    setAP1MotCtrlVector((AP1MotCtrl *)&p->part[0], &dir);
    setAP1MotCtrlVector((AP1MotCtrl *)&p->part[1], &dir);
}

static int attackMot(GObj *self)
{
    AP1Work *p = GOBJ_SUB(self)->work;
    int ret = fitToCol(self, 0);
    if (ret != -1)
        return ret;
    p->tilt = 0.0f;
    p->roll = 0.0f;
    p->sink += 0.05f;
    if (p->sink > 1.0f) {
        setAP1MotCtrlState((AP1MotCtrl *)&p->part[0], 2);
        setAP1MotCtrlState((AP1MotCtrl *)&p->part[1], 2);
        return 0;
    }
    return 3;
}
