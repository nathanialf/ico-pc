/* rd_raw_test.c: the raw packet builders outside seki (renderer wave 5, R5c;
 * docs/port/RENDER_API.md section 18).
 *
 * darkVolume.c, particleEffect.c, lineManager.c and lightning.c with the
 * VU1 chain reader (MicroCode.c), Primitive.c, matrixDrive.c, the 2D layer
 * (GifPacket.c, DisplayList.c, DmaPacket.c) and Matrix.c, compiled as the
 * window build has them (ICO_HOST, ICO_RD); the rest of the game is stubbed
 * below (one uniform texture for every TEX0).  No disc data.
 *
 * Recording (no device):
 *   particle  dispParticleEffect's packet, hand-decoded from the packet
 *             buffer (below), then the recorded PABE/ALPHA ahead of the
 *             batch for alphaMode 0, 1, 2; enemy.c's sequence
 *             (gif_SetAlpha(1, 4, 128) then prim_DispParticle) likewise
 *   lightning the DIRECT packet hand-decoded (the REGLIST PRIM tag, the strip
 *             tag, its vertices) against the triangles the decoder records;
 *             c = 0..11 give the twelve ALPHA registers of gif_SetAlpha's
 *             table, c = 12 and -1 mode 0 (reported once)
 *   lines     Draw2DLine (flat, Z given or the vertices'), Draw2DLineG
 *             (Gouraud), the segment pair, DrawLine / DrawLineG through
 *             _getLine against a double-precision projection
 *   dark      SetupDarkVolume and DispGameOverEffect: packet 1 hand-decoded,
 *             the block target (not AA0) for the clear and the spheres, the
 *             spheres against SCENE's depth under COLCLAMP 0 / ALPHA 0x68
 *             FIX 0x80 (the wrap path), the composite into SCENE with
 *             FBMSK 0xFF000000 (FRAME PSMCT24) reading the block's RGB24
 *             view, the state left (ZBUF write on, TEST 0x50000); sonic's
 *             TEST 0x33001 (RGB_ONLY) sprite reading SCENE
 *   nothing reaches the decoder undecoded.
 * On a Vulkan device (exit 77 without one, after the recording checks):
 *   dark      a wall over the left half of SCENE at the volume's centre
 *             depth: the block (the count) against a CPU raster of the
 *             recorded spheres (GS 12.4 edge functions, flat colour, Z
 *             GEQUAL against the known depth, sum modulo 256; ties of an
 *             edge or a depth within 1024 Z units masked), 0 LSB; SCENE
 *             after the composite against the GS LERP of the bilinear count
 *             (TEXA expanded after filtering, as rd samples RGB24), 1 LSB,
 *             SCENE's alpha untouched (PSMCT24); the GS order (TEXA before
 *             filtering) is measured and printed
 *   lightning one bolt with c = 4 (LERP As) and one with c = 5 (Cs As + Cd)
 *             over a grey SCENE against a CPU raster of the recorded
 *             triangles with the GS integer blend: 1 LSB a layer
 *   particle  particleEffect's batch against vu1_ref's sprites (vu1ref_Particle
 *             on the batch the frame drew) rasterised on the CPU with the
 *             GS blend: 1 LSB a layer
 *   lines     flat and Gouraud lines: the colour of every pixel drawn within
 *             1 LSB of the reference, about one pixel per unit of the major
 *             axis
 * No validation errors; no stubbed command replayed.
 *
 * Hand-decoded packets (the bytes are asserted in the recording checks):
 *
 * dispParticleEffect (particleEffect.c:384), alphaMode 1, at c:
 *   c+0x00  0x10000004 0x00000000      DMA cnt, QWC 4
 *   c+0x08  0x11000000                 VIF FLUSH
 *   c+0x0C  0x6C038000                 VIF UNPACK V4-32, 3 qwords, FLG, TOP+0
 *   c+0x10  0x00008002 0x10000000      GIF tag: NLOOP 2, EOP, PACKED, NREG 1
 *   c+0x18  0x0000000E 0x00000000      REGS: A+D
 *   c+0x20  0 / 0x49                   PABE 0
 *   c+0x30  0x48 / 0x42                ALPHA_1 0x48: Cs*As + Cd (mode 5), FIX 0
 *   c+0x40  0x15000000 0 0 0           VIF MSCALF 0 (SET_GSREGISTER), NOPs
 *   c+0x50  0x60000000 0 0 0           DMA ret
 *
 * DrawLightning2's strip packet (lightning.c:331), at pk:
 *   pk+0x00 0x1000000n                 DMA cnt, QWC n
 *   pk+0x08 0x11000000 0x500000nn      VIF FLUSH, VIF DIRECT nn qwords
 *   pk+0x10 0x1400000000008001, 0      GIF tag: NLOOP 1, EOP, REGLIST, NREG 1,
 *                                      REGS 0 (PRIM)
 *   pk+0x20 84, 0                      PRIM 0x54 (strip, TME, ABE), pad
 *   pk+0x30 0x3400000000008000 | k, 0x521
 *                                      GIF tag: NLOOP k, EOP, REGLIST, NREG 3,
 *                                      REGS RGBAQ, ST, XYZ2
 *   pk+0x40 k x (RGBAQ, ST, XYZ2)      raw register values; the first two are
 *                                      the strip's previous two vertices
 *
 * darkVolume's packet 1 (darkVolume.c:599), ScreenWidth = ScreenHeight = 512:
 *   DMA cnt, FLUSH, UNPACK V4-32 18 qwords to TOP, GIF tag NLOOP 17 EOP
 *   PACKED A+D, then FRAME_1 0x80140 (FBP 0x140, FBW 8, PSMCT32), SCISSOR_1
 *   511/511, XYOFFSET_1 0x7000/0x7000, FBA_1 0, TEXA 0x80/0x80, ZBUF_1
 *   0x1300000C0 (ZBP 0xC0, ZMSK), TEST_1 0x30000, PABE 0, ALPHA_1
 *   0x8000000044, PRIM 0x406, RGBAQ 0, XYZ2 (0x7000, 0x7000, 0xFFFFFFFF),
 *   XYZ2 (0x9000, 0x9000, 0xFFFFFFFF), TEST_1 0x50000, ALPHA_1 0x8000000068
 *   (Cs*FIX + Cd, FIX 0x80), COLCLAMP 0; MSCALF 0; DMA ret. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
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
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "MicroCode.h"
#include "Primitive.h"
#include "darkVolume.h"
#include "lightning.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "particleEffect.h"
#include "ico_math.h"

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
float vsWidth = 640.0f, vsHeight = 448.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
void *ios_partition_common;
struct IosMemPart *ios_partition_seki, *ios_partition_oomori;
void *dmaVif;
char *matrixptr;
int systemStatus[12];
int buffer_ID;
int debug_bounding_flag, debug_specular_flag, debug_shadow_flag, debug_window_flag;
int debug_disp_mesh = 1, debug_disp_particle = 1;
int debug_font_flag;
float inflateSec;
int texturetranssize, GlobalTimer, currentScreenWidth;
void *girlGObj, *boyGObj;

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

void malloc_MemCpy(void *dst, void *src, int size)
{
    memcpy(dst, src, (size_t)size);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
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

/* the game-over effect's object side: no objects */
void ExecuteSEPackage(void *g, int no)
{
    (void)g, (void)no;
}

void *isysGObjSearchFromObjKindID_begin(int kind)
{
    (void)kind;
    return NULL;
}

void *isysGObjSearchFromObjKindID_next(void *g)
{
    (void)g;
    return NULL;
}

void iosOmSendMail(void *to, int mail, void *from)
{
    (void)to, (void)mail, (void)from;
}

void GetRootPosition(float *pos, void *g)
{
    (void)g;
    memset(pos, 0, 16);
}

/* the particle effect's world: no wind; the effect's matrix is a
   translation (the quaternion is the identity in every call here) */
static float s_zeroWind[4];

void *GetWindVector(int no, void *pos)
{
    (void)no, (void)pos;
    return s_zeroWind;
}

void GetMatrixFromQuaternionPos(void *mtx, void *q, void *pos)
{
    float *m = mtx;
    (void)q;
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    memcpy(m + 12, pos, 12);
}

void CopyQuaternion(void *d, void *s)
{
    memcpy(d, s, 16);
}

/* EE scratchpad (Basic.c), lightning's matrix helper (itou_sub.c, the same
   host body), matrixDrive's table set-up and Primitive.c's shine list */
char ico_scratchpad[16 * 1024] __attribute__((aligned(16)));

const ParticleEffectFile particleEffectFile[1] = {{"test", "test"}};

void apply_matrix_w1(void *out, void *m, void *in)
{
    float mm[4][4];
    memcpy(mm, m, sizeof mm);
    ico_apply_matrix_w1((float *)out, (const float (*)[4])mm, (const float *)in);
}

void InitTableSin(void) {}

void InitQuaternionDrive(void) {}

short GetTableArcTan2(float y, float x)
{
    return (short)(atan2f(y, x) * (32768.0f / 3.14159265f));
}

int reg_GetShinePri(int shine)
{
    (void)shine;
    return 7;
}

/* ------------------------------------------------- the texture stand-in
 * One texture for every name, 16 x 16 PSMCT32 of one colour (so STQ and
 * bilinear filtering cannot matter); tex_TransTexture writes what
 * Texture.c's host path writes, TEX1 and TEX0, and the resolver binds it. */
#define TEST_TBP 0x1000u

static const uint8_t kTexel[4] = {200, 160, 120, 0x80};

static RdTex s_tex;

static unsigned char s_zeroRec[4096] __attribute__((aligned(16)));

void *tex_GetTexExtData(int idx)
{
    (void)idx;
    return s_zeroRec;
}

void *tex_GetTextureData(int idx)
{
    (void)idx;
    return s_zeroRec;
}

int tex_GetTextureNo(const char *name)
{
    (void)name;
    return 0;
}

int tex_GetTextureNum(void)
{
    return 1;
}

int tex_TransTexture(int no, int pri)
{
    (void)no;
    gif_StartPacketPri(pri);
    gif_SetGsReg(0x14, 0); /* TEX1: nearest */
    gif_SetGsReg(6, (long long)TEST_TBP | (1LL << 14) | (4LL << 26) | (4LL << 30) | (1LL << 34));
    gif_EndPacket();
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
        memcpy(&px[i * 4], kTexel, 4);
    }
    s_tex = rd_CreateTexture(16, 16, px, RD_TEXA_80_80, "rd_raw_test texture");
}

/* --------------------------------------------------------- the scene
 * View identity (+0x80); the screen matrix (+0xC0, and +0x100 = screen x
 * view) maps view (x, y, z) to X = 2048 + 400 x / z, Y = 2048 + 400 y / z,
 * GS Z = 2^24 / z (nearer is larger, as the game's); +0x1C0, lightning's
 * clip matrix, keeps every point inside.  vu_common's block as rd_mesh's. */
static float s_scratch[0x800 / 4] __attribute__((aligned(16)));

static float s_common[16][4];

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

#define SCALE 400.0f
#define ZK 16777216.0f

static void buildScene(void)
{
    matrixptr = (char *)s_scratch;
    identity((float *)(matrixptr + 0x80));
    float *S = (float *)(matrixptr + 0xC0);
    qw4(S + 0, SCALE, 0, 0, 0);
    qw4(S + 4, 0, SCALE, 0, 0);
    qw4(S + 8, 2048, 2048, 0, 1);
    qw4(S + 12, 0, 0, ZK, 0);
    memcpy(matrixptr + 0x100, S, 64);
    float *C = (float *)(matrixptr + 0x1C0);
    identity(C);
    C[0] = C[5] = C[10] = 0.001f;
    memset(s_common, 0, sizeof(s_common));
    qw4(s_common[0], 0, 0, 0, 1);
    qw4(s_common[1], 4095, 4095, 0, 16777215);
    qw4(s_common[3], ubitsF(0x8000), ubitsF(0x302EC000), ubitsF(0x512), 0);
    memcpy(s_common[4], S, 64);
    identity(&s_common[8][0]);
    identity(&s_common[12][0]);
}

static void setCommon(void)
{
    RdVuCommon b;
    memcpy(&b, s_common, sizeof(b));
    rd_SetVuCommon(&b);
}

/* ---------------------------------------------------------- the frame
 * Every command of the last frame with the state block in force after it. */
typedef struct Ev {
    int list;
    const RdCmd *cmd;
    RdStateBlock st;
} Ev;

#define MAXEV 16384

static Ev s_ev[MAXEV];

static int s_nev;

static const RdFrame *s_frame;

static void walkCb(void *user, int list, uint32_t index, const RdCmd *cmd,
                   const RdStateBlock *state)
{
    (void)user, (void)index;
    if (s_nev < MAXEV) {
        s_ev[s_nev].list = list;
        s_ev[s_nev].cmd = cmd;
        s_ev[s_nev].st = *state;
        s_nev++;
    }
}

static void collect(void)
{
    s_frame = rd__LastFrame();
    RdStateBlock s = s_frame->startState;
    s_nev = 0;
    rd__Walk(s_frame, s_frame->keep, &s, walkCb, NULL);
}

static const RdScreenVtx *vtxOf(const RdCmd *c)
{
    return (const RdScreenVtx *)(s_frame->payload + c->u[0]);
}

/* the 64-bit word i of a packet */
static uint64_t dw(const char *p, int i)
{
    uint64_t v;
    memcpy(&v, p + 8 * i, 8);
    return v;
}

static uint32_t w32(const char *p, int i)
{
    uint32_t v;
    memcpy(&v, p + 4 * i, 4);
    return v;
}

/* gif_SetAlpha's table (GifPacket.c alphaTable): A, B, C, D */
static const uint8_t kAlphaTable[12][4] = {
    {0, 2, 2, 1}, {2, 0, 2, 1}, {0, 1, 2, 1}, {1, 2, 2, 0}, {0, 1, 0, 1}, {0, 2, 0, 1},
    {2, 0, 0, 1}, {0, 1, 0, 1}, {0, 2, 1, 1}, {2, 0, 1, 1}, {0, 1, 1, 1}, {1, 2, 0, 1},
};

static uint32_t alphaReg(int mode)
{
    const uint8_t *e = kAlphaTable[mode];
    return (uint32_t)(e[0] | e[1] << 2 | e[2] << 4 | e[3] << 6);
}

/* ------------------------------------------------------ the particles */
static PEPackage s_pkg;

static int s_effect = -1;

static void setPackage(int alphaMode)
{
    memset(&s_pkg, 0, sizeof(s_pkg));
    s_pkg.version = 11;
    s_pkg.mode = 0;
    s_pkg.alphaMode = (unsigned int)alphaMode;
    s_pkg.spread = 180;
    s_pkg.speed = 0.15f;
    s_pkg.drag = 1.0f;
    s_pkg.size = 0.5f;
    s_pkg.sizeStepDecay = 1.0f;
    s_pkg.count = 24;
    s_pkg.emit = 200;
    s_pkg.emitStep = 24.0f;
    s_pkg.alpha = 0.6f;
    s_pkg.life = 200;
    s_pkg.col[0] = s_pkg.col[1] = s_pkg.col[2] = s_pkg.col[3] = 128;
    s_pkg.wind = 0.0f;
    SetParticleEffectPackage(0, (int *)&s_pkg, (int)sizeof(s_pkg));
}

static void makeEffect(int alphaMode)
{
    static float pos[4] = {0.0f, 0.0f, 20.0f, 1.0f}, quat[4] = {0, 0, 0, 1};
    if (s_effect >= 0) {
        DeleteParticleEffect(s_effect);
    }
    setPackage(alphaMode);
    s_effect = SetParticleEffect(0, pos, quat);
    for (int i = 0; i < 12; i++) {
        ExecParticleEffects();
    }
}

/* The particle buffer the next DispParticleEffects draws (prim_DispParticle
   alternates the two). */
static const float (*particleBuffer(void))[4]
{
    PEGeo *g = GetParticleEffectData(s_effect);
    return (const float (*)[4])(const void *)g->prim->objs[g->prim->cur]->num;
}

static char *s_pktStart;

static void recordParticles(int alphaMode, int enemy, const uint8_t *bg)
{
    dl_Clear();
    setCommon();
    dl_SetDLPriority(6);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
    s_pktStart = PacketBufferStruct.ptr.c;
    if (enemy) {
        /* enemy.c:312-322: its own packet code */
        PEGeo *g = GetParticleEffectData(s_effect);
        gif_StartPacketPri(6);
        gif_SetAlpha(1, 4, 128);
        gif_EndPacket();
        prim_DispParticle(g->prim, matrixptr + 0x100);
    } else {
        (void)alphaMode;
        DispParticleEffects();
    }
    dl_Swap();
}

static void checkParticleRecording(void)
{
    static const uint32_t kAlpha[3] = {0x44, 0x48, 0x42};
    static const uint8_t bg[4] = {40, 40, 60, 0x80};
    for (int mode = 0; mode < 3; mode++) {
        makeEffect(mode);
        recordParticles(mode, 0, bg);
        /* the packet, hand-decoded (header comment) */
        const char *c = s_pktStart;
        CHECK(w32(c, 0) == 0x10000004u && w32(c, 2) == 0x11000000u && w32(c, 3) == 0x6C038000u,
              "particle packet: cnt 4, FLUSH, UNPACK 3 qw (%08x %08x %08x)", w32(c, 0), w32(c, 2),
              w32(c, 3));
        CHECK(dw(c, 2) == 0x1000000000008002ull && dw(c, 3) == 0xE,
              "particle packet: GIF tag NLOOP 2 EOP PACKED A+D (%016llx)",
              (unsigned long long)dw(c, 2));
        CHECK(dw(c, 4) == 0 && dw(c, 5) == 0x49 && dw(c, 6) == kAlpha[mode] && dw(c, 7) == 0x42,
              "particle packet: PABE 0, ALPHA 0x%x (got 0x%llx)", kAlpha[mode],
              (unsigned long long)dw(c, 6));
        CHECK(w32(c, 16) == 0x15000000u && w32(c, 20) == 0x60000000u,
              "particle packet: MSCALF 0, then ret");
        collect();
        int seen = 0, ok = 0;
        for (int i = 0; i < s_nev; i++) {
            if (s_ev[i].cmd->type == RDC_PARTICLES) {
                seen++;
                ok += s_ev[i].list == 6 && s_ev[i].st.ds.pabe == 0 &&
                      rd__AlphaRegister(s_ev[i].st.ds.blend) == kAlpha[mode] &&
                      s_ev[i].st.ds.abe == 1;
            }
        }
        CHECK(seen == 1 && ok == 1, "alphaMode %d: one batch in list 6 with ALPHA 0x%x (%d/%d)",
              mode, kAlpha[mode], ok, seen);
    }
    /* enemy.c's packet code: mode 4 */
    recordParticles(0, 1, bg);
    collect();
    int ok = 0;
    for (int i = 0; i < s_nev; i++) {
        ok += s_ev[i].cmd->type == RDC_PARTICLES &&
              rd__AlphaRegister(s_ev[i].st.ds.blend) == 0x44 && s_ev[i].st.ds.pabe == 0;
    }
    CHECK(ok == 1, "enemy.c: gif_SetAlpha(1, 4, 128) then prim_DispParticle: ALPHA 0x44 (%d)", ok);
}

/* ------------------------------------------------------ the lightning */
static void recordLightning(int c, float seed, const uint8_t *bg, float x0, float x1)
{
    LightningColor col = {{0xC0, 0xFF, 0xE0, 0x80}};
    /* about 400 x 120 pixels at view depth 200 (400 / 200 = 2 pixels a
       unit); the bolt is flat in depth, so Q is the same at every vertex */
    LightningVtx v[3] = {
        {{x0, -30.0f, 200.0f, 1.0f}},
        {{(x0 + x1) * 0.5f, 5.0f, 200.0f, 1.0f}},
        {{x1, 30.0f, 200.0f, 1.0f}},
    };
    dl_Clear();
    dl_SetDLPriority(6);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
    s_pktStart = PacketBufferStruct.ptr.c;
    DrawLightning2(3, v, &col, 15.0f, 25.0f, 2.0f, 4.0f, 5.0f, 20.0f, 10.0f, 8.0f, 20.0f, seed, c);
    dl_Swap();
}

/* the DIRECT packet DrawLightning2 built: the first qword-aligned block
   after s_pktStart whose VIF words are FLUSH, DIRECT */
static const char *findDirect(void)
{
    for (const char *p = s_pktStart; p < PacketBufferStruct.ptr.c; p += 16) {
        if (w32(p, 2) == 0x11000000u && (w32(p, 3) >> 24) == 0x50 &&
            dw(p, 2) == 0x1400000000008001ull) {
            return p; /* the 2D layer's packets are DIRECT too: this one opens with REGLIST */
        }
    }
    return NULL;
}

typedef struct Vtx3 {
    uint64_t rgbaq, st, xyz;
} Vtx3;

#define MAXSTRIP 4096

static Vtx3 s_sv[MAXSTRIP];

static int s_svN, s_stripStarts[64], s_strips;

/* walks the DIRECT block as the GIF would (REGLIST only, as built) */
static int decodeDirect(const char *pk)
{
    int qwc = (int)(w32(pk, 3) & 0xFFFF);
    const char *p = pk + 16, *e = pk + 16 + 16 * qwc;
    s_svN = 0;
    s_strips = 0;
    while (p < e) {
        uint64_t tag = dw(p, 0), regs = dw(p, 1);
        int nloop = (int)(tag & 0x7FFF), nreg = (int)((tag >> 60) & 0xF);
        if (((tag >> 58) & 3) != 1) {
            return 0;
        }
        p += 16;
        if (nreg == 1 && regs == 0) {
            if (dw(p, 0) != 84) {
                return 0;
            }
        } else if (nreg == 3 && regs == 0x521) {
            s_stripStarts[s_strips++] = s_svN;
            for (int k = 0; k < nloop && s_svN < MAXSTRIP; k++) {
                s_sv[s_svN].rgbaq = dw(p, 3 * k);
                s_sv[s_svN].st = dw(p, 3 * k + 1);
                s_sv[s_svN].xyz = dw(p, 3 * k + 2);
                s_svN++;
            }
        } else {
            return 0;
        }
        p += 16 * ((nloop * nreg + 1) / 2);
    }
    s_stripStarts[s_strips] = s_svN;
    return 1;
}

static void checkLightningRecording(void)
{
    static const uint8_t bg[4] = {40, 40, 60, 0x80};
    for (int c = -1; c <= 12; c++) {
        recordLightning(c, 1.25f, bg, -80.0f, 80.0f);
        collect();
        const int mode = c >= 0 && c < 12 ? c : 0;
        int tris = 0, alphaOk = 1;
        for (int i = 0; i < s_nev; i++) {
            if (s_ev[i].cmd->type == RDC_SCREEN && s_ev[i].list == 6) {
                tris += (int)s_ev[i].cmd->u[1] / 3;
                alphaOk &= rd__AlphaRegister(s_ev[i].st.ds.blend) == alphaReg(mode) &&
                           s_ev[i].st.ds.blendFix == 128 && s_ev[i].st.ds.abe == 1 &&
                           s_ev[i].st.ds.zwrite == RD_ZWRITE_OFF && s_ev[i].st.ds.texEnabled;
            }
        }
        CHECK(alphaOk && tris > 0, "lightning c = %d: ALPHA 0x%x FIX 128 ABE, ZMSK, TME (%d tris)",
              c, alphaReg(mode), tris);
        if (c != 4) {
            continue;
        }
        /* c = 4: the packet against the triangles */
        const char *pk = findDirect();
        CHECK(pk != NULL, "lightning: the DIRECT packet");
        if (!pk) {
            continue;
        }
        CHECK(dw(pk + 16, 0) == 0x1400000000008001ull && dw(pk + 16, 1) == 0 &&
                  dw(pk + 16, 2) == 84,
              "lightning: REGLIST PRIM tag, PRIM 84");
        CHECK(decodeDirect(pk) && s_strips >= 1, "lightning: the packet decodes (%d strips)",
              s_strips);
        int want = 0;
        for (int s = 0; s < s_strips; s++) {
            int n = s_stripStarts[s + 1] - s_stripStarts[s];
            want += n >= 3 ? n - 2 : 0;
        }
        /* every recorded triangle is three consecutive packet vertices */
        int k = 0, match = 1;
        for (int s = 0; s < s_strips; s++) {
            for (int j = s_stripStarts[s]; j + 2 < s_stripStarts[s + 1]; j++) {
                const RdScreenVtx *tv = NULL;
                int seen = 0;
                for (int i = 0; i < s_nev && !tv; i++) {
                    const RdCmd *cm = s_ev[i].cmd;
                    if (cm->type == RDC_SCREEN && s_ev[i].list == 6) {
                        if (k < seen + (int)cm->u[1] / 3) {
                            tv = vtxOf(cm) + 3 * (k - seen);
                        }
                        seen += (int)cm->u[1] / 3;
                    }
                }
                if (!tv) {
                    match = 0;
                    break;
                }
                for (int q = 0; q < 3; q++) {
                    const Vtx3 *pv = &s_sv[j + q];
                    float sf, tf, qf;
                    uint32_t sb = (uint32_t)pv->st, tb = (uint32_t)(pv->st >> 32),
                             qb = (uint32_t)(pv->rgbaq >> 32);
                    memcpy(&sf, &sb, 4);
                    memcpy(&tf, &tb, 4);
                    memcpy(&qf, &qb, 4);
                    match &= tv[q].x == (int32_t)(pv->xyz & 0xFFFF) &&
                             tv[q].y == (int32_t)((pv->xyz >> 16) & 0xFFFF) &&
                             tv[q].z == (uint32_t)(pv->xyz >> 32) && tv[q].s == sf &&
                             tv[q].t == tf && tv[q].q == qf &&
                             memcmp(tv[q].rgba, &pv->rgbaq, 4) == 0;
                }
                k++;
            }
        }
        CHECK(match && k == want && tris == want,
              "lightning: %d recorded triangles are the packet's strips' (%d, %d)", tris, want, k);
    }
}

/* -------------------------------------------------------------- lines */
static const int kLineCol[4] = {250, 30, 90, 0x80};

static const int kLineColB[4] = {20, 240, 160, 0x80};

static void recordLines(const uint8_t *bg)
{
    dl_Clear();
    dl_SetDLPriority(2);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
    gif_StartPacketPri(2);
    gif_SetAlpha(1, 2, 128); /* LERP FIX 0x80: the colour itself */
    /* window pixel (px, py) at 12.4 (0x7000 + 16 px) */
    int a[3] = {0x7000 + 16 * 40, 0x7000 + 16 * 100, 0},
        b[3] = {0x7000 + 16 * 300, 0x7000 + 16 * 100, 0};
    Draw2DLine(a, b, (int *)kLineCol, -1);
    int c0[3] = {0x7000 + 16 * 60, 0x7000 + 16 * 140, 0},
        c1[3] = {0x7000 + 16 * 60, 0x7000 + 16 * 400, 0};
    Draw2DLine(c0, c1, (int *)kLineColB, -1);
    int g0[3] = {0x7000 + 16 * 100, 0x7000 + 16 * 200, 0},
        g1[3] = {0x7000 + 16 * 420, 0x7000 + 16 * 200, 0};
    Draw2DLineG(g0, (int *)kLineCol, g1, (int *)kLineColB, -1);
    Draw2DLineSeg_Start();
    int s0[3] = {0x7000 + 16 * 120, 0x7000 + 16 * 300, 0},
        s1[3] = {0x7000 + 16 * 380, 0x7000 + 16 * 300, 0};
    Draw2DLineSeg_Loop(s0, s1, (int *)kLineColB);
    gif_EndPacket();
    dl_Swap();
}

static void checkLineRecording(void)
{
    static const uint8_t bg[4] = {10, 10, 10, 0x80};
    recordLines(bg);
    collect();
    int lines = 0, flat = 0, gour = 0;
    for (int i = 0; i < s_nev; i++) {
        const RdCmd *c = s_ev[i].cmd;
        if (c->type != RDC_SCREEN || s_ev[i].list != 2) {
            continue;
        }
        CHECK(c->b[0] == RD_PRIM_LINES, "lines are RD_PRIM_LINES (%u)", c->b[0]);
        lines += (int)c->u[1] / 2;
        if (s_ev[i].st.gouraud) {
            gour += (int)c->u[1] / 2;
        } else {
            flat += (int)c->u[1] / 2;
        }
        const RdScreenVtx *v = vtxOf(c);
        for (uint32_t k = 0; k < c->u[1]; k++) {
            /* z = -1 gives Z 0xFFFFFFFF; the segment takes the vertices' Z (0) */
            CHECK(v[k].z == 0xFFFFFFFFu || (v[k].z == 0 && v[k].y == 0x7000 + 16 * 300),
                  "line Z: z = -1 gives 0xFFFFFFFF (%u)", v[k].z);
        }
    }
    CHECK(lines == 4 && flat == 2 && gour == 2, "4 lines, 2 flat (0x142), 2 Gouraud (%d, %d, %d)",
          lines, flat, gour);
    /* DrawLine through _getLine: the projected end points (12.4, as the
       FTOI4 of the projection) */
    dl_Clear();
    dl_SetDLPriority(2);
    gif_StartPacketPri(2);
    float from[4] = {-2.0f, 1.0f, 10.0f, 1.0f}, to[4] = {3.0f, -1.5f, 12.0f, 1.0f};
    MatrixDrive_PushMatrix();
    identity((float *)MatrixDrive_GetMatrix());
    DrawLine(from, to, (void *)kLineCol, 0);
    MatrixDrive_PopMatrix();
    gif_EndPacket();
    dl_Swap();
    collect();
    int found = 0;
    for (int i = 0; i < s_nev; i++) {
        const RdCmd *c = s_ev[i].cmd;
        if (c->type == RDC_SCREEN && s_ev[i].list == 2 && c->u[1] == 2) {
            const RdScreenVtx *v = vtxOf(c);
            double ex[2] = {2048.0 + 400.0 * -2.0 / 10.0, 2048.0 + 400.0 * 3.0 / 12.0};
            double ey[2] = {2048.0 + 400.0 * 1.0 / 10.0, 2048.0 + 400.0 * -1.5 / 12.0};
            double ez[2] = {ZK / 10.0 * 16.0, ZK / 12.0 * 16.0};
            int ok = 1;
            for (int k = 0; k < 2; k++) {
                /* _getLine sorts the ends by Z, X, then Y: either order */
                int hit = 0;
                for (int j = 0; j < 2; j++) {
                    hit |= fabs(v[k].x - ex[j] * 16.0) <= 1.0 &&
                           fabs(v[k].y - ey[j] * 16.0) <= 1.0 &&
                           fabs((double)v[k].z - ez[j]) <= ez[j] * 1e-6;
                }
                ok &= hit;
            }
            found += ok;
        }
    }
    CHECK(found == 1, "DrawLine: the end points of the projection (%d)", found);
}

/* --------------------------------------------------------- dark volume */
#define DV_Z 10.0f /* the volume's centre depth, and the wall's */

static void sceneWall(const uint8_t *bg, const uint8_t *wall, uint32_t zWall, uint32_t zBg)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, zBg);
    gif_StartPacketPri(0);
    gif_SetGsReg(0x47, 0x30000);    /* Z ALWAYS */
    gif_SetGsReg(0x4E, 0x300000C0); /* Z write */
    gif_SetGsReg(0x00, 6);
    gif_SetGsReg(0x01, (long long)wall[0] | (long long)wall[1] << 8 | (long long)wall[2] << 16 |
                           (long long)wall[3] << 24);
    gif_SetGsReg(0x05, 0x7000 | (0x7000LL << 16) | ((long long)zWall << 32));
    gif_SetGsReg(0x05, (0x7000 + 16 * 256) | (0x9000LL << 16) | ((long long)zWall << 32));
    gif_SetGsReg(0x47, 0x50000);
    gif_EndPacket();
}

static const uint8_t kDvBg[4] = {60, 90, 120, 0x40};

static const uint8_t kDvWall[4] = {150, 110, 70, 0x20};

#define DV_ZWALL ((uint32_t)(ZK / DV_Z * 16.0f))
#define DV_ZBG ((uint32_t)(ZK / 1000.0f * 16.0f))

static void recordDark(int gameOver)
{
    static float pos[4] = {0.0f, 0.0f, DV_Z, 1.0f};
    dl_Clear();
    sceneWall(kDvBg, kDvWall, DV_ZWALL, DV_ZBG);
    s_pktStart = PacketBufferStruct.ptr.c;
    if (gameOver) {
        StartQueenAttackEffect(pos, 25.0f);
        DispGameOverEffect();
    } else {
        SetupDarkVolume(pos, 2.4f, 0.9f);
    }
    dl_Swap();
}

static uint32_t s_block;

static void checkDarkRecording(void)
{
    InitGameOverEffect();
    recordDark(0);
    /* packet 1, hand-decoded (header comment) */
    const char *c = s_pktStart;
    static const uint64_t kAd[17][2] = {
        {0x80140, 0x4C},
        {(511ull << 16) | (511ull << 48), 0x40},
        {0x7000 | (0x7000ull << 32), 0x18},
        {0, 0x4A},
        {0x8000000080ull, 0x3B},
        {0x1300000C0ull, 0x4E},
        {0x30000, 0x47},
        {0, 0x49},
        {0x8000000044ull, 0x42},
        {0x406, 0x00},
        {0, 0x01},
        {0x7000 | (0x7000ull << 16) | 0xFFFFFFFF00000000ull, 0x05},
        {0x9000 | (0x9000ull << 16) | 0xFFFFFFFF00000000ull, 0x05},
        {0x50000, 0x47},
        {0x8000000068ull, 0x42},
        {0, 0x46},
    };
    CHECK(w32(c, 2) == 0x11000000u && w32(c, 3) == 0x6C118000u,
          "dark packet 1: FLUSH, UNPACK 17 qw");
    CHECK(dw(c, 2) == 0x1000000000008010ull && dw(c, 3) == 0xE,
          "dark packet 1: GIF tag NLOOP 16 EOP PACKED A+D (%016llx)", (unsigned long long)dw(c, 2));
    int adOk = 1;
    for (int i = 0; i < 16; i++) {
        adOk &= dw(c, 4 + 2 * i) == kAd[i][0] && dw(c, 5 + 2 * i) == kAd[i][1];
    }
    CHECK(adOk, "dark packet 1: the 16 A+D pairs");
    CHECK(w32(c, 4 * 18) == 0x15000000u, "dark packet 1: MSCALF 0");
    CHECK(w32(c, 0) == 0x10000012u, "dark packet 1: cnt 18 (%08x)", w32(c, 0));

    collect();
    const uint32_t scene = rd_Target(RD_TARGET_SCENE).id, aa0 = rd_Target(RD_TARGET_AA0).id;
    int phase = 0, clearInBlock = 0, wrapTris = 0, wrapOk = 1, compOk = 0, aa0Used = 0;
    s_block = 0;
    for (int i = 0; i < s_nev; i++) {
        const Ev *e = &s_ev[i];
        if (e->list != 10) {
            continue;
        }
        aa0Used |= e->st.color == aa0;
        if (e->cmd->type != RDC_SCREEN) {
            continue;
        }
        if (phase == 0) {
            /* the clear sprite: into the block, which is scene-sized */
            const RdTargetRec *t = rd__TargetRec(e->st.color);
            clearInBlock = e->cmd->b[0] == RD_PRIM_SPRITES && e->st.color != scene && t &&
                           t->w == W && t->h == H && e->st.ds.test.ztst == RD_ZTST_ALWAYS &&
                           !e->st.ds.abe;
            s_block = e->st.color;
            phase = 1;
        } else if (phase == 1 && e->st.color == s_block) {
            wrapTris += (int)e->cmd->u[1] / 3;
            wrapOk &= e->st.depth == scene && e->st.ds.colclamp == 0 && e->st.ds.abe &&
                      rd__AlphaRegister(e->st.ds.blend) == 0x68 && e->st.ds.blendFix == 0x80 &&
                      e->st.ds.test.ztst == RD_ZTST_GEQUAL && e->st.ds.zwrite == RD_ZWRITE_OFF &&
                      !e->st.gouraud && rd__WrapApplies(&e->st);
        } else if (e->st.color == scene) {
            const RdTexRec *tr = rd__TexRec(e->st.tex);
            compOk = e->cmd->b[0] == RD_PRIM_SPRITES && e->st.ds.fbmsk == 0xFF000000u &&
                     (e->st.ds.colorMask & 8) == 0 && tr && tr->kind == RD_TEXKIND_TARGET &&
                     tr->target == s_block && tr->view == RD_VIEW_RGB24_TA0 &&
                     e->st.ds.texa == RD_TEXA_80_80_AEM && e->st.ds.colclamp == 1 &&
                     rd__AlphaRegister(e->st.ds.blend) == 0x44;
            phase = 2;
        }
    }
    CHECK(clearInBlock, "dark: the clear sprite draws into a scene-sized block target");
    CHECK(wrapTris > 100 && wrapOk,
          "dark: %d sphere triangles into the block with SCENE's depth, COLCLAMP 0, ALPHA 0x68 "
          "FIX 0x80, Z GEQUAL, ZMSK, flat (%d)",
          wrapTris, wrapOk);
    CHECK(compOk, "dark: the composite into SCENE, alpha masked, the block's RGB24 view, TEXA AEM");
    CHECK(!aa0Used, "dark: AA0 is never bound");
    const RdStateBlock *end = &s_ev[s_nev - 1].st;
    (void)end;
    /* the state list 10 leaves */
    RdStateBlock last;
    int haveLast = 0;
    for (int i = 0; i < s_nev; i++) {
        if (s_ev[i].list == 10) {
            last = s_ev[i].st;
            haveLast = 1;
        }
    }
    CHECK(haveLast && last.ds.zwrite == RD_ZWRITE_ON && last.ds.test.ztst == RD_ZTST_GEQUAL &&
              last.color == scene,
          "dark: list 10 left with ZBUF write on, TEST 0x50000, FRAME 0x40");

    /* the game-over path: sonic, then the dark volume */
    InitGameOverEffect();
    recordDark(1);
    collect();
    int rgbOnly = 0, blocks = 0;
    for (int i = 0; i < s_nev; i++) {
        const Ev *e = &s_ev[i];
        if (e->list != 10 || e->cmd->type != RDC_SCREEN) {
            continue;
        }
        const RdTexRec *tr = rd__TexRec(e->st.tex);
        if (e->st.ds.test.ate && e->st.ds.test.atst == RD_ATST_NEVER &&
            e->st.ds.test.afail == RD_AFAIL_RGB_ONLY && e->st.color == scene && tr &&
            tr->kind == RD_TEXKIND_TARGET && tr->target == scene) {
            rgbOnly++;
        }
        blocks += e->st.color != scene && rd__TargetRec(e->st.color) &&
                  rd__TargetRec(e->st.color)->w == W;
    }
    CHECK(rgbOnly == 1, "sonic: one TEST 0x33001 (RGB_ONLY) sprite reading SCENE (%d)", rgbOnly);
    CHECK(blocks > 0, "sonic: the block target again");
    ResetGameOverEffect();
}

/* --------------------------------------------------------- CPU raster */
typedef void (*PixelFn)(void *user, int x, int y, double z, int tie);

/* GS window samples of pixel (x, y) at (origin + x, origin + y): rd puts GS
   integers on GPU pixel centres.  Edge functions in 12.4 units, exact. */
static void rasterTri(const RdScreenVtx *v, int64_t ox16, int64_t oy16, PixelFn fn, void *user)
{
    const int64_t x0 = v[0].x, y0 = v[0].y, x1 = v[1].x, y1 = v[1].y, x2 = v[2].x, y2 = v[2].y;
    const int64_t area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) {
        return;
    }
    int64_t minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int64_t maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int64_t miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int64_t maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int px0 = (int)((minx - ox16) / 16) - 1, px1 = (int)((maxx - ox16) / 16) + 1;
    int py0 = (int)((miny - oy16) / 16) - 1, py1 = (int)((maxy - oy16) / 16) + 1;
    px0 = px0 < 0 ? 0 : px0;
    py0 = py0 < 0 ? 0 : py0;
    px1 = px1 >= W ? W - 1 : px1;
    py1 = py1 >= H ? H - 1 : py1;
    for (int y = py0; y <= py1; y++) {
        for (int x = px0; x <= px1; x++) {
            const int64_t sx = ox16 + 16 * x, sy = oy16 + 16 * y;
            int64_t e0 = (x2 - x1) * (sy - y1) - (y2 - y1) * (sx - x1);
            int64_t e1 = (x0 - x2) * (sy - y2) - (y0 - y2) * (sx - x2);
            int64_t e2 = (x1 - x0) * (sy - y0) - (y1 - y0) * (sx - x0);
            if (area < 0) {
                e0 = -e0, e1 = -e1, e2 = -e2;
            }
            if (e0 < 0 || e1 < 0 || e2 < 0) {
                continue;
            }
            const int tie = e0 == 0 || e1 == 0 || e2 == 0;
            const double a = (double)(area < 0 ? -area : area);
            const double z = ((double)e0 * v[0].z + (double)e1 * v[1].z + (double)e2 * v[2].z) / a;
            fn(user, x, y, z, tie);
        }
    }
}

static int gsBlendCh(int mode, int cs, int cd, int as, int fix)
{
    const uint8_t *e = kAlphaTable[mode];
    const int sel[3] = {cs, cd, 0};
    const int A = sel[e[0]], B = sel[e[1]], C = e[2] == 0 ? as : (e[2] == 1 ? 0 : fix),
              D = sel[e[3]];
    int r = (((A - B) * C) >> 7) + D;
    return r < 0 ? 0 : (r > 255 ? 255 : r);
}

/* ------------------------------------------------------- the device */
static uint8_t s_gpu[W * H * 4], s_ref[W * H * 4], s_cnt[W * H * 4];

static uint8_t s_mask[W * H], s_layers[W * H];

static int readTarget(uint32_t id, uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    return rd__ReadTarget((RdTarget){id}, dst, W * H * 4, &w, &h) && w == W && h == H;
}

/* the dark volume's count, on the CPU */
typedef struct DarkPix {
    const RdScreenVtx *v;
} DarkPix;

/* the scene's Z: the wall (x < 256) and the background */
static double s_zw = (double)DV_ZWALL, s_zb = (double)DV_ZBG;

static void darkPixel(void *user, int x, int y, double z, int tie)
{
    const RdScreenVtx *v = ((DarkPix *)user)->v;
    const double zb = x < 256 ? s_zw : s_zb;
    const int i = y * W + x;
    if (tie || fabs(z - zb) < 1024.0) {
        s_mask[i] = 1;
    }
    if (z < zb) {
        return; /* GEQUAL fails */
    }
    for (int c = 0; c < 3; c++) {
        s_cnt[i * 4 + c] = (uint8_t)(s_cnt[i * 4 + c] + v[2].rgba[c]);
    }
    s_cnt[i * 4 + 3] = v[2].rgba[3];
}

/* bilinear of the count at the composite's sample of pixel (x, y): texels
   x - 1 and x weighted 1/4 and 3/4 in each axis (UV 4 + 16 x in 12.4); rd
   filters RGB and expands TEXA (AEM: A = 0x80 where RGB is not 0) after,
   the GS before (expandFirst) */
static void compositeRef(int x, int y, int expandFirst, uint8_t out[4], const uint8_t *before)
{
    static const int wq[2] = {4, 12}; /* 16ths */
    int acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            const int tx = x - 1 + i < 0 ? 0 : x - 1 + i, ty = y - 1 + j < 0 ? 0 : y - 1 + j;
            const uint8_t *t = &s_cnt[(ty * W + tx) * 4];
            const int wgt = wq[i] * wq[j];
            for (int c = 0; c < 3; c++) {
                acc[c] += t[c] * wgt;
            }
            acc[3] += ((t[0] | t[1] | t[2]) ? 0x80 : 0) * wgt;
        }
    }
    int tex[4];
    for (int c = 0; c < 4; c++) {
        tex[c] = (acc[c] + 128) >> 8;
    }
    if (!expandFirst) {
        tex[3] = (tex[0] | tex[1] | tex[2]) ? 0x80 : 0;
    }
    /* MODULATE by (128, 128, 128, 80), TCC RGBA; LERP As */
    const int as = (tex[3] * 80) >> 7;
    for (int c = 0; c < 3; c++) {
        const int cs = (tex[c] * 128) >> 7 > 255 ? 255 : (tex[c] * 128) >> 7;
        out[c] = (uint8_t)gsBlendCh(4, cs, before[c], as, 0);
    }
    out[3] = before[3];
}

static void gpuDark(void)
{
    s_zw = (double)DV_ZWALL;
    s_zb = (double)DV_ZBG;
    InitGameOverEffect();
    recordDark(0);
    collect();
    uint32_t block = 0;
    memset(s_cnt, 0, sizeof(s_cnt));
    memset(s_mask, 0, sizeof(s_mask));
    for (int i = 0; i < s_nev; i++) {
        const Ev *e = &s_ev[i];
        if (e->list == 10 && e->cmd->type == RDC_SCREEN && e->st.ds.colclamp == 0) {
            block = e->st.color;
            const RdScreenVtx *v = vtxOf(e->cmd);
            for (uint32_t k = 0; k + 2 < e->cmd->u[1]; k += 3) {
                DarkPix dp = {v + k};
                rasterTri(v + k, (2048 - W / 2) * 16, (2048 - H / 2) * 16, darkPixel, &dp);
            }
        }
    }
    CHECK(block != 0 && readTarget(block, s_gpu), "dark: the block's readback");
    int bad = 0, maxd = 0, masked = 0, drawn = 0;
    for (int i = 0; i < W * H; i++) {
        if (s_mask[i]) {
            masked++;
            continue;
        }
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)s_gpu[i * 4 + c] - (int)s_cnt[i * 4 + c]);
            d = e > d ? e : d;
        }
        drawn += s_cnt[i * 4 + 3] != 0;
        maxd = d > maxd ? d : maxd;
        bad += d != 0;
    }
    printf("  dark count: %d pixels written, %d masked (edge or depth ties), max difference %d, "
           "%d differ\n",
           drawn, masked, maxd, bad);
    CHECK(bad == 0 && drawn > 20000, "dark count: %d pixels differ from the wrapped sum", bad);
    /* the values the nested spheres give where the wall cuts them: 255
       (outer), 128/255/157 (outer and middle), 1/1/1 (all three) */
    int v255 = 0, vMid = 0, vIn = 0;
    for (int i = 0; i < W * H; i++) {
        const uint8_t *p = &s_gpu[i * 4];
        v255 += p[0] == 255 && p[1] == 255 && p[2] == 255;
        vMid += p[0] == 128 && p[1] == 255 && p[2] == 157;
        vIn += p[0] == 1 && p[1] == 1 && p[2] == 1;
    }
    printf("  dark count regions: %d outer, %d middle, %d inner\n", v255, vMid, vIn);
    CHECK(v255 > 100 && vMid > 100 && vIn > 1000, "dark count: the three shells (%d %d %d)", v255,
          vMid, vIn);

    /* SCENE after the composite */
    CHECK(readTarget(rd_Target(RD_TARGET_SCENE).id, s_gpu), "dark: SCENE readback");
    int badRd = 0, maxRd = 0, maxGs = 0, diffGs = 0, alphaBad = 0, cmp = 0;
    for (int y = 1; y < H; y++) {
        for (int x = 1; x < W; x++) {
            const int i = y * W + x;
            if (s_mask[i] || s_mask[i - 1] || s_mask[i - W] || s_mask[i - W - 1]) {
                continue;
            }
            const uint8_t *before = x < 256 ? kDvWall : kDvBg;
            uint8_t rr[4], rg[4];
            compositeRef(x, y, 0, rr, before);
            compositeRef(x, y, 1, rg, before);
            int d = 0, dg = 0;
            for (int c = 0; c < 3; c++) {
                int e = abs((int)s_gpu[i * 4 + c] - (int)rr[c]);
                d = e > d ? e : d;
                e = abs((int)s_gpu[i * 4 + c] - (int)rg[c]);
                dg = e > dg ? e : dg;
            }
            alphaBad += s_gpu[i * 4 + 3] != before[3];
            maxRd = d > maxRd ? d : maxRd;
            badRd += d > 1;
            maxGs = dg > maxGs ? dg : maxGs;
            diffGs += dg > 1;
            cmp++;
        }
    }
    printf("  dark composite: %d pixels compared, max %d from the reference (TEXA after "
           "filtering, as rd), %d over 1; GS order (TEXA before filtering): max %d, %d over 1\n",
           cmp, maxRd, badRd, maxGs, diffGs);
    CHECK(badRd == 0, "dark composite: %d pixels over 1 LSB", badRd);
    CHECK(alphaBad == 0, "dark composite: SCENE alpha written on %d pixels (FRAME PSMCT24)",
          alphaBad);
}

/* The game-over path (DispGameOverEffect: sonic, then the dark volume) at
 * view depth 1000, the wall at the centre's depth over the left half.
 * sonic's spheres (radii 150 and 0) count into the block; its second packet
 * adds the count times (0, 0, 0) into SCENE (RGB unchanged) and writes A =
 * the count's AEM alpha (0x80 where the count is not 0) over the inset
 * rectangle; its third, TEST 0x33001 (alpha test NEVER, AFAIL RGB_ONLY),
 * draws SCENE zoomed (UV 31.5 + 0.939 x) times (245, 255, 245) with LERP As
 * from the sampled SCENE alpha, RGB only.  Then the dark volume (radii 30,
 * 20, 0) and its composite (PSMCT24).  Checked where the CPU model is
 * unambiguous (every texel a pixel's filters read is the same colour and
 * the same count class, away from the dark volume): RGB within 1 LSB, and
 * SCENE's alpha equal to what sonic's second packet wrote, exactly (neither
 * the RGB_ONLY pass nor the PSMCT24 composite writes alpha). */
#define SO_Z 1000.0f
#define SO_ZWALL ((uint32_t)(ZK / SO_Z * 16.0f))
#define SO_ZBG ((uint32_t)(ZK / 100000.0f * 16.0f))

static uint8_t s_cnt2[W * H * 4], s_mask2[W * H], s_a2[W * H], s_a2amb[W * H];

static void gpuSonic(void)
{
    static float pos[4] = {0.0f, 0.0f, SO_Z, 1.0f};
    InitGameOverEffect();
    dl_Clear();
    sceneWall(kDvBg, kDvWall, SO_ZWALL, SO_ZBG);
    StartQueenAttackEffect(pos, 25.0f);
    DispGameOverEffect();
    dl_Swap();
    ResetGameOverEffect();
    collect();
    s_zw = (double)SO_ZWALL;
    s_zb = (double)SO_ZBG;
    /* sonic's spheres are the COLCLAMP 0 commands before the first draw into
       SCENE in list 10, the dark volume's those after */
    const uint32_t scene = rd_Target(RD_TARGET_SCENE).id;
    int group = 0;
    memset(s_cnt, 0, sizeof(s_cnt));
    memset(s_mask, 0, sizeof(s_mask));
    for (int i = 0; i < s_nev; i++) {
        const Ev *e = &s_ev[i];
        if (e->list != 10 || e->cmd->type != RDC_SCREEN) {
            continue;
        }
        if (e->st.color == scene && group == 0) {
            memcpy(s_cnt2, s_cnt, sizeof(s_cnt));
            memcpy(s_mask2, s_mask, sizeof(s_mask));
            memset(s_cnt, 0, sizeof(s_cnt));
            memset(s_mask, 0, sizeof(s_mask));
            group = 1;
        }
        if (e->st.ds.colclamp == 0) {
            const RdScreenVtx *v = vtxOf(e->cmd);
            for (uint32_t k = 0; k + 2 < e->cmd->u[1]; k += 3) {
                DarkPix dp = {v + k};
                rasterTri(v + k, (2048 - W / 2) * 16, (2048 - H / 2) * 16, darkPixel, &dp);
            }
        }
    }
    /* s_cnt2 / s_mask2: sonic's count; s_cnt / s_mask: the dark volume's.
       SCENE alpha after sonic's second packet: pixels x, y >= 32 (XY from
       0x7000 + 500) sample the count at u = 0.25 + (x - 31.25) 8192 / 7692 */
    int sonicPix = 0;
    for (int i = 0; i < W * H; i++) {
        sonicPix += s_cnt2[i * 4] != 0;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int i = y * W + x;
            s_a2amb[i] = 0;
            if (x < 32 || y < 32) {
                s_a2[i] = x < 256 ? kDvWall[3] : kDvBg[3];
                continue;
            }
            const double u = 0.25 + (x - 31.25) * 8192.0 / 7692.0 - 0.5;
            const double v = 0.25 + (y - 31.25) * 8192.0 / 7692.0 - 0.5;
            const int u0 = (int)floor(u), v0 = (int)floor(v);
            int any = 0, all = 1, amb = 0;
            for (int j = 0; j < 2; j++) {
                for (int k = 0; k < 2; k++) {
                    const int tx = u0 + k < 0 ? 0 : (u0 + k >= W ? W - 1 : u0 + k);
                    const int ty = v0 + j < 0 ? 0 : (v0 + j >= H ? H - 1 : v0 + j);
                    const int t = ty * W + tx;
                    const int nz = (s_cnt2[t * 4] | s_cnt2[t * 4 + 1] | s_cnt2[t * 4 + 2]) != 0;
                    any |= nz;
                    all &= nz;
                    amb |= s_mask2[t];
                }
            }
            s_a2amb[i] = (any && !all) || amb;
            s_a2[i] = all ? 0x80 : 0;
        }
    }
    CHECK(readTarget(scene, s_gpu), "sonic: SCENE readback");
    static const uint8_t kMod[3] = {245, 255, 245};
    int cmpA = 0, badA = 0, cmpRgb = 0, badRgb = 0, maxRgb = 0, zoomed = 0;
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            const int i = y * W + x;
            if (s_a2amb[i] || s_mask[i]) {
                continue;
            }
            /* the dark volume's composite: away from its count */
            int nearDark = 0;
            for (int dy = -2; dy <= 2 && !nearDark; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    const int yy = y + dy < 0 ? 0 : (y + dy >= H ? H - 1 : y + dy);
                    const int xx = x + dx < 0 ? 0 : (x + dx >= W ? W - 1 : x + dx);
                    nearDark |= s_cnt[(yy * W + xx) * 4] != 0 || s_cnt[(yy * W + xx) * 4 + 1] != 0;
                }
            }
            if (nearDark) {
                continue;
            }
            cmpA++;
            badA += s_gpu[i * 4 + 3] != s_a2[i];
            /* the RGB_ONLY pass samples SCENE at u = 31.5 + 0.939 x */
            const double u = 504.0 / 16.0 + x * (7692.0 / 8192.0) - 0.5;
            const double v = 504.0 / 16.0 + y * (7692.0 / 8192.0) - 0.5;
            const int u0 = (int)floor(u), v0 = (int)floor(v);
            int uni = 1, side = u0 < 256, a = -1;
            for (int j = 0; j < 2 && uni; j++) {
                for (int k = 0; k < 2; k++) {
                    const int tx = u0 + k, ty = v0 + j;
                    const int t = ty * W + tx;
                    uni &= (tx < 256) == side && !s_a2amb[t] && (a < 0 || a == s_a2[t]);
                    a = s_a2[t];
                }
            }
            if (!uni) {
                continue;
            }
            const uint8_t *tex = side ? kDvWall : kDvBg;
            const uint8_t *cd = x < 256 ? kDvWall : kDvBg;
            int d = 0;
            for (int c = 0; c < 3; c++) {
                int cs = (tex[c] * kMod[c]) >> 7;
                cs = cs > 255 ? 255 : cs;
                const int want = gsBlendCh(4, cs, cd[c], a, 0);
                const int e2 = abs((int)s_gpu[i * 4 + c] - want);
                d = e2 > d ? e2 : d;
            }
            zoomed += a == 0x80;
            cmpRgb++;
            maxRgb = d > maxRgb ? d : maxRgb;
            badRgb += d > 1;
        }
    }
    printf("  sonic: count %d pixels; alpha %d pixels compared, %d differ; RGB_ONLY pass %d "
           "pixels compared (%d zoomed), max difference %d, %d over 1\n",
           sonicPix, cmpA, badA, cmpRgb, zoomed, maxRgb, badRgb);
    CHECK(sonicPix > 2000 && zoomed > 1000, "sonic: the ring covers pixels (%d, %d)", sonicPix,
          zoomed);
    CHECK(badA == 0, "sonic: %d pixels' alpha differ (RGB_ONLY and PSMCT24 keep it)", badA);
    CHECK(badRgb == 0, "sonic: %d RGB_ONLY pixels over 1 LSB", badRgb);
}

/* the lightning, on the CPU: triangles in order, the uniform texel
   modulated by the flat colour, the GS blend */
typedef struct LitPix {
    const RdScreenVtx *v;
    int mode;
} LitPix;

static void litPixel(void *user, int x, int y, double z, int tie)
{
    const LitPix *lp = user;
    const int i = y * W + x;
    (void)z;
    if (tie) {
        s_mask[i] = 1;
    }
    const uint8_t *col = lp->v[2].rgba;
    int cs[4];
    for (int c = 0; c < 4; c++) {
        int m = (kTexel[c] * col[c]) >> 7;
        cs[c] = m > 255 ? 255 : m;
    }
    for (int c = 0; c < 3; c++) {
        s_ref[i * 4 + c] = (uint8_t)gsBlendCh(lp->mode, cs[c], s_ref[i * 4 + c], cs[3], 128);
    }
    s_ref[i * 4 + 3] = (uint8_t)cs[3];
    s_layers[i]++;
}

static int compareLayers(const char *what, int minCover)
{
    int bad = 0, maxd = 0, cover = 0, masked = 0;
    for (int i = 0; i < W * H; i++) {
        if (s_mask[i]) {
            masked++;
            continue;
        }
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)s_gpu[i * 4 + c] - (int)s_ref[i * 4 + c]);
            d = e > d ? e : d;
        }
        if (d > (s_layers[i] > 1 ? s_layers[i] : 1) && getenv("RD_RAW_DEBUG") && bad < 3) {
            printf("    pixel %d,%d (%d layers): gpu %d %d %d %d ref %d %d %d %d\n", i % W, i / W,
                   s_layers[i], s_gpu[i * 4], s_gpu[i * 4 + 1], s_gpu[i * 4 + 2], s_gpu[i * 4 + 3],
                   s_ref[i * 4], s_ref[i * 4 + 1], s_ref[i * 4 + 2], s_ref[i * 4 + 3]);
        }
        maxd = d > maxd ? d : maxd;
        bad += d > (s_layers[i] > 1 ? s_layers[i] : 1);
        cover += s_layers[i] != 0;
    }
    printf("  %s: %d pixels drawn, %d masked (edge ties), max difference %d, %d over 1 a layer\n",
           what, cover, masked, maxd, bad);
    CHECK(bad == 0 && cover >= minCover, "%s: %d pixels over 1 LSB a layer, %d drawn", what, bad,
          cover);
    return bad;
}

static void gpuLightning(int c)
{
    static const uint8_t bg[4] = {70, 70, 90, 0x80};
    recordLightning(c, 1.25f, bg, -100.0f, 100.0f);
    collect();
    for (int i = 0; i < W * H; i++) {
        memcpy(&s_ref[i * 4], bg, 4);
    }
    memset(s_mask, 0, sizeof(s_mask));
    memset(s_layers, 0, sizeof(s_layers));
    for (int i = 0; i < s_nev; i++) {
        const Ev *e = &s_ev[i];
        if (e->list == 6 && e->cmd->type == RDC_SCREEN) {
            const RdScreenVtx *v = vtxOf(e->cmd);
            for (uint32_t k = 0; k + 2 < e->cmd->u[1]; k += 3) {
                LitPix lp = {v + k, c};
                rasterTri(v + k, (2048 - W / 2) * 16, (2048 - H / 2) * 16, litPixel, &lp);
            }
        }
    }
    CHECK(readTarget(rd_Target(RD_TARGET_SCENE).id, s_gpu), "lightning: SCENE readback");
    char what[64];
    snprintf(what, sizeof(what), "lightning c = %d", c);
    compareLayers(what, 500);
}

/* the particles: vu1_ref's sprites of the batch the frame drew */
static void gpuParticles(int alphaMode, int enemy)
{
    static const uint8_t bg[4] = {30, 40, 50, 0x80};
    static const int kMode[3] = {4, 5, 6};
    makeEffect(alphaMode);
    static float buf[8 + 2 * 80][4];
    const PEGeo *g = GetParticleEffectData(s_effect);
    const int n = g->n;
    /* the object after its VIF qword: count, tags, clip box, vertices */
    memcpy(buf, particleBuffer(), (size_t)(g->prim->objSize - 1) * 16);
    recordParticles(alphaMode, enemy, bg);
    static Vu1Ref r;
    float m[8][4];
    memcpy(m[0], matrixptr + 0x100, 64);
    memcpy(m[4], matrixptr + 0xC0, 64);
    vu1ref_Init(&r);
    vu1ref_LoadCommon(&r, (const float (*)[4])s_common);
    vu1ref_ParticleSetMatrix(&r, (const float (*)[4])m);
    static VuParticleOut out;
    vu1ref_Particle(&r, (const float (*)[4])buf, &out);
    const int mode = enemy ? 4 : kMode[alphaMode];
    for (int i = 0; i < W * H; i++) {
        memcpy(&s_ref[i * 4], bg, 4);
    }
    memset(s_mask, 0, sizeof(s_mask));
    memset(s_layers, 0, sizeof(s_layers));
    for (int k = 0; k < out.count; k++) {
        const VuGsSprite *s = &out.s[k];
        const int64_t x0 = s->xyz[0][0] & 0xFFFF, y0 = s->xyz[0][1] & 0xFFFF;
        const int64_t x1 = s->xyz[1][0] & 0xFFFF, y1 = s->xyz[1][1] & 0xFFFF;
        int cs[4];
        for (int c = 0; c < 4; c++) {
            int m2 = (kTexel[c] * (s->rgba[c] & 255)) >> 7;
            cs[c] = m2 > 255 ? 255 : m2;
        }
        for (int y = 0; y < H; y++) {
            const int64_t sy = (2048 - H / 2) * 16 + 16 * y;
            if (sy < y0 || sy >= y1) {
                continue;
            }
            for (int x = 0; x < W; x++) {
                const int64_t sx = (2048 - W / 2) * 16 + 16 * x;
                if (sx < x0 || sx >= x1) {
                    continue;
                }
                const int i = y * W + x;
                for (int c = 0; c < 3; c++) {
                    s_ref[i * 4 + c] = (uint8_t)gsBlendCh(mode, cs[c], s_ref[i * 4 + c], cs[3], 0);
                }
                s_ref[i * 4 + 3] = (uint8_t)cs[3];
                s_layers[i]++;
            }
        }
    }
    CHECK(out.count >= n / 2, "particles: the reference draws %d of %d", out.count, n);
    CHECK(readTarget(rd_Target(RD_TARGET_SCENE).id, s_gpu), "particles: SCENE readback");
    char what[64];
    snprintf(what, sizeof(what), enemy ? "particles (enemy.c, mode 4)" : "particles alphaMode %d",
             alphaMode);
    compareLayers(what, 200);
}

static void gpuLines(void)
{
    static const uint8_t bg[4] = {10, 10, 10, 0x80};
    recordLines(bg);
    CHECK(readTarget(rd_Target(RD_TARGET_SCENE).id, s_gpu), "lines: SCENE readback");

    /* row 100 (x 40..300, flat A), column 60 (y 140..400, flat B), row 200
       (x 100..420, Gouraud A to B), row 300 (x 120..380, flat B: the
       segment's colour) */
    struct {
        int horiz, fixed, from, to, gour;
        const int *c0, *c1;
    } L[4] = {{1, 100, 40, 300, 0, kLineCol, kLineCol},
              {0, 60, 140, 400, 0, kLineColB, kLineColB},
              {1, 200, 100, 420, 1, kLineCol, kLineColB},
              {1, 300, 120, 380, 0, kLineColB, kLineColB}};

    for (int l = 0; l < 4; l++) {
        int drawn = 0, bad = 0, maxd = 0;
        for (int k = L[l].from - 2; k <= L[l].to + 2; k++) {
            const int x = L[l].horiz ? k : L[l].fixed, y = L[l].horiz ? L[l].fixed : k;
            const uint8_t *p = &s_gpu[(y * W + x) * 4];
            if (p[0] == bg[0] && p[1] == bg[1] && p[2] == bg[2]) {
                continue;
            }
            drawn++;
            const double t =
                L[l].gour ? (double)(k - L[l].from) / (double)(L[l].to - L[l].from) : 1.0;
            for (int c = 0; c < 3; c++) {
                const double want =
                    L[l].gour ? L[l].c0[c] + (L[l].c1[c] - L[l].c0[c]) * t : (double)L[l].c1[c];
                const int dd = (int)floor(fabs((double)p[c] - want));
                maxd = dd > maxd ? dd : maxd;
                bad += dd > 1;
            }
        }
        const int len = L[l].to - L[l].from;
        printf("  line %d: %d pixels drawn (length %d), max difference %d\n", l, drawn, len, maxd);
        CHECK(bad == 0 && abs(drawn - len) <= 1, "line %d: %d pixels over 1 LSB, %d drawn of %d", l,
              bad, drawn, len);
    }
}

/* ----------------------------------------------------------- main */
static void setup(void)
{
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
    buildScene();
}

int main(void)
{
    printf("rd_raw_test\n");
    if (!rd__InitRecordOnly(W, H)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    InitParticleEffects();
    InitMatrixDrive();
    setup();
    checkParticleRecording();
    checkLightningRecording();
    checkLineRecording();
    checkDarkRecording();
    CHECK(gif_HostUndecodedTotal() == 0, "%u GS writes undecoded", gif_HostUndecodedTotal());
    rd_Shutdown();
    printf("  recording checks: %s\n", failures ? "FAILED" : "ok");
    if (failures) {
        printf("rd_raw_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(W, H, &st, NULL)) {
        printf("rd_raw_test: recording ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    setup();
    gpuDark();
    gpuSonic();
    gpuLightning(4);
    gpuLightning(5);
    gpuParticles(1, 0);
    gpuParticles(2, 0);
    gpuParticles(0, 1);
    gpuLines();
    printf("  wrap pipelines: %u\n", rd__WrapPipelineCount());
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    if (failures) {
        printf("rd_raw_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_raw_test: ok\n");
    return 0;
}
