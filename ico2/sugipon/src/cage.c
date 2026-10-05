#include "sugiCommon.h"
#include "cage.h"
#include "box.h"
#include "gobj.h"
#include "DisplayP2O.h"
#include "clothAnimation.h"
#include "quaternion.h"
#include "tableSin.h"
#include <libvu0.h>
#include <math.h>
#include "ios.h"
#include "Matrix.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "memory.h"
#include "windField.h"
#include "sceneManager.h"
#include "DObj.h"

/* the cage's 80-byte work record, InitCageGeo's allocation, kept at +0x830
   of the object's motion work: the cage and chain DObjs, the cage's rotation
   and turn, the chains InitChains builds and the swing state */
typedef struct {   /* field names derived */
    Sub15C *dobj;  /* 0x00 */
    Sub15C *dobj2; /* 0x04 */
    char pad08[8];
    float rot[4];     /* 0x10 */
    ChainSet *chains; /* 0x20 */
    int upperNode;    /* 0x24, the chain node weighted over 0 to 600 */
    int lowerNode;    /* 0x28, the chain node weighted over 500 to 1400 */
    int linkCount;    /* 0x2C, the chain's length over linkLength, the DObj node count */
    float linkLength; /* 0x30 */
    short angle;      /* 0x34 */
    short pad36;
    float mass;    /* 0x38, divides the impulse a rider gives */
    float damping; /* 0x3C, the per-frame velocity scale */
    int rideable;  /* 0x40, CageRideFunc's answer */
    char pad44[12];
} CageWork; /* derived name */

int CageRideFunc(ObjNode *self, GObj *rider)
{
    float v[4];
    float n[4];
    CageWork *w;
    float d;
    float t;

    w = GOBJ_SUB(self->obj)->work;
    CopyVector(v, GOBJ_SUB(rider)->root.pos);
    v[1] = v[1] - 250.0f;
    sceVu0Normalize(n, v);
    d = FSqrt(n[0] * n[0] + n[2] * n[2]) * 50.0f;
    t = VectorLength(v) * d / 250.0f;
    if (t < 0.0f) {
        t = -t;
    }
    v[1] = 0.0f;
    sceVu0ScaleVector(v, v, t * 0.06f / w->mass);

    sceVu0AddVector(&w->chains->nodes->ex[w->upperNode].v2, &w->chains->nodes->ex[w->upperNode].v2,
                    v);

    sceVu0SubVector(&w->chains->nodes->ex[w->lowerNode].v2, &w->chains->nodes->ex[w->lowerNode].v2,
                    v);

    return 1;
}

void SetCageFixGeometry(GObj *self, void *pos, void *dir)
{
    CageWork *w = GOBJ_SUB(self)->work;

    CopyVector(w->chains->cfg->pm.root, pos);
    CopyVector(w->rot, dir);
}

inline int GetCageChainPoint(float *root, float *tip, GObj *self)
{
    CageWork *w = GOBJ_SUB(self)->work;
    CopyVector(root, w->chains->nodes->pos[0]);
    CopyVector(tip, w->chains->nodes->pos[1]);
    root[1] = root[1] + 50.0f;
    tip[1] = tip[1] - 150.0f;
    return w->rideable;
}

/* the cage's one chain, 2 nodes 500 apart hanging from the cage root */
static ChainCfg cageChainParam[2] = {
    {2, {0, 0, 0}, {-1, 500.0f, {0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, 100.0f}},
    {-1},
}; /* derived name */

char *InitCageGeo(char *self, SObjSimpleSetting *lay)
{
    CageWork *w;
    ChainCfg *ch;
    int i;
    float one;

    w = (CageWork *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(CageWork, 80), __FILE__, 97);
    ch = iosMallocDebug(ios_partition_sugipon, 160, __FILE__, 98);
    w->dobj = CSVSYSTEM_InitDObj(accessary[SUBHANDLE_OF(self)->sub->accessary].model, lay);
    w->dobj2 = CSVSYSTEM_InitDObj(accessary[SUBHANDLE_OF(self)->sub->accessary].model2, lay);
    w->damping = 0.995f;
    w->mass = lay->scale[0];
    ch[0] = cageChainParam[0];
    ch[1] = cageChainParam[1];
    ch->pm.root[0] = lay->pos[0];
    ch->pm.root[1] = lay->pos[1];
    ch->pm.root[2] = lay->pos[2];
    ch->pm.step = lay->scale[1];
    SetIdentityQuaternion(w->rot);
    w->chains = InitChains(ch);
    w->upperNode = SetChainExtendedWeight(w->chains->nodes, 1, 0.0f, 600.0f);
    w->lowerNode = SetChainExtendedWeight(w->chains->nodes, 1, 500.0f, 1400.0f);
    w->angle = (short)(-lay->rot[1] * 10430.378f);
    w->rideable = 1;
    one = 1.0f;
    {
        struct DObjNode *e = SUBHANDLE_OF(self)->sub->nodes;

        e->scale[0] = e->scale[1] = e->scale[2] = one;
    }
    {
        struct DObjNode *e = SUBHANDLE_OF(self)->sub->nodes;

        e->rot[0] = e->rot[1] = e->rot[2] = 0;
    }
    w->linkLength = lay->scale[2];
    w->linkCount = (int)(lay->scale[1] / w->linkLength);

    /* from here to the 0x84C store: the DObj buffer reallocation, in the
       statement order box.c, boy.c and omori's chain.c also use */
    if (DOBJ_NODEMTX_PTR(w->dobj) != 0) {
        iosFree((void *)ICO_PHYS(ICO_ADDR(DOBJ_NODEMTX_PTR(w->dobj))));
    }
    if (w->dobj->nodeQuat != 0) {
        iosFree((void *)(ICO_PHYS(w->dobj->nodeQuat)));
    }
    DOBJ_NODEQUAT_PTR(w->dobj) = DOBJ_NODEMTX_PTR(w->dobj) = 0;
    DOBJ_NODEMTX_PTR(w->dobj) =
        iosMallocDebug(ios_partition_seki, w->linkCount << 6, __FILE__, 128);
    DOBJ_NODEQUAT_PTR(w->dobj) =
        iosMallocDebug(ios_partition_seki, w->linkCount << 4, __FILE__, 128);
    w->dobj->nodeNum = w->linkCount;
    if (w->dobj->nodes != 0) {
        iosFree((void *)(ICO_PHYS(ICO_ADDR(w->dobj->nodes))));
    }
    w->dobj->nodes = iosMallocDebug(ios_partition_seki, w->linkCount * 80, __FILE__, 128);
    for (i = 0; i < w->linkCount; i++) {
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->flags.ll &= ~1;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->flags.ll &= ~2;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->pos[0] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->pos[1] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->pos[2] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->pos[3] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->flags.ll &= ~4;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->fade = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->alpha = 1.0f;
        }
        {
            char *e = (char *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            *(short *)(e + 0x3A) = 0;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->scale[0] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->scale[1] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)w->dobj->nodes);
            e->scale[2] = 1.0f;
        }
    }
    w->dobj->dispType = 2;
    SUBHANDLE_OF(self)->sub->rideFunc = CageRideFunc;
    return (char *)w;
}

inline void SetCageChainHangableFlag(GObj *self, int flag)
{
    ((CageWork *)GOBJ_SUB(self)->work)->rideable = flag;
}

void HotInitCageGeo(GObj *self)
{
    CageWork *w = GOBJ_SUB(self)->work;

    CopyVector(&w->chains->nodes->ex[w->upperNode].v2, ZeroVector);
    CopyVector(&w->chains->nodes->ex[w->lowerNode].v2, ZeroVector);

    CopyVector(&w->chains->nodes->ex[w->upperNode].v1, w->chains->cfg->pm.root);
    CopyVector(&w->chains->nodes->ex[w->lowerNode].v1, w->chains->cfg->pm.root);

    CopyVector(w->chains->nodes->pos[0], w->chains->cfg->pm.root);
    CopyVector(w->chains->nodes->pos[1], w->chains->cfg->pm.root);

    w->chains->nodes->pos[1][1] = w->chains->nodes->pos[1][1] + w->linkLength * (float)w->linkCount;

    w->chains->nodes->ex[w->upperNode].v1.y =
        w->chains->nodes->ex[w->upperNode].v1.y + w->linkLength * (float)w->linkCount;

    w->chains->nodes->ex[w->lowerNode].v1.y =
        w->chains->nodes->ex[w->lowerNode].v1.y + (w->linkLength * (float)w->linkCount + 500.0f);
}

inline void StabilizeAllLayoutedCage(void)
{
    void *gobj;

    gobj = isysGObjSearchFromObjKindID_begin(44);
    while (gobj != 0) {
        HotInitCageGeo(gobj);
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }
}

inline void SetCageVelocityFriction(GObj *self, float friction)
{
    ((CageWork *)GOBJ_SUB(self)->work)->damping = friction;
}

/* the world down axis the chain's swing axis is taken against */
static sceVu0FVECTOR cageDown = {0.0f, -1.0f, 0.0f, 0.0f}; /* derived name */

static inline void SetCageChainQuaternion(void *q, void *a, void *b) /* derived name */
{
    float d[4];
    float n[4];
    float axis[4];

    sceVu0SubVector(d, a, b);
    sceVu0Normalize(d, d);
    CopyVector(n, d);
    n[1] = 0.0f;
    sceVu0OuterProduct(axis, cageDown, n);
    SetQuaternionByAxisRotateV(
        q, (short)(atan2f(FSqrt(d[0] * d[0] + d[2] * d[2]), d[1]) * 10430.378f), axis);
}

static inline void AddCageWindForce(ExW *n, float k) /* derived name */
{
    float v[4];

    CopyVector(v, GetWindVector(0, &n->v1.x));
    _ScaleVector(v, v, k / n->len1);
    _AddVector(&n->v2, &n->v2, v);
}

void CageGeo(GObj *self)
{
    CageWork *w;
    ExW *n0;
    ExW *n1;
    int i;

    w = GOBJ_SUB(self)->work;

    n0 = &w->chains->nodes->ex[w->upperNode];
    n1 = &w->chains->nodes->ex[w->lowerNode];
    AddCageWindForce(n0, 1.0f);
    AddCageWindForce(n1, 10.0f);

    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    GetChainAnimation(w->chains, 0, MatrixDrive_GetMatrix());

    {
        float v[4];
        int angle;
        float f;

        sceVu0SubVector(v, &w->chains->nodes->ex[w->lowerNode].v1,
                        &w->chains->nodes->ex[w->upperNode].v1);
        sceVu0Normalize(v, v);
        angle = GetTableArcTan2(FSqrt(v[0] * v[0] + v[2] * v[2]), v[1]);
        if (angle >= 2731) {
            f = (float)angle / 2730.0f;
            v[1] = 0.0f;
            sceVu0ScaleVector(v, v, f);
            sceVu0SubVector(&w->chains->nodes->ex[w->lowerNode].v2,
                            &w->chains->nodes->ex[w->lowerNode].v2, v);
            sceVu0AddVector(&w->chains->nodes->ex[w->upperNode].v2,
                            &w->chains->nodes->ex[w->upperNode].v2, v);
        }
    }

    sceVu0ScaleVectorXYZ(&w->chains->nodes->ex[w->lowerNode].v2,
                         &w->chains->nodes->ex[w->lowerNode].v2, w->damping);
    sceVu0ScaleVectorXYZ(&w->chains->nodes->ex[w->upperNode].v2,
                         &w->chains->nodes->ex[w->upperNode].v2, w->damping);

    SetCageChainQuaternion((char *)GOBJ_SUB(self)->nodeQuat, &w->chains->nodes->ex[w->lowerNode].v1,
                           &w->chains->nodes->ex[w->upperNode].v1);
    RotQuaternionY((char *)GOBJ_SUB(self)->nodeQuat, w->angle);
    MultiQuaternion((char *)GOBJ_SUB(self)->nodeQuat, (char *)GOBJ_SUB(self)->nodeQuat, w->rot);
    RegularizeQuaternion((char *)GOBJ_SUB(self)->nodeQuat);
    GetMatrixFromQuaternionPos(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(self)->nodeQuat,
                               &w->chains->nodes->ex[w->upperNode].v1);
    MatrixDrive_TransMatrix(0.0f, 0.0f, 0.0f);
    CopyMatrix((char *)GOBJ_SUB(self)->nodeMtx, MatrixDrive_GetMatrix());

    {
        float q[4];

        SetCageChainQuaternion(q, w->chains->nodes->pos[1], w->chains->nodes->pos[0]);
        GetMatrixFromQuaternionPos(MatrixDrive_GetMatrix(), q, w->chains->nodes->pos[0]);
        CopyVector(MatrixDrive_GetMatrix()[3], w->chains->nodes->pos[1]);
    }
    MatrixDrive_TransMatrix(0.0f, -(w->linkLength * 0.5f - 20.0f), 0.0f);

    for (i = 0; i < w->linkCount; i++) {
        MatrixDrive_PushMatrix();
        MatrixDrive_RotMatrixX(-32768);
        CopyMatrix((char *)w->dobj->nodeMtx + i * 64, MatrixDrive_GetMatrix());
        MatrixDrive_PopMatrix();
        MatrixDrive_TransMatrix(0.0f, -w->linkLength, 0.0f);
    }
}

void CageDL(GObj *self)
{
    CageWork *w = GOBJ_SUB(self)->work;

    p2o_DispVU1(self);
    p2o_DispVU1DObjMulti(w->dobj);
}
