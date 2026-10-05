#include "typedef.h"
#include "sugiCommon.h"
#include "debug.h"
#include "memory.h"
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "Primitive.h"
#include "Texture.h"
#include "lineManager.h"
#include "motionManager2.h"
#include "quaternion.h"
#include <libvu0.h>
#include <stdlib.h>
#include "ios.h"
#include "tableSin.h"
#include <string.h>
#include "matrixDrive.h"
#include "main.h"
#include "fieldCollision.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "windField.h"
#include "clothAnimation.h"

typedef struct { /* field names derived */
    float v[4];
    unsigned short a;
    unsigned short b;
} ClothBuf; /* derived name */

/* the two line colours the chain debug draw alternates between, as the
   quadword DrawLine takes; constant and quadword aligned, so each copy into a
   local is one quadword load and store */
typedef struct { /* field names derived */
    int r, g, b, a;
} __attribute__((aligned(16))) LineColor; /* derived name */

static const LineColor chainLineColor0 = {255, 255, 255, 128}; /* derived name */

static const LineColor chainLineColor1 = {255, 0, 0, 128}; /* derived name */

void TestDispChainAnimation(ChainSet *sys)
{
    LineColor c0 = chainLineColor0;
    LineColor c1 = chainLineColor1;
    VECTOR mid;
    int i;
    int j;

    gif_StartPacketPri(0xB);
    gif_SetAlpha(1, 5, 0x80);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < sys->num; i++) {
        int n = sys->cfg[i].num;
        float (*pts)[4] = sys->nodes[i].pos;
        for (j = 1; j < n; j++) {
            float *p = pts[j];
            float *q = pts[j - 1];
            LineColor *col = (j & 1) ? &c1 : &c0;
            sceVu0AddVector(&mid, p, q);
            DrawLine(p, q, col, 0);
        }
        for (j = 0; j < 5; j++) {
            char *base = (char *)&sys->nodes[i];
            char *w = base + j * 0x50;
            if (0.0f <= *(float *)(w + 0x10)) {
                DrawLine(base + 0x30, base + 0x80, &c0, 0);
            }
        }
    }
    gif_EndPacket();
}

void GetChainExWeightGlobalPos(float *pos, ChainNode *node, int idx)
{
    CopyVector(pos, &node->ex[idx].v1);
}

/* the chain's red debug segment, built only when DEBUG is defined */
static __inline__ void chainDebugOld(VECTOR *old) /* derived name */
{
#ifdef DEBUG
    DrawLine((char *)old[0], (char *)old[1], (LineColor *)&chainLineColor1, 0);
#endif
}

/* GetChainAnimation's constraint passes.  In the original, bindExWeight,
   bind2 and calc2 were GNU nested functions inside its node loop; they are
   at file scope so clang compiles them, and what they captured (the loop's
   node, its points and matrix, and the dv/tv work vectors they share with
   the enclosing function) is passed in a ChainStepCtx. */
typedef struct ChainStepCtx {
    ChainSet *sys;
    int i;
    int n;
    int no;
    float (*pts)[4];
    float (*mtx)[4];
    ChainParam *cp;
    VECTOR *ew;
    VECTOR *old;
    VECTOR *dv;
    VECTOR *tv;
} ChainStepCtx; /* derived name */

static void bindExWeight(ChainStepCtx *c, char *ex, void *ev, float t)
{
    VECTOR va;
    VECTOR vb;
    int id;
    int id1;
    float ll;
    float ka;
    float kb;
    float ka2;
    float kb2;
    float cl;
    float wa;
    float wb;
    float l;

    id = (int)*(float *)ex;
    id1 = id + 1;
    ll = *(float *)(ex + 0x40) * *(float *)(ex + 0x40);
    ka = t * (*(float *)ex - (float)(int)*(float *)ex);
    kb = t * (1.0f - (*(float *)ex - (float)(int)*(float *)ex));
    ka2 = ka * ka;
    kb2 = kb * kb;
    cl = c->sys->cfg[c->i].pm.weight;

    wa = cl;
    wb = *(float *)(ex + 0x44);

    sceVu0InterVector(&vb, c->pts[id], c->pts[id1],
                      1.0f - (*(float *)ex - (float)(int)*(float *)ex));

    sceVu0SubVector(c->dv, &vb, ex + 0x20);
    l = VectorLengthSquare(c->dv);
    if (ll < l) {
        sceVu0ScaleVectorXYZ(c->dv, c->dv, *(float *)(ex + 0x40) / _Sqrt(l));
        AddVectorXYZ(ex + 0x10, ex + 0x20, c->dv);

        SubVectorXYZ(c->dv, ex + 0x10, c->pts[id]);
        l = VectorLengthSquare(c->dv);
        if (ka2 < l) {
            sceVu0ScaleVectorXYZ(c->dv, c->dv, ka / _Sqrt(l));
            AddVectorXYZ(c->dv, c->pts[id], c->dv);
        } else {
            CopyVector(c->dv, ex + 0x10);
            wa = wb;
        }

        SubVectorXYZ(&va, ex + 0x10, c->pts[id1]);
        l = VectorLengthSquare(&va);
        if (kb2 < l) {
            sceVu0ScaleVectorXYZ(&va, &va, kb / _Sqrt(l));
            AddVectorXYZ(&va, c->pts[id1], &va);
        } else {
            CopyVector(&va, ex + 0x10);
            cl = wb;
        }

        sceVu0ScaleVector(c->dv, c->dv, wa / (cl + wa));
        sceVu0ScaleVector(&va, &va, cl / (cl + wa));
        AddVectorXYZ(c->dv, c->dv, &va);

        SubVectorXYZ(c->dv, c->dv, ex + 0x10);
        sceVu0ScaleVector(c->dv, c->dv, 1.0f - wb / (cl + wa + wb));
        AddVectorXYZ(ex + 0x10, ex + 0x10, c->dv);

        SubVectorXYZ(c->dv, c->pts[id], ex + 0x10);
        l = VectorLengthSquare(c->dv);
        if (ka2 < l) {
            sceVu0ScaleVectorXYZ(c->dv, c->dv, ka / _Sqrt(l));
            AddVectorXYZ(c->pts[id], ex + 0x10, c->dv);
        }

        SubVectorXYZ(c->dv, c->pts[id1], ex + 0x10);
        l = VectorLengthSquare(c->dv);
        if (kb2 < l) {
            sceVu0ScaleVectorXYZ(c->dv, c->dv, kb / _Sqrt(l));
            AddVectorXYZ(c->pts[id1], ex + 0x10, c->dv);
        }

        sceVu0SubVector(c->dv, ex + 0x20, ex + 0x10);
        l = VectorLengthSquare(c->dv);
        if (ll < l) {
            sceVu0ScaleVectorXYZ(c->dv, c->dv, *(float *)(ex + 0x40) / _Sqrt(l));
            sceVu0AddVector(ex + 0x20, ex + 0x10, c->dv);
        }
    }
    sceVu0InterVector(ex + 0x10, c->pts[id], c->pts[id1],
                      1.0f - (*(float *)ex - (float)(int)*(float *)ex));
}

static void bind2(ChainStepCtx *c, float (*pp)[4], int id, int ip, int in, float t)
{
    float ka;
    float kb;
    float ka2;
    float kb2;
    float cl;
    float wa;
    float wb;
    float l;

    ka = (float)(id - ip < 0 ? -(id - ip) : id - ip) * t;
    kb = (float)(id - in < 0 ? -(id - in) : id - in) * t;
    ka2 = ka * ka;
    kb2 = kb * kb;
    cl = c->sys->cfg[c->i].pm.weight;

    wa = cl;
    wb = cl;

    SubVectorXYZ(c->dv, pp[id], pp[ip]);
    l = VectorLengthSquare(c->dv);
    if (ka2 < l) {
        sceVu0ScaleVectorXYZ(c->dv, c->dv, ka / _Sqrt(l));
        AddVectorXYZ(c->dv, pp[ip], c->dv);
    } else {
        CopyVector(c->dv, pp[id]);
        cl = wb;
    }

    SubVectorXYZ(c->tv, pp[id], pp[in]);
    l = VectorLengthSquare(c->tv);
    if (kb2 < l) {
        sceVu0ScaleVectorXYZ(c->tv, c->tv, kb / _Sqrt(l));
        AddVectorXYZ(c->tv, pp[in], c->tv);
    } else {
        CopyVector(c->tv, pp[id]);
        cl = wb;
    }

    sceVu0ScaleVector(c->dv, c->dv, wa / (cl + wa));
    sceVu0ScaleVector(c->tv, c->tv, cl / (cl + wa));
    AddVectorXYZ(c->dv, c->dv, c->tv);

    SubVectorXYZ(c->dv, c->dv, pp[id]);
    sceVu0ScaleVector(c->dv, c->dv, 1.0f - wb / (cl + wa + wb));
    AddVectorXYZ(pp[id], pp[id], c->dv);

    SubVectorXYZ(c->dv, pp[ip], pp[id]);
    l = VectorLengthSquare(c->dv);
    if (ka < l) {
        sceVu0ScaleVectorXYZ(c->dv, c->dv, ka / _Sqrt(l));
    }
    AddVectorXYZ(pp[ip], pp[id], c->dv);

    SubVectorXYZ(c->dv, pp[in], pp[id]);
    l = VectorLengthSquare(c->dv);
    if (kb < l) {
        sceVu0ScaleVectorXYZ(c->dv, c->dv, kb / _Sqrt(l));
    }
    AddVectorXYZ(pp[in], pp[id], c->dv);
}

static void calc2(ChainStepCtx *c, float (*pp)[4], int lo, int hi)
{
    int m;
    int q;

    sceVu0ApplyMatrix(c->pts, c->mtx + c->no * 4, c->cp->root);
    for (m = 0; m < 5; m++) {
        if (0.0f <= (c->sys->nodes + c->i)->ex[m].w) {
            bindExWeight(c, (char *)&(c->sys->nodes + c->i)->ex[m], &c->ew[m], c->cp->step);
        }
    }
    for (m = 0; m < c->n; m++) {
        int mp = m + 1;
        q = c->n - mp;
        bind2(c, pp, m, m - 1 < 0 ? 0 : m - 1, mp < c->n ? mp : c->n - 1, c->cp->step);
        bind2(c, pp, q, q - 1 < 0 ? 0 : q - 1, q + 1 < c->n ? q + 1 : c->n - 1, c->cp->step);
    }
    chainDebugOld(c->old);
    sceVu0ApplyMatrix(c->pts, c->mtx + c->no * 4, c->cp->root);
}

void GetChainAnimation(ChainSet *sys, GObj *obj, float (*mtx)[4])
{
    VECTOR dv;
    VECTOR tv;
    float mm[4][4];
    VECTOR ew[5];
    float (*pm)[4];
    int i;

    pm = mm;
    sceVu0UnitMatrix(pm);
    for (i = 0; i < sys->num; i++) {
        ChainCfg *cf = (ChainCfg *)(i * 0x50 + (ICO_WORD)sys->cfg);
        int n = cf->num;
        float (*pts)[4] = (sys->nodes + i)->pos;
        float (*vel)[4] = (sys->nodes + i)->vel;
        ChainParam *cp = &cf->pm;
        int no;
        int j;
        int k;
        VECTOR old[n];

        if (mtx != 0 && obj != 0 && cf->pm.node != -1) {
            no = GetSkeltonFocusNode(obj, cf->pm.node);
        } else {
            if (mtx == 0) {
                mtx = pm;
            }
            no = 0;
        }

        /* the DEBUG build draws the chain as it stands before this frame's
           step, one red segment from the node drawn last (k) to each next
           one, as TestDispChainAnimation draws it live; without DEBUG the
           loop keeps only `k = j` */
        for (j = 0; j < n; j++) {
#ifdef DEBUG
            if (j > 0) {
                DrawLine(pts[k], pts[j], (LineColor *)&chainLineColor1, 0);
            }
#endif
            k = j;
        }

        for (j = 0; j < n; j++) {
            CopyVector(&dv, pts[j]);
            vel[j][1] += 60.0f / (float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]) * 0.5f *
                         (60.0f / (float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]));
            AddVectorXYZ(pts[j], pts[j], vel[j]);
            CopyVector(&old[j], pts[j]);
            sceVu0ScaleVector(vel[j], &dv, -1.0f);
        }

        for (k = 0; k < 5; k++) {
            if (0.0f <= (sys->nodes + i)->ex[k].w) {
                CopyVector(&dv, &(sys->nodes + i)->ex[k].v1);
                (sys->nodes + i)->ex[k].v2.y +=
                    60.0f / (float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]) * 0.5f *
                    (60.0f / (float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]));
                CopyVector(&ew[k], &(sys->nodes + i)->ex[k].v2);
                AddVectorXYZ(&(sys->nodes + i)->ex[k].v1, &(sys->nodes + i)->ex[k].v1,
                             &(sys->nodes + i)->ex[k].v2);
                sceVu0ScaleVector(&(sys->nodes + i)->ex[k].v2, &dv, -1.0f);
            }
        }

        {
            ChainStepCtx ctx;

            ctx.sys = sys;
            ctx.i = i;
            ctx.n = n;
            ctx.no = no;
            ctx.pts = pts;
            ctx.mtx = mtx;
            ctx.cp = cp;
            ctx.ew = ew;
            ctx.old = old;
            ctx.dv = &dv;
            ctx.tv = &tv;
            calc2(&ctx, pts, 0, n - 1);
            calc2(&ctx, pts, 0, n - 1);
            calc2(&ctx, pts, 0, n - 1);
            calc2(&ctx, pts, 0, n - 1);
        }

        sceVu0ApplyMatrix(pts, mtx + no * 4, cp->root);

        sceVu0ApplyMatrix(pts, mtx + no * 4, cp->root);

        for (j = 0; j < n; j++) {
            AddVectorXYZ(vel[j], vel[j], pts[j]);
            vel[j][3] = 0.0f;
        }

        for (k = 0; k < 5; k++) {
            if (0.0f <= (sys->nodes + i)->ex[k].w) {
                AddVectorXYZ(&(sys->nodes + i)->ex[k].v2, &(sys->nodes + i)->ex[k].v2,
                             &(sys->nodes + i)->ex[k].v1);
            }
        }
    }
    sys->oddFrame = sys->oddFrame == 0;
}

int SetChainExtendedWeight(ChainNode *node, int idx, float w0, float w1)
{
    int i;
    ExW *ex;

    if (node->exNum >= 5) {
        debug_StdPrintfDummy("No more weights... \n");
        return -1;
    }
    for (i = 0; i < 5; i++) {
        if (node->ex[i].w < 0.0f) {
            node->ex[i].w = (float)idx - 1e-6f;
            node->ex[i].len0 = w0;
            node->ex[i].len1 = w1;
            ex = &node->ex[i];
            CopyVector(&ex->v2, ZeroVector);
            CopyVector(&ex->v0, node->pos[idx]);
            CopyVector(&ex->v1, node->pos[idx]);
            node->ex[i].v1.y = node->ex[i].v1.y + w0;
            node->exNum = node->exNum + 1;
            return i;
        }
    }
    debug_StdPrintfDummy("Illegal weight number\n");
    return -1;
}

/* push a point back to the inner side of a wall plane */
static __inline__ void pushInsidePlane(void *p, const void *plane) /* derived name */
{
    VECTOR tv;
    float d = plane_distance(p, plane);

    if (d < 0.0f) {
        _ScaleVector(&tv, plane, d);
        _SubVectorXYZ(p, p, &tv);
    }
}

/* the law of cosines on a triangle whose sides are a, b and c, handed
   straight to the arc-cosine table (a GNU nested function of
   GetClothAnimation in the original, capturing nothing) */
static __inline__ int arcCosOfTriangle(float a, float b, float c) /* derived name */
{
    float aa = a * a;
    float bb = b * b;
    float cc = c * c;

    return GetTableArcCos((aa + bb - cc) / ((a + a) * b));
}

/* The sixth parameter is the wall count, which the function recomputes from
   the wall owner before any use, so the value passed in is never read.  The
   wall owner is an object handle passed as an int, the form the wall-plane
   query GetGlobalWallPlane takes (fieldCollision.h). */
void GetClothAnimation(VECTOR **pos, VECTOR **vel, GObj *obj, void *m, ClothCfg *cfg, int nwall,
                       ICO_WORD_PTR(GObj *) wallOwner, int fixEnd)
{
    VECTOR dv;
    float pw;
    float len;
    float d2;
    void *wind;
    int i;
    int j;
    int n;
    int node;
    SkelNode *focus;
    char **rowsB = (char **)vel;
    int n0 = cfg->num;
    float seg = cfg->segLength;
    int nx = cfg->div - (fixEnd != 0);
    float rnx = 1.0f / (float)nx;
    char *pts = (char *)cfg->anchors;
    int wrap = cfg->wrap;

    focus = 0;
    if (wallOwner != 0) {
        nwall = *(int *)(GOBJ_SUB(wallOwner)->colData + 8);
    } else {
        nwall = 0;
    }
    if (obj != 0) {
        focus = GOBJ_SUB(obj)->skel;
    }
    for (i = 0; i < n0; i++) {
        float damp = 0.8f;

        for (j = 1; j < nx; j++) {
            sceVu0ScaleVector(&dv, rowsB[i] + j * 16, damp);
            damp = damp * 0.98f;
            sceVu0ScaleVector(rowsB[i] + j * 16, ((char **)pos)[i] + j * 16, -1.0f);
            AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i] + j * 16, &dv);
        }
    }
    for (i = 0; i < n0; i++) {
        if (focus != 0) {
            node = GetSkeltonFocusNode(obj, *(int *)(pts + i * 48));
            sceVu0ApplyMatrix(((char **)pos)[i], (char *)GOBJ_SUB(obj)->nodeMtx + node * 64,
                              pts + i * 48 + 16);
        } else {
            if (m != 0) {
                _ApplyMatrix(((char **)pos)[i], m, pts + i * 48 + 16);
                if (fixEnd != 0) {
                    _ApplyMatrix(((char **)pos)[i] + nx * 16, m, pts + i * 48 + 32);
                }
            }
        }
        for (j = 1; j < nx; j++) {
            char *p = ((char **)pos)[i] + j * 16;
            /* a block-scoped work area with one word cleared that nothing
               reads after the gravity add; its type is box.c's union
               quadword */
            Vec4u work[3];

            work[2].f[1] = 0.0f;
            *(float *)(p + 4) = *(float *)(p + 4) + cfg->weight;
        }
    }
    for (j = 1; j < nx; j++) {
        float t = ((float)nx + (float)j * 0.5f) * rnx;
        float lim = t * t * seg * seg;

        for (i = 1; i < n0 / 2; i++) {
            SubVectorXYZ(&dv, ((char **)pos)[i] + j * 16, ((char **)pos)[i - 1] + j * 16);
            len = VectorLengthSquare(&dv);
            if (lim < len) {
                sceVu0ScaleVectorXYZ(&dv, &dv, t * seg / _Sqrt(len));
                AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i - 1] + j * 16, &dv);
            }
        }
        if (n0 != 1) {
            for (i = wrap ? n0 - 1 : n0 - 2; i >= n0 / 2 - 1; i--) {
                int q;

                if (wrap != 0) {
                    q = (i + 1) % n0;
                } else {
                    q = i + 1;
                }
                SubVectorXYZ(&dv, ((char **)pos)[i] + j * 16, ((char **)pos)[q] + j * 16);
                len = VectorLengthSquare(&dv);
                if (lim < len) {
                    sceVu0ScaleVectorXYZ(&dv, &dv, t * seg / _Sqrt(len));
                    AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[q] + j * 16, &dv);
                }
            }
        }
    }
    if (fixEnd == 0) {
        for (i = 0; i < n0; i++) {
            float len = *(float *)(pts + i * 48 + 4);
            float lim = len * len;

            for (j = 1; j < nx; j++) {
                SubVectorXYZ(&dv, ((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 - 16));
                d2 = VectorLengthSquare(&dv);
                if (lim < d2) {
                    sceVu0ScaleVectorXYZ(&dv, &dv, len / _Sqrt(d2));
                    AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 - 16),
                                 &dv);
                }
            }
        }
    } else {
        for (i = 0; i < n0; i++) {
            float len = *(float *)(pts + i * 48 + 4);
            float lim = len * len;

            for (j = nx - 1; j > 0; j--) {
                SubVectorXYZ(&dv, ((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 + 16));
                d2 = VectorLengthSquare(&dv);
                if (lim < d2) {
                    sceVu0ScaleVectorXYZ(&dv, &dv, len / _Sqrt(d2));
                    AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 + 16),
                                 &dv);
                }
            }
            for (j = 1; j < nx; j++) {
                SubVectorXYZ(&dv, ((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 - 16));
                d2 = VectorLengthSquare(&dv);
                if (lim < d2) {
                    sceVu0ScaleVectorXYZ(&dv, &dv, len / _Sqrt(d2));
                    AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i] + (j * 16 - 16),
                                 &dv);
                }
            }
            /* the vectors of the row's rotation, in a block of their own
               inside the row loop, so the wall and wind loops below reuse
               their stack */
            {
                VECTOR va;
                VECTOR vb;
                VECTOR vc;
                VECTOR vd;
                float qt[4];
                float mx[16];
                /* ve, vf and vg are read only by the DEBUG build's drawing
                   of the row's axis after the correction loop */
                VECTOR ve;
                VECTOR vf;
                VECTOR vg;
                float total;
                float rtotal;
                int angle;

                _SubVector(&va, ((char **)pos)[i] + nx * 16, ((char **)pos)[i]);
                total = VectorLength(&va);
                rtotal = 1.0f / total;
                for (j = 1; j < nx; j++) {
                    float l0;
                    float l1;

                    _SubVector(&vb, ((char **)pos)[i] + j * 16, ((char **)pos)[i]);
                    _SubVector(&vc, ((char **)pos)[i] + j * 16, ((char **)pos)[i] + nx * 16);
                    l0 = VectorLengthSquare(&vb);
                    l1 = VectorLengthSquare(&vc);
                    if (lim * (float)j * (float)j < l0 ||
                        lim * (float)(nx - j) * (float)(nx - j) < l1) {
                        _OuterProduct(&vd, &vb, &va);
                        angle = arcCosOfTriangle(total, len * (float)j, len * (float)(nx - j));
                        SetQuaternionByAxisRotateV(qt, angle, (float *)&vd);
                        GetMatrixFromQuaternion((char *)mx, (char *)qt);
                        _ApplyMatrix(&vb, mx, &va);
                        _ScaleVector(&vb, &vb, len * (float)j * rtotal);
                        _AddVectorXYZ(((char **)pos)[i] + j * 16, ((char **)pos)[i], &vb);
                    }
                }
#ifdef DEBUG
                _ScaleVector(&ve, &va, 0.5f);
                _AddVectorXYZ(&vf, ((char **)pos)[i], &ve);
                _AddVectorXYZ(&vg, ((char **)pos)[i], &va);
                DrawLine(((char **)pos)[i], (char *)&vf, (LineColor *)&chainLineColor0, 0);
                DrawLine((char *)&vf, (char *)&vg, (LineColor *)&chainLineColor1, 0);
#endif
            }
        }
    }
    for (n = 0; n < nwall; n++) {
        /* the wall owner's sub-record, then the query for its n-th wall
           plane */
#ifdef ICO_HOST
        FcColl *fc = (FcColl *)GOBJ_SUB(wallOwner)->colData;
        WallCfg q = {{wallOwner, 0}, ICO_EEPTR(char *, fc->wcl) + n * 80};
#else
        int sub = *(int *)(wallOwner + 0x15C),
            q[3] = {wallOwner, 0, *(int *)(*(int *)(sub + 0x70) + 0x10) + n * 80};
#endif
        VECTOR pl;

#ifdef ICO_HOST
        GetGlobalWallPlane(&pl.x, &q);
#else
        GetGlobalWallPlane(&pl.x, (WallCfg *)q);
#endif
        for (i = 0; i < n0; i++) {
            for (j = 1; j < nx; j++) {
                pushInsidePlane(((char **)pos)[i] + j * 16, &pl);
            }
        }
    }
    wind = GetWindVector(&pw, (float *)((char **)pos)[0]);
    pw = pw / 40960.0f;
    for (i = 0; i < n0; i++) {
        for (j = 1; j < nx; j++) {
            AddVectorXYZ(rowsB[i] + j * 16, rowsB[i] + j * 16, ((char **)pos)[i] + j * 16);
            AddVectorXYZ(rowsB[i] + j * 16, rowsB[i] + j * 16, wind);
            {
                VECTOR r = {pw * (float)((rand() & 0x7FFF) - 16383),
                            pw * (float)((rand() & 0x7FFF) - 16383),
                            pw * (float)((rand() & 0x7FFF) - 16383), 0.0f};

                AddVectorXYZ(rowsB[i] + j * 16, rowsB[i] + j * 16, &r);
            }
        }
    }
}

/* One entry of the four-corner anchor table the cloth is pinned to, the
   48-byte anchor record ClothCfg's anchors point at: the middle vector is the
   local-space anchor position _ApplyMatrix transforms into the corner point. */
typedef struct { /* field names derived */
    VECTOR v0;
    VECTOR v1;
    VECTOR v2;
} ClothFixPoint; /* derived name */

/* the debug draw of a fix point's anchor, built only when DEBUG is defined */
static __inline__ void clothFixDebug(ClothFixPoint *fix) /* derived name */
{
#ifdef DEBUG
    DrawLine((char *)fix->v0, (char *)fix->v1, (LineColor *)&chainLineColor0, 0);
#endif
}

/* GetClothAnimationFix4Points's row and column passes.  In the original they
   were GNU nested functions (each with its own nested copy of interHalf)
   capturing pa, q, nx and ny, which are now parameters. */
static __inline__ void interHalf(VECTOR *d, VECTOR *a, VECTOR *b) /* derived name */
{
    _InterVectorXYZ(d, a, b, 0.5f);
}

static void yTension(VECTOR **pa, ClothFixPoint *q, int ny, int y)
{
    VECTOR *r;
    int x;
    int xp;
    int m;
    int mp;
    int mm;

    r = pa[y];
    clothFixDebug(q);
    for (x = 1; x < ny - 1; x++) {
        xp = x + 1;
        m = ny - xp;
        mp = m + 1;
        mm = m - 1;
        interHalf(&r[x], &r[x + 1], &r[x - 1]);
        interHalf(&r[m], &r[mp], &r[mm]);
    }
}

static void xTension(VECTOR **pa, int nx, int x)
{
    int y;
    int yp;
    int m;
    int mp;
    int mm;

    for (y = 1; y < nx - 1; y++) {
        yp = y + 1;
        m = nx - yp;
        mp = m + 1;
        mm = m - 1;
        interHalf(&pa[y][x], &pa[y + 1][x], &pa[y - 1][x]);
        interHalf(&pa[m][x], &pa[mp][x], &pa[mm][x]);
    }
}

void GetClothAnimationFix4Points(VECTOR **pa, VECTOR **pv, ClothCfg *cfg, void *mtx)
{
    VECTOR dv;
    int i;
    int j;
    int nx;
    int ny;
    ClothFixPoint *q;

    nx = cfg->num;
    ny = cfg->div;
    q = cfg->anchors;

    for (i = 0; i < nx; i++) {
        for (j = 0; j < ny; j++) {
            _ScaleVector(&dv, &pv[i][j], 0.98f);
            _ScaleVector(&pv[i][j], &pa[i][j], -1.0f);
            pv[i][j].w = 0.0f;
            _AddVectorXYZ(&pa[i][j], &pa[i][j], &dv);
        }
    }

    for (i = 0; i < nx; i++) {
        for (j = 0; j < ny; j++) {}
    }

    _ApplyMatrix(pa[0], mtx, &q[0].v1);
    _ApplyMatrix(pa[nx - 1], mtx, &q[1].v1);
    _ApplyMatrix(&pa[0][ny - 1], mtx, &q[2].v1);
    _ApplyMatrix(&pa[nx - 1][ny - 1], mtx, &q[3].v1);

    {
        VECTOR rv;
        VECTOR rt;
        VECTOR wp;
        VECTOR *wv;
        int n;

        yTension(pa, q, ny, 0);
        yTension(pa, q, ny, nx - 1);
        xTension(pa, nx, 0);
        xTension(pa, nx, ny - 1);
        for (n = 1; n < nx - 1; n++) {
            yTension(pa, q, ny, n);
        }
        for (n = 1; n < ny - 1; n++) {
            xTension(pa, nx, n);
        }

        wv = (VECTOR *)GetWindVector(&wp.x, &pa[0]->x);
        wp.x = wp.x / 40960.0f;
        for (i = 0; i < nx; i++) {
            for (j = 0; j < ny; j++) {
                AddVectorXYZ(&pv[i][j], &pv[i][j], &pa[i][j]);
                AddVectorXYZ(&pv[i][j], &pv[i][j], wv);
                rt.x = wp.x * 3.0f * (float)((rand() & 0x7fff) - 16383);
                rt.y = wp.x * 3.0f * (float)((rand() & 0x7fff) - 16383);
                rt.z = wp.x * 3.0f * (float)((rand() & 0x7fff) - 16383);
                rt.w = 0.0f;
                rv = rt;
                AddVectorXYZ(&pv[i][j], &pv[i][j], &rv);
            }
        }
    }
}

/* the two cap planes of the unit cylinder clipCylinderCollision clips
   against, each with a zero vector after it */
static sceVu0FVECTOR clipPlane[2][2] = {
    {{0.0f, -1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
}; /* derived name */

/* the squared radius of the cylinder the cloth is clipped against */
static float cylinderRadiusSq = 1.0f; /* derived name */

/* checkOverThePlane and getCrossPoint, which clipCylinderCollision and
   getCloth4D inline and the exported functions further down call, and a
   file-static copy of checkFrontAcross (a call of the inline would return
   through a temporary) */
static __inline__ int checkOverThePlane_i(void *pt, const void *plane) /* derived name */
{
    if (0.0f < plane_distance(pt, plane))
        return 1;
    return 0;
}

static __inline__ int checkFrontAcross_i(void *seg, void *plane) /* derived name */
{
    if (0.0f <= plane_distance(seg, plane)) {
        if (plane_distance((char *)seg + 0x10, plane) < 0.0f)
            return 1;
    }
    return 0;
}

static __inline__ void getCrossPoint_i(void *out, void *seg, void *plane) /* derived name */
{
    float v[4];
    float d0 = plane_distance(seg, plane);
    float d1 = -plane_distance((char *)seg + 0x10, plane);

    sceVu0SubVector(v, (char *)seg + 0x10, seg);
    sceVu0ScaleVectorXYZ(v, v, d0 / (d0 + d1));
    AddVectorXYZ(out, seg, v);
}

/* the squared XZ length, getXZLengthSquare's sequence */
static __inline__ float xzLengthSquare(const void *p) /* derived name */
{
#ifdef ICO_HOST
    return ico_xz_length_square(p);
#else
    float d;
    /* one asm block in plane_distance's style, without a memory clobber */
    __asm__ __volatile__("lqc2 $vf4, 0x0(%1)\n\t"
                         "vmul.xz $vf4, $vf4, $vf4\n\t"
                         "vaddz.x $vf4, $vf4, $vf4z\n\t"
                         "qmfc2.ni $2, $vf4\n\t"
                         "mtc1 $2, %0"
                         : "=f"(d)
                         : "r"(p)
                         : "$2");
    return d;
#endif
}

/* tests both ends of the segment p against one plane; nested in
   clipCylinderCollision (before `d`) in the original, capturing p */
static __inline__ int bothOverThePlane(char *p, const void *pl) /* derived name */
{
    if (checkOverThePlane_i(p, pl) && checkOverThePlane_i(p + 0x10, pl))
        return 1;
    return 0;
}

static int clipCylinderCollision(char *p, void *pt)
{
    float d[4];

    if (bothOverThePlane(p, clipPlane[0])) {
        return -1;
    }
    if (bothOverThePlane(p, clipPlane[1])) {
        return -1;
    }
    sceVu0SubVector(d, p, p + 0x10);
    d[1] = 0.0f;
    if (checkFrontAcross_i(p, d)) {
        sceVu0Normalize(d, d);
        getCrossPoint_i(p + 0x20, p, d);
        if (xzLengthSquare(p + 0x20) < cylinderRadiusSq) {
            AddVectorXYZ(p + 0x20, p + 0x20, d);
            return 1;
        }
    }
    return -1;
}

ChainSet *InitChains(ChainCfg *cfg)
{
    ChainSet *r;
    int i = 0;
    int j;
    float step;

    r = (ChainSet *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(ChainSet, 0x10),
                                   "src/clothAnimation.c", 1192);
    r->cfg = cfg;
    while (cfg[i].num != -1) {
        i++;
    }
    r->num = i;
    r->nodes = (ChainNode *)iosMallocDebug(ios_partition_sugipon, i * sizeof(ChainNode),
                                           "src/clothAnimation.c", 1198);
    r->oddFrame = 0;
    for (i = 0; i < r->num; i++) {
        r->nodes[i].pos =
            iosMallocDebug(ios_partition_sugipon, cfg[i].num * 16, "src/clothAnimation.c", 1202);
        r->nodes[i].vel =
            iosMallocDebug(ios_partition_sugipon, cfg[i].num * 16, "src/clothAnimation.c", 1203);
        r->nodes[i].len =
            iosMallocDebug(ios_partition_sugipon, cfg[i].num * 4, "src/clothAnimation.c", 1204);
        r->nodes[i].exNum = 0;
        for (j = 0; j < 5; j++) {
            r->nodes[i].ex[j].w = -1.0f;
            CopyVector(&r->nodes[i].ex[j].v0, ZeroPoint);
            CopyVector(&r->nodes[i].ex[j].v1, ZeroPoint);
            CopyVector(&r->nodes[i].ex[j].v2, ZeroVector);
        }
        for (j = 0; j < r->cfg[i].num; j++) {
            CopyVector(r->nodes[i].pos[j], cfg[i].pm.root);
            CopyVector(r->nodes[i].vel[j], ZeroVector);
            step = cfg[i].pm.step;
            r->nodes[i].len[j] = step;
            if (j != 0) {
                r->nodes[i].pos[j][1] = r->nodes[i].pos[j - 1][1] + step;
            }
        }
    }
    return r;
}

ClothSet *InitClothes(ClothCfg *cfg)
{
    ClothSet *r;
    int i = 0;
    int m;
    int q;
    float aa[4];
    float bb[4];

    r = (ClothSet *)iosMallocDebug(ios_partition_sugipon, sizeof(ClothSet), "src/clothAnimation.c",
                                   1235);
    debug_StdPrintfDummy("\x1b[36mALLOC CLOTHES\x1b[m\n");
    while (cfg[i].num != -1) {
        i++;
    }
    r->num = i;
    r->rec = (ClothRec *)iosMallocDebug(ios_partition_sugipon, i * sizeof(ClothRec),
                                        "src/clothAnimation.c", 1240);
    for (i = 0; i < r->num; i++) {
        if (cfg[i].tex != 0) {
            r->rec[i].mesh = prim_InitMesh3D(cfg[i].div, cfg[i].num, 1, 0x5C, 0x80808080, 1);
            r->rec[i].textured = 1;
            r->rec[i].tex = *(TexBlob *)tex_GetTextureData(tex_GetTextureNo(cfg[i].tex));
        } else {
            r->rec[i].mesh = prim_InitMesh3D(cfg[i].div, cfg[i].num, 1, 0x4C, 0xFFFFFF80, 1);
            r->rec[i].textured = 0;
        }
        r->rec[i].pos = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                       "src/clothAnimation.c", 1268);
        r->rec[i].vel = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                       "src/clothAnimation.c", 1269);
        r->rec[i].mark = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                        "src/clothAnimation.c", 1270);
        for (m = 0; m < cfg[i].num; m++) {
            r->rec[i].pos[m] = (VECTOR *)(r->rec[i].mesh->pos + m * cfg[i].div);
            r->rec[i].vel[m] = iosMallocDebug(ios_partition_sugipon, cfg[i].div * 16,
                                              "src/clothAnimation.c", 1275);
            r->rec[i].mark[m] =
                iosMallocDebug(ios_partition_sugipon, cfg[i].div * 4, "src/clothAnimation.c", 1276);
            memset(aa, 0, 16);
            aa[3] = 1.0f;
            memset(bb, 0, 16);
            for (q = 0; q < cfg[i].div; q++) {
                CopyVector(&r->rec[i].pos[m][q], aa);
                CopyVector(&r->rec[i].vel[m][q], bb);
                r->rec[i].mark[m][q] = -1;
            }
        }
    }
    return r;
}

ClothSet *InitClothesNoShade(ClothCfg *cfg)
{
    ClothSet *r;
    int i = 0;
    int m;
    int q;
    float aa[4];
    float bb[4];

    r = (ClothSet *)iosMallocDebug(ios_partition_sugipon, sizeof(ClothSet), "src/clothAnimation.c",
                                   1296);
    debug_StdPrintfDummy("\x1b[36mALLOC CLOTHES\x1b[m\n");
    while (cfg[i].num != -1) {
        i++;
    }
    r->num = i;
    r->rec = (ClothRec *)iosMallocDebug(ios_partition_sugipon, i * sizeof(ClothRec),
                                        "src/clothAnimation.c", 1301);
    for (i = 0; i < r->num; i++) {
        if (cfg[i].tex != 0) {
            r->rec[i].mesh = prim_InitMesh3D(cfg[i].div, cfg[i].num, 1, 0x5C, 0x80808080, 0);
            r->rec[i].textured = 1;
            r->rec[i].tex = *(TexBlob *)tex_GetTextureData(tex_GetTextureNo(cfg[i].tex));
        } else {
            r->rec[i].mesh = prim_InitMesh3D(cfg[i].div, cfg[i].num, 1, 0x4C, 0xFFFFFF80, 0);
            r->rec[i].textured = 0;
        }
        r->rec[i].pos = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                       "src/clothAnimation.c", 1329);
        r->rec[i].vel = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                       "src/clothAnimation.c", 1330);
        r->rec[i].mark = iosMallocDebug(ios_partition_sugipon, cfg[i].num * sizeof(void *),
                                        "src/clothAnimation.c", 1331);
        for (m = 0; m < cfg[i].num; m++) {
            r->rec[i].pos[m] = (VECTOR *)(r->rec[i].mesh->pos + m * cfg[i].div);
            r->rec[i].vel[m] = iosMallocDebug(ios_partition_sugipon, cfg[i].div * 16,
                                              "src/clothAnimation.c", 1336);
            r->rec[i].mark[m] =
                iosMallocDebug(ios_partition_sugipon, cfg[i].div * 4, "src/clothAnimation.c", 1337);
            memset(aa, 0, 16);
            aa[3] = 1.0f;
            memset(bb, 0, 16);
            for (q = 0; q < cfg[i].div; q++) {
                CopyVector(&r->rec[i].pos[m][q], aa);
                CopyVector(&r->rec[i].vel[m][q], bb);
                r->rec[i].mark[m][q] = -1;
            }
        }
    }
    return r;
}

void DispClothMesh(ClothRec *rec, void *la, void *lb)
{
    int t;
    dl_SetDLPriority(2);
    p2o_SetDefaultEnviroment();
    prim_UpdateMesh3D(rec->mesh, 5, buffer_ID);
    gif_StartPacketPri(2);
    gif_SetAlpha(1, 7, 0x80);
    gif_SetGsReg(8, 0);
    gif_EndPacket();
    _SetCurrentMatrix(matrixptr + 0x100);
    if (rec->textured != 0) {
        t = tex_GetTextureNo((char *)&rec->tex);
    } else {
        t = -1;
    }
    prim_DispMesh3D(rec->mesh, la, lb, t);
}

/* the wire mesh's three line colours, one word per channel, RGBA: the cross
   links between rows, the general line, and the seam columns (i == 0 and the
   middle column) */
static int wireCrossColor[4] = {0, 32, 128, 128}; /* derived name */

static int wireColor[4] = {128, 64, 0, 128}; /* derived name */

static int wireSeamColor[4] = {0, 128, 0, 128}; /* derived name */

void DispMeshWire(Prim3DVec **rows, int nx, int ny)
{
    int i;
    int j;

    gif_StartPacketPri(0xB);
    gif_SetAlpha(1, 5, 0x80);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < nx; i++) {
        for (j = 1; j < ny; j++) {
            if (i == 0 || i == nx / 2 - 1) {
                DrawLineG(&rows[i][j], wireSeamColor, &rows[i][j - 1], wireSeamColor, 0);
            } else {
                DrawLineG(&rows[i][j], wireColor, &rows[i][j - 1], wireColor, 0);
            }
        }
    }
    for (j = 0; j < ny; j++) {
        if (j == ny - 1) {
            for (i = 1; i < nx; i++) {
                DrawLineG(&rows[i][j], wireColor, &rows[i - 1][j], wireColor, 0);
            }
        } else {
            for (i = 1; i < nx; i++) {
                DrawLineG(&rows[i][j], wireColor, &rows[i - 1][j], wireCrossColor, 0);
            }
        }
    }
    gif_EndPacket();
}

void DispCloth4D(Cloth4D *c, void *la, void *lb)
{
    int t;
    dl_SetDLPriority(1);
    p2o_SetDefaultEnviroment();
    prim_UpdateMesh3D(c->mesh, 3, buffer_ID);
    if (c->cfg->tex != 0) {
        t = tex_GetTextureNo((const char *)&c->tex);
    } else {
        t = -1;
    }
    gif_StartPacketPri(1);
    gif_SetAlpha(1, 7, 0x80);
    gif_SetGsReg(8, 0);
    gif_EndPacket();
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(c->mesh, la, lb, t);
    if (debug_cloth_info != 0) {
        DispMeshWire(c->pos, c->cfg->nx, c->cfg->ny);
    }
}

void DispCloth4DWithAdd(Cloth4D *c, void *la, void *lb)
{
    int t;
    dl_SetDLPriority(1);
    p2o_SetDefaultEnviroment();
    prim_UpdateMesh3D(c->mesh, 3, buffer_ID);
    if (c->cfg->tex != 0) {
        t = tex_GetTextureNo((const char *)&c->tex);
    } else {
        t = -1;
    }
    gif_StartPacketPri(1);
    gif_SetAlpha(1, 5, 0x80);
    gif_SetGsReg(8, 0);
    gif_EndPacket();
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(c->mesh, la, lb, t);
    if (debug_cloth_info != 0) {
        DispMeshWire(c->pos, c->cfg->nx, c->cfg->ny);
    }
}

/* windNoise is the ring of eleven random wind vectors getCloth4D_preProcess
   fills and cycles through, explicitly zeroed; the up and down vectors are
   what getCloth4D applies the node matrix to; cylinderColor is its debug wire
   cylinder's colour; procMatrix is the matrix the nested proc applies */
static sceVu0FVECTOR windNoise[11] = {{0.0f, 0.0f, 0.0f, 0.0f}}; /* derived name */

static sceVu0FVECTOR clothUpVector = {0.0f, 1.0f, 0.0f, 0.0f}; /* derived name */

static sceVu0FVECTOR clothDownVector = {0.0f, -1.0f, 0.0f, 0.0f}; /* derived name */

static int cylinderColor[4] = {32, 64, 128, 128}; /* derived name */

static sceVu0FMATRIX procMatrix = {
    {1.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

/* the two point writers: add, or set, a scaled transformed vector */
static __inline__ void clothAddPoint(void *dst, const void *src, float f) /* derived name */
{
    VECTOR tv;

    _ApplyCurrentMatrix(&tv, src);
    _ScaleVector(&tv, &tv, f);
    _AddVectorXYZ(dst, dst, &tv);
}

static __inline__ void clothSetPoint(void *dst, const void *src, float f) /* derived name */
{
    VECTOR tv;

    _ApplyCurrentMatrix(&tv, src);
    _ScaleVectorXYZ(dst, &tv, f);
}

static void getCloth4D_preProcess(Cloth4D *c, float g, float damp, float z, float w, int tight,
                                  void *qa, void *qb)
{
    VECTOR dv;
    float work[4][4];
    int i;
    int j;
    Prim3DVec **pos = c->pos;
    Prim3DVec **nrm = c->nrm;
    Prim3DVec **vel = c->vel;
    Cloth4DCfg *cfg = c->cfg;
    int nx = cfg->nx - (cfg->wrap != 0);
    int ny = cfg->ny;
    VECTOR va[nx];
    float rz;
    float t;

    for (i = 0; i < nx; i++) {
        for (j = 1; j < ny; j++) {
            sceVu0ScaleVector(&dv, &vel[i][j], damp);
            CopyVector(&vel[i][j], &pos[i][j]);
            AddVectorXYZ(&pos[i][j], &pos[i][j], &dv);
        }
    }
    for (i = 0; i < nx; i++) {
        for (j = 0; j < 2; j++) {
            int n = cfg->cols[i].link[j].node;
            Cloth4DCol *pt;
            float f;
            Sub15C *sk;

            if (n == -1) {
                break;
            }
            pt = &cfg->cols[i];
            f = pt->link[j].weight;
            sk = GOBJ_SUB(c->gobj);
            _MulMatrix(work, (char *)sk->nodeMtx + n * 64, sk->clusterMtx + n * 64);
            _SetCurrentMatrix(work);
            if (j == 0) {
                clothSetPoint(pos[i], pt->pos, f);
                clothSetPoint(nrm[i], pt->dir, f);
                clothSetPoint(&va[i], pt->restDir, f);
            } else {
                clothAddPoint(pos[i], pt->pos, f);
                clothAddPoint(nrm[i], pt->dir, f);
                clothAddPoint(&va[i], pt->restDir, f);
            }
        }
        pos[i]->w = 1.0f;
        nrm[i]->w = 1.0f;
        va[i].w = 0.0f;
    }
    {
        float mx[16];
        float wpow;
        int c;

        c = 0;
        _ScaleVectorXYZ(&work[0], GetWindVector(&wpow, &pos[0]->x), w);
        wpow = wpow * (w * 2.44140625e-05f);
        for (i = 0; i < 11; i++) {
            for (j = 0; j < 3; j++) {
                windNoise[i][j] = wpow * (float)((rand() & 0x7FFF) - 16383);
            }
        }
        rz = 1.0f / (float)(ny - 1);
        GetInverseQuaternion(&work[2], qa);
        MultiQuaternion(&work[1], qa, qb);
        MultiQuaternion(&work[1], &work[1], &work[2]);
        GetMatrixFromQuaternion((char *)mx, (char *)&work[1]);
        for (j = 1; j < ny; j++) {
            t = (float)(ny - j) * rz * z + (1.0f - z);
            t = t * t;
            if (tight) {
                for (i = 0; i < nx; i++) {
                    _ApplyMatrix(&va[i], mx, &va[i]);
                }
            }
            for (i = 0; i < nx; i++) {
                SubVectorXYZ(&work[3], &pos[i][j], &pos[i][j - 1]);
                AddVectorXYZ(&work[3], &work[3], &work[0]);
                AddVectorXYZ(&work[3], &work[3], windNoise[c]);
                c = c + 1;
                if (c == 11) {
                    c = 0;
                }
                _InterVectorXYZ(&work[3], &va[i], &work[3], t);
                work[3][1] = work[3][1] + g;
                AddVectorXYZ(&pos[i][j], &pos[i][j - 1], &work[3]);
            }
        }
    }
}

/* the four procMatrix elements proc writes its Y rotation through */
static float *procCosXX = &procMatrix[0][0]; /* derived name */

static float *procCosZZ = &procMatrix[2][2]; /* derived name */

static float *procSinXZ = &procMatrix[0][2]; /* derived name */

static float *procSinZX = &procMatrix[2][0]; /* derived name */

static __inline__ float fSqrtInv_i(float x) /* derived name */
{
#ifdef ICO_HOST
    return ps2_rsqrt(1.0f, x);
#else
    float r;

    __asm__ __volatile__("mfc1 $8, %1\n\t"
                         "qmtc2.ni $8, $vf4\n\t"
                         "vrsqrt Q, $vf0w, $vf4x\n\t"
                         "vwaitq\n\t"
                         "cfc2.ni $2, $vi22\n\t"
                         "mtc1 $2, %0"
                         : "=f"(r)
                         : "f"(x)
                         : "$2", "$8");
    return r;
#endif
}

static __inline__ float xzInvLength_i(const void *v) /* derived name */
{
#ifdef ICO_HOST
    return ps2_rsqrt(1.0f, ico_xz_length_square(v));
#else
    float r;

    __asm__ __volatile__("lqc2 $vf4, 0x0(%1)\n\t"
                         "vmul.xz $vf4, $vf4, $vf4\n\t"
                         "vaddz.x $vf4, $vf4, $vf4z\n\t"
                         "vrsqrt Q, $vf0w, $vf4x\n\t"
                         "vwaitq\n\t"
                         "cfc2.ni $2, $vi22\n\t"
                         "mtc1 $2, %0"
                         : "=f"(r)
                         : "r"(v)
                         : "$2");
    return r;
#endif
}

static __inline__ void scaleVectorXZ_i(void *d, const void *s, float k) /* derived name */
{
#ifdef ICO_HOST
    ico_scale_xz(d, s, k);
#else
    __asm__ __volatile__("lqc2 $vf4, 0x0(%1)\n\t"
                         "mfc1 $8, %2\n\t"
                         "qmtc2.ni $8, $vf5\n\t"
                         "vmulx.xz $vf4, $vf4, $vf5x\n\t"
                         "sqc2 $vf4, 0x0(%0)"
                         :
                         : "r"(d), "r"(s), "f"(k)
                         : "$8");
#endif
}

static __inline__ float subAndGetInvLength_i(void *d, const void *a,
                                             const void *b) /* derived name */
{
#ifdef ICO_HOST
    return ico_sub_inv_length(d, a, b);
#else
    float inv;

    __asm__ __volatile__("lqc2 $vf1, 0x0(%1)\n\t"
                         "lqc2 $vf2, 0x0(%2)\n\t"
                         "vsub.xyzw $vf4, $vf1, $vf2\n\t"
                         "vmul.xyz $vf3, $vf4, $vf4\n\t"
                         "vaddy.x $vf3, $vf3, $vf3y\n\t"
                         "vaddz.x $vf3, $vf3, $vf3z\n\t"
                         "vrsqrt Q, $vf0w, $vf3x\n\t"
                         "sqc2 $vf4, 0x0(%3)\n\t"
                         "vwaitq\n\t"
                         "cfc2.ni $2, $vi22\n\t"
                         "mtc1 $2, %0"
                         : "=f"(inv)
                         : "r"(a), "r"(b), "r"(d)
                         : "$2");
    return inv;
#endif
}

static __inline__ void scaleAndAddVectorXYZ_i(void *d, const void *a, const void *b,
                                              float k) /* derived name */
{
#ifdef ICO_HOST
    ico_scale_add_xyz(d, a, b, k);
#else
    __asm__ __volatile__("lqc2 $vf4, 0x0(%1)\n\t"
                         "lqc2 $vf5, 0x0(%2)\n\t"
                         "mfc1 $8, %3\n\t"
                         "qmtc2.ni $8, $vf6\n\t"
                         "vmulx.xyz $vf5, $vf5, $vf6x\n\t"
                         "vadd.xyz $vf4, $vf4, $vf5\n\t"
                         "sqc2 $vf4, 0x0(%0)"
                         :
                         : "r"(d), "r"(a), "r"(b), "f"(k)
                         : "$8");
#endif
}

static __inline__ void tensionMove_i(void *out, const void *a, const void *b, float k,
                                     float lim) /* derived name */
{
    VECTOR buf;
    float inv = subAndGetInvLength_i(&buf, a, b);

    if (inv < lim) {
        scaleAndAddVectorXYZ_i(out, b, &buf, k * inv);
    }
}

/* the cylinder's two cap planes and squared radius, set from one collision
   point */
static __inline__ void setClipCylinder(ClothPoint *pt) /* derived name */
{
    clipPlane[0][0][3] = pt->bottom;
    clipPlane[1][0][3] = -pt->top;
    cylinderRadiusSq = pt->radius * pt->radius;
}

/* getCloth4D's collision step.  In the original, proc (and hit inside it)
   were GNU nested functions in getCloth4D; they are at file scope so clang
   compiles them, and what they captured (the collision cylinders' matrices
   and cap planes, and the current tension step tk, t1 = tk * tk and
   tlim = 1 / tk, which the sweep loops update between calls) is passed in a
   Cloth4DProcCtx.  proc reads only p, q and k.  Each caller also passes
   own, the owner of q's point that its test has just loaded. */
typedef struct Cloth4DProcCtx {
    int cnt;
    ClothPoint *pts;
    sceVu0FMATRIX *mD;
    sceVu0FMATRIX *mF;
    VECTOR *vG;
    VECTOR *vH;
    float *t1;
    float *tk;
    float *tlim;
} Cloth4DProcCtx; /* derived name */

static __inline__ int hit(Cloth4DProcCtx *c, Prim3DVec *p, Prim3DVec *q, float k,
                          int i) /* derived name */
{
    VECTOR a;
    VECTOR b;
    float r2;
    float r;

    if (checkOverThePlane_i(p, &c->vG[i]))
        return 0;
    if (checkOverThePlane_i(p, &c->vH[i]))
        return 0;
    r = c->pts[i].radius;
    r2 = r * r;
    if (*c->t1 < distance_squared(p, q)) {
        tensionMove_i(p, p, q, *c->tk, *c->tlim);
        _ApplyMatrix(&a, c->mF[i], p);
        if (xzLengthSquare(&a) < r2) {
            float rr = (r + *c->tk) * (r + *c->tk);
            float len;

            _ApplyMatrix(&b, c->mF[i], q);
            len = xzLengthSquare(&b);
            if (len < rr) {
                float d = r2 - *c->t1;
                float inv;
                float e;
                float s;
                float ir;
                float sy;
                float sn;

                inv = fSqrtInv_i(len);
                e = (len + d) * c->pts[i].invDiameter * inv;
                s = FSqrt(1.0f - e * e);
                ir = r * inv;
                sy = a.y;
                *procCosXX = *procCosZZ = e * ir;
                sn = k * c->pts[i].turn * s * ir;
                *procSinXZ = sn;
                *procSinZX = -sn;
                _ApplyMatrix(&a, procMatrix, &b);
                a.y = sy;
                _ApplyMatrix(p, c->mD[i], &a);
                return 1;
            }
        }
    } else {
        float len;

        _ApplyMatrix(&a, c->mF[i], p);
        len = xzLengthSquare(&a);
        if (len < r2) {
            VECTOR v;

            scaleVectorXZ_i(&v, &a, r * fSqrtInv_i(len));
            _ApplyMatrix(p, c->mD[i], &v);
            return 1;
        }
    }
    return 0;
}

static int proc(Cloth4DProcCtx *c, Prim3DVec *p, Prim3DVec *q, Prim3DVec *qa, Prim3DVec *qb,
                float k, int own)
{
    int i;
    int ret = -1;

    for (i = 0; i < c->cnt; i++) {
        if (hit(c, p, q, k, i))
            ret = i;
    }
    if (ret != -1)
        return ret;
    tensionMove_i(p, p, q, *c->tk, *c->tlim);
    return -1;
}

static void getCloth4D(Cloth4D *c, int **rows)
{
    Prim3DVec **pos = c->pos;
    Prim3DVec **vel = c->vel;
    Cloth4DCfg *cfg = c->cfg;
    int wrap = cfg->wrap;
    int nx = cfg->nx - (wrap != 0);
    int ny = cfg->ny;
    int cnt = c->collision ? c->colNum : 0;
    ClothPoint *pts = c->col;
    float scale = GOBJ_SUB(c->gobj)->nodes->scale[0];
    float inv = 1.0f / scale;
    float tbase = cfg->colSpacing * scale;
    int nyArr[ny];
    int nxArr[nx];
    sceVu0FMATRIX mC[cnt];
    sceVu0FMATRIX mD[cnt];
    sceVu0FMATRIX mE[cnt];
    sceVu0FMATRIX mF[cnt];
    VECTOR vG[cnt];
    VECTOR vH[cnt];
    sceVu0FMATRIX *pD;
    sceVu0FMATRIX *pC;
    sceVu0FMATRIX *pF;
    sceVu0FMATRIX *pE;
    sceVu0FMATRIX mtx;
    VECTOR clip[3];
    /* read only by the DEBUG build's drawing of each cylinder's clip planes
       after the collision pass */
    VECTOR work[9];
    float t1;
    float tk;
    float tlim;
    int i;
    int j;
    int n;

    pD = mD;
    pC = mC;
    pF = mF;
    pE = mE;
    _UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixZ(-0x4000);
    MatrixDrive_ScaleMatrix(inv, inv, inv);
    CopyMatrix(mtx, MatrixDrive_GetMatrix());
    for (i = 0; i < cnt; i++, pD++, pC++, pF++, pE++) {
        CopyVector(mtx[3], pts[i].pos);
        _MulMatrix(pD, (char *)GOBJ_SUB(c->gobj)->nodeMtx + c->colNode[i] * 64, mtx);
        _MulMatrix(pC, c->colMtx[i], mtx);
        MatrixDrive_SetTransposeMatrix(pE, pC);
        MatrixDrive_SetTransposeMatrix(pF, pD);
        _ApplyMatrix(&vG[i], pD, clothUpVector);
        vG[i].w = -pts[i].top - _InnerProduct(&vG[i], (*pD)[3]);
        _ApplyMatrix(&vH[i], pD, clothDownVector);
        vH[i].w = pts[i].bottom - _InnerProduct(&vH[i], (*pD)[3]);
    }
    if (debug_cloth_info) {
        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 0x80);
        gif_SetZWrite(0);
        gif_SetZTest(1);
        for (i = 0; i < cnt; i++) {
            CopyMatrix(MatrixDrive_GetMatrix(), mD[i]);
            prim_DispWireYCylinder(cylinderColor, 16, 0, pts[i].radius, pts[i].bottom, pts[i].top);
        }
        gif_EndPacket();
    }
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 0x80);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    {
        sceVu0FMATRIX *qF = mF;
        sceVu0FMATRIX *qE = mE;
        sceVu0FMATRIX *qD = mD;

        for (i = 0; i < nx; i++) {
            nxArr[i] = -1;
        }
        for (j = 1; j < ny; j++) {
            nyArr[j] = 0;
        }
        for (n = 0; n < cnt; n++, qF++, qE++, qD++) {
            setClipCylinder(&pts[n]);
            for (i = 0; i < nx; i++) {
                Prim3DVec *pb = pos[i] + 1;
                Prim3DVec *pc = vel[i] + 1;

                for (j = 1; j < ny; j++, pb++, pc++) {
                    _ApplyMatrix(&clip[0], qE, pc);
                    _ApplyMatrix(&clip[1], qF, pb);
                    if (clipCylinderCollision((char *)clip, &pts[n]) != -1) {
                        VECTOR tbuf;

                        scaleVectorXZ_i(&tbuf, &clip[2], pts[n].radius * xzInvLength_i(&clip[2]));
                        _ApplyMatrix(pb, qD, &tbuf);
                        nyArr[j] = i;
                        nxArr[i] = j;
                        rows[i][j] = n;
                    }
                }
            }
        }
    }
    gif_EndPacket();
#ifdef DEBUG
    for (i = 0; i < cnt; i++) {
        _ScaleVector(&work[0], &vG[i], 100.0f);
        _AddVectorXYZ(&work[1], mD[i][3], &work[0]);
        _ScaleVector(&work[2], &vH[i], 100.0f);
        _AddVectorXYZ(&work[3], mD[i][3], &work[2]);
        DrawLine((char *)mD[i][3], (char *)&work[1], (LineColor *)&chainLineColor0, 0);
        DrawLine((char *)mD[i][3], (char *)&work[3], (LineColor *)&chainLineColor1, 0);
    }
#endif
    {
        Cloth4DProcCtx pc;

        pc.cnt = cnt;
        pc.pts = pts;
        pc.mD = mD;
        pc.mF = mF;
        pc.vG = vG;
        pc.vH = vH;
        pc.t1 = &t1;
        pc.tk = &tk;
        pc.tlim = &tlim;

        if (wrap) {
            if (c->sweepRight) {
                for (j = 1; j < ny; j++) {
                    int s = nyArr[j];
                    tk = tbase * ((float)j * 0.2f / (float)ny + 1.0f);
                    t1 = tk * tk;
                    tlim = 1.0f / tk;
                    for (i = 1; i < nx + 2; i++) {
                        int x;
                        int y;
                        int xm;
                        int x0;
                        int xp;

                        x = s + i + nx;
                        y = s + nx * 3 - i;
                        xm = (x - 1) % nx;
                        x0 = x % nx;
                        xp = (x + 1) % nx;
                        if (rows[xm][j] != -1 || rows[x0][j] == -1) {
                            rows[x0][j] = proc(&pc, &pos[x0][j], &pos[xm][j], &pos[xp][j],
                                               &pos[x0][j - 1], -1.0f, rows[xm][j]);
                        }
                        xm = (y + 1) % nx;
                        x0 = y % nx;
                        xp = (y - 1) % nx;
                        if (rows[xm][j] != -1 || rows[x0][j] == -1) {
                            rows[x0][j] = proc(&pc, &pos[x0][j], &pos[xm][j], &pos[xp][j],
                                               &pos[x0][j - 1], 1.0f, rows[xm][j]);
                        }
                    }
                }
            } else {
                for (j = 1; j < ny; j++) {
                    int s = nyArr[j];
                    tk = tbase * ((float)j * 0.2f / (float)ny + 1.0f);
                    t1 = tk * tk;
                    tlim = 1.0f / tk;
                    for (i = 1; i < nx + 2; i++) {
                        int x;
                        int y;
                        int ym;
                        int y0;
                        int yp;

                        x = s + i + nx;
                        y = s + nx * 3 - i;
                        yp = (y + 1) % nx;
                        y0 = y % nx;
                        ym = (y - 1) % nx;
                        if (rows[yp][j] != -1 || rows[y0][j] == -1) {
                            rows[y0][j] = proc(&pc, &pos[y0][j], &pos[yp][j], &pos[ym][j],
                                               &pos[y0][j - 1], 1.0f, rows[yp][j]);
                        }
                        yp = (x - 1) % nx;
                        y0 = x % nx;
                        ym = (x + 1) % nx;
                        if (rows[yp][j] != -1 || rows[y0][j] == -1) {
                            rows[y0][j] = proc(&pc, &pos[y0][j], &pos[yp][j], &pos[ym][j],
                                               &pos[y0][j - 1], -1.0f, rows[yp][j]);
                        }
                    }
                }
            }
        } else {
            tk = tbase;
            t1 = tbase * tbase;
            tlim = 1.0f / tbase;
            if (c->sweepRight) {
                for (j = 1; j < ny; j++) {
                    for (i = 1; i < nx; i++) {
                        int x;
                        int y;
                        int ym;
                        int y0;
                        int yp;

                        x = i;
                        y = nx - 1 - i;
                        yp = y + 1;
                        y0 = y;
                        ym = y - 1;
                        if (rows[yp][j] != -1 || rows[y0][j] == -1) {
                            rows[y0][j] = proc(&pc, &pos[y0][j], &pos[yp][j], &pos[ym][j],
                                               &pos[y0][j - 1], 1.0f, rows[yp][j]);
                        }
                        yp = x - 1;
                        y0 = x;
                        ym = x + 1;
                        if (rows[yp][j] != -1 || rows[y0][j] == -1) {
                            rows[y0][j] = proc(&pc, &pos[y0][j], &pos[yp][j], &pos[ym][j],
                                               &pos[y0][j - 1], -1.0f, rows[yp][j]);
                        }
                    }
                }
            } else {
                for (j = 1; j < ny; j++) {
                    for (i = 1; i < nx; i++) {
                        int x;
                        int y;
                        int xm;
                        int x0;
                        int xp;

                        x = i;
                        y = nx - 1 - i;
                        xm = x - 1;
                        x0 = x;
                        xp = x + 1;
                        if (rows[xm][j] != -1 || rows[x0][j] == -1) {
                            rows[x0][j] = proc(&pc, &pos[x0][j], &pos[xm][j], &pos[xp][j],
                                               &pos[x0][j - 1], -1.0f, rows[xm][j]);
                        }
                        xm = y + 1;
                        x0 = y;
                        xp = y - 1;
                        if (rows[xm][j] != -1 || rows[x0][j] == -1) {
                            rows[x0][j] = proc(&pc, &pos[x0][j], &pos[xm][j], &pos[xp][j],
                                               &pos[x0][j - 1], 1.0f, rows[xm][j]);
                        }
                    }
                }
            }
        }
        for (i = 0; i < nx; i++) {
            float len = cfg->cols[i].length * scale;
            float linv = 1.0f / len;
            int last = nxArr[i];

            for (j = 1; j < ny; j++) {
                tensionMove_i(&pos[i][j], &pos[i][j], &pos[i][j - 1], len, linv);
            }
            if (last < 0) {
                for (j = ny - 2; j > 0; j--) {
                    tensionMove_i(&pos[i][j], &pos[i][j], &pos[i][j + 1], len, linv);
                }
            } else {
                for (j = last - 1; j > 0; j--) {
                    tensionMove_i(&pos[i][j], &pos[i][j], &pos[i][j + 1], len, linv);
                }
            }
        }
    }
}

static void getCloth4D_postProcess(Cloth4D *c, int **rows)
{
    float buf[4];
    int i;
    int j;
    Prim3DVec **pos = c->pos;
    Prim3DVec **nrm = c->nrm;
    Cloth4DCfg *cfg = c->cfg;
    Prim3DVec **vel = c->vel;
    int ny = cfg->ny;
    int nx = cfg->nx - (cfg->wrap != 0);

    for (i = 0; i < nx; i++) {
        for (j = 1; j < ny; j++) {
            if (rows[i][j] == -1) {
                SubVectorXYZ(&vel[i][j], &pos[i][j], &vel[i][j]);
            } else {
                CopyVector(&vel[i][j], ZeroVector);
            }
        }
    }
    if (cfg->wrap != 0) {
        memset(buf, 0, 0x10);
        for (j = 0; j < ny; j++) {
            sceVu0AddVector(&pos[nx][j], &pos[0][j], buf);
        }
        CopyVector(nrm[nx], nrm[0]);
    }
    for (i = 0; i < cfg->nx; i++) {
        for (j = 1; j < ny; j++) {
            CopyVector(&nrm[i][j], nrm[i]);
        }
    }
    for (i = 0; i < c->colNum; i++) {
        CopyMatrix(c->colMtx[i], (char *)GOBJ_SUB(c->gobj)->nodeMtx + c->colNode[i] * 64);
    }
}

/* declared here: motionOrientManager.h reaches ico2/fumi's files through
   typedef.h, and commonact.c declares the table char [] */
extern const MotionDef motionKind[];

static void _getCloth4D(Cloth4D *c, float x, float y, float z, float w, int tight, void *qa,
                        void *qb)
{
    float buf[4];
    int i;
    int j;
    int i2;
    int j2;
    int ny = c->cfg->ny;
    int nx = c->cfg->nx;
    int data[nx][ny];
    int *rows[nx];
    Sub15C *obj;
    Cloth4DCfg *cfg;
    ICO_WORD *rows2;
    int nx2;
    int ny2;
    Vec16 *plane;

    for (i = 0; i < nx; i++) {
        rows[i] = data[i];
        for (j = 0; j < ny; j++) {
            rows[i][j] = -1;
        }
    }
    getCloth4D_preProcess(c, x, y, z, w, tight, qa, qb);
    getCloth4D(c, rows);
    obj = GOBJ_SUB(c->gobj);
    if (motionKind[obj->ctrl.motion].flags.bits.clothPlane) {
        plane = &obj->root.plane;
        cfg = c->cfg;
        rows2 = (ICO_WORD *)c->pos;
        nx2 = cfg->nx - (cfg->wrap != 0);
        ny2 = cfg->ny;
        for (i2 = 0; i2 < nx2; i2++) {
            for (j2 = 1; j2 < ny2; j2++) {
                char *p = (char *)(j2 * 16 + rows2[i2]);
                float d = plane_distance(p, plane);
                if (d < 0.0f) {
                    _ScaleVector(buf, plane, d);
                    _SubVectorXYZ(p, p, buf);
                }
            }
        }
    }
    getCloth4D_postProcess(c, rows);
}

void GetCloth4D(Cloth4D *c, float x, float y)
{
    _getCloth4D(c, x, y, 1.0f, 1.0f, 0, IdentityQuaternion, IdentityQuaternion);
}

void GetCloth4DWithDetail(Cloth4D *c, float x, float y, float z, float w)
{
    _getCloth4D(c, x, y, z, w, 0, IdentityQuaternion, IdentityQuaternion);
}

/* the two pointers follow the four floats, like _getCloth4D's own tail */
void GetCloth4DWithTight(Cloth4D *c, float x, float y, float z, float w, void *qa, void *qb)
{
    _getCloth4D(c, x, y, z, w, 1, qa, qb);
}

typedef struct { /* field names derived */
    long long q[8];
} Blob64; /* derived name */

Cloth4D *InitCloth4D(GObj *gobj, Cloth4DCfg *cfg, ClothHangCfg *tbl)
{
    Cloth4D *r;
    int i;
    int j;
    float sc;

    r = (Cloth4D *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(Cloth4D, 0x300),
                                  "src/clothAnimation.c", 2183);
    r->gobj = gobj;
    r->cfg = cfg;
    r->sweepRight = 0;
    if (cfg->tex != 0) {
        r->mesh = prim_InitMesh3D(cfg->ny, cfg->nx, 1, 0x5C, 0x80808080, 1);
        r->tex = *(TexBlob *)tex_GetTextureData(tex_GetTextureNo(cfg->tex));
    } else {
        r->mesh = prim_InitMesh3D(cfg->ny, cfg->nx, 1, 0x4C, 0xFFFFFF80, 1);
    }
    r->pos = (Prim3DVec **)iosMallocDebug(ios_partition_sugipon, cfg->nx * sizeof(Prim3DVec *),
                                          "src/clothAnimation.c", 2218);
    r->vel = (Prim3DVec **)iosMallocDebug(ios_partition_sugipon, cfg->nx * sizeof(Prim3DVec *),
                                          "src/clothAnimation.c", 2219);
    r->nrm = (Prim3DVec **)iosMallocDebug(ios_partition_sugipon, cfg->nx * sizeof(Prim3DVec *),
                                          "src/clothAnimation.c", 2220);
    for (i = 0; i < cfg->nx; i++) {
        r->pos[i] = r->mesh->pos + i * cfg->ny;
        r->vel[i] = (Prim3DVec *)iosMallocDebug(ios_partition_sugipon, cfg->ny * 16,
                                                "src/clothAnimation.c", 2224);
        r->nrm[i] = r->mesh->nrm + i * cfg->ny;
        for (j = 0; j < cfg->ny; j++) {
            CopyVector(&r->pos[i][j], ZeroPoint);
            CopyVector(&r->vel[i][j], ZeroVector);
            r->mesh->st[i * cfg->ny + j].x = cfg->cols[i].uv[j][0];
            r->mesh->st[i * cfg->ny + j].y = 1.0f - cfg->cols[i].uv[j][1];
        }
    }
    prim_UpdateMesh3D(r->mesh, 8, buffer_ID);
    prim_UpdateMesh3D(r->mesh, 8, (buffer_ID + 1) & 1);
    if (tbl != 0) {
        i = 0;
        sc = GOBJ_SUB(r->gobj)->nodes->scale[0];
        while (tbl[i].enable != -1) {
            i++;
        }
        r->colNum = i;
        r->col = iosMallocDebug(ios_partition_sugipon, i * 0x40, "src/clothAnimation.c", 2253);
        r->colMtx =
            iosMallocDebug(ios_partition_sugipon, r->colNum * 0x40, "src/clothAnimation.c", 2254);
        r->colNode =
            iosMallocDebug(ios_partition_sugipon, r->colNum * 4, "src/clothAnimation.c", 2255);
        for (i = 0; tbl[i].enable != -1; i++) {
            *(Blob64 *)&r->col[i] = *(Blob64 *)&tbl[i];
            r->col[i].radius = r->col[i].radius * sc;
            r->col[i].top = r->col[i].top * sc;
            r->col[i].bottom = r->col[i].bottom * sc;
            r->col[i].invDiameter = 1.0f / (r->col[i].radius + r->col[i].radius);
            sceVu0UnitMatrix(r->colMtx[i]);
            r->colNode[i] = GetSkeltonFocusNode(gobj, tbl[i].node);
        }
        r->collision = 1;
    } else {
        r->colNum = 0;
        r->collision = 0;
    }
    return r;
}

void GetChainNodeGlobalQuaternion(void *q, ChainNode *node, int count)
{
    ClothBuf buf;
    SetIdentityQuaternion(q);
    if (count > 0) {
        float (*pts)[4] = node->pos;
        SubVectorXYZ(&buf, pts[count], pts[count - 1]);
        MatrixDrive_GetTurnYAngleXZ(&buf.a, &buf.b, buf.v[0], buf.v[1], buf.v[2]);
        RotQuaternionX(q, (short)-buf.a);
        RotQuaternionZ(q, (short)-buf.b);
    }
}

void MoveChainExtendedWeight(ChainNode *node, int slot, float f)
{
    node->ex[slot].w = f;
}

void InitChainVelocity(ChainSet *sys)
{
    int i;
    int j;
    int k;

    for (i = 0; i < sys->num; i++) {
        int cnt = sys->cfg[i].num;
        for (j = 0; j < cnt; j++) {
            CopyVector(sys->nodes[i].vel[j], ZeroVector);
        }
        for (k = 0; k < 5; k++) {
            ExW *w = &sys->nodes[i].ex[k];
            CopyVector(&w->v2, ZeroVector);
        }
    }
}

void DeleteChainExtendedWeight(ChainNode *node, int slot)
{
    node->ex[slot].w = -1.0f;
    node->exNum = node->exNum - 1;
}

float GetChainNodeID(ChainCfg *cfg, float f)
{
    return f / cfg->pm.step;
}

void ResetClothAnimation(VECTOR **pos, VECTOR **vel, ClothCfg *cfg)
{
    int outer = cfg->num;
    int inner = cfg->div;
    int i = 0;
    int j;

    if (outer > 0) {
        do {
            for (j = 1; j < inner; j++) {
                VECTOR *p = pos[i];
                CopyVector(&p[j], p);
                CopyVector(&vel[i][j], ZeroVector);
            }
            i++;
        } while (i < outer);
    }
}

void GetChainExWeightGlobalQuaternion(float *q, ChainNode *node, int i, int j)
{
    ClothBuf buf;
    SetIdentityQuaternion(q);
    SubVectorXYZ(&buf, &node->ex[j].v1, &node->ex[i].v0);
    buf.v[1] = buf.v[1] + 100.0f;
    MatrixDrive_GetTurnYAngleXZ(&buf.a, &buf.b, buf.v[0], buf.v[1], buf.v[2]);
    RotQuaternionX(q, (short)-buf.a);
    RotQuaternionZ(q, (short)-buf.b);
}

float GetChainCollision(ChainSet *sys, void *pos, float r)
{
    int i;
    int j;

    r = r * r;
    for (i = 0; i < sys->num; i++) {
        float (*pts)[4] = sys->nodes[i].pos;
        for (j = 0; j < sys->cfg[i].num - 1; j++) {
            if (distance_squared(pts[j], pos) < r) {
                return (float)j * sys->cfg[i].pm.step;
            }
        }
    }
    return -1.0f;
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

void FSqrtInv(void)
{
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 4);
    VU0_NOREORDER_END();
    VU0_REG("vrsqrt Q, $vf0w, $vf4x");
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

float getXZLength(void *v)
{
    VU0_LSV(lqc2, 4, 0x0, 4);
    VU0_V3OP(vmul.xz, 4, 4, 4);
    VU0_V3OP_BC(vaddz.x, 4, 4, 4, z);
    VU0_WORD(0x4A0403BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

float getXZInvLength(void *v)
{
    VU0_LSV(lqc2, 4, 0x0, 4);
    VU0_V3OP(vmul.xz, 4, 4, 4);
    VU0_V3OP_BC(vaddz.x, 4, 4, 4, z);
    VU0_REG("vrsqrt Q, $vf0w, $vf4x");
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

float getXZLengthSquare(void *v)
{
    VU0_LSV(lqc2, 4, 0x0, 4);
    VU0_V3OP(vmul.xz, 4, 4, 4);
    VU0_V3OP_BC(vaddz.x, 4, 4, 4, z);
    VU0_QMFC2_NI(2, 4);
    VU0_MTC1(2, 0);
}

float subAndGetInvLength(void *d, const void *a, const void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP(vsub.xyzw, 4, 1, 2);
    VU0_V3OP(vmul.xyz, 3, 4, 4);
    VU0_V3OP_BC(vaddy.x, 3, 3, 3, y);
    VU0_V3OP_BC(vaddz.x, 3, 3, 3, z);
    VU0_REG("vrsqrt Q, $vf0w, $vf3x");
    VU0_LSV(sqc2, 4, 0x0, 4);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

void scaleAndAddVectorXYZ(void *d, const void *a, const void *b, float k)
{
    VU0_LSV(lqc2, 4, 0x0, 5);
    VU0_LSV(lqc2, 5, 0x0, 6);
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 6);
    VU0_NOREORDER_END();
    VU0_V3OP_BC(vmulx.xyz, 5, 5, 6, x);
    VU0_V3OP(vadd.xyz, 4, 4, 5);
    VU0_LSV(sqc2, 4, 0x0, 4);
}

void scaleVectorXZ(void *d, const void *s, float k)
{
    VU0_LSV(lqc2, 4, 0x0, 5);
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 5);
    VU0_NOREORDER_END();
    VU0_V3OP_BC(vmulx.xz, 4, 4, 5, x);
    VU0_LSV(sqc2, 4, 0x0, 4);
}

void tensionMoveNoReduce(void *out, void *a, void *b, float k)
{
    int buf[4];
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_REG("vsub.xyzw $vf4, $vf1, $vf2");
    VU0_V3OP(vmul.xyz, 3, 4, 4);
    VU0_V3OP_BC(vaddy.x, 3, 3, 3, y);
    VU0_V3OP_BC(vaddz.x, 3, 3, 3, z);
    VU0_REG("vrsqrt Q, $vf0w, $vf3x");
    __asm__ __volatile__(".set noreorder\n\tsqc2 $vf4, %0\n\t.set reorder"
                         : "=m"(buf)
                         :
                         : "memory");
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_REG("mul.s $f12, $f12, $f0");
    VU0_NOREORDER_END();

    VU0_LSV(lqc2, 4, 0x0, 6);
    __asm__ __volatile__(".set noreorder\n\tlqc2 $vf5, %0\n\t.set reorder"
                         :
                         : "m"(buf[0])
                         : "memory");
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 6);
    VU0_NOREORDER_END();
    VU0_REG("vmulx.xyz $vf5, $vf5, $vf6x");
    VU0_V3OP(vadd.xyz, 4, 4, 5);
    VU0_LSV(sqc2, 4, 0x0, 4);
}

void tensionMove(void *out, void *a, void *b, float k, float lim)
{
    int buf[4];
    float inv;
    float scale;
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_REG("vsub.xyzw $vf4, $vf1, $vf2");
    VU0_V3OP(vmul.xyz, 3, 4, 4);
    VU0_V3OP_BC(vaddy.x, 3, 3, 3, y);
    VU0_V3OP_BC(vaddz.x, 3, 3, 3, z);
    VU0_REG("vrsqrt Q, $vf0w, $vf3x");
    __asm__ __volatile__(".set noreorder\n\tsqc2 $vf4, %0\n\t.set reorder"
                         : "=m"(buf)
                         :
                         : "memory");
    VU0_WAIT();
    __asm__ __volatile__(".set noreorder\n"
                         "cfc2.ni $2, $vi22\n"
                         "mtc1 $2, %0\n"
                         ".set reorder\n"
                         : "=f"(inv)::"$2");
    if (inv < lim) {
        scale = k * inv;
        VU0_LSV(lqc2, 4, 0x0, 6);
        __asm__ __volatile__(".set noreorder\n\tlqc2 $vf5, %0\n\t.set reorder"
                             :
                             : "m"(buf[0])
                             : "memory");
        __asm__ __volatile__(".set noreorder\n"
                             "mfc1 $8, %0\n"
                             "qmtc2.ni $8, $vf6\n"
                             ".set reorder\n" ::"f"(scale)
                             : "$8");
        VU0_REG("vmulx.xyz $vf5, $vf5, $vf6x");
        VU0_V3OP(vadd.xyz, 4, 4, 5);
        VU0_LSV(sqc2, 4, 0x0, 4);
    }
}

#endif /* ICO_HOST: port/math */

void getCrossPoint(void *out, void *seg, void *plane)
{
    getCrossPoint_i(out, seg, plane);
}

int checkOverThePlane(void *pt, void *plane)
{
    return checkOverThePlane_i(pt, plane);
}

int checkFrontAcross(void *seg, void *plane)
{
    if (0.0f <= plane_distance(seg, plane)) {
        if (plane_distance((char *)seg + 0x10, plane) < 0.0f)
            return 1;
    }
    return 0;
}

void LockZAnimation(ChainSet *sys)
{
    int i;
    int j;
    int n = sys->num;

    for (i = 0; i < n; i++) {
        int cnt = sys->cfg[i].num;
        for (j = 0; j < cnt; j++) {
            sys->nodes[i].vel[j][2] = 0.0f;
        }
    }
}

void getCloth4D_planeClip(Cloth4D *c, void *plane)
{
    float buf[4];
    int i;
    int j;
    Cloth4DCfg *cfg = c->cfg;
    ICO_WORD *rows = (ICO_WORD *)c->pos;
    int nx = cfg->nx - (cfg->wrap != 0);
    int ny = cfg->ny;

    for (i = 0; i < nx; i++) {
        for (j = 1; j < ny; j++) {
            char *p = (char *)(j * 16 + rows[i]);
            float d = plane_distance(p, plane);
            if (d < 0.0f) {
                _ScaleVector(buf, plane, d);
                _SubVectorXYZ(p, p, buf);
            }
        }
    }
}
