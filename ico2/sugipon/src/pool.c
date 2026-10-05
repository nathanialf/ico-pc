#include "sugiCommon.h"
#include "box.h"
#include "pool.h"
#include "memory.h"
#include "DisplayP2O.h"
#include "DObj.h"
#include "GsBase.h"
#include "Primitive.h"
#include "RegistPacket.h"
#include "clothAnimation.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include "multiBgaManager.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "StageAnimation.h"
#include "tableSin.h"
#include "debug.h"
#include <string.h>
#include "sceneManager.h"
#include "main.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "ios.h"
#include "Texture.h"

#ifdef ICO_RD

#include "GifHost.h"
#include "rd.h"

#endif

static void copyToWork(int pri);

#ifdef ICO_RD

/* PC port (renderer wave 5, R5b; docs/port/RENDER_API.md section 16).  The
   work block copyToWork and flushWork allocate right after tex_ResetVramPri
   is TBP 0x2800, which the GS register decoder takes for the named AA0
   target (no depth buffer); on the GS the scene copy, the refracting
   surface's texture, the reflection pass (with work1Vram as its Z buffer)
   and the reflecting surface's texture all use that one block.  The host
   binds a per-frame 256 x 256 target with its own depth in AA0's place in
   the list from the allocation to the last draw that samples it, and
   records the reflection camera of dispPool's gsb_SetVSMatrix(0xCC, ...)
   for the reflection draws.  The register writes are unchanged. */
static RdTarget poolHostAlias; /* the named target the block is bound over */

static void poolHostBlockBegin(int tbp)
{
    gif_HostFlush();
    poolHostAlias = rd_GsNamedBlock((unsigned int)tbp, 0x100, 0x100);
    if (poolHostAlias.id != 0) {
        rd_AliasTarget(poolHostAlias, rd_BlockTarget((unsigned int)tbp, 0x100, 0x100, 1));
    }
}

static void poolHostBlockEnd(void)
{
    gif_HostFlush();
    if (poolHostAlias.id != 0) {
        rd_AliasTarget(poolHostAlias, (RdTarget){0});
        poolHostAlias.id = 0;
    }
}

static void poolHostCamera(int push)
{
    gif_HostFlush();
    if (push) {
        RdCamera cam;

        memset(&cam, 0, sizeof(cam));
        CopyMatrix(cam.view, (char *)(matrixptr + 0x80));
        CopyMatrix(cam.proj43, (char *)(matrixptr + 0xC0));
        cam.aspect43 = 4.0f / 3.0f;
        cam.nearZ = 2.0f;
        cam.farZ = 262144.0f;
        rd_PushCamera(&cam);
    } else {
        rd_PopCamera();
    }
}

#endif

static void falldownSE(GObj *self)
{
    ExecuteSEPackage(self, 86);
}

/* The whole drawing area as a sprite rectangle in GS primitive coordinates,
   a corner and a size: copyToWork and flushWork blit the frame through it. */
static const GifRect workRect = {-2048, -2048, 4096, 4096}; /* derived name */

/* the two work-area VRAM addresses */
static int workVram = 0; /* derived name */

static int work1Vram = 0; /* derived name */

static void copyToWork(int pri)
{
    GifRect rect;

    tex_ResetVramPri(pri);
    workVram = tex_AllocVramAuto(0, 0x400);
#ifdef ICO_RD
    poolHostBlockBegin(workVram);
#endif
    gif_SetGsReg(6, ((long long)(ScreenWidth / 64) << 14) | 0x664000800LL);
    gif_SetDrawEnviroment(workVram, 0, 0x100, 0x100, 0, 0);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(0, 4, 0);
    gif_SetGsReg(0x47, 0x30000);
    gif_SetGsReg(0x14, 0x60);
    rect = workRect;
    /* the frame, through the work rectangle, at full colour */
    {
        GifRect uv = {8, 8, ScreenWidth * 16, ScreenHeight * 16};
        GifColor col = {128, 128, 128, 128};

        gif_SpriteSensitiveOrg(&rect, 0, &uv, &col, 0);
    }
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_SetGsReg(0x47, 0x5000D);
}

static void flushWork(int pri)
{
    GifRect rect;
    GifColor col;

    tex_ResetVramPri(pri);
    workVram = tex_AllocVramAuto(0, 0x400);
    work1Vram = tex_AllocVramAuto(0, 0x400);
#ifdef ICO_RD
    poolHostBlockBegin(workVram);
#endif
    gif_SetDrawEnviroment(workVram, 0, 0x100, 0x100, 0, 0);
    gif_SetZTest(0);
    gif_SetGsReg(0x4E, 0x30000000 | (work1Vram / 32));
    gif_SetAlpha(0, 4, 0);
    rect = workRect;
    col = (GifColor){255, 255, 255, 128};
    gif_SpriteSensitiveOrg(&rect, 0, 0, &col, 0);
    gif_SetZTest(1);
}

/* One ripple of the pool's surface: the grid cell it started in and the
   remainder inside that cell on each of the two horizontal axes, its
   amplitude (negative when the slot is free) and its age.  InitPoolGeo
   clears five of them, SetFallDownSplash starts the next one in turn. */
typedef struct { /* field names derived */
    int ix;      /* 0x00 */
    float fx;    /* 0x04 */
    int iz;      /* 0x08 */
    float fz;    /* 0x0C */
    float amp;   /* 0x10 */
    float age;   /* 0x14 */
} PoolRipple;    /* derived name */

/* The pool's work record, the 224 bytes InitPoolGeo allocates and leaves in
   the object's work word (Sub15C+0x830): the surface's position (its y is
   the water height every reader takes) and drain vector, the two splash
   managers, the height grid and its two meshes, the ripples, the wave phase
   and the reflected stage object. */
typedef struct {          /* field names derived */
    float pos[4];         /* 0x00 */
    float drain[4];       /* 0x10, GetPoolGlobalDrainVector's vector: the
                               layout's x and z angles in degrees */
    int splashNo;         /* 0x20, the next slot of splash */
    BgaDisp *splash;      /* 0x24, two splash animations */
    int word28;           /* 0x28 */
    BgaDisp *bga;         /* 0x2C, ten animations PoolDL draws */
    int hasGrid;          /* 0x30 */
    int nx;               /* 0x34 */
    int ny;               /* 0x38 */
    float step;           /* 0x3C, the grid spacing */
    Mesh3D *reflect;      /* 0x40, the mirrored surface */
    Mesh3D *surface;      /* 0x44, the refracting surface drawn into the work */
    Prim3DVec **wire;     /* 0x48, the rows of reflect's vertices */
    float **height;       /* 0x4C, the height grid, one row per x */
    PoolRipple ripple[5]; /* 0x50 */
    int rippleNo;         /* 0xC8 */
    short phase;          /* 0xCC */
    Sub15C *dobj;         /* 0xD0, the reflected stage object or 0 */
    int spin;             /* 0xD4 */
} PoolWork;               /* derived name */

/* Plant one cell of the pool's ripple grid at a world position: the grid
   index and the in-cell remainder on each of the two horizontal axes, then
   the amplitude the caller asks for and a zero age.  SetFallDownSplash and
   InitPoolGeo use it. */
static inline void setWaveCell(PoolWork *w, float *pos, PoolRipple *cell, float step, int nx,
                               int ny, float amp) /* derived name */
{
    float d[4];

    _SubVector(d, pos, w->pos);
    cell->ix = (int)(d[0] / step) + (nx >> 1);
    cell->fx = d[0] - (float)(int)(d[0] / step) * step;
    cell->iz = (int)(d[2] / step) + (ny >> 1);
    cell->fz = d[2] - (float)(int)(d[2] / step) * step;
    cell->amp = amp;
    cell->age = 0.0f;
}

static void setNodePursueParticleEffectWithUpperLimit(int id, GObj *obj, int focus, float limit)
{
    int ret = GetSkeltonFocusNode(obj, focus);
    if (ret != -1) {
        Sub15C *p = GOBJ_SUB(obj);
        int r = SetParticleEffectActiveSensing(id, (void *)(p->nodeMtx + ret * 64 + 48),
                                               IdentityQuaternion);
        SetParticleEffectUpperLimit(r, limit);
    }
}

void SetFallDownSplash(GObj *pool, GObj *self)
{
    float pos[4];
    float tmp[4];
    PoolWork *w = GOBJ_SUB(pool)->work;

    GetRootPosition(pos, self);
    _ScaleVectorXYZ(tmp, GOBJ_SUB(self)->root.move, 2.0f);
    _AddVector(pos, pos, tmp);
    pos[1] = w->pos[1];

    if (GOBJ_SUB(self)->skel != 0) {
        setNodePursueParticleEffectWithUpperLimit(48, self, 51, pos[1]);
        setNodePursueParticleEffectWithUpperLimit(48, self, 47, pos[1]);
    }

    stage_SetLoopFlag(499, 0);
    stage_SetFrameStep(499, 1);

    EntryMultiBgaManagerNoKind(w->splash, w->splashNo, pos);
    w->splashNo = (w->splashNo + 1) % 2;

    if (w->hasGrid != 0) {
        setWaveCell(w, pos, &w->ripple[w->rippleNo], w->step, w->nx, w->ny, 0.5f);
        w->rippleNo = w->rippleNo + 1;
        if (w->rippleNo == 5) {
            w->rippleNo = 0;
        }
    }

    falldownSE(self);
}

void GetPoolGlobalDrainVector(void *dst, GObj *pool)
{
    CopyVector(dst, ((PoolWork *)GOBJ_SUB(pool)->work)->drain);
}

typedef union { /* field names derived */
    int i[4];
    float f[4];
} PoolQuad; /* derived name */

typedef struct {    /* field names derived */
    PoolQuad pos;   /* 0x00 */
    PoolQuad rot;   /* 0x10 */
    PoolQuad scale; /* 0x20 */
} PoolDisp;         /* derived name */

static int poolRideFunc(ObjNode *on, GObj *rider);

char *InitPoolGeo(char *self, SObjSimpleSetting *lay)
{
    PoolWork *w =
        iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(PoolWork, 224), "src/pool.c", 316);
    int i;
    int j;
    int k;

    CopyVector(w->pos, lay->pos);
    w->pos[3] = 1.0f;
    CopyVector(w->drain, ZeroVector);
    w->drain[0] = -lay->rot[0] * 180.0f / 3.1415927f;
    w->drain[2] = -lay->rot[2] * 180.0f / 3.1415927f;

    if (lay->obj != 0) {
        w->hasGrid = 1;
        w->nx = (int)lay->scale[0];
        w->ny = (int)lay->scale[2];
        w->step = lay->scale[1];

        w->height =
            iosMallocDebug(ios_partition_sugipon, w->nx * sizeof(float *), "src/pool.c", 330);

        for (i = 0; i < w->nx; i++) {
            w->height[i] = iosMallocDebug(ios_partition_sugipon, w->ny * 4, "src/pool.c", 334);
        }

        w->surface = prim_InitMesh3D(w->ny, w->nx, 1, 0x1C, (lay->obj & 0xFFFFFF00) | 0x80, 1);

        w->reflect = prim_InitMesh3D(w->ny, w->nx, 1, 0x5C, 0x80808080, 1);

        w->phase = 0;
        w->wire =
            iosMallocDebug(ios_partition_sugipon, w->nx * sizeof(Prim3DVec *), "src/pool.c", 357);

        for (j = 0; j < w->nx; j++) {
            w->wire[j] = w->reflect->pos + j * w->ny;
        }

        for (k = 0; k < 5; k++) {
            setWaveCell(w, w->pos, &w->ripple[k], w->step, w->nx, w->ny, 1.0f);
            w->ripple[k].amp = -1.0f;
        }

        w->rippleNo = 0;

        if (GOBJ_SUB(self)->accessary != 26) {
            SObjSimpleSetting obj = InitialSObjSimpleSetting;

            obj.pos[0] = -accessary[GOBJ_SUB(self)->accessary].pivot[0];
            obj.pos[1] = -accessary[GOBJ_SUB(self)->accessary].pivot[1];
            obj.pos[2] = -accessary[GOBJ_SUB(self)->accessary].pivot[2];
            w->dobj = CSVSYSTEM_InitDObj(accessary[GOBJ_SUB(self)->accessary].model, &obj);

            w->spin = 0;
        } else {
            w->dobj = 0;

            w->spin = 0;
        }
    } else {
        w->hasGrid = 0;
    }

    {
        PoolDisp *q = (PoolDisp *)GOBJ_SUB(self)->nodes;
        q->pos.i[0] = q->pos.i[1] = q->pos.i[2] = 0;
    }
    {
        PoolDisp *q = (PoolDisp *)GOBJ_SUB(self)->nodes;
        q->scale.f[0] = q->scale.f[1] = q->scale.f[2] = 1.0f;
    }
    {
        PoolDisp *q = (PoolDisp *)GOBJ_SUB(self)->nodes;
        q->rot.f[0] = q->rot.f[1] = q->rot.f[2] = 0.0f;
    }

    _UnitMatrix(&GOBJ_SUB(self)->matrix);
    _UnitMatrix((char *)GOBJ_SUB(self)->nodeMtx);

    w->word28 = 0;
    w->bga = InitMultiBgaManager(10);

    w->splashNo = 0;
    w->splash = InitMultiBgaManager(2);

    SUBHANDLE_OF(self)->sub->rideFunc = poolRideFunc;

    return (char *)w;
}

static inline void decayRipple(PoolRipple *c) /* derived name */
{
    if (c->amp < 0.0f) {
        return;
    }
    c->age += 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    c->amp -= 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.0005f;
}

static inline void addRippleToGrid(PoolWork *w, PoolRipple *c, float **grid) /* derived name */
{
    float step = w->step;
    int nx = w->nx;
    int ny = w->ny;
    float r;
    float inv;
    float dx;
    float dy;
    float t;
    float *row;
    int n;
    int x;
    int y;
    int ix;
    int iz;

    if (c->amp < 0.0f) {
        return;
    }

    {
        r = c->age * 3.0f;
        inv = 1.0f / r;
        n = (int)(r / step);

        for (x = -n; x <= n; x++) {
            dx = (float)x * step - c->fx;
            if (dx < 0.0f) {
                dx = -dx;
            }
            ix = c->ix + x;

            if (ix < 0) {
                continue;
            }
            if (ix >= nx) {
                continue;
            }
            row = grid[ix];

            for (y = -n; y <= n; y++) {
                dy = (float)y * step - c->fz;
                if (dy < 0.0f) {
                    dy = -dy;
                }
                iz = c->iz + y;
                if (dx < dy) {
                    t = dy * 0.9375f + dx * 0.359375f;
                } else {
                    t = dx * 0.9375f + dy * 0.359375f;
                }
                t = t * inv;
                if (t <= 1.0f) {
                    if (iz < 0) {
                        continue;
                    }
                    if (iz >= ny) {
                        continue;
                    }

                    row[iz] +=
                        GetTableCos((t - 1.0f) * r * 512.0f) * 80.0f / (c->age * 2.0f + 10.0f);
                }
            }
        }
    }
}

static inline void makeWaveGrid(PoolWork *w, float **grid, int ang) /* derived name */
{
    int i;
    int j;
    int nx = w->nx;
    int ny = w->ny;
    int nn = (int)w->step;
    float *row;

    for (i = 0; i < nx; i++) {
        row = grid[i];
        for (j = 0; j < ny; j++) {
            *row++ = GetTableSin((i * ny + j) * nn * 1000 + ang) * 0.05f;
        }
    }
}

/* The GS drawing-area origin, the centre of the 4096-unit primitive space. */
static const ConstVec screenOrigin = {{2048.0f, 2048.0f, 0.0f, 0.0f}}; /* derived name */

static void updatePoolGeo(GObj *self)
{
    ConstVec org;
    float out[4];
    float nrm[4];
    float eye[4];
    char mat[64];
    float ref[4];
    float dir[4];
    float tmp[4];
    float pos[4];
    float sub[4];
    PoolWork *w = GOBJ_SUB(self)->work;
    Mesh3D *mesh0 = w->reflect;
    Mesh3D *mesh1 = w->surface;
    float step = w->step;
    float **grid = w->height;
    float sx;
    float sy;
    float *pc;
    float *pa;
    float *pb;
    float *pd;
    int ang;
    float *row;
    Prim3DVec *q;
    Prim3DVec *uv;
    Prim3DVec *q2;
    float *row2;
    Prim3DVec *uv2;
    Prim3DVec *qq;
    float h;
    float iw;
    float usc;
    float vsc;
    int k;
    int i;
    int j;

    org = screenOrigin;
    sx = 1.0f / (float)ScreenWidth;
    sy = 1.0f / (float)ScreenHeight;

    pc = (float *)(matrixptr + 0x4C0);
    pa = (float *)(matrixptr + 0x400);
    pb = (float *)(matrixptr + 0x440);
    pd = (float *)(matrixptr + 0x480);

    CopyVector(pa, w->pos);
    CopyVector(pb, &org);
    CopyVector(pd, ZeroVector);

    _InitCurrentMatrix();
    _SetCurrentMatrix(matrixptr + 0x100);

    if (systemStatus[5] == 0) {
        for (k = 0; k < 5; k++) {
            decayRipple(&w->ripple[k]);
        }
    }

    ang = w->phase;

    makeWaveGrid(w, grid, ang);

    for (k = 0; k < 5; k++) {
        addRippleToGrid(w, &w->ripple[k], grid);
    }

    for (i = 0; i < w->nx; i++) {
        q = mesh1->pos + i * w->ny;
        uv = mesh1->st + i * w->ny;
        row = grid[i];

        pd[0] = (float)(((1 - w->nx) >> 1) + i) * step;

        j = 0;

        for (; j < w->ny; j++, row++, q++, uv++) {
            h = *row;

            pd[1] = h * 30.0f;
            pd[2] = (float)(((1 - w->ny) >> 1) + j) * step;
            _AddVector(q, pa, pd);

            _ApplyCurrentMatrix(out, q);
            iw = 1.0f / out[3];
            _ScaleVector(pc, out, iw);
            _SubVector(out, pc, pb);
            uv->x = out[0] * sx + 0.5f + h * 30.0f * iw;
            uv->y = out[1] * sy + 0.5f + h * 30.0f * iw;
        }
    }

    memset(nrm, 0, 16);
    nrm[1] = -1.0f;

    usc = 1.0f / (float)ScreenWidth * 0.8f;
    vsc = 1.0f / (float)ScreenHeight * 0.8f;

    MatrixDrive_SetTransposeMatrix(mat, (char *)(matrixptr + 0x80));
    CopyVector(eye, mat + 0x30);

    _SetCurrentMatrix(matrixptr + 0x100);

    for (i = 0; i < w->nx; i++) {
        q2 = mesh1->pos + i * w->ny;
        qq = mesh0->pos + i * w->ny;
        uv2 = mesh0->st + i * w->ny;
        row2 = grid[i];

        for (j = 0; j < w->ny; j++, row2++, q2++, uv2++) {
            nrm[0] = nrm[2] = *row2 * 0.1f;

            CopyVector(qq, q2);

            _SubVector(dir, qq, eye);

            _NormalizeVector(dir, dir);
            _ScaleVector(tmp, nrm, _InnerProduct(dir, nrm) * -2.0f);

            _AddVectorXYZ(ref, dir, tmp);

            ref[1] = -ref[1];

            _ApplyCurrentMatrix(out, qq);
            _ScaleVectorXYZ(pc, out, 1.0f / out[3]);

            _AddVector(pos, qq, ref);
            qq++;
            _ApplyCurrentMatrix(pos, pos);
            _ScaleVectorXYZ(pos, pos, 1.0f / pos[3]);

            _SubVector(sub, pos, pc);

            _SubVector(out, pc, pb);

            uv2->x = (out[0] + sub[0] * 1000.0f) * usc + 0.5f;
            uv2->y = (out[1] + sub[1] * 1000.0f) * vsc + 0.5f;
        }
    }

    prim_UpdateMesh3D(mesh1, 9, buffer_ID);

    prim_UpdateMesh3D(mesh0, 9, buffer_ID);
}

/* The fixed lighting the pool surface is drawn under, in the two matrices
   light_MakeLightMatrix otherwise builds at +0x40 and +0x00 of the object's
   light work: a colour matrix (a row per colour channel, a column per light)
   and a normal matrix (a column per light direction).  The first pair goes
   straight to prim_DispMesh3D, the second is copied into the work. */
static float dispLightColor[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
}; /* derived name */

static float dispLightNormal[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

static float workLightColor[4][4] = {
    {0.707f, 0.707f, 0.0f, 0.0f},
    {0.707f, 0.707f, 0.0f, 0.0f},
    {0.707f, 0.707f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

static float workLightNormal[4][4] = {
    {1.0f, 1.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

static void dispPool(GObj *self)
{
    char m0[64];
    char m1[64];
    char m2[64];
    char m3[64];
    char m4[64];
    PoolWork *w = GOBJ_SUB(self)->work;

    gif_StartPacketPri(4);
    copyToWork(4);
    gif_SetGsReg(6, workVram | 0x20010000 | 0x600000000LL);

    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);

    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    gif_SetAlpha(0, 4, 0x80);

    gif_EndPacket();

    _SetCurrentMatrix(matrixptr + 0x100);

    prim_DispMesh3D(w->surface, dispLightColor, dispLightNormal, -1);

    CopyMatrix((char *)GOBJ_SUB(self)->lightMtx + 0x40, workLightColor);
    CopyMatrix((char *)GOBJ_SUB(self)->lightMtx, workLightNormal);

    gif_StartPacketPri(4);
    flushWork(4);

    CopyMatrix(m0, (char *)(matrixptr + 0xC0));

    CopyMatrix(m1, (char *)(matrixptr + 0x1C0));
    CopyMatrix(m2, (char *)(matrixptr + 0x100));
    CopyMatrix(m3, (char *)(matrixptr + 0x200));
    CopyMatrix(m4, (char *)(matrixptr + 0x340));

    gsb_SetVSMatrix(0xCC, 0xCC, (float)currentFocusDistance);

    _MulMatrix((char *)(matrixptr + 0x100), (char *)(matrixptr + 0xC0), (char *)(matrixptr + 0x80));
    _MulMatrix((char *)(matrixptr + 0x200), (char *)(matrixptr + 0x1C0),
               (char *)(matrixptr + 0x80));
#ifdef ICO_RD
    poolHostCamera(1);
#endif

    gif_SetZTest(1);
    gif_SetAlpha(0, 4, 0x80);
    gif_EndPacket();

    _UnitMatrix(MatrixDrive_GetMatrix());
    CopyMatrix((char *)GOBJ_SUB(self)->nodeMtx, MatrixDrive_GetMatrix());
    reg_RenderReflection(GOBJ_SUB(self), 4);

    if (w->dobj != 0) {
        CopyMatrix(MatrixDrive_GetMatrix(), &w->dobj->matrix);
        switch (stage_no) {
        case 101:
            MatrixDrive_RotMatrixZ((w->spin << 16) / 1600);
            break;
        case 15:
            MatrixDrive_RotMatrixZ(-(w->spin << 16) / 1600);
        }

        CopyMatrix((char *)w->dobj->nodeMtx, MatrixDrive_GetMatrix());

        reg_RenderReflection(w->dobj, 4);

        if (systemStatus[5] == 0) {
            if (++w->spin > 1600) {
                w->spin = 0;
            }
        }
    }

    gif_StartPacketPri(4);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    gif_SetAlpha(0, 4, 0x80);

    CopyMatrix((char *)(matrixptr + 0xC0), m0);
    CopyMatrix((char *)(matrixptr + 0x1C0), m1);
    CopyMatrix((char *)(matrixptr + 0x340), m4);
    CopyMatrix((char *)(matrixptr + 0x100), m2);
    CopyMatrix((char *)(matrixptr + 0x200), m3);
#ifdef ICO_RD
    poolHostCamera(0);
#endif
    vsWidth = ScreenWidth;
    vsHeight = ScreenHeight;

    gif_SetGsReg(6, workVram | 0x20010000 | 0x600000000LL);

    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);

    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    gif_SetAlpha(1, 0, 0x40);

    gif_EndPacket();

    _SetCurrentMatrix(matrixptr + 0x100);

    prim_DispMesh3D(w->reflect, dispLightColor, dispLightNormal, -1);

    gif_StartPacketPri(4);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);

    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    gif_SetAlpha(1, 0, 0x40);

    gif_EndPacket();

#ifdef ICO_RD
    poolHostBlockEnd();
#endif
    if (debug_skel_flag != 0) {
        DispMeshWire(w->wire, w->nx, w->ny);
    }
}

void PoolDL(GObj *self)
{
    PoolWork *w = GOBJ_SUB(self)->work;

    DispMultiBgaManagerWithKind(498, w->bga, 10);
    DispMultiBgaManagerWithKind(499, w->splash, 2);
    if (systemStatus[5] == 0) {
        w->phase =
            (short)((float)w->phase +
                    60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 2000.0f);
    }
    if (w->hasGrid != 0) {
        updatePoolGeo(self);
        dispPool(self);
    } else {
        p2o_DispVU1(self);
    }
}

void InitLimitedPoolReflactionMesh(PoolMesh *refl)
{
    int i;
    int j;

    refl->mesh = prim_InitMesh3D(refl->ncol, refl->nrow, 1, 0x1C, refl->color, 1);
    refl->height =
        iosMallocDebug(ios_partition_sugipon, refl->nrow * sizeof(float *), "src/pool.c", 884);
    refl->row =
        iosMallocDebug(ios_partition_sugipon, refl->nrow * sizeof(float *), "src/pool.c", 885);
    for (i = 0; i < refl->nrow; i++) {
        refl->row[i] = refl->mesh->pos + i * refl->ncol;
        refl->height[i] = iosMallocDebug(ios_partition_sugipon, refl->ncol * 4, "src/pool.c", 890);
        for (j = 0; j < refl->ncol; j++) {
            refl->height[i][j] = 0.0f;
        }
    }
}

void SetLayoutedPoolReflactionMesh(PoolMesh *refl)
{
    ConstVec vec;
    float out[4];
    Mesh3D *mesh;
    char *tmp;
    char *base;
    Prim3DVec *q;
    Prim3DVec *uv;
    float sx;
    float sy;
    float iw;
    float h;
    float t;
    int i;
    int j;

    if (systemStatus[5] == 0) {
        for (i = 0; i < refl->nrow; i++) {
            refl->height[i][0] -= (refl->height[i][0] - random_signed() * 0.1f) * 0.8f;
            for (j = refl->ncol - 1; j > 0; j--) {
                refl->height[i][j] -= (refl->height[i][j] - refl->height[i][j - 1] * 1.15f) * 0.8f;
            }
        }
    }

    mesh = refl->mesh;
    vec = screenOrigin;
    sx = 1.0f / (float)ScreenWidth;
    sy = 1.0f / (float)ScreenHeight;
    tmp = (char *)(matrixptr + 0x4C0);
    base = (char *)(matrixptr + 0x440);

    CopyVector(base, &vec);

    _InitCurrentMatrix();
    _SetCurrentMatrix(matrixptr + 0x100);

    for (i = 0; i < refl->nrow; i++) {
        q = mesh->pos + i * refl->ncol;
        uv = mesh->st + i * refl->ncol;
        for (j = 0; j < refl->ncol; j++) {
            h = refl->height[i][j];
            _ApplyCurrentMatrix(out, q);
            iw = 1.0f / out[3];
            _ScaleVector(tmp, out, iw);
            _SubVector(out, tmp, base);
            t = out[0] * sx + 0.5f + h * 50.0f * iw;
            uv->x = t < 0.0f ? 0.0f : t;
            t = out[1] * sy + 0.5f + h * 50.0f * iw;
            uv->y = 1.0f < t ? 1.0f : t;
            q++;
            uv++;
        }
    }
    prim_UpdateMesh3D(mesh, 9, buffer_ID);
}

void SetLimitedPoolReflactionMesh(PoolMesh *refl, GObj *pool, GObj *obj)
{
    PoolWork *w = GOBJ_SUB(pool)->work;
    float pos[4];
    float v1[4];
    float v2[4];
    ConstVec vec;
    float out[4];
    Mesh3D *mesh;
    char *tmp;
    char *base;
    Prim3DVec *q;
    Prim3DVec *uv;
    float dist;
    float dz;
    float sx;
    float sy;
    float iw;
    float h;
    int i;
    int j;

    if (systemStatus[5] == 0) {
        for (i = 0; i < refl->nrow; i++) {
            for (j = 0; j < refl->ncol; j++) {
                refl->height[i][j] -= (refl->height[i][j] - random_signed() * 0.3f) * 0.5f;
            }
        }
    }
    GetRootPosition(pos, obj);
    CopyVector(v1, pos);
    v1[1] = w->pos[1];
    CopyVector(v2, pos);
    v2[1] += GOBJ_SUB(obj)->root.projHeight;

    pos[1] = (v1[1] + v2[1]) * 0.5f;
    _InterVectorXYZ(pos, pos, (char *)(matrixptr + 944),
                    (v1[1] - *(float *)(matrixptr + 948)) / (pos[1] - *(float *)(matrixptr + 948)));

    _InterVectorXYZ(v2, v2, (char *)(matrixptr + 944),
                    (v1[1] - *(float *)(matrixptr + 948)) / (v2[1] - *(float *)(matrixptr + 948)));

    dist = GetPointDistance(v1, v2) + 100.0f;

    pos[0] -= dist * 0.5f;
    pos[2] -= dist * 0.5f;

    mesh = refl->mesh;
    vec = screenOrigin;
    sx = 1.0f / (float)ScreenWidth;
    sy = 1.0f / (float)ScreenHeight;
    tmp = (char *)(matrixptr + 0x4C0);
    base = (char *)(matrixptr + 0x440);

    dz = dist / (float)refl->nrow;

    CopyVector(base, &vec);

    _InitCurrentMatrix();
    _SetCurrentMatrix(matrixptr + 0x100);

    for (i = 0; i < refl->nrow; i++) {
        q = mesh->pos + i * refl->ncol;
        uv = mesh->st + i * refl->ncol;
        for (j = 0; j < refl->ncol; j++) {
            h = refl->height[i][j];
            q->x = pos[0] + (float)i * dz;
            q->y = pos[1];
            q->z = pos[2] + (float)j * dz;
            q->w = 1.0f;
            _ApplyCurrentMatrix(out, q);
            iw = 1.0f / out[3];
            _ScaleVector(tmp, out, iw);
            _SubVector(out, tmp, base);
            uv->x = out[0] * sx + 0.5f + h * 50.0f * iw;
            uv->y = out[1] * sy + 0.5f + h * 50.0f * iw;
            q++;
            uv++;
        }
    }
    prim_UpdateMesh3D(mesh, 9, buffer_ID);
}

void DispLimitedPoolReflactionMesh(PoolMesh *refl)
{
    gif_StartPacketPri(4);
    copyToWork(4);
    gif_SetGsReg(6, workVram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    gif_SetAlpha(0, 4, 0x80);
    gif_EndPacket();
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(refl->mesh, dispLightColor, dispLightNormal, -1);
#ifdef ICO_RD
    poolHostBlockEnd();
#endif
    if (debug_skel_flag != 0) {
        DispMeshWire(refl->row, refl->nrow, refl->ncol);
    }
}

void PoolGeo(void) {}

float GetPoolGlobalHeight(GObj *pool)
{
    return ((PoolWork *)GOBJ_SUB(pool)->work)->pos[1];
}

float GetPoolGlobalHeightDetail(GObj *pool, float *pos)
{
    PoolWork *p = GOBJ_SUB(pool)->work;
    float inv;
    int ix;
    int iz;

    if (p->hasGrid != 0) {
        inv = 1.0f / p->step;
        ix = (int)((pos[0] - p->pos[0]) * inv + (float)(p->nx >> 1));
        iz = (int)((pos[2] - p->pos[2]) * inv + (float)(p->ny >> 1));
        ix = ix >= 0 ? (ix < p->nx ? ix : p->nx - 1) : 0;
        iz = iz >= 0 ? (iz < p->ny ? iz : p->ny - 1) : 0;
        return p->height[ix][iz] * 100.0f + p->pos[1];
    }
    return p->pos[1];
}

int CheckPoolHasGridMesh(GObj *pool)
{
    return ((PoolWork *)GOBJ_SUB(pool)->work)->hasGrid != 0;
}

void InitLayoutedPoolReflactionMesh(PoolMesh *refl, PoolMeshQuad *quad)
{
    float v0[4];
    float v1[4];
    int i;
    int j;

    InitLimitedPoolReflactionMesh(refl);
    for (i = 0; i < refl->nrow; i++) {
        _InterVectorXYZ(v0, &quad->corner[0], &quad->corner[2], (float)i / (float)(refl->nrow - 1));
        _InterVectorXYZ(v1, &quad->corner[1], &quad->corner[3], (float)i / (float)(refl->nrow - 1));
        for (j = 0; j < refl->ncol; j++) {
            _InterVectorXYZ(&refl->mesh->pos[i * refl->ncol + j], v0, v1,
                            (float)j / (float)(refl->ncol - 1));
            refl->mesh->pos[i * refl->ncol + j].w = 1.0f;
        }
    }
}

static int poolRideFunc(ObjNode *on, GObj *rider)
{
    Sub15C *e = GOBJ_SUB(rider);
    PoolWork *p = GOBJ_SUB(on->obj)->work;
    e->ctrl.waterDepth = e->root.pos[1] - p->pos[1];
    return 1;
}

float getWave(float t)
{
    t += 50.0f;
    t -= (float)(int)(t * 0.005f) * 200.0f;
    if (t < 100.0f) {
        return t * 0.01f - 0.5f;
    }
    return -(t - 100.0f) * 0.01f + 0.5f;
}
