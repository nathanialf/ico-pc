#include "memory.h"
#include "clothAnimation.h"
#include "geometryManager.h"
#include "lineManager.h"
#include "motionManager2.h"
#include "Matrix.h"
#include "DisplayP2O.h"

void GetWormCaptureVector(void *out, GObj *act, void *node, float scale);

/* nonzero until the first worm has been set up */
static int wormFirst = 1; /* derived name */

static void disp(GObj *act);

/* A 16-byte GS colour: four 8-bit RGBA components, one per word, handed to
   DrawLine through the float view the call expects. */
typedef union { /* field names derived */
    int rgba[4];
    float f[4];
    long long d[2];
} Color16; /* derived name */

/* The worm tip highlight: the line's far endpoint and its colour. */
static const Vec16 tipLineTo = {{0.0f, 10.0f, 0.0f, 1.0f}}; /* derived name */

static const Color16 tipLineColor = {{128, 128, 128, 128}}; /* derived name */

typedef struct { /* field names derived */
    float x, y, z, w;
} __attribute__((aligned(16))) WormVec; /* derived name */

typedef struct WormInit { /* field names derived */
    float pos[3];         /* 0x00 */
    int pad0C;            /* 0x0C */
    float nseg;           /* 0x10 */
    float rate;           /* 0x14 */
    float num;            /* 0x18 */
} WormInit;               /* derived name */

typedef struct {     /* field names derived */
    ChainSet *route; /* the worm's chains (InitChains) */
    WormVec **src;
    float reduce;
    float ratio;
} WormWork; /* derived name */

static void simulate(float (*v)[4], int n, float len);
void GetWormRoute(GObj *act, void *target);

#include "worm.h"
#include <libvu0.h>
#include <string.h>
#include "typedef.h"
#include "matrixDrive.h"
#include "main.h"
#include "GifPacket.h"
#include "ios.h"

static void outerProcess(GObj *act)
{
    float v[4];
    float p[4];
    int n;

    if ((pad[1].flags & 0x20) != 0) {
        n = GetSkeltonFocusNode(boyGObj, 22);
        GetWormRoute(act, (char *)GOBJ_SUB(boyGObj)->nodeMtx + n * 64 + 48);
        SetWormReduceRatio(act, 1.0f);
    }

    if ((pad[1].now & 0x40) != 0) {
        n = GetSkeltonFocusNode(boyGObj, 22);
        SetDirectWormTargetPos(act, (char *)GOBJ_SUB(boyGObj)->nodeMtx + n * 64 + 48);
        GetWormCaptureVector(v, act, (char *)GOBJ_SUB(boyGObj)->nodeMtx + n * 64 + 48, 5.0f);
        GetRootPosition(p, boyGObj);
        sceVu0AddVector(p, p, v);
        SetDirectRootPosition(boyGObj, p);
        SetWormReduceRatio(act, 0.0f);
    } else {
        SetWormReduceRatio(act, 1.0f);
    }
}

static void simulate(float (*v)[4], int n, float len)
{
    float d[4];
    float acc[4];
    int i;
    int c;
    float l;
    float len2 = len * len;

    for (i = 1; i < n; i++) {
        c = 0;
        sceVu0SubVector(d, &v[i - 1], &v[i]);
        l = VectorLengthSquare(d);
        if (len2 <= l) {
            c = 1;
            sceVu0ScaleVector(acc, d, 1.0f - len / _Sqrt(l));
        } else {
            CopyVector(acc, ZeroVector);
        }
        sceVu0SubVector(d, &v[i + 1], &v[i]);
        l = VectorLengthSquare(d);
        if (len2 <= l) {
            c++;
            sceVu0ScaleVector(d, d, 1.0f - len / _Sqrt(l));
            sceVu0AddVector(acc, acc, d);
        }
        if (c == 2) {
            sceVu0ScaleVector(acc, acc, 0.5f);
        }
        sceVu0ScaleVector(acc, acc, 1.0f / (float)c);
        sceVu0AddVector(&v[i], &v[i], acc);
    }

    for (i = n - 1; i > 0; i--) {
        c = 0;
        sceVu0SubVector(d, &v[i - 1], &v[i]);
        l = VectorLengthSquare(d);
        if (len2 <= l) {
            c = 1;
            sceVu0ScaleVector(acc, d, 1.0f - len / _Sqrt(l));
        } else {
            CopyVector(acc, ZeroVector);
        }
        sceVu0SubVector(d, &v[i + 1], &v[i]);
        l = VectorLengthSquare(d);
        if (len2 <= l) {
            c++;
            sceVu0ScaleVector(d, d, 1.0f - len / _Sqrt(l));
            sceVu0AddVector(acc, acc, d);
        }
        if (c == 2) {
            sceVu0ScaleVector(acc, acc, 0.5f);
        }
        sceVu0ScaleVector(acc, acc, 1.0f / (float)c);
        sceVu0AddVector(&v[i], &v[i], acc);
    }
}

static void getAnimation(GObj *act)
{
    float tmp[4];
    WormWork *w = GOBJ_SUB(act)->work;
    ChainSet *r = w->route;
    int i, j;

    for (i = 0; i < r->num; i++) {
        int num = r->cfg[i].num;
        float (*pos)[4] = r->nodes[i].pos;
        float (*vel)[4] = r->nodes[i].vel;
        ChainParam *pm = &r->cfg[i].pm;

        for (j = 1; j < num; j++) {
            CopyVector(tmp, &vel[j]);
            sceVu0ScaleVector(&vel[j], &pos[j], -1.0f);
            sceVu0AddVector(&pos[j], &pos[j], tmp);
        }

        simulate(pos, num - 1, pm->step * w->reduce);

        for (j = 0; j < num - 1; j++) {
            r->nodes[i].len[j] = _GetLength(&pos[j], &pos[j + 1]);
        }

        for (j = 1; j < num; j++) {
            sceVu0AddVector(&vel[j], &vel[j], &pos[j]);
            sceVu0ScaleVector(&vel[j], &vel[j], 0.8f);
        }
    }
}

static void disp(GObj *act)
{
    unsigned short ax;
    unsigned short az;
    WormWork *w = GOBJ_SUB(act)->work;
    ChainSet *r = w->route;
    int i;
    int j;
    int k;

    p2o_SetDefaultEnviroment();

    for (i = 0; i < r->num; i++) {
        int num = r->cfg[i].num;
        float (*pos)[4] = r->nodes[i].pos;
        float *len = r->nodes[i].len;

        for (j = 1; j < num; j++) {
            sceVu0UnitMatrix(MatrixDrive_GetMatrix());
            MatrixDrive_TransMatrix(pos[j][0], pos[j][1], pos[j][2]);
            MatrixDrive_GetTurnYAngleXZ(&ax, &az, pos[j][0] - pos[j - 1][0],
                                        pos[j][1] - pos[j - 1][1], pos[j][2] - pos[j - 1][2]);
            MatrixDrive_RotMatrixX(-ax);
            MatrixDrive_RotMatrixZ(-az);
            MatrixDrive_PushMatrix();
            MatrixDrive_ScaleMatrix(2.0f, len[j - 1] * 0.02f, 2.0f);
            MatrixDrive_RotMatrixX(-32768);
            CopyMatrix((char *)GOBJ_SUB(act)->nodeMtx + (j * 64 - 64), MatrixDrive_GetMatrix());
            MatrixDrive_PopMatrix();

            if (j == num - 1) {
                float col[4];
                Vec16 c1;
                Color16 c2;

                gif_StartPacketPri(2);
                memset(col, 0, 16);
                col[3] = 1.0f;
                c1 = tipLineTo;
                c2 = tipLineColor;
                MatrixDrive_TransMatrix(0.0f, len[num - 2] * 0.02f, 0.0f);
                for (k = 0; k <= 65535; k += 16384) {
                    MatrixDrive_PushMatrix();
                    MatrixDrive_RotMatrixY(k);
                    MatrixDrive_TransMatrix(0.0f, 0.0f, -5.0f);
                    MatrixDrive_RotMatrixX(w->ratio * 32768.0f);
                    gif_SetAlpha(1, 7, 128);
                    DrawLine(col, c1.f, c2.f, 0);
                    MatrixDrive_PopMatrix();
                }
                gif_EndPacket();
            }
        }
        p2o_DispVU1DObjMulti((char *)GOBJ_SUB(act));
    }
}

/* the work record's reduce word as SetWormReduceRatio stores it; a store
   through WormWork moves WormGeo's reload of the work pointer (measured) */
typedef union { /* field names derived */
    float f;
    int i;
} WormFI; /* derived name */

inline void SetWormReduceRatio(GObj *act, float ratio)
{
    ((WormFI *)((char *)GOBJ_SUB(act)->work + 8))->f = ratio;
}

void GetWormRoute(GObj *act, void *target)
{
    WormVec d;
    WormWork *w = GOBJ_SUB(act)->work;
    ChainSet *r = w->route;
    int i;
    int j;
    float len;

    for (i = 0; i < r->num; i++) {
        CopyVector(w->src[i], r->nodes[i].pos);
        CopyVector(&w->src[i][9], target);

        for (j = 1; j < 9; j++) {
            sceVu0InterVectorXYZ(&w->src[i][j], &w->src[i][9], &w->src[i][0], j * 0.125f);
            w->src[i][j].w = 1.0f;
        }

        sceVu0SubVector(&d, &w->src[i][1], &w->src[i][0]);
        len = VectorLength(&d);

        for (j = 1; j < 9; j++) {
            WormVec v = {len * (_GetRandom() * 2.0f - 1.0f), len * (_GetRandom() * 2.0f - 1.0f),
                         len * (_GetRandom() * 2.0f - 1.0f), 0.0f};

            sceVu0AddVector(&w->src[i][j], &w->src[i][j], &v);
        }
    }

    w->ratio = 0.0f;
}

inline void SetDirectWormTargetPos(GObj *act, void *pos)
{
    WormWork *w = GOBJ_SUB(act)->work;
    ChainSet *r = w->route;
    int i;

    for (i = 0; i < r->num; i++) {
        int n = r->cfg[i].num;
        CopyVector(&r->nodes[i].pos[n - 1], pos);
        r->nodes[i].pos[n - 1][3] = 1.0f;
    }
    w->ratio = 1.0f;
}

inline void TraceWormRoute(GObj *act, float t)
{
    WormWork *w = GOBJ_SUB(act)->work;
    ChainSet *r = w->route;
    int i, j;
    float step = t * 8.99999f;

    for (i = 0; i < r->num; i++) {
        int n = r->cfg[i].num;
        for (j = 1; j < n; j++) {
            float f = step * (float)j / (float)(n - 1);
            sceVu0InterVectorXYZ(&r->nodes[i].pos[j], &w->src[i][(int)f + 1], &w->src[i][(int)f],
                                 f - (float)(int)f);
            r->nodes[i].pos[j][3] = 1.0f;
        }
    }
}

/* restart the worm's route; used only by WormGeo */
static inline void ResetWormRoute(GObj *act, WormWork *w) /* derived name */
{
    ChainSet *r = w->route;
    int i;
    int j;

    for (i = 0; i < r->num; i++) {
        int num = r->cfg[i].num;
        float (*pos)[4] = r->nodes[i].pos;
        float (*vel)[4] = r->nodes[i].vel;

        for (j = 0; j < num; j++) {
            CopyVector(&pos[j], r->cfg[i].pm.root);
            CopyVector(&vel[j], ZeroVector);
        }
    }

    GetWormRoute(act, r->cfg[0].pm.root);
    w->ratio = 0.0f;
}

void *InitWormGeo(GObj *act, WormInit *ini)
{
    Sub15C *d = GOBJ_SUB(act);
    WormWork *w;
    ChainCfg *seg;
    int nseg;
    int num;
    int i;

    w = (WormWork *)iosMallocDebug(ios_partition_sugipon, 16, __FILE__, 328);
    seg = iosMallocDebug(ios_partition_sugipon, 880, __FILE__, 329);

    nseg = (int)ini->nseg;
    if (nseg == 0) {
        nseg = 10;
    }
    num = (int)ini->num;
    if (num == 0) {
        num = 20;
    }

    w->src = (WormVec **)iosMallocDebug(ios_partition_sugipon, nseg * 4, __FILE__, 334);

    for (i = 0; i < nseg; i++) {
        WormVec pos = {ini->pos[0] + (_GetRandom() * 2.0f - 1.0f) * 50.0f,
                       ini->pos[1] + (_GetRandom() * 2.0f - 1.0f) * 50.0f,
                       ini->pos[2] + (_GetRandom() * 2.0f - 1.0f) * 50.0f, 1.0f};

        seg[i].num = num;
        seg[i].pm.node = -1;
        CopyVector(seg[i].pm.root, &pos);
        seg[i].pm.step = (int)ini->rate != 0 ? ini->rate : 20.0f;
        seg[i].pm.weight = 10.0f;

        w->src[i] = (WormVec *)iosMallocDebug(ios_partition_sugipon, 160, __FILE__, 350);
    }

    seg[nseg].num = -1;
    w->reduce = 1.0f;
    w->ratio = 1.0f;
    w->route = InitChains(seg);

    /* the first node's angle words cleared as floats; int stores through
       d->nodes->rot move InitWormGeo's schedule (measured) */
    *(float *)(*(char **)((char *)d + 0x870) + 0x8) = 0.0f;
    *(float *)(*(char **)((char *)d + 0x870) + 0x4) = 0.0f;
    *(float *)(*(char **)((char *)d + 0x870) + 0x0) = 0.0f;

    d->nodes->scale[2] = 1.0f;
    d->nodes->scale[1] = 1.0f;
    d->nodes->scale[0] = 1.0f;

    if (d->nodeMtx != 0) {
        iosFree(ICO_PHYS(d->nodeMtx));
    }
    if (d->nodeQuat != 0) {
        iosFree(ICO_PHYS(d->nodeQuat));
    }
    d->nodeMtx = 0;
    d->nodeQuat = 0;
    d->nodeMtx = (int)iosMallocDebug(ios_partition_seki, num * 64, __FILE__, 367);
    d->nodeQuat = (int)iosMallocDebug(ios_partition_seki, num * 16, __FILE__, 367);
    d->nodeNum = num;
    if ((int)d->nodes != 0) {
        iosFree(ICO_PHYS(ICO_ADDR(d->nodes)));
    }
    d->nodes = iosMallocDebug(ios_partition_seki, num * 80, __FILE__, 367);
    {
        int n;

        for (n = 0; n < num; n++) {
            d->nodes[n].flags.ll &= ~1;
            d->nodes[n].flags.ll &= ~2;
            d->nodes[n].pos[0] = 0.0f;
            d->nodes[n].pos[1] = 0.0f;
            d->nodes[n].pos[2] = 0.0f;
            d->nodes[n].pos[3] = 1.0f;
            d->nodes[n].flags.ll &= ~4;
            d->nodes[n].fade = 0.0f;
            d->nodes[n].alpha = 1.0f;
            *(short *)((char *)&d->nodes[n] + 0x3A) = 0;
            d->nodes[n].scale[0] = 1.0f;
            d->nodes[n].scale[1] = 1.0f;
            d->nodes[n].scale[2] = 1.0f;
        }
    }
    d->dispType = 2;

    return w;
}

void GetWormCaptureVector(void *out, GObj *act, void *node, float scale)
{
    sceVu0SubVector(out, (void *)((int)GOBJ_SUB(act) + 0x50), node);
    sceVu0Normalize(out, out);
    sceVu0ScaleVector(out, out, scale);
}

void WormGeo(GObj *act)
{
    WormWork *w = GOBJ_SUB(act)->work;

    if (wormFirst != 0) {
        ResetWormRoute(act, w);
        wormFirst = 0;
    }

    outerProcess(act);

    if (w->ratio < 1.0f) {
        w->ratio = w->ratio + 0.05f;
        if (1.0f < w->ratio) {
            w->ratio = 1.0f;
        }
        SetWormReduceRatio(act, 1.0f);
        TraceWormRoute(act, w->ratio);
    }

    getAnimation(act);
}

void WormDL(GObj *act)
{
    disp(act);
}
