#include "debug.h"
#include "DisplayP2O.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "tableSin.h"
#include <libvu0.h>
#include <string.h>
#include <math.h>
#include "geometryManager.h"
#include "motionOrientManager.h"
#include "motionFileManager.h"
#include "Matrix.h"
#include "debug_exception.h"
#include "motionManager2.h"
#include "main.h"
#include "fieldCollision.h"
#include "gv.h"
#include "chain.h"
#include "memory.h"
#include "ios.h"
#include "commonact.h"
#include "obj_manager.h"
#include "camera-editor.h"
#include <assert.h>

static void chain_sub_pendulum(struct ChainNode *base, int n, float *pos);

/* gobj and the sixth parameter (always 0) are passed by both callers and never read */
static void chain_sub_simulate(GObj *gobj, struct ChainNode *nd, int from, int to,
                               unsigned char flag, int flag2, float grav, float len, float damp);

static void pendulum_Process(struct ChainPendulum *pdl, int flag);

typedef struct ChainNode { /* field names derived */
    float x, y, z, w;
    float vx, vy, vz, vw;
} ChainNode; /* derived name */

/* One word of a chain record or of a chain vector: the chain code writes these
 * slots as float and reads them as int (and the other way round), so the word
 * itself is a union. */
typedef union ChainVal { /* field names derived */
    int i;
    float f;
} ChainVal; /* derived name */

/* The pendulum block at 0x20 of a chain record: the swing orientation, the
 * swing angle, its amplitude, the phase within one cycle and the cycle length
 * in frames, the rope length, the amplitude change per frame, the amplitude
 * ceiling at 0x48 (360 at every restart, lowered to the swing a wall hit
 * leaves), the swing limit at 0x4C and the swinging flag at 0x50.
 * pendulum_Process's trace prints them as time, rad, max, maxl, T and d. */
typedef struct ChainPendulum { /* field names derived */
    /* 0x20 */ sceVu0FVECTOR orient;
    /* 0x30 */ float angle;
    /* 0x34 */ float amp;
    /* 0x38 */ float phase;
    /* 0x3C */ float length;
    /* 0x40 */ float cycle;
    /* 0x44 */ float ampSpeed;
    /* 0x48 */ float ampLimit;
    /* 0x4C */ float limit;
    /* 0x50 */ unsigned char swing;
} ChainPendulum; /* derived name */

/* The head of a chain record, 0xE0 bytes, the node array following it. */
typedef struct {             /* field names derived */
    /* 0x00 */ GObj *root;   /* the object the chain hangs from, or 0 */
    /* 0x04 */ int rootNode; /* the node of that object it hangs from */

    /* 0x10 */ sceVu0FVECTOR rootPos;
    /* 0x20 */ ChainPendulum pdl;
    /* 0x60 */ unsigned char hold;
    /* 0x64 */ GObj *owner;
    /* 0x68 */ int holdNode;
    /* 0x6C */ unsigned char hasDirCorrect;
    /* 0x70 */ float dirCorrect;
    /* 0x74 */ int nodes;
    /* 0x78 */ int mode; /* the simulation mode the last ChainGeo ran */
    /* 0x80 */ sceVu0FVECTOR node2Pos;
    /* 0x90 */ sceVu0FVECTOR endPos;
    /* 0xA0 */ unsigned char wallHit;
    /* 0xA4 */ ClimbCol climb; /* the wall the chain hangs against */
    /* 0xB0 */ sceVu0FVECTOR wallOrient;
    /* 0xC0 */ unsigned char stopped;
    /* 0xC4 */ int count;
    /* 0xC8 */ float hangRange;
    /* 0xCC */ unsigned char locked;
    /* 0xCD */ unsigned char hangable;
    /* 0xD0 */ ChainNode *node;
} ChainRecord; /* derived name */

/* an address in a chain's node array, byte offset first as the ROM adds them */
#ifdef ICO_HOST
#define CHAIN_NODE_ADDR(T, cw, off) ((T)((char *)(cw)->node + (off)))
#else
#define CHAIN_NODE_ADDR(T, cw, off) ((T)((off) + (int)(cw)->node))
#endif

static int UpdateRootPosition(GObj *gobj)
{
    float pos[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    ChainNode *nd;
    int moved = 0;

    if (cw->root != 0) {
        /* the translation row of the parent node's 64-byte matrix */
        SetDirectRootPosition(gobj, (char *)GOBJ_SUB(cw->root)->nodeMtx + (cw->rootNode << 6) + 48);
    }
    GetRootPosition(pos, gobj);
    if (_DistSqGV(pos, cw->rootPos) < 1.0f) {
    } else {
        moved = 1;
    }
    cw->rootPos[0] = pos[0];
    cw->rootPos[1] = pos[1];
    cw->rootPos[2] = pos[2];
    nd = cw->node;
    nd[0].x = cw->rootPos[0];
    nd[0].y = cw->rootPos[1];
    nd[0].z = cw->rootPos[2];
    cw->node2Pos[0] = nd[2].x;
    cw->node2Pos[1] = nd[2].y;
    cw->node2Pos[2] = nd[2].z;
    cw->endPos[0] = nd[cw->nodes - 1].x;
    cw->endPos[1] = nd[cw->nodes - 1].y;
    cw->endPos[2] = nd[cw->nodes - 1].z;
    return moved;
}

/* InitPendulum's body, for StartPendulum above its definition */
static inline void initPendulum(GObj *gobj) /* derived name */
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    float a = (float)debug_chain_cycle_speed * -0.2f + 2.0f;
    float y;

    a = a < 0.1f ? 0.1f : (a > 2.0f ? 2.0f : a);

    y = (float)(int)(a * 6.0f * FSqrt(cw->pdl.length / 2.5f) * 8.0f / 10.0f);

    cw->pdl.cycle = y;
    cw->pdl.cycle = cw->pdl.cycle < 1.0f ? 1.0f : (cw->pdl.cycle > 255.0f ? 255.0f : cw->pdl.cycle);

    cw->pdl.phase = cw->pdl.cycle * 0.5f;
    cw->pdl.ampLimit = 360.0f;
    cw->pdl.swing = 1;
}

/* the hold starts the swing: the holder and its hand position fix the hold
   node and the swing plane */
static void StartPendulum(GObj *gobj, GObj *owner, float *pos)
{
    float d[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    int nearestNode = -1;
    float min = 3.40282347e+38f; /* FLT_MAX */
    int i;

    sceVu0SubVector(d, pos, cw->node);
    cw->owner = owner;

    for (i = 0; i < cw->nodes; i++) {
        int n = (int)(pos[1] - (cw->node)[i].y);
        float t = (float)(n < 0 ? -n : n);

        if (t < min) {
            min = t;
            nearestNode = i;
        }
    }
    if (nearestNode == -1) {
        debug_assert(__FILE__, 563);
        __assert(__FILE__, 563, "nearestNode!=-1");
    }
    cw->holdNode = nearestNode;
    cw->holdNode =
        cw->holdNode < 2 ? 2 : (cw->nodes - 1 < cw->holdNode ? cw->nodes - 1 : cw->holdNode);

    _GetCorrectOrientOfChain(cw->pdl.orient, gobj, test_CURRENTORIENT(owner));

    ((ChainVal *)&cw->pdl.length)->f = (float)cw->holdNode * 50.0f;

    initPendulum(gobj);
}

/* the debug trace line: every chain trace steps it by 10 and ChainGeo resets
 * it */
static int chainDebugY; /* derived name */

static int collisionCheck(GObj *gobj)
{
    ClipWork w;
    float v[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (cw->pdl.swing) {
        v[0] = cw->pdl.orient[0];
        v[1] = cw->pdl.orient[1];
        v[2] = cw->pdl.orient[2];
    } else {
        sceVu0ScaleVector(v, cw->pdl.orient, -1.0f);
    }
    v[1] = 0.0f;
    sceVu0Normalize(v, v);
    debug_Arrow(200.0f, &cw->node[cw->holdNode], v, 0xFF, 0, 0xFF);
    sceVu0ScaleVector(v, v, 140.0f);
    w.pt[0][0] = cw->node[cw->holdNode].x;
    w.pt[0][1] = cw->node[cw->holdNode].y;
    w.pt[0][2] = cw->node[cw->holdNode].z;
    sceVu0AddVector(w.pt[1], w.pt[0], v);
    w.radius = 10.0f;
    ClipWall(&w);
    if (w.wall.elem) {
        if (debug_font_flag & 1) {
            chainDebugY = chainDebugY + 10;
            debug_Printf(10, chainDebugY, 0x0FFFFFFF, "collision!!!\n");
        }
        return 1;
    }
    return 0;
}

static inline void ChainPendulumSwing(float *dst, ChainRecord *cw, float *orient) /* derived name */
{
    float ang = cw->pdl.angle;
    float len = cw->pdl.length;
    float m1[16];
    float m2[16];
    float v[4];

    v[0] = 0.0f;
    v[1] = len;
    v[2] = 0.0f;
#ifdef ICO_HOST
    /* the EE left v[3] as the stack word it found: the matrix's translation
       row is zero, so the product is 0 whatever the word; a host NaN or Inf
       pattern there would poison the nodes (docs/port/DIVERGENCES.md) */
    v[3] = 0.0f;
#endif

    sceVu0UnitMatrix(m1);
    sceVu0RotMatrixX(m2, m1, ang * 3.1415927f / 180.0f);
    sceVu0RotMatrixY(
        m1, m2, (float)(int)(_GetDirection(orient) / 3.1415927f * 180.0f) * 3.1415927f / 180.0f);

    sceVu0ApplyMatrix(dst, m1, v);
}

static void chain_simulate_term_simple(GObj *gobj)
{
    float pos[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    pendulum_Process(&cw->pdl, collisionCheck(gobj));
    ChainPendulumSwing(pos, cw, cw->pdl.orient);
    sceVu0AddVector(pos, cw->node, pos);
    chain_sub_pendulum(cw->node, cw->holdNode, pos);
    chain_sub_simulate(gobj, cw->node, cw->holdNode, cw->nodes, 1, 0, 20.0f, 50.0f, 0.6f);
}

static void chain_simulate_term_ropeturn(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_ropeturn\n");
    }
    cw->pdl.ampSpeed = -0.4f;
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term_loop(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_loop\n");
    }
    if (cw->pdl.amp < 0.5) {
        cw->pdl.ampSpeed = -0.01f;
    } else if (cw->pdl.amp < 1.0) {
        cw->pdl.ampSpeed = -0.05f;
    } else {
        cw->pdl.ampSpeed = -0.15f;
    }
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term_swingready(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_swingready\n");
    }
    if (cw->pdl.amp < 0.5) {
        cw->pdl.ampSpeed = -0.29999998f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    } else if (cw->pdl.amp < 1.0) {
        cw->pdl.ampSpeed = -1.5f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    } else {
        cw->pdl.ampSpeed = -4.5f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    }
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term_swingstart(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    float h;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_swingstart\n");
    }

    h = GOBJ_SUB(boyGObj)->ctrl.animFrame;

    if (h < 20.0f) {
        if (cw->pdl.amp < 0.3) {
            cw->pdl.ampSpeed =
                -0.29999998f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
        } else if (cw->pdl.amp < 1.0) {
            cw->pdl.ampSpeed = -6.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
        } else {
            cw->pdl.ampSpeed = -9.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
        }
    } else {
        if (h >= 20.0 && h < 21.5) {
            cw->pdl.phase = 0.0f;
            cw->pdl.ampLimit = 360.0f;
        }
        cw->pdl.ampSpeed = 0.0f;
        cw->pdl.amp = 3.0f;

        cw->pdl.phase = cw->pdl.phase - 1.0f +
                        cw->pdl.cycle * 0.5f / 41.0f * 30.0f /
                            (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    }
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term_moveup(GObj *gobj)
{
    float w[4];
    float v[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    float h;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_moveup\n");
    }
    if (cw->pdl.amp < 1.0f) {
        cw->pdl.amp = 1.0f;
        cw->pdl.ampSpeed = 0.0f;
    } else if (cw->pdl.amp < 2.0) {
        cw->pdl.ampSpeed = -0.05f;
    } else {
        cw->pdl.ampSpeed = -0.15f;
    }
    chain_simulate_term_simple(gobj);
    h = GOBJ_SUB(boyGObj)->ctrl.animFrame;
    v[0] = cw->pdl.orient[0];
    v[1] = cw->pdl.orient[1];
    v[2] = cw->pdl.orient[2];
    _ApplyRyGV(v, -1.5707964f);
    sceVu0ScaleVector(v, v,
                      GetTableSin(h * 6.283185307179586 / 40.0 * 32768.0 / 3.1415927f) * 5.0f);
    sceVu0AddVector(w, &cw->node[cw->holdNode], v);
    chain_sub_pendulum(cw->node, cw->holdNode, w);
}

static void chain_simulate_term_free(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_free\n");
    }
    if (cw->pdl.amp < 0.5) {
        cw->pdl.ampSpeed = -0.01f;
    } else if (cw->pdl.amp < 2.0) {
        cw->pdl.ampSpeed = -0.05f;
    } else {
        cw->pdl.ampSpeed = -0.15f;
    }
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term_down(GObj *gobj)
{
    float w[4];
    float v[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    ChainNode *nd;
    ChainNode *next;
    float h;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        /* a 2001 copy and paste: this arm prints the sibling term's name */
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term_free\n");
    }
    if (cw->pdl.amp < 0.5) {
        cw->pdl.ampSpeed = -0.01f;
    } else if (cw->pdl.amp < 2.0) {
        cw->pdl.ampSpeed = -0.05f;
    } else {
        cw->pdl.ampSpeed = -0.15f;
    }
    chain_simulate_term_simple(gobj);
    h = GOBJ_SUB(boyGObj)->ctrl.animFrame;
    v[0] = cw->pdl.orient[0];
    v[1] = cw->pdl.orient[1];
    v[2] = cw->pdl.orient[2];
    _ApplyRyGV(v, -1.5707964f);
    sceVu0ScaleVector(v, v,
                      GetTableSin(h * 6.283185307179586 / 23.0 * 32768.0 / 3.1415927f) * 2.0f);
    sceVu0AddVector(w, &cw->node[cw->holdNode], v);
    chain_sub_pendulum(cw->node, cw->holdNode, w);
    if (cw->holdNode + 1 <= cw->nodes - 1) {
        nd = CHAIN_NODE_ADDR(ChainNode *, cw, cw->holdNode << 5);
        next = nd + 1;
        next->x = nd->x;
        next->y = nd->y + 50.0f;
        next->z = nd->z;
    }
}

static void chain_simulate_hangstart(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_hangstart\n");
    }
    cw->pdl.ampSpeed = -1.5f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    chain_simulate_term_simple(gobj);
}

static void chain_simulate_term(GObj *gobj)
{
    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_term\n");
    }
    chain_simulate_term_simple(gobj);
}

static inline void ResetChainNodes(ChainRecord *cw, float *pos) /* derived name */
{
    int i;

    for (i = 0; i < cw->nodes; i++) {
        ChainNode *e = cw->node + i;
        e->x = pos[0];
        e->y = pos[1];
        e->z = pos[2];
        e->y += (float)i * 50.0f;
        e->vx = 0.0f;
        e->vy = 0.0f;
        e->vz = 0.0f;
    }
}

static void chain_simulate_stop(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    ResetChainNodes(cw, cw->rootPos);
    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_stop\n");
    }
}

static void chain_simulate_free(GObj *gobj)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    int i;

    if (debug_font_flag & 1) {
        chainDebugY = chainDebugY + 10;
        debug_Printf(10, chainDebugY, 0x0FFFFFFF, "chain_simulate_free\n");
    }
    chain_sub_simulate(gobj, cw->node, 0, cw->nodes, 1, 0, 10.0f, 50.0f, 0.675f);
    cw->pdl.amp = 0.0f;
    for (i = 1; i < cw->nodes; i++) {
        ChainNode *nd = cw->node;
        if (nd[i].y < nd[i - 1].y) {
            nd[i].x += 3.0f;
            nd[i].y += 3.0f;
        }
    }
}

void correct_vector(float *out, float *v)
{
    float a[4];
    float u[4];

    memset(a, 0, 16);

    MatrixDrive_PushMatrix();

    a[1] = -atan2f(v[0], v[2]);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY((short)(a[1] * 32768.0f / 3.1415927f));

    v[3] = 0.0f;
    sceVu0ApplyMatrix(u, MatrixDrive_GetMatrix(), v);

    a[0] = atan2f(u[1], u[2]);

    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixX((short)(a[0] * 32768.0f / 3.1415927f));
    MatrixDrive_RotMatrixY((short)(a[1] * 32768.0f / 3.1415927f));

    out[3] = 0.0f;
    sceVu0ApplyMatrix(u, MatrixDrive_GetMatrix(), out);

    u[2] = 0.0f;
    MatrixDrive_SetTransposeMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix());

    u[3] = 0.0f;
    sceVu0ApplyMatrix(out, MatrixDrive_GetMatrix(), u);

    MatrixDrive_PopMatrix();
}

/* K&R definition: the flag is an unsigned char promoted to int. */
static void pendulum_Process(w, flag) ChainPendulum *w;

unsigned char flag;

{
    int up;

    up = w->ampSpeed > 0.0f ? 1 : 0;

    if (debug_font_flag & 1) {
        debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "time = %f\n", w->phase);
        if (debug_font_flag & 1) {
            debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "rad  = %f\n", w->angle);
            if (debug_font_flag & 1) {
                debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "max  = %f\n", w->amp);
                if (debug_font_flag & 1) {
                    debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "maxl = %f\n", w->ampLimit);
                    if (debug_font_flag & 1) {
                        debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "T    = %f\n", w->cycle);
                        if (debug_font_flag & 1) {
                            debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "d    = %f\n",
                                         w->length);
                            if (debug_font_flag & 1) {
                                debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "inc  = %d\n", up);
                            }
                        }
                    }
                }
            }
        }
    }

    if (w->length > 0.0f) {
        w->phase = w->phase + 1.0f;
        if (w->cycle <= w->phase) {
            w->phase = 0.0f;
        }

        w->amp = w->amp + w->ampSpeed;
        w->amp = w->amp < 0.0f ? 0.0f : (w->limit < w->amp ? w->limit : w->amp);
        w->ampSpeed = 0.0f;

        w->angle = -GetTableSin(((int)w->phase << 16) / (int)w->cycle) * w->amp;

        if (flag) {
            if ((w->angle < 0.0f ? -w->angle : w->angle) < w->ampLimit) {
                w->ampLimit = w->angle < 0.0f ? -w->angle : w->angle;
            }
        }

        if (w->ampLimit < w->amp) {
            w->amp = w->amp - 0.2f;
        }

        w->angle = w->angle < -w->ampLimit ? -w->ampLimit
                                           : (w->ampLimit < w->angle ? w->ampLimit : w->angle);

        if (w->cycle * 0.25 < w->phase && w->phase < w->cycle * 0.75) {
            w->swing = 1;
        } else {
            w->swing = 0;
        }
    }
}

/* the two templates a new chain geometry starts from, the pendulum block and
 * the record head */
static ChainPendulum chainPendulumDefault = {
    /* derived name */
    {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 360.0f, 45.0f, 1};

static ChainRecord chainRecordDefault = {
    /* derived name */
    0,
    0,
    {0.0f, 0.0f, 0.0f, 0.0f},
    {{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0},
    0,
    0,
    -1,
    0,
    0.0f,
    0,
    0,
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    0,
    {{0, 0}, 0},
    {0.0f, 0.0f, 0.0f, 0.0f},
    1,
    0,
    70.0f,
    0,
    1,
    0};

/* The geometry request the caller fills in: the anchor position, whether to
 * look for the wall the chain hangs against and the direction to look in, the
 * direction correction (negative for none), the hand's reach for a hang
 * (-1 keeps the default), the chain length and the swing limit. */
typedef struct { /* field names derived */
    /* 0x00 */ float pos[4];
    /* 0x10 */ float wallCheck;
    /* 0x14 */ float wallDir;
    /* 0x18 */ float dirCorrect;
    /* 0x1C */ char pad1C[4];
    /* 0x20 */ float hangRange;
    /* 0x24 */ float length;
    /* 0x28 */ float limit;
} ChainGeoReq; /* derived name */

/* the record templates, copied whole as doubleword runs */
typedef struct { /* field names derived */
    long long words[28];
} ChainRecTemplate; /* derived name */

typedef struct { /* field names derived */
    long long words[8];
} ChainPendTemplate; /* derived name */

#ifdef ICO_HOST

#include "ee_view.h"

/* PC port: the pendulum is reset by moving chainPendulumDefault through this
   block, which must cover the whole record on the host too
   (tools/template_audit.py) */
ICO_LAYOUT_SIZE(ChainPendTemplate, ChainPendulum);

#endif

/* the gobj extension pointer */
typedef union { /* field names derived */
    Sub15C *sub;
} ChainExtPtr; /* derived name */

/* the DObj entry flag word, the same union DObj.c's allocObjectData uses */
typedef union { /* field names derived */
    long long ll;
    int i[2];
} ChainDObjFlags; /* derived name */

ChainRecord *InitChainGeo(GObj *gobj, ChainGeoReq *req)
{
    ChainRecord *cw;
    int n;
    int i;

    n = (int)(req->length / 50.0f + 0.5f);

    if (n < 2) {
        /* "the chain is too short (set it with the Y-scale of the placement table)" */
        debug_StdPrintfDummy("鎖の長さが短かすぎます(配置表のY-scaleで指定します)");
        debug_assert(__FILE__, 1178);
        __assert(__FILE__, 1178, "0");
    }

    cw = iosMallocDebug(ios_partition_sugipon, (n << 5) + sizeof(ChainRecord), __FILE__, 1181);

#ifdef ICO_HOST
    /* the record is wider here (8-byte pointers): copy the typed template */
    *cw = chainRecordDefault;
#else
    *(ChainRecTemplate *)cw = *(ChainRecTemplate *)&chainRecordDefault;
#endif

    cw->nodes = n;
    cw->node = (ChainNode *)(cw + 1);
    cw->holdNode = -1;
    if (req->hangRange != -1.0f) {
        cw->hangRange = req->hangRange;
    }

    *(ChainPendTemplate *)&cw->pdl = *(ChainPendTemplate *)&chainPendulumDefault;

    cw->pdl.limit = req->limit;
    cw->pdl.limit = cw->pdl.limit < 5.0f ? 5.0f : (90.0f < cw->pdl.limit ? 90.0f : cw->pdl.limit);

    ResetChainNodes(cw, (float *)req);

    if (req->wallCheck != 0.0f) {
        sceVu0FVECTOR p0 = {0.0f, 0.0f, -25.0f, 1.0f};
        sceVu0FVECTOR p1 = {0.0f, 0.0f, 25.0f, 1.0f};
        ClipWork w;
#ifdef ICO_HOST
        /* PC port (X2, DIVERGENCES.md F15): the original sets only the two
           points, so _Clip reads the radius and the skip filter from
           whatever the stack holds, and the wall it picks (climb.wall,
           wallOrient: the climb-off's wall plane) followed the host's
           garbage. The host starts from radius 0 and no filter: the wall
           the segment crosses behind the chain. */
        memset(&w, 0, sizeof(w));
#endif
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrix(req->pos[0], req->pos[1] + 10.0f, req->pos[2]);
        MatrixDrive_RotMatrixY((short)(req->wallDir * 32768.0f / 3.1415927f));
        sceVu0ApplyMatrix(w.pt[0], MatrixDrive_GetMatrix(), p0);
        sceVu0ApplyMatrix(w.pt[1], MatrixDrive_GetMatrix(), p1);
        ClipWall(&w);
        if (w.wall.elem == 0) {
            /* "cannot find the wall above the chain. / is the direction wrong, or is
             * it placed where there is no wall?" (in yellow) */
            debug_StdPrintfDummy(
                "\033[33m鎖の上の壁を見付けることができません。\n方向が間違っているか、壁が無いところに置いていませんか?\033[m\n");
        } else {
            memcpy(&cw->climb.wallSrc, &w.wall.o, sizeof(w.wall.o));
            cw->climb.wall = w.wall.elem;
            GetOrientOfWall(cw->wallOrient, w.wall.elem, &w.wall.o);
            cw->wallHit = 1;
        }
    } else {
        cw->climb.wallSrc.obj = 0;
        cw->climb.wallSrc.node = 0;
        cw->climb.wall = 0;
        cw->wallHit = 0;
    }

    if (req->dirCorrect < 0.0f) {
        cw->hasDirCorrect = 0;
    } else {
        cw->hasDirCorrect = 1;
        cw->dirCorrect = req->dirCorrect;
    }

    if (((ChainExtPtr *)&gobj->dobj)->sub->nodeMtx != 0) {
        iosFree((void *)ICO_PHYS(((ChainExtPtr *)&gobj->dobj)->sub->nodeMtx));
    }
    if (((ChainExtPtr *)&gobj->dobj)->sub->nodeQuat != 0) {
        iosFree((void *)ICO_PHYS(((ChainExtPtr *)&gobj->dobj)->sub->nodeQuat));
    }
    /* the node matrix and quaternion buffers are stored here as the pointers
       iosMallocDebug returns; typedef.h's Sub15C holds them as words */
    *(char **)&((ChainExtPtr *)&gobj->dobj)->sub->nodeMtx = 0;

    *(char **)&((ChainExtPtr *)&gobj->dobj)->sub->nodeQuat = 0;
    *(char **)&((ChainExtPtr *)&gobj->dobj)->sub->nodeMtx =
        (char *)iosMallocDebug(ios_partition_seki, (cw->nodes - 1) << 6, __FILE__, 1245);
    *(char **)&((ChainExtPtr *)&gobj->dobj)->sub->nodeQuat =
        (char *)iosMallocDebug(ios_partition_seki, (cw->nodes - 1) << 4, __FILE__, 1245);
    ((ChainExtPtr *)&gobj->dobj)->sub->nodeNum = cw->nodes - 1;
    if (((ChainExtPtr *)&gobj->dobj)->sub->nodes != 0) {
        iosFree((void *)ICO_PHYS(ICO_ADDR(((ChainExtPtr *)&gobj->dobj)->sub->nodes)));
    }
    ((ChainExtPtr *)&gobj->dobj)->sub->nodes =
        iosMallocDebug(ios_partition_seki, (cw->nodes - 1) * 80, __FILE__, 1245);

    for (i = 0; i < cw->nodes - 1; i++) {
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].flags.ll &= ~1;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].flags.ll &= ~2;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].pos[0] = 0.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].pos[1] = 0.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].pos[2] = 0.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].pos[3] = 1.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].flags.ll &= ~4;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].fade = 0.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].alpha = 1.0f;
        ((short *)&((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].flags)[1] = 0;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].scale[0] = 1.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].scale[1] = 1.0f;
        ((ChainExtPtr *)&gobj->dobj)->sub->nodes[i].scale[2] = 1.0f;
    }

    ((ChainExtPtr *)&gobj->dobj)->sub->dispType = 2;

    return cw;
}

static void chain_set_charachara(GObj *gobj, float amp)
{
    float v[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    int deg;
    int idx;
    float c;
    float s;
    char *p;

    memset(v, 0, 16);

    deg = (int)(_GetDirection(test_CURRENTORIENT(boyGObj)) / 3.1415927f * 180.0f);
    idx = cw->holdNode + 2;

    if (cw->nodes - 2 < idx) {
        return;
    }

    c = GetTableCos(cw->count * 2000) * amp;
    s = GetTableSin(cw->count * 1500) * amp;

    v[0] = c;
    v[1] = 0.0f;
    v[2] = s;
    _ApplyRyGV(v, (float)deg * 3.1415927f / 180.0f);

    p = CHAIN_NODE_ADDR(char *, cw, idx << 5);
    *(float *)p = *(float *)(p - 32) + v[0];

    *(float *)(p + 8) = *(float *)(p - 24) + v[2];

    cw->count = cw->count + 1;
}

/* The enemy parameter table, one 404-byte row per motion id; ChainGeo reads
 * only the flag word at 0x18C.  Same record enemy_act.c reads as EnemyParaRow. */
typedef struct { /* field names derived */
    char pad00[396];
    unsigned int flags; /* bit 11: a motion the hand hangs on the chain from */
    char pad190[4];
} ChainParaRow; /* derived name */

/* the data-only member motion-def.o, read through this file's view of its
   rows; no header declares it */
extern ChainParaRow motionKind[];
static void TestChainUpDown(GObj *gobj, GObj *boy);

/* the simulation mode the boy's motion selects, for ChainGeo */
static inline int GetChainSimulateMode(GObj *gobj) /* derived name */
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    int mode = 1;

    if (cw->hold != 0) {
        GObj *holder = cw->owner;
        int st = GOBJ_ACT(holder)->actMode;

        mode = 6;
        if (st != 58) {
            mode = st == 59 ? 9 : 3;
        }

        switch (GOBJ_SUB(holder)->ctrl.motion) {
        case 136:
        case 137:
            if (mode == 3) {
                mode = 11;
            }
            break;
        case 140:
            mode = 11;
            break;
        case 123:
        case 124:
            mode = 4;
            break;
        case 120:
            mode = 10;
            break;
        case 119:
            mode = 7;
            break;
        case 121:
        case 122:
            mode = 8;
            break;
        case 128:
            mode = 2;
            break;
        case 134:
            mode = 5;
            break;
        }
    }
    return mode;
}

/* the hand-proximity probe down the chain, for ChainGeo; the caller reads the
 * result as one byte */
static inline unsigned char isChainHitByHand(GObj *gobj, float *p, float *v, float *o,
                                             float lim) /* derived name */
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    float d[4];
    int i;
    int ilim = (int)lim;

    /* clang-format off */
    v[0] = o[0]; v[1] = o[1]; v[2] = o[2];
    /* clang-format on */
    v[1] = 0.0f;

    for (i = 2; i <= cw->nodes - 1; i++) {
        ChainNode *nd = CHAIN_NODE_ADDR(ChainNode *, cw, i << 5);

        if (nd->y < p[1] && p[1] < nd->y + 50.0f) {
            float t;
            float r;

            sceVu0SubVector(d, nd, p);
            d[1] = 0.0f;
            t = sceVu0InnerProduct(d, v);
            r = FSqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - t * t);
            if (-50.0f < t && t < 50.0f && r < (float)ilim) {
                return 1;
            }
        }
    }
    return 0;
}

void ChainGeo(GObj *gobj)
{
    float p[4];
    float v[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    Act *act;
    int mode;
    int moved;
    int i;

    chainDebugY = 250;

    if (cw->locked != 0) {
        return;
    }

    moved = UpdateRootPosition(gobj);

    mode = GetChainSimulateMode(gobj);

    if (boyGObj != 0) {
        float lim;

        lim = cw->hangRange;
        if (GOBJ_ACT(boyGObj)->actMode == 5 ||
            (((motionKind + GOBJ_SUB(boyGObj)->ctrl.motion)->flags >> 11) & 1)) {
            lim = 70.0f;
        }

        GetRootPositionHandExtra(boyGObj, p);
        if (isChainHitByHand(gobj, p, v, test_CURRENTORIENT(boyGObj), lim)) {
            iosOmSendMail(boyGObj, 21, gobj);
        }
        if (_DistSqGV(p, cw->rootPos) < 900.0f) {
            iosOmSendMail(boyGObj, 166, gobj);
        }
    }

    if (mode != cw->mode) {
        switch (mode) {
        case 2:
            initPendulum(gobj);
            cw->pdl.amp = 10.0f;
            break;
        case 6:
            initPendulum(gobj);
            cw->pdl.amp = 5.0f;
            break;
        }
        cw->mode = mode;
    }

    if (debug_font_flag & 1) {
        debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "%d\n", mode);
    }
    if (debug_font_flag & 1) {
        debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "plumb = %d\n", cw->holdNode);
    }

    switch (mode) {
    case 1:
        if (cw->hold == 0 && cw->stopped != 0) {
            chain_simulate_stop(gobj);
        } else {
            chain_simulate_free(gobj);
        }
        break;
    case 2:
        chain_simulate_hangstart(gobj);
        break;
    case 3:
    case 11:
        chain_simulate_term_loop(gobj);
        break;
    case 9:
        chain_simulate_term_ropeturn(gobj);
        break;
    case 4:
        chain_set_charachara(gobj, 20.0f);
        chain_simulate_term_swingready(gobj);
        break;
    case 5:
        chain_simulate_term_swingstart(gobj);
        break;
    case 6:
        chain_simulate_term(gobj);
        break;
    case 8:
        chain_set_charachara(gobj, 10.0f);
        chain_simulate_term_down(gobj);
        break;
    case 7:
        chain_set_charachara(gobj, 20.0f);
        chain_simulate_term_moveup(gobj);
        break;
    default:
        chain_simulate_term_free(gobj);
        break;
    }

    if (cw->hold != 0) {
        act = GOBJ_ACT(boyGObj);
        ((ChainExtPtr *)&boyGObj->dobj)->sub->root.ropeState = 0;
        TestChainUpDown(gobj, cw->owner);

        /* 0x130..0x138 of the extension is a float vector (cleared here and in
         * case 2 beside the float stores at 0x410..0x418) */
        switch (mode) {
        case 8:
            ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[0] = 0.0f;
            ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[1] = 0.0f;
            ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[2] = 0.0f;
            SetChainRootUpdateMode(boyGObj, 2, &cw->node[cw->holdNode].x);
            break;
        case 7:
        case 10:
            SetChainRootUpdateMode(boyGObj, 2, &cw->node[cw->holdNode].x);
            break;
        case 3:
        case 9:
            SetChainRootUpdateMode(boyGObj, 3, &cw->node[cw->holdNode].x);
            break;
        case 2:
            if (act != 0) {
                float *nd = CHAIN_NODE_ADDR(float *, cw, cw->holdNode << 5);
                float h;

                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[0] = 0.0f;
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[1] = 0.0f;
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.move[2] = 0.0f;
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.holdPoint[0] = nd[0];
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.holdPoint[1] = nd[1];
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.holdPoint[2] = nd[2];
                h = ((ChainExtPtr *)&boyGObj->dobj)->sub->ctrl.animFrame;
                if (h < 3.0f) {
                    ((ChainExtPtr *)&boyGObj->dobj)->sub->root.ropeState = -1;
                    ropeInterRate = 0.5f;
                } else if (h < 10.0f) {
                    ((ChainExtPtr *)&boyGObj->dobj)->sub->root.ropeState = -1;
                    ropeInterRate = 1.0f;
                } else {
                    ((ChainExtPtr *)&boyGObj->dobj)->sub->root.ropeState = 1;
                }
            }
            break;
        default:
            if (act != 0) {
                CopyVector(&GOBJ_SUB(boyGObj)->root.holdPoint[0], &cw->node[cw->holdNode]);
                ((ChainExtPtr *)&boyGObj->dobj)->sub->root.ropeState = 1;
            }
            break;
        }
    }

    if (cw->hold != 0) {
        if (debug_font_flag & 1) {
            debug_Printf(10, chainDebugY += 10, 0x0FFFFFFF, "%f/%f, %d\n", cw->pdl.length,
                         (cw->node)[0].y - cw->node[cw->holdNode].y, cw->holdNode);
        }
    }

    cw->stopped = 0;

    if (moved == 0 && cw->pdl.amp < 5.0f) {
        cw->stopped = 1;
        for (i = 0; i < cw->nodes; i++) {
            /* clang-format off */
            v[0] = cw->rootPos[0]; v[1] = cw->rootPos[1]; v[2] = cw->rootPos[2];
            /* clang-format on */
            v[1] = v[1] + (float)i * 50.0f;
            if (!(_DistSqGV(v, &(cw->node)[i]) < 9.0f)) {
                cw->stopped = 0;
                break;
            }

            if (1.0f < (cw->node)[i].vx * (cw->node)[i].vx + (cw->node)[i].vy * (cw->node)[i].vy +
                           (cw->node)[i].vz * (cw->node)[i].vz) {
                cw->stopped = 0;
                break;
            }
        }
    }
}

void ChainDL(GObj *gobj)
{
    char q[16];
    char up[16];
    char d[16];
    char n[16];
    char dq[16];
    Sub15C *ext = GOBJ_SUB(gobj);
    ChainRecord *cw = ext->work;
    int i;

    memset(q, 0, 16);
    ((ChainVal *)(q + 12))->f = 1.0f;
    memset(up, 0, 16);
    ((ChainVal *)(up + 4))->f = 1.0f;

    for (i = 0; i < cw->nodes - 1; i++) {
        ChainNode *p = &(cw->node)[i];
        ChainNode *np = &(cw->node)[i + 1];

        _SubVector(d, np, p);
        _NormalizeVector(n, d);
        GetDifferencialQuaternionWithNoRegularize(dq, n, up);
        MultiQuaternion(q, dq, q);
        CopyVector(up, n);
        GetMatrixFromQuaternionPos((char *)GOBJ_SUB(gobj)->nodeMtx + (i << 6), q, p);
    }
    p2o_DispVU1DObjMulti(ext);
}

static inline void ChainNodeSpan(ChainRecord *cw, float *pos, int *i0, int *i1) /* derived name */
{
    ChainNode *nd = cw->node;

    /* the EE's cvt.w.s saturates an out-of-range position; the host's
       cvttss2si gives INT_MIN instead (docs/port/DIVERGENCES.md) */
    *i0 = ps2_ftoi((pos[1] - nd[0].y) / 50.0f);
    *i1 = *i0 + 1;
    *i0 = *i0 < 2 ? 2 : (cw->nodes - 1 < *i0 ? cw->nodes - 1 : *i0);
    *i1 = *i1 < 2 ? 2 : (cw->nodes - 1 < *i1 ? cw->nodes - 1 : *i1);
}

static void GetPositionOnTheChain(float *out, GObj *gobj, float *pos)
{
    float a[4];
    float b[4];
    int i0;
    int i1;
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    ChainNode *nd;

    ChainNodeSpan(cw, pos, &i0, &i1);
    nd = cw->node;
    a[0] = nd[i0].x;
    a[1] = nd[i0].y;
    a[2] = nd[i0].z;
    b[0] = nd[i1].x;
    b[1] = nd[i1].y;
    b[2] = nd[i1].z;
    if (i0 != i1) {
        float r = (a[1] - pos[1]) / (a[1] - b[1]);

        if (r < 0.0f) {
            r = -r;
        }
        _InterGV(out, a, b, r, 1.0f - r);
    } else {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
    }
}

static void PlumbPointUpdateChain(GObj *gobj, float *pos)
{
    float d[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    GObj *owner;
    int nearestNode = -1;
    float min = 3.40282347e+38f; /* FLT_MAX */
    int i;

    owner = cw->owner;
    sceVu0SubVector(d, pos, cw->node);
    cw->owner = owner;

    for (i = 0; i < cw->nodes; i++) {
        int n = (int)(pos[1] - (cw->node)[i].y);
        float t = (float)(n < 0 ? -n : n);

        if (t < min) {
            min = t;
            nearestNode = i;
        }
    }
    if (nearestNode == -1) {
        debug_assert(__FILE__, 1675);
        __assert(__FILE__, 1675, "nearestNode!=-1");
    }
    cw->holdNode = nearestNode;
    cw->holdNode =
        cw->holdNode < 2 ? 2 : (cw->nodes - 1 < cw->holdNode ? cw->nodes - 1 : cw->holdNode);

    _GetCorrectOrientOfChain(cw->pdl.orient, gobj, test_CURRENTORIENT(owner));

    ((ChainVal *)&cw->pdl.length)->f = (float)cw->holdNode * 50.0f;
}

/* the climb work the chain-climb modes share: the focus node point, the target
 * point the root is interpolated towards, the interpolation phase, the
 * motion's frame count and the mode the previous call left behind. */
typedef struct { /* field names derived */
    /* 0x00 */ sceVu0FVECTOR node;
    /* 0x10 */ sceVu0FVECTOR target;
    /* 0x20 */ float phase;
    /* 0x24 */ int frames;
    /* 0x28 */ int prev;
} ChainClimbWork; /* derived name */

/* the climb work's storage, twelve words reached through ChainClimbWork
 * casts; the mode word at 0x28 starts at -1 */
#ifdef ICO_HOST
/* ChainClimbWork holds 16-byte aligned vectors; the EE placed the array on a
   quadword, the host must say so (a 32-bit host would not) */
#define CHAIN_CLIMB_ALIGN __attribute__((aligned(16)))
#else
#define CHAIN_CLIMB_ALIGN
#endif

static int chainClimb[12] CHAIN_CLIMB_ALIGN = {
    /* derived name */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0};

/* the two climb helpers and TestChainUpDown */
/* clang-format off */
static inline void SetChainClimbNodePoint(GObj *obj, ChainClimbWork *rec) /* derived name */
{
    int n = GetSkeltonFocusNode(obj, 35);
    rec->node[0] = *(float *)(n * 64 + ((ChainExtPtr *)&obj->dobj)->sub->nodeMtx + 48); rec->node[1] = *(float *)(n * 64 + ((ChainExtPtr *)&obj->dobj)->sub->nodeMtx + 52); rec->node[2] = *(float *)(n * 64 + ((ChainExtPtr *)&obj->dobj)->sub->nodeMtx + 56);
}

static inline float *PushChainClimbRoot(GObj *obj, float *pos, float *out, float *ofs, float fwd, float side) /* derived name */
{
    /* pushes the root out from the chain point (fwd along the orientation, side
     * across it) and returns the pushed point, which no caller reads.  Both
     * work vectors are the caller's.
     */
    sceVu0ScaleVector(out, test_CURRENTORIENT(obj), fwd);
    sceVu0AddVector(out, pos, out);

    sceVu0ScaleVector(ofs, test_CURRENTORIENT(obj), side);
    _ApplyRyGV(ofs, 1.5707964f);
    sceVu0AddVector(out, out, ofs);

    debug_NMarker(out, 0, 0, 255, 100.0f);

    SetDirectRootPositionNoFittingWithNodePoint(obj, 35, out, 1.0f);
    return out;
}

/* Climbing the chain: moves the boy's root between the chain's node points.
 * The climb-mode selector is a nested function at its head: it reads the
 * action record's 0x34 state through the captured boy, and its parameter is
 * the motion id.  The node-point and push helpers precede the function at
 * file scope.  This region keeps its line layout and is fenced from
 * clang-format.
 */
/* GetChainClimbMode was nested in TestChainUpDown; the captured boy is a parameter. */
static inline int GetChainClimbMode(GObj *boy, int motion) /* derived name */
{
    int mode = -1;
    switch (motion) {
    case 119:
        mode = 4; if (GOBJ_ACT(boy)->actMode != 63) {
            mode = 0;
        }

        break;
    case 120:
        mode = 1;
        break;

    case 121:
        mode = 2;
        break;
    case 122:
        mode = 3;
        break;
    }
    return mode;
}

static void TestChainUpDown(GObj *gobj, GObj *boy)
{
    float v[4], org[4], w[4], d[4], hw[4], hd[4];
    ChainRecord *cw = GOBJ_SUB(gobj)->work;
    /* the boy's action record, whose chain field names the chain the boy hangs on */
    Act *sub = GOBJ_ACT(boy);

    /* Each arm has its own pointer to the climb work, set on the arm's first
     * test, and every read of an extension's 0x15C slot goes through the
     * ChainExtPtr union as in the node-point helper.
     */

    int mode = GetChainClimbMode(boy, GOBJ_SUB(boy)->ctrl.motion);

    switch (mode) {
    case 4: {
        ChainClimbWork *rec;

        org[0] = test_CURRENTROOT(boyGObj)[0]; org[1] = test_CURRENTROOT(boyGObj)[1]; org[2] = test_CURRENTROOT(boyGObj)[2];

        rec = (ChainClimbWork *)chainClimb; if (rec->prev != mode) {
            rec->phase = 0.0f;
            rec->frames = (int)(float)*motionTable[((ChainExtPtr *)&boy->dobj)->sub->ctrl.motion];
            SetChainClimbNodePoint(boy, rec);
            rec->target[0] = rec->node[0]; rec->target[2] = rec->node[2];
            rec->target[1] = rec->node[1] - 100.0f;
        }
        _InterGV(v, rec->node, rec->target, rec->phase, (float)rec->frames - rec->phase);

        v[1] = v[1] < cw->rootPos[1] ? cw->rootPos[1] : (cw->endPos[1] < v[1] ? cw->endPos[1] : v[1]);

        PushChainClimbRoot(boy, v, w, d, -10.0f, 3.0f);

        ((ChainClimbWork *)chainClimb)->phase = ((ChainClimbWork *)chainClimb)->phase + 1.0f;

        w[0] = test_CURRENTROOT(boyGObj)[0]; w[1] = test_CURRENTROOT(boyGObj)[1]; w[2] = test_CURRENTROOT(boyGObj)[2];
        w[1] = org[1] + ((ChainExtPtr *)&boyGObj->dobj)->sub->root.step[1];
        SetDirectRootPositionNoFitting(boyGObj, w);

    } break;

    case 0:
    case 1: {
        ChainClimbWork *rec;

        org[0] = test_CURRENTROOT(boyGObj)[0]; org[1] = test_CURRENTROOT(boyGObj)[1]; org[2] = test_CURRENTROOT(boyGObj)[2];

        rec = (ChainClimbWork *)chainClimb; if (rec->prev != mode) {
            rec->phase = 0.0f;
            rec->frames = (int)(float)*motionTable[((ChainExtPtr *)&boy->dobj)->sub->ctrl.motion];
            SetChainClimbNodePoint(boy, rec);
            rec->target[0] = rec->node[0]; rec->target[2] = rec->node[2];
            rec->target[1] = rec->node[1] - 100.0f;
        }
        _InterGV(v, rec->node, rec->target, rec->phase, (float)rec->frames - rec->phase);

        GetPositionOnTheChain(v, gobj, v);

        v[1] = v[1] < cw->node2Pos[1] ? cw->node2Pos[1] : (cw->endPos[1] < v[1] ? cw->endPos[1] : v[1]);

        PushChainClimbRoot(boy, v, w, d, mode == 0 ? -15.0f : -10.0f, mode == 0 ? -3.0f : -10.0f);

        ((ChainClimbWork *)chainClimb)->phase = ((ChainClimbWork *)chainClimb)->phase + 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);

        w[0] = test_CURRENTROOT(boyGObj)[0]; w[1] = test_CURRENTROOT(boyGObj)[1]; w[2] = test_CURRENTROOT(boyGObj)[2];
        w[1] = org[1] + ((ChainExtPtr *)&boyGObj->dobj)->sub->root.step[1];
        w[1] = w[1] < cw->rootPos[1] + 150.0f ? cw->rootPos[1] + 150.0f : (cw->endPos[1] < w[1] ? cw->endPos[1] : w[1]);
        SetDirectRootPosition(boyGObj, w);

        PlumbPointUpdateChain(gobj, v);

    } break;
    case 2: case 3: {
        ChainClimbWork *rec;
        rec = (ChainClimbWork *)chainClimb; if (rec->prev != mode) {
            rec->phase = 0.0f;
            rec->frames = (int)(float)*motionTable[((ChainExtPtr *)&boy->dobj)->sub->ctrl.motion];
            SetChainClimbNodePoint(boy, rec);
            rec->target[0] = rec->node[0]; rec->target[2] = rec->node[2];
            rec->target[1] = rec->node[1] + 200.0f;
        }

        _InterGV(v, rec->node, rec->target, rec->phase, (float)rec->frames - rec->phase);

        GetPositionOnTheChain(v, gobj, v);

        v[1] = v[1] < cw->node2Pos[1] ? cw->node2Pos[1] : (cw->endPos[1] < v[1] ? cw->endPos[1] : v[1]);

        PushChainClimbRoot(boy, v, hw, hd, -20.0f, -5.0f);
        ((ChainClimbWork *)chainClimb)->phase = ((ChainClimbWork *)chainClimb)->phase + 1.0f;
        PlumbPointUpdateChain(gobj, v);
    } break;
    default: { ChainClimbWork *rec;
        rec = (ChainClimbWork *)chainClimb; if ((unsigned int)rec->prev < 2) {

            int n = GetSkeltonFocusNode(boyGObj, 22);
            v[0] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 48); v[1] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 52); v[2] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 56);
            PlumbPointUpdateChain(sub->chain, v);
        }
        if ((unsigned int)(rec->prev - 2) < 2) {

            int n = GetSkeltonFocusNode(boyGObj, 22);
            v[0] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 48); v[1] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 52); v[2] = *(float *)(n * 64 + ((ChainExtPtr *)&boyGObj->dobj)->sub->nodeMtx + 56);
            PlumbPointUpdateChain(sub->chain, v);
        }

        if (sub->actMode != 59) {

            _GetCorrectOrientOfChain(cw->pdl.orient, gobj, test_CURRENTORIENT(boy));
        }
    } break;
    }

    ((ChainClimbWork *)chainClimb)->prev = mode;
}

/* clang-format on */

void SetChainRootUpdateMode(GObj *gobj, int mode, float *pos)
{
    GOBJ_SUB(gobj)->root.ropeState = mode;
    ((ChainVal *)&GOBJ_SUB(gobj)->root.holdPoint[0])->f = pos[0];
    ((ChainVal *)&GOBJ_SUB(gobj)->root.holdPoint[1])->f = pos[1];
    ((ChainVal *)&GOBJ_SUB(gobj)->root.holdPoint[2])->f = pos[2];
    if (mode == 3) {
        SetDirectRootPositionNoFittingWithNodePoint(gobj, 22, pos, 1.0f);
    }
}

void HoldChain(GObj *chain, GObj *owner, float *pos)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    StartPendulum(chain, owner, pos);
    p->hold = 1;
}

void ReleaseChain(GObj *chain, GObj *owner)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->hold = 0;
}

void GetChainPendulum(GObj *chain, float *angle, float *amp, float *cycle)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    *angle = p->pdl.angle;
    *amp = p->pdl.amp;
    if (p->pdl.ampLimit < p->pdl.amp) {
        *amp = p->pdl.ampLimit;
    }
    *cycle = p->pdl.cycle;
}

void IncreasePdlChain(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->pdl.ampSpeed = 0.1f;
}

void DecreasePdlChain(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->pdl.ampSpeed = (float)debug_chain_slow_speed * 0.5f * -0.1f;
}

void PlumbOrientUpdateChain(GObj *chain, float *src)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    p->pdl.orient[0] = src[0];
    p->pdl.orient[1] = src[1];
    p->pdl.orient[2] = src[2];
}

int isBottomOfChain(GObj *chain)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    return p->holdNode == p->nodes - 1;
}

int isStopChain(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    return cw->stopped;
}

void GetChainClimbOrient(float *dst, GObj *chain)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    dst[0] = p->wallOrient[0];
    dst[1] = p->wallOrient[1];
    dst[2] = p->wallOrient[2];
}

int CheckChainClimbablePos(GObj *chain)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;

    if (p->wallHit != 0 && p->holdNode < 3)
        return 1;
    return 0;
}

void GetChainClimbCollision(ClimbCol *dst, GObj *chain)
{
    *dst = ((ChainRecord *)GOBJ_SUB(chain)->work)->climb;
}

void SetChainParentGObj(GObj *chain, void *parent)
{
    ((ChainRecord *)GOBJ_SUB(chain)->work)->root = parent;
}

/* the chain's direction correction in degrees, and whether it has one;
 * getChainDirCorrectVal below is the same body, for _GetCorrectOrientOfChain */
int GetChainDirCorrectVal(GObj *chain, int *deg)
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    *deg = (int)(p->dirCorrect * 180.0f / 3.1415927f);
    return p->hasDirCorrect;
}

static inline int getChainDirCorrectVal(GObj *chain, int *deg) /* derived name */
{
    ChainRecord *p = GOBJ_SUB(chain)->work;
    *deg = (int)(p->dirCorrect * 180.0f / 3.1415927f);
    return p->hasDirCorrect;
}

void GetRootPositionHandExtra(void *gobj, float *out)
{
    out[0] = test_CURRENTROOT(gobj)[0];
    out[1] = test_CURRENTROOT(gobj)[1];
    out[2] = test_CURRENTROOT(gobj)[2];
    out[1] -= 50.0f;
}

void InitPendulum(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;
    float a = (float)debug_chain_cycle_speed * -0.2f + 2.0f;
    float y;

    a = a < 0.1f ? 0.1f : (a > 2.0f ? 2.0f : a);

    y = (float)(int)(a * 6.0f * FSqrt(cw->pdl.length / 2.5f) * 8.0f / 10.0f);

    cw->pdl.cycle = y;
    cw->pdl.cycle = cw->pdl.cycle < 1.0f ? 1.0f : (cw->pdl.cycle > 255.0f ? 255.0f : cw->pdl.cycle);

    cw->pdl.phase = cw->pdl.cycle * 0.5f;
    cw->pdl.ampLimit = 360.0f;
    cw->pdl.swing = 1;
}

void LockChainGeo(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->locked = 1;
}

void UnLockChainGeo(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->locked = 0;
}

float GetChainHangRange(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    return cw->hangRange;
}

float GetChainLength(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    return (float)(cw->nodes - 1) * 50.0f;
}

void EnableChainHang(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->hangable = 1;
}

void UnableChainHang(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    cw->hangable = 0;
}

int IsAbleChainHang(GObj *chain)
{
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    return cw->hangable;
}

void ChainPositionReset(GObj *chain)
{
    float pos[4];
    ChainRecord *cw = GOBJ_SUB(chain)->work;

    UpdateRootMatrix(chain);
    GetRootPosition(pos, chain);
    ResetChainNodes(cw, pos);
}

void _GetCorrectOrientOfChain(float *out, GObj *gobj, float *dir)
{
    float v[4];
    int deg;

    if (getChainDirCorrectVal(gobj, &deg) != 0) {
        float pi = 3.1415927f;
        int d;

        memset(v, 0, 16);
        v[2] = 1.0f;
        d = (int)(_GetDirection(dir) / pi * 180.0f);
        d = RoundDegGV(d - deg);
        d = AlignDegGV(d);
        d = RoundDegGV(deg + d);
        _ApplyRyGV(v, (float)d * pi / 180.0f);
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
    } else {
        out[0] = dir[0];
        out[1] = dir[1];
        out[2] = dir[2];
    }
}

static void chain_sub_simulate(GObj *gobj, ChainNode *nd, int from, int to, unsigned char flag,
                               int flag2, float grav, float len, float damp)
{
    float d[4];
    float t[4];
    ChainNode *p;
    ChainNode *q;
    ChainNode *e;
    int step;
    float l;

    step = from < to ? 1 : -1;
    e = nd + to;
    q = nd + from;

    for (p = q + step; p != e; p += step, q += step) {
        if (flag) {
            p->vy += grav;
            sceVu0SubVector(d, p, q);
            correct_vector(&p->vx, d);
        } else {
            p->vy += grav;
        }
        sceVu0ScaleVector(&p->vx, &p->vx, damp);
        sceVu0AddVector(t, p, &p->vx);
        sceVu0SubVector(d, t, q);
        l = FSqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l == 0.0f)
            continue;
        if (l < len && d[1] < 0.0f)
            continue;
        sceVu0ScaleVector(d, d, len / l);
        sceVu0AddVector(t, q, d);
        sceVu0SubVector(&p->vx, t, p);
        p->x = t[0];
        p->y = t[1];
        p->z = t[2];
    }
}

static void chain_sub_pendulum(ChainNode *base, int n, float *pos)
{
    ChainNode *p;
    int i = 0;
    if (n < 0) {
        return;
    }
    p = base;
    do {
        _InterGV(&p->x, &base->x, pos, (float)i, (float)(n - i));
        i++;
        p++;
    } while (i <= n);
}

int GetChainNearestNodePosition(float *out, GObj *gobj, float *p)
{
    ChainRecord *cw = GOBJ_SUB(gobj)->work;

    float best = 3.40282347e+38f; /* FLT_MAX */
    int ret = 0;
    int i;

    for (i = 2; i <= cw->nodes - 1; i++) {
        float d = _DistSqGV(p, &cw->node[i]);

        if (d < best) {
            float *e = CHAIN_NODE_ADDR(float *, cw, i * 32);
            out[0] = e[0];
            out[1] = e[1];
            out[2] = e[2];
            best = d;
            ret = 1;
        }
    }
    return ret;
}
