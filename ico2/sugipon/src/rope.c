#include "sugiCommon.h"

#ifdef ICO_HOST
/* clothAnimation.h for the ChainSet record only: the file's own prototypes of
   the chain functions (below) stay */
#define GetChainCollision hdr_GetChainCollision
#define InitChains hdr_InitChains
#define GetChainNodeID hdr_GetChainNodeID
#define SetChainExtendedWeight hdr_SetChainExtendedWeight
#define GetChainAnimation hdr_GetChainAnimation
#define TestDispChainAnimation hdr_TestDispChainAnimation

#include "clothAnimation.h"

#undef GetChainCollision
#undef InitChains
#undef GetChainNodeID
#undef SetChainExtendedWeight
#undef GetChainAnimation
#undef TestDispChainAnimation

#include <stddef.h>

#define ROPE_EX_OFS offsetof(ChainNode, ex)
#else
#define ROPE_EX_OFS 0x10
#endif

#include "rope.h"
#include "debug.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "memory.h"
#include "sugiCommon.h"
#include <libvu0.h>
#include "debug_exception.h"
#include "main.h"
#include "fieldCollision.h"
#include "DisplayP2O.h"
#include "ios.h"
#include <assert.h>

/* The chain functions are declared here, not through clothAnimation.h:
   HoldRope calls GetChainNodeID with the parameter record alone (it writes
   no $f12 before the call), where the definition takes the length as a
   second argument.  The chain system is held as a void * (RopeGeoWork). */
extern float GetChainCollision(void *sys, void *pos, float r);
extern void *InitChains(void *c);

/* The chain list the rope starts from, in clothAnimation.h's ChainCfg
   layout (unreachable here, above): two 0x50-byte records, the second
   ending the list, copied whole into the allocated list with doubleword
   moves, so the record carries 8-byte alignment (its zero doubleword at
   0x18 is spelled as one, as particleEffect.c's staging record does). */
typedef struct { /* field names derived */
    int num;     /* 0x00, the node count, -1 ends the list */
    int pad04[3];
    int node;        /* 0x10, the skeleton node the chain hangs from, -1 for none */
    float step;      /* 0x14, the length of one segment */
    long long pad18; /* 0x18 */
    float root[4];   /* 0x20, where the chain starts */
    int pad30[4];
    float weight; /* 0x40, against the extended weights */
    int pad44[3];
} RopeTemplate; /* derived name */

/* the zero vector HoldRope clears the holder's offset with, then the
   template */
static float ropeZeroVector[4] = {0.0f, 0.0f, 0.0f, 0.0f}; /* derived name */

static RopeTemplate ropeChainInit[2] = {
    /* derived name */
    {55, {0, 0, 0}, -1, 20.0f, 0, {0.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, 10.0f},
    {-1},
}; /* derived name */

typedef struct {                       /* field names derived */
    /* 0x00 */ void *chains;           /* InitChains' chain system */
    /* 0x04 */ int upperWallClimbable; /* a wall was found above the rope */
    /* 0x08 */ ObjNode wallSrc;        /* that wall's object and node */
    /* 0x10 */ FcWallEnt *wall;        /* that wall */
} RopeGeoWork;                         /* derived name */

void *InitRopeGeo(GObj *o, const float *p)
{
    RopeTemplate *c;
    Sub15C *sub;
    RopeGeoWork *w;
    int i;

    sub = o->dobj;
    w = (RopeGeoWork *)iosMallocDebug(ios_partition_sugipon, sizeof(RopeGeoWork), __FILE__, 38);
    c = (RopeTemplate *)iosMallocDebug(ios_partition_sugipon, sizeof(ropeChainInit), __FILE__, 39);

    c[0] = ropeChainInit[0];
    c[1] = ropeChainInit[1];
    c->root[0] = p[0];
    c->root[1] = p[1];
    c->root[2] = p[2];
    c->step = p[10];
    c->weight = p[8];
    c->num = (int)(p[9] / c->step);
    if (c->num == 0) {
        /* "The rope is too short. Change scale-y in the table." (EUC-JP) */
        debug_StdPrintfDummy("ロープの長さが短"
                             "すぎます。表のscale-y"
                             "を変更してくださ"
                             "い。\n");
        debug_assert(__FILE__, 51);
        __assert(__FILE__, 51, "0");
    }

    if (p[4] != 0.0f) {
        sceVu0FVECTOR v0 = {0.0f, 0.0f, -10.0f, 1.0f};
        sceVu0FVECTOR v1 = {0.0f, 0.0f, 10.0f, 1.0f};
        ClipWork cw;

        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrix(p[0], p[1] + 10.0f, p[2]);
        MatrixDrive_RotMatrixY(p[5] * 10430.378f);
        sceVu0ApplyMatrix(cw.pt[0], MatrixDrive_GetMatrix(), &v0);
        sceVu0ApplyMatrix(cw.pt[1], MatrixDrive_GetMatrix(), &v1);
        ClipWall(&cw);
        if (cw.wall.elem == 0) {
            /* "Cannot find the wall above the rope. Is the direction wrong, or
               is it placed where there is no wall?" (EUC-JP, in yellow) */
            debug_StdPrintfDummy("\033[33m鎖の上の壁を見"
                                 "付けることができ"
                                 "ません。\n方向が間"
                                 "違っているか、壁"
                                 "が無いところに置"
                                 "いていませんか?\033[m\n");
            debug_assert(__FILE__, 69);
            __assert(__FILE__, 69, "0");
        }
        w->wallSrc = cw.wall.o;
        w->wall = cw.wall.elem;
        w->upperWallClimbable = 1;
    } else {
        w->wallSrc.obj = 0;
        w->wallSrc.node = 0;
        w->wall = 0;
        w->upperWallClimbable = 0;
    }

    if (sub->nodeMtx != 0) {
        iosFree((void *)(ICO_PHYS(sub->nodeMtx)));
    }
    if (sub->nodeQuat != 0) {
        iosFree((void *)(ICO_PHYS(sub->nodeQuat)));
    }
    /* the matrix and quaternion buffers stored as pointers: int stores
       through sub->nodeMtx and nodeQuat reload c->num after each (measured) */
    DOBJ_NODEMTX_PTR(sub) = 0;
    DOBJ_NODEQUAT_PTR(sub) = 0;
    DOBJ_NODEMTX_PTR(sub) = iosMallocDebug(ios_partition_seki, (c->num - 1) * 64, __FILE__, 82);
    DOBJ_NODEQUAT_PTR(sub) = iosMallocDebug(ios_partition_seki, (c->num - 1) * 16, __FILE__, 82);
    sub->nodeNum = c->num - 1;
    if ((ICO_WORD)sub->nodes != 0) {
        iosFree((void *)(ICO_PHYS(ICO_ADDR(sub->nodes))));
    }
    sub->nodes = iosMallocDebug(ios_partition_seki, (c->num - 1) * 80, __FILE__, 82);
    for (i = 0; i < c->num - 1; i++) {
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->flags.ll &= ~1;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->flags.ll &= ~2;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->pos[0] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->pos[1] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->pos[2] = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->pos[3] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->flags.ll &= ~4;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            /* a float store, where the other expansions of this reset write
               an int */
            e->fade = 0.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->alpha = 1.0f;
        }
        {
            char *e = (char *)(i * 80 + (ICO_WORD)sub->nodes);
            *(short *)(e + 0x3A) = 0;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->scale[0] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->scale[1] = 1.0f;
        }
        {
            struct DObjNode *e = (struct DObjNode *)(i * 80 + (ICO_WORD)sub->nodes);
            e->scale[2] = 1.0f;
        }
    }
    sub->dispType = 2;
    w->chains = InitChains(c);
    sub->nodes->scale[0] = sub->nodes->scale[1] = sub->nodes->scale[2] = 1.0f;
    sub->nodes->rot[0] = sub->nodes->rot[1] = sub->nodes->rot[2] = 0;
    return w;
}

inline int CheckRopeUpperWallClimbable(int unused, GObj *rope)
{
    RopeGeoWork *w = GOBJ_SUB(rope)->work;

    return w->upperWallClimbable;
}

void SetRopeFixPoint(GObj *rope, void *pos, int flag)
{
    CopyVector(**(char ***)((char *)GOBJ_SUB(rope)->work) + 0x20, pos);
}

extern float GetChainNodeID(void *cfg);
extern int SetChainExtendedWeight(void *node, int idx, float w0, float w1);

void HoldRope(GObj *rope, GObj *holder)
{
    float v[4];
    float u[4];
    RopeGeoWork *w = GOBJ_SUB(rope)->work;
    void **sys = w->chains;
    int n = (int)GetChainNodeID(sys[0]);
    int w1 = SetChainExtendedWeight(sys[2], n, 0.0f, 100.0f);
    int w2 = SetChainExtendedWeight(sys[2], n, 100.0f, 300.0f);

    GetRootPosition(v, holder);
    CopyVector(u, GOBJ_SUB(holder)->root.move);
    CopyVector((float *)((char *)sys[2] + (w1 * 0x50 + ROPE_EX_OFS)) + 12, u);
    CopyVector((float *)((char *)sys[2] + (w2 * 0x50 + ROPE_EX_OFS)) + 12, u);
    CopyVector((float *)((char *)sys[2] + (w1 * 0x50 + ROPE_EX_OFS)) + 4, v);
    CopyVector((float *)((char *)sys[2] + (w2 * 0x50 + ROPE_EX_OFS)) + 4, v);
    CopyVector((float *)((char *)sys[2] + (w1 * 0x50 + ROPE_EX_OFS)) + 8, v);
    CopyVector((float *)((char *)sys[2] + (w2 * 0x50 + ROPE_EX_OFS)) + 8, v);
#ifdef ICO_HOST
    /* floats 9 and 13 from node + w1 * 0x50 are ex[w1].v0.y and .v1.y (ex
       starts at 0x10 on the EE, after the node's three pointers here) */
    ((ChainNode *)sys[2])->ex[w1].v0.y -= 100.0f;
    ((ChainNode *)sys[2])->ex[w1].v1.y -= 100.0f;
#else
    {
        float *q = (float *)((char *)sys[2] + w1 * 0x50);
        q[9] -= 100.0f;
        q[13] -= 100.0f;
    }
#endif
    /* read through the SubHandle union: GOBJ_SUB's int read is hoisted
       above the float stores (measured) */
    CopyVector(((SubHandle *)&holder->dobj)->sub->root.move, ropeZeroVector);
    debug_StdPrintfDummy("HOLD ROPE\n");
}

inline void ReleaseRope(void) {}

extern void GetChainAnimation(void *sys, int obj, void *mtx);

static void ropeGeo(GObj *rope)
{
    RopeGeoWork *w = GOBJ_SUB(rope)->work;

    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    if (GOBJ_SUB(rope)->parent.obj != 0) {
        sceVu0MulMatrix(MatrixDrive_GetMatrix(),
                        (char *)GOBJ_SUB(GOBJ_SUB(rope)->parent.obj)->nodeMtx +
                            (GOBJ_SUB(rope)->parent.node << 6),
                        MatrixDrive_GetMatrix());
    }
    GetChainAnimation(w->chains, 0, MatrixDrive_GetMatrix());
}

/* extra chains hung from the rope; none in the release build */
#define ROPE_EXTRA_CHAINS 0 /* derived name */

static inline void ropeChainCollision(GObj *rope) /* derived name */
{
    GObj *g = boyGObj;
    void **obj = GOBJ_SUB(rope)->work;
    float m[4];
    float w;
    int i;

    GetRootPosition(m, g);
    w = GetChainCollision(obj[0], m, 200.0f);
    if (0.0f < w) {
        GOBJ_SUB(g)->ctrl.ropeHangPos = w;
        /* the rope's extra chains, none in the release build */
        for (i = 0; i < ROPE_EXTRA_CHAINS; i++) {
            w = GetChainCollision(obj[i + 1], m, w);
        }
    }
}

inline void RopeGeo(GObj *rope)
{
    ropeGeo(rope);
    ropeChainCollision(rope);
}

extern void TestDispChainAnimation(void *sys);

void RopeDL(GObj *rope)
{
    unsigned short ax;
    unsigned short az;
    Sub15C *sub = rope->dobj;
    RopeGeoWork *w = sub->work;
    char *set = w->chains;
    int i;
    int j;
    int n;
    float (*v)[4];

    p2o_SetDefaultEnviroment();
#ifdef ICO_HOST
    for (i = 0; i < ((ChainSet *)set)->num; i++) {
        n = ((ChainSet *)set)->cfg[i].num;
        v = ((ChainSet *)set)->nodes[i].pos;
#else
    for (i = 0; i < *(int *)(set + 4); i++) {
        n = *(int *)(*(char **)set + i * 0x50);
        v = (float (*)[4]) * (int *)(*(char **)(set + 8) + i * 0x1A0);
#endif
        for (j = 1; j < n; j++) {
            sceVu0UnitMatrix(MatrixDrive_GetMatrix());
            MatrixDrive_TransMatrix(v[j][0], v[j][1], v[j][2]);
            MatrixDrive_GetTurnYAngleXZ(&ax, &az, v[j][0] - v[j - 1][0], v[j][1] - v[j - 1][1],
                                        v[j][2] - v[j - 1][2]);
            MatrixDrive_RotMatrixX(-ax);
            MatrixDrive_RotMatrixZ(-az);
            MatrixDrive_RotMatrixX(-0x8000);
            MatrixDrive_ScaleMatrix(1.0f, 1.0f, 1.0f);
            CopyMatrix((char *)sub->nodeMtx + (j * 64 - 64), MatrixDrive_GetMatrix());
        }
        p2o_DispVU1DObjMulti(sub);
    }
    if (debug_skel_flag != 0) {
        TestDispChainAnimation(w->chains);
    }
}
