#include "typedef.h"
#include "debug.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "Primitive.h"
#include "tableSin.h"
#include "queen_barrier_disp.h"
#include <string.h>
#include <libvu0.h>
#include "Texture.h"
#include "geometryManager.h"
#include "main.h"
#include "Matrix.h"
#include "DmaPacket.h"

#ifdef ICO_RD

#include "GifHost.h"
#include "rd.h"

#endif

/* The barrier mesh, the damage flash timer queen_barrier_set_damage starts
   at 60, and the ripple phase the animation advances each frame. */
static Mesh3D *barrierMesh; /* derived name */

static int damageTimer; /* derived name */

static int ripplePhase; /* derived name */

#define GIF_COLOR(c)                                                                               \
    ((long long)(c)->r | ((long long)(c)->g << 8) | ((long long)(c)->b << 16) |                    \
     ((long long)(c)->a << 24))
#define GIF_UV(u, v) ((long long)(u) | ((long long)(v) << 16))
#define GIF_XY(x, y) ((long long)((x) + 0x8000) | ((long long)((y) + 0x8000) << 16))
#define GIF_XY0(x, y) ((long long)(x) | ((long long)(y) << 16))

/* gif_SetGsReg's body: one GS register write, data then address */
static inline void setGsReg(long long addr, long long data) /* derived name */
{
    *PacketBufferStruct.ptr.d++ = data;
    *PacketBufferStruct.ptr.d++ = addr;
}

static void MakeRefractTexture(int frame)
{
    GifColor col = {128, 128, 128, 128};
    GifRect r = {-4096, -2048, 8192, 4096};
    GifRect uv = {8, 8, ScreenWidth * 16, ScreenHeight * 16};
    int fx;
    int fy;

    gif_SetGsReg(0x47, 0x30000);

    gif_SetGsReg(0x4E, 0x1300000C0LL);

    gif_SetGsReg(0x06, ((long long)(ScreenWidth / 64) << 14) | 0x664000800LL);

    setGsReg(0x4C, frame | 0x80000);
    setGsReg(0x40, 0xFF000001FF0000LL);
    setGsReg(0x18, 0x780000007000LL);
    setGsReg(0x00, 0x116);
    setGsReg(0x01, GIF_COLOR(&col));
    setGsReg(0x03, GIF_UV(uv.x, uv.y));
    setGsReg(0x05, GIF_XY(r.x, r.y));
    /* the far corner is offset once and the near corner added to it, the
       gif_MakeSprite idiom: the offset goes on the size, not on the sum */
    fx = r.w + 0x8000;
    fy = r.h + 0x8000;
    setGsReg(0x03, GIF_UV(uv.x + uv.w, uv.y + uv.h));
    setGsReg(0x05, GIF_XY0(r.x + fx, r.y + fy));
}

#ifdef ICO_RD

/* PC port (renderer wave 5, R5b; docs/port/RENDER_API.md section 16).  The
   block queen_barrier_disp_proc allocates right after tex_ResetVramPri(10)
   is TBP 0x2800; MakeRefractTexture draws the scene into it as a 512 x 256
   frame (FBW 8, XYOFFSET 1792/1920) and the barrier mesh samples it with
   TEX0 TW 9, TH 8.  The GS register decoder takes the block for the named
   AA0 target, 256 x 256; the host binds a per-frame 512 x 256 target in its
   place in list 10 from the allocation to the mesh draw.  The register
   writes are unchanged. */
static RdTarget barrierHostAlias; /* the named target the block is bound over */

static void barrierHostBlock(int tbp, int begin)
{
    gif_HostFlush();
    if (begin) {
        barrierHostAlias = rd_GsNamedBlock((unsigned int)tbp, 512, 256);
        if (barrierHostAlias.id != 0) {
            rd_AliasTarget(barrierHostAlias, rd_BlockTarget((unsigned int)tbp, 512, 256, 0));
        }
    } else if (barrierHostAlias.id != 0) {
        rd_AliasTarget(barrierHostAlias, (RdTarget){0});
        barrierHostAlias.id = 0;
    }
}

#endif

void queen_barrier_set_damage(void)
{
    damageTimer = 60;
    debug_StdPrintfDummy("queen barrier damaged\n");
}

/* the barrier's refraction phase, stepped each frame */
static unsigned short barrierAnimAngle = 0; /* derived name */

inline void queen_barrier_anim(void)
{
    barrierAnimAngle += 0x7D0;
    ripplePhase += 0x1000;
    if (damageTimer > 0) {
        if (--damageTimer < 0) {
            damageTimer = 0;
        }
    }
}

static void makeRefractST(float k)
{
    QVec v;
    QVec w;
    float t;
    float f;
    int ang;
    int i;
    int j;
    int idx;

    memset(&v, 0, sizeof(v));
    v.f[3] = 1.0f;

    t = (float)damageTimer / 60.0f;

    ang = (short)barrierAnimAngle;

    for (i = 0; i < 15; i++) {
        for (j = 0; j < 15; j++) {
            idx = i * 15 + j;

            sceVu0CopyVector(&v, &barrierMesh->nrm[idx]);
            v.f[2] = 0.0f;
            f = GetTableSin((short)(_GetNorm(&v) * 4.0f * 65536.0f + (float)ripplePhase)) * 60.0f;

            _ScaleVectorXYZ(&w, &barrierMesh->nrm[idx], f * t);

            f = GetTableSin((short)ang) * 18.0f;
            ang += 0x4000;

            _ScaleVectorXYZ(&v, &barrierMesh->nrm[idx], f * k);

            _AddVectorXYZ(&v, &barrierMesh->pos[idx], &v);
            _AddVectorXYZ(&v, &v, &w);
            v.f[3] = 1.0f;
            _RotTransPersCurrentMatrix(&v, &v);
            barrierMesh->st[idx].x =
                ((v.f[0] - 2048.0f) + (float)(ScreenWidth >> 1)) * (1.0f / (float)ScreenWidth);
            barrierMesh->st[idx].y =
                ((v.f[1] - 2048.0f) + (float)(ScreenHeight >> 1)) * (1.0f / (float)ScreenHeight);
        }
    }
}

/* the barrier tint, faded from a to b across the damage timer and packed
   into the mesh colour word; the packing helper takes ints converted at its
   call */
static __inline__ int packBarrierColor(int r, int g, int b) /* derived name */
{
    return ((r & 0xFF) << 24) | ((g & 0xFF) << 16) | ((b & 0xFF) << 8) | 0x80;
}

static __inline__ void updateBarrierColor(void) /* derived name */
{
    QVec c;
    sceVu0FVECTOR a = {250.0f, 150.0f, 200.0f, 0.0f};
    sceVu0FVECTOR b = {100.0f, 120.0f, 115.0f, 0.0f};

    sceVu0InterVector(&c, a, b, (float)damageTimer / 60.0f);
    barrierMesh->col = packBarrierColor(c.f[0], c.f[1], c.f[2]);
}

void queen_barrier_disp_proc(GObj *g, float k)
{
    int vram;

    gif_StartPacketPriPath1(10);
    tex_ResetVramPri(10);
    vram = tex_AllocVramAuto(0, 2048);
#ifdef ICO_RD
    barrierHostBlock(vram, 1);
#endif
    MakeRefractTexture(vram >> 5);

    setGsReg(0x4C, ((long long)((ScreenWidth >> 6) & 0x3F) << 16) | 64);
    setGsReg(0x40, ((long long)(ScreenWidth - 1) << 16) | ((long long)(ScreenHeight - 1) << 48));
    setGsReg(0x18, (((long long)(2048 - ScreenWidth / 2) << 4) + screenOffsetX) |
                       ((((long long)(2048 - ScreenHeight / 2) << 4) + screenOffsetY) << 32));

    gif_SetGsReg(0x4E, 0x300000C0);
    gif_SetGsReg(0x47, 0x50000);
    gif_SetGsReg(0x06, (vram | 0x24020000) | 0x600000000LL);
    gif_SetGsReg(0x14, 0x60);

    setGsReg(0x49, 1);
    setGsReg(0x42, ((long long)128 << 32) | 0x44);

    gif_EndPacketPath1();

    updateBarrierColor();

    {
        QVec m1[4];
        QVec root[4];
        QVec m2[4];

        sceVu0InversMatrix(m1, (char *)matrixptr + 0x80);
        GetRootMatrix(root, g);
        sceVu0CopyVector(&m1[3], &root[3]);
        sceVu0MulMatrix(m2, (char *)matrixptr + 0x100, m1);
        _SetCurrentMatrix(m2);
    }

    makeRefractST(k);

    prim_UpdateMesh3D(barrierMesh, 24, buffer_ID);
    prim_DispMesh3D(barrierMesh, 0, 0, -1);
#ifdef ICO_RD
    barrierHostBlock(vram, 0);
#endif
}

void queen_barrier_disp_init(void)
{
    Prim3DVec *pos;
    Prim3DVec *nrm;
    int i;
    int j;
    int idx;
    float x;
    float y;
    float step;

    /* the 14-step angular increment */
    step = 32768.0f / 14.0f;

    barrierMesh = prim_InitMesh3D(15, 15, 1, 0x1C, 0x64787380, 0);

    for (i = 0, x = -16384.0f; x < 16384.0f; i++, x += step) {
        for (j = 0, y = 16384.0f; y < 49152.0f; j++, y += step) {
            QVec v = {{GetTableSin((short)y) * GetTableCos((short)x), GetTableSin((short)x),
                       GetTableCos((short)y) * GetTableCos((short)x), 1.0f}};

            pos = barrierMesh->pos;
            nrm = barrierMesh->nrm;
            idx = i * 15 + j;
            _ScaleVectorXYZ(&pos[idx], &v, 300.0f);
            _NormalizeVector(&nrm[idx], &pos[idx]);
            barrierMesh->st[idx].x = (float)j / 14.0f;
            barrierMesh->st[idx].y = (float)i / 14.0f;
        }
    }
    prim_UpdateMesh3D(barrierMesh, 11, 0);
    prim_UpdateMesh3D(barrierMesh, 11, 1);
    damageTimer = 0;
}
