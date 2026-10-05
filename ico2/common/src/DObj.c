#include "charFileManager.h"

#ifdef ICO_HOST

#include <string.h>

#endif

#include "debug.h"
#include "fieldCollision.h"
#include "memory.h"
#include "gv.h"
#include "Matrix.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include "quaternion.h"
#include "ios.h"
#include "DObj.h"
#include "DisplayP2O.h"
#include "Light.h"

/* The object initGeometryState builds on its stack to hand SetMotionDirection
   the display object at 0x15C.  The word there is a union, so each read of it
   follows the stores made through it; as a GObj with its Sub15C * field the
   reads merge and initGeometryState is 0x58 bytes shorter (measured). */
typedef union { /* field names derived */
    Sub15C *d;
    int i;
    float f;
} DObjWord; /* derived name */

#ifdef ICO_HOST

/* the stand-in is a real GObj on the host (its dobj is not at 0x15C there) */
typedef GObj DObjGObj;

#define DOBJ_D(p) ((p)->dobj)
#else

typedef struct { /* field names derived */
    char pad[348];
    DObjWord data;
    char pad2[32];
} DObjGObj; /* derived name */

#define DOBJ_D(p) ((p)->data.d)
#endif

typedef union { /* field names derived */
    int i[8];
    long long w[4];
} DObjBlk20; /* derived name */

typedef struct { /* field names derived */
    long long w[2];
} DObjBlk10; /* derived name */

/* The unit +Z direction the motion state starts from, with a long long view
   for its 8-byte copy. */
typedef union { /* field names derived */
    float f[4];
    long long w[2];
} DObjVec; /* derived name */

typedef union { /* field names derived */
    float q[4][4];
    long long w[8];
} DObjBlk40; /* derived name */

/* The four DObj templates (names derived).  The record is 0x880 bytes, the
   size CSVSYSTEM_InitDObj allocates before the copy; its named fields are the
   words the template sets.  The slot table pointer at 0x840 and the character
   file id at 0x84 are named from this file; the parent link at 0x00, the
   collision words at 0x70 to 0x80 and the accessary row at 0x844 as Sub15C
   (typedef.h) names the same offsets.  The long long pads give the record the
   8-byte alignment its copy loop uses. */
typedef struct { /* field names derived */
    ObjNode parent;
    long long pad08[13];
    int colData;
    int disp;
    int colRotate;
    int cylinderOn;
    int colPerNode;
    int charFileId;
    long long pad88[247];
    char *slotTable;
    int accessary;
    long long pad848[7];
} DObjRecord; /* derived name */

#ifndef ICO_HOST

static DObjRecord emptyDObj = {
    {0, -1}, {0}, 0, 1, 1, 1, 0, 1552, {0}, 0, -1, {0},
}; /* derived name */

#endif

/* One entry of the rotation element array at 0x80c: a zero vector then
   three identity quaternions. */
static DObjBlk40 initialRotElem = {{
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}}; /* derived name */

/* The 32-byte record at 0x660 that closes initGeometryState. */
static DObjBlk20 initialGeoState = {{1, 0, 0, 0, 0, 0, 0, 0}}; /* derived name */

/* The motion record at 0x680, cleared before the motion buffers. */
static DObjMotion initialGeoWork = {{0}}; /* derived name */

/* One entry of the blend rotation array at 0x818: four identity
   quaternions. */
static DObjBlk40 initialBlendRot = {{
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}}; /* derived name */

#ifdef ICO_HOST

#include "ee_view.h"

/* PC port: CSVSYSTEM_InitDObj moves these blocks whole over each node's
   MotIk, over each node's blend rotation (four quaternions) and over the 32
   bytes from Sub15C.streamScale (streamScale, pad664, streamOfs, pad67C, up
   to motion); the host layouts must agree (tools/template_audit.py). */
ICO_LAYOUT_SIZE(DObjBlk40, MotIk);

_Static_assert(sizeof(DObjBlk40) == sizeof(*((Sub15C *)0)->blendRot),
               "DObjBlk40 is not one blend rotation");

_Static_assert(sizeof(DObjBlk20) ==
                   __builtin_offsetof(Sub15C, motion) - __builtin_offsetof(Sub15C, streamScale),
               "DObjBlk20 is not Sub15C's streamScale to motion");

#endif

static inline void initGeometryScaleRatio(Sub15C *d) /* derived name */
{
    float r;
    float t;

    r = 1.0f;
    if (d->skel != 0) {
        t = (d->nodes->scale[0] + d->nodes->scale[1] + d->nodes->scale[2]) * 0.333f;
        r = 1.0f / (d->skel->pos[1] * t * 2.0f * 1.5f);
    }
    d->scaleRatio = r;
}

static void initGeometryState(Sub15C *self, SObjSimpleSetting *lay)
{
    DObjGObj g;
    DObjGObj *p;
    int i;
    int j;
    int k;
    int m;
    int n;

    p = &g;
    DOBJ_D(&g) = self;
    InitMotionGeoInfo(&self->root, lay->pos[0], lay->pos[1], lay->pos[2], lay->rot[0], lay->rot[1],
                      lay->rot[2]);
    InitMotionStateInfo(&DOBJ_D(p)->ctrl);
    InitFrameDependSequence(DOBJ_D(p)->fdsFlags);
    DOBJ_D(p)->motion = initialGeoWork;

    if (DOBJ_D(p)->skel != 0) {
        DOBJ_D(p)->root.plane.f[3] =
            DOBJ_D(p)->root.plane.f[3] + DOBJ_D(p)->skel->pos[1] * self->nodes->scale[0];
        DOBJ_D(p)->blendBuf =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 5, __FILE__, 125);
        DOBJ_D(p)->motionBuf =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 5, __FILE__, 127);
        InitMotionRotElem(DOBJ_D(p)->blendBuf, DOBJ_D(p)->skelNodeNum);
        InitMotionRotElem(DOBJ_D(p)->motionBuf, DOBJ_D(p)->skelNodeNum);
        CopyVector(DOBJ_D(p)->localPos, ZeroPoint);
        CopyVector(DOBJ_D(p)->localPos, ZeroPoint);
        CopyVector(DOBJ_D(p)->localMove, ZeroVector);
        DOBJ_D(p)->localHeight = 0.0f;
        DOBJ_D(p)->local = InitialObjPointer;
        DOBJ_D(p)->nodeRotElem =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 6, __FILE__, 137);
        for (i = 0; i < DOBJ_D(p)->skelNodeNum; i++) {
            *(DObjBlk40 *)&DOBJ_D(p)->nodeRotElem[i] = initialRotElem;
        }
        DOBJ_D(p)->nodeLimit =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 2, __FILE__, 145);
        for (j = 0; j < DOBJ_D(p)->skelNodeNum; j++) {
            DOBJ_D(p)->nodeLimit[j] = 0;
        }
        DOBJ_D(p)->nodeVec =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 4, __FILE__, 153);
        for (k = 0; k < DOBJ_D(p)->skelNodeNum; k++) {
            CopyVector(DOBJ_D(p)->nodeVec[k], ZeroVector);
        }
        DOBJ_D(p)->blendRot =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum << 6, __FILE__, 161);
        for (m = 0; m < DOBJ_D(p)->skelNodeNum; m++) {
            *(DObjBlk40 *)DOBJ_D(p)->blendRot[m] = initialBlendRot;
        }
        DOBJ_D(p)->blendless =
            iosMallocDebug(ios_partition_sugipon, DOBJ_D(p)->skelNodeNum, __FILE__, 169);
        for (n = 0; n < DOBJ_D(p)->skelNodeNum; n++) {
            DOBJ_D(p)->blendless[n] = 0;
        }

        {
            DObjVec dir = {{0.0f, 0.0f, 1.0f, 1.0f}};
            _ApplyRyGV(dir.f, -lay->rot[1]);
            SetMotionDirection(p, dir.f);
        }
    } else {
        DOBJ_D(p)->blendBuf = 0;
        DOBJ_D(p)->nodeRotElem = 0;
        DOBJ_D(p)->nodeLimit = 0;
        DOBJ_D(p)->nodeVec = 0;
        DOBJ_D(p)->blendRot = 0;
        DOBJ_D(p)->blendless = 0;
    }
    DOBJ_D(p)->rideFunc = 0;
    *(DObjBlk20 *)&DOBJ_D(p)->streamScale = initialGeoState;
    initGeometryScaleRatio(DOBJ_D(p));
}

static void initMatrixDObj(Sub15C *self, SObjSimpleSetting *lay)
{
    float v[4];
    float d;

    MatrixDrive_PushMatrix();
    CopyVector(v, lay->pos);
    d = 3.1415927f;
    v[3] = 1.0f;
    _UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrixV(v);
    MatrixDrive_RotMatrixY((short)(lay->rot[1] * 32768.0f / d));
    MatrixDrive_RotMatrixX((short)(lay->rot[0] * 32768.0f / d));
    MatrixDrive_RotMatrixZ((short)(lay->rot[2] * 32768.0f / d));
    CopyMatrix(self->matrix, MatrixDrive_GetMatrix());

    SetIdentityQuaternion(self->quat);
    RotQuaternionY(self->quat, (short)(lay->rot[1] * 32768.0f / d));
    RotQuaternionX(self->quat, (short)(lay->rot[0] * 32768.0f / d));
    RotQuaternionZ(self->quat, (short)(lay->rot[2] * 32768.0f / d));
    MatrixDrive_PopMatrix();
}

typedef struct DObjNode DObjNode; /* derived name */

#ifdef ICO_HOST

/* the node records are 80 bytes on every host (no pointers); the node is
   addressed by index instead of an int sum */
_Static_assert(sizeof(struct DObjNode) == 80, "DObjNode is 80 bytes");

#define DOBJ_NODE_AT(self, i) (&(self)->nodes[i])
#else
#define DOBJ_NODE_AT(self, i) ((DObjNode *)((i) * 80 + (int)(self)->nodes))
#endif

/* A block's node is addressed as an int sum, the index first: the ROM adds
   in that order, and &nodes[i] or nodes + i put the base first (measured). */
static void allocObjectData(Sub15C *self, SObjSimpleSetting *lay, int n)
{
    int i;
    int j;
    int k;

    self->nodes = iosMallocDebug(ios_partition_seki, n * 80, __FILE__, 299);
    for (i = 0; i < n; i++) {
        self->nodes[i].flags.ll &= ~1;
        self->nodes[i].flags.ll &= ~2;
        {
            DObjNode *e = DOBJ_NODE_AT(self, i);
            e->flags.ll &= ~4;
            e->pos[1] = 0.0f;
            e->pos[2] = 0.0f;
            e->pos[3] = 1.0f;
            e->pos[0] = 0.0f;
        }
        {
            DObjNode *e = DOBJ_NODE_AT(self, i);
            e->fade = 0.0f;
            e->alpha = 1.0f;
            *(short *)((char *)e + 0x3A) = 0;
            e->scale[0] = 1.0f;
            e->scale[1] = 1.0f;
            e->scale[2] = 1.0f;
        }
    }
    for (j = 0; j < n; j++) {
        for (k = 0; k < 4; k++) {
            self->nodes[j].rot[k] = 0;
            self->nodes[j].word10[k] = 0;
        }
        self->nodes[j].flags.ll &= ~2;
        {
            DObjNode *e = DOBJ_NODE_AT(self, j);
            e->pos[0] = e->pos[1] = e->pos[2] = 0.0f;
            e->pos[3] = 1.0f;
            e->flags.ll &= ~4;
        }
        self->nodes[j].flags.ll &= ~1;
        {
            DObjNode *e = DOBJ_NODE_AT(self, j);
            e->fade = 0.0f;
            e->alpha = 1.0f;
            *(short *)((char *)e + 0x3A) = 0;
            _CopyVector(e->scale, lay->scale);
        }
    }
}

static void initInitialInverseMatrix(Sub15C *d)
{
    char *m = iosMallocDebug(ios_partition_sugipon, d->skelNodeNum << 6, __FILE__, 333);
    d->clusterMtx = m;
    GetInitialInverseMatrixByDObj(m, d);
}

static inline void initPolyHead(Sub15C *d) /* derived name */
{
    PObjModel *h;
    Sub15C *q;

    h = d->model;
    q = h->dobj;
    d->lightMtx = iosMallocDebug(ios_partition_seki, 256, __FILE__, 238);
    d->lightMtx->mode = q->lightMtx->mode;
    if (d->lightMtx->mode == 4) {
        h->mode.s.type = 3;
    } else {
        h->mode.s.type = h->disp > 0;
        if (h->parts->lineCount != 0) {
            h->mode.s.type = 2;
        }
    }
}

static inline void allocMatrixArrays(Sub15C *d, int n) /* derived name */
{
    int i;

    *(char **)&d->nodeMtx = iosMallocDebug(ios_partition_seki, n * 64, __FILE__, 259);
    *(char **)&d->nodeQuat = iosMallocDebug(ios_partition_seki, n * 16, __FILE__, 259);
    d->nodeNum = n;
    for (i = 0; i < n; i++) {
        _CopyMatrix(*(char **)&d->nodeMtx + i * 64, d->matrix);
        CopyQuaternion(*(char **)&d->nodeQuat + i * 16, d->quat);
    }
}

static inline void applySkeltonMatrices(Sub15C *d) /* derived name */
{
    int i;

    GetInitialSkeltonMatrixByDObj(d);
    for (i = 0; i < d->skelNodeNum; i++) {
        _MulMatrix(*(char **)&d->nodeMtx + i * 64, d->matrix, *(char **)&d->nodeMtx + i * 64);
    }
}

static inline void allocMorphWeights(Sub15C *d, int n) /* derived name */
{
    int i;

    d->morphWeight = iosMallocDebug(ios_partition_seki, n * 4, __FILE__, 283);
    for (i = 0; i < n; i++) {
        d->morphWeight[i] = 0.0f;
    }
}

static void initPolygonState(Sub15C *d, SObjSimpleSetting *lay)
{
    PObjModel *p;
    PObjPart *e;
    unsigned int mx;
    int j;
    int k;

    mx = 0;
    p = d->model;
    initMatrixDObj(d, lay);
    initPolyHead(d);

    k = p->mode.s.type;
    switch (k) {
    case 1:
        d->dispType = k;
        d->nodeNum = d->skelNodeNum;
        allocMatrixArrays(d, d->skelNodeNum);
        allocObjectData(d, lay, p->partCount);
        applySkeltonMatrices(d);
        break;
    case 0:
    case 2:
    case 3:
        d->dispType = 0;
        d->nodeNum = d->skelNodeNum < p->partCount ? p->partCount : d->skelNodeNum;
        allocMatrixArrays(d, d->nodeNum);
        allocObjectData(d, lay, p->partCount);
        break;
    }

    for (j = 0; j < p->partCount; j++) {
        e = &p->parts[j];
        if (mx < e->morphCount) {
            mx = e->morphCount;
        }
    }

    if (p->mode.s.type == 1) {
        if (mx != 0) {
            allocMorphWeights(d, mx);
        }
        d->morphNum = mx;
    } else {
        if (mx != 0) {
            allocMorphWeights(d, 6);
        }
        d->morphNum = mx;
    }
}

inline void FreeDObj(void) {}

/* the skeleton node of kind id, or -1 */
static inline int findSlot(Sub15C *d, int id) /* derived name */
{
    SkelNode *p;
    int k;

    p = d->skel;
    k = 0;
    while (p[k].mirror != -1) {
        if (p[k].kind == id) {
            return k;
        }
        k++;
    }
    return -1;
}

/* the 53-entry focus node table */
static inline void makeSlotTable(Sub15C *d) /* derived name */
{
    int i;

    d->focusNodes = iosMallocDebug(ios_partition_sugipon, 53, __FILE__, 440);
    for (i = 0; i < 53; i++) {
        d->focusNodes[i] = findSlot(d, i);
    }
}

Sub15C *CSVSYSTEM_InitDObj(int id, SObjSimpleSetting *lay)
{
    Sub15C *d;

#ifdef ICO_HOST
    /* the record is a runtime Sub15C here (wider than the EE's 0x880): the
       same all-zero start with the template's non-zero words by name */
    d = iosMallocDebug(ios_partition_seki, sizeof(Sub15C), __FILE__, 463);
    memset(d, 0, sizeof(Sub15C));
    d->parent.node = -1;
    d->disp = 1;
    d->colRotate = 1;
    d->cylinderOn = 1;
    d->modelId = 1552;
    d->accessary = -1;
#else
    d = iosMallocDebug(ios_partition_seki, sizeof(DObjRecord), __FILE__, 463);
    *(DObjRecord *)d = emptyDObj;
#endif
    if (id != 1552) {
        CSVSYSTEM_ReadCharFiles(d, id);
    }
    debug_StdPrintfDummy("\x1b[35mALLOCED DOBJ\x1b[m\n");
    if (d->model != 0 || d->skel != 0) {
        initPolygonState(d, lay);
    }
    debug_StdPrintfDummy(" ------------------ allocate DOBJ %p: POBJ: %p\n", d, d->model);
    debug_StdPrintfDummy("\x1b[35mINITED POLYGONSTATE\x1b[m\n");
    initGeometryState(d, lay);
    debug_StdPrintfDummy("\x1b[35mINITED GEOMETRYSTATE\x1b[m\n");
    if (d->skel != 0) {
        initInitialInverseMatrix(d);
        makeSlotTable(d);
    }
    debug_StdPrintfDummy("\x1b[35mEND OF INIT DOBJ\x1b[m\n");
    return d;
}

inline void LinkParentOfDObj(void *obj, ObjNode *link)
{
    LocalizeGeometry(obj, link);
    GOBJ_SUB(obj)->parent = *link;
}

inline void UnlinkParentOfDObj(void *obj)
{
    GlobalizeGeometry(obj);
    GOBJ_SUB(obj)->parent = InitialObjPointer;
}
