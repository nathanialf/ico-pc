/*
 * port/game/test/collision_carrier_test.c
 *
 * A display object whose model has no parts but which carries a collision
 * (ico2/common/src/DObj.c initPolygonState, the PC-port block at its end):
 * DObj.c, the game's allocator (fumi/ios/memory.c, over the EE RAM arena)
 * and the field collision (fumi/src/fieldCollision.c) compiled in, with the
 * game's matrix stack and tables (sugipon/src/matrixDrive.c, quaternion.c,
 * tableSin.c); the character files, the motion set-up, the object table and
 * the debug layer stubbed below.  The objects are made as
 * CreateLayoutedGObj makes them: the GObj first, then CSVSYSTEM_InitDObj.
 *
 *   synth <slug>   (every disc's arms of CSVSYSTEM_InitDObj; nothing read)
 *     carrier     a 0-part model with a one-floor collision (a floor at
 *                 local y 0 across the 32 x 32 block grid): nodeNum 0, a
 *                 64-byte node matrix block holding the layout's matrix
 *                 (identity for the row at the origin; for a row moved and
 *                 turned about y, the row's), the one `dobj:` line with the
 *                 model, the layout row and the collision path; a floor
 *                 probe (the 10000-long ray down GetLowerPlaneCollision
 *                 casts, motionManager2.c) hits y 0, and for the moved row
 *                 the row's y
 *     shipped     the same object with its collision attached after the
 *                 create (so the block never runs: what the original does):
 *                 its node matrices are a 0-byte block whose first 16 bytes
 *                 are the allocator's next block header's tag, and no probe
 *                 finds the floor
 *     unchanged   a one-part model with a collision, a 0-part model without
 *                 one and a 0-part model with a skeleton and a collision:
 *                 the allocated blocks of both partitions after the create
 *                 (sizes, in order) are the ones made with the collision
 *                 attached after the create, and no line; the carrier's are
 *                 those plus one 64-byte block
 *     row         an object made while the newest object already has its
 *                 display object (not a layout row's create): the line says
 *                 layout row -1
 *
 *   disc <slug> <image>   (skipped, 77, without the image)
 *     gate        DATA.DF holds exactly one model member with no parts
 *                 (.p2o .p2c .p2g .plo .p2v .p2s, a PS2O header with object
 *                 count 0), STG8TEST.DF's object/sdf/8test/model/st08a_p1.p2o
 *                 (48 bytes) with this disc's model id; none on the June 2001
 *                 demo, which has no STG8TEST.DF
 *     floor       the pack's object/sdf/8test/collis/st08a.cl, relocated as
 *                 ReadCollisionFile does, on that model's carrier at the
 *                 room's layout row (the origin, no turn): the probe at the
 *                 origin finds y 0, the probe from camera zone 20's box top
 *                 (1111, -1990, 81) the ramp at y -1323.67; with the
 *                 collision attached after the create neither finds a floor
 *
 * CPU only.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "typedef.h"
#include "eeword.h"
#include "DisplayP2O.h"
#include "Light.h"
#include "charFileManager.h"
#include "fieldCollision.h"
#include "memory.h"
#include "sceneManager.h"
#include "DObj.h"
#include "tableSin.h"

#include "../../test/ico_check.h"
#include "../../core/region.h"
#include "arena.h"
#include "df_pack.h"
#include "vfs.h"

/* matrixDrive.c: the stack, the sine tables and the quaternion drive */
void InitMatrixDrive(void);

/* -------------------------------------------------- the game's neighbours */
struct IosMemPart *ios_partition_seki;
struct IosMemPart *ios_partition_sugipon;
PObjMdl modelData[2048];
int ScreenHeight;
int ScreenWidth;
int debug_font_flag;
int frame_count;
int game_pause;
int graphics_ready;

static int s_asserts;

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a;
    (void)b;
    (void)c;
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    s_asserts++;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d: %s\n", file, line, mes);
    s_asserts++;
}

void FlushCache(int operation)
{
    (void)operation;
}

int file_LoadFile(void **adr, char *fname, int area)
{
    (void)adr;
    (void)fname;
    (void)area;
    return 0;
}

void DrawLineG(void *from, void *fromColor, void *to, void *toColor, int z)
{
    (void)from;
    (void)fromColor;
    (void)to;
    (void)toColor;
    (void)z;
}

void gif_StartPacketPri(int pri)
{
    (void)pri;
}

void gif_EndPacket(void) {}

void gif_SetAlpha(long long alpha, long long mode, long long fix)
{
    (void)alpha;
    (void)mode;
    (void)fix;
}

void gif_SetZTest(int on)
{
    (void)on;
}

/* the motion and geometry set-up CSVSYSTEM_InitDObj calls: nothing of it
   bears on the node matrices or the collision */
void InitMotionGeoInfo(struct MotRoot *self, float x, float y, float z, float rx, float ry,
                       float rz)
{
    (void)self;
    (void)x;
    (void)y;
    (void)z;
    (void)rx;
    (void)ry;
    (void)rz;
}

void InitMotionStateInfo(struct MotCtrl *self)
{
    (void)self;
}

void InitFrameDependSequence(void *flags)
{
    (void)flags;
}

void InitMotionRotElem(int *elem, int count)
{
    (void)elem;
    (void)count;
}

void SetMotionDirection(GObj *self, float *dir)
{
    (void)self;
    (void)dir;
}

void _ApplyRyGV(float *vec, float ang)
{
    (void)vec;
    (void)ang;
}

void GetInitialSkeltonMatrixByDObj(struct Sub15C *mdl)
{
    (void)mdl;
}

void GetInitialInverseMatrixByDObj(char *mat, struct Sub15C *mdl)
{
    (void)mat;
    (void)mdl;
}

void LocalizeGeometry(struct GObj *gobj, struct ObjNode *link)
{
    (void)gobj;
    (void)link;
}

void GlobalizeGeometry(struct GObj *gobj)
{
    (void)gobj;
}

/* --------------------------------------------------- the character files */
static struct {
    PObjModel *model;
    SkelNode *skel;
    FcColl *coll;
} s_char;

/* charFileManager.c's: the model, skeleton and collision of the id */
void CSVSYSTEM_ReadCharFiles(struct Sub15C *rec, int id)
{
    int n = 0;

    rec->modelId = id;
    rec->model = s_char.model;
    rec->shadow = 0;
    rec->skel = s_char.skel;
    rec->colData = (ICO_WORD)s_char.coll;
    if (rec->skel != 0) {
        while (rec->skel[n].mirror != -1) {
            n++;
        }
        rec->skelNodeNum = n;
        rec->morphNum = rec->skel[n].kind < 0 ? 0 : rec->skel[n].kind;
    }
}

/* ------------------------------------------------------ the object table */
#define OBJS 2
static GObj s_obj[OBJS];
static int s_count;

void *isysGObjGetExist_begin(void)
{
    for (int i = 0; i < OBJS; i++) {
        if (s_obj[i].self != 0) {
            return &s_obj[i];
        }
    }
    return 0;
}

void *isysGObjGetExist_next(struct GObj *start)
{
    for (int i = (int)(start - s_obj) + 1; i < OBJS; i++) {
        if (s_obj[i].self != 0) {
            return &s_obj[i];
        }
    }
    return 0;
}

/* GobjProc.c's table: the objects in the order they were made */
int GetMaxGObj(void)
{
    return s_count;
}

GObj *GetGObjP(int idx)
{
    return idx >= 0 && idx < s_count ? &s_obj[idx] : 0;
}

/* ------------------------------------------------------------- the log */
static char s_dir[1024] = ".";
static const char *s_slug = "pal";
static int s_errSaved = -1;
static char s_log[4096];

static void logPath(char *p, size_t n)
{
    /* one file per run: the per-disc runs go in parallel */
    snprintf(p, n, "%s/collision_carrier_test_%s_%d_stderr.txt", s_dir, s_slug, (int)getpid());
}

static void logStart(void)
{
    char p[1200];

    logPath(p, sizeof(p));
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static void logEnd(void)
{
    char p[1200];

    logPath(p, sizeof(p));
    fflush(stderr);
    dup2(s_errSaved, 2);
    close(s_errSaved);
    clearerr(stderr);
    s_log[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(s_log, 1, sizeof(s_log) - 1, f);
        s_log[got] = '\0';
        fclose(f);
    }
    remove(p);
}

/* ------------------------------------------------------------- the heap */
static IosMemPart *s_root;

static void heapInit(void)
{
    if (ico_arena_init() != 0) {
        fprintf(stderr, "FAIL: no arena\n");
        exit(1);
    }
    s_root = iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    ios_partition_seki = iosMallocSetPartition(s_root, 4 << 20, 16);
    ios_partition_sugipon = iosMallocSetPartition(s_root, 1 << 20, 16);
    if (ios_partition_seki == 0 || ios_partition_sugipon == 0) {
        fprintf(stderr, "FAIL: no partitions\n");
        exit(1);
    }
}

static void heapReset(void)
{
    iosMallocResetPartition(ios_partition_seki);
    iosMallocResetPartition(ios_partition_sugipon);
}

/* the allocated blocks of both partitions, in order: their sizes in
   quadwords (the free blocks between and after them left out) */
typedef struct {
    int n;
    int size[256];
} Blocks;

static void blocksOf(IosMemPart *part, Blocks *b)
{
    for (IosMemNode *n = (IosMemNode *)part->start; n != 0 && b->n < 256; n = n->next) {
        if (memcmp(n->tag, "<ALLOC>________", 16) == 0) {
            b->size[b->n++] = n->size;
        }
    }
}

static void snapshot(Blocks *b)
{
    b->n = 0;
    blocksOf(ios_partition_seki, b);
    b->size[b->n++] = -1; /* the partitions' boundary */
    blocksOf(ios_partition_sugipon, b);
}

static int sameBlocks(const Blocks *a, const Blocks *b)
{
    return a->n == b->n && memcmp(a->size, b->size, sizeof(int) * (size_t)a->n) == 0;
}

/* a is b with one more block of `qw` quadwords somewhere */
static int oneMoreBlock(const Blocks *a, const Blocks *b, int qw)
{
    int i = 0;

    if (a->n != b->n + 1) {
        return 0;
    }
    while (i < b->n && a->size[i] == b->size[i]) {
        i++;
    }
    return a->size[i] == qw &&
           memcmp(&a->size[i + 1], &b->size[i], sizeof(int) * (size_t)(b->n - i)) == 0;
}

/* the block header in front of an allocation */
static const IosMemNode *headerOf(const void *p)
{
    return (const IosMemNode *)((const char *)p - sizeof(IosMemNode));
}

/* -------------------------------------------------------- the models */
static LightMatrix s_light;
static Sub15C s_proto; /* the model's own display object: its light mode */
static PObjPart s_parts[1];
static PObjModel s_model0; /* no parts */
static PObjModel s_model1; /* one part */
static SkelNode s_skel[3]; /* two nodes, then the end */

static void modelsInit(void)
{
    s_light.mode = 0;
    s_proto.lightMtx = &s_light;
    memset(s_parts, 0, sizeof(s_parts));
    memset(&s_model0, 0, sizeof(s_model0));
    s_model0.dobj = &s_proto;
    s_model0.partCount = 0;
    s_model0.disp = 0;
    s_model0.parts = s_parts; /* initPolyHead reads the first part's line count */
    s_model1 = s_model0;
    s_model1.partCount = 1;
    memset(s_skel, 0, sizeof(s_skel));
    s_skel[0].mirror = 0;
    s_skel[0].pos[1] = 72.0f;
    s_skel[0].parent = -1;
    s_skel[1].mirror = 1;
    s_skel[1].kind = 1;
    s_skel[1].parent = 0;
    s_skel[2].mirror = -1;
    s_skel[2].kind = 0;
}

/* -------------------------------------------------------- the collisions */
/* one floor at local y `y`, facing up (-y), over x and z -8000 .. 8000, in
   every cell of the 32 x 32 grid of 512 from -8192 */
static FcColl *floorSet(float y)
{
    IosMemPart *s = ios_partition_seki;
    FcColl *c = iosMallocDebug(s, sizeof(FcColl), "test.c", 1);
    FcFloorEnt *f = iosMallocDebug(s, sizeof(FcFloorEnt), "test.c", 2);
    int *wblk = iosMallocDebug(s, 1024 * sizeof(int), "test.c", 3);
    int *fblk = iosMallocDebug(s, 1024 * sizeof(int), "test.c", 4);
    short *list = iosMallocDebug(s, 2 * sizeof(short), "test.c", 5);
    float *ofs = iosMallocDebug(s, 4 * sizeof(float), "test.c", 6);
    static const float corner[4][2] = {{-8000, -8000}, {8000, -8000}, {8000, 8000}, {-8000, 8000}};

    memset(c, 0, sizeof(*c));
    memset(f, 0, sizeof(*f));
    for (int k = 0; k < 4; k++) {
        f->v[k].x = corner[k][0];
        f->v[k].y = y;
        f->v[k].z = corner[k][1];
    }
    f->ny = -1.0f;
    f->d = y;
    f->nex = 1;
    list[0] = 0;
    list[1] = -1;
    for (int k = 0; k < 1024; k++) {
        wblk[k] = 0;
        fblk[k] = ICO_EEW(list);
    }
    ofs[0] = -8192.0f;
    ofs[1] = 0.0f;
    ofs[2] = -8192.0f;
    ofs[3] = 0.0f;
    c->count = 0;
    c->nfloor = 1;
    c->fcl = ICO_EEW(f);
    c->wblk = ICO_EEW(wblk);
    c->fblk = ICO_EEW(fblk);
    c->ofs = ICO_EEW(ofs);
    return c;
}

/* a .cl file's bytes into the stage partition, relocated as
   charFileManager.c ReadCollisionFile does */
static FcColl *loadCollision(const uint8_t *file, size_t size)
{
    FcColl *p = iosMallocDebug(ios_partition_seki, (int)size, "test.cl", 1);
    int *wb;
    int *fb;
    float *q;

    memcpy(p, file, size);
    p->wcl = ICO_EEW(p) + p->wcl;
    p->fcl = ICO_EEW(p) + p->fcl;
    p->wblk = ICO_EEW(p) + p->wblk;
    p->fblk = ICO_EEW(p) + p->fblk;
    p->ofs = ICO_EEW(p) + p->ofs;
    wb = ICO_EEPTR(int *, p->wblk);
    fb = ICO_EEPTR(int *, p->fblk);
    for (int k = 0; k < 1024; k++) {
        if (wb[k] != 0) {
            wb[k] = ICO_EEW(p) + wb[k];
        }
        if (fb[k] != 0) {
            fb[k] = ICO_EEW(p) + fb[k];
        }
    }
    q = iosMallocDebug(ios_partition_seki, p->count * 8 > 0 ? p->count * 8 : 16, "test.cl", 2);
    for (int j = 0; j < p->count; j++) {
        FcWallEnt *w = &ICO_EEPTR(FcWallEnt *, p->wcl)[j];

        w->normal = ICO_EEW(q + j * 2);
        FC_WALL_NORMAL(w)[0] = GetTableSin(w->angle);
        FC_WALL_NORMAL(w)[1] = GetTableCos(w->angle);
    }
    return p;
}

/* -------------------------------------------------------- the objects */
#define CARRIER_ROW 3716
static const char kTestPath[] = "object/test/collis/floor.cl";

static void layout(SObjSimpleSetting *lay, float x, float y, float z, float ry)
{
    memset(lay, 0, sizeof(*lay));
    lay->pos[0] = x;
    lay->pos[1] = y;
    lay->pos[2] = z;
    lay->pos[3] = 1.0f;
    lay->rot[1] = ry;
    lay->scale[0] = lay->scale[1] = lay->scale[2] = 1.0f;
    lay->scale[3] = 1.0f;
}

/* CreateLayoutedGObj's order: the object (layout row `row`) first, then its
   display object from the character files; the collision `coll` handed by
   the character files (atCreate) or attached afterwards. `newestHasDObj`
   makes the object table's newest object one with a display object already. */
static Sub15C *create(int id, const SObjSimpleSetting *lay, PObjModel *model, SkelNode *skel,
                      FcColl *coll, int atCreate, int newestHasDObj)
{
    static Sub15C other;
    Sub15C *d;

    memset(s_obj, 0, sizeof(s_obj));
    s_count = 0;
    if (newestHasDObj) {
        s_obj[1].self = 0; /* not in the collision list, only the table */
        s_obj[1].labelType = 1;
        s_obj[1].labelId = 17;
        s_obj[1].dobj = &other;
    }
    s_obj[0].self = &s_obj[0];
    s_obj[0].active = 1;
    s_obj[0].labelType = 1;
    s_obj[0].labelId = CARRIER_ROW;
    s_obj[0].dobj = 0;
    s_count = 1;
    if (newestHasDObj) {
        s_count = 2;
    }
    s_char.model = model;
    s_char.skel = skel;
    s_char.coll = atCreate ? coll : 0;
    logStart();
    d = CSVSYSTEM_InitDObj(id, (SObjSimpleSetting *)lay);
    logEnd();
    s_obj[0].dobj = d;
    if (!atCreate) {
        d->colData = (ICO_WORD)coll;
    }
    return d;
}

/* GetLowerPlaneCollision's probe from pos: 1 and the floor's y, or 0 */
static int probe(float x, float y, float z, float *floorY)
{
    ClipWork w;

    MakeCollisionDependGObjList();
    memset(&w, 0, sizeof(w));
    w.pt[0][0] = x;
    w.pt[0][1] = y;
    w.pt[0][2] = z;
    w.pt[0][3] = 1.0f;
    memcpy(w.pt[1], w.pt[0], sizeof(w.pt[1]));
    w.pt[1][1] = w.pt[1][1] + 10000.0f;
    ClipFloor(&w);
    if (w.floor.elem == 0) {
        return 0;
    }
    *floorY = w.pt[2][1];
    return 1;
}

static int near(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

static int tagged(const void *p)
{
    return memcmp(p, "<ALLOC>________", 16) == 0 || memcmp(p, "<FREE AREA>____", 16) == 0;
}

/* the one `dobj:` line: its words, then the matrix's translation (to 0.01;
   "-0" is 0) */
static void checkLine(int line, int model, int row, const char *path, float x, float y, float z)
{
    char head[512];
    float got[3] = {0.0f, 0.0f, 0.0f};
    char tail[8] = "";
    size_t n;

    n = (size_t)snprintf(head, sizeof(head),
                         "dobj: model %d (layout row %d) has no parts but carries the collision "
                         "%s: node 0's matrix is the layout's, at (",
                         model, row, path);
    if (strncmp(s_log, head, n) != 0 ||
        sscanf(s_log + n, "%f, %f, %f%7s", &got[0], &got[1], &got[2], tail) != 4 ||
        strcmp(tail, ")") != 0 || strchr(s_log, '\n') != s_log + strlen(s_log) - 1 ||
        !near(got[0], x, 0.01f) || !near(got[1], y, 0.01f) || !near(got[2], z, 0.01f)) {
        ico_check_fail(__FILE__, line, "", "the dobj line \"%s\", want \"%s%g, %g, %g)\"", s_log,
                       head, (double)x, (double)y, (double)z);
    }
}

/* ------------------------------------------------------------ synth */
static void testCarrier(void)
{
    SObjSimpleSetting lay;
    float fy = 0.0f;
    int hit;

    /* the row at the origin */
    heapReset();
    layout(&lay, 0.0f, 0.0f, 0.0f, 0.0f);
    snprintf(modelData[1140].collPath, sizeof(modelData[1140].collPath), "%s", kTestPath);
    FcColl *c = floorSet(0.0f);
    Sub15C *d = create(1140, &lay, &s_model0, 0, c, 1, 0);
    const float *m = (const float *)d->nodeMtx;
    CHECK(d->nodeNum == 0, "nodeNum %d", d->nodeNum);
    CHECK(m != 0 && headerOf(m)->size == 4, "the node matrix block is 64 bytes (%d qw)",
          m ? headerOf(m)->size : -1);
    CHECK(m != 0 && memcmp(m, d->matrix, 64) == 0, "node 0's matrix is the layout's");
    for (int i = 0; i < 16; i++) {
        CHECK(d->matrix[i / 4][i % 4] == (i % 5 == 0 ? 1.0f : 0.0f), "identity [%d] %g", i,
              (double)d->matrix[i / 4][i % 4]);
    }
    checkLine(__LINE__, 1140, CARRIER_ROW, kTestPath, 0.0f, 0.0f, 0.0f);
    hit = probe(0.0f, -50.0f, 0.0f, &fy);
    CHECK(hit && near(fy, 0.0f, 0.01f), "the probe at the origin: hit %d y %g", hit, (double)fy);
    hit = probe(100.0f, -50.0f, 200.0f, &fy);
    CHECK(hit && near(fy, 0.0f, 0.01f), "a probe beside: hit %d y %g", hit, (double)fy);
    hit = probe(0.0f, 50.0f, 0.0f, &fy);
    CHECK(!hit, "a probe under the floor finds none: hit %d y %g", hit, (double)fy);

    /* a row moved and turned about y: the floor at the row's y */
    heapReset();
    layout(&lay, 300.0f, -200.0f, 500.0f, 0.7f);
    c = floorSet(0.0f);
    d = create(1140, &lay, &s_model0, 0, c, 1, 0);
    m = (const float *)d->nodeMtx;
    CHECK(d->nodeNum == 0, "moved: nodeNum %d", d->nodeNum);
    CHECK(m != 0 && memcmp(m, d->matrix, 64) == 0, "moved: node 0's matrix is the layout's");
    CHECK(near(d->matrix[3][0], 300.0f, 0.01f) && near(d->matrix[3][1], -200.0f, 0.01f) &&
              near(d->matrix[3][2], 500.0f, 0.01f),
          "moved: the translation (%g, %g, %g)", (double)d->matrix[3][0], (double)d->matrix[3][1],
          (double)d->matrix[3][2]);
    CHECK(near(d->matrix[0][0], cosf(0.7f), 0.001f) && near(d->matrix[1][1], 1.0f, 0.0001f),
          "moved: turned about y (%g, %g)", (double)d->matrix[0][0], (double)d->matrix[1][1]);
    checkLine(__LINE__, 1140, CARRIER_ROW, kTestPath, 300.0f, -200.0f, 500.0f);
    hit = probe(300.0f, -400.0f, 500.0f, &fy);
    CHECK(hit && near(fy, -200.0f, 0.01f), "moved: the probe at the row: hit %d y %g", hit,
          (double)fy);
    hit = probe(1000.0f, -400.0f, -700.0f, &fy);
    CHECK(hit && near(fy, -200.0f, 0.01f), "moved: a probe beside: hit %d y %g", hit, (double)fy);
}

static void testShipped(void)
{
    SObjSimpleSetting lay;
    float fy = 0.0f;
    int hit;

    heapReset();
    layout(&lay, 0.0f, 0.0f, 0.0f, 0.0f);
    FcColl *c = floorSet(0.0f);
    Sub15C *d = create(1140, &lay, &s_model0, 0, c, 0, 0);
    const char *m = (const char *)d->nodeMtx;
    CHECK(d->nodeNum == 0, "shipped: nodeNum %d", d->nodeNum);
    CHECK(m != 0 && headerOf(m)->size == 0, "shipped: the node matrix block is 0 bytes (%d qw)",
          m ? headerOf(m)->size : -1);
    CHECK(m != 0 && tagged(m), "shipped: node 0's matrix starts with a block header's tag");
    CHECK_STR(s_log, "");
    hit = probe(0.0f, -50.0f, 0.0f, &fy);
    CHECK(!hit, "shipped: the probe at the origin finds no floor (hit %d y %g)", hit, (double)fy);
    hit = probe(100.0f, -50.0f, 200.0f, &fy);
    CHECK(!hit, "shipped: a probe beside finds no floor (hit %d y %g)", hit, (double)fy);
}

static void testUnchanged(void)
{
    SObjSimpleSetting lay;
    Blocks a;
    Blocks b;
    Blocks none;
    Sub15C *d;

    layout(&lay, 10.0f, 20.0f, 30.0f, 0.3f);

    /* the carrier: one more 64-byte block than without the collision */
    heapReset();
    d = create(1140, &lay, &s_model0, 0, floorSet(0.0f), 1, 0);
    snapshot(&a);
    heapReset();
    d = create(1140, &lay, &s_model0, 0, floorSet(0.0f), 0, 0);
    snapshot(&b);
    CHECK(oneMoreBlock(&a, &b, 4), "the carrier: one more 64-byte block (%d and %d blocks)", a.n,
          b.n);

    /* a 0-part model without a collision: the blocks of the shipped create */
    heapReset();
    (void)floorSet(0.0f);
    d = create(1140, &lay, &s_model0, 0, 0, 1, 0);
    snapshot(&none);
    CHECK(sameBlocks(&none, &b), "no collision: the same blocks (%d and %d)", none.n, b.n);
    CHECK(d->nodeNum == 0 && d->colData == 0 && headerOf((void *)d->nodeMtx)->size == 0,
          "no collision: nodeNum %d, a 0-byte node matrix block", d->nodeNum);
    CHECK_STR(s_log, "");

    /* a one-part model with a collision */
    heapReset();
    d = create(1141, &lay, &s_model1, 0, floorSet(0.0f), 1, 0);
    snapshot(&a);
    CHECK(d->nodeNum == 1 && headerOf((void *)d->nodeMtx)->size == 4,
          "one part: nodeNum %d, one 64-byte matrix", d->nodeNum);
    CHECK(memcmp((void *)d->nodeMtx, d->matrix, 64) == 0, "one part: node 0 is the layout's");
    CHECK_STR(s_log, "");
    heapReset();
    d = create(1141, &lay, &s_model1, 0, floorSet(0.0f), 0, 0);
    snapshot(&b);
    CHECK(sameBlocks(&a, &b), "one part: the same blocks (%d and %d)", a.n, b.n);

    /* a 0-part model with a skeleton and a collision */
    s_model0.disp = 1; /* a skinned display type, as a skeleton's model has */
    heapReset();
    d = create(1142, &lay, &s_model0, s_skel, floorSet(0.0f), 1, 0);
    snapshot(&a);
    CHECK(d->nodeNum == 2 && headerOf((void *)d->nodeMtx)->size == 8,
          "skeleton: nodeNum %d, two matrices", d->nodeNum);
    CHECK_STR(s_log, "");
    heapReset();
    d = create(1142, &lay, &s_model0, s_skel, floorSet(0.0f), 0, 0);
    snapshot(&b);
    CHECK(sameBlocks(&a, &b), "skeleton: the same blocks (%d and %d)", a.n, b.n);
    s_model0.disp = 0;
}

static void testRow(void)
{
    SObjSimpleSetting lay;

    heapReset();
    layout(&lay, 0.0f, 0.0f, 0.0f, 0.0f);
    snprintf(modelData[1140].collPath, sizeof(modelData[1140].collPath), "%s", kTestPath);
    (void)create(1140, &lay, &s_model0, 0, floorSet(0.0f), 1, 1);
    checkLine(__LINE__, 1140, -1, kTestPath, 0.0f, 0.0f, 0.0f);
}

/* ------------------------------------------------------------ disc */
static const char kPack[] = "STG8TEST.DF";
static const char kModel[] = "object/sdf/8test/model/st08a_p1.p2o";
static const char kColl[] = "object/sdf/8test/collis/st08a.cl";

typedef struct {
    const char *pack;
    int zero;
    char zeroPack[64];
    char zeroName[128];
    int zeroId;
    uint32_t zeroSize;
    uint8_t *cl;
    uint32_t clSize;
} Scan;

static int endsWith(const char *s, const char *t)
{
    size_t a = strlen(s);
    size_t b = strlen(t);
    return a >= b && strcmp(s + a - b, t) == 0;
}

static int modelName(const char *name)
{
    static const char *const ext[] = {".p2o", ".p2c", ".p2g", ".plo", ".p2v", ".p2s"};
    for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
        if (endsWith(name, ext[i])) {
            return 1;
        }
    }
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int scanMember(void *user, int member, const char *name, const IcoDfMember *m,
                      const uint8_t *data)
{
    Scan *s = (Scan *)user;

    (void)member;
    if (modelName(name) && m->size >= 12 && memcmp(data, "PS2O", 4) == 0 && le32(data + 8) == 0) {
        if (s->zero == 0) {
            snprintf(s->zeroPack, sizeof(s->zeroPack), "%s", s->pack);
            snprintf(s->zeroName, sizeof(s->zeroName), "%s", name);
            s->zeroId = m->id;
            s->zeroSize = m->size;
        }
        s->zero++;
    }
    if (strcmp(s->pack, kPack) == 0 && strcmp(name, kColl) == 0 && s->cl == NULL) {
        s->cl = malloc(m->size);
        if (s->cl != NULL) {
            memcpy(s->cl, data, m->size);
            s->clSize = m->size;
        }
    }
    return 0;
}

/* this disc's model id of the stub (its PackEnt id, the model table row) */
static int stubId(const char *slug)
{
    if (strcmp(slug, "pal") == 0) {
        return 1140;
    }
    if (strcmp(slug, "ntsc") == 0) {
        return 1105;
    }
    if (strcmp(slug, "jpn") == 0 || strcmp(slug, "kor") == 0 || strcmp(slug, "cn") == 0) {
        return 1136;
    }
    if (strcmp(slug, "aug6") == 0) {
        return 1100;
    }
    return -1; /* jun26: no STG8TEST.DF */
}

static int runDisc(const char *image)
{
    FILE *f = image != NULL && image[0] != '\0' ? fopen(image, "rb") : NULL;
    IcoVfs *vfs;
    IcoDfIndex *idx;
    Scan s;
    const int want = stubId(s_slug);

    if (f == NULL) {
        printf("collision_carrier_test disc %s: no image \"%s\": skipped\n", s_slug,
               image ? image : "");
        return 77;
    }
    fclose(f);
    vfs = ico_vfs_mount(&ico_vfs_iso9660, image);
    if (vfs == NULL) {
        printf("collision_carrier_test disc %s: the image does not mount: skipped\n", s_slug);
        return 77;
    }
    idx = ico_df_index_open(vfs);
    CHECK(idx != NULL, "DATA.DF's index");
    if (idx == NULL) {
        ico_vfs_unmount(vfs);
        return 1;
    }

    /* the gate: every model member of every pack */
    memset(&s, 0, sizeof(s));
    for (int e = 0; e < ico_df_index_entries(idx); e++) {
        int first = 0;
        int n = ico_df_index_pack_members(idx, e, &first);
        int models = 0;

        for (int k = 0; k < n; k++) {
            const char *nm = ico_df_index_member_name(idx, first + k);
            models += nm != NULL && modelName(nm);
        }
        if (models == 0) {
            continue;
        }
        s.pack = ico_df_index_entry_name(idx, e);
        CHECK(ico_df_index_read_pack(idx, e, scanMember, &s) == 0, "pack %s reads", s.pack);
    }
    if (want < 0) {
        CHECK(s.zero == 0, "no model without parts on this disc (%d, the first %s %s)", s.zero,
              s.zeroPack, s.zeroName);
        printf("collision_carrier_test disc %s: no model without parts\n", s_slug);
    } else {
        CHECK(s.zero == 1, "exactly one model without parts (%d)", s.zero);
        CHECK_STR(s.zeroPack, kPack);
        CHECK_STR(s.zeroName, kModel);
        CHECK(s.zeroId == want && s.zeroSize == 48, "the stub: id %d (want %d), %u bytes", s.zeroId,
              want, (unsigned)s.zeroSize);
        printf("collision_carrier_test disc %s: %d model without parts: %s %s, id %d, %u bytes\n",
               s_slug, s.zero, s.zeroPack, s.zeroName, s.zeroId, (unsigned)s.zeroSize);
    }

    /* the room's collision on its carrier */
    if (want >= 0) {
        CHECK(s.cl != NULL && s.clSize > sizeof(FcColl), "%s in %s", kColl, kPack);
    }
    if (want >= 0 && s.cl != NULL && s.clSize > sizeof(FcColl)) {
        SObjSimpleSetting lay;
        float fy = 0.0f;
        int hit;

        layout(&lay, 0.0f, 0.0f, 0.0f, 0.0f);
        snprintf(modelData[want].collPath, sizeof(modelData[want].collPath), "%s", kColl);
        heapReset();
        (void)create(want, &lay, &s_model0, 0, loadCollision(s.cl, s.clSize), 1, 0);
        CHECK(strstr(s_log, "dobj: model ") != NULL && strstr(s_log, kColl) != NULL,
              "the dobj line: %s", s_log);
        hit = probe(0.0f, 0.0f, 0.0f, &fy);
        CHECK(hit && near(fy, 0.0f, 0.01f), "the origin: hit %d y %g", hit, (double)fy);
        printf("collision_carrier_test disc %s: the origin's floor: %s %.2f\n", s_slug,
               hit ? "y" : "none", (double)fy);
        hit = probe(1111.0f, -1990.0f, 81.0f, &fy);
        CHECK(hit && near(fy, -1323.67f, 0.01f), "zone 20's box top: hit %d y %g", hit, (double)fy);
        printf("collision_carrier_test disc %s: zone 20's floor: %s %.2f\n", s_slug,
               hit ? "y" : "none", (double)fy);

        heapReset();
        (void)create(want, &lay, &s_model0, 0, loadCollision(s.cl, s.clSize), 0, 0);
        hit = probe(0.0f, 0.0f, 0.0f, &fy);
        CHECK(!hit, "shipped: the origin finds no floor (hit %d y %g)", hit, (double)fy);
        hit = probe(1111.0f, -1990.0f, 81.0f, &fy);
        CHECK(!hit, "shipped: zone 20's box top finds no floor (hit %d y %g)", hit, (double)fy);
    }
    free(s.cl);
    ico_df_index_close(idx);
    ico_vfs_unmount(vfs);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "synth";
    int rc = 0;

    if (argc > 2) {
        s_slug = argv[2];
    }
    if (argc > 3 && strcmp(mode, "synth") == 0) {
        snprintf(s_dir, sizeof(s_dir), "%s", argv[3]);
    }
    if (argc > 4 && strcmp(mode, "disc") == 0) {
        snprintf(s_dir, sizeof(s_dir), "%s", argv[4]);
    }
    if (ico_region_select(s_slug) != 0) {
        fprintf(stderr, "FAIL: no disc \"%s\"\n", s_slug);
        return 1;
    }
    heapInit();
    InitMatrixDrive();
    modelsInit();

    if (strcmp(mode, "synth") == 0) {
        testCarrier();
        testShipped();
        testUnchanged();
        testRow();
    } else if (strcmp(mode, "disc") == 0) {
        rc = runDisc(argc > 3 ? argv[3] : NULL);
        if (rc == 77) {
            return 77;
        }
    } else {
        fprintf(stderr,
                "usage: collision_carrier_test synth <slug> <dir> | disc <slug> <image> <dir>\n");
        return 2;
    }
    CHECK(s_asserts == 0, "%d game assert(s)", s_asserts);
    return ico_check_summary("collision_carrier_test") || rc;
}
