/*
 * port/game/test/shadow_spawn_test.c
 *
 * On the PC every shadow came out of its black pool standing on the floor
 * and rose above the pool, where the PS2 lifts it out of the floor.
 *
 * The depth a shadow starts at is the generator's enemy kind: kind 0 starts
 * the rise bodySize * 100 below the generator (enemy_act.c actEnemyRestart),
 * and the kind is the word at 0x30 of what the kind's constructor is handed
 * (generator.c InitGeneratorGeo).  CreateLayoutedGObj (sceneManager.c) hands
 * it the scene object's SObjSimpleSetting, whose word at 0x30 is the layout
 * row's initArg, 0 in all 92 generator rows of the disc.  Read as a GenGeo
 * (the decompilation's type) the host took the kind from 0x3C, the setting's
 * never written alignment tail: stack contents.
 *
 * generator   the real generator.c, its constructor called the way
 *             CreateLayoutedGObj calls it, with the setting's tail filled with
 *             a non-zero pattern as an unwritten stack slot may be: the kind is
 *             the setting's object word (0, and 1 for a row whose initArg is
 *             1), and GeneratorGeo, run tick by tick after a Generator_Call as
 *             the stage scripts call it, calls actEnemyRestart (recorded here)
 *             with kind 0 at the generator's root.
 * rise        with the disc (argv[1], else skipped): EN1 START (motion 955,
 *             object/sdf/enemy/motion/hnac250b.mob) relocated by the real
 *             InitMotionFile and read by the real GetFloatingMotion and
 *             GetFloatingMotionRootPos (motionManager2.c), on the shadows'
 *             skeleton (object/sdf/enemy/model/skelton.skb).  The root follows
 *             the motion as root update mode 2 moves it (rootUpdateXZ_MotPos:
 *             the step from the last frame's root to this frame's is added,
 *             nothing fits it to the floor while the motion has no land frame)
 *             and the nodes are placed as getFinalMatrixWithNaturalGeometry
 *             places them (each node's offset in its parent, its rotation the
 *             parent's times the motion's; the roots at the root position).
 *             Started at kind 0's depth, every node is under the floor at the
 *             first tick and the root ends at the skeleton's rest hip height
 *             (105) above the floor; started on the generator (the old host
 *             kind), the hips are on the floor at the first tick and end 200
 *             above it.  Each tick's root and lowest and highest node Y are
 *             printed with their bits, for comparing builds.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "sceneManager.h"
#include "generator.h"
#include "motionFileManager.h"
#include "motionManager2.h"
#include "motionOrientManager.h"

#include "arena.h"
#include "df_pack.h"
#include "vfs.h"

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

/* --- the game's data the compiled-in units reach ---------------------------- */
GenGeo objLayout[3759];
const StgPre stageData[106];
const MotionDef motionKind[1];
const SafePosOffset generatorSubPosition[1];
int systemStatus[12];
int stage_no;
GObj *girlGObj;
struct IosMemPart *ios_partition_sugipon;

/* GetFloatingMotion and the rest of motionManager2.c (the decode compiled in) */
void GetFloatingMotion(StreamElem *dst, float t, float *root, void *motion, int count,
                       unsigned char *mask, SkelNode *hrc);
void GetFloatingMotionRootPos(float *dst, void *m, float t);
void GetMotionRootPos(float *dst, void *motion, int idx);

/* the head of generator.c's GenWork, up to the enemy kind at 0x14 */
typedef struct GenWorkHead {
    int count;
    int autoCallTimer;
    int callRequests;
    int hard;
    unsigned char masked;
    unsigned char active;
    unsigned char resetRequest;
    char pad13[1];
    int kind; /* 0x14 */
} GenWorkHead;

void GeneratorGeo(GObj *gobj);
void Generator_Call(GObj *gobj);

/* the vector helpers of ico_math the test uses itself */
void sceVu0ApplyMatrix(void *out, void *m, void *v);
void MultiQuaternion(void *out, void *qa, void *qb);
void GetMatrixFromQuaternionRotElem(void *mtx, void *q);

/* --- the generator's world ------------------------------------------------ */
enum { GEN_LABEL = 511, ENEMY_LABEL = 500 };

static GObj genObj;
static GObj enemyObj;
static float genRoot[4];
static int restartCalls;
static int restartKind;
static float restartPos[4];

static void unexpected(const char *name)
{
    printf("FAIL unexpected call: %s\n", name);
    failures++;
}

void *iosMallocDebug(struct IosMemPart *part, int size, const char *file, int line)
{
    (void)part;
    (void)file;
    (void)line;
    return calloc(1, (size_t)size);
}

void *InitMultiBgaManager(int n)
{
    (void)n;
    return calloc(1, 4096);
}

void EntryMultiBgaManager(void *p, int a, int b, void *pos, void *mtx)
{
    (void)p;
    (void)a;
    (void)b;
    (void)pos;
    (void)mtx;
}

void DispMultiBgaManagerWithKind(int kind, void *p, int n)
{
    (void)kind;
    (void)p;
    (void)n;
}

/* the aim direction turned by the angle: kept as given (the test aims along
   z, angle 0) */
void _ApplyRyGV(float *v, float ang)
{
    (void)v;
    (void)ang;
}

float _DistxzSqGV(float *a, float *b)
{
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];
    return dx * dx + dz * dz;
}

void GetRootPosition(float *dst, GObj *obj)
{
    if (obj == &genObj) {
        memcpy(dst, genRoot, sizeof genRoot);
        return;
    }
    memset(dst, 0, 16);
    dst[1] = -1000000.0f;
    dst[3] = 1.0f;
}

void SetRootPosition(GObj *obj, float *pos)
{
    if (obj == &genObj) {
        memcpy(genRoot, pos, 12);
    }
}

void GetRootMatrix(float *m, GObj *obj)
{
    (void)obj;
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void UpdateRootMatrix(GObj *obj)
{
    (void)obj;
}

float *test_CURRENTROOT(GObj *obj)
{
    (void)obj;
    return genRoot;
}

/* the object lists: one enemy (kind 4) and the generator (kind 33) */
GObj *isysGObjSearchFromObjKindID_begin(int kind)
{
    if (kind == 4) {
        return &enemyObj;
    }
    if (kind == 33) {
        return &genObj;
    }
    return 0;
}

GObj *isysGObjSearchFromObjKindID_next(GObj *g)
{
    (void)g;
    return 0;
}

GObj *isysGObjSearchFromObjLayoutID(int label)
{
    (void)label;
    return 0;
}

int isEnemyActive(GObj *g)
{
    (void)g;
    return 0;
}

int GetMotherGeneratorLabelAskEnemy(GObj *g)
{
    (void)g;
    return -1;
}

GObj *GetMotherGeneratorGObjAskEnemy(GObj *g)
{
    (void)g;
    return 0;
}

void actEnemyRestart(GObj *self, float *pos, float *dir, int kind, GObj *mother)
{
    (void)dir;
    (void)mother;
    if (self == &enemyObj) {
        restartCalls++;
        restartKind = kind;
        memcpy(restartPos, pos, sizeof restartPos);
    }
}

void ACTGame_SaveActorInformation(GObj *g)
{
    (void)g;
}

void gamesysObjInfoUniqDataSet(GObj *g)
{
    (void)g;
}

void iosOmSendMail(GObj *g, int mail, GObj *from)
{
    (void)g;
    (void)mail;
    (void)from;
}

void debug_Arrow(float len, void *pos, void *dir, int r, int g, int b)
{
    (void)len;
    (void)pos;
    (void)dir;
    (void)r;
    (void)g;
    (void)b;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    failures++;
}

void debug_assertMessage(const char *file, int line, const char *msg)
{
    printf("FAIL debug_assertMessage %s:%d %s\n", file, line, msg);
    failures++;
}

/* reached only by code paths the test does not take */
#define UNEXPECTED(name)                                                                           \
    void name(void);                                                                               \
    void name(void)                                                                                \
    {                                                                                              \
        unexpected(#name);                                                                         \
    }
UNEXPECTED(GetKidnapInfo)
UNEXPECTED(GetStageFromLabel)
UNEXPECTED(ClipFloor)
UNEXPECTED(ClipFloorE)
UNEXPECTED(ClipWall)
UNEXPECTED(CompareAttribute)
UNEXPECTED(DrawLineG)
UNEXPECTED(GetFloorAttribute)
UNEXPECTED(GetGlobalDirectionOrient)
UNEXPECTED(GetGlobalWallPlane)
UNEXPECTED(GetPoolGlobalDrainVector)
UNEXPECTED(GetPoolGlobalHeight)
UNEXPECTED(GetPoolGlobalHeightDetail)
UNEXPECTED(GetProjectionOfPlane)
UNEXPECTED(GetProjectionOfPlaneWithKeepAway)
UNEXPECTED(GetRootQuaternion)
UNEXPECTED(GetWallGlobalInfo)
UNEXPECTED(GetYDistanceFromPlane)
UNEXPECTED(GetYProjectionOfPlane)
UNEXPECTED(LocalizeDirectionOrient)
UNEXPECTED(SetDirectRootPosition)
UNEXPECTED(SetFallDownSplash)
UNEXPECTED(SetSimplePlane)
UNEXPECTED(gif_EndPacket)
UNEXPECTED(gif_SetAlpha)
UNEXPECTED(gif_StartPacketPri)
UNEXPECTED(soundSeGroupGet)

/* --- generator ------------------------------------------------------------ */

/* The setting initSceneGObj builds for a layout row (sceneManager.c): the
   position negated, the rotations in radians, the scale and the row's
   initArg as the object word.  The tail past the object word is left as a
   stack slot may hold it, a non-zero pattern. */
static void layoutSetting(SObjSimpleSetting *a, const GenGeo *gen)
{
    memset(a, 0xA5, sizeof *a);
    a->pos[0] = -gen->pos[0];
    a->pos[1] = -gen->pos[1];
    a->pos[2] = -gen->pos[2];
    a->pos[3] = 1.0f;
    a->rot[0] = gen->rot[0] * 3.1415927f / 180.0f;
    a->rot[1] = gen->rot[1] * 3.1415927f / 180.0f;
    a->rot[2] = gen->rot[2] * 3.1415927f / 180.0f;
    a->rot[3] = 0.0f;
    a->scale[0] = gen->scale[0];
    a->scale[1] = gen->scale[1];
    a->scale[2] = gen->scale[2];
    a->scale[3] = 1.0f;
    a->obj = gen->initArg;
}

/* the kind's constructor through objKindData's create slot's type */
typedef void *(*CreateFn)(GObj *, void *);

static void testGenerator(void)
{
    static Sub15C genSub;
    static Act enemyAct;
    GenGeo *row = &objLayout[GEN_LABEL];
    SObjSimpleSetting a;
    CreateFn create = (CreateFn)InitGeneratorGeo;
    _Static_assert(offsetof(GenWorkHead, kind) == 0x14, "GenWork's kind is at 0x14");
    GenWorkHead *w;
    int tick;

    /* stage 10's generator 511: 1495, 405, 1600 in the layout, initArg 0
       (the disc's row; the 91 others have initArg 0 too) */
    row->scale[0] = row->scale[1] = row->scale[2] = 1.0f;
    row->pos[0] = -1495.0f;
    row->pos[1] = 405.0f;
    row->pos[2] = 1600.0f;
    row->kind = 33;
    row->accessary = 26;
    row->initArg = 0;
    layoutSetting(&a, row);
    w = create(&genObj, &a);
    CHECK(w != NULL && w->kind == 0, "generator kind %d, want the setting's object word 0",
          w != NULL ? w->kind : -1);

    row->initArg = 1;
    layoutSetting(&a, row);
    {
        GenWorkHead *w1 = create(&genObj, &a);
        CHECK(w1 != NULL && w1->kind == 1, "generator kind %d, want 1 for initArg 1",
              w1 != NULL ? w1->kind : -1);
    }
    row->initArg = 0;

    /* the generator and one enemy of it, out of the generator (reviveCount
       -1, display bit clear); the scripts call Generator_Call */
    genObj.labelId = GEN_LABEL;
    genObj.kind = 33;
    genObj.dobj = &genSub;
    genSub.work = w;
    genRoot[0] = a.pos[0];
    genRoot[1] = a.pos[1];
    genRoot[2] = a.pos[2];
    genRoot[3] = 1.0f;
    enemyObj.labelId = ENEMY_LABEL;
    enemyObj.kind = 4;
    enemyObj.act = (void *)&enemyAct;
    objLayout[ENEMY_LABEL].kind = 4;
    objLayout[ENEMY_LABEL].parent = GEN_LABEL;
    objLayout[ENEMY_LABEL].reviveCount = -1;
    systemStatus[0] = 1; /* PAL */
    systemStatus[1] = 2; /* 25 ticks a second */

    Generator_Call(&genObj);
    for (tick = 0; tick < 200 && restartCalls == 0; tick++) {
        GeneratorGeo(&genObj);
    }
    CHECK(restartCalls == 1, "%d enemies called in %d ticks, want 1", restartCalls, tick);
    CHECK(restartKind == 0, "actEnemyRestart kind %d, want 0", restartKind);
    CHECK(restartPos[0] == genRoot[0] && restartPos[1] == genRoot[1] && restartPos[2] == genRoot[2],
          "called at %g %g %g, want the generator's root %g %g %g", restartPos[0], restartPos[1],
          restartPos[2], genRoot[0], genRoot[1], genRoot[2]);
    printf("generator: kind %d, enemy called after %d ticks at %g %g %g\n", restartKind, tick,
           restartPos[0], restartPos[1], restartPos[2]);
}

/* --- rise ----------------------------------------------------------------- */

static uint32_t bitsOf(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void *readMember(IcoVfs *vfs, const char *name, uint32_t *size, void *dst)
{
    IcoDfMember m;

    if (ico_df_find_member(vfs, name, &m) != 0) {
        printf("FAIL %s is not on the disc\n", name);
        failures++;
        return NULL;
    }
    if (dst == NULL) {
        dst = malloc(m.size);
    }
    if (dst == NULL || ico_df_read_member(vfs, &m, dst) != 0) {
        printf("FAIL %s could not be read\n", name);
        failures++;
        return NULL;
    }
    *size = m.size;
    return dst;
}

enum { MAX_NODES = 32, RISE_TICKS = 110 };

typedef struct RiseRow {
    float root;    /* the root (hips) Y */
    float lowest;  /* the greatest node Y (Y grows downward) */
    float highest; /* the least node Y */
} RiseRow;

/* One rise: the root starts at startY (world Y, down positive) over a floor
   at floorY; t frames a tick.  rows[k] is tick k's root and node extremes. */
static void playRise(void *mot, SkelNode *skel, int n, float scale, float startY, float t,
                     RiseRow *rows, const char *label, float floorY)
{
    int frames = *(int *)mot;
    float anim = 0.0f;
    float last = 0.0f;
    float pos[4] = {0.0f, startY, 0.0f, 1.0f};
    float rootQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int k;

    for (k = 0; k < RISE_TICKS; k++) {
        StreamElem el[MAX_NODES];
        float m[MAX_NODES][4][4];
        float q[MAX_NODES][4];
        float v[4] = {0, 0, 0, 0};
        float rv[4] = {0, 0, 0, 0};
        float lo = -1e30f;
        float hi = 1e30f;
        int i;

        /* getMotionGeometry: this frame's root (v) and the last frame's (rv),
           scaled; the step is their difference once the frame has moved on */
        GetFloatingMotion(el, anim, v, mot, n, NULL, skel);
        GetFloatingMotionRootPos(rv, mot, last);
        if (!(anim < last)) {
            pos[0] += v[0] * scale - rv[0] * scale;
            pos[1] += v[1] * scale - rv[1] * scale;
            pos[2] += v[2] * scale - rv[2] * scale;
        }

        /* getFinalMatrixWithNaturalGeometry: a root node at the root
           position, each other node at its offset in its parent */
        for (i = 0; i < n; i++) {
            int p = skel[i].parent;

            if (p < 0) {
                MultiQuaternion(q[i], rootQuat, el[i].q);
                GetMatrixFromQuaternionRotElem(m[i], q[i]);
                m[i][3][0] = 0.0f;
                m[i][3][1] = 0.0f;
                m[i][3][2] = 0.0f;
                m[i][3][3] = 1.0f;
            } else {
                float off[4] = {skel[i].pos[0] * scale, skel[i].pos[1] * scale,
                                skel[i].pos[2] * scale, skel[i].pos[3]};
                MultiQuaternion(q[i], q[p], el[i].q);
                GetMatrixFromQuaternionRotElem(m[i], q[i]);
                sceVu0ApplyMatrix(m[i][3], m[p], off);
            }
        }
        for (i = 0; i < n; i++) {
            float y = m[i][3][1] + pos[1];
            lo = y > lo ? y : lo;
            hi = y < hi ? y : hi;
        }
        rows[k].root = pos[1];
        rows[k].lowest = lo;
        rows[k].highest = hi;
        printf("%s %2d frame %6.2f root %8.2f (%08x) lowest %8.2f (%08x) highest %8.2f "
               "(%08x)\n",
               label, k, anim, floorY - pos[1], bitsOf(pos[1]), floorY - lo, bitsOf(lo),
               floorY - hi, bitsOf(hi));

        /* UpdateFrameCounter, play mode 2: the frame stops at the last */
        last = anim;
        anim += t;
        if ((float)(frames - 1) <= anim) {
            anim = last;
        }
    }
}

static int testRise(const char *disc)
{
    IcoVfs *vfs;
    unsigned char *mob;
    SkelNode *skel;
    uint32_t mobSize = 0;
    uint32_t skelSize = 0;
    RiseRow ps2[RISE_TICKS];
    RiseRow old[RISE_TICKS];
    int n = 0;
    int skelNodes;
    const float scale = 1.0f;
    /* stage 10's generator 511 stands 5 over its floor: the layout's 405 is
       world Y -405, over st47a.cl's floor at -400 */
    const float genY = -405.0f;
    const float floorY = -400.0f;
    /* PAL, 25 ticks a second: 60 / 25 * 0.5 frames a tick, EN1 START's play
       speed and PAL ratio 1 */
    const float t = 60.0f / 25.0f * 0.5f;

    vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("shadow_spawn_test: rise SKIP (%s is not a readable disc image)\n", disc);
        return 0;
    }
    if (ico_arena_init() != 0) {
        printf("FAIL no arena\n");
        failures++;
        return 1;
    }
    /* the motion in the arena, where the game's loader puts it */
    mob = (unsigned char *)ico_arena_ee_addr(0x800000);
    if (readMember(vfs, "object/sdf/enemy/motion/hnac250b.mob", &mobSize, mob) == NULL) {
        return 1;
    }
    skel = readMember(vfs, "object/sdf/enemy/model/skelton.skb", &skelSize, NULL);
    if (skel == NULL) {
        return 1;
    }
    InitMotionFile(mob, "hnac250b");
    {
        IcoEEWord *nl = ICO_EEPTR(IcoEEWord *, ((MotFileHdr *)mob)->nodeList);
        while (nl[n] != 0) {
            n++;
        }
    }
    skelNodes = 0;
    while ((size_t)(skelNodes + 1) * sizeof(SkelNode) <= skelSize && skel[skelNodes].parent >= -1 &&
           skel[skelNodes].mirror >= 0) {
        skelNodes++;
    }
    CHECK(*(int *)mob == 125, "EN1 START has %d frames, want 125", *(int *)mob);
    CHECK(n > 0 && n <= MAX_NODES && n <= skelNodes, "%d motion nodes, %d skeleton nodes", n,
          skelNodes);
    if (failures != 0) {
        return 1;
    }
    {
        float r0[4];
        float r1[4];
        GetFloatingMotionRootPos(r0, mob, 0.0f);
        GetMotionRootPos(r1, mob, 124);
        printf("rise: %d nodes, root Y %.2f at frame 0 and %.2f at frame 124 (world, down "
               "positive)\n",
               n, r0[1], r1[1]);
    }

    /* the PS2: kind 0 starts the root bodySize * 100 under the generator */
    playRise(mob, skel, n, scale, genY + scale * 100.0f, t, ps2, "kind0", floorY);
    /* the old host: a stray kind left the root on the generator */
    playRise(mob, skel, n, scale, genY, t, old, "stray", floorY);

    CHECK(ps2[0].highest > floorY, "kind 0: the highest node is %.2f over the floor at the start",
          floorY - ps2[0].highest);
    CHECK(fabsf((floorY - ps2[RISE_TICKS - 1].root) - 105.0f) < 6.0f,
          "kind 0: the root ends %.2f over the floor, want about the rest hip height 105",
          floorY - ps2[RISE_TICKS - 1].root);
    CHECK(fabsf(floorY - old[0].root) < 10.0f && old[0].highest < floorY - 50.0f,
          "stray kind: the root starts %.2f over the floor, the top %.2f", floorY - old[0].root,
          floorY - old[0].highest);
    free(skel);
    return 0;
}

int main(int argc, char **argv)
{
    testGenerator();
    if (argc > 1 && argv[1][0] != '\0') {
        testRise(argv[1]);
    } else {
        printf("shadow_spawn_test: rise SKIP (no disc image)\n");
    }
    if (failures != 0) {
        printf("shadow_spawn_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("shadow_spawn_test: OK\n");
    return 0;
}
