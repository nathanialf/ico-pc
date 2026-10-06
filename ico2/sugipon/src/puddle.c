#include "ee_view.h"
#include "puddle.h"
#include "box.h"
#include "DObj.h"
#include "GifPacket.h"
#include "memory.h"
#include "GsBase.h"
#include "Matrix.h"
#include "RegistPacket.h"
#include "Texture.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include "main.h"
#include "ios.h"
#include "sceneManager.h"

#ifdef ICO_RD

#include "GifHost.h"
#include "rd.h"

#endif

/* 16-byte aligned: the template copy in InitPuddleGeo is ld/sd, not ldl/ldr. */
typedef struct { /* field names derived */
    float pos[4];
    float t;
    float pad[3];
} __attribute__((aligned(16))) Ripple; /* derived name */

typedef struct PuddleWork { /* field names derived */
    Sub15C *reflect;        /* the reflection display object */
    int idx;
    int pad8[2];
    Ripple rip[6];
} PuddleWork; /* derived name */

/* the ripple every slot starts from, and the centre and scale of the ripple
   mesh in texture space */
static Ripple rippleInit = {{0.0f, 0.0f, 0.0f, 1.0f}, 10000.0f}; /* derived name */

static float rippleCenter[4] = {2048.0f, 2048.0f, 0.0f, 0.0f}; /* derived name */

static float rippleScale[4] = {1.5f, 1.5f, 0.0f, 0.0f}; /* derived name */

/* the two work-area VRAM addresses and the three sprite colours */
static int workVram = 0; /* derived name */

static int work1Vram = 0; /* derived name */

static GifColor setupColor = {128, 128, 128, 128}; /* derived name */

static GifColor leveldownColor = {0, 0, 0, 0}; /* derived name */

static GifColor copyColor = {128, 128, 128, 128}; /* derived name */

/* the two vertex strips of the ripple mesh, the nine spoke directions and
   their scaled copies, and the five camera matrices drawAreaSetup saves and
   drawAreaRestore puts back */
static float stripUpper[9 * 8]; /* derived name */

static float stripLower[9 * 8]; /* derived name */

static float spokeScaled[9 * 4]; /* derived name */

static float spokeDir[9 * 4]; /* derived name */

static float savedMatrixC0[16]; /* derived name */

static float savedMatrix1C0[16]; /* derived name */

static float savedMatrix100[16]; /* derived name */

static float savedMatrix200[16]; /* derived name */

static float savedMatrix340[16]; /* derived name */

/* the C library's memset (the original declared it with an int size) */
#include <string.h>

void PuddleGeo(GObj *self);
void EntryRippleToPuddle(GObj *self, void *vec);
int puddleRideFunc(ObjNode *on, GObj *rider);

PuddleWork *InitPuddleGeo(GObj *self, SObjSimpleSetting *setting)
{
    PuddleWork *w = (PuddleWork *)iosMallocDebug(ios_partition_sugipon,
                                                 ICO_MAX_SIZE(PuddleWork, 208), __FILE__, 69);
    float *v;
    int i;

    w->reflect = CSVSYSTEM_InitDObj(accessary[GOBJ_SUB(self)->accessary].model, setting);

    v = spokeDir;
    for (i = 0; i < 9; i++) {
        short s = i * 8192;

        v[0] = GetTableCos(s);
        v[2] = GetTableSin(s);
        v[1] = 0.0f;
        v[3] = 1.0f;
        v += 4;
    }

    w->idx = 0;

    for (i = 0; i < 6; i++) {
        w->rip[i] = rippleInit;
    }

    GOBJ_SUB(self)->rideFunc = puddleRideFunc;
    return w;
}

#ifdef ICO_RD

/* PC port (renderer wave 5, R5b).  The
   work block drawAreaSetup allocates right after tex_ResetVramPri(4) is TBP
   0x2800, which the GS register decoder takes for the named AA0 target (no
   depth buffer); on the GS the reflection draws there with work1Vram as its
   Z buffer.  The host binds a per-frame 256 x 256 target with its own depth
   in AA0's place in list 4 from the allocation to the last packet that
   samples it (copy), and records the reflection camera gsb_SetVSMatrix
   leaves in the scratchpad for the reflection draws.  The register writes
   are unchanged. */
static RdTarget puddleHostAlias; /* the named target the block is bound over */

static void puddleHostBlockBegin(int tbp)
{
    gif_HostFlush();
    puddleHostAlias = rd_GsNamedBlock((unsigned int)tbp, 0x100, 0x100);
    if (puddleHostAlias.id != 0) {
        rd_AliasTarget(puddleHostAlias, rd_BlockTarget((unsigned int)tbp, 0x100, 0x100, 1));
    }
}

static void puddleHostBlockEnd(void)
{
    gif_HostFlush();
    if (puddleHostAlias.id != 0) {
        rd_AliasTarget(puddleHostAlias, (RdTarget){0});
        puddleHostAlias.id = 0;
    }
}

/* the reflection view: +0x80 is the frame's view, +0xC0 the 230 x 230
   screen matrix gsb_SetVSMatrix(0xE6, ...) just built */
static void puddleHostCamera(int push)
{
    gif_HostFlush();
    if (push) {
        RdCamera cam;

        memset(&cam, 0, sizeof(cam));
        CopyMatrix(cam.view, matrixptr + 0x80);
        CopyMatrix(cam.proj43, matrixptr + 0xC0);
        cam.aspect43 = 4.0f / 3.0f;
        cam.nearZ = 2.0f;
        cam.farZ = 262144.0f;
        rd_PushCamera(&cam);
    } else {
        rd_PopCamera();
    }
}

#endif

void baseSetup(GObj *self)
{
    gif_StartPacketPri(4);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 5, 128);

    {
        GifRect r = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16,
                     ScreenHeight * 16};

        {
            GifColor col;

            memset(&col, 0, 4);
            gif_SpriteSensitiveOrg(&r, 0, 0, &col, 1);
        }
    }

    gif_SetZTest(1);
    gif_EndPacket();

    reg_RenderReflection(self->dobj, 4);
}

/* the sprite rectangle drawAreaSetup blits the frame through, in GS primitive
   coordinates */
static const GifRect drawAreaRect = {-2048, -2048, 4096, 4096}; /* derived name */

void drawAreaSetup(void)
{
    GifRect r;

    tex_ResetVramPri(4);
    workVram = tex_AllocVramAuto(0, 0x400);
    work1Vram = tex_AllocVramAuto(0, 0x400);
#ifdef ICO_RD
    puddleHostBlockBegin(workVram);
#endif

    gif_StartPacketPri(4);

    CopyMatrix(savedMatrixC0, matrixptr + 0xC0);
    CopyMatrix(savedMatrix1C0, matrixptr + 0x1C0);
    CopyMatrix(savedMatrix100, matrixptr + 0x100);
    CopyMatrix(savedMatrix200, matrixptr + 0x200);
    CopyMatrix(savedMatrix340, matrixptr + 0x340);

    gsb_SetVSMatrix(0xE6, 0xE6, (float)currentFocusDistance);

    _MulMatrix(matrixptr + 0x100, matrixptr + 0xC0, matrixptr + 0x80);
    _MulMatrix(matrixptr + 0x200, matrixptr + 0x1C0, matrixptr + 0x80);
#ifdef ICO_RD
    puddleHostCamera(1);
#endif

    gif_SetGsReg(0x14, 0x60);
    gif_SetZTest(1);
    gif_SetAlpha(0, 4, 0x80);
    gif_EndPacket();

    gif_StartPacketPri(4);
    gif_SetDrawEnviroment(workVram, 0, 0x100, 0x100, 0, 0);
    gif_SetZTest(0);
    gif_SetGsReg(0x4E, 0x30000000 | (work1Vram / 32));
    gif_SetAlpha(0, 4, 0);

    r = drawAreaRect;
    gif_SpriteSensitiveOrg(&r, 0, 0, &setupColor, 0);

    gif_SetZTest(1);
    gif_EndPacket();
}

void drawAreaRestore(void)
{
    gif_StartPacketPri(4);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    gif_SetAlpha(0, 4, 0x80);

    CopyMatrix(matrixptr + 0xC0, savedMatrixC0);
    CopyMatrix(matrixptr + 0x1C0, savedMatrix1C0);
    CopyMatrix(matrixptr + 0x340, savedMatrix340);
    CopyMatrix(matrixptr + 0x100, savedMatrix100);
    CopyMatrix(matrixptr + 0x200, savedMatrix200);
#ifdef ICO_RD
    puddleHostCamera(0);
#endif

    vsWidth = ScreenWidth;
    vsHeight = ScreenHeight;
    gif_SetGsReg(6, (long long)workVram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    gif_SetAlpha(1, 0, 0x40);
    gif_EndPacket();
}

void leveldown(int pri)
{
    gif_StartPacketPri(pri);
    gif_SetGsReg(6, (long long)workVram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetGsReg(0x47, 0x3F001);
    gif_SetAlpha(1, 2, 0x10);

    {
        GifRect r = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16,
                     ScreenHeight * 16};

        gif_SpriteSensitiveOrg(&r, 0, 0, &leveldownColor, 1);
    }

    gif_EndPacket();
}

/* gif_SpriteSensitiveOrg passes the uv rectangle straight through to
   gif_MakeSprite, so its caller supplies UV already in GS 1/16-texel units
   (gif_SpriteOrg is the variant that scales by 16 itself); this converts a
   texel coordinate to those units */
static inline int texUV(float texel) /* derived name */
{
    return (int)(texel * 16.0f);
}

void copy(int pri)
{
    gif_StartPacketPri(pri);
    gif_SetGsReg(6, (long long)workVram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetGsReg(0x47, 0x3F001);

    if (stage_no == 0x22) {
        gif_SetAlpha(1, 4, 0x60);
    } else {
        gif_SetAlpha(1, 0, 0x60);
    }

    {
        GifRect r = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16,
                     ScreenHeight * 16};
        GifRect uv = {texUV(13.25f), texUV(13.25f), texUV(230.375f), texUV(230.375f)};

        gif_SpriteSensitiveOrg(&r, 0, &uv, &copyColor, 1);
    }

    gif_EndPacket();
}

void drawRipple(float t, void *pos)
{
    GifColor col;
    float v[4];
    float age;
    float sx;
    float sy;
    float s2;
    int c;
    int i;
    float *p0;
    float *p1;
    float *q0;
    float *q1;
    float *r0;
    float *r1;

    age = 200.0f - t;
    col.a = 0x80;
    c = (int)(age * 0.635f + 128.0f);
    col.r = c;
    col.g = c;
    col.b = c;
    sx = 0.9f / (float)ScreenWidth;
    sy = 0.9f / (float)ScreenHeight;
    _UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY((short)(int)(age * 10.24f));

    p1 = spokeDir;
    p0 = spokeScaled;
    for (i = 8; i >= 0; i--) {
        _ApplyMatrix(p0, MatrixDrive_GetMatrix(), p1);
        p0 += 4;
        p1 += 4;
    }

    _SetCurrentMatrix(matrixptr + 0x100);

    for (i = 0; i < 9; i++) {
        float *m = &spokeDir[i * 4];

        q0 = &stripUpper[i * 8];
        r0 = &stripUpper[i * 8 + 4];
        q1 = &stripLower[i * 8];
        r1 = &stripLower[i * 8 + 4];

        _ScaleVectorXYZ(q0, m, t);
        s2 = t + 5.0f;
        _ScaleVectorXYZ(r0, m, s2);
        _AddVectorXYZ(q0, q0, pos);
        _AddVectorXYZ(r0, r0, pos);
        _ScaleVectorXYZ(v, &spokeScaled[i * 4], s2);
        _AddVectorXYZ(v, v, pos);
        _ApplyCurrentMatrix(q1, q0);
        _ApplyCurrentMatrix(r1, v);
        _ScaleVector(q1, q1, 1.0f / q1[3]);
        _ScaleVector(r1, r1, 1.0f / r1[3]);
        _SubVector(q1, q1, rippleCenter);
        _SubVector(r1, r1, rippleCenter);
        q1[0] = q1[0] * sx;
        q1[1] = q1[1] * sy;
        r1[0] = r1[0] * sx;
        r1[1] = r1[1] * sy;
        _AddVector(q1, q1, rippleScale);
        _AddVector(r1, r1, rippleScale);
    }

    gif_DrawStripFST(stripUpper, stripLower, col, 0x12, 1);
}

void drawRipples(GObj *self, int pri)
{
    PuddleWork *w = GOBJ_SUB(self)->work;
    Ripple *p;
    float *t;
    int i;

    gif_StartPacketPri(pri);
    gif_SetGsReg(6, (long long)workVram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x14, 0x60);
    gif_SetGsReg(0x47, 0x3F000);

    if (stage_no == 0x22) {
        gif_SetAlpha(1, 4, 0x60);
    } else {
        gif_SetAlpha(1, 0, 0x60);
    }

    gif_EndPacket();

    gif_StartPacketPri(pri);

    p = w->rip;
    t = &w->rip[0].t;
    for (i = 5; i >= 0; i--) {
        if (*t < 200.0f) {
            drawRipple(*t, p);
        }
        p++;
        t += sizeof(Ripple) / sizeof(float);
    }

    gif_EndPacket();
}

void PuddleDL(GObj *self)
{
    Sub15C *p = ((PuddleWork *)GOBJ_SUB(self)->work)->reflect;

    baseSetup(self);
    drawAreaSetup();
    _UnitMatrix(MatrixDrive_GetMatrix());
    CopyMatrix((void *)p->nodeMtx, MatrixDrive_GetMatrix());
    reg_RenderReflection(p, 4);
    drawAreaRestore();
    leveldown(4);
    drawRipples(self, 4);
    copy(4);
#ifdef ICO_RD
    puddleHostBlockEnd();
#endif
}

inline void PuddleGeo(GObj *self)
{
    char *p;
    int i;

    p = GOBJ_SUB(self)->work;
    for (i = 0; i < 6; i++) {
        if (((PuddleWork *)p)->rip[i].t < 200.0f) {
            ((PuddleWork *)p)->rip[i].t +=
                60.0f / (float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]) * 2.0f;
        }
    }
}

inline void EntryRippleToPuddle(GObj *self, void *vec)
{
    PuddleWork *w;

    w = GOBJ_SUB(self)->work;
    CopyVector(w->rip[w->idx].pos, vec);
    w->rip[w->idx].t = 0.0f;
    w->idx = w->idx + 1;
    if (w->idx >= 6) {
        w->idx = 0;
    }
}

inline int puddleRideFunc(ObjNode *on, GObj *rider)
{
    float v[4];
    Sub15C *e;
    int n;

    e = rider->dobj;
    if (e->ctrl.landed != 0) {
        n = e->root.standNode;
        if (n != -1) {
            CopyVector(v, (char *)e->nodeMtx + n * 64 + 48);
            EntryRippleToPuddle(on->obj, v);
        }
    }
    return 1;
}
