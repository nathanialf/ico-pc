/*
 * tools/arm64_diff/followdiff.c
 *
 * The hand-holding walk's differential harness (tools/arm64_diff.sh, the
 * second program after ikdiff): the game's own code of Yorda following Ico
 * by the hand, compiled once per target with that target's flags, run over
 * the same deterministic inputs, every result printed as bits in a fixed
 * order.  The script diffs the outputs of the arm64 build (under qemu) and
 * the x86-64 builds (gcc, the NDK's clang, the linux-x64-clang build's).
 * Issue 19: on the phone Ico and Yorda walk hand in hand with both arms
 * straight up; ikdiff showed the arm IK itself gives the same bits on every
 * target, and connectToTarget raises the meeting point of the hands only
 * when Yorda's shoulder is nearer Ico's than the two arms reach, so this
 * harness runs what decides where Yorda walks.
 *
 * Linked as the game links them: girl_act.c (actGirlHand, the hand manager
 * HandMgr_* of girl_act_hand.c.inc), act-game.c (ACTGame_ConnectHand,
 * RequestChangeHandMode, the play-speed reserve, the look target),
 * commonact.c (SetMotionDirectionSmooze, test_CURRENTROOT/ORIENT,
 * ACTSendMailCorrect), geometryManager.c, motionManager2.c,
 * fieldCollision.c (GetYProjectionOfPlane), gv.c (the steering helpers),
 * handManager.c (HandManager, connectToTarget), matrixDrive.c,
 * quaternion.c, tableSin.c, port/math, the data table loader (the game's
 * motion and parameter tables from the ELF when one is given, zero rows
 * otherwise) and port/platform/fiber.c, on which actGirlHand runs as the
 * game runs an actor: _ACTWait yields to the driver.  Every other call the
 * linked code makes stops the program with its name (stubs the script
 * generates); every other object it reads is zero.
 *
 * Two parts:
 *   - the helpers over random and edge inputs: gv.c's _RotyGV,
 *     _AbsRotyGV, _InterRotGV, _OrientXZGV, _OrientGV, _MoveGV, _ApplyRyGV,
 *     _GetDirection, _FrontGV, GetMatrixDirectionToZ and the distances;
 *     SetMotionDirection, SetMotionDirectionWithLimit,
 *     SetMotionDirectionSmooze; HandMgr_GetDistHand/Update/Judge/Speed;
 *   - walks: Ico (scripted: standing, walking, running, turning, stopping)
 *     and Yorda (actGirlHand decides her motion, her direction and her play
 *     speed; the harness moves her by them, a stand-in for the motion
 *     system), both skeletons placed from their roots each frame, then
 *     HandManager for both: Ico's hand target (connectToTarget) and the
 *     elevation of the meeting point above his shoulder.
 * What the walk model does with the game's decisions is the harness's; the
 * decisions are the game's.  Not a test of the right answer: of whether
 * every build gives the same one.
 *
 * Arguments: [elf].  elf, the PS2 game's main ELF: the tables are loaded
 * from it as the game loads them; without it they stay zero.
 */
#include "typedef.h"
#include "main.h"
#include "debug.h"
#include "act-game.h"
#include "commonact.h"
#include "girl_act.h"
#include "gv.h"
#include "geometryManager.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "handManager.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "tableSin.h"
#include "enemy_act.h"
#include <libvu0.h>
#include <string.h>
#include <stdio.h>
#include "../../port/platform/fiber.h"
#include "../../port/platform/fpenv.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

/* port/data/tables.h (its include directory is not the game's) */
int ico_tables_load_elf(const uint8_t *elf, size_t size, char *err, size_t errsz);

/* girl_act.c: defined there, declared nowhere (the record's type is the
   TU's own; this is its layout, the offsets its comments give) */
void HandMgr_GetDistHand(float *dist, float *height);
void HandMgr_Init(void);
void HandMgr_Update(void);
void HandMgr_Judge(void);
void HandMgr_Speed(GObj *self, float f);
void actGirlHand(GObj *volatile self);

typedef struct {
    float prev[4];
    float cur[4];
    float moveDist;
    char pad24[12];
    float orient[4];
    float toBoy[4];
    float handDist;
    float handHeight;
    unsigned char still;
    unsigned char turned;
    unsigned char far100;
    unsigned char far125;
    unsigned char far135;
    unsigned char near90;
    unsigned char heightGap;
} FdGirlStand;

#define HANDMGR ((FdGirlStand *)(void *)&handmgr)

/* --- output -------------------------------------------------------------- */
static FILE *out;

static unsigned int fbits(float f)
{
    union {
        float f;
        unsigned int u;
    } c;

    if (f != f) {
        return 0x7FC00000u;
    }
    c.f = f;
    return c.u;
}

static void pf(float f)
{
    fprintf(out, " %08x", fbits(f));
}

static void pfv(const float *v, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        pf(v[i]);
    }
}

static void pi(int i)
{
    fprintf(out, " %d", i);
}

/* --- inputs -------------------------------------------------------------- */
static uint32_t rs = 0x2B0A19C3u;

static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd() >> 8) * (1.0f / 16777216.0f));
}

static int irand(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* Newton steps from a bit guess: identical on every target, no libm */
static float fsqrt_(float x)
{
    union {
        float f;
        unsigned int u;
    } c;

    float g;
    int i;

    if (x <= 0.0f) {
        return 0.0f;
    }
    c.f = x;
    c.u = 0x1FBD1DF5u + (c.u >> 1);
    g = c.f;
    for (i = 0; i < 4; i++) {
        g = 0.5f * (g + x / g);
    }
    return g;
}

/* a direction in the xz plane from a game angle (0x10000 a turn) */
static void yawDir(float *v, int yaw)
{
    v[0] = GetTableSin((short)yaw);
    v[1] = 0.0f;
    v[2] = GetTableCos((short)yaw);
    v[3] = 0.0f;
}

/* a vector: random, along an axis, tiny, zero, backwards, or huge */
static void randVec(float *v)
{
    int k = irand(0, 11);

    v[3] = 0.0f;
    switch (k) {
    case 0:
        v[0] = v[1] = v[2] = 0.0f;
        break;
    case 1:
        v[0] = 0.0f;
        v[1] = frand(-1.0f, 1.0f);
        v[2] = -1.0f;
        break;
    case 2:
        v[0] = -0.0f;
        v[1] = 0.0f;
        v[2] = -frand(0.5f, 50.0f);
        break;
    case 3:
        v[0] = frand(-1e-6f, 1e-6f);
        v[1] = frand(-1e-6f, 1e-6f);
        v[2] = frand(-1e-6f, 1e-6f);
        break;
    case 4:
        v[0] = frand(-1e18f, 1e18f);
        v[1] = frand(-1e18f, 1e18f);
        v[2] = frand(-1e18f, 1e18f);
        break;
    case 5:
        yawDir(v, irand(-32768, 32767));
        break;
    default:
        v[0] = frand(-3000.0f, 3000.0f);
        v[1] = frand(-300.0f, 300.0f);
        v[2] = frand(-3000.0f, 3000.0f);
        break;
    }
}

/* --- the game's calls this harness answers ------------------------------- */
static unsigned int faults;

int ico_diag_float_fault(const char *site, const void *caller)
{
    (void)caller;
    faults++;
    fprintf(out, "  fault %s\n", site);
    return 1;
}

void ico_diag_log(const char *fmt, ...)
{
    (void)fmt;
}

unsigned int ico_diag_float_faults(void)
{
    return faults;
}

void ico_diag_set_failure(const char *fmt, ...)
{
    (void)fmt;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void ExecuteSEPackage(struct GObj *gobj, int id)
{
    (void)gobj;
    fprintf(out, "  se %d\n", id);
}

float GetWeaponWeight(GObj *w)
{
    (void)w;
    return 0.0f;
}

/* the mails the actors send this frame (obj_manager.c queues them) */
static int mails[32];
static int nmails;

int iosOmSendMail(GObj *g, int type, void *arg)
{
    (void)arg;
    if (nmails < 32) {
        mails[nmails++] = type | (g == girlGObj ? 0x10000 : 0);
    }
    return 1;
}

/* motionOrientManager.c's request: the motion the actor asked for */
static int motionReq = -1;

char *SetMotionRequest(void *self, int mot, MotOriReq req)
{
    (void)req;
    if (self == (void *)girlGObj) {
        motionReq = mot;
    }
    return (char *)&GOBJ_SUB(self)->ctrl;
}

/* stubs.c (generated by tools/arm64_diff.sh) sends every other call here */
void ikdiff_stub(const char *name)
{
    fflush(out);
    fprintf(stderr, "followdiff: the harness has no %s\n", name);
    abort();
}

/* --- the two actors ----------------------------------------------------- */
#define NN 22

typedef struct {
    GObj g;
    Sub15C sub;
    Act act;
    ActWork work;
    EnemyBattleWork enemy;
    struct DObjNode node;
    SkelNode skel[NN];
    float mtx[NN][16] __attribute__((aligned(16)));
    float quat[NN][4] __attribute__((aligned(16)));
    MotIk ik[NN];
    int limit[NN];
    char focus[64];
    float fwd[4]; /* the facing the skeleton is placed by */
} Actor;

static Actor boy, girl;

/* the skeleton: kind (the focus id it answers to), parent, offset (x to
   the actor's right, y up, z forward); the arms hang down.  Ico holds
   Yorda by his right hand (hand 1), she him by her left (hand 0), at his
   right */
static const struct {
    int kind;
    int parent;
    float x, y, z;
} kSkel[NN] = {
    {0, -1, 0.0f, 0.0f, 0.0f},    /* 0 the hips */
    {1, 0, 0.0f, 10.0f, 0.0f},    /* 1 the spine */
    {18, 1, 0.0f, 12.0f, 0.0f},   /* 2 the chest (the girl's hand manager node) */
    {34, 2, 0.0f, 10.0f, 0.0f},   /* 3 the neck */
    {35, 3, 0.0f, 6.0f, 0.0f},    /* 4 the head */
    {3, 2, 9.0f, 8.0f, 0.0f},     /* 5 hand 1's upper arm (the right shoulder) */
    {4, 5, 0.0f, -14.0f, 0.0f},   /* 6 its forearm */
    {5, 6, 0.0f, -12.0f, 0.0f},   /* 7 its wrist */
    {6, 7, 0.0f, -4.0f, 0.0f},    /* 8 its hand */
    {16, 8, 0.0f, -2.0f, 0.0f},   /* 9 its finger tip */
    {19, 2, -9.0f, 8.0f, 0.0f},   /* 10 hand 0's upper arm (the left shoulder) */
    {20, 10, 0.0f, -14.0f, 0.0f}, /* 11 its forearm */
    {21, 11, 0.0f, -12.0f, 0.0f}, /* 12 its wrist */
    {22, 12, 0.0f, -4.0f, 0.0f},  /* 13 its hand */
    {32, 13, 0.0f, -2.0f, 0.0f},  /* 14 its finger tip */
    {45, 0, -5.0f, -3.0f, 0.0f},  /* 15 a thigh */
    {46, 15, 0.0f, -25.0f, 0.0f}, /* 16 its shin */
    {47, 16, 0.0f, -25.0f, 0.0f}, /* 17 its foot */
    {49, 0, 5.0f, -3.0f, 0.0f},   /* 18 the other thigh */
    {50, 18, 0.0f, -25.0f, 0.0f}, /* 19 its shin */
    {51, 19, 0.0f, -25.0f, 0.0f}, /* 20 its foot */
    {2, 1, 0.0f, 6.0f, 0.0f},     /* 21 the boy's hand manager node */
};

static void setupActor(Actor *a, int kind, float scale)
{
    int i;

    memset(a, 0, sizeof *a);
    a->g.self = &a->g;
    a->g.kind = kind;
    a->g.labelId = kind;
    a->g.dobj = &a->sub;
    a->g.act = &a->act;
    a->g.active = 1;
    a->act.work = &a->work;
    a->act.enemy = &a->enemy;
    a->enemy.speedRatio = 1.0f;
    a->sub.nodes = &a->node;
    a->sub.skel = a->skel;
    a->sub.nodeNum = NN;
    a->sub.skelNodeNum = NN;
    a->sub.nodeMtx = (ICO_WORD)a->mtx;
    a->sub.nodeQuat = (ICO_WORD)a->quat;
    a->sub.nodeRotElem = a->ik;
    a->sub.nodeLimit = a->limit;
    a->sub.focusNodes = a->focus;
    a->node.scale[0] = a->node.scale[1] = a->node.scale[2] = scale;
    memset(a->focus, -1, sizeof a->focus);
    for (i = 0; i < NN; i++) {
        SkelNode *n = &a->skel[i];
        int j;

        n->kind = kSkel[i].kind;
        n->parent = kSkel[i].parent;
        n->child = -1;
        n->sibling = -1;
        n->pos[0] = kSkel[i].x;
        n->pos[1] = kSkel[i].y;
        n->pos[2] = kSkel[i].z;
        n->pos[3] = 1.0f;
        n->quat[3] = 1.0f;
        a->focus[n->kind] = (char)i;
        for (j = i + 1; j < NN; j++) {
            if (kSkel[j].parent == i) {
                n->child = j;
                break;
            }
        }
        a->quat[i][3] = 1.0f;
        a->ik[i].q[3] = 1.0f;
        a->ik[i].step[3] = 1.0f;
        a->ik[i].offset[3] = 1.0f;
    }
    /* getBone reads the bone lengths along x: upper arm 14, forearm 12,
       hand 4, a reach of 30 */
    a->skel[6].pos[0] = a->skel[11].pos[0] = 14.0f;
    a->skel[7].pos[0] = a->skel[12].pos[0] = 12.0f;
    a->skel[9].pos[0] = a->skel[14].pos[0] = 4.0f;
    a->sub.root.handIK = 1;
    a->sub.root.handTurnIK = 1;
    a->sub.root.twistRate = 1.0f;
    a->sub.root.handRate = 0.3f;
    a->sub.root.quat[3] = 1.0f;
    a->sub.root.pos[3] = 1.0f;
    /* the field plane: y = 0 */
    a->sub.root.plane.f[1] = 1.0f;
    a->sub.ctrl.dir[2] = 1.0f;
    a->fwd[2] = 1.0f;
}

/* every node's world matrix from the root, the facing and the skeleton's
   offsets (no motion): what the hand code reads */
static void placeActor(Actor *a)
{
    float right[4];
    float local[NN][4];
    int i;

    right[0] = a->fwd[2];
    right[1] = 0.0f;
    right[2] = -a->fwd[0];
    for (i = 0; i < NN; i++) {
        int p = a->skel[i].parent;
        float *m = a->mtx[i];

        local[i][0] = kSkel[i].x;
        local[i][1] = kSkel[i].y;
        local[i][2] = kSkel[i].z;
        if (p >= 0) {
            local[i][0] += local[p][0];
            local[i][1] += local[p][1];
            local[i][2] += local[p][2];
        }
        memset(m, 0, 64);
        m[0] = right[0];
        m[2] = right[2];
        m[5] = 1.0f;
        m[8] = a->fwd[0];
        m[10] = a->fwd[2];
        m[12] = a->sub.root.pos[0] + right[0] * local[i][0] + a->fwd[0] * local[i][2];
        m[13] = a->sub.root.pos[1] + 60.0f + local[i][1];
        m[14] = a->sub.root.pos[2] + right[2] * local[i][0] + a->fwd[2] * local[i][2];
        m[15] = 1.0f;
    }
}

/* --- part 1: the helpers ------------------------------------------------- */
static void sweepGv(void)
{
    int i;

    fprintf(out, "# gv.c\n");
    for (i = 0; i < 40000; i++) {
        float a[4], b[4], c[4], d[4], m[16];
        int step = irand(0, 40);

        randVec(a);
        randVec(b);
        randVec(c);
        fprintf(out, "gv %d", i);
        pfv(a, 3);
        pfv(b, 3);
        pi(_RotyGV(a, b));
        pi(_AbsRotyGV(a, b));
        pi(_InterRotGV(d, a, b, step));
        pfv(d, 3);
        _OrientXZGV(d, a, b);
        pfv(d, 3);
        _OrientGV(d, a, b);
        pfv(d, 3);
        pf(_MoveGV(d, a, b, frand(0.0f, 50.0f)));
        pfv(d, 3);
        d[0] = a[0];
        d[1] = a[1];
        d[2] = a[2];
        d[3] = 0.0f;
        _ApplyRyGV(d, frand(-6.4f, 6.4f));
        pfv(d, 3);
        pf(_GetDirection(a));
        pi(_FrontGV(a, b, c, irand(0, 180)));
        pf(_DistGV(a, b));
        pf(_DistxzGV(a, b));
        pf(_DistSqGV(a, b));
        pf(_DistxzSqGV(a, b));
        d[0] = c[0];
        d[1] = c[1];
        d[2] = c[2];
        d[3] = 0.0f;
        GetMatrixDirectionToZ(m, d);
        pfv(m, 16);
        fprintf(out, "\n");
    }
}

static void sweepDirection(void)
{
    int i;

    fprintf(out, "# SetMotionDirection, SetMotionDirectionWithLimit, SetMotionDirectionSmooze\n");
    for (i = 0; i < 30000; i++) {
        float d[4];
        float q[4];
        int yaw = irand(-32768, 32767);
        float s = (i & 3) == 0 ? frand(-5.0f, 0.0f) : frand(0.0f, 60.0f);
        /* drawn one by one: two draws in one argument list run in an order
           the compiler picks (gcc right to left, clang left to right) */
        float lim0 = frand(0.0f, 30.0f);
        float lim1 = frand(30.0f, 180.0f);

        yawDir(girl.sub.ctrl.dir, yaw);
        q[0] = 0.0f;
        q[1] = GetTableSin((short)(yaw >> 1));
        q[2] = 0.0f;
        q[3] = GetTableCos((short)(yaw >> 1));
        CopyQuaternion(girl.sub.root.quat, q);
        randVec(d);
        if (i % 5 == 1) {
            /* exactly behind */
            d[0] = -girl.sub.ctrl.dir[0];
            d[1] = 0.0f;
            d[2] = -girl.sub.ctrl.dir[2];
        }
        fprintf(out, "dir %d %d", i, yaw);
        pfv(d, 3);
        SetMotionDirection(&girl.g, d);
        pfv(girl.sub.ctrl.dir, 4);
        yawDir(girl.sub.ctrl.dir, yaw);
        SetMotionDirectionWithLimit(&girl.g, d, lim0, lim1);
        pfv(girl.sub.ctrl.dir, 4);
        yawDir(girl.sub.ctrl.dir, yaw);
        pi(SetMotionDirectionSmooze(&girl.g, d, s));
        pfv(girl.sub.ctrl.dir, 4);
        pfv(test_CURRENTORIENT(&girl.g), 4);
        pfv(test_CURRENTROOT(&girl.g), 4);
        fprintf(out, "\n");
    }
}

static void sweepHandMgr(void)
{
    int i;

    fprintf(out, "# HandMgr_GetDistHand, HandMgr_Update, HandMgr_Judge, HandMgr_Speed\n");
    HandMgr_Init();
    for (i = 0; i < 20000; i++) {
        float dist;
        float height;
        int yb = irand(-32768, 32767);
        int yg = (i & 1) ? yb + irand(-4000, 4000) : irand(-32768, 32767);
        float r = (i % 7 == 0) ? frand(0.0f, 2.0f) : frand(0.0f, 220.0f);
        float side[4];

        boy.sub.root.pos[0] = frand(-2000.0f, 2000.0f);
        boy.sub.root.pos[1] = frand(-50.0f, 50.0f);
        boy.sub.root.pos[2] = frand(-2000.0f, 2000.0f);
        yawDir(boy.fwd, yb);
        yawDir(boy.sub.ctrl.dir, yb);
        yawDir(side, yb + 16384 + irand(-12000, 12000));
        girl.sub.root.pos[0] = boy.sub.root.pos[0] + side[0] * r;
        girl.sub.root.pos[1] = boy.sub.root.pos[1] + frand(-30.0f, 30.0f);
        girl.sub.root.pos[2] = boy.sub.root.pos[2] + side[2] * r;
        yawDir(girl.fwd, yg);
        yawDir(girl.sub.ctrl.dir, yg);
        placeActor(&boy);
        placeActor(&girl);
        boy.act.actMode = irand(0, 5);
        boy.act.curItem = (i % 11 == 3) ? &boy.g : 0;
        systemStatus[0] = (i % 13 == 5) ? 1 : 0;
        girl.enemy.speedRatioPri = 0;
        girl.enemy.speedRatio = 1.0f;
        HandMgr_GetDistHand(&dist, &height);
        HandMgr_Update();
        HandMgr_Judge();
        HandMgr_Speed(&girl.g, frand(-1.0f, 4.0f));
        fprintf(out, "hm %d", i);
        pf(dist);
        pf(height);
        pf(HANDMGR->moveDist);
        pfv(HANDMGR->toBoy, 3);
        pfv(HANDMGR->orient, 3);
        pi(HANDMGR->still);
        pi(HANDMGR->turned);
        pi(HANDMGR->far100);
        pi(HANDMGR->far125);
        pi(HANDMGR->far135);
        pi(HANDMGR->near90);
        pi(HANDMGR->heightGap);
        pf(girl.enemy.speedRatio);
        pi((int)girl.enemy.speedRatioPri);
        fprintf(out, "\n");
    }
    systemStatus[0] = 0;
    boy.act.curItem = 0;
}

/* --- part 2: the walks ----------------------------------------------------- */
static IcoFiber *girlFiber;

/* act.c's: the actor waits frames frames; here a frame is one resume */
void _ACTWait(int frames)
{
    int i;

    for (i = 0; i < (frames > 0 ? frames : 1); i++) {
        ico_fiber_yield();
    }
}

static void girlMain(void *arg)
{
    (void)arg;
    actGirlHand(&girl.g);
}

/* the boy's script: actMode by frame (1 stand, 2 walk, 3 run), the turn a
   frame and the speed a frame for each mode */
typedef struct {
    const char *name;
    int frames;
    int turnEvery;
    int turn;
    int (*mode)(int f);
} Walk;

static int modeWalk(int f)
{
    return f < 20 ? 1 : 2;
}

static int modeRun(int f)
{
    return f < 20 ? 1 : (f < 60 ? 2 : 3);
}

static int modeStopGo(int f)
{
    return ((f / 45) & 1) ? 1 : ((f / 90) & 1 ? 3 : 2);
}

static int modeStand(int f)
{
    (void)f;
    return 1;
}

static const Walk walks[] = {
    {"walk-straight", 240, 0, 0, modeWalk}, {"run-straight", 240, 0, 0, modeRun},
    {"walk-left", 240, 1, 120, modeWalk},   {"run-right", 240, 1, -160, modeRun},
    {"stop-go", 360, 7, 900, modeStopGo},   {"stand", 120, 0, 0, modeStand},
    {"zigzag", 300, 30, 5000, modeRun},
};

/* the stand-in for the motion system: a frame's move for each motion the
   girl asked for and each boy mode */
static float girlSpeed(int req)
{
    switch (req) {
    case 0xE:
        return 2.2f;
    case 0x10:
        return 5.0f;
    default:
        return 0.0f;
    }
}

static float boySpeed(int mode)
{
    return mode == 3 ? 5.0f : (mode == 2 ? 2.2f : 0.0f);
}

static void runWalk(const Walk *w, int start)
{
    static const float offsets[][2] = {{60.0f, 0.0f}, {40.0f, -20.0f},  {90.0f, 10.0f},
                                       {30.0f, 0.0f}, {120.0f, -40.0f}, {5.0f, 0.0f}};
    int yaw = irand(-32768, 32767);
    float side[4];
    float fwd[4];
    int f;
    float maxUp = -1.0f;
    float minLen = 1e30f;

    setupActor(&boy, 1, 1.0f);
    setupActor(&girl, 2, 1.0f);
    boyGObj = &boy.g;
    girlGObj = &girl.g;
    boy.act.actMode = 1;
    girl.act.actMode = 0x45;
    yawDir(fwd, yaw);
    yawDir(side, yaw + 16384);
    boy.sub.root.pos[0] = frand(-1000.0f, 1000.0f);
    boy.sub.root.pos[2] = frand(-1000.0f, 1000.0f);
    CopyVector(boy.fwd, fwd);
    CopyVector(boy.sub.ctrl.dir, fwd);
    girl.sub.root.pos[0] =
        boy.sub.root.pos[0] + side[0] * offsets[start][0] + fwd[0] * offsets[start][1];
    girl.sub.root.pos[2] =
        boy.sub.root.pos[2] + side[2] * offsets[start][0] + fwd[2] * offsets[start][1];
    CopyVector(girl.fwd, fwd);
    CopyVector(girl.sub.ctrl.dir, fwd);
    placeActor(&boy);
    placeActor(&girl);
    motionReq = -1;
    girlFiber = ico_fiber_create(girlMain, 0, 256 * 1024);
    if (girlFiber == 0) {
        fprintf(stderr, "followdiff: no fiber\n");
        exit(2);
    }
    fprintf(out, "# walk %s, start %d\n", w->name, start);
    for (f = 0; f < w->frames; f++) {
        int mode = w->mode(f);
        float d[4];
        float sh[4];
        float up;
        float len;
        float g;
        unsigned int f0 = faults;
        int k;

        /* Ico */
        if (w->turnEvery != 0 && f % w->turnEvery == 0) {
            yaw += w->turn;
            yawDir(boy.fwd, yaw);
            CopyVector(boy.sub.ctrl.dir, boy.fwd);
        }
        boy.act.actMode = mode;
        boy.sub.root.pos[0] += boy.fwd[0] * boySpeed(mode);
        boy.sub.root.pos[2] += boy.fwd[2] * boySpeed(mode);
        placeActor(&boy);
        /* Yorda decides */
        nmails = 0;
        girl.enemy.speedRatioPri = 0;
        girl.enemy.speedRatio = 1.0f;
        if (ico_fiber_resume(girlFiber) != 0) {
            fprintf(stderr, "followdiff: the girl's fiber failed\n");
            exit(2);
        }
        /* and walks by it */
        g = girlSpeed(motionReq) * girl.enemy.speedRatio;
        girl.sub.root.pos[0] += girl.sub.ctrl.dir[0] * g;
        girl.sub.root.pos[2] += girl.sub.ctrl.dir[2] * g;
        if (girl.sub.ctrl.dir[0] != 0.0f || girl.sub.ctrl.dir[2] != 0.0f) {
            girl.fwd[0] = girl.sub.ctrl.dir[0];
            girl.fwd[2] = girl.sub.ctrl.dir[2];
        }
        placeActor(&girl);
        HandManager(&boy.g);
        HandManager(&girl.g);
        /* the meeting point against Ico's right shoulder */
        CopyVector(sh, boy.mtx[5] + 12);
        d[0] = boy.sub.root.hand1.ikDir[0] - sh[0];
        d[1] = boy.sub.root.hand1.ikDir[1] - sh[1];
        d[2] = boy.sub.root.hand1.ikDir[2] - sh[2];
        len = fsqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        up = len > 0.0f ? d[1] / len : 0.0f;
        if (up > maxUp) {
            maxUp = up;
        }
        {
            float e0 = boy.mtx[5][12] - girl.mtx[10][12];
            float e2 = boy.mtx[5][14] - girl.mtx[10][14];
            float sl = fsqrt_(e0 * e0 + e2 * e2);

            if (sl < minLen) {
                minLen = sl;
            }
        }
        fprintf(out, "walk %d %d %d req %d", start, f, mode, motionReq);
        pf(girl.enemy.speedRatio);
        pfv(girl.sub.root.pos, 3);
        pfv(girl.sub.ctrl.dir, 3);
        pf(HANDMGR->handDist);
        pi(HANDMGR->still | HANDMGR->turned << 1 | HANDMGR->far100 << 2 | HANDMGR->far125 << 3 |
           HANDMGR->far135 << 4 | HANDMGR->near90 << 5 | HANDMGR->heightGap << 6);
        pi(boy.sub.root.hand1.mode);
        pi(girl.sub.root.hand0.mode);
        pfv(boy.sub.root.hand1.ikDir, 3);
        pfv(girl.sub.root.hand0.ikDir, 3);
        pf(up);
        fprintf(out, " mails");
        for (k = 0; k < nmails; k++) {
            fprintf(out, " %x", mails[k]);
        }
        fprintf(out, "%s\n", faults != f0 ? " fault" : "");
    }
    fprintf(out, "walk-end %s %d: highest meeting point", w->name, start);
    pf(maxUp);
    fprintf(out, " (%.3f), nearest shoulders", (double)maxUp);
    pf(minLen);
    fprintf(out, " (%.1f)\n", (double)minLen);
    ico_fiber_destroy(girlFiber);
    girlFiber = 0;
}

static uint8_t *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (f == 0) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf == 0 || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    unsigned int i;
    int s;

    out = stdout;
    if (argc > 1 && argv[1][0] != 0) {
        size_t n;
        uint8_t *elf = readFile(argv[1], &n);
        char err[256];

        if (elf == 0 || ico_tables_load_elf(elf, n, err, sizeof err) != 0) {
            fprintf(stderr, "followdiff: %s: no tables (%s)\n", argv[1], elf ? err : "unreadable");
            return 2;
        }
        free(elf);
        fprintf(out, "# the tables from the ELF\n");
    } else {
        fprintf(out, "# zero tables\n");
    }
    /* the game runs in the EE's rounding (port/platform/fpenv.c) */
    ico_fpenv_sim_enter();
    InitMatrixDrive();
    /* NTSC timing at the frame step 2: 30 game frames a second */
    systemStatus[0] = 0;
    systemStatus[1] = 2;
    setupActor(&boy, 1, 1.0f);
    setupActor(&girl, 2, 1.0f);
    boyGObj = &boy.g;
    girlGObj = &girl.g;
    sweepGv();
    sweepDirection();
    sweepHandMgr();
    for (i = 0; i < sizeof walks / sizeof walks[0]; i++) {
        for (s = 0; s < 6; s++) {
            runWalk(&walks[i], s);
        }
    }
    fprintf(out, "# faults %u\n", faults);
    return 0;
}
