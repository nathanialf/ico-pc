/* rd_shadow_test.c: the shadow count on the stencil.
 *
 * Shadow.c with the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c) and
 * Matrix.c, compiled as the window build has them (ICO_HOST, ICO_RD); the
 * rest of the game is stubbed below.  No disc data: a receiver plane drawn
 * as sprites, volumes as synthetic rectangles (rd_ShadowTris) and one
 * synthetic shadow model (a triangle caster) through shadow_RenderVolume.
 *
 * Recording (no device):
 *   e  shadow_Reset, shadow_RenderVolume and shadow_Draw record, in list 3,
 *      the register writes of their packets as rd state, the reset and the
 *      resolve, and the volume's eight triangles with the signs of the RGBAQ
 *      the packet carries (compared with the packet words Shadow.c wrote),
 *      each vertex tagged with its triangle's place; at a work scale
 *      of 2 the blur levels keep 256, 128 and 64 texels.
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
 * Every pipeline created is enumerated; no validation errors. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "Matrix.h"
#include "Shadow.h"
#include "main.h"

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

float center_X = 2048.0f, center_Y = 2048.0f;

int screenOffsetX, screenOffsetY;

int fbKeep;

void *ios_partition_common;

void *dmaVif;

char *matrixptr;

int debug_font_flag;

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
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, 0);
}

static void drawReceiver(void)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, RD_TARGET_OFFSET);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kClear, 1, 0);
    rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP); /* leaks into list 3: the chain's CLAMP */
    rd_TextureOff();
    rd_ABE(0);
    rd_TestGs(0x30000);
    rd_ZWrite(1);
    rd_FBA(0);
    sprite(0, 0, W / 2, H, kZr, kRecv);
    rd_FBA(1);
    sprite(W / 2, 0, W, H, kZr, kRecv);
    rd_FBA(0);
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

/* the GS: 0x04 or 0xFC per face, summed with COLCLAMP 0 */
static uint8_t wrappedColour(int n)
{
    uint8_t c = 0;
    for (int i = 0; i < (n < 0 ? -n : n); i++) {
        c = (uint8_t)(c + (n < 0 ? 0xFC : 0x04));
    }
    return c;
}

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
        rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H,
                     RD_TARGET_OFFSET);
        rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kClear, 1, 0);
        rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_TextureOff();
        rd_ABE(0);
        rd_TestGs(0x30000);
        rd_ZWrite(1);
        rd_FBA(0);
        sprite(0, 0, W, H, (uint32_t)(16.0f * kB / kZPlane), kRecv);
    } else {
        drawReceiver();
    }
    shadow_Reset();
    s_count = rd_ShadowCountTarget(W, H);
    if (vol == VOL_SYNTH) {
        buildVolumes();
        rd_ShadowTris(s_tri, s_sign, s_nt, 1);
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
        rd_ShadowTris(q, plus, 2, 1);
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
    const RdTestState t = rd_TestFromGs(gs);
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
    const RdTexRec *tr = c ? rd__TexRec(c->u[0]) : NULL;
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
            sign[n] = r[i] == 0x04 ? 1 : r[i] == 0xFC ? -1 : 0;
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
    const RdFrame *f = rd__LastFrame();
    if (!f) {
        CHECK(0, "a closed frame");
        return;
    }
    const RdCmdList *cl = &f->lists[3];
    const uint32_t scene = rd_Target(RD_TARGET_SCENE).id;
    const RdTargetRec *ct = rd__TargetRec(s_count.id);
    CHECK(ct && ct->w == W && ct->h == H && !ct->withDepth && !ct->named,
          "the count target: a %ux%u temporary target without depth", W, H);
    uint32_t i = 0;
    /* shadow_Reset */
    expectTarget(cl, &i, s_count.id, scene, W, H, "reset: FRAME 0x142, ZBUF 0xC0");
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_OFF, "reset: ZBUF ZMSK");
    expectTest(cl, &i, 0x30000, "reset: TEST");
    expect1(cl, &i, RDC_ABE, 0, "reset: PRIM 0x406 ABE");
    expect1(cl, &i, RDC_SHADE, 0, "reset: PRIM 0x406 IIP");
    nextCmd(cl, &i, RDC_TEXTURE_OFF, "reset: PRIM 0x406 TME");
    nextCmd(cl, &i, RDC_SHADOW_RESET, "reset: the clear of 0x142");
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
            CHECK(psign[t] != 0, "packet triangle %u: RGBAQ neither 0x04 nor 0xFC", t);
            uint32_t *at = psign[t] > 0 ? &a : &b;
            inc += psign[t] > 0;
            for (int k = 0; k < 3; k++) {
                bad += *at + k < c->u[0] + c->u[3] ? !sameXyz(&v[*at + k], &ptri[t * 3 + k]) ||
                                                         rd__ShadowTag(&v[*at + k]) != t + 1
                                                   : 1;
            }
            *at += 3;
        }
        CHECK(bad == 0 && inc * 3 == c->u[0],
              "volume: the triangles and signs are the packet's kicks, each vertex tagged with "
              "its triangle's place in the packet (V3) (%u mismatched vertices)",
              bad);
        printf("  volume strip: %u triangles count +1 (RGBAQ 0x04), %u count -1 (0xFC)\n", inc,
               pn - inc);
    }
    /* shadow_Draw */
    expectTarget(cl, &i, s_count.id, scene, W, H, "draw: the count before the resolve");
    nextCmd(cl, &i, RDC_SHADOW_RESOLVE, "draw: the resolve");
    expectTest(cl, &i, 0x30000, "draw: TEST");
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_OFF, "draw: ZBUF ZMSK");
    expect1(cl, &i, RDC_COLCLAMP, 1, "draw: COLCLAMP 1");
    expect1(cl, &i, RDC_FBA, 0, "draw: FBA");
    expect1(cl, &i, RDC_TEXA, RD_TEXA_80_80_AEM, "draw: TEXA 0x80 AEM");
    c = nextCmd(cl, &i, RDC_FILTER, "draw: TEX1 0x60");
    CHECK(c && c->b[0] == RD_FILTER_LINEAR && c->b[1] == RD_FILTER_LINEAR, "draw: TEX1 linear");
    const uint8_t col[4] = {128, 128, 128, kDepth};
    for (int l = 0; l < 3; l++) {
        const int s = 256 >> l;
        const int r[4] = {-(s * 8 + 4), -(s * 8 + 4), s * 16, s * 16};
        const int uv[4] = {4, 4, 2 * s * 16, 2 * s * 16};
        expectTarget(cl, &i, rd_Target((RdTargetId)(RD_TARGET_SHADOW0 + l)).id, 0, (uint32_t)s,
                     (uint32_t)s, "chain: FRAME");
        expectTexture(cl, &i, l == 0 ? s_count : rd_Target((RdTargetId)(RD_TARGET_SHADOW0 + l - 1)),
                      "chain: TEX0");
        expectSprite(f, cl, &i, 0, r, uv, col, "chain: sprite");
    }
    expectTarget(cl, &i, scene, scene, W, H, "composite: FRAME 0x40");
    c = nextCmd(cl, &i, RDC_ALPHA, "composite: ALPHA 0x44");
    CHECK(c && c->b[0] == RD_BLEND_LERP_AS && c->b[1] == 0, "composite: ALPHA 0x44");
    expectTest(cl, &i, 0x3400D, "composite: TEST");
    for (int l = 3; l > 0; l--) {
        const int off[4] = {-(W / 2) * 16, -(H / 2) * 16, W * 16, H * 16};
        const int uv[4] = {4, 4, (W * 16) >> l, (H * 16) >> l};
        const uint8_t col2[4] = {(uint8_t)kShadowCol[0], (uint8_t)kShadowCol[1],
                                 (uint8_t)kShadowCol[2],
                                 (uint8_t)GlobalStageSetting.shadowBlend[l]};
        expectTexture(cl, &i, rd_Target((RdTargetId)(RD_TARGET_SHADOW0 + l - 1)),
                      "composite: TEX0");
        c = nextCmd(cl, &i, RDC_FILTER, "composite: TEX1");
        expectSprite(f, cl, &i, 1, off, uv, col2, "composite: sprite");
    }
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_ON, "end: ZBUF");
    expectTest(cl, &i, 0x50000, "end: TEST");
    expectTarget(cl, &i, scene, scene, W, H, "end: FRAME 0x40");
    CHECK(i == cl->count, "list 3 ends after shadow_Draw (%u of %u commands)", i, cl->count);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

/* -------------------------------------------------- the device references */

static uint8_t s_cnt[W * H * 4], s_lv[3][256 * 256 * 4], s_scene[W * H * 4];

static bool readTarget(RdTarget t, uint8_t *dst, uint32_t ew, uint32_t eh)
{
    uint32_t w = 0, h = 0;
    bool ok = rd__ReadTarget(t, dst, (size_t)ew * eh * 4, &w, &h) && w == ew && h == eh;
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
            const uint8_t c = wrappedColour(n);
            const uint8_t *p = &s_cnt[(y * W + x) * 4];
            const uint8_t a = c ? 0x80 : 0;
            if (p[0] != c || p[1] != c || p[2] != c || p[3] != a) {
                if (!bad++) {
                    firstX = x, firstY = y;
                }
            }
        }
    }
    CHECK(bad == 0, "(a) count: %d pixels differ from 4 n mod 256, first (%d,%d) n %d: %u %u %u %u",
          bad, firstX, firstY, firstX >= 0 ? expectedCount(firstX, firstY) : 0,
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

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        const uint8_t *p = &s_cnt[(k[i].y * W + k[i].x) * 4];
        CHECK(expectedCount(k[i].x, k[i].y) == k[i].n && wrappedColour(k[i].n) == k[i].c &&
                  p[0] == k[i].c,
              "(a) n = %d at (%d,%d): colour %u, expected %u", k[i].n, k[i].x, k[i].y, p[0],
              k[i].c);
    }
    printf("  (a) count: %d of %d pixels off the wrapped colour sum\n", bad, W * H);
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
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u", n);
    for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u fs %u stencil %u) is not enumerated", i,
              k->gs.program, k->vs, k->fs, k->gs.stencil);
    }
    printf("  pipelines: %u created, %u reachable\n", rd__PipelineCount(), n);
}

/* At a work scale of 2 (Enhanced, 896 lines and up) the blur levels
 * keep the PS2 sizes, whose texels are the shadow's blur; WORK0 scales */
static void checkLevelScale(void)
{
    const float keep = g_rd.workScale;
    g_rd.workScale = 2.0f;

    static const struct {
        int id;
        uint32_t w;
    } k[4] = {{RD_TARGET_SHADOW0, 256},
              {RD_TARGET_SHADOW1, 128},
              {RD_TARGET_SHADOW2, 64},
              {RD_TARGET_WORK0, 512}};

    for (int i = 0; i < 4; i++) {
        RdTargetRec t;
        memset(&t, 0, sizeof(t));
        t.w = t.h = k[i].id == RD_TARGET_WORK0 ? 256 : k[i].w;
        rd__TargetScaleOf(&t, k[i].id);
        CHECK(t.tw == k[i].w, "work scale 2: target %d is %u texels wide (%u)", k[i].id, t.tw,
              k[i].w);
    }
    g_rd.workScale = keep;
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
    if (!rd_Init(W, H, &st, NULL)) {
        CHECK(0, "rd_Init at scene scale 4");
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
        if (!readTarget(rd_Target(RD_TARGET_SHADOW0), s_lv[0], 256, 256)) {
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
    rd_Shutdown();
}

int main(void)
{
    rd__SetNotImplementedFatal(true); /* a stub command replayed stops the test */
    static const int kAll[4] = {0, kBlend[1], kBlend[2], kBlend[3]};
    setMatrices();
    buildModel();

    /* (e) recording */
    if (!rd__InitRecordOnly(W, H)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    recordFrame(VOL_MODEL, kAll);
    checkRecording();
    checkLevelScale();
    rd_Shutdown();
    if (failures) {
        printf("rd_shadow_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(W, H, &st, NULL)) {
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
        lv = readTarget(rd_Target((RdTargetId)(RD_TARGET_SHADOW0 + l)), s_lv[l],
                        (uint32_t)(256 >> l), (uint32_t)(256 >> l)) &&
             lv;
    }
    if (lv) {
        checkChain();
    }
    if (lv && readTarget(rd_Target(RD_TARGET_SCENE), s_scene, W, H)) {
        checkComposite(kAll, 3, 3, "levels 3, 2, 1 together");
    }
    /* (c) one level at a time */
    for (int l = 1; l <= 3; l++) {
        int one[4] = {0, 0, 0, 0};
        one[l] = kBlend[l];
        recordFrame(VOL_SYNTH, one);
        char what[32];
        snprintf(what, sizeof(what), "level %d alone", l);
        if (readTarget(rd_Target(RD_TARGET_SCENE), s_scene, W, H)) {
            checkComposite(one, 2, 2, what);
        }
    }
    /* (e) on the device: Shadow.c's own volume */
    recordFrame(VOL_MODEL, kAll);
    if (readTarget(s_count, s_cnt, W, H)) {
        checkModel();
    }

    checkPipelines();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    checkScaledIntegral();
    if (failures) {
        printf("rd_shadow_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_test: ok\n");
    return 0;
}
