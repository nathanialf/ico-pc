/* mesh_env.h: what the mesh tests share (rd_mesh_test.c, modelpack_test.c):
 * the game symbols Packet.c, RegistPacket.c, MicroCode.c, DisplayP2O.c,
 * Primitive.c and the 2D layer import, stubbed; the texture stand-in; the
 * scene's matrices and common block; the walk of a packet's VU batches and
 * of the recorded frame; a packet's desc as pac_hostStream builds it; the
 * SCENE readback.  Header only, included once by a test after the game
 * headers (typedef.h, DisplayP2O.h, Packet.h, ...), rd_internal.h,
 * rd_mesh.h and vu_models.h. */
#ifndef PORT_RENDER_TEST_MESH_ENV_H
#define PORT_RENDER_TEST_MESH_ENV_H

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

/* port/game/title_logo.c: RegistPacket.c's reg_DispObj asks it; no title
   here */
int ico_title_logo_skip(const char *model)
{
    (void)model;
    return 0;
}

/* port/game/title_logo.c: reg_DispObj's full-width title models; the
   model a case names (none unless one does) */
static const char *s_stretchModel;

int ico_title_stretch_model(const char *model)
{
    return s_stretchModel != NULL && strcmp(model, s_stretchModel) == 0;
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

static int readScene(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    return rd__ReadTarget(rd_Target(RD_TARGET_SCENE), dst, 512 * 512 * 4, &w, &h) && w == 512 &&
           h == 512;
}

/* a packet's desc as pac_hostStream builds it: every UNPACK payload back
 * to back, the packet's material, group 0 */
typedef struct PkDesc {
    float qw[1024][4];
    RdVuBatchDesc b[16];
    RdVuMeshDesc d;
} PkDesc;

static void packetDesc(const PacHeader *pk, int qpv, PkDesc *o)
{
    Batches b;
    packetBatches(pk, &b);
    memset(o, 0, sizeof(*o));
    uint32_t at = 0;
    for (int i = 0; i < b.n; i++) {
        uint32_t tag;
        memcpy(&tag, b.in[i][0], 4);
        const uint32_t n = 1 + (tag & 0x7FFF) * (uint32_t)qpv;
        if (at + n > 1024) {
            break;
        }
        memcpy(o->qw[at], b.in[i], (size_t)n * 16);
        o->b[i].firstQw = at;
        o->b[i].material = (uint16_t)pk->mat;
        at += n;
    }
    o->d.qw = (const float (*)[4])o->qw;
    o->d.qwCount = at;
    o->d.qwPerVertex = (uint32_t)qpv;
    o->d.batchCount = (uint32_t)b.n;
    o->d.batches = o->b;
    o->d.materialCount = 1;
    o->d.debugName = "packet";
}

#endif
