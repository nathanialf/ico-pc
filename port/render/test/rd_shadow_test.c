/* rd_shadow_test.c: the shadow count on the stencil.
 *
 * Shadow.c with the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c) and
 * Matrix.c, compiled as the window build has them (ICO_HOST, ICO_RD); the
 * rest of the game is stubbed below.  No disc data: a receiver plane drawn
 * as sprites, volumes as synthetic rectangles (rd_shadow_tris) and one
 * synthetic shadow model (a triangle caster) through shadow_RenderVolume.
 *
 * Recording (no device):
 *   e  shadow_Reset, shadow_RenderVolume and shadow_Draw record, in list 3,
 *      the register writes of their packets as rd state, the reset and the
 *      resolve, and the volume's eight triangles with the signs of the RGBAQ
 *      the packet carries (compared with the packet words Shadow.c wrote),
 *      each vertex tagged with its triangle's place; at a scene scale
 *      of 2 the blur levels keep 256, 128 and 64 texels, WORK0 256.  The same on a
 *      frame 1376 wide (32:9): the count 1376 x H, the levels the PS2's
 *      sizes per 512 columns (688 x 256, 344 x 128, 172 x 64: the named
 *      targets and every FRAME, rect and UV of the chain), the composites'
 *      UVs over those widths.
 *   w  on a frame 2048 wide (48:9), a caster's volume
 *      running past the picture's side: every edge clipped at the edge clip
 *      (GSB_SHADOW_CLIP_MAX), the strip drawn whole (eight triangles), its
 *      clipped ends inside the range emitVolumeStrip keeps.
 * On a Vulkan device (exit 77 without one, after the recording checks):
 *   a  the resolved count against the wrapped colour sum the GS makes (4 n
 *      mod 256, A = 0x80 where non-zero), exact, for net counts 0..127,
 *      -1..-127, 2, 64, 128, 256 and faces behind the receiver;
 *   b  each blur level against a GS bilinear reference of the level before
 *      it (as read back), within 1 LSB;
 *   c  each composite alone (the other blends 0) against the GS LERP of the
 *      bilinear texel, within 1 LSB; all three together within 3;
 *   d  the receiver half whose alpha MSB is set (FBA) is left untouched by
 *      the composites (DATE, DATM 0), the other half is shadowed;
 *   e  Shadow.c's own volume shadows the receiver where the caster's
 *      projection along the shadow direction lands (count +-1) and nowhere
 *      far from it.
 *   f  at scene scale 4 the first blur level's integral of a
 *      rectangle moved by quarter pixels varies by under 0.5 % (the count is
 *      box-reduced to the GS size before level 1 samples it).
 *   s  above 1x at scene scale 4: each composite, not flagged above 1x,
 *      against the sampler's bilinear of its level at each texel's UV (as
 *      at 1x) within 3 LSB; then with RdProfile.shadowBspline ([dev]
 *      shadow_bspline) flagged and against above1x_ref.c's B-spline of its
 *      level within 3 LSB (4 on the fixed-function blender), a row across
 *      the shadow falling and rising once; the softening's AA0 (at its GS size) the area box
 *      of SCENE byte for byte and its composite back the sampler's bilinear
 *      between the same texels within 2, not counted above 1x; the passes
 *      counted (g_rd.above1xDraws), none in the 1x run.
 *   o  the count resolved in one draw from the stencil view against the
 *      seven stencil-tested draws (and the box reduction above 1x;
 *      RdProfile.shadowOne flipped between two recordings of the same
 *      frame): the count at its GS size (rd__shadow_count_read), the three
 *      blur levels and SCENE byte for byte, for the synthetic volumes,
 *      Shadow.c's model and a rectangle at sub-pixel offsets, at 1x, at
 *      scale 2 and at 1931 x 1093 texels (a fractional scale both ways);
 *      each recording resolved by the path asked (g_rdPerf.shadowOne,
 *      shadowSeven).  The count's bytes for n = 0, 1, 63, 64, 128 and 255
 *      (rd__shadow_table_texel, the one draw's table) against 4 n mod 256
 *      and A = 0x80 where that is non-zero.  Without the view (a backend
 *      whose RhiLimits.stencilView is false) the comparison is skipped
 *      with a line.
 *   x  (`rd_shadow_test edges`, ctest rd_shadow_edges: this case alone)
 *      the two kernels at scene scale 2: the same frame (the synthetic
 *      volumes, Shadow.c's model, a rectangle at a sub-pixel offset, all
 *      three composites) replayed with one bilinear tap (the default) and
 *      with the B-spline (RdProfile.shadowBspline flipped between two
 *      recordings): the count's levels byte for byte, the composites
 *      flagged above 1x only with the B-spline, and SCENE equal byte for
 *      byte at every texel but those within N of a shadow edge, N the two
 *      kernels' joint support (level texels i0 - 1 .. i0 + 2 about the
 *      sample point) and one texel each side for a rounding tie: a texel
 *      may differ only where a composited level is not constant over that
 *      4 x 4 + 1 window (at 2x, N = 3 level texels = 12, 24 and 48 SCENE
 *      texels for levels 1, 2 and 3); some texels do differ.
 *   8  (the step 8: the North American disc and the 2001 previews, their
 *      face colours 0x08 / 0xF8; rd_shadow.c "The step") without a device:
 *      Shadow.c's model recorded with ico_region the North American disc:
 *      the packet's colours are 0x08 / 0xF8 and the recorded signs are the
 *      packet's (both signs present: before the shadow step change every
 *      face was recorded as a decrement), the reset and the resolve carry b[0] 8 (the European
 *      recordings b[0] 0, the step 4, their bytes unchanged); the one draw's
 *      table for the step 8 against 8 n mod 256 for n = 0, 1, 31, 32, 33,
 *      63, 64, 255.  On the device (the seven draws, and the one draw where
 *      the profile has it): the synthetic volumes' count against 8 n mod 256
 *      (A = 0x80 where non-zero) at every pixel, n = 1, 31, 32, 33, 64 spelled
 *      out; the one draw against the seven draws as in (o); every pipeline
 *      created enumerated with the step 8's A key in the set.
 * Every pipeline created is enumerated; no validation errors. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "above1x_ref.h"
#include "rd_internal.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "Matrix.h"
#include "Shadow.h"
#include "main.h"
#include "../../include/ico_frame.h"
#include "../../core/region.h" /* ico_region: the step 8's recordings */

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

/* ------------------------------------------------- what the files import */
int ScreenWidth = 512, ScreenHeight = 512;
/* GsBase.c's native frame: 4:3 */
IcoFrame gsFrame = {512, 512, 8, 512.0f, 4.0f / 3.0f};

float center_X = 2048.0f, center_Y = 2048.0f;

int screenOffsetX, screenOffsetY;

int fbKeep;

void *ios_partition_common;

void *dmaVif;

char *matrixptr;

int debug_font_flag;

/* debug.c's and GsBase.c's; only the 2001 builds' shadow twins and volume test read them */
int debug_fullscreen_effect, debug_shadow_flag;

int gsb_ClipBox(float *p)
{
    (void)p;
    return 1;
}

StageSetting GlobalStageSetting;

PadState pad[16];

static unsigned char s_heap[8u << 20] __attribute__((aligned(16)));

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

void iosFree(void *p)
{
    (void)p;
}

void *mallocseki(int size)
{
    return zalloc((size_t)size);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a, (void)b, (void)c, (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...)
{
    (void)x, (void)y, (void)col, (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

void mc_Reset(void) {}

/* GsBase.c: Shadow.c's edge clip across, the frame's width (ScreenWidth at
   4:3), at most GSB_SHADOW_CLIP_MAX (gsb_cull_test checks GsBase.c's own) */
float gsb_HostShadowClipX(void)
{
    const float x = (float)gsFrame.w;
    return x > GSB_SHADOW_CLIP_MAX ? GSB_SHADOW_CLIP_MAX : x;
}

float GetTableSin(short angle)
{
    return sinf((float)angle * (3.14159265f / 32768.0f));
}

float GetTableCos(short angle)
{
    return cosf((float)angle * (3.14159265f / 32768.0f));
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch, (void)addr;
}

void tex_LockHeadTBP(int tbp, int pri)
{
    (void)tbp, (void)pri;
}

void tex_UnlockHeadTBP(int pri)
{
    (void)pri;
}

void GetRootPositionByDObj(void *pos, struct Sub15C *src)
{
    (void)src;
    memset(pos, 0, 16);
}

void *isysGObjGetExist_begin(void)
{
    return NULL;
}

void *isysGObjGetExist_next(void *o)
{
    (void)o;
    return NULL;
}

/* the EE word arena (eeword.h) */
static unsigned char s_arena[1 << 12] __attribute__((aligned(16)));

unsigned char *ico_arena_cached_base = s_arena;

unsigned char *ico_arena_base(void)
{
    return s_arena;
}

int ico_arena_contains(const void *p, __SIZE_TYPE__ n)
{
    const unsigned char *c = p;
    return c >= s_arena && c + n <= s_arena + sizeof(s_arena);
}

/* ------------------------------------------------------------- the scene
 * SCENE 512 x 512, cleared to kClear; the receiver: two sprites at GS Z
 * kZr, Z write on, the left half alpha 0x40 (receives), the right half the
 * same with FBA on (alpha MSB set: masked by the composites' DATE). */
#define W 512
#define H 512

static const uint8_t kClear[4] = {20, 40, 60, 0x80};

static const uint8_t kRecv[4] = {200, 180, 160, 0x40};

static const uint32_t kZr = 0x40000000u; /* receiver */

static const uint32_t kZf = 0x50000000u; /* volume faces in front of it */

static const uint32_t kZb = 0x30000000u; /* and behind it (fail Z GEQUAL) */

static const int kShadowCol[3] = {64, 48, 32}; /* GlobalStageSetting.shadowColR/G/B */

static const int kDepth = 0x70; /* shadowDepth */

static const int kBlend[4] = {0, 0x80, 0x60, 0x40};

static RdScreenVtx sv(int px, int py, uint32_t z, const uint8_t *rgba)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - W / 2 + px) * 16;
    v.y = (2048 - H / 2 + py) * 16;
    v.z = z;
    v.q = 1.0f;
    if (rgba) {
        memcpy(v.rgba, rgba, 4);
    }
    return v;
}

static void sprite(int x0, int y0, int x1, int y1, uint32_t z, const uint8_t *rgba)
{
    RdScreenVtx v[2] = {sv(x0, y0, z, rgba), sv(x1, y1, z, rgba)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, 0);
}

static void drawReceiver(void)
{
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, RD_TARGET_OFFSET);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kClear, 1, 0);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP); /* leaks into list 3: the chain's CLAMP */
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(0x30000);
    rd_z_write(1);
    rd_fba(0);
    sprite(0, 0, W / 2, H, kZr, kRecv);
    rd_fba(1);
    sprite(W / 2, 0, W, H, kZr, kRecv);
    rd_fba(0);
}

/* ------------------------------------------------- the synthetic volumes
 * Axis-aligned rectangles as two triangles each, all of one sign:
 *   rows   0..63   n = column / 4 (0..127): rectangle k covers x >= 4k
 *   rows  64..127  n = -(column / 4)
 *   rows 128..191  x <  64: n = 2; 64..127: 64; 128..255: 128; 256..383:
 *                  256; 384..511: 3 in front of the receiver and 5 more
 *                  behind it (n = 3)
 * The GS sums 4 or 0xFC per face, so the count colour is 4 n mod 256. */
static RdScreenVtx s_tri[4096 * 3];

static int8_t s_sign[4096];

static uint32_t s_nt;

static void rect(int x0, int y0, int x1, int y1, uint32_t z, int sign)
{
    RdScreenVtx a = sv(x0, y0, z, NULL), b = sv(x1, y0, z, NULL), c = sv(x0, y1, z, NULL),
                d = sv(x1, y1, z, NULL);
    RdScreenVtx q[6] = {a, b, c, c, b, d};
    for (int t = 0; t < 2; t++) {
        memcpy(&s_tri[s_nt * 3], &q[t * 3], 3 * sizeof(RdScreenVtx));
        s_sign[s_nt++] = (int8_t)sign;
    }
}

static void buildVolumes(void)
{
    s_nt = 0;
    for (int k = 1; k < 128; k++) {
        rect(4 * k, 0, W, 64, kZf, 1);
        rect(4 * k, 64, W, 128, kZf, -1);
    }
    for (int k = 0; k < 256; k++) {
        if (k < 2) {
            rect(0, 128, 64, 192, kZf, 1);
        }
        if (k < 64) {
            rect(64, 128, 128, 192, kZf, 1);
        }
        if (k < 128) {
            rect(128, 128, 256, 192, kZf, 1);
        }
        rect(256, 128, 384, 192, kZf, 1);
        if (k < 3) {
            rect(384, 128, W, 192, kZf, 1);
        }
        if (k < 5) {
            rect(384, 128, W, 192, kZb, 1);
        }
    }
}

static int expectedCount(int x, int y)
{
    if (y < 64) {
        return x / 4;
    }
    if (y < 128) {
        return -(x / 4);
    }
    if (y < 192) {
        return x < 64 ? 2 : x < 128 ? 64 : x < 256 ? 128 : x < 384 ? 256 : 3;
    }
    return 0;
}

/* the GS: the step's face colour per face (0x04 / 0xFC, or 0x08 / 0xF8
 * on the North American disc and the 2001 previews), summed with COLCLAMP 0 */
static uint8_t wrappedColourStep(int n, int step)
{
    uint8_t c = 0;
    for (int i = 0; i < (n < 0 ? -n : n); i++) {
        c = (uint8_t)(c + (n < 0 ? 256 - step : step));
    }
    return c;
}

static uint8_t wrappedColour(int n)
{
    return wrappedColourStep(n, 4);
}

/* the step the frames being recorded carry (8 under ico_region's North
 * American disc), which checkRecording expects in the reset and resolve */
static int s_step = 4;

/* --------------------------------------------- the synthetic shadow model
 * View = identity; the screen matrix (+0xC0) maps view (x, y, z) to GS
 * (2048 + kF x / z, 2048 + kF y / z, kB / z); the mesh programs' ftoi4 makes
 * GS Z = 16 kB / z.  The caster is one triangle at z = 100, the shadow
 * direction kDir, the volume kLen long; the receiver plane is at view depth
 * kZPlane (its sprites at GS Z 16 kB / kZPlane). */
static const float kF = 256.0f, kB = 1048576.0f, kZPlane = 150.0f, kLen = 100.0f;

static const float kDir[3] = {0.3f, 0.2f, 1.0f};

static const float kCaster[3][3] = {
    {30.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 100.0f}, {0.0f, 30.0f, 100.0f}};

typedef struct TestRun {
    short count;
    short pad2;
    short vtx;
    char pad6[10];
} TestRun;

_Static_assert(sizeof(TestRun) == 16, "Shadow.c's ShadowRun");

static Sub15C *s_obj;

static void setMatrices(void)
{
    static float block[0x400 / 4] __attribute__((aligned(16)));
    matrixptr = (char *)block;
    float (*view)[4] = (float (*)[4])(matrixptr + 0x80);
    float (*scr)[4] = (float (*)[4])(matrixptr + 0xC0);
    memset(block, 0, sizeof(block));
    for (int i = 0; i < 4; i++) {
        view[i][i] = 1.0f;
    }
    scr[0][0] = kF;
    scr[1][1] = kF;
    scr[2][0] = 2048.0f;
    scr[2][1] = 2048.0f;
    scr[2][3] = 1.0f;
    scr[3][2] = kB;
}

static void buildModel(void)
{
    Sub15C *o = zalloc(sizeof(Sub15C));
    PObjModel *m = zalloc(sizeof(PObjModel));
    PObjModel *x = zalloc(sizeof(PObjModel));
    PObjPart *p = zalloc(sizeof(PObjPart));
    float (*node)[4] = zalloc(64);
    for (int i = 0; i < 4; i++) {
        node[i][i] = 1.0f;
    }
    float (*vtx)[4] = zalloc(3 * 16);
    for (int i = 0; i < 3; i++) {
        vtx[i][0] = kCaster[i][0];
        vtx[i][1] = kCaster[i][1];
        vtx[i][2] = kCaster[i][2];
        vtx[i][3] = 1.0f;
    }
    TestRun *run = zalloc(5 * sizeof(TestRun));
    run[0].count = 3;
    for (int i = 0; i < 3; i++) {
        run[1 + i].count = 1; /* the facing flag of the first gives the sign +1 */
        run[1 + i].vtx = (short)i;
    }
    TestRun **strips = zalloc(sizeof(TestRun *));
    strips[0] = run;
    p->vtx = (char *)vtx;
    p->vtxCount = 3;
    p->vtxSave = zalloc(3 * 16);
    p->nrmSave = zalloc(3 * 16);
    p->strips = strips;
    p->stripCount = 1;
    x->partCount = 1;
    x->parts = p;
    /* the part's box (eight vectors), which the 2001 builds' and the North
       American disc's on-screen test reads (gsb_ClipBox is 1 here) */
    x->boxes = zalloc(8 * 16);
    m->shadowLength = kLen;
    o->nodeNum = 1;
    o->nodeMtx = (ICO_WORD)node;
    o->model = m;
    o->shadow = x;
    o->dispType = 0;
    o->shadowDir[0] = kDir[0];
    o->shadowDir[1] = kDir[1];
    o->shadowDir[2] = kDir[2];
    s_obj = o;
}

/* the receiver pixel the caster's centroid projects to along the shadow
   direction */
static void modelShadowPixel(int *px, int *py)
{
    float c[3] = {0};
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) {
            c[k] += kCaster[i][k] / 3.0f;
        }
    }
    const float t = (kZPlane - c[2]) / kDir[2];
    const float q[3] = {c[0] + kDir[0] * t, c[1] + kDir[1] * t, kZPlane};
    *px = (int)floorf(kF * q[0] / q[2] + (float)(W / 2));
    *py = (int)floorf(kF * q[1] / q[2] + (float)(H / 2));
}

/* ------------------------------------------------------------- the frame */
enum { VOL_SYNTH = 0, VOL_MODEL = 1, VOL_SUB = 2 };

/* VOL_SUB: one rectangle, 64.25 pixels wide, shifted by s_subOff sixteenths */
static int s_subOff;

static char *s_volPacket, *s_volEnd; /* Shadow.c's packet for the model's volume */

static RdTarget s_count;

static void recordFrame(int vol, const int blend[4])
{
    GlobalStageSetting.shadowDepth = kDepth;
    GlobalStageSetting.shadowColR = kShadowCol[0];
    GlobalStageSetting.shadowColG = kShadowCol[1];
    GlobalStageSetting.shadowColB = kShadowCol[2];
    for (int i = 0; i < 4; i++) {
        GlobalStageSetting.shadowBlend[i] = blend[i];
    }
    if (vol == VOL_MODEL) {
        /* the receiver at the model's plane */
        dl_SetDLPriority(0);
        rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H,
                      RD_TARGET_OFFSET);
        rd_clear_target(rd_target(RD_TARGET_SCENE), kClear, 1, 0);
        rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture_off();
        rd_abe(0);
        rd_test_gs(0x30000);
        rd_z_write(1);
        rd_fba(0);
        sprite(0, 0, W, H, (uint32_t)(16.0f * kB / kZPlane), kRecv);
    } else {
        drawReceiver();
    }
    shadow_Reset();
    s_count = rd_shadow_count_target((uint32_t)gsFrame.w, H);
    if (vol == VOL_SYNTH) {
        buildVolumes();
        rd_shadow_tris(s_tri, s_sign, s_nt, 1);
    } else if (vol == VOL_SUB) {
        const int32_t x0 = (2048 - W / 2 + 100) * 16 + s_subOff, x1 = x0 + 64 * 16 + 4;
        const int32_t y0 = (2048 - H / 2 + 100) * 16 + s_subOff, y1 = y0 + 40 * 16 + 4;
        RdScreenVtx q[6];
        const int32_t xs[6] = {x0, x1, x0, x0, x1, x1}, ys[6] = {y0, y0, y1, y1, y0, y1};
        for (int i = 0; i < 6; i++) {
            q[i] = sv(0, 0, kZf, NULL);
            q[i].x = xs[i];
            q[i].y = ys[i];
        }
        static const int8_t plus[2] = {1, 1};
        rd_shadow_tris(q, plus, 2, 1);
    } else {
        s_volPacket = PacketBufferStruct.ptr.c;
        shadow_RenderVolume(s_obj);
        s_volEnd = PacketBufferStruct.ptr.c;
    }
    shadow_Draw();
    dl_Swap();
}

/* --------------------------------------------- (e) the recording checks */

static const RdCmd *nextCmd(const RdCmdList *cl, uint32_t *i, uint8_t type, const char *what)
{
    while (*i < cl->count && cl->cmds[*i].type == RDC_NOP) {
        (*i)++;
    }
    if (*i >= cl->count) {
        CHECK(0, "list 3 ends before %s", what);
        return NULL;
    }
    const RdCmd *c = &cl->cmds[(*i)++];
    CHECK(c->type == type, "%s: command %u has type %u, expected %u", what, *i - 1, c->type, type);
    return c->type == type ? c : NULL;
}

static void expectTest(const RdCmdList *cl, uint32_t *i, uint64_t gs, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, RDC_TEST, what);
    const RdTestState t = rd_test_from_gs(gs);
    CHECK(c && c->b[0] == t.ate && c->b[1] == t.atst && c->b[2] == t.aref && c->b[3] == t.afail &&
              c->b[4] == t.date && c->b[5] == t.zte && c->b[6] == t.ztst,
          "%s: TEST 0x%llx", what, (unsigned long long)gs);
}

static void expect1(const RdCmdList *cl, uint32_t *i, uint8_t type, int v, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, type, what);
    CHECK(c && c->b[0] == (uint8_t)v, "%s: %d, got %d", what, v, c ? c->b[0] : -1);
}

static void expectTarget(const RdCmdList *cl, uint32_t *i, uint32_t color, uint32_t depth,
                         uint32_t w, uint32_t h, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, RDC_TARGET, what);
    CHECK(c && c->u[0] == color && c->u[1] == depth && c->u[2] == (w | (h << 16)),
          "%s: target %u/%u %ux%u, got %u/%u 0x%x", what, color, depth, w, h, c ? c->u[0] : 0,
          c ? c->u[1] : 0, c ? c->u[2] : 0);
}

static void expectTexture(const RdCmdList *cl, uint32_t *i, RdTarget t, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, RDC_TEXTURE, what);
    const RdTexRec *tr = c ? rd__tex_rec(c->u[0]) : NULL;
    CHECK(tr && tr->kind == RD_TEXKIND_TARGET && tr->target == t.id && tr->view == RD_VIEW_RGBA &&
              c->b[0] == RD_TEXFN_MODULATE && c->b[1] == RD_TCC_RGBA,
          "%s: the RGBA view of target %u, MODULATE, TCC RGBA", what, t.id);
}

static void expectSprite(const RdFrame *f, const RdCmdList *cl, uint32_t *i, int abe,
                         const int r[4], const int uv[4], const uint8_t col[4], const char *what)
{
    expect1(cl, i, RDC_ABE, abe, what);
    expect1(cl, i, RDC_SHADE, 0, what);
    const RdCmd *c = nextCmd(cl, i, RDC_SCREEN, what);
    if (!c) {
        return;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    CHECK(c->b[0] == RD_PRIM_SPRITES && c->b[2] == 1 && c->u[1] == 2, "%s: one FST sprite", what);
    CHECK(v[0].x == r[0] + 0x8000 && v[0].y == r[1] + 0x8000 && v[1].x == r[0] + r[2] + 0x8000 &&
              v[1].y == r[1] + r[3] + 0x8000 && v[0].z == 0xFFFFFFFFu,
          "%s: corners (%d,%d)-(%d,%d)", what, v[0].x, v[0].y, v[1].x, v[1].y);
    CHECK(v[0].s == (float)uv[0] && v[0].t == (float)uv[1] && v[1].s == (float)(uv[0] + uv[2]) &&
              v[1].t == (float)(uv[1] + uv[3]),
          "%s: UVs", what);
    CHECK(memcmp(v[1].rgba, col, 4) == 0, "%s: RGBAQ %u %u %u %u", what, v[1].rgba[0], v[1].rgba[1],
          v[1].rgba[2], v[1].rgba[3]);
}

/* the strips Shadow.c wrote into its packet: 10 (RGBAQ, XYZ2) positions
   after each strip's two GIF tags and PRIM */
static uint32_t packetTris(const char *pkt, RdScreenVtx *tri, int8_t *sign, uint32_t max)
{
    const unsigned long long *p = (const unsigned long long *)(pkt + 0x10);
    const unsigned long long *end = (const unsigned long long *)s_volEnd;
    uint32_t n = 0;
    while (p + 26 <= end && p[0] == 0x1000000000008001ull && p[1] == 0xE && p[2] == 0x144 &&
           p[4] == 0x240000000000800Aull && p[5] == 0x51) {
        RdScreenVtx pos[10];
        uint8_t r[10];
        for (int i = 0; i < 10; i++) {
            const unsigned long long rgbaq = p[6 + 2 * i], xyz = p[7 + 2 * i];
            memset(&pos[i], 0, sizeof(pos[i]));
            pos[i].x = (int32_t)(xyz & 0xFFFF);
            pos[i].y = (int32_t)((xyz >> 16) & 0xFFFF);
            pos[i].z = (uint32_t)(xyz >> 32);
            r[i] = (uint8_t)rgbaq;
        }
        for (int i = 2; i < 10 && n < max; i++, n++) {
            tri[n * 3 + 0] = pos[i - 2];
            tri[n * 3 + 1] = pos[i - 1];
            tri[n * 3 + 2] = pos[i];
            sign[n] = r[i] == (uint8_t)s_step ? 1 : r[i] == (uint8_t)(256 - s_step) ? -1 : 0;
        }
        p += 26;
    }
    return n;
}

static int sameXyz(const RdScreenVtx *a, const RdScreenVtx *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

static void checkRecording(void)
{
    const RdFrame *f = rd__last_frame();
    if (!f) {
        CHECK(0, "a closed frame");
        return;
    }
    const RdCmdList *cl = &f->lists[3];
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *ct = rd__target_rec(s_count.id);
    const int fw = gsFrame.w; /* the native frame's width: W at 4:3 */
    CHECK(ct && ct->w == (uint32_t)fw && ct->h == H && !ct->withDepth && !ct->named,
          "the count target: a %ux%u temporary target without depth", fw, H);
    for (int l = 0; l < 3; l++) {
        const RdTargetRec *lt = rd__target_rec(rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l)).id);
        CHECK(lt && lt->w == (uint32_t)(fw >> (l + 1)) && lt->h == (uint32_t)(256 >> l),
              "W %d: SHADOW%d is %ux%u (want %dx%d)", fw, l, lt ? lt->w : 0, lt ? lt->h : 0,
              fw >> (l + 1), 256 >> l);
    }
    uint32_t i = 0;
    /* shadow_Reset */
    expectTarget(cl, &i, s_count.id, scene, (uint32_t)fw, H, "reset: FRAME 0x142, ZBUF 0xC0");
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_OFF, "reset: ZBUF ZMSK");
    expectTest(cl, &i, 0x30000, "reset: TEST");
    expect1(cl, &i, RDC_ABE, 0, "reset: PRIM 0x406 ABE");
    expect1(cl, &i, RDC_SHADE, 0, "reset: PRIM 0x406 IIP");
    nextCmd(cl, &i, RDC_TEXTURE_OFF, "reset: PRIM 0x406 TME");
    const RdCmd *rc = nextCmd(cl, &i, RDC_SHADOW_RESET, "reset: the clear of 0x142");
    CHECK(rc && rc->b[0] == (s_step == 8 ? 8 : 0) && rd__shadow_step(rc) == (uint32_t)s_step,
          "reset: carries the step %d (b[0] %u)", s_step, rc ? rc->b[0] : 0);
    expect1(cl, &i, RDC_FBA, 0, "reset: FBA");
    expect1(cl, &i, RDC_TEXA, RD_TEXA_80_80, "reset: TEXA");
    expectTest(cl, &i, 0x50000, "reset: TEST");
    const RdCmd *c = nextCmd(cl, &i, RDC_ALPHA, "reset: ALPHA 0x68 FIX 0x80");
    CHECK(c && c->b[0] == RD_BLEND_CS_FIX_ADD_CD && c->b[1] == 0x80, "reset: ALPHA 0x68 FIX 0x80");
    expect1(cl, &i, RDC_COLCLAMP, 0, "reset: COLCLAMP 0");
    /* shadow_RenderVolume */
    expect1(cl, &i, RDC_ABE, 1, "volume: PRIM 0x144 ABE");
    expect1(cl, &i, RDC_SHADE, 0, "volume: PRIM 0x144 IIP");
    nextCmd(cl, &i, RDC_TEXTURE_OFF, "volume: PRIM 0x144 TME");
    c = nextCmd(cl, &i, RDC_SHADOW_STRIP, "volume: the triangles");
    static RdScreenVtx ptri[64 * 3];
    static int8_t psign[64];
    const uint32_t pn = packetTris(s_volPacket, ptri, psign, 64);
    CHECK(pn == 8, "the packet holds one strip (8 triangles), got %u", pn);
    if (c) {
        CHECK(c->b[0] == RD_SHADOW_TRIS && c->u[0] + c->u[3] == pn * 3,
              "volume: %u triangles recorded (%u increment, %u decrement)", (c->u[0] + c->u[3]) / 3,
              c->u[0] / 3, c->u[3] / 3);
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[1]);
        uint32_t a = 0, b = c->u[0], bad = 0, inc = 0;
        for (uint32_t t = 0; t < pn; t++) {
            CHECK(psign[t] != 0, "packet triangle %u: RGBAQ neither 0x%02X nor 0x%02X", t, s_step,
                  256 - s_step);
            uint32_t *at = psign[t] > 0 ? &a : &b;
            inc += psign[t] > 0;
            for (int k = 0; k < 3; k++) {
                bad += *at + k < c->u[0] + c->u[3] ? !sameXyz(&v[*at + k], &ptri[t * 3 + k]) ||
                                                         rd__shadow_tag(&v[*at + k]) != t + 1
                                                   : 1;
            }
            *at += 3;
        }
        CHECK(bad == 0 && inc * 3 == c->u[0],
              "volume: the triangles and signs are the packet's kicks, each vertex tagged with "
              "its triangle's place in the packet (V3) (%u mismatched vertices)",
              bad);
        CHECK(inc > 0 && inc < pn, "volume: both signs recorded (%u of %u count +1, step %d)", inc,
              pn, s_step);
        printf("  volume strip: %u triangles count +1 (RGBAQ 0x%02X), %u count -1 (0x%02X)\n", inc,
               s_step, pn - inc, 256 - s_step);
    }
    /* shadow_Draw */
    expectTarget(cl, &i, s_count.id, scene, (uint32_t)fw, H, "draw: the count before the resolve");
    rc = nextCmd(cl, &i, RDC_SHADOW_RESOLVE, "draw: the resolve");
    CHECK(rc && rc->b[0] == (s_step == 8 ? 8 : 0) && rd__shadow_step(rc) == (uint32_t)s_step,
          "resolve: carries the step %d (b[0] %u)", s_step, rc ? rc->b[0] : 0);
    expectTest(cl, &i, 0x30000, "draw: TEST");
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_OFF, "draw: ZBUF ZMSK");
    expect1(cl, &i, RDC_COLCLAMP, 1, "draw: COLCLAMP 1");
    expect1(cl, &i, RDC_FBA, 0, "draw: FBA");
    expect1(cl, &i, RDC_TEXA, RD_TEXA_80_80_AEM, "draw: TEXA 0x80 AEM");
    c = nextCmd(cl, &i, RDC_FILTER, "draw: TEX1 0x60");
    CHECK(c && c->b[0] == RD_FILTER_LINEAR && c->b[1] == RD_FILTER_LINEAR, "draw: TEX1 linear");
    const uint8_t col[4] = {128, 128, 128, kDepth};
    for (int l = 0; l < 3; l++) {
        const int s = 256 >> l, sw = fw >> (l + 1); /* level l + 1: sw x s */
        const int r[4] = {-(sw * 8 + 4), -(s * 8 + 4), sw * 16, s * 16};
        const int uv[4] = {4, 4, 2 * sw * 16, 2 * s * 16};
        expectTarget(cl, &i, rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l)).id, 0, (uint32_t)sw,
                     (uint32_t)s, "chain: FRAME");
        expectTexture(cl, &i, l == 0 ? s_count : rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l - 1)),
                      "chain: TEX0");
        expectSprite(f, cl, &i, 0, r, uv, col, "chain: sprite");
    }
    expectTarget(cl, &i, scene, scene, (uint32_t)fw, H, "composite: FRAME 0x40");
    c = nextCmd(cl, &i, RDC_ALPHA, "composite: ALPHA 0x44");
    CHECK(c && c->b[0] == RD_BLEND_LERP_AS && c->b[1] == 0, "composite: ALPHA 0x44");
    expectTest(cl, &i, 0x3400D, "composite: TEST");
    for (int l = 3; l > 0; l--) {
        const int off[4] = {-(fw / 2) * 16, -(H / 2) * 16, fw * 16, H * 16};
        const int uv[4] = {4, 4, (fw * 16) >> l, (H * 16) >> l};
        const uint8_t col2[4] = {(uint8_t)kShadowCol[0], (uint8_t)kShadowCol[1],
                                 (uint8_t)kShadowCol[2],
                                 (uint8_t)GlobalStageSetting.shadowBlend[l]};
        expectTexture(cl, &i, rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l - 1)),
                      "composite: TEX0");
        c = nextCmd(cl, &i, RDC_FILTER, "composite: TEX1");
        expectSprite(f, cl, &i, 1, off, uv, col2, "composite: sprite");
    }
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_ON, "end: ZBUF");
    expectTest(cl, &i, 0x50000, "end: TEST");
    expectTarget(cl, &i, scene, scene, (uint32_t)fw, H, "end: FRAME 0x40");
    CHECK(i == cl->count, "list 3 ends after shadow_Draw (%u of %u commands)", i, cl->count);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

/* -------------------------------------------------- the device references */

static uint8_t s_cnt[W * H * 4], s_lv[3][256 * 256 * 4], s_scene[W * H * 4];

static bool readTarget(RdTarget t, uint8_t *dst, uint32_t ew, uint32_t eh)
{
    uint32_t w = 0, h = 0;
    bool ok = rd__read_target(t, dst, (size_t)ew * eh * 4, &w, &h) && w == ew && h == eh;
    CHECK(ok, "readback of target %u (%ux%u)", t.id, ew, eh);
    return ok;
}

/* GS bilinear at texel coordinates (u, v) (texel centres at i + 0.5),
   CLAMP, of an RGBA8 image; the float result per channel */
static void bilinear(const uint8_t *img, int w, int h, float u, float v, float out[4])
{
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float ax = fx - (float)x0, ay = fy - (float)y0;
    for (int c = 0; c < 4; c++) {
        float s = 0.0f;
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                int x = x0 + i, y = y0 + j;
                x = x < 0 ? 0 : x >= w ? w - 1 : x;
                y = y < 0 ? 0 : y >= h ? h - 1 : y;
                s += (i ? ax : 1.0f - ax) * (j ? ay : 1.0f - ay) * img[(y * w + x) * 4 + c];
            }
        }
        out[c] = s;
    }
}

static int mod7(int t, int c)
{
    const int v = (t * c) >> 7;
    return v > 255 ? 255 : v;
}

static void checkCount(void)
{
    int bad = 0, firstX = -1, firstY = -1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int n = expectedCount(x, y);
            const uint8_t c = wrappedColourStep(n, s_step);
            const uint8_t *p = &s_cnt[(y * W + x) * 4];
            const uint8_t a = c ? 0x80 : 0;
            if (p[0] != c || p[1] != c || p[2] != c || p[3] != a) {
                if (!bad++) {
                    firstX = x, firstY = y;
                }
            }
        }
    }
    CHECK(bad == 0,
          "(a) count: %d pixels differ from %d n mod 256, first (%d,%d) n %d: %u %u %u %u", bad,
          s_step, firstX, firstY, firstX >= 0 ? expectedCount(firstX, firstY) : 0,
          firstX >= 0 ? s_cnt[(firstY * W + firstX) * 4] : 0,
          firstX >= 0 ? s_cnt[(firstY * W + firstX) * 4 + 1] : 0,
          firstX >= 0 ? s_cnt[(firstY * W + firstX) * 4 + 2] : 0,
          firstX >= 0 ? s_cnt[(firstY * W + firstX) * 4 + 3] : 0);

    /* the cases the brief names, spelled out */
    static const struct {
        int x, y, n;
        uint8_t c;
    } k[] = {{8, 0, 2, 8},      {16, 100, -4, 0xF0}, {32, 150, 2, 8},    {256, 0, 64, 0},
             {100, 150, 64, 0}, {260, 0, 65, 4},     {200, 150, 128, 0}, {300, 150, 256, 0},
             {400, 150, 3, 12}, {4, 70, -1, 0xFC},   {508, 70, -127, 4}, {8, 300, 0, 0}};

    /* the step 8's: 8 n mod 256 = 8 (n mod 32), so 32 and 64 are no shadow */
    static const struct {
        int x, y, n;
        uint8_t c;
    } k8[] = {{4, 0, 1, 8},      {124, 0, 31, 248}, {128, 0, 32, 0},  {132, 0, 33, 8},
              {256, 0, 64, 0},   {4, 70, -1, 0xF8}, {32, 150, 2, 16}, {400, 150, 3, 24},
              {100, 150, 64, 0}, {8, 300, 0, 0}};

    for (size_t i = 0; s_step == 4 && i < sizeof(k) / sizeof(k[0]); i++) {
        const uint8_t *p = &s_cnt[(k[i].y * W + k[i].x) * 4];
        CHECK(expectedCount(k[i].x, k[i].y) == k[i].n && wrappedColour(k[i].n) == k[i].c &&
                  p[0] == k[i].c,
              "(a) n = %d at (%d,%d): colour %u, expected %u", k[i].n, k[i].x, k[i].y, p[0],
              k[i].c);
    }
    for (size_t i = 0; s_step == 8 && i < sizeof(k8) / sizeof(k8[0]); i++) {
        const uint8_t *p = &s_cnt[(k8[i].y * W + k8[i].x) * 4];
        CHECK(expectedCount(k8[i].x, k8[i].y) == k8[i].n &&
                  wrappedColourStep(k8[i].n, 8) == k8[i].c && p[0] == k8[i].c &&
                  p[3] == (k8[i].c ? 0x80 : 0),
              "(8) n = %d at (%d,%d): colour %u alpha %u, expected %u", k8[i].n, k8[i].x, k8[i].y,
              p[0], p[3], k8[i].c);
    }
    printf("  (a) count, step %d: %d of %d pixels off the wrapped colour sum\n", s_step, bad,
           W * H);
}

/* level l + 1 (size s) from level l (s_cnt for l = 0, else s_lv[l - 1]) */
static void checkChain(void)
{
    for (int l = 0; l < 3; l++) {
        const int s = 256 >> l, sw = 2 * s;
        const uint8_t *src = l == 0 ? s_cnt : s_lv[l - 1];
        int worst = 0, wx = 0, wy = 0, nonzero = 0;
        for (int y = 0; y < s; y++) {
            for (int x = 0; x < s; x++) {
                float f[4];
                bilinear(src, sw, sw, 2.0f * (float)x + 0.75f, 2.0f * (float)y + 0.75f, f);
                const uint8_t *g = &s_lv[l][(y * s + x) * 4];
                for (int c = 0; c < 4; c++) {
                    const int t = (int)floorf(f[c] + 0.5f);
                    const int e = c < 3 ? mod7(t, 128) : mod7(t, kDepth);
                    const int d = abs((int)g[c] - e);
                    if (d > worst) {
                        worst = d, wx = x, wy = y;
                    }
                }
                nonzero += g[3] != 0;
            }
        }
        CHECK(worst <= 1, "(b) level %d (%dx%d): max difference %d at (%d,%d)", l + 1, s, s, worst,
              wx, wy);
        CHECK(nonzero > 0, "(b) level %d holds shadow", l + 1);
        printf("  (b) level %d (%dx%d): max %d LSB against the GS bilinear reference\n", l + 1, s,
               s, worst);
    }
}

/* the composites of the levels whose blend is non-zero, in order 3, 2, 1,
   over the receiver as drawn: GS LERP with As, alpha test As > 0, DATE
   against the alpha MSB (which each composite rewrites with As) */
/* the texel the GPU's floor(x * 255 + 0.5) gives for a filtered value f; at
   an exact .5 the float filter may land on either side (down picks which) */
static int texel(float f, int down)
{
    return (down && f - floorf(f) == 0.5f) ? (int)floorf(f) : (int)floorf(f + 0.5f);
}

static int checkComposite(const int blend[4], int tol, int tolFloat, const char *what)
{
    int worst = 0, wx = 0, wy = 0, changed = 0, maskedBad = 0, worstF = 0, overF = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t *g = &s_scene[(y * W + x) * 4];
            int best[3] = {999, 999, 999}, bestF[3] = {999, 999, 999};
            /* variant bit 0: alpha ties round down, bit 1: colour ties */
            for (int var = 0; var < 4; var++) {
                int cd[4] = {kRecv[0], kRecv[1], kRecv[2],
                             x < W / 2 ? kRecv[3] : (kRecv[3] | 0x80)};
                int cf[3] = {kRecv[0], kRecv[1], kRecv[2]}; /* the same with a float blender */
                for (int l = 3; l > 0; l--) {
                    if (blend[l] == 0) {
                        continue;
                    }
                    const int s = 512 >> l;
                    float f[4];
                    bilinear(s_lv[l - 1], s, s, 0.25f + (float)x / (float)(1 << l),
                             0.25f + (float)y / (float)(1 << l), f);
                    const int as = mod7(texel(f[3], var & 1), blend[l]);
                    if (as == 0 || (cd[3] & 0x80)) {
                        continue;
                    }
                    for (int c = 0; c < 3; c++) {
                        const int cs = mod7(texel(f[c], var & 2), kShadowCol[c]);
                        cd[c] = (((cs - cd[c]) * as) >> 7) + cd[c];
                        cf[c] = (int)floorf(
                            ((float)cs * (float)as + (float)cf[c] * (float)(128 - as)) / 128.0f +
                            0.5f);
                    }
                    cd[3] = as;
                }
                for (int c = 0; c < 3; c++) {
                    const int d = abs((int)g[c] - cd[c]), df = abs((int)g[c] - cf[c]);
                    best[c] = d < best[c] ? d : best[c];
                    bestF[c] = df < bestF[c] ? df : bestF[c];
                }
            }
            for (int c = 0; c < 3; c++) {
                if (best[c] > worst) {
                    worst = best[c], wx = x, wy = y;
                }
                worstF = bestF[c] > worstF ? bestF[c] : worstF;
                overF += bestF[c] > 1;
            }
            if (x >= W / 2 && memcmp(g, kRecv, 3) != 0) {
                maskedBad++;
            }
            changed += x < W / 2 && memcmp(g, kRecv, 3) != 0;
        }
    }
    CHECK(worst <= tol, "(c) %s: max difference %d at (%d,%d) (tolerance %d)", what, worst, wx, wy,
          tol);
    CHECK(worstF <= tolFloat && overF <= W * H * 3 / 1000,
          "(c) %s: max difference %d from the float LERP (tolerance %d), %d channels above 1", what,
          worstF, tolFloat, overF);
    CHECK(maskedBad == 0, "(d) %s: %d pixels of the FBA half changed", what, maskedBad);
    CHECK(changed > 0, "(d) %s: the receiver half is shadowed", what);
    printf("  (c) %s: max %d LSB from the GS LERP, %d from the float LERP of the same operands "
           "(%d channels above 1); (d) %d receiver pixels shadowed, FBA half untouched\n",
           what, worst, worstF, overF, changed);
    return worst;
}

static void checkModel(void)
{
    int px, py;
    modelShadowPixel(&px, &py);
    const uint8_t *p = &s_cnt[(py * W + px) * 4];
    CHECK(p[0] == 4 || p[0] == 0xFC, "(e) the caster's shadow at (%d,%d): count colour %u (+-1)",
          px, py, p[0]);
    int far = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (abs(x - px) > 100 || abs(y - py) > 100) {
                far += s_cnt[(y * W + x) * 4] != 0;
            }
        }
    }
    CHECK(far == 0, "(e) %d counted pixels far from the shadow", far);
    int area = 0;
    for (int i = 0; i < W * H; i++) {
        area += s_cnt[i * 4] != 0;
    }
    printf("  (e) Shadow.c volume: count colour %u at (%d,%d) (%s), %d pixels counted\n", p[0], px,
           py, p[0] == 4 ? "+1" : "-1", area);
}

static void checkPipelines(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u", n);
    for (uint32_t i = 0; i < rd__pipeline_count(); i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        int found = 0;
        /* the whole set: with the triangle passes' plane keys the shadow
           family sits past the first 512 */
        for (uint32_t j = 0; j < n && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            found |= rd__pipe_key_equal(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u fs %u stencil %u) is not enumerated", i,
              k->gs.program, k->vs, k->fs, k->gs.stencil);
    }
    printf("  pipelines: %u created, %u reachable\n", rd__pipeline_count(), n);
}

/* At a scene scale of 2 (Enhanced, 896 lines and up) the blur levels
 * keep the PS2 sizes, whose texels are the shadow's blur, and so does
 * WORK0 (every work buffer: rd_core.c rd__target_scale_of); SCENE scales */
static void checkLevelScale(void)
{
    const float keepX = g_rd.sceneSx, keepY = g_rd.sceneSy;
    g_rd.sceneSx = g_rd.sceneSy = 2.0f;

    static const struct {
        int id;
        uint32_t w;
    } k[4] = {{RD_TARGET_SHADOW0, 256},
              {RD_TARGET_SHADOW1, 128},
              {RD_TARGET_SHADOW2, 64},
              {RD_TARGET_WORK0, 256}};

    for (int i = 0; i < 4; i++) {
        RdTargetRec t;
        memset(&t, 0, sizeof(t));
        t.w = t.h = k[i].w;
        rd__target_scale_of(&t, k[i].id);
        CHECK(t.tw == k[i].w && t.sx == 1.0f, "scene scale 2: target %d is %u texels wide (%u)",
              k[i].id, t.tw, k[i].w);
    }
    RdTargetRec sc;
    memset(&sc, 0, sizeof(sc));
    sc.w = 512;
    sc.h = 448;
    rd__target_scale_of(&sc, RD_TARGET_SCENE);
    CHECK(sc.tw == 1024 && sc.sx == 2.0f, "scene scale 2: SCENE is %u texels wide (1024)", sc.tw);
    g_rd.sceneSx = keepX;
    g_rd.sceneSy = keepY;
}

/* ----------------------------------------------- (s) above 1x at 4x
 *
 * The composites at scene scale 4 (Enhanced): each level (scale 1, the
 * PS2's size) is magnified into SCENE by 8, 16 or 32 texels a level
 * texel, which the replay draws by default with one bilinear tap of the
 * sampler at the texel's UV, as at 1x (not flagged DF_ABOVE1X:
 * rd_fx_plan.c rd__sprite_above1x): SCENE against the float bilinear of
 * the level as read back, then MODULATE and the LERP, within 3 LSB.  With
 * RdProfile.shadowBspline ([dev] shadow_bspline) the cubic B-spline
 * (common.hlsli DF_ABOVE1X): SCENE against above1x_ref.c's B-spline of the
 * level, at each texel's GS sample point, within 3 LSB (the B-spline is
 * four bilinear taps of the sampler there); a row across the shadow falls
 * and rises once (no bump the bilinear's corners would leave across a
 * diagonal, no overshoot).  Then the
 * softening's passes (rd_post.c AA_DOWNSAMPLE and AA_COMPOSITE: SCENE at 4
 * into AA0 at its GS size, a step of 8 texels, the area box, exact; AA0
 * back over SCENE, an eighth of a texel a texel, read as at 1x: the
 * sampler's bilinear between the same texels at each texel's GS sample
 * point, rd_internal.h rd__work_class, within 2 LSB), the composite not
 * counted above 1x. */
#define S4 4
#define S4W (W * S4)
#define S4H (H * S4)

static uint8_t s_big[S4W * S4H * 4], s_pic[S4W * S4H * 4];

typedef struct A1xImg {
    const uint8_t *px;
    int w, h;
} A1xImg;

/* CLAMP, as the level and SCENE are sampled */
static void a1xFetch(void *user, int x, int y, uint32_t out[4])
{
    const A1xImg *m = (const A1xImg *)user;
    x = x < 0 ? 0 : (x >= m->w ? m->w - 1 : x);
    y = y < 0 ? 0 : (y >= m->h ? m->h - 1 : y);
    for (int c = 0; c < 4; c++) {
        out[c] = m->px[((size_t)y * m->w + x) * 4 + c];
    }
}

/* the sample point of texel p of a target at scale s, in GS pixels: the
 * replay moves a scaled sprite's UVs so that the texel's centre samples
 * the GS's coordinate (rd_replay.c setUvShift) */
static double gsAt(int p, int s)
{
    return ((double)p + 0.5) / (double)s - 0.5;
}

/* the level-l composite's texel at SCENE texel (x, y) (the sprite's U is
 * 0.25 + X / 2^l at GS X, as checkComposite reads it): the B-spline
 * (a1x) or the GS's bilinear */
static void compositeTexel(const A1xImg *lv, int l, int x, int y, int a1x, int tie, uint32_t t[4])
{
    /* at an exact half sixteenth the shader's float coordinate rounds
     * either way: tie bit 0 (x) and 1 (y) take the lower */
    const double ux = 16.0 * (0.25 + gsAt(x, S4) / (double)(1 << l));
    const double uy = 16.0 * (0.25 + gsAt(y, S4) / (double)(1 << l));
    const int c16x = (int)floor(ux + 0.5) - ((tie & 1) && ux - floor(ux) == 0.5);
    const int c16y = (int)floor(uy + 0.5) - ((tie & 2) && uy - floor(uy) == 0.5);
    const uint32_t step = a1x ? (uint32_t)(32 / (S4 << l)) : 32u;
    a1x_resample(a1xFetch, (void *)lv, c16x, c16y, step, step, 1, t);
}

/* the level-l composite's texel at SCENE texel (x, y) as sprite_ps samples
 * it without DF_ABOVE1X (the default, as at 1x): the sampler's bilinear at
 * the texel's UV, the float filter's value converted with ties either way
 * (tie bit 0 the alpha, bit 1 the colour, as checkComposite) */
static void compositeTexelBilinear(const A1xImg *lv, int l, int x, int y, int tie, uint32_t t[4])
{
    float f[4];
    bilinear(lv->px, lv->w, lv->h, (float)(0.25 + gsAt(x, S4) / (double)(1 << l)),
             (float)(0.25 + gsAt(y, S4) / (double)(1 << l)), f);
    for (int c = 0; c < 4; c++) {
        t[c] = (uint32_t)texel(f[c], c == 3 ? (tie & 1) : (tie & 2));
    }
}

/* level l's composite at scale 4: bspline the B-spline ([dev]
 * shadow_bspline), else the default's one bilinear tap */
static void checkComposite4x(int l, bool bspline)
{
    int one[4] = {0, 0, 0, 0};
    one[l] = kBlend[l];
    s_subOff = 0;
    g_rd.profile.shadowBspline = bspline;
    const uint32_t before = g_rd.above1xDraws;
    recordFrame(VOL_SUB, one);
    g_rd.profile.shadowBspline = false;
    const char *kern = bspline ? "B-spline" : "bilinear";
    const int n = 256 >> (l - 1);
    if (!readTarget(rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l - 1)), s_lv[l - 1], (uint32_t)n,
                    (uint32_t)n) ||
        !readTarget(rd_target(RD_TARGET_SCENE), s_big, S4W, S4H)) {
        return;
    }
    CHECK(bspline ? g_rd.above1xDraws > before : g_rd.above1xDraws == before,
          "(s) level %d (%s): %u passes drawn above 1x (%s)", l, kern, g_rd.above1xDraws - before,
          bspline ? "the composite flagged" : "none: sampled as at 1x");
    const A1xImg lv = {s_lv[l - 1], n, n};
    int worst = 0, wx = 0, wy = 0, shadowed = 0, worstTent = 0;
    for (int y = 0; y < S4H; y++) {
        for (int x = 0; x < S4W; x++) {
            const uint8_t *g = &s_big[((size_t)y * S4W + x) * 4];
            int best = 999, bestTent = 999;
            for (int k = 0; k < 16; k++) {
                /* k bit 0: the other reference (with the B-spline the GS's
                 * bilinear, with the default the B-spline); bits 1..2: the
                 * ties; bit 3: a float blender's LERP (else the GS's) */
                int o[3] = {kRecv[0], kRecv[1], kRecv[2]};
                uint32_t t[4];
                if (bspline || (k & 1)) {
                    compositeTexel(&lv, l, x, y, bspline ? !(k & 1) : 1, (k >> 1) & 3, t);
                } else {
                    compositeTexelBilinear(&lv, l, x, y, (k >> 1) & 3, t);
                }
                const int as = mod7((int)t[3], one[l]);
                if (as != 0 && x < S4W / 2) {
                    for (int c = 0; c < 3; c++) {
                        const int cs = mod7((int)t[c], kShadowCol[c]);
                        o[c] = (k & 8) ? (int)floorf(((float)cs * (float)as +
                                                      (float)o[c] * (float)(128 - as)) /
                                                         128.0f +
                                                     0.5f)
                                       : (((cs - o[c]) * as) >> 7) + o[c];
                    }
                }
                int d = 0;
                for (int c = 0; c < 3; c++) {
                    const int e = abs((int)g[c] - o[c]);
                    d = e > d ? e : d;
                }
                if (k & 1) {
                    bestTent = d < bestTent ? d : bestTent;
                } else {
                    best = d < best ? d : best;
                }
            }
            if (best > worst) {
                worst = best, wx = x, wy = y;
            }
            worstTent = bestTent > worstTent ? bestTent : worstTent;
            shadowed += memcmp(g, kRecv, 3) != 0;
        }
    }
    /* a row through the shadow (GS y 120): one fall, one rise */
    int turns = 0;
    for (int c = 0; c < 3; c++) {
        const uint8_t *row = &s_big[(size_t)(120 * S4 + 1) * S4W * 4];
        int dir = 0;
        for (int x = 1; x < S4W / 2; x++) {
            const int d = (int)row[x * 4 + c] - (int)row[(x - 1) * 4 + c];
            const int sg = d > 0 ? 1 : (d < 0 ? -1 : 0);
            if (sg != 0 && dir != 0 && sg != dir) {
                turns++;
            }
            dir = sg != 0 ? sg : dir;
        }
    }
    printf("  (s) 4x level %d (%s): %d texels shadowed, max %d LSB from the %s reference (at "
           "%d,%d), %d from the %s's; the row at GS y 120 turns %d times in 3 channels\n",
           l, kern, shadowed, worst, bspline ? "B-spline" : "sampler's bilinear", wx, wy, worstTent,
           bspline ? "GS bilinear" : "B-spline", turns);
    CHECK(shadowed > 0, "(s) level %d: the receiver is shadowed at 4x", l);
    if (!bspline) {
        /* the sampler's own fraction precision and the blender's rounding,
           as the softening's composite back over SCENE (2) plus the
           MODULATE before it; lavapipe on arm64 lands 4 away from the
           reference where x86 lands 2 (the v0.9.4 tag's CI run) */
        CHECK(worst <= 4, "(s) level %d at 4x: max difference %d from the bilinear at (%d,%d)", l,
              worst, wx, wy);
        return;
    }
    /* the fixed-function blender rounds its LERP once more (checkComposite);
       the B-spline's four hardware bilinear taps carry the sampler's own
       fraction precision (lavapipe on arm64 lands 3 away from the
       reference where x86 lands 2) */
    const int tol = rd_blend_exact() != RD_BLEND_EXACT_OFF ? 3 : 4;
    CHECK(worst <= tol, "(s) level %d at 4x: max difference %d from the B-spline at (%d,%d)", l,
          worst, wx, wy);
    CHECK(turns <= 3, "(s) level %d at 4x: the row turns %d times (a fall and a rise a channel)", l,
          turns);
}

/* a picture with detail below the GS pixel: sprites on a 12.4 grid of
 * quarter pixels, hashed colours, over the receiver colour */
static void drawPicture(void)
{
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, RD_TARGET_OFFSET);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kRecv, 1, 0);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(0x30000);
    rd_z_write(1);
    rd_fba(0);
    for (int k = 0; k < 600; k++) {
        const uint32_t h = (uint32_t)k * 2654435761u;
        const uint8_t col[4] = {(uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
                                (uint8_t)(h & 0x7F)};
        RdScreenVtx v[2] = {sv(0, 0, kZr, col), sv(0, 0, kZr, col)};
        const int32_t x0 = (int32_t)((h >> 3) % (uint32_t)(W * 16 - 256)) & ~3;
        const int32_t y0 = (int32_t)((h >> 11) % (uint32_t)(H * 16 - 256)) & ~3;
        v[0].x = (2048 - W / 2) * 16 + x0;
        v[0].y = (2048 - H / 2) * 16 + y0;
        v[1].x = v[0].x + 4 + (int32_t)((h >> 5) % 120u) * 4;
        v[1].y = v[0].y + 4 + (int32_t)((h >> 13) % 120u) * 4;
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, 0);
    }
}

static void checkSoftening4x(void)
{
    drawPicture();
    dl_Swap();
    if (!readTarget(rd_target(RD_TARGET_SCENE), s_pic, S4W, S4H)) {
        return;
    }
    const RdTargetRec *aa = rd__target_rec(rd_target(RD_TARGET_AA0).id);
    CHECK(aa && aa->sx == 1.0f, "(s) AA0 at its GS size (%.2f)", aa ? (double)aa->sx : 0.0);
    const uint32_t before = g_rd.above1xDraws;
    drawPicture();
    RdPostParams pp;
    dl_SetDLPriority(10);
    memset(&pp, 0, sizeof(pp));
    pp.lines = 1;
    rd_post(RD_POST_AA_DOWNSAMPLE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 64;
    rd_post(RD_POST_AA_COMPOSITE, &pp);
    dl_Swap();
    static uint8_t aa0[(W / 2) * 256 * 4];
    const int aw = W / 2, ah = 256;
    if (!readTarget(rd_target(RD_TARGET_AA0), aa0, (uint32_t)aw, (uint32_t)ah) ||
        !readTarget(rd_target(RD_TARGET_SCENE), s_big, S4W, S4H)) {
        return;
    }
    CHECK(g_rd.above1xDraws == before + 1,
          "(s) softening: %u passes drawn above 1x (the downsample alone)",
          g_rd.above1xDraws - before);
    /* AA0: the sprite spans GS -0.25 .. W / 2 - 0.25 with U 0.25 .. W +
     * 0.25 (rd_post.c postAaDownsample), the box of 8 SCENE texels about
     * each texel's sample point */
    const A1xImg pic = {s_pic, S4W, S4H};
    int worstA = 0, ax = 0, ay = 0;
    for (int y = 0; y < ah; y++) {
        for (int x = 0; x < aw; x++) {
            const double gx = gsAt(x, 1), gy = gsAt(y, 1);
            const double u = 0.25 + (gx + 0.25) * 2.0, v = 0.25 + (gy + 0.25) * 2.0;
            uint32_t t[4];
            a1x_resample(a1xFetch, (void *)&pic, (int)lrint(u * 16.0 * S4),
                         (int)lrint(v * 16.0 * S4), 32u * 2u * S4, 32u * 2u * S4, 1, t);
            const uint8_t *g = &aa0[((size_t)y * aw + x) * 4];
            for (int c = 0; c < 4; c++) {
                const int d = abs((int)g[c] - (int)t[c]);
                if (d > worstA) {
                    worstA = d, ax = x, ay = y;
                }
            }
        }
    }
    /* SCENE: AA0 (texels at 1; the sprite spans GS -0.25 .. W - 0.25 with
     * U 0.25 .. W / 2 + 0.25, rd_post.c postAaComposite) at an eighth of a
     * texel a texel, the bilinear between the same texels, LERP_FIX 64 over
     * the picture */
    int worstS = 0, sx = 0, sy = 0;
    for (int y = 0; y < S4H; y++) {
        for (int x = 0; x < S4W; x++) {
            const double gx = gsAt(x, S4), gy = gsAt(y, S4);
            const double u = 0.25 + (gx + 0.25) * 0.5, v = 0.25 + (gy + 0.25) * 0.5;
            float t[4];
            bilinear(aa0, aw, ah, (float)u, (float)v, t);
            const uint8_t *g = &s_big[((size_t)y * S4W + x) * 4];
            const uint8_t *p = &s_pic[((size_t)y * S4W + x) * 4];
            for (int c = 0; c < 3; c++) {
                const int tc = (int)floorf(t[c] + 0.5f);
                const int want = (((tc - (int)p[c]) * 64) >> 7) + (int)p[c];
                const int d = abs((int)g[c] - want);
                if (d > worstS) {
                    worstS = d, sx = x, sy = y;
                }
            }
        }
    }
    printf("  (s) 4x softening: AA0 (at 1) max %d LSB from the box (at %d,%d), SCENE max %d from "
           "the bilinear composite (at %d,%d)\n",
           worstA, ax, ay, worstS, sx, sy);
    CHECK(worstA == 0, "(s) AA0 at 4x: max difference %d from the box at (%d,%d)", worstA, ax, ay);
    CHECK(worstS <= 2, "(s) SCENE at 4x: max difference %d from the composite at (%d,%d)", worstS,
          sx, sy);
}

/* The count of a rectangle at scale 4 (Enhanced), after
 * the box reduction to the GS size, gives the first blur level the integral
 * the area gives however the rectangle sits against the GS pixel grid
 * (sub-pixel offsets of a quarter pixel); sampled with 2x2 taps it varied by
 * about 2.6 %. */
static void checkScaledIntegral(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 4.0f;
    if (!rd_init(W, H, &st, NULL)) {
        CHECK(0, "rd_init at scene scale 4");
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    double lo = 1e30, hi = 0.0;
    for (int k = 0; k < 4; k++) {
        s_subOff = 4 * k;
        recordFrame(VOL_SUB, kAll);
        CHECK(g_rd.sceneSx > 3.9f, "the scene is at scale %.2f", (double)g_rd.sceneSx);
        if (!readTarget(rd_target(RD_TARGET_SHADOW0), s_lv[0], 256, 256)) {
            break;
        }
        double sum = 0.0;
        for (int i = 0; i < 256 * 256; i++) {
            sum += s_lv[0][i * 4] + s_lv[0][i * 4 + 3];
        }
        printf("  scale 4, offset %d/16: level 1 integral %.0f\n", s_subOff, sum);
        lo = sum < lo ? sum : lo;
        hi = sum > hi ? sum : hi;
    }
    CHECK(hi > 0.0 && (hi - lo) / hi < 0.005, "level 1 integral varies by %.2f %% with the offset",
          hi > 0.0 ? 100.0 * (hi - lo) / hi : 0.0);
    for (int l = 1; l <= 3; l++) {
        checkComposite4x(l, false);
        checkComposite4x(l, true);
    }
    checkSoftening4x();
    CHECK(rhi_vk_validation_error_count() == 0, "scale 4: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

/* ---------------------------------- (o) one draw against seven draws */

/* the one draw's table against the GS colour sum, for the counts the brief
 * names (no device) */
static void checkTable(void)
{
    static const int kN[] = {0, 1, 63, 64, 128, 255};
    for (size_t i = 0; i < sizeof(kN) / sizeof(kN[0]); i++) {
        uint8_t t[4];
        rd__shadow_table_texel((uint32_t)kN[i] & RD_SHADOW_STENCIL_MASK, 4, t);
        const uint8_t c = wrappedColour(kN[i]);
        CHECK(t[0] == c && t[1] == c && t[2] == c && t[3] == (c ? 0x80 : 0),
              "(o) table: n = %d gives %u %u %u %u, the GS %u %u %u %u", kN[i], t[0], t[1], t[2],
              t[3], c, c, c, c ? 0x80 : 0);
    }
    /* the step 8's table: the stencil keeps n mod 64, the colour 8 n mod 256 */
    static const int kN8[] = {0, 1, 31, 32, 33, 63, 64, 255, -1, -33};
    for (size_t i = 0; i < sizeof(kN8) / sizeof(kN8[0]); i++) {
        uint8_t t[4];
        rd__shadow_table_texel((uint32_t)kN8[i] & RD_SHADOW_STENCIL_MASK, 8, t);
        const uint8_t c = wrappedColourStep(kN8[i], 8);
        CHECK(t[0] == c && t[1] == c && t[2] == c && t[3] == (c ? 0x80 : 0),
              "(8) table: n = %d gives %u %u %u %u, the GS %u %u %u %u", kN8[i], t[0], t[1], t[2],
              t[3], c, c, c, c ? 0x80 : 0);
    }
}

typedef struct AbPics {
    uint8_t *cnt, *lv[3], *scene;
    uint32_t cw, ch, sw, sh;
} AbPics;

static void abFree(AbPics *p)
{
    free(p->cnt);
    for (int l = 0; l < 3; l++) {
        free(p->lv[l]);
    }
    free(p->scene);
    memset(p, 0, sizeof(*p));
}

/* the frame recorded and replayed with the one draw (one) or the seven
 * draws, and its pictures read back; false (reported) when one is missing */
static bool abRecord(int vol, bool one, AbPics *p, const char *what)
{
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    memset(p, 0, sizeof(*p));
    g_rd.profile.shadowOne = one;
    recordFrame(vol, kAll);
    CHECK(one ? (g_rdPerf.shadowOne >= 1 && g_rdPerf.shadowSeven == 0)
              : (g_rdPerf.shadowSeven >= 1 && g_rdPerf.shadowOne == 0),
          "(o) %s: the %s path resolved the count (one %u, seven %u)", what,
          one ? "one-draw" : "seven-draw", g_rdPerf.shadowOne, g_rdPerf.shadowSeven);
    const RdTargetRec *c = rd__target_rec(s_count.id);
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!c || !sc) {
        CHECK(0, "(o) %s: no count or SCENE target", what);
        return false;
    }
    p->cw = c->w;
    p->ch = c->h;
    p->sw = sc->tw;
    p->sh = sc->th;
    p->cnt = malloc((size_t)p->cw * p->ch * 4);
    p->scene = malloc((size_t)p->sw * p->sh * 4);
    bool ok = p->cnt && p->scene;
    uint32_t w = 0, h = 0;
    ok = ok && rd__shadow_count_read(s_count.id, p->cnt, (size_t)p->cw * p->ch * 4, &w, &h) &&
         w == p->cw && h == p->ch;
    CHECK(ok, "(o) %s: the count at its GS size %ux%u read back", what, p->cw, p->ch);
    for (int l = 0; l < 3 && ok; l++) {
        const uint32_t s = 256u >> l;
        p->lv[l] = malloc((size_t)s * s * 4);
        ok = p->lv[l] && readTarget(rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l)), p->lv[l], s, s);
    }
    ok = ok && readTarget(rd_target(RD_TARGET_SCENE), p->scene, p->sw, p->sh);
    return ok;
}

/* the first texel that differs (-1 none), the count of those that do */
static long firstDiff(const uint8_t *a, const uint8_t *b, size_t texels, size_t *n)
{
    long first = -1;
    *n = 0;
    for (size_t i = 0; i < texels; i++) {
        if (memcmp(&a[i * 4], &b[i * 4], 4) != 0) {
            if (first < 0) {
                first = (long)i;
            }
            (*n)++;
        }
    }
    return first;
}

static void abCompareOne(const uint8_t *a, const uint8_t *b, uint32_t w, uint32_t h,
                         const char *what, const char *pic)
{
    size_t n = 0;
    const long f = firstDiff(a, b, (size_t)w * h, &n);
    CHECK(f < 0,
          "(o) %s: %s differs between the one draw and the seven draws at %zu of %u texels, the "
          "first (%ld,%ld): %02X%02X%02X%02X for %02X%02X%02X%02X",
          what, pic, n, w * h, f < 0 ? 0L : f % (long)w, f < 0 ? 0L : f / (long)w,
          f < 0 ? 0 : a[f * 4], f < 0 ? 0 : a[f * 4 + 1], f < 0 ? 0 : a[f * 4 + 2],
          f < 0 ? 0 : a[f * 4 + 3], f < 0 ? 0 : b[f * 4], f < 0 ? 0 : b[f * 4 + 1],
          f < 0 ? 0 : b[f * 4 + 2], f < 0 ? 0 : b[f * 4 + 3]);
}

/* one frame through both paths, every picture compared; the count's
 * non-zero texels counted so a comparison of empty counts says so */
static void abFrame(int vol, const char *what)
{
    AbPics one, seven;
    const bool okOne = abRecord(vol, true, &one, what);
    const bool okSeven = abRecord(vol, false, &seven, what);
    g_rd.profile.shadowOne = true;
    if (okOne && okSeven) {
        abCompareOne(one.cnt, seven.cnt, one.cw, one.ch, what, "the count");
        for (int l = 0; l < 3; l++) {
            char pic[16];
            snprintf(pic, sizeof pic, "SHADOW%d", l);
            abCompareOne(one.lv[l], seven.lv[l], 256u >> l, 256u >> l, what, pic);
        }
        abCompareOne(one.scene, seven.scene, one.sw, one.sh, what, "SCENE");
        size_t lit = 0;
        for (size_t i = 0; i < (size_t)one.cw * one.ch; i++) {
            lit += one.cnt[i * 4 + 3] != 0;
        }
        CHECK(lit > 0, "(o) %s: the count is empty", what);
        printf("  (o) %s: count %ux%u (%zu texels shadowed), SCENE %ux%u: one draw = seven draws\n",
               what, one.cw, one.ch, lit, one.sw, one.sh);
    }
    abFree(&one);
    abFree(&seven);
}

/* the frames at the renderer's current scale */
static void abFrames(const char *scale)
{
    char what[64];
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    snprintf(what, sizeof what, "%s, synthetic volumes", scale);
    abFrame(VOL_SYNTH, what);
    snprintf(what, sizeof what, "%s, Shadow.c's model", scale);
    abFrame(VOL_MODEL, what);
    for (int k = 0; k < 4; k++) {
        s_subOff = 4 * k + 3;
        snprintf(what, sizeof what, "%s, a rectangle at +%d/16", scale, s_subOff);
        abFrame(VOL_SUB, what);
    }
    s_subOff = 0;
}

/* (o) above 1x: at scale 2 and at 1931 x 1093 texels (Enhanced) */
static void checkOneScaled(void)
{
    if (!rhi_limits()->stencilView) {
        return;
    }

    static const struct {
        float scale;
        uint32_t tw, th;
        const char *what;
    } kRuns[] = {{2.0f, 0, 0, "scale 2"}, {0.0f, 1931, 1093, "1931x1093 texels"}};

    for (size_t i = 0; i < sizeof(kRuns) / sizeof(kRuns[0]); i++) {
        RdSettings st;
        memset(&st, 0, sizeof(st));
        st.preset = RD_PRESET_ENHANCED;
        st.outputWidth = 640;
        st.outputHeight = 480;
        st.aspect = 4.0f / 3.0f;
        st.sceneScale = kRuns[i].scale;
        st.sceneWidth = kRuns[i].tw;
        st.sceneHeight = kRuns[i].th;
        if (!rd_init(W, H, &st, NULL)) {
            CHECK(0, "(o) rd_init at %s", kRuns[i].what);
            continue;
        }
        if (!g_rd.profile.shadowOne) {
            printf("  (o) %s: the profile has the seven draws (%s): not compared\n", kRuns[i].what,
                   rd__profile_text());
            rd_shutdown();
            continue;
        }
        abFrames(kRuns[i].what);
        CHECK(rhi_vk_validation_error_count() == 0, "(o) %s: %u validation errors", kRuns[i].what,
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
}

/* ---------------------------------- (8) the step 8 on the device */

/* the synthetic volumes recorded under the North American disc (the reset's
 * step 8 from Shadow.c) at 1x: the count against 8 n mod 256 through the
 * seven draws, then through the one draw (where the profile has it) and the
 * one draw against the seven draws as in (o); every pipeline created
 * enumerated (the step 8's A key in the set under that disc) */
static void checkStep8(void)
{
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    ico_region = ICO_REGION_NTSC;
    s_step = 8;
    if (rd_init(W, H, &st, NULL)) {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        dl_Clear();
        const bool one = g_rd.profile.shadowOne && rhi_limits()->stencilView;
        g_rd.profile.shadowOne = false;
        recordFrame(VOL_SYNTH, kAll);
        CHECK(g_rdPerf.shadowSeven >= 1, "(8) the seven draws resolved the count");
        if (readTarget(s_count, s_cnt, W, H)) {
            checkCount();
        }
        if (one) {
            g_rd.profile.shadowOne = true;
            recordFrame(VOL_SYNTH, kAll);
            CHECK(g_rdPerf.shadowOne >= 1, "(8) the one draw resolved the count");
            if (readTarget(s_count, s_cnt, W, H)) {
                checkCount();
            }
            abFrames("1x, step 8");
        } else {
            printf("  (8) no stencil view, or the profile has the seven draws (%s): the one draw "
                   "not checked\n",
                   rd__profile_text());
        }
        checkPipelines();
        CHECK(rhi_vk_validation_error_count() == 0, "(8) %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    } else {
        CHECK(0, "(8) rd_init at 1x");
    }
    ico_region = ICO_REGION_PAL;
    s_step = 4;
}

/* ---------------------------------- (x) the two kernels at scale 2 */

#define EDGE_S 2

/* level l (1..3, size n) is constant over the window of texels lo..hi on
 * both axes about the SCENE texel's sample point (CLAMP, as sampled) */
static bool edgeFlat(const uint8_t *lv, int n, int x0, int x1, int y0, int y1)
{
    const uint8_t *ref = NULL;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            const int cx = x < 0 ? 0 : x >= n ? n - 1 : x;
            const int cy = y < 0 ? 0 : y >= n ? n - 1 : y;
            const uint8_t *p = &lv[((size_t)cy * n + cx) * 4];
            if (!ref) {
                ref = p;
            } else if (memcmp(p, ref, 4) != 0) {
                return false;
            }
        }
    }
    return true;
}

/* SCENE texel (x, y) at scale EDGE_S lies within N of a shadow edge: some
 * composited level is not constant over the kernels' joint support about
 * its sample point (the bilinear's texels i0, i0 + 1 and the B-spline's
 * i0 - 1 .. i0 + 2, i0 = floor(u - 0.5) at the level's U u), widened by one
 * texel each side for the 12.4 rounding's tie */
static bool nearEdge(uint8_t *const lv[3], int x, int y)
{
    for (int l = 1; l <= 3; l++) {
        const int n = 256 >> (l - 1);
        const double u = 0.25 + (((double)x + 0.5) / EDGE_S - 0.5) / (double)(1 << l);
        const double v = 0.25 + (((double)y + 0.5) / EDGE_S - 0.5) / (double)(1 << l);
        const int ix = (int)floor(u - 0.5), iy = (int)floor(v - 0.5);
        if (!edgeFlat(lv[l - 1], n, ix - 2, ix + 3, iy - 2, iy + 3)) {
            return true;
        }
    }
    return false;
}

/* the frame recorded with the kernel asked, its levels and SCENE read */
static bool edgeRecord(int vol, bool bspline, AbPics *p, uint32_t *a1x, const char *what)
{
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    memset(p, 0, sizeof(*p));
    g_rd.profile.shadowBspline = bspline;
    const uint32_t before = g_rd.above1xDraws;
    recordFrame(vol, kAll);
    g_rd.profile.shadowBspline = false;
    *a1x = g_rd.above1xDraws - before;
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!sc) {
        CHECK(0, "(x) %s: no SCENE target", what);
        return false;
    }
    p->sw = sc->tw;
    p->sh = sc->th;
    p->scene = malloc((size_t)p->sw * p->sh * 4);
    bool ok = p->scene != NULL;
    for (int l = 0; l < 3 && ok; l++) {
        const uint32_t n = 256u >> l;
        p->lv[l] = malloc((size_t)n * n * 4);
        ok = p->lv[l] && readTarget(rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l)), p->lv[l], n, n);
    }
    return ok && readTarget(rd_target(RD_TARGET_SCENE), p->scene, p->sw, p->sh);
}

static void edgeFrame(int vol, const char *what)
{
    AbPics bi, bs;
    uint32_t a1xBi = 0, a1xBs = 0;
    const bool okBi = edgeRecord(vol, false, &bi, &a1xBi, what);
    const bool okBs = edgeRecord(vol, true, &bs, &a1xBs, what);
    CHECK(a1xBi == 0 && a1xBs >= 3,
          "(x) %s: passes above 1x: %u with the bilinear (none), %u with the B-spline (the three "
          "composites)",
          what, a1xBi, a1xBs);
    if (okBi && okBs && bi.sw == bs.sw && bi.sh == bs.sh) {
        for (int l = 0; l < 3; l++) {
            const uint32_t n = 256u >> l;
            CHECK(memcmp(bi.lv[l], bs.lv[l], (size_t)n * n * 4) == 0,
                  "(x) %s: SHADOW%d differs between the kernels", what, l);
        }
        size_t diff = 0, far = 0;
        int maxd = 0, fx = -1, fy = -1;
        int hist[4] = {0, 0, 0, 0}; /* |d| 1, 2..3, 4..7, 8 and up */
        for (uint32_t y = 0; y < bi.sh; y++) {
            for (uint32_t x = 0; x < bi.sw; x++) {
                const uint8_t *a = &bi.scene[((size_t)y * bi.sw + x) * 4];
                const uint8_t *b = &bs.scene[((size_t)y * bi.sw + x) * 4];
                if (memcmp(a, b, 4) == 0) {
                    continue;
                }
                diff++;
                int d = 0;
                for (int c = 0; c < 4; c++) {
                    const int e = abs((int)a[c] - (int)b[c]);
                    d = e > d ? e : d;
                }
                maxd = d > maxd ? d : maxd;
                hist[d >= 8 ? 3 : d >= 4 ? 2 : d >= 2 ? 1 : 0]++;
                if (!nearEdge(bi.lv, (int)x, (int)y) && far++ == 0) {
                    fx = (int)x, fy = (int)y;
                }
            }
        }
        printf("  (x) %s at %dx: SCENE %ux%u, %zu texels differ between the bilinear and the "
               "B-spline (max %d; |d| 1: %d, 2-3: %d, 4-7: %d, 8+: %d), %zu of them farther than N "
               "from a shadow edge\n",
               what, EDGE_S, bi.sw, bi.sh, diff, maxd, hist[0], hist[1], hist[2], hist[3], far);
        CHECK(far == 0,
              "(x) %s: %zu texels differ farther than N from a shadow edge, the first (%d,%d)",
              what, far, fx, fy);
        CHECK(diff > 0, "(x) %s: the two kernels give the same SCENE", what);
    } else {
        CHECK(0, "(x) %s: the pictures were not read (or differ in size)", what);
    }
    abFree(&bi);
    abFree(&bs);
}

/* (x): the frames at scene scale 2 (Enhanced); false without a device */
static bool checkEdges2x(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = (float)EDGE_S;
    if (!rd_init(W, H, &st, NULL)) {
        printf("rd_shadow_test edges: SKIP: no usable Vulkan device\n");
        return false;
    }
    dl_Init();
    CHECK(!g_rd.profile.shadowBspline, "(x) the profile's default is the bilinear (%s)",
          rd__profile_text());
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    edgeFrame(VOL_SYNTH, "synthetic volumes");
    edgeFrame(VOL_MODEL, "Shadow.c's model");
    s_subOff = 7;
    edgeFrame(VOL_SUB, "a rectangle at +7/16");
    s_subOff = 0;
    CHECK(rhi_vk_validation_error_count() == 0, "(x) %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return true;
}

/* ---------------------------------- (w) a strip across a 48:9 side
 *
 * On a frame 2048 wide (48:9) the edge clip is GSB_SHADOW_CLIP_MAX.  A
 * caster at the picture's right side (GS x 3482 .. 3584) whose volume runs
 * out along view +x (the shadow direction (1, 0, 0.01), 600 long) past
 * GS x 4094: each of its three edges is clipped at the edge clip, and the
 * strip they make is drawn (its eight triangles recorded), the clipped ends
 * at GS x 2048 + GSB_SHADOW_CLIP_MAX.  A clip of 2047 would not do: a
 * clipped end at GS x 4095 (65520 sixteenths) fails emitVolumeStrip's
 * range test (at most 0xFFEF) and the strip, the shadow inside the picture
 * with it, is dropped. */
static Sub15C *buildWideModel(void)
{
    static const float caster[3][3] = {
        {560.0f, 0.0f, 100.0f}, {530.0f, 0.0f, 100.0f}, {530.0f, 30.0f, 100.0f}};
    Sub15C *o = zalloc(sizeof(Sub15C));
    PObjModel *m = zalloc(sizeof(PObjModel));
    PObjModel *x = zalloc(sizeof(PObjModel));
    PObjPart *p = zalloc(sizeof(PObjPart));
    float (*node)[4] = zalloc(64);
    for (int i = 0; i < 4; i++) {
        node[i][i] = 1.0f;
    }
    float (*vtx)[4] = zalloc(3 * 16);
    for (int i = 0; i < 3; i++) {
        vtx[i][0] = caster[i][0];
        vtx[i][1] = caster[i][1];
        vtx[i][2] = caster[i][2];
        vtx[i][3] = 1.0f;
    }
    TestRun *run = zalloc(5 * sizeof(TestRun));
    run[0].count = 3;
    for (int i = 0; i < 3; i++) {
        run[1 + i].count = 1;
        run[1 + i].vtx = (short)i;
    }
    TestRun **strips = zalloc(sizeof(TestRun *));
    strips[0] = run;
    p->vtx = (char *)vtx;
    p->vtxCount = 3;
    p->vtxSave = zalloc(3 * 16);
    p->nrmSave = zalloc(3 * 16);
    p->strips = strips;
    p->stripCount = 1;
    x->partCount = 1;
    x->parts = p;
    m->shadowLength = 600.0f;
    o->nodeNum = 1;
    o->nodeMtx = (ICO_WORD)node;
    o->model = m;
    o->shadow = x;
    o->dispType = 0;
    o->shadowDir[0] = 1.0f;
    o->shadowDir[1] = 0.0f;
    o->shadowDir[2] = 0.01f;
    return o;
}

static void checkWideClip(void)
{
    const RdFrame *f = rd__last_frame();
    const RdCmdList *cl = f ? &f->lists[3] : NULL;
    const RdCmd *c = NULL;
    for (uint32_t i = 0; cl && i < cl->count; i++) {
        if (cl->cmds[i].type == RDC_SHADOW_STRIP) {
            c = &cl->cmds[i];
            break;
        }
    }
    CHECK(c != NULL, "48:9: the strip across the side is drawn (edge clip %g)",
          (double)gsb_HostShadowClipX());
    if (!c) {
        return;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[1]);
    const uint32_t n = c->u[0] + c->u[3];
    int32_t maxX = 0;
    for (uint32_t k = 0; k < n; k++) {
        maxX = v[k].x > maxX ? v[k].x : maxX;
    }
    const int32_t want = (int32_t)((2048.0f + GSB_SHADOW_CLIP_MAX) * 16.0f);
    CHECK(n == 8 * 3 && maxX >= want - 1 && maxX <= 0xFFEF,
          "48:9: eight triangles (%u), the clipped ends at GS x %d / 16 (want %d, at most %d)",
          n / 3, maxX, want, 0xFFEF);
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    setMatrices();
    buildModel();
    if (argc > 1 && strcmp(argv[1], "edges") == 0) {
        /* (x) alone (ctest rd_shadow_edges) */
        if (!checkEdges2x()) {
            return 77;
        }
        if (failures) {
            printf("rd_shadow_test edges: %d failures\n", failures);
            return 1;
        }
        printf("rd_shadow_test edges: ok\n");
        return 0;
    }

    /* (e) recording */
    if (!rd__init_record_only(W, H)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    recordFrame(VOL_MODEL, kAll);
    checkRecording();
    checkLevelScale();
    checkTable();
    rd_shutdown();
    /* the same on a frame 1376 wide (32:9), as GsBase.c's latch leaves it */
    {
        const int fw = 1376;

        gsFrame.w = fw;
        gsFrame.fbw = (fw + 63) / 64;
        gsFrame.wExact = (float)fw;
        gsFrame.aspect = 4.0f / 3.0f * (float)fw / 512.0f;
        gif_HostSetFrameWidth(fw);
        if (!rd__init_record_only((uint32_t)fw, H)) {
            printf("FAIL rd__init_record_only at W %d\n", fw);
            return 1;
        }
        gif_HostForgetTextures();
        gif_HostFrameReset();
        dl_Clear();
        recordFrame(VOL_MODEL, kAll);
        checkRecording();
        rd_shutdown();
        gsFrame.w = W;
        gsFrame.fbw = W / 64;
        gsFrame.wExact = (float)W;
        gsFrame.aspect = 4.0f / 3.0f;
        gif_HostSetFrameWidth(W);
    }
    /* (w) a strip across the side of a frame 2048 wide (48:9) */
    {
        const int fw = 2048;
        Sub15C *keep = s_obj;

        gsFrame.w = fw;
        gsFrame.fbw = fw / 64;
        gsFrame.wExact = (float)fw;
        gsFrame.aspect = 4.0f / 3.0f * (float)fw / 512.0f;
        gif_HostSetFrameWidth(fw);
        if (!rd__init_record_only((uint32_t)fw, H)) {
            printf("FAIL rd__init_record_only at W %d\n", fw);
            return 1;
        }
        gif_HostForgetTextures();
        gif_HostFrameReset();
        dl_Clear();
        s_obj = buildWideModel();
        recordFrame(VOL_MODEL, kAll);
        checkWideClip();
        s_obj = keep;
        rd_shutdown();
        gsFrame.w = W;
        gsFrame.fbw = W / 64;
        gsFrame.wExact = (float)W;
        gsFrame.aspect = 4.0f / 3.0f;
        gif_HostSetFrameWidth(W);
    }
    /* (8) the step 8 recorded: Shadow.c's model on the North American disc */
    ico_region = ICO_REGION_NTSC;
    s_step = 8;
    if (!rd__init_record_only(W, H)) {
        printf("FAIL rd__init_record_only (the step 8)\n");
        return 1;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    recordFrame(VOL_MODEL, kAll);
    checkRecording();
    rd_shutdown();
    ico_region = ICO_REGION_PAL;
    s_step = 4;
    if (failures) {
        printf("rd_shadow_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(W, H, &st, NULL)) {
        printf("rd_shadow_test: recording ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();

    /* (a), (b), (c) all three, (d) */
    recordFrame(VOL_SYNTH, kAll);
    if (readTarget(s_count, s_cnt, W, H)) {
        checkCount();
    }
    bool lv = true;
    for (int l = 0; l < 3; l++) {
        lv = readTarget(rd_target((RdTargetId)(RD_TARGET_SHADOW0 + l)), s_lv[l],
                        (uint32_t)(256 >> l), (uint32_t)(256 >> l)) &&
             lv;
    }
    if (lv) {
        checkChain();
    }
    if (lv && readTarget(rd_target(RD_TARGET_SCENE), s_scene, W, H)) {
        checkComposite(kAll, 3, 3, "levels 3, 2, 1 together");
    }
    /* (c) one level at a time */
    for (int l = 1; l <= 3; l++) {
        int one[4] = {0, 0, 0, 0};
        one[l] = kBlend[l];
        recordFrame(VOL_SYNTH, one);
        char what[32];
        snprintf(what, sizeof(what), "level %d alone", l);
        if (readTarget(rd_target(RD_TARGET_SCENE), s_scene, W, H)) {
            checkComposite(one, 2, 2, what);
        }
    }
    /* (e) on the device: Shadow.c's own volume */
    recordFrame(VOL_MODEL, kAll);
    if (readTarget(s_count, s_cnt, W, H)) {
        checkModel();
    }
    /* (o) at 1x: the one draw against the seven draws */
    if (!rhi_limits()->stencilView || !g_rd.profile.shadowOne) {
        printf("  (o) no stencil view, or the profile has the seven draws (%s): not compared\n",
               rd__profile_text());
    } else {
        abFrames("1x");
    }

    checkPipelines();
    CHECK(g_rd.above1xDraws == 0, "1x: %u passes drawn above 1x", g_rd.above1xDraws);
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    rd_shutdown();
    checkStep8();
    checkScaledIntegral();
    checkOneScaled();
    if (failures) {
        printf("rd_shadow_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_test: ok\n");
    return 0;
}
