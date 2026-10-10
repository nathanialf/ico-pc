/* rd_water_test.c: the surfaces that render to and sample from textures.
 *
 * puddle.c, pool.c, queen_barrier_disp.c, waterDot.c and clothAnimation.c
 * with the mesh path (Packet.c, RegistPacket.c, MicroCode.c, DisplayP2O.c,
 * Primitive.c, Matrix.c), the 2D layer (GifPacket.c, DisplayList.c,
 * DmaPacket.c) and matrixDrive.c, compiled as the window build has them
 * (ICO_HOST, ICO_RD).  The rest of the game is stubbed below: the texture
 * module by a stand-in with Texture.c's VRAM bump allocator (a block
 * allocated after tex_ResetVramPri is TBP 0x2800) and one constant texture,
 * gsb_SetVSMatrix by the screen-matrix formula of GsBase.c
 * (gsb_SetVSMatrixSub) with a synthetic clip matrix that accepts every
 * vertex.  Models are synthetic p2o-decoded quads; no disc data.
 *
 * Recording checks (no device):
 *   decoder   what the GS register decoder alone does with the block at TBP
 *             0x2800: FRAME and TEX0 name AA0, without a depth buffer (the
 *             finding the game files' host paths fix)
 *   puddle    PuddleDL: the reflection draws into a 256 x 256 block target
 *             with its own depth (Z GEQUAL, Z write on), no AA0 in list 4,
 *             every TEX0 of the block binds the block target; the camera
 *             scope of the reflection draw holds the 230 x 230 screen
 *             matrix, the frame camera is untouched; the VuCB matrix of the
 *             reflection draw is the reflection camera's (double precision
 *             reference, 1e-5 relative); the DATE/alpha-test state of
 *             leveldown and copy; nothing undecoded
 *   pool      PoolDL and DispLimitedPoolReflactionMesh: the block target
 *             for the scene copy, the refracting grid, the reflection pass
 *             and the reflecting grid; the 204 x 204 camera scope; the
 *             surface STs against a double-precision recomputation
 *   barrier   queen_barrier_disp_proc: a 512 x 256 block target in list 10
 *             for the scene copy and the barrier grid; the refraction STs
 *             against a double-precision recomputation of makeRefractST
 *   waterdot  DispWaterDot: points in list 11 at the game's ftoi4
 *             positions, colour (128, 128, 128, life), ABE, ALPHA 0x48;
 *             dots 600 GS pixels left and right of the centre are dropped
 *             at 4:3 (the game's 400 pixel window) and drawn at 32:9
 *   cloth     DispClothMesh: a lit, textured grid in list 2 (ALPHA 0x44
 *             with ABE, CLAMP 0)
 *
 * Pixel checks (Vulkan; exit 77 without a device, after the above):
 *   puddle    the block against a CPU raster of the VU reference's
 *             triangles (1 LSB) and against the reflected quad projected
 *             in double precision through the reflection camera (interior
 *             and exterior pixels; vertices within 1/16 px); SCENE after
 *             leveldown and copy against a CPU model (DATE mask, LERP FIX
 *             0x10, bilinear of the block at the copy sprite's UVs, ADD FIX
 *             0x60), 1 LSB
 *   pool      the scene copy (2:1 point-sampled) exactly; the refracting
 *             grid against a CPU raster (perspective-correct STQ, bilinear,
 *             modulate) of the VU reference over the copy; then PoolDL's
 *             reflection block and SCENE after the reflecting grid (ADD FIX
 *             0x40)
 *   barrier   the 512 x 256 scene copy exactly; the barrier grid against a
 *             CPU raster of the VU reference's triangles sampling the copy
 *   waterdot  one pixel per dot, Cd + life (ALPHA 0x48, As = life)
 *   cloth     the replay against the VU reference's triangles drawn as
 *             screen prims in the same state (rd_mesh's method), 1 LSB
 *   16:9      Enhanced at 16:9, scene 1x (widescreen reflections): the
 *             puddle's block 341 texels across holding the reflection
 *             compressed by 3/4 about its centre, its clear past the 4:3
 *             picture; SCENE after the copy registered with the picture;
 *             the pool's refracting grid against the 4:3 render at the
 *             same picture position (3 LSB)
 * Every created pipeline is enumerated; no validation error. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"
#include "vu1_ref.h"
/* the game's side */
#include "typedef.h"
#include "box.h"
#include "clothAnimation.h"
#include "debug.h"
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "DmaPacket.h"
#include "DObj.h"
#include "fieldCollision.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "ios.h"
#include "Light.h"
#include "main.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include "MicroCode.h"
#include "motionManager2.h"
#include "multiBgaManager.h"
#include "Packet.h"
#include "particleEffect.h"
#include "pool.h"
#include "Primitive.h"
#include "puddle.h"
#include "quaternion.h"
#include "queen_barrier_disp.h"
#include "RegistPacket.h"
#include "sceneManager.h"
#include "StageAnimation.h"
#include "tableSin.h"
#include "Texture.h"
#include "waterDot.h"
#include "windField.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define W 512
#define H 512

/* ------------------------------------------------- what the files import */
int ScreenWidth = W, ScreenHeight = H;
float center_X = 2048.0f, center_Y = 2048.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
struct IosMemPart *ios_partition_common, *ios_partition_seki, *ios_partition_oomori,
    *ios_partition_sugipon;
void *dmaVif;
char *matrixptr;
int systemStatus[12];
int buffer_ID;
int debug_bounding_flag, debug_specular_flag, debug_shadow_flag, debug_window_flag;
int debug_disp_mesh = 1, debug_disp_particle = 1;
int debug_skel_flag, debug_cloth_info;
float inflateSec;
int texturetranssize, GlobalTimer, currentScreenWidth;
int stage_no = 3;
int currentFocusDistance = 100;
int vsWidth = W, vsHeight = H;
AccessaryRec accessary[32];
SObjSimpleSetting InitialSObjSimpleSetting;
float IdentityQuaternion[4] = {0, 0, 0, 1};
const MotionDef motionKind[1];

static unsigned char s_heap[48u << 20] __attribute__((aligned(16)));

static size_t s_heapAt;

static void *zalloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (s_heapAt + n > sizeof(s_heap)) {
        printf("test heap exhausted\n");
        abort();
    }
    void *p = s_heap + s_heapAt;
    s_heapAt += n ? n : 16;
    memset(p, 0, n);
    return p;
}

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return zalloc((size_t)size);
}

void *iosMallocDebugNoAssert(struct IosMemPart *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return zalloc((size_t)size);
}

void iosFree(void *p)
{
    (void)p;
}

void EntryDelayFree(void *p)
{
    (void)p;
}

void *mallocseki(int size)
{
    return zalloc((size_t)size);
}

void *mallocsekistage(int size)
{
    return zalloc((size_t)size);
}

void *reallocseki(void *p, int size)
{
    (void)size;
    return p;
}

void malloc_MemCpy(void *dst, void *src, int size)
{
    memcpy(dst, src, (size_t)size);
}

int malloc_GetPartition(void)
{
    return 0;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_PrintFontWindow(int col, const char *fmt, ...)
{
    (void)col, (void)fmt;
}

float debug_GetTimerSec(void)
{
    return 0.0f;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d %s\n", file, line, mes);
    abort();
}

void debug_Assert(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

float GetTableSin(short angle)
{
    return sinf((float)angle * (3.14159265f / 32768.0f));
}

float GetTableCos(short angle)
{
    return cosf((float)angle * (3.14159265f / 32768.0f));
}

short GetTableArcTan2(float y, float x)
{
    return (short)(atan2f(y, x) * (32768.0f / 3.14159265f));
}

short GetTableArcCos(float x)
{
    return (short)(acosf(x) * (32768.0f / 3.14159265f));
}

void InitTableSin(void) {}

void InitQuaternionDrive(void) {}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch, (void)addr;
}

void light_MakeLightMatrix(struct Sub15C *self, int idx)
{
    (void)self, (void)idx;
}

void shadow_Render(Sub15C *o)
{
    (void)o;
}

void shadow_RenderVolume(Sub15C *o)
{
    (void)o;
}

void shadow_RenderVolumeMulti(Sub15C *o, int idx)
{
    (void)o, (void)idx;
}

void DrawLine(void *from, void *to, void *color, int z)
{
    (void)from, (void)to, (void)color, (void)z;
}

void DrawLineG(void *from, void *fc, void *to, void *tc, int z)
{
    (void)from, (void)fc, (void)to, (void)tc, (void)z;
}

int gsb_ClipBox(float *p)
{
    (void)p;
    return 1;
}

/* GsBase.c: RegistPacket.c says whether the next part is locked to the
   camera; the stub cull above ignores it */
void gsb_HostCullCameraLocked(int on)
{
    (void)on;
}

static unsigned char s_arena[1 << 16] __attribute__((aligned(16)));

unsigned char *ico_arena_cached_base = s_arena;

unsigned char *ico_arena_base(void)
{
    return s_arena;
}

/* port/game/title_logo.c: RegistPacket.c's reg_DispObj asks it; no title
   here */
int ico_title_logo_skip(const char *model)
{
    (void)model;
    return 0;
}

/* port/game/title_logo.c: reg_DispObj's full-width title models; none
   here */
int ico_title_stretch_model(const char *model)
{
    (void)model;
    return 0;
}

/* port/game/video_options.c: how much wider than 4:3 the picture is
   (waterDot.c's window); 1 unless a case sets it */
static float s_wideX = 1.0f;

float ico_video_wide_x(void)
{
    return s_wideX;
}

/* GsBase.c gsb_HostDotVisible's window (waterDot.c), with no earlier
   camera (the pictures are not blended here): the picture's half-width,
   s_wideX times the 4:3 one, and its half-height, each plus 144 pixels;
   gsb_cull_test checks the real one */
int gsb_HostDotVisible(const int *ip, const float *pos)
{
    const int hx = (int)(((float)(ScreenWidth / 2) * s_wideX + 144.0f) * 16.0f);
    const int hy = (int)(((float)(ScreenHeight / 2) + 144.0f) * 16.0f);

    (void)pos;
    return ip[0] >= 32768 - hx && ip[0] <= 32768 + hx && ip[1] >= 32768 - hy && ip[1] <= 32768 + hy;
}

int ico_arena_contains(const void *p, __SIZE_TYPE__ n)
{
    const unsigned char *c = p;
    return c >= s_arena && c + n <= s_arena + sizeof(s_arena);
}

/* the game functions the five files reach that these cases never call */
#define UNREACHED(name)                                                                            \
    do {                                                                                           \
        printf("unexpected call: %s\n", name);                                                     \
        abort();                                                                                   \
    } while (0)

void ExecuteSEPackage(struct GObj *gobj, int id)
{
    (void)gobj, (void)id;
}

int GetSkeltonFocusNode(GObj *self, int focus)
{
    (void)self, (void)focus;
    return -1;
}

int SetParticleEffectActiveSensing(int id, void *pos, void *quat)
{
    (void)id, (void)pos, (void)quat;
    return 0;
}

void SetParticleEffectUpperLimit(int no, float f)
{
    (void)no, (void)f;
}

void stage_SetLoopFlag(int key, int loop)
{
    (void)key, (void)loop;
}

void stage_SetFrameStep(int target, int val)
{
    (void)target, (void)val;
}

static BgaDisp s_bga[16];

BgaDisp *InitMultiBgaManager(int n)
{
    (void)n;
    return s_bga;
}

void DispMultiBgaManagerWithKind(int kind, BgaDisp *base, int n)
{
    (void)kind, (void)base, (void)n;
}

void EntryMultiBgaManagerNoKind(BgaDisp *bga, int no, void *pos)
{
    (void)bga, (void)no, (void)pos;
}

void GetRootPosition(void *pos, struct GObj *obj)
{
    (void)obj;
    memset(pos, 0, 16);
    ((float *)pos)[3] = 1.0f;
}

/* the queen's root: 1200 units along the frame camera's view direction from
 * its eye (0, -300, -500) */
static float s_barrierRoot[4] = {0.0f, -300.0f + 1200.0f * 0.51449576f,
                                 -500.0f + 1200.0f * 0.85749293f, 1.0f};

void GetRootMatrix(void *mtx, struct GObj *obj)
{
    float *m = mtx;
    (void)obj;
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    memcpy(m + 12, s_barrierRoot, 16);
}

float *GetWindVector(float *power, float *pos)
{
    static float zero[4];
    (void)power, (void)pos;
    return zero;
}

void GetGlobalWallPlane(float *plane, WallCfg *wall)
{
    (void)plane, (void)wall;
    UNREACHED("GetGlobalWallPlane");
}

void GetInverseQuaternion(void *dst, void *src)
{
    (void)dst, (void)src;
    UNREACHED("GetInverseQuaternion");
}

void GetMatrixFromQuaternion(void *mtx, void *q)
{
    (void)mtx, (void)q;
    UNREACHED("GetMatrixFromQuaternion");
}

void RotQuaternionX(void *self, short ang)
{
    (void)self, (void)ang;
    UNREACHED("RotQuaternionX");
}

void RotQuaternionZ(void *self, short ang)
{
    (void)self, (void)ang;
    UNREACHED("RotQuaternionZ");
}

void SetIdentityQuaternion(void *q)
{
    (void)q;
    UNREACHED("SetIdentityQuaternion");
}

void SetQuaternionByAxisRotateV(float *self, short ang, float *src)
{
    (void)self, (void)ang, (void)src;
    UNREACHED("SetQuaternionByAxisRotateV");
}

/* ------------------------------------------------- the texture stand-in
 * Texture.c's VRAM bump allocator (tex_AllocVramAuto, tex_ResetVramPri:
 * 0x2800 after a reset, no head TBP locked in lists 4 and 10), one texture
 * "testtex" of one colour at TBP 0x1000, and the TEX1/TEX0 packet and the UV
 * offset packet tex_TransTexture writes (rd_mesh_test.c's stand-in). */
#define TEST_TBP 0x1000u

static RdTex s_tex;

static unsigned char s_zeroRec[4096] __attribute__((aligned(16)));

static float s_uvPkt[3][4] __attribute__((aligned(16)));

static int s_vramAt[13];

void tex_ResetVramPri(int pri)
{
    dl_SetDLPriority(pri);
    s_vramAt[pri] = 0x2800;
}

int tex_AllocVramAuto(int kind, int size)
{
    int pri = dl_GetPri();
    int r;
    (void)kind;
    if (s_vramAt[pri] == 0) {
        s_vramAt[pri] = 0x2800;
    }
    r = s_vramAt[pri];
    s_vramAt[pri] += size;
    return r;
}

TexExt *tex_GetTexExtData(int idx)
{
    (void)idx;
    return (TexExt *)(void *)s_zeroRec;
}

TexData *tex_GetTextureData(int idx)
{
    (void)idx;
    return (TexData *)(void *)s_zeroRec;
}

int tex_GetTextureNo(const char *name)
{
    return strcmp(name, "testtex") == 0 ? 0 : -1;
}

int tex_GetTextureNum(void)
{
    return 1;
}

int tex_TransTexture(int no, int pri)
{
    (void)no;
    gif_StartPacketPri(pri);
    gif_SetGsReg(0x14, 0);
    gif_SetGsReg(6, (long long)TEST_TBP | (1LL << 14) | (4LL << 26) | (4LL << 30) | (1LL << 34));
    gif_EndPacket();
    uint32_t w0[4] = {0, 0, 0x13000000u, 0x6C018000u};
    uint32_t w2[4] = {0x15000002u, 0, 0, 0};
    memcpy(s_uvPkt[0], w0, 16);
    memset(s_uvPkt[1], 0, 16);
    memcpy(s_uvPkt[2], w2, 16);
    dl_SetDLPriority(pri);
    dl_OpenDma(2, s_uvPkt, 3);
    dl_CloseDma();
    mc_HostDma(2, s_uvPkt, 3);
    return 0;
}

static RdTex texResolve(unsigned long long tex0, int list)
{
    (void)list;
    return (tex0 & 0x3FFF) == TEST_TBP ? s_tex : (RdTex){0};
}

static void makeTexture(void)
{
    static uint8_t px[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; i++) {
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = px[i * 4 + 3] = 0x80;
    }
    s_tex = rd_create_texture(16, 16, px, RD_TEXA_80_80, "testtex");
}

/* ----------------------------------------------------------- the camera
 * The frame camera: zoom 600, a view looking down at the origin from
 * (0, -300, -500) (y down on the screen).  gsb_SetVSMatrix(w, h, d) builds
 * +0xC0 as GsBase.c's gsb_SetVSMatrixSub does (screen = m0 x diag(zoom),
 * vs[1] = w / ScreenWidth, vs[2] = 4/3 h / ScreenWidth, centre 2048, Z from
 * vs[5..8] = 1, 536870880, 2, 262144) and +0x1C0 as a clip matrix that
 * accepts everything in front (x/1000, y/1000, z/2, w = z). */
static float s_scratch[0x800 / 4] __attribute__((aligned(16)));

static float s_common[16][4];

static const float kZoom = 600.0f;

static void qw4(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

static void identity(float *m)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static float ubitsF(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void screenMatrix(float *m, int w, int h)
{
    const float vs0 = kZoom, vs1 = (float)w / (float)ScreenWidth;
    const float vs2 =
        (float)ScreenHeight * 4.0f / ((float)ScreenWidth * 3.0f) * (float)h / (float)ScreenHeight;
    const float vs5 = 1.0f, vs6 = 536870880.0f, vs7 = 2.0f, vs8 = 262144.0f;
    const float zn = (-vs6 * vs7 + vs5 * vs8) / (-vs7 + vs8);
    const float zf = vs8 * vs7 * (-vs5 + vs6) / (-vs7 + vs8);
    memset(m, 0, 64);
    qw4(m + 0, vs1 * vs0, 0, 0, 0);
    qw4(m + 4, 0, vs2 * vs0, 0, 0);
    qw4(m + 8, 2048.0f, 2048.0f, zn, 1.0f);
    qw4(m + 12, 0, 0, zf, 0);
}

static int s_vsCalls;

void gsb_SetVSMatrix(int w, int h, float d)
{
    (void)d;
    s_vsCalls++;
    vsWidth = w;
    vsHeight = h;
    screenMatrix((float *)(matrixptr + 0xC0), w, h);
    float *p = (float *)(matrixptr + 0x1C0);
    memset(p, 0, 64);
    qw4(p + 0, 0.001f, 0, 0, 0);
    qw4(p + 4, 0, 0.001f, 0, 0);
    qw4(p + 8, 0, 0, 0.5f, 1.0f);
    /* +0x240 (projHalf), +0x340 (viewport): not read by these paths */
}

/* the view: world to view, y down, looking from the eye at the origin */
static void makeView(float *m)
{
    const double e[3] = {0, -300, -500};
    double f[3] = {-e[0], -e[1], -e[2]};
    double n = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; i++) {
        f[i] /= n;
    }
    const double r[3] = {1, 0, 0};
    const double d[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                         f[0] * r[1] - f[1] * r[0]};
    memset(m, 0, 64);
    for (int i = 0; i < 3; i++) {
        m[i * 4 + 0] = (float)r[i];
        m[i * 4 + 1] = (float)d[i];
        m[i * 4 + 2] = (float)f[i];
    }
    m[12] = (float)-(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]);
    m[13] = (float)-(d[0] * e[0] + d[1] * e[1] + d[2] * e[2]);
    m[14] = (float)-(f[0] * e[0] + f[1] * e[1] + f[2] * e[2]);
    m[15] = 1.0f;
}

/* the frame's matrices, as camera-root.c and gsb_MakeCommonMatrix leave them */
static void buildScene(void)
{
    matrixptr = (char *)s_scratch;
    memset(s_scratch, 0, sizeof(s_scratch));
    makeView((float *)(matrixptr + 0x80));
    gsb_SetVSMatrix(W, H, 0.0f);
    _MulMatrix(matrixptr + 0x100, matrixptr + 0xC0, matrixptr + 0x80);
    _MulMatrix(matrixptr + 0x200, matrixptr + 0x1C0, matrixptr + 0x80);
    identity((float *)(matrixptr + 0x280));
    identity((float *)(matrixptr + 0x340));
    _InversMatrix(matrixptr + 0x380, matrixptr + 0x80);
    memset(s_common, 0, sizeof(s_common));
    qw4(s_common[0], 0, 0, 0, 1);
    qw4(s_common[1], 4095, 4095, 0, 16777215);
    qw4(s_common[3], ubitsF(0x8000), ubitsF(0x302EC000), ubitsF(0x512), 0);
    memcpy(s_common[4], matrixptr + 0x100, 64);
    memcpy(s_common[8], matrixptr + 0x340, 64);
    memcpy(s_common[12], matrixptr + 0x380, 64);
}

static RdCamera s_frameCam;

/* gsb_MakeCommonMatrix's block and camera, as GsBase.c's host hook */
static void setCommon(void)
{
    RdVuCommon b;
    memcpy(&b, s_common, sizeof(b));
    rd_set_vu_common(&b);
    memset(&s_frameCam, 0, sizeof(s_frameCam));
    memcpy(s_frameCam.view, matrixptr + 0x80, 64);
    memcpy(s_frameCam.proj43, matrixptr + 0xC0, 64);
    s_frameCam.zoom = kZoom;
    s_frameCam.aspect43 = 4.0f / 3.0f;
    s_frameCam.nearZ = 2.0f;
    s_frameCam.farZ = 262144.0f;
    rd_set_camera(&s_frameCam);
}

/* a double-precision product a x b of column-major 4 x 4 matrices */
static void mulD(double *o, const double *a, const double *b)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            double s = 0;
            for (int k = 0; k < 4; k++) {
                s += a[k * 4 + r] * b[c * 4 + k];
            }
            o[c * 4 + r] = s;
        }
    }
}

static void toD(double *o, const float *m)
{
    for (int i = 0; i < 16; i++) {
        o[i] = m[i];
    }
}

/* GS window X, Y (pixels, before XYOFFSET) and Z of p through m */
static void projD(const double *m, const double p[3], double out[3])
{
    double v[4];
    /* column-major: m[0..3] is the column x multiplies */
    for (int r = 0; r < 4; r++) {
        v[r] = m[0 + r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    }
    out[0] = v[0] / v[3];
    out[1] = v[1] / v[3];
    out[2] = v[2] / v[3];
}

/* ------------------------------------------------------------- models
 * A prelit quad as a p2o-decoded part (rd_mesh_test.c's builder, one strip
 * of four vertices): v0 (x0, y0), v1 (x0, y1), v2 (x1, y0), v3 (x1, y1). */
#define NV 4

typedef struct Model {
    PObjModel mdl __attribute__((aligned(16)));
    PObjPart part;
    PObjMatDef mat;
    PObjTexDef texDef;
    float vtx[NV][4] __attribute__((aligned(16)));
    float uv[NV][4] __attribute__((aligned(16)));
    unsigned char col[NV][4];
    short strip[(NV + 2) * 8];
    void *stripTbl[1];
    float boxes[8][4] __attribute__((aligned(16)));
    struct DObjNode nodes[2];
    float nodeMtx[2][16] __attribute__((aligned(16)));
    LightMatrix light __attribute__((aligned(16)));
} Model;

static void makeQuad(Model *m, Sub15C *o, const float c[4][3], const uint8_t rgba[4],
                     const char *name)
{
    memset(m, 0, sizeof(*m));
    memset(o, 0, sizeof(*o));
    for (int v = 0; v < NV; v++) {
        qw4(m->vtx[v], c[v][0], c[v][1], c[v][2], 1.0f);
        qw4(m->uv[v], (float)(v >> 1), (float)(v & 1), 0, 0);
        memcpy(m->col[v], rgba, 4);
    }
    short *p = m->strip;
    p[0] = NV;
    p += 8;
    for (int v = 0; v < NV; v++) {
        p[2] = p[3] = p[4] = p[5] = (short)v;
        p[6] = p[7] = 0;
        p += 8;
    }
    p[0] = -1;
    m->stripTbl[0] = m->strip;
    m->mat.alpha = 1.0f;
    m->mat.wrap = 1;
    m->mat.fbaOff = 1; /* FBA 0 */
    snprintf(m->texDef.name, sizeof(m->texDef.name), "testtex");
    m->texDef.scaleU = m->texDef.scaleV = 1.0f;
    m->part.vtx = (char *)m->vtx;
    m->part.vtxCount = NV;
    m->part.uv = (char *)m->uv;
    m->part.col = (char *)m->col;
    m->part.mats = &m->mat;
    m->part.matCount = 1;
    m->part.texDefs = &m->texDef;
    m->part.texCount = 1;
    m->part.strips = m->stripTbl;
    m->part.stripCount = 1;
    snprintf(m->mdl.name, sizeof(m->mdl.name), "%s", name);
    m->mdl.partCount = 1;
    m->mdl.disp = 0;
    m->mdl.mode.s.shade = 1;
    m->mdl.parts = &m->part;
    m->mdl.boxes = (char *)m->boxes;
    for (int i = 0; i < 2; i++) {
        m->nodes[i].scale[0] = m->nodes[i].scale[1] = m->nodes[i].scale[2] = 1.0f;
        identity(m->nodeMtx[i]);
    }
    o->model = &m->mdl;
    o->nodes = m->nodes;
    o->nodeNum = 1;
    o->nodeMtx = (ICO_WORD)m->nodeMtx;
    o->lightMtx = &m->light;
    o->dispType = 0;
    m->light.mode = 0;
}

static PacHeader *packetOf(Model *m)
{
    return (PacHeader *)m->mdl.groups->packets;
}

/* ------------------------------------------------------- the recording */

typedef struct Cmd {
    int list;
    uint32_t index;
    const RdCmd *c;
    RdStateBlock st;
} Cmd;

typedef struct Found {
    int n;
    Cmd cmd[512];
} Found;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Found *fd = user;
    if (fd->n < 512) {
        fd->cmd[fd->n].list = list;
        fd->cmd[fd->n].index = index;
        fd->cmd[fd->n].c = c;
        fd->cmd[fd->n].st = *s;
        fd->n++;
    }
}

static Found s_fd;

static void walkFrame(const RdFrame *f)
{
    memset(&s_fd, 0, sizeof(s_fd));
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &s_fd);
}

static int isNamed(uint32_t id)
{
    return id != 0 && (id & 0xFFFF) <= RD_TARGET_COUNT;
}

static RdVuPayload payloadOf(const RdFrame *f, const RdCmd *c, const float **mem)
{
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    *mem = (const float *)(const void *)(f->payload + c->u[1] + sizeof(p));
    return p;
}

/* The block target of a list: the non-named colour target of its
 * RDC_TARGETs; checks there is one, that no RDC_TARGET of the list names
 * AA0 and every target-view texture of the list samples the block. */
static uint32_t checkBlock(const char *what, int list, uint32_t ew, uint32_t eh, int depth)
{
    uint32_t block = 0;
    int aa0 = 0, texOk = 1, nTex = 0;
    for (int i = 0; i < s_fd.n; i++) {
        const Cmd *k = &s_fd.cmd[i];
        if (k->list != list) {
            continue;
        }
        if (k->c->type == RDC_TARGET) {
            uint32_t id = k->c->u[0];
            aa0 |= id == rd_target(RD_TARGET_AA0).id;
            if (!isNamed(id)) {
                CHECK(block == 0 || block == id, "%s: one block target in list %d", what, list);
                block = id;
                CHECK(k->c->u[1] == (depth ? id : 0u), "%s: the block's depth bound (%u, %u)", what,
                      k->c->u[1], id);
                CHECK(k->c->u[2] == (ew | eh << 16), "%s: GS size %ux%u (0x%x)", what, ew, eh,
                      k->c->u[2]);
            }
        }
        if (k->c->type == RDC_TEXTURE) {
            const RdTexRec *t = rd__tex_rec(k->c->u[0]);
            if (t && t->kind == RD_TEXKIND_TARGET && t->target != rd_target(RD_TARGET_SCENE).id) {
                nTex++;
                texOk &= t->target == block && block != 0;
            }
        }
    }
    const RdTargetRec *r = rd__target_rec(block);
    CHECK(block != 0 && r && r->w == ew && r->h == eh && r->withDepth == (depth != 0),
          "%s: a %ux%u block target%s in list %d", what, ew, eh, depth ? " with depth" : "", list);
    CHECK(!aa0, "%s: no draw into AA0 in list %d", what, list);
    CHECK(nTex > 0 && texOk, "%s: every TEX0 of the block (%d) samples the block target", what,
          nTex);
    return block;
}

/* the VU draws of a list, in order */
static int vuDraws(int list, const Cmd **out, int max)
{
    int n = 0;
    for (int i = 0; i < s_fd.n && n < max; i++) {
        const Cmd *k = &s_fd.cmd[i];
        if (k->list == list && k->c->type >= RDC_MESH && k->c->type <= RDC_PARTICLES) {
            out[n++] = k;
        }
    }
    return n;
}

/* ---------------------------------------------------- the CPU raster
 * Triangles as the VU program sends them (GS 12.4 X/Y around 2048, STQ,
 * RGBAQ), rasterised at the GS sample points (pixel (x, y) at its integer
 * coordinates), STQ interpolated linearly in screen space and divided per
 * pixel, the texture sampled as the GS does (texel centres at i + 0.5,
 * bilinear or nearest, CLAMP or REPEAT), MODULATE with TCC RGBA, then a
 * blend.  `cover` marks the pixels whose sample point lies inside a
 * triangle. */
typedef struct RTri {
    double x[3], y[3], z[3], s[3], t[3], q[3], c[3][4];
} RTri;

#define MAXTRI 4096

static RTri s_tris[MAXTRI];

static int s_ntri;

static void addVtx(RTri *t, int j, const VuGsVertex *v, int tw, int th)
{
    (void)tw, (void)th;
    t->x[j] = (double)vu_gs_x(v) / 16.0 - (2048.0 - tw / 2.0);
    t->y[j] = (double)vu_gs_y(v) / 16.0 - (2048.0 - th / 2.0);
    t->z[j] = (double)(uint32_t)v->xyz[2];
    t->s[j] = v->stq[0];
    t->t[j] = v->stq[1];
    t->q[j] = v->stq[2];
    for (int k = 0; k < 4; k++) {
        t->c[j][k] = v->rgba[k] & 255;
    }
}

/* the triangles of a VU batch output; fw/fh: the frame the XY live in */
static void addBatch(const VuBatchOut *out, int fw, int fh)
{
    static int kicks[VU_BATCH_MAX];
    int nk = vu1ref_kicks(out, kicks);
    for (int i = 0; i < nk && s_ntri < MAXTRI; i++) {
        RTri *t = &s_tris[s_ntri++];
        for (int j = 0; j < 3; j++) {
            addVtx(t, j, &out->v[kicks[i] - 2 + j], fw, fh);
        }
    }
}

typedef struct RTex {
    const uint8_t *px;
    int w, h, linear, repeat;
} RTex;

static double texel(const RTex *x, int i, int j, int k)
{
    if (x->repeat) {
        i = ((i % x->w) + x->w) % x->w;
        j = ((j % x->h) + x->h) % x->h;
    } else {
        i = i < 0 ? 0 : i >= x->w ? x->w - 1 : i;
        j = j < 0 ? 0 : j >= x->h ? x->h - 1 : j;
    }
    return x->px[(j * x->w + i) * 4 + k];
}

static int sampleTex(const RTex *x, double u, double v, int k)
{
    if (!x->linear) {
        return (int)texel(x, (int)floor(u), (int)floor(v), k);
    }
    const double fx = u - 0.5, fy = v - 0.5;
    const int x0 = (int)floor(fx), y0 = (int)floor(fy);
    const double ax = fx - x0, ay = fy - y0;
    double s = (1 - ax) * (1 - ay) * texel(x, x0, y0, k) + ax * (1 - ay) * texel(x, x0 + 1, y0, k) +
               (1 - ax) * ay * texel(x, x0, y0 + 1, k) + ax * ay * texel(x, x0 + 1, y0 + 1, k);
    return (int)floor(s + 0.5);
}

static int mod7(int t, int c)
{
    const int v = (t * c) >> 7;
    return v > 255 ? 255 : v;
}

typedef enum { BL_NONE, BL_ADD_FIX, BL_LERP_AS } BlendKind;

/* The GS addresses texels in 1/16 steps and filters with 4-bit weights,
 * the GPU with 8-bit weights: with s_range set, rasterise also keeps, per
 * pixel and channel, the lowest and highest result over texel coordinates
 * moved by -1/16, 0, +1/16 on each axis and the filtered texel moved by -1,
 * 0, +1 (s_lo, s_hi); compareImg accepts anything in that range (plus its
 * tolerance).  The texture function then scales the texel's rounding by
 * the vertex colour / 128. */
static int s_range;

static uint8_t s_lo[512 * 512 * 4], s_hi[512 * 512 * 4];

/* the Z model: with s_zbuf set, rasterise tests GS Z GEQUAL against it (and
 * writes it with s_zwrite); a pixel where two fragments' Z lie within
 * 512 GS units of each other or of the buffer (D32F at 2^-32 resolves
 * about 256 units there) is left out of cover */
static double *s_zbuf;

static int s_zwrite;

static uint8_t s_ambig[512 * 512];

/* s_lo/s_hi hold a range for the pixel (rasterise sets it; clear it before
 * a new composite) */
static uint8_t s_rangeValid[512 * 512];

static float s_dbgU[512 * 512], s_dbgV[512 * 512];

static int s_dbgTri[512 * 512];

/* Rasterises the collected triangles over dst (fw x fh RGBA), sampling tex
 * (NULL: untextured), blending; marks cover. */
static void rasterise(uint8_t *dst, uint8_t *cover, int fw, int fh, const RTex *tex, BlendKind bk,
                      int fix, int pabe)
{
    for (int n = 0; n < s_ntri; n++) {
        const RTri *t = &s_tris[n];
        double minx = fmin(t->x[0], fmin(t->x[1], t->x[2])),
               maxx = fmax(t->x[0], fmax(t->x[1], t->x[2]));
        double miny = fmin(t->y[0], fmin(t->y[1], t->y[2])),
               maxy = fmax(t->y[0], fmax(t->y[1], t->y[2]));
        double area =
            (t->x[1] - t->x[0]) * (t->y[2] - t->y[0]) - (t->x[2] - t->x[0]) * (t->y[1] - t->y[0]);
        if (fabs(area) < 1e-9) {
            continue;
        }
        /* vertex order with a positive area, for the top-left rule */
        const int o1 = area > 0 ? 1 : 2, o2 = area > 0 ? 2 : 1;
        const int ord[3] = {0, o1, o2};
        area = fabs(area);
        for (int y = (int)ceil(miny); y <= (int)floor(maxy); y++) {
            for (int x = (int)ceil(minx); x <= (int)floor(maxx); x++) {
                if (x < 0 || y < 0 || x >= fw || y >= fh) {
                    continue;
                }
                double b[3];
                int outside = 0;
                for (int jj = 0; jj < 3; jj++) {
                    /* the edge opposite vertex ord[jj], from a to b */
                    const int a1 = ord[(jj + 1) % 3], a2 = ord[(jj + 2) % 3];
                    const double ex = t->x[a2] - t->x[a1], ey = t->y[a2] - t->y[a1];
                    const double e = ex * (y - t->y[a1]) - ey * (x - t->x[a1]);
                    /* GS and GPU fill rule: a sample on an edge belongs to the
                     * triangle when the edge is a left or a top edge */
                    if (e < 0 || (e == 0 && !(ey < 0 || (ey == 0 && ex > 0)))) {
                        outside = 1;
                    }
                    b[ord[jj]] = e / area;
                }
                if (outside) {
                    continue;
                }
                if (s_zbuf) {
                    double z = 0;
                    for (int j = 0; j < 3; j++) {
                        z += b[j] * t->z[j];
                    }
                    double *zb = &s_zbuf[y * fw + x];
                    if (fabs(z - *zb) < 512.0) {
                        s_ambig[y * fw + x] = 1;
                    }
                    if (z < *zb) {
                        continue;
                    }
                    if (s_zwrite) {
                        *zb = z;
                    }
                }
                double S = 0, T = 0, Q = 0, c[4] = {0, 0, 0, 0};
                for (int j = 0; j < 3; j++) {
                    S += b[j] * t->s[j];
                    T += b[j] * t->t[j];
                    Q += b[j] * t->q[j];
                    for (int k = 0; k < 4; k++) {
                        c[k] += b[j] * t->c[j][k];
                    }
                }
                if (tex) {
                    s_dbgU[y * fw + x] = (float)(S / Q * tex->w);
                    s_dbgV[y * fw + x] = (float)(T / Q * tex->h);
                    s_dbgTri[y * fw + x] = n;
                }
                uint8_t *d = &dst[(y * fw + x) * 4];
                uint8_t *lo = &s_lo[(y * fw + x) * 4], *hi = &s_hi[(y * fw + x) * 4];
                uint8_t res[4] = {0, 0, 0, 0}, mn[4] = {255, 255, 255, 255}, mx[4] = {0, 0, 0, 0};
                const int nOff = tex && tex->linear && s_range ? 3 : 1;
                for (int oi = 0; oi < nOff * nOff * nOff; oi++) {
                    {
                        const int ox = oi % nOff, oy = (oi / nOff) % nOff, ot = oi / (nOff * nOff);
                        const int centre = nOff == 1 || (ox == 1 && oy == 1 && ot == 1);
                        const double du = nOff == 1 ? 0 : (ox - 1) / 16.0,
                                     dv = nOff == 1 ? 0 : (oy - 1) / 16.0;
                        int cs[4];
                        for (int k = 0; k < 4; k++) {
                            int ck = (int)floor(c[k] + 1e-6);
                            if (tex) {
                                int tv =
                                    sampleTex(tex, S / Q * tex->w + du, T / Q * tex->h + dv, k) +
                                    (nOff == 1 ? 0 : ot - 1); /* the filter's last bit */
                                tv = tv < 0 ? 0 : tv > 255 ? 255 : tv;
                                cs[k] = mod7(tv, ck);
                            } else {
                                cs[k] = ck > 255 ? 255 : ck;
                            }
                        }
                        const int blend = bk != BL_NONE && !(pabe && cs[3] < 0x80);
                        uint8_t o[4];
                        for (int k = 0; k < 3; k++) {
                            int v = cs[k];
                            if (blend && bk == BL_ADD_FIX) {
                                v = ((cs[k] * fix) >> 7) + d[k];
                            } else if (blend && bk == BL_LERP_AS) {
                                v = (((cs[k] - d[k]) * cs[3]) >> 7) + d[k];
                            }
                            o[k] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v);
                        }
                        o[3] = (uint8_t)cs[3];
                        for (int k = 0; k < 4; k++) {
                            mn[k] = o[k] < mn[k] ? o[k] : mn[k];
                            mx[k] = o[k] > mx[k] ? o[k] : mx[k];
                            if (centre) {
                                res[k] = o[k];
                            }
                        }
                    }
                }
                /* a range over an earlier pass's range: widen by it */
                for (int k = 0; k < 4; k++) {
                    const int wasLo = s_rangeValid[y * fw + x] ? lo[k] - d[k] : 0;
                    const int wasHi = s_rangeValid[y * fw + x] ? hi[k] - d[k] : 0;
                    int l = mn[k] + (k < 3 ? wasLo : 0), h2 = mx[k] + (k < 3 ? wasHi : 0);
                    lo[k] = (uint8_t)(l < 0 ? 0 : l > 255 ? 255 : l);
                    hi[k] = (uint8_t)(h2 < 0 ? 0 : h2 > 255 ? 255 : h2);
                    d[k] = res[k];
                }
                cover[y * fw + x] = 1;
                s_rangeValid[y * fw + x] = 1;
            }
        }
    }
}

/* the compared pixels: covered with all eight neighbours covered */
static int interior(const uint8_t *cover, int fw, int fh, int x, int y)
{
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            const int u = x + i, v = y + j;
            if (u < 0 || v < 0 || u >= fw || v >= fh || !cover[v * fw + u] ||
                (s_zbuf && s_ambig[v * fw + u])) {
                return 0;
            }
        }
    }
    return 1;
}

static void compareImg(const char *what, const uint8_t *gpu, const uint8_t *ref,
                       const uint8_t *cover, int fw, int fh, int tol, int minPixels, int alpha)
{
    int n = 0, maxd = 0, maxc = 0, bad = 0, bx = -1, by = -1;
    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            if (cover && !interior(cover, fw, fh, x, y)) {
                continue;
            }
            n++;
            int d = 0;
            for (int k = 0; k < (alpha ? 4 : 3); k++) {
                const int i4 = (y * fw + x) * 4 + k;
                int e = abs((int)gpu[i4] - (int)ref[i4]);
                maxc = e > maxc ? e : maxc;
                if (s_range && cover) {
                    /* inside the 1/16-texel range: as far as it is outside */
                    e = gpu[i4] < s_lo[i4]   ? s_lo[i4] - gpu[i4]
                        : gpu[i4] > s_hi[i4] ? gpu[i4] - s_hi[i4]
                                             : 0;
                }
                d = e > d ? e : d;
            }
            if (d > maxd) {
                maxd = d;
            }
            if (d > tol && getenv("RDW_DEBUG") && bad < 8) {
                printf("    bad %d,%d gpu %u %u %u ref %u %u %u lo %u hi %u uv %.4f %.4f tri %d\n",
                       x, y, gpu[(y * fw + x) * 4], gpu[(y * fw + x) * 4 + 1],
                       gpu[(y * fw + x) * 4 + 2], ref[(y * fw + x) * 4], ref[(y * fw + x) * 4 + 1],
                       ref[(y * fw + x) * 4 + 2], s_lo[(y * fw + x) * 4], s_hi[(y * fw + x) * 4],
                       s_dbgU[y * fw + x], s_dbgV[y * fw + x], s_dbgTri[y * fw + x]);
            }
            if (d > tol) {
                if (bad == 0) {
                    bx = x;
                    by = y;
                }
                bad++;
            }
        }
    }
    if (s_range && cover) {
        printf("  %s: %d pixels compared, max %d outside the GS precision range (%d from the "
               "centre sample)\n",
               what, n, maxd, maxc);
    } else {
        printf("  %s: %d pixels compared, max difference %d\n", what, n, maxd);
    }
    CHECK(bad == 0,
          "%s: %d pixels over %d (max %d), first at %d,%d: gpu %u %u %u %u ref %u %u %u %u", what,
          bad, tol, maxd, bx, by, bx >= 0 ? gpu[(by * fw + bx) * 4] : 0,
          bx >= 0 ? gpu[(by * fw + bx) * 4 + 1] : 0, bx >= 0 ? gpu[(by * fw + bx) * 4 + 2] : 0,
          bx >= 0 ? gpu[(by * fw + bx) * 4 + 3] : 0, bx >= 0 ? ref[(by * fw + bx) * 4] : 0,
          bx >= 0 ? ref[(by * fw + bx) * 4 + 1] : 0, bx >= 0 ? ref[(by * fw + bx) * 4 + 2] : 0,
          bx >= 0 ? ref[(by * fw + bx) * 4 + 3] : 0);
    CHECK(n >= minPixels, "%s: only %d pixels compared", what, n);
}

/* ------------------------------------------------------ VU references */

/* the batches of a model packet as the VIF unpacks them */
static int packetBatches(const PacHeader *pk, const float (**in)[4], int max)
{
    const uint32_t *w = (const uint32_t *)(const void *)pk->data;
    uint32_t nw = pk->size / 4;
    int n = 0;
    for (uint32_t i = 0; i < nw;) {
        uint32_t code = w[i++];
        if (((code >> 24) & 0x7F) == 0x6C) {
            uint32_t num = (code >> 16) & 0xFF;
            if (n < max) {
                in[n++] = (const float (*)[4])(const void *)&w[i];
            }
            i += num * 4;
        }
    }
    return n;
}

/* a normal_c (prelit) draw: the recorded VuCB through vu1_ref */
static void refPrelitFromCmd(const RdFrame *f, const RdCmd *c, PacHeader *pk, int fw, int fh,
                             VuBatchOut *last)
{
    static Vu1Ref r;
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4])mem);
    vu1ref_normal_set_matrix(&r, (const float (*)[4])(mem + 16 * 4));
    const float (*in[16])[4];
    int nb = packetBatches(pk, in, 16);
    static VuBatchOut out;
    for (int i = 0; i < nb; i++) {
        vu1ref_normal_c(&r, (int)p.code, in[i], &out);
        addBatch(&out, fw, fh);
        if (last) {
            *last = out;
        }
    }
}

/* a mesh (grid) draw: the recorded VuCB and the Mesh3D buffer */
static void refGridFromCmd(const RdFrame *f, const RdCmd *c, const Mesh3D *m, int fw, int fh)
{
    static Vu1Ref r;
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4])mem);
    vu1ref_mesh_set_matrix(&r, (const float (*)[4])(mem + 16 * 4));
    if (m->lit) {
        vu1ref_mesh_set_light(&r, (const float (*)[4])(mem + 28 * 4));
    }
    const float (*buf)[4] = (const float (*)[4])m->bufs[buffer_ID];
    const int per = m->stripLen * (m->lit + 2) + 4;
    static VuBatchOut out;
    for (int i = 0; i < m->strips; i++) {
        vu1ref_mesh(&r, (int)p.code, buf + i * per + 1, &out);
        addBatch(&out, fw, fh);
    }
}

/* ---------------------------------------------------- frame helpers */

static uint8_t s_pattern[W * H * 4];

static RdTex s_patternTex;

/* SCENE before the effect: a smooth pattern, alpha 0x80, depth cleared to
 * Z 0, drawn 1:1 in list 0 (nearest, UV +8) */
static void makePattern(void)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &s_pattern[(y * W + x) * 4];
            p[0] = (uint8_t)(x / 2);
            p[1] = (uint8_t)(y / 2);
            p[2] = (uint8_t)((x + y) / 4);
            p[3] = 0x80;
        }
    }
    s_patternTex = rd_create_texture(W, H, s_pattern, RD_TEXA_80_80, "pattern");
}

static void beginScene(void)
{
    static const uint8_t grey[4] = {60, 70, 80, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(0x30000);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_patternTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = (2048 - W / 2) * 16;
    v[0].y = (2048 - H / 2) * 16;
    v[0].s = 8;
    v[0].t = 8;
    v[1].x = (2048 + W / 2) * 16;
    v[1].y = (2048 + H / 2) * 16;
    v[1].s = W * 16 + 8;
    v[1].t = H * 16 + 8;
    for (int i = 0; i < 2; i++) {
        v[i].q = 1.0f;
        v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 0x80;
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    rd_test_gs(0x50000);
    rd_z_write(1);
}

static int readTarget(uint32_t id, uint8_t *dst, uint32_t ew, uint32_t eh)
{
    uint32_t w = 0, h = 0;
    int ok =
        rd__read_target((RdTarget){id}, dst, (size_t)ew * eh * 4, &w, &h) && w == ew && h == eh;
    CHECK(ok, "readback of target %u (%ux%u)", id, ew, eh);
    return ok;
}

/* ============================================================ the puddle */

static Model s_puddleSurf, s_puddleRefl;

static Sub15C s_puddleObj, s_puddleReflObj;

static GObj s_puddleG;

static const uint8_t kSurfCol[4] = {90, 100, 110, 0x80};

/* the surface draws alpha 0x7F, so leveldown and copy (DATE, MSB set)
 * leave SCENE as it is; the 16:9 case turns FBA on for it (MSB forced) to
 * see the copy */
static int s_surfFba;

static const uint8_t kReflCol[4] = {200, 60, 40, 0x80};

/* the reflected quad, at view depth about 450 */
static const float kReflQuad[4][3] = {{-40, -60, 0}, {-40, 20, 0}, {40, -60, 0}, {40, 20, 0}};

/* the puddle surface (the DATE mask) */
static const float kSurfQuad[4][3] = {
    {-120, 0, -100}, {-120, 0, 120}, {120, 0, -100}, {120, 0, 120}};

Sub15C *CSVSYSTEM_InitDObj(int id, SObjSimpleSetting *lay)
{
    (void)id, (void)lay;
    return &s_puddleReflObj;
}

static void setupPuddle(void)
{
    makeQuad(&s_puddleSurf, &s_puddleObj, kSurfQuad, kSurfCol, "puddle_surface");
    s_puddleSurf.mat.fbaOff = !s_surfFba;
    makeQuad(&s_puddleRefl, &s_puddleReflObj, kReflQuad, kReflCol, "puddle_reflect");
    p2o_MakePacket(&s_puddleObj);
    p2o_MakePacket(&s_puddleReflObj);
    memset(&s_puddleG, 0, sizeof(s_puddleG));
    s_puddleG.dobj = &s_puddleObj;
    s_puddleObj.accessary = 1;
    s_puddleObj.work = InitPuddleGeo(&s_puddleG, &InitialSObjSimpleSetting);
}

static void recordPuddle(void)
{
    beginScene();
    PuddleDL(&s_puddleG);
    dl_Swap();
}

/* the reflection camera in double precision: +0xC0 of a 230 x 230 screen
 * times the view */
static void puddleCamD(double *out, int size)
{
    float scr[16];
    double s[16], v[16];
    screenMatrix(scr, size, size);
    toD(s, scr);
    toD(v, (const float *)(matrixptr + 0x80));
    mulD(out, s, v);
}

static void decoderOnlyChecks(void)
{
    /* the GS register decoder alone, the same writes as drawAreaSetup's
     * work packet and copy's TEX0, no host binding */
    dl_Clear();
    setCommon();
    tex_ResetVramPri(4);
    int vram = tex_AllocVramAuto(0, 0x400);
    gif_StartPacketPri(4);
    gif_SetDrawEnviroment(vram, 0, 0x100, 0x100, 0, 0);
    gif_SetZTest(0);
    gif_SetGsReg(0x4E, 0x30000000 | ((vram + 0x400) / 32));
    {
        GifRect r = {-2048, -2048, 4096, 4096};
        GifColor col = {128, 128, 128, 128};
        gif_SpriteSensitiveOrg(&r, 0, 0, &col, 0);
    }
    gif_SetGsReg(6, (long long)vram | 0x20010000 | 0x600000000LL);
    gif_SetDrawEnviroment(0x800, 0, W, H, 1, 0);
    {
        GifRect r = {-W / 2 * 16, -H / 2 * 16, W * 16, H * 16};
        GifRect uv = {212, 212, 3686, 3686};
        GifColor col = {128, 128, 128, 128};
        gif_SpriteSensitiveOrg(&r, 0, &uv, &col, 1);
    }
    gif_EndPacket();
    dl_Swap();
    walkFrame(rd__last_frame());
    int sawAA0 = 0, depth0 = 0, texAA0 = 0;
    for (int i = 0; i < s_fd.n; i++) {
        const RdCmd *c = s_fd.cmd[i].c;
        if (s_fd.cmd[i].list != 4) {
            continue;
        }
        if (c->type == RDC_TARGET && c->u[0] == rd_target(RD_TARGET_AA0).id) {
            sawAA0 = 1;
            depth0 = c->u[1] == 0;
        }
        if (c->type == RDC_TEXTURE) {
            const RdTexRec *t = rd__tex_rec(c->u[0]);
            texAA0 |= t && t->kind == RD_TEXKIND_TARGET && t->target == rd_target(RD_TARGET_AA0).id;
        }
    }
    printf("  decoder alone: block 0x%x drawn as AA0 %d (no depth %d), sampled as AA0 %d\n", vram,
           sawAA0, depth0, texAA0);
    CHECK(vram == 0x2800, "the block after tex_ResetVramPri is 0x2800 (0x%x)", vram);
    CHECK(sawAA0 && depth0 && texAA0,
          "the decoder alone maps the block to AA0 without depth (what the named block fixes)");
    CHECK(rd_gs_named_block(0x2800, 256, 256).id == rd_target(RD_TARGET_AA0).id &&
              rd_gs_named_block(0x2800, 512, 256).id == rd_target(RD_TARGET_AA0).id &&
              rd_gs_named_block(0x2900, 256, 256).id == 0,
          "rd_gs_named_block follows the decoder's table");
}

static uint32_t s_puddleBlock;

static int s_puddleReflIdx = -1, s_puddleSurfIdx = -1;

static void checkPuddleRecording(void)
{
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    s_puddleBlock = checkBlock("puddle", 4, 256, 256, 1);
    const Cmd *vu[8];
    int n = vuDraws(4, vu, 8);
    CHECK(n == 2, "puddle: two mesh draws in list 4 (the surface, the reflection), got %d", n);
    if (n != 2) {
        return;
    }
    s_puddleSurfIdx = (int)(vu[0] - s_fd.cmd);
    s_puddleReflIdx = (int)(vu[1] - s_fd.cmd);
    const RdStateBlock *st = &vu[1]->st;
    CHECK(st->ds.test.ztst == RD_ZTST_GEQUAL && st->ds.zwrite == RD_ZWRITE_ON,
          "puddle: the reflection pass tests Z GEQUAL and writes Z (ztst %u zwrite %u)",
          st->ds.test.ztst, st->ds.zwrite);
    CHECK(vu[0]->st.ds.zwrite == RD_ZWRITE_OFF, "puddle: the surface does not write Z");
    /* the surface draws into SCENE, the reflection into the block: the last
     * RDC_TARGET of list 4 before each */
    uint32_t tgt[2] = {0, 0};
    for (int i = 0; i < s_puddleReflIdx; i++) {
        if (s_fd.cmd[i].list == 4 && s_fd.cmd[i].c->type == RDC_TARGET) {
            tgt[i < s_puddleSurfIdx ? 0 : 1] = s_fd.cmd[i].c->u[0];
            if (i < s_puddleSurfIdx) {
                tgt[1] = tgt[0];
            }
        }
    }
    CHECK(tgt[0] == rd_target(RD_TARGET_SCENE).id && tgt[1] == s_puddleBlock,
          "puddle: the surface draws into SCENE, the reflection into the block (%u, %u)", tgt[0],
          tgt[1]);
    /* the VuCB matrix of the reflection: +0x100 = the 230 x 230 screen x the
     * view (the node is the unit matrix) */
    const float *mem;
    payloadOf(f, vu[1]->c, &mem);
    double cam[16];
    puddleCamD(cam, 0xE6);
    double worst = 0;
    for (int i = 0; i < 16; i++) {
        const double e = fabs(mem[64 + i] - cam[i]) / (fabs(cam[i]) + 1.0);
        worst = e > worst ? e : worst;
    }
    printf("  puddle: VuCB reflection matrix vs double reference: %.2g relative\n", worst);
    CHECK(worst < 1e-5, "puddle: the reflection draw's matrix is the reflection camera (%g)",
          worst);
    payloadOf(f, vu[0]->c, &mem);
    double frame[16];
    puddleCamD(frame, W);
    worst = 0;
    for (int i = 0; i < 16; i++) {
        const double e = fabs(mem[64 + i] - frame[i]) / (fabs(frame[i]) + 1.0);
        worst = e > worst ? e : worst;
    }
    CHECK(worst < 1e-5, "puddle: the surface draw's matrix is the frame camera (%g)", worst);
    /* the camera scope */
    const RdCamera *cs = rd__camera_at(f, 4, vu[1]->index);
    const RdCamera *cf = rd__camera_at(f, 4, vu[0]->index);
    float scr[16];
    screenMatrix(scr, 0xE6, 0xE6);
    CHECK(cs && memcmp(cs->proj43, scr, 64) == 0 && memcmp(cs->view, matrixptr + 0x80, 64) == 0,
          "puddle: the reflection draw's camera scope holds the 230 x 230 screen matrix");
    CHECK(cf && memcmp(cf, &s_frameCam, sizeof(*cf)) == 0,
          "puddle: the surface draw sees the frame camera");
    CHECK(f->hasCamera && memcmp(&f->camera, &s_frameCam, sizeof(s_frameCam)) == 0,
          "puddle: the frame camera is untouched");
    CHECK(rd__camera_scopes(f, NULL) == 1, "puddle: one camera scope (%u)",
          rd__camera_scopes(f, NULL));
    /* the matrices are restored: +0xC0 is the frame's again */
    float frameScr[16];
    screenMatrix(frameScr, W, H);
    CHECK(memcmp(matrixptr + 0xC0, frameScr, 64) == 0, "puddle: +0xC0 restored");
    /* leveldown's and copy's TEST 0x3F001: DATE DATM 1, ATST NEVER, AFAIL
     * RGB_ONLY; the last screen draw is copy's sprite */
    const Cmd *last = NULL;
    for (int i = 0; i < s_fd.n; i++) {
        if (s_fd.cmd[i].list == 4 && s_fd.cmd[i].c->type == RDC_SCREEN) {
            last = &s_fd.cmd[i];
        }
    }
    CHECK(last && last->st.ds.test.date == RD_DATE_DEST_ALPHA_1 &&
              last->st.ds.test.atst == RD_ATST_NEVER && last->st.ds.test.afail == RD_AFAIL_RGB_ONLY,
          "puddle: copy draws under TEST 0x3F001");
    CHECK(last && last->st.ds.blend == RD_BLEND_CS_FIX_ADD_CD && last->st.ds.blendFix == 0x60 &&
              last->st.ds.abe,
          "puddle: copy adds the block at FIX 0x60");
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

/* ============================================================== the pool */

static Model s_poolModel;

static Sub15C s_poolObj;

static GObj s_poolG;

static struct DObjNode s_poolNodes[2];

static float s_poolNodeMtx[2][16] __attribute__((aligned(16)));

static const uint8_t kWallCol[4] = {220, 180, 60, 0x80};

static const float kWallQuad[4][3] = {
    {-90, -140, 120}, {-90, -10, 120}, {90, -140, 120}, {90, -10, 120}};

/* PoolWork's leading fields (pool.c), for the checks */
typedef struct {
    float pos[4];
    float drain[4];
    int splashNo;
    void *splash;
    int word28;
    void *bga;
    int hasGrid, nx, ny;
    float step;
    Mesh3D *reflect, *surface;
    Prim3DVec **wire;
    float **height;
} PoolHead;

static void setupPool(void)
{
    SObjSimpleSetting lay;
    makeQuad(&s_poolModel, &s_poolObj, kWallQuad, kWallCol, "pool_wall");
    p2o_MakePacket(&s_poolObj);
    s_poolObj.nodes = s_poolNodes;
    s_poolObj.nodeMtx = (ICO_WORD)s_poolNodeMtx;
    s_poolObj.accessary = 26; /* no second reflected object */
    memset(&s_poolG, 0, sizeof(s_poolG));
    s_poolG.dobj = &s_poolObj;
    memset(&lay, 0, sizeof(lay));
    lay.pos[0] = 0;
    lay.pos[1] = 0;
    lay.pos[2] = 0;
    lay.scale[0] = 8;     /* nx */
    lay.scale[1] = 36;    /* step */
    lay.scale[2] = 8;     /* ny */
    lay.obj = 0x50709000; /* the surface colour 0x50, 0x70, 0x90 */
    s_poolObj.work = InitPoolGeo((char *)&s_poolG, &lay);
}

static void recordPool(void)
{
    beginScene();
    PoolDL(&s_poolG);
    dl_Swap();
}

/* the surface STs (updatePoolGeo) recomputed in double precision from the
 * mesh positions and heights the game left */
static void checkPoolSurfaceST(void)
{
    const PoolHead *w = s_poolObj.work;
    const Mesh3D *m = w->surface;
    float **grid = w->height;
    double cam[16];
    toD(cam, (const float *)(matrixptr + 0x100));
    double worst = 0;
    for (int i = 0; i < w->nx; i++) {
        for (int j = 0; j < w->ny; j++) {
            const Prim3DVec *q = &m->pos[i * w->ny + j];
            const double p[3] = {q->x, q->y, q->z};
            double v[4];
            for (int r = 0; r < 4; r++) {
                v[r] = cam[r] * p[0] + cam[4 + r] * p[1] + cam[8 + r] * p[2] + cam[12 + r];
            }
            const double h = grid[i][j];
            const double su = (v[0] / v[3] - 2048.0) / W + 0.5 + h * 30.0 / v[3];
            const double sv = (v[1] / v[3] - 2048.0) / H + 0.5 + h * 30.0 / v[3];
            const Prim3DVec *st = &m->st[i * w->ny + j];
            worst = fmax(worst, fmax(fabs(st->x - su), fabs(st->y - sv)));
        }
    }
    printf("  pool: refraction STs vs double reference: max %.2g\n", worst);
    CHECK(worst < 2e-5, "pool: the surface STs (%g)", worst);
}

static uint32_t s_poolBlock;

static void checkPoolRecording(void)
{
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    s_poolBlock = checkBlock("pool", 4, 256, 256, 1);
    const Cmd *vu[8];
    int n = vuDraws(4, vu, 8);
    CHECK(n == 3, "pool: three VU draws in list 4 (surface grid, reflection, reflect grid), got %d",
          n);
    if (n != 3) {
        return;
    }
    CHECK(vu[0]->c->type == RDC_GRID && vu[1]->c->type == RDC_MESH && vu[2]->c->type == RDC_GRID,
          "pool: grid, mesh, grid");
    const RdCamera *cs = rd__camera_at(f, 4, vu[1]->index);
    float scr[16];
    screenMatrix(scr, 0xCC, 0xCC);
    CHECK(cs && memcmp(cs->proj43, scr, 64) == 0,
          "pool: the reflection draw's camera scope holds the 204 x 204 screen matrix");
    CHECK(rd__camera_at(f, 4, vu[0]->index) == &f->camera &&
              rd__camera_at(f, 4, vu[2]->index) == &f->camera,
          "pool: the grids see the frame camera");
    CHECK(vu[1]->st.ds.zwrite == RD_ZWRITE_ON && vu[1]->st.ds.test.ztst == RD_ZTST_GEQUAL,
          "pool: the reflection pass writes and tests Z");
    CHECK(vu[2]->st.ds.blend == RD_BLEND_CS_FIX_ADD_CD && vu[2]->st.ds.blendFix == 0x40 &&
              vu[2]->st.ds.abe,
          "pool: the reflecting grid adds at FIX 0x40");
    CHECK(!vu[0]->st.ds.abe, "pool: the refracting grid (PRIM 0x1C) does not blend");
    checkPoolSurfaceST();
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

/* DispLimitedPoolReflactionMesh: the scene copy and one grid sampling it */
static PoolMesh s_limited;

static void setupLimited(void)
{
    PoolMeshQuad q;
    memset(&s_limited, 0, sizeof(s_limited));
    s_limited.ncol = 10;
    s_limited.nrow = 10;
    s_limited.color = 0x60687080;
    memset(&q, 0, sizeof(q));
    qw4(&q.corner[0].x, -140, 0, -120, 1);
    qw4(&q.corner[1].x, 140, 0, -120, 1);
    qw4(&q.corner[2].x, -140, 0, 140, 1);
    qw4(&q.corner[3].x, 140, 0, 140, 1);
    InitLayoutedPoolReflactionMesh(&s_limited, &q);
}

static void recordLimited(void)
{
    beginScene();
    systemStatus[5] = 1; /* the heights stay 0: no random */
    SetLayoutedPoolReflactionMesh(&s_limited);
    systemStatus[5] = 0;
    DispLimitedPoolReflactionMesh(&s_limited);
    dl_Swap();
}

/* ============================================================ the barrier */

static const float kBarrierK = 1.0f;

static void recordBarrier(void)
{
    beginScene();
    queen_barrier_disp_proc(NULL, kBarrierK);
    dl_Swap();
}

/* queen_barrier_disp.c's static barrierMesh: prim_InitMesh3D's first
 * allocation, made from the test heap by queen_barrier_disp_init */
static Mesh3D *s_barrierMesh;

static Mesh3D *barrierMesh(void)
{
    return s_barrierMesh;
}

static void setupBarrier(void)
{
    s_barrierMesh = (Mesh3D *)(void *)(s_heap + s_heapAt);
    queen_barrier_disp_init();
}

/* makeRefractST in double precision (damage timer 0, ripple and angle
 * phases 0 at the first call) */
static void checkBarrierST(const Mesh3D *m)
{
    double m1[16], v[16], inv[16], cam[16];
    float invF[16];
    _InversMatrix(invF, matrixptr + 0x80);
    toD(inv, invF);
    for (int i = 0; i < 3; i++) {
        inv[12 + i] = s_barrierRoot[i];
    }
    inv[15] = 1.0;
    memcpy(m1, inv, sizeof(m1));
    toD(v, (const float *)(matrixptr + 0x100));
    mulD(cam, v, m1);
    double worst = 0;
    int ang = 0;
    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < 15; j++) {
            const int idx = i * 15 + j;
            const double f = sin((double)(short)ang * M_PI / 32768.0) * 18.0;
            ang += 0x4000;
            double p[3] = {m->pos[idx].x + m->nrm[idx].x * f * kBarrierK,
                           m->pos[idx].y + m->nrm[idx].y * f * kBarrierK,
                           m->pos[idx].z + m->nrm[idx].z * f * kBarrierK};
            double o[3];
            projD(cam, p, o);
            const double s = ((o[0] - 2048.0) + W / 2) / W, t = ((o[1] - 2048.0) + H / 2) / H;
            worst = fmax(worst, fmax(fabs(m->st[idx].x - s), fabs(m->st[idx].y - t)));
        }
    }
    printf("  barrier: refraction STs vs double reference: max %.2g\n", worst);
    CHECK(worst < 2e-5, "barrier: makeRefractST (%g)", worst);
}

static uint32_t s_barrierBlock;

static void checkBarrierRecording(void)
{
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    s_barrierBlock = checkBlock("barrier", 10, 512, 256, 0);
    const Cmd *vu[4];
    int n = vuDraws(10, vu, 4);
    CHECK(n == 1 && vu[0]->c->type == RDC_GRID, "barrier: one grid in list 10 (%d)", n);
    if (n == 1) {
        CHECK(vu[0]->st.ds.pabe && vu[0]->st.ds.blend == RD_BLEND_LERP_AS &&
                  vu[0]->st.ds.magFilter == RD_FILTER_LINEAR,
              "barrier: PABE, ALPHA 0x44, TEX1 linear");
    }
    checkBarrierST(barrierMesh());
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

/* ============================================================ water dots */

static WaterDotWork *s_dots;

#define NDOTS 6

static void setupDots(void)
{
    s_dots = AllocWaterDot(NULL, NDOTS, 1);
    for (int i = 0; i < NDOTS; i++) {
        WaterDot *d = &s_dots->dot[i];
        d->used = i != 2;
        d->life = 32 + 12 * i;
        qw4(&d->pos.x, -80.0f + 30.0f * (float)i, -20.0f + 7.0f * (float)i, 10.0f * (float)i, 1);
    }
}

static void recordDots(void)
{
    beginScene();
    DispWaterDot(s_dots);
    dl_Swap();
}

/* the game's own projection: ftoi4 of (+0x100 x pos) / w */
static void dotScreen(int i, int out[4])
{
    float v[4];
    _ApplyMatrix(v, matrixptr + 0x100, &s_dots->dot[i].pos);
    _ScaleVector(v, v, 1.0f / v[3]);
    _FTOI4Vector(out, v);
}

static void checkDotsRecording(void)
{
    walkFrame(rd__last_frame());
    const Cmd *pts = NULL;
    for (int i = 0; i < s_fd.n; i++) {
        if (s_fd.cmd[i].c->type == RDC_SCREEN && s_fd.cmd[i].list == 11) {
            pts = &s_fd.cmd[i];
        }
    }
    CHECK(pts && pts->c->b[0] == RD_PRIM_POINTS, "water dots: points in list 11");
    if (!pts) {
        return;
    }
    const RdScreenVtx *v =
        (const RdScreenVtx *)(const void *)(rd__last_frame()->payload + pts->c->u[0]);
    const uint32_t n = pts->c->u[1];
    CHECK(n == NDOTS - 1, "water dots: %d points (%u)", NDOTS - 1, n);
    for (uint32_t k = 0, i = 0; k < n && i < NDOTS; i++) {
        if (!s_dots->dot[i].used) {
            continue;
        }
        int ip[4];
        dotScreen((int)i, ip);
        CHECK(v[k].x == (ip[0] & 0xFFFF) && v[k].y == (ip[1] & 0xFFFF) && v[k].z == (uint32_t)ip[2],
              "water dot %u at the game's ftoi4 position", i);
        CHECK(v[k].rgba[0] == 128 && v[k].rgba[3] == (uint8_t)s_dots->dot[i].life,
              "water dot %u colour", i);
        k++;
    }
    CHECK(pts->st.ds.abe && pts->st.ds.blend == RD_BLEND_CS_AS_ADD_CD &&
              pts->st.ds.zwrite == RD_ZWRITE_OFF,
          "water dots: ABE, ALPHA 0x48, no Z write");
}

/* The water dots' window across (DispWaterDot, GsBase.c
 * gsb_HostDotVisible): 400 GS pixels either side of the centre at 4:3 (the
 * 256 pixel half-width plus 144), the half-width times the wide factor plus
 * 144 on a wider picture (827 at 32:9).  Two dots placed through the
 * game's projection 600 pixels left and right of the centre, on the centre
 * row: none drawn at 4:3, both at 32:9 (8/3). */
static WaterDotWork *s_edgeDots;

/* the x that +0x100 projects to GS x gx (pixels) at y, z: the projection is
   (a x + b) / (c x + d) along x */
static float dotXFor(float gx, float y, float z)
{
    VECTOR p0, p1;
    float v0[4], v1[4];
    qw4(&p0.x, 0.0f, y, z, 1.0f);
    qw4(&p1.x, 1.0f, y, z, 1.0f);
    _ApplyMatrix(v0, matrixptr + 0x100, &p0);
    _ApplyMatrix(v1, matrixptr + 0x100, &p1);
    const float a = v1[0] - v0[0], b = v0[0], c = v1[3] - v0[3], d = v0[3];
    return (gx * d - b) / (a - gx * c);
}

static int edgeDotsDrawn(float wideX)
{
    s_wideX = wideX;
    beginScene();
    DispWaterDot(s_edgeDots);
    dl_Swap();
    s_wideX = 1.0f;
    walkFrame(rd__last_frame());
    int n = 0;
    for (int i = 0; i < s_fd.n; i++) {
        if (s_fd.cmd[i].c->type == RDC_SCREEN && s_fd.cmd[i].list == 11) {
            n += (int)s_fd.cmd[i].c->u[1];
        }
    }
    return n;
}

static void checkDotsWindow(void)
{
    s_edgeDots = AllocWaterDot(NULL, 2, 1);
    if (!s_edgeDots) {
        CHECK(0, "water dots: no work for the window case");
        return;
    }
    const float y = s_dots->dot[0].pos.y, z = s_dots->dot[0].pos.z;
    for (int i = 0; i < 2; i++) {
        WaterDot *d = &s_edgeDots->dot[i];
        d->used = 1;
        d->life = 100;
        qw4(&d->pos.x, dotXFor(i ? 2648.0f : 1448.0f, y, z), y, z, 1.0f);
        float v[4];
        int ip[4];
        _ApplyMatrix(v, matrixptr + 0x100, &d->pos);
        _ScaleVector(v, v, 1.0f / v[3]);
        _FTOI4Vector(ip, v);
        CHECK(v[3] != 0.0f && abs(ip[0] - (i ? 2648 : 1448) * 16) <= 16 && ip[1] >= 29568 &&
                  ip[1] <= 35968,
              "water dot window: dot %d at GS (%g, %g), want x %d inside the rows", i, ip[0] / 16.0,
              ip[1] / 16.0, i ? 2648 : 1448);
    }
    const int n43 = edgeDotsDrawn(1.0f);
    const int n329 = edgeDotsDrawn(8.0f / 3.0f);
    CHECK(n43 == 0, "water dots 600 pixels out: none drawn at 4:3 (%d)", n43);
    CHECK(n329 == 2, "water dots 600 pixels out: both drawn at 32:9 (%d)", n329);
}

/* ================================================================ cloth */

static ClothRec s_cloth;

static float s_clothLa[4][4] = {{0.6f, 0.2f, 0.0f, 0.0f},
                                {0.6f, 0.2f, 0.0f, 0.0f},
                                {0.6f, 0.2f, 0.0f, 0.0f},
                                {0.3f, 0.3f, 0.3f, 0.0f}};

static float s_clothLb[4][4] = {{0.0f, 0.0f, -1.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f, 0.0f},
                                {1.0f, 0.0f, 0.0f, 0.0f},
                                {0.0f, 0.0f, 0.0f, 1.0f}};

static void setupCloth(void)
{
    memset(&s_cloth, 0, sizeof(s_cloth));
    s_cloth.mesh = prim_InitMesh3D(6, 5, 1, 0x5C, 0x80808080u, 1);
    Mesh3D *m = s_cloth.mesh;
    for (int i = 0; i < m->nx * m->ny; i++) {
        int x = i % m->nx, y = i / m->nx;
        qw4(&m->pos[i].x, -100.0f + 40.0f * (float)x, -160.0f + 30.0f * (float)y,
            20.0f * (float)((x + y) & 1), 1.0f);
        qw4(&m->st[i].x, (float)x / 5.0f, (float)y / 4.0f, 1.0f, 0.0f);
    }
    prim_UpdateMesh3D(m, 8 | 0x10, 0);
    prim_UpdateMesh3D(m, 8 | 0x10, 1);
    s_cloth.textured = 1;
    snprintf((char *)&s_cloth.tex, 16, "testtex");
}

static void recordCloth(void)
{
    beginScene();
    DispClothMesh(&s_cloth, s_clothLa, s_clothLb);
    dl_Swap();
}

static void checkClothRecording(void)
{
    walkFrame(rd__last_frame());
    const Cmd *vu[4];
    int n = vuDraws(2, vu, 4);
    CHECK(n == 1 && vu[0]->c->type == RDC_GRID, "cloth: one grid in list 2 (%d)", n);
    if (n != 1) {
        return;
    }
    const float *mem;
    RdVuPayload p = payloadOf(rd__last_frame(), vu[0]->c, &mem);
    CHECK(p.prog == RD_PROG_GRID_LIT && p.code == 22, "cloth: mesh code 22 (prog %u code %u)",
          p.prog, p.code);
    const RdStateBlock *s = &vu[0]->st;
    CHECK(s->ds.abe && s->ds.blend == RD_BLEND_LERP_AS && s->ds.texEnabled && s->tex == s_tex.id,
          "cloth: ALPHA 0x44 with ABE, the test texture");
    CHECK(s->ds.wrap.s == RD_WRAP_REPEAT && s->ds.wrap.t == RD_WRAP_REPEAT, "cloth: CLAMP 0");
}

/* ============================================================ pixel cases */

static uint8_t s_img[W * H * 4], s_ref[W * H * 4], s_cover[W * H];

static uint8_t s_blk[512 * 256 * 4], s_blkRef[512 * 256 * 4];

static void puddlePixels(void)
{
    recordPuddle();
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    uint32_t block = checkBlock("puddle (device)", 4, 256, 256, 1);
    if (!block || !readTarget(block, s_blk, 256, 256)) {
        return;
    }
    const Cmd *vu[4];
    if (vuDraws(4, vu, 4) != 2) {
        CHECK(0, "puddle: two VU draws");
        return;
    }
    /* (1) the block: the clear (128, 128, 128, 128), then the reflected quad
     * as the VU reference draws it */
    for (int i = 0; i < 256 * 256; i++) {
        s_blkRef[i * 4 + 0] = s_blkRef[i * 4 + 1] = s_blkRef[i * 4 + 2] = s_blkRef[i * 4 + 3] = 128;
    }
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    static VuBatchOut last;
    refPrelitFromCmd(f, vu[1]->c, packetOf(&s_puddleRefl), 256, 256, &last);
    static const uint8_t flat[4] = {0x80, 0x80, 0x80, 0x80};
    RTex t = {flat, 1, 1, 0, 0};
    rasterise(s_blkRef, s_cover, 256, 256, &t, BL_NONE, 0, 0);
    compareImg("puddle block, reflected quad (VU reference raster)", s_blk, s_blkRef, s_cover, 256,
               256, 1, 1500, 1);
    /* (2) the projection: the VU reference's vertices against the quad
     * projected in double precision through the reflection camera */
    double cam[16];
    puddleCamD(cam, 0xE6);
    double worst = 0;
    for (int k = 0; k < last.count && k < NV; k++) {
        double o[3];
        const double p[3] = {kReflQuad[k][0], kReflQuad[k][1], kReflQuad[k][2]};
        projD(cam, p, o);
        worst = fmax(worst, fmax(fabs(vu_gs_x(&last.v[k]) / 16.0 - o[0]),
                                 fabs(vu_gs_y(&last.v[k]) / 16.0 - o[1])));
    }
    printf("  puddle: reflected vertices vs the double projection: max %.4f px\n", worst);
    CHECK(worst <= 1.0 / 16.0, "puddle: reflected vertices within 1/16 px (%g)", worst);
    /* (3) the block against the double projection itself: pixels 1 px inside
     * the projected quad are the quad's colour, 1 px outside the clear */
    double q[4][3];
    for (int k = 0; k < 4; k++) {
        const double p[3] = {kReflQuad[k][0], kReflQuad[k][1], kReflQuad[k][2]};
        projD(cam, p, q[k]);
        q[k][0] -= 2048.0 - 128.0;
        q[k][1] -= 2048.0 - 128.0;
    }
    const int order[4] = {0, 1, 3, 2};
    int in = 0, out = 0, badIn = 0, badOut = 0;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            double dmin = 1e9;
            int inside = 1;
            for (int e = 0; e < 4; e++) {
                const double *a = q[order[e]], *b = q[order[(e + 1) % 4]];
                const double ex = b[0] - a[0], ey = b[1] - a[1];
                const double d = ((x - a[0]) * ey - (y - a[1]) * ex) / sqrt(ex * ex + ey * ey);
                dmin = fmin(dmin, fabs(d));
                inside &= d >= 0;
            }
            if (dmin < 1.0) {
                continue;
            }
            const uint8_t *g = &s_blk[(y * 256 + x) * 4];
            /* the order's winding may be either: count the minority as inside */
            const int isQuad = g[0] == kReflCol[0] && g[1] == kReflCol[1] && g[2] == kReflCol[2];
            const int isClear = g[0] == 128 && g[1] == 128 && g[2] == 128;
            if (inside) {
                in++;
                badIn += !isQuad;
            } else {
                out++;
                badOut += !isClear;
            }
        }
    }
    if (in > out) { /* the other winding */
        int t2 = in;
        in = out;
        out = t2;
        t2 = badIn;
        badIn = badOut;
        badOut = t2;
    }
    printf("  puddle: block vs the double projection: %d inside, %d outside, %d + %d differ\n", in,
           out, badIn, badOut);
    CHECK(in > 1500 && badIn == 0 && badOut == 0,
          "puddle: the block matches the reflection camera's projection");
    /* (4) SCENE: pattern, alpha 0 (baseSetup's sprite), the surface where it
     * covers (its colour, alpha 0x80), leveldown and copy where alpha >= 0x80 */
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    memcpy(s_ref, s_pattern, sizeof(s_ref));
    for (int i = 0; i < W * H; i++) {
        s_ref[i * 4 + 3] = 0;
    }
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    refPrelitFromCmd(f, vu[0]->c, packetOf(&s_puddleSurf), W, H, NULL);
    RTex tf = {flat, 1, 1, 0, 0};
    rasterise(s_ref, s_cover, W, H, &tf, BL_NONE, 0, 0);
    RTex blk = {s_blk, 256, 256, 1, 0};
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *d = &s_ref[(y * W + x) * 4];
            if (!(d[3] & 0x80)) {
                continue;
            }
            const double u = 13.25 + x * 230.375 / W, v = 13.25 + y * 230.375 / H;
            for (int k = 0; k < 3; k++) {
                int c = d[k] + (((0 - d[k]) * 0x10) >> 7);
                c = c + ((mod7(sampleTex(&blk, u, v, k), 128) * 0x60) >> 7);
                d[k] = (uint8_t)(c > 255 ? 255 : c);
            }
        }
    }
    /* compare everywhere except next to the surface's edges */
    static uint8_t mask[W * H];
    for (int i = 0; i < W * H; i++) {
        mask[i] = 1;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (s_cover[y * W + x] && !interior(s_cover, W, H, x, y)) {
                for (int j = -1; j <= 1; j++) {
                    for (int i = -1; i <= 1; i++) {
                        if (x + i >= 0 && x + i < W && y + j >= 0 && y + j < H) {
                            mask[(y + j) * W + x + i] = 0;
                        }
                    }
                }
            }
        }
    }
    compareImg("puddle SCENE after leveldown and copy", s_img, s_ref, mask, W, H, 1, 100000, 1);
    int masked = 0;
    for (int i = 0; i < W * H; i++) {
        masked += s_cover[i];
    }
    CHECK(masked > 5000, "puddle: the surface covers %d pixels", masked);
}

static uint8_t s_lim43[W * H * 4], s_lim43Cover[W * H];

static void poolPixels(void)
{
    s_range = 1;
    /* (1) DispLimitedPoolReflactionMesh: the copy and the grid sampling it */
    recordLimited();
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    uint32_t block = checkBlock("pool limited", 4, 256, 256, 1);
    if (!block || !readTarget(block, s_blk, 256, 256)) {
        return;
    }
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            memcpy(&s_blkRef[(y * 256 + x) * 4], &s_pattern[(2 * y * W + 2 * x) * 4], 4);
        }
    }
    compareImg("pool: the scene copy (every second texel)", s_blk, s_blkRef, NULL, 256, 256, 0,
               65536, 1);
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    const Cmd *vu[4];
    if (vuDraws(4, vu, 4) != 1) {
        CHECK(0, "pool limited: one grid");
        return;
    }
    memcpy(s_ref, s_pattern, sizeof(s_ref));
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    refGridFromCmd(f, vu[0]->c, s_limited.mesh, W, H);
    RTex t = {s_blkRef, 256, 256, 1, vu[0]->st.ds.wrap.s == RD_WRAP_REPEAT};
    rasterise(s_ref, s_cover, W, H, &t, BL_NONE, 0, 0);
    compareImg("pool: refracting grid over the copy (CPU raster)", s_img, s_ref, s_cover, W, H, 1,
               20000, 0);
    memcpy(s_lim43, s_img, sizeof(s_lim43)); /* for wide169Pixels */
    memcpy(s_lim43Cover, s_cover, sizeof(s_lim43Cover));

    /* (2) PoolDL: the reflection block and the reflecting grid over the
     * refracting one */
    recordPool();
    f = rd__last_frame();
    walkFrame(f);
    block = checkBlock("pool (device)", 4, 256, 256, 1);
    if (!block || !readTarget(block, s_blk, 256, 256) ||
        !readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    if (vuDraws(4, vu, 4) != 3) {
        CHECK(0, "pool: three VU draws");
        return;
    }
    const PoolHead *w = s_poolObj.work;
    /* the block: white clear (255, 255, 255, 128), then the wall quad */
    for (int i = 0; i < 256 * 256; i++) {
        s_ref[i * 4 + 0] = s_ref[i * 4 + 1] = s_ref[i * 4 + 2] = 255;
        s_ref[i * 4 + 3] = 128;
    }
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    refPrelitFromCmd(f, vu[1]->c, packetOf(&s_poolModel), 256, 256, NULL);
    static const uint8_t flat[4] = {0x80, 0x80, 0x80, 0x80};
    RTex tf = {flat, 1, 1, 0, 0};
    rasterise(s_ref, s_cover, 256, 256, &tf, BL_NONE, 0, 0);
    compareImg("pool block, reflected wall (VU reference raster)", s_blk, s_ref, s_cover, 256, 256,
               1, 1000, 1);
    /* SCENE: the refracting grid over the copy, then the reflecting grid
     * adding the block at FIX 0x40 */
    memcpy(s_ref, s_pattern, sizeof(s_ref));
    static uint8_t cover2[W * H];
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    refGridFromCmd(f, vu[0]->c, w->surface, W, H);
    RTex tc = {s_blkRef, 256, 256, 1, vu[0]->st.ds.wrap.s == RD_WRAP_REPEAT};
    rasterise(s_ref, s_cover, W, H, &tc, BL_NONE, 0, 0);
    memset(cover2, 0, sizeof(cover2));
    s_ntri = 0;
    refGridFromCmd(f, vu[2]->c, w->reflect, W, H);
    RTex tr = {s_blk, 256, 256, 1, vu[2]->st.ds.wrap.s == RD_WRAP_REPEAT};
    rasterise(s_ref, cover2, W, H, &tr, BL_ADD_FIX, 0x40, 0);
    for (int i = 0; i < W * H; i++) {
        s_cover[i] &= cover2[i];
    }
    compareImg("pool SCENE: refraction then reflection (CPU raster)", s_img, s_ref, s_cover, W, H,
               1, 20000, 0);
}

/* ============================================================ 16:9
 * Widescreen reflections: with
 * the Enhanced preset at 16:9 (f = 3/4) and the scene at 1x, the blocks
 * with a depth buffer are 256 x 256 GS pixels in 341 texels across and
 * draws into them take the wide x scale; the draws that sample them map
 * their 4:3 u into that (fillDrawCB, gs_block_uv). */
#define WT 683 /* SCENE's texels across at 16:9, 1x */
#define BT 341 /* a block's */

static uint8_t s_wide[WT * H * 4], s_wblk[BT * 256 * 4];

static int nearCol(const uint8_t *a, const int b[3], int tol)
{
    for (int k = 0; k < 3; k++) {
        if (abs((int)a[k] - b[k]) > tol) {
            return 0;
        }
    }
    return 1;
}

/* the 4:3 block position (x, y) against the projected quad q: 1 inside, 0
 * outside, -1 within margin of an edge */
static int quadSide(double q[4][3], double x, double y, double margin)
{
    static const int order[4] = {0, 1, 3, 2};
    double dmin = 1e9;
    int pos = 0, neg = 0;
    for (int e = 0; e < 4; e++) {
        const double *a = q[order[e]], *b = q[order[(e + 1) % 4]];
        const double ex = b[0] - a[0], ey = b[1] - a[1];
        const double d = ((x - a[0]) * ey - (y - a[1]) * ex) / sqrt(ex * ex + ey * ey);
        dmin = fmin(dmin, fabs(d));
        pos += d >= 0;
        neg += d < 0;
    }
    return dmin < margin ? -1 : (pos == 4 || neg == 4);
}

static void wide169Pixels(void)
{
    const double f = 0.75;
    CHECK(fabsf(g_rd.wideX - 0.75f) < 1e-6f, "16:9: the wide factor (%g)", (double)g_rd.wideX);

    /* (1) the puddle: the block widened, its picture the 4:3 one compressed
     * about the block's centre */
    recordPuddle();
    walkFrame(rd__last_frame());
    uint32_t block = checkBlock("puddle 16:9", 4, 256, 256, 1);
    const RdTargetRec *br = rd__target_rec(block);
    CHECK(br && br->tw == BT && br->th == 256 && br->wideBlock && br->wide,
          "16:9: the puddle block is %ux%u texels (wide %d)", br ? br->tw : 0, br ? br->th : 0,
          br ? br->wideBlock : -1);
    if (!br || br->tw != BT || !readTarget(block, s_wblk, BT, 256)) {
        return;
    }
    double cam[16], q[4][3];
    puddleCamD(cam, 0xE6);
    for (int k = 0; k < 4; k++) {
        const double p[3] = {kReflQuad[k][0], kReflQuad[k][1], kReflQuad[k][2]};
        projD(cam, p, q[k]);
        q[k][0] -= 2048.0 - 128.0;
        q[k][1] -= 2048.0 - 128.0;
    }
    int in = 0, out = 0, bad = 0, sideClear = 0;
    for (int y = 0; y < 256; y++) {
        for (int tx = 0; tx < BT; tx++) {
            /* texel tx is GS x tx / sx of the block; the 4:3 picture's
             * x is 128 + (x - 128) / f */
            const double xg = tx * 256.0 / BT, x43 = 128.0 + (xg - 128.0) / f;
            const int side = quadSide(q, x43, (double)y, 1.5);
            if (side < 0) {
                continue;
            }
            const uint8_t *g = &s_wblk[(y * BT + tx) * 4];
            const int isQuad = g[0] == kReflCol[0] && g[1] == kReflCol[1] && g[2] == kReflCol[2];
            const int isClear = g[0] == 128 && g[1] == 128 && g[2] == 128;
            in += side;
            out += !side;
            bad += side ? !isQuad : !isClear;
            sideClear += !side && (x43 < 0.0 || x43 >= 256.0) && isClear;
        }
    }
    printf("  16:9 puddle block: %d inside, %d outside (%d beyond the 4:3 block), %d differ\n", in,
           out, sideClear, bad);
    CHECK(in > 1000 && bad == 0, "16:9: the puddle block is the reflection compressed by f");
    CHECK(sideClear > 5000, "16:9: the puddle block's clear reaches past the 4:3 picture (%d)",
          sideClear);

    /* SCENE: where the surface set the alpha MSB (s_surfFba), the
     * copy adds the block at
     * the 4:3 position of what the pixel shows (the copy sprite stretches,
     * the picture under it is compressed): two colours, the quad's and the
     * clear's, 54 apart in red and -51 in green (FIX 0x60) */
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_wide, WT, H)) {
        return;
    }
    int nq = 0, nc = 0, badQ = 0, badC = 0;
    int vq[3] = {-1, -1, -1}, vc[3] = {-1, -1, -1};
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 1; y < H - 1; y++) {
            for (int tx = 1; tx < WT - 1; tx++) {
                int msb = 1;
                for (int j = -1; j <= 1; j++) {
                    for (int i = -1; i <= 1; i++) {
                        msb &= s_wide[((y + j) * WT + tx + i) * 4 + 3] >= 0x80;
                    }
                }
                if (!msb) {
                    continue;
                }
                const double xw = tx * (double)W / WT, x43 = W / 2 + (xw - W / 2) / f;
                const double u = 13.25 + x43 * 230.375 / W, v = 13.25 + y * 230.375 / H;
                const int side = quadSide(q, u, v, 1.5);
                if (side < 0) {
                    continue;
                }
                const uint8_t *g = &s_wide[(y * WT + tx) * 4];
                int *ref = side ? vq : vc;
                if (pass == 0) {
                    if (ref[0] < 0) {
                        ref[0] = g[0], ref[1] = g[1], ref[2] = g[2];
                    }
                } else if (side) {
                    nq++;
                    badQ += !nearCol(g, vq, 1);
                } else {
                    nc++;
                    badC += !nearCol(g, vc, 1);
                }
            }
        }
    }
    printf("  16:9 puddle SCENE: %d quad pixels (%d differ), %d clear pixels (%d differ); "
           "quad (%d %d %d), clear (%d %d %d)\n",
           nq, badQ, nc, badC, vq[0], vq[1], vq[2], vc[0], vc[1], vc[2]);
    CHECK(nq > 500 && nc > 5000 && badQ == 0 && badC == 0,
          "16:9: the puddle's copy registers with the scene");
    CHECK(abs(vq[0] - vc[0] - 54) <= 1 && abs(vq[1] - vc[1] + 51) <= 1,
          "16:9: the quad and the clear through the copy (FIX 0x60)");

    /* (2) the pool's refracting grid (a mesh: vu_ps) samples the scene
     * copied into the widened block where the pixel itself is: the 16:9
     * result at texel tx is the 4:3 one at GS x tx / sx (the pattern
     * stretches with SCENE, the grid is compressed and its 4:3 STs scaled
     * by f).  Without the scaling a pixel d from the centre would read the
     * copy d / f from it. */
    recordLimited();
    walkFrame(rd__last_frame());
    block = checkBlock("pool limited 16:9", 4, 256, 256, 1);
    br = rd__target_rec(block);
    CHECK(br && br->tw == BT && br->wideBlock, "16:9: the pool block is %u texels across",
          br ? br->tw : 0);
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_wide, WT, H)) {
        return;
    }
    int n = 0, badP = 0, worst = 0;
    for (int y = 0; y < H; y++) {
        for (int tx = 0; tx < WT; tx++) {
            const int x = (int)floor(tx * (double)W / WT + 0.5);
            if (x < 2 || x >= W - 2 || !s_lim43Cover[y * W + x] ||
                !interior(s_lim43Cover, W, H, x, y)) {
                continue;
            }
            /* inside the 16:9 grid: it is the 4:3 grid compressed, so the
             * pixel at 4:3 x43 must be covered there too */
            const double x43 = W / 2 + (tx * (double)W / WT - W / 2) / f;
            const int xi = (int)floor(x43);
            if (xi < 2 || xi >= W - 2 || !interior(s_lim43Cover, W, H, xi, y)) {
                continue;
            }
            const uint8_t *g = &s_wide[(y * WT + tx) * 4], *r = &s_lim43[(y * W + x) * 4];
            int d = 0;
            for (int k = 0; k < 3; k++) {
                d = abs((int)g[k] - r[k]) > d ? abs((int)g[k] - r[k]) : d;
            }
            worst = d > worst ? d : worst;
            badP += d > 3;
            n++;
        }
    }
    printf("  16:9 pool: refracting grid vs 4:3 at the same picture position: %d pixels, max %d, "
           "%d over 3\n",
           n, worst, badP);
    CHECK(n > 20000 && badP == 0, "16:9: the pool's grid samples the widened copy in place");
}

static void barrierPixels(void)
{
    s_range = 1;
    recordBarrier();
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    uint32_t block = checkBlock("barrier (device)", 10, 512, 256, 0);
    if (!block || !readTarget(block, s_blk, 512, 256)) {
        return;
    }
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 512; x++) {
            memcpy(&s_blkRef[(y * 512 + x) * 4], &s_pattern[(2 * y * W + x) * 4], 4);
        }
    }
    compareImg("barrier: the 512 x 256 scene copy", s_blk, s_blkRef, NULL, 512, 256, 0, 131072, 1);
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    const Cmd *vu[4];
    if (vuDraws(10, vu, 4) != 1) {
        CHECK(0, "barrier: one grid");
        return;
    }
    memcpy(s_ref, s_pattern, sizeof(s_ref));
    memset(s_cover, 0, sizeof(s_cover));
    memset(s_rangeValid, 0, sizeof(s_rangeValid));
    s_ntri = 0;
    refGridFromCmd(f, vu[0]->c, barrierMesh(), W, H);
    RTex t = {s_blkRef, 512, 256, 1, vu[0]->st.ds.wrap.s == RD_WRAP_REPEAT};
    /* TEST 0x50000 with ZBUF write on, over SCENE's depth cleared to Z 0 */
    static double zb[W * H];
    memset(zb, 0, sizeof(zb));
    memset(s_ambig, 0, sizeof(s_ambig));
    s_zbuf = zb;
    s_zwrite = vu[0]->st.ds.zwrite == RD_ZWRITE_ON;
    rasterise(s_ref, s_cover, W, H, &t, vu[0]->st.ds.abe ? BL_LERP_AS : BL_NONE, 0,
              vu[0]->st.ds.pabe);
    compareImg("barrier: refracted grid (CPU raster of the ST warp)", s_img, s_ref, s_cover, W, H,
               1, 20000, 0);
    s_zbuf = NULL;
}

static void dotPixels(void)
{
    recordDots();
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    memcpy(s_ref, s_pattern, sizeof(s_ref));
    for (int i = 0; i < NDOTS; i++) {
        if (!s_dots->dot[i].used) {
            continue;
        }
        int ip[4];
        dotScreen(i, ip);
        /* rd's point rule (rd_replay.c expand): the pixel whose integer
         * coordinate is nearest, ties to the left/top */
        const int px = (int)ceil(((ip[0] & 0xFFFF) - (2048 - W / 2) * 16 - 8) / 16.0);
        const int py = (int)ceil(((ip[1] & 0xFFFF) - (2048 - H / 2) * 16 - 8) / 16.0);
        if (px < 0 || py < 0 || px >= W || py >= H) {
            continue;
        }
        uint8_t *d = &s_ref[(py * W + px) * 4];
        for (int k = 0; k < 3; k++) {
            const int c = d[k] + ((128 * s_dots->dot[i].life) >> 7);
            d[k] = (uint8_t)(c > 255 ? 255 : c);
        }
        d[3] = (uint8_t)s_dots->dot[i].life;
    }
    compareImg("water dots (one pixel each, Cd + life)", s_img, s_ref, NULL, W, H, 0, W * H, 1);
}

/* cloth: the VU reference's triangles drawn as screen prims in the state of
 * the grid draw (rd_mesh_test.c's method) */
static void applyState(const RdStateBlock *s)
{
    rd_test(&s->ds.test);
    rd_z_write(s->ds.zwrite == RD_ZWRITE_ON);
    rd_fba(s->ds.fba);
    rd_pabe(s->ds.pabe);
    rd_col_clamp(s->ds.colclamp);
    rd_tex_a((RdTexA)s->ds.texa);
    rd_blend((RdBlend)s->ds.blend, s->ds.blendFix, s->ds.abe);
    rd_sampler_filter((RdFilter)s->ds.magFilter, (RdFilter)s->ds.minFilter);
    rd_sampler_wrap((RdWrap)s->ds.wrap.s, (RdWrap)s->ds.wrap.t);
    if (s->ds.texEnabled) {
        rd_texture((RdTex){s->tex}, (RdTexFn)s->ds.texFn, (RdTcc)s->ds.tcc);
    } else {
        rd_texture_off();
    }
    rd_gouraud((int)s->gouraud);
    rd_color_mask(s->ds.fbmsk);
    rd_uv_offset(0.0f, 0.0f);
}

static RdScreenVtx s_sv[3 * MAXTRI];

static void clothPixels(void)
{
    recordCloth();
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    const RdFrame *f = rd__last_frame();
    walkFrame(f);
    const Cmd *vu[4];
    if (vuDraws(2, vu, 4) != 1) {
        CHECK(0, "cloth: one grid");
        return;
    }
    const RdStateBlock st = vu[0]->st;
    s_ntri = 0;
    /* the reference triangles, back to GS 12.4 */
    static Vu1Ref r;
    const float *mem;
    RdVuPayload p = payloadOf(f, vu[0]->c, &mem);
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4])mem);
    vu1ref_mesh_set_matrix(&r, (const float (*)[4])(mem + 16 * 4));
    vu1ref_mesh_set_light(&r, (const float (*)[4])(mem + 28 * 4));
    const Mesh3D *m = s_cloth.mesh;
    const float (*buf)[4] = (const float (*)[4])m->bufs[buffer_ID];
    const int per = m->stripLen * (m->lit + 2) + 4;
    static VuBatchOut out;
    static int kicks[VU_BATCH_MAX];
    uint32_t nv = 0;
    for (int i = 0; i < m->strips; i++) {
        vu1ref_mesh(&r, (int)p.code, buf + i * per + 1, &out);
        int nk = vu1ref_kicks(&out, kicks);
        for (int k = 0; k < nk; k++) {
            for (int j = 0; j < 3; j++) {
                const VuGsVertex *v = &out.v[kicks[k] - 2 + j];
                RdScreenVtx *o = &s_sv[nv++];
                o->x = (int32_t)vu_gs_x(v);
                o->y = (int32_t)vu_gs_y(v);
                o->z = (uint32_t)v->xyz[2];
                o->s = v->stq[0];
                o->t = v->stq[1];
                o->q = v->stq[2];
                for (int c = 0; c < 4; c++) {
                    o->rgba[c] = (uint8_t)(v->rgba[c] & 255);
                }
            }
        }
    }
    memcpy(s_ref, s_img, sizeof(s_ref));
    beginScene();
    dl_SetDLPriority(2);
    applyState(&st);
    rd_screen_prims(RD_PRIM_TRIANGLES, s_sv, nv, RD_SPACE_WORLD, 0, 0);
    dl_Swap();
    if (!readTarget(rd_target(RD_TARGET_SCENE).id, s_img, W, H)) {
        return;
    }
    int drawn = 0;
    for (int i = 0; i < W * H; i++) {
        drawn += memcmp(&s_ref[i * 4], &s_pattern[i * 4], 3) != 0;
    }
    compareImg("cloth: grid vs the VU reference as screen prims", s_ref, s_img, NULL, W, H, 1,
               W * H, 1);
    CHECK(drawn > 5000, "cloth: %d pixels drawn", drawn);
}

/* ------------------------------------------------------------- the main */

static void setup(void)
{
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
    makePattern();
}

static void buildAll(void)
{
    buildScene();
    systemStatus[0] = 1;
    systemStatus[1] = 2;
    setupPuddle();
    setupPool();
    setupLimited();
    setupBarrier();
    setupDots();
    setupCloth();
}

static void recordingChecks(void)
{
    decoderOnlyChecks();
    recordPuddle();
    checkPuddleRecording();
    recordPool();
    checkPoolRecording();
    recordBarrier();
    checkBarrierRecording();
    recordDots();
    checkDotsRecording();
    checkDotsWindow();
    recordCloth();
    checkClothRecording();
    CHECK(s_vsCalls >= 3, "gsb_SetVSMatrix called by the effects (%d)", s_vsCalls);
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(W, H)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    setup();
    buildAll();
    recordingChecks();
    rd_shutdown();
    if (failures) {
        printf("rd_water_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_water_test: recording ok\n");

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(W, H, &st, NULL)) {
        printf("rd_water_test: SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    setup();
    buildScene();
    puddlePixels();
    poolPixels();
    barrierPixels();
    s_range = 0;
    dotPixels();
    clothPixels();

    static RdPipeKeyInt keys[1024];
    const uint32_t n = rd__enumerate_reachable(keys, 1024);
    printf("  pipelines: %u created, %u reachable\n", rd__pipeline_count(), n);
    for (uint32_t i = 0; i < rd__pipeline_count(); i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 1024; j++) {
            found |= rd__pipe_key_equal(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u blend %u z %u/%u) is not enumerated", i,
              k->gs.program, k->vs, k->gs.blend, k->gs.ztst, k->gs.zwrite);
    }
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    rd_shutdown();

    /* widescreen reflections: the same effects at 16:9 */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 1.0f;
    if (!rd_init(W, H, &st, NULL)) {
        CHECK(0, "rd_init at 16:9");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        buildScene();
        s_surfFba = 1;
        setupPuddle();
        wide169Pixels();
        CHECK(rhi_vk_validation_error_count() == 0, "16:9: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
    if (failures) {
        printf("rd_water_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_water_test: ok\n");
    return 0;
}
