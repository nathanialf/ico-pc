/* rd_mesh_test.c: the mesh path (renderer wave 3, R3ab).
 *
 * Packet.c, RegistPacket.c, MicroCode.c, DisplayP2O.c and Primitive.c with
 * the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c) and Matrix.c,
 * compiled as the window build has them (ICO_HOST, ICO_RD); the rest of the
 * game is stubbed below (the texture module by a stand-in that writes the
 * TEX1/TEX0 packet and the UV offset packet tex_TransTexture writes).  The
 * models are synthetic p2o-decoded parts built here; no disc data.
 *
 * Cases:
 *   prelit   a normal_c model (four strips, two VU batches, a strip restart
 *            inside a batch) through p2o_MakePacket and reg_DispObj, region
 *            test (code 32)
 *   scissor  the same model with the clip type that gives code 36: two draws
 *            a batch (cut with ABE forced on, then kick)
 *   cluster  a skinned model (two bones) through reg_dispCObj (code 20)
 *   grid     a Mesh3D through prim_InitMesh3D / prim_UpdateMesh3D /
 *            prim_DispMesh3D (mesh code 20)
 *   particle prim_InitParticleByPartition / prim_DispParticle (code 18), the
 *            batch keyed by its emitter (package I1)
 *
 * Checks on the recording (no device needed): the mesh built from the
 * packet (vertex count, batches, the index list against vu1ref_StaticKicks);
 * the recorded commands (program, code, clip); the VuCB against what the EE
 * packet code uploads (the common block, the UV offset, the matrices +0x140,
 * +0x200 x node and +0x80 x node, the cluster bones and lights); the GS state
 * at the draw (the material packet's ALPHA, CLAMP and FBA, the texture the
 * TEX0 bound, PRIM.ABE from the batch tag); nothing undecoded.
 * Then on a Vulkan device (exit 77 without one, after the recording checks
 * passed): each case rendered into SCENE, and the CPU reference's triangles
 * (vu1_ref.h, the same VU state built independently) drawn through the
 * sprite path (rd_ScreenPrims) in the same GS state; the two images agree
 * within 1 per channel and the draw covers pixels.  Every pipeline created
 * is in the enumerated reachable set. */
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
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "Light.h"
#include "Matrix.h"
#include "MicroCode.h"
#include "Packet.h"
#include "Primitive.h"
#include "RegistPacket.h"

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
struct IosMemPart *ios_partition_seki, *ios_partition_oomori;
void *dmaVif;
char *matrixptr;
int systemStatus[12];
int buffer_ID;
int debug_bounding_flag, debug_specular_flag, debug_shadow_flag, debug_window_flag;
int debug_disp_mesh = 1, debug_disp_particle = 1;
float inflateSec;
int texturetranssize, GlobalTimer, currentScreenWidth;

/* The game heaps: one 16-byte aligned bump arena (no aligned_alloc on
 * mingw; the EE heaps align quadwords), never freed. */
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

static float s_driveMtx[4][4];

float (*MatrixDrive_GetMatrix(void))[4]
{
    return s_driveMtx;
}

/* gsb_ClipBox: every box in view; the value picks the packets' clip result
 * (reg_clipPacketBoundingBox: shade 1 and 1 give code 32, shade 2 and 2 give
 * the scissor code 36) */
static int s_clipRet = 1;

int gsb_ClipBox(float *p)
{
    (void)p;
    return s_clipRet;
}

/* the EE word arena (eeword.h): the cluster tables' bone lists live in it */
static unsigned char s_arena[1 << 16] __attribute__((aligned(16)));

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

/* ------------------------------------------------- the texture stand-in
 * One texture, "testtex" (id 0): a 16 x 16 PSMCT32 image of one colour, so
 * the perspective-correct STQ of the VU path and the affine UVs of the
 * sprite path sample the same texels.  tex_TransTexture writes what
 * Texture.c's host path writes: TEX1 and TEX0 in a GIF packet (the decoder
 * binds the texture the resolver returns), then the record's UV offset
 * packet, which the VU reads (SET_UVOFFSET). */
#define TEST_TBP 0x1000u

static RdTex s_tex;

static unsigned char s_zeroRec[4096] __attribute__((aligned(16)));

static float s_uvPkt[3][4] __attribute__((aligned(16)));

static const float kUv[4] = {0.25f, 0.5f, 0.0f, 0.0f};

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
    return strcmp(name, "testtex") == 0 ? 0 : -1;
}

int tex_GetTextureNum(void)
{
    return 1;
}

static uint32_t fbitsU(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float ubitsF(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int tex_TransTexture(int no, int pri)
{
    (void)no;
    gif_StartPacketPri(pri);
    gif_SetGsReg(0x14, 0); /* TEX1: nearest */
    gif_SetGsReg(6, (long long)TEST_TBP | (1LL << 14) | (4LL << 26) | (4LL << 30) | (1LL << 34));
    gif_EndPacket();
    /* t->uv: FLUSHA, UNPACK 1 qword to TOP, (uOfs, vOfs, 0, 0), MSCALF 2 */
    uint32_t w0[4] = {0, 0, 0x13000000u, 0x6C018000u};
    uint32_t w2[4] = {0x15000002u, 0, 0, 0};
    memcpy(s_uvPkt[0], w0, 16);
    s_uvPkt[1][0] = kUv[0];
    s_uvPkt[1][1] = kUv[1];
    s_uvPkt[1][2] = s_uvPkt[1][3] = 0.0f;
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
    s_tex = rd_CreateTexture(16, 16, px, RD_TEXA_80_80, "testtex");
}

/* --------------------------------------------------------- the scene
 * Matrices as vu1_test.c's: M (world to GS screen) maps (x, y, z) to
 * X = 2048 + 16 x / z, Y = 2048 + 16 y / z, GS Z / 16 = 3 * 2^20 (z - 1) / z,
 * w = z; M2 (clip) x/64, y/64, 2z - 3, w = z; view identity. */
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

static void buildScene(void)
{
    matrixptr = (char *)s_scratch;
    float *M = (float *)(matrixptr + 0x100);
    qw4(M + 0, 16, 0, 0, 0);
    qw4(M + 4, 0, 16, 0, 0);
    qw4(M + 8, 2048, 2048, 3145728, 1);
    qw4(M + 12, 0, 0, -3145728, 0);
    memcpy(matrixptr + 0xC0, M, 64); /* screen matrix (view identity) */
    identity((float *)(matrixptr + 0x80));
    float *M2 = (float *)(matrixptr + 0x200);
    qw4(M2 + 0, 0.015625f, 0, 0, 0);
    qw4(M2 + 4, 0, 0.015625f, 0, 0);
    qw4(M2 + 8, 0, 0, 2, 1);
    qw4(M2 + 12, 0, 0, -3, 0);
    identity((float *)(matrixptr + 0x280));
    identity((float *)(matrixptr + 0x340));
    identity((float *)(matrixptr + 0x380));
    memset(s_common, 0, sizeof(s_common));
    qw4(s_common[0], 0, 0, 0, 1);
    qw4(s_common[1], 4095, 4095, 0, 16777215);
    qw4(s_common[3], ubitsF(0x8000), ubitsF(0x302EC000), ubitsF(0x512), 0);
    memcpy(s_common[4], M, 64);
    float *V = &s_common[8][0];
    qw4(V + 0, 1024, 0, 0, 0); /* viewport, V x M2 = M */
    qw4(V + 4, 0, 1024, 0, 0);
    qw4(V + 8, 0, 0, 1048576, 0);
    qw4(V + 12, 2048, 2048, 1048576, 1);
    identity(&s_common[12][0]);
}

/* gsb_MakeCommonMatrix's block, as GsBase.c's host hook hands it over */
static void setCommon(void)
{
    RdVuCommon b;
    memcpy(&b, s_common, sizeof(b));
    rd_SetVuCommon(&b);
}

static uint32_t s_rng = 1234567u;

static float rnd(float lo, float hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(s_rng >> 8) * (1.0f / 16777216.0f);
}

/* ------------------------------------------------------------- models */

#define STRIPS 4
#define SLEN 24
#define NV (STRIPS * SLEN)

typedef struct Model {
    PObjModel mdl __attribute__((aligned(16)));
    PObjPart part;
    PObjMatDef mat;
    PObjTexDef texDef;
    float vtx[NV][4] __attribute__((aligned(16)));
    float nrm[NV][4] __attribute__((aligned(16)));
    float uv[NV][4] __attribute__((aligned(16)));
    unsigned char col[NV][4];
    short strip[(STRIPS * (SLEN + 1) + 1) * 8];
    void *stripTbl[1];
    float boxes[8][4] __attribute__((aligned(16)));
    struct DObjNode nodes[2];
    float nodeMtx[2][16] __attribute__((aligned(16)));
    float clusterMtx[2][16] __attribute__((aligned(16)));
    LightMatrix light __attribute__((aligned(16)));
    ObjEnt polys[2];
    float w0[NV];
} Model;

static Sub15C s_objA, s_objB;

static Model s_modelA, s_modelB;

/* A ribbon of STRIPS strips of SLEN vertices, x -14..14, rows of 6 units,
 * z 1.9..2.3: in view, and four strips of 72 quadwords make two VU batches
 * (pac_checkDivide's 192-quadword budget) with a restart inside each. */
static void makeModel(Model *m, Sub15C *o, int cluster, int shade)
{
    memset(m, 0, sizeof(*m));
    memset(o, 0, sizeof(*o));
    for (int s = 0, v = 0; s < STRIPS; s++) {
        for (int k = 0; k < SLEN; k++, v++) {
            float x = -14.0f + 28.0f * (float)(k >> 1) / (float)(SLEN / 2 - 1);
            float y = -12.0f + 6.0f * (float)s + 6.0f * (float)(k & 1);
            qw4(m->vtx[v], x + rnd(-0.3f, 0.3f), y + rnd(-0.3f, 0.3f), rnd(1.9f, 2.3f), 1.0f);
            qw4(m->nrm[v], rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), 1.0f);
            qw4(m->uv[v], rnd(0, 1), rnd(0, 1), 0, 0);
            m->col[v][0] = (unsigned char)rnd(20, 250);
            m->col[v][1] = (unsigned char)rnd(20, 250);
            m->col[v][2] = (unsigned char)rnd(20, 250);
            m->w0[v] = rnd(0.2f, 0.8f);
        }
    }
    /* the shape table: per strip a head record (count, the packet offset
     * pac_make*Strip fills) and a record per vertex (vertex, normal, uv,
     * colour indices, material, texture slot); count -1 ends it */
    short *p = m->strip;
    for (int s = 0; s < STRIPS; s++) {
        p[0] = SLEN;
        p += 8;
        for (int k = 0; k < SLEN; k++) {
            int v = s * SLEN + k;
            p[2] = (short)v;
            p[3] = (short)v;
            p[4] = (short)v;
            p[5] = (short)v;
            p[6] = 0;
            p[7] = 0;
            p += 8;
        }
    }
    p[0] = -1;
    m->stripTbl[0] = m->strip;
    m->mat.alpha = 1.0f;
    m->mat.wrap = 1;
    m->mat.fbaOff = 0;
    snprintf(m->texDef.name, sizeof(m->texDef.name), "testtex");
    m->texDef.scaleU = 1.0f;
    m->texDef.scaleV = 1.0f;
    m->part.vtx = (char *)m->vtx;
    m->part.vtxCount = NV;
    m->part.nrm = cluster ? (char *)m->nrm : NULL;
    m->part.nrmCount = cluster ? NV : 0;
    m->part.uv = (char *)m->uv;
    m->part.col = (char *)m->col;
    m->part.mats = &m->mat;
    m->part.matCount = 1;
    m->part.texDefs = &m->texDef;
    m->part.texCount = 1;
    m->part.strips = m->stripTbl;
    m->part.stripCount = 1;
    snprintf(m->mdl.name, sizeof(m->mdl.name), cluster ? "test_cluster" : "test_prelit");
    m->mdl.partCount = 1;
    m->mdl.disp = (signed char)(cluster ? 1 : 0);
    m->mdl.mode.s.shade = (unsigned short)shade;
    m->mdl.mode.s.lod = 0;
    m->mdl.parts = &m->part;
    m->mdl.boxes = (char *)m->boxes;
    for (int i = 0; i < 2; i++) {
        m->nodes[i].scale[0] = m->nodes[i].scale[1] = m->nodes[i].scale[2] = 1.0f;
        identity(m->nodeMtx[i]);
        identity(m->clusterMtx[i]);
    }
    /* node 0 moves the model half a unit right (the matrices the packets
     * carry are products, not copies) */
    m->nodeMtx[0][12] = 0.5f;
    m->nodeMtx[1][12] = 0.5f;
    m->clusterMtx[1][13] = 1.0f; /* bone 1 lifts its vertices a unit */
    o->model = &m->mdl;
    o->nodes = m->nodes;
    o->nodeNum = cluster ? 2 : 1;
    o->nodeMtx = (ICO_WORD)m->nodeMtx;
    o->clusterMtx = (char *)m->clusterMtx;
    o->lightMtx = &m->light;
    o->dispType = 0;
    if (cluster) {
        /* light: L1 = (n.z, n.x, n.y, n.w) as vu1_test.c, L2 colours with an
         * ambient column */
        float *l1 = &m->light.normal[0][0], *l2 = &m->light.color[0][0];
        qw4(l1 + 0, 0, 1, 0, 0);
        qw4(l1 + 4, 0, 0, 1, 0);
        qw4(l1 + 8, 1, 0, 0, 0);
        qw4(l1 + 12, 0, 0, 0, 1);
        qw4(l2 + 0, 0.5f, 0.5f, 0.5f, 0);
        qw4(l2 + 4, 0.25f, 0, 0, 0);
        qw4(l2 + 8, 0, 0.25f, 0, 0);
        qw4(l2 + 12, 0.125f, 0.125f, 0.25f, 0);
        m->light.mode = 1;
        /* cluster table: bone 0 and bone 1 each list every vertex with its
         * weight; the bone lists live in the EE word arena */
        static size_t arenaAt = 16;
        for (int j = 0; j < 2; j++) {
            unsigned char *list = s_arena + arenaAt;
            for (int v = 0; v < NV; v++) {
                int vi = v;
                float w = j == 0 ? m->w0[v] : 1.0f - m->w0[v];
                memcpy(list + v * 16, &vi, 4);
                memcpy(list + v * 16 + 4, &w, 4);
            }
            int end = -1;
            memcpy(list + NV * 16, &end, 4);
            arenaAt += (size_t)(NV + 1) * 16;
            m->polys[j].p = ico_eew(list);
            int bone = j;
            memcpy((char *)&m->polys[j] + 4, &bone, 4);
        }
        m->part.polys = m->polys;
        m->part.polyCount = 2;
    } else {
        m->light.mode = 0; /* normal_c, prelit */
    }
}

/* ------------------------------------------------- the packet's batches */

/* The VU batches of a packet as the VIF unpacks them (the GIF tag, then
 * the vertices), for the reference. */
typedef struct Batches {
    int n;
    const float (*in[16])[4];
} Batches;

static void packetBatches(const PacHeader *pk, Batches *b)
{
    const uint32_t *w = (const uint32_t *)(const void *)pk->data;
    uint32_t nw = pk->size / 4;
    b->n = 0;
    for (uint32_t i = 0; i < nw;) {
        uint32_t code = w[i++];
        if (((code >> 24) & 0x7F) == 0x6C) {
            uint32_t num = (code >> 16) & 0xFF;
            if (b->n < 16) {
                b->in[b->n++] = (const float (*)[4])(const void *)&w[i];
            }
            i += num * 4;
        }
    }
}

/* ------------------------------------------------------- the recording */

typedef struct Found {
    int n;
    const RdCmd *cmd[64];
    RdStateBlock st[64];
    int list[64];
} Found;

static void findVu(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Found *fd = user;
    (void)index;
    if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES && fd->n < 64) {
        fd->cmd[fd->n] = c;
        fd->st[fd->n] = *s;
        fd->list[fd->n] = list;
        fd->n++;
    }
}

static void walkFrame(const RdFrame *f, Found *fd)
{
    memset(fd, 0, sizeof(*fd));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, findVu, fd);
}

static RdVuPayload payloadOf(const RdFrame *f, const RdCmd *c, const float **mem)
{
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    *mem = (const float *)(const void *)(f->payload + c->u[1] + sizeof(p));
    return p;
}

static int memEq(const float *a, const float *b, int qw)
{
    return memcmp(a, b, (size_t)qw * 16) == 0;
}

/* The EE uploads of a normal object: +0x140 = +0x100 x node, +0x200 x node,
 * +0x80 x node (reg_setNMatrixPacket_setMatrix) */
static void normalMatrices(const Model *m, float out[12][4])
{
    float node[16];
    memcpy(node, m->nodeMtx[0], 64);
    _MulMatrix(out[0], matrixptr + 0x100, node);
    _MulMatrix(out[4], matrixptr + 0x200, node);
    _MulMatrix(out[8], matrixptr + 0x80, node);
}

static void checkMesh(const PacHeader *pk, int qpv)
{
    RdMesh m = {pac_HostMesh((PacHeader *)pk)};
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->vu, "a VU mesh for the packet");
    if (!r) {
        return;
    }
    Batches b;
    packetBatches(pk, &b);
    CHECK(r->qwPerVertex == (uint32_t)qpv, "%u quadwords a vertex, expected %d", r->qwPerVertex,
          qpv);
    CHECK(r->vertexCount == NV, "%u vertices, expected %d", r->vertexCount, NV);
    CHECK((int)r->batchCount == b.n && b.n >= 2, "%u batches, the packet has %d", r->batchCount,
          b.n);
    /* the static kicks: vu1ref_StaticKicks over the stream */
    static float stw[NV];
    static int first[NV], kicks[NV];
    int v = 0;
    for (int i = 0; i < b.n; i++) {
        uint32_t tag;
        memcpy(&tag, b.in[i][0], 4);
        for (uint32_t k = 0; k < (tag & 0x7FFF) && v < NV; k++, v++) {
            stw[v] = b.in[i][1 + k * qpv + (qpv - 2)][3];
            first[v] = v - (int)k;
        }
    }
    int nk = vu1ref_StaticKicks(stw, first, v, kicks);
    CHECK(r->indexCount == (uint32_t)nk * 3, "%u indices, %d kicks", r->indexCount, nk);
    for (int i = 0; i < nk && (uint32_t)i * 3 + 2 < r->indexCount; i++) {
        for (int c = 0; c < 3; c++) {
            if (r->index[i * 3 + c] != ICO_VU_INDEX(kicks[i], c)) {
                CHECK(0, "index %d.%d %u, expected %u", i, c, r->index[i * 3 + c],
                      ICO_VU_INDEX(kicks[i], c));
                return;
            }
        }
    }
    /* the stream: the batches' vertices without tags */
    for (int i = 0, at = 0; i < b.n; i++) {
        uint32_t tag;
        memcpy(&tag, b.in[i][0], 4);
        CHECK(memcmp(r->stream[at], b.in[i][1], (size_t)(tag & 0x7FFF) * qpv * 16) == 0,
              "batch %d's vertices in the stream", i);
        at += (int)(tag & 0x7FFF) * qpv;
    }
}

static void checkState(const RdStateBlock *s, int expectAbe, int expectFba)
{
    CHECK(s->ds.texEnabled && s->tex == s_tex.id, "the TEX0 bound the test texture (%u)", s->tex);
    CHECK(s->ds.blend == RD_BLEND_LERP_AS, "material ALPHA 0x44 (%u)", s->ds.blend);
    CHECK(s->ds.abe == expectAbe, "PRIM.ABE %d from the batch tag (%d)", expectAbe, s->ds.abe);
    /* FBA: the material's fbaOff == 0; cluster materials take
     * debug_shadow_flag == 1 instead (pac_makeMaterialTable) */
    CHECK(s->ds.fba == expectFba, "material FBA %d (%d)", expectFba, s->ds.fba);
    /* Packet.c maps the material's wrap 1 to CLAMP 4: S repeat, T clamp */
    CHECK(s->ds.wrap.s == RD_WRAP_REPEAT && s->ds.wrap.t == RD_WRAP_CLAMP,
          "material CLAMP 4 (%u %u)", s->ds.wrap.s, s->ds.wrap.t);
    CHECK(s->gouraud == 1, "PRIM.IIP 1");
}

static void recordPrelit(Sub15C *o, int clipRet)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = clipRet;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(o);
    dl_Swap();
}

static void checkPrelitRecording(int code, int clip)
{
    const RdFrame *f = rd__LastFrame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1, "one mesh draw, got %d", fd.n);
    if (fd.n < 1) {
        return;
    }
    const RdCmd *c = fd.cmd[0];
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    CHECK(c->type == RDC_MESH && fd.list[0] == 0, "RDC_MESH in list 0");
    CHECK(p.prog == RD_PROG_PRELIT && p.code == code && p.clip == clip,
          "normal_c code %d clip %d (got prog %u code %u clip %u)", code, clip, p.prog, p.code,
          p.clip);
    CHECK(p.firstBatch == 0 && p.batchCount == 2, "all batches (%u from %u)", p.batchCount,
          p.firstBatch);
    /* VuCB: the common block with the UV offset in qw 2, then the matrices */
    float want[36][4];
    memcpy(want, s_common, sizeof(s_common));
    want[2][0] = kUv[0];
    want[2][1] = kUv[1];
    float nm[12][4];
    normalMatrices(&s_modelA, nm);
    memcpy(want[16], nm, sizeof(nm));
    CHECK(memEq(mem, want[0], 16), "VuCB 0..15: the common block, UV offset (%g %g)", mem[8],
          mem[9]);
    CHECK(memEq(mem + 64, want[16], 12), "VuCB 16..27: +0x140, +0x200 and +0x80 times the node");
    checkState(&fd.st[0], 0, 1);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

static void recordCluster(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = 1;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(&s_objB);
    dl_Swap();
}

/* the cluster bones the EE packs: nodeMtx[i] x clusterMtx[i] */
static void clusterBones(float out[8][4])
{
    for (int i = 0; i < 2; i++) {
        _MulMatrix(out[i * 4], s_modelB.nodeMtx[i], s_modelB.clusterMtx[i]);
    }
}

static void checkClusterRecording(void)
{
    const RdFrame *f = rd__LastFrame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1, "one skinned draw, got %d", fd.n);
    if (fd.n < 1) {
        return;
    }
    const RdCmd *c = fd.cmd[0];
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    CHECK(c->type == RDC_SKINNED, "RDC_SKINNED");
    CHECK(p.prog == RD_PROG_SKIN && p.code == 20 && p.clip == RD_VU_CLIP_REGION,
          "cluster code 20 (prog %u code %u)", p.prog, p.code);
    CHECK(p.boneQw == 240, "VU memory 16..255 as bones (%u)", p.boneQw);
    float want[16][4];
    memcpy(want, s_common, sizeof(want));
    want[2][0] = kUv[0];
    want[2][1] = kUv[1];
    want[2][3] = 1.0f; /* the fade alpha of reg_setCMatrixPacket */
    CHECK(memEq(mem, want[0], 16), "VuCB 0..15: common block, UV offset, fade alpha");
    CHECK(memEq(mem + 28 * 4, &s_modelB.light.normal[0][0], 4) &&
              memEq(mem + 32 * 4, &s_modelB.light.color[0][0], 4),
          "VuCB 28..35: the light matrices (vf13..vf20)");
    float bones[8][4];
    clusterBones(bones);
    CHECK(memEq(mem + 36 * 4, bones[0], 8), "bones: nodeMtx x clusterMtx at VU 16 and 20");
    checkState(&fd.st[0], 0, 0);
}

/* ---------------------------------------------------------- the grid */

static Mesh3D *s_grid;

static void makeGrid(void)
{
    /* PRIM 0x5C: strip, IIP, TME, ABE (cloth); col2 RGBA 200 150 100 128 */
    s_grid = prim_InitMesh3D(6, 4, 0, 0x5C, 0xC8966480u, 0);
    for (int i = 0; i < s_grid->nx * s_grid->ny; i++) {
        int x = i % s_grid->nx, y = i / s_grid->nx;
        s_grid->pos[i].x = -12.0f + 4.8f * (float)x;
        s_grid->pos[i].y = -10.0f + 6.0f * (float)y;
        s_grid->pos[i].z = 2.0f + 0.05f * (float)(x + y);
        s_grid->pos[i].w = 1.0f;
        s_grid->st[i].x = (float)x / 5.0f;
        s_grid->st[i].y = (float)y / 3.0f;
        s_grid->st[i].z = 1.0f;
    }
    prim_UpdateMesh3D(s_grid, 1 | 8 | 0x10, 0);
    prim_UpdateMesh3D(s_grid, 1 | 8 | 0x10, 1);
}

static void recordGrid(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(s_grid, 0, 0, 0);
    dl_Swap();
}

static void checkGridRecording(void)
{
    const RdFrame *f = rd__LastFrame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_GRID, "one grid draw (%d)", fd.n);
    if (fd.n < 1) {
        return;
    }
    const float *mem;
    RdVuPayload p = payloadOf(f, fd.cmd[0], &mem);
    CHECK(p.prog == RD_PROG_GRID && p.code == 20, "mesh code 20 (prog %u code %u)", p.prog, p.code);
    CHECK(p.batchCount == (uint32_t)s_grid->strips &&
              p.vertsPerBatch == (uint32_t)s_grid->stripLen && p.streamQw == (uint32_t)s_grid->qwc,
          "the Mesh3D buffer whole (%u strips of %u, %u qw)", p.batchCount, p.vertsPerBatch,
          p.streamQw);
    CHECK(memEq(mem + 64, (const float *)(matrixptr + 0x100), 4),
          "VuCB 16..19: SET_MESH_MATRIX's matrix (vf01..vf04)");
    CHECK(mem[8] == kUv[0] && mem[9] == kUv[1], "UV offset from the texture packet");
    CHECK(fd.st[0].ds.abe == 1 && fd.st[0].ds.texEnabled, "PRIM 0x5C: ABE and TME");
    CHECK(fd.st[0].ds.fba == 0, "FBA 0 from prim_DispMesh3D's packet");
}

/* ------------------------------------------------------ the particles */

static PrimParticle *s_part;

#define NPART 12

static void makeParticles(void)
{
    /* x, y, z of the init call: the size scale and the UV step (du, dv) */
    s_part = prim_InitParticleByPartition(NPART, 1.0f, 0.25f, 0.25f, 0, "testtex", 0, NULL);
    /* the same particles in both buffers (prim_DispParticle alternates) */
    for (int i = 0; i < NPART; i++) {
        float a[4] = {rnd(-14, 14), rnd(-12, 12), rnd(1.9f, 2.4f), rnd(0.5f, 1.5f)};
        float b[4] = {rnd(0, 0.75f), rnd(0, 0.75f), (float)(int)rnd(40, 250),
                      i == 3 ? 0.0f : (float)(int)rnd(30, 128)};
        for (int c = 0; c < 2; c++) {
            memcpy(&s_part->objs[c]->vtx[i][0], a, 16);
            memcpy(&s_part->objs[c]->vtx[i][4], b, 16);
        }
    }
}

static void recordParticles(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(6);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    prim_DispParticle(s_part, matrixptr + 0x100);
    dl_Swap();
}

static void checkParticleRecording(void)
{
    const RdFrame *f = rd__LastFrame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_PARTICLES && fd.list[0] == 6,
          "one particle draw in list 6 (%d)", fd.n);
    if (fd.n < 1) {
        return;
    }
    const float *mem;
    RdVuPayload p = payloadOf(f, fd.cmd[0], &mem);
    CHECK(p.prog == RD_PROG_PARTICLE && p.code == 18 && p.vertsPerBatch == NPART &&
              p.streamQw == 6 + 2 * NPART,
          "particle batch of %d (%u)", NPART, p.vertsPerBatch);
    CHECK(memEq(mem + 64, (const float *)(matrixptr + 0x100), 4) &&
              memEq(mem + 80, (const float *)(matrixptr + 0xC0), 4),
          "VuCB 16..23: SET_PARTICLE_MATRIX's two matrices");
    CHECK(fd.st[0].ds.abe == 1, "PRIM 0xD6: ABE");
    /* package I1: keyed by its emitter (mc_HostParticleKey), so the
     * presenter matches it whatever other emitters draw before it */
    const RdKey k = RD_KEY(s_part, 18, 0);
    CHECK(fd.cmd[0]->keyLo == (uint32_t)k && fd.cmd[0]->keyHi == (uint32_t)(k >> 32),
          "the batch is keyed by its emitter (%08x%08x)", fd.cmd[0]->keyHi, fd.cmd[0]->keyLo);
}

/* ------------------------------------------------------ the reference */

#define MAXREF 2048

static RdScreenVtx s_ref[MAXREF];

static int s_nref;

static void refVertex(const VuGsVertex *v)
{
    if (s_nref >= MAXREF) {
        return;
    }
    RdScreenVtx *o = &s_ref[s_nref++];
    o->x = (int32_t)vu_gs_x(v);
    o->y = (int32_t)vu_gs_y(v);
    o->z = (uint32_t)v->xyz[2];
    o->s = v->stq[0];
    o->t = v->stq[1];
    o->q = v->stq[2];
    for (int i = 0; i < 4; i++) {
        o->rgba[i] = (uint8_t)(v->rgba[i] & 255);
    }
}

static void refBatch(const VuBatchOut *out)
{
    for (int f = 0; f < out->fanCount; f++) {
        const VuGsFan *fan = &out->fans[f];
        for (int i = 2; i < fan->n; i++) {
            refVertex(&fan->v[0]);
            refVertex(&fan->v[i - 1]);
            refVertex(&fan->v[i]);
        }
    }
    static int kicks[VU_BATCH_MAX];
    int nk = vu1ref_Kicks(out, kicks);
    for (int i = 0; i < nk; i++) {
        for (int j = 0; j < 3; j++) {
            refVertex(&out->v[kicks[i] - 2 + j]);
        }
    }
}

static void refCommon(Vu1Ref *r)
{
    vu1ref_Init(r);
    vu1ref_LoadCommon(r, (const float (*)[4])s_common);
    vu1ref_SetUVOffset(r, kUv);
}

static void refPrelit(const PacHeader *pk, int code)
{
    static Vu1Ref r;
    float m[12][4];
    refCommon(&r);
    normalMatrices(&s_modelA, m);
    vu1ref_NormalSetMatrix(&r, (const float (*)[4])m);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_NormalC(&r, code, b.in[i], &out);
        refBatch(&out);
    }
}

static void refCluster(const PacHeader *pk)
{
    static Vu1Ref r;
    float pk2[10][4];
    memset(pk2, 0, sizeof(pk2));
    float bones[8][4], light[8][4];
    qw4(pk2[0], ubitsF(9), 0, 0, 1.0f);
    clusterBones(bones);
    memcpy(pk2[1], bones, sizeof(bones));
    memcpy(light[0], s_modelB.light.normal, 64);
    memcpy(light[4], s_modelB.light.color, 64);
    refCommon(&r);
    vu1ref_ClusterSetMatrix(&r, (const float (*)[4])pk2);
    vu1ref_ClusterSetLight(&r, (const float (*)[4])light);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_Cluster(&r, 20, b.in[i], &out);
        refBatch(&out);
    }
}

static void refGrid(void)
{
    static Vu1Ref r;
    refCommon(&r);
    vu1ref_MeshSetMatrix(&r, (const float (*)[4])(matrixptr + 0x100));
    s_nref = 0;
    static VuBatchOut out;
    const float (*buf)[4] = s_grid->bufs[0];
    const int per = s_grid->stripLen * 2 + 4;
    for (int i = 0; i < s_grid->strips; i++) {
        vu1ref_Mesh(&r, 20, buf + i * per + 1, &out);
        refBatch(&out);
    }
}

static void refParticles(void)
{
    static Vu1Ref r;
    float m[8][4];
    memcpy(m[0], matrixptr + 0x100, 64);
    memcpy(m[4], matrixptr + 0xC0, 64);
    refCommon(&r);
    vu1ref_ParticleSetMatrix(&r, (const float (*)[4])m);
    VuParticleOut out;
    vu1ref_Particle(&r, (const float (*)[4])(const void *)s_part->objs[0]->num, 6 + 2 * NPART,
                    &out);
    s_nref = 0;
    for (int i = 0; i < out.count && s_nref + 2 <= MAXREF; i++) {
        const VuGsSprite *s = &out.s[i];
        for (int c = 0; c < 2; c++) {
            RdScreenVtx *o = &s_ref[s_nref++];
            o->x = s->xyz[c][0] & 0xFFFF;
            o->y = s->xyz[c][1] & 0xFFFF;
            o->z = (uint32_t)s->xyz[c][2];
            o->s = s->st[c][0];
            o->t = s->st[c][1];
            o->q = 1.0f;
            for (int k = 0; k < 4; k++) {
                o->rgba[k] = (uint8_t)(s->rgba[k] & 255);
            }
        }
    }
    CHECK(out.count == NPART - 1, "the reference draws %d particles (one has alpha 0)", out.count);
}

/* The GS state of the mesh draw, recorded as rd state for the reference. */
static void applyState(const RdStateBlock *s)
{
    rd_Test(&s->ds.test);
    rd_ZWrite(s->ds.zwrite == RD_ZWRITE_ON);
    rd_FBA(s->ds.fba);
    rd_PABE(s->ds.pabe);
    rd_ColClamp(s->ds.colclamp);
    rd_TexA((RdTexA)s->ds.texa);
    rd_Blend((RdBlend)s->ds.blend, s->ds.blendFix, s->ds.abe);
    rd_SamplerFilter((RdFilter)s->ds.magFilter, (RdFilter)s->ds.minFilter);
    rd_SamplerWrap((RdWrap)s->ds.wrap.s, (RdWrap)s->ds.wrap.t);
    if (s->ds.texEnabled) {
        rd_Texture((RdTex){s->tex}, (RdTexFn)s->ds.texFn, (RdTcc)s->ds.tcc);
    } else {
        rd_TextureOff();
    }
    rd_Gouraud((int)s->gouraud);
    rd_ColorMask(s->ds.fbmsk);
    rd_UVOffset(0.0f, 0.0f);
}

static uint8_t s_imgA[512 * 512 * 4], s_imgB[512 * 512 * 4];

static int readScene(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    return rd__ReadTarget(rd_Target(RD_TARGET_SCENE), dst, 512 * 512 * 4, &w, &h) && w == 512 &&
           h == 512;
}

/* Frame 2: the reference triangles (or sprites) in the state the mesh draw
 * had, over the same clear. */
static void drawReference(const RdStateBlock *st, int list, RdPrim prim)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(list);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    applyState(st);
    rd_ScreenPrims(prim, s_ref, (uint32_t)s_nref, RD_SPACE_WORLD, 0, 0);
    dl_Swap();
}

static void compare(const char *what, int minCover)
{
    int bad = 0, maxd = 0, cover = 0;
    for (int i = 0; i < 512 * 512; i++) {
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)s_imgA[i * 4 + c] - (int)s_imgB[i * 4 + c]);
            d = e > d ? e : d;
        }
        maxd = d > maxd ? d : maxd;
        bad += d > 1;
        cover += s_imgA[i * 4] != 40 || s_imgA[i * 4 + 1] != 40 || s_imgA[i * 4 + 2] != 60;
    }
    printf("  %s: %d pixels drawn, max difference %d, %d over 1\n", what, cover, maxd, bad);
    CHECK(bad == 0, "%s: %d pixels differ from the reference by more than 1 (max %d)", what, bad,
          maxd);
    CHECK(cover >= minCover, "%s: only %d pixels drawn", what, cover);
}

/* One case on the device: the game path, then the reference. */
static void gpuCase(const char *what, void (*record)(void), void (*ref)(void), RdPrim prim,
                    int minCover)
{
    record();
    if (!readScene(s_imgA)) {
        CHECK(0, "%s: SCENE readback", what);
        return;
    }
    Found fd;
    walkFrame(rd__LastFrame(), &fd);
    if (fd.n < 1) {
        CHECK(0, "%s: nothing recorded", what);
        return;
    }
    const RdStateBlock st = fd.st[0];
    const int list = fd.list[0];
    ref();
    drawReference(&st, list, prim);
    if (!readScene(s_imgB)) {
        CHECK(0, "%s: SCENE readback", what);
        return;
    }
    compare(what, minCover);
}

static PacHeader *packetA(void)
{
    return (PacHeader *)s_modelA.mdl.groups->packets;
}

static PacHeader *packetB(void)
{
    return (PacHeader *)s_modelB.mdl.groups->packets;
}

static void recPrelit(void)
{
    recordPrelit(&s_objA, 1);
}

/* the packet's clip type 2 (the model's shade) with gsb_ClipBox 2: code 36 */
static void recScissor(void)
{
    packetA()->clip = 2;
    recordPrelit(&s_objA, 2);
    packetA()->clip = 1;
}

static void refPrelit32(void)
{
    refPrelit(packetA(), 32);
}

static void refPrelit36(void)
{
    refPrelit(packetA(), 36);
}

static void refClusterA(void)
{
    refCluster(packetB());
}

/* ----------------------------------------------------------- the setup */

static void setup(void)
{
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
}

static void buildModels(void)
{
    s_rng = 1234567u;
    buildScene();
    makeModel(&s_modelA, &s_objA, 0, 1);
    makeModel(&s_modelB, &s_objB, 1, 1);
    p2o_MakePacket(&s_objA);
    p2o_MakePacket(&s_objB);
    makeGrid();
    makeParticles();
}

static void recordingChecks(void)
{
    CHECK(packetA() && packetA()->next == NULL, "one packet for the prelit model");
    CHECK(packetB() && packetB()->next == NULL, "one packet for the cluster model");
    if (!packetA() || !packetB()) {
        return;
    }
    checkMesh(packetA(), RD_VU_QW_PRELIT);
    checkMesh(packetB(), RD_VU_QW_SKIN);

    recordPrelit(&s_objA, 1);
    checkPrelitRecording(32, RD_VU_CLIP_REGION);
    recScissor();
    checkPrelitRecording(36, RD_VU_CLIP_SCISSOR);
    recordCluster();
    checkClusterRecording();
    recordGrid();
    checkGridRecording();
    recordParticles();
    checkParticleRecording();
}

int main(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    setup();
    buildModels();
    recordingChecks();
    rd_Shutdown();
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: recording ok\n");

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("rd_mesh_test: SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    setup();
    buildScene();
    gpuCase("prelit 32", recPrelit, refPrelit32, RD_PRIM_TRIANGLES, 2000);
    gpuCase("prelit 36 (scissor)", recScissor, refPrelit36, RD_PRIM_TRIANGLES, 2000);
    gpuCase("cluster 20", recordCluster, refClusterA, RD_PRIM_TRIANGLES, 2000);
    gpuCase("grid 20", recordGrid, refGrid, RD_PRIM_TRIANGLES, 2000);
    gpuCase("particle 18", recordParticles, refParticles, RD_PRIM_SPRITES, 200);

    /* every pipeline created is in the enumerated reachable set */
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    rd__EnumerateReachable(keys, 512);
    printf("  pipelines: %u created, %u reachable (%u screen and post)\n", rd__PipelineCount(), n,
           ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX && ns < 250, "reachable pipelines %u (screen %u)", n, ns);
    for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u blend %u z %u/%u) is not enumerated", i,
              k->gs.program, k->vs, k->gs.blend, k->gs.ztst, k->gs.zwrite);
    }
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: ok\n");
    return 0;
}
